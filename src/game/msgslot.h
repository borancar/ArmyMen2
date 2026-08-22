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
 * Their callers are PacketThreadProc and its neighbours, so "message" rather
 * than anything more specific is as far as the evidence goes.
 */
#ifndef AM2_MSGSLOT_H
#define AM2_MSGSLOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AM2_MSGSLOT_COUNT  120u
#define AM2_MSGSLOT_A_OFF  0x420u
#define AM2_MSGSLOT_B_OFF  0x600u

void __cdecl MsgSlotA0(void *comm, uint32_t seq);
void __cdecl MsgSlotA1(void *comm, uint32_t seq);
void __cdecl MsgSlotA2(void *comm, uint32_t seq);
void __cdecl MsgSlotB0(void *comm, uint32_t seq);
void __cdecl MsgSlotB1(void *comm, uint32_t seq);
void __cdecl MsgSlotB2(void *comm, uint32_t seq);

/* 0x00401040. The dword at +0xC of a comm message. Callers are CommReceive,
 * PacketThreadProc and RemovePlayer, so it is the message rather than the comm
 * object; the same callers read the uid at +4. */
uint32_t __cdecl MsgField12(const void *msg);

/* 0x00402E90. Mean of the 32 dwords at +0x3A0 of the comm object, divided with
 * round-toward-zero rather than an arithmetic shift: the original adds 31 when
 * the sum is negative before shifting by 5. A plain `sum >> 5` would round the
 * wrong way for negative totals. Thirty-two samples on the comm object is the
 * shape of a latency or rate average. */
int32_t __cdecl CommMean32(const void *comm);

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
void __cdecl RingPush32(void *comm, uint32_t value);

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
void __cdecl CommRemoveKeyed(void *comm, uint32_t key);

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
void *__cdecl MsgListRemHead(void *list);

/* Original: 0x00410B70, and it names itself -- "ReceiveEndSetupMsg". Log if
 * the comm object's verbosity field is set, then post AM2_WM_SETUP_DONE to the
 * game window. That is all of it: the receive side of this handshake holds no
 * state, it just tells the message pump something arrived, and WndProc's
 * 0x046E case does the work.
 *
 * It cannot be exercised here -- the handshake needs a second player -- so it
 * is verified by reading, like the rest of that group. */
void __cdecl ReceiveEndSetupMsg(void);

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

int msgslot_install(void);

#ifdef __cplusplus
}
#endif

/* 0x00401050. Append a node to the tail of a mutex-guarded list. Twelve
 * callers and multi-threaded; the size complaint above 400 or below zero does
 * not stop the append and is issued while the mutex is still held. */
void __cdecl MsgListAdd(void *list, void *node);

/* 0x00402720. Raise the exit flag, log, and post WM_CLOSE to the game window
 * -- in that order. */
void __cdecl ExitGamePostClose(void);

#endif /* AM2_MSGSLOT_H */
