/* Undoing edits made to the shipping executable. See restore.c. */

#ifndef AM2_RESTORE_H
#define AM2_RESTORE_H

/* Applies whichever restores are asked for by environment variable. Currently
 * one: AM2_MULTIPLAYER=1 puts the MULTIPLAYER button back on the title screen.
 *
 * Off by default. Anything enabled here makes the process differ from the
 * original binary, which tools/ab.sh will correctly report as a difference. */
void restore_install(void);

#endif /* AM2_RESTORE_H */
