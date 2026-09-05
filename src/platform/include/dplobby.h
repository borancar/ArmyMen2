/* dplobby.h -- the DirectPlay lobby interface the game asks CoCreateInstance
 * for. Like dplay.h, types only: the object the platform hands back is not
 * lobbied and says so. */
#ifndef AM2_PLATFORM_DPLOBBY_H
#define AM2_PLATFORM_DPLOBBY_H

#include <dplay.h>

AM2_EXTERN_C_BEGIN

typedef struct IDirectPlayLobby3 IDirectPlayLobby3, *LPDIRECTPLAYLOBBY3, *LPDIRECTPLAYLOBBY3A;

extern const GUID CLSID_DirectPlayLobby;
extern const GUID IID_IDirectPlayLobby3A;

#define DPL_NOCONFIRMATION 0x00000000
#define DPLSYS_CONNECTIONSETTINGSREAD 0x00000001
#define DPLSYS_DPLAYCONNECTFAILED 0x00000002
#define DPLSYS_DPLAYCONNECTSUCCEEDED 0x00000003
#define DPLSYS_APPTERMINATED 0x00000004
#define DPLSYS_SETPROPERTY 0x00000005
#define DPLSYS_SETPROPERTYRESPONSE 0x00000006
#define DPLSYS_GETPROPERTY 0x00000007
#define DPLSYS_GETPROPERTYRESPONSE 0x00000008
#define DPLMSG_SYSTEM      0x00000001
#define DPLMSG_STANDARD    0x00000002
#define DPLAPP_NOENUM      0x80000000
#define DPLCONNECTION_CREATESESSION 0x00000001
#define DPLCONNECTION_JOINSESSION   0x00000002

typedef struct {
    DWORD            dwSize;
    DWORD            dwFlags;
    LPDPSESSIONDESC2 lpSessionDesc;
    LPDPNAME         lpPlayerName;
    GUID             guidSP;
    LPVOID           lpAddress;
    DWORD            dwAddressSize;
} DPLCONNECTION, *LPDPLCONNECTION;
typedef const DPLCONNECTION *LPCDPLCONNECTION;

typedef struct { DWORD dwType; } DPLMSG_GENERIC, *LPDPLMSG_GENERIC;

typedef struct {
    DWORD dwType;
    DWORD dwRequestID;
    GUID  guidPlayer;
    GUID  guidPropertyTag;
    DWORD dwDataSize;
    DWORD dwPropertyData[1];
} DPLMSG_SETPROPERTY, *LPDPLMSG_SETPROPERTY;

typedef struct IDirectPlayLobby3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectPlayLobby3 *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectPlayLobby3 *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectPlayLobby3 *);
    HRESULT (STDMETHODCALLTYPE *Connect)(IDirectPlayLobby3 *, DWORD, LPDIRECTPLAY4 *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *CreateAddress)(IDirectPlayLobby3 *, REFGUID, REFGUID, LPCVOID, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *EnumAddress)(IDirectPlayLobby3 *, LPVOID, LPCVOID, DWORD, LPVOID);
    HRESULT (STDMETHODCALLTYPE *EnumAddressTypes)(IDirectPlayLobby3 *, LPVOID, REFGUID, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *EnumLocalApplications)(IDirectPlayLobby3 *, LPVOID, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetConnectionSettings)(IDirectPlayLobby3 *, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *ReceiveLobbyMessage)(IDirectPlayLobby3 *, DWORD, DWORD, LPDWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *RunApplication)(IDirectPlayLobby3 *, DWORD, LPDWORD, LPDPLCONNECTION, HANDLE);
    HRESULT (STDMETHODCALLTYPE *SendLobbyMessage)(IDirectPlayLobby3 *, DWORD, DWORD, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetConnectionSettings)(IDirectPlayLobby3 *, DWORD, DWORD, LPDPLCONNECTION);
    HRESULT (STDMETHODCALLTYPE *SetLobbyMessageEvent)(IDirectPlayLobby3 *, DWORD, DWORD, HANDLE);
    HRESULT (STDMETHODCALLTYPE *CreateCompoundAddress)(IDirectPlayLobby3 *, LPCVOID, DWORD, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *ConnectEx)(IDirectPlayLobby3 *, DWORD, REFIID, LPVOID *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *RegisterApplication)(IDirectPlayLobby3 *, DWORD, LPVOID);
    HRESULT (STDMETHODCALLTYPE *UnregisterApplication)(IDirectPlayLobby3 *, DWORD, REFGUID);
    HRESULT (STDMETHODCALLTYPE *WaitForConnectionSettings)(IDirectPlayLobby3 *, DWORD);
} IDirectPlayLobby3Vtbl;
struct IDirectPlayLobby3 { const IDirectPlayLobby3Vtbl *lpVtbl; };

#define IDirectPlayLobby_QueryInterface(p, a, b)        ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectPlayLobby_AddRef(p)                      ((p)->lpVtbl->AddRef(p))
#define IDirectPlayLobby_Release(p)                     ((p)->lpVtbl->Release(p))
#define IDirectPlayLobby_Connect(p, a, b, c)            ((p)->lpVtbl->Connect(p, a, b, c))
#define IDirectPlayLobby_GetConnectionSettings(p, a, b, c) ((p)->lpVtbl->GetConnectionSettings(p, a, b, c))
#define IDirectPlayLobby_SendLobbyMessage(p, a, b, c, d) ((p)->lpVtbl->SendLobbyMessage(p, a, b, c, d))
#define IDirectPlayLobby_SetConnectionSettings(p, a, b, c) ((p)->lpVtbl->SetConnectionSettings(p, a, b, c))

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_DPLOBBY_H */
