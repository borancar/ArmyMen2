/* callconv.h -- the MSVC calling-convention keywords, for the native build.
 *
 * mingw's compiler knows __cdecl, __stdcall, __fastcall and __thiscall as
 * keywords; a Linux-targeted GCC knows the attributes and not the words.
 * The reconstruction uses the words everywhere -- in the flat half, which
 * includes no Windows header at all -- so the native build force-includes
 * this file into every translation unit (-include in the Makefile), and
 * windows.h includes it too. */
#ifndef AM2_PLATFORM_CALLCONV_H
#define AM2_PLATFORM_CALLCONV_H

#ifndef __cdecl
#define __cdecl    __attribute__((cdecl))
#endif
#ifndef __stdcall
#define __stdcall  __attribute__((stdcall))
#endif
#ifndef __fastcall
#define __fastcall __attribute__((fastcall))
#endif
#ifndef __thiscall
#define __thiscall __attribute__((thiscall))
#endif

#endif /* AM2_PLATFORM_CALLCONV_H */
