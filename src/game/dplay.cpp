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
#define orig_drop_directplay (*(am2_comm_method_fn)ADDR_COMM_DROP_DPLAY)
#define orig_comm_connected  (*(am2_comm_method_fn)ADDR_COMM_CONNECTED)

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

int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection)
{
    LPDIRECTPLAY4A *slot = DirectPlaySlot(comm);
    HRESULT         hr;

    /* Whatever is there now goes first; creating a second one over the top
     * would leak the interface and the socket underneath it. */
    if (*slot)
        orig_drop_directplay(comm);

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
        orig_comm_connected(comm);
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

typedef void *(__cdecl *am2_malloc_fn)(size_t);
typedef void  (__cdecl *am2_free_fn)(void *);
#define orig_malloc (*(am2_malloc_fn)ADDR_GAME_MALLOC)
#define orig_free   (*(am2_free_fn)ADDR_GAME_FREE)

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
    return rc;
}
