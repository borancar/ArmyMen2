#include "control.h"
#include "hooklog.h"
#include "input.h"
#include "trace.h"
#include "orig.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 31337
/* Big enough for `counts` to name all MAX_TRACED functions at once. At 512 it
 * was not: the reply stopped mid-list and the four functions patched last never
 * appeared at all, which reads exactly like they were never installed. Raised
 * again with MAX_TRACED, which is the other half of the same failure. The
 * client frames on a newline and does not care how long a line is. */
#define MAX_LINE     4096

static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread;
static volatile LONG g_stop;

/* The widget tree, described for comparison rather than for reading.
 *
 * Offsets are the base layout established in src/game/win32/widget.h: the
 * absolute rectangle at 0x14, the child list at 0x24, the parent at 0x28, the
 * sibling links at 0x2C and 0x30, the focused child at 0x34, the sprite at
 * 0x38 and the four flags. Everything past 0x54 belongs to whichever subclass
 * is looking and is deliberately NOT printed -- the same offset means three
 * different things in three classes, so a generic dump of it would be noise
 * that differs for reasons no defect caused. */
/* Already named: ADDR_PAINT_OBJECT. WndProc repaints whatever is there and the
 * dialog openers store the current dialog in it, which are the same object. */
#define WD_ROOT        ADDR_PAINT_OBJECT
#define WD_OFF_RECT    0x14
#define WD_OFF_CHILD   0x24
#define WD_OFF_SIBLING 0x30
#define WD_OFF_FOCUSED 0x34
#define WD_OFF_SPRITE  0x38
#define WD_OFF_FLAG3C  0x3C
/* 0x40 is NOT printed. It is the one field WidgetConstruct deliberately never
 * writes -- ButtonUpdate computes it before anything reads it -- so for every
 * widget whose update has not run it holds whatever the allocator left, and it
 * came back as 25, 1 and 27346604 on runs that were otherwise identical. An
 * oracle has to be exact to be worth anything, and an uninitialised field
 * cannot be. Two runs compared by hand happened to agree, which is how it got
 * into the first version. */
#define WD_OFF_HOVER   0x40
#define WD_OFF_DIRTY   0x44
#define WD_OFF_NOFOCUS 0x4C
#define WD_OFF_CANFOCUS 0x50
#define WD_MAX_PTRS    64

static const void *wd_seen[WD_MAX_PTRS];
static int         wd_nseen;

/* First-seen index for a pointer, so the dump survives the heap moving. */
static int wd_index(const void *p)
{
    int i;

    if (!p)
        return -1;
    for (i = 0; i < wd_nseen; i++)
        if (wd_seen[i] == p)
            return i;
    if (wd_nseen < WD_MAX_PTRS)
        wd_seen[wd_nseen++] = p;
    return wd_nseen - 1;
}

static uint32_t wd_node(char *out, uint32_t at, uint32_t cap,
                        const uint8_t *w, int depth)
{
    const int32_t *r;
    const uint8_t *spr_ptr;
    int32_t        sid;
    int            self;
    int            vt;
    int            spr;
    int            foc;

    if (!w || depth > 8 || at + 240 >= cap)
        return at;

    /* Read AFTER the null guard. Initialising it at the declaration
     * dereferenced w before the guard ran, so every null child faulted and the
     * control socket closed mid-reply. */
    spr_ptr = *(const uint8_t *const *)(w + WD_OFF_SPRITE);

    /* Indices are taken into locals first, in a defined order. Passing three
     * wd_index() calls as arguments numbered them BACKWARDS, because an i386
     * cdecl call evaluates its arguments right to left -- the dump was
     * self-consistent and read #3 for the first node. */
    /* The sprite's own id, not just its pointer index. First-seen indices are
     * what make this dump reproducible across runs, but they also hide a
     * SUBSTITUTION: swap which of two sprites a widget uses and the new one is
     * simply first-seen at the same position, taking the same index. Forcing
     * TogglePaint to the wrong sprite moved 212 pixels and left the tree
     * identical until this line existed. The id is real data and differs.
     *
     * Range-checked before dereferencing: the FIRST version read it
     * unconditionally and took the game down, closing the control socket. A
     * debug dump that can fault is worse than no dump -- it fails the run it
     * was meant to explain. */
    sid = -1;
    if ((uintptr_t)spr_ptr >= 0x00400000u
        && (uintptr_t)spr_ptr < 0x80000000u
        && ((uintptr_t)spr_ptr & 3u) == 0u)
        sid = (int32_t)*(const uint32_t *)spr_ptr;

    self = wd_index(w);
    /* The vtable is an IMAGE address, not a heap one -- fixed for the life of
     * the build -- so it is printed raw. That costs nothing in reproducibility
     * and says which of the 33 classes each node is, which is what decides
     * where the next reconstruction is worth doing. */
    vt   = (int)(uintptr_t)*(void *const *)w;
    spr  = wd_index(*(void *const *)(w + WD_OFF_SPRITE));
    foc  = wd_index(*(void *const *)(w + WD_OFF_FOCUSED));

    r = (const int32_t *)(w + WD_OFF_RECT);
    /* One line, separated by `|`: the control protocol frames on a newline, so
     * a multi-line reply is silently truncated to its first line. */
    at += (uint32_t)_snprintf(out + at, cap - at,
                              "%s[%d] d%d vt=%06x r=%d,%d,%d,%d spr=%d "
                              "sid=%d foc=%d dirty=%d nofoc=%d canfoc=%d c3c=%d",
                              at ? " | " : "",
                              self, depth, vt,
                              r[0], r[1], r[2], r[3], spr, sid, foc,
                              *(const int32_t *)(w + WD_OFF_DIRTY),
                              *(const int32_t *)(w + WD_OFF_NOFOCUS),
                              *(const int32_t *)(w + WD_OFF_CANFOCUS),
                              *(const int32_t *)(w + WD_OFF_FLAG3C));

    at = wd_node(out, at, cap, *(const uint8_t *const *)(w + WD_OFF_CHILD),
                 depth + 1);
    return wd_node(out, at, cap,
                   *(const uint8_t *const *)(w + WD_OFF_SIBLING), depth);
}

/* The dialog if one is up, and the HUD otherwise.
 *
 * The dialog root was the only one for as long as this existed, and it left
 * the widget oracle unavailable for exactly the configurations where the most
 * game code runs: a live mission has no dialog, so `mission` and `campaign`
 * had a log and a pixel count that ab.sh itself calls meaningless. Every menu
 * configuration got an exact comparison and the two gameplay ones got none.
 *
 * The three HUD roots are walked in sequence with ONE first-seen sequence
 * across all three, so a pointer shared between them takes the same index in
 * both places and the dump still survives the heap moving.
 *
 * THE DUMP SAYS WHICH ROOT IT CAME FROM, and that is not decoration. ab.sh
 * diffs these exactly; a menu configuration that sampled between dialogs would
 * quietly fall through to the HUD, and the two sides could then compare
 * different trees and report a widget defect that is nothing of the kind. The
 * label makes that read as "one side had a dialog and the other did not",
 * which is a different bug and an obvious one. Same lesson as two files that
 * are both missing diffing clean.
 *
 * The HUD classes are NOT the 33 menu vtables -- they are their own family at
 * 0x0046F8B8..0x0046F944 -- so what they keep at WD_OFF_SPRITE is unverified
 * here. wd_node's range check on the sprite id is what makes that safe to
 * print, and it is there because the first version of it faulted and closed
 * the socket mid-reply. */
static void widget_describe(char *out, uint32_t cap, int hud_only)
{
    static const uintptr_t kHudRoots[] = {
        ADDR_HUD_WIDGET_A, ADDR_HUD_WIDGET_B, ADDR_HUD_WIDGET_C
    };
    const uint8_t *root = *(const uint8_t *const *)(uintptr_t)WD_ROOT;
    uint32_t       at   = 0;
    int            i, any = 0;

    wd_nseen = 0;
    out[0] = '\0';

    /* `widgets hud` forces the HUD roots even when a dialog is up, and it
     * exists because the alternative was driving the game somewhere it should
     * not go. campaign has to dismiss MAP 01's briefing to see the HUD, and
     * MAP 01 is hostile the moment that dialog clears -- so the dump cost the
     * configuration its deterministic log, 24 FIRE lines of a 38-line log
     * whose order differs between two unsynchronised runs. Shortening the wait
     * got that to one line moving position, which is chasing the symptom.
     *
     * The HUD is BUILT while the briefing is up; `widgets` simply prefers the
     * dialog root. Asking for it directly means campaign never has to clear
     * the dialog, never reaches combat, and keeps the log it had. */
    if (root && !hud_only) {
        at = (uint32_t)_snprintf(out, cap, "dialog: ");
        wd_node(out, at, cap, root, 0);
        out[cap - 1] = '\0';
        return;
    }

    at = (uint32_t)_snprintf(out, cap, "hud: ");
    for (i = 0; i < 3; i++) {
        const uint8_t *w = *(const uint8_t *const *)kHudRoots[i];

        if (!w)
            continue;
        any = 1;
        at  = wd_node(out, at, cap, w, 0);
    }
    if (!any)
        _snprintf(out, cap, "(no dialog open and no HUD)");
    out[cap - 1] = '\0';
}

static int reply(SOCKET s, const char *fmt, ...)
{
    char    buf[MAX_LINE];
    int     n;
    va_list ap;

    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    buf[n] = '\n';
    return send(s, buf, n + 1, 0);
}

/* Split on spaces in place. Returns the token count. */
static int tokenize(char *line, char **argv, int max)
{
    int argc = 0;

    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
    }
    return argc;
}

static int button_from_name(const char *s)
{
    if (!strcmp(s, "left"))   return 0;
    if (!strcmp(s, "right"))  return 1;
    if (!strcmp(s, "middle")) return 2;
    if (s[0] >= '0' && s[0] <= '9')
        return atoi(s);
    return -1;
}

/* `tap` is a timed hold rather than an immediate press-release pair: the game
 * polls DirectInput once per frame, and an instant release would land between
 * two polls and never be observed. */
#define DEFAULT_TAP_MS 120

/* One frame is plenty; the field consumes a character per pump. */
#define TYPE_GAP_MS    40

static void handle_line(SOCKET s, char *line)
{
    char *argv[8];
    int   argc;

    /* `type <text>` goes first, before the line is split: the text is the rest
     * of the line verbatim, spaces and all, and tokenising would eat them.
     *
     * A text field reads WM_CHAR, which DirectInput never produces -- so the
     * `key` command below, which is what the game polls for menus and
     * movement, cannot fill one in. Posting WM_CHAR to the window directly
     * does, and it needs no X server cooperation at all: no XTEST, no real
     * key events, nothing outside the process. */
    if (!strncmp(line, "type ", 5)) {
        HWND        hwnd = *(HWND *)(uintptr_t)ADDR_HWND;
        const char *p    = line + 5;
        int         n    = 0;

        if (!hwnd) {
            reply(s, "err no window yet");
            return;
        }
        /* The real thing: WM_KEYDOWN, WM_CHAR, WM_KEYUP for every character.
         *
         * Two behaviours of this game decide the timing between them, and
         * getting either wrong is silent.
         *
         * WndProc drops a WM_CHAR when the previously dispatched message was
         * also a WM_CHAR -- reproduced from the original in winproc.cpp,
         * because a genuine keystroke is a keydown followed by a char, so two
         * chars running can only be a duplicate. Post a bare string and its
         * first character arrives and the rest vanish.
         *
         * And PumpMessage calls TranslateMessage, which turns our keydown into
         * a WM_CHAR of its own and appends it to the queue. So every character
         * is delivered twice unless something eats one -- and the duplicate
         * check above is exactly the thing that will, provided the translated
         * copy lands immediately after ours rather than after the keyup.
         *
         * Hence the pause before the keyup: it lets the pump dispatch our char
         * and the translated one back to back, so the game discards the second
         * itself. The two together produce one character, correctly cased,
         * from the full three-message sequence a keyboard would send. */
        /* `\r` is the one escape, and it is here because RETURN is what
         * commits an edit box: EditCharHandler fires the field's handler on
         * WM_CHAR 0x0D and on nothing else, so a DirectInput `key 1c tap`
         * -- which is what the game polls for menus -- never reaches it.
         * A literal backslash is not in the edit box's whitelist, so the
         * escape cannot collide with anything that would have been typed. */
        for (; *p; p++, n++) {
            unsigned char c;
            WPARAM        vk;

            if (p[0] == '\\' && p[1] == 'r') {
                p++;
                c = '\r';
            } else {
                c = (unsigned char)*p;
            }
            vk = (c >= 'a' && c <= 'z') ? (WPARAM)(c - 32) : (WPARAM)c;

            PostMessageA(hwnd, WM_KEYDOWN, vk, 1);
            PostMessageA(hwnd, WM_CHAR, (WPARAM)c, 1);
            Sleep(TYPE_GAP_MS);
            PostMessageA(hwnd, WM_KEYUP, vk, 0xC0000001u);
            Sleep(TYPE_GAP_MS);
        }
        reply(s, "ok typed %d char(s)", n);
        return;
    }

    argc = tokenize(line, argv, 8);

    if (argc == 0)
        return;

    if (!strcmp(argv[0], "ping")) {
        reply(s, "ok pong");
        return;
    }
    if (!strcmp(argv[0], "clear")) {
        input_clear();
        reply(s, "ok cleared");
        return;
    }
    /* `dump <hex addr> [len]` -- read the game's memory. Sprite and glyph data
     * only exists at runtime, so decoding an encoding by hand needs a way to
     * look at the real bytes. Bounded well under the reply buffer. */
    if (!strcmp(argv[0], "dump") && argc >= 2) {
        char           out[MAX_LINE];
        const uint8_t *p = (const uint8_t *)(uintptr_t)strtoul(argv[1], NULL, 16);
        uint32_t       n = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 32;
        uint32_t       i, at = 0;

        if (n == 0 || n > 96)
            n = 96;
        if (IsBadReadPtr(p, n)) {
            reply(s, "err %p not readable for %u bytes", (void *)p, n);
            return;
        }
        for (i = 0; i < n && at + 3 < sizeof out; i++)
            at += (uint32_t)_snprintf(out + at, sizeof out - at, "%02x", p[i]);
        out[at] = '\0';
        reply(s, "ok %p %s", (void *)p, out);
        return;
    }
    /* `poke <hex addr> <hex dword>` -- write one dword of the game's memory.
     *
     * The asymmetry with the keyboard is deliberate and is explained in
     * CLAUDE.md: some globals the game ACCUMULATES and a write survives, and
     * some it overwrites every frame. This is for the first kind, and its one
     * real use is the menu-request pair -- writing ADDR_MENU_REQUEST and
     * ADDR_MENU_REQUEST_SET is the route the game itself takes into a menu
     * screen, and it is the only way to reach the screens no click in this
     * build can arrive at. The multiplayer host options screen is the
     * example: it needs a DirectPlay session that will not open here.
     *
     * It writes through the GAME's memory, so it works identically under
     * AM2_NOPATCH=1 -- which is the whole point, because a screen only one
     * side can reach cannot be compared. */
    if (!strcmp(argv[0], "poke") && argc >= 3) {
        uint32_t *p = (uint32_t *)(uintptr_t)strtoul(argv[1], NULL, 16);
        uint32_t  v = (uint32_t)strtoul(argv[2], NULL, 16);

        if (IsBadWritePtr(p, sizeof *p)) {
            reply(s, "err %p not writable", (void *)p);
            return;
        }
        *p = v;
        reply(s, "ok %p = %08x", (void *)p, v);
        return;
    }
    /* `widgets` -- walk the current dialog's widget tree and describe every
     * node. This exists because the menu layer's defects are TOO SMALL for a
     * whole-frame comparison: STATUS.md's table has a wrong toggle sprite at
     * 212 pixels, a missing WM_CHAR handler at 72, and an unrepainted list row
     * at 0, all under any budget that survives a blinking caret. The state
     * those defects live in is right here in the tree, exactly, and comparing
     * it is not a matter of budgets at all.
     *
     * Pointers are printed as an index in first-seen order rather than as
     * addresses, for the reason tools/actdiff.py renumbers them: a heap
     * address moves between runs and a raw one would differ every time. The
     * sequence restarts on every call.
     *
     * The root is 0x0065A058, which is where the dialog opener at 0x00451210
     * stores whatever dialog is up; there is no tree at all when no dialog is
     * open, and the reply says so rather than inventing one. */
    if (!strcmp(argv[0], "widgets")) {
        char out[MAX_LINE];

        widget_describe(out, sizeof out,
                        argc >= 2 && !strcmp(argv[1], "hud"));
        reply(s, "ok %s", out);
        return;
    }
    /* `peek ADDR [COUNT]` -- COUNT dwords at ADDR, as hex.
     *
     * IT ANSWERS UNDER AM2_NOPATCH=1, which is the whole point: the control
     * socket is harness rather than reconstruction, so the same command
     * compares the ORIGINAL's globals against ours after an identical drive,
     * with no probe code in either side.  Every other way of comparing a
     * global here needs code in the function under test, and that code cannot
     * exist on the original's side.
     *
     * Built for the map layer block, where a reconstruction completes and
     * returns 1 while leaving something downstream cannot use -- and every
     * remaining candidate is a global, so one dump settles what would
     * otherwise be one full A/B run per candidate.
     *
     * Reads only, and refuses a count that would overrun the reply. */
    if (!strcmp(argv[0], "peek")) {
        char     out[MAX_LINE - 8];
        uint32_t addr;
        int      n = 1, i, used = 0;

        if (argc < 2) {
            reply(s, "err peek ADDR [COUNT]");
            return;
        }
        addr = (uint32_t)strtoul(argv[1], NULL, 0);
        if (argc >= 3)
            n = atoi(argv[2]);
        if (n < 1)
            n = 1;
        if (n > 64)
            n = 64;

        out[0] = '\0';
        for (i = 0; i < n; i++) {
            used += snprintf(out + used, sizeof out - (size_t)used, "%08x ",
                             *(const uint32_t *)(uintptr_t)(addr + 4u * (unsigned)i));
            if (used >= (int)sizeof out - 10)
                break;
        }
        reply(s, "ok %s", out);
        return;
    }
    if (!strcmp(argv[0], "counts")) {
        char buf[MAX_LINE - 8];
        trace_describe(buf, sizeof buf, (argc >= 2) ? argv[1] : NULL);
        reply(s, "ok %s", buf[0] ? buf : "(nothing traced)");
        return;
    }
    /* `pointer` -- the mouse/selection layer's own state, as VALUES.
     *
     * It exists because a mutation measured the alternative. HudPostUpdate
     * (0x00413E70) is the per-frame mouse dispatch and its whole output is
     * these globals plus the slots it calls; deleting its ENTIRE BODY moves
     * `bootcamp` from 22 differing pixels to 160 and `campaign` from 2 to 39,
     * both comfortably inside a 500-pixel budget that has to survive a moving
     * scene. So the pixel check covers that function and cannot discriminate
     * on it, which is the same standing MoveStepPoint and NearestClearPoint
     * already have -- and the cure this file already prescribes is to dump the
     * global rather than widen the pixels.
     *
     * The four function-pointer slots ARE printed, because relocations are
     * stripped and an image address is a stable, meaningful datum here: which
     * handler is installed is exactly what the pick/action band decides. The
     * mouse grab is NOT, because it is a heap widget -- only whether it is
     * clear, this layer's own -1, or somebody else's, which is the same rule
     * `ctl widgets` follows for its pointers. */
    if (!strcmp(argv[0], "pointer")) {
        const int32_t *rect  = (const int32_t *)(uintptr_t)ADDR_VIEW_RECT;
        int32_t        grab  = *(const int32_t *)(uintptr_t)ADDR_MOUSE_GRAB;

        reply(s, "ok hover=%08x grab=%s claimed=%d"
                 " drag=%d/%d anchor=%08x band=%d rect=%d,%d,%d,%d"
                 " slots=%08x,%08x,%d,%d pick=%08x act=%08x f14=%08x"
                 " overlay=%d leader=%08x ctx=%08x",
              *(const uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID,
              grab == 0 ? "none" : (grab == -1 ? "map" : "widget"),
              *(const int32_t *)(uintptr_t)ADDR_MOUSE_CLAIMED,
              *(const int32_t *)(uintptr_t)ADDR_DRAG_ACTIVE,
              *(const int32_t *)(uintptr_t)ADDR_CLICK_ENABLED,
              *(const uint32_t *)(uintptr_t)ADDR_DRAG_ANCHOR,
              *(const int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON,
              rect[0], rect[1], rect[2], rect[3],
              *(const uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT0,
              *(const uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT1,
              *(const int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT2,
              *(const int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT3,
              *(const uint32_t *)(uintptr_t)ADDR_POINTER_PICK,
              *(const uint32_t *)(uintptr_t)ADDR_POINTER_ACTION,
              *(const uint32_t *)(uintptr_t)ADDR_POINTER_F14,
              *(const int32_t *)(uintptr_t)ADDR_POINTER_OVERLAY,
              *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID,
              *(const uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A);
        return;
    }
    if (!strcmp(argv[0], "state")) {
        char buf[MAX_LINE - 8];
        input_describe(buf, sizeof buf);
        reply(s, "ok %s", buf);
        return;
    }
    if (!strcmp(argv[0], "key") && argc >= 3) {
        uint8_t  dik = input_dik_from_name(argv[1]);
        uint32_t ms  = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10)
                                   : DEFAULT_TAP_MS;
        if (!dik) {
            reply(s, "err unknown key '%s'", argv[1]);
            return;
        }
        if (!strcmp(argv[2], "down"))
            input_key(dik, 1, (argc >= 4) ? ms : 0);
        else if (!strcmp(argv[2], "up"))
            input_key(dik, 0, 0);
        else if (!strcmp(argv[2], "tap"))
            input_key(dik, 1, ms);
        else {
            reply(s, "err expected down|up|tap");
            return;
        }
        reply(s, "ok key %s(%02x) %s", argv[1], dik, argv[2]);
        return;
    }
    /* `cursor` reads the pointer, `cursor <x> <y>` puts it somewhere.
     *
     * This exists because the reconstruction owns the mouse state now.
     * UpdateMouseState is what turns DirectInput's relative deltas into the
     * absolute cursor at ADDR_CURSOR_POINT, which 32 sites in the image read
     * to decide what the pointer is over -- so writing that trio IS placing
     * the pointer, and no delta, no acceleration curve and no screenshot are
     * involved.
     *
     * What it replaces was genuinely hard. The game reads BUFFERED DirectInput,
     * so the socket could only offer relative motion; Wine's acceleration is
     * non-linear on top of that, so a computed delta overshot; and the fix was
     * tools/point.py closing the loop on a screenshot by finding the pointer by
     * colour. That could not work at all where the cursor is not drawn -- the
     * Boot Camp instruction sign and the briefing screen both defeated it.
     *
     * These are the GAME's globals, not ours, so this works identically with
     * the reconstruction installed and under AM2_NOPATCH=1. That is what makes
     * it usable for driving both halves of an A/B: the same two numbers land in
     * the same two places either way, where a relative delta depended on
     * acceleration and arrived somewhere slightly different each run.
     *
     * It does NOT exercise PollMouse or UpdateMouseState -- nothing is read
     * from the device. Use `mouse move` when the input path itself is what is
     * under test. */
    /* `keys` -- what the GAME's keyboard state actually is.
     *
     * The counterpart of `cursor`, and it exists for the same reason: `state`
     * reports what the harness is INJECTING, which is its intent and not the
     * outcome. If the DirectInput hook were ever bypassed -- the failure
     * tools/checkhooks.py guards, which no A/B can see because both sides
     * would be equally undriven -- `state` would keep cheerfully reporting a
     * key as held while the game saw nothing at all. This reads the other end.
     *
     * Two arrays, both the game's own. ADDR_KEYS_NOW_PTR is whichever of the
     * two 256-byte buffers PollKeyboard filled this poll -- it swaps them, so
     * the pointer has to be followed rather than assumed -- and the down bit
     * is DirectInput's 0x80. ADDR_KEY_PRESSED is the edge-and-auto-repeat
     * array, set for one poll when a key goes down and again on each repeat,
     * which is what most of the game actually tests.
     *
     * There is deliberately no way to SET them here, and that is the
     * difference from `cursor`. The cursor ACCUMULATES -- UpdateMouseState
     * adds the deltas to what is already there, so a write survives and is
     * the next starting point. The key buffer is REPLACED wholesale from
     * GetDeviceState on every poll, so a poke would last until the next frame
     * and no longer. Keys go in through `key`, which the harness already
     * releases on a poll rather than a timer precisely so a tap cannot fall
     * between two of them. */
    if (!strcmp(argv[0], "keys")) {
        const uint8_t *now = *(const uint8_t *const *)(uintptr_t)ADDR_KEYS_NOW_PTR;
        const int32_t *hit = (const int32_t *)(uintptr_t)ADDR_KEY_PRESSED;
        char           out[MAX_LINE];
        uint32_t       at = 0;
        int32_t        i;

        if (!now || IsBadReadPtr(now, AM2_KEY_STATES)) {
            reply(s, "err keyboard state not up yet");
            return;
        }
        at += (uint32_t)_snprintf(out + at, sizeof out - at, "down:");
        for (i = 0; i < AM2_KEY_STATES && at < sizeof out - 16; i++)
            if (now[i] & AM2_KEY_DOWN)
                at += (uint32_t)_snprintf(out + at, sizeof out - at, " %02x", i);
        at += (uint32_t)_snprintf(out + at, sizeof out - at, " pressed:");
        for (i = 0; i < AM2_KEY_STATES && at < sizeof out - 8; i++)
            if (hit[i])
                at += (uint32_t)_snprintf(out + at, sizeof out - at, " %02x", i);
        out[sizeof out - 1] = '\0';
        reply(s, "ok %s", out);
        return;
    }
    if (!strcmp(argv[0], "cursor")) {
        int32_t       *cx   = (int32_t *)(uintptr_t)ADDR_CURSOR_X;
        int32_t       *cy   = (int32_t *)(uintptr_t)ADDR_CURSOR_Y;
        int16_t       *pt   = (int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
        const int32_t *clip = (const int32_t *)(uintptr_t)ADDR_SCREEN_CLIP;

        if (argc >= 3) {
            int32_t x = atoi(argv[1]);
            int32_t y = atoi(argv[2]);

            /* The same clamp UpdateMouseState applies, so a poke cannot put
             * the pointer anywhere a real move could not. */
            if (x < clip[0])
                x = clip[0];
            if (x > clip[2] - 1)
                x = clip[2] - 1;
            if (y < clip[1])
                y = clip[1];
            if (y > clip[3] - 1)
                y = clip[3] - 1;

            *cx = x;
            *cy = y;
            pt[0] = (int16_t)x;
            pt[1] = (int16_t)y;
            /* What a real move sets, so hover and repaint behave. PollMouse
             * clears it again next frame, exactly as after a real one. */
            *(int32_t *)(uintptr_t)ADDR_MOUSE_MOVED = 1;
        }
        reply(s, "ok cursor %d %d", (int)*cx, (int)*cy);
        return;
    }
    if (!strcmp(argv[0], "mouse") && argc >= 2) {
        if (!strcmp(argv[1], "move") && argc >= 4) {
            input_mouse_move(atoi(argv[2]), atoi(argv[3]),
                             argc >= 5 ? atoi(argv[4]) : 0);
            reply(s, "ok mouse move");
            return;
        }
        if (argc >= 3) {
            int      btn = button_from_name(argv[1]);
            uint32_t ms  = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10)
                                       : DEFAULT_TAP_MS;
            if (btn < 0) {
                reply(s, "err unknown button '%s'", argv[1]);
                return;
            }
            if (!strcmp(argv[2], "down"))
                input_button(btn, 1, (argc >= 4) ? ms : 0);
            else if (!strcmp(argv[2], "up"))
                input_button(btn, 0, 0);
            else if (!strcmp(argv[2], "tap"))
                input_button(btn, 1, ms);
            else {
                reply(s, "err expected down|up|tap");
                return;
            }
            reply(s, "ok mouse %d %s", btn, argv[2]);
            return;
        }
    }
    reply(s, "err unknown command '%s'", argv[0]);
}

/* One client is served at a time, which is ample for scripting -- but without a
 * timeout a client that connects and then stalls (or is killed mid-request, or
 * whose socket is left dangling) wedges the listener for the life of the
 * process. New connections then sit in the backlog and time out, which looks
 * exactly like a hung game. A receive timeout makes that self-healing. */
#define CLIENT_IDLE_MS 15000

static void serve(SOCKET client)
{
    char buf[MAX_LINE];
    int  used = 0;
    DWORD tmo = CLIENT_IDLE_MS;

    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);

    for (;;) {
        int n = recv(client, buf + used, (int)(sizeof buf - used - 1), 0);
        char *nl;

        if (n <= 0)
            return;                      /* closed, error, or idle timeout */
        used += n;
        buf[used] = '\0';

        while ((nl = strchr(buf, '\n')) != NULL) {
            int len = (int)(nl - buf);
            *nl = '\0';
            if (len && buf[len - 1] == '\r')
                buf[len - 1] = '\0';
            handle_line(client, buf);
            memmove(buf, nl + 1, (size_t)(used - len - 1));
            used -= len + 1;
            buf[used] = '\0';
        }
        if (used >= (int)sizeof buf - 1)
            used = 0;                       /* overlong line: drop it */
    }
}

static DWORD WINAPI control_thread(LPVOID unused)
{
    (void)unused;

    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        SOCKET client = accept(g_listen, NULL, NULL);
        if (client == INVALID_SOCKET)
            break;
        serve(client);
        closesocket(client);
    }
    return 0;
}

int control_start(void)
{
    const char        *opt = getenv("AM2_CONTROL");
    const char        *pstr = getenv("AM2_CTL_PORT");
    uint16_t           port = pstr && *pstr ? (uint16_t)atoi(pstr) : DEFAULT_PORT;
    WSADATA            wsa;
    struct sockaddr_in addr;
    int                yes = 1;

    if (!opt || *opt != '1') {
        hooklog("control: disabled (set AM2_CONTROL=1 to enable)");
        return 0;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        hooklog("control: WSAStartup failed");
        return 1;
    }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) {
        hooklog("control: socket() failed");
        return 1;
    }
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* loopback only */

    if (bind(g_listen, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(g_listen, 4) != 0) {
        hooklog("control: bind/listen on port %u failed (%d)", port, WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return 1;
    }

    g_thread = CreateThread(NULL, 0, control_thread, NULL, 0, NULL);
    if (!g_thread) {
        hooklog("control: CreateThread failed");
        return 1;
    }
    hooklog("control: listening on 127.0.0.1:%u", port);
    return 0;
}

void control_stop(void)
{
    InterlockedExchange(&g_stop, 1);
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
}
