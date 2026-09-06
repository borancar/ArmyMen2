/* exit.cpp -- atexit and the way out, from the bodies at 0x00464FA4
 * (_onexit), 0x00465011 (atexit), 0x00465023 (_onexitinit), 0x0046930F
 * (exit), 0x00469320 (_exit), 0x00469331 (doexit), 0x004693CA (_initterm),
 * 0x00469C8B (_endstdio), 0x0046C20B (_fcloseall) and the exception-filter
 * pair at 0x0046B3AD and 0x0046B3BE.
 *
 * The onexit table is a malloc'd array of function pointers between
 * __onexitbegin and __onexitend, 32 entries from startup and four more
 * each time it fills; exit runs it from the end, then the image's two
 * terminator tables, then ExitProcess. In the standalone and native
 * builds those tables still hold the ORIGINAL's addresses -- {0, _endstdio}
 * and {0, the filter restore} -- and mkglobals rewrites every pointer in
 * the image's data that a standalone.h seam names, so with the two seamed
 * they call the reconstructions there. Startup's _onexitinit runs from
 * the first registration here, since nothing runs the startup.
 */
#include "crt.h"
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define crt_onexitbegin   (*(CRT_ExitFn **)(uintptr_t)AM2_IMAGE(ADDR_CRT_ONEXITBEGIN))
#define crt_onexitend     (*(CRT_ExitFn **)(uintptr_t)AM2_IMAGE(ADDR_CRT_ONEXITEND))
#define crt_exit_done     (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_EXIT_DONE))
#define crt_exit_started  (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_EXIT_STARTED))
#define crt_exit_retcaller (*(uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_EXIT_RETCALLER))
#define crt_piob          (*(CRT_FILE ***)(uintptr_t)AM2_IMAGE(ADDR_CRT_PIOB))
#define crt_nstream       (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_NSTREAM))
#define crt_old_filter    (*(LPTOP_LEVEL_EXCEPTION_FILTER *)(uintptr_t)AM2_IMAGE(ADDR_CRT_SEH_OLD_FILTER))
#define CRT_TABLE(a)      ((CRT_ExitFn *)(uintptr_t)AM2_IMAGE(a))

void __cdecl crt_onexitinit(void)
{
    CRT_ExitFn *table = (CRT_ExitFn *)crt_malloc(0x80);

    crt_onexitbegin = table;
    if (!table)
        crt_amsg_exit(0x18);
    *table = NULL;
    crt_onexitend = table;
}

CRT_ExitFn __cdecl crt_onexit(CRT_ExitFn fn)
{
    CRT_ExitFn *begin, *end;

    if (crt_onexitbegin == NULL)
        crt_onexitinit();
    begin = crt_onexitbegin;
    end = crt_onexitend;
    if (crt_msize(begin) < (uint32_t)((uint8_t *)end - (uint8_t *)begin) + 4) {
        CRT_ExitFn *grown = (CRT_ExitFn *)crt_realloc(begin, crt_msize(begin) + 0x10);

        if (!grown)
            return NULL;
        end = grown + (end - begin);
        crt_onexitbegin = grown;
        crt_onexitend = end;
    }
    *end = fn;
    crt_onexitend = end + 1;
    return fn;
}

int32_t __cdecl crt_atexit(CRT_ExitFn fn)
{
    return crt_onexit(fn) ? 0 : -1;
}

void __cdecl crt_initterm(CRT_ExitFn *begin, CRT_ExitFn *end)
{
    for (; begin < end; begin++)
        if (*begin)
            (*begin)();
}

int32_t __cdecl crt_fcloseall(void)
{
    int32_t i, count = 0;

    for (i = 3; i < crt_nstream; i++) {
        CRT_FILE *f = crt_piob[i];

        if (!f)
            continue;
        if ((f->flag & (CRT_IOREAD | CRT_IOWRT | CRT_IORW)) && crt_fclose(f) != -1)
            count++;
        if (i >= 20) {
            crt_free(crt_piob[i]);
            crt_piob[i] = NULL;
        }
    }
    return count;
}

void __cdecl crt_endstdio(void)
{
    crt_flushall(1);
    if (crt_exit_retcaller)
        crt_fcloseall();
}

/* The CRT installs its own unhandled-exception filter at startup and puts
 * the previous one back on exit. The filter itself (0x0046B367) is the
 * C++ exception dispatcher's and is not reproduced: nothing here throws. */
void __cdecl crt_seh_set(void)
{
    crt_old_filter = SetUnhandledExceptionFilter(NULL);
}

void __cdecl crt_seh_restore(void)
{
    SetUnhandledExceptionFilter(crt_old_filter);
}

void __cdecl crt_doexit(int32_t code, int32_t quick, int32_t retcaller)
{
    if (crt_exit_done == 1)
        TerminateProcess(GetCurrentProcess(), (UINT)code);
    crt_exit_started = 1;
    crt_exit_retcaller = (uint8_t)retcaller;
    if (!quick) {
        CRT_ExitFn *begin = crt_onexitbegin;

        if (begin != NULL) {
            CRT_ExitFn *p = crt_onexitend - 1;

            for (; p >= begin; p--)
                if (*p)
                    (*p)();
        }
        crt_initterm(CRT_TABLE(ADDR_CRT_XP_BEGIN), CRT_TABLE(ADDR_CRT_XP_END));
    }
    crt_initterm(CRT_TABLE(ADDR_CRT_XT_BEGIN), CRT_TABLE(ADDR_CRT_XT_END));
    if (!retcaller) {
        crt_exit_done = 1;
        ExitProcess((UINT)code);
    }
}

void __cdecl crt_exit(int32_t code)
{
    crt_doexit(code, 0, 0);
}

void __cdecl crt_exit_quick(int32_t code)
{
    crt_doexit(code, 1, 0);
}
