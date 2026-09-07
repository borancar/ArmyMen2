/* loader.cpp -- the ORIGINAL ArmyMen2.exe, loaded by us and run over
 * src/platform, with no Wine anywhere.
 *
 * The native build links the reconstruction against the platform layer.
 * This build links the platform layer alone and loads the retail PE into
 * the process the way Windows would: the image is mapped at the base its
 * pointers were written for -- 0x00400000, which an i386 ELF placed at
 * 0x00700000 leaves free -- its sections are copied in, every slot of its
 * import table is filled with the address of our implementation of that
 * name, and its entry point, the MSVC 6 CRT's WinMainCRTStartup, is
 * called. From then on the retail code runs unmodified, and every call it
 * makes out of itself lands in the same src/platform the reconstruction
 * runs over.
 *
 * What the CRT and the game need from the process beyond the imports:
 *
 *   fs:[0]      The SEH chain head. 340 sites in the image push and pop
 *               it and nothing reads any other TEB field, so each thread
 *               gets a page whose first dword is the end-of-chain marker,
 *               reached through a GDT entry set_thread_area allocates.
 *               Threads inherit their creator's, and the ones the game
 *               makes get their own through am2_thread_attach.
 *   the cwd     The original reads its install directory with getcwd, so
 *               AM2_GAMEDIR is entered before the entry point runs.
 *   the log     The retail logger at ADDR_LOG is a bare `ret`, and the
 *               harness patches it to capture what the game writes. So
 *               does this, to the file AM2_LOG names -- which is how a
 *               hybrid run is compared against a Wine run of the same
 *               binary. Unset, the line is dropped as the retail stub
 *               drops it.
 *
 * sdl.cpp's main calls WinMain, and this file IS WinMain.
 */
#include "../platform/platform.h"
#include "../inject/orig.h"
#include "../platform/crt/crt.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <asm/ldt.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ---- the PE, as much of it as the loader reads ------------------------- */

typedef struct AM2_PeFileHeader {
    uint16_t Machine, NumberOfSections;
    uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    uint16_t SizeOfOptionalHeader, Characteristics;
} AM2_PeFileHeader;

typedef struct AM2_PeDataDir { uint32_t VirtualAddress, Size; } AM2_PeDataDir;

typedef struct AM2_PeOptionalHeader {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint, BaseOfCode, BaseOfData, ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion;
    uint16_t MajorImageVersion, MinorImageVersion;
    uint16_t MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint32_t SizeOfStackReserve, SizeOfStackCommit;
    uint32_t SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags, NumberOfRvaAndSizes;
    AM2_PeDataDir DataDirectory[16];
} AM2_PeOptionalHeader;

typedef struct AM2_PeSection {
    char     Name[8];
    uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData;
    uint32_t PointerToRelocations, PointerToLinenumbers;
    uint16_t NumberOfRelocations, NumberOfLinenumbers;
    uint32_t Characteristics;
} AM2_PeSection;

typedef struct AM2_PeImport {
    uint32_t OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk;
} AM2_PeImport;

#define AM2_PE_DIR_IMPORT 1
#define AM2_IMAGE_BASE 0x00400000u

/* ---- diagnostics ----------------------------------------------------------------- */

static void am2_hybrid_die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void am2_hybrid_die(const char *fmt, ...)
{
    va_list ap;

    fputs("hybrid: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ---- the TEB ---------------------------------------------------------------------- */

/* One page a thread: fs:[0] the end-of-chain marker, fs:[0x18] itself,
 * the rest zero. set_thread_area gives it a GDT slot the kernel keeps per
 * thread, so the same selector means a different page on each. */
static void am2_hybrid_teb_attach(void)
{
    static __thread int32_t done;
    uint32_t        *teb;
    struct user_desc d;
    uint16_t         sel;

    if (done)
        return;
    done = 1;
    teb = (uint32_t *)calloc(1024, sizeof *teb);
    if (!teb)
        am2_hybrid_die("no memory for a TEB");
    teb[0] = 0xFFFFFFFFu;
    teb[6] = (uint32_t)(uintptr_t)teb;
    memset(&d, 0, sizeof d);
    d.entry_number = (unsigned int)-1;
    d.base_addr = (unsigned int)(uintptr_t)teb;
    d.limit = 0xFFFFF;
    d.seg_32bit = 1;
    d.limit_in_pages = 1;
    d.useable = 1;
    if (syscall(SYS_set_thread_area, &d) != 0)
        am2_hybrid_die("set_thread_area: %s", strerror(errno));
    sel = (uint16_t)(d.entry_number * 8 + 3);
    __asm__ volatile("mov %0, %%fs" : : "r"(sel));
}

/* Before main, so every thread SDL or anyone else creates inherits a
 * usable fs from the one that made it. */
__attribute__((constructor)) static void am2_hybrid_init(void)
{
    am2_hybrid_teb_attach();
    am2_thread_attach = am2_hybrid_teb_attach;
}

/* ---- missing imports ----------------------------------------------------------------- */

/* An import we do not provide gets a stub of its own that names it and
 * stops, rather than the shared trap that answers 0: a silent 0 from,
 * say, CreateFileA is a game that cannot explain what went wrong. */
static void __attribute__((noreturn)) am2_hybrid_missing(const char *what)
{
    am2_hybrid_die("the game called %s, which this build does not provide", what);
}

static uint8_t *am2_stub_page;
static size_t   am2_stub_used;

static const void *am2_hybrid_make_stub(const char *module, const char *name)
{
    char    *what = (char *)malloc(strlen(module) + strlen(name) + 2);
    uint8_t *s;
    uint32_t rel;

    if (!am2_stub_page) {
        am2_stub_page = (uint8_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (am2_stub_page == MAP_FAILED)
            am2_hybrid_die("cannot map the import stubs");
    }
    if (!what || am2_stub_used + 16 > 4096)
        am2_hybrid_die("too many missing imports");
    sprintf(what, "%s!%s", module, name);
    s = am2_stub_page + am2_stub_used;
    am2_stub_used += 16;
    s[0] = 0x68;                                   /* push what */
    memcpy(s + 1, &what, 4);
    s[5] = 0xE8;                                   /* call am2_hybrid_missing */
    rel = (uint32_t)((uintptr_t)&am2_hybrid_missing - (uintptr_t)(s + 10));
    memcpy(s + 6, &rel, 4);
    s[10] = 0xCC;
    return s;
}

/* ---- the game's log ------------------------------------------------------------------ */

#ifdef AM2_DEVTOOLS
extern "C" void input_init(void);
extern "C" int32_t WINAPI am2_crtcheck_main(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int32_t show);
extern "C" int  control_start(void);
#endif

static FILE *am2_hybrid_logfile(void)
{
    static FILE   *fh;
    static int32_t tried;

    if (!tried) {
        const char *path = getenv("AM2_LOG");
        tried = 1;
        if (path && *path)
            fh = !strcmp(path, "-") ? stderr : fopen(path, "w");
    }
    return fh;
}

/* cdecl and variadic, as the retail stub's callers expect. Two of the
 * image's call sites reach it with NO argument frame -- CLAUDE.md records
 * 0x00460290 and a widget vtable slot folded onto the same `ret` -- so
 * the "format" is whatever sits above the return address and is checked
 * before it is used, as src/standalone/runtime.cpp checks it. */
static void __attribute__((cdecl)) am2_hybrid_log(const char *fmt, ...)
{
    FILE   *fh = am2_hybrid_logfile();
    va_list ap;
    int32_t i;

    if (!fh || IsBadStringPtrA(fmt, 4096))
        return;
    for (i = 0; i < 8 && fmt[i]; i++) {
        unsigned char c = (unsigned char)fmt[i];
        if ((c < 0x20 && c != '\n' && c != '\t' && c != '\r') || c >= 0x7F)
            return;
    }
    va_start(ap, fmt);
    vfprintf(fh, fmt, ap);
    va_end(ap);
    fflush(fh);
}

/* The harness's own lines -- the control socket's -- into the same file,
 * one per call, as src/standalone/runtime.cpp's hooklog does. */
extern "C" void am2_hybrid_log_line(const char *fmt, va_list ap)
{
    FILE *fh = am2_hybrid_logfile();

    if (!fh)
        return;
    vfprintf(fh, fmt, ap);
    fputc('\n', fh);
    fflush(fh);
}

/* The reconstruction's AngleOfDelta over the image's own tables, with a log
 * line per call; dev builds only, installed by AM2_TRACE_ANGLE. */
extern "C" uint8_t __cdecl am2_hybrid_trace_angle_between(const int16_t *from, const int16_t *to)
{
    int32_t dx = (int32_t)to[0] - (int32_t)from[0];
    int32_t dy = (int32_t)to[1] - (int32_t)from[1];
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const int8_t *atanCos = (const int8_t *)(uintptr_t)ADDR_TRIG_ATAN_COS;
    const int8_t *atanSin = (const int8_t *)(uintptr_t)ADDR_TRIG_ATAN_SIN;
    uint8_t h;

    if (adx > ady) {
        h = (uint8_t)atanCos[(dy << 9) / dx];
    } else if (dx == 0) {
        h = dy < 0 ? 0u : 0x80u;
        if (am2_trace_window())
            fprintf(stderr, "ANGLE pump %u from=(%d,%d) to=(%d,%d) -> %d\n", (unsigned)am2_host_pump_number(),
                from[0], from[1], to[0], to[1], h);
        return h;
    } else {
        h = (uint8_t)atanSin[(dx << 9) / dy];
    }
    if (dx > 0)
        h = (uint8_t)(h + 0x80u);
    if (am2_trace_window())
        fprintf(stderr, "ANGLE pump %u from=(%d,%d) to=(%d,%d) -> %d\n", (unsigned)am2_host_pump_number(),
            from[0], from[1], to[0], to[1], h);
    return h;
}

/* The reconstruction's DrawMapObject (win32/mapdraw.cpp) with a log line
 * per draw, calling the ORIGINAL DrawSpriteClipped; dev builds only. */
typedef void (__cdecl *am2_draw_sprite_clipped_fn)(void *spr, int32_t x, int32_t y, const RECT *src, int32_t mode);
extern "C" void __cdecl am2_hybrid_trace_draw_map_object(void *obj, const RECT *world)
{
    uint8_t    *o      = (uint8_t *)obj;
    const RECT *bounds = (const RECT *)(o + OBJ_OFF_BOUNDS);
    const RECT *view   = (const RECT *)(uintptr_t)ADDR_VIEW_ORIGIN_X;
    RECT        clip, src;
    uint8_t    *spr;

    if (!IntersectRect(&clip, bounds, world))
        return;
    if (!(*(const uint8_t *)(o + MAPOBJ_OFF_FLAGS) & MAPOBJ_FLAG_VISIBLE))
        return;
    src.left = clip.left - bounds->left; src.top = clip.top - bounds->top;
    src.right = clip.right - bounds->left; src.bottom = clip.bottom - bounds->top;
    if (src.top == src.bottom || src.left == src.right)
        return;
    spr = *(uint8_t **)(o + MAPOBJ_OFF_SPRITE);
    *(uint8_t **)(spr + 0x34) = *(uint8_t **)(o + MAPOBJ_OFF_LUT);
    *(void **)(spr + 0x38)    = *(void **)(o + MAPOBJ_OFF_PALETTE);
    if (am2_trace_window())
        fprintf(stderr, "MAPOBJ pump %u bounds=%ld,%ld-%ld,%ld world=%ld,%ld-%ld,%ld clip=%ld,%ld-%ld,%ld dst=%ld,%ld spr=%u fmt=%u flags=%x\n",
            (unsigned)am2_host_pump_number(), (long)bounds->left, (long)bounds->top, (long)bounds->right, (long)bounds->bottom,
            (long)world->left, (long)world->top, (long)world->right, (long)world->bottom,
            (long)clip.left, (long)clip.top, (long)clip.right, (long)clip.bottom,
            (long)(clip.left - view->left), (long)(clip.top - view->top),
            *(unsigned *)spr, *(unsigned *)(spr + 8), *(unsigned *)(spr + 0xC));
    ((am2_draw_sprite_clipped_fn)(uintptr_t)ADDR_DRAW_SPRITE_CLIPPED)(spr, clip.left - view->left, clip.top - view->top, &src, 0);
}

/* The reconstruction's DrawSprite (win32/sprite.cpp) with a log line per
 * draw, calling the ORIGINAL ClipRect and DrawSpriteClipped. */
typedef int32_t (__cdecl *am2_clip_rect_fn)(const RECT *src, const RECT *clip, int32_t *x, int32_t *y, RECT *out);
extern "C" void __cdecl am2_hybrid_trace_draw_sprite(uint8_t *spr, int32_t x, int32_t y, int32_t mode)
{
    RECT clipped;
    if (!spr)
        return;
    if (!*(void **)(spr + 0x10) && !*(void **)(spr + 0x30))
        return;
    x -= *(const int16_t *)(spr + 0x24);
    y -= *(const int16_t *)(spr + 0x26);
    if (!((am2_clip_rect_fn)(uintptr_t)ADDR_CLIP_RECT)((const RECT *)(spr + 0x14), (const RECT *)(uintptr_t)ADDR_SCREEN_CLIP, &x, &y, &clipped))
        return;
    if (am2_trace_window())
        fprintf(stderr, "SPRITE pump %u id=%u fmt=%u flags=%x at=%d,%d src=%ld,%ld-%ld,%ld mode=%d\n",
            (unsigned)am2_host_pump_number(), *(unsigned *)spr, *(unsigned *)(spr + 8), *(unsigned *)(spr + 0xC),
            x, y, (long)clipped.left, (long)clipped.top, (long)clipped.right, (long)clipped.bottom, mode);
    ((am2_draw_sprite_clipped_fn)(uintptr_t)ADDR_DRAW_SPRITE_CLIPPED)(spr, x, y, &clipped, mode);
}

static int32_t am2_hybrid_depth_project(int32_t dx, float slope, int32_t y)
{
    return (int16_t)(int32_t)((float)dx * slope) + y;
}

extern "C" int32_t __cdecl am2_hybrid_trace_depth_compare(const uint8_t *pa, const uint8_t *pb)
{
    int32_t r;
    if (!pa || !pb)
        return 0;
    {
        int16_t la = *(const int16_t *)(pa + 0x26), lb = *(const int16_t *)(pb + 0x26);
        float   sa = *(const float *)(pa + 0x28),   sb = *(const float *)(pb + 0x28);
        int32_t ax = *(const int16_t *)(pa + 0x1C), ay = *(const int16_t *)(pa + 0x1E);
        int32_t bx = *(const int16_t *)(pb + 0x1C), by = *(const int16_t *)(pb + 0x1E);
        r = 0;
        if (la > 0 && lb > 0 && la != lb)
            r = la > lb ? 1 : -1;
        else if (sa != 0.0f && sb != 0.0f) {
            int32_t fromA = am2_hybrid_depth_project(bx - ax, sa, ay);
            int32_t fromB = am2_hybrid_depth_project(ax - bx, sb, by);
            int32_t aInFront = fromA > by;
            if (ay > fromB && aInFront) r = 1;
            else if (ay < fromB && !aInFront) r = -1;
        } else if (sa != 0.0f) {
            int32_t p = am2_hybrid_depth_project(bx - ax, sa, ay);
            r = p > by ? 1 : p == by ? ((pb < pa) ? 1 : -1) : -1;
        } else if (sb != 0.0f) {
            int32_t p = am2_hybrid_depth_project(ax - bx, sb, by);
            r = ay > p ? 1 : ay == p ? ((pb < pa) ? 1 : -1) : -1;
        }
        if (r == 0)
            r = (int16_t)ay > (int16_t)by ? 1 : (int16_t)ay < (int16_t)by ? -1 : ((pb < pa) ? 1 : -1);
        if (am2_trace_window())
            fprintf(stderr, "DEPTH pump %u a=(l%d s%g %d,%d) b=(l%d s%g %d,%d) -> %d\n", (unsigned)am2_host_pump_number(),
                la, (double)sa, ax, ay, lb, (double)sb, bx, by, r);
    }
    return r;
}

static void am2_hybrid_patch_jmp(uintptr_t at, const void *to)
{
    uint8_t *p = (uint8_t *)at;
    uint32_t rel = (uint32_t)((uintptr_t)to - (at + 5));

    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
}

/* ---- the cursor ------------------------------------------------------------------------ */

static void am2_hybrid_cursor_query(int32_t *x, int32_t *y)
{
    *x = *(const int32_t *)(uintptr_t)ADDR_CURSOR_X;
    *y = *(const int32_t *)(uintptr_t)ADDR_CURSOR_Y;
}


/* The replay's `cursor`: the three globals the control socket's `cursor`
 * writes, clamped as UpdateMouseState clamps, and the moved flag a real
 * move sets. Kept identical to control.c's on purpose. */
static void am2_hybrid_cursor_set(int32_t x, int32_t y)
{
    int32_t       *cx   = (int32_t *)(uintptr_t)ADDR_CURSOR_X;
    int32_t       *cy   = (int32_t *)(uintptr_t)ADDR_CURSOR_Y;
    int16_t       *pt   = (int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
    const int32_t *clip = (const int32_t *)(uintptr_t)ADDR_SCREEN_CLIP;

    if (x < clip[0]) x = clip[0];
    if (x > clip[2] - 1) x = clip[2] - 1;
    if (y < clip[1]) y = clip[1];
    if (y > clip[3] - 1) y = clip[3] - 1;
    *cx = x;
    *cy = y;
    pt[0] = (int16_t)x;
    pt[1] = (int16_t)y;
    *(int32_t *)(uintptr_t)ADDR_MOUSE_MOVED = 1;
}

/* ---- the fault line --------------------------------------------------------------------- */

static LONG CALLBACK am2_hybrid_fault(EXCEPTION_POINTERS *ep)
{
    uintptr_t  eip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    uintptr_t *esp = (uintptr_t *)ep->ContextRecord->Esp;
    int32_t    i;

    fprintf(stderr, "hybrid: fault at 0x%08lx (%s) eax=%08lx ebx=%08lx ecx=%08lx "
            "edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx\n",
            (unsigned long)eip,
            eip >= AM2_IMAGE_BASE && eip < AM2_IMAGE_BASE + 0x267000 ? "in the image" : "outside the image",
            (unsigned long)ep->ContextRecord->Eax, (unsigned long)ep->ContextRecord->Ebx,
            (unsigned long)ep->ContextRecord->Ecx, (unsigned long)ep->ContextRecord->Edx,
            (unsigned long)ep->ContextRecord->Esi, (unsigned long)ep->ContextRecord->Edi,
            (unsigned long)ep->ContextRecord->Ebp, (unsigned long)esp);
    if (esp && !IsBadReadPtr(esp, 64)) {
        fputs("hybrid: stack:", stderr);
        for (i = 0; i < 16; i++)
            fprintf(stderr, " %08lx", (unsigned long)esp[i]);
        fputc('\n', stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- loading ------------------------------------------------------------------------------ */

static uint8_t *am2_hybrid_read_file(const char *path, size_t *size)
{
    FILE    *fh = fopen(path, "rb");
    uint8_t *buf;
    long     n;

    if (!fh)
        return NULL;
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fh) != (size_t)n) {
        fclose(fh);
        free(buf);
        return NULL;
    }
    fclose(fh);
    *size = (size_t)n;
    return buf;
}

static void am2_hybrid_find_exe(char *out, size_t cap)
{
    const char *exe = getenv("AM2_EXE");
    const char *dir = getenv("AM2_GAMEDIR");

    if (exe && *exe)
        snprintf(out, cap, "%s", exe);
    else if (dir && *dir)
        snprintf(out, cap, "%s/ArmyMen2.exe", dir);
    else
        snprintf(out, cap, "ArmyMen2.exe");
}

static uint32_t am2_hybrid_bind_imports(const uint8_t *image, const AM2_PeDataDir *dir)
{
    const AM2_PeImport *imp = (const AM2_PeImport *)(image + dir->VirtualAddress);
    uint32_t            missing = 0, bound = 0;

    if (!dir->VirtualAddress)
        return 0;
    for (; imp->Name; imp++) {
        const char     *module = (const char *)(image + imp->Name);
        const uint32_t *names = (const uint32_t *)(image + (imp->OriginalFirstThunk
                                                            ? imp->OriginalFirstThunk
                                                            : imp->FirstThunk));
        uint32_t       *slots = (uint32_t *)(image + imp->FirstThunk);
        uint32_t        i;

        for (i = 0; names[i]; i++) {
            char        ordinal[16];
            const char *name;
            const void *fn;

            if (names[i] & 0x80000000u) {
                /* DSOUND's #1 is DirectSoundCreate; nothing else imports
                 * by ordinal. */
                if (!strcasecmp(module, "DSOUND.dll") && (names[i] & 0xFFFF) == 1)
                    name = "DirectSoundCreate";
                else {
                    snprintf(ordinal, sizeof ordinal, "#%u", (unsigned)(names[i] & 0xFFFF));
                    name = ordinal;
                }
            } else {
                name = (const char *)(image + (names[i] & 0x7FFFFFFFu) + 2);
            }
            fn = am2_export_lookup(module, name);
            if (!fn) {
                am2_plat_log("import %s!%s is not provided", module, name);
                fn = am2_hybrid_make_stub(module, name);
                missing++;
            } else {
                bound++;
            }
            slots[i] = (uint32_t)(uintptr_t)fn;
        }
    }
    am2_plat_log("imports: %u bound, %u missing", (unsigned)bound, (unsigned)missing);
    return missing;
}

extern "C" int32_t WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int32_t show);
extern "C" int32_t WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int32_t show)
{
    char                        path[4096];
    uint8_t                    *file;
    size_t                      size;
    const AM2_PeFileHeader     *fh;
    const AM2_PeOptionalHeader *oh;
    const AM2_PeSection        *sec;
    uint8_t                    *image;
    uint32_t                    lfanew, i;
    void                      (*entry)(void);

    (void)inst; (void)prev; (void)show;

    am2_hybrid_find_exe(path, sizeof path);
    file = am2_hybrid_read_file(path, &size);
    if (!file)
        am2_hybrid_die("cannot read %s (set AM2_EXE or AM2_GAMEDIR)", path);
    if (size < 0x40 || file[0] != 'M' || file[1] != 'Z')
        am2_hybrid_die("%s is not a PE image", path);
    memcpy(&lfanew, file + 0x3C, 4);
    if (lfanew + 4 + sizeof *fh + sizeof *oh > size || memcmp(file + lfanew, "PE\0\0", 4))
        am2_hybrid_die("%s has no PE header", path);
    fh = (const AM2_PeFileHeader *)(file + lfanew + 4);
    oh = (const AM2_PeOptionalHeader *)(fh + 1);
    if (fh->Machine != 0x14C || oh->Magic != 0x10B)
        am2_hybrid_die("%s is not an i386 PE32 image", path);
    if (oh->ImageBase != AM2_IMAGE_BASE)
        am2_hybrid_die("%s is based at 0x%08x, not 0x%08x", path,
                       (unsigned)oh->ImageBase, (unsigned)AM2_IMAGE_BASE);

    /* The whole image, readable, writable and executable: the CRT writes
     * its own .data, the IAT lives in .rdata and is bound below, and the
     * log detour rewrites five bytes of .text. */
    image = (uint8_t *)mmap((void *)(uintptr_t)oh->ImageBase, oh->SizeOfImage,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (image == MAP_FAILED || image != (uint8_t *)(uintptr_t)oh->ImageBase)
        am2_hybrid_die("cannot map the image at 0x%08x: %s", (unsigned)oh->ImageBase,
                       strerror(errno));
    memcpy(image, file, oh->SizeOfHeaders < size ? oh->SizeOfHeaders : size);
    sec = (const AM2_PeSection *)((const uint8_t *)oh + fh->SizeOfOptionalHeader);
    for (i = 0; i < fh->NumberOfSections; i++) {
        uint32_t n = sec[i].SizeOfRawData;
        if (sec[i].PointerToRawData + n > size)
            am2_hybrid_die("section %.8s runs past the end of the file", sec[i].Name);
        if (sec[i].VirtualAddress + n > oh->SizeOfImage)
            am2_hybrid_die("section %.8s runs past the end of the image", sec[i].Name);
        memcpy(image + sec[i].VirtualAddress, file + sec[i].PointerToRawData, n);
        am2_plat_debug("section %.8s at 0x%08x, %u bytes of %u",
                       sec[i].Name, (unsigned)(oh->ImageBase + sec[i].VirtualAddress),
                       (unsigned)n, (unsigned)sec[i].VirtualSize);
    }
    entry = (void (*)(void))(uintptr_t)(oh->ImageBase + oh->AddressOfEntryPoint);
    am2_hybrid_bind_imports(image, &oh->DataDirectory[AM2_PE_DIR_IMPORT]);
    free(file);

    am2_hybrid_patch_jmp(ADDR_LOG, (const void *)&am2_hybrid_log);
    am2_host_cursor_query = am2_hybrid_cursor_query;
    am2_host_cursor_set = am2_hybrid_cursor_set;
    AddVectoredExceptionHandler(1, am2_hybrid_fault);
    am2_set_command_line(cmdline);

    {
        const char *dir = getenv("AM2_GAMEDIR");
        if (dir && *dir && chdir(dir) != 0)
            am2_hybrid_die("cannot enter AM2_GAMEDIR \"%s\": %s", dir, strerror(errno));
    }

#ifdef AM2_DEVTOOLS
    /* The control socket, on port 31337 as the native development binary
     * has it. It reads the game's globals by address, which are the
     * original's here, so `dump`, `cursor` and tools/objdump.py work as
     * they do there. */
    input_init();
    control_start();

    /* AM2_CRTCHECK=<dir>: run src/hybrid/crtcheck.cpp instead of the game.
     * It takes over at WinMain, so the original CRT's startup has run and
     * both stacks share the tables it built. */
    if (getenv("AM2_CRTCHECK"))
        am2_hybrid_patch_jmp(ADDR_WIN_MAIN, (const void *)&am2_crtcheck_main);
    /* AM2_TRACE_ANGLE=1: replace the original's AngleBetween with a copy of
     * the reconstruction that logs its arguments and answer with the pump
     * number, so the two games' calls can be compared line for line. */
    if (getenv("AM2_TRACE_ANGLE"))
        am2_hybrid_patch_jmp(ADDR_ANGLE_BETWEEN, (const void *)&am2_hybrid_trace_angle_between);
    /* AM2_TRACE_MAPOBJ=1: the same over DrawMapObject, logging every map
     * object drawn with its bounds, clip and sprite, in draw order. */
    if (getenv("AM2_TRACE_MAPOBJ"))
        am2_hybrid_patch_jmp(ADDR_DRAW_MAP_OBJECT, (const void *)&am2_hybrid_trace_draw_map_object);
    /* AM2_TRACE_SPRITE=1: the same over DrawSprite, every clipped sprite draw
     * with its screen position and source rectangle. */
    if (getenv("AM2_TRACE_SPRITE"))
        am2_hybrid_patch_jmp(ADDR_DRAW_SPRITE, (const void *)&am2_hybrid_trace_draw_sprite);
    /* AM2_TRACE_DEPTH=1: the reconstruction's DepthCompare over the original,
     * logging both records' keys and the answer. If the hybrid's draw order
     * changes under it, the comparator is the divergence; if not, its inputs
     * or the insertion are. */
    if (getenv("AM2_TRACE_DEPTH"))
        am2_hybrid_patch_jmp(ADDR_DEPTH_COMPARE, (const void *)&am2_hybrid_trace_depth_compare);
    /* AM2_TRACE_HEAP=1: the reconstruction's _nh_malloc, free and realloc
     * over the original's, logging every call. They run on the image's own
     * heap state, which the original's startup has already built, and
     * tools/crtcheck.py holds them exact against the original operation by
     * operation -- so the hybrid's heap behaves as before and merely logs. */
    if (getenv("AM2_TRACE_HEAP")) {
        am2_hybrid_patch_jmp(ADDR_CRT_NH_MALLOC, (const void *)&crt_nh_malloc);
        am2_hybrid_patch_jmp(ADDR_CRT_MALLOC, (const void *)&crt_malloc);
        am2_hybrid_patch_jmp(ADDR_GAME_OPERATOR_NEW, (const void *)&crt_operator_new);
        am2_hybrid_patch_jmp(ADDR_CRT_FREE, (const void *)&crt_free);
        am2_hybrid_patch_jmp(ADDR_CRT_REALLOC, (const void *)&crt_realloc);
    }
#endif

    am2_plat_log("running %s from its entry point 0x%08lx", path, (unsigned long)(uintptr_t)entry);
    entry();
    /* WinMainCRTStartup ends in ExitProcess and does not return. */
    return 0;
}
