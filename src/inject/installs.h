/* The game modules' install entry points, as the C harness sees them.
 *
 * dllmain.c calls one per module and needs nothing else from the game
 * headers -- which are C++ now (thiscall members, classes, `extern "C++"`
 * openers) and no longer compile as C. Each is declared `extern "C"` in its
 * own header too; a mismatch there is a link error, not a silent one. */
#ifndef AM2_INSTALLS_H
#define AM2_INSTALLS_H

#include <stdint.h>

void air_install(void);                   /* air.h */
int32_t anim_install(void);                  /* anim.h */
int32_t army_install(void);                  /* army.h */
int32_t armymsg_install(void);               /* armymsg.h */
int32_t audio_install(void);                 /* win32/audio.h */
int32_t blit_install(void);                  /* blit.h */
int32_t cheat_install(void);                 /* cheat.h */
int32_t commmsg_install(void);               /* commmsg.h */
int32_t definfo_install(void);               /* definfo.h */
int32_t defparse_install(void);              /* defparse.h */
int32_t device_install(void);                /* win32/device.h */
int32_t dirty_install(void);                 /* dirty.h */
int32_t dist_install(void);                  /* dist.h */
int32_t dplay_install(void);                 /* win32/dplay.h */
int32_t event_install(void);                 /* event.h */
int32_t font_install(void);                  /* win32/font.h */
int32_t frame_install(void);                 /* win32/frame.h */
int32_t gamedir_install(void);               /* gamedir.h */
void gameproc_install(void);              /* gameproc.h */
void item_install(void);                  /* item.h */
void map_install(void);                   /* map.h */
int32_t mapdraw_install(void);               /* win32/mapdraw.h */
int32_t maprow_install(void);                /* maprow.h */
int32_t misc_install(void);                  /* misc.h */
int32_t movie_install(void);                 /* win32/movie.h */
int32_t msgslot_install(void);               /* msgslot.h */
int32_t objflag_install(void);               /* objflag.h */
int32_t objscript_install(void);             /* objscript.h */
int32_t objtable_install(void);              /* objtable.h */
int32_t objtype_install(void);               /* objtype.h */
int32_t packkey_install(void);               /* packkey.h */
void pad_install(void);                   /* pad.h */
int32_t palette_install(void);               /* win32/palette.h */
int32_t place_install(void);                 /* place.h */
int32_t rect_install(void);                  /* rect.h */
int32_t region_install(void);                /* region.h */
int32_t report_install(void);                /* win32/report.h */
int32_t savetag_install(void);               /* savetag.h */
int32_t script_install(void);                /* script.h */
int32_t sprite_install(void);                /* win32/sprite.h */
int32_t startgame_install(void);             /* win32/startgame.h */
int32_t surface_install(void);               /* win32/surface.h */
int32_t text_install(void);                  /* text.h */
int32_t trig_install(void);                  /* trig.h */
int32_t wavefile_install(void);              /* win32/wavefile.h */
int32_t widget_install(void);                /* win32/widget.h */
int32_t winmain_install(void);               /* win32/winmain.h */

#endif
