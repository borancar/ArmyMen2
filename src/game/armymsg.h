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
#include "dist.h"        /* AM2_Point */
#include "rect.h"        /* AM2_Rect */

#ifdef __cplusplus
extern "C" {
/* 0x0045E3C0, two callers, both a unit leaving a vehicle. Twelve bytes: the
 * header's uid is the VEHICLE and the dword after it is the occupant. */
void __cdecl SendVehicleExit(struct AM2_Object *vehicle, struct AM2_Object *occupant);

/* 0x0043A210, four callers. Increment the reveal count on the 5x5 block of
 * tiles around a new object, in every allied army's grid. Its first argument
 * is an ARMY, which orig.h had as an object for a long time -- see
 * armymsg.cpp. */
void __cdecl ItemPostCreate(int32_t army, AM2_Point where);

/* 0x0042AB50, four callers, all of them creators. Declared here now that
 * CreateItem is reconstructed and calls it by name; it had no header
 * declaration for as long as its only C++ caller was in armymsg.cpp itself. */
void __cdecl SendItemCreate(struct AM2_Object *obj);

/* 0x0044C480, one caller. The SENDER of the kind-0x16 batch RecvTroopBatch
 * receives: walk one comm slot's army object list and append every live
 * trooper's state to one message, flushing whenever another record would not
 * fit. See armymsg.cpp for why the flush test is a guess rather than a bound,
 * and for the one uid on this transport that is not put on the wire. */
void __cdecl TellOneSlot(int32_t slot);

/* 0x0044C150, two callers, and it names itself in both log lines. The
 * kind-0x21 message a drop goes out as. Its "request" field is the literal 3
 * every time, and its second log puts the uids through UidOnWire a SECOND
 * time -- harmless only because that function is the identity here. See
 * armymsg.cpp. */
void __cdecl TrooperDropItemSend(struct AM2_Object *unit, struct AM2_Object *item, int8_t slot,
                                 int32_t quantity, AM2_Point at);

/* 0x0044BFA0, two callers, one of them the kind-0x19 receiver turning a WANT
 * into a DO. The same 0x1C record under kind 0x19, with the point taken from
 * the ITEM's position and the request as an argument. Its four log lines are
 * what name AM2_WANT_PICKUP, AM2_WANT_DROP, AM2_DO_PICKUP and AM2_DO_DROP. */
void __cdecl TrooperWantItemSend(struct AM2_Object *trooper, struct AM2_Object *item, int32_t request,
                                 int8_t slot, int32_t quantity);

#endif

/* The header every message on this transport begins with. ArmyMessageSend
 * reads all three: the length decides how much is copied, and the uid is fed
 * to UidArmy for the debug line. */
typedef struct AM2_ArmyMsgHdr {
    uint16_t len;    /* +0x00, bytes to copy, including this header */
    uint16_t kind;   /* +0x02 */
    uint32_t uid;    /* +0x04 */
} AM2_ArmyMsgHdr;

/* The item request, AM2_MSG_TROOPER_DROP_ITEM and _WANT_ITEM, 0x1C bytes. */
typedef struct AM2_ItemMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_DROP_OFF_TROOPER */
    uint32_t  item;             /* +0x08 MSG_DROP_OFF_ITEM, on the wire */
    AM2_Point at;               /* +0x0C MSG_DROP_OFF_AT */
    int32_t   request;          /* +0x10 MSG_DROP_OFF_REQUEST: AM2_WANT_PICKUP.. */
    int32_t   quant;            /* +0x14 MSG_DROP_OFF_QUANT */
    int32_t   slot;             /* +0x18 MSG_DROP_OFF_SLOT, sign-extended */
} AM2_ItemMsg;

/* AM2_MSG_PAIR, 0x1C bytes: two objects and b's position. */
typedef struct AM2_PairMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_PAIR_OFF_A */
    uint32_t  b;                /* +0x08 MSG_PAIR_OFF_B */
    AM2_Point pos;              /* +0x0C MSG_PAIR_OFF_POS */
    int32_t   hole;             /* +0x10 MSG_PAIR_OFF_HOLE, never written */
    int32_t   arg;              /* +0x14 MSG_PAIR_OFF_ARG */
    int32_t   byteArg;          /* +0x18 MSG_PAIR_OFF_BYTE, sign-extended from int8 */
} AM2_PairMsg;

/* AM2_MSG_TROOPER_WEAPON, 0x1C bytes. */
typedef struct AM2_SetWeaponMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_SETWEAPON_OFF_TROOPER */
    uint32_t  weapon;           /* +0x08 MSG_SETWEAPON_OFF_WEAPON */
    uint8_t   pad0C[0x0C];      /* +0x0C */
    int32_t   slot;             /* +0x18 MSG_SETWEAPON_OFF_SLOT, 0..5 */
} AM2_SetWeaponMsg;

/* AM2_MSG_ITEM_DEPLOY, 0x10 bytes. */
typedef struct AM2_DeployMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_DEPLOY_OFF_UID */
    AM2_Point pos;              /* +0x08 MSG_DEPLOY_OFF_POS */
    uint8_t   facing;           /* +0x0C MSG_DEPLOY_OFF_FACING */
    uint8_t   resurrect;        /* +0x0D MSG_DEPLOY_OFF_RESURRECT */
    uint8_t   pad0E[2];         /* +0x0E */
} AM2_DeployMsg;

/* The damage message, 0x14 bytes. */
typedef struct AM2_DamageMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_DAMAGE_OFF_UID, the victim */
    uint32_t  attacker;         /* +0x08 MSG_DAMAGE_OFF_ATTACKER */
    AM2_Point pos;              /* +0x0C MSG_DAMAGE_OFF_POS */
    int16_t   amount;           /* +0x10 MSG_DAMAGE_OFF_AMOUNT */
    uint8_t   damageKind;       /* +0x12 MSG_DAMAGE_OFF_KIND */
    uint8_t   pad13;            /* +0x13 */
} AM2_DamageMsg;

/* AM2_MSG_TROOPER_FIRE, 0x1C bytes. */
typedef struct AM2_TrooperFireMsg {
    AM2_ArmyMsgHdr hdr;             /* +0x00 uid: the trooper */
    uint32_t  target;               /* +0x08 the trooper's local target */
    int16_t   x;                    /* +0x0C its aim spot */
    int16_t   y;                    /* +0x0E */
    int16_t   z;                    /* +0x10 the spot's ground height */
    uint16_t  pad12;                /* +0x12 never written */
    uint32_t  shotAt;               /* +0x14 the target argument; read back as the weapon */
    uint8_t   flag;                 /* +0x18 the fire flag, the byte at +0x529 */
    uint8_t   pad19[3];             /* +0x19 */
} AM2_TrooperFireMsg;

/* Kind 0x1C, a vehicle's shot, 0x18 bytes. */
typedef struct AM2_VehicleFireMsg {
    AM2_ArmyMsgHdr hdr;             /* +0x00 uid: the vehicle */
    uint32_t  target;               /* +0x08 */
    int16_t   x;                    /* +0x0C */
    int16_t   y;                    /* +0x0E */
    int16_t   z;                    /* +0x10 */
    uint16_t  gun;                  /* +0x12 the turret facing */
    uint8_t   seed;                 /* +0x14 the spread seed at +0x529 */
    uint8_t   pad15[3];             /* +0x15 */
} AM2_VehicleFireMsg;

/* A vehicle and the unit getting in or out, 0x0C bytes. */
typedef struct AM2_VehicleMsg {
    AM2_ArmyMsgHdr hdr;             /* +0x00 uid: the vehicle */
    uint32_t  occupant;             /* +0x08 */
} AM2_VehicleMsg;

/* AM2_MSG_DEATH, AM2_MSG_DEATH_BYTES. */
typedef struct AM2_DeathMsg {
    AM2_ArmyMsgHdr hdr;             /* +0x00 uid: the victim */
    uint32_t  by;                   /* +0x08 the attacker */
    uint8_t   kind;                 /* +0x0C how it died */
    uint8_t   pad0D[3];             /* +0x0D */
} AM2_DeathMsg;

/* AM2_MSG_ITEM_CREATE, 0x70 bytes. */
typedef struct AM2_CreateMsg {
    AM2_ArmyMsgHdr hdr;         /* +0x00 uid: MSG_CREATE_OFF_UID */
    char      name[0x40];       /* +0x08 MSG_CREATE_OFF_NAME, empty for none */
    int16_t   type;             /* +0x48 MSG_CREATE_OFF_TYPE, 1..4 */
    AM2_Point pos;              /* +0x4A MSG_CREATE_OFF_A and _B */
    uint16_t  pad4E;            /* +0x4E */
    AM2_Rect  box;              /* +0x50 MSG_CREATE_OFF_BLOCK */
    uint32_t  flags;            /* +0x60 MSG_CREATE_OFF_C */
    int16_t   subtype;          /* +0x64 MSG_CREATE_OFF_SUBTYPE */
    uint16_t  pad66;            /* +0x66 */
    uint32_t  ref;              /* +0x68 MSG_CREATE_OFF_D: weapon uid, target uid or key */
    uint8_t   extra;            /* +0x6C MSG_CREATE_OFF_E, never written */
    uint8_t   pad6D[3];         /* +0x6D */
} AM2_CreateMsg;

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
void __cdecl SendTrooperSetWeapon(const struct AM2_Object *trooper, uint32_t weaponUid,
                                  int32_t weapon);

/* 0x0042AA10, two callers. Tell the other players an object was destroyed.
 * Silent outside a multiplayer session. */
void __cdecl SendObjDestroyed(const struct AM2_Object *obj);

/* 0x0042A9A0. Tell the other players an ITEM is gone. Silent outside a
 * multiplayer session, and only for object types 1..4. */
void __cdecl ItemGoneMessageSend(const struct AM2_Object *obj);

/* 0x0042ACE0. Routes an army message to its receiver; 1 if it took it. Six
 * codes, and the middle of the range is NOT among them -- 0x13..0x22 get 0.
 * `army` is pushed by the caller and never read. */
int32_t __cdecl ArmyMsgFilter(void *msg, int32_t army);

/* 0x0042AA50. Tell the other players an item was deployed. Sixteen bytes; the
 * two trailing bytes are the item's facing and the caller's argument, in that
 * order. Guarded on the SESSION pointer, not COMM_OFF_DPLAY. */
void __cdecl SendItemDeploy(const struct AM2_Object *item, int32_t arg);

/* 0x0042A880. The only sender of the damage message. The position it carries
 * is the VICTIM's own -- every caller passes that -- and the sixth argument
 * is never read. */
void __cdecl DamageBroadcast(struct AM2_Object *obj, uint32_t attacker, int32_t amount,
                             int32_t kind, const AM2_Point *where, int32_t unused);

/* 0x0044C0F0, five callers. A 28-byte kind-0x18 message naming two objects.
 * Structural name; see armymsg.cpp, and note the hole at +0x10. */
void __cdecl SendPairMessage(const struct AM2_Object *a, const struct AM2_Object *b, int32_t byteArg,
                             int32_t arg);

/* 0x00410420. Send the accumulated packet to everyone else, if the rate limit
 * lets it. Answers 1 when the packet is dealt with -- including when there is
 * nobody to send to, which is what stops ArmyMessageSend's spin in a
 * single-player game -- and 0 only when it declined. */
int32_t __cdecl ArmyMessageFlush(int32_t least);

int armymsg_install(void);

#ifdef __cplusplus
}
#endif

/* 0x0045E300. Kind 0x24, twelve bytes: tell the other players a unit got
 * into a vehicle. */
void __cdecl SendVehicleEnter(struct AM2_Object *vehicle, struct AM2_Object *unit);

/* 0x0045E220. Kind 0x1C, twenty-four bytes: a vehicle fired. Also CLEARS
 * UNIT_OFF_FIRE_ACTIVE, so the send is the acknowledgement too. */
void __cdecl SendVehicleFire(struct AM2_Object *vehicle);

/* 0x0045E480. The vehicle batch sender -- TellOneSlot compiled a second time
 * for type 3, differing in the kind, the predicate and the appender. */
void __cdecl SendVehicleUpdates(int32_t slot);

/* 0x0045DAA0. Append one vehicle's DELTA update to a message buffer whose
 * first word is its length. The sibling of AppendTroopState, sharing its
 * design and not its code. */
void __cdecl VehicleUpdateAppend(void *msg, struct AM2_Object *obj);

/* 0x0045E550. Ask each of the four comm slots to send its vehicle batch. */
void __cdecl SendAllVehicleUpdates(void);

/* 0x0045E0D0. Kind 0x1D, 28 bytes: a vehicle asks to pick an item up or
 * drop one, or is told the answer. The receive half is RecvVehicle1D. */
void __cdecl SendVehicleWantItem(struct AM2_Object *vehicle, struct AM2_Object *item, int32_t request,
                                 int8_t slot, int32_t quant);

/* 0x004623D0. Paint one player's latency bar, its colour stepping through
 * thresholds at 750, 1250 and 1750 ms. */
void __cdecl DrawLatencyBar(int32_t row, int32_t slot);

#endif /* AM2_ARMYMSG_H */
