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
/* Original: 0x0042A7C0. The kind of whatever a uid names, or 0 -- and a
 * missing slot, a null object and a kind of zero are indistinguishable in
 * the answer. */
int32_t __cdecl UidObjKind(uint32_t uid);

#endif

/* 0x0044C250, one caller, and it names itself -- "Trooper Fire Send,
 * trooper: %d,  face:%d, pos (%d,%d,%d), loctarg %x, globTarg %x, weap %d,
 * seq:%d", which is where every field name below comes from.
 *
 * A 28-byte army message of kind 0x17: the trooper's uid and its local target
 * on the wire, where it is, and the byte at +0x529. Two of the trooper's own
 * fields are zeroed as it goes and the flow record's sequence number is
 * recorded on it -- READ, not bumped, so something else owns that counter.
 *
 * Does nothing at all without a DirectPlay session. */
void __cdecl TrooperFireSend(void *trooper, void *target);

int commmsg_install(void);

#ifdef __cplusplus
}
#endif

#endif
