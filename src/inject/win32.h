#ifndef AM2_WIN32_H
#define AM2_WIN32_H

/* The one place that pulls in the Windows and DirectDraw headers.
 *
 * This is Windows code calling Windows APIs, so it uses the real declarations
 * rather than restating them. CINTERFACE selects the C view of the COM
 * interfaces and COBJMACROS provides the IDirectDrawSurface_* call wrappers, so
 * nothing about DDSURFACEDESC or any vtable layout is written out by hand.
 *
 * The one wrinkle is that winuser.h defines DrawText as a macro expanding to
 * DrawTextA, which collides with the game's own DrawText that we reconstructed.
 * That is handled here, once, instead of being dodged with forward declarations
 * in every header that needs a Windows type. Any future reconstruction that
 * reuses a Win32 name should be undone here too.
 */

#define CINTERFACE
#define COBJMACROS
/* The game asks for DirectInput 5. The version matters: it selects which
 * interface the headers declare, and IDirectInputDevice's vtable is what the
 * reconstruction indexes into. */
#define DIRECTINPUT_VERSION 0x0500
#include <windows.h>
#include <ddraw.h>
#include <dinput.h>

#undef DrawText

#endif /* AM2_WIN32_H */
