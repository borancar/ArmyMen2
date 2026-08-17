/* am2hook.dll -- the reconstruction harness.
 *
 * Injected into ArmyMen2.exe by launcher.exe before the game's entry point
 * runs. On attach it fingerprints the loaded image, then installs each
 * reconstructed function over its original.
 *
 * Nothing on disk is modified. Every patch is an in-memory `jmp rel32`, so
 * reverting is a matter of not injecting.
 */

#include "hooklog.h"
#include "control.h"
#include "dinput_hook.h"
#include "gamelog.h"
#include "input.h"
#include "observe.h"
#include "orig.h"
#include "patch.h"
#include "restore.h"
#include "sites.h"
#include "trace.h"
#include "../game/audio.h"
#include "../game/blit.h"
#include "../game/dist.h"
#include "../game/font.h"
#include "../game/mapdraw.h"
#include "../game/objtable.h"
#include "../game/objtype.h"
#include "../game/packkey.h"
#include "../game/movie.h"
#include "../game/palette.h"
#include "../game/rect.h"
#include "../game/report.h"
#include "../game/savetag.h"
#include "../game/sprite.h"
#include "../game/surface.h"
#include "../game/text.h"
#include "../game/device.h"
#include "../game/dplay.h"
#include "../game/startgame.h"
#include "../game/wavefile.h"
#include "../game/winmain.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

/* Refuse to patch anything unless the bytes we are about to overwrite are
 * exactly the ones we disassembled. A different build of the game would have
 * everything at different addresses, and blind patching would corrupt it. */
static const struct {
    uint32_t       addr;
    const char    *name;
    uint8_t        bytes[8];
    uint32_t       len;
} kFingerprints[] = {
    { ADDR_CHECK_SAVE_TAG, "CheckSaveTag",
      { 0x8B, 0x44, 0x24, 0x04, 0x8D, 0x4C, 0x24, 0x04 }, 8 },
    { ADDR_LOG, "Log",
      { 0xC3, 0x90, 0x90, 0x90, 0x90 }, 5 },
    { ADDR_FREAD, "fread",
      { 0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57 }, 7 },
    { ADDR_RECT_SET, "RectSet",
      { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08 }, 8 },
    { ADDR_APPROX_DIST, "ApproxDist",
      { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x54, 0x24, 0x08 }, 8 },
    { ADDR_CLAMP, "Clamp",
      { 0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x44, 0x24, 0x08 }, 8 },
    { ADDR_POINT_IN_RECT, "PointInRect",
      { 0x8B, 0x54, 0x24, 0x08, 0x8B, 0x44, 0x24, 0x04 }, 8 },
    { ADDR_FIND_SLOT, "FindSlot",
      { 0x53, 0x8B, 0x1D, 0x0C, 0x4F, 0x51, 0x00 }, 7 },
    { ADDR_LOOKUP_BY_UID, "LookupByUID",
      { 0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x44, 0x24, 0x04 }, 8 },
    { ADDR_ADD_TO_ITEM_LIST, "AddToItemList",
      { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x57 }, 8 },
    { ADDR_REMOVE_FROM_ITEM_LIST, "RemoveFromItemList",
      { 0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x44, 0x24, 0x04 }, 8 },
    { ADDR_FIRST_ITEM, "FirstItem",
      { 0xA1, 0x04, 0x4F, 0x51, 0x00 }, 5 },
    { ADDR_NEXT_ITEM, "NextItem",
      { 0xA1, 0x08, 0x4F, 0x51, 0x00, 0x56 }, 6 },
    { ADDR_CLIP_RECT, "ClipRect",
      { 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x4C, 0x24, 0x04 }, 8 },
    { ADDR_DRAW_TEXT, "DrawText",
      { 0xA1, 0x80, 0xDF, 0x4F, 0x00, 0x83, 0xEC, 0x2C }, 8 },
    { ADDR_LOCK_SURFACE, "LockSurface",
      { 0xA1, 0x80, 0xDF, 0x4F, 0x00, 0x83, 0xEC, 0x6C }, 8 },
    { ADDR_UNLOCK_SURFACE, "UnlockSurface",
      { 0xA1, 0x80, 0xDF, 0x4F, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_DRAW_SPRITE_CLIPPED, "DrawSpriteClipped",
      { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x85 }, 8 },
    { ADDR_PLAY_DYNAMIC_SOUND, "PlayDynamicSound",
      { 0x83, 0xEC, 0x28, 0x33, 0xC0, 0x53, 0x89, 0x44 }, 8 },
    { ADDR_PLAY_SOUND_AT, "PlaySoundAt",
      { 0x51, 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x53, 0x56 }, 8 },
    { ADDR_STOP_NAMED_SOUND, "StopNamedSound",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x56, 0x85, 0xC0 }, 8 },
    { ADDR_UPDATE_3D_AUDIO, "Update3DAudioVolumes",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x83, 0xEC, 0x10 }, 8 },
    { ADDR_FREE_DYN_SOUNDS, "FreeDynamicSounds",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_LOAD_WAVE_SOUND, "LoadWaveSound",
      { 0x83, 0xEC, 0x1C, 0x33, 0xC0, 0x53, 0x89, 0x44 }, 8 },
    { ADDR_INIT_WAVE_SOUNDS, "InitWaveSounds",
      { 0xA1, 0xCC, 0x52, 0x48, 0x00, 0x83, 0xEC, 0x14 }, 8 },
    { ADDR_FILL_SOUND_BUFFER, "FillSoundBuffer",
      { 0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10, 0x85 }, 8 },
    { ADDR_FREE_MAP_SURFACES, "FreeMapSurfaces",
      { 0xA1, 0x90, 0x4E, 0x51, 0x00, 0x50, 0xE8, 0xE5 }, 8 },
    { ADDR_FREE_MENU_SPRITES, "FreeMenuSprites",
      { 0x56, 0x57, 0xBE, 0xAC, 0xCA, 0x4F, 0x00, 0xBF }, 8 },
    { ADDR_DRAW_MENU_OVERLAY, "DrawMenuOverlay",
      { 0xA1, 0xC4, 0x1D, 0x51, 0x00, 0x53, 0x56, 0x33 }, 8 },
    { ADDR_MAKE_BITMAP, "MakeBitmap",
      { 0x81, 0xEC, 0x80, 0x01, 0x00, 0x00, 0x8B, 0x84 }, 8 },
    { ADDR_BLIT_CENTRED, "BlitCentred",
      { 0xA1, 0xD4, 0x2A, 0x50, 0x00, 0x8B, 0x51, 0x1C }, 8 },
    { ADDR_DRAW_SEQ_BAR, "DrawSeqBar",
      { 0x8B, 0x4C, 0x24, 0x10, 0x83, 0xEC, 0x78, 0xB8 }, 8 },
    { ADDR_RELEASE_SPRITE, "ReleaseSprite",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x0F }, 8 },
    { ADDR_CLEAR_SPRITE, "ClearSprite",
      { 0x57, 0x8B, 0x7C, 0x24, 0x08, 0x85, 0xFF, 0x74 }, 8 },
    { ADDR_RESTORE_CHAIN, "RestoreSpriteSurface",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x46, 0x10 }, 8 },
    { ADDR_BLIT_OVERLAY, "BlitOverlay",
      { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x53, 0x56 }, 8 },
    { ADDR_ENCODE_GLYPH, "EncodeGlyph",
      { 0x83, 0xEC, 0x0C, 0x8B, 0x44, 0x24, 0x10, 0x66 }, 8 },
    { ADDR_RENDER_GLYPH, "RenderGlyph",
      { 0xA1, 0x8C, 0xE0, 0x4F, 0x00, 0x83, 0xEC, 0x10 }, 8 },
    { ADDR_REDRAW_MAP_REGION, "RedrawMapRegion",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x46, 0x04 }, 8 },
    { ADDR_PAINT_MAP_TILES, "PaintMapTiles",
      { 0x83, 0xEC, 0x38, 0x53, 0x8B, 0x1D, 0x94, 0x4E }, 8 },
    { ADDR_BLIT_MAP_BACKDROP, "BlitMapBackdrop",
      { 0x83, 0xEC, 0x10, 0x8B, 0x0D, 0xA8, 0x4E, 0x51 }, 8 },
    { ADDR_SET_DRAW_TARGET, "SetDrawTarget",
      { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x0D, 0x28, 0x71 }, 8 },
    { ADDR_CALIBRATE_PALETTE, "CalibratePalette",
      { 0xA1, 0xD4, 0x2A, 0x50, 0x00, 0x81, 0xEC, 0x0C }, 8 },
    { ADDR_WIN_MAIN, "WinMain",
      { 0x83, 0xEC, 0x1C, 0xB9, 0x07, 0x00, 0x00, 0x00 }, 8 },
    { ADDR_INIT_APPLICATION, "InitApplication",
      { 0x83, 0xEC, 0x28, 0x68, 0x50, 0x43, 0x47, 0x00 }, 8 },
    { ADDR_PUMP_MESSAGE, "PumpMessage",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x83, 0x7E, 0x04 }, 8 },
    { ADDR_POSITION_WINDOW, "PositionWindow",
      { 0xA1, 0x44, 0x73, 0x50, 0x00, 0x83, 0xEC, 0x20 }, 8 },
    /* Never patched -- but src/game/winproc.cpp forwards the comm messages
     * into it, so its entry has to be the one we read. */
    { ADDR_WND_PROC, "WndProc",
      { 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0x53, 0x8B }, 8 },
    { ADDR_INIT_DIRECTDRAW, "InitDirectDraw",
      { 0x83, 0xEC, 0x6C, 0x53, 0x56, 0x8B, 0x74, 0x24 }, 8 },
    { ADDR_INIT_INPUT, "InitInput",
      { 0xA1, 0x80, 0x25, 0x51, 0x00, 0x56, 0x6A, 0x00 }, 8 },
    { ADDR_CREATE_OFFSCREEN, "CreateOffscreenSurface",
      { 0x83, 0xEC, 0x78, 0x56, 0x57, 0xB9, 0x1B, 0x00 }, 8 },
    { ADDR_CLEAR_SURFACE, "ClearSurface",
      { 0x83, 0xEC, 0x74, 0xA1, 0x30, 0x53, 0x48, 0x00 }, 8 },
    { ADDR_REALIZE_PALETTE, "RealizeSystemPalette",
      { 0x53, 0x55, 0x56, 0x57, 0x6A, 0x00, 0xFF, 0x15 }, 8 },
    { ADDR_SNAPSHOT_PALETTE, "SnapshotSystemPalette",
      { 0xA1, 0x5C, 0x24, 0x51, 0x00, 0x56, 0x50, 0xFF }, 8 },
    { ADDR_REPORT_ERROR, "ReportError",
      { 0x8B, 0x4C, 0x24, 0x08, 0x8D, 0x44, 0x24, 0x0C }, 8 },
    { ADDR_FATAL_ERROR, "FatalError",
      { 0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x44, 0x24, 0x08 }, 8 },
    { ADDR_WAVE_OPEN_FILE, "WaveOpenFile",
      { 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x4C, 0x24, 0x04 }, 8 },
    { ADDR_WAVE_START_DATA, "WaveStartDataRead",
      { 0x56, 0x8B, 0x74, 0x24, 0x10, 0x57, 0x8B, 0x7C }, 8 },
    { ADDR_MOVIE_STOP, "MovieStop",
      { 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x1C, 0xC7, 0x06 }, 8 },
    { ADDR_MOVIE_POLL, "MoviePoll",
      { 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x0C, 0x85, 0xC0 }, 8 },
    { ADDR_MOVIE_START, "MovieStart",
      { 0x56, 0x8B, 0xF1, 0x33, 0xC0, 0x8B, 0x4E, 0x1C }, 8 },
    { ADDR_MOVIE_APPLY_PALETTE, "MovieApplyPalette",
      { 0x51, 0x8B, 0x41, 0x1C, 0xC7, 0x44, 0x24, 0x00 }, 8 },
    { ADDR_MOVIE_OPEN, "MovieOpen",
      { 0x8B, 0x54, 0x24, 0x0C, 0x8B, 0x44, 0x24, 0x10 }, 8 },
    { ADDR_MOVIE_DRAW_FRAME, "MovieDrawFrame",
      { 0x83, 0xEC, 0x6C, 0x53, 0x56, 0x8B, 0xF1, 0xFF }, 8 },
    { ADDR_MOVIE_FINISHED, "MovieFinished",
      { 0xA1, 0x5C, 0x24, 0x51, 0x00, 0x6A, 0x00, 0x6A }, 8 },
    { ADDR_MOVIE_SET_VOLUME, "MovieSetVolume",
      { 0xA1, 0xA8, 0x98, 0x65, 0x00, 0x56, 0x85, 0xC0 }, 8 },
    { ADDR_FIND_GAME_CD, "FindGameCD",
      { 0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xB4, 0xF0 }, 8 },
    { ADDR_BUILD_FONT, "BuildFont",
      { 0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x57, 0x8B }, 8 },
    { ADDR_PRESENT_FRAME, "PresentFrame",
      { 0xA1, 0x30, 0xA0, 0x4F, 0x00, 0x83, 0xEC, 0x1C }, 8 },
    { ADDR_DETECT_CPU_SPEED, "DetectCpuSpeed",
      { 0x56, 0x68, 0x88, 0x42, 0x47, 0x00, 0xFF, 0x15 }, 8 },
    { ADDR_CREATE_GAME_FONT, "CreateGameFont",
      { 0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x75, 0x03 }, 8 },
    { ADDR_WAVE_READ_FILE, "WaveReadFile",
      { 0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x48, 0x8D }, 8 },
    { ADDR_WAVE_CLOSE_FILE, "WaveCloseReadFile",
      { 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x8B, 0x06, 0x85 }, 8 },
    { ADDR_RELEASE_PALETTE, "ReleasePalette",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x86, 0x00 }, 8 },
    { ADDR_SET_PALETTE_RANGE, "SetPaletteRange",
      { 0xA1, 0x44, 0x73, 0x50, 0x00, 0x85, 0xC0, 0x75 }, 8 },
    { ADDR_SET_SURF_COLORKEY, "SetSurfaceColorKey",
      { 0x83, 0xEC, 0x08, 0x8B, 0x44, 0x24, 0x10, 0x8D }, 8 },
    { ADDR_CREATE_BITMAP, "CreateBitmapSurface",
      { 0x83, 0xEC, 0x6C, 0x53, 0x55, 0x56, 0x8B, 0xB4 }, 8 },
    { ADDR_RELOAD_BITMAP, "ReloadBitmapSurface",
      { 0x83, 0xEC, 0x6C, 0x53, 0x56, 0x8B, 0xB4, 0x24 }, 8 },
    { ADDR_CLEAR_REGION, "ClearRegion",
      { 0xA1, 0x80, 0xDF, 0x4F, 0x00, 0x83, 0xEC, 0x74 }, 8 },
    { ADDR_REFRESH_SCREEN, "RefreshScreen",
      { 0x56, 0x8B, 0x35, 0x30, 0xA0, 0x4F, 0x00, 0x6A }, 8 },
    { ADDR_RESTORE_LOST, "RestoreLostSurfaces",
      { 0xA1, 0xD4, 0x2A, 0x50, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_REFILL_AUDIO, "RefillAudioBuffer",
      { 0x83, 0xEC, 0x14, 0x8D, 0x54, 0x24, 0x10, 0xA1 }, 8 },
    { ADDR_STOP_ALL_SOUNDS, "StopAllSounds",
      { 0x51, 0x53, 0x55, 0x56, 0x57, 0xBE, 0x40, 0xA0 }, 8 },
    { ADDR_FREE_SOUND, "FreeSound",
      { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74 }, 8 },
    { ADDR_INIT_DIRECTSOUND, "InitDirectSound",
      { 0x83, 0xEC, 0x14, 0x6A, 0x00, 0x68, 0x6C, 0xA4 }, 8 },
    { ADDR_SET_STREAM_VOLUME, "SetStreamVolume",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_RELEASE_SOUND_BUFS, "ReleaseSoundBuffers",
      { 0xA1, 0x70, 0xA4, 0x4F, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_SHUTDOWN_INPUT, "ShutdownInput",
      { 0xA1, 0xD0, 0x2F, 0x51, 0x00, 0x56, 0x33, 0xF6 }, 8 },
    { ADDR_ACQUIRE_MOUSE, "AcquireMouse",
      { 0xA1, 0xD4, 0x2F, 0x51, 0x00, 0x85, 0xC0, 0x74 }, 8 },
    { ADDR_SHUTDOWN_DDRAW, "ShutdownDirectDraw",
      { 0xA1, 0x78, 0xDF, 0x4F, 0x00, 0x56, 0x33, 0xF6 }, 8 },
    { ADDR_STOP_AUDIO_STREAM, "StopAudioStream",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x85, 0xC0, 0x0F }, 8 },
    { ADDR_START_AUDIO_STREAM, "StartAudioStream",
      { 0xA1, 0x68, 0xA4, 0x4F, 0x00, 0x85, 0xC0, 0x0F }, 8 },
    { ADDR_COMM_CLOSE, "CommClose",
      { 0xA1, 0xB0, 0x51, 0x47, 0x00, 0x8B, 0x80, 0xEC }, 8 },
    { ADDR_COMM_INIT_CONN, "CommInitializeConnection",
      { 0x8B, 0x81, 0xEC, 0x03, 0x00, 0x00, 0x85, 0xC0 }, 8 },
    { ADDR_COMM_GET_SESSION, "CommGetSessionDesc",
      { 0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xEC, 0x03 }, 8 },
    { ADDR_START_MULTIPLAYER, "StartMultiplayerGame",
      { 0x6A, 0xFF, 0x68, 0xFB, 0xDB, 0x46, 0x00, 0x64 }, 8 },
    { ADDR_START_SELECTED_GAME, "StartSelectedGame",
      { 0xA1, 0x58, 0xA0, 0x65, 0x00, 0x53, 0x56, 0x57 }, 8 },
    { ADDR_COMM_OPEN_SESSION, "CommOpenSession",
      { 0x8B, 0x15, 0xB0, 0x51, 0x47, 0x00, 0x83, 0xEC }, 8 },
    { ADDR_COMM_SEND, "CommSend",
      { 0x55, 0x56, 0x57, 0x8B, 0xF9, 0x33, 0xED, 0x39 }, 8 },
    { ADDR_COMM_ENUM_CONNECTIONS, "CommEnumConnections",
      { 0x8B, 0x81, 0xEC, 0x03, 0x00, 0x00, 0x85, 0xC0 }, 8 },
    { ADDR_COMM_CREATE_PLAYER, "CommCreatePlayer",
      { 0x83, 0xEC, 0x10, 0x33, 0xC0, 0x53, 0x56, 0x89 }, 8 },
    { ADDR_COMM_RECEIVE, "CommReceive",
      { 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x86 }, 8 },
    { ADDR_COMM_JOIN_SESSION, "CommJoinSession",
      { 0x83, 0xEC, 0x50, 0x56, 0x8B, 0xF1, 0x8B, 0x96 }, 8 },
    { ADDR_COMM_SEND_PROPERTY, "CommSendLobbyProperty",
      { 0x8B, 0x81, 0xF8, 0x03, 0x00, 0x00, 0x83, 0xEC }, 8 },
    { ADDR_COMM_LOBBY_START, "CommLobbyStart",
      { 0x83, 0xEC, 0x30, 0x53, 0x56, 0x8B, 0xF1, 0x57 }, 8 },
    { ADDR_COMM_DROP_DPLAY, "CommDropDirectPlay",
      { 0x51, 0x53, 0x55, 0x8B, 0xE9, 0x33, 0xDB, 0x8B }, 8 },
    { ADDR_COMM_CONNECTED, "CommOnConnected",
      { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xEC, 0x03, 0x00 }, 8 },
    { ADDR_COMM_ENUM_SESSIONS, "CommEnumSessions",
      { 0x83, 0xEC, 0x50, 0x56, 0x57, 0x8B, 0x7C, 0x24 }, 8 },
    { ADDR_COMM_CONSTRUCT, "CommConstruct",
      { 0x51, 0x53, 0x55, 0x56, 0x57, 0x8B, 0xF1, 0xE8 }, 8 },
    { ADDR_COMM_DESTRUCT, "CommDestruct",
      { 0x56, 0x8B, 0xF1, 0xE8, 0x78, 0x0D, 0x00, 0x00 }, 8 },
    { ADDR_COMM_SET_SESSION, "CommSetSessionDesc",
      { 0x8B, 0x81, 0xEC, 0x03, 0x00, 0x00, 0x85, 0xC0 }, 8 },
    { ADDR_COMM_SHUTDOWN, "CommShutdown",
      { 0x51, 0x57, 0x68, 0xE8, 0xD8, 0x48, 0x00, 0xE8 }, 8 },
    { ADDR_COMM_CREATE_DPLAY, "CommCreateDirectPlay",
      { 0x53, 0x8B, 0xD9, 0x56, 0x57, 0x8B, 0x83, 0xEC }, 8 },
    { ADDR_CREATE_LOBBY, "CreateDirectPlayLobby",
      { 0x51, 0x8D, 0x44, 0x24, 0x00, 0xC7, 0x44, 0x24 }, 8 },
    /* Both are jmp-through-IAT thunks we call rather than import. If either
     * moved, we would be calling into something else entirely. */
    { ADDR_DIRECTDRAWCREATE, "DirectDrawCreate thunk",
      { 0xFF, 0x25, 0x0C, 0xF0, 0x46, 0x00 }, 6 },
    { ADDR_DIRECTINPUTCREATE, "DirectInputCreateA thunk",
      { 0xFF, 0x25, 0x14, 0xF0, 0x46, 0x00 }, 6 },
};

static int verify_image(void)
{
    size_t i;
    int    bad = 0;

    if (GetModuleHandleA(NULL) != (HMODULE)AM2_IMAGE_BASE) {
        hooklog("verify: image base is %p, expected %08x",
                GetModuleHandleA(NULL), AM2_IMAGE_BASE);
        return 1;
    }

    for (i = 0; i < sizeof kFingerprints / sizeof kFingerprints[0]; i++) {
        const void *p = (const void *)(uintptr_t)kFingerprints[i].addr;
        if (IsBadReadPtr(p, kFingerprints[i].len) ||
            memcmp(p, kFingerprints[i].bytes, kFingerprints[i].len) != 0) {
            hooklog("verify: %s at %08x does not match expected bytes",
                    kFingerprints[i].name, kFingerprints[i].addr);
            bad = 1;
        }
    }
    return bad;
}

/* Watch the most-called game functions to learn which of them are actually on
 * the startup path, and with what arguments, before committing to
 * reconstructing any of them. None of these are replaced -- their bodies are
 * untouched and only their call sites are redirected. Names are placeholders
 * until each is identified. Enable with AM2_OBSERVE=1.
 *
 * nargs is a guess of 4: reading a few extra dwords only walks into the
 * caller's own frame, which is always mapped, so an over-estimate is noise
 * rather than a fault. */
static void observe_hot_functions(void)
{
    const char *opt = getenv("AM2_OBSERVE");

    if (!opt || *opt != '1')
        return;

    /* Measure the GDI glyph renderer's arguments rather than deriving them
     * from a frame that juggles seven Win32 calls. */
    OBSERVE(0x004465E0u, "RenderGlyph", 5, sites_004465e0);
}

static void install(void)
{
    hooklog_open();
    hooklog("am2hook attached to pid %lu", GetCurrentProcessId());

    if (verify_image()) {
        hooklog("am2hook: image fingerprint failed -- no patches installed");
        return;
    }
    hooklog("verify: image matches ArmyMen2.exe (MSVC 6, 1999-02-03)");

    /* AM2_NOPATCH=1 installs the harness but not the reconstruction: the game
     * runs entirely on its own code, with the debug logger, the input hook and
     * the control socket still in place. That is the A/B this project has never
     * had -- `run-stock` drops the harness too, so it cannot be driven, cannot
     * be logged and does not in fact start on this machine at all. With this,
     * the same scripted run can be played twice and the game's own log compared
     * line for line.
     *
     * Note the counts are empty under it: the per-function counters are the
     * trace stubs, and those are installed by patch_replace along with
     * everything else. The evidence is the game's log, not the counters. */
    /* Before the AM2_NOPATCH gate on purpose. This is not reconstruction --
     * it undoes an edit made to the shipping binary -- so it has to apply to
     * both sides of an A/B or the two runs are not the same scenario. Without
     * it here, the original side would have no MULTIPLAYER button and the
     * multiplayer path could never be compared at all. Still off unless
     * AM2_MULTIPLAYER=1; see restore.c. */
    restore_install();

    {
        const char *nopatch = getenv("AM2_NOPATCH");

        gamelog_install();
        if (nopatch && *nopatch == '1') {
            hooklog("am2hook: AM2_NOPATCH=1 -- reconstruction NOT installed");
            input_init();
            dinput_hook_install();
            control_start();
            return;
        }
    }
    savetag_install();
    rect_install();
    dist_install();
    objtable_install();
    objtype_install();
    packkey_install();
    text_install();
    font_install();
    mapdraw_install();
    report_install();
    wavefile_install();
    dplay_install();
    startgame_install();

    movie_install();
    audio_install();
    palette_install();
    blit_install();
    sprite_install();
    surface_install();
    device_install();
    winmain_install();
    observe_hot_functions();

    /* Input interception is independent of the reconstruction: it exists so
     * gameplay code paths can be reached deterministically from outside. */
    input_init();
    dinput_hook_install();
    control_start();

    hooklog("am2hook: %d patch(es) installed", patch_count());
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(inst);
        install();
        break;
    case DLL_PROCESS_DETACH:
        trace_report();
        hooklog("am2hook detaching");
        hooklog_close();
        break;
    default:
        break;
    }
    return TRUE;
}
