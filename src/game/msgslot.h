/* msgslot.cpp -- the comm object's two message-slot state arrays.
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

int msgslot_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MSGSLOT_H */
