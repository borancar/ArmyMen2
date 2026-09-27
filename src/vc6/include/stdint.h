#ifndef _VC6_STDINT_H
#define _VC6_STDINT_H
/* Minimal C99 <stdint.h> for MSVC 6 (i386) plus the two MSVC6-compat shims the
 * reconstruction needs when compiled for byte-match verification.  Included
 * first by every src/game TU (each module .h opens with <stdint.h>), so it is
 * in force before orig.h and the bodies.  All of this is gated to non-GCC so
 * the real Linux build never sees it. */
#ifndef __GNUC__
/* Map the GCC attributes the reconstruction uses to their MSVC 6 equivalent
 * rather than dropping them wholesale -- dropping `stdcall` miscompiled every
 * __stdcall reconstruction to __cdecl (wrong `ret N`, wrong ABI for the byte
 * compare).  `__attribute__((X))` -> `AM2_ATTR (X)` -> `AM2_ATTR(X)`, which
 * token-pastes to `AM2_ATTR_X`: a calling convention becomes the MSVC keyword,
 * everything else (weak, section(...)) becomes nothing.  `thiscall` cannot be
 * spelled in VC6 (the keyword post-dates it; thiscall is implicit on members
 * only), so a free-function thiscall reconstruction compiles as cdecl and
 * simply will not match -- they are all in src/game/win32 regardless. */
#define __attribute__(x) AM2_ATTR x
#define AM2_ATTR(a) AM2_ATTR_ ## a
#define AM2_ATTR_stdcall  __stdcall
#define AM2_ATTR_cdecl    __cdecl
#define AM2_ATTR_thiscall
#define AM2_ATTR_weak
#define AM2_ATTR_section(x)
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
