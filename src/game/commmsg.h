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

/* 0x0044BC10, one caller: TellOneSlot. Append one trooper's state to a batch
 * message -- a DELTA record, emitted only when position, facing or pose has
 * changed enough, with one presence bit each in the top three bits of its
 * first dword. Five bytes at least and NINE at most, which is what makes the
 * caller's ten-byte reservation a bound rather than a guess. */
void __cdecl AppendTroopState(void *msg, void *obj);
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
 * Does nothing at all without a DirectPlay session.
 *
 * ITS SECOND ARGUMENT IS THE WEAPON, not a target, and reading the one caller
 * is what says so: TrooperFire passes the object it just fired. The field its
 * uid lands in is the one the message logs as `globTarg`, so the wire format's
 * own word and the caller disagree; the parameter is named for what is passed
 * to it, which is the half that is evidenced. */
void __cdecl TrooperFireSend(void *trooper, void *weapon);

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

/* 0x0040FD40, one caller. Stamp the two-dword header -- a kind and the
 * record's whole size -- of the twenty comm messages that live in .bss. */
void __cdecl CommInitDefaults(void);

/* 0x00411F10, one caller. The chat record sent to ONE player rather than
 * broadcast: truncate at 254, copy to the record's +9, take the sender's ink
 * from ADDR_ARMY_INK with white as the fallback, address it to the slot's
 * DirectPlay id. */
void __cdecl SendChatTo(char *text, int32_t slot);

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

/* 0x004014C0. The flow-control receive path, called from the receive thread
 * for every message on a reliable channel. Dispatches AM2_FLOW_DATA,
 * AM2_FLOW_NACK and AM2_FLOW_PULSE_ACK. Answers 1 when it has dealt with the
 * message and 0 when the caller should queue it. */
int32_t __cdecl FlowRecvMessage(void *nodev);

/* 0x0045DAA0. Append one vehicle's DELTA update to a message buffer whose
 * first word is its length. The sibling of AppendTroopState, sharing its
 * design and not its code. */
void __cdecl VehicleUpdateAppend(void *msg, void *obj);

/* 0x0045E590. The vehicle message dispatcher: eleven table slots over seven
 * handlers, with kinds 0x20..0x23 sharing the unknown-message arm. */
void __cdecl VehicleMsgRecv(void *msg, int32_t army);

/* 0x0045EB10. Kind 0x1B, a BATCH: walk the records from +8 to the message's
 * own length word, handing each to the applier. */
void __cdecl RecvVehicle1B(void *msg, int32_t army);

/* 0x0045E810. Kind 0x1E, which resolves two objects and then does nothing --
 * its only effect was a debug print the retail build stubbed to `ret`. */
void __cdecl RecvVehicle1E(void *msg);

/* 0x0045EA30. Kind 0x24, the receive half of SendVehicleEnter. */
void __cdecl RecvVehicle24(void *msg);

/* 0x0045DF10. Decode one vehicle delta record and RETURN the cursor past it,
 * which is what lets RecvVehicle1B find the next one. */
uint8_t *__cdecl VehicleUpdateApply(void *rec, int32_t army);

#endif
