/* vc6run.cpp -- slim runtime for the RUNNABLE VC6 standalone (make vc6-link
 * --run), the symbol-relative build whose goal is to RUN even though LINK.EXE
 * places sections at non-original VAs.  That it runs at all is the proof the
 * reconstruction uses proper structs/function-pointers, not absolute addresses.
 *
 * It is NOT src/standalone/runtime.cpp: that one pulls in platform.h (GCC
 * attributes VC6 can't parse) and the reconstructed CRT (crt_startup/crt_malloc
 * -- POSIX, GCC-only).  Here the CRT is VC6's real MSVCRT (LIBC.LIB): LIBC's
 * WinMainCRTStartup runs the C++ static initialisers and then WinMain, so no
 * hand-rolled CRT bring-up is needed.  This file supplies only the symbols the
 * AM2_STANDALONE game TUs still reference that MSVCRT does not provide.
 *
 * Everything here is milestone-minimal; the runtime reliances the RUN then
 * faults on (un-migrated globals read by absolute VA, the carried blob's moved
 * self-pointers) are the absolute-address dependencies to migrate away, one at
 * a time, until the image boots.
 */
#include "../game/win32/dplay.h"     /* comm decls + windows types, for the stubs */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>

extern "C" {

/* ---- CRT seams (crt.h, C linkage) -> the genuine MSVC CRT ---------------- */
static void am2_log_impl(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
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

/* Reconstructed-CRT calls the game TUs make by symbol -> the real MSVCRT.
 * Signatures match crt.h exactly (dplay.h pulls it in); CRT_FILE* is treated
 * as an opaque handle -- since ALL file ops route through these wrappers, the
 * real FILE* returned by crt_fopen flows back in unexamined, so the cast is
 * safe. */
uint32_t crt_fread(void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{ return (uint32_t)fread(buf, size, count, (FILE *)f); }
uint32_t crt_fwrite(const void *buf, uint32_t size, uint32_t count, CRT_FILE *f)
{ return (uint32_t)fwrite(buf, size, count, (FILE *)f); }
int32_t crt_rand(void) { return rand(); }
void *crt_malloc(uint32_t n) { return malloc(n); }
CRT_FILE *crt_fopen(const char *path, const char *mode) { return (CRT_FILE *)fopen(path, mode); }
int32_t crt_fclose(CRT_FILE *f) { return fclose((FILE *)f); }
char *crt_fgets(char *s, int32_t n, CRT_FILE *f) { return fgets(s, n, (FILE *)f); }
int32_t crt_findclose(int32_t handle) { return _findclose(handle); }
void *crt_bsearch(const void *key, const void *base, uint32_t num, uint32_t width, crt_compare_fn cmp)
{ return bsearch(key, base, num, width, cmp); }
void crt_qsort(void *base, uint32_t num, uint32_t width, crt_compare_fn cmp)
{ qsort(base, num, width, cmp); }
int32_t crt_sprintf(char *b, const char *f, ...)
{ va_list ap; va_start(ap, f); int r = vsprintf(b, f, ap); va_end(ap); return (int32_t)r; }
int32_t crt_vsprintf(char *b, const char *f, va_list ap) { return (int32_t)vsprintf(b, f, ap); }
int32_t crt_stricmp(const char *a, const char *b) { return _stricmp(a, b); }
char *crt_strlwr(char *s) { return _strlwr(s); }
double crt_strtod(const char *s, char **end) { return strtod(s, end); }
void crt_free(void *p) { free(p); }
void *crt_realloc(void *p, uint32_t n) { return realloc(p, n); }
int32_t crt_atoi(const char *s) { return atoi(s); }
void *crt_memmove(void *d, const void *s, uint32_t n) { return memmove(d, s, n); }
char *crt_strtok(char *s, const char *d) { return strtok(s, d); }
char *crt_strchr(const char *s, int32_t c) { return strchr(s, c); }
char *crt_strstr(const char *s, const char *sub) { return strstr(s, sub); }
char *crt_strncpy(char *d, const char *s, uint32_t n) { return strncpy(d, s, n); }
int32_t crt_strncmp(const char *a, const char *b, uint32_t n) { return strncmp(a, b, n); }
int32_t crt_fseek(CRT_FILE *f, int32_t off, int32_t whence) { return fseek((FILE *)f, off, whence); }
int32_t crt_ftell(CRT_FILE *f) { return ftell((FILE *)f); }
int32_t crt_fflush(CRT_FILE *f) { return fflush((FILE *)f); }
int32_t crt_mkdir(const char *p) { return _mkdir(p); }
int32_t crt_rmdir(const char *p) { return _rmdir(p); }
int32_t crt_remove(const char *p) { return remove(p); }
int32_t crt_chmod(const char *p, int32_t m) { return _chmod(p, m); }
int32_t crt_atexit(CRT_ExitFn fn) { (void)fn; return 0; }  /* boot: handlers unneeded */
int32_t crt_findfirst(const char *spec, CRT_FINDDATA *out) { (void)spec; (void)out; return -1; }
int32_t crt_findnext(int32_t h, CRT_FINDDATA *out) { (void)h; (void)out; return -1; }

/* Standalone glue runtime.cpp defined (all extern "C", __stdcall for the
 * DirectX trio) -> the real Wine DirectX.  win32.h (via dplay.h) already
 * declares DirectDrawCreate/DirectInputCreateA/DirectSoundCreate and the
 * CINTERFACE pointer types; the import libs (ddraw/dinput/dsound) are linked. */
int32_t __stdcall am2_sa_ddraw_create(void *guid, void **out, void *outer)
{ return (int32_t)DirectDrawCreate((GUID *)guid, (LPDIRECTDRAW *)out, (IUnknown *)outer); }
int32_t __stdcall am2_sa_dinput_create(void *inst, uint32_t ver, void **out, void *outer)
{ return (int32_t)DirectInputCreateA((HINSTANCE)inst, (DWORD)ver, (LPDIRECTINPUTA *)out, (IUnknown *)outer); }
int32_t __stdcall am2_sa_dsound_create(void *guid, void **out, void *outer)
{ return (int32_t)DirectSoundCreate((GUID *)guid, (LPDIRECTSOUND *)out, (IUnknown *)outer); }
void *am2_sa_operator_new(size_t n) { return malloc(n ? n : 1); }
void am2_sa_operator_delete(void *p) { free(p); }
void am2_sa_free_army_lists(void) { }
int32_t am2_sa_unimplemented(void) { return 0; }

/* Misc harness/CRT/input symbols the excluded TUs defined -- milestone stubs. */
void am2_crt_use_game(void) { }
void BuildTrigTables(void) { }
void hooklog(const char *fmt, ...) { (void)fmt; }
int restore_multiplayer(void) { return 0; }
void input_overlay_keyboard(uint8_t *state, uint32_t len) { (void)state; (void)len; }
void input_pump(void) { }
uint32_t input_take_events(int32_t kind, void *buf, uint32_t elem, uint32_t max)
{ (void)kind; (void)buf; (void)elem; (void)max; return 0; }

/* trig.cpp is GCC-only; stub the two the game calls (zeros for now). */
float Sin8(int heading) { (void)heading; return 0.0f; }
float Cos8(int heading) { (void)heading; return 0.0f; }

/* The standalone logger the game reaches through ADDR_LOG = &am2_sa_log. */
void am2_sa_log(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr); }

/* Host/harness/comm symbols the excluded runtime.cpp and dplay.cpp defined;
 * milestone stubs so the runnable image links.  am2_host_pump_number is a
 * lockstep/trace hook; patch_replace is unused in a drop-in; SendGameMsg is
 * the comm layer (dplay holdout). */
unsigned int am2_host_pump_number(void) { return 0; }
int am2_trace_window(void) { return 0; }
int patch_replace(unsigned int t, const void *r, const char *n, int a)
{ (void)t; (void)r; (void)n; (void)a; return 0; }
int SendGameMsg(void *msg, int to, int flags) { (void)msg; (void)to; (void)flags; return 0; }

} /* extern "C" */

/* Comm/MP layer (dplay.cpp holdout) -- milestone stubs.  Linkage follows
 * dplay.h: the extern "C" ones resolve as _Name, the mangled rest keep C++
 * decoration, matching what the callers emitted. */
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
int32_t CommGetSessionDesc(void *comm) { (void)comm; return 0; }
int32_t CommPlayerSlot(void *comm, uint32_t id) { (void)comm; (void)id; return 0; }
int32_t CommSlotOfId(void *comm, uint32_t id) { (void)comm; (void)id; return 0; }
int32_t CommRecentTotal(void *comm) { (void)comm; return 0; }
void CommSessionOver(void *comm) { (void)comm; }
void DumpMsgList(void *list) { (void)list; }
void SendPlayerMsg(int32_t arg) { (void)arg; }
void *MsgListCopyByKey(void *list, int32_t key, void *dst) { (void)list; (void)key; (void)dst; return 0; }
int CommSend(void *buf, unsigned int a, unsigned int b, void *dst, unsigned int c)
{ (void)buf; (void)a; (void)b; (void)dst; (void)c; return 0; }

/* The comm subsystem's block, migrated to a placed C array (standalone.h
 * redirects the MSG_LIST/COMM_EVENT/PACKET_* addresses into it).  C++ linkage,
 * matching standalone.h's `extern uint8_t am2_comm_block[1832];`. */
uint8_t am2_comm_block[1832];

/* The original's import address table, migrated to a placed C array
 * (standalone.h redirects ADDR_IAT_* into it).  C++ linkage, matching
 * standalone.h's `extern uint8_t am2_iat[720];`.  Left zero for now: nothing in
 * the AM2_STANDALONE game TUs calls am2_bind_imports (that lived in the excluded
 * runtime.cpp), so calls through it -- Smacker, a few Win32 -- will fault at
 * run time and get wired up when the run reaches them. */
uint8_t am2_iat[720];

/* The game rand's seed, a placed C global (C++ linkage, matches
 * ?am2_rand_seed@@3IA -> unsigned int am2_rand_seed). */
uint32_t am2_rand_seed = 1;

/* Milestone probe: a static initialiser so a run shows it reached _initterm
 * (C++ static init, before WinMain).  LIBC's WinMainCRTStartup runs these then
 * calls WinMain, so "static-init" printing but no further output localises a
 * hang to WinMain or its early callees. */
namespace {
struct Vc6RunProbe { Vc6RunProbe() { if (getenv("VC6RUN_PROBE")) { fprintf(stderr, "vc6run: static-init reached\n"); fflush(stderr); } } };
Vc6RunProbe vc6run_probe;
}
