/* vc6host -- run the Visual C++ 6.0 toolchain PEs natively on Linux, no Wine.
 *
 * The same trick as src/hybrid/loader.cpp: map a Win32 PE at the base its
 * pointers were written for, fill its import table with our own Linux
 * implementations of the Win32/CRT functions it names, and jump to its entry.
 * CL.EXE, C1/C1XX/C2.DLL and LINK.EXE are all i386 PE32 console/DLL images at
 * fixed, non-overlapping bases (0x00400000, 0x10400000, 0x10600000, 0x10700000)
 * that an i386 Linux process leaves free, so no relocation is needed.
 *
 * This first cut loads ONE image and runs it; every import that is not yet
 * implemented gets a stub that names itself and stops, so a run tells us which
 * Win32/CRT call the startup path reaches next. Implemented in hit order.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <asm/ldt.h>

#define STDCALL __attribute__((stdcall))
#define CDECL   __attribute__((cdecl))

/* ---- PE32 structures (i386 subset) --------------------------------------- */

typedef struct { uint16_t Machine, NumberOfSections; uint32_t TimeDateStamp,
    PointerToSymbolTable, NumberOfSymbols; uint16_t SizeOfOptionalHeader,
    Characteristics; } PeFileHeader;

typedef struct { uint32_t VirtualAddress, Size; } PeDataDir;

typedef struct {
    uint16_t Magic; uint8_t MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData,
        AddressOfEntryPoint, BaseOfCode, BaseOfData, ImageBase,
        SectionAlignment, FileAlignment;
    uint16_t MajorOSVersion, MinorOSVersion, MajorImageVersion, MinorImageVersion,
        MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint32_t SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve,
        SizeOfHeapCommit, LoaderFlags, NumberOfRvaAndSizes;
    PeDataDir DataDirectory[16];
} PeOptionalHeader;

typedef struct { char Name[8]; uint32_t VirtualSize, VirtualAddress,
    SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers;
    uint16_t NumberOfRelocations, NumberOfLinenumbers; uint32_t Characteristics;
} PeSection;

typedef struct { uint32_t OriginalFirstThunk, TimeDateStamp, ForwarderChain,
    Name, FirstThunk; } PeImport;

typedef struct { uint32_t Characteristics, TimeDateStamp; uint16_t MajorVersion,
    MinorVersion; uint32_t Name, Base, NumberOfFunctions, NumberOfNames,
    AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals; } PeExportDir;

#define PE_DIR_EXPORT 0
#define PE_DIR_IMPORT 1

/* ---- a loaded module ----------------------------------------------------- */

typedef struct {
    char        name[64];   /* lower-cased basename, for GetModuleHandle */
    uint8_t    *image;      /* == ImageBase */
    uint32_t    base, entry, size;
    int         is_dll;
} Vc6Module;

static void vc6_die(const char *fmt, ...) __attribute__((noreturn, format(printf,1,2)));
static void vc6_die(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("vc6host: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); exit(2);
}

static int vc6_verbose(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("VC6_VERBOSE"); v = e && *e ? 1 : 0; }
    return v;
}
static void vc6_log(const char *fmt, ...) __attribute__((format(printf,1,2)));
static void vc6_log(const char *fmt, ...)
{
    if (!vc6_verbose()) return;
    va_list ap; va_start(ap, fmt);
    fputs("vc6host: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

/* ---- the TEB ------------------------------------------------------------- *
 * The MSVC CRT startup and SEH read the thread environment block through the
 * FS segment: fs:[0] is the SEH handler-chain head (0xFFFFFFFF = end) and
 * fs:[0x18] points at the TEB itself. set_thread_area gives FS a GDT slot. */
static void vc6_teb_attach(void)
{
    static __thread int done;
    if (done) return;
    done = 1;
    uint32_t *teb = (uint32_t *)calloc(1024, sizeof *teb);
    if (!teb) vc6_die("no memory for a TEB");
    /* The real stack region, so the compiler's own stack-overflow guard (it
     * reads fs:[4]/fs:[8]) sees a sane range rather than a 4 GB fiction. */
    uint32_t sbase = 0xC0000000u, slimit = 0xBF000000u;
    FILE *mf = fopen("/proc/self/maps", "r");
    if (mf) { char line[512]; unsigned long lo, hi;
        while (fgets(line, sizeof line, mf))
            if (strstr(line, "[stack]") && sscanf(line, "%lx-%lx", &lo, &hi) == 2) {
                slimit = (uint32_t)lo; sbase = (uint32_t)hi; break; }
        fclose(mf); }
    teb[0] = 0xFFFFFFFFu;                 /* fs:[0]    end of SEH chain       */
    teb[1] = sbase;                       /* fs:[4]    StackBase (top)        */
    teb[2] = slimit;                      /* fs:[8]    StackLimit (bottom)    */
    teb[6] = (uint32_t)(uintptr_t)teb;    /* fs:[0x18] self                   */
    struct user_desc d; memset(&d, 0, sizeof d);
    d.entry_number = (unsigned)-1;
    d.base_addr = (unsigned)(uintptr_t)teb;
    d.limit = 0xFFFFF; d.seg_32bit = 1; d.limit_in_pages = 1; d.useable = 1;
    if (syscall(SYS_set_thread_area, &d) != 0) vc6_die("set_thread_area: %s", strerror(errno));
    uint16_t sel = (uint16_t)(d.entry_number * 8 + 3);
    __asm__ volatile("mov %0, %%fs" : : "r"(sel));
}

/* ---- the import dispatch (grows as functions are implemented) ------------ */

extern "C" const void *vc6_shim_lookup(const char *module, const char *name);
extern "C" uint32_t vc6_loadlibrary(const char *name);
extern "C" uint32_t vc6_getproc(uint32_t hmod, const char *name);

/* An unresolved import gets a stub that names itself and aborts on first call. */
static void __attribute__((noreturn)) vc6_missing(const char *what)
{
    vc6_die("called %s, which vc6host does not provide yet", what);
}
static uint8_t *vc6_stub_page;
static size_t   vc6_stub_used;
static const void *vc6_make_stub(const char *module, const char *name)
{
    char *what = (char *)malloc(strlen(module) + strlen(name) + 2);
    uint8_t *s; uint32_t rel;
    if (!vc6_stub_page) {
        vc6_stub_page = (uint8_t *)mmap(NULL, 1 << 20, PROT_READ|PROT_WRITE|PROT_EXEC,
                                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (vc6_stub_page == MAP_FAILED) vc6_die("cannot map import stubs");
    }
    if (!what || vc6_stub_used + 16 > (1 << 20)) vc6_die("too many missing imports");
    sprintf(what, "%s!%s", module, name);
    if (getenv("VC6_VERBOSE")) vc6_log("stub: %s", what);
    s = vc6_stub_page + vc6_stub_used; vc6_stub_used += 16;
    s[0] = 0x68; memcpy(s + 1, &what, 4);              /* push what          */
    s[5] = 0xE8;                                        /* call vc6_missing   */
    rel = (uint32_t)((uintptr_t)&vc6_missing - (uintptr_t)(s + 10));
    memcpy(s + 6, &rel, 4);
    s[10] = 0xCC;
    return s;
}

/* ---- loader -------------------------------------------------------------- */

static uint8_t *vc6_read_file(const char *path, size_t *size)
{
    FILE *fh = fopen(path, "rb"); uint8_t *buf; long n;
    if (!fh) return NULL;
    fseek(fh, 0, SEEK_END); n = ftell(fh); fseek(fh, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fh) != (size_t)n) { fclose(fh); free(buf); return NULL; }
    fclose(fh); *size = (size_t)n; return buf;
}

static uint32_t vc6_bind_imports(uint8_t *image, const PeDataDir *dir)
{
    if (!dir->VirtualAddress) return 0;
    const PeImport *imp = (const PeImport *)(image + dir->VirtualAddress);
    uint32_t bound = 0, missing = 0;
    for (; imp->Name; imp++) {
        const char *module = (const char *)(image + imp->Name);
        const uint32_t *names = (const uint32_t *)(image +
            (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        uint32_t *slots = (uint32_t *)(image + imp->FirstThunk);
        for (uint32_t i = 0; names[i]; i++) {
            char ord[16]; const char *name; const void *fn;
            if (names[i] & 0x80000000u) {
                snprintf(ord, sizeof ord, "#%u", (unsigned)(names[i] & 0xFFFF));
                name = ord;
            } else {
                name = (const char *)(image + (names[i] & 0x7FFFFFFFu) + 2);
            }
            fn = vc6_shim_lookup(module, name);
            if (!fn) {
                /* Not a system DLL we shim -> host the real PE (e.g. mspdb60.dll)
                 * and resolve from its exports. */
                static const char *sys[] = { "kernel32.dll", "user32.dll",
                    "msvcrt.dll", "advapi32.dll", "gdi32.dll", "version.dll", 0 };
                int is_sys = 0;
                for (int k = 0; sys[k]; k++) if (!strcasecmp(module, sys[k])) { is_sys = 1; break; }
                if (!is_sys) {
                    uint32_t base = vc6_loadlibrary(module);
                    if (base) fn = (const void *)(uintptr_t)vc6_getproc(base, name);
                }
            }
            if (!fn) { fn = vc6_make_stub(module, name); missing++; }
            else bound++;
            slots[i] = (uint32_t)(uintptr_t)fn;
        }
    }
    vc6_log("%s: %u imports bound, %u stubbed", "imports", bound, missing);
    return missing;
}

static void vc6_map(const char *path, Vc6Module *mod)
{
    size_t size; uint8_t *file = vc6_read_file(path, &size);
    if (!file) vc6_die("cannot read %s: %s", path, strerror(errno));
    if (size < 0x40 || file[0] != 'M' || file[1] != 'Z') vc6_die("%s is not a PE", path);
    uint32_t lfanew; memcpy(&lfanew, file + 0x3C, 4);
    if (memcmp(file + lfanew, "PE\0\0", 4)) vc6_die("%s has no PE header", path);
    const PeFileHeader *fh = (const PeFileHeader *)(file + lfanew + 4);
    const PeOptionalHeader *oh = (const PeOptionalHeader *)(fh + 1);
    if (fh->Machine != 0x14C || oh->Magic != 0x10B) vc6_die("%s is not i386 PE32", path);

    uint8_t *image = (uint8_t *)mmap((void *)(uintptr_t)oh->ImageBase, oh->SizeOfImage,
        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
    if (image != (uint8_t *)(uintptr_t)oh->ImageBase)
        vc6_die("cannot map %s at 0x%08x: %s", path, (unsigned)oh->ImageBase, strerror(errno));
    memcpy(image, file, oh->SizeOfHeaders < size ? oh->SizeOfHeaders : size);
    const PeSection *sec = (const PeSection *)((const uint8_t *)oh + fh->SizeOfOptionalHeader);
    for (uint32_t i = 0; i < fh->NumberOfSections; i++) {
        uint32_t n = sec[i].SizeOfRawData;
        if (n) memcpy(image + sec[i].VirtualAddress, file + sec[i].PointerToRawData, n);
        vc6_log("  section %.8s @ 0x%08x  %u bytes", sec[i].Name,
                (unsigned)(oh->ImageBase + sec[i].VirtualAddress), (unsigned)sec[i].VirtualSize);
    }
    vc6_bind_imports(image, &oh->DataDirectory[PE_DIR_IMPORT]);

    const char *bn = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    size_t j = 0; for (; bn[j] && j < sizeof mod->name - 1; j++) mod->name[j] = (char)tolower((unsigned char)bn[j]);
    mod->name[j] = 0;
    mod->image  = image;
    mod->base   = oh->ImageBase;
    mod->entry  = oh->ImageBase + oh->AddressOfEntryPoint;
    mod->size   = oh->SizeOfImage;
    mod->is_dll = (fh->Characteristics & 0x2000) != 0;
    free(file);
    vc6_log("mapped %s at 0x%08x, entry 0x%08x", path, (unsigned)mod->base, (unsigned)mod->entry);
}

/* VirtualAlloc backing: honor a requested base, else let the kernel choose. */
extern "C" void *vc6_valloc(void *addr, uint32_t size)
{
    void *p = mmap(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | (addr ? MAP_FIXED_NOREPLACE : 0), -1, 0);
    if (p == MAP_FAILED) return 0;
    if (addr && p != addr) { munmap(p, size);
        p = mmap(0, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return 0; }
    return p;
}

/* Resolve an export by name (for GetProcAddress and the runner). */
const void *vc6_get_proc(const Vc6Module *mod, const char *want)
{
    const PeOptionalHeader *oh = (const PeOptionalHeader *)
        (mod->image + *(uint32_t *)(mod->image + 0x3C) + 4 + sizeof(PeFileHeader));
    const PeDataDir *d = &oh->DataDirectory[PE_DIR_EXPORT];
    if (!d->VirtualAddress) return NULL;
    const PeExportDir *ed = (const PeExportDir *)(mod->image + d->VirtualAddress);
    const uint32_t *funcs = (const uint32_t *)(mod->image + ed->AddressOfFunctions);
    const uint32_t *names = (const uint32_t *)(mod->image + ed->AddressOfNames);
    const uint16_t *ords  = (const uint16_t *)(mod->image + ed->AddressOfNameOrdinals);
    for (uint32_t i = 0; i < ed->NumberOfNames; i++) {
        const char *nm = (const char *)(mod->image + names[i]);
        if (!strcmp(nm, want)) return mod->image + funcs[ords[i]];
    }
    return NULL;
}

/* ---- module registry + DLL loading (LoadLibrary/GetProcAddress) ---------- */

static Vc6Module g_mod[32];
static int       g_nmod;

static const Vc6Module *mod_by_base(uint32_t base)
{ for (int i = 0; i < g_nmod; i++) if (g_mod[i].base == base) return &g_mod[i]; return 0; }

static const Vc6Module *mod_by_name(const char *name)
{
    const char *b = name;
    for (const char *p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    char low[64]; size_t j = 0;
    for (; b[j] && j < sizeof low - 1; j++) low[j] = (char)tolower((unsigned char)b[j]);
    low[j] = 0;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, low)) return &g_mod[i];
    return 0;
}

/* The VC6 install root and the two bin dirs, all overridable by env. */
static const char *vc6_root(void)
{
    static char r[4096]; static int done;
    if (!done) { done = 1; const char *e = getenv("VC6_ROOT");
        snprintf(r, sizeof r, "%s", e && *e ? e : "/home/boran/claude/en_vs6_ent"); }
    return r;
}
static const char *vc6_bindir(void)
{
    static char dir[4096]; static int done;
    if (!done) { done = 1; const char *e = getenv("VC6_BINDIR");
        if (e && *e) snprintf(dir, sizeof dir, "%s", e);
        else snprintf(dir, sizeof dir, "%s/VC98/BIN", vc6_root()); }
    return dir;
}
static const char *vc6_msdevdir(void)
{
    static char dir[4096]; static int done;
    if (!done) { done = 1; const char *e = getenv("VC6_MSDEVBIN");
        if (e && *e) snprintf(dir, sizeof dir, "%s", e);
        else snprintf(dir, sizeof dir, "%s/COMMON/MSDEV98/BIN", vc6_root()); }
    return dir;
}

extern "C" uint32_t vc6_loadlibrary(const char *name)
{
    const Vc6Module *m = mod_by_name(name);
    if (getenv("VC6_VERBOSE")) { fprintf(stderr, "vc6host: loadlibrary(%s): %d mods [", name, g_nmod);
        for (int i = 0; i < g_nmod; i++) fprintf(stderr, "%s ", g_mod[i].name); fprintf(stderr, "] cached=%d\n", m != 0); }
    if (m) return m->base;
    const char *b = name;
    for (const char *p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    char path[4096]; path[0] = 0;
    /* search dirs (mspdb60 lives in the msdev bin dir, not VC98/BIN) and try
     * case variants of the basename -- Linux is case-sensitive but the DOS
     * names are UPPERCASE while the import names are lower. */
    const char *dirs[] = { vc6_bindir(), vc6_msdevdir(), 0 };
    char up[128], lo[128];
    size_t bl = strlen(b);
    for (size_t i = 0; i < bl && i < 127; i++) { up[i] = (char)toupper((unsigned char)b[i]); lo[i] = (char)tolower((unsigned char)b[i]); }
    up[bl < 127 ? bl : 127] = 0; lo[bl < 127 ? bl : 127] = 0;
    if (!access(name, F_OK)) snprintf(path, sizeof path, "%s", name);
    else for (int k = 0; dirs[k] && !path[0]; k++) {
        const char *variants[] = { b, up, lo, 0 };
        for (int v = 0; variants[v]; v++) {
            char cand[4096]; snprintf(cand, sizeof cand, "%s/%s", dirs[k], variants[v]);
            if (!access(cand, F_OK)) { snprintf(path, sizeof path, "%s", cand); break; }
        }
    }
    if (!path[0] || access(path, F_OK)) {
        /* Not on disk -> a Windows system DLL we don't ship (ole32, oleaut32,
         * ...): the C++ frontend delay-loads ole32 the moment windows.h pulls
         * in COM.  Register a SYNTHETIC stub module (image == 0) so the probe
         * succeeds; vc6_getproc hands back logging abort-stubs, so we only die
         * -- naming the function -- if the pass actually CALLS into it.  This
         * is the "stub, then implement in hit order" rule applied to whole
         * DLLs.  A genuinely-needed toolchain DLL would still be found on disk
         * above; anything reaching here is a system DLL. */
        if (g_nmod >= (int)(sizeof g_mod / sizeof g_mod[0])) vc6_die("too many modules");
        Vc6Module *stub = &g_mod[g_nmod++];
        size_t k = 0; for (; b[k] && k < sizeof stub->name - 1; k++)
            stub->name[k] = (char)tolower((unsigned char)b[k]);
        stub->name[k] = 0;
        stub->image = 0;                 /* marks it synthetic */
        stub->base  = 0x0E000000u + (uint32_t)(g_nmod * 0x10000);
        stub->entry = 0; stub->size = 0; stub->is_dll = 1;
        vc6_log("LoadLibrary(%s): synthesised stub module @ 0x%08x", name, stub->base);
        return stub->base;
    }
    if (g_nmod >= (int)(sizeof g_mod / sizeof g_mod[0])) vc6_die("too many modules");
    Vc6Module *mod = &g_mod[g_nmod++];       /* reserve the slot BEFORE mapping,
                                              * so recursive loads use later slots */
    vc6_map(path, mod);
    if (mod->is_dll) {                       /* DllMain(hinst, DLL_PROCESS_ATTACH, 0) */
        int (STDCALL *dllmain)(void *, uint32_t, void *) =
            (int (STDCALL *)(void *, uint32_t, void *))(uintptr_t)mod->entry;
        dllmain((void *)(uintptr_t)mod->base, 1, 0);
    }
    return mod->base;
}

/* VC6_IL_OVERRIDE=DIR: a probe of the code generator. Before c2 runs, the
 * intermediate files c1xx just wrote (BASEex, BASEgl, BASEin, BASEsy, BASE
 * taken from c2's -il argument) are replaced by DIR/ex, DIR/gl, DIR/in and
 * DIR/sy where those exist, so c2 compiles hand-edited IL. VC6_IL_LOG=1
 * prints c2's argv. */
extern "C" const char *vc6_xpath(const char *p, char *buf, size_t cap);
typedef int (STDCALL *Vc6PassFn)(int argc, char **argv, int mode);
static Vc6PassFn g_c2_pass;

static void c2_override_il(const char *dir, const char *il)
{
    char base[4096];
    snprintf(base, sizeof base, "%s", vc6_xpath(il, (char[4096]){0}, 4096));
    {
        static const char *const sfx[] = { "ex", "gl", "in", "sy" };
        for (int k = 0; k < 4; k++) {
            char src[4096], dst[4200];
            snprintf(src, sizeof src, "%s/%s", dir, sfx[k]);
            snprintf(dst, sizeof dst, "%s%s", base, sfx[k]);
            FILE *in = fopen(src, "rb");
            if (!in) continue;
            FILE *out = fopen(dst, "wb");
            if (out) { char b[65536]; size_t n;
                while ((n = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, n, out);
                fclose(out); }
            fclose(in);
            if (getenv("VC6_IL_LOG")) fprintf(stderr, "vc6host: IL override %s -> %s\n", src, dst);
        }
    }
}

/* c2's switches arrive in MSC_CMD_FLAGS, not argv: the -il token is there. */
static int STDCALL c2_override_pass(int argc, char **argv, int mode)
{
    const char *dir = getenv("VC6_IL_OVERRIDE");
    const char *fl = getenv("MSC_CMD_FLAGS");
    if (getenv("VC6_IL_LOG")) fprintf(stderr, "vc6host: c2 MSC_CMD_FLAGS=[%s]\n", fl ? fl : "");
    for (const char *p = fl; dir && *dir && p && (p = strstr(p, "-il")); p += 3) {
        char tok[4096]; size_t n = 0;
        const char *q = p + 3;
        while (*q == ' ') q++;
        if (*q == '"') { q++; while (*q && *q != '"' && n < sizeof tok - 1) tok[n++] = *q++; }
        else while (*q && *q != ' ' && n < sizeof tok - 1) tok[n++] = *q++;
        tok[n] = 0;
        if (n) c2_override_il(dir, tok);
        break;
    }
    return g_c2_pass(argc, argv, mode);
}

extern "C" uint32_t vc6_getproc(uint32_t hmod, const char *name)
{
    const Vc6Module *m = mod_by_base(hmod);
    if (!m) return 0;
    if ((getenv("VC6_IL_OVERRIDE") || getenv("VC6_IL_LOG")) && m->image
        && !strcasecmp(m->name, "C2.DLL") && !strcmp(name, "_InvokeCompilerPass@12")) {
        g_c2_pass = (Vc6PassFn)(uintptr_t)vc6_get_proc(m, name);
        return g_c2_pass ? (uint32_t)(uintptr_t)c2_override_pass : 0;
    }
    if (!m->image) {  /* synthetic system-DLL stub: a shimmed export if we have
                       * one (e.g. ole32!CLSIDFromString), else an abort-stub */
        const void *fn = vc6_shim_lookup(m->name, name);
        return (uint32_t)(uintptr_t)(fn ? fn : vc6_make_stub(m->name, name));
    }
    return (uint32_t)(uintptr_t)vc6_get_proc(m, name);
}

extern "C" uint32_t vc6_module_base(const char *name)
{
    if (!name) return g_nmod ? g_mod[0].base : 0x00400000u;   /* NULL == main EXE */
    const Vc6Module *m = mod_by_name(name);
    return m ? m->base : 0;
}

/* The compiler passes are DLLs, not EXEs: cl spawns "c1"/"c1xx"/"c2" and each
 * exports _InvokeCompilerPass@12. We bridge the spawn to the DLL interface --
 * run the pass in a forked child (for isolation) via its own entry, with cl's
 * exact argv. Real EXEs (link, cvtres) are exec'd in a fresh vc6host. */
typedef int (STDCALL *InvokePassFn)(int argc, char **argv, int mode);

static int is_pass(const char *b, char *dll, size_t cap)
{
    if (!strcasecmp(b, "c1") || !strcasecmp(b, "c1.dll"))   { snprintf(dll, cap, "C1.DLL");   return 1; }
    if (!strcasecmp(b, "c1xx") || !strcasecmp(b, "c1xx.dll")){ snprintf(dll, cap, "C1XX.DLL"); return 1; }
    if (!strcasecmp(b, "c2") || !strcasecmp(b, "c2.dll"))   { snprintf(dll, cap, "C2.DLL");   return 1; }
    return 0;
}

extern "C" int vc6_spawn(int wait_mode, const char *file, char *const *argv)
{
    const char *b = file;
    for (const char *p = file; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;

    int ac = 0; while (argv && argv[ac]) ac++;
    if (vc6_verbose()) {
        fprintf(stderr, "vc6host: spawn(mode=%d) %s  argv=[", wait_mode, file);
        for (int i = 0; i < ac; i++) fprintf(stderr, "%s ", argv[i]);
        fprintf(stderr, "]\n");
    }

    char dll[64];
    if (is_pass(b, dll, sizeof dll)) {                 /* run the pass DLL in a child */
        pid_t pid = fork();
        if (pid == 0) {
            if (vc6_verbose()) { char cwd[4096];
                fprintf(stderr, "vc6host: [child %s] cwd=%s MSC_CMD_FLAGS=[%s]\n", dll,
                        getcwd(cwd, sizeof cwd) ? cwd : "?", getenv("MSC_CMD_FLAGS") ? getenv("MSC_CMD_FLAGS") : "(unset)"); }
            uint32_t base = vc6_loadlibrary(dll);
            if (!base) { fprintf(stderr, "vc6host: cannot load pass %s\n", dll); _exit(3); }
            InvokePassFn ip = (InvokePassFn)(uintptr_t)vc6_getproc(base, "_InvokeCompilerPass@12");
            if (!ip) { fprintf(stderr, "vc6host: %s has no _InvokeCompilerPass\n", dll); _exit(3); }
            int rc = ip(ac, (char **)argv, 0);
            _exit(rc & 0xFF);
        }
        if (pid < 0) return -1;
        if (wait_mode == 1) return (int)pid;
        int st = 0; waitpid(pid, &st, 0);
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }

    /* A real EXE (link, cvtres, ...): exec it under a fresh vc6host. DOS names
     * are UPPERCASE, so try case variants of the basename in the bin dirs. */
    char pe[4096]; pe[0] = 0;
    char nm[128]; snprintf(nm, sizeof nm, "%s", b);
    if (!strchr(nm, '.')) strncat(nm, ".exe", sizeof nm - strlen(nm) - 1);
    char up[128], lo[128];
    size_t nl = strlen(nm);
    for (size_t i = 0; i < nl && i < 127; i++) { up[i] = (char)toupper((unsigned char)nm[i]); lo[i] = (char)tolower((unsigned char)nm[i]); }
    up[nl < 127 ? nl : 127] = 0; lo[nl < 127 ? nl : 127] = 0;
    if (!access(file, F_OK)) snprintf(pe, sizeof pe, "%s", file);
    else { const char *ds[] = { vc6_bindir(), vc6_msdevdir(), 0 };
        for (int d = 0; ds[d] && !pe[0]; d++) { const char *vv[] = { nm, up, lo, 0 };
            for (int v = 0; vv[v]; v++) { char c[4096]; snprintf(c, sizeof c, "%s/%s", ds[d], vv[v]);
                if (!access(c, F_OK)) { snprintf(pe, sizeof pe, "%s", c); break; } } } }
    if (!pe[0] || access(pe, F_OK)) { vc6_log("spawn: %s not found", file); errno = ENOENT; return -1; }
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n < 0) return -1; self[n] = 0;
    char **nv = (char **)malloc((size_t)(ac + 3) * sizeof *nv);
    int k = 0; nv[k++] = self; nv[k++] = pe;
    for (int i = 1; i < ac; i++) nv[k++] = argv[i];
    nv[k] = 0;
    pid_t pid = fork();
    if (pid == 0) { execv(self, nv); _exit(127); }
    if (pid < 0) { free(nv); return -1; }
    if (wait_mode == 1) { free(nv); return (int)pid; }
    int st = 0; waitpid(pid, &st, 0); free(nv);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* ---- LoadStringA: read a string from a module's RT_STRING resource -------
 * String resources live in bundles of 16 UTF-16 strings (each a WORD length
 * then that many wchars). Bundle number is id/16+1; the string is at id%16. */
typedef struct { uint32_t Characteristics, TimeDateStamp; uint16_t Major, Minor,
    NumberOfNamedEntries, NumberOfIdEntries; } ResDir;
typedef struct { uint32_t Name, OffsetToData; } ResEntry;
typedef struct { uint32_t OffsetToData, Size, CodePage, Reserved; } ResData;

static const ResEntry *res_find_id(const uint8_t *rbase, uint32_t diroff, uint32_t id)
{
    const ResDir *d = (const ResDir *)(rbase + diroff);
    const ResEntry *e = (const ResEntry *)(d + 1) + d->NumberOfNamedEntries;
    for (uint32_t i = 0; i < d->NumberOfIdEntries; i++)
        if ((e[i].Name & 0x7FFFFFFFu) == id) return &e[i];
    return 0;
}

extern "C" int vc6_load_string(uint32_t hinst, uint32_t id, char *buf, int maxch)
{
    if (!maxch) return 0;
    /* hinst == 0 means the main module (Win32 LoadString(NULL, ...)); LINK.EXE
     * loads its diagnostic strings this way. g_mod[0] is always the main EXE. */
    if (!hinst && g_nmod) hinst = g_mod[0].base;
    uint8_t *base = (uint8_t *)(uintptr_t)hinst;
    uint32_t lfanew = *(uint32_t *)(base + 0x3C);
    const PeOptionalHeader *oh = (const PeOptionalHeader *)(base + lfanew + 4 + sizeof(PeFileHeader));
    const PeDataDir *rd = &oh->DataDirectory[2];        /* resource */
    if (!rd->VirtualAddress) { buf[0] = 0; return 0; }
    const uint8_t *rbase = base + rd->VirtualAddress;

    const ResEntry *type = res_find_id(rbase, 0, 6);    /* RT_STRING */
    if (!type || !(type->OffsetToData & 0x80000000u)) { buf[0] = 0; return 0; }
    const ResEntry *bundle = res_find_id(rbase, type->OffsetToData & 0x7FFFFFFFu, id / 16 + 1);
    if (!bundle || !(bundle->OffsetToData & 0x80000000u)) { buf[0] = 0; return 0; }
    const ResDir *ld = (const ResDir *)(rbase + (bundle->OffsetToData & 0x7FFFFFFFu));
    const ResEntry *lang = (const ResEntry *)(ld + 1);  /* first language */
    if (!ld->NumberOfIdEntries && !ld->NumberOfNamedEntries) { buf[0] = 0; return 0; }
    const ResData *data = (const ResData *)(rbase + (lang->OffsetToData & 0x7FFFFFFFu));

    const uint16_t *p = (const uint16_t *)(base + data->OffsetToData);
    for (uint32_t i = 0; i < (id & 15); i++) p += 1 + *p;   /* skip to our slot */
    uint32_t len = *p++;
    int n = 0;
    for (uint32_t i = 0; i < len && n < maxch - 1; i++) buf[n++] = (char)p[i];
    buf[n] = 0;
    return n;
}

/* ---- entry --------------------------------------------------------------- */

/* The arguments the loaded pass should see, filled by the CRT's __getmainargs. */
extern "C" int    vc6_argc;
extern "C" char **vc6_argv;
extern "C" char **vc6_env;

int main(int argc, char **argv)
{
    const char *path = getenv("VC6_LOAD");
    int         first = 1;                     /* argv index of the PE path   */
    if (!path) { { static char clp[4096]; snprintf(clp,sizeof clp,"%s/CL.EXE",vc6_bindir()); path = argc > 1 ? argv[1] : clp; } first = 2; }

    /* Everything after the PE path is the compiler's own argv; argv[0] is the
     * program name the pass reports in diagnostics. */
    static char  prog[] = "cl";
    static char *cargv[64];
    static char *cenv[1] = { 0 };
    int n = 0;
    cargv[n++] = prog;
    for (int i = first; i < argc && n < 63; i++) cargv[n++] = argv[i];
    cargv[n] = 0;
    vc6_argc = n; vc6_argv = cargv; vc6_env = cenv;

    if (!getenv("TMP"))  setenv("TMP", "/tmp", 0);
    if (!getenv("TEMP")) setenv("TEMP", "/tmp", 0);

    vc6_teb_attach();

    g_nmod = 1;                              /* reserve slot 0 before mapping, so
                                              * recursive loads use later slots */
    vc6_map(path, &g_mod[0]);

    if (g_mod[0].is_dll) {
        /* A DLL: run DllMain, then (VC6_INVOKE) call its _InvokeCompilerPass. */
        int (STDCALL *dllmain)(void *, uint32_t, void *) =
            (int (STDCALL *)(void *, uint32_t, void *))(uintptr_t)g_mod[0].entry;
        dllmain((void *)(uintptr_t)g_mod[0].base, 1, 0);
        if (getenv("VC6_INVOKE")) {
            typedef int (STDCALL *IP)(int, char **, int);
            IP ip = (IP)(uintptr_t)vc6_get_proc(&g_mod[0], "_InvokeCompilerPass@12");
            if (!ip) vc6_die("%s has no _InvokeCompilerPass", path);
            vc6_log("invoking pass with argc=%d", vc6_argc);
            int rc = ip(vc6_argc, vc6_argv, 0);
            vc6_log("pass returned %d", rc);
            return rc;
        }
        return 0;
    }

    vc6_log("loaded OK; jumping to entry 0x%08x", (unsigned)g_mod[0].entry);
    void (*entry)(void) = (void (*)(void))(uintptr_t)g_mod[0].entry;
    entry();                                 /* CRT startup -> main -> exit() */
    return 0;
}
