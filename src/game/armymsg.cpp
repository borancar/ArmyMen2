/* armymsg.cpp -- see armymsg.h. */
#include <stdint.h>
#include <string.h>

#include "armymsg.h"
#include "map.h"       /* TileOfPoint -- reconstructed */
#include "image.h"
#include "item.h"          /* UidOnWire, UidArmy */
#include "../inject/orig.h"
#include "../inject/patch.h"
#include "objtable.h" /* LookupByUID */
#include "objtype.h"  /* ObjIsType2 */
#include "dist.h"     /* AngleBetween */
#include "army.h"     /* AllyFlag, SetLeadsAndAct */
#include "packkey.h"
#include "misc.h"
#include "air.h"      /* ObjConceal */

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

/* ADDR_APPEND_TROOP_STATE stays original and is reached by address. It writes
 * one variable-length record for a trooper into the message and advances the
 * length itself -- 632 bytes of it, none of them read here. */
typedef void (__cdecl *AM2_AppendTroopStateFn)(void *msg, void *obj);
#define orig_append_troop_state \
    ((AM2_AppendTroopStateFn)(uintptr_t)ADDR_APPEND_TROOP_STATE)

/* TrooperDropItemSend -- original 0x0044C150, two callers, and it names itself
 * in both of its log lines: "<--Trooper Drop Item Send" going out and
 * "-->Trooper Drop Item Sent" once it has gone.
 *
 * A 0x1C-byte kind-0x21 message. Five of its six fields come from the
 * arguments and the sixth, MSG_DROP_OFF_REQUEST, is the same value every
 * time. That looked like a magic 3 when this was written; TrooperWantItemSend
 * below names it. It is AM2_DO_DROP -- the "this has happened" half of a
 * four-value protocol whose "may I" half is AM2_WANT_DROP -- so the
 * "request: %d" the second log prints is a constant because a drop message
 * IS the completed request.
 *
 * THE SECOND LOG PUTS THE UIDS THROUGH UidOnWire A SECOND TIME. The message
 * fields already hold wire uids, and the "Sent" line reads them back out and
 * wires them again. It is harmless HERE and only because UidOnWire is the
 * identity in this build -- the harmlessness is a property of that function,
 * not of this code, and if it ever stopped being the identity the two log
 * lines would disagree about the same message.
 *
 * That second log also reads its values back OUT OF THE MESSAGE rather than
 * from the arguments it still has in registers, which is the detail that
 * makes the double-wiring visible at all.
 *
 * IT DEREFERENCES THE ITEM BEFORE TESTING IT, exactly as TrooperDropItem
 * does one file over: the first log takes `item->uid` and the `if (!item)`
 * guard is eleven instructions later. The unit is never null-tested at all.
 * Both reproduced.
 *
 * The two logs are gated on COMM_OFF_VERBOSE and the SEND is not, so a quiet
 * build still sends and simply says nothing.
 */
void __cdecl TrooperDropItemSend(void *unit, void *item, int32_t slot,
                                 int32_t quantity, uint32_t at)
{
    uint8_t  msg[AM2_MSG_DROP_ITEM_LEN];
    uint8_t *u = (uint8_t *)unit;
    uint8_t *it = (uint8_t *)item;

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log("<--Trooper Drop Item Send: Trooper: %x, item: %x,  slot: %d,"
                " quant: %d \n",
                UidOnWire(((const AM2_Object *)u)->uid),
                UidOnWire(((const AM2_Object *)it)->uid),
                slot, quantity);

    if (!it)
        return;

    *(uint16_t *)(msg + 0) = AM2_MSG_DROP_ITEM_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_TROOPER_DROP_ITEM;
    *(uint32_t *)(msg + MSG_DROP_OFF_TROOPER) =
        UidOnWire(((const AM2_Object *)u)->uid);
    *(uint32_t *)(msg + MSG_DROP_OFF_ITEM) =
        UidOnWire(((const AM2_Object *)it)->uid);
    *(uint32_t *)(msg + MSG_DROP_OFF_AT)      = at;
    *(int32_t *)(msg + MSG_DROP_OFF_REQUEST)  = AM2_DO_DROP;
    *(int32_t *)(msg + MSG_DROP_OFF_QUANT)    = quantity;
    *(int32_t *)(msg + MSG_DROP_OFF_SLOT)     = (int32_t)(int8_t)slot;

    ArmyMessageSend(msg);

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log("-->Trooper Drop Item Sent: Trooper: %x, item: %x,"
                " request: %d, slot: %d, quant: %d \n",
                UidOnWire(*(const uint32_t *)(msg + MSG_DROP_OFF_TROOPER)),
                UidOnWire(*(const uint32_t *)(msg + MSG_DROP_OFF_ITEM)),
                *(const int32_t *)(msg + MSG_DROP_OFF_REQUEST),
                *(const int32_t *)(msg + MSG_DROP_OFF_SLOT),
                *(const int32_t *)(msg + MSG_DROP_OFF_QUANT));
}

/* TrooperWantItemSend -- original 0x0044BFA0, two callers, and one of them is
 * the kind-0x19 RECEIVER, which turns a WANT into a DO and sends it back.
 * That is the whole shape of the protocol in one call graph.
 *
 * ITS FOUR LOG LINES NAME THE PROTOCOL, which is why it was worth doing:
 * request 0 is WANT_PICKUP, 1 WANT_DROP, 2 DO_PICKUP and 3 DO_DROP. The
 * halves pair up -- a client asks WANT and the host answers DO -- and that
 * retroactively explains TrooperDropItemSend's literal 3 one function up,
 * which had gone in as a magic number with a note saying so.
 *
 * It fills the same 0x1C-byte record TrooperDropItemSend does, under kind
 * 0x19 instead of 0x21, with two differences: the point comes from the ITEM's
 * own position rather than from the caller, and the request is an argument
 * rather than a constant.
 *
 * THE TAIL IS A CHAIN OF `cmp` AND NOT A SWITCH, and it matters: request 0
 * has no test of its own -- it is the `!request` arm reached when the header
 * log has already run -- while 1, 2 and 3 are compared in turn and anything
 * else falls off the end silently. So a fifth request value would send the
 * message and say nothing.
 *
 * All five logs are gated on COMM_OFF_VERBOSE, and the SEND is not. The
 * header log dereferences the item before the `if (!item)` guard below it,
 * which is the same shape the other three functions in this family have.
 */
void __cdecl TrooperWantItemSend(void *trooper, void *item, int32_t request,
                                 int32_t slot, int32_t quantity)
{
    uint8_t  msg[AM2_MSG_DROP_ITEM_LEN];
    uint8_t *t = (uint8_t *)trooper;
    uint8_t *it = (uint8_t *)item;

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_WANT_SEND_HDR),
                 UidOnWire(((const AM2_Object *)t)->uid),
                 UidOnWire(((const AM2_Object *)it)->uid),
                 request, slot, quantity);

    if (!it)
        return;

    *(uint16_t *)(msg + 0) = AM2_MSG_DROP_ITEM_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_TROOPER_WANT_ITEM;
    *(uint32_t *)(msg + MSG_DROP_OFF_TROOPER) =
        UidOnWire(((const AM2_Object *)t)->uid);
    *(uint32_t *)(msg + MSG_DROP_OFF_ITEM) =
        UidOnWire(((const AM2_Object *)it)->uid);
    *(uint32_t *)(msg + MSG_DROP_OFF_AT) =
        *(const uint32_t *)(it + OBJ_OFF_POS);
    *(int32_t *)(msg + MSG_DROP_OFF_REQUEST) = request;
    *(int32_t *)(msg + MSG_DROP_OFF_QUANT)   = quantity;
    *(int32_t *)(msg + MSG_DROP_OFF_SLOT)    = (int32_t)(int8_t)slot;

    ArmyMessageSend(msg);

    if (!*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        return;

    /* All four read the QUANTITY back out of the MESSAGE rather than using
     * the argument still in a register -- the same habit TrooperDropItemSend's
     * second log has. The format calls it "ammo", which is what settles what
     * MSG_DROP_OFF_QUANT counts. */
    if (!request)
        orig_log((const char *)AM2_IMAGE(ADDR_STR_WANT_PICKUP),
                 ((const AM2_Object *)it)->uid,
                 *(const int32_t *)(msg + MSG_DROP_OFF_QUANT));
    else if (request == AM2_WANT_DROP)
        orig_log((const char *)AM2_IMAGE(ADDR_STR_WANT_DROP),
                 ((const AM2_Object *)it)->uid,
                 *(const int32_t *)(msg + MSG_DROP_OFF_QUANT));
    else if (request == AM2_DO_PICKUP)
        orig_log((const char *)AM2_IMAGE(ADDR_STR_DO_PICKUP),
                 ((const AM2_Object *)it)->uid,
                 *(const int32_t *)(msg + MSG_DROP_OFF_QUANT));
    else if (request == AM2_DO_DROP)
        orig_log((const char *)AM2_IMAGE(ADDR_STR_DO_DROP),
                 ((const AM2_Object *)it)->uid,
                 *(const int32_t *)(msg + MSG_DROP_OFF_QUANT));
}

/* TellOneSlot -- original 0x0044C480, one caller, which is TellEachSlot.
 *
 * The SENDER of the kind-0x16 batch. Its receiver, RecvTroopBatch, has been
 * reconstructed since long before this: that one walks a run of
 * variable-length sub-records from +8 to the header's length, and this is
 * where the run is built. One comm slot's army object list in, one message
 * out -- or several, if they do not fit.
 *
 * TWO TESTS DECIDE WHAT GOES IN. The object must not be destroyed, and it
 * must be a type 2 -- a trooper -- which is what makes this a TROOPER message
 * despite the list being every object the army owns.
 *
 * THE FLUSH TEST IS A GUESS AND NOT A BOUND. It asks whether the current
 * length plus TEN would exceed the buffer, and ten is nobody's record size:
 * the appender writes variable-length records and is never asked how long the
 * next one will be. So the check runs AFTER the append, which is the only
 * reason it works -- an oversized record has already been written by the time
 * anything notices. Reproduced, including the ordering, because the ordering
 * is what makes it safe.
 *
 * THE HEADER UID IS THE FIRST ACCEPTED TROOPER'S, RAW. Every other sender in
 * this module puts UidOnWire(uid) there and this one does not, so the field
 * carries a local uid where the rest of the transport carries a wire one.
 * Reproduced; the receiver hands each sub-record its army separately and does
 * not read this field at all, so nothing here depends on which it is.
 *
 * The list POINTER and its count are both re-read from the table every
 * iteration, so an append that reallocated the list would be seen. Nothing in
 * the loop does, and it is reproduced rather than hoisted for the usual
 * reason: which loads the original chose to repeat is evidence.
 *
 * A flush inside the loop resets the length and NOT the uid, so the second
 * and later messages of a long list carry the first trooper's uid. */
void __cdecl TellOneSlot(int32_t slot)
{
    uint8_t         msg[AM2_TROOP_BATCH_MAX];
    AM2_ArmyMsgHdr *h = (AM2_ArmyMsgHdr *)msg;
    const uint8_t  *list;
    int32_t         i = 0;

    h->len  = (uint16_t)sizeof(AM2_ArmyMsgHdr);
    h->kind = AM2_MSG_TROOP_BATCH;
    h->uid  = 0;

    list = (const uint8_t *)
        (*(void *const *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
    if (*(const int32_t *)(list + LIST_OFF_COUNT) <= 0)
        return;

    do {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(list + LIST_OFF_UIDS))[i]);

        if (obj) {
            if (!(*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
                && ObjIsType2((const AM2_Object *)obj)) {
                if (h->uid == 0)
                    h->uid = *(const uint32_t *)(obj + 4);
                orig_append_troop_state(msg, obj);
            }
            if ((uint32_t)h->len + AM2_TROOP_BATCH_SLACK
                    > AM2_TROOP_BATCH_MAX) {
                ArmyMessageSend(msg);
                h->len = (uint16_t)sizeof(AM2_ArmyMsgHdr);
            }
        }

        list = (const uint8_t *)
            (*(void *const *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
        i++;
    } while (i < *(const int32_t *)(list + LIST_OFF_COUNT));

    if (h->len != sizeof(AM2_ArmyMsgHdr))
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
/* 0x0044C0F0, five callers -- all in the trooper band. A 28-byte message of
 * kind 0x18 naming two objects: each one's uid through UidOnWire, the SECOND
 * one's position, and two values the caller supplies.
 *
 * THERE IS A HOLE AT MSG_PAIR_OFF_HOLE. Every other dword of the 0x1C is
 * written and that one is not, so four bytes of this function's own stack go
 * out on the wire. Reproduced rather than zeroed: a memset would be tidier and
 * would change what the far end sees, and nothing here says the receiver
 * ignores it.
 *
 * UNLIKE SendItemDeploy IT DOES NOT CHECK ADDR_MP_SESSION, so it builds and
 * sends even in single player -- ArmyMessageSend is where that ends. The two
 * senders sit in the same family and differ on that; the difference is the
 * original's.
 *
 * The byte argument is SIGN-EXTENDED into a dword, so a caller passing 0x80
 * puts -128 on the wire and not 128.
 *
 * The name is structural. Kind 0x18 has no receiver reconstructed yet and
 * nothing read so far says what the pair means. */
void __cdecl SendPairMessage(const void *a, const void *b, int32_t byteArg,
                             int32_t arg)
{
    uint8_t        msg[AM2_MSG_PAIR_LEN];
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    *(uint16_t *)(msg + 0) = AM2_MSG_PAIR_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_PAIR;

    *(uint32_t *)(msg + MSG_PAIR_OFF_A) =
        UidOnWire(*(const uint32_t *)(pa + 4));
    *(uint32_t *)(msg + MSG_PAIR_OFF_B) =
        UidOnWire(*(const uint32_t *)(pb + 4));
    *(uint32_t *)(msg + MSG_PAIR_OFF_POS) =
        *(const uint32_t *)(pb + OBJ_OFF_POS);
    *(int32_t *)(msg + MSG_PAIR_OFF_ARG)  = arg;
    *(int32_t *)(msg + MSG_PAIR_OFF_BYTE) = (int8_t)byteArg;

    ArmyMessageSend(msg);
}

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

    ObjDie(obj, (int32_t)*(const uint8_t *)(m + 0x0C), by);
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
 * THE MESSAGE CARRIES NO DIRECTION. The receiver computes one with
 * AngleBetween from the position in the message to the victim's own, masked
 * to a byte, and that is what reaches DamageObject's third argument.
 *
 * WHOSE POSITION IS IN THE MESSAGE -- CORRECTED. This comment first said the
 * ATTACKER's, inferred from the arithmetic right here, and it is wrong.
 * DamageBroadcast is the only sender and all FOUR of its callers pass
 * `victim + OBJ_OFF_POS`: it is the VICTIM's position as the SENDER saw it.
 * The angle is therefore between two views of one object, near zero whenever
 * the two sides agree, and what it is FOR is not established.
 *
 * The argument order is still worth stating because it is reversible: the
 * message's position is the `from` and the local one the `to`.
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
/* ItemPostCreate -- original 0x0043A210, four callers.
 *
 * Walk the 5x5 block of map tiles around a new object and increment the entry
 * in every ALLIED army's reveal grid. Four grids, one byte per tile: a count
 * of what reveals each tile, not a flag.
 *
 * ITS SIGNATURE WAS WRONG IN orig.h AND THE ERROR WAS LIVE. The macro read
 * `void(obj, int32)`, and RecvItemCreate below passed the freshly created
 * object into the first slot. The function opens `cmp eax, 4; jge` and
 * returns -- so with an object pointer there, every call returned at the
 * first instruction and no tile was ever revealed. The original's own call
 * site pushes the ARMY, which is what settles it; the argument that reaches
 * the second slot was right all along.
 *
 * It survived because nothing here can run it: RecvItemCreate is a
 * multiplayer message receiver, so no configuration in ab.sh reaches this at
 * all. A wrong signature on a `void` function called through a pointer costs
 * nothing at compile time and nothing at run time until somebody plays a
 * network game. Found by reading the callee, which is the only thing that
 * could have found it.
 *
 * THE INITIAL INDEX USES ADDR_MAP_TILES_H AND THE ROW STRIDE USES
 * ADDR_MAP_TILES_W. `H * y0 + x0` starts it and the row advance works out to
 * exactly W, which the loop's own arithmetic proves -- it adds
 * `W - x1 + x0 - 1` after covering `x1 - x0 + 1` cells. Those two agree only
 * on a square map. CLAUDE.md already records that 0x00514DDC carried two
 * names and that width won on three counts; this is a fourth reading and it
 * does not fit either way round. Reproduced exactly as written, because a
 * "fix" here would be inventing an index the game does not use.
 *
 * The block is clamped per axis before the walk, so an object at the map's
 * edge reveals a smaller rectangle rather than wrapping.
 */
void __cdecl ItemPostCreate(int32_t army, uint32_t where)
{
    int32_t tile;
    int32_t x, y, x0, x1, y0, y1;
    int32_t other;

    if (army >= AM2_COMM_SLOTS)
        return;

    tile = TileOfPoint(where) & 0xFFFF;

    x = tile & (*(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W - 1);
    y = tile >> *(const int32_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT;

    x0 = Clamp(x - AM2_REVEAL_RADIUS, 0,
               *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W - 1);
    x1 = Clamp(x + AM2_REVEAL_RADIUS, 0,
               *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W - 1);
    y0 = Clamp(y - AM2_REVEAL_RADIUS, 0,
               *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H - 1);
    y1 = Clamp(y + AM2_REVEAL_RADIUS, 0,
               *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H - 1);

    for (other = 0; other < AM2_COMM_SLOTS; other++) {
        uint8_t *grid =
            ((uint8_t *const *)(uintptr_t)ADDR_TILE_REVEAL_GRIDS)[other];
        int32_t  at;
        int32_t  row;

        if (!ArmiesAllied(army, other))
            continue;

        /* H here and W in the row advance below; see the note above. */
        at = *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_H * y0 + x0;

        for (row = y0; row <= y1; row++) {
            int32_t col;

            for (col = x0; col <= x1; col++) {
                grid[at & 0xFFFF]++;
                at++;
            }

            at += *(const int32_t *)(uintptr_t)ADDR_MAP_TILES_W - x1 + x0 - 1;
        }
    }
}

#define orig_create_item     ((AM2_CreateItemFn)(uintptr_t)ADDR_CREATE_ITEM)
#define orig_create_trooper  ((AM2_CreateTrooperFn)(uintptr_t)ADDR_CREATE_TROOPER)
#define orig_create_vehicle  ((AM2_CreateVehicleFn)(uintptr_t)ADDR_CREATE_VEHICLE)
#define orig_create_weapon   ((AM2_CreateWeaponFn)(uintptr_t)ADDR_CREATE_WEAPON)

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
        /* ARMY first, not the object -- see ItemPostCreate above. */
        ItemPostCreate(army, *(const uint32_t *)(m + MSG_CREATE_OFF_A));
        if (!AllyFlag(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER, army))
            ObjConceal(made, 1);
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

/* 0x0042A880, four callers -- the only sender of the damage message, and the
 * far end of RecvDamage above. Twenty bytes.
 *
 * READING IT IS WHAT CORRECTED RecvDamage'S COMMENT. The position field is
 * filled from the caller's fifth argument, and all four callers pass the
 * VICTIM's own position, so what travels is where the SENDER thinks the
 * victim is -- not where the attacker is, which is what the receiver's
 * arithmetic had led me to write.
 *
 * The SIXTH argument is never read. The two callers in item.cpp pass 0 and
 * the two in the image pass their own value; nothing in these 176 bytes
 * touches it. Kept in the signature because every call site has it.
 *
 * Both uids go through UidOnWire and the position is copied FIELD BY FIELD
 * from the caller's point rather than as a dword -- two int16 loads and two
 * int16 stores. Reproduced that way; it is the same bytes, but a dword copy
 * would assume an alignment the original does not.
 *
 * The trailing log is gated on COMM_OFF_VERBOSE and prints the victim's
 * HEALTH, which is in neither the message nor the arguments -- it is read
 * back off the object, so it is the health AFTER whatever the caller already
 * did to it. */
void __cdecl DamageBroadcast(void *obj, uint32_t attacker, int32_t amount,
                             int32_t kind, const void *where, int32_t unused)
{
    uint8_t        msg[AM2_MSG_DAMAGE_LEN];
    const uint8_t *o = (const uint8_t *)obj;
    const uint8_t *comm;

    (void)unused;

    if (!*(void *const *)(uintptr_t)ADDR_MP_SESSION)
        return;

    *(uint16_t *)(msg + 0) = AM2_MSG_DAMAGE_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_DAMAGE;
    *(uint32_t *)(msg + MSG_DAMAGE_OFF_UID) =
        UidOnWire(*(const uint32_t *)(o + 4));

    *(int16_t *)(msg + MSG_DAMAGE_OFF_AMOUNT) = (int16_t)amount;
    *(uint8_t *)(msg + MSG_DAMAGE_OFF_KIND)   = (uint8_t)kind;

    *(int16_t *)(msg + MSG_DAMAGE_OFF_POS)     =
        *(const int16_t *)where;
    *(int16_t *)(msg + MSG_DAMAGE_OFF_POS + 2) =
        *(const int16_t *)((const uint8_t *)where + 2);

    *(uint32_t *)(msg + MSG_DAMAGE_OFF_ATTACKER) = UidOnWire(attacker);

    ArmyMessageSend(msg);

    comm = kComm;
    if (!*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        return;

    orig_log((const char *)(uintptr_t)ADDR_STR_SEND_DAMAGE,
             *(const uint32_t *)(o + 4), amount,
             (int32_t)*(const int16_t *)(o + OBJ_OFF_HEALTH),
             UidArmy(attacker));
}

/* SendVehicleExit -- original 0x0045E3C0, two callers.
 *
 * Tell the other players a unit has got out of a vehicle. Twelve bytes: the
 * header's uid is the VEHICLE and the dword after it is the occupant.
 *
 * It names itself twice and the pair is the identification: "<--Vehicle Exit
 * Send" before the send and "-->Vehicle Exit Sent" after it, both gated on
 * COMM_OFF_VERBOSE. The arrows are the original's convention, not ours.
 *
 * THE OUTGOING LOG CONVERTS THE UIDS A SECOND TIME, and that is harmless
 * rather than a misreading: UidOnWire is `mov eax, [esp+4]; ret` -- five
 * bytes and the identity, with a hundred callers. So a uid that has already
 * been through it can go through it again and nothing happens. Worth knowing
 * before treating any UidOnWire call as evidence of a conversion.
 *
 * The occupant is tested for null and the vehicle is not, which is the wrong
 * way round for a function whose header uid comes from the VEHICLE: a null
 * vehicle faults in the first log line and again in the send. Its two callers
 * both have one; reproduced as it stands.
 */
void __cdecl SendVehicleExit(void *vehicle, void *occupant)
{
    struct {
        AM2_ArmyMsgHdr hdr;
        uint32_t       occupant;
    } msg;

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_VEH_EXIT_SEND),
                 UidOnWire(((const AM2_Object *)vehicle)->uid),
                 UidOnWire(((const AM2_Object *)occupant)->uid));

    if (!occupant)
        return;

    msg.hdr.len  = AM2_MSG_VEHICLE_EXIT_LEN;
    msg.hdr.kind = AM2_MSG_VEHICLE_EXIT;
    msg.hdr.uid  = UidOnWire(((const AM2_Object *)vehicle)->uid);
    msg.occupant = UidOnWire(((const AM2_Object *)occupant)->uid);

    ArmyMessageSend(&msg);

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_VEH_EXIT_SENT),
                 UidOnWire(msg.hdr.uid), UidOnWire(msg.occupant));
}

int armymsg_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ARMY_MESSAGE_SEND, (const void *)ArmyMessageSend,
                        "ArmyMessageSend", 1);
    rc |= patch_replace(ADDR_VEHICLE_DROP_OCCUPANT, (const void *)SendVehicleExit,
                        "SendVehicleExit", 2);
    rc |= patch_replace(ADDR_ITEM_POST_CREATE, (const void *)ItemPostCreate,
                        "ItemPostCreate", 4);
    rc |= patch_replace(ADDR_DAMAGE_BROADCAST, (const void *)DamageBroadcast,
                        "DamageBroadcast", 4);
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
    rc |= patch_replace(ADDR_TELL_ONE_SLOT, (const void *)TellOneSlot,
                        "TellOneSlot", 1);
    rc |= patch_replace(ADDR_TROOPER_WANT_ITEM_SEND,
                        (const void *)TrooperWantItemSend,
                        "TrooperWantItemSend", 2);
    rc |= patch_replace(ADDR_TROOPER_DROP_ITEM_SEND,
                        (const void *)TrooperDropItemSend,
                        "TrooperDropItemSend", 2);
    rc |= patch_replace(ADDR_ITEM_GONE_SEND, (const void *)ItemGoneMessageSend,
                        "ItemGoneMessageSend", 1);
    rc |= patch_replace(ADDR_SEND_GAME_PAUSE, (const void *)SendGamePause,
                        "SendGamePause", 2);
    rc |= patch_replace(ADDR_SEND_PAIR_MSG, (const void *)SendPairMessage,
                        "SendPairMessage", 5);
    return rc;
}
