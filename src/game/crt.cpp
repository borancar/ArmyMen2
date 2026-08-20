/* See crt.h. */
#include <stdlib.h>
#include <unistd.h>

#include "crt.h"
#include "../inject/orig.h"

/* Host libc until told otherwise, so a build that forgets to call
 * am2_crt_use_game() gets a heap mismatch on the first free rather than a call
 * through a null pointer -- both are bugs, but the first is the one that shows
 * up in the A/B instead of at startup. */
void *(__cdecl *am2_malloc)(size_t) = malloc;
void *(__cdecl *am2_realloc)(void *, size_t) = realloc;
void  (__cdecl *am2_free)(void *) = free;

static void __cdecl DropLog(const char *, ...)
{
}

void (__cdecl *am2_log)(const char *, ...) = DropLog;

static int32_t __cdecl HostChdir(const char *path)
{
    return (int32_t)chdir(path);
}

int32_t (__cdecl *am2_chdir)(const char *) = HostChdir;

void am2_crt_use_game(void)
{
    am2_malloc  = (void *(__cdecl *)(size_t))(uintptr_t)ADDR_CRT_MALLOC;
    am2_realloc = (void *(__cdecl *)(void *, size_t))(uintptr_t)ADDR_CRT_REALLOC;
    am2_free    = (void (__cdecl *)(void *))(uintptr_t)ADDR_CRT_FREE;
    am2_log     = (void (__cdecl *)(const char *, ...))(uintptr_t)ADDR_LOG;
    am2_chdir   = (int32_t (__cdecl *)(const char *))(uintptr_t)ADDR_CRT_CHDIR;
}

void am2_crt_use_host(void)
{
    am2_malloc  = malloc;
    am2_realloc = realloc;
    am2_free    = free;
    am2_log     = DropLog;
    am2_chdir   = HostChdir;
}
