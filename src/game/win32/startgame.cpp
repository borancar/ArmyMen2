/* The menu's "start the selected game" button -- reconstructed from
 * ArmyMen2.exe.
 *
 *   StartSelectedGame  0x0042ECF0
 *
 * Not called directly by anything. It is registered as a button's click handler
 * -- `push 0x42ecf0` alongside the button's rect and label, at 0x0042EBCA -- so
 * the only reference to it in the image is an instruction operand. That is
 * worth knowing before concluding it is unused; a cross-reference scan looking
 * for calls and aligned pointers finds nothing and is wrong. See CLAUDE.md.
 *
 * It reads the highlighted row out of the session list, plays the click sound,
 * and then splits: a row carrying a DirectPlay connection is a session to join,
 * and a row without one is a local game, which is set up here by filling three
 * of the comm object's four player slots with computer opponents.
 *
 * THE CD CHECK ON THE LOCAL PATH IS DISABLED IN THIS EXECUTABLE, and that is
 * reproduced rather than repaired. `FindGameCD` is still called and its answer
 * is still discarded, because only the branch was patched and not the call --
 * see src/game/cdcheck.h, which carries the retail check behind an #ifdef, and
 * docs/binarypatches.md for the byte. Building the check in would make this
 * function refuse where the original proceeds and would fail the A/B for a
 * reason having nothing to do with whether the reconstruction is right.
 */

#include "audio.h"
#include "startgame.h"
#include "cdcheck.h"
#include "dplay.h"
#include "../gamedir.h"
#include "../../inject/patch.h"

#include <stdint.h>

#define g_paintObj      (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_commObject    (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_menuRequest   (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST)
#define g_menuRequestSet (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)

typedef void (__cdecl *am2_play_sound_fn)(int32_t, int32_t, int32_t, int32_t,
                                          int32_t);
typedef void (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *, const char *, ...);

#define orig_apply_game_settings (*(am2_void_fn)ADDR_APPLY_GAME_SETTINGS)
#define orig_sprintf             (*(am2_sprintf_fn)ADDR_GAME_SPRINTF)

/* Reaching the highlighted row: the paint object holds the current screen at
 * +0x64, and the screen holds the list and the index into it. */
#define SCREEN_OFF        0x64
#define SCREEN_OFF_INDEX  0x5C
#define SCREEN_OFF_LIST   0x60

/* The list is { int32 count; Row *rows; }, and a row is 0x104 bytes with the
 * DirectPlay connection last. 0x104 is how the original computes it -- index
 * times 65, then scaled by four. */
#define ROW_STRIDE        0x104
#define ROW_OFF_CONNECTION 0x100

/* The comm object's four player slots, as laid out by CommConstruct. Slot 0 is
 * the human; 1..3 are filled in here. */
#define COMM_SLOT_BASE    0x20C
#define COMM_SLOT_STRIDE  0x70
#define SLOT_OFF_ACTIVE   0x50

/* COMM_OFF_LOCAL, COMM_OFF_READY and COMM_OFF_DPLAY are in orig.h. */

/* Menu request codes, as written to ADDR_MENU_REQUEST. */
#define REQUEST_REFUSED   1
#define REQUEST_JOINED    0x0A
#define REQUEST_LOCAL     0x0B

void __cdecl StartSelectedGame(void)
{
    uint8_t *screen = *(uint8_t **)(g_paintObj + SCREEN_OFF);
    int32_t *list   = *(int32_t **)(screen + SCREEN_OFF_LIST);
    int32_t  index  = *(int32_t *)(screen + SCREEN_OFF_INDEX);
    uint8_t *rows;
    void    *connection;
    uint8_t *comm;
    int32_t  i;

    /* No list, or nothing sensibly selected, and the button does nothing. */
    if (!list)
        return;
    if (index < 0 || index >= list[0])
        return;

    PlaySoundAt(2, 0, 0, 0, 0);

    /* { int32 count; Row *rows; } -- the pointer is the second dword. */
    rows = *(uint8_t **)(list + 1);
    connection = *(void **)(rows + index * ROW_STRIDE + ROW_OFF_CONNECTION);

    if (connection) {
        /* An existing session: point DirectPlay at it and let the menu know. */
        comm = g_commObject;
        if (CommInitializeConnection(comm, connection)) {
            *(int32_t *)(comm + COMM_OFF_LOCAL) = 0;
            g_menuRequest    = REQUEST_JOINED;
            g_menuRequestSet = 1;
        }
        return;
    }

    /* A local game. The CD check here is the disabled one -- the call still
     * happens, the answer is still thrown away. */
    if (!RequireGameCD()) {
        /* Unreachable as built; see the file comment and cdcheck.h. */
        g_menuRequest    = REQUEST_REFUSED;
        g_menuRequestSet = 1;
        return;
    }

    comm = g_commObject;
    *(int32_t *)(comm + COMM_OFF_LOCAL) = 1;

    /* A DirectPlay object left over from browsing sessions is dropped: a local
     * game has no use for it and it would otherwise stay connected. */
    if (*(void **)(comm + COMM_OFF_DPLAY))
        CommDropDirectPlay(comm);

    /* Slots 1..3 become the computer opponents. Slot 0 is left alone. */
    for (i = 1; i < 4; i++) {
        uint8_t *slot = comm + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;

        *(int32_t *)(slot + SLOT_OFF_ACTIVE) = 1;
        orig_sprintf((char *)(slot + COMM_SLOT_OFF_NAME),
                     (const char *)(uintptr_t)ADDR_FMT_COMPUTER_N, i);
    }

    g_menuRequest    = REQUEST_LOCAL;
    g_menuRequestSet = 1;
    orig_apply_game_settings();
    *(int32_t *)(comm + COMM_OFF_READY) = 1;
}

/* The multiplayer HOST button, 0x0042F310, registered the same way.
 *
 * It is the last function in the image holding a Win32 call that can actually
 * execute and was not ours: the "Data Missing" dialog, and the ShowCursor pair
 * around the session-open. Everything else still outside reconstructed code is
 * either incidental -- a GetTickCount, an IntersectRect -- or sits behind a
 * copy-protection check that has been patched to skip it.
 *
 * The interesting part is the three-step dance in the middle, which is what a
 * fullscreen DirectDraw application has to do before it can show a dialog:
 * FlipToGDISurface so GDI has the screen, ShowCursor(TRUE) so the pointer
 * exists again, and ShowCursor(FALSE) afterwards. Slot 10 on IDirectDraw is
 * FlipToGDISurface, checked against the SDK header rather than counted by eye.
 *
 * DELIBERATE DEVIATION -- the original carries an MSVC structured-exception
 * frame (`push -1; push handler; mov fs:[0], esp`) around the whole body, with
 * the usual state variable written at two points. That is C++ unwind
 * bookkeeping for the object allocated below, it cannot be reproduced in
 * MinGW's model, and it is inert here: the allocation goes through the
 * non-throwing operator new and the original checks the result for NULL rather
 * than relying on a throw. So the frame is omitted, and nothing in this path
 * can raise anything for it to have caught.
 */

typedef void *(__cdecl *am2_operator_new_fn)(size_t);
typedef void (__attribute__((thiscall)) *am2_session_ctor_fn)(void *, int32_t);
typedef int32_t (__attribute__((thiscall)) *am2_enum_sessions_fn)(void *, void *);

#define orig_operator_new  (*(am2_operator_new_fn)ADDR_GAME_OPERATOR_NEW)
#define orig_session_ctor  (*(am2_session_ctor_fn)ADDR_SESSION_CTOR)
#define orig_drop_obj      (*(am2_void_fn)ADDR_DROP_OBJ_51612C)

#define g_ddraw          (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)
#define g_sessionObject  (*(void **)(uintptr_t)ADDR_SESSION_OBJECT)

#define REQUEST_MULTIPLAYER 0x0C
#define SESSION_OBJECT_SIZE 0x0C

void __cdecl StartMultiplayerGame(void)
{
    uint8_t *comm;

    PlaySoundAt(2, 0, 0, 0, 0);

    /* A compact install leaves the multiplayer maps on the CD. This one is a
     * real check with a real conditional -- unlike the five copy-protection
     * checks, it was not patched, so this dialog can appear. */
    if (!SetGameDir((const char *)(uintptr_t)ADDR_MP_DATA_PROBE)) {
        MessageBoxA(GetActiveWindow(),
                    (const char *)(uintptr_t)ADDR_DATA_MISSING_TEXT,
                    (const char *)(uintptr_t)ADDR_DATA_MISSING_CAPTION,
                    MB_ICONHAND);
        g_menuRequest    = REQUEST_REFUSED;
        g_menuRequestSet = 1;
        return;
    }

    g_menuRequest    = REQUEST_MULTIPLAYER;
    g_menuRequestSet = 1;
    orig_apply_game_settings();
    orig_drop_obj();

    comm = g_commObject;
    *(int32_t *)(comm + COMM_OFF_READY) = 0;

    /* Made once and kept. A failed allocation leaves it null, which the open
     * below then refuses -- the original checks rather than assuming. */
    if (!g_sessionObject) {
        void *obj = orig_operator_new(SESSION_OBJECT_SIZE);
        if (obj)
            orig_session_ctor(obj, 1);
        g_sessionObject = obj;
    }

    /* Hand the screen back to GDI and put the pointer up, or the dialog the
     * session browser shows would be invisible and unclickable. */
    IDirectDraw_FlipToGDISurface(g_ddraw);
    ShowCursor(TRUE);

    if (!CommEnumSessions(comm, g_sessionObject)) {
        g_menuRequest    = REQUEST_REFUSED;
        g_menuRequestSet = 1;
    }

    ShowCursor(FALSE);
}

/* Host a new battle -- 0x0042FFF0.
 *
 * The OK on ENTER BATTLE NAME, and the last named DirectDraw call in the menu
 * layer. The dialog carries two strings inside the paint object: the battle's
 * name and the hosting player's. Both must be non-empty; either blank refuses
 * with the error sound and nothing else happens.
 *
 * WHY A GRAPHICS CALL IS HERE AT ALL. FlipToGDISurface hands the display back
 * to GDI before the two comm calls, which open a DirectPlay session and create
 * a player and can block for as long as the network takes. Without it a
 * fullscreen exclusive-mode game would sit on an undrawable primary while that
 * happened. It is the one line in this function that is not menu logic, and it
 * is why the function is reconstructed rather than declined.
 *
 * The failure paths share their tail with the success path in the original: the
 * three zero arguments PlaySoundAt needs are pushed BEFORE the branch that
 * decides which sound to play, and the failing branch jumps into the middle of
 * the sequence to push the remaining two. Written out as two calls here; the
 * arguments are identical either way and only the index differs.
 *
 * Both comm calls are already ours, so this is glue over reconstructed code
 * plus one DirectDraw call.
 *
 * NOT EXERCISED without AM2_MULTIPLAYER=1 -- the whole path is behind the
 * patched-out button. Verified by reading. */
#define REQUEST_HOSTED 7

#define g_paintObject (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_ddraw       (*(LPDIRECTDRAW *)(uintptr_t)ADDR_DIRECTDRAW)

/* strcpy, which is what the original inlines as scasb-then-movsd. */
static void CopyName(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

void __cdecl HostBattle(void)
{
    uint8_t    *dlg    = g_paintObject;
    const char *battle = (const char *)(dlg + DLG_OFF_BATTLE_NAME);
    const char *player = (const char *)(dlg + DLG_OFF_PLAYER_NAME);

    if (!*battle || !*player) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }

    /* Give the screen back to GDI before anything that can block. */
    IDirectDraw_FlipToGDISurface(g_ddraw);

    if (!CommOpenSession(g_commObject, battle)) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }
    if (!CommCreatePlayer(g_commObject, player, NULL, NULL, 0)) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }

    /* The host is always slot 0. */
    CopyName((char *)(g_commObject + COMM_SLOT_BASE + COMM_SLOT_OFF_NAME),
             player);

    PlaySoundAt(2, 0, 0, 0, 0);
    g_menuRequest    = REQUEST_HOSTED;
    g_menuRequestSet = 1;

    CopyName((char *)(uintptr_t)ADDR_SAVED_PLAYER_NAME, player);
    CopyName((char *)(uintptr_t)ADDR_SAVED_BATTLE_NAME, battle);
}

int startgame_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_HOST_BATTLE, (const void *)HostBattle,
                        "HostBattle", 0);

    rc |= patch_replace(ADDR_START_SELECTED_GAME, (const void *)StartSelectedGame,
                        "StartSelectedGame", 0);
    rc |= patch_replace(ADDR_START_MULTIPLAYER, (const void *)StartMultiplayerGame,
                        "StartMultiplayerGame", 0);
    return rc;
}
