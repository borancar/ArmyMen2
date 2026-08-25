/* armymsg.cpp -- see armymsg.h. */
#include <stdint.h>
#include <string.h>

#include "armymsg.h"
#include "image.h"
#include "item.h"          /* UidOnWire, UidArmy */
#include "../inject/orig.h"
#include "../inject/patch.h"

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

int armymsg_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ARMY_MESSAGE_SEND, (const void *)ArmyMessageSend,
                        "ArmyMessageSend", 1);
    rc |= patch_replace(ADDR_SEND_TROOPER_WEAPON,
                        (const void *)SendTrooperSetWeapon,
                        "SendTrooperSetWeapon", 10);
    rc |= patch_replace(ADDR_SEND_GAME_PAUSE, (const void *)SendGamePause,
                        "SendGamePause", 2);
    return rc;
}
