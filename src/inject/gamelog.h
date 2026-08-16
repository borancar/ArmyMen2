#ifndef AM2_GAMELOG_H
#define AM2_GAMELOG_H

#ifdef __cplusplus
extern "C" {
#endif


/* Replace the stubbed logger at 0x0045CAA0 with a working one.
 * No-op unless AM2_GAMELOG=1. Returns 0 on success. */
int gamelog_install(void);


#ifdef __cplusplus
}
#endif

#endif /* AM2_GAMELOG_H */
