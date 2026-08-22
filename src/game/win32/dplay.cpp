/* DirectPlay object creation -- reconstructed from ArmyMen2.exe.
 *
 *   CommCreateDirectPlay    0x0040DD20  1 call site, thiscall
 *   CreateDirectPlayLobby   0x0040DDD0  1 call site, stdcall
 *
 * These are the only two CoCreateInstance sites in the game and, between them,
 * its entire outward network surface. See dplay.h for why that surface is two
 * functions long.
 *
 * It also holds the comm subsystem's teardown, which is the other half of the
 * same boundary: DirectPlay is created here and the threads that feed it are
 * stopped here.
 *
 * The identification came out of the GUIDs rather than out of any name. The
 * game carries its own copies in .rdata, and they are CLSID_DirectPlay with
 * IID_IDirectPlay4A, and CLSID_DirectPlayLobby with IID_IDirectPlayLobby3A.
 * The vtable slot the first one then calls, 38, is IDirectPlay4's
 * InitializeConnection -- which is the check that the interface really is what
 * the IID says, because slot 38 would be off the end of any smaller one.
 *
 * The first is the project's first reconstructed thiscall function. GCC's
 * `thiscall` attribute is exactly MSVC's convention: first argument in ecx, the
 * rest on the stack, callee cleans. `ret 4` and one stack argument agree.
 */

#include "../gameproc.h"
#include "dplay.h"
#include "../armymsg.h"   /* SendGamePause */
#include "frame.h"
#include "cdcheck.h"
#include "../msgslot.h"
#include "../objtable.h"
#include "../../inject/patch.h"

#include <stdint.h>

static_assert(CLSCTX_INPROC_SERVER == 1, "CLSCTX_INPROC_SERVER");

#define kCLSID_DirectPlay      (*(const CLSID *)(uintptr_t)ADDR_CLSID_DIRECTPLAY)
#define kIID_IDirectPlay4A     (*(const IID *)(uintptr_t)ADDR_IID_DIRECTPLAY4A)
#define kCLSID_DirectPlayLobby (*(const CLSID *)(uintptr_t)ADDR_CLSID_DPLAY_LOBBY)
#define kIID_IDirectPlayLobby3A (*(const IID *)(uintptr_t)ADDR_IID_DPLAY_LOBBY3A)

/* Both take the comm object in ecx and nothing else. */
typedef void (__attribute__((thiscall)) *am2_comm_method_fn)(void *comm);

typedef void (__cdecl *am2_destroy_list_fn)(void *list);

#define g_commEvent    (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT)
#define g_commEvent2   (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT_2)
#define g_packetThread (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)

/* The interface pointer lives inside the comm object rather than in a global,
 * so it is reached through `this` like any other member. */
static inline LPDIRECTPLAY4A *DirectPlaySlot(void *comm)
{
    return (LPDIRECTPLAY4A *)((uint8_t *)comm + COMM_OFF_DPLAY);
}

/* All defined further down; called from here. */
int32_t __attribute__((thiscall)) CommOnConnected(void *self);
int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm);
int32_t __attribute__((thiscall)) CommCreatePlayer(void *comm, const char *name,
                                                   HANDLE event, void *data,
                                                   DWORD length);

int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection)
{
    LPDIRECTPLAY4A *slot = DirectPlaySlot(comm);
    HRESULT         hr;

    /* Whatever is there now goes first; creating a second one over the top
     * would leak the interface and the socket underneath it. */
    if (*slot)
        CommDropDirectPlay(comm);

    hr = CoCreateInstance(kCLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER,
                          kIID_IDirectPlay4A, (LPVOID *)slot);
    if (hr != S_OK)
        return 0;

    /* A null connection means "create the object and stop" -- which is what
     * enumerating the available providers needs, since that is done on an
     * object that has not been pointed at one yet. */
    if (connection) {
        hr = IDirectPlayX_InitializeConnection(*slot, connection, 0);
        if (hr != S_OK)
            return 0;
        CommOnConnected(comm);
    }
    return 1;
}

int32_t __stdcall CreateDirectPlayLobby(LPDIRECTPLAYLOBBY3A *out)
{
    LPDIRECTPLAYLOBBY3A lobby = NULL;
    HRESULT             hr;

    hr = CoCreateInstance(kCLSID_DirectPlayLobby, NULL, CLSCTX_INPROC_SERVER,
                          kIID_IDirectPlayLobby3A, (LPVOID *)&lobby);
    /* Stored either way. The local was zeroed first, so a failure hands back
     * NULL rather than leaving the caller holding whatever it had. */
    *out = lobby;
    return hr == S_OK;
}

/* Original: 0x004020A0, 1 call site. Shut the comm subsystem down.
 *
 * Four message lists go first -- each one closes its own mutex -- then the
 * packet thread is woken by signalling its event and its exit code collected,
 * and finally both event handles are closed.
 *
 * The wait is not really a wait. GetExitCodeThread is retried until the *call*
 * succeeds, which it does immediately; the loop does not look at the code it
 * gets back, so a thread still running reports STILL_ACTIVE and the log says it
 * "exited" anyway. That is what the original does, and it is why the shutdown
 * lines appear in every run's log whether or not the thread had finished. */
void __cdecl CommShutdown(void)
{
    DWORD exitCode;

    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_A);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_B);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_C);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_D);

    orig_log("Setting Event 0 \n");
    SetEvent(g_commEvent);

    if (g_packetThread) {
        /* Retried until the call itself succeeds -- see the note above. */
        while (!GetExitCodeThread(g_packetThread, &exitCode))
            ;
        orig_log("Packet Thread Exited with return code %d\n", exitCode);
        CloseHandle(g_packetThread);
    }

    if (g_commEvent)
        CloseHandle(g_commEvent);
    g_commEvent = NULL;
    if (g_commEvent2)
        CloseHandle(g_commEvent2);
    g_commEvent2 = NULL;
}

/* The comm object lives behind a pointer; these three reach the interface the
 * same way CommCreateDirectPlay does. */
#define g_commObject (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_commEnumCount (*(int32_t *)(uintptr_t)ADDR_COMM_ENUM_COUNT)
#define g_hwnd          (*(HWND *)(uintptr_t)ADDR_HWND)

int32_t __cdecl CommClose(void)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(g_commObject);

    /* Nothing open is as good as closed, from the caller's point of view. */
    if (!dp)
        return 1;
    return IDirectPlayX_Close(dp) == DP_OK;
}

int32_t __attribute__((thiscall)) CommInitializeConnection(void *comm,
                                                           void *connection)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);

    /* No object, so nothing to point anywhere -- and the original answers 0
     * here by falling through with the null still in the return register. */
    if (!dp)
        return 0;

    if (IDirectPlayX_InitializeConnection(dp, connection, 0) != DP_OK) {
        orig_log("Unable to intialize connection\n");
        return 0;
    }
    return 1;
}

int32_t __attribute__((thiscall)) CommSetSessionDesc(void *comm, void *desc,
                                                     uint32_t flags)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);

    if (!dp)
        return 0;
    return IDirectPlayX_SetSessionDesc(dp, (LPDPSESSIONDESC2)desc, flags) == DP_OK;
}

/* The SDK spells this one unsigned and HRESULT is signed, so the comparison
 * needs a cast to keep -Wsign-compare quiet. The value is checked here rather
 * than trusted. */
static_assert((uint32_t)DPERR_BUFFERTOOSMALL == 0x8877001Eu,
              "DPERR_BUFFERTOOSMALL");


int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm)
{
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(comm);
    void         **slot = (void **)((uint8_t *)comm + COMM_OFF_SESSION_BUF);
    DWORD          size;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* Whatever was fetched last time goes first -- on the game's heap, so
     * through the game's free. */
    if (*slot) {
        orig_free(*slot);
        *slot = NULL;
    }

    /* DirectPlay will not say how big the description is except by refusing to
     * write it, so ask once with no buffer and read the size out of the
     * complaint. Anything other than that complaint is the final answer. */
    hr = IDirectPlayX_GetSessionDesc(dp, NULL, &size);
    if (hr == (HRESULT)DPERR_BUFFERTOOSMALL) {
        *slot = orig_malloc(size);
        if (!*slot)
            return 0;
        hr = IDirectPlayX_GetSessionDesc(*DirectPlaySlot(comm), *slot, &size);
    }
    return hr == DP_OK;
}

/* ---- the comm object's own lifetime ------------------------------------
 *
 * These two are the game's entire registry surface. There is no third call:
 * the import table lists RegCreateKeyExA and RegCloseKey once each, and both
 * sites are below, and there is no RegQueryValue or RegSetValue anywhere in
 * the image. The key is created with KEY_ALL_ACCESS at startup, the handle is
 * parked at +0x204, and it is closed again at exit without a single value ever
 * being read or written through it. The `\1.00` subkey that exists under it in
 * a real install is the installer's, not the game's.
 *
 * The constructor runs from the CRT's static-initialiser table before WinMain,
 * and the destructor is handed to atexit by a thunk at 0x0040DB60, so a run
 * that is killed rather than quit never reaches it. Both are thiscall on the
 * single global at ADDR_COMM_OBJECT; the two thunks that supply ecx are left
 * original, which is why the bodies are patched and not their entry points.
 *
 * Most of the constructor is field initialisation whose meaning is not
 * recoverable from one function, and it is transcribed rather than
 * interpreted -- offsets and constants exactly as the original writes them,
 * in the order it writes them. */

/* The two constants the original hardcodes. HKEY_LOCAL_MACHINE is a cast
 * pointer and so cannot be static_asserted; it is 0x80000002, which is the
 * value pushed at 0x0040DC27. */
static_assert(KEY_ALL_ACCESS == 0xF003F, "KEY_ALL_ACCESS");

#define comm_u32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))

typedef void (__cdecl *am2_comm_void_fn)(void);
#define orig_comm_init_defaults (*(am2_comm_void_fn)ADDR_COMM_INIT_DEFAULTS)
#define orig_comm_reset_state   (*(am2_comm_method_fn)ADDR_COMM_RESET_STATE)
/* The logger is called with no arguments at all. It is stubbed to `ret` in the
 * shipping image, so this is a no-op either way, but it is reproduced rather
 * than dropped -- see ADDR_LOG. */
#define orig_log_noargs         (*(am2_comm_void_fn)ADDR_LOG)

/* Four of these, back to back at +0x20C. Only the index and a timestamp are
 * ever set to anything; the rest is cleared. */
#define COMM_SLOT_BASE   0x20C
#define COMM_SLOT_STRIDE 0x70

void *__attribute__((thiscall)) CommConstruct(void *comm)
{
    uint8_t *self = (uint8_t *)comm;
    uint32_t now;
    int32_t  i;

    StartPacketThread();
    orig_comm_init_defaults();
    now = GetTickCount();

    comm_u32(self, 0x3EC) = 0;
    comm_u32(self, 0x3F4) = 0;

    for (i = 0; i < 4; i++) {
        uint8_t *slot = self + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;

        comm_u32(slot, 0x00) = 0;
        comm_u32(slot, 0x64) = 0;
        comm_u32(slot, 0x68) = 0;
        comm_u32(slot, 0x04) = (uint32_t)i;
        comm_u32(slot, 0x54) = 0;
        comm_u32(slot, 0x08) = 0;
        comm_u32(slot, 0x50) = 0;
        comm_u32(slot, 0x4C) = 0;
        comm_u32(slot, 0x58) = 0;
        comm_u32(slot, 0x5C) = 0;
        comm_u32(slot, 0x60) = now;
        memset(slot + 0x0C, 0, 0x40);
    }

    comm_u32(self, 0x008) = 0;
    comm_u32(self, 0x408) = now;
    comm_u32(self, 0x004) = 0;
    comm_u32(self, 0x410) = 0x400;
    orig_comm_reset_state(comm);

    /* The one key the game ever touches. Created, never read. */
    RegCreateKeyExA(HKEY_LOCAL_MACHINE, (const char *)(uintptr_t)ADDR_REGISTRY_KEY,
                    0, NULL, 0, KEY_ALL_ACCESS, NULL,
                    (PHKEY)(self + 0x204), (LPDWORD)(self + 0x208));

    comm_u32(self, 0x3D4) = ADDR_APP_GUID;   /* the DirectPlay application id */
    comm_u32(self, 0x3D0) = 1;
    comm_u32(self, 0x3E4) = 0;
    comm_u32(self, 0x3D8) = 0;
    comm_u32(self, 0x3DC) = 0;
    comm_u32(self, 0x404) = 0;
    comm_u32(self, 0x414) = 0;
    comm_u32(self, 0x3FC) = 0;
    comm_u32(self, 0x3F8) = 0;
    comm_u32(self, 0x418) = 0;
    comm_u32(self, 0x41C) = 0;
    comm_u32(self, 0x454) = 1;
    comm_u32(self, 0x420) = 100;
    comm_u32(self, 0x424) = 1000;
    comm_u32(self, 0x428) = 996;
    orig_log_noargs();
    comm_u32(self, 0x478) = 1;
    comm_u32(self, 0x47C) = 2;

    /* A constructor returns its object. */
    return comm;
}

void __attribute__((thiscall)) CommDestruct(void *comm)
{
    uint8_t *self = (uint8_t *)comm;

    CommDropDirectPlay(comm);
    CommShutdown();
    comm_u32(self, 0x3DC) = 0;
    comm_u32(self, 0x404) = 0;
    RegCloseKey((HKEY)(uintptr_t)comm_u32(self, 0x204));
}

/* Ask DirectPlay which sessions are out there.
 *
 * 0x0040E3B0, thiscall, `ret 4`. The multiplayer HOST button calls it with the
 * object that collects the results; the callback at 0x0040E280 fills that
 * object and stays original.
 *
 * The identification is the descriptor and the slot together. It zeroes 0x14
 * dwords -- 0x50 bytes, sizeof(DPSESSIONDESC2) -- sets dwSize to 0x50 and
 * copies four dwords from the comm object's GUID pointer to offset 0x18, which
 * is guidApplication and nothing else. Slot 13 on IDirectPlay4 is EnumSessions.
 * The descriptor alone would equally fit Open, which is what it was first
 * written down as; counting the slot is what settled it.
 *
 * Note the filter is by application GUID only. guidInstance is left zeroed, so
 * this asks for every Army Men II session on the transport rather than a
 * particular one. */
static_assert(sizeof(DPSESSIONDESC2) == 0x50, "DPSESSIONDESC2");
static_assert(DPENUMSESSIONS_AVAILABLE == 1, "DPENUMSESSIONS_AVAILABLE");
static_assert(DPENUMSESSIONS_ASYNC == 0x10, "DPENUMSESSIONS_ASYNC");

/* Resets the collecting object before it is filled again. Stays original. */
typedef void (__attribute__((thiscall)) *am2_session_reset_fn)(void *);
#define orig_session_reset (*(am2_session_reset_fn)ADDR_SESSION_RESET)

#define g_sessionList  (*(void **)(uintptr_t)ADDR_SESSION_LIST)
/* lpContext for both enumerations is the game window; see orig.h. */
#define g_enumContext  (*(void **)(uintptr_t)ADDR_HWND)

int32_t __attribute__((thiscall)) CommEnumSessions(void *comm, void *list)
{
    DPSESSIONDESC2  desc;
    LPDIRECTPLAY4A  dp;
    const GUID     *app;
    HRESULT         hr;

    /* Nowhere to put the answers, or nothing to ask. */
    if (!list)
        return 0;
    dp = *DirectPlaySlot(comm);
    if (!dp)
        return 0;

    orig_session_reset(list);
    /* The callback is a plain function and gets the object through this global
     * rather than through lpContext, which carries something else entirely. */
    g_sessionList = list;

    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;

    app = *(const GUID **)((uint8_t *)comm + COMM_OFF_APP_GUID);
    if (app)
        desc.guidApplication = *app;

    hr = IDirectPlayX_EnumSessions(dp, &desc, 0,
                                   (LPDPENUMSESSIONSCALLBACK2)(uintptr_t)ADDR_ENUM_SESSIONS_CB,
                                   g_enumContext,
                                   DPENUMSESSIONS_AVAILABLE | DPENUMSESSIONS_ASYNC);
    return hr == DP_OK;
}

/* Ask DirectPlay which transports are available, and offer them as menu rows.
 *
 * 0x0040E530, thiscall, `ret 4`, 97 bytes. Slot 35 is EnumConnections. The
 * callback at 0x0040E460 stays original and does two things worth knowing
 * about: it compares each provider's name against "Play on HEAT" and "Play on
 * Mplayer" and drops those two, and it copies the names it keeps onto the
 * game's heap before adding them.
 *
 * Whatever the providers come to, "Play Against Computer Only" is appended
 * afterwards, so the offline choice is always the last row and is there even
 * when DirectPlay offers nothing at all.
 *
 * lpguidApplication is the same GUID pointer CommConstruct installs, so the
 * enumeration is filtered to providers that can carry this game. */
typedef void (__attribute__((thiscall)) *am2_list_add_fn)(void *list,
                                                          const char *name,
                                                          void *data);
#define orig_list_add     (*(am2_list_add_fn)ADDR_LIST_ADD)
#define g_connectionList  (*(void **)(uintptr_t)ADDR_CONNECTION_LIST)

int32_t __attribute__((thiscall)) CommEnumConnections(void *comm, void *list)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);
    const GUID    *app;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* As with the session browser, the callback finds the list here rather
     * than through lpContext, which carries something else. */
    g_connectionList = list;

    app = *(const GUID **)((uint8_t *)comm + COMM_OFF_APP_GUID);
    hr = IDirectPlayX_EnumConnections(dp, app,
                                      (LPDPENUMCONNECTIONSCALLBACK)(uintptr_t)ADDR_ENUM_CONNECTIONS_CB,
                                      g_enumContext, 0);
    if (hr != DP_OK)
        return 0;

    /* Read back through the global rather than the argument, as the original
     * does -- they are the same object, but only because nothing reassigned it. */
    orig_list_add(g_connectionList,
                  (const char *)(uintptr_t)ADDR_STR_COMPUTER_ONLY, NULL);
    return 1;
}

/* The packet transmit -- 0x0040EB70, thiscall, `ret 0x10`.
 *
 * The DirectPlay call in the middle is three lines; the rest is a statistics
 * ring in front of it and a per-player watchdog behind it. Both are the comm
 * object's own bookkeeping and neither is optional, because the counters they
 * keep are read elsewhere.
 *
 * The watchdog is the interesting half. Every successful send bumps an
 * unacknowledged counter on each *other* live player, and when one passes 30 a
 * bit -- 0x800 shifted by the slot index -- is raised in the global event flags,
 * once. That is how a player who has stopped answering is noticed: not by a
 * timeout, but by getting too far ahead of them.
 *
 * On failure it posts 0x046C to the game window, which is one of the six comm
 * messages WndProc forwards to the original.
 *
 * Two faithfulness details. The maximum-size comparison is UNSIGNED in the
 * original (`jbe`), so a length with the top bit set would be recorded as the
 * new maximum and nothing else would ever beat it -- kept. And the watchdog
 * reads the slot id twice, once through `this` and once through the comm
 * global; those are the same object in every call that exists, but the original
 * does both and so does this. */
static_assert((uint32_t)DPERR_INVALIDPLAYER == 0x88770096u, "DPERR_INVALIDPLAYER");
static_assert((uint32_t)E_INVALIDARG == 0x80070057u, "E_INVALIDARG");

typedef void *(__cdecl *am2_find_player_fn)(uint32_t id);
typedef void (__cdecl *am2_set_flags_fn)(uint32_t bits);

#define g_hwnd           (*(HWND *)(uintptr_t)ADDR_HWND)

/* 0x046C -- WndProc forwards this one to the original. */
#define AM2_WM_COMM_SEND_FAILED 0x046Cu
int32_t __attribute__((thiscall)) CommSend(void *comm, uint32_t idTo,
                                           uint32_t flags, void *data,
                                           uint32_t size)
{
    uint8_t       *self = (uint8_t *)comm;
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(comm);
    uint32_t       at, i, n;
    uint8_t       *other;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* Statistics first, whether or not the send goes through. */
    at = comm_u32(self, COMM_OFF_STAT_INDEX);
    comm_u32(self, COMM_OFF_STAT_TIMES + at * 4) = GetTickCount();
    comm_u32(self, COMM_OFF_STAT_SIZES + at * 4) = size;
    comm_u32(self, COMM_OFF_STAT_BYTES)   += size;
    comm_u32(self, COMM_OFF_STAT_PACKETS) += 1;
    if (size > comm_u32(self, COMM_OFF_STAT_MAX))
        comm_u32(self, COMM_OFF_STAT_MAX) = size;
    if (++at >= COMM_STAT_RING)
        at = 0;
    comm_u32(self, COMM_OFF_STAT_INDEX) = at;

    hr = IDirectPlayX_Send(dp, comm_u32(self, COMM_OFF_OUR_PLAYER_ID), idTo,
                           flags, data, size);

    if (hr != DP_OK) {
        uint8_t *cm = g_commObject;
        int32_t  found = 0;

        /* Only these two are reported; anything else fails silently. */
        if (hr == E_INVALIDARG)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_BADPARAM, idTo);
        else if (hr == (HRESULT)DPERR_INVALIDPLAYER)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_BADPLAYER, idTo);
        else
            return 0;

        n = comm_u32(cm, COMM_OFF_PLAYER_COUNT);
        for (i = 0; i < n; i++) {
            if (comm_u32(cm, COMM_SLOT_BASE + i * COMM_SLOT_STRIDE
                             + COMM_SLOT_OFF_ID) == idTo) {
                found = 1;
                break;
            }
        }
        if (!found)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_NOENTRY, idTo);

        PostMessageA(g_hwnd, AM2_WM_COMM_SEND_FAILED, (WPARAM)idTo, 0);
        return 0;
    }

    /* Sent. Everyone else who is live and reachable is now one packet further
     * behind, and past 30 that is worth raising once. */
    other = g_commObject;
    n = comm_u32(other, COMM_OFF_PLAYER_COUNT);
    for (i = 0; i < n; i++) {
        uint8_t *slot = self  + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;
        uint32_t id;
        uint32_t bit;

        if (comm_u32(slot, COMM_SLOT_OFF_ID) == 0xFFFFFFFFu)
            continue;
        id = comm_u32(other, COMM_SLOT_BASE + i * COMM_SLOT_STRIDE
                             + COMM_SLOT_OFF_ID);
        if (id == comm_u32(self, COMM_OFF_OUR_PLAYER_ID))
            continue;
        if (!FindPlayerById(id))
            continue;

        bit = 0x800u << i;
        if (++comm_u32(slot, COMM_SLOT_OFF_UNACKED) <= COMM_STAT_RING)
            continue;
        if (GetPauseFlags() & bit)
            continue;
        PauseGame(bit);
    }
    return 1;
}

/* Create the session -- 0x0040DFC0, thiscall, `ret 4`, 226 bytes.
 *
 * START A WAR ends up here. Slot 24 is Open and the flag is DPOPEN_CREATE, so
 * unlike CommEnumSessions this makes a session rather than looking for one.
 *
 * The descriptor is worth reading as a statement of what a game of Army Men II
 * is: DPSESSION_MIGRATEHOST, four players maximum -- the same four the comm
 * object keeps slots for -- the battle name the player typed as the session
 * name, and the application GUID so only this game finds it.
 *
 * A local game short-circuits the whole thing. StartSelectedGame sets +0x400
 * when the chosen row was "Play Against Computer Only", and that is checked
 * first: there is nothing to open, and the function reports success without
 * touching DirectPlay at all. */
static_assert(DPOPEN_CREATE == 2, "DPOPEN_CREATE");
static_assert(DPSESSION_MIGRATEHOST == 4, "DPSESSION_MIGRATEHOST");

#define g_ourSlot     (*(int32_t *)(uintptr_t)ADDR_OUR_SLOT)

typedef int32_t (__attribute__((thiscall)) *am2_slot_of_id_fn)(void *, DPID);
typedef int32_t (__cdecl *am2_msg_free_fn)(void *list);
#define orig_msg_list_free  (*(am2_msg_free_fn)ADDR_MSG_LIST_POOL)
#define g_joinContext  (*(int32_t *)(uintptr_t)ADDR_JOIN_CONTEXT)

#define AM2_MAX_PLAYERS 4

int32_t __attribute__((thiscall)) CommOpenSession(void *self, const char *name)
{
    uint8_t        *comm = g_commObject;
    DPSESSIONDESC2  desc;
    LPDIRECTPLAY4A  dp;
    const GUID     *app;

    /* Only a networked game needs a session. */
    if (!comm_u32(comm, COMM_OFF_LOCAL)) {
        dp = *DirectPlaySlot(comm);
        if (!dp)
            return 0;

        memset(&desc, 0, sizeof desc);
        desc.dwSize        = sizeof desc;
        desc.dwFlags       = DPSESSION_MIGRATEHOST;
        desc.dwMaxPlayers  = AM2_MAX_PLAYERS;
        desc.lpszSessionNameA = (LPSTR)name;
        /* Our own slot's index field, carried across as user data. Only the
         * host reaches this branch, so "ours" and "the host's" are the same
         * slot here -- which is how the global came to be misnamed. */
        desc.dwUser1 = comm_u32((uint8_t *)self,
                                COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE + 4);

        app = *(const GUID **)(comm + COMM_OFF_APP_GUID);
        if (app)
            desc.guidApplication = *app;

        if (IDirectPlayX_Open(dp, &desc, DPOPEN_CREATE) != DP_OK)
            return 0;
    }

    g_joinContext  = 0;
    g_defaultOwner = 0;
    g_ourSlot     = 0;
    return 1;
}

/* Settle the transport's limits once a connection exists -- 0x0040E660.
 *
 * CommCreateDirectPlay calls this immediately after InitializeConnection
 * succeeds. It asks DirectPlay what the provider can do and then trims the comm
 * object's buffer sizes to fit, because a modem and a LAN do not carry the same
 * packet and the defaults were chosen without knowing which was in use.
 *
 * The defaults it trims are the ones CommConstruct writes -- 0x400 for the
 * maximum and 0x3E4 for the working size -- and the two agree without being
 * made to. Neither is raised, only lowered: a provider that can carry more than
 * 1024 bytes does not get to.
 *
 * The 0x14 subtracted from the working size is DirectPlay's own per-message
 * overhead, left in the number rather than accounted for anywhere else.
 *
 * UNREACHABLE IN THIS BUILD, and that is structural rather than a gap in
 * testing. The only reference to it is the call at 0x0040DD72, inside
 * CommCreateDirectPlay's `if (connection)` branch -- and CommCreateDirectPlay
 * has exactly one caller, at 0x0042EE78, which passes a literal 0. So the
 * branch is never taken and neither is this. The transport is initialised
 * through CommInitializeConnection instead, from StartSelectedGame, which
 * matches what driving the multiplayer path shows: the connection comes up,
 * START A WAR works, and this counter stays at zero.
 *
 * It is reconstructed anyway because it is DirectPlay boundary code and the
 * cost is a fingerprint, but do not go looking for a way to exercise it.
 *
 * The whole capabilities dump is behind `-debugComm`, which is the flag at
 * +0x418. It is also the confirmation that this is a DPCAPS: each of the four
 * values it prints sits exactly where that structure puts the field the message
 * names, and the bit it tests for guaranteed messaging is 0x40, which is
 * DPCAPS_GUARANTEEDSUPPORTED. */
static_assert(sizeof(DPCAPS) == 0x28, "DPCAPS");
static_assert(DPCAPS_GUARANTEEDSUPPORTED == 0x40, "DPCAPS_GUARANTEEDSUPPORTED");

/* DirectPlay's per-message overhead, subtracted from the usable payload. */
#define DPLAY_MESSAGE_OVERHEAD 0x14

#define g_commDebug(self) comm_u32((uint8_t *)(self), COMM_OFF_DEBUG)

int32_t __attribute__((thiscall)) CommOnConnected(void *self)
{
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(self);
    uint8_t       *comm = g_commObject;
    LPDPCAPS       caps = (LPDPCAPS)(comm + COMM_OFF_CAPS);
    uint32_t       usable;

    if (!dp)
        return 0;

    caps->dwSize = sizeof *caps;
    if (IDirectPlayX_GetCaps(dp, caps, 0) != DP_OK)
        return 0;

    if (g_commDebug(self)) {
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_HEAD);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_PACKET, caps->dwMaxBufferSize);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_HEADER, caps->dwHeaderLength);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_LATENCY, caps->dwLatency);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_TIMEOUT, caps->dwTimeout);
        orig_log((const char *)(uintptr_t)
                 ((caps->dwFlags & DPCAPS_GUARANTEEDSUPPORTED)
                  ? ADDR_STR_CAPS_GUAR_YES : ADDR_STR_CAPS_GUAR_NO));
    }

    /* Lowered to fit the provider, never raised past what was asked for. */
    if (caps->dwMaxBufferSize < comm_u32(comm, COMM_OFF_BUFFER_MAX))
        comm_u32(comm, COMM_OFF_BUFFER_MAX) = caps->dwMaxBufferSize;

    usable = caps->dwMaxBufferSize - DPLAY_MESSAGE_OVERHEAD;
    if (usable < comm_u32(comm, COMM_OFF_BUFFER_DEFAULT))
        comm_u32(comm, COMM_OFF_BUFFER_DEFAULT) = usable;

    if (g_commDebug(self))
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_BUFFERS,
                 comm_u32(comm, COMM_OFF_BUFFER_MAX),
                 comm_u32(comm, COMM_OFF_BUFFER_DEFAULT));
    return 1;
}

/* Give the connection back -- 0x0040EA40, thiscall, and the last of the
 * DirectPlay surface.
 *
 * Everything the connection owns goes: two heap buffers, the IDirectPlay4A
 * itself, every remote player, and finally the lobby interface at +0x3F4 --
 * which is named by the store at 0x0040ED3C, where CreateDirectPlayLobby is
 * handed that exact address to fill.
 *
 * The four player slots are not merely cleared, they are put back to what
 * CommConstruct built: zeroed apart from the slot index and a fresh
 * GetTickCount, with the 0x40-byte name buffer blanked. That is the third
 * function to write this layout -- the constructor, StartSelectedGame's
 * computer-player fill, and now this -- and all three agree.
 *
 * Only remote players are destroyed. Ours, at +0x3CC, is skipped inside the
 * loop and removed afterwards, so it outlives the slots it appears in.
 *
 * Returns 0 always, which every caller ignores. */
typedef void (__cdecl *am2_remove_player_fn)(uint32_t id);
#define orig_remove_player     (*(am2_remove_player_fn)ADDR_REMOVE_PLAYER)
#define g_commUnknown4F48E0    (*(int32_t *)(uintptr_t)ADDR_COMM_UNKNOWN_4F48E0)

int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm)
{
    uint8_t             *self = (uint8_t *)comm;
    LPDIRECTPLAY4A      *dp    = DirectPlaySlot(comm);
    LPDIRECTPLAYLOBBY3A *lobby =
        (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    uint32_t             ours;
    int32_t              i;

    if (comm_u32(self, COMM_OFF_SEND_BUF)) {
        orig_free(*(void **)(self + COMM_OFF_SEND_BUF));
        comm_u32(self, COMM_OFF_SEND_BUF) = 0;
    }
    if (comm_u32(self, COMM_OFF_RECV_BUF)) {
        orig_free(*(void **)(self + COMM_OFF_RECV_BUF));
        comm_u32(self, COMM_OFF_RECV_BUF) = 0;
    }
    if (*dp) {
        IDirectPlayX_Release(*dp);
        *dp = NULL;
    }
    comm_u32(self, 0x3DC) = 0;

    orig_log((const char *)(uintptr_t)ADDR_STR_RELEASING_COMM);

    comm_u32(self, COMM_OFF_PLAYER_COUNT) = 1;   /* just us again */
    comm_u32(self, 0x3E4) = 0;

    ours = comm_u32(self, COMM_OFF_OUR_PLAYER_ID);

    for (i = 0; i < 4; i++) {
        uint8_t *slot = self + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;
        uint32_t id;

        comm_u32(slot, 0x00) = 0;
        comm_u32(slot, 0x64) = 0;
        comm_u32(slot, 0x68) = 0;
        comm_u32(slot, 0x50) = 0;
        comm_u32(slot, 0x4C) = 0;
        comm_u32(slot, 0x04) = (uint32_t)i;
        comm_u32(slot, 0x54) = 0;

        /* Remote players are destroyed; ours is left for the call below. */
        id = comm_u32(slot, COMM_SLOT_OFF_ID);
        if (id && id != ours)
            orig_remove_player(id);
        comm_u32(slot, COMM_SLOT_OFF_ID) = 0;

        comm_u32(slot, 0x58) = 0;
        comm_u32(slot, 0x5C) = 0;
        comm_u32(slot, 0x60) = GetTickCount();
        memset(slot + 0x0C, 0, 0x40);
    }

    orig_remove_player(ours);
    comm_u32(self, COMM_OFF_OUR_PLAYER_ID) = 0;

    if (*lobby) {
        IDirectPlayLobby_Release(*lobby);
        *lobby = NULL;
    }

    UnPauseGame(COMM_DROP_EVENT_MASK);
    g_defaultOwner      = 0;
    g_commUnknown4F48E0 = 0;
    return 0;
}

/* Start the game from a DirectPlay lobby -- 0x0040ED10, thiscall.
 *
 * The last DirectPlay function, and the one path into the game that no menu
 * reaches: another application launches it through DirectPlay, hands over the
 * connection settings it has already chosen, and the game joins whatever
 * session was arranged for it.
 *
 * The shape is ask, adjust, connect. GetConnectionSettings fetches the
 * DPLCONNECTION the launcher prepared; the session description inside it is
 * amended with this game's own requirements -- host migration, four players,
 * and dwUser1..4 set to 1, 2, 3, 4 -- and SetConnectionSettings hands it back
 * before Connect makes it real. What Connect returns is an IDirectPlay2, so
 * the very next thing is a QueryInterface for IID_IDirectPlay4A, which is the
 * interface everything else in this file uses; the intermediate is released
 * immediately.
 *
 * DPERR_NOTLOBBIED is the ordinary answer, not a failure. It means nobody
 * launched us through a lobby, and it exits quietly -- releasing the lobby
 * interface and the buffer -- where every other error is named in the log
 * first.
 *
 * Host or slave is decided by DirectPlay rather than by the launcher: GetCaps
 * is called and DPCAPS_ISHOST picked out of the flags, which is the `>> 1 & 1`
 * in the original. A slave then fetches the session description to learn the
 * player count; a host already knows it.
 *
 * The CD check on the way through is one of the five patched to unconditional,
 * so the "insert the CD" dialog after it is unreachable. Reproduced through
 * RequireGameCD like the others -- see cdcheck.h.
 *
 * NOT EXERCISABLE HERE, and not for want of trying: it needs a DirectPlay
 * lobby application to launch the game, which is a thing that no longer
 * exists. Verified by reading. */
static_assert((uint32_t)DPERR_NOTLOBBIED == 0x8877042Eu, "DPERR_NOTLOBBIED");
static_assert((uint32_t)DPERR_INVALIDINTERFACE == 0x88770406u, "DPERR_INVALIDINTERFACE");
static_assert((uint32_t)DPERR_INVALIDOBJECT == 0x88770082u, "DPERR_INVALIDOBJECT");
static_assert(DPCAPS_ISHOST == 2, "DPCAPS_ISHOST");
static_assert(sizeof(DPLCONNECTION) <= LOBBY_CONN_BUF_SIZE, "the 0x800 buffer");

typedef void (__cdecl *am2_lobby_void_fn)(void);
/* CommCreatePlayer is reconstructed; called directly below. */
#define orig_read_mp_maps    (*(am2_lobby_void_fn)ADDR_READ_MP_MAPS)
#define orig_on_lobby_slave  (*(am2_lobby_void_fn)ADDR_ON_LOBBY_SLAVE)
#define orig_apply_settings  (*(am2_lobby_void_fn)ADDR_APPLY_GAME_SETTINGS)

/* Release the lobby interface and the connection buffer, in that order. Used
 * by the quiet DPERR_NOTLOBBIED exit and by the dead copy-protection path. */
static void LobbyGiveUp(uint8_t *self)
{
    LPDIRECTPLAYLOBBY3A *lobby = (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);

    if (*lobby) {
        IDirectPlayLobby_Release(*lobby);
        *lobby = NULL;
        if (comm_u32(self, COMM_OFF_LOBBY_BUF)) {
            orig_free(*(void **)(self + COMM_OFF_LOBBY_BUF));
            comm_u32(self, COMM_OFF_LOBBY_BUF) = 0;
        }
    }
}

int32_t __attribute__((thiscall)) CommLobbyStart(void *comm)
{
    uint8_t             *self  = (uint8_t *)comm;
    LPDIRECTPLAYLOBBY3A *lobby = (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    uint8_t             *shared;
    LPDPLCONNECTION      conn;
    LPDPSESSIONDESC2     sd;
    LPDIRECTPLAY2A       dp2 = NULL;
    DPCAPS               caps;
    DWORD                size = LOBBY_CONN_BUF_SIZE;
    HRESULT              hr;
    const char          *playerName;

    comm_u32(self, COMM_OFF_LOBBY_STARTING) = 1;
    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_START);
    orig_read_mp_maps();

    /* Answers 0 or 1, never negative, so this test can only ever pass -- the
     * original's, kept. */
    if (CreateDirectPlayLobby(lobby) < 0) {
        *lobby = NULL;
        return 0;
    }

    conn = (LPDPLCONNECTION)orig_malloc(size);
    *(void **)(self + COMM_OFF_LOBBY_BUF) = conn;
    if (!conn) {
        orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_NOMEM);
        return 0;
    }

    shared = g_commObject;
    hr = IDirectPlayLobby_GetConnectionSettings(
             *(LPDIRECTPLAYLOBBY3A *)(shared + COMM_OFF_LOBBY), 0, conn, &size);
    if (hr < 0) {
        /* Nobody launched us. Ordinary, and silent. */
        if (hr == (HRESULT)DPERR_NOTLOBBIED && *lobby) {
            LobbyGiveUp(self);
            return 0;
        }
        orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_GCS_FAIL, hr);
        if (hr == (HRESULT)DPERR_BUFFERTOOSMALL)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_SMALL);
        else if (hr == (HRESULT)DPERR_INVALIDINTERFACE)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_IFACE);
        else if (hr == (HRESULT)DPERR_INVALIDOBJECT)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_OBJECT);
        else if (hr == E_INVALIDARG)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_PARAMS);
        else if (hr == E_OUTOFMEMORY)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_MEMORY);
        return 0;
    }

    /* Disabled in this build; the refusal below cannot run. */
    if (!RequireGameCD()) {
        LobbyGiveUp(self);
        return 0;
    }

    /* The launcher chose the transport; the game chooses the rest. */
    conn = *(LPDPLCONNECTION *)(self + COMM_OFF_LOBBY_BUF);
    sd = conn->lpSessionDesc;
    sd->dwUser1      = 1;
    sd->dwUser2      = 2;
    sd->dwUser3      = 3;
    sd->dwUser4      = 4;
    sd->dwFlags      = DPSESSION_MIGRATEHOST;
    sd->dwMaxPlayers = AM2_MAX_PLAYERS;

    if (IDirectPlayLobby_SetConnectionSettings(
            *(LPDIRECTPLAYLOBBY3A *)(g_commObject + COMM_OFF_LOBBY),
            0, 0, conn) < 0)
        return 0;

    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_CONNECT);
    hr = IDirectPlayLobby_Connect(
             *(LPDIRECTPLAYLOBBY3A *)(g_commObject + COMM_OFF_LOBBY),
             0, &dp2, NULL);
    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_CONNRET, hr);
    if (hr < 0)
        return 0;

    /* Connect hands back an IDirectPlay2; everything else here wants a 4A. */
    if (dp2) {
        hr = IDirectPlayX_QueryInterface(dp2, kIID_IDirectPlay4A,
                                         (LPVOID *)DirectPlaySlot(self));
        IDirectPlayX_Release(dp2);
        if (hr < 0)
            return 0;
    }

    shared = g_commObject;
    comm_u32(shared, 0x3E0) = 0;
    caps.dwSize = sizeof caps;
    IDirectPlayX_GetCaps(*DirectPlaySlot(shared), &caps, 0);
    comm_u32(shared, COMM_OFF_IS_HOST) = (caps.dwFlags >> 1) & 1;

    conn = *(LPDPLCONNECTION *)(self + COMM_OFF_LOBBY_BUF);
    playerName = conn->lpPlayerName->lpszShortNameA;

    if (!comm_u32(g_commObject, COMM_OFF_IS_HOST)) {
        /* A slave has to be told how many players there are. */
        CommGetSessionDesc(g_commObject);
        comm_u32(g_commObject, COMM_OFF_PLAYER_COUNT) =
            ((LPDPSESSIONDESC2)*(void **)(g_commObject + COMM_OFF_SESSION_DESC))
                ->dwCurrentPlayers;
    }

    if (CommCreatePlayer(g_commObject, playerName, NULL, NULL, 0) < 0)
        return 0;

    orig_log((const char *)(uintptr_t)
             (comm_u32(g_commObject, COMM_OFF_IS_HOST)
              ? ADDR_STR_LOBBY_AS_HOST : ADDR_STR_LOBBY_AS_SLAVE),
             comm_u32(g_commObject, COMM_OFF_OUR_PLAYER_ID));

    comm_u32(g_commObject, COMM_OFF_LOBBIED) = 1;
    CommMarkLobbied();

    if (!comm_u32(g_commObject, COMM_OFF_IS_HOST)) {
        orig_on_lobby_slave();
    } else {
        /* The host records its own name in the slot it occupies. */
        char *slotName = (char *)(g_commObject + COMM_SLOT_BASE
                                  + g_ourSlot * COMM_SLOT_STRIDE
                                  + COMM_SLOT_OFF_NAME);
        strcpy(slotName, playerName);
    }
    orig_apply_settings();
    return 1;
}

/* Make our player -- 0x0040DE10, thiscall, `ret 0x10`.
 *
 * A local game short-circuits it: there is no DirectPlay to tell, so the
 * hosting slot is simply marked taken with a player id of 1 and that is that.
 * Everything below is the networked case.
 *
 * The DPNAME is built on the stack and only its short name is set, which is
 * the name the player typed. hEvent defaults to the comm event when the caller
 * passes none, so a player made without one still wakes the packet thread.
 *
 * Afterwards, host and joiner diverge. A host writes its own id into its slot
 * and marks it taken; a joiner instead bumps the shared player count and says
 * so in the log. Both then mark the comm object as joined and register the id.
 *
 * Not exercised -- it needs a session, and CommLobbyStart is one of the two
 * callers, which needs a lobby. Read, not run. */
static_assert(sizeof(DPNAME) == 0x10, "DPNAME");

typedef void (__cdecl *am2_register_self_fn)(DPID id);
#define orig_register_self (*(am2_register_self_fn)ADDR_COMM_REGISTER_SELF)
#define g_defaultPlayerEvent (*(HANDLE *)(uintptr_t)ADDR_DEFAULT_PLAYER_EVT)

int32_t __attribute__((thiscall)) CommCreatePlayer(void *comm, const char *name,
                                                   HANDLE event, void *data,
                                                   DWORD length)
{
    uint8_t        *self   = (uint8_t *)comm;
    uint8_t        *shared = g_commObject;
    LPDIRECTPLAY4A  dp;
    DPNAME          who;
    HRESULT         hr;

    memset(&who, 0, sizeof who);
    who.dwSize          = sizeof who;
    who.lpszShortNameA  = (LPSTR)name;

    /* Offline: nobody to tell, so just claim the slot. */
    if (comm_u32(shared, COMM_OFF_LOCAL)) {
        comm_u32(shared, COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE
                         + COMM_SLOT_OFF_TAKEN) = 1;
        comm_u32(shared, COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE
                         + COMM_SLOT_OFF_ID) = 1;
        comm_u32(self, COMM_OFF_PLAYER_MADE) = 1;
        return 1;
    }

    dp = *DirectPlaySlot(shared);
    if (!dp)
        return 0;

    if (!event)
        event = g_defaultPlayerEvent;

    hr = IDirectPlayX_CreatePlayer(dp, (LPDPID)(self + COMM_OFF_OUR_PLAYER_ID),
                                   &who, event, data, length, 0);
    if (hr != DP_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_CREATE_PLAYER_FAIL, hr);
        return 0;
    }

    if (comm_u32(self, COMM_OFF_IS_HOST)) {
        comm_u32(self, COMM_OFF_PLAYER_MADE) = 1;
    } else {
        /* One more of us. */
        comm_u32(shared, COMM_OFF_PLAYER_COUNT) += 1;
        orig_log((const char *)(uintptr_t)ADDR_STR_NUM_PLAYERS,
                 comm_u32(shared, COMM_OFF_PLAYER_COUNT));
    }

    if (comm_u32(self, COMM_OFF_IS_HOST)) {
        comm_u32(shared, COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE
                         + COMM_SLOT_OFF_ID) =
            comm_u32(self, COMM_OFF_OUR_PLAYER_ID);
        comm_u32(shared, COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE
                         + COMM_SLOT_OFF_TAKEN) = 1;
    }

    comm_u32(shared, COMM_OFF_READY)  = 1;
    comm_u32(shared, COMM_OFF_JOINED) = 1;
    orig_register_self((DPID)comm_u32(shared, COMM_OFF_OUR_PLAYER_ID));
    return 1;
}

/* Take a packet off the wire -- 0x0040E8A0, thiscall, `ret 0x14`.
 *
 * The other half of CommSend, and it keeps the same books: a thirty-entry ring
 * of arrival times and lengths, running totals, and a maximum. The two sets of
 * fields sit in separate ranges -- receive at +0x08/+0xFC/+0x174, send at
 * +0x04/+0x0C/+0x84 -- so a busy session can be read from either direction.
 *
 * It also undoes what CommSend does. Send raises a bit in the global event
 * flags when a player falls more than thirty packets behind; this clears it
 * once they are back under fifteen. That gap is deliberate hysteresis, not an
 * inconsistency: an alarm that cleared at the same count it raised at would
 * chatter.
 *
 * A message of type 0x0B resets the sender's outstanding count outright, which
 * is what makes it an acknowledgement in all but name.
 *
 * Finally, flow control. If the message list has more than 300 entries free and
 * the paused bit is set, it is cleared and the fact logged -- the counterpart
 * of whatever set it when the list filled up. */
int32_t __attribute__((thiscall)) CommReceive(void *comm, DPID *from, DPID *to,
                                              DWORD flags, void *data,
                                              DWORD *size)
{
    uint8_t        *self = (uint8_t *)comm;
    LPDIRECTPLAY4A  dp   = *DirectPlaySlot(comm);
    uint8_t        *shared;
    uint32_t        at, now;
    int32_t         slot, i, n;

    if (!dp)
        return 0;

    if (IDirectPlayX_Receive(dp, from, to, flags, data, size) != DP_OK)
        return 0;

    at = comm_u32(self, COMM_OFF_RX_INDEX);
    comm_u32(self, COMM_OFF_RX_SIZES + at * 4) = *size;
    comm_u32(self, COMM_OFF_RX_BYTES)   += *size;
    comm_u32(self, COMM_OFF_RX_PACKETS) += 1;
    if (*size > comm_u32(self, COMM_OFF_RX_MAX))
        comm_u32(self, COMM_OFF_RX_MAX) = *size;

    now = GetTickCount();
    comm_u32(self, COMM_OFF_RX_TIMES + at * 4) = now;
    if (++at >= COMM_STAT_RING)
        at = 0;
    comm_u32(self, COMM_OFF_RX_INDEX) = at;

    slot = CommSlotOfId(comm, *from);

    /* Type 0x0B is the acknowledgement: it wipes the outstanding count. */
    if (*(uint32_t *)data == COMM_MSG_TYPE_ACK)
        comm_u32(self, COMM_SLOT_BASE + slot * COMM_SLOT_STRIDE
                       + COMM_SLOT_OFF_UNACKED) = 0;
    comm_u32(self, COMM_SLOT_BASE + slot * COMM_SLOT_STRIDE
                   + COMM_SLOT_OFF_HEARD) = now;

    /* Anyone who has caught up gets their alarm cleared. */
    shared = g_commObject;
    n = (int32_t)comm_u32(shared, COMM_OFF_PLAYER_COUNT);
    for (i = 0; i < n; i++) {
        uint32_t id = comm_u32(shared, COMM_SLOT_BASE + i * COMM_SLOT_STRIDE
                                       + COMM_SLOT_OFF_ID);
        uint32_t bit;

        if (id == 0xFFFFFFFFu)
            continue;
        if (id == comm_u32(self, COMM_OFF_OUR_PLAYER_ID))
            continue;
        if (comm_u32(self, COMM_SLOT_BASE + i * COMM_SLOT_STRIDE
                           + COMM_SLOT_OFF_UNACKED) >= COMM_UNACKED_CLEAR)
            continue;
        bit = 0x800u << i;
        if (GetPauseFlags() & bit)
            UnPauseGame(bit);
    }

    /* Room again in the message list: let the senders go. */
    {
        int32_t free = orig_msg_list_free((void *)(uintptr_t)ADDR_MSG_LIST_A);

        if (free > COMM_FLOW_FREE_OK
                && (GetPauseFlags() & COMM_FLOW_PAUSED_BIT)) {
            orig_log((const char *)(uintptr_t)ADDR_STR_FLOW_UNPAUSE, free);
            UnPauseGame(COMM_FLOW_PAUSED_BIT);
        }
    }
    return 1;
}

/* Join a session someone else is hosting -- 0x0040E7B0, thiscall.
 *
 * The counterpart of CommOpenSession: the same Open, with DPOPEN_JOIN instead
 * of DPOPEN_CREATE, and a descriptor that names which session rather than
 * describing a new one. guidInstance identifies it and guidApplication keeps
 * the answer to this game; nothing else in the descriptor is set, because
 * joining does not get to choose the rules.
 *
 * Afterwards it reads the session back and, when the host left dwUser1 clear,
 * records the current player count in the hosting slot. CommOpenSession puts
 * the host's slot index in dwUser1, so a zero there means the host did not say
 * -- and the player count is what is used instead. */
int32_t __attribute__((thiscall)) CommJoinSession(void *comm, const GUID *instance)
{
    uint8_t         *self = (uint8_t *)comm;
    LPDIRECTPLAY4A   dp   = *DirectPlaySlot(comm);
    DPSESSIONDESC2   desc;
    const GUID      *app;
    HRESULT          hr;

    if (!dp)
        return 0;

    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    if (instance)
        desc.guidInstance = *instance;
    app = *(const GUID **)(self + COMM_OFF_APP_GUID);
    if (app)
        desc.guidApplication = *app;

    hr = IDirectPlayX_Open(dp, &desc, DPOPEN_JOIN);
    if (hr != DP_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_OPEN_FAILED, hr);
        return 0;
    }

    if (!CommGetSessionDesc(comm))
        return 0;

    {
        uint8_t          *shared = g_commObject;
        LPDPSESSIONDESC2  sd =
            *(LPDPSESSIONDESC2 *)(shared + COMM_OFF_SESSION_DESC);

        if (sd->dwUser1 == 0)
            comm_u32(shared, COMM_SLOT_BASE + g_ourSlot * COMM_SLOT_STRIDE + 4)
                = sd->dwCurrentPlayers;
    }
    return 1;
}

/* Tell the lobby something -- 0x0040FAA0, thiscall.
 *
 * Only the host of a lobby-launched game says anything, which is what the
 * three guards at the top are: launched from a lobby, hosting it, and holding
 * a lobby interface.
 *
 * The message is a DPLMSG_SETPROPERTY, and it is identifiable as one without
 * guessing -- the 0x30 bytes it builds are exactly that structure's fields in
 * order. dwType is 5, which is DPLSYS_SETPROPERTY; dwRequestID is 0, which is
 * DPL_NOCONFIRMATION, so nothing is expected back; guidPlayer is the null GUID
 * the image carries at 0x0046FD98; and the property tag is the game's own,
 * carrying four bytes of value. It goes as DPLMSG_STANDARD.
 *
 * Answers non-zero when the send was accepted. */
static_assert(DPLSYS_SETPROPERTY == 5, "DPLSYS_SETPROPERTY");
static_assert(DPL_NOCONFIRMATION == 0, "DPL_NOCONFIRMATION");
static_assert(sizeof(DPLMSG_SETPROPERTY) == 0x30, "DPLMSG_SETPROPERTY");

int32_t __attribute__((thiscall)) CommSendLobbyProperty(void *comm, uint32_t value)
{
    uint8_t             *self = (uint8_t *)comm;
    LPDIRECTPLAYLOBBY3A  lobby;
    DPLMSG_SETPROPERTY   msg;

    if (!comm_u32(self, COMM_OFF_LOBBIED))
        return 0;
    if (!comm_u32(self, COMM_OFF_IS_HOST))
        return 0;
    lobby = *(LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    if (!lobby)
        return 0;

    msg.dwType          = DPLSYS_SETPROPERTY;
    msg.dwRequestID     = DPL_NOCONFIRMATION;
    msg.guidPlayer      = *(const GUID *)(uintptr_t)ADDR_GUID_NULL;
    msg.guidPropertyTag = *(const GUID *)(uintptr_t)ADDR_GUID_GAME_PROPERTY;
    msg.dwDataSize      = sizeof value;
    msg.dwPropertyData[0] = value;

    return IDirectPlayLobby_SendLobbyMessage(lobby, DPLMSG_STANDARD, 0,
                                             &msg, sizeof msg) >= 0;
}

/* Ask DirectPlay who is in the session -- 0x0040E200.
 *
 * The last DirectPlay call outside the reconstruction, and the smallest
 * remaining boundary function in the image at 128 bytes for one COM call. It
 * was ranked at 432 bytes and three places lower until tools/merges.py learned
 * to count unaligned `push imm32` operands as references: the function after
 * this one is reached only that way, so its start was invisible and its bytes
 * were charged to this one.
 *
 * Every slot is emptied before the enumeration rather than after it, because
 * the callback fills them by the running count and has no way to know which
 * were stale. A reset slot has index 0x63, no player id and an empty name.
 *
 * The callback stays original. It is the other half of the same enumeration
 * and touches no import; only the EnumPlayers call itself is boundary.
 *
 * Returns 1 when DirectPlay answered DP_OK and 0 otherwise -- the original
 * spells that `neg`/`sbb`/`inc`, which is the usual MSVC idiom for
 * `hr == 0` and not a sign that anything subtle is happening.
 *
 * NOT EXERCISED HERE, and it cannot be without AM2_MULTIPLAYER=1: with the
 * button patched out the whole DirectPlay subsystem is unreachable. Verified
 * by reading, like the rest of dplay.cpp. */
int32_t __cdecl CommEnumPlayers(void)
{
    uint8_t       *comm = g_commObject;
    LPDIRECTPLAY4A dp;
    int32_t        i;

    dp = *(LPDIRECTPLAY4A *)(comm + COMM_OFF_DPLAY);
    if (!dp)
        return 0;

    for (i = 0; i < 4; i++) {
        uint8_t *slot = comm + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;

        comm_u32(slot, COMM_SLOT_OFF_INDEX) = COMM_SLOT_INDEX_NONE;
        comm_u32(slot, COMM_SLOT_OFF_ID)    = 0;
        slot[COMM_SLOT_OFF_NAME]            = 0;
    }
    g_commEnumCount = 0;

    return IDirectPlayX_EnumPlayers(dp, *(GUID **)comm,
                                    (LPDPENUMPLAYERSCALLBACK2)
                                        (uintptr_t)ADDR_ENUM_PLAYERS_CB,
                                    (LPVOID)g_hwnd, 0) == DP_OK;
}

/* Bring the packet subsystem up -- 0x004021A0.
 *
 * The last boundary work in the image, and it was hidden by a classification
 * rather than by being hard: CreateThread, CreateEventA, CreateMutexA and
 * SetThreadPriority were all on coverage.py's incidental list, so a function
 * that starts a thread counted as game logic. Creating an OS object is the same
 * kind of act as creating a DirectDraw surface.
 *
 * Named from its own error string, "Error launching packet thread". It went
 * into orig.h as ADDR_COMM_INIT_SYNC with the note "mirrors CommShutdown",
 * which is where it is called from and not what it does.
 *
 * Four message lists, then 400 records each owning a kilobyte of buffer, then
 * two events, then the thread. The buffers are filled with rand() after
 * srand(0) -- a fixed seed, so the contents are the same every run, which is
 * the only reason that is not simply strange. Transcribed as written.
 *
 * EVERY FAILURE RETURNS EARLY WITHOUT UNDOING ANYTHING. A list that cannot get
 * its mutex leaves the earlier ones created and returns, and the caller --
 * CommConstruct -- ignores the answer entirely. Kept: it is the original's, and
 * the alternative would be inventing a teardown path that does not exist.
 *
 * The thread procedure at 0x00401F00 stays original. It is the packet pump and
 * it is game logic; what is boundary here is asking the OS for the thread. */
static_assert(sizeof(HANDLE) == 4, "a 32-bit handle");

#define g_packetRecords ((uint8_t *)(uintptr_t)ADDR_PACKET_RECORDS)
#define g_packetThread  (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)
#define g_packetState   (*(int32_t *)(uintptr_t)ADDR_PACKET_STATE)

typedef void (__cdecl *am2_slot_reset_fn)(int32_t slot);
typedef void (__cdecl *am2_srand_fn)(uint32_t seed);
typedef int32_t (__cdecl *am2_rand_fn)(void);

#define orig_slot_reset (*(am2_slot_reset_fn)ADDR_PACKET_SLOT_RESET)
#define orig_srand      (*(am2_srand_fn)ADDR_GAME_SRAND)
#define orig_rand       (*(am2_rand_fn)ADDR_GAME_RAND)

/* 0x00401000. Clear the list and give it a mutex. Answers 0 if the mutex
 * could not be had, and leaves the list zeroed either way. */
int32_t __cdecl MsgListInit(void *list)
{
    uint32_t *l = (uint32_t *)list;

    l[1] = 0;
    l[2] = 0;
    l[3] = 0;
    l[0] = (uint32_t)(uintptr_t)CreateMutexA(NULL, FALSE, NULL);
    return l[0] != 0;
}

/* 0x00402170. Close a handle and forget it, along with two fields beside it.
 * Safe on a holder that never got one. */
void __cdecl EventClose(void *holder)
{
    uint32_t *h = (uint32_t *)holder;
    HANDLE    handle = (HANDLE)(uintptr_t)h[0];

    h[1] = 0;
    h[2] = 0;
    if (handle)
        CloseHandle(handle);
    h[0] = 0;
}

int32_t __cdecl StartPacketThread(void)
{
    uint8_t *rec  = g_packetRecords;
    uint8_t *data = (uint8_t *)(uintptr_t)ADDR_PACKET_BUFFERS;
    int32_t  i;

    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_POOL)) return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_B))    return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_C))    return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_D))    return 0;

    /* A fixed seed, so every run fills the buffers identically. */
    orig_srand(0);

    while (data < (uint8_t *)(uintptr_t)ADDR_PACKET_BUFFERS_END) {
        uint32_t *w = (uint32_t *)data;

        *(uint32_t *)(rec + PACKET_REC_OFF_SIZE) = PACKET_BUFFER_BYTES;
        *(uint32_t **)(rec + PACKET_REC_OFF_DATA) = w;

        for (i = 0; i < (int32_t)(PACKET_BUFFER_BYTES / 4); i++)
            *w++ = (uint32_t)orig_rand();

        MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_POOL, rec);
        rec  += PACKET_RECORD_STRIDE;
        data  = (uint8_t *)w;
    }

    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_A =
        CreateEventA(NULL, FALSE, FALSE, NULL);
    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B =
        CreateEventA(NULL, FALSE, FALSE, NULL);
    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B2 =
        *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B;

    g_packetState = 2;
    for (i = 0; i < 6; i++)
        orig_slot_reset(i);

    g_packetThread = CreateThread(NULL, 0,
                                  (LPTHREAD_START_ROUTINE)
                                      (uintptr_t)ADDR_PACKET_THREAD_PROC,
                                  NULL, 0,
                                  (LPDWORD)(uintptr_t)ADDR_PACKET_THREAD_ID);
    if (!g_packetThread) {
        orig_log((const char *)(uintptr_t)ADDR_STR_THREAD_FAILED, 0, 0, 0);
        return 0;
    }
    return SetThreadPriority(g_packetThread, THREAD_PRIORITY_HIGHEST) != 0;
}
/* The CRT clock and the state request this reaches, both still original. */
typedef int32_t (__cdecl *AM2_CrtTimeFn)(int32_t *out);
#define orig_crt_time      (*(AM2_CrtTimeFn)(uintptr_t)ADDR_CRT_TIME)
typedef void (__cdecl *AM2_RequestStateFn)(int32_t state);

/* The two widget slots the lobby repaint reaches, and the comm fields this
 * shares with msgslot.cpp. */
typedef void (__attribute__((thiscall)) *AM2_DlgUpdateFn2)(void *w);
#define AM2_START_COMM_LOG   0x418
#define AM2_START_SELF_ID    0x3CC

void __cdecl SendGameStartMsg(void)
{
    uint8_t *comm;
    uint8_t *msg = (uint8_t *)(uintptr_t)ADDR_MSG_GAME_START;

    /* NOT gated on the verbosity field, unlike the rest of this group. */
    orig_log("SendGameStartMsg\n");

    comm = (uint8_t *)(*(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT);

    if (!*(const int32_t *)(comm + COMM_OFF_STARTED)) {
        int32_t seed;
        void   *desc;

        /* Host only; a client does nothing at all, not even the tail. */
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            return;

        *(int32_t *)(msg + 8) = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(int32_t *)(msg + 12) = *(const int32_t *)(comm + AM2_START_SELF_ID);

        /* The shared seed: read once here and sent, so every machine agrees. */
        seed = orig_crt_time((int32_t *)0);
        *(int32_t *)(uintptr_t)ADDR_GAME_SEED      = seed;
        *(int32_t *)(uintptr_t)ADDR_GAME_SEED_SENT = seed;

        if (*(const int32_t *)(comm + AM2_START_COMM_LOG))
            /* "Seed is %d" with a literal 0 -- the original never prints the
             * seed it just chose. Kept. */
            orig_log("SendGameStartMsg for %d  Players: Seed is %d \n",
                    *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT), 0);

        CommGetSessionDesc(comm);

        desc = *(void **)(comm + COMM_OFF_SESSION_BUF);
        *(uint32_t *)((uint8_t *)desc + 4) |= AM2_SESSION_FLAGS_START;
        CommSetSessionDesc(comm, desc, 0);

        CommSend(comm, 0, 1, msg, (uint32_t)*(const int32_t *)(msg + 4));
    }

    /* Reached by the host AND by the already-started case. */
    SendGamePause(1, 0x10000);
    RequestState(2);
    *(int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE = 1;
    *(int32_t *)(uintptr_t)ADDR_NET_GAME         = 1;
}




/* The two names on this disagree and neither is settled from the body, which
 * is one store. orig.h calls the function ADDR_COMM_MARK_LOBBIED, from its one
 * caller; it calls +0x404 COMM_OFF_MSGS_ENABLED, from somewhere else. The
 * offset's name is the one already used elsewhere in the tree, so it is the
 * one used here -- adding a third would be the mistake checkglobals exists to
 * stop. */
void __cdecl CommMarkLobbied(void)
{
    *(int32_t *)(g_commObject + COMM_OFF_MSGS_ENABLED) = 1;
}

void __attribute__((thiscall)) CommSessionOver(void *comm)
{
    CommSendLobbyProperty(comm, 1);
}

typedef int32_t (__attribute__((thiscall)) *AM2_CommSlotOfIdFn)(void *comm,
                                                                uint32_t id);
#define orig_comm_slot_of_id \
    ((AM2_CommSlotOfIdFn)(uintptr_t)ADDR_COMM_SLOT_OF_ID)

int32_t __attribute__((thiscall)) CommPlayerSlot(void *comm, uint32_t id)
{
    return orig_comm_slot_of_id(comm, id);
}

int32_t __attribute__((thiscall)) CommDropSession(void *comm)
{
    *(int32_t *)(g_commObject + COMM_OFF_MSGS_ENABLED) = 0;
    return CommDropDirectPlay(comm);
}

#define COMM_OFF_PLAYER_COUNT 0x3D0u

int32_t __attribute__((thiscall)) CommSlotOfId(void *comm, uint32_t id)
{
    const uint8_t *p = g_commObject + AM2_PLAYER_ID;
    int32_t        n = *(const int32_t *)(g_commObject + COMM_OFF_PLAYER_COUNT);
    int32_t        i;

    (void)comm;   /* the original ignores `this` and uses the global */
    for (i = 0; i < n; i++, p += AM2_PLAYER_STRIDE)
        if (*(const uint32_t *)p == id)
            return i;
    return 0;
}

int dplay_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_COMM_CREATE_DPLAY, (const void *)CommCreateDirectPlay,
                        "CommCreateDirectPlay", 1);
    rc |= patch_replace(ADDR_CREATE_LOBBY, (const void *)CreateDirectPlayLobby,
                        "CreateDirectPlayLobby", 1);
    rc |= patch_replace(ADDR_COMM_SHUTDOWN, (const void *)CommShutdown,
                        "CommShutdown", 0);
    rc |= patch_replace(ADDR_COMM_CLOSE, (const void *)CommClose, "CommClose", 0);
    rc |= patch_replace(ADDR_COMM_INIT_CONN, (const void *)CommInitializeConnection,
                        "CommInitializeConnection", 1);
    rc |= patch_replace(ADDR_COMM_SET_SESSION, (const void *)CommSetSessionDesc,
                        "CommSetSessionDesc", 2);
    rc |= patch_replace(ADDR_COMM_GET_SESSION, (const void *)CommGetSessionDesc,
                        "CommGetSessionDesc", 0);
    rc |= patch_replace(ADDR_START_PACKET_THREAD, (const void *)StartPacketThread,
                        "StartPacketThread", 0);
    rc |= patch_replace(ADDR_MSG_LIST_INIT, (const void *)MsgListInit,
                        "MsgListInit", 1);
    rc |= patch_replace(ADDR_EVENT_CLOSE, (const void *)EventClose,
                        "EventClose", 1);
    rc |= patch_replace(ADDR_COMM_ENUM_PLAYERS, (const void *)CommEnumPlayers,
                        "CommEnumPlayers", 0);
    rc |= patch_replace(ADDR_COMM_CONSTRUCT, (const void *)CommConstruct,
                        "CommConstruct", 0);
    rc |= patch_replace(ADDR_COMM_DESTRUCT, (const void *)CommDestruct,
                        "CommDestruct", 0);
    rc |= patch_replace(ADDR_COMM_ENUM_SESSIONS, (const void *)CommEnumSessions,
                        "CommEnumSessions", 1);
    rc |= patch_replace(ADDR_COMM_ENUM_CONNECTIONS, (const void *)CommEnumConnections,
                        "CommEnumConnections", 1);
    rc |= patch_replace(ADDR_SEND_GAME_START, (const void *)SendGameStartMsg,
                        "SendGameStartMsg", 3);
    rc |= patch_replace(ADDR_COMM_SEND, (const void *)CommSend, "CommSend", 4);
    rc |= patch_replace(ADDR_COMM_OPEN_SESSION, (const void *)CommOpenSession,
                        "CommOpenSession", 1);
    rc |= patch_replace(ADDR_COMM_CONNECTED, (const void *)CommOnConnected,
                        "CommOnConnected", 0);
    rc |= patch_replace(ADDR_COMM_DROP_DPLAY, (const void *)CommDropDirectPlay,
                        "CommDropDirectPlay", 0);
    rc |= patch_replace(ADDR_COMM_LOBBY_START, (const void *)CommLobbyStart,
                        "CommLobbyStart", 0);
    rc |= patch_replace(ADDR_COMM_JOIN_SESSION, (const void *)CommJoinSession,
                        "CommJoinSession", 1);
    rc |= patch_replace(ADDR_COMM_RECEIVE, (const void *)CommReceive,
                        "CommReceive", 5);
    rc |= patch_replace(ADDR_COMM_CREATE_PLAYER, (const void *)CommCreatePlayer,
                        "CommCreatePlayer", 4);
    rc |= patch_replace(ADDR_COMM_SLOT_OF_ID, (const void *)CommSlotOfId,
                        "CommSlotOfId", 2);
    rc |= patch_replace(ADDR_COMM_PLAYER_SLOT, (const void *)CommPlayerSlot,
                        "CommPlayerSlot", 2);
    rc |= patch_replace(ADDR_COMM_DROP_SESSION, (const void *)CommDropSession,
                        "CommDropSession", 1);
    rc |= patch_replace(ADDR_COMM_MARK_LOBBIED, (const void *)CommMarkLobbied,
                        "CommMarkLobbied", 0);
    rc |= patch_replace(ADDR_COMM_SESSION_OVER, (const void *)CommSessionOver,
                        "CommSessionOver", 1);
    rc |= patch_replace(ADDR_COMM_SEND_PROPERTY, (const void *)CommSendLobbyProperty,
                        "CommSendLobbyProperty", 1);
    return rc;
}
