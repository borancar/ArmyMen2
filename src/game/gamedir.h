/* gamedir.cpp -- changing into one of the game's data directories.
 *
 * 0x00422DE0, 82 direct callers: everything that loads a map, a script, a
 * bitmap or a sound goes through it first, because the game chdirs into the
 * subdirectory and then opens files by bare name. That is why the script sweep
 * has to be given absolute paths -- see AM2_SCRIPTS in CLAUDE.md.
 *
 * It is the only reconstructed function that changes the process's working
 * directory, and it does it through the game's own CRT rather than the
 * harness's libc (src/game/crt.h) -- one process, one current directory, and
 * the CRT keeps drive-relative bookkeeping of its own behind SetCurrentDirectoryA.
 */
#ifndef AM2_GAMEDIR_H
#define AM2_GAMEDIR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if the directory was entered, 0 if neither path worked.
 *
 * Two bases are tried, and the second is THE CD: the fallback path and the
 * flag that gates it are ADDR_CD_PATH and ADDR_CD_PRESENT, so a data directory
 * missing from the install is looked for on the disc. The concatenation
 * differs between them -- the install directory gets a separator appended, the
 * CD path already carries one -- and that asymmetry is not cosmetic. */
int32_t __cdecl SetGameDir(const char *subdir);

/* 1 if the file can be opened for reading, 0 if it cannot. Nothing is read
 * and nothing distinguishes "absent" from "unreadable". */
int32_t __cdecl FileExists(const char *path);

int gamedir_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_GAMEDIR_H */
