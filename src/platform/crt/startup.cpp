/* startup.cpp -- the CRT's startup and its error exit, from the bodies at
 * 0x004664C0 (WinMainCRTStartup) and what it calls: 0x0046A396
 * (__crtGetEnvironmentStringsA), 0x0046A149 (_setargv) with 0x0046A1E2
 * (parse_cmdline), 0x0046A090 (_setenvp), 0x0046A038 (__wincmdln),
 * 0x004692E2 (_cinit), 0x0046443E (_fpmath) with 0x0046A769
 * (_control87); 0x0046C263 (_setmbcp) with 0x0046C627 (__initmbctable),
 * 0x0046C3FC, 0x0046C446, 0x0046C479, 0x0046C4A2, 0x0046C643 and
 * 0x0046C654; and 0x004665B6 (_amsg_exit) with 0x004665DB, 0x0046A5E1
 * (_NMSG_WRITE), 0x0046A5A8 (_FF_MSGBANNER) and 0x0046C685
 * (__crtMessageBoxA).
 *
 * This is what fills the tables the other modules read: the heap and its
 * header list, the handle and stream tables, _environ, __argv, the
 * multibyte-character tables, the version words. The original's entry
 * point does it all and then calls WinMain and exit; the standalone and
 * native builds call crt_startup from their startup object, WinMain from
 * the host's main, and doexit from the startup object's destructor. The
 * lazy initialisations the modules keep are then never reached.
 *
 * Two things are not reproduced. The entry point's SEH frame and its
 * filter, _XcptFilter, which maps a machine exception to a C signal
 * handler the game never installs; and the C++ exception dispatcher the
 * unhandled-exception filter is. Nothing here throws and nothing here
 * expects a signal.
 */
#include "crt.h"
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The runtime-error message table at 0x0048D338 that crt_nmsg_write scans:
 * 18 {error number, message} pairs, up to ADDR_CRT_CVTINFO_DOUBLE. Migrating
 * it to C literals drops both the table and its ~600 bytes of message strings
 * from the blob (the sweep no longer sees a live pointer into them). On -m32
 * int32_t and const char* are both 4 bytes, so the int32* stride the reader
 * uses still lands one field per step. */
/* __app_type at 0x0048CC54 (2 = GUI; __set_app_type writes it at startup). */
extern "C" int32_t am2_crt_app_type = 2;
/* The _fpinit and _exit function-pointer slots at 0x0048CC28 / 0x0048CC50: image
 * .text addresses, so crt_cinit's call through them is dead in native (the boot
 * proves it -- see the init/term tables). Transcribed byte-exact as pointers. */
extern "C" const uint32_t am2_crt_fpinit_ptr = 0x0046443Eu;
extern "C" const uint32_t am2_crt_exit_fn_ptr = 0x00469320u;
/* The Pentium-FDIV self-test operands at 0x0046FDB8/0x0046FDC0 (the famous
 * 4195835.0 / 3145727.0 division _adjust_fdiv runs). Read-only const doubles. */
extern "C" const double am2_crt_fdiv_num = 4195835.0;
extern "C" const double am2_crt_fdiv_den = 3145727.0;
/* The C-runtime init/term function-pointer tables at 0x00473000: XC (the C++
 * static initializers, 0x473000..0x473058), XI (0x47305C..0x473070), XP
 * (0x473074..0x47307C) and XT (0x473080..0x473088), each 0-terminated at both
 * ends. crt_cinit walks XI then XC through crt_initterm at startup and exit
 * walks XP and XT -- and that walk IS reached natively: every entry is a
 * reconstruction. (An earlier note here called the walk dead; it was live all
 * along -- the original stored jmp THUNKS into its own .text and mkglobals
 * rewrote each slot to ours before WinMain, which is what InitRemapIdentity
 * needs to fill g_remapIdent before SetGamePalette writes through it.)
 * Transcribed as a named table of the reconstructions themselves, so no fixup
 * is needed: checkimagedata follows each image thunk to its target and checks
 * it names the same function, and the range sits in mkglobals.py's MIGRATED
 * list. The _BEGIN/_END macros alias into it (standalone.h). The 21 XC entries
 * are game initializers, declared here with their exact extern "C" __cdecl
 * signatures (air.h, dplay.h, surface.h, gameproc.h, item.h) rather than by
 * pulling those headers into the CRT. */
extern "C" {
void    __cdecl AirInitLeg2MsSpan(void);
void    __cdecl AirInitTurnYIn(void);
void    __cdecl AirInitTurnYOut(void);
void    __cdecl AirInitLeg2Divisor(void);
void    __cdecl AirInitLeg1X0(void);
void    __cdecl AirInitLeg3X1(void);
void    __cdecl AirInitLeg1Dx(void);
void    __cdecl AirInitLeg1Dy(void);
void    __cdecl AirInitLeg3Dx(void);
void    __cdecl AirInitLeg3Dy(void);
void    __cdecl AirInitLeg2Dy(void);
int32_t __cdecl CommGlobalInit(void);
void    __cdecl InitViewColourCopy(void);
void    __cdecl InitRemapIdentity(void);
void    __cdecl InitDefaultPalette(void);
void    __cdecl InitRemapBright(void);
void    __cdecl InitOverlayPalette(void);
void    __cdecl InitStartupColours(void);
void    __cdecl InitStartupColoursB(void);
}
/* These two are declared OUTSIDE their headers' extern "C" blocks (item.h,
 * gameproc.h) and so carry C++ linkage; the forward declaration has to match
 * or the link fails -- linkage mismatches go both ways. */
int32_t __cdecl SelListInit(void);
void    __cdecl InitItemHeaderSize(void);
extern "C" const am2_init_fn am2_crt_inittab[36] = {
    0,                                             /* XC begin */
    (am2_init_fn)AirInitLeg2MsSpan,
    (am2_init_fn)AirInitTurnYIn,
    (am2_init_fn)AirInitTurnYOut,
    (am2_init_fn)AirInitLeg2Divisor,
    (am2_init_fn)AirInitLeg1X0,
    (am2_init_fn)AirInitLeg3X1,
    (am2_init_fn)AirInitLeg1Dx,
    (am2_init_fn)AirInitLeg1Dy,
    (am2_init_fn)AirInitLeg3Dx,
    (am2_init_fn)AirInitLeg3Dy,
    (am2_init_fn)AirInitLeg2Dy,
    (am2_init_fn)CommGlobalInit,
    (am2_init_fn)InitViewColourCopy,
    (am2_init_fn)InitRemapIdentity,
    (am2_init_fn)InitDefaultPalette,
    (am2_init_fn)InitRemapBright,
    (am2_init_fn)InitOverlayPalette,
    (am2_init_fn)SelListInit,
    (am2_init_fn)InitStartupColours,
    (am2_init_fn)InitItemHeaderSize,
    (am2_init_fn)InitStartupColoursB,
    0, 0,                                          /* XC end, XI begin */
    (am2_init_fn)crt_onexitinit,
    (am2_init_fn)crt_initstdio,
    (am2_init_fn)crt_initmbctable,
    (am2_init_fn)crt_seh_set,
    0, 0,                                          /* XI end, XP begin */
    (am2_init_fn)crt_endstdio,
    0, 0,                                          /* XP end, XT begin */
    (am2_init_fn)crt_seh_restore,
    0, 0,                                          /* XT end, pad */
};

struct AM2_RtErr { int32_t num; const char *msg; };
extern "C" const AM2_RtErr am2_crt_rterr_table[18] = {
    { 0x0002, "R6002\r\n- floating point not loaded\r\n" },
    { 0x0008, "R6008\r\n- not enough space for arguments\r\n" },
    { 0x0009, "R6009\r\n- not enough space for environment\r\n" },
    { 0x000A, "\r\nabnormal program termination\r\n" },
    { 0x0010, "R6016\r\n- not enough space for thread data\r\n" },
    { 0x0011, "R6017\r\n- unexpected multithread lock error\r\n" },
    { 0x0012, "R6018\r\n- unexpected heap error\r\n" },
    { 0x0013, "R6019\r\n- unable to open console device\r\n" },
    { 0x0018, "R6024\r\n- not enough space for _onexit/atexit table\r\n" },
    { 0x0019, "R6025\r\n- pure virtual function call\r\n" },
    { 0x001A, "R6026\r\n- not enough space for stdio initialization\r\n" },
    { 0x001B, "R6027\r\n- not enough space for lowio initialization\r\n" },
    { 0x001C, "R6028\r\n- unable to initialize heap\r\n" },
    { 0x0078, "DOMAIN error\r\n" },
    { 0x0079, "SING error\r\n" },
    { 0x007A, "TLOSS error\r\n" },
    { 0x00FC, "\r\n" },
    { 0x00FF, "runtime error " },
};

/* The multibyte-codepage init data crt_setmbcp reads: eight range-set flag
 * bytes at 0x0048D520, and the five-record codepage table at 0x0048D528
 * (932/936/949/950/1361; 0x30 bytes each, scanned up to ADDR_CRT_POW10_TABLE).
 * Pure const data, so the blob copies drop -- placed at their VAs, byte-checked. */
extern "C" const uint8_t am2_crt_mbctype_range_flags[8] = { 1, 2, 4, 8, 0, 0, 0, 0 };
extern "C" const uint8_t am2_crt_mbcp_table[240] = {
    0xA4, 0x03, 0x00, 0x00, 0x60, 0x82, 0x79, 0x82, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA1, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x9F, 0xE0, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x40, 0x7E, 0x80, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0xA8, 0x03, 0x00, 0x00, 0xC1, 0xA3, 0xDA, 0xA3, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xB5, 0x03, 0x00, 0x00, 0xC1, 0xA3, 0xDA, 0xA3, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xB6, 0x03, 0x00, 0x00, 0xCF, 0xA2, 0xE4, 0xA2, 0x1A, 0x00, 0xE5, 0xA2, 0xE8, 0xA2, 0x5B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x7E, 0xA1, 0xFE, 0x00, 0x00, 0x00, 0x00,
    0x51, 0x05, 0x00, 0x00, 0x51, 0xDA, 0x5E, 0xDA, 0x20, 0x00, 0x5F, 0xDA, 0x6A, 0xDA, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0xD3, 0xD8, 0xDE, 0xE0, 0xF9, 0x00, 0x00, 0x31, 0x7E, 0x81, 0xFE, 0x00, 0x00, 0x00, 0x00,
};
#endif

#define G32(a)   (*(int32_t *)(uintptr_t)AM2_IMAGE(a))
#define GU32(a)  (*(uint32_t *)(uintptr_t)AM2_IMAGE(a))
typedef void (__cdecl *CRT_VoidFn)(void);
typedef void (__cdecl *CRT_IntFn)(int32_t);
#define GVAL(t, a) (*(t *)(uintptr_t)AM2_IMAGE(a))
#define STR(a)   ((const char *)(uintptr_t)AM2_IMAGE(a))

#define crt_acmdln       GVAL(char *, ADDR_CRT_ACMDLN)
#define crt_aenvptr      GVAL(char *, ADDR_CRT_AENVPTR)
#define crt_pgmptr       GVAL(char *, ADDR_CRT_PGMPTR)
#define crt_pgmname      ((char *)(uintptr_t)AM2_IMAGE(ADDR_CRT_PGMNAME))
#define crt_argv         GVAL(char **, ADDR_CRT_ARGV)
#define crt_argc         G32(ADDR_CRT_ARGC)
#define crt_environ      GVAL(char **, ADDR_CRT_ENVIRON)
#define crt_env_initialized G32(ADDR_CRT_ENV_INITIALIZED)
#define crt_env_kind     G32(ADDR_CRT_ENV_KIND)
#define crt_mbctable_init G32(ADDR_CRT_MBCTABLE_INIT)
#define crt_mbctype      ((uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCTYPE))
#define crt_mbcasemap    ((uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCASEMAP))
#define crt_mbcodepage   GU32(ADDR_CRT_MBCODEPAGE)
#define crt_mblcid       GU32(ADDR_CRT_MBLCID)
#define crt_ismbcodepage G32(ADDR_CRT_ISMBCODEPAGE)
#define crt_mbulinfo     ((uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBULINFO))
#define crt_mbcp_from_system G32(ADDR_CRT_MBCP_FROM_SYSTEM)
#define crt_lc_codepage  GU32(ADDR_CRT_LC_CODEPAGE)
#define crt_error_mode   G32(ADDR_CRT_ERROR_MODE)
#define crt_app_type     G32(ADDR_CRT_APP_TYPE)
#define crt_adjust_fdiv  G32(ADDR_CRT_ADJUST_FDIV)
#define crt_pctype_tab   GVAL(const uint16_t *, ADDR_CRT_PCTYPE)

#define CRT_MB_CP_OEM    (-2)
#define CRT_MB_CP_ANSI   (-3)
#define CRT_MB_CP_LOCALE (-4)
#define CRT_MB_SBUP      0x10
#define CRT_MB_SBLOW     0x20

/* ---- the environment ---------------------------------------------------- */

char *__cdecl crt_get_environment_strings(void)
{
    int32_t  kind = crt_env_kind;
    WCHAR   *wide = NULL;
    char    *narrow = NULL;
    char    *copy;

    if (kind == 0) {
        wide = GetEnvironmentStringsW();
        if (wide != NULL) {
            crt_env_kind = 1;
        } else {
            narrow = GetEnvironmentStrings();
            if (narrow == NULL)
                return NULL;
            crt_env_kind = 2;
            goto narrow_block;
        }
    } else if (kind != 1) {
        if (kind != 2)
            return NULL;
        goto narrow_block;
    }

    /* The wide block, to its double NUL, converted through the ANSI code
     * page into a malloc'd copy. */
    if (wide == NULL) {
        wide = GetEnvironmentStringsW();
        if (wide == NULL)
            return NULL;
    }
    {
        const WCHAR *p = wide;
        int32_t      n, bytes;
        BOOL         ok;

        if (*p != 0) {
            do {
                p++;
                while (*p != 0)
                    p++;
                p++;
            } while (*p != 0);
        }
        n = (int32_t)(p - wide) + 1;
        bytes = WideCharToMultiByte(CP_ACP, 0, wide, n, NULL, 0, NULL, NULL);
        copy = NULL;
        if (bytes != 0) {
            copy = (char *)crt_malloc((uint32_t)bytes);
            if (copy != NULL) {
                ok = WideCharToMultiByte(CP_ACP, 0, wide, n, copy, bytes, NULL, NULL) != 0;
                if (!ok) {
                    crt_free(copy);
                    copy = NULL;
                }
            }
        }
        FreeEnvironmentStringsW(wide);
        return copy;
    }

narrow_block:
    if (narrow == NULL) {
        narrow = GetEnvironmentStrings();
        if (narrow == NULL)
            return NULL;
    }
    {
        const char *p = narrow;
        uint32_t    n;

        if (*p != 0) {
            do {
                p++;
                while (*p != 0)
                    p++;
                p++;
            } while (*p != 0);
        }
        n = (uint32_t)(p - narrow) + 1;
        copy = (char *)crt_malloc(n);
        if (copy != NULL)
            crt_memcpy(copy, narrow, n);
        FreeEnvironmentStringsA(narrow);
        return copy;
    }
}

void __cdecl crt_setenvp(void)
{
    char  *env;
    char **table;
    int32_t count = 0;

    if (crt_mbctable_init == 0)
        crt_initmbctable();
    /* Count the strings, leaving out the =X: drive entries. */
    for (env = crt_aenvptr; *env != 0; env += crt_strlen(env) + 1)
        if (*env != '=')
            count++;
    table = (char **)crt_malloc((uint32_t)(count + 1) * sizeof(char *));
    crt_environ = table;
    if (table == NULL)
        crt_amsg_exit(9);
    for (env = crt_aenvptr; *env != 0; ) {
        int32_t len = crt_strlen(env) + 1;

        if (*env != '=') {
            *table = (char *)crt_malloc((uint32_t)len);
            if (*table == NULL)
                crt_amsg_exit(9);
            crt_strcpy(*table, env);
            table++;
        }
        env += len;
    }
    crt_free(crt_aenvptr);
    crt_aenvptr = NULL;
    *table = NULL;
    crt_env_initialized = 1;
}

/* ---- the command line --------------------------------------------------- */

void __cdecl crt_parse_cmdline(const char *cmd, char **argv, char *args, int32_t *argc, int32_t *nchars)
{
    const uint8_t *p = (const uint8_t *)cmd;
    int32_t        inquote, copychar, slashes;

    *nchars = 0;
    *argc = 1;
    if (argv)
        *argv++ = args;

    /* The program name, which is one argument whatever it contains: a
     * quoted one runs to the closing quote, a bare one to a space. A lead
     * byte takes the byte after it along. */
    if (*p == '"') {
        p++;
        while (*p != '"' && *p != 0) {
            if (crt_mbctype[*p + 1] & 4) {
                (*nchars)++;
                if (args)
                    *args++ = (char)*p;
                p++;
            }
            (*nchars)++;
            if (args)
                *args++ = (char)*p;
            p++;
        }
        (*nchars)++;
        if (args)
            *args++ = 0;
        if (*p == '"')
            p++;
    } else {
        do {
            uint8_t c;

            (*nchars)++;
            if (args)
                *args++ = (char)*p;
            c = *p++;
            if (crt_mbctype[c + 1] & 4) {
                (*nchars)++;
                if (args)
                    *args++ = (char)*p;
                p++;
            }
            if (c == ' ' || c == 0 || c == '\t')
                break;
        } while (1);
        if (p[-1] == 0)
            p--;
        else if (args)
            args[-1] = 0;
    }

    /* The arguments: backslashes before a quote pair down, a quote
     * toggles the run, a doubled quote inside a run is a literal one. */
    inquote = 0;
    while (*p != 0) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == 0)
            break;
        if (argv)
            *argv++ = args;
        (*argc)++;
        for (;;) {
            copychar = 1;
            slashes = 0;
            while (*p == '\\') {
                p++;
                slashes++;
            }
            if (*p == '"') {
                if (!(slashes & 1)) {
                    if (inquote && p[1] == '"')
                        p++;
                    else
                        copychar = 0;
                    inquote = !inquote;
                }
                slashes >>= 1;
            }
            while (slashes--) {
                if (args)
                    *args++ = '\\';
                (*nchars)++;
            }
            if (*p == 0 || (!inquote && (*p == ' ' || *p == '\t')))
                break;
            if (copychar) {
                if (args) {
                    if (crt_mbctype[*p + 1] & 4) {
                        *args++ = (char)*p++;
                        (*nchars)++;
                    }
                    *args++ = (char)*p;
                } else if (crt_mbctype[*p + 1] & 4) {
                    p++;
                    (*nchars)++;
                }
                (*nchars)++;
            }
            p++;
        }
        if (args)
            *args++ = 0;
        (*nchars)++;
    }
    if (argv)
        *argv = NULL;
    (*argc)++;
}

void __cdecl crt_setargv(void)
{
    const char *cmd;
    char      **table;
    int32_t     argc, nchars;

    if (crt_mbctable_init == 0)
        crt_initmbctable();
    GetModuleFileNameA(NULL, crt_pgmname, 0x104);
    crt_pgmptr = crt_pgmname;
    cmd = crt_acmdln;
    if (*cmd == 0)
        cmd = crt_pgmname;
    crt_parse_cmdline(cmd, NULL, NULL, &argc, &nchars);
    table = (char **)crt_malloc((uint32_t)nchars + (uint32_t)argc * sizeof(char *));
    if (table == NULL)
        crt_amsg_exit(8);
    crt_parse_cmdline(cmd, table, (char *)(table + argc), &argc, &nchars);
    crt_argv = table;
    crt_argc = argc - 1;
}

char *__cdecl crt_wincmdln(void)
{
    const uint8_t *p;

    if (crt_mbctable_init == 0)
        crt_initmbctable();
    p = (const uint8_t *)crt_acmdln;
    if (*p == '"') {
        /* Past the closing quote, lead bytes taking the byte after. */
        for (;;) {
            uint8_t c = *++p;

            if (c == '"' || c == 0)
                break;
            if (crt_ismbblead(c))
                p++;
        }
        if (*p == '"')
            p++;
    } else {
        while (*p > ' ')
            p++;
    }
    while (*p != 0 && *p <= ' ')
        p++;
    return (char *)p;
}

/* ---- the multibyte-character tables -------------------------------------- */

int32_t __cdecl crt_getsystemcp(int32_t cp)
{
    crt_mbcp_from_system = 0;
    if (cp == CRT_MB_CP_OEM) {
        crt_mbcp_from_system = 1;
        return (int32_t)GetOEMCP();
    }
    if (cp == CRT_MB_CP_ANSI) {
        crt_mbcp_from_system = 1;
        return (int32_t)GetACP();
    }
    if (cp == CRT_MB_CP_LOCALE) {
        crt_mbcp_from_system = 1;
        return (int32_t)crt_lc_codepage;
    }
    return cp;
}

uint32_t __cdecl crt_cp_lcid(int32_t cp)
{
    switch (cp) {
    case 932:  return 0x411;
    case 936:  return 0x804;
    case 949:  return 0x412;
    case 950:  return 0x404;
    default:   return 0;
    }
}

void __cdecl crt_mbctype_reset(void)
{
    int32_t i;

    for (i = 0; i < 0x101; i++)
        crt_mbctype[i] = 0;
    crt_mbcodepage = 0;
    crt_ismbcodepage = 0;
    crt_mblcid = 0;
    for (i = 0; i < 12; i++)
        crt_mbulinfo[i] = 0;
}

int32_t __cdecl crt_ismbbtype(int32_t c, int32_t ctype_mask, int32_t mbctype_mask)
{
    uint8_t b = (uint8_t)c;

    if (crt_mbctype[b + 1] & mbctype_mask)
        return 1;
    if (ctype_mask == 0)
        return 0;
    return (crt_pctype_tab[b] & ctype_mask) != 0;
}

int32_t __cdecl crt_ismbblead(int32_t c)
{
    return crt_ismbbtype(c, 0, 4);
}

/* The case bits and the case map for a single-byte code page: every byte
 * classified through GetStringTypeA and mapped both ways through
 * LCMapStringA, or, when GetCPInfo has nothing to say, the ASCII letters. */
void __cdecl crt_setsbuplow(void)
{
    CPINFO   info;
    uint8_t  chars[256];
    uint16_t types[256];
    char     lower[256], upper[256];
    int32_t  i;

    if (GetCPInfo(crt_mbcodepage, &info)) {
        for (i = 0; i < 256; i++)
            chars[i] = (uint8_t)i;
        chars[0] = ' ';
        /* Lead bytes cannot be classified alone: a space stands in. */
        for (i = 0; info.LeadByte[i] != 0 && info.LeadByte[i + 1] != 0; i += 2) {
            int32_t k;

            for (k = info.LeadByte[i]; k <= info.LeadByte[i + 1]; k++)
                chars[k] = ' ';
        }
        crt_get_string_type_a(1, (const char *)chars, 256, types, crt_mbcodepage, crt_mblcid, 0);
        crt_lcmapstring_a(crt_mblcid, 0x100, (const char *)chars, 256, lower, 256, crt_mbcodepage, 0);
        crt_lcmapstring_a(crt_mblcid, 0x200, (const char *)chars, 256, upper, 256, crt_mbcodepage, 0);
        for (i = 0; i < 256; i++) {
            if (types[i] & 1) {
                crt_mbctype[i + 1] |= CRT_MB_SBUP;
                crt_mbcasemap[i] = (uint8_t)lower[i];
            } else if (types[i] & 2) {
                crt_mbctype[i + 1] |= CRT_MB_SBLOW;
                crt_mbcasemap[i] = (uint8_t)upper[i];
            } else {
                crt_mbcasemap[i] = 0;
            }
        }
    } else {
        for (i = 0; i < 256; i++) {
            if (i >= 'A' && i <= 'Z') {
                crt_mbctype[i + 1] |= CRT_MB_SBUP;
                crt_mbcasemap[i] = (uint8_t)(i + 0x20);
            } else if (i >= 'a' && i <= 'z') {
                crt_mbctype[i + 1] |= CRT_MB_SBLOW;
                crt_mbcasemap[i] = (uint8_t)(i - 0x20);
            } else {
                crt_mbcasemap[i] = 0;
            }
        }
    }
}

int32_t __cdecl crt_setmbcp(int32_t cp)
{
    const uint8_t *table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCP_TABLE);
    const uint8_t *table_end = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_POW10_TABLE);
    const uint8_t *flags = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MBCTYPE_RANGE_FLAGS);
    const uint8_t *entry;
    CPINFO         info;
    int32_t        i;

    cp = crt_getsystemcp(cp);
    if ((uint32_t)cp == crt_mbcodepage)
        return 0;
    if (cp == 0) {
        crt_mbctype_reset();
        crt_setsbuplow();
        return 0;
    }
    /* One of the five multibyte code pages the image carries tables for:
     * its lead-byte ranges and the rest, no system call needed. */
    for (entry = table; entry < table_end; entry += 0x30)
        if (*(const uint32_t *)entry == (uint32_t)cp)
            break;
    if (entry < table_end) {
        int32_t set;

        for (i = 0; i < 0x101; i++)
            crt_mbctype[i] = 0;
        for (set = 0; set < 4; set++) {
            const uint8_t *range = entry + 0x10 + set * 8;

            for (; range[0] != 0 && range[1] != 0; range += 2) {
                int32_t k;

                for (k = range[0]; k <= range[1]; k++)
                    crt_mbctype[k + 1] |= flags[set];
            }
        }
        crt_ismbcodepage = 1;
        crt_mbcodepage = (uint32_t)cp;
        crt_mblcid = crt_cp_lcid(cp);
        for (i = 0; i < 12; i++)
            crt_mbulinfo[i] = entry[4 + i];
        crt_setsbuplow();
        return 0;
    }
    if (GetCPInfo((UINT)cp, &info) != TRUE) {
        if (crt_mbcp_from_system == 0)
            return -1;
        crt_mbctype_reset();
        crt_setsbuplow();
        return 0;
    }
    for (i = 0; i < 0x101; i++)
        crt_mbctype[i] = 0;
    crt_mbcodepage = (uint32_t)cp;
    crt_mblcid = 0;
    if (info.MaxCharSize > 1) {
        /* A multibyte page the table does not know: its lead bytes from
         * GetCPInfo, every trail byte assumed. */
        for (i = 0; info.LeadByte[i] != 0 && info.LeadByte[i + 1] != 0; i += 2) {
            int32_t k;

            for (k = info.LeadByte[i]; k <= info.LeadByte[i + 1]; k++)
                crt_mbctype[k + 1] |= 4;
        }
        for (i = 1; i < 0xFF; i++)
            crt_mbctype[i + 1] |= 8;
        crt_mblcid = crt_cp_lcid(cp);
        crt_ismbcodepage = 1;
    } else {
        crt_ismbcodepage = 0;
    }
    for (i = 0; i < 12; i++)
        crt_mbulinfo[i] = 0;
    crt_setsbuplow();
    return 0;
}

void __cdecl crt_initmbctable(void)
{
    if (crt_mbctable_init == 0) {
        crt_setmbcp(CRT_MB_CP_ANSI);
        crt_mbctable_init = 1;
    }
}

/* ---- the runtime-error exit --------------------------------------------- */

int32_t __cdecl crt_messagebox(const char *text, const char *caption, uint32_t type)
{
    typedef int32_t (WINAPI *MsgBoxFn)(HWND, LPCSTR, LPCSTR, UINT);
    typedef HWND (WINAPI *WndFn)(void);
    typedef HWND (WINAPI *PopupFn)(HWND);
    MsgBoxFn *msgbox = (MsgBoxFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_MSGBOX_FN);
    WndFn    *active = (WndFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_GETACTIVEWINDOW_FN);
    PopupFn  *popup = (PopupFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_GETLASTACTIVEPOPUP_FN);
    HWND      owner = NULL;

    if (*msgbox == NULL) {
        HMODULE user32 = LoadLibraryA(STR(ADDR_STR_USER32));

        if (user32 == NULL)
            return 0;
        *msgbox = (MsgBoxFn)(uintptr_t)GetProcAddress(user32, STR(ADDR_STR_MESSAGEBOXA));
        if (*msgbox == NULL)
            return 0;
        *active = (WndFn)(uintptr_t)GetProcAddress(user32, STR(ADDR_STR_GETACTIVEWINDOW));
        *popup = (PopupFn)(uintptr_t)GetProcAddress(user32, STR(ADDR_STR_GETLASTACTIVEPOPUP));
    }
    if (*active != NULL) {
        owner = (*active)();
        if (owner != NULL && *popup != NULL)
            owner = (*popup)(owner);
    }
    return (*msgbox)(owner, text, caption, type);
}

void __cdecl crt_nmsg_write(int32_t rterrnum)
{
    const int32_t *table = (const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_RTERR_TABLE);
    const int32_t *end = (const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CVTINFO_DOUBLE);
    const char    *text;
    char           name[0x104];
    char           message[0x74];
    const char    *shown;

    for (; table < end; table += 2)
        if (table[0] == rterrnum)
            break;
    if (table >= end)
        return;
    text = (const char *)(uintptr_t)AM2_IMAGE((uintptr_t)table[1]);
    if (crt_error_mode == 1 || (crt_error_mode == 0 && crt_app_type == 1)) {
        /* A console application: the text to stderr. */
        DWORD wrote;

        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)crt_strlen(text), &wrote, NULL);
        return;
    }
    if (rterrnum == 0xFC)
        return;
    /* A windowed one: a box, with the program's name shortened from the
     * left to sixty characters. */
    if (GetModuleFileNameA(NULL, name, 0x104) == 0)
        crt_strcpy(name, STR(ADDR_STR_RTERR_NONAME));
    shown = name;
    if (crt_strlen(name) + 1 > 0x3C) {
        char *tail = name + crt_strlen(name) - 0x3B;

        crt_strncpy(tail, STR(ADDR_STR_RTERR_ELLIPSIS), 3);
        shown = tail;
    }
    crt_strcpy(message, STR(ADDR_STR_RTERR_BANNER));
    crt_strcat(message, shown);
    crt_strcat(message, STR(ADDR_STR_RTERR_CRLF2));
    crt_strcat(message, text);
    crt_messagebox(message, STR(ADDR_STR_RTERR_CAPTION), 0x12010);
}

void __cdecl crt_ff_msgbanner(void)
{
    CRT_VoidFn hook;

    if (crt_error_mode == 1 || (crt_error_mode == 0 && crt_app_type == 1)) {
        crt_nmsg_write(0xFC);
        hook = GVAL(CRT_VoidFn, ADDR_CRT_MSGBANNER_HOOK);
        if (hook)
            hook();
        crt_nmsg_write(0xFF);
    }
}

void __cdecl crt_amsg_exit(int32_t rterrnum)
{
    CRT_IntFn exit_fn = GVAL(CRT_IntFn, ADDR_CRT_EXIT_FN_PTR);

    if (crt_error_mode == 1)
        crt_ff_msgbanner();
    crt_nmsg_write(rterrnum);
    exit_fn(255);
}

void __cdecl crt_fast_error_exit(int32_t rterrnum)
{
    if (crt_error_mode == 1)
        crt_ff_msgbanner();
    crt_nmsg_write(rterrnum);
    ExitProcess(255);
}

/* ---- floating point ------------------------------------------------------ */

uint32_t __cdecl crt_hw2abstract(uint32_t cw)
{
    uint32_t f = 0;

    if (cw & 0x01) f |= 0x10;
    if (cw & 0x04) f |= 0x08;
    if (cw & 0x08) f |= 0x04;
    if (cw & 0x10) f |= 0x02;
    if (cw & 0x20) f |= 0x01;
    if (cw & 0x02) f |= 0x80000;
    switch (cw & 0xC00) {
    case 0x400: f |= 0x100; break;
    case 0x800: f |= 0x200; break;
    case 0xC00: f |= 0x300; break;
    default: break;
    }
    switch (cw & 0x300) {
    case 0x000: f |= 0x20000; break;
    case 0x200: f |= 0x10000; break;
    default: break;
    }
    if (cw & 0x1000)
        f |= 0x40000;
    return f;
}

uint32_t __cdecl crt_abstract2hw(uint32_t f)
{
    uint32_t cw = 0;

    if (f & 0x10) cw |= 0x01;
    if (f & 0x08) cw |= 0x04;
    if (f & 0x04) cw |= 0x08;
    if (f & 0x02) cw |= 0x10;
    if (f & 0x01) cw |= 0x20;
    if (f & 0x80000) cw |= 0x02;
    switch (f & 0x300) {
    case 0x100: cw |= 0x400; break;
    case 0x200: cw |= 0x800; break;
    case 0x300: cw |= 0xC00; break;
    default: break;
    }
    switch (f & 0x30000) {
    case 0x00000: cw |= 0x300; break;
    case 0x10000: cw |= 0x200; break;
    default: break;
    }
    if (f & 0x40000)
        cw |= 0x1000;
    return cw;
}

/* The x87 control word read and written; the body is the original's, the
 * two instructions are what it executes. */
static uint16_t crt_fnstcw(void)
{
    uint16_t cw;

    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    return cw;
}

static void crt_fldcw(uint16_t cw)
{
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
}

uint32_t __cdecl crt_control87(uint32_t newval, uint32_t mask)
{
    uint32_t old, next;

    mask &= 0xFFF7FFFFu;   /* the infinity-control bit is not a thing on this FPU */
    old = crt_hw2abstract(crt_fnstcw());
    next = (old & ~mask) | (newval & mask);
    crt_fldcw((uint16_t)crt_abstract2hw(next));
    return next;
}

void __cdecl crt_setdefaultprecision(void)
{
    crt_control87(0x10000, 0x30000);
}

int32_t __cdecl crt_fdiv_test(void)
{
    /* The Pentium FDIV test, in the arithmetic the original does: the
     * quotient times the divisor, back from the dividend, over one. */
    volatile double num = GVAL(const double, ADDR_CRT_FDIV_NUM);
    volatile double den = GVAL(const double, ADDR_CRT_FDIV_DEN);
    volatile double r = num - (num / den) * den;

    return r > GVAL(const double, ADDR_DBL_MAX_PERIOD) ? 1 : 0;   /* the image's one 1.0, shared */
}

int32_t __cdecl crt_fdiv_detect(void)
{
    HMODULE kernel32 = GetModuleHandleA(STR(ADDR_STR_KERNEL32));

    if (kernel32 != NULL) {
        BOOL (WINAPI *present)(DWORD) =
            (BOOL (WINAPI *)(DWORD))(uintptr_t)GetProcAddress(kernel32, STR(ADDR_STR_ISPROCESSORFEATUREPRESENT));

        if (present != NULL)
            return present(0) ? 1 : 0;
    }
    return crt_fdiv_test();
}

void __cdecl crt_cfltcvt_init(void)
{
    /* The original fills a table of eight function pointers that _output
     * calls through; printf.cpp calls fltcvt.cpp by name. */
}

void __cdecl crt_fpmath(void)
{
    crt_cfltcvt_init();
    crt_adjust_fdiv = crt_fdiv_detect();
    crt_setdefaultprecision();
    __asm__ __volatile__("fnclex");
}

/* ---- _cinit and the startup ---------------------------------------------- */

void __cdecl crt_cinit(void)
{
    CRT_VoidFn fpinit = GVAL(CRT_VoidFn, ADDR_CRT_FPINIT_PTR);

    if (fpinit != NULL)
        fpinit();
    crt_initterm((CRT_ExitFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_XI_BEGIN),
                 (CRT_ExitFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_XI_END));
    crt_initterm((CRT_ExitFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_XC_BEGIN),
                 (CRT_ExitFn *)(uintptr_t)AM2_IMAGE(ADDR_CRT_XC_END));
}

void __cdecl crt_startup(void)
{
    uint32_t version = GetVersion();

    GU32(ADDR_CRT_WINMINOR) = (version >> 8) & 0xFF;
    GU32(ADDR_CRT_WINMAJOR) = version & 0xFF;
    GU32(ADDR_CRT_WINVER) = ((version & 0xFF) << 8) + ((version >> 8) & 0xFF);
    GU32(ADDR_CRT_OSVER) = version >> 16;
    if (!crt_heap_init(0))
        crt_fast_error_exit(0x1C);
    crt_ioinit();
    crt_acmdln = GetCommandLineA();
    crt_aenvptr = crt_get_environment_strings();
    crt_setargv();
    crt_setenvp();
    crt_cinit();
}
