/* dsound.cpp -- DirectSound and winmm.
 *
 * A DirectSound secondary buffer is a circular run of PCM the game writes
 * into through Lock/Unlock and plays from with a cursor that advances in
 * real time; the game's stream refills the part behind that cursor from a
 * timer, and its effects restart by moving the cursor to 0. That is exactly
 * what is emulated: every buffer keeps its bytes, and one mixer, pulled by
 * the host device from its own thread, walks every playing buffer's cursor
 * at the buffer's own rate, converting 8/16-bit mono/stereo to float stereo
 * on the way and applying the buffer's volume, pan and frequency. Nothing
 * in the game reads the primary; it exists to be QueryInterface'd for the
 * 3D listener, whose settings are taken and ignored -- the game computes
 * its 3D volumes itself (Update3DAudioVolumes) and only ever SetVolumes.
 *
 * winmm's mmio is the buffered RIFF reader the wave loader was written
 * against: the game copies straight out of MMIOINFO's buffer and asks for
 * the next window with mmioAdvance, so the buffer, the two cursors into it
 * and the disk offset are real and kept exactly as the API describes them.
 * The multimedia timer is the host's.
 *
 *   AM2_NOSOUND=1   do not open a device; DirectSoundCreate answers
 *                   DSERR_NODRIVER, the path the original takes on a
 *                   machine without one.
 */
#include "platform.h"
#include <dsound.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

AM2_DEFINE_GUID_VALUE(IID_IDirectSound,           0x279AFA83, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSoundBuffer,     0x279AFA85, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSound3DListener, 0x279AFA84, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
AM2_DEFINE_GUID_VALUE(IID_IDirectSound3DBuffer,   0x279AFA86, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);

/* The rate the mixer runs at; the host converts to the device's. */
#define AM2_MIX_RATE 44100
/* How far DirectSound's write cursor leads the play cursor, in ms. */
#define AM2_WRITE_LEAD_MS 15

/* ---- objects ----------------------------------------------------------- */

typedef struct AM2_DSBuffer {
    const IDirectSoundBufferVtbl *lpVtbl;
    int32_t      refs;
    DWORD        flags;          /* the DSBCAPS_ it was created with */
    int32_t      primary;
    uint8_t     *data;
    uint32_t     size;
    WAVEFORMATEX fmt;
    /* Everything below is the mixer's and is read and written under the
     * mixer lock. `pos` is the play cursor in FRAMES, fractional so a
     * buffer resampled to the mix rate keeps its place exactly. */
    int32_t      playing;
    int32_t      looping;
    double       pos;
    LONG         volume;         /* hundredths of a dB, -10000..0 */
    LONG         pan;            /* hundredths of a dB, -10000..10000 */
    DWORD        freq;           /* 0: the format's own */
    struct AM2_DSBuffer *next;
} AM2_DSBuffer;

typedef struct AM2_DSound {
    const IDirectSoundVtbl *lpVtbl;
    int32_t refs;
} AM2_DSound;

typedef struct AM2_DSListener {
    const IDirectSound3DListenerVtbl *lpVtbl;
    int32_t refs;
} AM2_DSListener;

static pthread_mutex_t am2_mix_lock = PTHREAD_MUTEX_INITIALIZER;
static AM2_DSBuffer   *am2_buffers;          /* every live secondary */
static int32_t         am2_device_open;
static AM2_DSListener  am2_listener;

static void lock_mix(void)   { pthread_mutex_lock(&am2_mix_lock); }
static void unlock_mix(void) { pthread_mutex_unlock(&am2_mix_lock); }

/* ---- the mixer ----------------------------------------------------------- */

static float db_gain(LONG hundredths)
{
    if (hundredths <= DSBVOLUME_MIN)
        return 0.0f;
    if (hundredths >= 0)
        return 1.0f;
    return powf(10.0f, (float)hundredths / 2000.0f);
}

/* One frame of a buffer as left/right in -1..1. */
static inline void sample_at(const AM2_DSBuffer *b, uint32_t frame, float *l, float *r)
{
    const uint8_t *p = b->data + (size_t)frame * b->fmt.nBlockAlign;

    if (b->fmt.wBitsPerSample == 8) {
        *l = ((float)p[0] - 128.0f) / 128.0f;
        *r = (b->fmt.nChannels >= 2) ? ((float)p[1] - 128.0f) / 128.0f : *l;
    } else {
        int16_t s0, s1;

        memcpy(&s0, p, 2);
        *l = (float)s0 / 32768.0f;
        if (b->fmt.nChannels >= 2) {
            memcpy(&s1, p + 2, 2);
            *r = (float)s1 / 32768.0f;
        } else {
            *r = *l;
        }
    }
}

static void am2_mix(void *ud, float *out, int32_t frames)
{
    AM2_DSBuffer *b;
    int32_t       i;

    (void)ud;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));

    lock_mix();
    for (b = am2_buffers; b; b = b->next) {
        uint32_t total;
        double   step, pos;
        float    gain, gl, gr;

        if (!b->playing || !b->data || b->fmt.nBlockAlign == 0)
            continue;
        total = b->size / b->fmt.nBlockAlign;
        if (total == 0)
            continue;

        step = (double)(b->freq ? b->freq : b->fmt.nSamplesPerSec)
               / (double)AM2_MIX_RATE;
        gain = db_gain(b->volume);
        gl   = gain * (b->pan > 0 ? db_gain(-b->pan) : 1.0f);
        gr   = gain * (b->pan < 0 ? db_gain(b->pan) : 1.0f);
        pos  = b->pos;

        for (i = 0; i < frames; i++) {
            uint32_t f0 = (uint32_t)pos;
            uint32_t f1;
            float    l0, r0, l1, r1, t;

            if (f0 >= total) {
                if (!b->looping) {
                    /* Played out: DirectSound stops the buffer and puts
                     * the play cursor back at the start. */
                    b->playing = 0;
                    pos = 0.0;
                    break;
                }
                pos -= (double)total * floor(pos / (double)total);
                f0 = (uint32_t)pos;
            }
            f1 = f0 + 1;
            if (f1 >= total)
                f1 = b->looping ? 0 : f0;
            t = (float)(pos - (double)f0);
            sample_at(b, f0, &l0, &r0);
            sample_at(b, f1, &l1, &r1);
            out[i * 2]     += (l0 + (l1 - l0) * t) * gl;
            out[i * 2 + 1] += (r0 + (r1 - r0) * t) * gr;
            pos += step;
        }
        b->pos = pos;
    }
    unlock_mix();

    for (i = 0; i < frames * 2; i++) {
        if (out[i] > 1.0f)
            out[i] = 1.0f;
        else if (out[i] < -1.0f)
            out[i] = -1.0f;
    }
}

/* ---- IDirectSoundBuffer --------------------------------------------------- */

#define BUF(p) ((AM2_DSBuffer *)(p))

static HRESULT STDMETHODCALLTYPE buf_QueryInterface(IDirectSoundBuffer *p, REFIID iid, LPVOID *out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    if (IsEqualGUID(iid, IID_IDirectSound3DListener) && BUF(p)->primary) {
        am2_listener.refs++;
        *out = &am2_listener;
        return DS_OK;
    }
    if (IsEqualGUID(iid, IID_IDirectSoundBuffer)) {
        BUF(p)->refs++;
        *out = p;
        return DS_OK;
    }
    *out = NULL;
    return DSERR_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE buf_AddRef(IDirectSoundBuffer *p)
{
    return (ULONG)++BUF(p)->refs;
}

static ULONG STDMETHODCALLTYPE buf_Release(IDirectSoundBuffer *p)
{
    AM2_DSBuffer *b = BUF(p);

    if (--b->refs > 0)
        return (ULONG)b->refs;

    lock_mix();
    {
        AM2_DSBuffer **pp = &am2_buffers;

        while (*pp && *pp != b)
            pp = &(*pp)->next;
        if (*pp)
            *pp = b->next;
    }
    unlock_mix();
    free(b->data);
    free(b);
    return 0;
}

static HRESULT STDMETHODCALLTYPE buf_GetCaps(IDirectSoundBuffer *p, LPDSBCAPS caps)
{
    if (!caps || caps->dwSize < sizeof(DSBCAPS))
        return DSERR_INVALIDPARAM;
    caps->dwFlags              = BUF(p)->flags | DSBCAPS_LOCSOFTWARE;
    caps->dwBufferBytes        = BUF(p)->size;
    caps->dwUnlockTransferRate = 0;
    caps->dwPlayCpuOverhead    = 0;
    return DS_OK;
}

static uint32_t play_cursor_bytes(const AM2_DSBuffer *b)
{
    uint32_t frames = (uint32_t)b->pos;
    uint32_t bytes  = frames * b->fmt.nBlockAlign;

    return b->size ? bytes % b->size : 0;
}

static HRESULT STDMETHODCALLTYPE buf_GetCurrentPosition(IDirectSoundBuffer *p, LPDWORD play, LPDWORD write)
{
    AM2_DSBuffer *b = BUF(p);
    uint32_t      pc, lead;

    lock_mix();
    pc = play_cursor_bytes(b);
    unlock_mix();

    if (play)
        *play = pc;
    if (write) {
        /* The write cursor leads the play cursor by what the device has
         * already been handed, and is only meaningful while playing. */
        lead = b->playing
             ? (uint32_t)((uint64_t)b->fmt.nAvgBytesPerSec * AM2_WRITE_LEAD_MS / 1000)
             : 0;
        lead -= lead % (b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1);
        *write = b->size ? (pc + lead) % b->size : 0;
    }
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetFormat(IDirectSoundBuffer *p, LPWAVEFORMATEX fmt, DWORD bytes, LPDWORD written)
{
    if (written)
        *written = sizeof(WAVEFORMATEX);
    if (fmt) {
        if (bytes < sizeof(WAVEFORMATEX))
            return DSERR_INVALIDPARAM;
        *fmt = BUF(p)->fmt;
    }
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetVolume(IDirectSoundBuffer *p, LPLONG v)
{
    if (!v) return DSERR_INVALIDPARAM;
    *v = BUF(p)->volume;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetPan(IDirectSoundBuffer *p, LPLONG v)
{
    if (!v) return DSERR_INVALIDPARAM;
    *v = BUF(p)->pan;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetFrequency(IDirectSoundBuffer *p, LPDWORD v)
{
    if (!v) return DSERR_INVALIDPARAM;
    *v = BUF(p)->freq ? BUF(p)->freq : BUF(p)->fmt.nSamplesPerSec;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetStatus(IDirectSoundBuffer *p, LPDWORD status)
{
    DWORD s = 0;

    if (!status)
        return DSERR_INVALIDPARAM;
    lock_mix();
    if (BUF(p)->playing) {
        s |= DSBSTATUS_PLAYING;
        if (BUF(p)->looping)
            s |= DSBSTATUS_LOOPING;
    }
    unlock_mix();
    *status = s;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Initialize(IDirectSoundBuffer *p, LPDIRECTSOUND ds, LPCDSBUFFERDESC desc)
{
    (void)p; (void)ds; (void)desc;
    return DSERR_ALLOCATED;      /* CreateSoundBuffer already did */
}

static HRESULT STDMETHODCALLTYPE buf_Lock(IDirectSoundBuffer *p, DWORD offset, DWORD bytes,
                                          LPVOID *p1, LPDWORD n1, LPVOID *p2, LPDWORD n2, DWORD flags)
{
    AM2_DSBuffer *b = BUF(p);

    if (!p1 || !n1)
        return DSERR_INVALIDPARAM;
    if (!b->data)
        return DSERR_INVALIDCALL;
    if (flags & DSBLOCK_FROMWRITECURSOR) {
        DWORD play, write;

        buf_GetCurrentPosition(p, &play, &write);
        offset = write;
    }
    if (flags & DSBLOCK_ENTIREBUFFER)
        bytes = b->size;
    if (offset >= b->size || bytes > b->size)
        return DSERR_INVALIDPARAM;

    *p1 = b->data + offset;
    *n1 = (bytes <= b->size - offset) ? bytes : b->size - offset;
    if (p2)
        *p2 = (*n1 < bytes) ? b->data : NULL;
    if (n2)
        *n2 = (*n1 < bytes) ? bytes - *n1 : 0;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Play(IDirectSoundBuffer *p, DWORD r1, DWORD r2, DWORD flags)
{
    (void)r1; (void)r2;
    if (!BUF(p)->data)
        return DS_OK;            /* the primary: nothing to hear from it */
    lock_mix();
    BUF(p)->playing = 1;
    BUF(p)->looping = (flags & DSBPLAY_LOOPING) ? 1 : 0;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetCurrentPosition(IDirectSoundBuffer *p, DWORD pos)
{
    AM2_DSBuffer *b = BUF(p);

    if (pos >= b->size && b->size)
        return DSERR_INVALIDPARAM;
    lock_mix();
    b->pos = b->fmt.nBlockAlign ? (double)(pos / b->fmt.nBlockAlign) : 0.0;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetFormat(IDirectSoundBuffer *p, LPCWAVEFORMATEX fmt)
{
    if (!fmt)
        return DSERR_INVALIDPARAM;
    if (!BUF(p)->primary)
        return DSERR_INVALIDCALL;
    BUF(p)->fmt = *fmt;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetVolume(IDirectSoundBuffer *p, LONG v)
{
    if (v < DSBVOLUME_MIN || v > DSBVOLUME_MAX)
        return DSERR_INVALIDPARAM;
    lock_mix();
    BUF(p)->volume = v;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetPan(IDirectSoundBuffer *p, LONG v)
{
    if (v < DSBPAN_LEFT || v > DSBPAN_RIGHT)
        return DSERR_INVALIDPARAM;
    lock_mix();
    BUF(p)->pan = v;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetFrequency(IDirectSoundBuffer *p, DWORD hz)
{
    if (hz && (hz < 100 || hz > 100000))
        return DSERR_INVALIDPARAM;
    lock_mix();
    BUF(p)->freq = hz;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Stop(IDirectSoundBuffer *p)
{
    lock_mix();
    BUF(p)->playing = 0;
    unlock_mix();
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Unlock(IDirectSoundBuffer *p, LPVOID p1, DWORD n1, LPVOID p2, DWORD n2)
{
    /* The bytes went straight into the buffer; the mixer reads the same
     * memory, so there is nothing to transfer. */
    (void)p; (void)p1; (void)n1; (void)p2; (void)n2;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Restore(IDirectSoundBuffer *p)
{
    (void)p;                     /* nothing is ever lost */
    return DS_OK;
}

static const IDirectSoundBufferVtbl am2_buffer_vtbl = {
    buf_QueryInterface, buf_AddRef, buf_Release,
    buf_GetCaps, buf_GetCurrentPosition, buf_GetFormat, buf_GetVolume, buf_GetPan,
    buf_GetFrequency, buf_GetStatus, buf_Initialize, buf_Lock, buf_Play,
    buf_SetCurrentPosition, buf_SetFormat, buf_SetVolume, buf_SetPan,
    buf_SetFrequency, buf_Stop, buf_Unlock, buf_Restore,
};

/* ---- IDirectSound3DListener ------------------------------------------------- */

#define LST(p) ((AM2_DSListener *)(p))

static HRESULT STDMETHODCALLTYPE lst_QueryInterface(IDirectSound3DListener *p, REFIID iid, LPVOID *out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    if (IsEqualGUID(iid, IID_IDirectSound3DListener)) {
        LST(p)->refs++;
        *out = p;
        return DS_OK;
    }
    *out = NULL;
    return DSERR_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE lst_AddRef(IDirectSound3DListener *p)  { return (ULONG)++LST(p)->refs; }
static ULONG STDMETHODCALLTYPE lst_Release(IDirectSound3DListener *p)
{
    if (LST(p)->refs > 0)
        LST(p)->refs--;
    return (ULONG)LST(p)->refs;  /* static: never freed */
}

/* The listener's parameters are accepted and not applied: the game does
 * its own distance attenuation and only ever sets volumes. */
static HRESULT STDMETHODCALLTYPE lst_GetAllParameters(IDirectSound3DListener *p, LPDS3DLISTENER v) { (void)p; (void)v; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_GetValue(IDirectSound3DListener *p, D3DVALUE *v) { (void)p; if (v) *v = 1.0f; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_GetOrientation(IDirectSound3DListener *p, D3DVECTOR *a, D3DVECTOR *b) { (void)p; (void)a; (void)b; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_GetVector(IDirectSound3DListener *p, D3DVECTOR *v) { (void)p; (void)v; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_SetAllParameters(IDirectSound3DListener *p, const DS3DLISTENER *v, DWORD apply) { (void)p; (void)v; (void)apply; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_SetValue(IDirectSound3DListener *p, D3DVALUE v, DWORD apply) { (void)p; (void)v; (void)apply; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_SetOrientation(IDirectSound3DListener *p, D3DVALUE a, D3DVALUE b, D3DVALUE c, D3DVALUE d, D3DVALUE e, D3DVALUE f, DWORD apply) { (void)p; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)apply; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_SetVector(IDirectSound3DListener *p, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) { (void)p; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
static HRESULT STDMETHODCALLTYPE lst_Commit(IDirectSound3DListener *p) { (void)p; return DS_OK; }

static const IDirectSound3DListenerVtbl am2_listener_vtbl = {
    lst_QueryInterface, lst_AddRef, lst_Release,
    lst_GetAllParameters, lst_GetValue, lst_GetValue, lst_GetOrientation, lst_GetVector,
    lst_GetValue, lst_GetVector, lst_SetAllParameters, lst_SetValue, lst_SetValue,
    lst_SetOrientation, lst_SetVector, lst_SetValue, lst_SetVector, lst_Commit,
};


/* ---- IDirectSound --------------------------------------------------------- */

#define DS(p) ((AM2_DSound *)(p))

static HRESULT STDMETHODCALLTYPE ds_QueryInterface(IDirectSound *p, REFIID iid, LPVOID *out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    if (IsEqualGUID(iid, IID_IDirectSound)) {
        DS(p)->refs++;
        *out = p;
        return DS_OK;
    }
    *out = NULL;
    return DSERR_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ds_AddRef(IDirectSound *p) { return (ULONG)++DS(p)->refs; }
static ULONG STDMETHODCALLTYPE ds_Release(IDirectSound *p)
{
    if (--DS(p)->refs > 0)
        return (ULONG)DS(p)->refs;
    free(p);
    return 0;
}

static HRESULT STDMETHODCALLTYPE ds_CreateSoundBuffer(IDirectSound *p, LPCDSBUFFERDESC desc,
                                                      LPDIRECTSOUNDBUFFER *out, IUnknown *outer)
{
    AM2_DSBuffer *b;

    (void)p; (void)outer;
    if (!desc || !out || desc->dwSize < sizeof(DSBUFFERDESC))
        return DSERR_INVALIDPARAM;
    *out = NULL;

    b = (AM2_DSBuffer *)calloc(1, sizeof *b);
    if (!b)
        return DSERR_OUTOFMEMORY;
    b->lpVtbl = &am2_buffer_vtbl;
    b->refs   = 1;
    b->flags  = desc->dwFlags;
    b->volume = DSBVOLUME_MAX;
    b->pan    = DSBPAN_CENTER;

    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
        /* The primary has no bytes of its own; the mix is the primary. */
        b->primary = 1;
        b->fmt.wFormatTag      = WAVE_FORMAT_PCM;
        b->fmt.nChannels       = 2;
        b->fmt.nSamplesPerSec  = AM2_MIX_RATE;
        b->fmt.wBitsPerSample  = 16;
        b->fmt.nBlockAlign     = 4;
        b->fmt.nAvgBytesPerSec = AM2_MIX_RATE * 4;
        *out = (LPDIRECTSOUNDBUFFER)b;
        return DS_OK;
    }

    if (!desc->lpwfxFormat || desc->dwBufferBytes == 0
        || desc->lpwfxFormat->wFormatTag != WAVE_FORMAT_PCM
        || desc->lpwfxFormat->nBlockAlign == 0
        || (desc->lpwfxFormat->wBitsPerSample != 8 && desc->lpwfxFormat->wBitsPerSample != 16)
        || desc->lpwfxFormat->nChannels < 1 || desc->lpwfxFormat->nChannels > 2) {
        free(b);
        return DSERR_INVALIDPARAM;
    }
    b->fmt  = *desc->lpwfxFormat;
    b->size = desc->dwBufferBytes - desc->dwBufferBytes % b->fmt.nBlockAlign;
    b->data = (uint8_t *)malloc(b->size ? b->size : 1);
    if (!b->data) {
        free(b);
        return DSERR_OUTOFMEMORY;
    }
    /* Silence, in the format's own idea of it. */
    memset(b->data, b->fmt.wBitsPerSample == 8 ? 0x80 : 0, b->size);

    lock_mix();
    b->next = am2_buffers;
    am2_buffers = b;
    unlock_mix();

    am2_plat_debug("dsound: buffer %u bytes, %u Hz %u-bit x%u, flags %08x",
                   (unsigned)b->size, (unsigned)b->fmt.nSamplesPerSec,
                   (unsigned)b->fmt.wBitsPerSample, (unsigned)b->fmt.nChannels,
                   (unsigned)b->flags);
    *out = (LPDIRECTSOUNDBUFFER)b;
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_GetCaps(IDirectSound *p, LPDSCAPS caps)
{
    (void)p;
    if (!caps || caps->dwSize < sizeof(DSCAPS))
        return DSERR_INVALIDPARAM;
    memset(&caps->dwFlags, 0, sizeof(DSCAPS) - sizeof(DWORD));
    caps->dwFlags = 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008; /* primary mono/stereo/8/16 */
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_DuplicateSoundBuffer(IDirectSound *p, LPDIRECTSOUNDBUFFER a, LPDIRECTSOUNDBUFFER *b)
{
    (void)p; (void)a;
    if (b) *b = NULL;
    return DSERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE ds_SetCooperativeLevel(IDirectSound *p, HWND hwnd, DWORD level)
{
    (void)p; (void)hwnd; (void)level;   /* accepted, like every other exclusivity */
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE ds_Compact(IDirectSound *p) { (void)p; return DS_OK; }
static HRESULT STDMETHODCALLTYPE ds_GetSpeakerConfig(IDirectSound *p, LPDWORD v) { (void)p; if (v) *v = 0; return DS_OK; }
static HRESULT STDMETHODCALLTYPE ds_SetSpeakerConfig(IDirectSound *p, DWORD v) { (void)p; (void)v; return DS_OK; }
static HRESULT STDMETHODCALLTYPE ds_Initialize(IDirectSound *p, LPCGUID g) { (void)p; (void)g; return DSERR_ALLOCATED; }

static const IDirectSoundVtbl am2_dsound_vtbl = {
    ds_QueryInterface, ds_AddRef, ds_Release,
    ds_CreateSoundBuffer, ds_GetCaps, ds_DuplicateSoundBuffer, ds_SetCooperativeLevel,
    ds_Compact, ds_GetSpeakerConfig, ds_SetSpeakerConfig, ds_Initialize,
};

HRESULT WINAPI DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND *out, IUnknown *outer)
{
    AM2_DSound *ds;
    const char *off = getenv("AM2_NOSOUND");

    (void)guid; (void)outer;
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = NULL;

    if (off && *off == '1') {
        am2_plat_log("dsound: AM2_NOSOUND set; the game runs silent");
        return DSERR_NODRIVER;
    }
    if (!am2_device_open) {
        if (!am2_host_audio_open(AM2_MIX_RATE, am2_mix, NULL))
            return DSERR_NODRIVER;
        am2_device_open = 1;
        am2_listener.lpVtbl = &am2_listener_vtbl;
    }

    ds = (AM2_DSound *)calloc(1, sizeof *ds);
    if (!ds)
        return DSERR_OUTOFMEMORY;
    ds->lpVtbl = &am2_dsound_vtbl;
    ds->refs   = 1;
    *out = (LPDIRECTSOUND)ds;
    return DS_OK;
}

/* ---- winmm: multimedia file I/O ------------------------------------------------ */

/* A buffered RIFF reader. The window into the file is MMIOINFO's own
 * pchBuffer..pchEndRead, `lBufOffset` is where that window starts in the
 * file and `lDiskOffset` is where the next read from disk will begin;
 * pchNext is the read position inside the window. The game copies out of
 * the window and calls mmioAdvance when it runs dry, so all four are
 * maintained exactly as the API documents them. */
typedef struct AM2_MMIO {
    FILE    *fp;
    MMIOINFO info;
    LONG     length;
} AM2_MMIO;

#define MM(h) ((AM2_MMIO *)(h))

static LONG mm_position(const AM2_MMIO *m)
{
    return m->info.lBufOffset + (LONG)(m->info.pchNext - m->info.pchBuffer);
}

/* Refill the window from lDiskOffset. Returns how much it holds. */
static LONG mm_fill(AM2_MMIO *m)
{
    size_t got;

    m->info.lBufOffset = m->info.lDiskOffset;
    if (fseek(m->fp, m->info.lDiskOffset, SEEK_SET) != 0)
        got = 0;
    else
        got = fread(m->info.pchBuffer, 1, (size_t)m->info.cchBuffer, m->fp);
    m->info.pchNext    = m->info.pchBuffer;
    m->info.pchEndRead = m->info.pchBuffer + got;
    m->info.lDiskOffset += (LONG)got;
    return (LONG)got;
}

HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags)
{
    AM2_MMIO *m;
    FILE     *fp;

    if (info)
        info->wErrorRet = 0;
    if (!name || (flags & (MMIO_WRITE | MMIO_READWRITE))) {
        if (info)
            info->wErrorRet = MMIOERR_CANNOTOPEN;
        return NULL;
    }
    fp = am2_fopen(name, "rb");
    if (!fp) {
        if (info)
            info->wErrorRet = MMIOERR_FILENOTFOUND;
        return NULL;
    }
    m = (AM2_MMIO *)calloc(1, sizeof *m);
    if (!m) {
        fclose(fp);
        if (info)
            info->wErrorRet = MMIOERR_OUTOFMEMORY;
        return NULL;
    }
    m->fp = fp;
    fseek(fp, 0, SEEK_END);
    m->length = (LONG)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    m->info.dwFlags   = flags;
    m->info.cchBuffer = (info && info->cchBuffer > 0) ? info->cchBuffer : MMIO_DEFAULTBUFFER;
    m->info.pchBuffer = (HPSTR)malloc((size_t)m->info.cchBuffer);
    if (!m->info.pchBuffer) {
        fclose(fp);
        free(m);
        if (info)
            info->wErrorRet = MMIOERR_OUTOFMEMORY;
        return NULL;
    }
    m->info.pchNext = m->info.pchEndRead = m->info.pchBuffer;
    m->info.pchEndWrite = m->info.pchBuffer + m->info.cchBuffer;
    m->info.hmmio = (HMMIO)m;
    return (HMMIO)m;
}

MMRESULT WINAPI mmioClose(HMMIO h, UINT flags)
{
    AM2_MMIO *m = MM(h);

    (void)flags;
    if (!m)
        return MMSYSERR_INVALHANDLE;
    fclose(m->fp);
    free(m->info.pchBuffer);
    free(m);
    return MMSYSERR_NOERROR;
}

LONG WINAPI mmioRead(HMMIO h, HPSTR out, LONG n)
{
    AM2_MMIO *m = MM(h);
    LONG      done = 0;

    if (!m || n < 0)
        return -1;
    while (done < n) {
        LONG have = (LONG)(m->info.pchEndRead - m->info.pchNext);

        if (have <= 0) {
            if (mm_fill(m) <= 0)
                break;
            continue;
        }
        if (have > n - done)
            have = n - done;
        memcpy(out + done, m->info.pchNext, (size_t)have);
        m->info.pchNext += have;
        done += have;
    }
    return done;
}

LONG WINAPI mmioSeek(HMMIO h, LONG off, int32_t whence)
{
    AM2_MMIO *m = MM(h);
    LONG      target, held;

    if (!m)
        return -1;
    switch (whence) {
    case SEEK_SET: target = off; break;
    case SEEK_CUR: target = mm_position(m) + off; break;
    case SEEK_END: target = m->length + off; break;
    default:       return -1;
    }
    if (target < 0)
        return -1;

    held = (LONG)(m->info.pchEndRead - m->info.pchBuffer);
    if (target >= m->info.lBufOffset && target <= m->info.lBufOffset + held
        && held > 0) {
        m->info.pchNext = m->info.pchBuffer + (target - m->info.lBufOffset);
    } else {
        /* Empty the window; the next read fills it from there. */
        m->info.lBufOffset  = target;
        m->info.lDiskOffset = target;
        m->info.pchNext = m->info.pchEndRead = m->info.pchBuffer;
    }
    return target;
}

static int32_t mm_read_header(AM2_MMIO *m, LPMMCKINFO ck)
{
    DWORD hdr[2];

    if (mmioRead((HMMIO)m, (HPSTR)hdr, 8) != 8)
        return 0;
    ck->ckid         = hdr[0];
    ck->cksize       = hdr[1];
    ck->dwDataOffset = (DWORD)mm_position(m);
    ck->dwFlags      = 0;
    if (ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST) {
        if (mmioRead((HMMIO)m, (HPSTR)&ck->fccType, 4) != 4)
            return 0;
    } else {
        ck->fccType = 0;
    }
    return 1;
}

MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent, UINT flags)
{
    AM2_MMIO *m = MM(h);
    FOURCC    wantId = 0, wantType = 0;
    LONG      limit;

    if (!m || !ck)
        return MMSYSERR_INVALPARAM;

    if (flags & MMIO_FINDRIFF) { wantId = FOURCC_RIFF; wantType = ck->fccType; }
    else if (flags & MMIO_FINDLIST) { wantId = FOURCC_LIST; wantType = ck->fccType; }
    else if (flags & MMIO_FINDCHUNK) { wantId = ck->ckid; }

    limit = parent ? (LONG)(parent->dwDataOffset + parent->cksize) : m->length;

    for (;;) {
        if (mm_position(m) + 8 > limit)
            return MMIOERR_CHUNKNOTFOUND;
        if (!mm_read_header(m, ck))
            return MMIOERR_CHUNKNOTFOUND;
        if (!wantId)
            return MMSYSERR_NOERROR;
        if (ck->ckid == wantId && (!wantType || ck->fccType == wantType))
            return MMSYSERR_NOERROR;
        /* Not it: step over the chunk, padded to an even length. */
        mmioSeek(h, (LONG)(ck->dwDataOffset + ck->cksize + (ck->cksize & 1)), SEEK_SET);
    }
}

MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    (void)flags;
    if (!MM(h) || !ck)
        return MMSYSERR_INVALPARAM;
    mmioSeek(h, (LONG)(ck->dwDataOffset + ck->cksize + (ck->cksize & 1)), SEEK_SET);
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT flags)
{
    (void)flags;
    if (!MM(h) || !info)
        return MMSYSERR_INVALPARAM;
    *info = MM(h)->info;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioSetInfo(HMMIO h, const MMIOINFO *info, UINT flags)
{
    AM2_MMIO *m = MM(h);

    (void)flags;
    if (!m || !info)
        return MMSYSERR_INVALPARAM;
    /* What the caller may have moved is the read position. */
    if (info->pchNext >= m->info.pchBuffer && info->pchNext <= m->info.pchEndRead)
        m->info.pchNext = info->pchNext;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT flags)
{
    AM2_MMIO *m = MM(h);

    (void)flags;
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (info && info->pchNext >= m->info.pchBuffer && info->pchNext <= m->info.pchEndRead)
        m->info.pchNext = info->pchNext;
    /* Whatever is left unread in the window is re-read with the next: the
     * disk offset steps back to the read position first. */
    m->info.lDiskOffset = mm_position(m);
    mm_fill(m);
    if (info)
        *info = m->info;
    return MMSYSERR_NOERROR;
}

/* ---- winmm: timers --------------------------------------------------------------- */

typedef struct AM2_MMTimer {
    LPTIMECALLBACK cb;
    DWORD_PTR      user;
    UINT           id;
} AM2_MMTimer;

static void mm_timer_fire(void *ud)
{
    AM2_MMTimer *t = (AM2_MMTimer *)ud;

    t->cb(t->id, 0, t->user, 0, 0);
}

/* A handful of timers at most; the game uses one. */
#define AM2_MM_TIMERS 8
static AM2_MMTimer am2_mm_timers[AM2_MM_TIMERS];
static uint32_t    am2_mm_host[AM2_MM_TIMERS];

MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK cb,
                             DWORD_PTR user, UINT flags)
{
    int32_t i;

    (void)resolution;
    if (!cb)
        return 0;
    for (i = 0; i < AM2_MM_TIMERS; i++) {
        if (am2_mm_timers[i].cb)
            continue;
        am2_mm_timers[i].cb   = cb;
        am2_mm_timers[i].user = user;
        am2_mm_timers[i].id   = (UINT)(i + 1);
        am2_mm_host[i] = am2_host_timer_add(delay, (flags & TIME_PERIODIC) ? 1 : 0,
                                            mm_timer_fire, &am2_mm_timers[i]);
        if (!am2_mm_host[i]) {
            am2_mm_timers[i].cb = NULL;
            return 0;
        }
        return am2_mm_timers[i].id;
    }
    return 0;
}

MMRESULT WINAPI timeKillEvent(UINT id)
{
    if (id == 0 || id > AM2_MM_TIMERS || !am2_mm_timers[id - 1].cb)
        return MMSYSERR_INVALPARAM;
    am2_host_timer_remove(am2_mm_host[id - 1]);
    am2_mm_host[id - 1] = 0;
    am2_mm_timers[id - 1].cb = NULL;
    return TIMERR_NOERROR;
}

MMRESULT WINAPI timeBeginPeriod(UINT period) { (void)period; return TIMERR_NOERROR; }
MMRESULT WINAPI timeEndPeriod(UINT period) { (void)period; return TIMERR_NOERROR; }
