/* globals.h -- the original's globals by name and type.
 *
 * Game code names a global (`g_pathNodes[t].g`) and never its address. What
 * the name binds to is the build's business, and this header is the one
 * place that knows it:
 *
 *   relink (AM2_RELINK)       the typed am2_os_* extern relinkos.py declares;
 *                              relink.py resolves the symbol to its VA.
 *   standalone, native,       the typed member of am2_origstate, wherever
 *   vc6-link (AM2_STANDALONE) the link put it.
 *   injected hook DLL         the running original's own storage, which is
 *                              where orig.h says it is.
 *
 * Types live in globaltypes.h, shared with origstate.h and the relink header
 * so a global is declared with one type everywhere. */
#ifndef AM2_GLOBALS_H
#define AM2_GLOBALS_H


#if defined(AM2_RELINK)
#define g_pathNodes am2_os_path_nodes
#elif defined(AM2_STANDALONE)
#define g_pathNodes am2_origstate.path_nodes
#else
#include "globaltypes.h"
#define g_pathNodes (*(AM2_PathNode (*)[0x10000])(uintptr_t)ADDR_PATH_NODES)
#endif

#endif /* AM2_GLOBALS_H */
