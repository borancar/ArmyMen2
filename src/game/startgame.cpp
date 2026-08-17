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
 * docs/copyprotection.md for the byte. Building the check in would make this
 * function refuse where the original proceeds and would fail the A/B for a
 * reason having nothing to do with whether the reconstruction is right.
 */

#include "startgame.h"
#include "cdcheck.h"
#include "dplay.h"
#include "../inject/patch.h"

#include <stdint.h>

#define g_paintObj      (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_commObject    (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)
#define g_menuRequest   (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST)
#define g_menuRequestSet (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)

typedef void (__cdecl *am2_play_sound_fn)(int32_t, int32_t, int32_t, int32_t,
                                          int32_t);
typedef void (__cdecl *am2_void_fn)(void);
typedef int32_t (__cdecl *am2_sprintf_fn)(char *, const char *, ...);
/* Tears down an existing DirectPlay object. Takes the comm object in ecx and
 * nothing else; stays original, as in dplay.cpp. */
typedef void (__attribute__((thiscall)) *am2_comm_method_fn)(void *comm);
#define orig_drop_directplay (*(am2_comm_method_fn)ADDR_COMM_DROP_DPLAY)

#define orig_play_sound          (*(am2_play_sound_fn)ADDR_PLAY_SOUND)
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
#define SLOT_OFF_NAME     0x0C   /* the 0x40-byte buffer CommConstruct clears */
#define SLOT_OFF_ACTIVE   0x50

#define COMM_OFF_HAS_DPLAY 0x3EC
#define COMM_OFF_LOCAL     0x400
#define COMM_OFF_READY     0x3D8

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

    orig_play_sound(2, 0, 0, 0, 0);

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
    if (*(void **)(comm + COMM_OFF_HAS_DPLAY))
        orig_drop_directplay(comm);

    /* Slots 1..3 become the computer opponents. Slot 0 is left alone. */
    for (i = 1; i < 4; i++) {
        uint8_t *slot = comm + COMM_SLOT_BASE + i * COMM_SLOT_STRIDE;

        *(int32_t *)(slot + SLOT_OFF_ACTIVE) = 1;
        orig_sprintf((char *)(slot + SLOT_OFF_NAME),
                     (const char *)(uintptr_t)ADDR_FMT_COMPUTER_N, i);
    }

    g_menuRequest    = REQUEST_LOCAL;
    g_menuRequestSet = 1;
    orig_apply_game_settings();
    *(int32_t *)(comm + COMM_OFF_READY) = 1;
}

int startgame_install(void)
{
    return patch_replace(ADDR_START_SELECTED_GAME, (const void *)StartSelectedGame,
                         "StartSelectedGame", 0);
}
