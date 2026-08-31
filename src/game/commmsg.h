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

/* Original: 0x0040F330, thiscall. The slot holding this DirectPlay id, or -1
 * -- and it returns the slot's own INDEX FIELD rather than the loop counter.
 * The two agree in every state driven so far; the original reads the stored
 * one. */
int32_t __attribute__((thiscall)) CommFindPlayer(void *comm, int32_t dpid);

/* Original: 0x0040F920, thiscall. Is the army owning this uid still playing?
 * Army 4 is neutral and answers yes without touching the object. */
int32_t __attribute__((thiscall)) ArmyInPlay(void *comm, uint32_t uid);

/* 0x0044CC90 and 0x0044C960, two arms of the trooper dispatcher: kind 0x16 is
 * a batch of variable-length sub-records, kind 0x18 a (trooper, weapon) pair
 * whose sender armymsg.cpp already has. */
void __cdecl RecvTroopBatch(void *msg, int32_t army);
void __cdecl RecvTroopPair(void *msg);

/* 0x0044C3E0, kind 0x22 -- the twin of armymsg.cpp's SendTrooperSetWeapon, so
 * that message is ours at both ends. */
void __cdecl RecvTrooperSetWeapon(void *msg);

/* 0x0044C590, one caller. The trooper half of the army-message dispatcher:
 * thirteen arms over kinds 0x16..0x22, seven of which are the unknown log. */
void __cdecl TroopMessageRecv(void *msg, int32_t army);

/* 0x0045E590, one caller. The vehicle half of the army-message dispatcher:
 * eleven arms over kinds 0x1B..0x25, four of which are the unknown log. */
void __cdecl VehicleMsgRecv(void *msg, int32_t army);

/* 0x0045EAA0 and 0x0045ADD0, the receive side of a unit leaving a vehicle --
 * the twin of armymsg.cpp's SendVehicleExit, so message kind 0x25 is now ours
 * at both ends. */
void __cdecl RecvVehicleExit(void *msg);
void __cdecl VehicleTakeOutOccupant(uint32_t uid, void *vehicle);

/* 0x00401210. Drain one message list into another, head first. Here rather
 * than in gameproc.cpp because of SELFTEST_SRC; see commmsg.cpp. */
void __cdecl DrainMsgList(void *list);

/* 0x0044C550, one caller. Run one thing on every occupied comm slot that
 * CommMustBroadcast accepts -- so nothing at all in single player. */
void __cdecl TellEachSlot(void);

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

/* 0x00431C30 and 0x00430120. MenuMessage logs a line and shows it; its third
 * argument picks WHICH of the panel's two indicators blinks, and is not a
 * boolean. Announce is the pair -- log it locally, broadcast it -- and both
 * ends stamp colour 4, the system colour. */
void __cdecl MenuMessage(const char *text, int32_t colour, int32_t indicator);
void __cdecl Announce(const char *text);

/* 0x00401240, five callers. Find the node with a given key and set or clear
 * bits in its flags, under the list's mutex. Structural name; see
 * commmsg.cpp. */
void *__cdecl MsgListSetFlag(void *list, int32_t key, int32_t set,
                             uint32_t bits);

/* 0x00402F50, one caller -- the frame chain's post-work. Drain the delayed
 * send queue: every node whose MSGNODE_OFF_KEY deadline GetTickCount has
 * reached goes out through CommSend and its buffer returns to the pool. */
void __cdecl FlushDelayedSends(void);

int commmsg_install(void);

#ifdef __cplusplus
}
#endif

/* Original: 0x00402690. Drain the receive queue, dispatching each message and
 * returning its node to the pool. Called once a frame from FramePre. */
void __cdecl CommDrainMsgs(void);

/* 0x0044BEA0, one caller. Parse one variable-length trooper record from a
 * batch and answer the pointer past it. */
const void *__cdecl TroopSubParse(const void *rec, int32_t army);

#endif
