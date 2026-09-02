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
#include "commmsg.h"  /* AppendTroopState -- reconstructed, its receiver too */
#include "dist.h"     /* AngleBetween */
#include "army.h"     /* AllyFlag, SetLeadsAndAct */
#include "packkey.h"
#include "scriptint.h"  /* kScriptNames -- the object name the message carries */
#include "misc.h"
#include "air.h"      /* ObjConceal */
#include "msgslot.h"  /* FindPlayerById, MsgField12 */

typedef uint32_t (__stdcall *AM2_TickFn)(void);
#define orig_get_tick_count (*(AM2_TickFn *)AM2_IMAGE(ADDR_IAT_GET_TICK_COUNT))

#define kComm       (*(uint8_t **)AM2_IMAGE(ADDR_COMM_OBJECT))
#define kPacket     ((uint8_t *)AM2_IMAGE(ADDR_ARMY_PACKET))
#define kPacketLen  (*(int32_t *)AM2_IMAGE(ADDR_ARMY_PACKET_LEN))

#define kCommField(off) (*(const int32_t *)(kComm + (off)))

/* SendGameMsg is reconstructed and lives in win32/dplay.cpp, which this flat
 * module may not include -- dplay.h names DirectPlay types and
 * tools/checksplit.py refuses a flat module that reaches a Win32 header even
 * transitively. Its own signature names nothing platform, so a declaration
 * here is enough; `extern "C"` because that is how dplay.h declares it. */
extern "C" int32_t __cdecl SendGameMsg(void *msg, int32_t to, int32_t flags);


/* 0x00410420, and it names itself twice -- "ArmyMessageFlush: can't send since
 * no flowq for %x yet" and the "myflow null %d" beside it.
 *
 * The rate limit is the whole function, and every constant in it is written by
 * CommConstruct, which this port already owns: COMM_OFF_COALESCE_MS is 100,
 * COMM_OFF_MAX_HOLD_MS is 1000 and COMM_OFF_BUFFER_DEFAULT is 996 of the
 * 1024-byte COMM_OFF_BUFFER_MAX. So the packet goes out when it is at least as
 * big as the caller asked for AND 100ms have passed, or when a second has
 * passed whatever its size, or when it is within 28 bytes of filling the
 * buffer. That last arm is the one ArmyMessageSend's lookahead aims at.
 *
 * The three comparisons do NOT agree about signedness, and it is reproduced
 * rather than tidied: both size tests are signed and both elapsed tests are
 * unsigned. Unsigned is right for the elapsed ones -- GetTickCount wraps, and
 * the subtraction carries the wrap correctly -- so this is the ordinary shape
 * of a tick comparison rather than a slip.
 *
 * Two zero returns and two ONE returns, which is worth stating because they
 * mean opposite things. Returning 1 for "not joined" or "fewer than two
 * players" tells the caller the packet is dealt with, and ArmyMessageSend
 * spins on the zero -- so a single-player game must answer 1 or the spin in
 * ArmyMessageSend would never end. The rate-limit refusal is the zero that
 * means "not yet".
 *
 * The count is re-read at the bottom of the recipient loop rather than latched,
 * so a player leaving mid-flush shortens it. The sequence is bumped once for
 * the whole flush, not once per recipient.
 *
 * The tail reads field 12 of the message pool and throws the answer away. That
 * is the original's, and MsgField12 really is nothing but that read, so
 * nothing is lost by it -- reproduced because it is a call, and calls are
 * observable. */
int32_t __cdecl ArmyMessageFlush(int32_t least)
{
    uint8_t *me;
    int32_t  payload, seq, i, off;
    uint32_t elapsed;

    if (kCommField(COMM_OFF_JOINED) == 0)
        return 1;

    if ((uint32_t)kCommField(COMM_OFF_PLAYER_COUNT) < 2u)
        return 1;

    payload = kPacketLen - (int32_t)AM2_ARMY_PACKET_HDR;
    elapsed = orig_get_tick_count()
              - (uint32_t)kCommField(COMM_OFF_SEND_STAMP);

    if (!((payload >= least
           && elapsed > (uint32_t)kCommField(COMM_OFF_COALESCE_MS))
          || elapsed > (uint32_t)kCommField(COMM_OFF_MAX_HOLD_MS)
          || payload > kCommField(COMM_OFF_BUFFER_DEFAULT)))
        return 0;

    me = (uint8_t *)FindPlayerById((uint32_t)kCommField(COMM_OFF_OUR_PLAYER_ID));
    if (me == 0) {
        orig_log("myflow null %d\n", kCommField(COMM_OFF_OUR_PLAYER_ID));
        return 0;
    }

    if (*(const int32_t *)(me + FLOW_OFF_READY) == 0)
        return 0;

    seq = *(const int32_t *)(me + FLOW_OFF_SEQUENCE);
    *(int32_t *)AM2_IMAGE(ADDR_ARMY_PACKET_SEQ) = seq;

    for (i = 0, off = 0;
         i < kCommField(COMM_OFF_PLAYER_COUNT);
         i++, off += (int32_t)AM2_COMM_SLOT_STRIDE) {
        int32_t id = *(const int32_t *)(kComm + COMM_OFF_PLAYER_SLOTS + off);

        if (id == kCommField(COMM_OFF_OUR_PLAYER_ID) || id == -1)
            continue;

        if (FindPlayerById((uint32_t)id) == 0) {
            if (kCommField(COMM_OFF_VERBOSE))
                orig_log("ArmyMessageFlush: can't send since no flowq for "
                         "%x yet\n", id);
            continue;
        }

        SendGameMsg(kPacket, id, kCommField(COMM_OFF_SEND_FLAGS));

        /* Re-read rather than reuse `seq`: nothing in the loop bumps it, so
         * this is the original's spelling and not a second value. */
        if ((uint32_t)*(const int32_t *)(me + FLOW_OFF_SEQUENCE) < 5u
            && kCommField(COMM_OFF_VERBOSE))
            orig_log("Sending Flow Packet seq %d to %x \n",
                     *(const int32_t *)(me + FLOW_OFF_SEQUENCE), id);
    }

    *(int32_t *)(me + FLOW_OFF_SEQUENCE) += 1;
    *(int32_t *)(kComm + COMM_OFF_SEND_STAMP) = (int32_t)orig_get_tick_count();
    kPacketLen = (int32_t)AM2_ARMY_PACKET_HDR;
    (void)MsgField12((const void *)AM2_IMAGE(ADDR_MSG_LIST_POOL));
    return 1;
}

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
                ArmyMessageFlush(0);

            return;
        }

        while (!ArmyMessageFlush(0))
            orig_log("Send Over Flow, Couldn't Empty Buffer \n");

        if (kCommField(COMM_OFF_DPLAY) == 0)
            return;
    }
}

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

    SendGameMsg(msg, 0, 1);

    if (kCommField(COMM_OFF_VERBOSE))
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

/* AppendTroopState is ours now; the seam and its typedef went with it. What
 * this comment used to say -- "none of them read here" -- was true and is why
 * the record's length was a guess: it is 5 bytes at least and 9 at most. */

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
 * IT REACHED ADDR_ARMY_OBJ_LISTS THROUGH TWO DEREFERENCES AND THE ORIGINAL
 * USES ONE. Both sites here read `mem[mem[0x4F9ECC] + slot*4]` where
 * `mov eax, [ebp*4 + 0x4F9ECC]` reads `mem[0x4F9ECC + slot*4]` -- a different
 * pointer entirely, then dereferenced for its count. event.cpp spells the
 * same table the single way at both of its sites and EvtArmyAtPoint's
 * original agrees at all three of its own, so the table is an array of
 * pointers and not a pointer to one.
 *
 * Nothing could have caught it. This function sends a comm batch, so it needs
 * a live DirectPlay session with a second player; its counter reads 0 in
 * every configuration this machine can drive, including the one just run.
 * Found by grepping for the SHAPE after CreateRoach indexed the same table --
 * two spellings of one access is the tell, and the disassembly is the tie
 * breaker.
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
        ((void *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
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
                AppendTroopState(msg, obj);
            }
            if ((uint32_t)h->len + AM2_TROOP_BATCH_SLACK
                    > AM2_TROOP_BATCH_MAX) {
                ArmyMessageSend(msg);
                h->len = (uint16_t)sizeof(AM2_ArmyMsgHdr);
            }
        }

        list = (const uint8_t *)
            ((void *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
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

/* CreateItem is reconstructed and called by name; its typedef went with it. */
/* CreateTrooper is reconstructed too, and its typedef went with it. */
/* CreateVehicle is reconstructed in item.cpp and declared in item.h. */
/* AM2_CreateWeaponFn moved to item.h -- place.cpp wants it too, and its
 * second argument is an ARMY rather than the type it was called there. */
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

#define orig_create_trooper  CreateTrooper
#define orig_create_weapon   CreateWeapon

/* SendItemCreate -- original 0x0042AB50, FOUR callers, and they are the four
 * creators: CreateItem, CreateTrooper, CreateVehicle and CreateWeapon. So
 * every object the game makes announces itself, and the fourfold split here
 * is the same one RecvItemCreate below dispatches on.
 *
 * A 0x70-byte kind-0x12 message, ZEROED FIRST and then filled in two passes:
 * a common part every type gets, and one arm per type. The zeroing is what
 * makes MSG_CREATE_OFF_E meaningful -- nothing ever writes it, so the
 * receiver's read of that byte always sees 0.
 *
 * THE NAME IS OPTIONAL AND THE EMPTY CASE IS ONE BYTE. A negative name index,
 * or one whose table entry has a null string, writes a single NUL at
 * MSG_CREATE_OFF_NAME rather than clearing the field -- the buffer was zeroed
 * already, so the two are the same thing said twice.
 *
 * MSG_CREATE_OFF_A GETS A DWORD HERE AND IS READ BOTH WAYS. The sender copies
 * the object's whole packed position into it; orig.h records that the
 * receiver takes a WORD for types 2 and 3 and a DWORD for 1 and 4. That is a
 * fact about the reader, not about this, and both notes are right.
 *
 * THE FOUR ARMS DISAGREE ABOUT WHICH FIELDS THEY USE:
 *
 *   - an ITEM puts its type record's +8 in MSG_CREATE_OFF_D;
 *   - a TROOPER puts 0x0A in the SUBTYPE when OBJ_OFF_SARGE is set, and
 *     otherwise the class of the weapon in its first inventory slot -- so
 *     "Sarge" travels as a subtype rather than as a flag;
 *   - a VEHICLE puts its kind in the SUBTYPE and a wire uid in _D;
 *   - a WEAPON puts its type record's first word in the SUBTYPE and
 *     OBJ_OFF_TARGET_UID in _D.
 *
 * So SUBTYPE means four different things and _D means three, which is why
 * neither is named for a meaning.
 *
 * AN UNKNOWN TYPE RETURNS WITHOUT SENDING. The jump table covers 1..4 and the
 * default arm skips the ArmyMessageSend entirely -- it does not send a
 * half-filled message.
 *
 * The whole function is behind ADDR_MP_SESSION, so in single player it is one
 * compare and a return.
 */
void __cdecl SendItemCreate(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t        msg[AM2_MSG_ITEM_CREATE_LEN];
    int32_t        nameidx;
    const char    *name;
    int32_t        type;

    if (!*(const int32_t *)(uintptr_t)ADDR_MP_SESSION)
        return;

    memset(msg, 0, sizeof msg);
    *(uint16_t *)(msg + 0) = AM2_MSG_ITEM_CREATE_LEN;
    *(uint16_t *)(msg + 2) = AM2_MSG_ITEM_CREATE;
    *(uint32_t *)(msg + MSG_CREATE_OFF_UID) =
        UidOnWire(((const AM2_Object *)o)->uid);

    nameidx = *(const int32_t *)(o + AM2_OBJ_NAME_IDX_OFF);
    name = (nameidx >= 0) ? kScriptNames[nameidx].name : (const char *)0;
    if (name)
        strcpy((char *)(msg + MSG_CREATE_OFF_NAME), name);
    else
        *(char *)(msg + MSG_CREATE_OFF_NAME) = '\0';

    type = *(const int32_t *)o;
    *(uint16_t *)(msg + MSG_CREATE_OFF_TYPE) = (uint16_t)type;
    *(uint32_t *)(msg + MSG_CREATE_OFF_A) =
        *(const uint32_t *)(o + OBJ_OFF_POS);
    memcpy(msg + MSG_CREATE_OFF_BLOCK, o + OBJ_OFF_BOX_OFFSETS,
           AM2_OBJ_BOX_BYTES);
    *(uint32_t *)(msg + MSG_CREATE_OFF_C) =
        *(const uint32_t *)(o + OBJ_OFF_FLAGS);

    switch (type) {
    case AM2_OBJ_TYPE_ITEM:
        *(int32_t *)(msg + MSG_CREATE_OFF_D) =
            *(const int32_t *)(*(const uint8_t *const *)
                                   (o + OBJ_OFF_FIELD_94) + 8);
        break;

    case AM2_OBJ_TYPE_TROOPER:
        if (*(const int32_t *)(o + OBJ_OFF_SARGE))
            *(uint16_t *)(msg + MSG_CREATE_OFF_SUBTYPE) =
                AM2_TROOPER_SARGE_SUBTYPE;
        else
            *(uint16_t *)(msg + MSG_CREATE_OFF_SUBTYPE) =
                (uint16_t)WeaponClassOf(
                    *(const uint32_t *)(o + UNIT_OFF_INVENTORY));
        break;

    case AM2_OBJ_TYPE_VEHICLE:
        *(uint16_t *)(msg + MSG_CREATE_OFF_SUBTYPE) =
            *(const uint16_t *)(o + VEHICLE_OFF_KIND);
        *(uint32_t *)(msg + MSG_CREATE_OFF_D) =
            UidOnWire(*(const uint32_t *)(o + VEHICLE_OFF_WEAPON_UID));
        break;

    case AM2_OBJ_TYPE_WEAPON:
        *(uint16_t *)(msg + MSG_CREATE_OFF_SUBTYPE) =
            **(const uint16_t *const *)(o + OBJ_OFF_FIELD_C0);
        *(uint32_t *)(msg + MSG_CREATE_OFF_D) =
            *(const uint32_t *)(o + OBJ_OFF_TARGET_UID);
        break;

    default:
        return;                 /* nothing is sent for an unknown type */
    }

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_SEND_ITEM_CREATE),
                 UidOnWire(*(const uint32_t *)(msg + MSG_CREATE_OFF_UID)),
                 *(const int32_t *)o,
                 (int32_t)*(const int16_t *)(msg + MSG_CREATE_OFF_SUBTYPE));

    ArmyMessageSend(msg);
}

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
        /* The const goes away because ObjInitCommon lower-cases the name in
         * place two levels down -- so this really does rewrite the RECEIVED
         * MESSAGE's own bytes, which is what the original does. */
        made = CreateItem((char *)(uintptr_t)name, army,
                          *(const int32_t *)(m + MSG_CREATE_OFF_D),
                          (uint32_t)*(const int32_t *)(m + MSG_CREATE_OFF_A),
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
                   (char *)(uintptr_t)name,
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
        made = CreateVehicle(
                   (int32_t)*(const int16_t *)(m + MSG_CREATE_OFF_SUBTYPE),
                   (char *)(uintptr_t)name,
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

/* RecvTrooperWantItem -- original 0x0044C680, one caller, and it NAMES ITSELF:
 * "-->Trooper Want Item Received". The kind-0x19 receiver for
 * TrooperWantItemSend above, and between them they are the whole pickup
 * protocol -- the sender's comment already sets out the four request values
 * and says one of its own callers is this function, turning a WANT into a DO
 * and sending it back. This is that call.
 *
 * THE FIRST TEST IS WHICH SIDE YOU ARE ON, and it is the protocol in two
 * lines. A host drops a DO -- those are its own answers coming back -- and a
 * client drops a WANT, because asking is not its job. Everything after runs
 * only on the machine the message was meant for.
 *
 * THE SECOND IS WHOSE TROOPER IT IS: a DO is applied only where
 * CommMustBroadcast accepts the trooper's army, and a WANT is not gated at
 * all, because the host answers for everyone.
 *
 * THE ITEM MAY ALREADY BE GONE, and there is one arm for that. If the uid no
 * longer resolves, the trooper's own slot is checked instead: a weapon there
 * that is already OBJ_FLAG_OVERDUE, on a DO_DROP, means the drop happened
 * before the message arrived -- so the slot is emptied and the log says "but
 * we handled it". Any other combination is dropped silently.
 *
 * DO_PICKUP RE-ASKS. It does not trust the host's answer: CanPickUpWeapon
 * runs again locally and the pickup only happens if it agrees. The item's
 * OBJ_OFF_PICKUP_AFTER is set to the clock before the test and to the clock
 * plus AM2_PICKUP_HOLD_MS after it, whichever way the test went -- so a
 * refused pickup still holds the item off for two seconds.
 *
 * CanPickUpWeapon's THIRD argument is the ARGUMENT SLOT of this function,
 * reused as an out parameter, and the slot it writes there is what both
 * TrooperHostApprovedPickupItem and the broadcast are given. Its FOURTH is a
 * local zeroed before the call and never read after.
 *
 * WANT_PICKUP IS THE HOST'S DECISION and the only arm that answers. The item
 * must be AM2_ARMY_NEUTRAL and have ammo; the amount granted is the smaller of
 * what was asked and what is there; and it is taken off the item only when the
 * trooper's slot already holds something that is NOT a KindInSetB weapon --
 * so topping up ammo deducts and swapping a weapon does not. Then
 * TrooperWantItemSend goes back out as AM2_DO_PICKUP.
 *
 * ITS TWO WANT_PICKUP LOGS ARE THE ONLY UNGATED ONES. Every other message in
 * this function is behind COMM_OFF_VERBOSE; "OKed" and "denied" are not, so
 * the host's decision is always on the record.
 *
 * WANT_DROP DOES NOTHING BUT LOG, which is worth saying plainly: the arm
 * exists, it is reached, and the drop itself must happen somewhere else.
 */
void __cdecl RecvTrooperWantItem(void *msg)
{
    const uint8_t *m = (const uint8_t *)msg;
    int32_t        request = *(const int32_t *)(m + MSG_DROP_OFF_REQUEST);
    int32_t        slot;                 /* the original's own argument slot */
    int32_t        unused = 0;
    uint8_t       *t;
    uint8_t       *w;

    if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_WANT_RECV_HDR),
                 UidOnWire(*(const uint32_t *)(m + MSG_DROP_OFF_TROOPER)),
                 UidOnWire(*(const uint32_t *)(m + MSG_DROP_OFF_ITEM)),
                 request,
                 *(const int32_t *)(m + MSG_DROP_OFF_SLOT),
                 *(const int32_t *)(m + MSG_DROP_OFF_QUANT));

    if (*(const int32_t *)(kComm + COMM_OFF_IS_HOST)) {
        if (request == AM2_DO_PICKUP || request == AM2_DO_DROP)
            return;
    } else {
        if (request == AM2_WANT_PICKUP || request == AM2_WANT_DROP)
            return;
    }

    t = (uint8_t *)ObjByUidAlias(
        UidOnWire(*(const uint32_t *)(m + MSG_DROP_OFF_TROOPER)));
    if (!t)
        return;

    if ((request == AM2_DO_PICKUP || request == AM2_DO_DROP)
        && !CommMustBroadcast((void *)kComm,
                              (int16_t)*(const int8_t *)(t + OBJ_OFF_ARMY)))
        return;

    w = (uint8_t *)LookupByUID(
        UidOnWire(*(const uint32_t *)(m + MSG_DROP_OFF_ITEM)));

    if (!w) {
        /* The item is gone. One combination is still meaningful. */
        uint8_t *held = (uint8_t *)WeaponByUid(
            *(const uint32_t *)(t + OBJ_OFF_WEAPON_UID
                                + (uint32_t)*(const int32_t *)
                                      (m + MSG_DROP_OFF_SLOT) * 4));

        if (!held)
            return;
        if (!(*(const uint32_t *)(held + OBJ_OFF_FLAGS) & OBJ_FLAG_OVERDUE))
            return;
        if (request != AM2_DO_DROP)
            return;

        RemoveInventoryItem(t,
                            *(const int32_t *)(m + MSG_DROP_OFF_SLOT));

        if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_RECV_DROP_GONE));
        return;
    }

    if (!ObjIsType4((const AM2_Object *)w))
        return;

    if (request == AM2_DO_PICKUP) {
        if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_TELL_PICKUP),
                     *(const int32_t *)(m + MSG_DROP_OFF_QUANT));

        *(uint32_t *)(w + OBJ_OFF_PICKUP_AFTER) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

        if (CanPickUpWeapon(w, t, &slot, &unused)) {
            TrooperHostApprovedPickupItem(
                t, w, slot, *(const int32_t *)(m + MSG_DROP_OFF_QUANT));
            SendPairMessage(t, w, slot,
                            *(const int32_t *)(m + MSG_DROP_OFF_QUANT));
        }

        *(uint32_t *)(w + OBJ_OFF_PICKUP_AFTER) =
            *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
            + AM2_PICKUP_HOLD_MS;
        return;
    }

    if (request == AM2_DO_DROP) {
        if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_TELL_DROP),
                     *(const int32_t *)(m + MSG_DROP_OFF_QUANT));

        /* NOT the same sense as the DO gate above: here a trooper we would
         * broadcast for is one we must NOT drop for. */
        if (CommMustBroadcast((void *)kComm,
                              (int16_t)*(const int8_t *)(t + OBJ_OFF_ARMY)))
            return;

        TrooperDropItem(t, *(const int32_t *)(m + MSG_DROP_OFF_SLOT),
                        *(const uint32_t *)(m + MSG_DROP_OFF_AT));
        return;
    }

    if (request == AM2_WANT_PICKUP) {
        int32_t have = *(const int32_t *)(w + OBJ_OFF_TARGET_UID);
        int32_t give;
        int32_t at;

        if (*(const int8_t *)(w + OBJ_OFF_ARMY) != AM2_ARMY_NEUTRAL || !have) {
            orig_log((const char *)AM2_IMAGE(ADDR_STR_REQ_PICKUP_DENY));
            return;
        }

        give = *(const int32_t *)(m + MSG_DROP_OFF_QUANT);
        if (give >= have)
            give = have;

        at = *(const int32_t *)(m + MSG_DROP_OFF_SLOT);
        if (at != -1
            && *(const uint32_t *)(t + OBJ_OFF_WEAPON_UID + (uint32_t)at * 4)
            && !KindInSetB(**(const int32_t *const *)(w + OBJ_OFF_FIELD_C0)))
            *(int32_t *)(w + OBJ_OFF_TARGET_UID) -= give;

        TrooperWantItemSend(t, w, AM2_DO_PICKUP,
                            (int8_t)*(const int32_t *)(m + MSG_DROP_OFF_SLOT),
                            give);
        orig_log((const char *)AM2_IMAGE(ADDR_STR_REQ_PICKUP_OK), give);
        return;
    }

    if (request == AM2_WANT_DROP) {
        if (*(const int32_t *)(kComm + COMM_OFF_VERBOSE))
            orig_log((const char *)AM2_IMAGE(ADDR_STR_REQ_DROP),
                     *(const int32_t *)(m + MSG_DROP_OFF_QUANT));
    }
}

int armymsg_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ARMY_MESSAGE_FLUSH, (const void *)ArmyMessageFlush,
                        "ArmyMessageFlush", 1);
    rc |= patch_replace(ADDR_ARMY_MESSAGE_SEND, (const void *)ArmyMessageSend,
                        "ArmyMessageSend", 1);
    rc |= patch_replace(ADDR_VEHICLE_DROP_OCCUPANT, (const void *)SendVehicleExit,
                        "SendVehicleExit", 2);
    rc |= patch_replace(ADDR_ITEM_POST_CREATE, (const void *)ItemPostCreate,
                        "ItemPostCreate", 4);
    rc |= patch_replace(ADDR_DAMAGE_BROADCAST, (const void *)DamageBroadcast,
                        "DamageBroadcast", 4);
    rc |= patch_replace(ADDR_SEND_ITEM_CREATE, (const void *)SendItemCreate,
                        "SendItemCreate", 4);
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
    rc |= patch_replace(ADDR_RECV_TROOPER_WANT_ITEM,
                        (const void *)RecvTrooperWantItem,
                        "RecvTrooperWantItem", 1);
    rc |= patch_replace(ADDR_SEND_PAIR_MSG, (const void *)SendPairMessage,
                        "SendPairMessage", 5);
    rc |= patch_replace(ADDR_SEND_VEHICLE_ENTER, (const void *)SendVehicleEnter,
                        "SendVehicleEnter", 2);
    rc |= patch_replace(ADDR_SEND_VEHICLE_FIRE, (const void *)SendVehicleFire,
                        "SendVehicleFire", 1);
    rc |= patch_replace(ADDR_VEHICLE_UPDATE_APPEND,
                        (const void *)VehicleUpdateAppend,
                        "VehicleUpdateAppend", 2);
    rc |= patch_replace(ADDR_SEND_VEHICLE_UPDATES,
                        (const void *)SendVehicleUpdates,
                        "SendVehicleUpdates", 1);
    rc |= patch_replace(ADDR_SEND_ALL_VEH_UPDATES,
                        (const void *)SendAllVehicleUpdates,
                        "SendAllVehicleUpdates", 0);
    rc |= patch_replace(ADDR_SEND_VEHICLE_WANT_ITEM,
                        (const void *)SendVehicleWantItem,
                        "SendVehicleWantItem", 5);
    return rc;
}

/* SendVehicleEnter -- original 0x0045E300, one caller. Message kind 0x24,
 * twelve bytes, and it names itself twice: "<--Vehicle Enter Send" before the
 * send and "-->Vehicle Enter Sent" after it.
 *
 * THE UNIT IS CHECKED AND THE VEHICLE IS NOT. The null test before the send
 * is on argument 2, so a null vehicle would be dereferenced building the
 * header. Reproduced -- and it is the reason the argument order matters here
 * rather than being cosmetic, which orig.h fixes from the first log's push
 * sequence: `Vehicle: %x` takes argument 1.
 *
 * IT LOGS THE SAME TWO UIDS TWICE, once from the objects and once back out of
 * the message it just built, with UidOnWire applied a SECOND time to values
 * that already went through it. That is the identity on every drive this
 * project has -- armymsg.cpp records why -- so the double application is
 * harmless and is reproduced because the original does it, not because
 * anything is converted. */
void __cdecl SendVehicleEnter(void *vehicle, void *unit)
{
    uint8_t  *veh  = (uint8_t *)vehicle;
    uint8_t  *unt  = (uint8_t *)unit;
    uint8_t  *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    struct { AM2_ArmyMsgHdr hdr; uint32_t unitUid; } msg;

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_VEHICLE_ENTER_SEND),
                UidOnWire(*(const uint32_t *)(veh + OBJ_OFF_UID)),
                UidOnWire(*(const uint32_t *)(unt + OBJ_OFF_UID)));

    if (unt == 0)
        return;

    msg.hdr.len  = AM2_MSG_VEHICLE_ENTER_LEN;
    msg.hdr.kind = AM2_MSG_VEHICLE_ENTER;
    msg.hdr.uid  = UidOnWire(*(const uint32_t *)(veh + OBJ_OFF_UID));
    msg.unitUid  = UidOnWire(*(const uint32_t *)(unt + OBJ_OFF_UID));
    ArmyMessageSend(&msg);

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_VEHICLE_ENTER_SENT),
                UidOnWire(msg.hdr.uid), UidOnWire(msg.unitUid));
}

/* SendVehicleFire -- original 0x0045E220, one caller: Step3Drive, once a shot
 * has been taken. Message kind 0x1C, twenty-four bytes, and it names itself
 * "Vehicle Fire Send, vehicle: %x,  gunface:%d, pos (%d,%d,%d),  globTarg %d".
 *
 * THAT LOG LINE IS WHAT NAMES THE FIELDS, and they turn out to be named
 * already: the three `pos` words are UNIT_OFF_FIRE_X/Y/Z and `globTarg` is
 * UNIT_OFF_FIRE_UID -- the same storage a TROOPER fires through, at the same
 * displacements. Grepping the offsets before inventing a VEHICLE_OFF_FIRE_*
 * family is what caught that; the ratchet could not have, because a new
 * prefix has nothing to compare against.
 *
 * IT IS GATED ON COMM_OFF_DPLAY, not on a session being joined -- the
 * DirectPlay object merely existing is enough to build and queue the
 * message, and ArmyMessageSend does the rest of the refusing.
 *
 * AND IT CLEARS UNIT_OFF_FIRE_ACTIVE AS A SIDE EFFECT, between building the
 * message and sending it. So the send is also the acknowledgement: whoever
 * set the flag to ask for a shot has it taken away here, and a reconstruction
 * that only emitted the packet would fire every frame. */
void __cdecl SendVehicleFire(void *vehicle)
{
    uint8_t  *veh  = (uint8_t *)vehicle;
    uint8_t  *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    struct {
        AM2_ArmyMsgHdr hdr;
        uint32_t       target;
        int16_t        x, y, z;
        uint16_t       gun;
        uint8_t        seed;
        uint8_t        pad[3];
    } msg;

    if (*(const void *const *)(comm + COMM_OFF_DPLAY) == 0)
        return;

    msg.hdr.len  = 0x18;
    msg.hdr.kind = 0x1C;
    msg.hdr.uid  = UidOnWire(*(const uint32_t *)(veh + OBJ_OFF_UID));
    msg.target   = UidOnWire(*(const uint32_t *)(veh + UNIT_OFF_FIRE_UID));
    msg.x        = *(const int16_t *)(veh + UNIT_OFF_FIRE_X);
    msg.y        = *(const int16_t *)(veh + UNIT_OFF_FIRE_Y);
    msg.z        = *(const int16_t *)(veh + UNIT_OFF_FIRE_Z);
    msg.gun      = *(const uint8_t *)(veh + OBJ_OFF_FIELD_530);
    /* +0x529 is TROOPER_OFF_FIRE_FLAG for a type-2 record; on a vehicle
     * Step3Drive writes a fresh GameRand byte there before each shot, so it
     * travels as a spread seed. Raw, because the existing name asserts a
     * meaning this use contradicts. */
    msg.seed     = *(const uint8_t *)(veh + 0x529u);

    *(int32_t *)(veh + UNIT_OFF_FIRE_ACTIVE) = 0;
    ArmyMessageSend(&msg);

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_VEHICLE_FIRE_SEND),
                *(const uint32_t *)(veh + OBJ_OFF_UID),
                (uint32_t)*(const uint8_t *)(veh + OBJ_OFF_FIELD_530),
                (int32_t)msg.x, (int32_t)msg.y, (int32_t)msg.z,
                msg.target);
}

/* VehicleUpdateAppend -- original 0x0045DAA0, 1136 bytes, one caller. The
 * program's own name for it is in the message it logs: "<--
 * vehicleUpdateMessageAppend: id: %x, Change:%d%d%d, Pos/last (%d,%d)/(%d,%d),
 * Facing/last:%d/%d, Gunfacing/last:%d/%d, intFacing/last:%d/%d,
 * intGunfacing/last:%d/%d, action/last:%x/%x, seq:%d".
 *
 * The vehicle's answer to AppendTroopState above, and NOT the same function
 * written twice: 344 instructions against 201, and normalised disassembly
 * puts their similarity at 0.191 with only the seven-instruction epilogue in
 * common. What they share is a DESIGN -- a (value, sequence) shadow pair per
 * field, a fine interval and a coarse one, and the twelve-bit nibble merge --
 * so this is a sibling to read alongside, not a body to factor out. Reading
 * it is what exposed the length defect fixed in AppendTroopState.
 *
 * THREE FIELDS EACH SEND TWO BYTES, which is why six shadow pairs produce
 * only three change bits: bit 31 carries the position AND the action byte,
 * bit 30 the hull facing AND the turret facing, bit 29 both interpolation
 * bytes. Counting the pairs predicts six bits and there are three.
 *
 * THE THIRD TEST IN EACH PAIR COMPARES ACROSS THE PAIRS, and it is systematic
 * rather than a slip. The hull facing is checked against the INTFACING shadow
 * at 0x5C4, and the turret facing against the INTGUN shadow at 0x5CC -- not
 * against their own. Both arms do it, in the same shape, which is what argues
 * against a copy-paste error: the receiver interpolates these, so the sender
 * is asking whether the value it last interpolated from has drifted from the
 * real one. Reproduced as written either way; the reading of WHY is offered,
 * the behaviour is transcribed.
 *
 * The argument slot is reused, and mis-reading it is exactly what cost the
 * sibling its length: at 0x0045DDE1 the cursor's START goes into `msg`'s slot,
 * which the function has already copied into ebx, and 0x0045DEFD subtracts
 * that -- so the buffer's length gains what THIS call wrote and not where the
 * cursor happened to end. */
void __cdecl VehicleUpdateAppend(void *msg, void *obj)
{
    uint8_t  *o = (uint8_t *)obj;
    uint8_t  *m = (uint8_t *)msg;
    uint8_t  *comm;
    uint8_t  *out, *outStart;
    uint32_t  seq, flags = 0, interval, coarse;
    int32_t   dx, dz;
    uint8_t   action;

    if (*(const int16_t *)(o + OBJ_OFF_COUNT62) == 0)
        return;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    seq = *(const uint32_t *)((const uint8_t *)FindPlayerById(
              *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID))
              + FLOW_OFF_SEQUENCE);
    coarse = *(const uint32_t *)(comm + COMM_OFF_SEND_COARSE);

    dx = *(const int16_t *)(o + OBJ_OFF_POS)
         - *(const int16_t *)(o + VEHICLE_OFF_SENT_POS);
    dz = *(const int16_t *)(o + OBJ_OFF_POS + 2)
         - *(const int16_t *)(o + VEHICLE_OFF_SENT_POS + 2);
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;

    /* Sarge's vehicle is sent every frame it moves, the same forcing
     * AppendTroopState applies to Sarge himself. */
    interval = *(const uint32_t *)(comm + COMM_OFF_SEND_INTERVAL);
    if (ListFirstField548(o) && interval > 1)
        interval = 1;

    {
        uint32_t age = seq - *(const uint32_t *)(o + VEHICLE_OFF_SENT_POS_SEQ);

        if (age >= interval
            && (*(const int16_t *)(o + OBJ_OFF_POS)
                    != *(const int16_t *)(o + VEHICLE_OFF_SENT_POS)
                || *(const int16_t *)(o + OBJ_OFF_POS + 2)
                    != *(const int16_t *)(o + VEHICLE_OFF_SENT_POS + 2)))
            flags = 0x80000000u;
        else if (age >= coarse && (dx > 8 || dz > 8))
            flags = 0x80000000u;
    }

    /* +0x544 is the vehicle KIND -- the same field Step3Drive dispatches its
     * engine sounds on -- with three state bits laid over the top. */
    action = *(const uint8_t *)(o + 0x544u);
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_584))
        action |= 0x80u;
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_588))
        action |= 0x40u;
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_58C))
        action |= 0x20u;

    /* The action rides bit 31 with the position, and is tested ONLY against
     * the coarse interval -- there is no fine arm for it. */
    if (seq - *(const uint32_t *)(o + VEHICLE_OFF_SENT_ACTION_SEQ) >= coarse
        && action != *(const uint8_t *)(o + VEHICLE_OFF_SENT_ACTION))
        flags |= 0x80000000u;

    {
        uint32_t age = seq - *(const uint32_t *)(o + VEHICLE_OFF_SENT_FACING_SEQ);
        uint8_t  f  = *(const uint8_t *)(o + OBJ_OFF_FACING);
        uint8_t  lf = *(const uint8_t *)(o + VEHICLE_OFF_SENT_FACING);

        if ((age >= interval && f != lf)
            || (age >= coarse && (((uint32_t)lf ^ (uint32_t)f) & 0xFFFFFFF0u))
            || (age >= interval
                && f != *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTFACING)))
            flags |= 0x40000000u;
    }

    {
        uint32_t age = seq - *(const uint32_t *)(o + VEHICLE_OFF_SENT_GUN_SEQ);
        uint8_t  g  = *(const uint8_t *)(o + OBJ_OFF_FIELD_530);
        uint8_t  lg = *(const uint8_t *)(o + VEHICLE_OFF_SENT_GUN);

        if ((age >= interval && g != lg)
            || (age >= coarse && (((uint32_t)lg ^ (uint32_t)g) & 0xFFFFFFF0u))
            || (age >= interval
                && g != *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTGUN)))
            flags |= 0x40000000u;
    }

    {
        uint32_t age = seq - *(const uint32_t *)
                           (o + VEHICLE_OFF_SENT_INTFACING_SEQ);
        uint8_t  v  = *(const uint8_t *)(o + OBJ_OFF_FIELD_578);
        uint8_t  lv = *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTFACING);

        if ((age >= interval && v != lv)
            || (age >= coarse && (((uint32_t)lv ^ (uint32_t)v) & 0xFFFFFFF0u)))
            flags |= 0x20000000u;
    }

    {
        uint32_t age = seq - *(const uint32_t *)
                           (o + VEHICLE_OFF_SENT_INTGUN_SEQ);
        uint8_t  v  = *(const uint8_t *)(o + OBJ_OFF_FACING_COPY2);
        uint8_t  lv = *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTGUN);

        if ((age >= interval && v != lv)
            || (age >= coarse && (((uint32_t)lv ^ (uint32_t)v) & 0xFFFFFFF0u)))
            flags |= 0x20000000u;
    }

    if (!flags)
        return;

    /* COMM_OFF_VERBOSE gates it, and unlike the bare call in Step3Drive this
     * one carries its format string, so it is reproduced. */
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log((const char *)(uintptr_t)AM2_IMAGE(
                    ADDR_STR_VEHICLE_UPDATE_APPEND),
            *(const uint32_t *)(o + OBJ_OFF_UID),
            (flags >> 31) & 1u, (flags >> 30) & 1u, (flags >> 29) & 1u,
            *(const int16_t *)(o + OBJ_OFF_POS),
            *(const int16_t *)(o + OBJ_OFF_POS + 2),
            *(const int16_t *)(o + VEHICLE_OFF_SENT_POS),
            *(const int16_t *)(o + VEHICLE_OFF_SENT_POS + 2),
            *(const uint8_t *)(o + OBJ_OFF_FACING),
            *(const uint8_t *)(o + VEHICLE_OFF_SENT_FACING),
            *(const uint8_t *)(o + OBJ_OFF_FIELD_530),
            *(const uint8_t *)(o + VEHICLE_OFF_SENT_GUN),
            *(const uint8_t *)(o + OBJ_OFF_FIELD_578),
            *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTFACING),
            *(const uint8_t *)(o + OBJ_OFF_FACING_COPY2),
            *(const uint8_t *)(o + VEHICLE_OFF_SENT_INTGUN),
            (uint32_t)action,
            (uint32_t)*(const uint8_t *)(o + VEHICLE_OFF_SENT_ACTION),
            seq);

    out = m + *(const uint16_t *)m;
    outStart = out;

    {
        uint32_t head = (UidOnWire(*(const uint32_t *)(o + OBJ_OFF_UID))
                         & 0x1FFFFFFFu) | flags;

        *out++ = (uint8_t)(head >> 24);
        *out++ = (uint8_t)(head >> 16);
        *out++ = (uint8_t)(head >> 8);
        *out++ = (uint8_t)head;
    }

    if (flags & 0x80000000u) {
        int16_t x, z;

        /* Clamped IN THE OBJECT, not just on the wire -- the same as the
         * trooper's, and the reason twelve bits is enough below. */
        if (*(const int16_t *)(o + OBJ_OFF_POS) > 0xFFF)
            *(int16_t *)(o + OBJ_OFF_POS) = 0xFFF;
        if (*(const int16_t *)(o + OBJ_OFF_POS + 2) > 0xFFF)
            *(int16_t *)(o + OBJ_OFF_POS + 2) = 0xFFF;

        x = *(const int16_t *)(o + OBJ_OFF_POS);
        z = *(const int16_t *)(o + OBJ_OFF_POS + 2);
        *out++ = (uint8_t)x;
        *out++ = (uint8_t)z;
        *out++ = (uint8_t)(((((uint8_t)(x >> 8)) ^ ((uint8_t)(z >> 4))) & 0x0F)
                           ^ ((uint8_t)(z >> 4)));

        *(uint32_t *)(o + VEHICLE_OFF_SENT_POS) =
            *(const uint32_t *)(o + OBJ_OFF_POS);
        *(uint32_t *)(o + VEHICLE_OFF_SENT_POS_SEQ) = seq;

        *out++ = action;
        *(uint8_t *)(o + VEHICLE_OFF_SENT_ACTION) = action;
        *(uint32_t *)(o + VEHICLE_OFF_SENT_ACTION_SEQ) = seq;
    }

    if (flags & 0x40000000u) {
        *out++ = *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(uint8_t *)(o + VEHICLE_OFF_SENT_FACING) =
            *(const uint8_t *)(o + OBJ_OFF_FACING);
        *(uint32_t *)(o + VEHICLE_OFF_SENT_FACING_SEQ) = seq;

        *out++ = *(const uint8_t *)(o + OBJ_OFF_FIELD_530);
        *(uint8_t *)(o + VEHICLE_OFF_SENT_GUN) =
            *(const uint8_t *)(o + OBJ_OFF_FIELD_530);
        *(uint32_t *)(o + VEHICLE_OFF_SENT_GUN_SEQ) = seq;
    }

    if (flags & 0x20000000u) {
        *out++ = *(const uint8_t *)(o + OBJ_OFF_FIELD_578);
        *(uint8_t *)(o + VEHICLE_OFF_SENT_INTFACING) =
            *(const uint8_t *)(o + OBJ_OFF_FIELD_578);
        *(uint32_t *)(o + VEHICLE_OFF_SENT_INTFACING_SEQ) = seq;

        *out++ = *(const uint8_t *)(o + OBJ_OFF_FACING_COPY2);
        *(uint8_t *)(o + VEHICLE_OFF_SENT_INTGUN) =
            *(const uint8_t *)(o + OBJ_OFF_FACING_COPY2);
        *(uint32_t *)(o + VEHICLE_OFF_SENT_INTGUN_SEQ) = seq;
    }

    *(uint16_t *)m += (uint16_t)(out - outStart);
}

/* SendVehicleUpdates -- original 0x0045E480, one caller. The vehicle batch
 * sender: walk one comm slot's object list, append a delta for every live
 * vehicle, and flush whenever the next record might not fit.
 *
 * IT IS TellOneSlot COMPILED A SECOND TIME. Sixty-four instructions each,
 * instruction for instruction, and normalising the immediates makes them
 * identical -- similarity 1.000. Diffing the operands gives exactly THREE
 * real differences, and every other line that moved is a branch target
 * relocated by the address change:
 *
 *     kind        AM2_MSG_TROOP_BATCH (0x16)  ->  0x1B
 *     predicate   ObjIsType2                  ->  ObjIsType3
 *     appender    AppendTroopState            ->  VehicleUpdateAppend
 *
 * DELIBERATELY NOT MERGED. A helper taking (slot, kind, predicate, appender)
 * would be faithful today -- the three differences are enumerated above and
 * there is provably nothing else -- and this project has already paid for
 * exactly that tidiness once, in VehicleBlockWeight, where two loops that
 * read as one differed in five ways and a merge would have lost all five.
 * The image has two functions; so does this. What the diff buys is not a
 * factoring, it is the certainty that the buffer size, the flush headroom,
 * the uid latch and the null-object arm need no separate reading.
 *
 * THE SIZE CONSTANTS ARE SHARED AND THAT IS THE IMAGE'S DOING, not ours:
 * both use a 0x12C buffer and the same ten bytes of headroom, so
 * AM2_TROOP_BATCH_MAX and _SLACK are reused rather than duplicated under a
 * vehicle name. Two concepts that genuinely are one number.
 *
 * The list pointer is RE-READ every iteration, which is the original's and is
 * why it is written that way here: appending can register or free objects, so
 * the count is not loop-invariant. Reading it once would be a faster function
 * and a different one. */
void __cdecl SendVehicleUpdates(int32_t slot)
{
    uint8_t         msg[AM2_TROOP_BATCH_MAX];
    AM2_ArmyMsgHdr *h = (AM2_ArmyMsgHdr *)msg;
    const uint8_t  *list;
    int32_t         i = 0;

    h->len  = (uint16_t)sizeof(AM2_ArmyMsgHdr);
    h->kind = AM2_MSG_VEHICLE_BATCH;
    h->uid  = 0;

    list = (const uint8_t *)
        ((void *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
    if (*(const int32_t *)(list + LIST_OFF_COUNT) <= 0)
        return;

    do {
        uint8_t *obj = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(list + LIST_OFF_UIDS))[i]);

        if (obj) {
            if (!(*(const uint8_t *)(obj + OBJ_OFF_FLAGS) & OBJ_FLAG_DESTROYED)
                && ObjIsType3((const AM2_Object *)obj)) {
                if (h->uid == 0)
                    h->uid = *(const uint32_t *)(obj + OBJ_OFF_UID);
                VehicleUpdateAppend(msg, obj);
            }
            if ((uint32_t)h->len + AM2_TROOP_BATCH_SLACK
                    > AM2_TROOP_BATCH_MAX) {
                ArmyMessageSend(msg);
                h->len = (uint16_t)sizeof(AM2_ArmyMsgHdr);
            }
        }

        list = (const uint8_t *)
            ((void *const *)AM2_IMAGE(ADDR_ARMY_OBJ_LISTS))[slot];
        i++;
    } while (i < *(const int32_t *)(list + LIST_OFF_COUNT));

    if (h->len != sizeof(AM2_ArmyMsgHdr))
        ArmyMessageSend(msg);
}

/* SendAllVehicleUpdates -- original 0x0045E550, one caller: CommSyncCheck.
 * Ask every comm slot in turn to send its vehicle batch.
 *
 * FOUR SLOTS, AND THE BOUND IS A BYTE COUNT RATHER THAN A COUNT. The loop
 * runs `esi` from 0 to 0x1C0 in steps of COMM_PLAYER_STRIDE, which is four
 * iterations because 4 x 112 is 448 -- so the slot count is implied by the
 * limit and the stride and appears nowhere as a number. Written with the
 * index as the loop variable, since that is what both calls inside take;
 * reading the original's byte offset as the argument would pass 0x70 where a
 * slot number belongs.
 *
 * THE COMM POINTER IS RE-READ EVERY ITERATION, and not for the field: it is
 * reloaded into ecx immediately before the thiscall to CommMustBroadcast,
 * which needs it there. One load serving two purposes, which is why it sits
 * inside the loop rather than above it.
 *
 * The gate is the player record's +0x50. That offset has no name in this tree
 * and gets none here: the record is reached as COMM_OFF_PLAYERS plus a
 * stride, so 0x25C in the disassembly is 0x20C + 0x50, and what the field
 * MEANS is not established by a single non-zero test. */
void __cdecl SendAllVehicleUpdates(void)
{
    int32_t slot;

    for (slot = 0; slot < 4; slot++) {
        const uint8_t *comm = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;

        if (*(const int32_t *)(comm + COMM_OFF_PLAYERS
                               + slot * COMM_PLAYER_STRIDE + 0x50) == 0)
            continue;
        if (!CommMustBroadcast((void *)comm, (int16_t)slot))
            continue;
        SendVehicleUpdates(slot);
    }
}

/* SendVehicleWantItem -- original 0x0045E0D0, and this project named it
 * earlier from its own log: "<--Vehicle Want Item Send: Vehicle: %x, item:
 * %x, request: %d, slot: %d, quant: %d". Message kind 0x1D, 28 bytes, and
 * with RecvVehicle1D the pickup/drop negotiation is ours at both ends.
 *
 * ITS WIRE LAYOUT CONFIRMS THE RECEIVER, READ WEEKS APART IN THE OTHER
 * DIRECTION. request lands at msg+0x10, quant at +0x14 and slot at +0x18 --
 * exactly the three displacements RecvVehicle1D reads, which were taken from
 * that function alone. Two independent routes to one layout.
 *
 * THE ARGUMENT ORDER WAS THE HARD PART AND IS NOT SETTLED HERE EITHER. The
 * prologue loads the five out of order, so orig.h fixes the identity from the
 * LOG's push sequence -- `Vehicle: %x` takes argument 1 -- and RecvVehicle1D's
 * reply call agreed independently by passing (vehicle, item, 2, slot, quant).
 * This function's own body cannot distinguish them: both are objects and both
 * are only read for a uid.
 *
 * `slot` IS SIGN-EXTENDED FROM A BYTE (`movsx eax, bl`) and travels as a full
 * dword, which is why the parameter is int8_t and the field is not.
 *
 * IT SENDS THE ITEM'S POSITION AND NOBODY READS IT. msg+0x0C carries
 * OBJ_OFF_POS off the item, and RecvVehicle1D touches +4, +8, +0x10, +0x14
 * and +0x18 only. Reproduced -- the sender writes it -- and recorded, because
 * a field with no reader is the kind of thing worth knowing before anyone
 * reasons about what this message means.
 *
 * The null check is on the ITEM, not the vehicle, and it sits AFTER the log:
 * a null item is announced and then not sent. */
void __cdecl SendVehicleWantItem(void *vehicle, void *item, int32_t request,
                                 int8_t slot, int32_t quant)
{
    uint8_t  *veh  = (uint8_t *)vehicle;
    uint8_t  *itm  = (uint8_t *)item;
    uint8_t  *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    struct {
        AM2_ArmyMsgHdr hdr;
        uint32_t       itemUid;
        uint32_t       itemPos;
        int32_t        request;
        int32_t        quant;
        int32_t        slot;
    } msg;

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_VEH_WANT_ITEM_SEND),
                UidOnWire(*(const uint32_t *)(veh + OBJ_OFF_UID)),
                UidOnWire(*(const uint32_t *)(itm + OBJ_OFF_UID)),
                request, (int32_t)slot, quant);

    if (itm == 0)
        return;

    msg.hdr.len  = 0x1C;
    msg.hdr.kind = 0x1D;
    msg.hdr.uid  = UidOnWire(*(const uint32_t *)(veh + OBJ_OFF_UID));
    msg.itemUid  = UidOnWire(*(const uint32_t *)(itm + OBJ_OFF_UID));
    msg.itemPos  = *(const uint32_t *)(itm + OBJ_OFF_POS);
    msg.request  = request;
    msg.quant    = quant;
    msg.slot     = (int32_t)slot;
    ArmyMessageSend(&msg);

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE) == 0)
        return;

    /* Four separate `cmp`s in the original, not a table -- and each arm logs
     * the same two values, so what differs between them is only the text. */
    if (request == 0)
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_WANT_PICKUP),
                *(const uint32_t *)(itm + OBJ_OFF_UID), quant);
    else if (request == 1)
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_WANT_DROP),
                *(const uint32_t *)(itm + OBJ_OFF_UID), quant);
    else if (request == 2)
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_DO_PICKUP),
                *(const uint32_t *)(itm + OBJ_OFF_UID), quant);
    else if (request == 3)
        am2_log((const char *)(uintptr_t)AM2_IMAGE(ADDR_STR_DO_DROP),
                *(const uint32_t *)(itm + OBJ_OFF_UID), quant);
}
