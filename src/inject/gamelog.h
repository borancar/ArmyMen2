#ifndef AM2_GAMELOG_H
#define AM2_GAMELOG_H

/* Replace the stubbed logger at 0x0045CAA0 with a working one.
 * No-op unless AM2_GAMELOG=1. Returns 0 on success. */
int gamelog_install(void);

#endif /* AM2_GAMELOG_H */
