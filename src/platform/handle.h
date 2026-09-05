/* handle.h -- the one handle record kernel32.cpp and kernel32crt.cpp share.
 *
 * A HANDLE the game holds is a pointer to one of these. kernel32.cpp makes
 * the threads, events and mutexes; kernel32crt.cpp makes the files and the
 * find-file walks the original's CRT asks for. CloseHandle and the waits
 * are in kernel32.cpp and dispatch on `kind`, so a new kind is added here
 * and released there.
 */
#ifndef AM2_PLATFORM_HANDLE_H
#define AM2_PLATFORM_HANDLE_H

#include "platform.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { AM2_H_THREAD = 1, AM2_H_EVENT, AM2_H_MUTEX, AM2_H_FILE, AM2_H_FIND };

typedef struct AM2_Handle {
    int32_t   kind;
    pthread_t thread;
    int32_t   done;
    DWORD     exitCode;
    int32_t   manualReset;
    int32_t   signaled;
    LPTHREAD_START_ROUTINE start;
    LPVOID    param;
    int32_t   refs;
    int32_t   fd;        /* AM2_H_FILE: the descriptor */
    int32_t   keepFd;    /* AM2_H_FILE: a standard stream, never closed */
    intptr_t  find;      /* AM2_H_FIND: crt.cpp's _findfirst handle */
} AM2_Handle;

/* A fresh record of `kind` with one reference, or NULL. */
AM2_Handle *am2_handle_new(int32_t kind);
/* Drop one reference; the last one closes what the record owns. */
void am2_handle_release(AM2_Handle *h);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PLATFORM_HANDLE_H */
