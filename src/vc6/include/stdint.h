#ifndef _VC6_STDINT_H
#define _VC6_STDINT_H
/* Minimal C99 <stdint.h> for MSVC 6 (i386) plus the two MSVC6-compat shims the
 * reconstruction needs when compiled for byte-match verification.  Included
 * first by every src/game TU (each module .h opens with <stdint.h>), so it is
 * in force before orig.h and the bodies.  All of this is gated to non-GCC so
 * the real Linux build never sees it. */
#ifndef __GNUC__
#define __attribute__(x)
/* MSVC6 leaks a for-init declaration into the enclosing scope (pre-standard
 * for-scope), so two `for (int i ...)` in one block collide.  This gives each
 * loop its own scope with no codegen change (verified byte-identical at /O2).
 * The `{}` (not `;`) keeps a trailing `else` bound to the user's own if. */
#define for if (0) {} else for
#define __builtin_memcpy memcpy   /* type-pun helper; <string.h> declares it */
#endif
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef __int64            int64_t;
typedef unsigned __int64   uint64_t;
typedef int                intptr_t;
typedef unsigned int       uintptr_t;
#endif
