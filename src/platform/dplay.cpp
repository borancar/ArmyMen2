/* dplay.cpp -- DirectPlay objects that decline everything.
 *
 * The game creates two COM objects: an IDirectPlay4A it points at a
 * service provider, and an IDirectPlayLobby3A it asks whether it was
 * launched from a lobby. Both exist here so the comm layer's startup and
 * teardown run as they do on a machine with DirectPlay installed and no
 * network provider configured: InitializeConnection fails, the lobby is
 * not lobbied, and every enumeration finds nothing. No transport is
 * emulated and none will be.
 */
#include "platform.h"
#include <dplobby.h>

#include <stdlib.h>
#include <string.h>

AM2_DEFINE_GUID_VALUE(CLSID_DirectPlay,       0xD1EB6D20, 0x8923, 0x11D0, 0x9D, 0x97, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB);
AM2_DEFINE_GUID_VALUE(IID_IDirectPlay4A,      0x133EFE41, 0x32DC, 0x11D0, 0x9C, 0xFB, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB);
AM2_DEFINE_GUID_VALUE(CLSID_DirectPlayLobby,  0x2FE8F810, 0xB2A5, 0x11D0, 0xA7, 0x87, 0x00, 0x00, 0xF8, 0x03, 0xAB, 0xFC);
AM2_DEFINE_GUID_VALUE(IID_IDirectPlayLobby3A, 0x1BB4AF80, 0xA303, 0x11D0, 0x9C, 0x4F, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);

/* ---- IDirectPlay4 ------------------------------------------------------------- */

typedef struct AM2_DPlay {
    const IDirectPlay4Vtbl *lpVtbl;
    int32_t refs;
} AM2_DPlay;

static HRESULT STDMETHODCALLTYPE DP_QueryInterface(IDirectPlay4 *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectPlay4A)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DP_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DP_AddRef(IDirectPlay4 *p) { return (ULONG)++((AM2_DPlay *)p)->refs; }

static ULONG STDMETHODCALLTYPE DP_Release(IDirectPlay4 *p)
{
    AM2_DPlay *d = (AM2_DPlay *)p;
    int32_t    n = --d->refs;

    if (n <= 0) {
        free(d);
        return 0;
    }
    return (ULONG)n;
}

/* Every other method: not connected, nothing to do. One body under many
 * signatures, which is legal for stdcall because the CALLEE pops -- so
 * the shared body must not be reached through a mismatched prototype.
 * Each slot therefore gets its own thunk of the right arity. */
#define DP_DECLINE(name, ...) \
    static HRESULT STDMETHODCALLTYPE DP_##name(IDirectPlay4 *, ##__VA_ARGS__) { return DPERR_NOCONNECTION; }

DP_DECLINE(AddPlayerToGroup, DPID, DPID)
static HRESULT STDMETHODCALLTYPE DP_Close(IDirectPlay4 *p) { (void)p; return DP_OK; }
DP_DECLINE(CreateGroup, LPDPID, LPDPNAME, LPVOID, DWORD, DWORD)
DP_DECLINE(CreatePlayer, LPDPID, LPDPNAME, HANDLE, LPVOID, DWORD, DWORD)
DP_DECLINE(DeletePlayerFromGroup, DPID, DPID)
DP_DECLINE(DestroyGroup, DPID)
DP_DECLINE(DestroyPlayer, DPID)
DP_DECLINE(EnumGroupPlayers, DPID, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD)
DP_DECLINE(EnumGroups, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD)
DP_DECLINE(EnumPlayers, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD)
static HRESULT STDMETHODCALLTYPE DP_EnumSessions(IDirectPlay4 *p, LPDPSESSIONDESC2 a, DWORD b,
                                                 LPDPENUMSESSIONSCALLBACK2 c, LPVOID d, DWORD e)
{
    (void)p; (void)a; (void)b; (void)c; (void)d; (void)e;
    return DPERR_NOSESSIONS;
}
DP_DECLINE(GetCaps, LPDPCAPS, DWORD)
DP_DECLINE(GetGroupData, DPID, LPVOID, LPDWORD, DWORD)
DP_DECLINE(GetGroupName, DPID, LPVOID, LPDWORD)
DP_DECLINE(GetMessageCount, DPID, LPDWORD)
DP_DECLINE(GetPlayerAddress, DPID, LPVOID, LPDWORD)
DP_DECLINE(GetPlayerCaps, DPID, LPDPCAPS, DWORD)
DP_DECLINE(GetPlayerData, DPID, LPVOID, LPDWORD, DWORD)
DP_DECLINE(GetPlayerName, DPID, LPVOID, LPDWORD)
DP_DECLINE(GetSessionDesc, LPVOID, LPDWORD)
DP_DECLINE(Initialize, LPGUID)
DP_DECLINE(Open, LPDPSESSIONDESC2, DWORD)
static HRESULT STDMETHODCALLTYPE DP_Receive(IDirectPlay4 *p, LPDPID a, LPDPID b, DWORD c,
                                            LPVOID d, LPDWORD e)
{
    (void)p; (void)a; (void)b; (void)c; (void)d; (void)e;
    return DPERR_NOMESSAGES;
}
DP_DECLINE(Send, DPID, DPID, DWORD, LPVOID, DWORD)
DP_DECLINE(SetGroupData, DPID, LPVOID, DWORD, DWORD)
DP_DECLINE(SetGroupName, DPID, LPDPNAME, DWORD)
DP_DECLINE(SetPlayerData, DPID, LPVOID, DWORD, DWORD)
DP_DECLINE(SetPlayerName, DPID, LPDPNAME, DWORD)
DP_DECLINE(SetSessionDesc, LPDPSESSIONDESC2, DWORD)
DP_DECLINE(AddGroupToGroup, DPID, DPID)
DP_DECLINE(CreateGroupInGroup, DPID, LPDPID, LPDPNAME, LPVOID, DWORD, DWORD)
DP_DECLINE(DeleteGroupFromGroup, DPID, DPID)
static HRESULT STDMETHODCALLTYPE DP_EnumConnections(IDirectPlay4 *p, LPCGUID a,
                                                    LPDPENUMCONNECTIONSCALLBACK b, LPVOID c, DWORD d)
{
    (void)p; (void)a; (void)b; (void)c; (void)d;
    return DP_OK;                      /* no providers to enumerate */
}
DP_DECLINE(EnumGroupsInGroup, DPID, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD)
DP_DECLINE(GetGroupConnectionSettings, DWORD, DPID, LPVOID, LPDWORD)
static HRESULT STDMETHODCALLTYPE DP_InitializeConnection(IDirectPlay4 *p, LPVOID a, DWORD b)
{
    (void)p; (void)a; (void)b;
    return DPERR_UNAVAILABLE;
}
DP_DECLINE(SecureOpen, LPCDPSESSIONDESC2, DWORD, LPCVOID, LPCVOID)
DP_DECLINE(SendChatMessage, DPID, DPID, DWORD, LPVOID)
DP_DECLINE(SetGroupConnectionSettings, DWORD, DPID, LPVOID)
DP_DECLINE(StartSession, DWORD, DPID)
DP_DECLINE(GetGroupFlags, DPID, LPDWORD)
DP_DECLINE(GetGroupParent, DPID, LPDPID)
DP_DECLINE(GetPlayerAccount, DPID, DWORD, LPVOID, LPDWORD)
DP_DECLINE(GetPlayerFlags, DPID, LPDWORD)
DP_DECLINE(GetGroupOwner, DPID, LPDPID)
DP_DECLINE(SetGroupOwner, DPID, DPID)
DP_DECLINE(SendEx, DPID, DPID, DWORD, LPVOID, DWORD, DWORD, DWORD, LPVOID, LPDWORD)
DP_DECLINE(GetMessageQueue, DPID, DPID, DWORD, LPDWORD, LPDWORD)
DP_DECLINE(CancelMessage, DWORD, DWORD)
DP_DECLINE(CancelPriority, DWORD, DWORD, DWORD)

static const IDirectPlay4Vtbl am2_dplay_vtbl = {
    DP_QueryInterface, DP_AddRef, DP_Release,
    DP_AddPlayerToGroup, DP_Close, DP_CreateGroup, DP_CreatePlayer,
    DP_DeletePlayerFromGroup, DP_DestroyGroup, DP_DestroyPlayer, DP_EnumGroupPlayers,
    DP_EnumGroups, DP_EnumPlayers, DP_EnumSessions, DP_GetCaps, DP_GetGroupData,
    DP_GetGroupName, DP_GetMessageCount, DP_GetPlayerAddress, DP_GetPlayerCaps,
    DP_GetPlayerData, DP_GetPlayerName, DP_GetSessionDesc, DP_Initialize, DP_Open,
    DP_Receive, DP_Send, DP_SetGroupData, DP_SetGroupName, DP_SetPlayerData,
    DP_SetPlayerName, DP_SetSessionDesc,
    DP_AddGroupToGroup, DP_CreateGroupInGroup, DP_DeleteGroupFromGroup, DP_EnumConnections,
    DP_EnumGroupsInGroup, DP_GetGroupConnectionSettings, DP_InitializeConnection,
    DP_SecureOpen, DP_SendChatMessage, DP_SetGroupConnectionSettings, DP_StartSession,
    DP_GetGroupFlags, DP_GetGroupParent, DP_GetPlayerAccount, DP_GetPlayerFlags,
    DP_GetGroupOwner, DP_SetGroupOwner, DP_SendEx, DP_GetMessageQueue, DP_CancelMessage,
    DP_CancelPriority,
};

/* ---- IDirectPlayLobby3 ------------------------------------------------------------ */

typedef struct AM2_Lobby {
    const IDirectPlayLobby3Vtbl *lpVtbl;
    int32_t refs;
} AM2_Lobby;

static HRESULT STDMETHODCALLTYPE LB_QueryInterface(IDirectPlayLobby3 *p, REFIID iid, LPVOID *out)
{
    if (IsEqualGUID(iid, IID_IDirectPlayLobby3A)) {
        *out = p;
        p->lpVtbl->AddRef(p);
        return DP_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE LB_AddRef(IDirectPlayLobby3 *p) { return (ULONG)++((AM2_Lobby *)p)->refs; }

static ULONG STDMETHODCALLTYPE LB_Release(IDirectPlayLobby3 *p)
{
    AM2_Lobby *l = (AM2_Lobby *)p;
    int32_t    n = --l->refs;

    if (n <= 0) {
        free(l);
        return 0;
    }
    return (ULONG)n;
}

#define LB_DECLINE(name, ...) \
    static HRESULT STDMETHODCALLTYPE LB_##name(IDirectPlayLobby3 *, ##__VA_ARGS__) { return DPERR_NOTLOBBIED; }

LB_DECLINE(Connect, DWORD, LPDIRECTPLAY4 *, IUnknown *)
LB_DECLINE(CreateAddress, REFGUID, REFGUID, LPCVOID, DWORD, LPVOID, LPDWORD)
LB_DECLINE(EnumAddress, LPVOID, LPCVOID, DWORD, LPVOID)
LB_DECLINE(EnumAddressTypes, LPVOID, REFGUID, LPVOID, DWORD)
LB_DECLINE(EnumLocalApplications, LPVOID, LPVOID, DWORD)
LB_DECLINE(GetConnectionSettings, DWORD, LPVOID, LPDWORD)
LB_DECLINE(ReceiveLobbyMessage, DWORD, DWORD, LPDWORD, LPVOID, LPDWORD)
LB_DECLINE(RunApplication, DWORD, LPDWORD, LPDPLCONNECTION, HANDLE)
LB_DECLINE(SendLobbyMessage, DWORD, DWORD, LPVOID, DWORD)
LB_DECLINE(SetConnectionSettings, DWORD, DWORD, LPDPLCONNECTION)
LB_DECLINE(SetLobbyMessageEvent, DWORD, DWORD, HANDLE)
LB_DECLINE(CreateCompoundAddress, LPCVOID, DWORD, LPVOID, LPDWORD)
LB_DECLINE(ConnectEx, DWORD, REFIID, LPVOID *, IUnknown *)
LB_DECLINE(RegisterApplication, DWORD, LPVOID)
LB_DECLINE(UnregisterApplication, DWORD, REFGUID)
LB_DECLINE(WaitForConnectionSettings, DWORD)

static const IDirectPlayLobby3Vtbl am2_lobby_vtbl = {
    LB_QueryInterface, LB_AddRef, LB_Release, LB_Connect, LB_CreateAddress,
    LB_EnumAddress, LB_EnumAddressTypes, LB_EnumLocalApplications,
    LB_GetConnectionSettings, LB_ReceiveLobbyMessage, LB_RunApplication,
    LB_SendLobbyMessage, LB_SetConnectionSettings, LB_SetLobbyMessageEvent,
    LB_CreateCompoundAddress, LB_ConnectEx, LB_RegisterApplication,
    LB_UnregisterApplication, LB_WaitForConnectionSettings,
};

/* ---- CoCreateInstance -------------------------------------------------------------- */

HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD ctx,
                                REFIID iid, LPVOID *out)
{
    (void)ctx;
    if (out)
        *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (IsEqualGUID(clsid, CLSID_DirectPlay)) {
        AM2_DPlay *d;
        if (!IsEqualGUID(iid, IID_IDirectPlay4A))
            return E_NOINTERFACE;
        d = (AM2_DPlay *)calloc(1, sizeof *d);
        if (!d)
            return E_OUTOFMEMORY;
        d->lpVtbl = &am2_dplay_vtbl;
        d->refs = 1;
        *out = d;
        return S_OK;
    }
    if (IsEqualGUID(clsid, CLSID_DirectPlayLobby)) {
        AM2_Lobby *l;
        if (!IsEqualGUID(iid, IID_IDirectPlayLobby3A))
            return E_NOINTERFACE;
        l = (AM2_Lobby *)calloc(1, sizeof *l);
        if (!l)
            return E_OUTOFMEMORY;
        l->lpVtbl = &am2_lobby_vtbl;
        l->refs = 1;
        *out = l;
        return S_OK;
    }
    return REGDB_E_CLASSNOTREG;
}
