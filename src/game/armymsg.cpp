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

int armymsg_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_ARMY_MESSAGE_SEND, (const void *)ArmyMessageSend,
                        "ArmyMessageSend", 1);
    rc |= patch_replace(ADDR_SEND_GAME_PAUSE, (const void *)SendGamePause,
                        "SendGamePause", 2);
    return rc;
}
