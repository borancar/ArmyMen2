/* commmsg.h -- see commmsg.cpp.
 *
 * The functions themselves are declared in msgslot.h, and deliberately: that
 * header documents the comm object's fields and the whole message family
 * against them, and cutting it in half would separate a field's description
 * from the two functions that write it. What the split is about is the
 * selftest LINK -- one half is pure and has recorded vectors, the other needs
 * the running game -- and that is a property of the .cpp, not of the prose.
 */
#ifndef AM2_COMMMSG_H
#define AM2_COMMMSG_H

#include "msgslot.h"

#ifdef __cplusplus
extern "C" {
#endif

int commmsg_install(void);

#ifdef __cplusplus
}
#endif

#endif
