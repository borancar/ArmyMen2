/* direct.h -- the MSVC directory calls, plus the string functions MSVC
 * spells with a leading underscore, all of which the CRT seams in
 * ArmyMen2's src/inject/standalone.h name. Every path-taking one translates the
 * game's Windows path -- backslashes, a drive letter, any case -- into the
 * file that actually exists; see src/crt.cpp. */
#ifndef W32L_PLATFORM_DIRECT_H
#define W32L_PLATFORM_DIRECT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t _chdir(const char *path);
char   *_getcwd(char *out, int32_t cap);
int32_t _mkdir(const char *path);
int32_t _rmdir(const char *path);
int32_t _stricmp(const char *a, const char *b);
int32_t _strnicmp(const char *a, const char *b, size_t n);
char   *_strlwr(char *s);
int32_t _snprintf(char *out, size_t cap, const char *fmt, ...);
int32_t _vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);

/* The path-translating forms of the libc calls the game reaches through
 * its CRT seams. fopen is the one that matters: every data file the game
 * opens comes through it with a Windows path. */
FILE   *w32l_fopen(const char *path, const char *mode);
int32_t w32l_remove(const char *path);

/* Translate a Windows path to the native file it names, resolving case
 * component by component where the exact name does not exist. Returns
 * `out`, which must hold at least W32L_NATIVE_PATH_MAX bytes. */
#define W32L_NATIVE_PATH_MAX 1024
char   *w32l_native_path(const char *win, char *out);

#ifdef __cplusplus
}
#endif

#endif /* W32L_PLATFORM_DIRECT_H */
