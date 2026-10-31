/* smackw32.cpp -- RAD's Smacker video player, as the game imports it.
 *
 * No movie can be opened: SmackOpen answers NULL, which PlayMovie treats as
 * a missing file, so the intro and the cutscenes are skipped exactly as the
 * original skips a film it cannot find. Every other entry point is inert.
 * They are reached only through the game's own import slots (bound from
 * kernel32.cpp's export table), and all but SmackSoundUseDirectSound only
 * on a handle SmackOpen returned -- so, here, never. The signatures are the
 * ones the game declares (src/game/win32/movie.cpp).
 */
#include "platform.h"

void *WINAPI SmackOpen(const char *name, uint32_t flags, uint32_t extra)
{
    (void)flags; (void)extra;
    am2_plat_log("SmackOpen(%s): movies are not available in this build", name);
    return NULL;
}

void WINAPI SmackClose(void *smack) { (void)smack; }
void WINAPI SmackDoFrame(void *smack) { (void)smack; }
void WINAPI SmackNextFrame(void *smack) { (void)smack; }
uint32_t WINAPI SmackWait(void *smack) { (void)smack; return 0; }
void WINAPI SmackSoundUseDirectSound(void *dsound) { (void)dsound; }
uint32_t WINAPI SmackDDSurfaceType(void *surface) { (void)surface; return 0; }

void WINAPI SmackToBuffer(void *smack, uint32_t left, uint32_t top,
                          uint32_t pitch, uint32_t destHeight, void *dest,
                          uint32_t flags)
{
    (void)smack; (void)left; (void)top; (void)pitch; (void)destHeight;
    (void)dest; (void)flags;
}

void WINAPI SmackVolumePan(void *smack, uint32_t trackFlags, uint32_t volume,
                           uint32_t pan)
{
    (void)smack; (void)trackFlags; (void)volume; (void)pan;
}
