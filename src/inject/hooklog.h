/* Harness diagnostics.
 *
 * Deliberately separate from the game's own logger: hooklog() is our voice,
 * game_log() (see gamelog.c) is the un-stubbed voice of the original code.
 * Keeping them apart makes it obvious which lines came from reconstruction
 * scaffolding and which are genuine 1999 debug output.
 */

#ifndef AM2_HOOKLOG_H
#define AM2_HOOKLOG_H

void hooklog_open(void);
void hooklog_close(void);
void hooklog(const char *fmt, ...);

/* Shared sink, so harness and game lines interleave in true order. */
void hooklog_raw(const char *line);

#endif /* AM2_HOOKLOG_H */
