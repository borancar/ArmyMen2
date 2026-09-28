/* glue.cpp -- CRT-seam bindings and milestone stubs for the VC6 LINK milestone.
 *
 * The reconstruction reaches the game's CRT through function-pointer seams
 * (crt.h's AM2_ORIGFN: am2_malloc, am2_free, am2_log, ...), all C linkage.
 * The injected build fills them from the game's own statically-linked MSVC
 * CRT; the native build points them at the reconstructed CRT.  For the VC6
 * standalone the choice (per the build decision) is VC6's real MSVCRT, so each
 * seam is bound to the genuine CRT here.  Compiled by VC6 CL, linked against
 * LIBC.LIB.
 *
 * It also STUBS the symbols the reconstruction references that this image
 * otherwise lacks: the whole comm/multiplayer layer (defined in dplay.cpp,
 * which VC98's DirectX 6.0 headers can't compile -- IDirectPlay4A/Lobby3), the
 * trig tables (trig.cpp is GCC-only), the injected harness's hooklog /
 * restore_multiplayer / patch_replace, and the host trace/pump hooks.
 *
 * These are MILESTONE stubs: they make the image LINK into a valid PE, which
 * proves the VC6 compile -> blob -> LINK.EXE pipeline end to end.  They are NOT
 * a runtime -- a correct-running image needs the comm and trig layers really
 * built and the blob's baked code-pointers rewritten (the fixups dataset).
 * Including dplay.h gives every comm stub the exact linkage/decoration its
 * callers expect (the extern "C" ones stay C, the rest keep C++ mangling).
 */
#include "../game/win32/dplay.h"     /* comm decls + windows types (HANDLE/DWORD/GUID) */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <stdarg.h>

/* ---- CRT seams (crt.h, C linkage) -> the genuine MSVC CRT ---------------- */
extern "C" {
static void am2_log_impl(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
void *(*am2_malloc)(size_t) = malloc;
void *(*am2_realloc)(void *, size_t) = realloc;
void  (*am2_free)(void *) = free;
void  (*am2_log)(const char *, ...) = am2_log_impl;
long  (*am2_strtol)(const char *, char **, int) = strtol;
char *(*am2_strtok)(char *, const char *) = strtok;
int   (*am2_sprintf)(char *, const char *, ...) = sprintf;
int   (*am2_chdir)(const char *) = _chdir;
char *(*am2_getcwd)(char *, int) = (char *(*)(char *, int))_getcwd;

/* ---- host / trace / CRT hooks (platform.h + crt.h, C linkage) ------------ */
unsigned int am2_host_pump_number(void) { return 0; }
int am2_trace_window(void) { return 0; }
void am2_crt_use_game(void) { }

/* ---- injected-harness stubs (unused in a drop-in image) ------------------ */
int patch_replace(unsigned int target, const void *replacement, const char *name, int nargs)
{ (void)target; (void)replacement; (void)name; (void)nargs; return 0; }
void hooklog(const char *fmt, ...) { (void)fmt; }
int restore_multiplayer(void) { return 0; }

/* ---- trig (trig.cpp is GCC-only) ----------------------------------------- */
void BuildTrigTables(void) { }
float Sin8(int heading) { (void)heading; return 0.0f; }
float Cos8(int heading) { (void)heading; return 0.0f; }
} /* extern "C" */

/* ---- comm / multiplayer layer (dplay.cpp holdout) ------------------------
 * Linkage follows dplay.h: the extern "C" ones resolve as _Name, the rest keep
 * C++ mangling -- matching what the callers emitted.  All return 0 / void. */
int32_t CommClose(void) { return 0; }
int32_t CommCreateDirectPlay(void *comm, void *connection) { (void)comm; (void)connection; return 0; }
int32_t CommCreatePlayer(void *comm, const char *name, HANDLE event, void *data, DWORD length)
{ (void)comm; (void)name; (void)event; (void)data; (void)length; return 0; }
int32_t CommDropDirectPlay(void *comm) { (void)comm; return 0; }
int32_t CommDropSession(void *comm) { (void)comm; return 0; }
int32_t CommEnumConnections(void *comm, void *list) { (void)comm; (void)list; return 0; }
int32_t CommEnumSessions(void *comm, void *list) { (void)comm; (void)list; return 0; }
int32_t CommInitializeConnection(void *comm, void *connection) { (void)comm; (void)connection; return 0; }
int32_t CommJoinSession(void *comm, const GUID *instance) { (void)comm; (void)instance; return 0; }
int32_t CommLobbyStart(void *comm) { (void)comm; return 0; }
void CommNoBuffers(void) { }
int32_t CommOpenSession(void *self, const char *name) { (void)self; (void)name; return 0; }
void CommPublishResult(void *comm) { (void)comm; }
void CommReopenSession(void *comm) { (void)comm; }
void CommReportStats(void *comm) { (void)comm; }
void CommResetStats(void *comm) { (void)comm; }
int32_t CommSetSessionDesc(void *comm, void *desc, uint32_t flags) { (void)comm; (void)desc; (void)flags; return 0; }
void CheckPlayerTimeout(void) { }
void OnLobbySlave(void) { }
void SendGameStartMsg(void) { }
int32_t CommPlayerLeft(void *comm, int32_t id) { (void)comm; (void)id; return 0; }
int32_t CommRemovePlayer(void *self, int32_t id) { (void)self; (void)id; return 0; }
int32_t DestroyFlow(uint32_t id) { (void)id; return 0; }
void ProcessResendQueue(void) { }
int32_t CommRegisterSelf(uint32_t id) { (void)id; return 0; }
void CommSystemMessage(void *msg, int32_t size, int32_t from, int32_t to)
{ (void)msg; (void)size; (void)from; (void)to; }
int32_t SendGameMsg(void *msg, int32_t to, int32_t flags) { (void)msg; (void)to; (void)flags; return 0; }
int32_t CommGetSessionDesc(void *comm) { (void)comm; return 0; }
int32_t CommPlayerSlot(void *comm, uint32_t id) { (void)comm; (void)id; return 0; }
int32_t CommSlotOfId(void *comm, uint32_t id) { (void)comm; (void)id; return 0; }
int32_t CommRecentTotal(void *comm) { (void)comm; return 0; }
void CommSessionOver(void *comm) { (void)comm; }
void DumpMsgList(void *list) { (void)list; }
void SendPlayerMsg(int32_t arg) { (void)arg; }
void *MsgListCopyByKey(void *list, int32_t key, void *dst) { (void)list; (void)key; (void)dst; return 0; }

/* CommSend has no header prototype (only a comment in commmsg.h); its callers
 * emit the C++ mangling ?CommSend@@YAHPAXII0I@Z, so declare+define it here to
 * match: int __cdecl CommSend(void*, unsigned, unsigned, void*, unsigned). */
int CommSend(void *buf, unsigned int a, unsigned int b, void *dst, unsigned int c)
{ (void)buf; (void)a; (void)b; (void)dst; (void)c; return 0; }
