/* standalone.h -- the function seams, for the drop-in replacement build.
 *
 * src/game reaches a handful of things in the original's .text by address:
 * its statically linked MSVC CRT, its rand, its logger, and the three
 * DirectX creator thunks.  There are 47 and they are the ONLY .text
 * addresses the reconstruction uses for anything but a patch_replace --
 * every other one is a detour target, which a standalone build has nothing
 * to detour.
 *
 * Each is redefined here to the address of a real function, so the call
 * sites -- which cast to their own function-pointer type and call through --
 * do not change.  The injected build includes none of this.
 *
 * TWO ARE NOT LIBC AND MUST NOT BE.  The game's rand is MSVC's LCG, and the
 * sequence is observable in play: reproducing it is what keeps a standalone
 * mission behaving like the original's, so am2_sa_rand implements the
 * multiplier and addend rather than calling the host's rand.  The logger is
 * stubbed to `ret` in this retail build, so it drops its message here too --
 * anything else would put lines on screen the original never printed.
 */
#ifndef AM2_STANDALONE_H
#define AM2_STANDALONE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <direct.h>
#include <io.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MSVC's rand, not the host's: the LCG whose constants are visible in the
 * image at 0x00464420 -- imul 0x343FD, add 0x269EC3, and the answer is bits
 * 16..30 of the seed. */
int am2_sa_rand(void);

/* The retail logger is a bare `ret`.  This drops the message for the same
 * reason. */
void am2_sa_log(const char *fmt, ...);

/* The three DirectX creators are the game's own `jmp [IAT]` thunks.  They go
 * through wrappers rather than being named here, so this header -- which
 * every translation unit sees -- does not have to pull in ddraw.h, dinput.h
 * and dsound.h. */
/* __stdcall, NOT cdecl: the game calls these through WINAPI function-pointer
 * typedefs, so a cdecl wrapper leaves the four arguments on the stack that
 * neither side pops. The frame then shifts by sixteen bytes and the CALLER's
 * next parameter read returns a stack address -- which is exactly how this
 * was found, with InitInput's hWnd reading 0x00c3fdbc and SetCooperativeLevel
 * answering E_HANDLE for a window that was demonstrably valid. */
int32_t __stdcall am2_sa_ddraw_create(void *guid, void **out, void *outer);
int32_t __stdcall am2_sa_dinput_create(void *inst, uint32_t ver, void **out,
                                       void *outer);
int32_t __stdcall am2_sa_dsound_create(void *guid, void **out, void *outer);

/* MSVC spells these with a leading underscore and mingw agrees, but the
 * find-file family also shares a STRUCT layout with the caller, so these
 * wrappers are where any divergence gets absorbed. */
intptr_t am2_sa_findfirst(const char *spec, void *data);
int      am2_sa_findnext(intptr_t handle, void *data);
int      am2_sa_findclose(intptr_t handle);

void *am2_sa_operator_new(size_t n);
void  am2_sa_operator_delete(void *p);

/* Seams that live in orig.h itself rather than in src/game, which is why the
 * first sweep for these missed them: it scanned src/game only, and orig_ftell
 * is defined beside the very macros it uses.  The gap surfaced as a fault
 * inside ReadWaveFile, three layers below where it was introduced. */
long am2_sa_ftell(void *fp);

/* 0x0040A6A0 is a one-instruction `jmp 0x0040A660` -- a linker thunk, not a
 * function -- and its target is FreeArmyObjLists, which is reconstructed. */
void am2_sa_free_army_lists(void);

/* ONE address the standalone build has no code for, and it is not a gap: the
 * AM2_PROBE_NOACTION seam exists to call the ORIGINAL action parser, so it
 * cannot mean anything in a build that carries none of the original. It LOGS
 * and returns 0 -- a stub that announces itself beats a jump into unmapped
 * memory, and beats a silent wrong answer by more.
 *
 * The other two were real functions and are gone: 0x00451990 became
 * OnEnterNameOk and 0x004185C0 became HudChatChar. Both were interior
 * addresses of merged entries, which is why remaining.py, coverage.py and
 * checkinstalled all read them as done; the standalone build is the only
 * thing in the tree that cannot call code it does not contain, and it is what
 * found them. */
int32_t am2_sa_unimplemented(void);


/* Tables MSVC placed in .text, which this build does not carry. Extracted
 * into build/standalone/tables.cpp rather than transcribed. */
extern const uint8_t am2_pickup_kind_index[29];

#ifdef __cplusplus
}
#endif




#define AM2_SA(fn) ((uintptr_t)(void *)&(fn))

#undef ADDR_CRT_ATEXIT
#undef ADDR_CRT_ATOI
#undef ADDR_CRT_BSEARCH
#undef ADDR_CRT_CHDIR
#undef ADDR_CRT_CHMOD
#undef ADDR_CRT_FFLUSH
#undef ADDR_CRT_FGETS
#undef ADDR_CRT_FINDCLOSE
#undef ADDR_CRT_FINDFIRST
#undef ADDR_CRT_FINDNEXT
#undef ADDR_CRT_FREE
#undef ADDR_CRT_GETCWD
#undef ADDR_CRT_MALLOC
#undef ADDR_CRT_MKDIR
#undef ADDR_CRT_QSORT
#undef ADDR_CRT_REALLOC
#undef ADDR_CRT_REMOVE
#undef ADDR_CRT_RMDIR
#undef ADDR_CRT_STRCHR
#undef ADDR_CRT_STRLWR
#undef ADDR_CRT_STRNCPY
#undef ADDR_CRT_STRNICMP
#undef ADDR_CRT_STRSTR
#undef ADDR_CRT_STRTOD
#undef ADDR_CRT_STRTOK
#undef ADDR_CRT_STRTOL
#undef ADDR_CRT_TIME
#undef ADDR_FCLOSE
#undef ADDR_FOPEN
#undef ADDR_FREAD
#undef ADDR_FSEEK
#undef ADDR_FWRITE
#undef ADDR_GAME_DELETE
#undef ADDR_GAME_MALLOC
#undef ADDR_GAME_OPERATOR_NEW
#undef ADDR_GAME_RAND
#undef ADDR_GAME_SPRINTF
#undef ADDR_GAME_STRICMP
#undef ADDR_LOG
#undef ADDR_REALLOC
#undef ADDR_MEMMOVE
#undef ADDR_VSPRINTF
#undef ADDR_DIRECTDRAWCREATE
#undef ADDR_DIRECTINPUTCREATE
#undef ADDR_DIRECTSOUNDCREATE

#define ADDR_CRT_ATEXIT       AM2_SA(atexit)
#define ADDR_CRT_ATOI         AM2_SA(atoi)
#define ADDR_CRT_BSEARCH      AM2_SA(bsearch)
#define ADDR_CRT_CHDIR        AM2_SA(_chdir)
#define ADDR_CRT_CHMOD        AM2_SA(_chmod)
#define ADDR_CRT_FFLUSH       AM2_SA(fflush)
#define ADDR_CRT_FGETS        AM2_SA(fgets)
#define ADDR_CRT_FINDCLOSE    AM2_SA(am2_sa_findclose)
#define ADDR_CRT_FINDFIRST    AM2_SA(am2_sa_findfirst)
#define ADDR_CRT_FINDNEXT     AM2_SA(am2_sa_findnext)
#define ADDR_CRT_FREE         AM2_SA(free)
#define ADDR_CRT_GETCWD       AM2_SA(_getcwd)
#define ADDR_CRT_MALLOC       AM2_SA(malloc)
#define ADDR_CRT_MKDIR        AM2_SA(_mkdir)
#define ADDR_CRT_QSORT        AM2_SA(qsort)
#define ADDR_CRT_REALLOC      AM2_SA(realloc)
#define ADDR_CRT_REMOVE       AM2_SA(remove)
#define ADDR_CRT_RMDIR        AM2_SA(_rmdir)
#define ADDR_CRT_STRCHR       AM2_SA(strchr)
#define ADDR_CRT_STRLWR       AM2_SA(_strlwr)
#define ADDR_CRT_STRNCPY      AM2_SA(strncpy)
#define ADDR_CRT_STRNICMP     AM2_SA(_strnicmp)
#define ADDR_CRT_STRSTR       AM2_SA(strstr)
#define ADDR_CRT_STRTOD       AM2_SA(strtod)
#define ADDR_CRT_STRTOK       AM2_SA(strtok)
#define ADDR_CRT_STRTOL       AM2_SA(strtol)
#define ADDR_CRT_TIME         AM2_SA(time)
#define ADDR_FCLOSE           AM2_SA(fclose)
#define ADDR_FOPEN            AM2_SA(fopen)
#define ADDR_FREAD            AM2_SA(fread)
#define ADDR_FSEEK            AM2_SA(fseek)
#define ADDR_FWRITE           AM2_SA(fwrite)
#define ADDR_GAME_DELETE      AM2_SA(am2_sa_operator_delete)
#define ADDR_GAME_MALLOC      AM2_SA(malloc)
#define ADDR_GAME_OPERATOR_NEW AM2_SA(am2_sa_operator_new)
#define ADDR_GAME_RAND        AM2_SA(am2_sa_rand)
#define ADDR_GAME_SPRINTF     AM2_SA(sprintf)
#define ADDR_GAME_STRICMP     AM2_SA(_stricmp)
#define ADDR_LOG              AM2_SA(am2_sa_log)
#define ADDR_REALLOC          AM2_SA(realloc)
#define ADDR_MEMMOVE          AM2_SA(memmove)
#define ADDR_VSPRINTF         AM2_SA(vsprintf)
#define ADDR_DIRECTDRAWCREATE  AM2_SA(am2_sa_ddraw_create)
#define ADDR_DIRECTINPUTCREATE AM2_SA(am2_sa_dinput_create)
#define ADDR_DIRECTSOUNDCREATE AM2_SA(am2_sa_dsound_create)

#undef ADDR_FTELL
#undef ADDR_GAME_FREE
#undef ADDR_FREE_ARMY_LISTS_ALIAS
#undef ADDR_SCRIPT_PARSE_ACTION
#define ADDR_FTELL            AM2_SA(am2_sa_ftell)
#define ADDR_GAME_FREE        AM2_SA(free)
#define ADDR_FREE_ARMY_LISTS_ALIAS AM2_SA(am2_sa_free_army_lists)
#define ADDR_SCRIPT_PARSE_ACTION AM2_SA(am2_sa_unimplemented)


/* AM2_ITEM_KIND_IS_SPECIAL reads a 29-byte table at 0x00433770, inside the
 * .text range a standalone build does not carry -- so every byte read 0xCC
 * and the predicate was false for every kind, quietly dropping kinds 1, 7,
 * 8, 9, 10 and 29 out of the special set. */
#undef AM2_ITEM_KIND_IS_SPECIAL
#define AM2_ITEM_KIND_IS_SPECIAL(kind) \
    ((uint32_t)((kind) - 1) <= 0x1Cu \
     && am2_pickup_kind_index[(kind) - 1] == 0)

#endif /* AM2_STANDALONE_H */
