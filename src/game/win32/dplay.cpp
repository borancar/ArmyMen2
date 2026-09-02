/* DirectPlay object creation -- reconstructed from ArmyMen2.exe.
 *
 *   CommCreateDirectPlay    0x0040DD20  1 call site, thiscall
 *   CreateDirectPlayLobby   0x0040DDD0  1 call site, stdcall
 *
 * These are the only two CoCreateInstance sites in the game and, between them,
 * its entire outward network surface. See dplay.h for why that surface is two
 * functions long.
 *
 * It also holds the comm subsystem's teardown, which is the other half of the
 * same boundary: DirectPlay is created here and the threads that feed it are
 * stopped here.
 *
 * The identification came out of the GUIDs rather than out of any name. The
 * game carries its own copies in .rdata, and they are CLSID_DirectPlay with
 * IID_IDirectPlay4A, and CLSID_DirectPlayLobby with IID_IDirectPlayLobby3A.
 * The vtable slot the first one then calls, 38, is IDirectPlay4's
 * InitializeConnection -- which is the check that the interface really is what
 * the IID says, because slot 38 would be off the end of any smaller one.
 *
 * The first is the project's first reconstructed thiscall function. GCC's
 * `thiscall` attribute is exactly MSVC's convention: first argument in ecx, the
 * rest on the stack, callee cleans. `ret 4` and one stack argument agree.
 */

#include "../gameproc.h"
#include "../map.h"      /* the level table -- reconstructed */
#include "widget.h"   /* ListAdd */
#include "dplay.h"
#include "../armymsg.h"   /* SendGamePause */
#include "frame.h"
#include "cdcheck.h"
#include "../msgslot.h"
#include "../misc.h"     /* XorChecksum -- reconstructed */
#include "../objtable.h"
#include "../commmsg.h"  /* CommInitDefaults -- reconstructed */
#include "../../inject/patch.h"
#include "winmain.h"   /* Ticks */

/* The CRT's atexit, above the CRT line and not on the patch list. */
typedef int32_t (__cdecl *AM2_AtExitFn)(void (__cdecl *)(void));
#define orig_atexit (*(AM2_AtExitFn)AM2_IMAGE(ADDR_CRT_ATEXIT))
#include "../image.h"  /* AM2_IMAGE */

#include <stdint.h>

static_assert(CLSCTX_INPROC_SERVER == 1, "CLSCTX_INPROC_SERVER");

#define kCLSID_DirectPlay      (*(const CLSID *)(uintptr_t)ADDR_CLSID_DIRECTPLAY)
#define kIID_IDirectPlay4A     (*(const IID *)(uintptr_t)ADDR_IID_DIRECTPLAY4A)
#define kCLSID_DirectPlayLobby (*(const CLSID *)(uintptr_t)ADDR_CLSID_DPLAY_LOBBY)
#define kIID_IDirectPlayLobby3A (*(const IID *)(uintptr_t)ADDR_IID_DPLAY_LOBBY3A)

/* Both take the comm object in ecx and nothing else. */
typedef void (__attribute__((thiscall)) *am2_comm_method_fn)(void *comm);

typedef void (__cdecl *am2_destroy_list_fn)(void *list);

#define g_commEvent    (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT)
#define g_commEvent2   (*(HANDLE *)(uintptr_t)ADDR_COMM_EVENT_2)
#define g_packetThread (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)

/* The interface pointer lives inside the comm object rather than in a global,
 * so it is reached through `this` like any other member. */
static inline LPDIRECTPLAY4A *DirectPlaySlot(void *comm)
{
    return (LPDIRECTPLAY4A *)((uint8_t *)comm + COMM_OFF_DPLAY);
}

/* All defined further down; called from here. */
int32_t __attribute__((thiscall)) CommOnConnected(void *self);
int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm);
int32_t __attribute__((thiscall)) CommCreatePlayer(void *comm, const char *name,
                                                   HANDLE event, void *data,
                                                   DWORD length);

int32_t __attribute__((thiscall)) CommCreateDirectPlay(void *comm, void *connection)
{
    LPDIRECTPLAY4A *slot = DirectPlaySlot(comm);
    HRESULT         hr;

    /* Whatever is there now goes first; creating a second one over the top
     * would leak the interface and the socket underneath it. */
    if (*slot)
        CommDropDirectPlay(comm);

    hr = CoCreateInstance(kCLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER,
                          kIID_IDirectPlay4A, (LPVOID *)slot);
    if (hr != S_OK)
        return 0;

    /* A null connection means "create the object and stop" -- which is what
     * enumerating the available providers needs, since that is done on an
     * object that has not been pointed at one yet. */
    if (connection) {
        hr = IDirectPlayX_InitializeConnection(*slot, connection, 0);
        if (hr != S_OK)
            return 0;
        CommOnConnected(comm);
    }
    return 1;
}

int32_t __stdcall CreateDirectPlayLobby(LPDIRECTPLAYLOBBY3A *out)
{
    LPDIRECTPLAYLOBBY3A lobby = NULL;
    HRESULT             hr;

    hr = CoCreateInstance(kCLSID_DirectPlayLobby, NULL, CLSCTX_INPROC_SERVER,
                          kIID_IDirectPlayLobby3A, (LPVOID *)&lobby);
    /* Stored either way. The local was zeroed first, so a failure hands back
     * NULL rather than leaving the caller holding whatever it had. */
    *out = lobby;
    return hr == S_OK;
}

/* Original: 0x004020A0, 1 call site. Shut the comm subsystem down.
 *
 * Four message lists go first -- each one closes its own mutex -- then the
 * packet thread is woken by signalling its event and its exit code collected,
 * and finally both event handles are closed.
 *
 * The wait is not really a wait. GetExitCodeThread is retried until the *call*
 * succeeds, which it does immediately; the loop does not look at the code it
 * gets back, so a thread still running reports STILL_ACTIVE and the log says it
 * "exited" anyway. That is what the original does, and it is why the shutdown
 * lines appear in every run's log whether or not the thread had finished. */
void __cdecl CommShutdown(void)
{
    DWORD exitCode;

    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_POOL);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_B);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ);
    EventClose((void *)(uintptr_t)ADDR_MSG_LIST_DELAYED);

    orig_log("Setting Event 0 \n");
    SetEvent(g_commEvent);

    if (g_packetThread) {
        /* Retried until the call itself succeeds -- see the note above. */
        while (!GetExitCodeThread(g_packetThread, &exitCode))
            ;
        orig_log("Packet Thread Exited with return code %d\n", exitCode);
        CloseHandle(g_packetThread);
    }

    if (g_commEvent)
        CloseHandle(g_commEvent);
    g_commEvent = NULL;
    if (g_commEvent2)
        CloseHandle(g_commEvent2);
    g_commEvent2 = NULL;
}

/* The comm object lives behind a pointer; these three reach the interface the
 * same way CommCreateDirectPlay does. */
#define g_commObject (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_commEnumCount (*(int32_t *)(uintptr_t)ADDR_COMM_ENUM_COUNT)
#define g_hwnd          (*(HWND *)(uintptr_t)ADDR_HWND)

int32_t __cdecl CommClose(void)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(g_commObject);

    /* Nothing open is as good as closed, from the caller's point of view. */
    if (!dp)
        return 1;
    return IDirectPlayX_Close(dp) == DP_OK;
}

int32_t __attribute__((thiscall)) CommInitializeConnection(void *comm,
                                                           void *connection)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);

    /* No object, so nothing to point anywhere -- and the original answers 0
     * here by falling through with the null still in the return register. */
    if (!dp)
        return 0;

    if (IDirectPlayX_InitializeConnection(dp, connection, 0) != DP_OK) {
        orig_log("Unable to intialize connection\n");
        return 0;
    }
    return 1;
}

int32_t __attribute__((thiscall)) CommSetSessionDesc(void *comm, void *desc,
                                                     uint32_t flags)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);

    if (!dp)
        return 0;
    return IDirectPlayX_SetSessionDesc(dp, (LPDPSESSIONDESC2)desc, flags) == DP_OK;
}

/* The SDK spells this one unsigned and HRESULT is signed, so the comparison
 * needs a cast to keep -Wsign-compare quiet. The value is checked here rather
 * than trusted. */
static_assert((uint32_t)DPERR_BUFFERTOOSMALL == 0x8877001Eu,
              "DPERR_BUFFERTOOSMALL");


int32_t __attribute__((thiscall)) CommGetSessionDesc(void *comm)
{
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(comm);
    void         **slot = (void **)((uint8_t *)comm + COMM_OFF_SESSION_BUF);
    DWORD          size;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* Whatever was fetched last time goes first -- on the game's heap, so
     * through the game's free. */
    if (*slot) {
        orig_free(*slot);
        *slot = NULL;
    }

    /* DirectPlay will not say how big the description is except by refusing to
     * write it, so ask once with no buffer and read the size out of the
     * complaint. Anything other than that complaint is the final answer. */
    hr = IDirectPlayX_GetSessionDesc(dp, NULL, &size);
    if (hr == (HRESULT)DPERR_BUFFERTOOSMALL) {
        *slot = orig_malloc(size);
        if (!*slot)
            return 0;
        hr = IDirectPlayX_GetSessionDesc(*DirectPlaySlot(comm), *slot, &size);
    }
    return hr == DP_OK;
}

/* ---- the comm object's own lifetime ------------------------------------
 *
 * These two are the game's entire registry surface. There is no third call:
 * the import table lists RegCreateKeyExA and RegCloseKey once each, and both
 * sites are below, and there is no RegQueryValue or RegSetValue anywhere in
 * the image. The key is created with KEY_ALL_ACCESS at startup, the handle is
 * parked at +0x204, and it is closed again at exit without a single value ever
 * being read or written through it. The `\1.00` subkey that exists under it in
 * a real install is the installer's, not the game's.
 *
 * The constructor runs from the CRT's static-initialiser table before WinMain,
 * and the destructor is handed to atexit by a thunk at 0x0040DB60, so a run
 * that is killed rather than quit never reaches it. Both are thiscall on the
 * single global at ADDR_COMM_OBJECT; the two thunks that supply ecx are left
 * original, which is why the bodies are patched and not their entry points.
 *
 * Most of the constructor is field initialisation whose meaning is not
 * recoverable from one function, and it is transcribed rather than
 * interpreted -- offsets and constants exactly as the original writes them,
 * in the order it writes them. */

/* The two constants the original hardcodes. HKEY_LOCAL_MACHINE is a cast
 * pointer and so cannot be static_asserted; it is 0x80000002, which is the
 * value pushed at 0x0040DC27. */
static_assert(KEY_ALL_ACCESS == 0xF003F, "KEY_ALL_ACCESS");

#define comm_u32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))

typedef void (__cdecl *am2_comm_void_fn)(void);
/* The logger is called with no arguments at all. It is stubbed to `ret` in the
 * shipping image, so this is a no-op either way, but it is reproduced rather
 * than dropped -- see ADDR_LOG. */
#define orig_log_noargs         (*(am2_comm_void_fn)ADDR_LOG)

/* Four player records, back to back. Only the index and a timestamp are ever
 * set to anything; the rest is cleared. These were a private COMM_SLOT_BASE
 * and COMM_SLOT_STRIDE here, both correct, while orig.h's COMM_OFF_PLAYERS was
 * twelve bytes late -- so the file that had the right base kept it to itself.
 * One pair of names now. */

/* DestroyFlow is reconstructed now -- the packet layer's own bookkeeping.
 * Declared here rather than beside its other caller further down because
 * CommRemovePlayer needs it first. */
/* am2_remove_player_fn went with its seam: DestroyFlow is ours. Its two
 * spellings differed only in signedness, which is why neither was wrong. */

/* CommRegisterSelf -- original 0x004027F0, five callers. Give a DirectPlay id
 * a player record -- what the flow-control code calls a FlowQ and CommSend
 * calls a player, one thing under two of the program's own vocabularies.
 *
 * TWO SCANS, AND THE SECOND IS NOT A CONTINUATION OF THE FIRST. It walks all
 * six records looking for this id and answers 1 if it finds one, then walks
 * them again from the start looking for a free slot. So a re-registration is
 * free and a first registration costs two passes; the original does not fold
 * them into one and neither does this.
 *
 * Both scans bound on ADDR_DEFAULT_PLAYER_EVT, the next global after the
 * table -- the same "the next global is the bound" evidence that sized the
 * event registration table.
 *
 * TWO BIT MASKS, FOUR BITS APART. PLAYER_REC_OFF_OWN_BIT is `1 << slot` and
 * goes into ADDR_PLAYER_SLOT_MASK; PLAYER_REC_OFF_WANT_BIT is
 * `1 << (slot + 4)` and goes into ADDR_MSG_WANTED_FLAGS. One word holds both
 * families and the shift is what keeps them apart, which is why six slots fit
 * in a byte's worth of each.
 *
 * +0x88 IS WRITTEN TWICE, 1 and then the comm object's COMM_OFF_IS_HOST. The
 * first store is dead and is reproduced: a reconstruction that dropped it
 * would differ in the one place a debugger would look.
 *
 * The field list was EXTRACTED from the disassembly by script rather than
 * transcribed -- thirty stores through two zeroed registers is the shape a
 * hand copy gets wrong, and tools/posecheck.py exists because of one that did.
 */
int32_t __cdecl CommRegisterSelf(uint32_t id)
{
    uint8_t *table = (uint8_t *)(uintptr_t)ADDR_PLAYER_RECORDS;
    uint8_t *end   = (uint8_t *)(uintptr_t)ADDR_DEFAULT_PLAYER_EVT;
    uint8_t *rec;
    int32_t  slot;
    uint32_t own, want;

    for (rec = table; rec < end; rec += AM2_PLAYER_RECORD_BYTES)
        if (*(const uint32_t *)rec == id)
            return 1;

    slot = 0;
    for (rec = table; rec < end; rec += AM2_PLAYER_RECORD_BYTES, slot++)
        if (*(const uint32_t *)rec == 0)
            break;

    if (rec >= end) {
        orig_log((const char *)(uintptr_t)ADDR_STR_FLOWQ_FAILED, id);
        return 0;
    }

    if (comm_u32(*(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT,
                 COMM_OFF_VERBOSE) != 0)
        orig_log((const char *)(uintptr_t)ADDR_STR_FLOWQ_MAKING, id);

    own  = 1u << slot;
    want = 1u << (slot + AM2_PLAYER_WANT_SHIFT);

    *(uint32_t *)(rec + 0x00) = id;
    *(uint32_t *)(rec + 0x04) = 0;
    *(uint32_t *)(rec + 0x08) = 0;
    *(uint32_t *)(rec + 0x0C) = 0;
    *(uint32_t *)(rec + PLAYER_REC_OFF_FLAG_94) = 1;
    *(uint32_t *)(rec + PLAYER_REC_OFF_OWN_BIT) = own;
    *(uint32_t *)(uintptr_t)ADDR_PLAYER_SLOT_MASK |= own;
    *(uint32_t *)(rec + PLAYER_REC_OFF_WANT_BIT) = want;
    *(uint32_t *)(rec + 0x10) = 0;
    *(uint32_t *)(rec + 0x1C) = 0;
    *(uint32_t *)(rec + 0x20) = 0;
    *(uint32_t *)(rec + 0x28) = 0;
    *(uint32_t *)(rec + 0x30) = 0;
    *(uint32_t *)(rec + 0x34) = 0;
    *(uint32_t *)(rec + 0x38) = 0;
    *(uint32_t *)(rec + 0x40) = 0;
    *(uint32_t *)(rec + 0x44) = 0;
    *(uint32_t *)(rec + 0x48) = 0;
    *(uint32_t *)(rec + 0x4C) = 0;
    *(uint32_t *)(uintptr_t)ADDR_MSG_WANTED_FLAGS |= want;
    *(uint32_t *)(rec + PLAYER_REC_OFF_MADE_AT) = GetTickCount();
    *(uint32_t *)(rec + PLAYER_REC_OFF_IS_HOST) = 1;
    *(uint32_t *)(rec + 0x0C) = 0;
    *(uint32_t *)(rec + 0x8C) = 0;
    *(uint32_t *)(rec + 0x9C) = 0;
    *(uint32_t *)(rec + 0xA0) = 0;
    *(uint32_t *)(rec + 0xA4) = 0;
    *(uint32_t *)(rec + 0xA8) = 0;
    *(uint32_t *)(rec + 0xAC) = 0;
    *(uint32_t *)(rec + 0xB0) = 0;
    *(uint32_t *)(rec + 0xB4) = 0;

    *(uint32_t *)(rec + PLAYER_REC_OFF_IS_HOST) =
        comm_u32(*(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT,
                 COMM_OFF_IS_HOST);

    {
        uint32_t *a = (uint32_t *)(rec + PLAYER_REC_OFF_RING_A);
        uint32_t *b = (uint32_t *)(rec + PLAYER_REC_OFF_RING_B);
        int32_t   i;

        for (i = 0; i < AM2_PLAYER_RING_LEN; i++) {
            a[i] = 0;
            b[i] = 0;
        }
    }

    return 1;
}


/* CommRemovePlayer -- original 0x0040F640, three callers. Take a player out of
 * the comm object: drop the count, tell the packet layer, close the gap in the
 * record array, and stamp the six fields that record who went.
 *
 * IT IS WHAT SETTLED COMM_OFF_PLAYERS. The compaction copies fields at +0x00,
 * +0x04 and +0x08 -- twelve bytes BELOW what orig.h used to call the start of
 * the record -- which is what showed the base was the NAME field rather than
 * the record. With the base corrected every offset in the loop lands on a
 * field the COMM_SLOT_OFF_ family already names.
 *
 * IT DOES NOT MOVE THE WHOLE RECORD. Eleven fields are copied one slot down
 * and the other 68 bytes are left where they are, so a compacted slot keeps
 * whatever the departing player had in the fields nobody listed. Reproduced; a
 * memmove of the stride would be tidier and would differ.
 *
 * IT READS ONE SLOT PAST THE ARRAY. The loop runs 4 - slot times, so removing
 * slot 0 copies 3 from 4 -- and there is no record 4. The read lands in the
 * comm object's own fields just past them. That is the original's behaviour
 * and is kept.
 *
 * AND A SLOT OF -1 IS WORSE. CommPlayerSlot answers -1 for a player it cannot
 * find, the guard here is only `slot >= 4`, and -1 passes it -- so the loop
 * starts one record BEFORE the array and writes there. Nothing in the three
 * callers is known to do that; it is recorded rather than defended against,
 * because a guard we added would be a difference.
 *
 * THE GLOBAL AND `this` ARE BOTH USED, and not interchangeably: the count and
 * the log read ADDR_COMM_OBJECT while the records and the six fields are
 * reached through `this`. They are the same object on every path this project
 * can drive, which is exactly why the distinction has to be preserved rather
 * than tidied.
 */
int32_t __attribute__((thiscall)) CommRemovePlayer(void *self, int32_t id)
{
    uint8_t *me = (uint8_t *)self;
    uint8_t *comm;
    int32_t  slot = CommPlayerSlot(self, id);
    int32_t  index;
    int32_t  n;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    comm_u32(comm, COMM_OFF_PLAYER_COUNT) -= 1;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    orig_log((const char *)(uintptr_t)ADDR_STR_REMOVE_PLAYER,
            comm_u32(comm, COMM_OFF_PLAYER_COUNT));

    if (id != -1)
        DestroyFlow((uint32_t)id);

    comm  = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    index = *(const int32_t *)(comm + COMM_OFF_PLAYERS
                               + slot * COMM_PLAYER_STRIDE
                               + COMM_SLOT_OFF_INDEX);

    if (slot < AM2_COMM_PLAYERS) {
        for (n = AM2_COMM_PLAYERS - slot; n > 0; n--) {
            uint8_t *dst = me + COMM_OFF_PLAYERS
                           + (slot + (AM2_COMM_PLAYERS - slot) - n)
                             * COMM_PLAYER_STRIDE;
            uint8_t *src = dst + COMM_PLAYER_STRIDE;

            *(int32_t *)(dst + COMM_SLOT_OFF_INDEX) =
                *(const int32_t *)(src + COMM_SLOT_OFF_INDEX);
            *(int32_t *)(dst + COMM_SLOT_OFF_ID) =
                *(const int32_t *)(src + COMM_SLOT_OFF_ID);
            *(int32_t *)(dst + COMM_SLOT_OFF_READY_TO_LOAD) =
                *(const int32_t *)(src + COMM_SLOT_OFF_READY_TO_LOAD);
            *(int32_t *)(dst + COMM_SLOT_OFF_READY) =
                *(const int32_t *)(src + COMM_SLOT_OFF_READY);
            strcpy((char *)(dst + COMM_SLOT_OFF_NAME),
                   (const char *)(src + COMM_SLOT_OFF_NAME));
            *(int32_t *)(dst + COMM_SLOT_OFF_TEAM) =
                *(const int32_t *)(src + COMM_SLOT_OFF_TEAM);
            *(int32_t *)(dst + COMM_SLOT_OFF_TAKEN) =
                *(const int32_t *)(src + COMM_SLOT_OFF_TAKEN);
            *(int32_t *)dst = *(const int32_t *)src;
            *(int32_t *)(dst + COMM_SLOT_OFF_UNACKED) =
                *(const int32_t *)(src + COMM_SLOT_OFF_UNACKED);
            *(int32_t *)(dst + COMM_SLOT_OFF_FIELD_5C) =
                *(const int32_t *)(src + COMM_SLOT_OFF_FIELD_5C);
            *(int32_t *)(dst + COMM_SLOT_OFF_HEARD) =
                *(const int32_t *)(src + COMM_SLOT_OFF_HEARD);
        }
    }

    comm_u32(me, COMM_OFF_FIELD_364) = 0;
    comm_u32(me, COMM_OFF_FIELD_35C) = 0;
    *(uint8_t *)(me + COMM_OFF_FIELD_368) = 0;
    comm_u32(me, COMM_OFF_FIELD_360) = (uint32_t)index;
    comm_u32(me, COMM_OFF_FIELD_3A8) = 0;
    comm_u32(me, COMM_OFF_FIELD_3AC) = 0;

    if ((int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER > slot) {
        *(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER -= 1;
        *(uint32_t *)(uintptr_t)ADDR_OUR_SLOT      -= 1;
    }

    return 1;
}

void *__attribute__((thiscall)) CommConstruct(void *comm)
{
    uint8_t *self = (uint8_t *)comm;
    uint32_t now;
    int32_t  i;

    StartPacketThread();
    CommInitDefaults();
    now = GetTickCount();

    comm_u32(self, 0x3EC) = 0;
    comm_u32(self, 0x3F4) = 0;

    for (i = 0; i < 4; i++) {
        uint8_t *slot = self + COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE;

        comm_u32(slot, 0x00) = 0;
        comm_u32(slot, 0x64) = 0;
        comm_u32(slot, 0x68) = 0;
        comm_u32(slot, 0x04) = (uint32_t)i;
        comm_u32(slot, 0x54) = 0;
        comm_u32(slot, 0x08) = 0;
        comm_u32(slot, 0x50) = 0;
        comm_u32(slot, 0x4C) = 0;
        comm_u32(slot, 0x58) = 0;
        comm_u32(slot, 0x5C) = 0;
        comm_u32(slot, 0x60) = now;
        memset(slot + 0x0C, 0, 0x40);
    }

    comm_u32(self, 0x008) = 0;
    comm_u32(self, 0x408) = now;
    comm_u32(self, 0x004) = 0;
    comm_u32(self, 0x410) = 0x400;
    CommResetStats(comm);

    /* The one key the game ever touches. Created, never read. */
    RegCreateKeyExA(HKEY_LOCAL_MACHINE, (const char *)(uintptr_t)ADDR_REGISTRY_KEY,
                    0, NULL, 0, KEY_ALL_ACCESS, NULL,
                    (PHKEY)(self + 0x204), (LPDWORD)(self + 0x208));

    comm_u32(self, 0x3D4) = ADDR_APP_GUID;   /* the DirectPlay application id */
    comm_u32(self, 0x3D0) = 1;
    comm_u32(self, 0x3E4) = 0;
    comm_u32(self, 0x3D8) = 0;
    comm_u32(self, 0x3DC) = 0;
    comm_u32(self, 0x404) = 0;
    comm_u32(self, 0x414) = 0;
    comm_u32(self, 0x3FC) = 0;
    comm_u32(self, 0x3F8) = 0;
    comm_u32(self, 0x418) = 0;
    comm_u32(self, 0x41C) = 0;
    comm_u32(self, 0x454) = 1;
    comm_u32(self, 0x420) = 100;
    comm_u32(self, 0x424) = 1000;
    comm_u32(self, 0x428) = 996;
    orig_log_noargs();
    comm_u32(self, 0x478) = 1;
    comm_u32(self, 0x47C) = 2;

    /* A constructor returns its object. */
    return comm;
}

void __attribute__((thiscall)) CommDestruct(void *comm)
{
    uint8_t *self = (uint8_t *)comm;

    CommDropDirectPlay(comm);
    CommShutdown();
    comm_u32(self, 0x3DC) = 0;
    comm_u32(self, 0x404) = 0;
    RegCloseKey((HKEY)(uintptr_t)comm_u32(self, 0x204));
}

/* Ask DirectPlay which sessions are out there.
 *
 * 0x0040E3B0, thiscall, `ret 4`. The multiplayer HOST button calls it with the
 * object that collects the results; the callback at 0x0040E280 fills that
 * object and stays original.
 *
 * The identification is the descriptor and the slot together. It zeroes 0x14
 * dwords -- 0x50 bytes, sizeof(DPSESSIONDESC2) -- sets dwSize to 0x50 and
 * copies four dwords from the comm object's GUID pointer to offset 0x18, which
 * is guidApplication and nothing else. Slot 13 on IDirectPlay4 is EnumSessions.
 * The descriptor alone would equally fit Open, which is what it was first
 * written down as; counting the slot is what settled it.
 *
 * Note the filter is by application GUID only. guidInstance is left zeroed, so
 * this asks for every Army Men II session on the transport rather than a
 * particular one. */
static_assert(sizeof(DPSESSIONDESC2) == 0x50, "DPSESSIONDESC2");
static_assert(DPENUMSESSIONS_AVAILABLE == 1, "DPENUMSESSIONS_AVAILABLE");
static_assert(DPENUMSESSIONS_ASYNC == 0x10, "DPENUMSESSIONS_ASYNC");

#define g_sessionList  (*(void **)(uintptr_t)ADDR_SESSION_LIST)
/* lpContext for both enumerations is the game window; see orig.h. */
#define g_enumContext  (*(void **)(uintptr_t)ADDR_HWND)

int32_t __attribute__((thiscall)) CommEnumSessions(void *comm, void *list)
{
    DPSESSIONDESC2  desc;
    LPDIRECTPLAY4A  dp;
    const GUID     *app;
    HRESULT         hr;

    /* Nowhere to put the answers, or nothing to ask. */
    if (!list)
        return 0;
    dp = *DirectPlaySlot(comm);
    if (!dp)
        return 0;

    RecordReset(list);
    /* The callback is a plain function and gets the object through this global
     * rather than through lpContext, which carries something else entirely. */
    g_sessionList = list;

    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;

    app = *(const GUID **)((uint8_t *)comm + COMM_OFF_APP_GUID);
    if (app)
        desc.guidApplication = *app;

    hr = IDirectPlayX_EnumSessions(dp, &desc, 0,
                                   (LPDPENUMSESSIONSCALLBACK2)(uintptr_t)ADDR_ENUM_SESSIONS_CB,
                                   g_enumContext,
                                   DPENUMSESSIONS_AVAILABLE | DPENUMSESSIONS_ASYNC);
    return hr == DP_OK;
}

/* Ask DirectPlay which transports are available, and offer them as menu rows.
 *
 * 0x0040E530, thiscall, `ret 4`, 97 bytes. Slot 35 is EnumConnections. The
 * callback at 0x0040E460 stays original and does two things worth knowing
 * about: it compares each provider's name against "Play on HEAT" and "Play on
 * Mplayer" and drops those two, and it copies the names it keeps onto the
 * game's heap before adding them.
 *
 * Whatever the providers come to, "Play Against Computer Only" is appended
 * afterwards, so the offline choice is always the last row and is there even
 * when DirectPlay offers nothing at all.
 *
 * lpguidApplication is the same GUID pointer CommConstruct installs, so the
 * enumeration is filtered to providers that can carry this game. */
typedef void (__attribute__((thiscall)) *am2_list_add_fn)(void *list,
                                                          const char *name,
                                                          void *data);
#define g_connectionList  (*(void **)(uintptr_t)ADDR_CONNECTION_LIST)

int32_t __attribute__((thiscall)) CommEnumConnections(void *comm, void *list)
{
    LPDIRECTPLAY4A dp = *DirectPlaySlot(comm);
    const GUID    *app;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* As with the session browser, the callback finds the list here rather
     * than through lpContext, which carries something else. */
    g_connectionList = list;

    app = *(const GUID **)((uint8_t *)comm + COMM_OFF_APP_GUID);
    hr = IDirectPlayX_EnumConnections(dp, app,
                                      (LPDPENUMCONNECTIONSCALLBACK)(uintptr_t)ADDR_ENUM_CONNECTIONS_CB,
                                      g_enumContext, 0);
    if (hr != DP_OK)
        return 0;

    /* Read back through the global rather than the argument, as the original
     * does -- they are the same object, but only because nothing reassigned it. */
    ListAdd(g_connectionList,
                  (const char *)(uintptr_t)ADDR_STR_COMPUTER_ONLY, NULL);
    return 1;
}

/* The packet transmit -- 0x0040EB70, thiscall, `ret 0x10`.
 *
 * The DirectPlay call in the middle is three lines; the rest is a statistics
 * ring in front of it and a per-player watchdog behind it. Both are the comm
 * object's own bookkeeping and neither is optional, because the counters they
 * keep are read elsewhere.
 *
 * The watchdog is the interesting half. Every successful send bumps an
 * unacknowledged counter on each *other* live player, and when one passes 30 a
 * bit -- 0x800 shifted by the slot index -- is raised in the global event flags,
 * once. That is how a player who has stopped answering is noticed: not by a
 * timeout, but by getting too far ahead of them.
 *
 * On failure it posts 0x046C to the game window, which is one of the six comm
 * messages WndProc forwards to the original.
 *
 * Two faithfulness details. The maximum-size comparison is UNSIGNED in the
 * original (`jbe`), so a length with the top bit set would be recorded as the
 * new maximum and nothing else would ever beat it -- kept. And the watchdog
 * reads the slot id twice, once through `this` and once through the comm
 * global; those are the same object in every call that exists, but the original
 * does both and so does this. */
static_assert((uint32_t)DPERR_INVALIDPLAYER == 0x88770096u, "DPERR_INVALIDPLAYER");
static_assert((uint32_t)E_INVALIDARG == 0x80070057u, "E_INVALIDARG");

typedef void *(__cdecl *am2_find_player_fn)(uint32_t id);
typedef void (__cdecl *am2_set_flags_fn)(uint32_t bits);

#define g_hwnd           (*(HWND *)(uintptr_t)ADDR_HWND)

/* 0x046C -- WndProc forwards this one to the original. */
#define AM2_WM_COMM_SEND_FAILED 0x046Cu
int32_t __attribute__((thiscall)) CommSend(void *comm, uint32_t idTo,
                                           uint32_t flags, void *data,
                                           uint32_t size)
{
    uint8_t       *self = (uint8_t *)comm;
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(comm);
    uint32_t       at, i, n;
    uint8_t       *other;
    HRESULT        hr;

    if (!dp)
        return 0;

    /* Statistics first, whether or not the send goes through. */
    at = comm_u32(self, COMM_OFF_STAT_INDEX);
    comm_u32(self, COMM_OFF_STAT_TIMES + at * 4) = GetTickCount();
    comm_u32(self, COMM_OFF_STAT_SIZES + at * 4) = size;
    comm_u32(self, COMM_OFF_STAT_BYTES)   += size;
    comm_u32(self, COMM_OFF_STAT_PACKETS) += 1;
    if (size > comm_u32(self, COMM_OFF_STAT_MAX))
        comm_u32(self, COMM_OFF_STAT_MAX) = size;
    if (++at >= COMM_STAT_RING)
        at = 0;
    comm_u32(self, COMM_OFF_STAT_INDEX) = at;

    hr = IDirectPlayX_Send(dp, comm_u32(self, COMM_OFF_OUR_PLAYER_ID), idTo,
                           flags, data, size);

    if (hr != DP_OK) {
        uint8_t *cm = g_commObject;
        int32_t  found = 0;

        /* Only these two are reported; anything else fails silently. */
        if (hr == E_INVALIDARG)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_BADPARAM, idTo);
        else if (hr == (HRESULT)DPERR_INVALIDPLAYER)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_BADPLAYER, idTo);
        else
            return 0;

        n = comm_u32(cm, COMM_OFF_PLAYER_COUNT);
        for (i = 0; i < n; i++) {
            if (comm_u32(cm, COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE
                             + COMM_SLOT_OFF_ID) == idTo) {
                found = 1;
                break;
            }
        }
        if (!found)
            orig_log((const char *)(uintptr_t)ADDR_STR_SEND_NOENTRY, idTo);

        PostMessageA(g_hwnd, AM2_WM_COMM_SEND_FAILED, (WPARAM)idTo, 0);
        return 0;
    }

    /* Sent. Everyone else who is live and reachable is now one packet further
     * behind, and past 30 that is worth raising once. */
    other = g_commObject;
    n = comm_u32(other, COMM_OFF_PLAYER_COUNT);
    for (i = 0; i < n; i++) {
        uint8_t *slot = self  + COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE;
        uint32_t id;
        uint32_t bit;

        if (comm_u32(slot, COMM_SLOT_OFF_ID) == 0xFFFFFFFFu)
            continue;
        id = comm_u32(other, COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE
                             + COMM_SLOT_OFF_ID);
        if (id == comm_u32(self, COMM_OFF_OUR_PLAYER_ID))
            continue;
        if (!FindPlayerById(id))
            continue;

        bit = 0x800u << i;
        if (++comm_u32(slot, COMM_SLOT_OFF_UNACKED) <= COMM_STAT_RING)
            continue;
        if (GetPauseFlags() & bit)
            continue;
        PauseGame(bit);
    }
    return 1;
}

/* Create the session -- 0x0040DFC0, thiscall, `ret 4`, 226 bytes.
 *
 * START A WAR ends up here. Slot 24 is Open and the flag is DPOPEN_CREATE, so
 * unlike CommEnumSessions this makes a session rather than looking for one.
 *
 * The descriptor is worth reading as a statement of what a game of Army Men II
 * is: DPSESSION_MIGRATEHOST, four players maximum -- the same four the comm
 * object keeps slots for -- the battle name the player typed as the session
 * name, and the application GUID so only this game finds it.
 *
 * A local game short-circuits the whole thing. StartSelectedGame sets +0x400
 * when the chosen row was "Play Against Computer Only", and that is checked
 * first: there is nothing to open, and the function reports success without
 * touching DirectPlay at all. */
static_assert(DPOPEN_CREATE == 2, "DPOPEN_CREATE");
static_assert(DPSESSION_MIGRATEHOST == 4, "DPSESSION_MIGRATEHOST");

#define g_ourSlot     (*(int32_t *)(uintptr_t)ADDR_OUR_SLOT)

typedef int32_t (__attribute__((thiscall)) *am2_slot_of_id_fn)(void *, DPID);
typedef int32_t (__cdecl *am2_msg_free_fn)(void *list);
#define orig_msg_list_free  (*(am2_msg_free_fn)ADDR_MSG_LIST_POOL)

#define AM2_MAX_PLAYERS 4

int32_t __attribute__((thiscall)) CommOpenSession(void *self, const char *name)
{
    uint8_t        *comm = g_commObject;
    DPSESSIONDESC2  desc;
    LPDIRECTPLAY4A  dp;
    const GUID     *app;

    /* Only a networked game needs a session. */
    if (!comm_u32(comm, COMM_OFF_LOCAL)) {
        dp = *DirectPlaySlot(comm);
        if (!dp)
            return 0;

        memset(&desc, 0, sizeof desc);
        desc.dwSize        = sizeof desc;
        desc.dwFlags       = DPSESSION_MIGRATEHOST;
        desc.dwMaxPlayers  = AM2_MAX_PLAYERS;
        desc.lpszSessionNameA = (LPSTR)name;
        /* Our own slot's index field, carried across as user data. Only the
         * host reaches this branch, so "ours" and "the host's" are the same
         * slot here -- which is how the global came to be misnamed. */
        desc.dwUser1 = comm_u32((uint8_t *)self,
                                COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE + 4);

        app = *(const GUID **)(comm + COMM_OFF_APP_GUID);
        if (app)
            desc.guidApplication = *app;

        if (IDirectPlayX_Open(dp, &desc, DPOPEN_CREATE) != DP_OK)
            return 0;
    }

    g_commEnumCount = 0;
    g_defaultOwner = 0;
    g_ourSlot     = 0;
    return 1;
}

/* Settle the transport's limits once a connection exists -- 0x0040E660.
 *
 * CommCreateDirectPlay calls this immediately after InitializeConnection
 * succeeds. It asks DirectPlay what the provider can do and then trims the comm
 * object's buffer sizes to fit, because a modem and a LAN do not carry the same
 * packet and the defaults were chosen without knowing which was in use.
 *
 * The defaults it trims are the ones CommConstruct writes -- 0x400 for the
 * maximum and 0x3E4 for the working size -- and the two agree without being
 * made to. Neither is raised, only lowered: a provider that can carry more than
 * 1024 bytes does not get to.
 *
 * The 0x14 subtracted from the working size is DirectPlay's own per-message
 * overhead, left in the number rather than accounted for anywhere else.
 *
 * UNREACHABLE IN THIS BUILD, and that is structural rather than a gap in
 * testing. The only reference to it is the call at 0x0040DD72, inside
 * CommCreateDirectPlay's `if (connection)` branch -- and CommCreateDirectPlay
 * has exactly one caller, at 0x0042EE78, which passes a literal 0. So the
 * branch is never taken and neither is this. The transport is initialised
 * through CommInitializeConnection instead, from StartSelectedGame, which
 * matches what driving the multiplayer path shows: the connection comes up,
 * START A WAR works, and this counter stays at zero.
 *
 * It is reconstructed anyway because it is DirectPlay boundary code and the
 * cost is a fingerprint, but do not go looking for a way to exercise it.
 *
 * The whole capabilities dump is behind `-debugComm`, which is the flag at
 * +0x418. It is also the confirmation that this is a DPCAPS: each of the four
 * values it prints sits exactly where that structure puts the field the message
 * names, and the bit it tests for guaranteed messaging is 0x40, which is
 * DPCAPS_GUARANTEEDSUPPORTED. */
static_assert(sizeof(DPCAPS) == 0x28, "DPCAPS");
static_assert(DPCAPS_GUARANTEEDSUPPORTED == 0x40, "DPCAPS_GUARANTEEDSUPPORTED");

/* DirectPlay's per-message overhead, subtracted from the usable payload. */
#define DPLAY_MESSAGE_OVERHEAD 0x14

#define g_commDebug(self) comm_u32((uint8_t *)(self), COMM_OFF_VERBOSE)

int32_t __attribute__((thiscall)) CommOnConnected(void *self)
{
    LPDIRECTPLAY4A dp   = *DirectPlaySlot(self);
    uint8_t       *comm = g_commObject;
    LPDPCAPS       caps = (LPDPCAPS)(comm + COMM_OFF_CAPS);
    uint32_t       usable;

    if (!dp)
        return 0;

    caps->dwSize = sizeof *caps;
    if (IDirectPlayX_GetCaps(dp, caps, 0) != DP_OK)
        return 0;

    if (g_commDebug(self)) {
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_HEAD);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_PACKET, caps->dwMaxBufferSize);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_HEADER, caps->dwHeaderLength);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_LATENCY, caps->dwLatency);
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_TIMEOUT, caps->dwTimeout);
        orig_log((const char *)(uintptr_t)
                 ((caps->dwFlags & DPCAPS_GUARANTEEDSUPPORTED)
                  ? ADDR_STR_CAPS_GUAR_YES : ADDR_STR_CAPS_GUAR_NO));
    }

    /* Lowered to fit the provider, never raised past what was asked for. */
    if (caps->dwMaxBufferSize < comm_u32(comm, COMM_OFF_BUFFER_MAX))
        comm_u32(comm, COMM_OFF_BUFFER_MAX) = caps->dwMaxBufferSize;

    usable = caps->dwMaxBufferSize - DPLAY_MESSAGE_OVERHEAD;
    if (usable < comm_u32(comm, COMM_OFF_BUFFER_DEFAULT))
        comm_u32(comm, COMM_OFF_BUFFER_DEFAULT) = usable;

    if (g_commDebug(self))
        orig_log((const char *)(uintptr_t)ADDR_STR_CAPS_BUFFERS,
                 comm_u32(comm, COMM_OFF_BUFFER_MAX),
                 comm_u32(comm, COMM_OFF_BUFFER_DEFAULT));
    return 1;
}

/* Give the connection back -- 0x0040EA40, thiscall, and the last of the
 * DirectPlay surface.
 *
 * Everything the connection owns goes: two heap buffers, the IDirectPlay4A
 * itself, every remote player, and finally the lobby interface at +0x3F4 --
 * which is named by the store at 0x0040ED3C, where CreateDirectPlayLobby is
 * handed that exact address to fill.
 *
 * The four player slots are not merely cleared, they are put back to what
 * CommConstruct built: zeroed apart from the slot index and a fresh
 * GetTickCount, with the 0x40-byte name buffer blanked. That is the third
 * function to write this layout -- the constructor, StartSelectedGame's
 * computer-player fill, and now this -- and all three agree.
 *
 * Only remote players are destroyed. Ours, at +0x3CC, is skipped inside the
 * loop and removed afterwards, so it outlives the slots it appears in.
 *
 * Returns 0 always, which every caller ignores. */
#define g_noBuffersLatch       (*(int32_t *)(uintptr_t)ADDR_COMM_NO_BUFFERS_LATCH)

int32_t __attribute__((thiscall)) CommDropDirectPlay(void *comm)
{
    uint8_t             *self = (uint8_t *)comm;
    LPDIRECTPLAY4A      *dp    = DirectPlaySlot(comm);
    LPDIRECTPLAYLOBBY3A *lobby =
        (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    uint32_t             ours;
    int32_t              i;

    if (comm_u32(self, COMM_OFF_SEND_BUF)) {
        orig_free(*(void **)(self + COMM_OFF_SEND_BUF));
        comm_u32(self, COMM_OFF_SEND_BUF) = 0;
    }
    if (comm_u32(self, COMM_OFF_RECV_BUF)) {
        orig_free(*(void **)(self + COMM_OFF_RECV_BUF));
        comm_u32(self, COMM_OFF_RECV_BUF) = 0;
    }
    if (*dp) {
        IDirectPlayX_Release(*dp);
        *dp = NULL;
    }
    comm_u32(self, 0x3DC) = 0;

    orig_log((const char *)(uintptr_t)ADDR_STR_RELEASING_COMM);

    comm_u32(self, COMM_OFF_PLAYER_COUNT) = 1;   /* just us again */
    comm_u32(self, 0x3E4) = 0;

    ours = comm_u32(self, COMM_OFF_OUR_PLAYER_ID);

    for (i = 0; i < 4; i++) {
        uint8_t *slot = self + COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE;
        uint32_t id;

        comm_u32(slot, 0x00) = 0;
        comm_u32(slot, 0x64) = 0;
        comm_u32(slot, 0x68) = 0;
        comm_u32(slot, 0x50) = 0;
        comm_u32(slot, 0x4C) = 0;
        comm_u32(slot, 0x04) = (uint32_t)i;
        comm_u32(slot, 0x54) = 0;

        /* Remote players are destroyed; ours is left for the call below. */
        id = comm_u32(slot, COMM_SLOT_OFF_ID);
        if (id && id != ours)
            DestroyFlow(id);
        comm_u32(slot, COMM_SLOT_OFF_ID) = 0;

        comm_u32(slot, 0x58) = 0;
        comm_u32(slot, 0x5C) = 0;
        comm_u32(slot, 0x60) = GetTickCount();
        memset(slot + 0x0C, 0, 0x40);
    }

    DestroyFlow(ours);
    comm_u32(self, COMM_OFF_OUR_PLAYER_ID) = 0;

    if (*lobby) {
        IDirectPlayLobby_Release(*lobby);
        *lobby = NULL;
    }

    UnPauseGame(COMM_DROP_EVENT_MASK);
    g_defaultOwner      = 0;
    g_noBuffersLatch = 0;
    return 0;
}

/* Start the game from a DirectPlay lobby -- 0x0040ED10, thiscall.
 *
 * The last DirectPlay function, and the one path into the game that no menu
 * reaches: another application launches it through DirectPlay, hands over the
 * connection settings it has already chosen, and the game joins whatever
 * session was arranged for it.
 *
 * The shape is ask, adjust, connect. GetConnectionSettings fetches the
 * DPLCONNECTION the launcher prepared; the session description inside it is
 * amended with this game's own requirements -- host migration, four players,
 * and dwUser1..4 set to 1, 2, 3, 4 -- and SetConnectionSettings hands it back
 * before Connect makes it real. What Connect returns is an IDirectPlay2, so
 * the very next thing is a QueryInterface for IID_IDirectPlay4A, which is the
 * interface everything else in this file uses; the intermediate is released
 * immediately.
 *
 * DPERR_NOTLOBBIED is the ordinary answer, not a failure. It means nobody
 * launched us through a lobby, and it exits quietly -- releasing the lobby
 * interface and the buffer -- where every other error is named in the log
 * first.
 *
 * Host or slave is decided by DirectPlay rather than by the launcher: GetCaps
 * is called and DPCAPS_ISHOST picked out of the flags, which is the `>> 1 & 1`
 * in the original. A slave then fetches the session description to learn the
 * player count; a host already knows it.
 *
 * The CD check on the way through is one of the five patched to unconditional,
 * so the "insert the CD" dialog after it is unreachable. Reproduced through
 * RequireGameCD like the others -- see cdcheck.h.
 *
 * NOT EXERCISABLE HERE, and not for want of trying: it needs a DirectPlay
 * lobby application to launch the game, which is a thing that no longer
 * exists. Verified by reading. */
static_assert((uint32_t)DPERR_NOTLOBBIED == 0x8877042Eu, "DPERR_NOTLOBBIED");
static_assert((uint32_t)DPERR_INVALIDINTERFACE == 0x88770406u, "DPERR_INVALIDINTERFACE");
static_assert((uint32_t)DPERR_INVALIDOBJECT == 0x88770082u, "DPERR_INVALIDOBJECT");
static_assert(DPCAPS_ISHOST == 2, "DPCAPS_ISHOST");
static_assert(sizeof(DPLCONNECTION) <= LOBBY_CONN_BUF_SIZE, "the 0x800 buffer");

typedef void (__cdecl *am2_lobby_void_fn)(void);
/* CommCreatePlayer is reconstructed; called directly below. */
#define orig_apply_settings  (*(am2_lobby_void_fn)ADDR_APPLY_GAME_SETTINGS)

/* Release the lobby interface and the connection buffer, in that order. Used
 * by the quiet DPERR_NOTLOBBIED exit and by the dead copy-protection path. */
static void LobbyGiveUp(uint8_t *self)
{
    LPDIRECTPLAYLOBBY3A *lobby = (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);

    if (*lobby) {
        IDirectPlayLobby_Release(*lobby);
        *lobby = NULL;
        if (comm_u32(self, COMM_OFF_LOBBY_BUF)) {
            orig_free(*(void **)(self + COMM_OFF_LOBBY_BUF));
            comm_u32(self, COMM_OFF_LOBBY_BUF) = 0;
        }
    }
}

int32_t __attribute__((thiscall)) CommLobbyStart(void *comm)
{
    uint8_t             *self  = (uint8_t *)comm;
    LPDIRECTPLAYLOBBY3A *lobby = (LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    uint8_t             *shared;
    LPDPLCONNECTION      conn;
    LPDPSESSIONDESC2     sd;
    LPDIRECTPLAY2A       dp2 = NULL;
    DPCAPS               caps;
    DWORD                size = LOBBY_CONN_BUF_SIZE;
    HRESULT              hr;
    const char          *playerName;

    comm_u32(self, COMM_OFF_LOBBY_STARTING) = 1;
    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_START);
    ReadMpMapList();

    /* Answers 0 or 1, never negative, so this test can only ever pass -- the
     * original's, kept. */
    if (CreateDirectPlayLobby(lobby) < 0) {
        *lobby = NULL;
        return 0;
    }

    conn = (LPDPLCONNECTION)orig_malloc(size);
    *(void **)(self + COMM_OFF_LOBBY_BUF) = conn;
    if (!conn) {
        orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_NOMEM);
        return 0;
    }

    shared = g_commObject;
    hr = IDirectPlayLobby_GetConnectionSettings(
             *(LPDIRECTPLAYLOBBY3A *)(shared + COMM_OFF_LOBBY), 0, conn, &size);
    if (hr < 0) {
        /* Nobody launched us. Ordinary, and silent. */
        if (hr == (HRESULT)DPERR_NOTLOBBIED && *lobby) {
            LobbyGiveUp(self);
            return 0;
        }
        orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_GCS_FAIL, hr);
        if (hr == (HRESULT)DPERR_BUFFERTOOSMALL)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_SMALL);
        else if (hr == (HRESULT)DPERR_INVALIDINTERFACE)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_IFACE);
        else if (hr == (HRESULT)DPERR_INVALIDOBJECT)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_OBJECT);
        else if (hr == E_INVALIDARG)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_PARAMS);
        else if (hr == E_OUTOFMEMORY)
            orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_E_MEMORY);
        return 0;
    }

    /* Disabled in this build; the refusal below cannot run. */
    if (!RequireGameCD()) {
        LobbyGiveUp(self);
        return 0;
    }

    /* The launcher chose the transport; the game chooses the rest. */
    conn = *(LPDPLCONNECTION *)(self + COMM_OFF_LOBBY_BUF);
    sd = conn->lpSessionDesc;
    sd->dwUser1      = 1;
    sd->dwUser2      = 2;
    sd->dwUser3      = 3;
    sd->dwUser4      = 4;
    sd->dwFlags      = DPSESSION_MIGRATEHOST;
    sd->dwMaxPlayers = AM2_MAX_PLAYERS;

    if (IDirectPlayLobby_SetConnectionSettings(
            *(LPDIRECTPLAYLOBBY3A *)(g_commObject + COMM_OFF_LOBBY),
            0, 0, conn) < 0)
        return 0;

    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_CONNECT);
    hr = IDirectPlayLobby_Connect(
             *(LPDIRECTPLAYLOBBY3A *)(g_commObject + COMM_OFF_LOBBY),
             0, &dp2, NULL);
    orig_log((const char *)(uintptr_t)ADDR_STR_LOBBY_CONNRET, hr);
    if (hr < 0)
        return 0;

    /* Connect hands back an IDirectPlay2; everything else here wants a 4A. */
    if (dp2) {
        hr = IDirectPlayX_QueryInterface(dp2, kIID_IDirectPlay4A,
                                         (LPVOID *)DirectPlaySlot(self));
        IDirectPlayX_Release(dp2);
        if (hr < 0)
            return 0;
    }

    shared = g_commObject;
    comm_u32(shared, 0x3E0) = 0;
    caps.dwSize = sizeof caps;
    IDirectPlayX_GetCaps(*DirectPlaySlot(shared), &caps, 0);
    comm_u32(shared, COMM_OFF_IS_HOST) = (caps.dwFlags >> 1) & 1;

    conn = *(LPDPLCONNECTION *)(self + COMM_OFF_LOBBY_BUF);
    playerName = conn->lpPlayerName->lpszShortNameA;

    if (!comm_u32(g_commObject, COMM_OFF_IS_HOST)) {
        /* A slave has to be told how many players there are. */
        CommGetSessionDesc(g_commObject);
        comm_u32(g_commObject, COMM_OFF_PLAYER_COUNT) =
            ((LPDPSESSIONDESC2)*(void **)(g_commObject + COMM_OFF_SESSION_DESC))
                ->dwCurrentPlayers;
    }

    if (CommCreatePlayer(g_commObject, playerName, NULL, NULL, 0) < 0)
        return 0;

    orig_log((const char *)(uintptr_t)
             (comm_u32(g_commObject, COMM_OFF_IS_HOST)
              ? ADDR_STR_LOBBY_AS_HOST : ADDR_STR_LOBBY_AS_SLAVE),
             comm_u32(g_commObject, COMM_OFF_OUR_PLAYER_ID));

    comm_u32(g_commObject, COMM_OFF_LOBBIED) = 1;
    CommMarkLobbied();

    if (!comm_u32(g_commObject, COMM_OFF_IS_HOST)) {
        OnLobbySlave();
    } else {
        /* The host records its own name in the slot it occupies. */
        char *slotName = (char *)(g_commObject + COMM_OFF_PLAYERS
                                  + g_ourSlot * COMM_PLAYER_STRIDE
                                  + COMM_SLOT_OFF_NAME);
        strcpy(slotName, playerName);
    }
    orig_apply_settings();
    return 1;
}

/* Make our player -- 0x0040DE10, thiscall, `ret 0x10`.
 *
 * A local game short-circuits it: there is no DirectPlay to tell, so the
 * hosting slot is simply marked taken with a player id of 1 and that is that.
 * Everything below is the networked case.
 *
 * The DPNAME is built on the stack and only its short name is set, which is
 * the name the player typed. hEvent defaults to the comm event when the caller
 * passes none, so a player made without one still wakes the packet thread.
 *
 * Afterwards, host and joiner diverge. A host writes its own id into its slot
 * and marks it taken; a joiner instead bumps the shared player count and says
 * so in the log. Both then mark the comm object as joined and register the id.
 *
 * Not exercised -- it needs a session, and CommLobbyStart is one of the two
 * callers, which needs a lobby. Read, not run. */
static_assert(sizeof(DPNAME) == 0x10, "DPNAME");

typedef void (__cdecl *am2_register_self_fn)(DPID id);
#define g_defaultPlayerEvent (*(HANDLE *)(uintptr_t)ADDR_DEFAULT_PLAYER_EVT)

int32_t __attribute__((thiscall)) CommCreatePlayer(void *comm, const char *name,
                                                   HANDLE event, void *data,
                                                   DWORD length)
{
    uint8_t        *self   = (uint8_t *)comm;
    uint8_t        *shared = g_commObject;
    LPDIRECTPLAY4A  dp;
    DPNAME          who;
    HRESULT         hr;

    memset(&who, 0, sizeof who);
    who.dwSize          = sizeof who;
    who.lpszShortNameA  = (LPSTR)name;

    /* Offline: nobody to tell, so just claim the slot. */
    if (comm_u32(shared, COMM_OFF_LOCAL)) {
        comm_u32(shared, COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE
                         + COMM_SLOT_OFF_TAKEN) = 1;
        comm_u32(shared, COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE
                         + COMM_SLOT_OFF_ID) = 1;
        comm_u32(self, COMM_OFF_PLAYER_MADE) = 1;
        return 1;
    }

    dp = *DirectPlaySlot(shared);
    if (!dp)
        return 0;

    if (!event)
        event = g_defaultPlayerEvent;

    hr = IDirectPlayX_CreatePlayer(dp, (LPDPID)(self + COMM_OFF_OUR_PLAYER_ID),
                                   &who, event, data, length, 0);
    if (hr != DP_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_CREATE_PLAYER_FAIL, hr);
        return 0;
    }

    if (comm_u32(self, COMM_OFF_IS_HOST)) {
        comm_u32(self, COMM_OFF_PLAYER_MADE) = 1;
    } else {
        /* One more of us. */
        comm_u32(shared, COMM_OFF_PLAYER_COUNT) += 1;
        orig_log((const char *)(uintptr_t)ADDR_STR_NUM_PLAYERS,
                 comm_u32(shared, COMM_OFF_PLAYER_COUNT));
    }

    if (comm_u32(self, COMM_OFF_IS_HOST)) {
        comm_u32(shared, COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE
                         + COMM_SLOT_OFF_ID) =
            comm_u32(self, COMM_OFF_OUR_PLAYER_ID);
        comm_u32(shared, COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE
                         + COMM_SLOT_OFF_TAKEN) = 1;
    }

    comm_u32(shared, COMM_OFF_IS_HOST)  = 1;
    comm_u32(shared, COMM_OFF_JOINED) = 1;
    CommRegisterSelf((DPID)comm_u32(shared, COMM_OFF_OUR_PLAYER_ID));
    return 1;
}

/* Take a packet off the wire -- 0x0040E8A0, thiscall, `ret 0x14`.
 *
 * The other half of CommSend, and it keeps the same books: a thirty-entry ring
 * of arrival times and lengths, running totals, and a maximum. The two sets of
 * fields sit in separate ranges -- receive at +0x08/+0xFC/+0x174, send at
 * +0x04/+0x0C/+0x84 -- so a busy session can be read from either direction.
 *
 * It also undoes what CommSend does. Send raises a bit in the global event
 * flags when a player falls more than thirty packets behind; this clears it
 * once they are back under fifteen. That gap is deliberate hysteresis, not an
 * inconsistency: an alarm that cleared at the same count it raised at would
 * chatter.
 *
 * A message of type 0x0B resets the sender's outstanding count outright, which
 * is what makes it an acknowledgement in all but name.
 *
 * Finally, flow control. If the message list has more than 300 entries free and
 * the paused bit is set, it is cleared and the fact logged -- the counterpart
 * of whatever set it when the list filled up. */
int32_t __attribute__((thiscall)) CommReceive(void *comm, DPID *from, DPID *to,
                                              DWORD flags, void *data,
                                              DWORD *size)
{
    uint8_t        *self = (uint8_t *)comm;
    LPDIRECTPLAY4A  dp   = *DirectPlaySlot(comm);
    uint8_t        *shared;
    uint32_t        at, now;
    int32_t         slot, i, n;

    if (!dp)
        return 0;

    if (IDirectPlayX_Receive(dp, from, to, flags, data, size) != DP_OK)
        return 0;

    at = comm_u32(self, COMM_OFF_RX_INDEX);
    comm_u32(self, COMM_OFF_RX_SIZES + at * 4) = *size;
    comm_u32(self, COMM_OFF_RX_BYTES)   += *size;
    comm_u32(self, COMM_OFF_RX_PACKETS) += 1;
    if (*size > comm_u32(self, COMM_OFF_RX_MAX))
        comm_u32(self, COMM_OFF_RX_MAX) = *size;

    now = GetTickCount();
    comm_u32(self, COMM_OFF_RX_TIMES + at * 4) = now;
    if (++at >= COMM_STAT_RING)
        at = 0;
    comm_u32(self, COMM_OFF_RX_INDEX) = at;

    slot = CommSlotOfId(comm, *from);

    /* Type 0x0B is the acknowledgement: it wipes the outstanding count. */
    if (*(uint32_t *)data == COMM_MSG_TYPE_ACK)
        comm_u32(self, COMM_OFF_PLAYERS + slot * COMM_PLAYER_STRIDE
                       + COMM_SLOT_OFF_UNACKED) = 0;
    comm_u32(self, COMM_OFF_PLAYERS + slot * COMM_PLAYER_STRIDE
                   + COMM_SLOT_OFF_HEARD) = now;

    /* Anyone who has caught up gets their alarm cleared. */
    shared = g_commObject;
    n = (int32_t)comm_u32(shared, COMM_OFF_PLAYER_COUNT);
    for (i = 0; i < n; i++) {
        uint32_t id = comm_u32(shared, COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE
                                       + COMM_SLOT_OFF_ID);
        uint32_t bit;

        if (id == 0xFFFFFFFFu)
            continue;
        if (id == comm_u32(self, COMM_OFF_OUR_PLAYER_ID))
            continue;
        if (comm_u32(self, COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE
                           + COMM_SLOT_OFF_UNACKED) >= COMM_UNACKED_CLEAR)
            continue;
        bit = 0x800u << i;
        if (GetPauseFlags() & bit)
            UnPauseGame(bit);
    }

    /* Room again in the message list: let the senders go. */
    {
        int32_t free = orig_msg_list_free((void *)(uintptr_t)ADDR_MSG_LIST_POOL);

        if (free > COMM_FLOW_FREE_OK
                && (GetPauseFlags() & COMM_FLOW_PAUSED_BIT)) {
            orig_log((const char *)(uintptr_t)ADDR_STR_FLOW_UNPAUSE, free);
            UnPauseGame(COMM_FLOW_PAUSED_BIT);
        }
    }
    return 1;
}

/* Join a session someone else is hosting -- 0x0040E7B0, thiscall.
 *
 * The counterpart of CommOpenSession: the same Open, with DPOPEN_JOIN instead
 * of DPOPEN_CREATE, and a descriptor that names which session rather than
 * describing a new one. guidInstance identifies it and guidApplication keeps
 * the answer to this game; nothing else in the descriptor is set, because
 * joining does not get to choose the rules.
 *
 * Afterwards it reads the session back and, when the host left dwUser1 clear,
 * records the current player count in the hosting slot. CommOpenSession puts
 * the host's slot index in dwUser1, so a zero there means the host did not say
 * -- and the player count is what is used instead. */
int32_t __attribute__((thiscall)) CommJoinSession(void *comm, const GUID *instance)
{
    uint8_t         *self = (uint8_t *)comm;
    LPDIRECTPLAY4A   dp   = *DirectPlaySlot(comm);
    DPSESSIONDESC2   desc;
    const GUID      *app;
    HRESULT          hr;

    if (!dp)
        return 0;

    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    if (instance)
        desc.guidInstance = *instance;
    app = *(const GUID **)(self + COMM_OFF_APP_GUID);
    if (app)
        desc.guidApplication = *app;

    hr = IDirectPlayX_Open(dp, &desc, DPOPEN_JOIN);
    if (hr != DP_OK) {
        orig_log((const char *)(uintptr_t)ADDR_STR_OPEN_FAILED, hr);
        return 0;
    }

    if (!CommGetSessionDesc(comm))
        return 0;

    {
        uint8_t          *shared = g_commObject;
        LPDPSESSIONDESC2  sd =
            *(LPDPSESSIONDESC2 *)(shared + COMM_OFF_SESSION_DESC);

        if (sd->dwUser1 == 0)
            comm_u32(shared, COMM_OFF_PLAYERS + g_ourSlot * COMM_PLAYER_STRIDE + 4)
                = sd->dwCurrentPlayers;
    }
    return 1;
}

/* Tell the lobby something -- 0x0040FAA0, thiscall.
 *
 * Only the host of a lobby-launched game says anything, which is what the
 * three guards at the top are: launched from a lobby, hosting it, and holding
 * a lobby interface.
 *
 * The message is a DPLMSG_SETPROPERTY, and it is identifiable as one without
 * guessing -- the 0x30 bytes it builds are exactly that structure's fields in
 * order. dwType is 5, which is DPLSYS_SETPROPERTY; dwRequestID is 0, which is
 * DPL_NOCONFIRMATION, so nothing is expected back; guidPlayer is the null GUID
 * the image carries at 0x0046FD98; and the property tag is the game's own,
 * carrying four bytes of value. It goes as DPLMSG_STANDARD.
 *
 * Answers non-zero when the send was accepted. */
static_assert(DPLSYS_SETPROPERTY == 5, "DPLSYS_SETPROPERTY");
static_assert(DPL_NOCONFIRMATION == 0, "DPL_NOCONFIRMATION");
static_assert(sizeof(DPLMSG_SETPROPERTY) == 0x30, "DPLMSG_SETPROPERTY");

int32_t __attribute__((thiscall)) CommSendLobbyProperty(void *comm, uint32_t value)
{
    uint8_t             *self = (uint8_t *)comm;
    LPDIRECTPLAYLOBBY3A  lobby;
    DPLMSG_SETPROPERTY   msg;

    if (!comm_u32(self, COMM_OFF_LOBBIED))
        return 0;
    if (!comm_u32(self, COMM_OFF_IS_HOST))
        return 0;
    lobby = *(LPDIRECTPLAYLOBBY3A *)(self + COMM_OFF_LOBBY);
    if (!lobby)
        return 0;

    msg.dwType          = DPLSYS_SETPROPERTY;
    msg.dwRequestID     = DPL_NOCONFIRMATION;
    msg.guidPlayer      = *(const GUID *)(uintptr_t)ADDR_GUID_NULL;
    msg.guidPropertyTag = *(const GUID *)(uintptr_t)ADDR_GUID_GAME_PROPERTY;
    msg.dwDataSize      = sizeof value;
    msg.dwPropertyData[0] = value;

    return IDirectPlayLobby_SendLobbyMessage(lobby, DPLMSG_STANDARD, 0,
                                             &msg, sizeof msg) >= 0;
}

/* Ask DirectPlay who is in the session -- 0x0040E200.
 *
 * The last DirectPlay call outside the reconstruction, and the smallest
 * remaining boundary function in the image at 128 bytes for one COM call. It
 * was ranked at 432 bytes and three places lower until tools/merges.py learned
 * to count unaligned `push imm32` operands as references: the function after
 * this one is reached only that way, so its start was invisible and its bytes
 * were charged to this one.
 *
 * Every slot is emptied before the enumeration rather than after it, because
 * the callback fills them by the running count and has no way to know which
 * were stale. A reset slot has index 0x63, no player id and an empty name.
 *
 * The callback stays original. It is the other half of the same enumeration
 * and touches no import; only the EnumPlayers call itself is boundary.
 *
 * Returns 1 when DirectPlay answered DP_OK and 0 otherwise -- the original
 * spells that `neg`/`sbb`/`inc`, which is the usual MSVC idiom for
 * `hr == 0` and not a sign that anything subtle is happening.
 *
 * NOT EXERCISED HERE, and it cannot be without AM2_MULTIPLAYER=1: with the
 * button patched out the whole DirectPlay subsystem is unreachable. Verified
 * by reading, like the rest of dplay.cpp. */
int32_t __cdecl CommEnumPlayers(void)
{
    uint8_t       *comm = g_commObject;
    LPDIRECTPLAY4A dp;
    int32_t        i;

    dp = *(LPDIRECTPLAY4A *)(comm + COMM_OFF_DPLAY);
    if (!dp)
        return 0;

    for (i = 0; i < 4; i++) {
        uint8_t *slot = comm + COMM_OFF_PLAYERS + i * COMM_PLAYER_STRIDE;

        comm_u32(slot, COMM_SLOT_OFF_INDEX) = COMM_SLOT_INDEX_NONE;
        comm_u32(slot, COMM_SLOT_OFF_ID)    = 0;
        slot[COMM_SLOT_OFF_NAME]            = 0;
    }
    g_commEnumCount = 0;

    return IDirectPlayX_EnumPlayers(dp, *(GUID **)comm,
                                    (LPDPENUMPLAYERSCALLBACK2)
                                        (uintptr_t)ADDR_ENUM_PLAYERS_CB,
                                    (LPVOID)g_hwnd, 0) == DP_OK;
}

/* Bring the packet subsystem up -- 0x004021A0.
 *
 * The last boundary work in the image, and it was hidden by a classification
 * rather than by being hard: CreateThread, CreateEventA, CreateMutexA and
 * SetThreadPriority were all on coverage.py's incidental list, so a function
 * that starts a thread counted as game logic. Creating an OS object is the same
 * kind of act as creating a DirectDraw surface.
 *
 * Named from its own error string, "Error launching packet thread". It went
 * into orig.h as ADDR_COMM_INIT_SYNC with the note "mirrors CommShutdown",
 * which is where it is called from and not what it does.
 *
 * Four message lists, then 400 records each owning a kilobyte of buffer, then
 * two events, then the thread. The buffers are filled with rand() after
 * srand(0) -- a fixed seed, so the contents are the same every run, which is
 * the only reason that is not simply strange. Transcribed as written.
 *
 * EVERY FAILURE RETURNS EARLY WITHOUT UNDOING ANYTHING. A list that cannot get
 * its mutex leaves the earlier ones created and returns, and the caller --
 * CommConstruct -- ignores the answer entirely. Kept: it is the original's, and
 * the alternative would be inventing a teardown path that does not exist.
 *
 * The thread procedure at 0x00401F00 stays original. It is the packet pump and
 * it is game logic; what is boundary here is asking the OS for the thread. */
static_assert(sizeof(HANDLE) == 4, "a 32-bit handle");

#define g_packetRecords ((uint8_t *)(uintptr_t)ADDR_PACKET_RECORDS)
#define g_packetThread  (*(HANDLE *)(uintptr_t)ADDR_PACKET_THREAD)
#define g_packetState   (*(int32_t *)(uintptr_t)ADDR_PACKET_STATE)

typedef void (__cdecl *am2_slot_reset_fn)(int32_t slot);
typedef void (__cdecl *am2_srand_fn)(uint32_t seed);
typedef int32_t (__cdecl *am2_rand_fn)(void);

#define orig_srand      (*(am2_srand_fn)ADDR_GAME_SRAND)
#define orig_rand       (*(am2_rand_fn)ADDR_GAME_RAND)

/* 0x00401000. Clear the list and give it a mutex. Answers 0 if the mutex
 * could not be had, and leaves the list zeroed either way. */
int32_t __cdecl MsgListInit(void *list)
{
    uint32_t *l = (uint32_t *)list;

    l[1] = 0;
    l[2] = 0;
    l[3] = 0;
    l[0] = (uint32_t)(uintptr_t)CreateMutexA(NULL, FALSE, NULL);
    return l[0] != 0;
}

/* The neighbour RecvThreadProc hands each received node to; still original. */
typedef int32_t (__cdecl *AM2_RecvMsgFn)(void *node);

/* RecvThreadProc -- original 0x00401F00, 416 bytes, and the thread
 * StartPacketThread below creates. It carried "the thread, stays original" in
 * orig.h with NO reason beside it, which this project elsewhere calls out as
 * meaning "not yet".
 *
 * IT CALLS ITSELF THE *RECEIVE* THREAD, in the one message it logs on the way
 * out. The PACKET in ADDR_PACKET_THREAD_PROC's name came from its creator, not
 * from itself -- naming from a call site, one level out. The macro keeps its
 * name because the creator cluster shares it; this is named after the string.
 *
 * IT IS cdecl, NOT stdcall, and that is checked in the instruction rather than
 * assumed: the epilogue is a plain `ret`, where a __stdcall thread proc with
 * one parameter would be `ret 4`. CreateThread wants LPTHREAD_START_ROUTINE and
 * gets a cdecl function; it survives because a thread's return unwinds nobody.
 * Reproduced, since changing it would change the cast StartPacketThread makes.
 * The parameter is never read.
 *
 * The shape is one wait and then an inner drain: wake on any of
 * ADDR_PACKET_STATE handles, and keep receiving until the transport has nothing
 * left, taking a fresh node from the pool for each packet. Handle 0 means quit.
 *
 * A NODE SURVIVES A FAILED RECEIVE. `msg` is only cleared after a packet is
 * taken, so the node popped for a receive that finds nothing is still held on
 * the next pass round the outer wait. That is what the `if (msg)` at the top of
 * the drain is for, and it is why the pool is not drained by an idle link.
 *
 * THE EMPTY-POOL PATH TRIES TWICE AND LOGS BOTH TIMES. It pops, logs the result
 * and the free count, pops again, logs again -- the second pop is not a retry
 * loop, it is exactly one more attempt, and the message with five question
 * marks in it says what the author thought of the situation. If it still has
 * nothing it RECEIVES THE PACKET ANYWAY into a scratch buffer and throws it
 * away, so the transport does not keep redelivering it, and tells the main
 * thread with AM2_WM_NO_BUFFERS.
 *
 * The discard receive is handed three stack locals for the fields a real node
 * would have supplied, and the size local is the one initialised to
 * PACKET_BUFFER_BYTES at the top of the function -- so it is written once and
 * reused, where the node path rewrites the node's own length each time.
 *
 * ON THE WAY OUT it logs and then waits on the SAME handle that woke it, with
 * INFINITE. The events are auto-reset, so that blocks until whoever asked for
 * the shutdown sets it again -- a handshake, not a hang, and it is why
 * ab.sh quit sees this thread's line at all.
 *
 * MsgField12 is called on a LIST here, not on a node: a list header's +0x0C is
 * MSGLIST_OFF_COUNT, which is what the two log lines print as the free count.
 * The accessor is shared between the two structures. */
int32_t __cdecl RecvThreadProc(void *param)
{
    DPID   from;
    DPID   to;
    DWORD  size = PACKET_BUFFER_BYTES;
    void  *msg  = (void *)0;

    (void)param;

    for (;;) {
        int32_t got = 0;

        if (WaitForMultipleObjects(
                (DWORD)*(const int32_t *)(uintptr_t)ADDR_PACKET_STATE,
                (const HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_A,
                FALSE, INFINITE) == WAIT_OBJECT_0)
            break;

        for (;;) {
            uint8_t *node;

            if (!msg) {
                msg = MsgListRemHead((void *)(uintptr_t)ADDR_MSG_LIST_POOL);
                if (!msg) {
                    orig_log((const char *)AM2_IMAGE(ADDR_FMT_RECV_NO_NODE),
                             msg,
                             MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_POOL));
                    msg = MsgListRemHead(
                        (void *)(uintptr_t)ADDR_MSG_LIST_POOL);
                    orig_log((const char *)AM2_IMAGE(ADDR_FMT_RECV_NOW_NODE),
                             msg,
                             MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_POOL));
                }
            }

            if (!msg) {
                /* No node twice: take the packet and drop it. */
                orig_log((const char *)AM2_IMAGE(ADDR_FMT_RECV_NO_BUFFERS),
                         MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_POOL),
                         MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_B),
                         MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ));
                orig_log((const char *)AM2_IMAGE(ADDR_FMT_RECV_DUMPING));

                CommReceive(*(void **)(uintptr_t)ADDR_COMM_OBJECT, &from, &to,
                            1, (void *)(uintptr_t)ADDR_RECV_SCRATCH, &size);

                PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND,
                             AM2_WM_NO_BUFFERS, 1, 0);
                break;
            }

            node = (uint8_t *)msg;
            *(DWORD *)(node + MSGNODE_OFF_BODY_LEN) = PACKET_BUFFER_BYTES;

            if (!CommReceive(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                             (DPID *)(node + MSGNODE_OFF_FROM),
                             (DPID *)(node + MSGNODE_OFF_TO),
                             1,
                             *(void **)(node + MSGNODE_OFF_BODY),
                             (DWORD *)(node + MSGNODE_OFF_BODY_LEN)))
                break;   /* nothing left -- the node is kept for next time */

            if (!((AM2_RecvMsgFn)(uintptr_t)ADDR_RECV_MSG_4014C0)(msg))
                MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_B, msg);

            got = 1;
            msg = (void *)0;
        }

        if (got)
            PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND,
                         AM2_WM_PACKETS_READY, 0, 0);
    }

    orig_log((const char *)AM2_IMAGE(ADDR_STR_RECV_GOT_EVENT_0));
    WaitForSingleObject(*(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_A, INFINITE);
    return 0;
}

/* 0x00402170. Close a handle and forget it, along with two fields beside it.
 * Safe on a holder that never got one. */
void __cdecl EventClose(void *holder)
{
    uint32_t *h = (uint32_t *)holder;
    HANDLE    handle = (HANDLE)(uintptr_t)h[0];

    h[1] = 0;
    h[2] = 0;
    if (handle)
        CloseHandle(handle);
    h[0] = 0;
}

int32_t __cdecl StartPacketThread(void)
{
    uint8_t *rec  = g_packetRecords;
    uint8_t *data = (uint8_t *)(uintptr_t)ADDR_PACKET_BUFFERS;
    int32_t  i;

    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_POOL)) return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_B))    return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ))    return 0;
    if (!MsgListInit((void *)(uintptr_t)ADDR_MSG_LIST_DELAYED))    return 0;

/* Defined below, beside MsgListInit. */
void __cdecl PacketSlotReset(uint32_t slot);

    /* A fixed seed, so every run fills the buffers identically. */
    orig_srand(0);

    while (data < (uint8_t *)(uintptr_t)ADDR_PACKET_BUFFERS_END) {
        uint32_t *w = (uint32_t *)data;

        *(uint32_t *)(rec + PACKET_REC_OFF_SIZE) = PACKET_BUFFER_BYTES;
        *(uint32_t **)(rec + PACKET_REC_OFF_DATA) = w;

        for (i = 0; i < (int32_t)(PACKET_BUFFER_BYTES / 4); i++)
            *w++ = (uint32_t)orig_rand();

        MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_POOL, rec);
        rec  += PACKET_RECORD_STRIDE;
        data  = (uint8_t *)w;
    }

    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_A =
        CreateEventA(NULL, FALSE, FALSE, NULL);
    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B =
        CreateEventA(NULL, FALSE, FALSE, NULL);
    *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B2 =
        *(HANDLE *)(uintptr_t)ADDR_PACKET_EVENT_B;

    g_packetState = 2;
    for (i = 0; i < 6; i++)
        PacketSlotReset(i);

    /* The cast is the original's own mismatch: RecvThreadProc is cdecl and
     * CreateThread wants stdcall. See its note. */
    g_packetThread = CreateThread(NULL, 0,
                                  (LPTHREAD_START_ROUTINE)RecvThreadProc,
                                  NULL, 0,
                                  (LPDWORD)(uintptr_t)ADDR_PACKET_THREAD_ID);
    if (!g_packetThread) {
        orig_log((const char *)(uintptr_t)ADDR_STR_THREAD_FAILED, 0, 0, 0);
        return 0;
    }
    return SetThreadPriority(g_packetThread, THREAD_PRIORITY_HIGHEST) != 0;
}
/* The CRT clock and the state request this reaches, both still original. */
typedef int32_t (__cdecl *AM2_CrtTimeFn)(int32_t *out);
#define orig_crt_time      (*(AM2_CrtTimeFn)(uintptr_t)ADDR_CRT_TIME)
typedef void (__cdecl *AM2_RequestStateFn)(int32_t state);

/* The two widget slots the lobby repaint reaches, and the comm fields this
 * shares with msgslot.cpp. */
typedef void (__attribute__((thiscall)) *AM2_DlgUpdateFn2)(void *w);
#define AM2_START_COMM_LOG   0x418
#define AM2_START_SELF_ID    0x3CC

void __cdecl SendGameStartMsg(void)
{
    uint8_t *comm;
    uint8_t *msg = (uint8_t *)(uintptr_t)ADDR_MSG_GAME_START;

    /* NOT gated on the verbosity field, unlike the rest of this group. */
    orig_log("SendGameStartMsg\n");

    comm = (uint8_t *)(*(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT);

    if (!*(const int32_t *)(comm + COMM_OFF_STARTED)) {
        int32_t seed;
        void   *desc;

        /* Host only; a client does nothing at all, not even the tail. */
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            return;

        *(int32_t *)(msg + 8) = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(int32_t *)(msg + 12) = *(const int32_t *)(comm + AM2_START_SELF_ID);

        /* The shared seed: read once here and sent, so every machine agrees. */
        seed = orig_crt_time((int32_t *)0);
        *(int32_t *)(uintptr_t)ADDR_GAME_SEED      = seed;
        *(int32_t *)(uintptr_t)ADDR_GAME_SEED_SENT = seed;

        if (*(const int32_t *)(comm + AM2_START_COMM_LOG))
            /* "Seed is %d" with a literal 0 -- the original never prints the
             * seed it just chose. Kept. */
            orig_log("SendGameStartMsg for %d  Players: Seed is %d \n",
                    *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT), 0);

        CommGetSessionDesc(comm);

        desc = *(void **)(comm + COMM_OFF_SESSION_BUF);
        *(uint32_t *)((uint8_t *)desc + 4) |= AM2_SESSION_FLAGS_START;
        CommSetSessionDesc(comm, desc, 0);

        CommSend(comm, 0, 1, msg, (uint32_t)*(const int32_t *)(msg + 4));
    }

    /* Reached by the host AND by the already-started case. */
    SendGamePause(1, 0x10000);
    RequestState(2);
    *(int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE = 1;
    *(int32_t *)(uintptr_t)ADDR_NET_GAME         = 1;
}




/* The two names on this disagree and neither is settled from the body, which
 * is one store. orig.h calls the function ADDR_COMM_MARK_LOBBIED, from its one
 * caller; it calls +0x404 COMM_OFF_MSGS_ENABLED, from somewhere else. The
 * offset's name is the one already used elsewhere in the tree, so it is the
 * one used here -- adding a third would be the mistake checkglobals exists to
 * stop. */
void __cdecl CommMarkLobbied(void)
{
    *(int32_t *)(g_commObject + COMM_OFF_MSGS_ENABLED) = 1;
}

typedef void (__cdecl *AM2_StatLogFn)(const char *fmt, ...);
#define orig_stat_log ((AM2_StatLogFn)(uintptr_t)ADDR_LOG)

/* 0x0040F400, thiscall, three callers. Prints the traffic statistics
 * CommResetStats clears -- four lines, each of which prints nothing at all
 * when its own denominator is zero.
 *
 * That guarding is the substance of the function, not decoration: every one of
 * the seven divisions here is an unsigned `div`, which faults rather than
 * returning an infinity, so each line's `if` is what makes the division below
 * it safe. The two bandwidth lines are gated on their sample count and the two
 * packet lines on their packet count, and the elapsed seconds are CLAMPED UP
 * to 1 for the same reason -- a report taken inside the first second would
 * otherwise divide by zero.
 *
 * The percentages are computed, never stored: `over * 100 / samples`. The
 * design spec is the literal 2880 the format string prints back as
 * "design spec(%d)", so it is a constant here rather than a field.
 *
 * The elapsed division is the compiler's usual reciprocal for /1000 --
 * `mul 0x10624DD3` then `shr 6` -- reproduced as an ordinary unsigned divide,
 * which is the same function of the same inputs and lets the compiler pick
 * its own reciprocal.
 *
 * The line texts name every field this reads, which is where the seven
 * COMM_OFF_*_BW_* offsets got their names -- from the reporter rather than
 * from the reset that clears them.
 *
 * THIS ONE IS VERIFIED BY READING, and the reason is worth stating rather than
 * leaving as a zero counter. Its three callers are RecvFlowControl, the
 * multiplayer result screen and the dialog behind it, and every one of them
 * needs a live or finished session with a second player -- which DirectPlay
 * will not open on this machine. A probe confirms it: the counter is 0 and the
 * function is not entered once on the mpoptions drive, which is as far into
 * multiplayer as this project can currently get.
 *
 * So it is weaker evidence than the rest of this file, in the same way as the
 * five WM_ messages only a real session can post. What can be said is that the
 * function has NO side effects at all -- it reads fields and logs -- so its
 * entire observable output is those four lines, and a session that reached
 * them would compare them exactly through the harness's log capture. */
void __attribute__((thiscall)) CommReportStats(void *comm)
{
    const uint8_t *c = (const uint8_t *)comm;
    uint32_t       secs;

    {
        uint32_t samples = *(const uint32_t *)(c + COMM_OFF_STAT_BW_SAMPLES);

        if (samples) {
            uint32_t over = *(const uint32_t *)(c + COMM_OFF_STAT_BW_OVER);

            orig_stat_log((const char *)AM2_IMAGE(ADDR_STR_SEND_BANDWIDTH),
                          samples,
                          *(const uint32_t *)(c + COMM_OFF_STAT_BW_MAX),
                          over, over * 100u / samples,
                          AM2_COMM_BW_DESIGN_SPEC);
        }
    }

    {
        uint32_t samples = *(const uint32_t *)(c + COMM_OFF_RX_BW_SAMPLES);

        if (samples) {
            uint32_t over = *(const uint32_t *)(c + COMM_OFF_RX_BW_OVER);

            orig_stat_log((const char *)AM2_IMAGE(ADDR_STR_RECV_BANDWIDTH),
                          samples,
                          *(const uint32_t *)(c + COMM_OFF_RX_BW_MAX),
                          over, over * 100u / samples,
                          AM2_COMM_BW_DESIGN_SPEC);
        }
    }

    secs = (Ticks() - *(const uint32_t *)(c + COMM_OFF_STATS_SINCE)) / 1000u;
    if (secs < 1u)
        secs = 1u;

    {
        uint32_t packets = *(const uint32_t *)(c + COMM_OFF_STAT_PACKETS);

        if (packets) {
            uint32_t bytes = *(const uint32_t *)(c + COMM_OFF_STAT_BYTES);

            orig_stat_log((const char *)AM2_IMAGE(ADDR_STR_SENT_PACKETS),
                          packets,
                          *(const uint32_t *)(c + COMM_OFF_STAT_MAX),
                          bytes / packets, packets / secs, bytes / secs, secs);
        }
    }

    {
        uint32_t packets = *(const uint32_t *)(c + COMM_OFF_RX_PACKETS);

        if (packets) {
            uint32_t bytes = *(const uint32_t *)(c + COMM_OFF_RX_BYTES);

            orig_stat_log((const char *)AM2_IMAGE(ADDR_STR_RECV_PACKETS),
                          packets,
                          *(const uint32_t *)(c + COMM_OFF_RX_MAX),
                          bytes / packets, packets / secs, bytes / secs, secs);
        }
    }
}

/* 0x00403280, two callers -- WndProc's AM2_WM_NO_BUFFERS handler and the frame
 * path. The end of the line for a session that has run out of send buffers: it
 * reports once and asks the window to close.
 *
 * The whole function is behind a LATCH, and that is its point rather than a
 * detail. Buffers run out repeatedly once they run out at all, so without
 * ADDR_COMM_NO_BUFFERS_LATCH the log would fill with the same line while the
 * game came down, and a second WM_CLOSE would be posted for every failed send.
 * The latch is cleared with the connection, which re-arms it for the next
 * session.
 *
 * The message posted is WM_CLOSE -- 0x10, the ordinary window one -- and NOT
 * AM2_WM_NO_BUFFERS, which is what got this function called. Worth being
 * explicit about, because the two sit one line apart in the handler that
 * dispatches here and reading them as the same message would turn a quit into
 * a loop.
 *
 * VERIFIED BY READING. Reaching it needs the send buffers to actually run out,
 * which needs a live session with a peer -- the same wall CommReportStats is
 * behind. Both call sites are ours, so it is exercised the moment one is. */
void __cdecl CommNoBuffers(void)
{
    if (g_noBuffersLatch)
        return;

    g_noBuffersLatch = 1;
    orig_stat_log((const char *)AM2_IMAGE(ADDR_STR_NO_BUFFERS));
    PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND, WM_CLOSE, 0, 0);
}

/* 0x0040F380, thiscall, three callers. Clears the traffic statistics and
 * restarts the window they are measured over. Nothing here affects a packet;
 * it is all bookkeeping the report below prints.
 *
 * Four parallel rings of AM2_COMM_STAT_SAMPLES entries -- send times, send
 * sizes, receive times, receive sizes -- and the original walks all four in
 * ONE loop, thirty iterations, stepping a single pointer and reaching the
 * other three at fixed displacements of 0x78. That is what says the arrays are
 * contiguous, and they tile: 0x00C, 0x084, 0x0FC, 0x174 are 0x78 apart, which
 * is thirty dwords, and the last ends exactly where COMM_OFF_RX_MAX begins. A
 * layout that tiles is the check that no base is off by an element.
 *
 * The two TIME rings are stamped with the current tick and the two SIZE rings
 * are zeroed -- not all four cleared alike, which is the detail a tidier
 * rewrite would lose. A zero timestamp would read as a sample from 1970 rather
 * than as an empty slot.
 *
 * The same tick goes into COMM_OFF_STATS_SINCE, which only the report reads,
 * and the six bandwidth counters go to zero.
 *
 * Its counter is NOT blind -- one of the three callers is still original -- and
 * it read 0 on the multiplayer drive anyway, so a probe was used rather than a
 * guess: it fires once, during comm setup, on the mpoptions path. */
void __attribute__((thiscall)) CommResetStats(void *comm)
{
    uint8_t *c   = (uint8_t *)comm;
    uint32_t now = Ticks();
    int32_t  i;

    *(uint32_t *)(c + COMM_OFF_STATS_SINCE) = now;

    *(int32_t *)(c + COMM_OFF_STAT_PACKETS) = 0;
    *(int32_t *)(c + COMM_OFF_RX_PACKETS)   = 0;
    *(int32_t *)(c + COMM_OFF_STAT_BYTES)   = 0;
    *(int32_t *)(c + COMM_OFF_RX_BYTES)     = 0;
    *(int32_t *)(c + COMM_OFF_STAT_MAX)     = 0;
    *(int32_t *)(c + COMM_OFF_RX_MAX)       = 0;

    for (i = 0; i < AM2_COMM_STAT_SAMPLES; i++) {
        *(uint32_t *)(c + COMM_OFF_STAT_TIMES + (uint32_t)i * 4) = now;
        *(int32_t  *)(c + COMM_OFF_STAT_SIZES + (uint32_t)i * 4) = 0;
        *(uint32_t *)(c + COMM_OFF_RX_TIMES   + (uint32_t)i * 4) = now;
        *(int32_t  *)(c + COMM_OFF_RX_SIZES   + (uint32_t)i * 4) = 0;
    }

    *(int32_t *)(c + COMM_OFF_STAT_BW_MAX)     = 0;
    *(int32_t *)(c + COMM_OFF_RX_BW_MAX)       = 0;
    *(int32_t *)(c + COMM_OFF_RX_BW_SAMPLES)   = 0;
    *(int32_t *)(c + COMM_OFF_STAT_BW_SAMPLES) = 0;
    *(int32_t *)(c + COMM_OFF_STAT_BW_OVER)    = 0;
    *(int32_t *)(c + COMM_OFF_RX_BW_OVER)      = 0;
}

void __attribute__((thiscall)) CommSessionOver(void *comm)
{
    CommSendLobbyProperty(comm, 1);
}

int32_t __attribute__((thiscall)) CommPlayerSlot(void *comm, uint32_t id)
{
    return CommSlotOfId(comm, id);
}

int32_t __attribute__((thiscall)) CommDropSession(void *comm)
{
    *(int32_t *)(g_commObject + COMM_OFF_MSGS_ENABLED) = 0;
    return CommDropDirectPlay(comm);
}

#define COMM_OFF_PLAYER_COUNT 0x3D0u

int32_t __attribute__((thiscall)) CommSlotOfId(void *comm, uint32_t id)
{
    const uint8_t *p = g_commObject + AM2_PLAYER_ID;
    int32_t        n = *(const int32_t *)(g_commObject + COMM_OFF_PLAYER_COUNT);
    int32_t        i;

    (void)comm;   

/* the original ignores `this` and uses the global */
    for (i = 0; i < n; i++, p += AM2_PLAYER_STRIDE)
        if (*(const uint32_t *)p == id)
            return i;
    return 0;
}
/* 0x00402750, one caller. Puts one of the six player records back to its
 * empty state.
 *
 * Eighteen dwords cleared, ONE field set, and one list initialised. The field
 * is PLAYER_REC_OFF_OWN_BIT and it gets `1 << slot` -- so each record carries
 * a one-bit mask naming itself, which is the only thing here that depends on
 * WHICH slot is being reset.
 *
 * THE STRIDE IS COMPUTED, NOT MULTIPLIED: `slot << 6` minus slot, then `<< 5`,
 * which is slot * 63 * 32 = slot * 0x7E0. Written as the multiply it is; the
 * shift-subtract-shift is the compiler avoiding an imul and says nothing about
 * the structure.
 *
 * The original writes +0x8C in the middle of the run, between +0x34 and +0x38,
 * which is out of order and immaterial -- every one of those stores is the
 * same zero. Written in address order here, and the difference noted rather
 * than reproduced, because there is nothing to reproduce: no reader can tell.
 *
 * VERIFIED BY READING. Its one caller is the comm setup path, which needs a
 * session. */
void __cdecl PacketSlotReset(uint32_t slot)
{
    uint8_t *rec = (uint8_t *)(uintptr_t)ADDR_PLAYER_RECORDS
                   + slot * AM2_PLAYER_RECORD_BYTES;
    static const uint32_t kZeroed[] = {
        0x00, 0x04, 0x08, 0x0C, 0x10, 0x1C, 0x20, 0x28, 0x30, 0x34,
        0x38, 0x40, 0x44, 0x48, 0x4C, 0x5C, 0x8C
    };
    uint32_t i;

    for (i = 0; i < sizeof kZeroed / sizeof kZeroed[0]; i++)
        *(uint32_t *)(rec + kZeroed[i]) = 0;

    *(uint32_t *)(rec + PLAYER_REC_OFF_OWN_BIT) = 1u << slot;

    MsgListInit(rec + PLAYER_REC_OFF_MSGS);
}
/* 0x0040FB80, one caller -- ShowMpResult. EIGHT bytes: it forwards to
 * CommSendLobbyProperty with the property id 2 and returns.
 *
 * Worth stating what it is NOT. docs/functions.tsv gives this entry 48 bytes,
 * which is a merge: 0x0040FB90 is a separate function and the sprintf in it
 * belongs there. An earlier note in orig.h described this one as doing that
 * formatting, which came from sweeping the merged range; corrected.
 *
 * The property send itself declines unless the comm object is lobbied, is the
 * host, and has a lobby interface -- three tests, all inside the callee -- so
 * this forwards unconditionally and lets that decide. */
void __attribute__((thiscall)) CommPublishResult(void *comm)
{
    CommSendLobbyProperty(comm, AM2_COMM_PROPERTY_RESULT);
}




/* 0x0040F520, thiscall, one caller. Bytes sent in the last 100 ms.
 *
 * The loop gives the layout rather than the other way round: `ecx` starts at
 * this+0x84 and steps 4, and the timestamp it pairs with is read at
 * `[ecx - 0x78]`. Those two arrays are CommSend's, filled a slot per packet
 * round the ring at COMM_OFF_STAT_INDEX -- so this is a sliding window over
 * the send rate and the sum is in bytes.
 *
 * I wrote it up first as "a windowed sum of counters whose meaning is not
 * established", which was wrong twice over: both arrays were already named,
 * thirty lines apart in orig.h, and CommSend three hundred lines up this file
 * writes GetTickCount and the packet size into them. The offset ratchet
 * refused the second pair of names and that is the only reason it was caught.
 * Grep the offset as well as the address.
 *
 * The comparison is UNSIGNED on `now - stamp`, so a stamp in the FUTURE lands
 * enormous and is skipped rather than counted. Reproduced; it takes 49 days of
 * uptime to reach.
 *
 * Verified by reading, and behind the same wall as CommReportStats: its one
 * caller needs a live session with a peer. */
int32_t __attribute__((thiscall)) CommRecentTotal(void *comm)
{
    const uint8_t *c     = (const uint8_t *)comm;
    uint32_t       now   = GetTickCount();
    int32_t        total = 0;
    int32_t        i;

    for (i = 0; i < AM2_STAT_SLOTS; i++) {
        uint32_t stamp = *(const uint32_t *)(c + COMM_OFF_STAT_TIMES
                                             + (uint32_t)i * 4);

        if (now - stamp < AM2_RATE_WINDOW_MS)
            total += *(const int32_t *)(c + COMM_OFF_STAT_SIZES
                                        + (uint32_t)i * 4);
    }

    return total;
}

/* 0x00410F70, two callers. Fetch the session description, log three of its
 * fields when the comm object is verbose, publish the current player count,
 * and enumerate the players.
 *
 * THE THREE LOGGED FIELDS ARE DPSESSIONDESC2'S OWN. dwMaxPlayers,
 * dwCurrentPlayers and lpszSessionNameA sit at +0x28, +0x2C and +0x30, and the
 * original reads exactly those three -- which is what confirms
 * COMM_OFF_SESSION_DESC holds that structure rather than something shaped like
 * it. Three offsets agreeing with the SDK is better evidence than one, and it
 * is why this reads through LPDPSESSIONDESC2 rather than through offsets of
 * our own.
 *
 * IT ENDS IN A TAIL JUMP, so CommEnumPlayers' return value is this function's.
 * Neither caller reads it, which is what makes `void` honest here rather than
 * a guess; both were checked.
 *
 * The comm object is re-read from the global after the fetch and after each
 * log, five times in all. Kept: the fetch can reallocate nothing here today,
 * but the original does not assume that and neither should this. */
void __cdecl OnLobbySlave(void)
{
    uint8_t         *comm = g_commObject;
    LPDPSESSIONDESC2 sd;

    CommGetSessionDesc(comm);

    comm = g_commObject;
    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE)) {
        sd = *(LPDPSESSIONDESC2 *)(comm + COMM_OFF_SESSION_DESC);
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_SESSION_MAX),
                (int32_t)sd->dwMaxPlayers);

        comm = g_commObject;
        sd   = *(LPDPSESSIONDESC2 *)(comm + COMM_OFF_SESSION_DESC);
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_SESSION_CUR),
                (int32_t)sd->dwCurrentPlayers);

        comm = g_commObject;
        sd   = *(LPDPSESSIONDESC2 *)(comm + COMM_OFF_SESSION_DESC);
        orig_log((const char *)AM2_IMAGE(ADDR_FMT_SESSION_NAME),
                sd->lpszSessionNameA);

        comm = g_commObject;
    }

    sd = *(LPDPSESSIONDESC2 *)(comm + COMM_OFF_SESSION_DESC);
    *(int32_t *)(comm + COMM_OFF_PLAYER_COUNT) = (int32_t)sd->dwCurrentPlayers;

    CommEnumPlayers();
}

/* CommReopenSession -- original 0x0040FA00, thiscall, one caller.
 *
 * Put a finished multiplayer game back in the lobby: drop the slots whose
 * player has gone, take the JOIN-DISABLED bits back out of the session
 * description so new players can arrive, clear everybody's ready flags, and
 * empty the menu message log. ShowMpResult tail-jumps to it, so it is the
 * last thing the end screen does.
 *
 * A LOOP THAT DOES NOT ADVANCE OVER A REMOVAL, which is the second one this
 * project has transcribed this week -- DrawSelection has the same shape. The
 * original writes it as `dec ebx; sub esi, 0x70` immediately before the step
 * that adds them back, so a slot that was removed is looked at again. It
 * terminates only because the removal changes what is at that index;
 * that is a property of the callee and not of this function, and the comment
 * is here because the code alone reads like a hang.
 *
 * THE TWO BITS ARE WHAT NAMES IT. `and 0xFFFFFFDE` on the description's flags
 * clears 0x01 and 0x20 -- DPSESSION_NEWPLAYERSDISABLED and
 * DPSESSION_JOINDISABLED -- and orig.h already carries AM2_SESSION_FLAGS_START
 * as 0x21, the pair that gets OR'd in when the game STARTS. So this is
 * exactly that undone, and "reopen" is the right word rather than a guess off
 * the error string.
 *
 * Only the host does it, and only below four players: a full session has
 * nothing to reopen for.
 *
 * The clear at the end walks the same four records writing zero to
 * COMM_ARMY_OFF_READY_TO_LOAD and COMM_ARMY_OFF_READY, and then the function
 * TAIL-JUMPS to ClearMenuMsgs. Written as a call followed by a return, which
 * is the same thing here -- both are void and the argument lists are empty.
 *
 * Verified by reading. It needs a multiplayer session that has ENDED, which
 * needs a second player, which this machine cannot provide -- the same
 * standing as the five window messages in winproc.cpp.
 */
typedef int32_t (__attribute__((thiscall)) *am2_comm_remove_fn)(void *comm,
                                                                int32_t id);

void __attribute__((thiscall)) CommReopenSession(void *comm)
{
    uint8_t *c = (uint8_t *)comm;
    int32_t  i;

    for (i = 0; i < AM2_COMM_SLOTS; ) {
        int32_t id = *(const int32_t *)(c + i * COMM_ARMY_RECORD_SIZE
                                        + AM2_PLAYER_ID);

        if (id == -1) {
            /* No step: the slot is looked at again. See above. */
            CommRemovePlayer(comm, id);
            continue;
        }
        i++;
    }

    if (*(const int32_t *)(c + COMM_OFF_IS_HOST)
        && *(const uint32_t *)(c + COMM_OFF_PLAYER_COUNT) < AM2_COMM_SLOTS) {
        void *desc;

        CommGetSessionDesc(comm);

        desc = *(void **)(c + COMM_OFF_SESSION_DESC);
        if (desc) {
            *(uint32_t *)((uint8_t *)desc + AM2_DPSESSION_OFF_FLAGS)
                &= ~(uint32_t)AM2_SESSION_FLAGS_START;

            if (CommSetSessionDesc(comm, desc, 0) < 0)
                orig_log((const char *)(uintptr_t)ADDR_STR_SET_SESSION_FAIL);
        }
    }

    for (i = 0; i < AM2_COMM_SLOTS; i++) {
        uint8_t *rec = c + i * COMM_ARMY_RECORD_SIZE;

        *(int32_t *)(rec + COMM_ARMY_OFF_READY_TO_LOAD) = 0;
        *(int32_t *)(rec + COMM_ARMY_OFF_READY)         = 0;
    }

    ClearMenuMsgs();
}

/* MsgListCopyByKey -- original 0x004012C0, one caller.
 *
 * Find the node whose key matches, copy its body into the caller's buffer, and
 * answer the buffer -- or NULL if there is no such node. All of it under the
 * list's own mutex, which is the point: the packet thread is appending to this
 * list while the game reads it, so the walk and the copy have to be one
 * operation.
 *
 * It lives here rather than in msgslot.cpp for the same reason DumpMsgList
 * does: the flat half cannot name HANDLE.
 *
 * THE LENGTH COMES OFF THE NODE, NOT THE CALLER. Nothing bounds the copy
 * against the destination, so a node claiming more than the caller allotted
 * overruns it. The one caller passes a fixed-size stack buffer. The original's,
 * and reproduced -- the sizes are decided where the node is built.
 *
 * The original tests the found node for NULL a second time, immediately after
 * the loop that can only leave it non-NULL on that path. Dead, and not
 * reproduced as a branch: what it compiles to is the `if (n)` already here.
 *
 * The return is `(n ? ~0 : 0) & dst`, i.e. the destination or NULL, which is
 * the caller's way of asking "did you find it" without a second output.
 */
void *__cdecl MsgListCopyByKey(void *list, int32_t key, void *dst)
{
    uint8_t       *l = (uint8_t *)list;
    const uint8_t *n;

    WaitForSingleObject(*(HANDLE *)(l + MSGLIST_OFF_MUTEX), INFINITE);

    for (n = *(const uint8_t *const *)(l + MSGLIST_OFF_HEAD); n;
         n = *(const uint8_t *const *)(n + MSGNODE_OFF_NEXT))
        if (*(const int32_t *)(n + MSGNODE_OFF_KEY) == key)
            break;

    if (n)
        memcpy(dst, *(const void *const *)(n + MSGNODE_OFF_BODY),
               *(const uint32_t *)(n + MSGNODE_OFF_BODY_LEN));

    ReleaseMutex(*(HANDLE *)(l + MSGLIST_OFF_MUTEX));

    return n ? dst : (void *)0;
}

/* MsgListInsert -- original 0x00401150, one caller.
 *
 * Insert a node into the message list in ascending MSGNODE_OFF_KEY order,
 * under the list's own mutex, and answer the node. Four cases: an empty list,
 * before the head, between two nodes, and after the tail.
 *
 * THE KEY IS COMPARED UNSIGNED. The original uses `jbe` and `ja`, so a key
 * with the top bit set sorts ABOVE everything rather than below -- which is
 * what a wrapping counter wants and is not what a signed reading would give.
 *
 * IT IS A STABLE INSERT: the walk advances while the node's key is
 * `<=` the new one and stops at the first strictly greater, so a node ties
 * with the ones already there by going AFTER them.
 *
 * THERE IS A DEAD EARLY RETURN AND IT WOULD HAVE SKIPPED THE COUNT. After
 * taking `next` and finding it non-null, the original reloads it, tests it
 * again, and on the impossible zero falls into a path that releases the mutex
 * and returns the node WITHOUT incrementing MSGLIST_OFF_COUNT. The second test
 * cannot fail -- the first one already established it -- so the list can never
 * end up with a node it has not counted. Not reproduced as a branch, because
 * there is no input that reaches it; recorded here instead, since a reader
 * comparing the two will find one `ret` fewer than the original has.
 *
 * The tail pointer is maintained only on the two paths that need it -- empty
 * list, and append past the end -- and left alone on the two that insert
 * before an existing node. Correct, because neither can change the tail.
 */
void *__cdecl MsgListInsert(void *list, void *node)
{
    uint8_t *l = (uint8_t *)list;
    uint8_t *n = (uint8_t *)node;
    uint8_t *at;
    uint32_t key;

    WaitForSingleObject(*(HANDLE *)(l + MSGLIST_OFF_MUTEX), INFINITE);

    at = *(uint8_t **)(l + MSGLIST_OFF_HEAD);

    if (!at) {
        *(uint8_t **)(l + MSGLIST_OFF_HEAD) = n;
        *(uint8_t **)(l + MSGLIST_OFF_TAIL) = n;
        *(uint8_t **)(n + MSGNODE_OFF_PREV) = (uint8_t *)0;
        *(uint8_t **)(n + MSGNODE_OFF_NEXT) = (uint8_t *)0;
    } else {
        key = *(const uint32_t *)(n + MSGNODE_OFF_KEY);

        if (*(const uint32_t *)(at + MSGNODE_OFF_KEY) > key) {
            *(uint8_t **)(n + MSGNODE_OFF_PREV)  = (uint8_t *)0;
            *(uint8_t **)(n + MSGNODE_OFF_NEXT)  = at;
            *(uint8_t **)(l + MSGLIST_OFF_HEAD)  = n;
            *(uint8_t **)(at + MSGNODE_OFF_PREV) = n;
        } else {
            for (;;) {
                uint8_t *next = *(uint8_t **)(at + MSGNODE_OFF_NEXT);

                if (!next) {                    /* past the tail */
                    *(uint8_t **)(at + MSGNODE_OFF_NEXT) = n;
                    *(uint8_t **)(n + MSGNODE_OFF_PREV)  = at;
                    *(uint8_t **)(n + MSGNODE_OFF_NEXT)  = (uint8_t *)0;
                    *(uint8_t **)(l + MSGLIST_OFF_TAIL)  = n;
                    break;
                }

                at = next;

                if (*(const uint32_t *)(at + MSGNODE_OFF_KEY) > key) {
                    uint8_t *prev = *(uint8_t **)(at + MSGNODE_OFF_PREV);

                    if (prev)
                        *(uint8_t **)(prev + MSGNODE_OFF_NEXT) = n;
                    else
                        *(uint8_t **)(l + MSGLIST_OFF_HEAD) = n;

                    *(uint8_t **)(n + MSGNODE_OFF_PREV)  = prev;
                    *(uint8_t **)(n + MSGNODE_OFF_NEXT)  = at;
                    *(uint8_t **)(at + MSGNODE_OFF_PREV) = n;
                    break;
                }
            }
        }
    }

    *(int32_t *)(l + MSGLIST_OFF_COUNT) += 1;

    ReleaseMutex(*(HANDLE *)(l + MSGLIST_OFF_MUTEX));

    return n;
}

/* MsgListTakeFlags -- original 0x00401330, two callers.
 *
 * The same function as MsgListCopyByKey with the key test replaced by a mask
 * test: find the first node with any of ADDR_MSG_WANTED_FLAGS set, CLEAR
 * exactly those bits on it, copy its body out, and answer the bits that were
 * taken. Under the list's mutex throughout, for the same reason.
 *
 * THE SEARCH CONSUMES WHAT IT FINDS. Clearing the matched bits is what stops a
 * second call answering the same node, which is why this cannot be written as
 * a pure find. The node itself stays on the list.
 *
 * IT COPIES THE BODY EVEN THOUGH IT ALREADY HAS ITS ANSWER, and the length
 * again comes off the node rather than from the caller -- unbounded, as
 * MsgListCopyByKey is. Both callers pass a fixed stack buffer.
 *
 * The mask is a global rather than an argument. So "which messages do I want"
 * is set somewhere else entirely and this reads it fresh on every call: two
 * calls with the same list can answer differently for that reason alone.
 *
 * The return is `(found ? ~0 : 0) & taken`, and `taken` is already 0 when
 * nothing was found, so the mask is redundant. Reproduced as the plain value,
 * which is what it compiles to either way.
 */
int32_t __cdecl MsgListTakeFlags(void *list, void *dst)
{
    uint8_t       *l    = (uint8_t *)list;
    const uint32_t want = *(const uint32_t *)(uintptr_t)ADDR_MSG_WANTED_FLAGS;
    uint8_t       *n;
    uint32_t       taken = 0;

    WaitForSingleObject(*(HANDLE *)(l + MSGLIST_OFF_MUTEX), INFINITE);

    for (n = *(uint8_t **)(l + MSGLIST_OFF_HEAD); n;
         n = *(uint8_t **)(n + MSGNODE_OFF_NEXT))
        if (*(const uint32_t *)(n + MSGNODE_OFF_FLAGS) & want)
            break;

    if (n) {
        uint32_t flags = *(const uint32_t *)(n + MSGNODE_OFF_FLAGS);

        taken = flags & want;
        *(uint32_t *)(n + MSGNODE_OFF_FLAGS) = flags & ~want;

        memcpy(dst, *(const void *const *)(n + MSGNODE_OFF_BODY),
               *(const uint32_t *)(n + MSGNODE_OFF_BODY_LEN));
    }

    ReleaseMutex(*(HANDLE *)(l + MSGLIST_OFF_MUTEX));

    return (int32_t)taken;
}

/* CommPlayerLeft -- original 0x0040F790, two callers, both in WndProc's
 * player-gone handler.
 *
 * Take a departed player out: drop the DirectPlay player, find its slot, clear
 * the slot's id and its remote state, release the three pause reasons that
 * slot was holding, and mark its army ready. Answers 1, or 0 without touching
 * anything if the comm object is inactive or the id is 0 or -1.
 *
 * THE THREE PAUSE MASKS ARE `0x800 << slot`, `0x10 << slot` and
 * `0x20000 << slot` -- AND THE ORIGINAL DOES NOT COMPUTE THEM. All twelve are
 * literals across four `cmp slot, N` arms. Written the same way: collapsing
 * them into a shift would be a claim the binary does not make, and if a fifth
 * slot ever existed the shift would invent behaviour for it where the original
 * has none. A slot outside 0..3 releases NOTHING and still marks its army
 * ready.
 *
 * IT WRITES THE LAST FIELD THROUGH THE GLOBAL COMM, NOT THROUGH `this`. Every
 * other access goes via the `this` pointer; the army-ready store re-fetches
 * ADDR_COMM_OBJECT and indexes that. The same object in practice -- there is
 * one comm object -- and reproduced, because it is the only line here that
 * would still work if `this` were something else.
 *
 * THE ID IS REJECTED AT BOTH ENDS: zero and -1 are both "no player", which is
 * the convention AM2_PLAYER_ID's own comment records, and the slot's id field
 * is set back to -1 rather than to 0.
 *
 * RemovePlayer is still the original's and goes in by address; the other
 * three callees are ours and go in by name. This file already had the
 * orig_remove_player macro, further up, so it is reused rather than made a
 * second time.
 *
 * `ret 4` on a thiscall: one stack argument, `this` in ecx.
 */
int32_t __attribute__((thiscall)) CommPlayerLeft(void *comm, int32_t id)
{
    uint8_t *c = (uint8_t *)comm;
    int32_t  slot;

    if (!*(const int32_t *)(c + COMM_OFF_JOINED))
        return 0;
    if (!id || id == -1)
        return 0;

    DestroyFlow((uint32_t)id);

    slot = CommPlayerSlot(c, id);

    *(int32_t *)(c + (size_t)slot * COMM_PLAYER_STRIDE + AM2_PLAYER_ID) = -1;

    CommClearSlotRemote(c, slot);

    switch (slot) {
    case 0:
        UnPauseGame(0x800u); UnPauseGame(0x10u); UnPauseGame(0x20000u);
        break;
    case 1:
        UnPauseGame(0x1000u); UnPauseGame(0x20u); UnPauseGame(0x40000u);
        break;
    case 2:
        UnPauseGame(0x2000u); UnPauseGame(0x40u); UnPauseGame(0x80000u);
        break;
    case 3:
        UnPauseGame(0x4000u); UnPauseGame(0x80u); UnPauseGame(0x100000u);
        break;
    default:
        break;                  /* releases nothing; see above */
    }

    *(int32_t *)(*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT
                 + (size_t)slot * COMM_PLAYER_STRIDE + COMM_ARMY_OFF_READY) = 1;

    return 1;
}

/* DumpMsgList -- original 0x004013B0, one caller.
 *
 * It lives HERE rather than in msgslot.cpp with the rest of the list code:
 * msgslot.cpp is the FLAT half and cannot name HANDLE or call
 * WaitForSingleObject, and this function is nothing but a lock, three log
 * lines and an unlock. The split decides where a function goes even when
 * every other function it belongs beside is on the other side.
 *
 * Print one message list under its own mutex: "List: ", then "(%d %d)" for
 * every node, then a newline. A debug dump, and the only place those three
 * strings are used anywhere in the image.
 *
 * IT TAKES THE MUTEX AND HOLDS IT ACROSS EVERY Log CALL. In this build the
 * logger is a stub patched by the harness, so that is cheap; in the retail
 * build it was a varargs formatter running with the packet thread locked
 * out. Reproduced -- the lock is what makes the dump consistent, and moving
 * the logging outside it would be a different function.
 *
 * The second number printed is not the node's: it is the dword at +8 of
 * whatever MSGNODE_OFF_BODY points at. One dereference further than the
 * first, which is why the two offsets are named separately.
 */
void __cdecl DumpMsgList(void *list)
{
    const uint8_t *l = (const uint8_t *)list;
    const uint8_t *n;

    WaitForSingleObject(*(HANDLE *)(l + MSGLIST_OFF_MUTEX), INFINITE);

    orig_log((const char *)AM2_IMAGE(ADDR_STR_LIST_HEAD));

    for (n = *(const uint8_t *const *)(l + MSGLIST_OFF_HEAD); n;
         n = *(const uint8_t *const *)(n + MSGNODE_OFF_NEXT))
        orig_log((const char *)AM2_IMAGE(ADDR_STR_LIST_NODE),
                     *(const int32_t *)(n + MSGNODE_OFF_KEY),
                     *(const int32_t *)(*(const uint8_t *const *)
                                            (n + MSGNODE_OFF_BODY) + 8));

    orig_log((const char *)AM2_IMAGE(ADDR_STR_NEWLINE));

    ReleaseMutex(*(HANDLE *)(l + MSGLIST_OFF_MUTEX));
}

/* The comm object is the image's other static C++ global, and these four are
 * the same MSVC boilerplate the selected-units list gets in gameproc.cpp --
 * scanning for `push imm32; call ADDR_CRT_ATEXIT` returns exactly those two
 * groups and nothing else. orig.h carries the shape, and the probe showing
 * that all of it runs: CommGlobalInit reads 1, one line after the patch count.
 *
 * These four are invisible to the coverage count: they sit inside a
 * functions.tsv entry that CommConstruct already credits, so reconstructing
 * them moves nothing. That is the merged-entry problem seen from the side it
 * usually is not -- the real outstanding work is LARGER than the headline
 * number, not smaller. */

/* 0x0040DB50 and 0x0040DB70. `mov ecx, <object>; jmp <body>` apiece -- the only
 * two places the object itself is named. It is ADDR_COMM_GLOBAL, the object,
 * not ADDR_COMM_OBJECT, which is the pointer at it. */
void *__cdecl CommGlobalCtorThunk(void)
{
    return CommConstruct((void *)AM2_IMAGE(ADDR_COMM_GLOBAL));
}

void __cdecl CommGlobalDtorThunk(void)
{
    CommDestruct((void *)AM2_IMAGE(ADDR_COMM_GLOBAL));
}

/* 0x0040DB60. Hands the teardown to the CRT's atexit and returns its answer,
 * eax being untouched between the call and the `ret`. It passes OUR thunk for
 * the reason gameproc.cpp's twin does: the image address holds a detour into
 * this code by the time exit runs, and naming a patched address is exactly
 * what tools/checkseams.py exists to catch. */
int32_t __cdecl CommGlobalAtExit(void)
{
    return orig_atexit(CommGlobalDtorThunk);
}

/* 0x0040DB40. The initialiser-table entry: construct, then register the
 * teardown. Reached only as a dword in that table, never by a call. */
int32_t __cdecl CommGlobalInit(void)
{
    CommGlobalCtorThunk();
    return CommGlobalAtExit();
}

/* SendPlayerMsg -- original 0x00411270, 624 bytes, fifteen callers. The HOST's
 * game-setup broadcast: pack what every client needs to agree with us about --
 * the map checksum, the game version, the tileset and script names, and a
 * roster of who is playing -- and send it once.
 *
 * IT NAMES ITSELF: "SendPlayerMsg for %d  Players: \n". orig.h had
 * ADDR_COMM_SEND_PLAYERS, another call-site name.
 *
 * THE MESSAGE BASE IS 0x004FC3B8 AND NOT THE FIRST ADDRESS IT TOUCHES. The
 * function's first store is to 0x004FC3CC, which is 0x14 bytes in; taking that
 * for the base would put every field twenty bytes out. Three things agree on
 * the real one: SendGameMsg is handed 0x004FC3B8, the record block closes
 * exactly on the trailer, and +0x00..+0x07 are never written -- which is
 * AM2_ArmyMsgHdr, filled by the send.
 *
 * THE ROSTER IS FOUR RECORDS AND I FIRST READ SIX. (0x4FC638 - 0x4FC4B8) /
 * 0x60 is 4, it matches AM2_COMM_PLAYERS, and 0xA8 + 4 * 0x60 lands exactly on
 * the trailer at +0x228. When a loop's bound is a pointer comparison, divide;
 * eyeballing it got the flow-record array wrong earlier today too.
 *
 * THE RECORD CURSOR IS CENTRED. `ebx` walks the roster but sits 0x58 INTO each
 * record, so the fields run [ebx-0x58] to [ebx+0x04]. Reading those negative
 * displacements as offsets from a record start gives nonsense -- the same
 * shape as the trig tables, where two bases are the middles of their arrays.
 *
 * TWO GATES BEFORE ANYTHING, and the first is not an arbitrary flag:
 * COMM_OFF_DPLAY is the IDirectPlay4A pointer, so the pair reads "only if the
 * transport exists and we are the host". That is what makes the per-slot reset
 * below it safe -- nobody else is going to do it.
 *
 * THE PER-PLAYER LOG IS ONLY FOR THE DEGENERATE CASE. With two or more players
 * the roster is packed and sent; with fewer, the function logs each slot and
 * returns without sending. So those "  player: %s %x %d" lines are a "why is
 * nobody here" aid, not a normal trace.
 *
 * COLD: this is the sender for the handshake CLAUDE.md records as never
 * reaching the screen -- "a wrong total is invisible to both halves of an
 * A/B". Verified by reading; tools/ab.sh's `state` artifact is what would
 * check it if a session were available.
 */
void __cdecl SendPlayerMsg(int32_t arg)
{
    uint8_t *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    uint8_t *msg  = (uint8_t *)(uintptr_t)ADDR_PLAYER_MSG;
    int32_t  i;

    if (!*(const int32_t *)(comm + COMM_OFF_DPLAY))
        return;
    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    *(int32_t *)(msg + MSG_PLAYER_HAS_MAP) = arg;

    /* A non-zero argument clears each slot's +0x278 except the default
     * owner's, before anything is packed. */
    if (arg) {
        for (i = 0; i < *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT); i++)
            if (i != (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
                *(int32_t *)(comm + COMM_OFF_SLOT_FIELD_278
                             + i * AM2_COMM_SLOT_STRIDE) = 0;
    }

    *(uint32_t *)(msg + MSG_PLAYER_MAP_SUM) =
        *(const uint32_t *)(uintptr_t)ADDR_MAP_CHECKSUM_VAL;
    *(uint32_t *)(msg + MSG_PLAYER_RULE_SUM) =
        *(const uint32_t *)(uintptr_t)(ADDR_MAP_CHECKSUM_VAL + 4);
    *(uint32_t *)(msg + MSG_PLAYER_RULE_ARG) =
        *(const uint32_t *)(uintptr_t)(ADDR_MAP_CHECKSUM_VAL + 8);
    *(int32_t *)(msg + MSG_PLAYER_VERSION) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_VERSION;
    *(int32_t *)(msg + MSG_PLAYER_CHECKSUM) =
        *(const int32_t *)(uintptr_t)ADDR_DATA_CHECKSUM;

    strcpy((char *)(msg + MSG_PLAYER_LEVEL_NAME),
           (const char *)(uintptr_t)ADDR_MAP_NAME);
    strcpy((char *)(msg + MSG_PLAYER_MAP_NAME),
           (const char *)(uintptr_t)ADDR_MP_SCRIPT_NAME);

    *(int32_t *)(msg + MSG_PLAYER_COUNT) =
        *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
    *(uint32_t *)(msg + MSG_PLAYER_CONNECTED) =
        *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID);
    *(int32_t *)(msg + MSG_PLAYER_SCORE_LIMIT) =
        *(const int32_t *)(uintptr_t)ADDR_SCORE_LIMIT;
    *(int32_t *)(msg + MSG_PLAYER_OVER_FLAGS) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS;
    *(int32_t *)(msg + MSG_PLAYER_SETTING_22C) =
        *(const int32_t *)(uintptr_t)ADDR_GAME_SETTING_22C;

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("SendPlayerMsg for %d  Players: \n",
                 *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT));

    if (*(const uint32_t *)(comm + COMM_OFF_PLAYER_COUNT) < 2) {
        /* Nobody to send to: list the slots and leave. */
        if (!*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            return;
        for (i = 0; i < AM2_COMM_PLAYERS; i++) {
            uint8_t *slot = comm + i * AM2_COMM_SLOT_STRIDE;

            orig_log("  player: %s %x %d\n",
                     (const char *)(slot + COMM_OFF_SLOT_NAME),
                     *(const uint32_t *)(slot + COMM_OFF_PLAYER_SLOTS),
                     *(const int32_t *)(slot + COMM_OFF_SLOT_FIELD_210));
        }
        return;
    }

    for (i = 0; i < AM2_COMM_PLAYERS; i++) {
        uint8_t       *rec  = msg + MSG_PLAYER_RECORDS
                              + i * MSG_PLAYER_STRIDE;
        const uint8_t *slot = comm + i * AM2_COMM_SLOT_STRIDE;

        *(uint32_t *)(rec + MSGREC_OFF_ID) =
            *(const uint32_t *)(slot + COMM_OFF_PLAYER_SLOTS);
        *(int32_t *)(rec + MSGREC_OFF_FIELD_04) =
            *(const int32_t *)(slot + COMM_OFF_SLOT_FIELD_210);
        *(int32_t *)(rec + MSGREC_OFF_ZERO) = 0;
        *(int32_t *)(rec + MSGREC_OFF_POINTS) =
            ((const int32_t *)(uintptr_t)ADDR_ARMY_POINTS)[i];
        *(int32_t *)(rec + MSGREC_OFF_FIELD_54) =
            *(const int32_t *)(slot + COMM_OFF_SLOT_FIELD_258);
        *(int32_t *)(rec + MSGREC_OFF_FIELD_58) =
            *(const int32_t *)(slot + COMM_OFF_SLOT_FIELD_270);
        *(int32_t *)(rec + MSGREC_OFF_FIELD_5C) =
            *(const int32_t *)(slot + COMM_OFF_SLOT_FIELD_25C);

        strcpy((char *)(rec + MSGREC_OFF_NAME),
               (const char *)(slot + COMM_OFF_SLOT_NAME));

        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log("  player: %s %x %d\n",
                     (const char *)(rec + MSGREC_OFF_NAME),
                     *(const uint32_t *)(rec + MSGREC_OFF_ID),
                     *(const int32_t *)(rec + MSGREC_OFF_FIELD_04));
    }

    SendGameMsg(msg, 0, 1);
}

/* DestroyFlow -- original 0x004029B0, 544 bytes, seven callers. Tear down one
 * player's flow queue when they leave: reclaim everything still queued for
 * them, and free the record.
 *
 * IT NAMES ITSELF TWICE -- "DestroyFlow: Flow queue for Player %x not found"
 * and "...for me (%x) not found" -- where orig.h had ADDR_REMOVE_PLAYER, a
 * name off a call site. Fifth function this batch named from its own strings.
 *
 * IT NEEDED NO NEW NAMES AT ALL, which is worth recording as a measure of how
 * far the comm vocabulary has come: FLOW_OFF_SEQUENCE, ADDR_MSG_LIST_SENDQ,
 * ADDR_MSG_LIST_POOL, ADDR_PLAYER_SLOT_MASK, ADDR_PLAYER_RECORDS,
 * AM2_PLAYER_RECORD_BYTES, MsgListSetFlag, MsgListRemove, MsgListAdd and
 * FindPlayerById were all already there. Early units in this batch cost eight
 * or nine names each.
 *
 * "SIMULATING ACKS" IS THE WHOLE IDEA. The departing player can no longer
 * acknowledge anything, so every message queued between their last ack and our
 * own FLOW_OFF_SEQUENCE is treated as if they had -- and any that no OTHER
 * connected player still wants, by ADDR_PLAYER_SLOT_MASK against the node's
 * own bits, goes back on the free pool.
 *
 * AND IT CAN UNPAUSE THE GAME, which its name gives no hint of. The transport
 * pauses play when the send pool runs dry -- bit 0x8000 of the pause mask,
 * which CLAUDE.md records as "one bit per reason the game is paused" -- and
 * once reclaiming has put the pool back above 300 buffers this clears that
 * reason. So a player leaving is one of the things that can restart a stalled
 * game.
 *
 * THE RECORD ARRAY IS SIX ENTRIES and I first read it as eight by eyeballing
 * the bounds instead of dividing: (0x4F48C0 - 0x4F1980) / 0x7E0 is 6, and
 * ADDR_PLAYER_RECORDS already said "six of them". The index arithmetic is the
 * compiler expanding that multiply -- `(i << 6) - i` then `<< 5` is i * 0x7E0
 * -- and reading it as a shift pair gives a wrong stride.
 *
 * ITS FIRST LOG LINE NAMES THE WRONG PLAYER. "Flow queue for Player %x not
 * found" is handed COMM_OFF_OUR_PLAYER_ID, not the `id` that failed to
 * resolve, so the diagnostic points at us rather than at whoever left. The
 * original's, and reproduced -- but worth knowing before trusting that line
 * in a capture.
 *
 * ONE NEAR-MISS WORTH RECORDING: I had 0x00401040 down as `MsgListCount`,
 * because the log beside it says "sendqueue Size = %d". It is MsgField12 --
 * a field read, not a count. Naming a CALLEE from what the surrounding log
 * message seems to mean is the same mistake as naming a function from one
 * call site, one level down.
 *
 * COLD: nothing here runs without a live DirectPlay session. Verified by
 * reading; a clean A/B says only that nothing else regressed.
 */
int32_t __cdecl DestroyFlow(uint32_t id)
{
    uint8_t *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    uint8_t *flow;
    uint8_t *mine;
    int32_t  seq;
    int32_t  i;

    if (!id || id == 0xFFFFFFFFu)
        return 0;

    flow = (uint8_t *)FindPlayerById(id);
    if (!flow)
        orig_log("DestroyFlow: Flow queue for Player %x not found\n",
                 *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID));

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("About to Destroy FlowQ for id %x sendqueue Size = %d \n",
                 id, MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ));

    if (!flow)
        return 0;

    mine = (uint8_t *)FindPlayerById(
               *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID));
    if (!mine) {
        orig_log("DestroyFlow: Flow queue for me (%x) not found\n",
                 *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID));
        return 0;
    }

    DrainMsgList(flow + FLOW_OFF_QUEUE);

    if (id != *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID)) {
        uint32_t mask = GetReSendMask(id);

        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log(" Simulating Acks for  seq %d thru %d\n",
                     *(const int32_t *)(flow + FLOW_OFF_HE_HAS) + 1,
                     *(const int32_t *)(mine + FLOW_OFF_SEQUENCE) - 1);

        for (seq = *(const int32_t *)(flow + FLOW_OFF_HE_HAS) + 1;
             seq <= *(const int32_t *)(mine + FLOW_OFF_SEQUENCE) - 1; seq++) {
            uint8_t *node = (uint8_t *)MsgListSetFlag(
                (void *)(uintptr_t)ADDR_MSG_LIST_SENDQ, seq, 0, mask);

            if (!node)
                continue;
            if (*(const uint32_t *)(uintptr_t)ADDR_PLAYER_SLOT_MASK
                & *(const uint32_t *)(node + MSGNODE_OFF_FLAGS))
                continue;   /* somebody still connected wants it */

            if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
                orig_log("Adding to freelist from sendque Buffer seq %d "
                         " elelment %x \n",
                         *(const int32_t *)(node + MSGNODE_OFF_KEY), node);

            MsgListRemove((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ, node);
            MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_POOL, node);
        }
    }

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
        orig_log("Finished Destroying FlowQ for id %x sendqueue Size = %d \n",
                 id, MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ));

    {
        int32_t nfree = MsgField12((void *)(uintptr_t)ADDR_MSG_LIST_POOL);

        if (nfree > AM2_FLOW_UNPAUSE_FREE
            && (GetPauseFlags() & AM2_PAUSE_NO_BUFFERS)) {
            orig_log("FLOW UNPAUSE nfree = %d\n", nfree);
            UnPauseGame(AM2_PAUSE_NO_BUFFERS);
        }
    }

    for (i = 0; i < AM2_PLAYER_RECORDS; i++) {
        uint8_t *rec = (uint8_t *)(uintptr_t)ADDR_PLAYER_RECORDS
                       + i * AM2_PLAYER_RECORD_BYTES;

        if (*(const uint32_t *)rec == id) {
            *(uint32_t *)rec = 0;
            return 1;
        }
    }

    return 0;
}

/* CommSystemMessage -- original 0x00410090, one caller: the message drain. The
 * DirectPlay SYSTEM message handler, and it names its own five cases in its
 * own format strings, which is the whole of why they are named that way.
 *
 * THE MESSAGE LAYOUT IS THE SDK's, not ours. The dword at +0 is the DPSYS_
 * type, +8 is the player or group id and +0x20 is the name -- which is
 * DPMSG_CREATEPLAYERORGROUP's shape, and <dplay.h> is already pulled in
 * through win32.h, so the constants come from the SDK rather than being
 * restated here.
 *
 * ITS THREE TRAILING ARGUMENTS ARE size, from AND to, and the format strings
 * are what say so: "DPSYS_HOST Size=%d, from=%x, to = %x" takes them in that
 * order off [esp+0x18], [esp+0x1C] and [esp+0x20], four pushes deep.
 *
 * AND ITS ONE CALLER DOES NOT AGREE WITH THOSE NAMES. CommDrainMsgs passes
 * (msg, node dpid, 0, node +0x0C), so "Size=%d" logs a player id and "from=%x"
 * is always zero. The parameter names here stay the function's own -- they are
 * what it says about itself, which is the rule -- and the mismatch is recorded
 * rather than resolved by renaming to fit the one call site, which is the
 * mistake this project has made five times.
 *
 * THE HOST AND NON-HOST PATHS OF A JOIN ARE DIFFERENT LENGTHS, and the short
 * one is the non-host's: register the new player, repaint the current screen,
 * done. Everything else -- the slot table, the name copy, the session
 * description, the settle, the broadcast -- is the HOST's work.
 *
 * IT REMOVES PLAYER -1 FOR EVERY EMPTY SLOT. The host loop walks all four
 * slots and calls CommRemovePlayer when the slot's id IS -1, passing that -1
 * straight through. Nothing here explains it and the sense reads backwards
 * from what one would write; the `jne` skips the call for an OCCUPIED slot, so
 * this is not a misreading of the branch. Reproduced.
 *
 * IT BUSY-WAITS ONE SECOND ONCE THE FOURTH PLAYER IS IN. GetTickCount in a
 * loop, with nothing pumped and nothing drawn -- the window stops responding
 * for AM2_COMM_JOIN_SETTLE_MS. Written out as the original has it, including
 * the leading test that skips the loop entirely when a second has already
 * passed between the two reads.
 *
 * AND IT POSTS A MESSAGE NOTHING LISTENS FOR. AM2_WM_PLAYER_JOINED goes to the
 * window after a successful join, and WndProc has no case for it -- the six
 * messages orig.h names were found from the RECEIVER's switch, which is
 * exactly why this one was missing. DefWindowProc eats it.
 *
 * COLD, AND SAYING SO PLAINLY: this needs a live DirectPlay session with a
 * second player, which this machine cannot open. It is verified by reading and
 * by the ratchets, like the rest of the comm layer. */
typedef void (__attribute__((thiscall)) *AM2_ScreenUpdateFn)(void *w);
typedef void (__attribute__((thiscall)) *AM2_ScreenPaintFn)(void *w, RECT r);

/* Slot 1 with the screen's own rectangle -- frame.cpp's State1Menu spells the
 * identical call, and this is the second site to want it. */
static void ScreenPaint(void *dlg)
{
    ((AM2_ScreenPaintFn *)*(void **)dlg)[AM2_DLG_SLOT_PAINT](
        dlg, *(const RECT *)((const uint8_t *)dlg + AM2_DLG_OFF_RECT));
}

void __cdecl CommSystemMessage(void *msg, int32_t size, int32_t from,
                               int32_t to)
{
    const DPMSG_CREATEPLAYERORGROUP *m =
        (const DPMSG_CREATEPLAYERORGROUP *)msg;
    uint8_t *comm = g_commObject;
    int32_t  slot;
    uint32_t t0;

    switch ((int32_t)m->dwType) {
    case DPSYS_DESTROYPLAYERORGROUP:
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
            orig_log((const char *)(uintptr_t)ADDR_STR_SYS_DESTROY_PLAYER,
                     m->dpId, to);
        PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND, AM2_WM_PLAYER_GONE,
                     (WPARAM)m->dpId, (LPARAM)msg);
        return;

    case DPSYS_CREATEPLAYERORGROUP:
        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE)) {
            orig_log((const char *)(uintptr_t)ADDR_STR_SYS_CREATE_PLAYER,
                     to, m->dpnName.lpszShortNameA, m->dpId);
            comm = g_commObject;
        }

        slot = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(int32_t *)(comm + slot * AM2_COMM_SLOT_STRIDE
                     + COMM_OFF_SLOT_FIELD_25C) = 1;

        comm = g_commObject;
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST)) {
            /* The short path: register and repaint, nothing else. */
            CommRegisterSelf((uint32_t)m->dpId);
            if (*(void **)(uintptr_t)ADDR_PAINT_OBJECT)
                ScreenPaint(*(void **)(uintptr_t)ADDR_PAINT_OBJECT);
            return;
        }

        {
            int32_t i;

            for (i = 0; i < AM2_COMM_ARMY_COUNT; i++) {
                int32_t id = *(const int32_t *)(comm
                                                + i * AM2_COMM_SLOT_STRIDE
                                                + COMM_OFF_PLAYER_SLOTS);

                /* Yes, when the slot is EMPTY, and -1 is what goes in. */
                if (id == -1) {
                    CommRemovePlayer(comm, id);
                    comm = g_commObject;
                }
            }
        }

        *(int32_t *)(comm
                     + *(const int32_t *)(uintptr_t)ADDR_OUR_SLOT
                           * AM2_COMM_SLOT_STRIDE
                     + COMM_OFF_PLAYER_SLOTS) =
            *(const int32_t *)(comm + COMM_OFF_OUR_PLAYER_ID);

        comm = g_commObject;
        slot = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(int32_t *)(comm + slot * AM2_COMM_SLOT_STRIDE
                     + COMM_OFF_PLAYER_SLOTS) = (int32_t)m->dpId;

        comm = g_commObject;
        CommSetSlotRemote(comm,
                          *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT));

        if ((int32_t)strlen(m->dpnName.lpszShortNameA) < AM2_COMM_NAME_MAX) {
            comm = g_commObject;
            strcpy((char *)(comm
                            + *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT)
                                  * AM2_COMM_SLOT_STRIDE
                            + COMM_OFF_SLOT_NAME),
                   m->dpnName.lpszShortNameA);
        }

        comm = g_commObject;
        slot = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(uint32_t *)(comm + slot * AM2_COMM_SLOT_STRIDE
                      + COMM_OFF_SLOT_JOINED_MS) = Ticks();

        comm = g_commObject;
        slot = *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT);
        *(int32_t *)(comm + slot * AM2_COMM_SLOT_STRIDE
                     + COMM_OFF_SLOT_FIELD_278) = 0;

        comm = g_commObject;
        *(int32_t *)(comm + COMM_OFF_PLAYER_COUNT) =
            *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT) + 1;

        comm = g_commObject;
        if (*(const uint32_t *)(comm + COMM_OFF_PLAYER_COUNT)
            >= (uint32_t)AM2_COMM_ARMY_COUNT) {
            uint8_t *desc;

            CommGetSessionDesc(comm);
            desc = *(uint8_t **)(g_commObject + COMM_OFF_SESSION_DESC);
            *(uint32_t *)(desc + 4) |= AM2_SESSION_FULL_FLAGS;
            CommSetSessionDesc(g_commObject, desc, 0);
        }

        CommRegisterSelf((uint32_t)m->dpId);

        if (*(void **)(uintptr_t)ADDR_PAINT_OBJECT) {
            void *dlg = *(void **)(uintptr_t)ADDR_PAINT_OBJECT;

            ((AM2_ScreenUpdateFn *)*(void **)dlg)[AM2_DLG_SLOT_UPDATE](dlg);
            ScreenPaint(*(void **)(uintptr_t)ADDR_PAINT_OBJECT);
        }

        /* One second with nothing pumped -- see the note above. */
        t0 = Ticks();
        if (Ticks() - t0 < AM2_COMM_JOIN_SETTLE_MS)
            while (Ticks() - t0 < AM2_COMM_JOIN_SETTLE_MS)
                ;

        RefreshMapSelection();
        SendPlayerMsg(1);
        PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND, AM2_WM_PLAYER_JOINED,
                     (WPARAM)m->dpId, (LPARAM)msg);
        return;

    case DPSYS_SESSIONLOST:
        orig_log((const char *)(uintptr_t)ADDR_STR_SYS_SESSION_LOST,
                 from, to, 0);
        return;

    case DPSYS_HOST:
        orig_log((const char *)(uintptr_t)ADDR_STR_SYS_HOST, size, from, to);
        PostMessageA(*(HWND *)(uintptr_t)ADDR_HWND, AM2_WM_HOST_CHANGED, 0, 0);
        return;

    case DPSYS_SETSESSIONDESC:
        if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
            CommGetSessionDesc(comm);
        return;

    default:
        orig_log((const char *)(uintptr_t)ADDR_STR_SYS_UNHANDLED,
                 m->dwType, m->dwType, 0);
        return;
    }
}

/* ProcessResendQueue -- original 0x00403050, 560 bytes, one caller. Drain up
 * to three messages off the resend list, sending each to every player whose
 * resend mask wants it.
 *
 * IT NAMES ITSELF THREE TIMES -- "Entering ProcessResendQueue",
 * "RESENDING %d to %x size %d", "Exiting ProcessResendQueue" -- where orig.h
 * had it as ADDR_COMM_FRAME_POST_C, a name for where it sits in the frame
 * rather than for what it does. Fourth function this batch to be named from
 * its own log lines.
 *
 * THREE MESSAGES PER CALL, and that bound is explicit: `inc esi; cmp esi, 2;
 * jg` ends the outer loop, so a backed-up queue drains at a fixed rate instead
 * of in one burst. A CommSend failure abandons the whole drain, not just that
 * player.
 *
 * THE CHECKSUM FIELD IS ZEROED BEFORE IT IS COMPUTED. XorChecksum runs over
 * the whole buffer including that field, so leaving the previous value there
 * would give a different answer; the order is load-bearing and not tidiness.
 *
 * ITS SIX EXITS ARE ONE EXIT AND A DIAGNOSTIC LADDER. Five of them are
 * per-HRESULT arms that all do the same thing -- log and stop -- and every one
 * of those codes was already named, by the sibling in commmsg.cpp that walks
 * the identical CommSend failure chain. Written to match that function rather
 * than as five returns.
 *
 * THE PACKET LAYOUT WAS ALREADY NAMED and confirms the read: PACKET_OFF_LEN,
 * PACKET_OFF_SEQ and PACKET_OFF_CHECKSUM are 4, 8 and 0x0C, exactly where the
 * disassembly puts them, so only +0x10 is new here. COMM_OFF_PLAYER_SLOTS and
 * AM2_COMM_PLAYERS existed too.
 *
 * COLD, AND SAID PLAINLY: nothing here runs without a live DirectPlay session,
 * which this environment cannot open. Verified by reading; a clean A/B says
 * only that nothing else regressed.
 */
void __cdecl ProcessResendQueue(void)
{
    uint8_t  *comm;
    uint8_t  *buf = (uint8_t *)(uintptr_t)ADDR_RESEND_BUF;
    int32_t   taken;
    int32_t   drained = 0;

    taken = MsgListTakeFlags((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ, buf);
    if (!taken)
        return;

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;

    for (;;) {
        int32_t off;

        if (*(const int32_t *)(comm + COMM_OFF_VERBOSE) && drained == 0)
            orig_log("Entering ProcessResendQueue \n");

        for (off = 0; off < (int32_t)(AM2_COMM_PLAYERS * AM2_COMM_SLOT_STRIDE);
             off += AM2_COMM_SLOT_STRIDE) {
            uint32_t id = *(const uint32_t *)(comm + COMM_OFF_PLAYER_SLOTS + off);
            uint8_t *flow;
            int32_t  hr;

            if (!id || id == 0xFFFFFFFFu
                || id == *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID))
                continue;

            flow = (uint8_t *)FindPlayerById(id);
            if (!flow)
                continue;
            if (!(taken & (int32_t)GetReSendMask(id)))
                continue;
            if (*(const uint32_t *)(buf + PACKET_OFF_SEQ)
                < *(const uint32_t *)(flow + FLOW_OFF_HE_HAS))
                continue;

            *(uint32_t *)(buf + PACKET_OFF_ACK) =
                *(const uint32_t *)(flow + FLOW_OFF_FIELD_04);
            *(uint32_t *)(buf + PACKET_OFF_CHECKSUM) = 0;
            *(uint32_t *)(buf + PACKET_OFF_CHECKSUM) = XorChecksum(buf);

            MsgSlotA2(flow, *(const uint32_t *)(buf + PACKET_OFF_SEQ));

            hr = CommSend(*(void *const *)(uintptr_t)ADDR_COMM_OBJECT, id, 0,
                          buf, *(const uint32_t *)(buf + PACKET_OFF_LEN));

            if (*(const int32_t *)(comm + COMM_OFF_VERBOSE))
                orig_log("RESENDING %d to %x size %d \n",
                        *(const int32_t *)(buf + PACKET_OFF_SEQ), id,
                        *(const int32_t *)(buf + PACKET_OFF_LEN));

            if (hr < 0) {
                orig_log("DPlaySend Failure %x to %x size %d\n", hr, id,
                        *(const int32_t *)(buf + PACKET_OFF_LEN));

                if ((uint32_t)hr == AM2_DPERR_BUSY)
                    orig_log("DPLAY ERROR: Busy\n");
                else if ((uint32_t)hr == AM2_DPERR_INVALIDOBJECT)
                    orig_log("DPLAY ERROR: Invalid Object\n");
                else if ((uint32_t)hr == AM2_E_INVALIDARG)
                    orig_log("DPLAY ERROR: INVALID PARAMETERS\n");
                else if ((uint32_t)hr == AM2_DPERR_INVALIDPLAYER)
                    orig_log("DPLAY ERROR: INVALID PLAYER\n");
                else if ((uint32_t)hr == AM2_DPERR_SENDTOOBIG)
                    orig_log("DPLAY ERROR: Send too big\n");
                return;
            }
        }

        if (++drained > 2)
            break;

        taken = MsgListTakeFlags((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ, buf);
        if (!taken)
            break;
    }

    if (*(const int32_t *)(comm + COMM_OFF_VERBOSE) && drained)
        orig_log("Exiting ProcessResendQueue \n");
}

/* SendGameMsg -- original 0x004022D0, 928 bytes, fourteen callers. Every
 * outgoing packet in the game goes through here: the reliable ones are copied
 * into the send queue on the way past, the latency emulator gets its chance to
 * drop or delay them, and what is left is handed to CommSend.
 *
 * IT NAMES ITSELF -- "SendGameMsg, first message to %x, hehas set to %d" --
 * and that one line settles two things this port had only field numbers for.
 * FLOW_OFF_HE_HAS is the sequence a player is known to hold, written once on
 * the first packet ever sent to him; and the "Flow" of "Error Send can't find
 * Flow for Player %x" is the same record CommSend calls a PLAYER, which is the
 * synonym orig.h already records.
 *
 * THE FIVE ESP DISPLACEMENTS ARE NOT AT ONE DEPTH, which is the whole
 * difficulty of reading it and the same hazard that deferred CreateTrooper and
 * LoadType2. The four register pushes put the arguments at [esp+0x14],
 * [esp+0x18] and [esp+0x1C]; then `mov ebp, [esp+0x20]` at 0x004023B4 reads
 * the SECOND argument, because two words are still on the stack from a
 * MsgListAdd whose cleanup the compiler deferred, and `mov ebp, [esp+0x1C]` at
 * 0x0040253A reads it again with one word pushed. Both are the same restore of
 * `to` into a register the node pointer had borrowed. Taken at face value they
 * would be two more arguments and a third would move.
 *
 * The cleanup at 0x0040233C is `add esp, 0x14` for FIVE pushes -- GetPlayerMask's
 * one argument and MsgListSetFlag's four, merged. Twenty is 0x14 and not 20.
 *
 * ONE BUFFER PER SEQUENCE, NOT PER RECIPIENT. MsgListSetFlag is called with
 * `set` before the pool is touched, so a sequence already queued simply gains
 * the new recipient's bit and no second copy is made. ArmyMessageFlush sends
 * the same packet to every player in turn, so this is the common case and not
 * an edge.
 *
 * TWO PATHS LEAK A POOL BUFFER on "Message Too Big", and it is reproduced. The
 * node has been taken off the free list and filled in by the time the length
 * is checked, and neither arm puts it back. The original's, on a path that
 * needs a message longer than the 0x400-byte buffer to reach.
 *
 * THE LOSS EMULATION CANNOT RUN IN THIS BUILD: nothing anywhere in the image
 * writes FLOW_OFF_LOSS_BURST or FLOW_OFF_LOSS_PCT, so both arms read zero and
 * fall through. See orig.h. The LAG half is live -- the host sets it through
 * RecvFlowControl -- and a delayed packet is copied into ADDR_MSG_LIST_DELAYED
 * keyed on its due time, which is what FlushDelayedSends drains.
 *
 * COLD, AND SAID PLAINLY. Everything past the first `if` needs a live
 * DirectPlay session, which this environment cannot open; what an A/B here
 * compares is the refusal. Verified by reading, like its siblings.
 */
int32_t __cdecl SendGameMsg(void *msg, int32_t to, int32_t flags)
{
    uint8_t *comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    uint8_t *pkt  = (uint8_t *)msg;
    uint8_t *flow = 0;      /* the DESTINATION's record; only looked up for 0x0B */
    uint8_t *mine;
    uint8_t *node;
    int32_t  roll;
    int32_t  hr;

    if (*(const int32_t *)(comm + COMM_OFF_JOINED) == 0)
        return (int32_t)AM2_E_FAIL;

    if (to == -1)
        return (int32_t)AM2_E_FAIL;

    if (*(const int32_t *)(comm + COMM_OFF_SAW_KIND_31) != 0)
        return (int32_t)AM2_DPERR_SESSIONLOST;

    if (*(const uint32_t *)(pkt + COMMMSG_OFF_KIND) == AM2_COMMMSG_KIND_FLOW) {
        uint32_t mask = GetPlayerMask((uint32_t)to);

        if (!MsgListSetFlag((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ,
                            *(const int32_t *)(pkt + PACKET_OFF_SEQ),
                            1, (int32_t)mask)) {
            node = (uint8_t *)MsgListRemHead(
                       (void *)(uintptr_t)ADDR_MSG_LIST_POOL);
            if (!node) {
                orig_log("Out of Send Buffers \n");
                CommNoBuffers();
                return (int32_t)AM2_E_OUTOFMEMORY;
            }

            *(uint32_t *)(node + MSGNODE_OFF_FLAGS) = mask;
            *(uint32_t *)(node + MSGNODE_OFF_KEY) =
                *(const uint32_t *)(pkt + PACKET_OFF_SEQ);
            *(uint32_t *)(node + MSGNODE_OFF_STAMP) = GetTickCount();

            if (*(const uint32_t *)(pkt + PACKET_OFF_LEN)
                > *(const uint32_t *)(node + PACKET_REC_OFF_SIZE)) {
                orig_log("Message Too Big \n");
                return (int32_t)AM2_DPERR_SENDTOOBIG;
            }

            memcpy(*(void *const *)(node + MSGNODE_OFF_BODY), pkt,
                   *(const uint32_t *)(pkt + PACKET_OFF_LEN));
            *(uint32_t *)(node + MSGNODE_OFF_BODY_LEN) =
                *(const uint32_t *)(pkt + PACKET_OFF_LEN);

            MsgListAdd((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ, node);
        }

        flow = (uint8_t *)FindPlayerById((uint32_t)to);
        if (!flow) {
            orig_log("Error Send can't find Flow for Player %x\n", to);
        } else {
            /* The checksum covers the ack, so the order is load-bearing: stamp
             * it, clear the checksum field, then compute over the whole
             * buffer. The same three lines as ProcessResendQueue. */
            *(uint32_t *)(pkt + PACKET_OFF_ACK) =
                *(const uint32_t *)(flow + FLOW_OFF_FIELD_04);
            *(uint32_t *)(pkt + PACKET_OFF_CHECKSUM) = 0;
            *(uint32_t *)(pkt + PACKET_OFF_CHECKSUM) = XorChecksum(pkt);

            *(uint32_t *)(flow + FLOW_OFF_ACK_SENT) =
                *(const uint32_t *)(pkt + PACKET_OFF_ACK);
            *(uint32_t *)(flow + FLOW_OFF_SENT_AT) = GetTickCount();
            *(int32_t *)(flow + FLOW_OFF_SENT_PACKETS) += 1;
            *(int32_t *)(flow + FLOW_OFF_SENT_BYTES) +=
                *(const int32_t *)(pkt + PACKET_OFF_LEN);

            if (*(const int32_t *)(flow + FLOW_OFF_HE_HAS) == 0
                && *(const uint32_t *)(pkt + PACKET_OFF_SEQ) > 1u) {
                int32_t hehas = *(const int32_t *)(pkt + PACKET_OFF_SEQ) - 1;

                *(int32_t *)(flow + FLOW_OFF_HE_HAS) = hehas;
                if (*(const int32_t *)(
                        *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT
                        + COMM_OFF_VERBOSE))
                    orig_log("SendGameMsg, first message to %x, hehas set "
                             "to %d\n", to, hehas);
            }

            /* Not for a broadcast: `to` of 0 is DPID_ALLPLAYERS and there is
             * no one record to mark. */
            if (to)
                MsgSlotA1(flow, *(const uint32_t *)(pkt + PACKET_OFF_SEQ));
        }
    }

    comm = *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    mine = (uint8_t *)FindPlayerById(
               *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID));
    if (!mine)
        orig_log("Error Send can't find My Flow for Player %x\n",
                 *(const int32_t *)(
                     *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT
                     + COMM_OFF_OUR_PLAYER_ID));

    /* Rolled whether or not anything wants it, and stored where nothing reads
     * it. Kept because GameRand advances a shared sequence. */
    roll = orig_rand() % AM2_SEND_ROLL_MOD;
    *(int32_t *)(uintptr_t)ADDR_SEND_ROLL = roll;

    if (mine) {
        int32_t pct = *(const int32_t *)(mine + FLOW_OFF_LOSS_PCT);

        if (*(const int32_t *)(mine + FLOW_OFF_LOSS_BURST) != 0) {
            /* Drop the LAST `pct` of every hundred packets to this player --
             * a burst rather than a scatter, and it needs a destination
             * record, so a broadcast is never dropped this way. */
            if (pct != 0 && flow != 0
                && (int32_t)(*(const uint32_t *)(flow + FLOW_OFF_SENT_PACKETS)
                             % (uint32_t)AM2_SEND_ROLL_MOD)
                   > AM2_SEND_ROLL_MOD - pct
                && !(flags & AM2_DPSEND_GUARANTEED))
                return 0;
        } else {
            if (pct != 0 && roll <= pct
                && !(flags & AM2_DPSEND_GUARANTEED))
                return 0;
        }

        if (*(const int32_t *)(mine + FLOW_OFF_LAG_MS) != 0) {
            int32_t due = RandomAround(
                              *(const int32_t *)(mine + FLOW_OFF_LAG_MS),
                              *(const int32_t *)(mine + FLOW_OFF_LAG_SPREAD))
                          + (int32_t)GetTickCount();

            node = (uint8_t *)MsgListRemHead(
                       (void *)(uintptr_t)ADDR_MSG_LIST_POOL);
            if (!node) {
                orig_log("Latency Emulation is Out of Send Buffers \n");
                /* and fall through to the plain send */
            } else {
                *(int32_t *)(node + MSGNODE_OFF_KEY)    = due;
                *(int32_t *)(node + MSGNODE_OFF_FLAGS)  = flags;
                *(uint32_t *)(node + MSGNODE_OFF_STAMP) = GetTickCount();
                *(int32_t *)(node + MSGNODE_OFF_TO)     = to;
                *(uint32_t *)(node + MSGNODE_OFF_FROM) =
                    *(const uint32_t *)(
                        *(uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT
                        + COMM_OFF_OUR_PLAYER_ID);

                if (*(const uint32_t *)(pkt + PACKET_OFF_LEN)
                    > *(const uint32_t *)(node + PACKET_REC_OFF_SIZE)) {
                    orig_log("Message Too Big \n");
                    return (int32_t)AM2_DPERR_SENDTOOBIG;
                }

                memcpy(*(void *const *)(node + MSGNODE_OFF_BODY), pkt,
                       *(const uint32_t *)(pkt + PACKET_OFF_LEN));
                *(uint32_t *)(node + MSGNODE_OFF_BODY_LEN) =
                    *(const uint32_t *)(pkt + PACKET_OFF_LEN);

                MsgListInsert((void *)(uintptr_t)ADDR_MSG_LIST_DELAYED, node);
                return 0;
            }
        }
    }

    hr = CommSend(*(void *const *)(uintptr_t)ADDR_COMM_OBJECT, (uint32_t)to,
                  (uint32_t)flags, pkt,
                  *(const uint32_t *)(pkt + PACKET_OFF_LEN));
    if (hr >= 0)
        return hr;

    orig_log("DPlaySend Failure %x to %x size %d\n", hr, to,
             *(const int32_t *)(pkt + PACKET_OFF_LEN));

    if ((uint32_t)hr == AM2_DPERR_BUSY)
        orig_log("DPLAY ERROR: Busy\n");
    else if ((uint32_t)hr == AM2_DPERR_INVALIDOBJECT)
        orig_log("DPLAY ERROR: Invalid Object\n");
    else if ((uint32_t)hr == AM2_E_INVALIDARG)
        orig_log("DPLAY ERROR: INVALID PARAMETERS\n");
    else if ((uint32_t)hr == AM2_DPERR_INVALIDPLAYER)
        orig_log("DPLAY ERROR: INVALID PLAYER\n");
    else if ((uint32_t)hr == AM2_DPERR_SENDTOOBIG)
        orig_log("DPLAY ERROR: Send too big\n");

    orig_log("gpComm->Send Failure2 %x to %x size %d\n", hr, to,
             *(const int32_t *)(pkt + PACKET_OFF_LEN));

    *(int32_t *)(uintptr_t)ADDR_EXIT_GAME_FLAG = 1;
    ExitGamePostClose();
    return hr;
}

int dplay_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_COMM_SYSTEM_MSG, (const void *)CommSystemMessage,
                        "CommSystemMessage", 1);
    rc |= patch_replace(ADDR_COMM_CREATE_DPLAY, (const void *)CommCreateDirectPlay,
                        "CommCreateDirectPlay", 1);
    rc |= patch_replace(ADDR_CREATE_LOBBY, (const void *)CreateDirectPlayLobby,
                        "CreateDirectPlayLobby", 1);
    rc |= patch_replace(ADDR_COMM_SHUTDOWN, (const void *)CommShutdown,
                        "CommShutdown", 0);
    rc |= patch_replace(ADDR_COMM_CLOSE, (const void *)CommClose, "CommClose", 0);
    rc |= patch_replace(ADDR_COMM_INIT_CONN, (const void *)CommInitializeConnection,
                        "CommInitializeConnection", 1);
    rc |= patch_replace(ADDR_COMM_SET_SESSION, (const void *)CommSetSessionDesc,
                        "CommSetSessionDesc", 2);
    rc |= patch_replace(ADDR_COMM_GET_SESSION, (const void *)CommGetSessionDesc,
                        "CommGetSessionDesc", 0);
    rc |= patch_replace(ADDR_COMM_REOPEN_SESSION, (const void *)CommReopenSession,
                        "CommReopenSession", 1);
    rc |= patch_replace(ADDR_DUMP_MSG_LIST, (const void *)DumpMsgList,
                        "DumpMsgList", 1);
    rc |= patch_replace(ADDR_MSG_LIST_COPY_BY_KEY,
                        (const void *)MsgListCopyByKey,
                        "MsgListCopyByKey", 1);
    rc |= patch_replace(ADDR_MSG_LIST_TAKE_FLAGS,
                        (const void *)MsgListTakeFlags,
                        "MsgListTakeFlags", 2);
    rc |= patch_replace(ADDR_PROCESS_RESEND_QUEUE,
                        (const void *)ProcessResendQueue,
                        "ProcessResendQueue", 1);
    rc |= patch_replace(ADDR_SEND_GAME_MSG, (const void *)SendGameMsg,
                        "SendGameMsg", 14);
    rc |= patch_replace(ADDR_DESTROY_FLOW, (const void *)DestroyFlow,
                        "DestroyFlow", 7);
    rc |= patch_replace(ADDR_SEND_PLAYER_MSG, (const void *)SendPlayerMsg,
                        "SendPlayerMsg", 15);
    rc |= patch_replace(ADDR_MSG_LIST_INSERT, (const void *)MsgListInsert,
                        "MsgListInsert", 1);
    rc |= patch_replace(ADDR_COMM_PLAYER_LEFT, (const void *)CommPlayerLeft,
                        "CommPlayerLeft", 2);
    rc |= patch_replace(ADDR_START_PACKET_THREAD, (const void *)StartPacketThread,
                        "StartPacketThread", 0);
    rc |= patch_replace(ADDR_PACKET_SLOT_RESET, (const void *)PacketSlotReset,
                        "PacketSlotReset", 1);
    rc |= patch_replace(ADDR_MSG_LIST_INIT, (const void *)MsgListInit,
                        "MsgListInit", 1);
    rc |= patch_replace(ADDR_EVENT_CLOSE, (const void *)EventClose,
                        "EventClose", 1);
    rc |= patch_replace(ADDR_COMM_ENUM_PLAYERS, (const void *)CommEnumPlayers,
                        "CommEnumPlayers", 0);
    rc |= patch_replace(ADDR_COMM_GLOBAL_INIT, (const void *)CommGlobalInit,
                        "CommGlobalInit", 1);
    rc |= patch_replace(ADDR_COMM_GLOBAL_CTOR,
                        (const void *)CommGlobalCtorThunk,
                        "CommGlobalCtorThunk", 1);
    rc |= patch_replace(ADDR_COMM_GLOBAL_ATEXIT, (const void *)CommGlobalAtExit,
                        "CommGlobalAtExit", 1);
    rc |= patch_replace(ADDR_COMM_GLOBAL_DTOR,
                        (const void *)CommGlobalDtorThunk,
                        "CommGlobalDtorThunk", 1);
    rc |= patch_replace(ADDR_COMM_CONSTRUCT, (const void *)CommConstruct,
                        "CommConstruct", 0);
    rc |= patch_replace(ADDR_COMM_DESTRUCT, (const void *)CommDestruct,
                        "CommDestruct", 0);
    rc |= patch_replace(ADDR_COMM_ENUM_SESSIONS, (const void *)CommEnumSessions,
                        "CommEnumSessions", 1);
    rc |= patch_replace(ADDR_COMM_ENUM_CONNECTIONS, (const void *)CommEnumConnections,
                        "CommEnumConnections", 1);
    rc |= patch_replace(ADDR_SEND_GAME_START, (const void *)SendGameStartMsg,
                        "SendGameStartMsg", 3);
    rc |= patch_replace(ADDR_COMM_SEND, (const void *)CommSend, "CommSend", 4);
    rc |= patch_replace(ADDR_COMM_REMOVE_PLAYER,
                        (const void *)CommRemovePlayer,
                        "CommRemovePlayer", 3);
    rc |= patch_replace(ADDR_COMM_REGISTER_SELF,
                        (const void *)CommRegisterSelf,
                        "CommRegisterSelf", 5);
    rc |= patch_replace(ADDR_COMM_OPEN_SESSION, (const void *)CommOpenSession,
                        "CommOpenSession", 1);
    rc |= patch_replace(ADDR_COMM_CONNECTED, (const void *)CommOnConnected,
                        "CommOnConnected", 0);
    rc |= patch_replace(ADDR_COMM_DROP_DPLAY, (const void *)CommDropDirectPlay,
                        "CommDropDirectPlay", 0);
    rc |= patch_replace(ADDR_COMM_LOBBY_START, (const void *)CommLobbyStart,
                        "CommLobbyStart", 0);
    rc |= patch_replace(ADDR_COMM_JOIN_SESSION, (const void *)CommJoinSession,
                        "CommJoinSession", 1);
    rc |= patch_replace(ADDR_COMM_RECEIVE, (const void *)CommReceive,
                        "CommReceive", 5);
    rc |= patch_replace(ADDR_COMM_CREATE_PLAYER, (const void *)CommCreatePlayer,
                        "CommCreatePlayer", 4);
    rc |= patch_replace(ADDR_COMM_SLOT_OF_ID, (const void *)CommSlotOfId,
                        "CommSlotOfId", 2);
    rc |= patch_replace(ADDR_COMM_PLAYER_SLOT, (const void *)CommPlayerSlot,
                        "CommPlayerSlot", 2);
    rc |= patch_replace(ADDR_COMM_DROP_SESSION, (const void *)CommDropSession,
                        "CommDropSession", 1);
    rc |= patch_replace(ADDR_COMM_MARK_LOBBIED, (const void *)CommMarkLobbied,
                        "CommMarkLobbied", 0);
    rc |= patch_replace(ADDR_COMM_SESSION_OVER, (const void *)CommSessionOver,
                        "CommSessionOver", 1);
    rc |= patch_replace(ADDR_COMM_RESET_STATE, (const void *)CommResetStats,
                        "CommResetStats", 3);
    rc |= patch_replace(ADDR_COMM_REPORT_STATS, (const void *)CommReportStats,
                        "CommReportStats", 3);
    rc |= patch_replace(ADDR_COMM_NO_BUFFERS, (const void *)CommNoBuffers,
                        "CommNoBuffers", 2);
    rc |= patch_replace(ADDR_COMM_PUBLISH_RESULT, (const void *)CommPublishResult,
                        "CommPublishResult", 1);
    rc |= patch_replace(ADDR_COMM_SEND_PROPERTY, (const void *)CommSendLobbyProperty,
                        "CommSendLobbyProperty", 1);
    rc |= patch_replace(ADDR_COMM_RECENT_TOTAL, (const void *)CommRecentTotal,
                        "CommRecentTotal", 1);
    rc |= patch_replace(ADDR_ON_LOBBY_SLAVE, (const void *)OnLobbySlave,
                        "OnLobbySlave", 2);
    return rc;
}
