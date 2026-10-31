/* smackw32.cpp -- RAD's Smacker API over libsmacker, for the native port.
 *
 * The game plays its films through smackw32.dll (movie.cpp): SmackOpen hands
 * back a handle whose fields it reads directly -- the size, the frame count,
 * the current frame, and the palette with the flag saying this frame changed
 * it -- and each frame goes SmackWait (is it time?), SmackToBuffer (where to
 * draw), SmackDoFrame (draw it), SmackNextFrame. This is that API, adapted to
 * libsmacker (third_party/libsmacker, LGPL 2.1), which decodes and does its
 * own file access. The handle's public part is the game's own view of RAD's
 * struct, AM2_Smack in movie.h, so the offsets are the ones the image reads.
 *
 * libsmacker steps by decoding: smk_first and smk_next each unpack a whole
 * frame -- video, palette and audio. So SmackOpen decodes frame 0, DoFrame
 * copies the current frame out, NextFrame decodes the next, and the palette
 * flag is settled at decode time, before the game asks.
 *
 * Pacing is the film's own rate (66,660 us a frame for every shipped .smk),
 * timed with timeGetTime -- the platform's, so it is deterministic under
 * lockstep -- from the first frame drawn. The game's 50 ms timer only raises
 * a flag; SmackWait decides.
 *
 * Audio goes where smackw32 sends it: a looping secondary buffer on the
 * DirectSound object SmackSoundUseDirectSound was handed, one per enabled
 * track, written one frame's chunk at a time as the frame is drawn. A stall
 * long enough to drain it (the window losing focus stops DoFrame) pauses the
 * buffer instead of looping stale sound, and the next chunk restarts it.
 *
 * SmackDDSurfaceType answers 0, "an 8-bit surface Smacker does not write to
 * itself", which is what makes the game push each new palette to DirectDraw
 * (MovieApplyPalette). Native build only: everywhere else the real
 * smackw32.dll, or the hybrid's missing-import trap, answers.
 */
#if defined(AM2_NATIVE)

#include "../../inject/win32.h"
#include "movie.h"
#include "smackw32.h"
#include "../../../third_party/libsmacker/smacker.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RAD's track bits: SMACKTRACK1 is 0x2000, one bit per track up to seven. */
#define SMACK_TRACK_BIT(t)   (0x2000u << (t))
#define SMACK_TRACKS         7
/* How much sound a track buffer holds, and how far past the last chunk it is
 * kept silent, so a stall plays quiet rather than the loop's old contents. */
#define SMACK_AUDIO_SECONDS  4
#define SMACK_SILENCE_MS     500

struct AM2_SmackTrack {
    LPDIRECTSOUNDBUFFER buf;
    uint32_t size;          /* bytes in the ring */
    uint32_t align;         /* bytes per sample frame */
    uint32_t bytesPerSec;
    uint8_t  silence;       /* 0x80 for 8-bit, 0 for 16-bit */
    uint32_t write;         /* where the next chunk goes */
    uint64_t written;       /* bytes queued since the start */
    uint64_t played;        /* bytes the play cursor has passed */
    uint32_t lastPlay;      /* the play cursor when last looked at */
    int32_t  playing;
};

struct AM2_SmackState {
    AM2_Smack pub;          /* what the game reads -- first, at offset 0 */
    smk       s;
    double    usf;          /* microseconds per frame */
    uint8_t   yScale;       /* SMK_FLAG_Y_* */
    uint32_t  srcH;         /* the decoded frame's own height */
    int32_t   shown;        /* a frame has been drawn: t0 is set */
    uint32_t  t0;           /* timeGetTime of frame 0 */
    /* SmackToBuffer's destination */
    uint8_t  *dest;
    uint32_t  left, top, pitch, destHeight;
    AM2_SmackTrack track[SMACK_TRACKS];
};

static LPDIRECTSOUND am2_smack_dsound;

/* The palette the decoder now holds into the handle, and whether it differs
 * from what the handle held: the game repaints only on a change. Frame 0
 * always counts as one. */
static void SmackTakePalette(AM2_SmackState *st, int32_t first)
{
    const unsigned char *pal = smk_get_palette(st->s);

    if (!pal)
        return;
    st->pub.newPalette = first || memcmp(st->pub.palette, pal, 256 * 3) != 0;
    memcpy(st->pub.palette, pal, 256 * 3);
}

/* DirectSound volume (hundredths of a dB, 0 loudest) from RAD's linear
 * 0..32768. */
static LONG SmackDsVolume(uint32_t volume)
{
    double db;

    if (volume == 0)
        return DSBVOLUME_MIN;
    db = 2000.0 * log10((double)volume / 32768.0);
    if (db > DSBVOLUME_MAX)
        return DSBVOLUME_MAX;
    if (db < DSBVOLUME_MIN)
        return DSBVOLUME_MIN;
    return (LONG)db;
}

/* DirectSound pan from RAD's 0..65535, 32768 the centre. */
static LONG SmackDsPan(uint32_t pan)
{
    int32_t p = (int32_t)pan - 32768;

    if (p > 32767)
        p = 32767;
    return (LONG)(p * (int32_t)DSBPAN_RIGHT / 32768);
}

static void SmackTrackOpen(AM2_SmackTrack *t, uint32_t rate, uint32_t bits,
                           uint32_t channels)
{
    WAVEFORMATEX  fmt;
    DSBUFFERDESC  desc;

    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = (WORD)channels;
    fmt.nSamplesPerSec  = rate;
    fmt.wBitsPerSample  = (WORD)bits;
    fmt.nBlockAlign     = (WORD)(channels * bits / 8);
    fmt.nAvgBytesPerSec = rate * fmt.nBlockAlign;

    memset(&desc, 0, sizeof desc);
    desc.dwSize        = sizeof desc;
    desc.dwFlags       = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_GETCURRENTPOSITION2;
    desc.dwBufferBytes = fmt.nAvgBytesPerSec * SMACK_AUDIO_SECONDS;
    desc.lpwfxFormat   = &fmt;

    if (IDirectSound_CreateSoundBuffer(am2_smack_dsound, &desc, &t->buf, NULL) != DS_OK) {
        t->buf = NULL;
        return;
    }
    t->size        = desc.dwBufferBytes;
    t->align       = fmt.nBlockAlign;
    t->bytesPerSec = fmt.nAvgBytesPerSec;
    t->silence     = bits == 8 ? 0x80 : 0;
}

/* Copy n bytes into the ring at the write cursor, wrapping. */
static void SmackTrackPut(AM2_SmackTrack *t, uint32_t at, const uint8_t *src,
                          uint32_t n, int32_t fill)
{
    LPVOID   p1, p2;
    DWORD    n1, n2;

    if (n == 0)
        return;
    if (IDirectSoundBuffer_Lock(t->buf, at, n, &p1, &n1, &p2, &n2, 0) != DS_OK)
        return;
    if (src) {
        memcpy(p1, src, n1);
        if (p2)
            memcpy(p2, src + n1, n2);
    } else {
        memset(p1, fill, n1);
        if (p2)
            memset(p2, fill, n2);
    }
    IDirectSoundBuffer_Unlock(t->buf, p1, n1, p2, n2);
}

/* How far the play cursor has moved since last asked. Called at least once
 * a game frame, far inside the ring's four seconds, so a whole wrap between
 * two looks cannot happen. */
static void SmackTrackAdvance(AM2_SmackTrack *t)
{
    DWORD play = 0;

    if (!t->buf || !t->playing)
        return;
    IDirectSoundBuffer_GetCurrentPosition(t->buf, &play, NULL);
    t->played  += (play + t->size - t->lastPlay) % t->size;
    t->lastPlay = play;
    if (t->played >= t->written) {
        /* Drained: stop rather than loop old sound, and line the cursor up
         * with the next chunk. */
        IDirectSoundBuffer_Stop(t->buf);
        IDirectSoundBuffer_SetCurrentPosition(t->buf, t->write);
        t->lastPlay = t->write;
        t->played   = t->written;
        t->playing  = 0;
    }
}

static void SmackTrackQueue(AM2_SmackTrack *t, const uint8_t *pcm, uint32_t n)
{
    uint32_t room, quiet;

    if (!t->buf || !pcm || n == 0)
        return;
    SmackTrackAdvance(t);
    room = t->size - (uint32_t)(t->written - t->played);
    if (n > room)
        n = room - room % t->align;     /* never overwrite what is unplayed */
    SmackTrackPut(t, t->write, pcm, n, 0);
    t->write    = (t->write + n) % t->size;
    t->written += n;

    /* Silence ahead of the new end, inside what is free. */
    quiet = t->bytesPerSec * SMACK_SILENCE_MS / 1000;
    room  = t->size - (uint32_t)(t->written - t->played);
    if (quiet > room)
        quiet = room;
    SmackTrackPut(t, t->write, NULL, quiet - quiet % t->align, t->silence);

    if (!t->playing) {
        IDirectSoundBuffer_Play(t->buf, 0, 0, DSBPLAY_LOOPING);
        t->playing = 1;
    }
}

/* The game's name is a relative Windows path ("avi\act1.smk") under its own
 * directory, which is the working directory; libsmacker opens host paths. */
static void SmackHostPath(const char *name, char *out, size_t cap)
{
    size_t i;

    for (i = 0; name[i] && i + 1 < cap; i++)
        out[i] = name[i] == '\\' ? '/' : name[i];
    out[i] = 0;
}

void *__stdcall SmackOpen(const char *name, uint32_t flags, uint32_t extra)
{
    AM2_SmackState *st;
    char            path[260];
    unsigned long   frame, frames, w, h, rate[SMACK_TRACKS];
    unsigned char   mask, channels[SMACK_TRACKS], bits[SMACK_TRACKS];
    uint32_t        t;

    (void)extra;
    SmackHostPath(name, path, sizeof path);

    st = (AM2_SmackState *)calloc(1, sizeof *st);
    if (!st)
        return NULL;
    st->s = smk_open_file(path, SMK_MODE_DISK);
    if (!st->s) {
        free(st);
        return NULL;
    }
    smk_info_all(st->s, &frame, &frames, &st->usf);
    smk_info_video(st->s, &w, &h, &st->yScale);
    smk_info_audio(st->s, &mask, channels, bits, rate);

    /* A doubled or interlaced film is shown at twice its decoded height. */
    st->srcH       = (uint32_t)h;
    st->pub.width  = (uint32_t)w;
    st->pub.height = st->yScale == SMK_FLAG_Y_NONE ? (uint32_t)h : (uint32_t)h * 2;
    st->pub.frames = (uint32_t)frames;

    smk_enable_video(st->s, 1);
    for (t = 0; t < SMACK_TRACKS; t++) {
        int32_t want = (mask & (1u << t)) && (flags & SMACK_TRACK_BIT(t));

        smk_enable_audio(st->s, (unsigned char)t, want ? 1 : 0);
        if (want && am2_smack_dsound && bits[t] && channels[t])
            SmackTrackOpen(&st->track[t], (uint32_t)rate[t], bits[t], channels[t]);
    }

    if (smk_first(st->s) < 0) {
        SmackClose(st);
        return NULL;
    }
    st->pub.frameNum = 0;
    SmackTakePalette(st, 1);
    return st;
}

void __stdcall SmackClose(void *smack)
{
    AM2_SmackState *st = (AM2_SmackState *)smack;
    uint32_t        t;

    if (!st)
        return;
    for (t = 0; t < SMACK_TRACKS; t++) {
        if (st->track[t].buf) {
            IDirectSoundBuffer_Stop(st->track[t].buf);
            IDirectSoundBuffer_Release(st->track[t].buf);
        }
    }
    smk_close(st->s);
    free(st);
}

void __stdcall SmackToBuffer(void *smack, uint32_t left, uint32_t top,
                             uint32_t pitch, uint32_t destHeight, void *dest,
                             uint32_t flags)
{
    AM2_SmackState *st = (AM2_SmackState *)smack;

    (void)flags;                /* 0: the 8-bit surface SmackDDSurfaceType named */
    st->dest       = (uint8_t *)dest;
    st->left       = left;
    st->top        = top;
    st->pitch      = pitch;
    st->destHeight = destHeight;
}

void __stdcall SmackDoFrame(void *smack)
{
    AM2_SmackState      *st = (AM2_SmackState *)smack;
    const unsigned char *frame = smk_get_video(st->s);
    uint32_t             y, t, w = st->pub.width;

    if (frame && st->dest) {
        for (y = 0; y < st->pub.height && st->top + y < st->destHeight; y++) {
            uint8_t *row = st->dest + (st->top + y) * st->pitch + st->left;

            if (st->yScale == SMK_FLAG_Y_NONE)
                memcpy(row, frame + y * w, w);
            else if (st->yScale == SMK_FLAG_Y_DOUBLE)
                memcpy(row, frame + (y / 2) * w, w);
            else if (y % 2 == 0)        /* interlaced: every other line black */
                memcpy(row, frame + (y / 2) * w, w);
            else
                memset(row, 0, w);
        }
    }

    if (!st->shown) {
        st->shown = 1;
        st->t0    = timeGetTime();
    }
    for (t = 0; t < SMACK_TRACKS; t++)
        SmackTrackQueue(&st->track[t], smk_get_audio(st->s, (unsigned char)t),
                        (uint32_t)smk_get_audio_size(st->s, (unsigned char)t));
}

void __stdcall SmackNextFrame(void *smack)
{
    AM2_SmackState *st = (AM2_SmackState *)smack;

    if (smk_next(st->s) < 0)
        return;
    st->pub.frameNum++;
    SmackTakePalette(st, 0);
}

/* Non-zero while it is still too early for the current frame. */
uint32_t __stdcall SmackWait(void *smack)
{
    AM2_SmackState *st = (AM2_SmackState *)smack;
    uint32_t        due, t;

    for (t = 0; t < SMACK_TRACKS; t++)
        SmackTrackAdvance(&st->track[t]);
    if (!st->shown)
        return 0;
    due = st->t0 + (uint32_t)((double)st->pub.frameNum * st->usf / 1000.0);
    return (int32_t)(timeGetTime() - due) < 0 ? 1 : 0;
}

void __stdcall SmackSoundUseDirectSound(void *dsound)
{
    am2_smack_dsound = (LPDIRECTSOUND)dsound;
}

uint32_t __stdcall SmackDDSurfaceType(void *surface)
{
    (void)surface;
    return 0;
}

void __stdcall SmackVolumePan(void *smack, uint32_t trackFlags, uint32_t volume,
                              uint32_t pan)
{
    AM2_SmackState *st = (AM2_SmackState *)smack;
    uint32_t        t;

    for (t = 0; t < SMACK_TRACKS; t++) {
        if ((trackFlags & SMACK_TRACK_BIT(t)) && st->track[t].buf) {
            IDirectSoundBuffer_SetVolume(st->track[t].buf, SmackDsVolume(volume));
            IDirectSoundBuffer_SetPan(st->track[t].buf, SmackDsPan(pan));
        }
    }
}

#endif /* AM2_NATIVE */
