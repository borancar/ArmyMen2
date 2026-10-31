#ifndef AM2_SMACKW32_H
#define AM2_SMACKW32_H

#include <stdint.h>

/* smackw32.dll's entry points as the native port provides them: RAD's API
 * over libsmacker (third_party/libsmacker), in smackw32.cpp. The signatures
 * are the ones the game declares and calls (movie.cpp). Native build only --
 * every other build reaches the real smackw32.dll, or none, through the
 * original's import slots. */
#if defined(__cplusplus) && defined(AM2_NATIVE)
void *__stdcall SmackOpen(const char *name, uint32_t flags, uint32_t extra);
void __stdcall SmackClose(void *smack);
void __stdcall SmackDoFrame(void *smack);
void __stdcall SmackNextFrame(void *smack);
uint32_t __stdcall SmackWait(void *smack);
void __stdcall SmackSoundUseDirectSound(void *dsound);
uint32_t __stdcall SmackDDSurfaceType(void *surface);
void __stdcall SmackToBuffer(void *smack, uint32_t left, uint32_t top,
                             uint32_t pitch, uint32_t destHeight, void *dest,
                             uint32_t flags);
void __stdcall SmackVolumePan(void *smack, uint32_t trackFlags, uint32_t volume,
                              uint32_t pan);
#endif

#endif
