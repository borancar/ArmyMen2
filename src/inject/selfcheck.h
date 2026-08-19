/* Differential self-check, in the game's own process.
 *
 * Runs BEFORE any patch is installed, while the original functions are still
 * intact, and calls each reconstruction and the original it replaces with the
 * same arguments. Enabled with AM2_SELFCHECK=1.
 */
#ifndef AM2_SELFCHECK_H
#define AM2_SELFCHECK_H

#ifdef __cplusplus
extern "C" {
#endif

int selfcheck_run(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_SELFCHECK_H */
