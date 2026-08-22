#include "trace.h"
#include "hooklog.h"

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Both limits have to move together: the arena holds ARENA_BYTES/STUB_BYTES
 * stubs, so whichever is smaller is the real cap.
 *
 * This has now overflowed twice. It was 64, the reconstruction went past it,
 * and the last nine patches installed correctly -- trace_wrap falls back to
 * the unwrapped function -- but vanished from `counts`, which reads exactly
 * like they were never installed. Raised to 128, and the run of pure-leaf
 * reconstructions went past that too: about sixty functions lost their
 * counters at once, and the only sign was "table full, cannot wrap ..." in a
 * log nobody reads until they are already suspicious.
 *
 * Sized well clear of the reconstruction now rather than just above it. If it
 * ever fills again the message is real and the limits want raising, not the
 * message suppressing. */
#define MAX_TRACED   512
#define MAX_ARGS     8
#define LOG_FIRST_N  12
#define STUB_BYTES   27
#define ARENA_BYTES  16384

struct entry {
    const char *name;
    int32_t     nargs;
    uint32_t    calls;
};

static struct entry g_entries[MAX_TRACED];
static int32_t      g_count;
static uint8_t     *g_arena;
static size_t       g_arena_used;
static int          g_enabled = -1;

int trace_enabled(void)
{
    if (g_enabled < 0) {
        const char *opt = getenv("AM2_TRACE");
        g_enabled = (opt && *opt == '1');
    }
    return g_enabled;
}

/* A dword that points at a short, printable, NUL-terminated string is almost
 * always meant to be read as one. Rendering those inline is what makes a
 * generic hex trace actually legible. */
static const char *as_string(uint32_t v)
{
    const char *p = (const char *)(uintptr_t)v;
    int         i;

    if (v < 0x10000 || IsBadReadPtr(p, 1))
        return NULL;
    for (i = 0; i < 96; i++) {
        unsigned char c;

        if (IsBadReadPtr(p + i, 1))
            return NULL;
        c = (unsigned char)p[i];
        if (c == '\0')
            return i > 0 ? p : NULL;
        /* Nearly every format string in this binary ends in a newline, so
         * tab/CR/LF have to count as printable or none of them render. */
        if (c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c < 0x20 || c > 0x7E)
            return NULL;
    }
    return NULL;
}

/* Render a recovered string on one line, so an embedded newline cannot split a
 * trace record in two. Truncates rather than overflowing. */
static void escape(char *out, size_t cap, const char *in)
{
    size_t at = 0;

    for (; *in && at + 3 < cap; in++) {
        char c = *in;
        if (c == '\n' || c == '\r' || c == '\t' || c == '"' || c == '\\') {
            out[at++] = '\\';
            out[at++] = (c == '\n') ? 'n'
                      : (c == '\r') ? 'r'
                      : (c == '\t') ? 't'
                      : c;
        } else {
            out[at++] = c;
        }
    }
    out[at] = '\0';
}

/* Called by every generated stub. `args` points at the first argument dword. */
static void __cdecl trace_enter(uint32_t id, uint32_t *args)
{
    char        buf[512];
    size_t      at = 0;
    struct entry *e;
    int32_t     i;

    if (id >= (uint32_t)g_count)
        return;
    e = &g_entries[id];
    e->calls++;

    /* Log the first few calls in full, then count only. During gameplay a hot
     * function runs tens of thousands of times a second; logging every one
     * buries the interesting lines and slows the game badly. The first few
     * show the argument shape, and the count is what the survey wants. */
    if (e->calls > LOG_FIRST_N)
        return;

    at += (size_t)_snprintf(buf + at, sizeof buf - at, "trace %s#%u(",
                            e->name, e->calls);

    for (i = 0; i < e->nargs && at < sizeof buf - 32; i++) {
        uint32_t    v = args[i];
        const char *s = as_string(v);

        if (i)
            at += (size_t)_snprintf(buf + at, sizeof buf - at, ", ");
        /* Always show the value. A pointer whose target happens to be printable
         * is still a pointer, and hiding it behind the text loses the one thing
         * needed to go and look at the memory. */
        at += (size_t)_snprintf(buf + at, sizeof buf - at, "%08x", v);
        if (s) {
            char esc[128];
            escape(esc, sizeof esc, s);
            at += (size_t)_snprintf(buf + at, sizeof buf - at, "=\"%s\"", esc);
        }
    }
    _snprintf(buf + at, sizeof buf - at, ")");
    buf[sizeof buf - 1] = '\0';

    hooklog_raw(buf);
}

void trace_describe(char *out, uint32_t cap, const char *want)
{
    uint32_t at = 0;
    int32_t  i;
    int32_t  shown = 0;

    if (!out || cap < 8)
        return;
    if (want && !want[0])
        want = NULL;
    out[0] = '\0';
    for (i = 0; i < g_count && at < cap - 32; i++) {
        if (want && !strstr(g_entries[i].name, want))
            continue;
        at += (uint32_t)_snprintf(out + at, cap - at, "%s%s=%u",
                                  shown ? " " : "", g_entries[i].name,
                                  g_entries[i].calls);
        shown++;
    }
    /* Say so rather than just stopping. A list that ends early is
     * indistinguishable from a function that was never patched, and that is a
     * bad thing to have to guess about -- the whole point of the counts is to
     * tell "not called" apart from "not installed". */
    if (i < g_count)
        _snprintf(out + at, cap - at, " (+%d truncated)", g_count - i);
    out[cap - 1] = '\0';
}

void trace_report(void)
{
    int32_t i;

    if (!g_count)
        return;
    hooklog("trace: call totals for this session --");
    for (i = 0; i < g_count; i++)
        hooklog("  %-20s %10u", g_entries[i].name, g_entries[i].calls);
}

static uint8_t *arena_alloc(size_t n)
{
    if (!g_arena) {
        g_arena = VirtualAlloc(NULL, ARENA_BYTES, MEM_COMMIT | MEM_RESERVE,
                               PAGE_EXECUTE_READWRITE);
        if (!g_arena)
            return NULL;
    }
    if (g_arena_used + n > ARENA_BYTES)
        return NULL;

    {
        uint8_t *p = g_arena + g_arena_used;
        g_arena_used += n;
        return p;
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

const void *trace_wrap(const void *fn, const char *name, int32_t nargs)
{
    const void *stub;

    if (!trace_enabled())
        return fn;
    stub = trace_make_stub(fn, name, nargs);
    return stub ? stub : fn;
}

const void *trace_make_stub(const void *fn, const char *name, int32_t nargs)
{
    uint8_t *s;
    int32_t  id;

    if (g_count >= MAX_TRACED) {
        hooklog("trace: table full, cannot wrap %s", name);
        return NULL;
    }
    if (nargs < 0)
        nargs = 0;
    if (nargs > MAX_ARGS)
        nargs = MAX_ARGS;

    s = arena_alloc(STUB_BYTES);
    if (!s) {
        hooklog("trace: out of stub space for %s", name);
        return NULL;
    }

    id = g_count++;
    g_entries[id].name  = name;
    g_entries[id].nargs = nargs;
    g_entries[id].calls = 0;

    /* On entry [esp] is the return address and the arguments start at [esp+4].
     * pushfd + pushad move esp down 36, so the arguments sit at [esp+40].
     *
     *   9C                 pushfd
     *   60                 pushad
     *   8D 44 24 28        lea   eax, [esp+40]      ; &args
     *   50                 push  eax
     *   68 <id>            push  id
     *   E8 <rel>           call  trace_enter
     *   83 C4 08           add   esp, 8
     *   61                 popad
     *   9D                 popfd
     *   E9 <rel>           jmp   fn                 ; stack untouched
     */
    s[0] = 0x9C;
    s[1] = 0x60;
    s[2] = 0x8D; s[3] = 0x44; s[4] = 0x24; s[5] = 0x28;
    s[6] = 0x50;
    s[7] = 0x68; put32(s + 8, (uint32_t)id);
    s[12] = 0xE8; put32(s + 13, (uint32_t)((const uint8_t *)trace_enter - (s + 17)));
    s[17] = 0x83; s[18] = 0xC4; s[19] = 0x08;
    s[20] = 0x61;
    s[21] = 0x9D;
    s[22] = 0xE9; put32(s + 23, (uint32_t)((const uint8_t *)fn - (s + 27)));

    FlushInstructionCache(GetCurrentProcess(), s, STUB_BYTES);
    return s;
}
