/* armymsg.cpp -- see armymsg.h. */
#include <stdint.h>
#include <string.h>

#include "armymsg.h"
#include "image.h"
#include "item.h"          /* UidOnWire, UidArmy */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "objtable.h" /* LookupByUID */
#include "dist.h"     /* AngleBetween */
#include "army.h"     /* AllyFlag, SetLeadsAndAct */
#include "packkey.h"
#include "misc.h"

/* ArmyMessageFlush stays original and is reached by address. It empties the
 * packet and resets the cursor to AM2_ARMY_PACKET_HDR, returning zero when it
 * could not send. */
typedef int32_t (__cdecl *AM2_ArmyMessageFlushFn)(int32_t arg);
#define orig_army_message_flush \
    (*(AM2_ArmyMessageFlushFn)AM2_IMAGE(ADDR_ARMY_MESSAGE_FLUSH))

#define kComm       (*(uint8_t **)AM2_IMAGE(ADDR_COMM_OBJECT))
#define kPacket     ((uint8_t *)AM2_IMAGE(ADDR_ARMY_PACKET))
#define kPacketLen  (*(int32_t *)AM2_IMAGE(ADDR_ARMY_PACKET_LEN))

#define kCommField(off) (*(const int32_t *)(kComm + (off)))

/* 0x004105F0. Append one message to the outgoing packet.
 *
 * The three gates are re-read each time round, which is why this is a loop
 * rather than straight-line code: after a successful flush the original jumps
 * back to the `joined` test, not to the top, so DirectPlay going away during
 * the flush is caught by the check that follows it instead.
 *
 * Two of the three complaints it can make are pure diagnostics -- a zero
 * length and an oversized message are both logged and then sent anyway. Only
 * the third does anything, and what it does is spin: flush until it succeeds.
 * All of it is inert in this build, where the logger is a single `ret`.
 *
 * The unconditional debug line passes the MESSAGE as the format string. That
 * is the original's, not a transcription slip -- the harness recognises the
 * case and skips it. Reproduced so the two sides log alike.
 */
void __cdecl ArmyMessageSend(const void *msg)
{
    const AM2_ArmyMsgHdr *hdr = (const AM2_ArmyMsgHdr *)msg;

    if (kCommField(COMM_OFF_DPLAY) == 0)
        return;

    for (;;) {
        uint32_t n;

        if (kCommField(COMM_OFF_JOINED) == 0)
            return;

        if ((uint32_t)kCommField(COMM_OFF_PLAYER_COUNT) < 2u)
            return;

        orig_log((const char *)msg, 1, UidArmy(UidOnWire(hdr->uid)));

        if (hdr->len == 0)
            orig_log("ArmyMessageSend Zero length message \n");

        n = hdr->len;

        if (n + AM2_ARMY_MSG_HDR >= (uint32_t)kCommField(COMM_OFF_BUFFER_MAX))
            orig_log("Error msg larger (%d) than Max (%d) in "
                     "ArmyMessageSend\n", n,
                     kCommField(COMM_OFF_BUFFER_MAX));

        if (n + (uint32_t)kPacketLen
            < (uint32_t)kCommField(COMM_OFF_BUFFER_MAX)) {
            memcpy(kPacket + kPacketLen, msg, n);
            kPacketLen += (int32_t)n;

            /* Lookahead, not a bounds check: flush when ANOTHER message this
             * size would pass the threshold. */
            if ((uint32_t)kPacketLen + n
                >= (uint32_t)kCommField(COMM_OFF_BUFFER_DEFAULT))
                orig_army_message_flush(0);

            return;
        }

        while (!orig_army_message_flush(0))
            orig_log("Send Over Flow, Couldn't Empty Buffer \n");

        if (kCommField(COMM_OFF_DPLAY) == 0)
            return;
    }
}

/* SendGameMsg stays original -- 928 bytes and fourteen callers, the hub this
 * whole family funnels through. */
typedef int32_t (__cdecl *AM2_SendGameMsgFn)(void *msg, int32_t a, int32_t b);
#define orig_send_game_msg \
    (*(AM2_SendGameMsgFn)AM2_IMAGE(ADDR_SEND_GAME_MSG))

/* 0x00410820. Tell the other players the game has been paused or resumed.
 *
 * The message is a single static buffer in .bss, not a local -- nothing in the
 * file image backs 0x004FAA50, so its length and kind are filled in somewhere
 * else and this only writes the two payload fields. That also makes it
 * decidedly not re-entrant, which is the original's design and not a
 * transcription choice.
 *
 * Gated on the session being joined, and nothing else: unlike ArmyMessageSend
 * it does not care how many players there are.
 *
 * The log renders the pause as "TRUE"/"FALSE" from the image's own two string
 * literals and reports our own player id, so a capture shows who paused. */
void __cdecl SendGamePause(int32_t pause, int32_t flags)
{
    uint8_t *msg = (uint8_t *)AM2_IMAGE(ADDR_MSG_GAME_PAUSE);

    if (kCommField(COMM_OFF_JOINED) == 0)
        return;

    *(int32_t *)(msg + MSG_PAUSE_OFF_PAUSE) = pause;
    *(int32_t *)(msg + MSG_PAUSE_OFF_FLAGS) = flags;

    orig_send_game_msg(msg, 0, 1);

    if (kCommField(COMM_OFF_EVENT_DEBUG))
        orig_log("SendGamePause from %x  Pause =%s  Flags=%x \n",
                 kCommField(COMM_OFF_OUR_PLAYER_ID),
                 pause ? (const char *)AM2_IMAGE(ADDR_STR_TRUE)
                       : (const char *)AM2_IMAGE(ADDR_STR_FALSE),
                 flags);
}

/* 0x0044C370. Tell the other players that a trooper's weapon changed.
 *
 * Silent unless a DirectPlay session exists -- the same COMM_OFF_DPLAY test the
 * colour and team senders use, and the reason nothing in a single-player run
 * ever emits this.
 *
 * The record is 0x1C bytes and only FIVE fields of it are written: the length,
 * the kind, the two uids and the weapon at +0x18. Everything from +0x0C to
 * +0x17 is left as whatever was on the stack. That is the original's, not a
 * transcription gap -- it declares 0x1C of stack, fills the fields it means to
 * send, and hands the lot to ArmyMessageSend. Reproduced, and worth knowing
 * before anyone compares two of these records byte for byte.
 *
 * Both uids go through UidOnWire on the way out, which is what every other
 * message in this family does; the weapon does not, because it is a type
 * rather than an object. Note the LOG prints the raw values, not the wire
 * ones -- so a message and its log line disagree by construction.
 *
 * Measured: it runs EIGHT times in a Boot Camp mission, and all eight return
 * at the session test, because this machine cannot open a DirectPlay session.
 * So the guard is thoroughly exercised and nothing past it is -- the record,
 * both UidOnWire calls and the log line are verified by reading. That is the
 * same ceiling every sender in this family has, and the counter moving at all
 * is only visible because the ten callers are still the original's. */
void __cdecl SendTrooperSetWeapon(const void *trooper, uint32_t weaponUid,
                                  int32_t weapon)
{
    uint8_t         msg[AM2_MSG_TROOPER_WEAPON_LEN];
    const uint8_t  *comm = kComm;
    uint32_t        uid;

    if (!*(void *const *)(comm + COMM_OFF_DPLAY))
        return;

    uid = *(const uint32_t *)((const uint8_t *)trooper + 4);

    *(uint16_t *)(msg + 0)    = AM2_MSG_TROOPER_WEAPON_LEN;
    *(uint16_t *)(msg + 2)    = AM2_MSG_TROOPER_WEAPON;
    *(uint32_t *)(msg + 4)    = UidOnWire(uid);
    *(uint32_t *)(msg + 8)    = UidOnWire(weaponUid);
    *(int32_t  *)(msg + 0x18) = weapon;

    ArmyMessageSend(msg);
    orig_log((const char *)(uintptr_t)ADDR_STR_SEND_TROOPER_WEAPON,
             uid, weaponUid);
}

/* 0x0042AA10. Tell the other players an object was destroyed.
 *
 * The shortest message in this family: eight bytes, and after the length and
 * the kind there is nothing but the uid. Both callers have already done the
 * destroying by the time they reach it.
 *
 * The guard is ADDR_MP_SESSION, not the COMM_OFF_DPLAY test SendTrooperSetWeapon
 * uses -- a session that is being HOST or JOIN rather than a live DirectPlay
 * pointer. The two are not the same question and the family is not consistent
 * about which it asks; reproduced per function rather than unified.
 *
 * Unexercised, and for a reason this project already has written down. The
 * counter reads 0 through a driven Boot Camp mission -- nothing in that window
 * dies -- which is the same window that leaves RemoveFromItemList and FreeItem
 * cold. Reaching it needs a mission driven long enough for something to be
 * killed, which is a drive this project does not yet have. Not a missing code
 * path; a missing drive. Verified by reading. */
void __cdecl SendObjDestroyed(const void *obj)
{
    uint8_t msg[AM2_MSG_OBJ_DESTROYED_LEN];

    if (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION)
        return;

    *(uint16_t *)(msg + 0) = AM2_MSG_OBJ_DESTROYED_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_OBJ_DESTROYED;
    *(uint32_t *)(msg + 4) =
        UidOnWire(*(const uint32_t *)((const uint8_t *)obj + 4));

    ArmyMessageSend(msg);
}

/* 0x0042A9A0. Tell the other players an item is gone.
 *
 * The name is the program's own -- "itemGoneMessageSend uid %x item_type %d"
 * -- which also settles what kind 0x0E means without needing a receiver, the
 * thing its sibling SendObjDestroyed had to infer from its callers.
 *
 * Same eight-byte shape as that one and the same ADDR_MP_SESSION guard, with
 * one extra gate: the object's TYPE must be 1..4. So this is the ITEM half of
 * the family and the sibling is the general one, which is why the sweep that
 * calls this can leave the rest alone.
 *
 * The log line is behind COMM_OFF_VERBOSE and prints the RAW uid, where the
 * message carries the wire one -- the same disagreement SendTrooperSetWeapon
 * has, and the same reason: the log is for a human reading a single machine's
 * trace, not for matching two machines' traffic.
 *
 * Its counter reads 0 in a driven Boot Camp mission, and the reason is worth
 * getting right because I first wrote it down backwards. It is NOT the session
 * guard returning early -- the trace stub counts on ENTRY, before any of this
 * body runs, so a guard cannot hold the counter down. Nor is it the usual
 * count-of-0 blind spot, which needs the CALLER to be ours: the only caller is
 * the registry sweep at 0x00428C40 and that is still the original's, so a call
 * would cross the patch and be counted. Zero here means the function is simply
 * never called in that window -- the sweep frees only items flagged for it,
 * and nothing in a Boot Camp mission is. Verified by reading. */
void __cdecl ItemGoneMessageSend(const void *obj)
{
    uint8_t         msg[AM2_MSG_OBJ_DESTROYED_LEN];
    const uint8_t  *comm;
    uint32_t        uid;
    int32_t         type;

    if (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION)
        return;

    type = *(const int32_t *)obj;
    if (type <= 0 || type > 4)
        return;

    uid = *(const uint32_t *)((const uint8_t *)obj + 4);

    *(uint16_t *)(msg + 0) = AM2_MSG_OBJ_DESTROYED_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_ITEM_GONE;
    *(uint32_t *)(msg + 4) = UidOnWire(uid);

    comm = (const uint8_t *)kComm;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_ITEM_GONE_SEND, uid, type);

    ArmyMessageSend(msg);
}

typedef int32_t (__cdecl *AM2_RecvFn)(void *msg);

/* The three receivers, defined below beside the senders they pair with. */
void __cdecl RecvObjDestroyed(void *msg);
void __cdecl RecvItemGone(void *msg);
void __cdecl RecvDeath(void *msg);
void __cdecl RecvItemDeploy(void *msg);
void __cdecl RecvDamage(void *msg);
void __cdecl RecvItemCreate(void *msg);

/* 0x0042ACE0, one caller. Routes an incoming army message to its receiver and
 * answers whether it took it -- 1 for the six codes it knows, 0 otherwise, so
 * the caller can go on looking.
 *
 * The original is a sparse switch: a 22-byte index table at 0x0042AD7C mapping
 * code-0x0E to an arm, then a seven-entry jump table. Written as an ordinary
 * switch, because the codes are what the source had and the two tables are the
 * compiler's way of spelling it -- what matters is which codes are handled and
 * that is preserved exactly.
 *
 * THE RANGE IS 0x0E..0x23 AND THE MIDDLE OF IT IS UNHANDLED. Codes 0x13
 * through 0x22 land on the default and get 0, so the handled set is five
 * consecutive codes and then one lone code at the far end. Reading the six
 * arms as consecutive would be wrong by eleven.
 *
 * AM2_MSG_TROOPER_WEAPON is 0x22 and falls in that gap -- it is handled
 * somewhere else entirely, which is worth knowing before assuming this is the
 * one place army messages are dispatched.
 *
 * Five of the six receivers name themselves in their own trace lines, which is
 * where AM2_MSG_ITEM_DEPLOY, AM2_MSG_DAMAGE and AM2_MSG_ITEM_CREATE came from
 * -- those three codes had no name until the handlers were read.
 *
 * THE SECOND ARGUMENT IS IGNORED. The caller computes an army and pushes it,
 * and nothing in the 192 bytes reads [esp+8]; the six receivers are each
 * called with the message alone. It is cdecl, so the extra push is harmless,
 * and the parameter is kept in the signature because the call site has it --
 * dropping it would make the two disagree about the ABI for no gain.
 *
 * VERIFIED BY READING. Its counter is blind -- ReceiveArmyMsg is ours -- and
 * that caller reads 0 itself through a full Boot Camp mission, because no army
 * message arrives without a peer to send one. So this is the DirectPlay wall
 * again, and the reconstruction's real check is the dispatch table, which was
 * read out of the image rather than inferred: the 22-byte index table at
 * 0x0042AD7C and the seven-entry jump table above it agree that exactly six
 * codes are handled and which arm each takes. */
int32_t __cdecl ArmyMsgFilter(void *msg, int32_t army)
{
    uint32_t code = *(const uint16_t *)((const uint8_t *)msg + 2);

    (void)army;   /* pushed by the caller and never read -- see above */

    switch (code) {
    case AM2_MSG_ITEM_GONE:      RecvItemGone(msg);     return 1;
    case AM2_MSG_ITEM_DEPLOY:    RecvItemDeploy(msg);   return 1;
    case AM2_MSG_OBJ_DESTROYED:  RecvObjDestroyed(msg); return 1;
    case AM2_MSG_DAMAGE:         RecvDamage(msg);        return 1;
    case AM2_MSG_ITEM_CREATE:    RecvItemCreate(msg);   return 1;
    case AM2_MSG_DEATH:          RecvDeath(msg);         return 1;
    default:                                                   return 0;
    }
}

/* 0x0042AA50, one caller. Tell the other players an item was deployed --
 * placed on the map with a facing.
 *
 * Sixteen bytes and it uses fourteen of them: length, code, the uid on the
 * wire, the packed position, then TWO bytes -- the item's own facing and the
 * caller's second argument. The two bytes are adjacent and easy to transpose;
 * the facing comes from the OBJECT and the other from the CALLER, which is the
 * only thing distinguishing them.
 *
 * The guard is the multiplayer SESSION pointer, not COMM_OFF_DPLAY as the
 * senders above it use. Reproduced as the original has it rather than made
 * consistent with its neighbours -- they are different questions, and which
 * one a sender asks is not ours to normalise.
 *
 * The trailing log is gated on COMM_OFF_VERBOSE, so it is silent in an
 * ordinary session, and it re-reads the object's fields rather than the ones
 * just packed. That matters for the position: the message carries the packed
 * dword while the log prints the two int16 halves.
 *
 * VERIFIED BY READING. Its counter is blind and its first line returns unless
 * a multiplayer session is up, so it cannot run on any drive this project has.
 * What the reconstruction rests on is the message LAYOUT, and that has a
 * second witness: ADDR_RECV_ITEM_DEPLOY unpacks the same sixteen bytes at the
 * other end and prints the same four fields in the same order. */
void __cdecl SendItemDeploy(const void *item, int32_t arg)
{
    uint8_t        msg[AM2_MSG_ITEM_DEPLOY_LEN];
    const uint8_t *it = (const uint8_t *)item;
    const uint8_t *comm;
    uint32_t       uid;

    if (!*(void *const *)(uintptr_t)ADDR_MP_SESSION)
        return;

    uid = *(const uint32_t *)(it + 4);

    *(uint16_t *)(msg + 0)    = AM2_MSG_ITEM_DEPLOY_LEN;
    *(uint16_t *)(msg + 2)    = AM2_MSG_ITEM_DEPLOY;
    *(uint32_t *)(msg + 4)    = UidOnWire(uid);
    *(uint32_t *)(msg + 8)    = *(const uint32_t *)(it + OBJ_OFF_POS);
    *(uint8_t  *)(msg + 0x0C) = *(const uint8_t *)(it + OBJ_OFF_FACING);
    *(uint8_t  *)(msg + 0x0D) = (uint8_t)arg;

    ArmyMessageSend(msg);

    comm = kComm;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_SEND_ITEM_DEPLOY,
                 uid,
                 (int32_t)*(const int16_t *)(it + OBJ_OFF_POS),
                 (int32_t)*(const int16_t *)(it + OBJ_OFF_POS + 2),
                 (int32_t)*(const uint8_t *)(it + OBJ_OFF_FACING));
}

typedef void (__cdecl *AM2_ObjDieFn)(void *obj, int32_t kind, uint32_t by);
#define orig_obj_die ((AM2_ObjDieFn)(uintptr_t)ADDR_OBJ_DIE)

/* 0x0042AF00, 0x0042AEB0 and 0x0042AE50 -- three of the six receivers
 * ArmyMsgFilter routes to, and each is the far end of a sender in this file.
 * That pairing is the check: the sender packs a field and the receiver reads
 * the same offset, so the layouts witness each other.
 *
 * All three call UidOnWire on the uid they RECEIVE, having been sent through
 * UidOnWire too. That is not a mistake and not a byte swap undone twice: this
 * build's UidOnWire is the identity, a placeholder for a conversion the port
 * does not need. Reproduced rather than elided, because eliding it would hide
 * where the conversion belongs if it ever comes back.
 *
 * THEY DIFFER IN WHEN THEY LOG, and it is not tidy. ItemGone logs only under
 * COMM_OFF_VERBOSE; Death logs UNCONDITIONALLY. ObjDestroyed does not log at
 * all. Reproduced as found -- three sibling functions in one band disagreeing
 * about their own tracing is exactly the sort of thing a rewrite unifies. */
void __cdecl RecvObjDestroyed(void *msg)
{
    const uint8_t *m   = (const uint8_t *)msg;
    void          *obj = LookupByUID(UidOnWire(*(const uint32_t *)(m + 4)));

    if (!obj)
        return;
    /* Health ZERO means it is already gone; this destroys only what is still
     * alive, so a duplicate message is harmless. */
    if (*(const int16_t *)((uint8_t *)obj + OBJ_OFF_HEALTH) == 0)
        return;

    DestroyByType(obj);
}

void __cdecl RecvItemGone(void *msg)
{
    const uint8_t *m   = (const uint8_t *)msg;
    void          *obj = LookupByUID(UidOnWire(*(const uint32_t *)(m + 4)));
    const uint8_t *comm;

    if (!obj)
        return;

    *(uint32_t *)((uint8_t *)obj + OBJ_OFF_FLAGS) |= OBJ_FLAG_OVERDUE;

    comm = kComm;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_RECV_ITEM_GONE,
                 ((const AM2_Object *)obj)->uid);
}

void __cdecl RecvDeath(void *msg)
{
    const uint8_t *m  = (const uint8_t *)msg;
    uint32_t       by = UidOnWire(*(const uint32_t *)(m + 8));
    void          *obj;

    obj = LookupByUID(UidOnWire(*(const uint32_t *)(m + 4)));
    if (!obj)
        return;

    /* The log prints the ATTACKER's army, not the victim's, and the victim's
     * LOCAL uid rather than the one that came over the wire. */
    orig_log((const char *)(uintptr_t)ADDR_STR_RECV_DEATH,
             ((const AM2_Object *)obj)->uid, UidArmy(by));

    orig_obj_die(obj, (int32_t)*(const uint8_t *)(m + 0x0C), by);
}

/* 0x0042AF30, the far end of SendItemDeploy above -- and the pair is what
 * confirms the layout, because each names the same four offsets independently.
 * The byte at MSG_DEPLOY_OFF_RESURRECT reaches DeployItem's `resurrect`
 * parameter, which is what finally names the argument the sender is handed.
 *
 * THE LOG COMES FIRST, before the lookup, so a message for an object this
 * side does not have is still traced and then silently dropped. It also
 * prints the RAW wire uid rather than putting it through UidOnWire -- which
 * is the identity here, so the two agree, but the asymmetry with the lookup
 * below is the original's and is reproduced.
 *
 * The position is read twice and two different ways: as two int16 halves for
 * the log, and as the packed dword for DeployItem. That is what the sender
 * writes, so both readings are of one field. */
void __cdecl RecvItemDeploy(void *msg)
{
    const uint8_t *m = (const uint8_t *)msg;
    const uint8_t *comm = kComm;
    void          *obj;

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_RECV_ITEM_DEPLOY,
                 *(const uint32_t *)(m + MSG_DEPLOY_OFF_UID),
                 (int32_t)*(const int16_t *)(m + MSG_DEPLOY_OFF_POS),
                 (int32_t)*(const int16_t *)(m + MSG_DEPLOY_OFF_POS + 2),
                 (int32_t)*(const uint8_t *)(m + MSG_DEPLOY_OFF_FACING));

    obj = LookupByUID(UidOnWire(*(const uint32_t *)(m + MSG_DEPLOY_OFF_UID)));
    if (!obj)
        return;

    *(uint8_t *)((uint8_t *)obj + OBJ_OFF_FACING) =
        *(const uint8_t *)(m + MSG_DEPLOY_OFF_FACING);

    DeployItem(obj, *(const uint32_t *)(m + MSG_DEPLOY_OFF_POS),
               (int32_t)*(const uint8_t *)(m + MSG_DEPLOY_OFF_RESURRECT), 1);
}

/* 0x0042ADA0, the last of the six receivers and the one that settles a
 * question the whole damage family depends on.
 *
 * THE MESSAGE CARRIES NO DIRECTION. It carries the ATTACKER's position, and
 * the receiver computes the direction here with AngleBetween against the
 * victim's own position, masked to a byte. That is what makes the third
 * argument of DamageObject and its four type handlers a DIRECTION rather than
 * a second amount -- decoded here rather than inferred, which is how
 * OBJ_OFF_HIT_DIR got its name.
 *
 * The order of the two positions matters and is easy to reverse: the ATTACKER
 * is the `from` and the victim the `to`, so the direction points at the
 * victim. Reversing it would flip every recoil and hit animation by 128.
 *
 * The log is gated on COMM_OFF_VERBOSE and prints the ATTACKER's army through
 * UidArmy while printing the VICTIM's uid -- two different objects on one
 * line, in that order.
 *
 * The last argument to DamageObject is a literal 1, so damage arriving over
 * the wire suppresses whatever the local path would otherwise do with it --
 * which is what stops a hit being broadcast back. */
void __cdecl RecvDamage(void *msg)
{
    const uint8_t *m = (const uint8_t *)msg;
    const uint8_t *comm;
    void          *obj;
    uint32_t       attacker;
    AM2_Point      from;
    int32_t        dir;

    attacker = UidOnWire(*(const uint32_t *)(m + MSG_DAMAGE_OFF_ATTACKER));
    obj      = LookupByUID(UidOnWire(*(const uint32_t *)(m + MSG_DAMAGE_OFF_UID)));
    if (!obj)
        return;

    from.x = *(const int16_t *)(m + MSG_DAMAGE_OFF_POS);
    from.y = *(const int16_t *)(m + MSG_DAMAGE_OFF_POS + 2);
    dir    = AngleBetween(&from,
                          (const AM2_Point *)((uint8_t *)obj + OBJ_OFF_POS));

    comm = kComm;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log((const char *)(uintptr_t)ADDR_STR_RECV_DAMAGE,
                 ((const AM2_Object *)obj)->uid,
                 (int32_t)*(const int16_t *)(m + MSG_DAMAGE_OFF_AMOUNT),
                 UidArmy(attacker), dir);

    DamageObject(obj,
                 (int32_t)*(const int16_t *)(m + MSG_DAMAGE_OFF_AMOUNT),
                 (int32_t)*(const uint8_t *)(m + MSG_DAMAGE_OFF_KIND),
                 attacker, dir, 1);
}

typedef void *(__cdecl *AM2_CreateItemFn)(const char *, int32_t, int32_t,
                                          int32_t, int32_t, int32_t, uint32_t);
typedef void *(__cdecl *AM2_CreateTrooperFn)(const char *, int32_t, int32_t,
                                             int32_t, int32_t, int32_t,
                                             int32_t, uint32_t, int32_t,
                                             int32_t);
typedef void *(__cdecl *AM2_CreateVehicleFn)(int32_t, const char *, int32_t,
                                             int32_t, int32_t, int32_t,
                                             int32_t, int32_t, uint32_t,
                                             int32_t);
typedef void *(__cdecl *AM2_CreateWeaponFn)(const char *, int32_t, int32_t,
                                            int32_t, int32_t, int32_t,
                                            int32_t, uint32_t);
typedef void (__cdecl *AM2_PostCreateFn)(void *obj, int32_t a);
#define orig_create_item     ((AM2_CreateItemFn)(uintptr_t)ADDR_CREATE_ITEM)
#define orig_create_trooper  ((AM2_CreateTrooperFn)(uintptr_t)ADDR_CREATE_TROOPER)
#define orig_create_vehicle  ((AM2_CreateVehicleFn)(uintptr_t)ADDR_CREATE_VEHICLE)
#define orig_create_weapon   ((AM2_CreateWeaponFn)(uintptr_t)ADDR_CREATE_WEAPON)
#define orig_post_create     ((AM2_PostCreateFn)(uintptr_t)ADDR_ITEM_POST_CREATE)
typedef void (__cdecl *AM2_ConcealFn)(void *obj, int32_t force);
#define orig_obj_conceal     ((AM2_ConcealFn)(uintptr_t)ADDR_OBJ_CONCEAL)

/* 0x0042AFA0, the SIXTH receiver and the one that completes the family. It
 * makes an object the other side has made, dispatching on
 * MSG_CREATE_OFF_TYPE, whose four values are this project's object types:
 * 1 item, 2 trooper, 3 vehicle, 4 weapon.
 *
 * That mapping is not a guess. Two of the four creators name themselves --
 * 0x0045F0C0 logs "CreateWeapon" and 0x0045B090 logs "Vehicle aai entry not
 * found for type %d" -- and fixing those two fixes the other two by position.
 *
 * THE ARMS READ MSG_CREATE_OFF_A AT DIFFERENT WIDTHS. Types 2 and 3 take it
 * as a signed WORD; types 1 and 4 take the full DWORD. Reproduced, because
 * that is what the arms do and a uniform reading would be wrong for two of
 * the four -- silently, for any value that fits in 16 bits.
 *
 * Each arm's argument count was checked against its own `add esp`, which is
 * the one self-check available in a function this shape: 10 for the trooper,
 * 10 for the vehicle, 8 for the weapon, 7 for the item.
 *
 * Three arms have a tail the others do not. A trooper whose subtype is 10
 * gets SetLeadsAndAct. A vehicle stores a uid at +0x550. An item whose
 * MSG_CREATE_OFF_D matches ADDR_CREATE_WATCHED_KIND gets a post-create step
 * and is then CONCEALED -- but only if the player is not allied to it, so
 * what you can see depends on whose side it is on.
 *
 * The log is UNCONDITIONAL, unlike ItemGone's and ItemDeploy's. It also runs
 * before anything is created, so a message that creates nothing still traces.
 *
 * `ebx` is the uid up to the item arm's creator call and the CREATED OBJECT
 * after it -- the original reuses the register. Written as two named locals
 * here; conflating them would pass a uid to ObjConceal. */
void __cdecl RecvItemCreate(void *msg)
{
    const uint8_t *m    = (const uint8_t *)msg;
    uint32_t       uid  = UidOnWire(*(const uint32_t *)(m + MSG_CREATE_OFF_UID));
    int32_t        army = (int32_t)UidArmy(uid);
    const char    *name = NULL;
    void          *made;

    orig_log((const char *)(uintptr_t)ADDR_STR_RECV_ITEM_CREATE, uid,
             (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_TYPE),
             (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_SUBTYPE));

    /* An empty first byte means the message carries no name. */
    if (*(const char *)(m + MSG_CREATE_OFF_NAME))
        name = (const char *)(m + MSG_CREATE_OFF_NAME);

    switch (*(const int16_t *)(m + MSG_CREATE_OFF_TYPE)) {
    case 1:
        made = orig_create_item(name, army,
                                *(const int32_t *)(m + MSG_CREATE_OFF_D),
                                *(const int32_t *)(m + MSG_CREATE_OFF_A),
                                *(const int32_t *)(m + MSG_CREATE_OFF_C),
                                1, uid);
        if (*(const int32_t *)(m + MSG_CREATE_OFF_D)
            != *(const int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND)
            return;
        orig_post_create(made, *(const int32_t *)(m + MSG_CREATE_OFF_A));
        if (!AllyFlag(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER, army))
            orig_obj_conceal(made, 1);
        return;

    case 2:
        made = orig_create_trooper(
                   name,
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_A),
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_B),
                   CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                     army),
                   army,
                   *(const int32_t *)(m + MSG_CREATE_OFF_C),
                   1, uid, 1,
                   (int32_t)*(const uint8_t *)(m + MSG_CREATE_OFF_E));
        if (*(const int16_t *)(m + MSG_CREATE_OFF_SUBTYPE)
            == AM2_TROOPER_SUBTYPE_LEADS)
            SetLeadsAndAct(made);
        return;

    case 3:
        made = orig_create_vehicle(
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_SUBTYPE),
                   name,
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_A),
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_B),
                   CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                     army),
                   army,
                   *(const int32_t *)(m + MSG_CREATE_OFF_C),
                   1, uid,
                   (int32_t)*(const uint8_t *)(m + MSG_CREATE_OFF_E));
        *(uint32_t *)((uint8_t *)made + 0x550) =
            UidOnWire(*(const uint32_t *)(m + MSG_CREATE_OFF_D));
        return;

    case 4:
        orig_create_weapon(
            name, army,
            KeyLookupTriple(AM2_WEAPON_KEY_KIND,
                            (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_SUBTYPE),
                            0),
            *(const int32_t *)(m + MSG_CREATE_OFF_A),
            *(const int32_t *)(m + MSG_CREATE_OFF_C),
            *(const int32_t *)(m + MSG_CREATE_OFF_D),
            1, uid);
        return;

    default:
        return;
    }
}

int armymsg_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ARMY_MESSAGE_SEND, (const void *)ArmyMessageSend,
                        "ArmyMessageSend", 1);
    rc |= patch_replace(ADDR_RECV_ITEM_CREATE, (const void *)RecvItemCreate,
                        "RecvItemCreate", 1);
    rc |= patch_replace(ADDR_RECV_DAMAGE, (const void *)RecvDamage,
                        "RecvDamage", 1);
    rc |= patch_replace(ADDR_RECV_ITEM_DEPLOY, (const void *)RecvItemDeploy,
                        "RecvItemDeploy", 1);
    rc |= patch_replace(ADDR_RECV_OBJ_DESTROYED, (const void *)RecvObjDestroyed,
                        "RecvObjDestroyed", 1);
    rc |= patch_replace(ADDR_RECV_ITEM_GONE, (const void *)RecvItemGone,
                        "RecvItemGone", 1);
    rc |= patch_replace(ADDR_RECV_DEATH, (const void *)RecvDeath,
                        "RecvDeath", 1);
    rc |= patch_replace(ADDR_ITEM_DEPLOY_MSG, (const void *)SendItemDeploy,
                        "SendItemDeploy", 1);
    rc |= patch_replace(ADDR_ARMY_MSG_FILTER, (const void *)ArmyMsgFilter,
                        "ArmyMsgFilter", 1);
    rc |= patch_replace(ADDR_SEND_TROOPER_WEAPON,
                        (const void *)SendTrooperSetWeapon,
                        "SendTrooperSetWeapon", 10);
    rc |= patch_replace(ADDR_SEND_OBJ_DESTROYED, (const void *)SendObjDestroyed,
                        "SendObjDestroyed", 2);
    rc |= patch_replace(ADDR_ITEM_GONE_SEND, (const void *)ItemGoneMessageSend,
                        "ItemGoneMessageSend", 1);
    rc |= patch_replace(ADDR_SEND_GAME_PAUSE, (const void *)SendGamePause,
                        "SendGamePause", 2);
    return rc;
}
