/* msgslot.cpp -- the comm object's own bookkeeping: the two message-slot state
 * arrays, the latency ring, and the keyed removal that empties the send queue.
 *
 * The file started as the six slot writers and has grown along the object
 * rather than along any line of ours, which is the right way round: these are
 * all fields of one record, and two of them turned out to be a writer and a
 * reader of the SAME field.
 *
 * Six functions at 0x004032C0..0x004033B0 differing only in which array they
 * write and what value they store. Both arrays hold 120 dwords and are
 * adjacent: 0x420 + 120*4 is exactly 0x600. The index is the caller's sequence
 * number modulo 120, unsigned, so the pair is a sliding window over the last
 * 120 messages with three states each.
 *
 * Named for position, not meaning. What the states are is not established --
 * nothing in the image reads either array, which is itself worth knowing: the
 * six writers are the only references, so this is bookkeeping the retail build
 * never consults. That fits a debug build's message tracking left compiled in,
 * which is the same shape as ADDR_LOG being a bare `ret` here.
 *
 * THE FIRST ARGUMENT IS A PLAYER RECORD, NOT THE COMM OBJECT, and it was
 * spelled `comm` here for as long as nothing had read a caller. SendGameMsg
 * hands MsgSlotA1 the result of FindPlayerById, and ProcessResendQueue does
 * the same for MsgSlotA2 -- and the two offsets are PLAYER_REC_OFF_RING_A and
 * _RING_B, which orig.h had already filed on that record. Three things
 * agreeing, and the parameter name disagreeing with all of them.
 *
 * Their callers are PacketThreadProc and its neighbours, so "message" rather
 * than anything more specific is as far as the evidence goes.
 */
#ifndef AM2_MSGSLOT_H
#define AM2_MSGSLOT_H

#include <stdint.h>
#include "globaltypes.h"   /* AM2_PlayerMsg */

/* One queued message, 0x28 bytes: the node every message list links, and
 * the same record as the 400 packet records at ADDR_PACKET_RECORDS. */
typedef struct AM2_MsgNode {
    struct AM2_MsgNode *prev;   /* +0x00 MSGNODE_OFF_PREV */
    struct AM2_MsgNode *next;   /* +0x04 MSGNODE_OFF_NEXT */
    uint32_t  from;             /* +0x08 MSGNODE_OFF_FROM */
    uint32_t  to;               /* +0x0C MSGNODE_OFF_TO */
    uint32_t  size;             /* +0x10 PACKET_REC_OFF_SIZE */
    uint32_t  key;              /* +0x14 MSGNODE_OFF_KEY PACKET_REC_OFF_KEY */
    uint32_t  flags;            /* +0x18 MSGNODE_OFF_FLAGS PACKET_REC_OFF_FLAGS */
    uint32_t  stamp;            /* +0x1C MSGNODE_OFF_STAMP */
    uint8_t  *body;             /* +0x20 MSGNODE_OFF_BODY PACKET_REC_OFF_DATA, a 0x400 buffer */
    uint32_t  bodyLen;          /* +0x24 MSGNODE_OFF_BODY_LEN */
} AM2_MsgNode;

/* A message list header, 0x10 bytes. */
typedef struct AM2_MsgList {
    void        *mutex;         /* +0x00 MSGLIST_OFF_MUTEX, a HANDLE */
    AM2_MsgNode *head;          /* +0x04 MSGLIST_OFF_HEAD */
    AM2_MsgNode *tail;          /* +0x08 MSGLIST_OFF_TAIL */
    int32_t      count;         /* +0x0C MSGLIST_OFF_COUNT */
} AM2_MsgList;

/* The header every comm packet starts with, 0x14 bytes. */
typedef struct AM2_Packet {
    uint32_t kind;              /* +0x00 COMMMSG_OFF_KIND: AM2_FLOW_DATA, _NACK, _PULSE_ACK */
    uint32_t len;               /* +0x04 PACKET_OFF_LEN COMMMSG_OFF_SIZE, the whole record */
    uint32_t seq;               /* +0x08 PACKET_OFF_SEQ */
    uint32_t checksum;          /* +0x0C PACKET_OFF_CHECKSUM */
    uint32_t ack;               /* +0x10 PACKET_OFF_ACK */
} AM2_Packet;

/* The header every comm message starts with, COMMMSG_OFF_KIND and _SIZE. */
typedef struct AM2_CommMsgHdr {
    int32_t kind;                   /* +0x00 COMMMSG_OFF_KIND */
    int32_t size;                   /* +0x04 COMMMSG_OFF_SIZE, the whole record */
} AM2_CommMsgHdr;

/* The chat message, kind 3, 0x108 bytes at ADDR_MSG_CHAT. */
typedef struct AM2_ChatMsg {
    AM2_CommMsgHdr hdr;             /* +0x00 */
    uint8_t  ink;                   /* +0x08 MSG_CHAT_OFF_INK AM2_MSG_CHAT_SENDER, the sender's colour */
    char     text[0xFF];            /* +0x09 MSG_CHAT_OFF_TEXT AM2_MSG_CHAT_TEXT */
} AM2_ChatMsg;

/* The pause message, kind 6, 0x10 bytes at ADDR_MSG_GAME_PAUSE. */
typedef struct AM2_PauseMsg {
    AM2_CommMsgHdr hdr;             /* +0x00 */
    int32_t  pause;                 /* +0x08 MSG_PAUSE_OFF_PAUSE */
    int32_t  flags;                 /* +0x0C MSG_PAUSE_OFF_FLAGS, the reason mask */
} AM2_PauseMsg;

/* A NACK, AM2_FLOW_NACK: the range of sequences to send again. It shares
 * the packet's first two dwords and nothing after them. */
typedef struct AM2_FlowNack {
    uint32_t kind;              /* +0x00 AM2_FLOW_NACK */
    uint32_t len;               /* +0x04 */
    uint32_t first;             /* +0x08 FLOWMSG_NACK_FIRST */
    uint32_t last;              /* +0x0C FLOWMSG_NACK_LAST */
} AM2_FlowNack;

/* What the flow handler receives, told apart by the kind both start with. */
typedef union AM2_FlowMsg {
    AM2_Packet   pkt;           /* AM2_FLOW_DATA and AM2_FLOW_PULSE_ACK */
    AM2_FlowNack nack;          /* AM2_FLOW_NACK */
} AM2_FlowMsg;

/* One NACK record of a flow, 12 bytes: PeerShouldNack appends and bumps
 * them, CommRemoveKeyed drops one when its sequence arrives. */
typedef struct AM2_NackRec {
    uint32_t seq;                /* +0x00 NACKREC_OFF_SEQ */
    uint32_t time;               /* +0x04 NACKREC_OFF_TIME, when last NACKed */
    uint32_t count;              /* +0x08 NACKREC_OFF_COUNT, NACKs sent for it */
} AM2_NackRec;

/* One player's flow queue, 0x7E0 bytes, six at ADDR_PLAYER_RECORDS:
 * FindPlayerById's answer. */
typedef struct AM2_Flow {
    uint32_t    id;             /* +0x000 the DirectPlay id; 0 free */
    uint32_t    nextExpected;   /* +0x004 FLOW_OFF_NEXT_EXPECTED */
    uint32_t    highestSeq;     /* +0x008 FLOW_OFF_HIGHEST_SEQ */
    uint32_t    heHas;          /* +0x00C FLOW_OFF_HE_HAS */
    uint32_t    ackSent;        /* +0x010 FLOW_OFF_ACK_SENT */
    uint32_t    ownBit;         /* +0x014 PLAYER_REC_OFF_OWN_BIT AM2_FLOW_PLAYER_MASK, 1 << slot */
    uint32_t    wantBit;        /* +0x018 PLAYER_REC_OFF_WANT_BIT AM2_FLOW_RESEND_MASK */
    uint32_t    sentAt;         /* +0x01C FLOW_OFF_SENT_AT */
    uint32_t    recvPackets;    /* +0x020 FLOW_OFF_RECV_PACKETS */
    uint32_t    recvBytes;      /* +0x024 FLOW_OFF_RECV_BYTES */
    uint32_t    sentPackets;    /* +0x028 FLOW_OFF_SENT_PACKETS */
    uint32_t    sentBytes;      /* +0x02C FLOW_OFF_SENT_BYTES */
    uint32_t    acksSeen;       /* +0x030 FLOW_OFF_ACKS_SEEN */
    uint32_t    field34;        /* +0x034 */
    uint32_t    nacksSent;      /* +0x038 FLOW_OFF_NACKS_SENT */
    uint32_t    resends;        /* +0x03C FLOW_OFF_RESENDS */
    uint32_t    duplicates;     /* +0x040 FLOW_OFF_DUPLICATES */
    uint32_t    rttMin;         /* +0x044 FLOW_OFF_RTT_MIN */
    uint32_t    rttMax;         /* +0x048 FLOW_OFF_RTT_MAX */
    uint32_t    rttTotal;       /* +0x04C FLOW_OFF_RTT_TOTAL */
    uint32_t    gapMin;         /* +0x050 FLOW_OFF_GAP_MIN */
    uint32_t    gapMax;         /* +0x054 FLOW_OFF_GAP_MAX */
    uint32_t    gapTotal;       /* +0x058 FLOW_OFF_GAP_TOTAL */
    uint32_t    readyAt;        /* +0x05C FLOW_OFF_READY_AT PLAYER_REC_OFF_MADE_AT */
    uint32_t    field60;        /* +0x060 */
    uint32_t    field64;        /* +0x064 */
    uint32_t    field68;        /* +0x068 halved into FLOW_OFF_PEER_CLOCK */
    uint32_t    field6C;        /* +0x06C */
    uint32_t    deliveredAt;    /* +0x070 FLOW_OFF_DELIVERED_AT AM2_PLAYER_LAST_SEEN */
    uint32_t    recvAt;         /* +0x074 FLOW_OFF_RECV_AT */
    AM2_MsgList queue;          /* +0x078 FLOW_OFF_QUEUE PLAYER_REC_OFF_MSGS */
    uint32_t    ready;          /* +0x088 FLOW_OFF_READY PLAYER_REC_OFF_IS_HOST */
    uint32_t    badChecksums;   /* +0x08C FLOW_OFF_BAD_CHECKSUMS */
    uint32_t    peerClock;      /* +0x090 FLOW_OFF_PEER_CLOCK */
    uint32_t    sequence;       /* +0x094 FLOW_OFF_SEQUENCE PLAYER_REC_OFF_FLAG_94 */
    uint32_t    field98;        /* +0x098 */
    uint32_t    field9C;        /* +0x09C */
    uint32_t    fieldA0;        /* +0x0A0 */
    int32_t     lossBurst;      /* +0x0A4 FLOW_OFF_LOSS_BURST */
    int32_t     lossPct;        /* +0x0A8 FLOW_OFF_LOSS_PCT */
    int32_t     fieldAC;        /* +0x0AC */
    int32_t     lagMs;          /* +0x0B0 FLOW_OFF_LAG_MS */
    int32_t     lagSpread;      /* +0x0B4 FLOW_OFF_LAG_SPREAD */
    int32_t     nackCount;      /* +0x0B8 PEER_OFF_NACK_COUNT, records in nacks[] */
    AM2_NackRec nacks[60];      /* +0x0BC PEER_OFF_NACKS */
    int32_t     nackTally[4];   /* +0x38C removals by NACK count, 3 and up together */
    int32_t     latencyAt;      /* +0x39C the latency ring's write index */
    int32_t     latency[32];    /* +0x3A0 ms samples CommMean32 averages */
    int32_t     ringA[120];     /* +0x420 PLAYER_REC_OFF_RING_A AM2_MSGSLOT_A_OFF */
    int32_t     ringB[120];     /* +0x600 PLAYER_REC_OFF_RING_B AM2_MSGSLOT_B_OFF */
} AM2_Flow;

#ifdef __cplusplus
extern "C" {
#endif

#define AM2_MSGSLOT_COUNT  120u
#define AM2_MSGSLOT_A_OFF  0x420u
#define AM2_MSGSLOT_B_OFF  0x600u

void __cdecl MsgSlotA0(void *rec, uint32_t seq);
void __cdecl MsgSlotA1(void *rec, uint32_t seq);
void __cdecl MsgSlotA2(void *rec, uint32_t seq);
void __cdecl MsgSlotB0(void *rec, uint32_t seq);
void __cdecl MsgSlotB1(void *rec, uint32_t seq);
void __cdecl MsgSlotB2(void *rec, uint32_t seq);

/* 0x00401040. The dword at +0xC of a comm message. Callers are CommReceive,
 * PacketThreadProc and RemovePlayer, so it is the message rather than the comm
 * object; the same callers read the uid at +4. */
uint32_t __cdecl MsgField12(const void *msg);

/* 0x00402E90. Mean of the 32 dwords at +0x3A0 of a PLAYER record -- not of the
 * comm object, which is what this comment used to say. Its one caller passes
 * the result of FindPlayerById, and so does PlayerLatency below.
 *
 * And the samples are LATENCY IN MILLISECONDS, which the callers settle
 * rather than the body: both compare the answer against 750 and 1000 to pick
 * the colour a player's name is drawn in. "The shape of a latency or rate
 * average" was as far as reading this function alone could get.
 *
 * Divided with round-toward-zero rather than an arithmetic shift: the
 * original adds 31 when the sum is negative before shifting by 5, and a plain
 * `sum >> 5` would round the wrong way for negative totals. */
int32_t __cdecl CommMean32(const AM2_Flow *player);

/* 0x00402EC0. The same mean by player ID, and 0 for an ID nobody knows. */
int32_t __cdecl PlayerLatency(uint32_t id);

/* 0x00402E50. The writer for the ring CommMean32 averages: one dword into the
 * 32 entries at +0x3A0, with the write index at +0x39C, post-incremented and
 * wrapped to 0 at 32. It overwrites the oldest entry and reports nothing.
 *
 * What the samples ARE is now better than a guess. Both call sites are inside
 * 0x004014C0, the flow-control handler, whose own strings say
 * "??? PULSE seq %6d latency %d acks for %d msgs %d thru %d" -- and each site
 * adds the same value to a running total at +0x4C before pushing it. So a
 * 32-sample moving average of a latency, which is what CommMean32's comment
 * guessed from the shape alone before its writer was found.
 *
 * The wrap test is signed and reads the index back out of memory after storing
 * it rather than using the register it just wrote. Both are reproduced; neither
 * changes the result. */
void __cdecl RingPush32(AM2_Flow *player, uint32_t value);

/* 0x00402DB0. Remove the first 12-byte record at +0xBC whose leading dword
 * equals `key`, from an array whose count is the dword at +0xB8. On a match it
 * bumps one of four tallies at +0x38C, selected by the record's THIRD dword
 * clamped to 3 -- unsigned, so the clamp is a ceiling and not a range check --
 * then shifts every later record down by twelve bytes and decrements the count.
 * No match does nothing at all, silently.
 *
 * Named for what it does, not for what it is for. Both callers are in the same
 * flow-control handler as RingPush32, one of whose messages is "Flow Ack for
 * Message not in sendqueue sequence %d", so the array is very probably the
 * send queue and `key` a sequence number -- but that is the CALLER's
 * vocabulary, and this file has been wrong before about names taken from a
 * call site. The reading is recorded; the name does not depend on it. */
void __cdecl CommRemoveKeyed(AM2_Flow *player, uint32_t key);

/* 0x00402BD0 and 0x00402C00. Two dwords out of a player's flow record, looked
 * up by id through 0x00402990: the mask at +0x14 and the one at +0x18. Both
 * are named by their own error messages, which are identical but for the
 * name, and both answer 0 when there is no record for that id -- after
 * logging, so a missing record is loud rather than silent.
 *
 * They are the same function twice with a different offset, which is a shape
 * this file already has in the six MsgSlot writers. Kept as two, because the
 * image has them as two and the messages differ. */
uint32_t __cdecl GetPlayerMask(uint32_t id);
uint32_t __cdecl GetReSendMask(uint32_t id);

/* 0x004119C0 and 0x00411AC0. Broadcast this player's colour, or its team, to
 * everyone else. Named by their own messages, and one shape twice:
 *
 *   store the value into a static message record, at +8
 *   if the comm object's +0x3E4 is clear, stop -- nothing is connected
 *   hand the record to SendGameMsg
 *   RE-READ the comm object
 *   if its +0x418 is clear, stop -- that gate is the log, not the send
 *   log, with +0x3CC as the sender
 *
 * The re-read is the part worth keeping. The original loads the comm object
 * again after the send rather than reusing the register, so a send that
 * replaced the object would be followed correctly; nothing here establishes
 * that one can, and it is reproduced rather than folded away.
 *
 * The value is written BEFORE the connected check, so it lands in the record
 * even when nothing is sent. */
void __cdecl SendColorMsg(int32_t colour);
void __cdecl SendTeamMsg(int32_t team);

/* 0x00411E90, the third of the family and the one that is not a value.
 * `char *`, not `const char *`: a text longer than 255 bytes is truncated IN
 * THE CALLER'S BUFFER, at [0xFE] and not at [0xFF]. `system` non-zero stamps
 * sender 4, the announcement colour; zero stamps the army of our own slot.
 * Unlike the two above there is no connected check -- this one always hands
 * the record to SendGameMsg. */
void __cdecl SendChatMsg(char *text, int32_t system);

/* Original: 0x004010C0, cdecl, the mirror of MsgListAdd. Unlink the head node
 * under the mutex and answer it, or answer null when the list is empty.
 *
 * Two complaints, and they are not the same. The size check is the append's,
 * fires above 400 or below zero, and does not stop anything. The other --
 * "Empty List!" -- is gated on the list being ADDR_MSG_LIST_POOL specifically,
 * because an empty POOL means the game has run out of message buffers, while
 * an empty ordinary queue is the normal state of an idle one. Both complaints
 * are made while the mutex is still held, as in the append.
 *
 * The node's `prev` is cleared on the NEW head but the unlinked node's own
 * links are left alone, so a caller can still read its `next`. */
AM2_MsgNode *__cdecl MsgListRemHead(AM2_MsgList *list);

/* Original: 0x00401410, three callers, and it names itself in all three of
 * its complaints -- "RemMsg". Unlink a node the caller already holds,
 * wherever it sits, where MsgListRemHead unlinks the head. The list's TAIL is
 * maintained in both directions and the unlinked node's own links are left
 * alone, so a caller walking while removing can still read its `next`. See
 * commmsg.cpp for which complaint reaches which list. */
void __cdecl MsgListRemove(AM2_MsgList *list, AM2_MsgNode *node);

/* Original: 0x00410B70, and it names itself -- "ReceiveEndSetupMsg". Log if
 * the comm object's verbosity field is set, then post AM2_WM_SETUP_DONE to the
 * game window. That is all of it: the receive side of this handshake holds no
 * state, it just tells the message pump something arrived, and WndProc's
 * 0x046E case does the work.
 *
 * It cannot be exercised here -- the handshake needs a second player -- so it
 * is verified by reading, like the rest of that group. It has the family's
 * (message, sender) parameters and reads neither; CommDispatchMessage passes
 * both, as every other handler is passed them. */
void __cdecl ReceiveEndSetupMsg(void *msg, int32_t from);

/* Original: 0x00410E90, "ReceiveGameReadyToLoadMsg". HOST ONLY -- it returns
 * at once unless COMM_OFF_IS_HOST is set, so a client that somehow received
 * this does nothing with it.
 *
 * It records the sender's flag in `m_ArmyReadyToLoad[slot]`, a name the log
 * line hands over verbatim: "Setting m_ArmyReadyToLoad[%d] to %s". That places
 * the field at 0x0270 of the 112-byte per-army record, which is the same
 * record stride 0x0040F5A0 indexes.
 *
 * Then it repaints the lobby: the current dialog's slot 2 and slot 1, the same
 * update-then-paint pair the widget layer uses everywhere. This is the only
 * place found so far where the comm side drives the menu directly.
 *
 * The slot lookup is called TWICE when logging is on -- once to store and once
 * to print -- which is the original's, not a tidy-up opportunity: the second
 * call is inside the `if`. */
void __cdecl ReceiveGameReadyToLoadMsg(void *msg, int32_t dpid);

/* Original: 0x00410BB0, "ReceiveGameReadyMsg". Record the sender's flag in
 * `m_ArmyReady[slot]` -- 0x0274 of the per-army record, next door to
 * `m_ArmyReadyToLoad` -- and then, ON THE HOST ONLY, decide whether setup is
 * finished.
 *
 * Finished means every OCCUPIED slot is ready, and "occupied" is `player id
 * != -1`. Note that is the only value skipped: AM2_PLAYER_ID's own note says
 * "0 or -1 is none", so a slot holding 0 would still have to be ready. Left
 * as the original has it.
 *
 * When they all are, it sends the end-of-setup record and posts
 * AM2_WM_SETUP_DONE -- so the host both announces it and tells its own window,
 * which is why ReceiveEndSetupMsg exists on the other side.
 *
 * The player count is re-read from the comm object on every iteration rather
 * than hoisted. Reproduced. */
void __cdecl ReceiveGameReadyMsg(void *msg, int32_t dpid);

/* Original: 0x00410D90, "SendGameReadyToLoadMsg". The CLIENT half of the pair
 * whose host half is ReceiveGameReadyToLoadMsg: it returns at once if this
 * machine IS the host, which is the exact mirror of the other returning unless
 * it is.
 *
 * It sets its OWN m_ArmyReadyToLoad -- finding its slot from the comm object's
 * self id rather than from an argument -- then sends the record and repaints
 * the lobby.
 *
 * The repaint here has NO null check on the current dialog, while the host
 * side's identical repaint does. One of the two is wrong and the original
 * disagrees with itself; both are reproduced as written, because a crash on a
 * null dialog is the original's behaviour and inventing a guard would hide it.
 *
 * The slot lookup runs twice when logging is on, as in the host half. */
void __cdecl SendGameReadyToLoadMsg(int32_t ready);

/* Original: 0x00410CE0, and no string of its own -- the name is ours, from the
 * body: on the host, if every occupied slot is ready, send the end-of-setup
 * record and post AM2_WM_SETUP_DONE.
 *
 * The same block is INLINED at the end of both SendGameReadyMsg and
 * ReceiveGameReadyMsg, so the image holds three identical copies. That is what
 * an inline member function looks like after MSVC has declined to inline it at
 * one site out of three, and it is why this is written once here and called
 * from the other two rather than transcribed three times. */
void __cdecl CommEndSetup(void);

/* Original: 0x00410A10, "SendGameReadyMsg\n %s". The LOCAL half of
 * ReceiveGameReadyMsg: same field, same scan, but the slot comes from the comm
 * object's own player id rather than from a message's sender, and the record
 * at 0x004FAA28 goes out afterwards.
 *
 * Note the pairing is not symmetric the way the ready-to-load pair is. That
 * one splits host and client with an early return each; this one runs the host
 * scan for whoever calls it, because a host that marks itself ready may be the
 * last one the scan was waiting on. */
void __cdecl SendGameReadyMsg(int32_t ready);

/* Original: 0x00411880, "SendMapMsg from %x   Error = %d". Report how the map
 * check went -- and return 1 WITHOUT sending if this machine is the host,
 * which is the first thing it does. The host has nobody to tell.
 *
 * The value is a RESULT CODE and not a map index, which the name does not say:
 * ReceiveStartGameMsg sends 7, 0x00411830 sends 5, and ReceivedMapMsg switches
 * on 0..8 calling 4 nominal.
 *
 * Two parameters, and the body reads only the first. All three call sites push
 * two, and 0x0041116E cleans exactly eight; what the second holds differs by
 * site. It is named `unused` here because that is what it is to this function.
 *
 * The log is mislabelled in the original: it prints the ARGUMENT under "Error"
 * while the send result it just took sits in a register and is never printed.
 * The same author's "Seed is %d" in SendGameStartMsg pushes a literal 0 -- and
 * so does the RECEIVE half. Both kept. */
int32_t __cdecl SendMapMsg(int32_t result, int32_t unused);

/* Original: 0x00411100, "ReceiveStartGameMsg for %d Players.  Seed is %d ".
 * The client's half of SendGameStartMsg, and the place the shared seed lands:
 * 0x0190 into the record, straight into the global the host chose it in.
 *
 * It returns at once if this machine IS the host. Then, for every player that
 * is not us, it reports the map result and makes sure a flow queue exists --
 * and if either fails for any player it calls the session getter, logs "Error
 * in start" and does NOT start. Note the failure flag is checked only after
 * the loop, so one bad player stops the game for all of them.
 *
 * Zero players skips the loop AND the check, landing straight on the success
 * path. And "Seed is %d" prints a literal 0 here exactly as it does in the
 * send half, so the seed is never in the log at either end.
 *
 * It posts 0x0469, which nothing in the image handles. */
void __cdecl ReceiveStartGameMsg(void *msg, int32_t dpid);

/* Original: 0x00410890, "RemoteGamePause from %x; playerIndex== %d paused = %d
 * pauseflags = %x (%x) (msg Pause=%x)". The pause mask has one bit per player
 * per reason, and this is where a peer's bit is set or cleared.
 *
 * Two independent blocks, not a switch: bit 0x0008 of the message's flags
 * selects the 0x10<<slot family and bit 0x10000 selects 0x20000<<slot, and a
 * message carrying both runs both. Each block is four explicit compares on the
 * slot rather than a shift, so a slot above 3 does nothing at all -- it is not
 * clamped, it is simply not handled.
 *
 * The mask the log prints is whichever block ran LAST, and the log fires only
 * when a block actually ran: it is guarded on the mask being non-zero as well
 * as on the comm object's verbosity. */
void __cdecl RemoteGamePause(void *msg, int32_t dpid);

/* Original: 0x00411BD0, and the name is ours -- it carries no string of its
 * own. The host telling us how to send: the value becomes the comm object's
 * SEND FLAGS, which is the third argument ArmyMessageFlush hands SendGameMsg
 * for every outgoing packet, and two further fields go into our own flow
 * record.
 *
 * Client only, and the flow record is looked up by OUR id rather than the
 * sender's -- the one message in this family that does not use the dpid it is
 * given at all. If we have no flow record yet, the two fields are dropped and
 * the send flags are kept anyway. */
void __cdecl ReceiveFlowControlMsg(void *msg, int32_t dpid);

/* Original: 0x00410720. One arriving packet: log it if the sequence number is
 * below five, verify the checksum, then walk the messages inside it.
 *
 * The checksum failing does NOT stop the walk. It logs, bumps that player's
 * error count, and carries on into a packet it has just been told is corrupt.
 * The original's, and it is the sort of thing only a live session could show.
 *
 * The walk decrements the packet's own length field as it goes, so the packet
 * is consumed in place. But the bogus-length test compares each part against
 * the length SAVED on entry, not against what is left -- so a part longer than
 * the whole packet is caught while a part longer than the remainder is not,
 * and that drives the length field negative. The loop bound is UNSIGNED, so a
 * negative length is enormous and the walk carries on off the end of the
 * packet instead of stopping. Reproduced: a signed compare would quietly
 * repair it, and repairing the original is not what this port is for.
 *
 * And it re-reads the part's length AFTER handing the message on, so a handler
 * that rewrote those two bytes would move the cursor somewhere else. */
void __cdecl ReceivePacket(void *packet, int32_t dpid);

/* Original: 0x004114E0, "ReceivePlayerMsg for %d Players. I reckoned there
 * were %d Players ". The host's whole view of the lobby, arriving at a client:
 * the player count, every slot's id, colour, team and name, the map and level
 * names, and the version pair.
 *
 * Its loop bound is the ADDRESS OF THE NEXT GLOBAL. It fills ADDR_ARMY_POINTS
 * and stops when the cursor reaches ADDR_SCORE_LIMIT, which sits immediately
 * after it -- so there are exactly four slots, whatever the message says the
 * count is. The same shape as the registration table walking up to
 * ADDR_SCRIPT_CONDITIONS.
 *
 * Three things are done twice and one is done early:
 *
 *  - the opening log is NOT gated on the comm object's verbosity, unlike every
 *    other log in the function and like SendGameStartMsg's;
 *  - a record that is not ours has CommSetSlotRemote called on it twice, once
 *    before the bound check and once after;
 *  - and the bound check sits BETWEEN those two, so the fifth and later
 *    records still get their remote flag set before the loop gives up.
 *
 * The version-mismatch message names OUR player, not the sender's: "%s has a
 * different version of the game" is filled from ADDR_DEFAULT_OWNER. A client
 * that disagrees with the host announces itself. */
void __cdecl ReceivePlayerMsg(const AM2_PlayerMsg *msg, int32_t dpid);

/* Original: 0x0040FBB0, "Unknown Army Msg Item Type %d, msgtype:%d, item uid:
 * %x; msgsize: %d". The SECOND dispatcher -- one message out of a packet,
 * where CommDispatchMessage handles the packet-level ones. ReceivePacket is
 * its only caller.
 *
 * It switches on the object KIND behind the message's uid, not on a message
 * type: 2 goes to troopMessageReceive, 3 to the vehicle equivalent, 4 and
 * anything unrecognised fall through to the game-wide messages, and 1 and 5
 * are accepted and ignored in silence.
 *
 * The army it passes on is the uid's owner, EXCEPT for uid 0, where it is the
 * packet's sender slot instead -- so a message about nothing is attributed to
 * whoever sent it.
 *
 * Two of its calls are to ADDR_LOG, which this build has stubbed to a single
 * `ret`. One of them passes the MESSAGE BUFFER as the format string. Both are
 * reproduced, and neither does anything.
 *
 * GAME_WON is recorded as GAME_LOST unless ADDR_WIN_ENABLED is set, and when
 * it is enabled the winner is written as 1 if it was us and as the army
 * otherwise. Both arms then request menu 0x22 and raise the state-pending flag
 * DIRECTLY, without going through RequestState -- which is the same pair
 * CLAUDE.md records as the route to the level teardown. */
void __cdecl ReceiveArmyMsg(void *msg, int32_t slot, int32_t seq);

/* Original: 0x004118F0, "ReceivedMapMsg from %x  Result = %d (4 is nominal)".
 * Host only. Its one caller is the message dispatcher.
 *
 * A nine-arm jump table on the value, and the arms do NOT line up with the log
 * text: 0 sets the slot's flag; 1, 2, 3, 4, 6 and 8 clear it and play sound 3;
 * 5 and 7 do nothing at all, as does anything above 8. So the value the
 * message calls nominal takes the same arm as the failures. Transcribed from
 * the table at 0x00411998, not from the layout -- as always in this image, the
 * two differ. */
void __cdecl ReceivedMapMsg(void *msg, int32_t dpid);

/* Original: 0x00411A20 and 0x00411B20, the receive halves of SendColorMsg and
 * SendTeamMsg. Host only, and both end the same way: repaint the current
 * dialog through the update-then-paint pair, then send the player list.
 *
 * They differ in the middle. The colour one goes through
 * CommSetArmyColour, which SWAPS the colour with whoever already had it, and
 * gives up if that returns -1; the team one writes the field directly with no
 * check of any kind. */
void __cdecl ReceivedColorMsg(void *msg, int32_t dpid);
void __cdecl ReceivedTeamMsg(void *msg, int32_t dpid);

/* Original: 0x0040FEA0, and the name is ours -- it has no string that names
 * itself, only "Unknown message type %d" for its default arm. The receive
 * side's dispatcher, an eighteen-arm jump table on the message's first dword,
 * and the whole of it is gated on 0x0404 of the comm object.
 *
 * The arm order comes from the table at 0x00410044 and is nothing like the
 * layout: type 11 is the first arm emitted, type 3 the tenth. Types 4, 12, 13
 * and 16 land on the same arm as anything above 18 and are LOGGED as unknown,
 * while type 2 returns in silence -- so the original distinguishes a message
 * it knows and ignores from one it does not know.
 *
 * Three kinds of host test sit side by side here, and the differences are the
 * original's. Types 9, 17 and 18 return unless this machine is the host, on
 * top of the same test inside the handler they would have called. Type 8 tests
 * the host only to LOG that it should not have received the message, and then
 * calls the handler either way. Everything else does not test at all. */
/* Four arguments at its one call site -- the queue node's message, length,
 * sender and addressee, the order CommSystemMessage takes them in -- of which
 * it reads two: the message, and the SENDER it hands every handler. It once
 * handed them the length, reading the node's +0x24 as a sender. */
void __cdecl CommDispatchMessage(void *msg, int32_t size, int32_t from,
                                 int32_t to);


/* Original: 0x0040F600 and 0x0040F620, thiscall. Set and clear 0x020C of one
 * player record. ReceivePlayerMsg writes 1 into every record that is not ours
 * and 0 into the one that is, and CommPlayerLeft clears it -- so the field
 * says a remote player is in that slot, and the names are ours from that.
 *
 * Twenty-seven bytes each, and the only difference is the constant. Written as
 * two functions rather than one with a flag because that is what the image
 * holds: two entry points, four callers between them. */
/* CommSetSlotRemote / CommClearSlotRemote are AM2_Comm methods (below) -- they
 * were thiscall members, so they are modelled as C++ methods to recompile to
 * the shipped bytes; free-function forwarders keep the call sites unchanged. */

/* Original: 0x0040F5A0, thiscall. Three-valued, and only the first branch
 * reads the field the two above write.
 *
 * An OCCUPIED slot -- id neither 0 nor -1 -- answers with 0x020C normalised to
 * 0 or 1. An empty slot with 0x025C set answers "am I NOT the host" instead,
 * which is a different question entirely and returns 1 on a client. Anything
 * else answers -1. So a caller that treats the result as a boolean gets a
 * truthy answer from the -1 as well.
 *
 * The slot arrives as a SIGNED WORD, alone in this family; every other
 * accessor takes a full int32_t. A negative would index backwards. */
/* CommSlotRemote is an AM2_Comm method (below). */

/* Original: 0x0040F560, thiscall, and the name is ours. "Must I tell the other
 * players what this army just did?" -- TrooperDropItem and its relatives ask
 * this before sending anything.
 *
 * Three answers. No multiplayer session at all is NO, which is what settles
 * the name: under "is this army mine" a single-player game would answer yes to
 * everything. Army 4, the neutral one, answers "am I the host". Anything else
 * answers "is that slot NOT remote".
 *
 * That last one is CommSlotRemote inverted, so it inherits the three-valued
 * oddity: a slot that answers -1 is truthy there and becomes 0 here. The
 * original spells the inversion `neg; sbb; inc`, which is `== 0` and not a
 * logical not -- for -1 the two agree, and it is written as the compare.
 *
 * NOT pure, unlike the three accessors above: it reads the multiplayer-session
 * global, so it cannot have vectors. */
/* CommMustBroadcast is an AM2_Comm method (below). */

/* 0x00402990, 27 callers -- the record for a DirectPlay id, or null.
 *
 * Six records of 0x7E0 bytes from 0x004F1980, walked to the end whatever
 * happens: the loop does NOT break at a match, it overwrites the answer, so
 * the LAST match wins. orig.h has said so since long before this was written.
 *
 * Kept as PLAYER rather than FLOWQ for the reason orig.h gives: both words are
 * the program's own, from different error messages about the same record. */
AM2_Flow *__cdecl FindPlayerById(uint32_t id);

int msgslot_install(void);

#ifdef __cplusplus
}

/* The multiplayer comm object.  Opaque here -- its methods reach fields by
 * offset, as the original's thiscall members did; the class exists so the
 * hosted MSVC 6 recompiles them to the shipped thiscall bytes (src/vc6).  The
 * free-function forwarders keep every existing call site -- CommFoo(comm, ...)
 * -- unchanged: at -O2 each inlines to the method call with byte-identical
 * caller code, so the injected build is unaffected. */
/* One player slot of the comm object, 0x70 bytes, four from +0x20C. */
struct AM2_CommSlot {
    int32_t  remote;            /* +0x00 COMM_ARMY_OFF_REMOTE */
    int32_t  colour;            /* +0x04 COMM_SLOT_OFF_INDEX COMM_ARMY_OFF_COLOUR, 0x63 empty */
    uint32_t id;                /* +0x08 COMM_SLOT_OFF_ID, the DirectPlay id */
    char     name[0x40];        /* +0x0C COMM_SLOT_OFF_NAME */
    int32_t  team;              /* +0x4C COMM_SLOT_OFF_TEAM COMM_ARMY_OFF_TEAM */
    int32_t  wasHere;           /* +0x50 COMM_SLOT_OFF_TAKEN COMM_ARMY_OFF_WAS_HERE */
    int32_t  chksumErrs;        /* +0x54 COMM_ARMY_OFF_CHKSUM_ERRS */
    uint32_t unacked;           /* +0x58 COMM_SLOT_OFF_UNACKED */
    int32_t  field5C;           /* +0x5C COMM_SLOT_OFF_FIELD_5C */
    uint32_t heard;             /* +0x60 COMM_SLOT_OFF_HEARD, GetTickCount of the last packet */
    int32_t  readyToLoad;       /* +0x64 COMM_SLOT_OFF_READY_TO_LOAD */
    int32_t  ready;             /* +0x68 COMM_SLOT_OFF_READY */
    int32_t  mapOk;             /* +0x6C COMM_ARMY_OFF_MAP_OK, what ReceivedMapMsg sets */
};

/* The send and receive statistics rings, 30 samples each. */
#define AM2_COMM_STAT_SAMPLES 30

struct AM2_Comm {
    uint32_t     field000;      /* +0x000 */
    uint32_t     statIndex;     /* +0x004 COMM_OFF_STAT_INDEX, ring cursor, 0..29 */
    uint32_t     rxIndex;       /* +0x008 COMM_OFF_RX_INDEX */
    uint32_t     statTimes[AM2_COMM_STAT_SAMPLES]; /* +0x00C COMM_OFF_STAT_TIMES */
    uint32_t     statSizes[AM2_COMM_STAT_SAMPLES]; /* +0x084 COMM_OFF_STAT_SIZES */
    uint32_t     rxTimes[AM2_COMM_STAT_SAMPLES];   /* +0x0FC COMM_OFF_RX_TIMES */
    uint32_t     rxSizes[AM2_COMM_STAT_SAMPLES];   /* +0x174 COMM_OFF_RX_SIZES */
    uint32_t     rxMax;         /* +0x1EC COMM_OFF_RX_MAX */
    uint32_t     statMax;       /* +0x1F0 COMM_OFF_STAT_MAX */
    uint32_t     rxBytes;       /* +0x1F4 COMM_OFF_RX_BYTES */
    uint32_t     statBytes;     /* +0x1F8 COMM_OFF_STAT_BYTES */
    uint32_t     rxPackets;     /* +0x1FC COMM_OFF_RX_PACKETS */
    uint32_t     statPackets;   /* +0x200 COMM_OFF_STAT_PACKETS */
    void        *regKey;        /* +0x204 the HKEY RegCreateKeyExA opens */
    uint32_t     regDisposition; /* +0x208 what RegCreateKeyExA reports */
    AM2_CommSlot slots[4];      /* +0x20C COMM_OFF_PLAYERS */
    uint32_t     selfId;        /* +0x3CC COMM_OFF_OUR_PLAYER_ID AM2_COMM_SELF_ID */
    int32_t      playerCount;   /* +0x3D0 COMM_OFF_PLAYER_COUNT */
    const void  *appGuid;       /* +0x3D4 COMM_OFF_APP_GUID, a GUID * */
    int32_t      isHost;        /* +0x3D8 COMM_OFF_IS_HOST */
    int32_t      active;        /* +0x3DC COMM_OFF_JOINED AM2_COMM_OFF_ACTIVE */
    int32_t      sawKind31;     /* +0x3E0 COMM_OFF_SAW_KIND_31 */
    int32_t      connected;     /* +0x3E4 COMM_OFF_PLAYER_MADE AM2_COMM_CONNECTED */
    void        *sessionDesc;   /* +0x3E8 COMM_OFF_SESSION_DESC, a DPSESSIONDESC2 */
    void        *dplay;         /* +0x3EC COMM_OFF_DPLAY, an IDirectPlay4A */
    void        *lobbyBuf;      /* +0x3F0 COMM_OFF_LOBBY_BUF, a DPLCONNECTION */
    void        *lobby;         /* +0x3F4 COMM_OFF_LOBBY, an IDirectPlayLobby3A */
    int32_t      lobbied;       /* +0x3F8 COMM_OFF_LOBBIED */
    int32_t      lobbyStarting; /* +0x3FC COMM_OFF_LOBBY_STARTING */
    int32_t      local;         /* +0x400 COMM_OFF_LOCAL, non-zero once the game runs */
    int32_t      msgsEnabled;   /* +0x404 COMM_OFF_MSGS_ENABLED */
    uint32_t     sendStamp;     /* +0x408 COMM_OFF_SEND_STAMP, GetTickCount at the last send */
    uint32_t     statsSince;    /* +0x40C COMM_OFF_STATS_SINCE, ms */
    uint32_t     bufferMax;     /* +0x410 COMM_OFF_BUFFER_MAX */
    uint32_t     sendFlags;     /* +0x414 COMM_OFF_SEND_FLAGS */
    int32_t      verbose;       /* +0x418 COMM_OFF_VERBOSE AM2_COMM_LOG_ENABLED */
    int32_t      field41C;      /* +0x41C */
    uint32_t     coalesceMs;    /* +0x420 COMM_OFF_COALESCE_MS */
    uint32_t     maxHoldMs;     /* +0x424 COMM_OFF_MAX_HOLD_MS */
    uint32_t     bufferDefault; /* +0x428 COMM_OFF_BUFFER_DEFAULT */
    uint8_t      caps[0x28];    /* +0x42C COMM_OFF_CAPS, a DPCAPS */
    int32_t      timeoutsOn;    /* +0x454 COMM_OFF_TIMEOUTS_ON */
    int32_t      statBwMax;     /* +0x458 COMM_OFF_STAT_BW_MAX */
    int32_t      rxBwMax;       /* +0x45C COMM_OFF_RX_BW_MAX */
    int32_t      statBwSamples; /* +0x460 COMM_OFF_STAT_BW_SAMPLES */
    int32_t      rxBwSamples;   /* +0x464 COMM_OFF_RX_BW_SAMPLES */
    int32_t      statBwOver;    /* +0x468 COMM_OFF_STAT_BW_OVER */
    int32_t      rxBwOver;      /* +0x46C COMM_OFF_RX_BW_OVER */
    int32_t      trace;         /* +0x470 COMM_OFF_TRACE */
    int32_t      log;           /* +0x474 COMM_OFF_LOG */
    uint32_t     enoughChangeLimit; /* +0x478 COMM_OFF_ENOUGH_CHANGE_LIMIT */
    uint32_t     sendInterval;  /* +0x47C COMM_OFF_SEND_INTERVAL */

    void    __attribute__((thiscall)) CommSetSlotRemote(int32_t slot);
    void    __attribute__((thiscall)) CommClearSlotRemote(int32_t slot);
    int32_t __attribute__((thiscall)) CommSlotRemote(int16_t slot);
    int32_t __attribute__((thiscall)) CommMustBroadcast(int16_t army);
    /* defined in misc.cpp (the accessors) -- one class, several files */
    int32_t __attribute__((thiscall)) CommSlotForArmy(int32_t army);
    int32_t __attribute__((thiscall)) GetArmyScore(int32_t slot);
    int32_t __attribute__((thiscall)) CommTeamScore(int32_t slot);
    int32_t __attribute__((thiscall)) CommWasHereForArmy(int32_t army);
    int32_t __attribute__((thiscall)) CommSetArmyColour(int32_t slot, int32_t colour);
    int32_t __attribute__((thiscall)) CommSlotHasPlayer(int32_t slot);
    int32_t __attribute__((thiscall)) CommArmyOfSlot(int32_t slot);
    int32_t __attribute__((thiscall)) CommPlayerId(int32_t slot);
    int32_t __attribute__((thiscall)) CommAllPlayersAgreed();
    int32_t __attribute__((thiscall)) CommAllPlayersReady();
    /* defined in army.cpp */
    int32_t __attribute__((thiscall)) AllyFlag(int32_t a, int32_t b);
    /* defined in win32/dplay.cpp; reads the comm global, not `this` */
    int32_t __attribute__((thiscall)) CommClose();
    int32_t __attribute__((thiscall)) CommEnumPlayers();
    int32_t __attribute__((thiscall)) ArmyInPlay(uint32_t uid);
    void * __attribute__((thiscall)) CommConstruct();
    int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *connection);
    /* `this` is never read; the caller passes it all the same. */
    int32_t __attribute__((thiscall)) CreateDirectPlayLobby(void *out);
    void __attribute__((thiscall)) CommMarkLobbied();
    void __attribute__((thiscall)) CommDestruct();
    int32_t __attribute__((thiscall)) CommDropDirectPlay();
    int32_t __attribute__((thiscall)) CommDropSession();
    int32_t __attribute__((thiscall)) CommEnumConnections(void *list);
    int32_t __attribute__((thiscall)) CommEnumSessions(void *list);
    int32_t __attribute__((thiscall)) CommFindPlayer(int32_t dpid);
    int32_t __attribute__((thiscall)) CommGetSessionDesc();
    int32_t __attribute__((thiscall)) CommInitializeConnection(void *connection);
    int32_t __attribute__((thiscall)) CommLobbyStart();
    int32_t __attribute__((thiscall)) CommOnConnected();
    int32_t __attribute__((thiscall)) CommOpenSession(const char *name);
    int32_t __attribute__((thiscall)) CommPlayerLeft(int32_t id);
    int32_t __attribute__((thiscall)) CommPlayerSlot(uint32_t id);
    void __attribute__((thiscall)) CommPublishResult();
    int32_t __attribute__((thiscall)) CommRecentTotal();
    int32_t __attribute__((thiscall)) CommRemovePlayer(int32_t id);
    void __attribute__((thiscall)) CommReopenSession();
    void __attribute__((thiscall)) CommReportStats();
    void __attribute__((thiscall)) CommResetStats();
    int32_t __attribute__((thiscall)) CommSend(uint32_t idTo, uint32_t flags, void *data, uint32_t size);
    uint8_t __attribute__((thiscall)) CommSendLobbyProperty(uint32_t value);
    void __attribute__((thiscall)) CommSessionOver();
    int32_t __attribute__((thiscall)) CommSetSessionDesc(void *desc, uint32_t flags);
    int32_t __attribute__((thiscall)) CommSlotOfId(uint32_t id);
    /* Win32-typed in dplay.h's forwarders; flat here (GUID * / HANDLE /
     * DWORD / DPID * are pointer- or 32-bit-sized), cast back in the bodies. */
    int32_t __attribute__((thiscall)) CommJoinSession(const void *instance);
    int32_t __attribute__((thiscall)) CommCreatePlayer(const char *name, void *event,
                                                       void *data, uint32_t length);
    int32_t __attribute__((thiscall)) CommReceive(uint32_t *from, uint32_t *to,
                                                  uint32_t flags, void *data,
                                                  uint32_t *size);
};
#if !defined(_MSC_VER)
#include <stddef.h>   /* offsetof: GCC-side only, as in objtable.h */
static_assert(offsetof(AM2_Comm, slots)       == 0x20C, "slots");
static_assert(sizeof(AM2_CommSlot)            == 0x70,  "slot stride");
static_assert(sizeof(AM2_Flow)                == 0x7E0, "flow record");
static_assert(offsetof(AM2_Flow, latency)     == 0x3A0, "flow latency");
static_assert(offsetof(AM2_Flow, lagSpread)   == 0xB4,  "flow lag spread");
static_assert(offsetof(AM2_Flow, queue)       == 0x78,  "flow queue");
static_assert(sizeof(AM2_MsgNode)             == 0x28,  "msg node");
static_assert(sizeof(AM2_MsgList)             == 0x10,  "msg list");
static_assert(sizeof(AM2_ChatMsg)             == 0x108, "chat message");
static_assert(sizeof(AM2_PauseMsg)            == 0x10,  "pause message");
static_assert(offsetof(AM2_Comm, selfId)      == 0x3CC, "selfId");
static_assert(offsetof(AM2_Comm, connected)   == 0x3E4, "connected");
static_assert(offsetof(AM2_Comm, playerCount) == 0x3D0, "playerCount");
static_assert(offsetof(AM2_Comm, isHost)      == 0x3D8, "isHost");
static_assert(offsetof(AM2_Comm, sessionDesc) == 0x3E8, "sessionDesc");
static_assert(offsetof(AM2_Comm, dplay)       == 0x3EC, "dplay");
static_assert(offsetof(AM2_Comm, verbose)     == 0x418, "verbose");
static_assert(offsetof(AM2_Comm, sendInterval) == 0x47C, "sendInterval");
static_assert(offsetof(AM2_Comm, statPackets) == 0x200, "statPackets");
static_assert(offsetof(AM2_Comm, caps)        == 0x42C, "caps");
static_assert(offsetof(AM2_Comm, timeoutsOn)  == 0x454, "timeoutsOn");
static_assert(offsetof(AM2_Comm, rxTimes)     == 0x0FC, "rxTimes");
static_assert(offsetof(AM2_CommSlot, mapOk)   == 0x6C,  "mapOk");
#endif
static inline int32_t __attribute__((thiscall)) ArmyInPlay(void *comm, uint32_t uid)
{ return ((AM2_Comm *)comm)->ArmyInPlay(uid); }
static inline void * __attribute__((thiscall)) CommConstruct(void *comm)
{ return ((AM2_Comm *)comm)->CommConstruct(); }
static inline int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection)
{ return ((AM2_Comm *)comm)->CommCreateDirectPlay(connection); }
static inline void __attribute__((thiscall)) CommDestruct(void *comm)
{ ((AM2_Comm *)comm)->CommDestruct(); }
static inline int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm)
{ return ((AM2_Comm *)comm)->CommDropDirectPlay(); }
static inline int32_t __attribute__((thiscall)) CommDropSession(void *comm)
{ return ((AM2_Comm *)comm)->CommDropSession(); }
static inline int32_t __attribute__((thiscall)) CommEnumConnections(void *comm, void *list)
{ return ((AM2_Comm *)comm)->CommEnumConnections(list); }
static inline int32_t __attribute__((thiscall)) CommEnumSessions(void *comm, void *list)
{ return ((AM2_Comm *)comm)->CommEnumSessions(list); }
static inline int32_t __attribute__((thiscall)) CommFindPlayer(void *comm, int32_t dpid)
{ return ((AM2_Comm *)comm)->CommFindPlayer(dpid); }
static inline int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm)
{ return ((AM2_Comm *)comm)->CommGetSessionDesc(); }
static inline int32_t __attribute__((thiscall)) CommInitializeConnection(void *comm, void *connection)
{ return ((AM2_Comm *)comm)->CommInitializeConnection(connection); }
static inline int32_t __attribute__((thiscall)) CommLobbyStart(void *comm)
{ return ((AM2_Comm *)comm)->CommLobbyStart(); }
static inline int32_t __attribute__((thiscall)) CommOnConnected(void *self)
{ return ((AM2_Comm *)self)->CommOnConnected(); }
static inline int32_t __attribute__((thiscall)) CommOpenSession(void *self, const char *name)
{ return ((AM2_Comm *)self)->CommOpenSession(name); }
static inline int32_t __attribute__((thiscall)) CommPlayerLeft(void *comm, int32_t id)
{ return ((AM2_Comm *)comm)->CommPlayerLeft(id); }
static inline int32_t __attribute__((thiscall)) CommPlayerSlot(void *comm, uint32_t id)
{ return ((AM2_Comm *)comm)->CommPlayerSlot(id); }
static inline void __attribute__((thiscall)) CommPublishResult(void *comm)
{ ((AM2_Comm *)comm)->CommPublishResult(); }
static inline int32_t __attribute__((thiscall)) CommRecentTotal(void *comm)
{ return ((AM2_Comm *)comm)->CommRecentTotal(); }
static inline int32_t __attribute__((thiscall)) CommRemovePlayer(void *self, int32_t id)
{ return ((AM2_Comm *)self)->CommRemovePlayer(id); }
static inline void __attribute__((thiscall)) CommReopenSession(void *comm)
{ ((AM2_Comm *)comm)->CommReopenSession(); }
static inline void __attribute__((thiscall)) CommReportStats(void *comm)
{ ((AM2_Comm *)comm)->CommReportStats(); }
static inline void __attribute__((thiscall)) CommResetStats(void *comm)
{ ((AM2_Comm *)comm)->CommResetStats(); }
static inline int32_t __attribute__((thiscall)) CommSend(void *comm, uint32_t idTo, uint32_t flags, void *data, uint32_t size)
{ return ((AM2_Comm *)comm)->CommSend(idTo, flags, data, size); }
static inline uint8_t __attribute__((thiscall)) CommSendLobbyProperty(void *comm, uint32_t value)
{ return ((AM2_Comm *)comm)->CommSendLobbyProperty(value); }
static inline void __attribute__((thiscall)) CommSessionOver(void *comm)
{ ((AM2_Comm *)comm)->CommSessionOver(); }
static inline int32_t __attribute__((thiscall)) CommSetSessionDesc(void *comm, void *desc, uint32_t flags)
{ return ((AM2_Comm *)comm)->CommSetSessionDesc(desc, flags); }
static inline int32_t __attribute__((thiscall)) CommSlotOfId(void *comm, uint32_t id)
{ return ((AM2_Comm *)comm)->CommSlotOfId(id); }
static inline void CommSetSlotRemote(void *comm, int32_t slot)
{ ((AM2_Comm *)comm)->CommSetSlotRemote(slot); }
static inline void CommClearSlotRemote(void *comm, int32_t slot)
{ ((AM2_Comm *)comm)->CommClearSlotRemote(slot); }
static inline int32_t CommSlotRemote(void *comm, int16_t slot)
{ return ((AM2_Comm *)comm)->CommSlotRemote(slot); }
static inline int32_t CommMustBroadcast(void *comm, int16_t army)
{ return ((AM2_Comm *)comm)->CommMustBroadcast(army); }
/* The one comm object, for a direct member call. Calling through the inline
 * forwarders above changes VC6's register allocation at the call site, so a
 * byte-matched caller spells `AM2_THE_COMM->Method(...)`. */
#define AM2_THE_COMM (g_commObject)
#endif

/* 0x00401050. Append a node to the tail of a mutex-guarded list. Twelve
 * callers and multi-threaded; the size complaint above 400 or below zero does
 * not stop the append and is issued while the mutex is still held. */
void __cdecl MsgListAdd(AM2_MsgList *list, AM2_MsgNode *node);

/* 0x00402720. Raise the exit flag, log, and post WM_CLOSE to the game window
 * -- in that order. */
void __cdecl ExitGamePostClose(void);

#endif /* AM2_MSGSLOT_H */
