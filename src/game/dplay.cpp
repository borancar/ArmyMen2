/* DirectPlay object creation -- reconstructed from ArmyMen2.exe.
 *
 *   CommCreateDirectPlay    0x0040DD20  1 call site, thiscall
 *   CreateDirectPlayLobby   0x0040DDD0  1 call site, stdcall
 *
 * These are the only two CoCreateInstance sites in the game and, between them,
 * its entire outward network surface. See dplay.h for why that surface is two
 * functions long.
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

int dplay_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_COMM_CREATE_DPLAY, (const void *)CommCreateDirectPlay,
                        "CommCreateDirectPlay", 1);
    rc |= patch_replace(ADDR_CREATE_LOBBY, (const void *)CreateDirectPlayLobby,
                        "CreateDirectPlayLobby", 1);
    return rc;
}
