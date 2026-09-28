#ifndef AM2_WIN32_H
#define AM2_WIN32_H

/* The one place that pulls in the Windows and DirectDraw headers.
 *
 * This is Windows code calling Windows APIs, so it uses the real declarations
 * rather than restating them. CINTERFACE selects the C view of the COM
 * interfaces and COBJMACROS provides the IDirectDrawSurface_* call wrappers, so
 * nothing about DDSURFACEDESC or any vtable layout is written out by hand.
 *
 * The one wrinkle is that winuser.h defines DrawText as a macro expanding to
 * DrawTextA, which collides with the game's own DrawText that we reconstructed.
 * That is handled here, once, instead of being dodged with forward declarations
 * in every header that needs a Windows type. Any future reconstruction that
 * reuses a Win32 name should be undone here too.
 */

#define CINTERFACE
#define COBJMACROS
/* The game asks for DirectInput 5. The version matters: it selects which
 * interface the headers declare, and IDirectInputDevice's vtable is what the
 * reconstruction indexes into. */
#define DIRECTINPUT_VERSION 0x0500
#define DIRECTSOUND_VERSION 0x0300
/* The game creates its DirectPlay objects with CoCreateInstance rather than
 * linking dplayx, which is why nothing network-shaped appears in its import
 * table. The interfaces still come from the SDK. */
#define DIRECTPLAY_VERSION 0x0600
#include <windows.h>
#include <ddraw.h>
#include <dinput.h>
#include <dsound.h>
#include <dplay.h>
#include <dplobby.h>

/* VISUAL C++ 6.0: VC98 ships the DirectX 6.0 headers, which stop at
 * IDirectPlay3A and IDirectPlayLobby2A; the game (and its 1999 SDK) uses
 * IDirectPlay4A and IDirectPlayLobby3A. Both later interfaces only APPEND
 * methods to the earlier vtables, and every method dplay.cpp calls exists in
 * the earlier ones at the same slot -- so the pointer types alias the earlier
 * interfaces and the IDirectPlayX_* call macros (DX6.1's version-neutral
 * spelling) forward to IDirectPlay3_*. The objects themselves still come from
 * CoCreateInstance with IID_IDirectPlay4A / IID_IDirectPlayLobby3A. */
#if defined(_MSC_VER) && _MSC_VER < 1300 && !defined(__GNUC__)
typedef LPDIRECTPLAY3A      LPDIRECTPLAY4A;
typedef LPDIRECTPLAYLOBBY2A LPDIRECTPLAYLOBBY3A;
#define IDirectPlayX_Close(p)              IDirectPlay3_Close(p)
#define IDirectPlayX_CreatePlayer(p,a,b,c,d,e,f) IDirectPlay3_CreatePlayer(p,a,b,c,d,e,f)
#define IDirectPlayX_EnumConnections(p,a,b,c,d) IDirectPlay3_EnumConnections(p,a,b,c,d)
#define IDirectPlayX_EnumPlayers(p,a,b,c,d) IDirectPlay3_EnumPlayers(p,a,b,c,d)
#define IDirectPlayX_EnumSessions(p,a,b,c,d,e) IDirectPlay3_EnumSessions(p,a,b,c,d,e)
#define IDirectPlayX_GetCaps(p,a,b)        IDirectPlay3_GetCaps(p,a,b)
#define IDirectPlayX_GetSessionDesc(p,a,b) IDirectPlay3_GetSessionDesc(p,a,b)
#define IDirectPlayX_InitializeConnection(p,a,b) IDirectPlay3_InitializeConnection(p,a,b)
#define IDirectPlayX_Open(p,a,b)           IDirectPlay3_Open(p,a,b)
#define IDirectPlayX_QueryInterface(p,a,b) IDirectPlay3_QueryInterface(p,a,b)
#define IDirectPlayX_Receive(p,a,b,c,d,e)  IDirectPlay3_Receive(p,a,b,c,d,e)
#define IDirectPlayX_Release(p)            IDirectPlay3_Release(p)
#define IDirectPlayX_Send(p,a,b,c,d,e)     IDirectPlay3_Send(p,a,b,c,d,e)
#define IDirectPlayX_SetSessionDesc(p,a,b) IDirectPlay3_SetSessionDesc(p,a,b)
#endif

#undef DrawText
/* And winuser.h defines LoadBitmap as LoadBitmapA, which collides with the
 * game's own LoadBitmap -- 0x004462F0, the sprite loader that records an
 * absolute source path. Same wrinkle, same fix. */
#undef LoadBitmap

#endif /* AM2_WIN32_H */
