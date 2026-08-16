/* Interception of the game's DirectInput usage.
 *
 * The game imports exactly one DirectInput entry point, DirectInputCreateA, so
 * a single IAT patch is enough to get hold of the root object. From there we
 * patch vtable slots rather than building wrapper objects: only three methods
 * matter (CreateDevice, GetDeviceState, GetDeviceData), against eighteen that
 * would otherwise have to be forwarded by hand.
 *
 * Each hook calls the original first and then overlays injected state, so real
 * input keeps working and injection composes with it rather than replacing it.
 */

#ifndef AM2_DINPUT_HOOK_H
#define AM2_DINPUT_HOOK_H

#ifdef __cplusplus
extern "C" {
#endif


/* Patch the DirectInputCreateA import thunk. Returns 0 on success. */
int dinput_hook_install(void);


#ifdef __cplusplus
}
#endif

#endif /* AM2_DINPUT_HOOK_H */
