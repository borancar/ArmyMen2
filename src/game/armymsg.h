/* armymsg.cpp -- the game's outgoing message transport.
 *
 * A name of ours, not the image's. These functions sit in the band between
 * audio.cpp's last function and event.cpp's first, so the original's own
 * module boundary is not visible here the way it is for script.cpp and
 * objscript.cpp; what IS visible is that they call themselves ArmyMessageSend
 * and ArmyMessageFlush in their own log strings, and that they share one
 * packet buffer. Named for the family rather than for a file.
 *
 * The shape: callers hand ArmyMessageSend a message that opens with an
 * eight-byte header, it is appended to a packet at ADDR_ARMY_PACKET, and the
 * packet is flushed when it fills. Twenty callers, so this is how the whole
 * game talks to its peers.
 */
#ifndef AM2_ARMYMSG_H
#define AM2_ARMYMSG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
/* 0x0045E3C0, two callers, both a unit leaving a vehicle. Twelve bytes: the
 * header's uid is the VEHICLE and the dword after it is the occupant. */
void __cdecl SendVehicleExit(void *vehicle, void *occupant);

#endif

/* The header every message on this transport begins with. ArmyMessageSend
 * reads all three: the length decides how much is copied, and the uid is fed
 * to UidArmy for the debug line. */
typedef struct {
    uint16_t len;    /* +0x00, bytes to copy, including this header */
    uint16_t kind;   /* +0x02 */
    uint32_t uid;    /* +0x04 */
} AM2_ArmyMsgHdr;

/* 0x004105F0. Append one message to the outgoing packet, flushing first if it
 * will not fit and again afterwards if the packet has passed its threshold.
 * Does nothing at all unless DirectPlay is up, the session is joined and there
 * are at least two players. */
void __cdecl ArmyMessageSend(const void *msg);

/* 0x00410820. Tell the other players the game has paused or resumed, with the
 * reason mask. Does nothing unless the session is joined. Eight callers. */
void __cdecl SendGamePause(int32_t pause, int32_t mask);

/* 0x0044C370, ten callers. Tell the other players a trooper's weapon changed.
 * Silent unless a DirectPlay session exists. */
void __cdecl SendTrooperSetWeapon(const void *trooper, uint32_t weaponUid,
                                  int32_t weapon);

/* 0x0042AA10, two callers. Tell the other players an object was destroyed.
 * Silent outside a multiplayer session. */
void __cdecl SendObjDestroyed(const void *obj);

/* 0x0042A9A0. Tell the other players an ITEM is gone. Silent outside a
 * multiplayer session, and only for object types 1..4. */
void __cdecl ItemGoneMessageSend(const void *obj);

/* 0x0042ACE0. Routes an army message to its receiver; 1 if it took it. Six
 * codes, and the middle of the range is NOT among them -- 0x13..0x22 get 0.
 * `army` is pushed by the caller and never read. */
int32_t __cdecl ArmyMsgFilter(void *msg, int32_t army);

/* 0x0042AA50. Tell the other players an item was deployed. Sixteen bytes; the
 * two trailing bytes are the item's facing and the caller's argument, in that
 * order. Guarded on the SESSION pointer, not COMM_OFF_DPLAY. */
void __cdecl SendItemDeploy(const void *item, int32_t arg);

/* 0x0042A880. The only sender of the damage message. The position it carries
 * is the VICTIM's own -- every caller passes that -- and the sixth argument
 * is never read. */
void __cdecl DamageBroadcast(void *obj, uint32_t attacker, int32_t amount,
                             int32_t kind, const void *where, int32_t unused);

/* 0x0044C0F0, five callers. A 28-byte kind-0x18 message naming two objects.
 * Structural name; see armymsg.cpp, and note the hole at +0x10. */
void __cdecl SendPairMessage(const void *a, const void *b, int32_t byteArg,
                             int32_t arg);

int armymsg_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_ARMYMSG_H */
