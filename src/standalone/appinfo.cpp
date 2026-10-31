/* appinfo.cpp -- what Army Men II tells win32_layer about itself.
 *
 * The native port and the hybrid both link it, and must give the same
 * answers: GetModuleFileNameA returns module_path, which the CRT's startup
 * copies onto its heap, so a path that differed between the two would shift
 * every heap address after it. It is the install path the GOG release runs
 * from, the same on every host. */
#include "../../third_party/win32_layer/src/platform.h"

extern "C" const W32L_AppInfo w32l_app_info = {
    "Army Men II",
    "0.1",
    "org.armymen2.port",
    "C:\\GOG Games\\Army Men II\\ArmyMen2.exe",
};
