#ifndef AM2_DPLAY_H
#define AM2_DPLAY_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

/* The harness in src/inject is C; these are C++. Keep the linkage
 * compatible so dllmain.c can still call the install hooks. */
#ifdef __cplusplus
extern "C" {
/* 0x004013B0, one caller. Print one message list under its own mutex. Here
 * rather than in msgslot.cpp because the flat half cannot take a mutex. */
void __cdecl DumpMsgList(void *list);

#endif

/* Creating the DirectPlay objects -- the game's entire networking boundary.
 *
 * It is worth saying why this is so small. Searching the import table for a
 * network library finds nothing: no ws2_32, no wsock32, no dplayx, and the
 * strings are not in the image either. The multiplayer transport is DirectPlay,
 * and DirectPlay is reached through COM, so the only trace of it in the imports
 * is ole32's CoCreateInstance -- twice, and these are both of them.
 *
 * So the whole of the game's outward network surface is here, and the rest of
 * the comm subsystem talks to an interface pointer.
 */

/* Original: 0x0040DD20. Create the IDirectPlay4A and give it a connection.
 *
 * thiscall: the comm object is in ecx and the connection blob is the one stack
 * argument. Drops any previous object first, then creates and -- if given a
 * connection -- initialises it and runs the connected hook. Returns 1 on
 * success, 0 on any failure. A null connection is not an error: it creates the
 * object and stops there, which is what enumerating providers wants. */
int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection);

/* Original: 0x0040DDD0. Create the IDirectPlayLobby3A. stdcall, one argument.
 *
 * Writes the interface through the pointer whether or not it succeeded -- the
 * local it uses is zeroed first, so a failure stores NULL rather than leaving
 * the caller's pointer untouched. Returns 1 on success. */
int32_t __stdcall CreateDirectPlayLobby(LPDIRECTPLAYLOBBY3A *out);

/* Original: 0x004020A0. Shut the comm subsystem down: destroy the four
 * mutex-guarded message lists, signal the packet thread's event, collect its
 * exit code and close the handles. */
void __cdecl CommShutdown(void);

/* Original: 0x0040DCF0. Close the DirectPlay session.
 *
 * Answers 1 when there was no session to close, which is the same answer as
 * closing one successfully -- the caller only wants to know it can carry on. */
int32_t __cdecl CommClose(void);

/* Original: 0x0040DD90. Point the DirectPlay object at a connection.
 *
 * The same InitializeConnection CommCreateDirectPlay makes, reached separately
 * for a session that already has an object. Names the failure in the log. */
int32_t __attribute__((thiscall)) CommInitializeConnection(void *comm,
                                                           void *connection);

/* It lives HERE and not in msgslot.cpp because it calls three comm methods
 * that do -- and msgslot.cpp is in the selftest link, which does not pull in
 * DirectX. Closing those seams turned calls-by-address into link
 * dependencies, which is exactly what checkseams warns about.
 *
 * Original: 0x00411000, "SendGameStartMsg". The host announcing that play
 * begins, and the one place the SHARED RANDOM SEED is chosen: it reads the
 * clock, keeps the value in ADDR_GAME_SEED and sends a copy, so every machine
 * starts its RNG from the same number.
 *
 * Three exits, and they are not a simple chain. If the game has already
 * started it skips straight to the tail. If this machine is not the host it
 * returns outright, doing nothing. Only the host reaches the middle, which
 * fills the record, marks the session description with 0x21, publishes it and
 * sends.
 *
 * The tail runs for the host AND for the already-started case: unpause with
 * mask 0x10000, request state 2, and set the two flags.
 *
 * Two of the original's own oddities are kept. The opening log is NOT gated on
 * the comm verbosity field, unlike every other function in this group. And the
 * second log says "Seed is %d" while a literal 0 is pushed for it, so the seed
 * it reports is always zero -- the value it actually used is never printed. */
void __cdecl SendGameStartMsg(void);

/* Original: 0x0040E630. Set the session description. */
int32_t __attribute__((thiscall)) CommSetSessionDesc(void *comm, void *desc,
                                                     uint32_t flags);

/* Original: 0x0040E5A0. Fetch the current session description into the comm
 * object, replacing whatever was there.
 *
 * Asked for twice on purpose: DirectPlay will not say how large the description
 * is except by refusing to write it, so the first call passes no buffer and
 * reads the size out of the complaint. Returns 1 on success. */
int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm);

/* Original: 0x0040FA00, one caller -- ShowMpResult, which tail-jumps to it.
 * Put a finished game back in the lobby: drop the departed players, clear
 * DPSESSION_NEWPLAYERSDISABLED and DPSESSION_JOINDISABLED, reset the ready
 * flags and empty the menu message log. */
void __attribute__((thiscall)) CommReopenSession(void *comm);

/* Original: 0x0040DB80 and 0x0040DCC0, thiscall on the single global comm
 * object. The game's entire registry surface lives in these two: the key is
 * created at static-initialisation time and closed again from atexit, and
 * nothing is ever stored under it. See dplay.cpp. */
/* 0x0040DB40. The CRT initialiser-table entry for the comm object. */
int32_t __cdecl CommGlobalInit(void);

/* 0x0040DB50 / 0x0040DB70. The two thunks that name the object. */
void *__cdecl CommGlobalCtorThunk(void);
void __cdecl CommGlobalDtorThunk(void);

/* 0x0040DB60. Register the comm teardown with the CRT's atexit. */
int32_t __cdecl CommGlobalAtExit(void);

void *__attribute__((thiscall)) CommConstruct(void *comm);
void  __attribute__((thiscall)) CommDestruct(void *comm);

/* Original: 0x0040E3B0, thiscall. Enumerate the DirectPlay sessions matching
 * the game's application GUID, into the object the caller supplies.
 *
 * Asynchronous and filtered by application only -- guidInstance is left zero --
 * so it asks for every Army Men II session on the transport. Answers non-zero
 * when the request was accepted, which is not the same as any session having
 * been found; the callback at 0x0040E280 does the finding. */
int32_t __attribute__((thiscall)) CommEnumSessions(void *comm, void *list);

/* Original: 0x0040E530, thiscall. List the DirectPlay service providers that
 * can carry this game, as menu rows, with "Play Against Computer Only" always
 * appended last. The callback drops the two dead matchmaking services. */
int32_t __attribute__((thiscall)) CommEnumConnections(void *comm, void *list);

/* Original: 0x0040DFC0, thiscall. Create the DirectPlay session -- START A WAR.
 *
 * DPOPEN_CREATE, host migration on, four players, named by whatever the player
 * typed. A local game skips the whole thing and reports success, because there
 * is no session to make. */
int32_t __attribute__((thiscall)) CommOpenSession(void *self, const char *name);

/* Original: 0x0040DE10. Create the local player in the open session and record
 * its DPID. Declared here now that HostBattle calls it directly. */
int32_t __attribute__((thiscall)) CommCreatePlayer(void *comm, const char *name,
                                                   HANDLE event, void *data,
                                                   DWORD length);

/* Original: 0x0040EA40, thiscall. Give the DirectPlay connection back.
 *
 * Frees the two heap buffers, releases the IDirectPlay4A and the lobby
 * interface, destroys every remote player, and puts the four player slots back
 * to the state CommConstruct built them in. Ours is removed after the loop
 * rather than inside it, so it outlives the slots that name it.
 *
 * Returns 0 always; every caller ignores it. */
int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm);

/* Original: 0x0040E660, thiscall. Ask the provider what it can carry and trim
 * the comm object's buffer sizes to fit.
 *
 * UNREACHABLE in this build: its only caller is CommCreateDirectPlay's
 * `if (connection)` branch, and the one call to that passes a literal 0. See
 * dplay.cpp. */
int32_t __attribute__((thiscall)) CommOnConnected(void *self);

/* Original: 0x0040E200. Empty the four player slots and ask DirectPlay to
 * enumerate the session's players into them. Answers 1 on DP_OK. */
int32_t __cdecl CommEnumPlayers(void);

/* Original: 0x004021A0, from CommConstruct. Bring the packet subsystem up:
 * four message lists, 400 buffers, two events and the packet thread. Named
 * from its own error string, "Error launching packet thread". */
int32_t __cdecl StartPacketThread(void);

/* Original: 0x00401000. Clear a message list and give it a mutex. */
int32_t __cdecl MsgListInit(void *list);

/* Original: 0x00402170. Close a handle and forget it. */
void __cdecl EventClose(void *holder);

/* 0x0040ED10, thiscall. StartIntro brings the lobby up before deciding
 * anything, with the comm object already in ecx from the field it just read. */
int32_t __attribute__((thiscall)) CommLobbyStart(void *comm);

/* 0x0040F130 and 0x0040FB70, one caller each and both named in orig.h before
 * they were written. The first sets the lobby flag at comm+0x404; the second
 * is a thiscall that tail-calls CommSendLobbyProperty with 1, which is the
 * value that says the session is over. */
void __cdecl CommMarkLobbied(void);
void __attribute__((thiscall)) CommSessionOver(void *comm);

/* 0x0040F380, thiscall. Clears the four 30-sample traffic rings and the six
 * bandwidth counters, and restarts the window they are measured over. The two
 * TIME rings are stamped with the current tick, not zeroed. */
/* 0x00403280. Reports "COMM ERROR: NO BUFFERS" once and posts WM_CLOSE. The
 * latch is the point: buffers run out repeatedly once they run out at all. */
void __cdecl CommNoBuffers(void);

void __attribute__((thiscall)) CommResetStats(void *comm);

/* 0x0040F400. Prints the four statistics lines CommResetStats clears. Each
 * line is gated on its own denominator; nothing prints when nothing moved. */
void __attribute__((thiscall)) CommReportStats(void *comm);

/* 0x0040FB80. Eight bytes: forwards to CommSendLobbyProperty with id 2. */
void __attribute__((thiscall)) CommPublishResult(void *comm);

/* 0x0040F320, six callers, sixteen bytes: a thiscall that pushes its one
 * argument and tail-calls 0x0040F160, which is thiscall too and so already has
 * the object in ecx. A pass-through under a second name. */
int32_t __attribute__((thiscall)) CommPlayerSlot(void *comm, uint32_t id);

/* 0x0040F140, three callers -- CommMarkLobbied's counterpart. Clear the same
 * field and tail-jump to CommDropDirectPlay.
 *
 * THISCALL, which is not obvious from a body that names no `this`: all three
 * call sites load ecx with the comm object first, and the tail jump hands it
 * on. The store still goes through the GLOBAL rather than through `this` --
 * the same object either way, and reproduced as written. */
int32_t __attribute__((thiscall)) CommDropSession(void *comm);

/* 0x0040F160, 15 callers -- what CommPlayerSlot forwards to. The slot holding
 * this DirectPlay id, or 0.
 *
 * THISCALL by its `ret 4`, and it ignores `this` entirely: it loads the comm
 * object from the global instead, which is the same shape CommDropSession has.
 * Not found and slot 0 share an answer, as they do in CommSlotForArmy. */
int32_t __attribute__((thiscall)) CommSlotOfId(void *comm, uint32_t id);

/* 0x0040F520, thiscall. A windowed sum over the thirty rate slots. */
int32_t __attribute__((thiscall)) CommRecentTotal(void *comm);

/* 0x00410F70, two callers. Fetch the session description, log it when the comm
 * object is verbose, publish the player count and enumerate. */
void __cdecl OnLobbySlave(void);

int dplay_install(void);

#ifdef __cplusplus

/* 0x004027F0, five callers. Give a DirectPlay id a player record -- the
 * flow-control code's "FlowQ". Two scans: one for an existing record, one for
 * a free slot. Answers 1 either way, 0 when all six are taken. */
int32_t __cdecl CommRegisterSelf(uint32_t id);

}
#endif

/* 0x004012C0, one caller. Find the node whose key matches, copy its body to
 * dst under the list's mutex, and answer dst or NULL. */
void *__cdecl MsgListCopyByKey(void *list, int32_t key, void *dst);

/* 0x00401330, one caller. Find the first node with any wanted flag set, clear
 * those bits, copy its body to dst, and answer the bits taken. */
int32_t __cdecl MsgListTakeFlags(void *list, void *dst);

/* 0x00403050, one caller. Drain up to THREE messages off the resend list,
 * sending each to every player whose resend mask wants it. Names itself in
 * its own log lines; the old ADDR_COMM_FRAME_POST_C said where it sits in
 * the frame. Cold: needs a live DirectPlay session. */
void __cdecl ProcessResendQueue(void);

/* 0x004029B0, seven callers. Tear down one player's flow queue: reclaim
 * everything still queued for them by treating it as acknowledged, and free
 * the record. Can UNPAUSE the game when that refills the buffer pool. */
int32_t __cdecl DestroyFlow(uint32_t id);

/* 0x00401150, one caller. Insert a node in ascending key order under the
 * list's mutex, and answer the node. */
void *__cdecl MsgListInsert(void *list, void *node);

/* 0x0040F790, two callers. Take a departed player out: drop it, clear its
 * slot, release the three pause reasons it held, mark its army ready. */
int32_t __attribute__((thiscall)) CommPlayerLeft(void *comm, int32_t id);

/* 0x0040F640, three callers. Take a player out of the comm object: drop the
 * count, tell the packet layer, close the gap in the record array by copying
 * eleven named fields down a slot, and stamp the six fields that record who
 * went. Always answers 1. */
int32_t __attribute__((thiscall)) CommRemovePlayer(void *self, int32_t id);

#endif /* AM2_DPLAY_H */
