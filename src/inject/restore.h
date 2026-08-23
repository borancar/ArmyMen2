/* Undoing edits made to the shipping executable. See restore.c. */

#ifndef AM2_RESTORE_H
#define AM2_RESTORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Applies whichever restores are asked for by environment variable. Currently
 * one: AM2_MULTIPLAYER=1 puts the MULTIPLAYER button back on the title screen.
 *
 * Off by default. Anything enabled here makes the process differ from the
 * original binary, which tools/ab.sh will correctly report as a difference. */
void restore_install(void);

/* Non-zero when AM2_MULTIPLAYER=1, i.e. when the title screen is meant to
 * carry its MULTI-PLAYER entry.
 *
 * The byte patch above puts the button back in the ORIGINAL title-screen
 * builder, and that is still what the A/B's `orig` side runs. Our own
 * OpenTitleScreen cannot honour it, because the byte lives in the very
 * function it replaces -- so it asks this instead. Both are driven by the
 * same variable, which is what keeps the two sides of a run agreeing. */
int restore_multiplayer(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_RESTORE_H */
