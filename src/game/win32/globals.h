/* win32/globals.h -- the original's globals whose type is a Win32 or COM
 * one, by name; the rest are in ../globals.h, which says how a name binds.
 * Split out so the flat half never names a platform type; a binding is a
 * macro, so including this costs a flat module nothing it does not expand. */
#ifndef AM2_WIN32_GLOBALS_H
#define AM2_WIN32_GLOBALS_H

#define g_appMutex               (*(HANDLE *)(uintptr_t)ADDR_APP_MUTEX)  /* 0x004FA034 */
#define g_backBuffer             (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER)  /* 0x004FE08C */
#define g_buffer                 (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_AUDIO_BUFFER)  /* 0x004FA404 */
#define g_clipper                (*(LPDIRECTDRAWCLIPPER *)(uintptr_t)ADDR_DD_CLIPPER)  /* 0x00507340 */
#define g_clsidDirectplay        (*(CLSID *)(uintptr_t)ADDR_CLSID_DIRECTPLAY)  /* 0x0046F6D8 */
#define g_clsidDplayLobby        (*(CLSID *)(uintptr_t)ADDR_CLSID_DPLAY_LOBBY)  /* 0x0046F778 */
#define g_commEvent              (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT)  /* 0x0048D8F8 */
#define g_commEvent2             (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT_2)  /* 0x0048D8FC */
#define g_dataChunk              ((MMCKINFO *)(uintptr_t)ADDR_AUDIO_DATA_CHUNK)  /* 0x004FA418 */
#define g_ddraw                  (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)  /* 0x004FDF78 */
#define g_ddraw2                 (*(LPDIRECTDRAW2 *)(uintptr_t)ADDR_DIRECTDRAW2)  /* 0x004FE098 */
#define g_defaultPlayerEvent     (*(HANDLE *)(uintptr_t)ADDR_DEFAULT_PLAYER_EVT)  /* 0x004F48C0 */
#define g_dfKeyboard             (*(DIDATAFORMAT *)(uintptr_t)ADDR_DF_KEYBOARD)  /* 0x0046FD68 */
#define g_dfMouse                (*(DIDATAFORMAT *)(uintptr_t)ADDR_DF_MOUSE)  /* 0x0046FD80 */
#define g_diDevice3              (*(LPDIRECTINPUTDEVICEA *)(uintptr_t)ADDR_DI_DEVICE_3)  /* 0x00512FDC */
#define g_diKeyboard             (*(LPDIRECTINPUTDEVICEA *)(uintptr_t)ADDR_DI_KEYBOARD)  /* 0x00512FD8 */
#define g_diMouse                (*(LPDIRECTINPUTDEVICEA *)(uintptr_t)ADDR_DI_MOUSE)  /* 0x00512FD4 */
#define g_dinput                 (*(LPDIRECTINPUTA *)(uintptr_t)ADDR_DINPUT)  /* 0x00512FD0 */
#define g_dipropBufferSize       (*(DIPROPDWORD *)(uintptr_t)ADDR_DIPROP_BUFFER_SIZE)  /* 0x004854F8 */
#define g_dsListener             (*(LPDIRECTSOUND3DLISTENER *)(uintptr_t)ADDR_DS_LISTENER)  /* 0x004FA474 */
#define g_dsound                 (*(LPDIRECTSOUND *)(uintptr_t)ADDR_DSOUND)  /* 0x004FA46C */
#define g_dsPrimary              (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_DS_PRIMARY)  /* 0x004FA470 */
#define g_guidGameProperty       (*(GUID *)(uintptr_t)ADDR_GUID_GAME_PROPERTY)  /* 0x0046F888 */
#define g_guidNull               (*(GUID *)(uintptr_t)ADDR_GUID_NULL)  /* 0x0046FD98 */
#define g_guidSysKeyboard        (*(GUID *)(uintptr_t)ADDR_GUID_SYS_KEYBOARD)  /* 0x0046F5B8 */
#define g_guidSysMouse           (*(GUID *)(uintptr_t)ADDR_GUID_SYS_MOUSE)  /* 0x0046F5A8 */
#define g_hInstance              (*(HINSTANCE *)(uintptr_t)ADDR_HINSTANCE)  /* 0x00512580 */
#define g_hmmio                  (*(HMMIO *)(uintptr_t)ADDR_AUDIO_HMMIO)  /* 0x004FA414 */
#define g_iidDirectdraw2         (*(GUID *)(uintptr_t)ADDR_IID_DIRECTDRAW2)  /* 0x0046F338 */
#define g_iidDirectplay4a        (*(IID *)(uintptr_t)ADDR_IID_DIRECTPLAY4A)  /* 0x0046F6C8 */
#define g_iidDplayLobby3a        (*(IID *)(uintptr_t)ADDR_IID_DPLAY_LOBBY3A)  /* 0x0046F768 */
#define g_iidDs3dListener        (*(IID *)(uintptr_t)ADDR_IID_DS3D_LISTENER)  /* 0x0046F3E8 */
#define g_inCallback             (*(LONG *)(uintptr_t)ADDR_AUDIO_IN_CALLBACK)  /* 0x004FA478 */
#define g_logPalette             (*(LOGPALETTE *)(uintptr_t)ADDR_LOGPALETTE)  /* 0x00477A60, 256 entries in place */
#define g_mapCache               (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MAP_CACHE_SURFACE)  /* 0x00514E94 */
#define g_menuSurface            (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_MENU_SURFACE)  /* 0x004FCDF4 */
#define g_offscreen              (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_OFFSCREEN_SURFACE)  /* 0x00503100 */
#define g_packetEventA           (*(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_A)  /* 0x0048D8F8 */
#define g_packetEventB           (*(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B)  /* 0x0048D8FC */
#define g_packetEventB2          (*(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B2)  /* 0x004F48C0 */
#define g_packetThread           (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)  /* 0x004F48D8 */
#define g_playBuffer             (*(LPDIRECTSOUNDBUFFER *)(uintptr_t)ADDR_AUDIO_BUFFER_2)  /* 0x004FA440 */
#define g_riffChunk              ((MMCKINFO *)(uintptr_t)ADDR_AUDIO_RIFF_CHUNK)  /* 0x004FA42C */
#define g_saveSlot               ((RECT *)(uintptr_t)ADDR_MENU_SAVE_SLOT)  /* 0x00476198 */
#define g_systemPalette          ((PALETTEENTRY *)(uintptr_t)ADDR_SYSTEM_PALETTE)  /* 0x006564A0 */
#define g_timerId                (*(UINT *)(uintptr_t)ADDR_AUDIO_TIMER_ID)  /* 0x004FA408 */
#define g_viewClipped            ((RECT *)(uintptr_t)ADDR_VIEW_CLIPPED)  /* 0x00514E54 */
#define g_waveFormat             (*(WAVEFORMATEX **)(uintptr_t)ADDR_AUDIO_WAVEFORMAT)  /* 0x004FA410 */

#endif /* AM2_WIN32_GLOBALS_H */
