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

#include "dplay.h"
#include "../inject/patch.h"

#include <stdint.h>

static_assert(CLSCTX_INPROC_SERVER == 1, "CLSCTX_INPROC_SERVER");

#define kCLSID_DirectPlay      (*(const CLSID *)(uintptr_t)ADDR_CLSID_DIRECTPLAY)
#define kIID_IDirectPlay4A     (*(const IID *)(uintptr_t)ADDR_IID_DIRECTPLAY4A)
#define kCLSID_DirectPlayLobby (*(const CLSID *)(uintptr_t)ADDR_CLSID_DPLAY_LOBBY)
#define kIID_IDirectPlayLobby3A (*(const IID *)(uintptr_t)ADDR_IID_DPLAY_LOBBY3A)

/* Both take the comm object in ecx and nothing else. */
typedef void (__attribute__((thiscall)) *am2_comm_method_fn)(void *comm);

typedef void (__cdecl *am2_destroy_list_fn)(void *list);
#define orig_destroy_msg_list (*(am2_destroy_list_fn)ADDR_DESTROY_MSG_LIST)

#define g_commEvent    (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT)
#define g_commEvent2   (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT_2)
#define g_packetThread (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)

/* The interface pointer lives inside the comm object rather than in a global,
 * so it is reached through `this` like any other member. */
static inline LPDIRECTPLAY4A *DirectPlaySlot(void *comm)
{
    return (LPDIRECTPLAY4A *)((uint8_t *)comm + COMM_OFF_DPLAY);
}

/* Both defined further down; called from here. */
int32_t __attribute__((thiscall)) CommOnConnected(void *self);
int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm);

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

    orig_destroy_msg_list((void *)(uintptr_t)ADDR_MSG_LIST_A);
    orig_destroy_msg_list((void *)(uintptr_t)ADDR_MSG_LIST_B);
    orig_destroy_msg_list((void *)(uintptr_t)ADDR_MSG_LIST_C);
    orig_destroy_msg_list((void *)(uintptr_t)ADDR_MSG_LIST_D);

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
#define orig_comm_init_sync     (*(am2_comm_void_fn)ADDR_COMM_INIT_SYNC)
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

    orig_comm_init_sync();
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
typedef uint32_t (__cdecl *am2_get_flags_fn)(void);
typedef void (__cdecl *am2_set_flags_fn)(uint32_t bits);

#define orig_find_player (*(am2_find_player_fn)ADDR_FIND_PLAYER_BY_ID)
#define orig_get_flags   (*(am2_get_flags_fn)ADDR_GET_EVENT_FLAGS)
#define orig_set_flags   (*(am2_set_flags_fn)ADDR_SET_EVENT_FLAGS)
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
        if (!orig_find_player(id))
            continue;

        bit = 0x800u << i;
        if (++comm_u32(slot, COMM_SLOT_OFF_UNACKED) <= COMM_STAT_RING)
            continue;
        if (orig_get_flags() & bit)
            continue;
        orig_set_flags(bit);
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

#define g_hostSlot     (*(int32_t *)(uintptr_t)ADDR_HOST_SLOT)
#define g_joinContext  (*(int32_t *)(uintptr_t)ADDR_JOIN_CONTEXT)
#define g_defaultOwner (*(int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)

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
        /* The hosting slot's index field, carried across as user data. */
        desc.dwUser1 = comm_u32((uint8_t *)self,
                                COMM_SLOT_BASE + g_hostSlot * COMM_SLOT_STRIDE + 4);

        app = *(const GUID **)(comm + COMM_OFF_APP_GUID);
        if (app)
            desc.guidApplication = *app;

        if (IDirectPlayX_Open(dp, &desc, DPOPEN_CREATE) != DP_OK)
            return 0;
    }

    g_joinContext  = 0;
    g_defaultOwner = 0;
    g_hostSlot     = 0;
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
typedef void (__cdecl *am2_clear_flags_fn)(uint32_t bits);
#define orig_remove_player     (*(am2_remove_player_fn)ADDR_REMOVE_PLAYER)
#define orig_clear_event_flags (*(am2_clear_flags_fn)ADDR_CLEAR_EVENT_FLAGS)
#define g_defaultOwnerSlot     (*(int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
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

    orig_clear_event_flags(COMM_DROP_EVENT_MASK);
    g_defaultOwnerSlot  = 0;
    g_commUnknown4F48E0 = 0;
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
    rc |= patch_replace(ADDR_COMM_CONSTRUCT, (const void *)CommConstruct,
                        "CommConstruct", 0);
    rc |= patch_replace(ADDR_COMM_DESTRUCT, (const void *)CommDestruct,
                        "CommDestruct", 0);
    rc |= patch_replace(ADDR_COMM_ENUM_SESSIONS, (const void *)CommEnumSessions,
                        "CommEnumSessions", 1);
    rc |= patch_replace(ADDR_COMM_ENUM_CONNECTIONS, (const void *)CommEnumConnections,
                        "CommEnumConnections", 1);
    rc |= patch_replace(ADDR_COMM_SEND, (const void *)CommSend, "CommSend", 4);
    rc |= patch_replace(ADDR_COMM_OPEN_SESSION, (const void *)CommOpenSession,
                        "CommOpenSession", 1);
    rc |= patch_replace(ADDR_COMM_CONNECTED, (const void *)CommOnConnected,
                        "CommOnConnected", 0);
    rc |= patch_replace(ADDR_COMM_DROP_DPLAY, (const void *)CommDropDirectPlay,
                        "CommDropDirectPlay", 0);
    return rc;
}
