/* Typed pointers to functions still living inside the original ArmyMen2.exe.
 *
 * Relocations are stripped from the executable, so it can only ever map at its
 * preferred base of 0x400000 and these absolute addresses are stable.
 *
 * IMPORTANT: ArmyMen2.exe statically links the MSVC 6 CRT. Its FILE handles,
 * heap and locale state are entirely separate from the msvcrt.dll that our
 * injected DLL links against. Never pass a game FILE* to our own fread/fclose,
 * or a game pointer to our own free(). Route through the game's own CRT via the
 * declarations below until the owning subsystem is itself reconstructed.
 */

#ifndef AM2_ORIG_H
#define AM2_ORIG_H

#include <stdint.h>
#include <stddef.h>

/* ---- addresses ------------------------------------------------------- */

#define AM2_IMAGE_BASE      0x00400000u

/* Game code */
#define ADDR_CHECK_SAVE_TAG 0x004235D0u  /* BOOL(FILE*,uint32_t,const char*,int32_t) */
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
#define ADDR_RECT_SET       0x0042E1C0u  /* void(AM2_Rect*,int32,int32,int32,int32) */
#define ADDR_CLAMP          0x0042E180u  /* int32_t(int32_t v, int32_t lo, int32_t hi) */
#define ADDR_POINT_IN_RECT  0x0042E1F0u  /* int32_t(const AM2_Rect*, const AM2_Point*) */

#define ADDR_CLIP_RECT      0x0042E220u  /* int32_t(src, clip, int32*, int32*, out) */

/* Text rendering. */
#define ADDR_DRAW_TEXT      0x00446930u  /* void(x,y,str,font,?,colour) */
/* Runtime font generation: GDI-render a character, then RLE it. */
#define ADDR_ENCODE_GLYPH   0x004464C0u  /* uint32_t(uint8_t*,int32,int32,int32) */
#define ADDR_RENDER_GLYPH   0x004465E0u  /* uint32_t(int32,char,HFONT,AM2_Rle16*,int32) */
#define ADDR_CREATE_GAME_FONT 0x00446450u /* HFONT(const char *face, int32 h, uint16 style) */
/* Named for the use font.cpp makes of it -- it GDI-renders glyphs onto it --
 * but InitDirectDraw shows it is the back buffer proper: the surface taken off
 * the primary with DDSCAPS_BACKBUFFER when fullscreen, and a plain offscreen
 * surface of the same size when windowed, where there is no flipping chain to
 * take one from. It is also what the lock target starts out pointing at. */
#define ADDR_FONT_SURFACE   0x004FE08Cu  /* IDirectDrawSurface *, the back buffer */
/* One record per font, 524 bytes apart -- BuildFont computes the stride as
 * ((f<<6)+f)*2+f then <<2, which is 131 dwords and not the 133 this said
 * before. Within a record: +0 the total encoded size, +4 a uint16 offset for
 * each of the 256 characters, +0x204 the pointer to the encoded glyphs. */
#define ADDR_FONT_STRIDE    524u
#define ADDR_GLYPH_SIZE     0x006598D0u  /* uint32_t, total encoded bytes */
#define ADDR_GLYPH_OFFSETS  0x006598D4u  /* uint16_t[256] */
#define ADDR_FONT_BASES     0x00659AD4u  /* uint8_t *, the encoded glyphs */
#define ADDR_FONT_DESCS     0x004897E8u  /* {const char *face; int32 h; uint16 style}[] */
#define ADDR_BUILD_FONT     0x004466E0u  /* int32_t(int32_t fontIndex) */
#define ADDR_GAME_MALLOC    0x004647F8u  /* the game's own CRT malloc */
#define ADDR_GAME_FREE      0x004646A9u  /* the game's own CRT free */
#define ADDR_SCREEN_CLIP    0x00485310u  /* AM2_Rect -- text and sprites share it */

/* ---- DirectDraw ------------------------------------------------------
 *
 * The game does its own software rasterising, but it does it straight into a
 * locked DirectDraw surface. LockSurface (0x0041B9A0) fills a 0x6C-byte
 * DDSURFACEDESC via IDirectDrawSurface::Lock (vtable[25]), retrying through
 * Restore (vtable[27]) on DDERR_SURFACELOST (0x887601C2), and publishes
 * lpSurface and lPitch into the globals below. The sprite dispatcher calls
 * Unlock (vtable[32]) when it is done.
 *
 * So the blitters below are the game's own code and safe to replace. What must
 * NOT be touched is ddraw itself -- these write into surface bits that
 * DirectDraw owns and may move or lose between frames.
 */
/* Putting the finished frame on the screen: a Flip when fullscreen, a BltFast
 * from the back buffer when windowed, because a window has no flipping chain.
 * Gated by 0x004FA030 -- clear it and the game runs with nothing appearing. */
#define ADDR_PRESENT_FRAME  0x0041AC60u  /* void(void) */
/* Run at the top of every frame: any surface that has been lost -- to an
 * alt-tab or a mode change -- is restored before anything draws. */
#define ADDR_RESTORE_LOST   0x0041A8B0u  /* void(void) */
/* Force what is drawn onto the screen now, outside the normal frame rhythm. */
#define ADDR_REFRESH_SCREEN 0x0044D6D0u  /* void(void), 7 call sites */
/* Small DirectDraw wrappers that take their object as an argument, which is why
 * tools/comcalls.py cannot name the interface and why they were invisible in
 * docs/boundary.md until someone went looking. */
#define ADDR_RELEASE_PALETTE   0x0041B6A0u  /* void(void *holder) */
#define ADDR_SET_PALETTE_RANGE 0x0041B720u  /* void(PALETTEENTRY*, first, last) */
#define ADDR_SET_SURF_COLORKEY 0x0041B970u  /* void(surface *, uint8_t key) */
#define PALETTE_HOLDER_OFF     0x800u       /* where the DD palette hangs */
#define ADDR_REFRESH_GATE   0x00412DE0u  /* void(int32), stays original */
#define ADDR_REFRESH_DRAW   0x00424BF0u  /* void(void), stays original */
#define ADDR_SURFACE_514E94 0x00514E94u  /* IDirectDrawSurface * */
#define ADDR_MAP_SURFACE    0x00514E90u  /* desc; +8 a flag, +0x10 the surface */
#define ADDR_ON_MAP_RESTORED 0x0042C0E0u /* tail-called after a map restore */
#define ADDR_PRESENT_ENABLED 0x004FA030u /* int32_t */
#define ADDR_LOCK_SURFACE   0x0041B9A0u  /* int32_t(IDirectDrawSurface*) */
#define ADDR_UNLOCK_SURFACE 0x0041BA40u  /* int32_t(void) */
#define ADDR_SURFACE_LOCKED 0x004FDF80u  /* int32_t; non-zero while a lock is held */
#define ADDR_LOCKED_SURFACE 0x00507128u  /* IDirectDrawSurface *, currently locked */
#define ADDR_PRIMARY_SURFACE 0x00502AD4u /* IDirectDrawSurface *, Restore target */
#define ADDR_SCREEN_PITCH   0x00502AD0u  /* int32_t, DDSURFACEDESC.lPitch */
#define ADDR_FRAMEBUFFER    0x004FE1A8u  /* uint8_t *, DDSURFACEDESC.lpSurface */
/* Applied when the locked surface is the primary one. */
#define ADDR_ORIGIN_DX      0x00485330u  /* int32_t */
#define ADDR_ORIGIN_DY      0x00485334u  /* int32_t */

/* The software RLE blitter family. All four are the same routine differing
 * only in row-offset width and fill policy -- see src/game/blit.c. */
#define ADDR_BLIT_GLYPH     0x0041C710u  /* solid fill,   16-bit offsets */
#define ADDR_BLIT_COPY16    0x0041C2B0u  /* copy source,  16-bit offsets */
#define ADDR_BLIT_COPY32    0x0041C1C0u  /* copy source,  32-bit offsets */
#define ADDR_BLIT_REMAP16   0x0041C3A0u  /* copy via LUT, 16-bit offsets */

/* Sprite drawing. The dispatcher is not reconstructed yet; it fans out to the
 * four blitters above plus 0x0041C480 (656 bytes, unread) and 0x00445EB0,
 * which is not a blitter at all but a fallback chain that calls
 * IDirectDrawSurface::Restore on the sprite's own surface. */
#define ADDR_DRAW_SPRITE         0x00445FF0u  /* void(AM2_Sprite*,x,y,mode) */
#define ADDR_DRAW_SPRITE_CLIPPED 0x00446070u  /* void(spr,x,y,const AM2_Rect*,mode) */
#define ADDR_BLIT_OVERLAY        0x0041C480u  /* __fastcall(x,y,data,AM2_Rect) */
#define ADDR_RESTORE_CHAIN       0x00445EB0u  /* void(AM2_Sprite*) */
#define ADDR_OVERLAY_PALETTE     0x004FE1A4u  /* void *, set before the overlay blit */
#define ADDR_DEFAULT_PALETTE     0x004FE084u  /* void *, used when the sprite has none */

/* Map repainting. World space is 1/256-tile fixed point; the camera origin is
 * scaled by 16 when converting to screen space. */
#define ADDR_SET_DRAW_TARGET     0x0041AC40u  /* void(LPDIRECTDRAWSURFACE) */
#define ADDR_REDRAW_MAP_REGION   0x0041CF90u  /* void(const AM2_Rect*) */
#define ADDR_PREPARE_SCREEN_RECT 0x0042D9B0u  /* void(AM2_Rect by value) */
#define ADDR_DRAW_MAP_TILES      0x0041E440u  /* void(const AM2_Rect*,void*,int32) */
/* A second offscreen surface, the same size again, and NOT the back buffer
 * despite the name -- that is ADDR_FONT_SURFACE. Kept as-is because the name is
 * already spread across mapdraw.cpp; the comment is the correction. */
#define ADDR_BACK_SURFACE        0x00503100u  /* IDirectDrawSurface *, offscreen */
#define ADDR_MAP_DESC            0x00514F20u  /* map descriptor; +4 is a row count */
#define ADDR_CAMERA_X            0x00514EA8u  /* int32_t */
#define ADDR_CAMERA_Y            0x00514EACu  /* int32_t */

/* Display palette calibration. Paints a ramp of every palette index onto the
 * primary surface and reads it back with GDI to learn what actually displays.
 * The colour matcher stays original -- it is pure arithmetic. */
#define ADDR_CALIBRATE_PALETTE   0x0041AFC0u  /* void(uint32_t *palette[512]) */
#define ADDR_NEAREST_PAL_INDEX   0x0041B7C0u  /* uint8_t(const uint32_t*,uint32_t,uint32_t) */

/* The GDI half of the palette. The game is 8-bit, so what it can actually show
 * is negotiated with Windows rather than chosen. */
#define ADDR_REALIZE_PALETTE     0x0041AF00u  /* void(const uint32_t *palette) */
#define ADDR_SNAPSHOT_PALETTE    0x00445170u  /* void(void) */
/* A LOGPALETTE living in .data with palVersion and palNumEntries already set
 * to 0x300 and 256; only the 256 entries after them are ever written. */
#define ADDR_LOGPALETTE          0x00477A60u
#define ADDR_LOGPALETTE_ENTRIES  0x00477A64u  /* PALETTEENTRY[256] */
#define ADDR_SYSTEM_PALETTE      0x006564A0u  /* PALETTEENTRY[256], read back from GDI */

/* ---- DirectPlay -------------------------------------------------------
 *
 * The last outward channel, and the least visible one. The game imports no
 * networking library at all -- no ws2_32, no wsock32, no dplayx, and not even
 * the strings -- because its multiplayer transport is DirectPlay obtained
 * through COM. These two CoCreateInstance sites are the whole of it.
 *
 * Reconstructed in src/game/dplay.cpp. The GUIDs are the game's own copies.
 */
#define ADDR_COMM_CREATE_DPLAY   0x0040DD20u  /* thiscall int32(this, void *conn) */
/* Comm teardown: destroy the four mutex-guarded message lists, wake the packet
 * thread, wait for it and close the handles. */
#define ADDR_COMM_SHUTDOWN       0x004020A0u  /* void(void) */
#define ADDR_DESTROY_MSG_LIST    0x00402170u  /* void(list *), stays original */
#define ADDR_MSG_LIST_A          0x0048D8E8u
#define ADDR_MSG_LIST_B          0x004F48C8u
#define ADDR_MSG_LIST_C          0x0048D8D8u
#define ADDR_MSG_LIST_D          0x004F8780u
#define ADDR_COMM_EVENT          0x0048D8F8u  /* HANDLE, signalled to stop the thread */
#define ADDR_COMM_EVENT_2        0x0048D8FCu  /* HANDLE */
#define ADDR_PACKET_THREAD       0x004F48D8u  /* HANDLE */
#define ADDR_CREATE_LOBBY        0x0040DDD0u  /* int32 __stdcall(LPDIRECTPLAYLOBBY3A *) */
#define ADDR_CLSID_DIRECTPLAY    0x0046F6D8u  /* CLSID_DirectPlay */
#define ADDR_IID_DIRECTPLAY4A    0x0046F6C8u  /* IID_IDirectPlay4A */
#define ADDR_CLSID_DPLAY_LOBBY   0x0046F778u  /* CLSID_DirectPlayLobby */
#define ADDR_IID_DPLAY_LOBBY3A   0x0046F768u  /* IID_IDirectPlayLobby3A */
/* Both thiscall on the comm object, both taking nothing but `this`. */
#define ADDR_COMM_DROP_DPLAY     0x0040EA40u  /* tears down an existing one */
#define ADDR_COMM_CONNECTED      0x0040E660u  /* run once the connection is up */
#define COMM_OFF_DPLAY           0x3ECu       /* IDirectPlay4A * inside the comm object */

/* ---- streaming audio ---------------------------------------------------
 *
 * A looping DirectSound buffer kept fed by a multimedia timer, reading through
 * src/game/wavefile.cpp. Reconstructed in src/game/audio.cpp.
 */
#define ADDR_STOP_AUDIO_STREAM   0x0040D5D0u  /* void(void) */
#define ADDR_START_AUDIO_STREAM  0x0040D680u  /* void(void *track, int32) */
#define ADDR_AUDIO_ENABLED       0x004FA468u  /* int32_t; nothing runs while clear */
#define ADDR_AUDIO_BUFFER        0x004FA404u  /* IDirectSoundBuffer *, the stream */
#define ADDR_AUDIO_BUFFER_2      0x004FA440u  /* IDirectSoundBuffer *, the one played */
#define ADDR_STOP_ALL_SOUNDS     0x0040D730u  /* void(void) */
#define ADDR_FREE_SOUND          0x0040C6E0u  /* void(AM2_Sound *) */
/* 56 fixed slots of 16 bytes, then 17 pointers to allocated sounds. */
#define ADDR_SOUND_SLOTS         0x004FA040u
#define ADDR_SOUND_SLOTS_END     0x004FA3C0u
#define ADDR_SOUND_DYNAMIC       0x004FA3C0u  /* == SLOTS_END; the tables abut */
/* Inclusive: the original's loop is `cmp edi,0x4FA400 / jle`, so the entry AT
 * this address is processed too -- 17 pointers, not 16. */
#define ADDR_SOUND_DYNAMIC_LAST  0x004FA400u
#define SOUND_SLOT_STRIDE        0x10u
#define ADDR_RELEASE_SOUND_BUFS  0x0040C7D0u  /* void(void), 8 call sites */
#define ADDR_INIT_DIRECTSOUND    0x0040C800u  /* int32_t(void); 1 on success */
#define ADDR_SET_STREAM_VOLUME   0x0040CE90u  /* void(int32 pan) */
#define ADDR_DIRECTSOUNDCREATE   0x00463390u  /* jmp [0x0046F01C] */
#define ADDR_DSOUND              0x004FA46Cu  /* IDirectSound * (same as BUF_C) */
#define ADDR_DS_PRIMARY          0x004FA470u  /* the primary sound buffer */
#define ADDR_DS_LISTENER         0x004FA474u  /* IDirectSound3DListener * */
#define ADDR_IID_DS3D_LISTENER   0x0046F3E8u
#define ADDR_STREAM_VOLUME       0x0051231Cu  /* int32_t, the wanted volume */
#define ADDR_DSOUND_BUF_A        0x004FA470u
#define ADDR_DSOUND_BUF_B        0x004FA474u
#define ADDR_DSOUND_BUF_C        0x004FA46Cu
#define ADDR_AUDIO_TIMER_ID      0x004FA408u
#define ADDR_AUDIO_TIMER_RUN     0x004FA464u
#define ADDR_AUDIO_PERIOD        0x004FA448u  /* divided by 4 for timeBeginPeriod */
#define ADDR_AUDIO_IN_CALLBACK   0x004FA478u  /* set while the refill is running */
#define ADDR_AUDIO_TRACK_ARG     0x004FA45Cu
#define ADDR_AUDIO_HMMIO         0x004FA414u  /* HMMIO, closed by WaveCloseReadFile */
#define ADDR_AUDIO_WAVEFORMAT    0x004FA410u  /* WAVEFORMATEX * */
#define ADDR_AUDIO_TIMER_PROC    0x0040D020u  /* the refill callback, stays original */
#define ADDR_AUDIO_PREPARE       0x0040CED0u  /* void(void *), stays original */
#define ADDR_AUDIO_CHECK_PATH    0x00422DE0u  /* int32_t(void *), stays original */
#define ADDR_AUDIO_PATH_ARG      0x004852D4u

/* ---- Smacker video ----------------------------------------------------
 *
 * smackw32.dll has no SDK header and no import library, so its entry points are
 * reached through the game's own IAT slots -- the only place their addresses
 * exist. Reconstructed in src/game/movie.cpp; the movie object is thiscall and
 * deliberately opaque, with only the fields actually touched named there.
 */
#define ADDR_MOVIE_STOP          0x00445120u  /* thiscall void(this) */
#define ADDR_MOVIE_SET_VOLUME    0x00445280u  /* thiscall void(this, int32) */
#define ADDR_MOVIE_VTABLE        0x0046FAB4u  /* stamped into the object */
#define ADDR_MOVIE_SOUND_READY   0x006598A8u  /* int32_t; set once Smacker has sound */
#define ADDR_MOVIE_OPEN          0x00444FC0u  /* thiscall this(this,name,w,h,big) */
#define ADDR_MOVIE_MAKE_SURFACE  0x00445690u  /* surface *(w, h), stays original */
#define ADDR_MOVIE_DSOUND        0x004FA46Cu  /* the DirectSound object, may be null */
#define ADDR_IAT_SMACK_OPEN      0x0046F2C8u
#define ADDR_IAT_SMACK_DDTYPE    0x0046F2CCu
#define ADDR_IAT_SMACK_USE_DSOUND 0x0046F2C4u
#define ADDR_MOVIE_DRAW_FRAME    0x004453C0u  /* thiscall void(this, arg) */
#define ADDR_MOVIE_FINISHED      0x00445600u  /* void(void); posts WM_USER */
#define ADDR_MOVIE_START         0x004451F0u  /* thiscall int32(this, arg) */
/* Lives inside what docs/functions.tsv reports as one 160-byte function at
 * 0x00445320 -- the inventory merged the two. It is a separate function. */
#define ADDR_MOVIE_POLL          0x00445390u  /* thiscall int32(this) */
#define ADDR_IAT_SMACK_WAIT      0x0046F2BCu
#define ADDR_MOVIE_APPLY_PALETTE 0x00445320u  /* thiscall void(this, surface) */
#define ADDR_MOVIE_CURRENT       0x006568A0u  /* the movie being played, or null */
#define ADDR_MOVIE_TIMER_PROC    0x004455E0u  /* the timer callback, stays original */
#define ADDR_GAME_DELETE         0x004648F5u  /* the game's own operator delete */
/* Posted to the window to advance the game state machine: 0x400 when a movie
 * finishes, 0x402 when one could not be started. Both land in the same handler,
 * which is why src/game/winproc.cpp forwards them together. */
#define AM2_WM_STATE_ADVANCE     0x0400u
#define AM2_WM_STATE_ABORT       0x0402u
#define ADDR_MOVIE_BLIT          0x00445500u  /* thiscall, stays original */
#define ADDR_MOVIE_PALETTE_OWNER 0x00477A58u  /* void **; +0x800 is a DD palette */
#define ADDR_IAT_SMACK_TO_BUFFER 0x0046F2B0u
#define ADDR_IAT_SMACK_DO_FRAME  0x0046F2B4u
#define ADDR_IAT_SMACK_NEXT_FRAME 0x0046F2B8u
#define ADDR_IAT_SMACK_CLOSE     0x0046F2C0u
#define ADDR_IAT_SMACK_VOLUMEPAN 0x0046F2ACu

/* .WAV reading through WINMM's multimedia file services -- the only file I/O
 * in the game that does not go through the CRT. Reconstructed in
 * src/game/wavefile.cpp; these are the DirectX SDK sample's names. */
#define ADDR_WAVE_OPEN_FILE      0x0040CA10u  /* MMRESULT(char*,HMMIO*,WAVEFORMATEX**,MMCKINFO*) */
#define ADDR_WAVE_START_DATA     0x0040CBB0u  /* MMRESULT(HMMIO*,MMCKINFO*,MMCKINFO*) */
#define ADDR_WAVE_READ_FILE      0x0040CBF0u  /* MMRESULT(HMMIO,uint32,uint8*,MMCKINFO*,uint32*) */
#define ADDR_WAVE_CLOSE_FILE     0x0040CCE0u  /* MMRESULT(HMMIO*,WAVEFORMATEX**) */

/* Error reporting. Both format into the game's own static buffers and put a
 * message box up; both return 0, which is what lets callers `return` them. */
#define ADDR_FATAL_ERROR         0x0041E750u  /* int32_t(const char *fmt, ...) */
#define ADDR_ERROR_TEXT          0x0050B5E0u  /* char[], the formatted message */
#define ADDR_ERROR_TEXT_DD       0x0050B1E0u  /* char[], the same with an HRESULT */
#define ADDR_VSPRINTF            0x00465A45u  /* the game's own CRT vsprintf */

/* ---- application, window and message loop -----------------------------
 *
 * The outermost layer of the process: WinMain parses the command line, brings
 * up the window and DirectDraw, then runs a PeekMessage loop that ticks a frame
 * whenever the queue is empty.
 *
 * 0x00507344 is the single most useful global here. `-w` sets it, and it gates
 * every windowed-mode behaviour in the game -- the window gets a border and is
 * repositioned, DirectDraw asks for a palettized primary, and CalibratePalette
 * runs. Without it the game is fullscreen and none of that happens, which is
 * why CalibratePalette never fires under the harness as it is normally driven.
 */
#define ADDR_WIN_MAIN            0x0040B360u  /* int32 __stdcall(inst,prev,cmd,show) */
#define ADDR_INIT_APPLICATION    0x0040B600u  /* int32_t(HINSTANCE, int32_t nCmdShow) */
#define ADDR_PUMP_MESSAGE        0x0040B280u  /* int32_t(MSG *) -- 0 on WM_QUIT */
#define ADDR_POSITION_WINDOW     0x0040B070u  /* void(void) */
/* The window procedure. Reconstructed in src/game/winproc.cpp, but NOT patched:
 * it is reached only through the WNDCLASS field that InitApplication fills in,
 * so pointing that at our own leaves the original intact and callable. The
 * messages that are pure comm and game logic are forwarded straight back to it
 * rather than reconstructed. Nothing else in the image refers to this address. */
#define ADDR_WND_PROC            0x0040A6B0u  /* LRESULT CALLBACK(HWND,UINT,WPARAM,LPARAM) */

/* State the window procedure reads. */
#define ADDR_DIRECTDRAW          0x004FDF78u  /* IDirectDraw * */
#define ADDR_PAINT_OBJECT        0x0065A058u  /* see winproc.cpp -- not COM */
#define ADDR_APP_ACTIVE          0x004FA02Cu  /* int32_t; RunFrame ticks only if set */
#define ADDR_CHAR_HANDLER        0x005125B8u  /* void(*)(wparam, lo, hi), may be null */
#define ADDR_GAME_STATE          0x00511DA4u  /* int32_t, 0..4 */
#define ADDR_GAME_STATE_ARG      0x00511DB4u  /* int32_t */
#define ADDR_STATE_DISPATCH      0x00486550u  /* 12-byte records; +0 is a function */
#define ADDR_ON_APP_ACTIVATED    0x004269B0u  /* void(void) */
#define ADDR_CURRENT_STATE       0x0042E5D0u  /* int32_t(void), indexes the above */
#define ADDR_STATE_LEAVE         0x0042E720u  /* void(void) */
#define ADDR_STATE_ENTER         0x00424AD0u  /* void(int32_t) */

#define ADDR_HINSTANCE           0x00512580u  /* HINSTANCE */
/* Not HINSTANCE-related at all, despite sitting beside it: DetectCpuSpeed sets
 * it, and it means "this machine is fast enough". WinMain clears it first. */
#define ADDR_FAST_MACHINE        0x00512584u  /* int32_t */
#define ADDR_HWND                0x0051245Cu  /* HWND, the one game window */
#define ADDR_APP_MUTEX           0x004FA034u  /* HANDLE "ArmyMenMutex" */
#define ADDR_LAST_MESSAGE        0x004F9FE4u  /* uint32_t, last dispatched message */
#define ADDR_SCREEN_W            0x004852D8u  /* int32_t */
#define ADDR_SCREEN_H            0x004852DCu  /* int32_t */
/* AM2_Rect. ADDR_ORIGIN_DX and ADDR_ORIGIN_DY above are its first two members:
 * PositionWindow writes all four, as the client area in screen coordinates
 * when windowed and as (0,0,w,h) when not. */
#define ADDR_SCREEN_RECT         0x00485330u

/* Command-line switches, all set to 1 when the flag is present. Recovered from
 * WinMain's strstr chain; three are developer names. */
#define ADDR_OPT_WINDOWED        0x00507344u  /* -w */
#define ADDR_OPT_NO_INTRO        0x004FA038u  /* -nointro */
#define ADDR_OPT_TRACE_PF        0x0050C35Cu  /* -tracePF */
#define ADDR_OPT_TRACE_VEH       0x0050C360u  /* -traceVEH */
#define ADDR_OPT_TRACE_WIN       0x0050C354u  /* -tracewin */
#define ADDR_OPT_DBG             0x0050C358u  /* -dbg */
#define ADDR_OPT_ROB             0x004FD73Cu  /* -rob; same as ADDR_DEBUG_ITEMLIST */
#define ADDR_OPT_PETER           0x004FD744u  /* -peter */
#define ADDR_OPT_DAN             0x004FD740u  /* -dan */
#define ADDR_OPT_DF              0x0047894Cu  /* -df clears it; 1 by default */
#define ADDR_OPT_MUSIC           0x005125C4u  /* -bm sets, -sm clears */
#define ADDR_OPT_NM              0x0051259Cu  /* -nm */
#define ADDR_OPT_MAP_NAME        0x004F9FECu  /* char[], the text after -map: */
/* The comm subsystem object; -debugComm, -traceComm and -logComm set flags
 * inside it rather than in globals of their own. */
#define ADDR_COMM_OBJECT         0x004751B0u  /* void ** */
#define COMM_OFF_DEBUG           0x418u
#define COMM_OFF_TRACE           0x470u
#define COMM_OFF_LOG             0x474u

/* Startup and shutdown steps WinMain drives. Named where the imports or the
 * COM calls inside them say what they are, left at their address where they do
 * not -- these are pure game logic and stay in the original image. */
#define ADDR_CHECK_BASE_PATH     0x00422DB0u  /* getcwd, complains past 255 chars */
#define ADDR_DETECT_CPU_SPEED    0x0040B2B0u  /* void(void); logs "system speed" */
#define ADDR_SLOW_MACHINE        0x005125C0u  /* int32_t, the inverse of the above */
#define ADDR_WINCPUID_FN         0x004F9FE0u  /* cached cpuinf32.dll exports */
#define ADDR_CPUNORMSPEED_FN     0x004F9FE8u
#define ADDR_INIT_TIMER          0x00426C50u  /* QueryPerformanceFrequency/Counter */
/* Variadic, and always returns 0 -- which is why both device bring-up routines
 * can `return ReportError(...)` and mean "failed". */
#define ADDR_REPORT_ERROR        0x0041E7A0u  /* int32_t(HRESULT, const char *fmt, ...) */

/* ---- device bring-up --------------------------------------------------
 *
 * The two functions that create every DirectDraw and DirectInput object the
 * game owns. Both are reconstructed in src/game/device.cpp.
 *
 * They call DirectDrawCreate and DirectInputCreateA through the game's own
 * import thunks rather than through ours. For DirectDraw that is only tidiness;
 * for DirectInput it is required, because src/inject/dinput_hook.c works by
 * patching the game's IAT slot, and an import of our own would walk straight
 * past the hook and take the harness's input injection with it.
 */
#define ADDR_INIT_INPUT          0x00426D30u  /* int32_t(HWND); 1 on success */
#define ADDR_INIT_DIRECTDRAW     0x0041AA10u  /* HRESULT(HWND); 0 on success */
#define ADDR_DIRECTDRAWCREATE    0x00463396u  /* jmp [0x0046F00C] */
#define ADDR_DIRECTINPUTCREATE   0x00464410u  /* jmp [0x0046F014] -- the hooked slot */

/* DirectDraw. The game holds both interface generations: it queries v2 off v1
 * and then uses whichever one has the SetDisplayMode it wants, three arguments
 * on v1 and five on v2. */
#define ADDR_DIRECTDRAW2         0x004FE098u  /* IDirectDraw2 * */
#define ADDR_IID_DIRECTDRAW2     0x0046F338u  /* the game's own copy of the IID */
#define ADDR_PIXEL_FORMAT_BYTE   0x00502AD9u  /* uint8_t, passed to AttachPalette */
/* Both reconstructed in src/game/surface.cpp.
 *
 * ClearSurface was called ADDR_ATTACH_PALETTE for one commit, guessed from its
 * call site in InitDirectDraw. It is nothing of the kind: vtable slot 5 is Blt,
 * and it is a colour fill. */
#define ADDR_CREATE_OFFSCREEN    0x0041B850u  /* surface *(w, h, caps, int32 key) */
#define ADDR_CLEAR_SURFACE       0x0041AD30u  /* int32_t(surface *, uint32_t colour) */

/* DirectInput. The GUIDs and data formats are the game's own copies in .rdata,
 * so nothing here needs dxguid. */
#define ADDR_DINPUT              0x00512FD0u  /* IDirectInputA * */
#define ADDR_DI_MOUSE            0x00512FD4u  /* IDirectInputDeviceA * */
#define ADDR_DI_KEYBOARD         0x00512FD8u  /* IDirectInputDeviceA * */
#define ADDR_DI_DEVICE_3         0x00512FDCu  /* a third device, never created here */
#define ADDR_DI_MOUSE_ACQUIRED   0x00512FE0u  /* int32_t */
#define ADDR_SHUTDOWN_INPUT      0x00426EA0u  /* void(void) */
#define ADDR_ACQUIRE_MOUSE       0x00426F20u  /* void(void) */
#define ADDR_GUID_SYS_MOUSE      0x0046F5A8u
#define ADDR_GUID_SYS_KEYBOARD   0x0046F5B8u
#define ADDR_DF_MOUSE            0x0046FD80u  /* DIDATAFORMAT c_dfDIMouse */
#define ADDR_DF_KEYBOARD         0x0046FD68u  /* DIDATAFORMAT c_dfDIKeyboard */
#define ADDR_DIPROP_BUFFER_SIZE  0x004854F8u  /* DIPROPDWORD, the buffered-input size */
/* Two pointers into adjacent 256-byte input buffers, reset on bring-up. */
#define ADDR_INPUT_CURSOR_A      0x005127C8u
#define ADDR_INPUT_CURSOR_B      0x005127CCu
#define ADDR_INPUT_BUFFER_A      0x005125C8u
#define ADDR_INPUT_BUFFER_B      0x005126C8u
#define ADDR_RELEASE_APP_MUTEX   0x0040B220u  /* void(void) */
/* Look for the game CD: walk the logical drives, find one that is a CD-ROM and
 * whose volume label is ARMYMEN2, and remember where it is. */
#define ADDR_FIND_GAME_CD        0x00426B50u  /* int32_t(void) */
#define ADDR_CD_PRESENT          0x00512588u  /* int32_t */
#define ADDR_CD_FOUND_FLAG       0x00512594u  /* int32_t, set alongside it */
#define ADDR_CD_PATH             0x00512464u  /* char[], the drive root */
#define ADDR_CD_LABEL            0x004852B8u  /* "ARMYMEN2" */
#define ADDR_GAME_STRICMP        0x00465F90u  /* the game's own CRT */
#define ADDR_GAME_SPRINTF        0x00464CE2u
#define ADDR_STARTUP_4249C0      0x004249C0u
#define ADDR_STARTUP_42DC30      0x0042DC30u
#define ADDR_STARTUP_409920      0x00409920u
#define ADDR_STARTUP_40C9B0      0x0040C9B0u
#define ADDR_STARTUP_42E580      0x0042E580u
#define ADDR_START_INTRO         0x0040B7A0u  /* honours -nointro */
#define ADDR_RUN_FRAME           0x0040B000u  /* one tick; state machine of 5 */
#define ADDR_SHUTDOWN_423D20     0x00423D20u
#define ADDR_SHUTDOWN_DDRAW      0x0041A950u  /* void(void) */
#define ADDR_DD_CLIPPER          0x00507340u  /* IDirectDrawClipper * */
#define ADDR_REPORT_LEAKS        0x0041E690u  /* "Unreleased memory (%d) blocks:" */
#define ADDR_FREE_MEM_TRACKER    0x0041E710u

/* Packed map key: A(7) | gap(2) | B(10) | C(7). */
#define ADDR_PACK_KEY       0x00433810u
#define ADDR_KEY_FIELD_A    0x00433830u
#define ADDR_KEY_FIELD_B    0x00433840u
#define ADDR_KEY_FIELD_C    0x00433850u

/* Object type predicates; all accept NULL and answer 0. */
#define ADDR_OBJ_IS_ITEM    0x00433860u  /* types 1, 4 */
#define ADDR_OBJ_IS_TYPE2   0x00457470u
#define ADDR_OBJ_IS_TYPE3   0x00457490u
#define ADDR_OBJ_IS_TYPE238 0x00457420u  /* types 2, 3, 8 */
#define ADDR_APPROX_DIST    0x0042DDE0u  /* int32_t(const AM2_Point*, const AM2_Point*) */
#define ADDR_FIND_SLOT      0x004277A0u  /* int32_t(uint32_t uid, int32_t *insert_at) */
#define ADDR_LOOKUP_BY_UID  0x00427820u  /* void *(uint32_t uid) */

/* The global object registry: an array of 12-byte {uid, obj, serial} records
 * kept sorted by uid. See src/game/objtable.c. */
#define ADDR_OBJ_TABLE      0x00514F0Cu  /* AM2_ObjEntry * */
#define ADDR_OBJ_COUNT      0x00514F04u  /* int32_t */
#define ADDR_OBJ_CAPACITY   0x00514F00u  /* int32_t */

#define ADDR_ADD_TO_ITEM_LIST 0x00429740u /* uint32_t(AM2_Object*, uint32_t) */
#define ADDR_REMOVE_FROM_ITEM_LIST 0x00428590u /* int32_t(AM2_Object*) */
#define ADDR_FIRST_ITEM     0x00427850u  /* void *(void) */
#define ADDR_NEXT_ITEM      0x00427880u  /* void *(void) */

/* Iteration state. The cursor is an index into the table; the stamp is bumped
 * once per pass so a walk can tell which entries it has already visited even
 * though inserts shift indices underneath it. */
#define ADDR_ITER_CURSOR    0x00514F08u  /* int32_t */
#define ADDR_ITER_STAMP     0x0051308Cu  /* uint32_t */

/* A UID is (owner << 29) | counter, so eight owners each with a 29-bit
 * counter. These are the per-owner counters, indexed 0..7. */
#define ADDR_UID_COUNTERS   0x00511DE0u  /* uint32_t[8] */
/* Owner used for the object types that do not carry their own. */
#define ADDR_DEFAULT_OWNER  0x004F9FDCu  /* uint32_t */
/* Non-zero enables the AddToItemList commentary. */
#define ADDR_DEBUG_ITEMLIST 0x004FD73Cu  /* int32_t */

/* More of the statically linked MSVC 6 CRT. */
#define ADDR_REALLOC        0x004646D8u  /* void *(void*, size_t) */
#define ADDR_MEMMOVE        0x00465710u  /* void *(void*, const void*, size_t) */

/* Statically linked MSVC 6 CRT */
#define ADDR_FREAD          0x004645C1u  /* size_t(void*,size_t,size_t,FILE*) */

/* ---- typed accessors -------------------------------------------------- */

/* The game's FILE is an MSVC 6 _iobuf. We never dereference it, so keep it
 * opaque rather than pretending it matches ours. */
typedef struct am2_FILE am2_FILE;

typedef size_t (__cdecl *am2_fread_fn)(void *buf, size_t size, size_t count,
                                       am2_FILE *fp);
typedef void   (__cdecl *am2_log_fn)(const char *fmt, ...);

typedef void *(__cdecl *am2_realloc_fn)(void *p, size_t n);
typedef void *(__cdecl *am2_memmove_fn)(void *dst, const void *src, size_t n);

#define orig_fread   (*(am2_fread_fn)ADDR_FREAD)
#define orig_log     (*(am2_log_fn)ADDR_LOG)
/* The object table was allocated by the game's CRT, so it must be grown by the
 * game's CRT -- our msvcrt has a different heap entirely. */
#define orig_realloc (*(am2_realloc_fn)ADDR_REALLOC)
#define orig_memmove (*(am2_memmove_fn)ADDR_MEMMOVE)

#endif /* AM2_ORIG_H */
