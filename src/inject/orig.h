/* Typed pointers to functions still living inside the original ArmyMen2.exe.
 *
 * Relocations are stripped from the executable, so it can only ever map at its
 * preferred base of 0x400000 and these absolute addresses are stable.
 *
 * IMPORTANT: ArmyMen2.exe statically links the MSVC 6 CRT. Its FILE handles,
 * heap and locale state are entirely separate from the msvcrt.dll that our
 * injected DLL links against. Never pass a game FILE* to our own fread/fclose,
 * or a game pointer to our own free(). Route through the game's own CRT via the
 * declarations below until the owning subsystem is itself reconstructed.
 */

#ifndef AM2_ORIG_H
#define AM2_ORIG_H

#include <stdint.h>
#include <stddef.h>

/* ---- addresses ------------------------------------------------------- */

#define AM2_IMAGE_BASE      0x00400000u

/* Game code */
#define ADDR_CHECK_SAVE_TAG 0x004235D0u  /* BOOL(FILE*,uint32_t,const char*,int32_t) */
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
#define ADDR_RECT_SET       0x0042E1C0u  /* void(int32_t*,int32_t,int32_t,int32_t,int32_t) */
#define ADDR_APPROX_DIST    0x0042DDE0u  /* int32_t(const AM2_Point*, const AM2_Point*) */
#define ADDR_FIND_SLOT      0x004277A0u  /* int32_t(uint32_t uid, int32_t *insert_at) */
#define ADDR_LOOKUP_BY_UID  0x00427820u  /* void *(uint32_t uid) */

/* The global object registry: an array of 12-byte {uid, obj, serial} records
 * kept sorted by uid. See src/game/objtable.c. */
#define ADDR_OBJ_TABLE      0x00514F0Cu  /* AM2_ObjEntry * */
#define ADDR_OBJ_COUNT      0x00514F04u  /* int32_t */
#define ADDR_OBJ_CAPACITY   0x00514F00u  /* int32_t */

/* Statically linked MSVC 6 CRT */
#define ADDR_FREAD          0x004645C1u  /* size_t(void*,size_t,size_t,FILE*) */

/* ---- typed accessors -------------------------------------------------- */

/* The game's FILE is an MSVC 6 _iobuf. We never dereference it, so keep it
 * opaque rather than pretending it matches ours. */
typedef struct am2_FILE am2_FILE;

typedef size_t (__cdecl *am2_fread_fn)(void *buf, size_t size, size_t count,
                                       am2_FILE *fp);
typedef void   (__cdecl *am2_log_fn)(const char *fmt, ...);

#define orig_fread (*(am2_fread_fn)ADDR_FREAD)
#define orig_log   (*(am2_log_fn)ADDR_LOG)

#endif /* AM2_ORIG_H */
