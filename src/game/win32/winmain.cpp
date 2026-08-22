/* Application startup, the window, and the message loop -- reconstructed from
 * ArmyMen2.exe.
 *
 *   WinMain          0x0040B360   called once by the CRT startup
 *   InitApplication  0x0040B600   1 call site
 *   PumpMessage      0x0040B280   2 call sites
 *   PositionWindow   0x0040B070   2 call sites
 *
 * This is the whole of the game's contact with the window system: the mutex,
 * the class, the window, the queue, and where the window sits on the desktop.
 * Nothing else in the image calls CreateWindowEx or PeekMessage. What it hands
 * off to -- input, DirectDraw, the frame tick, the state machine -- is game
 * logic and stays in the original image, reached through the typed pointers
 * below.
 *
 * WinMain is stdcall, `ret 0x10`, and its four arguments are the standard ones.
 * The MSG it loops on is the 28 bytes of stack it zeroes on entry with a
 * `rep stosd` of 7 dwords, which is how the loop's shape was confirmed before a
 * single call had been identified.
 *
 * WHAT THE COMMAND LINE TURNED OUT TO BE HIDING
 *
 *   The flag parsing is a plain chain of strstr calls, and one of the switches
 *   it sets is `-w`. That global, 0x00507344, is the windowed-mode flag, and it
 *   is the condition on every piece of display behaviour that had looked
 *   unreachable: the border and repositioning here, the palettized primary in
 *   InitDirectDraw, and CalibratePalette. It reads 0 under the harness because
 *   nothing passes `-w`, not because the code is dead.
 *
 *   Three of the switches are people -- `-rob`, `-peter`, `-dan`. `-rob` is the
 *   flag src/game/objtable.cpp already knew as the one enabling AddToItemList's
 *   commentary; it just did not know it had a name.
 *
 * A note on argument evaluation. PositionWindow calls GetWindowLongA twice,
 * GetMenu between them, and feeds all three to AdjustWindowRectEx. The second
 * GetWindowLongA has to observe the SetWindowLongA above it, so the order is
 * load-bearing -- and C does not specify the order in which call arguments are
 * evaluated. The temporaries below are not for readability; removing them would
 * let the compiler reorder three Win32 calls against each other.
 */

#include "winmain.h"
#include "../gameproc.h"  /* SetGameOver */
#include "../air.h"    /* FreeSpriteList -- what the alias jumps to */
/* Compiled for the check, not called: see the header. The retail copy
 * protection is recorded there because this binary has it patched out. */
#include "cdcheck.h"
#include "winproc.h"
#include "../rect.h"
#include "surface.h"
#include "audio.h"
#include "device.h"
#include "dplay.h"
#include "frame.h"
#include "palette.h"
#include "report.h"
#include "../crt.h"
#include "../misc.h"   /* BuildRgb332Palette, which used to be a seam */
#include "../trig.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Every constant below was read out of the disassembly as a number and then
 * written as the SDK name it matches. These check that the substitution was
 * right, so a wrong guess is a build error rather than a subtly wrong window.
 * IDI_APPLICATION and IDC_ARROW are the other two -- both MAKEINTRESOURCE
 * casts, so not constant expressions, but both are 32512 which is the 0x7F00
 * the original pushes. */
static_assert(MUTEX_ALL_ACCESS == 0x1F0001, "MUTEX_ALL_ACCESS");
static_assert(CS_DBLCLKS == 0x0008, "CS_DBLCLKS");
static_assert((WS_POPUP | WS_VISIBLE) == 0x90000000, "window style");
static_assert(WS_EX_APPWINDOW == 0x00040000, "WS_EX_APPWINDOW");
static_assert((WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX) == 0x00C60000,
              "windowed style bits");
static_assert(GWL_STYLE == -16 && GWL_EXSTYLE == -20, "GetWindowLong indices");
static_assert((SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == 0x16, "resize");
static_assert((SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE) == 0x13, "restack");
static_assert((SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) == 0x15, "move");
static_assert(SPI_GETWORKAREA == 0x0030, "SPI_GETWORKAREA");
static_assert(WM_QUIT == 0x0012, "WM_QUIT");
static_assert(PM_REMOVE == 0x0001, "PM_REMOVE");
static_assert(sizeof(MSG) == 28, "the 7 dwords WinMain zeroes on entry");

/* ---- the globals this layer owns -------------------------------------- */

#define g_hInstance    (*(HINSTANCE *)(uintptr_t)ADDR_HINSTANCE)
#define g_fastMachine  (*(int32_t *)(uintptr_t)ADDR_FAST_MACHINE)
#define g_slowMachine  (*(int32_t *)(uintptr_t)ADDR_SLOW_MACHINE)
#define g_hWnd         (*(HWND *)(uintptr_t)ADDR_HWND)
#define g_appMutex     (*(HANDLE *)(uintptr_t)ADDR_APP_MUTEX)
#define g_lastMessage  (*(uint32_t *)(uintptr_t)ADDR_LAST_MESSAGE)
#define g_screenW      (*(const int32_t *)(uintptr_t)ADDR_SCREEN_W)
#define g_screenH      (*(const int32_t *)(uintptr_t)ADDR_SCREEN_H)
#define g_screenRect   (*(AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)
#define g_windowed     (*(const int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)
#define g_mapName      ((char *)(uintptr_t)ADDR_OPT_MAP_NAME)
#define g_commObject   (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)

/* An option global, written as int32_t whatever it is declared as elsewhere. */
#define opt(addr) (*(int32_t *)(uintptr_t)(addr))

/* ---- what stays in the original image --------------------------------- */

/* InitInput, InitDirectDraw and ReportError used to be reached by address
 * here. They are reconstructed, so the address is the patched entry and the
 * call landed in our own code anyway -- through a jump, and under a name that
 * said otherwise. They are ordinary calls to device.cpp, surface.cpp and
 * report.cpp now. */

/* What the WinMain chain still leaves original, one level down from here. Each
 * is called by exactly one of the functions reconstructed below. */
typedef void (__cdecl *am2_void_fn)(void);
typedef void (__cdecl *am2_str_fn)(const char *);
typedef void (__cdecl *am2_ptr_fn)(void *);
typedef void (__cdecl *am2_i32_fn)(int32_t);

#define orig_sprite_set_load  (*(am2_str_fn)ADDR_SPRITE_SET_LOAD)
#define orig_sprite_set_free  (*(am2_ptr_fn)ADDR_SPRITE_SET_FREE)
#define orig_request_state    (*(am2_i32_fn)ADDR_REQUEST_STATE)
#define orig_strncpy          (*(char *(__cdecl *)(char *, const char *, \
                                                   uint32_t))ADDR_CRT_STRNCPY)

/* The game statically links its own MSVC 6 CRT and ours is a separate world,
 * but strstr crosses nothing: it reads two strings and returns a pointer into
 * the first. No heap, no FILE, no locale. Ours will do. */

/* ---- the chain WinMain drives ------------------------------------------
 *
 * Twelve functions, none over 320 bytes, that WinMain calls in order and that
 * were reached by address until now. One level down from each of them stays
 * original: the thirteen teardowns, the five per-state frame handlers, the
 * sprite-set loader. What is reconstructed is the sequencing.
 */

/* 0x00422DB0. Where every data path starts.
 *
 * The install directory is not configured anywhere -- it is simply the
 * directory the process was launched in, read once at startup into the global
 * SetGameDir later concatenates every subdirectory onto. _getcwd fails when
 * the answer will not fit, which is the only thing that can go wrong and the
 * only thing the message mentions. */
void __cdecl CheckBasePath(void)
{
    if (!am2_getcwd((char *)(uintptr_t)ADDR_GAME_DIR, 0xFF))
        FatalError((const char *)(uintptr_t)ADDR_STR_BASE_PATH_LONG);
}

/* 0x00426C50. Take the high-performance counter, or decide not to.
 *
 * The period is milliseconds per tick, and a counter coarser than 1 kHz is
 * REJECTED by zeroing the frequency -- callers test that, so zero means "use
 * something else". Note the frequency is only announced when it survives, and
 * that a QueryPerformanceFrequency failure reaches the same announcement with
 * whatever it left behind, which is why the test is on the frequency and not
 * on the call. */
void __cdecl InitTimer(void)
{
    int64_t *freq = (int64_t *)(uintptr_t)ADDR_PERF_FREQ;

    *(int16_t *)(uintptr_t)ADDR_PERF_WORD_A = 0;
    *(int16_t *)(uintptr_t)ADDR_PERF_WORD_B = 0;
    *(int16_t *)(uintptr_t)ADDR_PERF_WORD_C = 0;

    if (QueryPerformanceFrequency((LARGE_INTEGER *)freq)) {
        QueryPerformanceCounter((LARGE_INTEGER *)(uintptr_t)ADDR_PERF_START);

        /* Both constants come out of the image rather than being written as
         * literals: same bits, and nothing to mistype. */
        double period = *(const double *)(uintptr_t)ADDR_DBL_MS_PER_SEC
                        / (double)*freq;

        *(double *)(uintptr_t)ADDR_PERF_PERIOD = period;

        if (period > *(const double *)(uintptr_t)ADDR_DBL_MAX_PERIOD) {
            *freq = 0;
            return;
        }
    }

    if (*freq > 0)
        orig_log((const char *)(uintptr_t)ADDR_STR_HIGH_PERF);
}

/* 0x0040B220. The whole teardown, ending in the mutex.
 *
 * It went in as ReleaseAppMutex, which is its last line and nothing else: the
 * thirteen calls before it are the subsystems coming down in order, and only
 * three of them are identified. They are a table here rather than thirteen
 * invented names in orig.h -- the order is the fact worth keeping, and a name
 * per entry would be a guess per entry.
 *
 * The first is thiscall on a fixed object; the rest take nothing. */
static const uint32_t kTeardown[] = {
    /* The first five entries are the anim sweeps, and none of them is a guess
     * any more -- each is a `push <table>; call FreeAnimTable` and the table
     * says which `.ani` it holds. The fifth read as 432 bytes and "does more
     * than free" until docs/functions.tsv was checked against the disassembly:
     * 0x0043C720 is twelve bytes and the rest of that entry is the roach
     * mask builder next door. A merged entry, exactly as merges.py says. */
    ADDR_FREE_EXPLOSION_ANIMS, ADDR_FREE_MISSILE_ANIMS, ADDR_FREE_ROACH_ANIMS,
    ADDR_FREE_VEHICLE_ANIMS, ADDR_FREE_SOLDIER_ANIMS,
    ADDR_FREE_SPRITE_LIST, 0x00445F40u, 0x00446880u, 0x0042E590u,
    0x0040C9F0u, ADDR_SHUTDOWN_DDRAW, ADDR_SHUTDOWN_INPUT,
};

void __cdecl ShutdownSubsystems(void)
{
    ((void (__attribute__((thiscall)) *)(void *))(uintptr_t)0x0042A680u)(
        (void *)(uintptr_t)ADDR_SHUTDOWN_OBJ);

    for (uint32_t i = 0; i < sizeof kTeardown / sizeof kTeardown[0]; i++)
        ((am2_void_fn)(uintptr_t)kTeardown[i])();

    ReleaseMutex(*(HANDLE *)(uintptr_t)ADDR_APP_MUTEX);
}

/* 0x00409920. A `jmp` and nothing else, to the sprite-list teardown that
 * ShutdownSubsystems also calls. Reconstructed as the alias it is: WinMain
 * runs it at STARTUP, where the list is empty and the point is to put the
 * three globals in a known state rather than to free anything. */
void __cdecl FreeSpriteListAlias(void)
{
    FreeSpriteList();
}

/* 0x0040C9B0. Bring audio up, and say so in a global rather than a return
 * value -- every arm answers 1, so the caller learns nothing from it.
 *
 * All three callees are reconstructed, so this reads as ordinary code. The
 * shape worth noticing is that a wave-loading failure releases the buffers
 * again and carries on with audio marked unavailable; only both halves
 * succeeding sets the flag. */
int32_t __cdecl InitAudio(void)
{
    if (InitDirectSound()) {
        if (InitWaveSounds()) {
            *(int32_t *)(uintptr_t)ADDR_AUDIO_ENABLED = 1;
            return 1;
        }
        ReleaseSoundBuffers();
    }
    *(int32_t *)(uintptr_t)ADDR_AUDIO_ENABLED = 0;
    return 1;
}

/* 0x0042E580. Clears the word 0x0042E5A0 sets, and answers 0 doing it. */
int32_t __cdecl ClearGameOver(void)
{
    *(int32_t *)(uintptr_t)ADDR_GAME_OVER_STATE = 0;
    return 0;
}

/* 0x004249C0. Put the process back into the state the title screen expects.
 *
 * Three strings first: the map name defaults when the command line left it
 * empty, and the multiplayer script name is copied unconditionally. Then the
 * palette. Then the title and shared sprite sets, but only under `-df` -- the
 * same switch FreeSpriteSets tests before releasing them, so the two are a
 * pair and both are debug-build behaviour. Then nine globals go to zero, the multiplayer session
 * flag among them. */
void __cdecl ResetToTitle(void)
{
    /* Two different globals, and it matters which way round: the command-line
     * option is the SOURCE and the level's own copy is the destination. */
    char *mapname = (char *)(uintptr_t)ADDR_MAP_NAME;

    strcpy(mapname, (const char *)(uintptr_t)ADDR_OPT_MAP_NAME);
    if (!mapname[0])
        strcpy(mapname, *(const char *const *)(uintptr_t)ADDR_MAP_NAME_DEFAULT);
    strcpy((char *)(uintptr_t)ADDR_MP_SCRIPT_NAME,
           *(const char *const *)(uintptr_t)ADDR_MP_SCRIPT_DEFAULT);

    uint8_t *palette = *(uint8_t **)(uintptr_t)ADDR_ACTIVE_PALETTE;

    BuildRgb332Palette(palette);
    SetGamePalette(palette);

    if (*(const int32_t *)(uintptr_t)ADDR_OPT_DF) {
        orig_sprite_set_load((const char *)(uintptr_t)ADDR_STR_SET_TITLE);
        orig_sprite_set_load((const char *)(uintptr_t)ADDR_STR_SET_SHARED);
    }

    static const uint32_t kCleared[] = {
        ADDR_MP_SESSION, 0x00511E14u, 0x00511E18u, 0x00511E1Cu, 0x005122CCu,
        0x00511E28u, 0x005122D0u, 0x00511E2Cu, 0x00511E44u,
    };
    for (uint32_t i = 0; i < sizeof kCleared / sizeof kCleared[0]; i++)
        *(int32_t *)(uintptr_t)kCleared[i] = 0;

    ((am2_void_fn)(uintptr_t)0x0042F140u)();
    ((am2_void_fn)(uintptr_t)0x0044D110u)();
    ((am2_void_fn)(uintptr_t)ADDR_APPLY_GAME_SETTINGS)();
}

/* 0x0040B7A0. Whether the intro movie plays, and what happens instead.
 *
 * Three ways to skip it and they are not interchangeable: a lobby that has not
 * started yet is started first and then re-read, a comm object that says skip
 * goes straight to state 1, and a global that remembers the intro was already
 * `-nointro` does the same -- and that third one is the command-line switch,
 * not a "we already showed it" flag, which is what the name it first went in
 * under would have suggested. Only the fall-through plays the movie, and that
 * arm also clears the game-over snapshot, which is why it requests state 0. */
void __cdecl StartIntro(void)
{
    uint8_t *comm = g_commObject;

    if (!*(const int32_t *)(comm + ADDR_COMM_OFF_LOBBY)) {
        CommLobbyStart(comm);
        comm = g_commObject;
    }

    if (*(const int32_t *)(comm + ADDR_COMM_OFF_SKIP_INTRO)) {
        orig_request_state(1);
        return;
    }
    if (*(const int32_t *)(uintptr_t)ADDR_OPT_NO_INTRO) {
        orig_request_state(1);
        return;
    }

    orig_request_state(0);
    SetGameOver(0);
}

/* 0x0040B000. One frame.
 *
 * The gate is ADDR_APP_ACTIVE: a background window composes no frames at all,
 * which is a fact about the window and not a "frames enabled" switch. Then the lost-surface check, the input poll and a pre-step,
 * then the state handler, then a post-step the original reaches by tail jump
 * from every arm INCLUDING the out-of-range one -- so a state above 4 is not
 * an error, it just runs no handler. */
typedef void (*am2_state_fn)(void);

static const am2_state_fn kStateFrame[] = {
    State0Frame, State1Frame, State2Frame, State3Frame, State4Frame,
};

void __cdecl RunFrame(void)
{
    if (!*(const int32_t *)(uintptr_t)ADDR_APP_ACTIVE)
        return;

    RestoreLostSurfaces();
    PollInput();
    FramePre();

    uint32_t state = *(const uint32_t *)(uintptr_t)ADDR_GAME_STATE;

    if (state <= 4)
        kStateFrame[state]();

    FramePost();
}

/* 0x00423D20. Release the three sprite sets, behind the flag that says they
 * were loaded -- the same flag ResetToTitle tests before loading two of them. */
void __cdecl FreeSpriteSets(void)
{
    if (!*(const int32_t *)(uintptr_t)ADDR_OPT_DF)
        return;

    orig_sprite_set_free((void *)(uintptr_t)ADDR_SPRITE_SET_TITLE);
    orig_sprite_set_free((void *)(uintptr_t)ADDR_SPRITE_SET_SHARED);
    orig_sprite_set_free((void *)(uintptr_t)ADDR_SPRITE_SET_THIRD);
}

/* 0x0041E690. What the debug allocator has left over.
 *
 * At most fifty rows are printed however many blocks there are, but the count
 * in the heading is the real one. The file name is four characters: the record
 * holds them inline and they are copied out with a strncpy that writes no
 * terminator, which is why the fifth byte of the buffer is zeroed once, before
 * the loop, and never again. */
void __cdecl ReportLeaks(void)
{
    char name[8];
    int32_t total = *(const int32_t *)(uintptr_t)ADDR_LEAK_COUNT;
    int32_t shown = total < 50 ? total : 50;

    name[4] = 0;
    orig_log((const char *)(uintptr_t)ADDR_STR_LEAK_HEADER, total);

    for (int32_t i = 0; i < shown; i++) {
        const uint8_t *rec = *(const uint8_t *const *)(uintptr_t)ADDR_LEAK_RECORDS
                             + (size_t)i * 16;

        orig_strncpy(name, (const char *)(rec + 4), 4);
        orig_log((const char *)(uintptr_t)ADDR_STR_LEAK_ROW,
                 *(const int32_t *)(rec + 12), name,
                 *(const int32_t *)(rec + 8));
    }
}

/* 0x0041E710. Drop the record array and the two counters with it. */
void __cdecl FreeMemTracker(void)
{
    void *recs = *(void **)(uintptr_t)ADDR_LEAK_RECORDS;

    *(int32_t *)(uintptr_t)ADDR_LEAK_COUNT = 0;
    *(int32_t *)(uintptr_t)ADDR_LEAK_TOTAL = 0;
    orig_free(recs);
    *(void **)(uintptr_t)ADDR_LEAK_RECORDS = 0;
}

/* ---- the game CD ------------------------------------------------------- */

/* Original: 0x00426B50, 6 call sites. Find the drive the game CD is in.
 *
 * Walks the logical drive strings -- a run of NUL-terminated roots ending in a
 * second NUL -- and for each CD-ROM asks for the volume label, looking for
 * ARMYMEN2. The first match wins: the path is remembered and the search stops.
 *
 * Returns 1 if a CD-ROM drive was accepted. Note that is not the same as having
 * found the disc: the empty-label branch below accepts any CD-ROM without
 * recording a path, and only the matching branch sets the present flag. That
 * branch is dead as shipped, since the label is a non-empty literal, but it is
 * the shape of the original and is kept.
 *
 * Both buffers come from the game's heap and go back to it. Their size is not
 * guessed -- GetLogicalDriveStringsA is asked how much it needs first, which is
 * why it is called twice. */
#define VOLUME_NAME_MAX 0x104

#define g_cdPresent   (*(int32_t *)(uintptr_t)ADDR_CD_PRESENT)
#define g_cdFoundFlag (*(int32_t *)(uintptr_t)ADDR_CD_FOUND_FLAG)
#define g_cdPath      ((char *)(uintptr_t)ADDR_CD_PATH)
#define g_cdLabel     ((const char *)(uintptr_t)ADDR_CD_LABEL)

typedef int32_t (__cdecl *am2_stricmp_fn)(const char *, const char *);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *, const char *, ...);
#define orig_stricmp (*(am2_stricmp_fn)ADDR_GAME_STRICMP)
#define orig_sprintf (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)

int32_t __cdecl FindGameCD(void)
{
    char    *drives, *volume, *p;
    uint32_t need;
    int32_t  found = 0;

    need   = GetLogicalDriveStringsA(0, NULL) + 1;
    drives = (char *)orig_malloc(need);
    GetLogicalDriveStringsA(need, drives);
    volume = (char *)orig_malloc(VOLUME_NAME_MAX + 1);

    g_cdPresent = 0;
    g_cdPath[0] = '\0';

    for (p = drives; *p && found != 1; ) {
        if (GetDriveTypeA(p) == DRIVE_CDROM) {
            if (g_cdLabel[0] == '\0') {
                /* No label to match, so any CD-ROM will do. */
                found = 1;
            } else if (GetVolumeInformationA(p, volume, VOLUME_NAME_MAX,
                                             NULL, NULL, NULL, NULL, 0) &&
                       orig_stricmp(g_cdLabel, volume) == 0) {
                found         = 1;
                g_cdPresent   = 1;
                g_cdFoundFlag = 1;
                orig_sprintf(g_cdPath, "%s", p);
            } else {
                found = 0;
            }
        }
        while (*p++)            /* step over this root and its terminator */
            ;
    }

    orig_free(drives);
    orig_free(volume);
    return found;
}

/* ---- machine speed ----------------------------------------------------- */

/* Original: 0x0040B2B0. Decide whether this machine is fast enough.
 *
 * A 1999 question, answered by loading cpuinf32.dll -- which ships beside the
 * game -- and calling two of its exports. "Fast" means a Pentium-class family
 * running above 133MHz, and a reported speed of zero also counts as fast, which
 * is the sensible reading of "the library could not tell".
 *
 * The two exported pointers are cached in globals for later use, and the answer
 * is published twice, once each way round. It is also what logs the
 * `system speed:` line that appears near the top of every run.
 *
 * Note the original calls GetProcAddress and then the result without checking
 * it. Kept: a cpuinf32.dll present but missing its exports would be a stranger
 * situation than the crash. */
typedef int32_t (__cdecl *am2_wincpuid_fn)(void);
typedef int32_t (__cdecl *am2_cpuspeed_fn)(int32_t);

#define g_wincpuid     (*(am2_wincpuid_fn *)(uintptr_t)ADDR_WINCPUID_FN)
#define g_cpunormspeed (*(am2_cpuspeed_fn *)(uintptr_t)ADDR_CPUNORMSPEED_FN)

#define MIN_CPU_FAMILY 5      /* Pentium */
#define MIN_CPU_MHZ    0x85   /* 133 */

void __cdecl DetectCpuSpeed(void)
{
    HMODULE  dll;
    int32_t  family, mhz, fast;

    dll = LoadLibraryA("cpuinf32.dll");
    if (!dll) {
        orig_log("Missing %s", "cpuinf32.dll");
        return;
    }

    /* Through void *: GetProcAddress answers FARPROC, and casting one function
     * type straight to another is what -Wcast-function-type objects to. */
    g_wincpuid = (am2_wincpuid_fn)(void *)GetProcAddress(dll, "wincpuid");
    family = g_wincpuid();

    g_cpunormspeed = (am2_cpuspeed_fn)(void *)GetProcAddress(dll, "cpunormspeed");
    mhz = g_cpunormspeed(0);

    /* The family test is on the low 16 bits and unsigned; the speed test is
     * signed, and zero passes. */
    fast = ((uint16_t)family >= MIN_CPU_FAMILY && (mhz > MIN_CPU_MHZ || mhz == 0));

    g_slowMachine = !fast;
    g_fastMachine = fast;
    orig_log("system speed: %d\n", fast);

    FreeLibrary(dll);
}

/* ---- command line ------------------------------------------------------ */

/* Every switch is a substring test against the whole command line, so `-w` is
 * matched anywhere in it and not only as a whole word. That is the original's
 * behaviour and it is kept. Order matters where two switches share a global:
 * `-bm -sm` leaves the music flag clear because `-sm` is tested second. */
static void ParseCommandLine(char *cmdLine)
{
    char *p;

    if (strstr(cmdLine, "-nointro"))   opt(ADDR_OPT_NO_INTRO) = 1;
    if (strstr(cmdLine, "-w"))         opt(ADDR_OPT_WINDOWED) = 1;
    if (strstr(cmdLine, "-tracePF"))   opt(ADDR_OPT_TRACE_PF) = 1;
    if (strstr(cmdLine, "-traceVEH"))  opt(ADDR_OPT_TRACE_VEH) = 1;

    if (strstr(cmdLine, "-debugComm"))
        *(int32_t *)(g_commObject + COMM_OFF_DEBUG) = 1;
    if (strstr(cmdLine, "-traceComm"))
        *(int32_t *)(g_commObject + COMM_OFF_TRACE) = 1;
    if (strstr(cmdLine, "-logComm"))
        *(int32_t *)(g_commObject + COMM_OFF_LOG) = 1;

    if (strstr(cmdLine, "-tracewin"))  opt(ADDR_OPT_TRACE_WIN) = 1;
    if (strstr(cmdLine, "-dbg"))       opt(ADDR_OPT_DBG) = 1;
    if (strstr(cmdLine, "-rob"))       opt(ADDR_OPT_ROB) = 1;
    if (strstr(cmdLine, "-peter"))     opt(ADDR_OPT_PETER) = 1;
    if (strstr(cmdLine, "-dan"))       opt(ADDR_OPT_DAN) = 1;
    if (strstr(cmdLine, "-df"))        opt(ADDR_OPT_DF) = 0;
    if (strstr(cmdLine, "-bm"))        opt(ADDR_OPT_MUSIC) = 1;
    if (strstr(cmdLine, "-sm"))        opt(ADDR_OPT_MUSIC) = 0;
    if (strstr(cmdLine, "-nm"))        opt(ADDR_OPT_NM) = 1;

    /* `-map:NAME` -- everything after the colon up to the first character that
     * is not greater than a space. The comparison is signed, so a byte with the
     * top bit set ends the name just as a space would. */
    g_mapName[0] = '\0';
    p = strstr(cmdLine, "-map:");
    if (p) {
        int8_t  c = (int8_t)p[5];
        int32_t i = 0;

        p += 5;
        while (c > 0x20) {
            g_mapName[i++] = (char)c;
            c = (int8_t)p[1];
            p++;
        }
        g_mapName[i] = '\0';
    }
}

/* ---- the window -------------------------------------------------------- */

void __cdecl PositionWindow(void)
{
    RECT r;

    if (!g_windowed) {
        /* Fullscreen: the drawing rectangle is the screen, at the origin. */
        SetRect((LPRECT)(uintptr_t)ADDR_SCREEN_RECT, 0, 0, g_screenW, g_screenH);
        return;
    }

    /* Turn the borderless popup into an ordinary sizeable window. WS_POPUP and
     * the three bits about to be set are the only ones cleared, so WS_VISIBLE
     * survives and the window does not blink out. */
    {
        LONG style = GetWindowLongA(g_hWnd, GWL_STYLE);
        SetWindowLongA(g_hWnd, GWL_STYLE,
                       (style & 0x7F39FFFF) |
                       (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX));
    }

    /* Grow the requested client size by whatever the new frame costs. */
    SetRect(&r, 0, 0, g_screenW, g_screenH);
    {
        LONG exStyle = GetWindowLongA(g_hWnd, GWL_EXSTYLE);
        BOOL hasMenu = (GetMenu(g_hWnd) != NULL);
        LONG style   = GetWindowLongA(g_hWnd, GWL_STYLE);

        AdjustWindowRectEx(&r, style, hasMenu, exStyle);
    }
    SetWindowPos(g_hWnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);

    /* Drag it back onto the work area if the resize pushed the caption off the
     * top or left of the desktop. Only those two edges are checked -- running
     * off the bottom right is left alone. */
    {
        RECT work;

        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        GetWindowRect(g_hWnd, &r);
        if (r.left < work.left)
            r.left = work.left;
        if (r.top < work.top)
            r.top = work.top;
        SetWindowPos(g_hWnd, NULL, r.left, r.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Publish where the client area ended up, in screen coordinates. Both
     * corners are converted, which is why the RECT is punned to two POINTs --
     * as the original does, and as the identical layout allows. */
    GetClientRect(g_hWnd, &r);
    ClientToScreen(g_hWnd, (LPPOINT)&r.left);
    ClientToScreen(g_hWnd, (LPPOINT)&r.right);

    g_screenRect.left   = r.left;
    g_screenRect.top    = r.top;
    g_screenRect.right  = r.right;
    g_screenRect.bottom = r.bottom;
}

int32_t __cdecl InitApplication(HINSTANCE hInstance, int32_t nCmdShow)
{
    WNDCLASSA wc;
    int32_t   err;

    (void)nCmdShow;   /* the window is created WS_VISIBLE; nothing to show */

    /* One instance at a time. A second copy finds the mutex and leaves without
     * a word -- which is also why a game left running by a crashed harness
     * silently prevents the next one from starting. */
    g_appMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "ArmyMenMutex");
    if (g_appMutex)
        return 0;
    g_appMutex = CreateMutexA(NULL, FALSE, "ArmyMenMutex");

    wc.style         = CS_DBLCLKS;
    /* The original registers 0x0040A6B0 here. Registering our reconstruction
     * instead is how src/game/winproc.cpp gets installed -- there is no patch,
     * because this field is the only thing in the image that refers to it. */
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    /* Resource 32512 out of the executable, not the system icon: the numeric
     * value of IDI_APPLICATION is reused as the game's own resource id. */
    wc.hIcon         = LoadIconA(hInstance, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName  = "Armymen2";
    wc.lpszClassName = "Armymen2";
    RegisterClassA(&wc);

    g_hWnd = CreateWindowExA(WS_EX_APPWINDOW, "Armymen2", "Armymen II",
                             WS_POPUP | WS_VISIBLE, 0, 0,
                             g_screenW, g_screenH,
                             NULL, NULL, hInstance, NULL);
    if (!g_hWnd)
        return 0;

    if (g_windowed)
        UpdateWindow(g_hWnd);
    SetFocus(g_hWnd);

    InitTimer();
    if (!InitInput(g_hWnd)) {
        ShutdownSubsystems();
        DestroyWindow(g_hWnd);
        return 0;
    }

    /* Sized before DirectDraw, because the cooperative level and the mode it
     * asks for depend on the window being the shape it means to keep. */
    PositionWindow();

    err = InitDirectDraw(g_hWnd);
    if (err) {
        ReportError(err, "InitDirectDraw");
        ShutdownSubsystems();
        DestroyWindow(g_hWnd);
        return 0;
    }

    /* Windowed mode gets positioned a second time. Setting the cooperative
     * level moves the window, so the client origin published above is stale by
     * now and has to be measured again. */
    if (g_windowed)
        PositionWindow();

    BuildTrigTables();
    FreeSpriteListAlias();
    InitAudio();
    ClearGameOver();
    return 1;
}

/* ---- the message loop -------------------------------------------------- */

int32_t __cdecl PumpMessage(MSG *msg)
{
    if (msg->message == WM_QUIT)
        return 0;

    TranslateMessage(msg);
    DispatchMessageA(msg);
    g_lastMessage = msg->message;
    return 1;
}

int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                       char *lpCmdLine, int32_t nCmdShow)
{
    MSG msg;

    (void)hPrevInstance;   /* always NULL on Win32, and never read */

    memset(&msg, 0, sizeof msg);
    g_hInstance    = hInstance;
    g_fastMachine  = 0;

    ParseCommandLine(lpCmdLine);

    CoInitialize(NULL);
    CheckBasePath();
    DetectCpuSpeed();

    if (!InitApplication(hInstance, nCmdShow))
        return 0;

    FindGameCD();
    ResetToTitle();
    StartIntro();

    /* Drain the queue, and when there is nothing in it run a frame. A game loop
     * rather than a GetMessage loop: the process never blocks waiting for
     * input, which is what makes it burn a core at idle. */
    for (;;) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!PumpMessage(&msg))
                break;
        } else {
            RunFrame();
        }
    }

    FreeSpriteSets();
    ShutdownDirectDraw();
    ReportLeaks();
    FreeMemTracker();

    if (g_appMutex)
        ReleaseMutex(g_appMutex);

    return (int32_t)msg.wParam;
}

int winmain_install(void)
{
    int rc = 0;

    /* AM2_PROBE_NOWIN leaves this whole layer original, so a run can be
     * compared against the untouched startup path with every other patch still
     * in place. `run-stock` cannot do that -- it drops all 40 at once.
     *
     * Worth keeping because patching WinMain makes three of the four counts
     * permanently 0: once our WinMain calls our InitApplication directly, the
     * patched entry points are never reached and the counters cannot move.
     * This is the switch that makes them mean something again. */
    if (getenv("AM2_PROBE_NOWIN"))
        return 0;

    rc |= patch_replace(ADDR_POSITION_WINDOW, (const void *)PositionWindow,
                        "PositionWindow", 0);
    rc |= patch_replace(ADDR_PUMP_MESSAGE, (const void *)PumpMessage,
                        "PumpMessage", 1);
    rc |= patch_replace(ADDR_INIT_APPLICATION, (const void *)InitApplication,
                        "InitApplication", 2);
    rc |= patch_replace(ADDR_DETECT_CPU_SPEED, (const void *)DetectCpuSpeed,
                        "DetectCpuSpeed", 0);
    rc |= patch_replace(ADDR_FIND_GAME_CD, (const void *)FindGameCD,
                        "FindGameCD", 0);
    rc |= patch_replace(ADDR_WIN_MAIN, (const void *)WinMain, "WinMain", 4);

    /* The chain WinMain drives. Every counter here reads 0 for the usual
     * reason -- our WinMain calls them directly -- except ShutdownSubsystems,
     * which winproc.cpp also reaches by address. */
    rc |= patch_replace(ADDR_CHECK_BASE_PATH, (const void *)CheckBasePath,
                        "CheckBasePath", 1);
    rc |= patch_replace(ADDR_INIT_TIMER, (const void *)InitTimer,
                        "InitTimer", 1);
    rc |= patch_replace(ADDR_SHUTDOWN_SUBSYSTEMS,
                        (const void *)ShutdownSubsystems,
                        "ShutdownSubsystems", 3);
    rc |= patch_replace(ADDR_FREE_SPRITE_LIST_ALIAS,
                        (const void *)FreeSpriteListAlias,
                        "FreeSpriteListAlias", 1);
    rc |= patch_replace(ADDR_INIT_AUDIO, (const void *)InitAudio,
                        "InitAudio", 1);
    rc |= patch_replace(ADDR_CLEAR_GAME_OVER, (const void *)ClearGameOver,
                        "ClearGameOver", 1);
    rc |= patch_replace(ADDR_RESET_TO_TITLE, (const void *)ResetToTitle,
                        "ResetToTitle", 1);
    rc |= patch_replace(ADDR_START_INTRO, (const void *)StartIntro,
                        "StartIntro", 1);
    rc |= patch_replace(ADDR_RUN_FRAME, (const void *)RunFrame, "RunFrame", 1);
    rc |= patch_replace(ADDR_FREE_SPRITE_SETS, (const void *)FreeSpriteSets,
                        "FreeSpriteSets", 1);
    rc |= patch_replace(ADDR_REPORT_LEAKS, (const void *)ReportLeaks,
                        "ReportLeaks", 1);
    rc |= patch_replace(ADDR_FREE_MEM_TRACKER, (const void *)FreeMemTracker,
                        "FreeMemTracker", 1);
    return rc;
}
