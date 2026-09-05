/* dplay.h -- DirectPlay 4 as the game sees it. The platform layer provides
 * no transport: CoCreateInstance hands back an object every method of which
 * declines, which is what a machine with no network provider looks like to
 * the original. The types are here so dplay.cpp and the comm layer compile
 * unchanged. Layouts are the SDK's (DPSESSIONDESC2 is 80 bytes). */
#ifndef AM2_PLATFORM_DPLAY_H
#define AM2_PLATFORM_DPLAY_H

#include <windows.h>

AM2_EXTERN_C_BEGIN

typedef struct IDirectPlay4 IDirectPlay4, *LPDIRECTPLAY4, *LPDIRECTPLAY4A,
                            *LPDIRECTPLAY2A, *LPDIRECTPLAY3A;
typedef DWORD DPID, *LPDPID;

extern const GUID CLSID_DirectPlay;
extern const GUID IID_IDirectPlay4A;

#define _FACDP 0x877
#define MAKE_DPHRESULT(code) MAKE_HRESULT(1, _FACDP, code)
#define DP_OK                    S_OK
#define DPERR_ALREADYINITIALIZED MAKE_DPHRESULT(5)
#define DPERR_BUFFERTOOSMALL     MAKE_DPHRESULT(30)
#define DPERR_CANTADDPLAYER      MAKE_DPHRESULT(40)
#define DPERR_CANTCREATEPLAYER   MAKE_DPHRESULT(60)
#define DPERR_CANTCREATESESSION  MAKE_DPHRESULT(70)
#define DPERR_EXCEPTION          MAKE_DPHRESULT(90)
#define DPERR_GENERIC            E_FAIL
#define DPERR_INVALIDFLAGS       MAKE_DPHRESULT(120)
#define DPERR_INVALIDOBJECT      MAKE_DPHRESULT(130)
#define DPERR_INVALIDPARAMS      E_INVALIDARG
#define DPERR_INVALIDPLAYER      MAKE_DPHRESULT(150)
#define DPERR_NOCONNECTION       MAKE_DPHRESULT(170)
#define DPERR_NOMESSAGES         MAKE_DPHRESULT(180)
#define DPERR_NOSESSIONS         MAKE_DPHRESULT(220)
#define DPERR_TIMEOUT            MAKE_DPHRESULT(230)
#define DPERR_UNAVAILABLE        MAKE_DPHRESULT(250)
#define DPERR_UNSUPPORTED        E_NOTIMPL
#define DPERR_UNINITIALIZED      MAKE_DPHRESULT(1050)
#define DPERR_INVALIDINTERFACE   MAKE_DPHRESULT(1030)
#define DPERR_NOTLOBBIED         MAKE_DPHRESULT(1070)
#define DPERR_CONNECTIONLOST     MAKE_DPHRESULT(1220)

#define DPESC_TIMEDOUT           0x00000001
#define DPENUMSESSIONS_AVAILABLE 0x00000001
#define DPENUMSESSIONS_ALL       0x00000002
#define DPENUMSESSIONS_ASYNC     0x00000010
#define DPOPEN_JOIN              0x00000001
#define DPOPEN_CREATE            0x00000002
#define DPSESSION_NEWPLAYERSDISABLED 0x00000001
#define DPSESSION_MIGRATEHOST    0x00000004
#define DPSESSION_KEEPALIVE      0x00000080
#define DPSEND_GUARANTEED        0x00000001
#define DPRECEIVE_ALL            0x00000001
#define DPPLAYERTYPE_GROUP       0x00000000
#define DPPLAYERTYPE_PLAYER      0x00000001
#define DPID_SYSMSG              0
#define DPID_ALLPLAYERS          0
#define DPID_SERVERPLAYER        1

#define DPCAPS_ISHOST               0x00000002
#define DPCAPS_GROUPOPTIMIZED       0x00000008
#define DPCAPS_KEEPALIVEOPTIMIZED   0x00000010
#define DPCAPS_GUARANTEEDOPTIMIZED  0x00000020
#define DPCAPS_GUARANTEEDSUPPORTED  0x00000040
#define DPCAPS_ASYNCSUPPORTED       0x00000080

#define DPSYS_CREATEPLAYERORGROUP  0x0003
#define DPSYS_DESTROYPLAYERORGROUP 0x0005
#define DPSYS_ADDPLAYERTOGROUP     0x0007
#define DPSYS_DELETEPLAYERFROMGROUP 0x0021
#define DPSYS_SESSIONLOST          0x0031
#define DPSYS_HOST                 0x0101
#define DPSYS_SETPLAYERORGROUPDATA 0x0102
#define DPSYS_SETPLAYERORGROUPNAME 0x0103
#define DPSYS_SETSESSIONDESC       0x0104
#define DPSYS_ADDGROUPTOGROUP      0x0105
#define DPSYS_DELETEGROUPFROMGROUP 0x0106
#define DPSYS_SECUREMESSAGE        0x0107
#define DPSYS_STARTSESSION         0x0108
#define DPSYS_CHAT                 0x0109
#define DPSYS_SETGROUPOWNER        0x010A
#define DPSYS_SENDCOMPLETE         0x010D

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    GUID  guidInstance;
    GUID  guidApplication;
    DWORD dwMaxPlayers;
    DWORD dwCurrentPlayers;
    union { LPWSTR lpszSessionName; LPSTR lpszSessionNameA; };
    union { LPWSTR lpszPassword; LPSTR lpszPasswordA; };
    DWORD dwReserved1;
    DWORD dwReserved2;
    DWORD dwUser1;
    DWORD dwUser2;
    DWORD dwUser3;
    DWORD dwUser4;
} DPSESSIONDESC2, *LPDPSESSIONDESC2;
typedef const DPSESSIONDESC2 *LPCDPSESSIONDESC2;

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    union { LPWSTR lpszShortName; LPSTR lpszShortNameA; };
    union { LPWSTR lpszLongName; LPSTR lpszLongNameA; };
} DPNAME, *LPDPNAME;
typedef const DPNAME *LPCDPNAME;

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMaxBufferSize;
    DWORD dwMaxQueueSize;
    DWORD dwMaxPlayers;
    DWORD dwHundredBaud;
    DWORD dwLatency;
    DWORD dwMaxLocalPlayers;
    DWORD dwHeaderLength;
    DWORD dwTimeout;
} DPCAPS, *LPDPCAPS;

typedef struct { DWORD dwType; } DPMSG_GENERIC, *LPDPMSG_GENERIC;

typedef struct {
    DWORD  dwType;
    DWORD  dwPlayerType;
    DPID   dpId;
    DWORD  dwCurrentPlayers;
    LPVOID lpData;
    DWORD  dwDataSize;
    DPNAME dpnName;
    DPID   dpIdParent;
    DWORD  dwFlags;
} DPMSG_CREATEPLAYERORGROUP, *LPDPMSG_CREATEPLAYERORGROUP;

typedef struct {
    DWORD  dwType;
    DWORD  dwPlayerType;
    DPID   dpId;
    LPVOID lpLocalData;
    DWORD  dwLocalDataSize;
    LPVOID lpRemoteData;
    DWORD  dwRemoteDataSize;
    DPNAME dpnName;
    DPID   dpIdParent;
    DWORD  dwFlags;
} DPMSG_DESTROYPLAYERORGROUP, *LPDPMSG_DESTROYPLAYERORGROUP;

typedef struct {
    DWORD dwType;
    DPID  dpId;
} DPMSG_HOST, *LPDPMSG_HOST;

typedef struct {
    DWORD          dwType;
    DPSESSIONDESC2 dpDesc;
} DPMSG_SETSESSIONDESC, *LPDPMSG_SETSESSIONDESC;

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    GUID  guidSP;
    LPVOID lpConnection;
    DWORD dwConnectionSize;
} DPCONNECTIONINFO;

typedef BOOL (WINAPI *LPDPENUMSESSIONSCALLBACK2)(LPCDPSESSIONDESC2, LPDWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *LPDPENUMPLAYERSCALLBACK2)(DPID, DWORD, LPCDPNAME, DWORD, LPVOID);
typedef BOOL (WINAPI *LPDPENUMCONNECTIONSCALLBACK)(LPCGUID, LPVOID, DWORD, LPCDPNAME, DWORD, LPVOID);

typedef struct IDirectPlay4Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectPlay4 *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectPlay4 *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectPlay4 *);
    /* IDirectPlay2 */
    HRESULT (STDMETHODCALLTYPE *AddPlayerToGroup)(IDirectPlay4 *, DPID, DPID);
    HRESULT (STDMETHODCALLTYPE *Close)(IDirectPlay4 *);
    HRESULT (STDMETHODCALLTYPE *CreateGroup)(IDirectPlay4 *, LPDPID, LPDPNAME, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *CreatePlayer)(IDirectPlay4 *, LPDPID, LPDPNAME, HANDLE, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *DeletePlayerFromGroup)(IDirectPlay4 *, DPID, DPID);
    HRESULT (STDMETHODCALLTYPE *DestroyGroup)(IDirectPlay4 *, DPID);
    HRESULT (STDMETHODCALLTYPE *DestroyPlayer)(IDirectPlay4 *, DPID);
    HRESULT (STDMETHODCALLTYPE *EnumGroupPlayers)(IDirectPlay4 *, DPID, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *EnumGroups)(IDirectPlay4 *, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *EnumPlayers)(IDirectPlay4 *, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *EnumSessions)(IDirectPlay4 *, LPDPSESSIONDESC2, DWORD, LPDPENUMSESSIONSCALLBACK2, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(IDirectPlay4 *, LPDPCAPS, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetGroupData)(IDirectPlay4 *, DPID, LPVOID, LPDWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetGroupName)(IDirectPlay4 *, DPID, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetMessageCount)(IDirectPlay4 *, DPID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetPlayerAddress)(IDirectPlay4 *, DPID, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetPlayerCaps)(IDirectPlay4 *, DPID, LPDPCAPS, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetPlayerData)(IDirectPlay4 *, DPID, LPVOID, LPDWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetPlayerName)(IDirectPlay4 *, DPID, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetSessionDesc)(IDirectPlay4 *, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectPlay4 *, LPGUID);
    HRESULT (STDMETHODCALLTYPE *Open)(IDirectPlay4 *, LPDPSESSIONDESC2, DWORD);
    HRESULT (STDMETHODCALLTYPE *Receive)(IDirectPlay4 *, LPDPID, LPDPID, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *Send)(IDirectPlay4 *, DPID, DPID, DWORD, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetGroupData)(IDirectPlay4 *, DPID, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetGroupName)(IDirectPlay4 *, DPID, LPDPNAME, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetPlayerData)(IDirectPlay4 *, DPID, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetPlayerName)(IDirectPlay4 *, DPID, LPDPNAME, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetSessionDesc)(IDirectPlay4 *, LPDPSESSIONDESC2, DWORD);
    /* IDirectPlay3 */
    HRESULT (STDMETHODCALLTYPE *AddGroupToGroup)(IDirectPlay4 *, DPID, DPID);
    HRESULT (STDMETHODCALLTYPE *CreateGroupInGroup)(IDirectPlay4 *, DPID, LPDPID, LPDPNAME, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *DeleteGroupFromGroup)(IDirectPlay4 *, DPID, DPID);
    HRESULT (STDMETHODCALLTYPE *EnumConnections)(IDirectPlay4 *, LPCGUID, LPDPENUMCONNECTIONSCALLBACK, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *EnumGroupsInGroup)(IDirectPlay4 *, DPID, LPGUID, LPDPENUMPLAYERSCALLBACK2, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetGroupConnectionSettings)(IDirectPlay4 *, DWORD, DPID, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *InitializeConnection)(IDirectPlay4 *, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *SecureOpen)(IDirectPlay4 *, LPCDPSESSIONDESC2, DWORD, LPCVOID, LPCVOID);
    HRESULT (STDMETHODCALLTYPE *SendChatMessage)(IDirectPlay4 *, DPID, DPID, DWORD, LPVOID);
    HRESULT (STDMETHODCALLTYPE *SetGroupConnectionSettings)(IDirectPlay4 *, DWORD, DPID, LPVOID);
    HRESULT (STDMETHODCALLTYPE *StartSession)(IDirectPlay4 *, DWORD, DPID);
    HRESULT (STDMETHODCALLTYPE *GetGroupFlags)(IDirectPlay4 *, DPID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetGroupParent)(IDirectPlay4 *, DPID, LPDPID);
    HRESULT (STDMETHODCALLTYPE *GetPlayerAccount)(IDirectPlay4 *, DPID, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetPlayerFlags)(IDirectPlay4 *, DPID, LPDWORD);
    /* IDirectPlay4 */
    HRESULT (STDMETHODCALLTYPE *GetGroupOwner)(IDirectPlay4 *, DPID, LPDPID);
    HRESULT (STDMETHODCALLTYPE *SetGroupOwner)(IDirectPlay4 *, DPID, DPID);
    HRESULT (STDMETHODCALLTYPE *SendEx)(IDirectPlay4 *, DPID, DPID, DWORD, LPVOID, DWORD, DWORD, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetMessageQueue)(IDirectPlay4 *, DPID, DPID, DWORD, LPDWORD, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *CancelMessage)(IDirectPlay4 *, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *CancelPriority)(IDirectPlay4 *, DWORD, DWORD, DWORD);
} IDirectPlay4Vtbl;
struct IDirectPlay4 { const IDirectPlay4Vtbl *lpVtbl; };

#define IDirectPlayX_QueryInterface(p, a, b)         ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectPlayX_AddRef(p)                       ((p)->lpVtbl->AddRef(p))
#define IDirectPlayX_Release(p)                      ((p)->lpVtbl->Release(p))
#define IDirectPlayX_Close(p)                        ((p)->lpVtbl->Close(p))
#define IDirectPlayX_CreatePlayer(p, a, b, c, d, e, f) ((p)->lpVtbl->CreatePlayer(p, a, b, c, d, e, f))
#define IDirectPlayX_DestroyPlayer(p, a)             ((p)->lpVtbl->DestroyPlayer(p, a))
#define IDirectPlayX_EnumPlayers(p, a, b, c, d)      ((p)->lpVtbl->EnumPlayers(p, a, b, c, d))
#define IDirectPlayX_EnumSessions(p, a, b, c, d, e)  ((p)->lpVtbl->EnumSessions(p, a, b, c, d, e))
#define IDirectPlayX_GetCaps(p, a, b)                ((p)->lpVtbl->GetCaps(p, a, b))
#define IDirectPlayX_GetSessionDesc(p, a, b)         ((p)->lpVtbl->GetSessionDesc(p, a, b))
#define IDirectPlayX_Open(p, a, b)                   ((p)->lpVtbl->Open(p, a, b))
#define IDirectPlayX_Receive(p, a, b, c, d, e)       ((p)->lpVtbl->Receive(p, a, b, c, d, e))
#define IDirectPlayX_Send(p, a, b, c, d, e)          ((p)->lpVtbl->Send(p, a, b, c, d, e))
#define IDirectPlayX_SetSessionDesc(p, a, b)         ((p)->lpVtbl->SetSessionDesc(p, a, b))
#define IDirectPlayX_EnumConnections(p, a, b, c, d)  ((p)->lpVtbl->EnumConnections(p, a, b, c, d))
#define IDirectPlayX_InitializeConnection(p, a, b)   ((p)->lpVtbl->InitializeConnection(p, a, b))

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_DPLAY_H */
