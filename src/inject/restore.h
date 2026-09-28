/* Undoing edits made to the shipping executable. See restore.c. */

#ifndef AM2_RESTORE_H
#define AM2_RESTORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Undoes the GOG build's MULTI-PLAYER patch, ALWAYS (Boran, 2026-09-28): the
 * title screen has its MULTI-PLAYER entry in every build. The reconstruction's
 * OpenTitleScreen always builds the button, so the original side of an A/B
 * must have the byte put back too, or the two runs are not the same game. */
void restore_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_RESTORE_H */
