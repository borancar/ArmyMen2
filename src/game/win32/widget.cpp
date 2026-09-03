/* The menu widget hierarchy -- see widget.h for the shape of the class tree.
 *
 * This module is the first piece of it. What is here is one painter; the
 * thirty-four constructors, the four other virtuals per class and the
 * containers that own them are all still original and reached by address. */

#include "widget.h"
#include "surface.h"
#include "font.h"    /* TextExtent -- reconstructed */
#include "../rect.h"
#include "../commmsg.h" /* Announce -- reconstructed */
#include "../misc.h"   /* IsKeyDown, KeyChanged, TitleCaseName */
#include "../text.h"   /* DrawText -- reconstructed */
#include "../msgslot.h" /* SendColorMsg, SendTeamMsg -- reconstructed */
#include "sprite.h"
#include "frame.h"
#include "audio.h"
#include "dplay.h"   /* CommCreateDirectPlay -- reconstructed */
#include "../gamedir.h" /* SetGameDir -- reconstructed */
#include "../place.h"  /* CanAffordUnit -- reconstructed */
#include "../map.h"     /* Checksum and the three totals -- reconstructed */
#include "../gameproc.h"  /* RequestState -- reconstructed */
#include "../image.h"
#include "../crt.h"
#include "../../inject/patch.h"
#include "startgame.h"
#include "../../inject/restore.h"
#include "mapdraw.h"   /* SetDrawTarget -- reconstructed */
#include "winmain.h"   /* Ticks -- reconstructed */
#include "../army.h"    /* LookupOwnerObj -- reconstructed */
#include "../air.h"     /* FormationSlotPoint -- PointerActionMode6 */
#include "../objtype.h" /* LookupType3ByUID -- reconstructed */
#include "../item.h"    /* WeaponByUid -- reconstructed */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* The seven title-screen handlers live in startgame.cpp, beside the other
 * menu handler that starts a game. They are declared void(void) because that
 * is what their bodies are; a button handler is called with the widget, and
 * cdecl means the caller cleans, so the extra argument the original's own
 * handlers also ignore is harmless. The cast is here rather than in their
 * declarations so the signature stays the one the disassembly shows. */
#define AM2_BUTTON_HANDLER(fn) ((void (__cdecl *)(AM2_Widget *))(fn))
/* A handler that is still the ORIGINAL's, as a pointer. The address form is
 * the general case -- the reconstruction owns a few dozen of these and the
 * image owns the rest -- so the two spellings sit side by side on purpose. */
#define kImageHandler(a)       AM2_BUTTON_HANDLER(AM2_IMAGE(a))
#define kOnBootCamp     AM2_BUTTON_HANDLER(OnBootCamp)
#define kOnSinglePlayer AM2_BUTTON_HANDLER(OnSinglePlayer)
#define kOnMultiPlayer  AM2_BUTTON_HANDLER(OnMultiPlayer)
#define kOnOptionsMenu  AM2_BUTTON_HANDLER(OnOptionsMenu)
#define kOnMovies       AM2_BUTTON_HANDLER(OnMovies)
#define kOnCredits      AM2_BUTTON_HANDLER(OnCredits)
#define kOnQuit         AM2_BUTTON_HANDLER(OnQuit)
/* JOIN A WAR on the war menu. startgame.cpp declares it void(void), which is
 * what its body is. */
#define kOnStartMultiplayer AM2_BUTTON_HANDLER(StartMultiplayerGame)
/* These already take the widget, so the cast only spells out the type the
 * table field wants. */
#define kOnMenuBack       OnMenuBack
#define kOnAudioOk        OnAudioOk
#define kOnAudioCancel    OnAudioCancel
#define kOnControlsCancel OnControlsCancel
#define kOnControlsOk      OnControlsOk
#define kOnControlsDefault OnControlsDefault
#define kOnArrowUp          OnArrowUp
#define kOnArrowDown        OnArrowDown
#define kOnArrowLeft        OnArrowLeft
#define kOnArrowRight       OnArrowRight
#define kOnRecruit          OnRecruit
#define kOnDeletePlayer     OnDeletePlayer
#define kOnSelectPlayer     OnSelectPlayer
#define kOnDelPlayerCancel  OnDelPlayerCancel
#define kOnReplayOk         OnReplayOk
#define kOnEnterNameCancel  OnEnterNameCancel
#define kOnLoadGameBack     OnLoadGameBack
#define kOnLoadGameDelete   OnLoadGameDelete
#define kOnDelGameCancel    OnDelGameCancel
#define kOnLoadGameNew      OnLoadGameNew
#define kOnMovieNextPage    OnMovieNextPage
#define kOnMoviePlay        OnMoviePlay
#define kOnLoadGameLoad     OnLoadGameLoad
#define kOnAudioButton      OnAudioButton
#define kOnControlsButton   OnControlsButton
#define kOnDifficultyButton OnDifficultyButton
#define kOnQuitOk           OnQuitOk
#define kOptionsSyncGroup   OptionsSyncGroup
#define kOptionsApply       OptionsApply
#define kOptionsDefaults    OptionsDefaults
/* These two take no widget, like the title screen's seven. */
#define kOptionsRequest     AM2_BUTTON_HANDLER(OptionsRequest)
#define kHostBattle         AM2_BUTTON_HANDLER(HostBattle)
#define kStartSelectedGame  AM2_BUTTON_HANDLER(StartSelectedGame)
#define kOnDifficultyOk   OnDifficultyOk

/* The layout claims above are compiler-checked rather than commented. */
static_assert(offsetof(AM2_Widget, rect)   == 0x14, "widget rect offset");
static_assert(offsetof(AM2_Widget, parent) == 0x28, "widget parent offset");

/* The surface subsequent drawing targets -- the same global mapdraw.cpp calls
 * g_drawTarget, under the same name, because it is the same global. */
#define g_drawTarget (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET)

/* 0x00446AB0, cdecl, 11 direct callers: the clipped sibling of DrawText at
 * 0x00446930. Measures the string with `repne scasb`, then walks it a
 * character at a time; `^` followed by any character is an escape that
 * overwrites its own colour ARGUMENT in place, so a caret sequence recolours
 * the rest of the line. The clip rectangle is passed by value. Still
 * original. */

void __attribute__((thiscall)) WidgetDestruct(AM2_Widget *w)
{
    AM2_Widget *child;

    w->vtable = (void *)AM2_IMAGE(VTABLE_WIDGET_BASE);

    child = w->firstChild;
    while (child) {
        /* Read the sibling BEFORE destroying the child, which is what makes
         * freeing from inside the walk safe. The original also tests the
         * child for null a second time here, on a value it has just branched
         * on and re-enters the loop with; that test can never fire and is not
         * reproduced. */
        AM2_Widget *next = child->nextSibling;

        ((AM2_WidgetDeleteFn *)child->vtable)[WIDGET_VSLOT_DTOR](child, 1);
        child = next;
    }
}

AM2_Widget *__attribute__((thiscall)) WidgetDelete(AM2_Widget *w,
                                                   int32_t flags)
{
    WidgetDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) LabelDestruct(AM2_Widget *w)
{
    w->vtable = (void *)AM2_IMAGE(VTABLE_LABEL);
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) LabelDelete(AM2_Widget *w, int32_t flags)
{
    LabelDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* The input trio and the base update, all still original. The queries are two
 * globals and a mask each and belong to the input layer rather than here. */
typedef int32_t (__cdecl *AM2_KeyQueryFn)(int32_t dik);
typedef void (__cdecl *AM2_ConsumeKeyFn)(int32_t dik);
typedef void (__attribute__((thiscall)) *AM2_WidgetUpdateFn)(AM2_Widget *w);

/* DirectInput scancodes, which is what every query here is indexed by. */
#define AM2_DIK_TAB    0x0F
#define AM2_DIK_UP     0xC8
#define AM2_DIK_DOWN   0xD0

/* The 640x480 screen rectangle, spelled exactly as sprite.cpp and text.cpp
 * spell it so it stays one definition rather than a drift. */
#define g_screenClip  (*(const AM2_Rect *)(uintptr_t)ADDR_SCREEN_CLIP)
#define orig_mouse_moved (*(const int32_t *)(uintptr_t)ADDR_MOUSE_MOVED)
/* Spelled as device.cpp spells them, so they stay one definition. */
#define g_backgroundColour (*(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR)
#define g_hiliteColour     (*(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR)
#define g_mouseButton  ((int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
#define g_mouseChanged ((int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)

void __attribute__((thiscall)) WidgetPaint(AM2_Widget *w, RECT clip)
{
    AM2_Widget *child;

    if (w->sprite) {
        AM2_Sprite *spr = w->sprite;
        int32_t     x;
        int32_t     y;
        RECT        visible;
        AM2_Rect    part;

        WidgetScreenRect(w);

        if (w->flag3C) {
            /* Each side is halved BEFORE the subtraction, which is not the
             * same as halving the difference when exactly one is odd. */
            x = w->rect.left + ((w->w >> 1) - (spr->bounds.right >> 1));
            y = w->rect.top  + ((w->h >> 1) - (spr->bounds.bottom >> 1));
        } else {
            x = w->rect.left;
            y = w->rect.top;
        }

        /* RECT and AM2_Rect are the same four int32_t; the casts are the price
         * of the flat half not being allowed to name RECT. */
        if (IntersectRect(&visible, &w->rect, &clip)
            && IntersectRect(&visible, (const RECT *)&g_screenClip, &visible)
            && ClipRect(&spr->bounds, (const AM2_Rect *)&visible, &x, &y,
                        &part))
            DrawSpriteClipped(spr, x, y, &part, 0);
    }

    /* Children are painted whether or not the sprite was, and against the
     * CALLER's clip rather than the intersection worked out above. */
    for (child = w->firstChild; child; child = child->nextSibling)
        ((AM2_WidgetPaintFn *)child->vtable)[WIDGET_VSLOT_PAINT](child, clip);
}

/* Which edit box has the focus, and the WM_CHAR consumer WndProc dispatches
 * through. Both are ours now: EditCharHandler is below. */
#define g_focusedEdit (*(AM2_Widget **)(uintptr_t)ADDR_FOCUSED_EDIT)
/* The game's own logger, spelled as winproc.cpp spells it. */
typedef void (__cdecl *am2_log_fn)(const char *fmt, ...);
#define orig_log      (*(am2_log_fn)ADDR_LOG)
/* Spelled exactly as winproc.cpp spells it, type and all, so the two stay one
 * definition rather than a drift -- checkglobals refused the first attempt,
 * which had it as a bare void *. */
typedef void (__cdecl *am2_char_fn)(uint32_t ch, uint32_t lo, uint32_t hi);
#define g_charHandler (*(am2_char_fn *)(uintptr_t)ADDR_CHAR_HANDLER)
/* The game's own rand, and it has to be: the LCG state at 0x0048CC1C is the
 * image's, so drawing from ours would leave it untouched. */
typedef int32_t (__cdecl *am2_rand_fn)(void);
#define orig_rand         (*(am2_rand_fn)ADDR_GAME_RAND)
/* The chat send, still the original's: it reaches the comm layer and the name
 * table below it, neither of which is ours. */
typedef void (__attribute__((thiscall)) *am2_chat_send_fn)(AM2_Widget *);
#define orig_hud_chat_send (*(am2_chat_send_fn)ADDR_HUD_CHAT_SEND)
/* The squad panel's per-slot detail painter, 3,328 bytes and still the
 * original's -- nothing about it is needed to know what HudSquadPaint does. */
typedef void (__attribute__((thiscall)) *am2_squad_detail_fn)(AM2_Widget *, int32_t);
#define orig_hud_squad_detail (*(am2_squad_detail_fn)ADDR_HUD_SQUAD_DETAIL)
/* Selecting a weapon reaches the unit and comm layers and stays the
 * original's. ItemIsReady and ItemTypeName were seams here until they were
 * reconstructed a commit later; checkseams caught both the moment they were,
 * which is the whole reason that ratchet exists. */
typedef void (__cdecl *am2_select_weapon_fn)(void *, int32_t);

/* The six pointer/weapon slots hold two shapes, and the difference is what the
 * original does with the result: a PICK is `call eax; add esp,4; test eax,eax`
 * and an ACTION is `call eax; add esp,8` with the answer dropped. Both match
 * the reconstructions that go into them -- PointerPickMode0.. and
 * PointerActionMode4.. -- so these agree with widget.h rather than restating
 * it loosely. */
typedef int32_t (__cdecl *AM2_PointerPickFn)(void *obj);
typedef void (__cdecl *AM2_PointerActionFn)(void *obj, uint32_t at);
#define orig_select_weapon (*(am2_select_weapon_fn)ADDR_SELECT_WEAPON)

/* Clear the focus record and the installed handler, but only if this widget is
 * the one that owns them. Both callers need the test: a field can be repainted
 * or destroyed while a DIFFERENT field holds the focus, and wiping a newer
 * field's handler would leave typing dead with nothing to say why. */
static void EditReleaseFocus(AM2_Widget *w)
{
    if (g_focusedEdit == w) {
        g_focusedEdit = (AM2_Widget *)0;
        g_charHandler = (am2_char_fn)0;
    }
}

/* GetTickCount through the game's own IAT slot, so this module names no Win32
 * type for it. */
typedef uint32_t (__attribute__((stdcall)) *AM2_TickFn)(void);
#define orig_get_tick_count (**(AM2_TickFn *)(uintptr_t)IAT_GET_TICK_COUNT)

/* 250 ms before a held button starts repeating, 150 ms between repeats. */
#define BUTTON_REPEAT_DELAY  250u
#define BUTTON_REPEAT_PERIOD 150u

typedef void (__cdecl *AM2_ButtonFn)(AM2_Widget *w);

static void ButtonFire(AM2_Widget *w, int32_t off)
{
    AM2_ButtonFn fn = *(AM2_ButtonFn *)((uint8_t *)w + off);

    if (fn)
        fn(w);
}

/* Repaint self through slot 1 with its own rectangle -- the tail every arm of
 * ButtonUpdate shares. */
static void ButtonRepaintSelf(AM2_Widget *w)
{
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

#define BUTTON_DEADLINE(w) (*(uint32_t *)((uint8_t *)(w) + BUTTON_OFF_DEADLINE))

int32_t __cdecl FindPressedKey(void)
{
    const AM2_KeyName *e   = (const AM2_KeyName *)AM2_IMAGE(ADDR_KEY_NAME_TABLE);
    const AM2_KeyName *end = (const AM2_KeyName *)AM2_IMAGE(ADDR_KEY_NAME_TABLE_END);
    int32_t            idx = 0;

    for (; e < end; e++, idx++) {
        /* Changed AND now down: the pressing edge. Only the low byte of the
         * record is the scancode as far as the queries are concerned -- they
         * mask to 8 bits themselves -- and the original loads it as a byte. */
        int32_t dik = (int32_t)(uint8_t)e->dik;

        if (KeyChanged(dik) && IsKeyDown(dik))
            return idx;
    }
    return -1;
}

void __attribute__((thiscall)) KeyRowUpdate(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  key;

    WidgetScreenRect(w);

    if (w->parent && orig_mouse_moved && !w->disabled) {
        w->unknown40 = PointInRect((const AM2_Rect *)&w->rect,
                                   (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT);
        if (w->unknown40)
            ((AM2_WidgetFocusFn *)w->vtable)[WIDGET_VSLOT_FOCUS](w, 1);
    }

    /* Only the focused row captures. */
    if (!w->flag44)
        return;

    key = FindPressedKey();
    if (key < 0)
        return;

    {
        const AM2_KeyName *table =
            (const AM2_KeyName *)AM2_IMAGE(ADDR_KEY_NAME_TABLE);

        *(int32_t *)(self + KEYROW_OFF_KEY)      = key;
        *(const char **)(self + LABEL_OFF_TEXT)  = table[key].name;
    }

    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);

    /* A key can be bound in one place only: take it off every other row. */
    {
        AM2_Widget **rows =
            (AM2_Widget **)((uint8_t *)w->parent + KEYROW_PARENT_ROWS);
        const char  *none = (const char *)AM2_IMAGE(ADDR_STR_NONE);
        int32_t      i;

        for (i = 0; i < KEYROW_ROW_COUNT; i++) {
            AM2_Widget *other = rows[i];

            if (other == w)
                continue;
            if (*(const int32_t *)((uint8_t *)other + KEYROW_OFF_KEY) != key)
                continue;
            *(int32_t *)((uint8_t *)other + KEYROW_OFF_KEY)     = 0;
            *(const char **)((uint8_t *)other + LABEL_OFF_TEXT) = none;
        }
    }
}

void __attribute__((thiscall)) MultiSpritePaint(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    int32_t        idx  = *(const int32_t *)(self + MULTISPR_OFF_INDEX);
    AM2_Sprite    *spr;
    RECT           dest;
    RECT           vis;
    AM2_Rect       part;
    int32_t        x;
    int32_t        y;

    WidgetScreenRect(w);

    spr = *(AM2_Sprite *const *)(self + MULTISPR_OFF_SPRITES + idx * 4);
    if (!spr)
        return;

    /* Halved AFTER the subtraction, which is not how WidgetPaint rounds. */
    x = w->rect.left
        + ((w->rect.right - spr->bounds.right - w->rect.left) >> 1);
    y = w->rect.top
        + ((w->rect.bottom - *(const int32_t *)(self + MULTISPR_OFF_Y_INSET)
            - w->rect.top) >> 1)
        + *(const int32_t *)(self + MULTISPR_OFF_Y_BIAS);

    dest.left   = x;
    dest.top    = y;
    dest.right  = x + spr->bounds.right;
    dest.bottom = y + spr->bounds.bottom;

    if (!IntersectRect(&vis, &dest, &clip))
        return;
    if (!ClipRect(&spr->bounds, (const AM2_Rect *)&vis, &x, &y, &part))
        return;

    DrawSpriteClipped(spr, x, y, &part, 0);
}

void __attribute__((thiscall)) BlinkerUpdate(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    uint32_t last;
    uint32_t elapsed;

    if (!*(const int32_t *)(self + BLINK_OFF_ACTIVE))
        return;

    last    = *(const uint32_t *)(self + BLINK_OFF_LAST);
    elapsed = orig_get_tick_count() - last;

    /* Written before the period test, so it is readable on the frames that do
     * nothing -- a readout rather than a working value. */
    *(uint32_t *)(self + BLINK_OFF_ELAPSED) = elapsed;
    if (elapsed < *(const uint32_t *)(self + BLINK_OFF_PERIOD))
        return;

    *(uint32_t *)(self + BLINK_OFF_LAST) = elapsed + last;
    *(int32_t *)(self + TOGGLE_OFF_STATE) =
        (*(const int32_t *)(self + TOGGLE_OFF_STATE) == 0);
    *(uint32_t *)(self + BLINK_OFF_ELAPSED) = 0;

    if (--*(int32_t *)(self + BLINK_OFF_REMAINING) == 0) {
        /* A blink always ends in the OFF sprite, whatever the count was. */
        *(int32_t *)(self + BLINK_OFF_ACTIVE)  = 0;
        *(int32_t *)(self + TOGGLE_OFF_STATE)  = 0;
    }

    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

void __attribute__((thiscall)) BlinkerStart(AM2_Widget *w, uint32_t periodMs,
                                            int32_t flips)
{
    uint8_t *self = (uint8_t *)w;

    *(int32_t *)(self + TOGGLE_OFF_STATE)     = 1;
    *(int32_t *)(self + BLINK_OFF_ACTIVE)     = 1;
    *(uint32_t *)(self + BLINK_OFF_PERIOD)    = periodMs;
    *(uint32_t *)(self + BLINK_OFF_LAST)      = orig_get_tick_count();
    *(int32_t *)(self + BLINK_OFF_REMAINING)  = flips;
    *(uint32_t *)(self + BLINK_OFF_ELAPSED)   = 0;

    /* Repaint at once, so the first flash does not wait a whole period. */
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

void __attribute__((thiscall)) TogglePaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;

    if (*(const int32_t *)(self + TOGGLE_OFF_STATE))
        w->sprite = *(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_ON);
    else
        w->sprite = *(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_OFF);

    WidgetPaint(w, clip);
}

void __attribute__((thiscall)) ListDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_LIST);

    if (*(const int32_t *)(self + LIST_OFF_OWNS_ROWS)) {
        void *rows = *(void **)(self + LIST_OFF_ROWS);

        if (rows) {
            RecordResetAlias(rows);
            am2_free(rows);
        }
    }
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) ListDelete(AM2_Widget *w, int32_t flags)
{
    ListDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* TextListConstruct -- original 0x00433290, one caller, and the TEXT LIST:
 * the message log's list box. It is the LIST BOX with a different vtable and
 * five colours, and nothing else -- `operator new` asks for AM2_LISTBOX_SIZE
 * and every field it writes is inside that, so the class adds no storage.
 *
 * ITS ONE CALLER PASSES ADDR_LOG AS THE ROW CALLBACK. In this build that
 * address is a bare `ret` -- the retail logger, stubbed -- so the hook does
 * nothing at all. Worth knowing before reading the argument as a feature;
 * see CLAUDE.md on the vtable slot that is the same address for the same
 * reason.
 *
 * THE FIVE COLOURS ARE COPIED AS BYTES INTO DWORDS. TEXTLIST_OFF_COLOURS is
 * an int32 array whose low byte is what the painter uses, and the sources are
 * five separate palette slots SetGamePalette fills by RGB. They are named for
 * the colour they hold rather than for a role, because neither of their two
 * readers establishes one.
 *
 * It marks itself LISTBOX_OFF_READ_ONLY, which is what makes it a log rather
 * than a menu: all three readers of that field skip something when it is set
 * -- the selected-row colour, the highlight, and the whole keyboard arm of the
 * update -- so a text list cannot be selected in or typed at.
 */
AM2_Widget *__attribute__((thiscall)) TextListConstruct(AM2_Widget *w,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t right,
                                                        int32_t bottom,
                                                        void *rows,
                                                        int32_t callback,
                                                        int32_t ownsRows)
{
    uint8_t  *self = (uint8_t *)w;
    int32_t  *colours;

    ListBoxConstruct(w, left, top, right, bottom, rows, callback, 0, ownsRows);

    w->vtable = (void *)AM2_IMAGE(VTABLE_TEXT_LIST);

    colours = (int32_t *)(self + TEXTLIST_OFF_COLOURS);
    colours[0] = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_DARK_GREEN;
    colours[1] = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_OLIVE;
    colours[2] = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_STEEL_BLUE;
    colours[3] = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_DARK_GREY;
    colours[4] = *(const uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR;

    *(int32_t *)(self + LISTBOX_OFF_READ_ONLY) = 1;
    *(int32_t *)(self + LISTBOX_OFF_SELECTED)  = -1;
    return w;
}

/* TextListDelete -- original 0x00433330, the scalar deleting destructor, and
 * the same shape as ListDelete above.
 *
 * The class has no teardown of its own: the original reaches the base through
 * 0x00433350, which is a one-instruction `jmp` to it and is left in the image.
 * Nothing but this function refers to that thunk, and it is five bytes -- the
 * exact size of a detour -- so patching it would be all risk and no gain.
 */
AM2_Widget *__attribute__((thiscall)) TextListDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    ListDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) ListDraw(AM2_Widget *w, RECT clip)
{
    uint8_t            *self = (uint8_t *)w;
    const AM2_ListRows *rows =
        *(const AM2_ListRows *const *)(self + LIST_OFF_ROWS);
    RECT     paint;
    RECT     rowRect;
    int32_t  idx;
    int32_t  offset;

    if (!rows)
        return;

    WidgetScreenRect(w);
    if (!IntersectRect(&paint, &w->rect, &clip))
        return;

    /* The row strip spans the widget's full width; only top and bottom move. */
    rowRect.left  = w->rect.left;
    rowRect.right = w->rect.right;

    ClearRegion(&paint, g_backgroundColour);

    idx = *(const int32_t *)(self + LIST_OFF_TOP_ROW);
    if (idx >= rows->count)
        return;

    offset = idx * LIST_ROW_STRIDE;
    do {
        int32_t top = *(const int32_t *)(self + LIST_OFF_TOP_ROW);
        int32_t sel = *(const int32_t *)(self + LIST_OFF_SELECTED);
        int32_t y;
        uint8_t ink;
        int32_t selectedHere;

        if (idx >= top + *(const int32_t *)(self + LIST_OFF_VISIBLE))
            return;

        selectedHere = (idx == sel)
                       && *(const int32_t *)(self + 0x44) != 0
                       && *(const int32_t *)(self + 0x4C) == 0;

        ink = *(const uint8_t *)(self + LIST_OFF_INK);
        if (idx == sel && w->flag44)
            ink = g_mouseButton[0]
                  ? *(const uint8_t *)(self + LIST_OFF_INK_SEL_DOWN)
                  : *(const uint8_t *)(self + LIST_OFF_INK_SEL);
        if (idx == *(const int32_t *)(self + LIST_OFF_HOT))
            ink = selectedHere ? *(const uint8_t *)(self + LIST_OFF_INK_HOT_SEL)
                               : g_hiliteColour;

        y = w->rect.top + (idx - top) * LIST_ROW_HEIGHT + LIST_ROW_TOP_MARGIN;
        rowRect.top    = y;
        rowRect.bottom = y + LIST_ROW_HEIGHT;

        /* A failed intersect skips only the FILL -- `paint` keeps whatever the
         * previous row left in it and the text below is drawn against that.
         * The original's defect, and the same shape as LockSurface's Restore
         * path; kept deliberately. */
        if (IntersectRect(&paint, &clip, &rowRect) && selectedHere)
            ClearRegion(&paint, g_hiliteColour);

        if (!LockSurface(g_drawTarget))
            return;
        DrawTextClipped(rowRect.left + LIST_TEXT_INDENT, rowRect.top,
                               rows->text + offset, 1, paint, ink);
        UnlockSurface();

        idx++;
        offset += LIST_ROW_STRIDE;
    } while (idx < rows->count);
}

void __attribute__((thiscall)) ListTakeFocus(AM2_Widget *w, int32_t announce)
{
    const uint8_t *self = (const uint8_t *)w;
    int32_t        sel  = *(const int32_t *)(self + LIST_OFF_SELECTED);

    WidgetScreenRect(w);

    if (sel >= 0 && announce) {
        int32_t row = sel - *(const int32_t *)(self + LIST_OFF_TOP_ROW);
        RECT    strip;

        strip.left   = w->rect.left;
        strip.top    = w->rect.top + row * LIST_ROW_HEIGHT
                       + LIST_ROW_TOP_MARGIN;
        strip.right  = w->rect.right;
        strip.bottom = strip.top + LIST_ROW_HEIGHT;

        ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, strip);
    }
    WidgetTakeFocus(w, announce);
}

void __attribute__((thiscall)) IconDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_ICON);
    /* No null test, as in the original. */
    ReleaseSprite(*(AM2_Sprite **)(self + ICON_OFF_SPRITE));
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) IconDelete(AM2_Widget *w, int32_t flags)
{
    IconDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) BlinkerDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_BLINKER);
    /* Settle the live sprite on the OFF one before letting the ON one go.
     * Nothing reads it after this; kept because the original does it. */
    w->sprite = *(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_OFF);
    ReleaseSprite(*(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_ON));
    IconDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) BlinkerDelete(AM2_Widget *w,
                                                    int32_t flags)
{
    BlinkerDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) ButtonDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_BUTTON);

    if (*(const uint8_t *)(self + BUTTON_OFF_OWNS_SPRITES)) {
        AM2_Sprite *s;

        s = *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL);
        if (s)
            ReleaseSprite(s);
        s = *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_FOCUS);
        if (s)
            ReleaseSprite(s);
        s = *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_PRESSED);
        if (s)
            ReleaseSprite(s);
    }
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) ButtonDelete(AM2_Widget *w, int32_t flags)
{
    ButtonDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) ButtonUpdate(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  repeats;

    WidgetScreenRect(w);

    if (!w->parent || w->disabled) {
        WidgetUpdate(w);
        return;
    }

    w->unknown40 = PointInRect((const AM2_Rect *)&w->rect,
                               (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT);
    if (!w->unknown40) {
        BUTTON_DEADLINE(w) = 0;
        WidgetUpdate(w);
        return;
    }

    if (orig_mouse_moved)
        ((AM2_WidgetFocusFn *)w->vtable)[WIDGET_VSLOT_FOCUS](w, 1);

    repeats = *(const int32_t *)(self + BUTTON_OFF_REPEATS);

    if (!repeats) {
        /* Plain button: both handlers fire on RELEASE, a press only repaints. */
        if (!orig_mouse_moved) {
            WidgetUpdate(w);
            return;
        }
        if (g_mouseChanged[0]) {
            if (!g_mouseButton[0]) {
                ButtonFire(w, BUTTON_OFF_ON_LEFT);
                w->unknown40 = 0;
            }
            ButtonRepaintSelf(w);
        } else if (g_mouseChanged[1]) {
            if (!g_mouseButton[1]) {
                ButtonFire(w, BUTTON_OFF_ON_RIGHT);
                w->unknown40 = 0;
            }
            ButtonRepaintSelf(w);
        }
        WidgetUpdate(w);
        return;
    }

    /* Auto-repeat. The left and right buttons do NOT behave the same: left
     * arms the deadline on first press without firing, right fires. Kept. */
    if (g_mouseButton[0]) {
        if (g_mouseChanged[0]) {
            BUTTON_DEADLINE(w) = orig_get_tick_count() + BUTTON_REPEAT_DELAY;
        } else {
            if (orig_get_tick_count() <= BUTTON_DEADLINE(w)) {
                WidgetUpdate(w);
                return;
            }
            BUTTON_DEADLINE(w) = orig_get_tick_count() + BUTTON_REPEAT_PERIOD;
            ButtonFire(w, BUTTON_OFF_ON_LEFT);
        }
    } else if (g_mouseChanged[0]) {
        BUTTON_DEADLINE(w) = orig_get_tick_count() + BUTTON_REPEAT_DELAY;
        ButtonFire(w, BUTTON_OFF_ON_LEFT);
    } else if (g_mouseButton[1]) {
        if (g_mouseChanged[1]) {
            BUTTON_DEADLINE(w) = orig_get_tick_count() + BUTTON_REPEAT_DELAY;
            ButtonFire(w, BUTTON_OFF_ON_RIGHT);
        } else {
            if (orig_get_tick_count() <= BUTTON_DEADLINE(w)) {
                WidgetUpdate(w);
                return;
            }
            BUTTON_DEADLINE(w) = orig_get_tick_count() + BUTTON_REPEAT_PERIOD;
            ButtonFire(w, BUTTON_OFF_ON_RIGHT);
        }
    } else {
        /* Neither button down: forget the deadline and the hover. */
        BUTTON_DEADLINE(w) = 0;
        w->unknown40 = 0;
        WidgetUpdate(w);
        return;
    }
    ButtonRepaintSelf(w);
    WidgetUpdate(w);
}

void __attribute__((thiscall)) ButtonPaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;

    if (w->parent) {
        if (w->parent->focusedChild != w) {
            w->sprite = *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL);
        } else {
            int32_t pressed = 0;

            if (w->unknown40 && (g_mouseButton[0] || g_mouseChanged[0]))
                pressed = 1;
            else if (IsKeyDown(AM2_DIK_RETURN))
                pressed = 1;

            w->sprite = pressed
                ? *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_PRESSED)
                : *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_FOCUS);
        }
    }
    WidgetPaint(w, clip);
}

void __attribute__((thiscall)) EditDraw(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    char           buf[EDIT_DRAW_BUFFER];
    RECT           paint;
    int32_t        ink;

    WidgetScreenRect(w);

    if (!IntersectRect(&paint, &w->rect, &clip))
        return;

    ClearRegion(&paint, *(const uint8_t *)(self + EDIT_OFF_PAPER));

    /* The field's own buffer is never written -- the caret lives only in this
     * copy, and therefore only in the painted image. */
    strcpy(buf, *(const char *const *)(self + EDIT_OFF_TEXT));
    if (w->flag44) {
        size_t len = strlen(buf);

        buf[len]     = '_';
        buf[len + 1] = '\0';
        ink = *(const uint8_t *)(self + EDIT_OFF_INK_FOCUS);
    } else {
        ink = *(const uint8_t *)(self + EDIT_OFF_INK);
    }

    if (!LockSurface(g_drawTarget))
        return;

    DrawTextClipped(w->rect.left, w->rect.top, buf,
                           *(const int32_t *)(self + EDIT_OFF_FONT),
                           paint, ink);

    UnlockSurface();
}

void __attribute__((thiscall)) EditTakeFocus(AM2_Widget *w, int32_t announce)
{
    g_focusedEdit = w;
    g_charHandler = EditCharHandler;
    WidgetTakeFocus(w, announce);
}

void __attribute__((thiscall)) EditRepaint(AM2_Widget *w)
{
    EditReleaseFocus(w);
    WidgetRepaint(w);
}

void __attribute__((thiscall)) EditDestruct(AM2_Widget *w)
{
    w->vtable = (void *)AM2_IMAGE(VTABLE_EDIT);
    EditReleaseFocus(w);
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) EditDelete(AM2_Widget *w, int32_t flags)
{
    EditDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) EditUpdate(AM2_Widget *w)
{
    WidgetScreenRect(w);

    /* Hover to focus, and nothing else -- which is how clicking a text field
     * gives it the caret and the keyboard. */
    if (w->parent && orig_mouse_moved && !w->disabled) {
        w->unknown40 = PointInRect((const AM2_Rect *)&w->rect,
                                   (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT);
        if (w->unknown40)
            ((AM2_WidgetFocusFn *)w->vtable)[WIDGET_VSLOT_FOCUS](w, 1);
    }
    WidgetUpdate(w);
}

void __attribute__((thiscall)) WidgetDestructThunk(AM2_Widget *w)
{
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) WidgetDeleteAlt(AM2_Widget *w,
                                                      int32_t flags)
{
    WidgetDestructThunk(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) FocusLabelDestruct(AM2_Widget *w)
{
    LabelDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) FocusLabelDelete(AM2_Widget *w,
                                                       int32_t flags)
{
    FocusLabelDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) FocusLabelDraw(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;

    if (w->flag44) {
        self[LABEL_OFF_INK]   = self[FOCUSLABEL_OFF_INK_FOCUS];
        self[LABEL_OFF_PAPER] = self[FOCUSLABEL_OFF_PAPER_FOCUS];
    } else {
        /* Paper first here and ink first above -- the original's order, and
         * nothing can tell the difference. */
        self[LABEL_OFF_PAPER] = self[FOCUSLABEL_OFF_PAPER];
        self[LABEL_OFF_INK]   = self[FOCUSLABEL_OFF_INK];
    }
    LabelDraw(w, clip);
}

void __attribute__((thiscall)) FocusLabelTakeFocus(AM2_Widget *w,
                                                   int32_t announce)
{
    WidgetTakeFocus(w, announce);
}

void __attribute__((thiscall)) WidgetPaintFwd1(AM2_Widget *w, RECT clip)
{
    WidgetPaint(w, clip);
}

void __attribute__((thiscall)) WidgetPaintFwd2(AM2_Widget *w, RECT clip)
{
    WidgetPaintFwd1(w, clip);
}

void __attribute__((thiscall)) WidgetUpdateThunk(AM2_Widget *w)
{
    WidgetUpdate(w);
}

void __attribute__((thiscall)) MultiUpdateThunk(AM2_Widget *w)
{
    WidgetScreenRect(w);
}

void __attribute__((thiscall)) ScrollBarPaint(AM2_Widget *w, RECT clip)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Sprite *bar;
    int32_t     x;
    int32_t     y;
    RECT        visible;
    RECT        box;
    AM2_Rect    part;

    WidgetScreenRect(w);

    bar = *(AM2_Sprite **)(self + SCROLLBAR_OFF_BAR);
    if (!bar)
        return;

    /* The subtraction happens first and the halving after, which rounds the
     * other way from WidgetPaint's centring on an odd difference. */
    x = w->rect.left
        + ((w->rect.right - *(const int32_t *)(self + SCROLLBAR_OFF_SPAN)
            - w->rect.left) >> 1)
        + *(const int32_t *)(self + SCROLLBAR_OFF_SHIFT);
    y = w->rect.top
        + ((w->rect.bottom - bar->bounds.bottom - w->rect.top) >> 1);

    /* The box is the sprite placed at (x, y), and it is what the clip is taken
     * against -- not the widget's own rectangle, which is what WidgetPaint
     * uses. A bar shifted past its track is clipped by its own extent. */
    box.left   = x;
    box.top    = y;
    box.right  = bar->bounds.right + x;
    box.bottom = bar->bounds.bottom + y;

    /* One IntersectRect, where WidgetPaint does two -- this one never meets
     * the screen rectangle. */
    if (IntersectRect(&visible, &box, &clip)
        && ClipRect(&bar->bounds, (const AM2_Rect *)&visible, &x, &y, &part))
        DrawSpriteClipped(bar, x, y, &part, 0);
}

void __attribute__((thiscall)) ScrollBarDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    AM2_Sprite *bar = *(AM2_Sprite **)(self + SCROLLBAR_OFF_BAR);

    w->vtable = (void *)AM2_IMAGE(VTABLE_SCROLLBAR);
    /* Tested here, where the icon's identical release is not. */
    if (bar)
        ReleaseSprite(bar);
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) ScrollBarDelete(AM2_Widget *w,
                                                      int32_t flags)
{
    ScrollBarDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) ArrowDestruct(AM2_Widget *w)
{
    ButtonDestruct(w);
}

/* The palette index the matcher filled with white. */
#define g_whiteInk (*(const uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE)

/* One line of the typewriter's text, drawn at the given y. Shared by the two
 * places the original emits the same six-argument call -- on reaching a `|`
 * and once more for whatever is left after the last one. Returns 0 if the
 * surface would not lock, which is the original's early return. */
static int32_t TyperDrawLine(AM2_Widget *w, const char *line, int32_t y,
                             RECT clip, uint8_t ink)
{
    if (!LockSurface(g_drawTarget))
        return 0;

    DrawTextClipped(w->rect.left, y, line, 1, clip, ink);
    UnlockSurface();
    return 1;
}

void __attribute__((thiscall)) TyperPaint(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    const char    *text = (const char *)(self + TYPER_OFF_TEXT);
    int32_t        shown;
    uint8_t        ink;
    RECT           visible;
    char           line[0x3F0];
    int32_t        count = 0;
    int32_t        yoff  = 0;
    int32_t        i;

    WidgetScreenRect(w);

    /* Computed, tested, and then not used -- the drawer gets `clip`. */
    if (!IntersectRect(&visible, &clip, &w->rect))
        return;

    shown = *(const int32_t *)(self + TYPER_OFF_SHOWN);
    ink   = g_whiteInk;
    if (shown <= 0)
        return;

    for (i = 0; i < shown; i++) {
        if (text[i] == '|') {
            line[count] = 0;
            if (!TyperDrawLine(w, line, w->rect.top + yoff, clip, ink))
                return;
            count = 0;
            yoff += TYPER_LINE_HEIGHT;
        } else {
            line[count++] = text[i];
        }
    }

    if (count > 0) {
        line[count] = 0;
        TyperDrawLine(w, line, w->rect.top + yoff, clip, ink);
    }
}

void __attribute__((thiscall)) TyperUpdate(AM2_Widget *w)
{
    uint8_t    *self = (uint8_t *)w;
    const char *text = (const char *)(self + TYPER_OFF_TEXT);
    uint32_t    now;

    WidgetScreenRect(w);
    now = orig_get_tick_count();

    if (*(const int32_t *)(self + TYPER_OFF_SHOWN) < (int32_t)strlen(text)
        && now - *(const uint32_t *)(self + TYPER_OFF_LAST) > TYPER_REVEAL_MS) {
        AM2_Widget *blink = *(AM2_Widget **)(self + TYPER_OFF_BLINKER);

        *(uint32_t *)(self + TYPER_OFF_LAST) = now;
        if (blink)
            BlinkerStart(blink, TYPER_BLINK_MS, 1);
        /* Sound 0, the typing click. Every argument but the index is zero. */
        PlaySoundAt(0, 0, 0, 0, 0);
        *(int32_t *)(self + TYPER_OFF_SHOWN) += 1;
    }

    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
    WidgetUpdate(w);
}

AM2_Widget *__attribute__((thiscall)) ArrowDelete(AM2_Widget *w, int32_t flags)
{
    ArrowDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) DialogDestruct(AM2_Widget *w)
{
    w->vtable = (void *)AM2_IMAGE(VTABLE_DIALOG);
    IconDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) DialogDelete(AM2_Widget *w, int32_t flags)
{
    DialogDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* One per class. See widget.h for why this is a macro. The BASE is a
 * parameter because the shape is not the dialogs' alone: the multiplayer name
 * button has the identical pair and jumps to WidgetDestruct instead. */
#define AM2_CLASS_DTOR(name, vt, base)                                   \
    void __attribute__((thiscall)) name##Destruct(AM2_Widget *w)             \
    {                                                                        \
        w->vtable = (void *)AM2_IMAGE(vt);                                   \
        base(w);                                                             \
    }                                                                        \
    AM2_Widget *__attribute__((thiscall)) name##Delete(AM2_Widget *w,        \
                                                       int32_t flags)        \
    {                                                                        \
        name##Destruct(w);                                                   \
        if (flags & 1)                                                       \
            am2_free(w);                                                     \
        return w;                                                            \
    }

#define AM2_DIALOG_DTOR(name, vt) AM2_CLASS_DTOR(name, vt, DialogDestruct)

AM2_DIALOG_DTOR(DlgSelectMap, VTABLE_DLG_SELECTMAP)
AM2_DIALOG_DTOR(DlgDifficulty, VTABLE_DLG_DIFFICULTY)
AM2_DIALOG_DTOR(DlgQuitGame, VTABLE_DLG_QUITGAME)
AM2_DIALOG_DTOR(DlgReplay, VTABLE_DLG_REPLAY)
AM2_DIALOG_DTOR(DlgAudio, VTABLE_DLG_AUDIO)
AM2_DIALOG_DTOR(DlgOptions, VTABLE_DLG_OPTIONS)
AM2_DIALOG_DTOR(DlgDelGame, VTABLE_DLG_DELGAME)
AM2_DIALOG_DTOR(DlgOverwrite, VTABLE_DLG_OVERWRITE)
AM2_DIALOG_DTOR(DlgDelPlayer, VTABLE_DLG_DELPLAYER)
AM2_DIALOG_DTOR(DlgControls, VTABLE_DLG_CONTROLS)
AM2_DIALOG_DTOR(DlgSelectPlayer, VTABLE_DLG_SELECTPLAYER)
AM2_DIALOG_DTOR(DlgNameEntry, VTABLE_DLG_NAMEENTRY)
AM2_DIALOG_DTOR(DlgLoadGame, VTABLE_DLG_LOADGAME)
AM2_DIALOG_DTOR(DlgMessage, VTABLE_DLG_MESSAGE)
AM2_DIALOG_DTOR(DlgGameMenu, VTABLE_DLG_GAMEMENU)

AM2_DIALOG_DTOR(CommPanel, VTABLE_COMM_PANEL)
AM2_DIALOG_DTOR(BattleJoin, VTABLE_BATTLE_JOIN)
AM2_CLASS_DTOR(MpName, VTABLE_MP_NAME, WidgetDestruct)

/* The HUD's own classes. Every name is from the widget dump: `ctl widgets` in
 * a live mission prints a rectangle per node and the screenshot says what is
 * in it. That is why these could not be written for most of a session -- the
 * geometry was readable from the constructors all along, and geometry alone
 * does not say what a box IS.
 *
 * All five chain to WidgetDestruct rather than DialogDestruct: this family is
 * not the menu's. Each carries the MSVC SEH prologue, which is deliberately
 * not reproduced -- see CLAUDE.md on destructor frames.
 */

/* 0x00417790. The strip across the top, and it CLEARS g_charHandler.
 *
 * NOT because the strip owns a text field, which is what I first wrote. orig.h
 * already records what installs a handler in this band: binding 0x13 --
 * BACKSPACE by default -- opens a CONSOLE, and 0x004186B3 puts 0x004185C0 in
 * that slot, verified live by tapping the key and reading the global back.
 *
 * So this is teardown hygiene for the console rather than a clue about the
 * strip. It still has to be reproduced, and the reason is sharper than
 * tidiness: leave the slot set and a handler inside freed memory is what
 * WndProc calls on the next keystroke. */
void __attribute__((thiscall)) HudTopDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_TOP_STRIP);

    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE0));
    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE1));
    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE2));

    *(void **)(uintptr_t)ADDR_CHAR_HANDLER = (void *)0;

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudTopDelete(AM2_Widget *w, int32_t flags)
{
    HudTopDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00419360. The right-hand panel -- HUD widget B, the one whose constructor
 * builds the radar, Sarge, squad and commands children. One sprite. */
void __attribute__((thiscall)) HudPanelDestruct(AM2_Widget *w)
{
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_PANEL);
    ReleaseSprite(*(AM2_Sprite **)((uint8_t *)w + HUD_OFF_SPRITE0));
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudPanelDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    HudPanelDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00414830. The radar, and it is the one that frees a BITMAP rather than
 * releasing a sprite -- FreeBitmap on the slot, then the slot cleared.
 *
 * The dump agrees from the other end: the radar node prints sid=0 where every
 * other node prints a real sprite id, because what it holds at +0x58 is not a
 * sprite at all. Two independent readings of the same field. */
void __attribute__((thiscall)) HudRadarDestruct(AM2_Widget *w)
{
    void **slot = (void **)((uint8_t *)w + HUD_OFF_SPRITE0);

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_RADAR);
    FreeBitmap(slot);
    *slot = (void *)0;
    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudRadarDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    HudRadarDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00415850. The squad panel: twelve PAIRS and then one sprite of its own.
 *
 * The pair walk is the film archive's shape with the halves apart rather than
 * adjacent -- one pointer steps four bytes at a time and reads 0x30 below
 * itself as well, so the two arrays are 0x5C.. and 0x8C.. and the loop covers
 * both in twelve turns. Twelve is the squad. */
void __attribute__((thiscall)) HudSquadDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  i;

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_SQUAD);

    for (i = 0; i < AM2_HUD_SQUAD_SLOTS; i++) {
        ReleaseSprite(*(AM2_Sprite **)(self + HUD_SQUAD_PAIR_LO + i * 4));
        ReleaseSprite(*(AM2_Sprite **)(self + HUD_SQUAD_PAIR_HI + i * 4));
    }

    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE0));

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudSquadDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    HudSquadDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00419670. The strip down the right edge. Same three slots as the top
 * strip and no character handler -- which is what says that clearing it up
 * there is the top strip's own business rather than something every strip
 * does. */
void __attribute__((thiscall)) HudEdgeDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_EDGE_STRIP);

    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE0));
    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE1));
    ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE2));

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudEdgeDelete(AM2_Widget *w,
                                                    int32_t flags)
{
    HudEdgeDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00414EB0. The SARGE panel: 31 sprites of its own, from the 31 slots its
 * constructor fills with set 13 indices 0..30. Ordinary per-object state. */
void __attribute__((thiscall)) HudSargeDestruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  i;

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_SARGE);

    for (i = 0; i < AM2_HUD_SARGE_SLOTS; i++)
        ReleaseSprite(*(AM2_Sprite **)(self + HUD_OFF_SPRITE0 + i * 4));

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudSargeDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    HudSargeDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00417110. The COMMANDS panel, and it frees sprites that are NOT ITS OWN.
 *
 * The other six release slots inside the object; this one walks the GLOBAL
 * pointer-mode table -- seven records of 40 bytes from ADDR_POINTER_MODES,
 * whose sprite sits 0x0C below the mode fields at ADDR_HUD_CMD_SPRITES -- and
 * releases each. Every commands panel would read that same table, so this is
 * only safe because exactly ONE exists: it is built as a child of HUD widget B
 * and nothing else constructs the class. A second instance turns this into a
 * double free, silently, and nothing in the code says so.
 *
 * Recorded because it is a constraint the program depends on and does not
 * state. The bound is the table's end and not a count, which is how the
 * original writes it.
 */
void __attribute__((thiscall)) HudCommandsDestruct(AM2_Widget *w)
{
    AM2_Sprite **spr;

    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_COMMANDS);

    for (spr = (AM2_Sprite **)AM2_IMAGE(ADDR_HUD_CMD_SPRITES);
         spr < (AM2_Sprite **)AM2_IMAGE(ADDR_HUD_CMD_SPRITES_END);
         spr = (AM2_Sprite **)((uint8_t *)spr + AM2_POINTER_MODE_SIZE))
        ReleaseSprite(*spr);

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) HudCommandsDelete(AM2_Widget *w,
                                                        int32_t flags)
{
    HudCommandsDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x004193C0. The right-hand panel's per-frame update, and it does three
 * things rather than one.
 *
 * THE SLIDE RESIZES THE WORLD. x moves toward its stop at 320 pixels a second
 * -- FRAME_DELTA_SEC times a float constant whose SIGN carries the direction,
 * +320 opening and -320 closing -- and the result is published into
 * ADDR_BLIT_RECT.right, which ComposeFrame reads every frame to know where the
 * map goes. So the panel is not animating over the view, it is changing the
 * view's extent as it moves. 320 is exactly its travel: it sits at x 480 on a
 * 640 screen and crosses in one second.
 *
 * The arithmetic is done in DOUBLE and cast, which is what air.cpp's
 * FormationPoint established for the original's _ftol: a C cast truncates
 * toward zero exactly as _ftol does, and the precision is provably enough --
 * FRAME_DELTA_SEC times 320 is under a hundred at any frame rate this runs at.
 * It has to stay floating point either way; an integer approximation would
 * drift against the original at a different frame rate, and no A/B here would
 * see it, because both sides run at the same rate.
 *
 * The closing arm re-evaluates SCREEN_W - HudPanelWidth() three times -- once
 * to test, once to clamp, once to store. Reproduced as written; the original
 * calls it three times and the value can change between them only if the
 * widget moved, which it did.
 *
 * THE SECOND LOOP IS THE BUILD MENU AND IT ONLY RUNS IN A NETWORK GAME. For
 * each of the eighteen records it asks CanAffordUnit -- which tests the game
 * type mask AND the price against our points, both in one ADDR_UNIT_TYPES
 * record -- and sets the matching HUD widget's flag to 0 when affordable and 1
 * when not. Greyed out is 1.
 *
 * The field it writes is the BASE WIDGET'S `disabled`, already named in
 * widget.h as "set disqualifies from focus" -- so this is not merely greying,
 * it takes unaffordable units out of the focus order and the grey is a
 * consequence. I nearly added HUDBTN_OFF_GREYED for it, which would have put a
 * HUD name on a field the whole widget tree shares.
 *
 * IT PASSES THE RECORD'S ID, NEVER THE LOOP COUNTER. The menu and the type
 * table are ordered differently in five of eighteen places, so indexing by
 * position would price Grenadier as Bazookaman and look perfectly fine doing
 * it. Nothing in a single-player drive reaches this loop at all.
 */
void __attribute__((thiscall)) HudPanelUpdate(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    /* Empty the caption. It is a one-frame buffer: the radar's update refills
     * it with "Stratmap" when the mouse has been idle a second, and two paint
     * paths test its first byte before drawing. Clearing byte zero is
     * emptying a string, not clearing a flag, which is what this looked like
     * with only this line in view. */
    self[HUDPANEL_OFF_CAPTION] = 0;

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER
        || *(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    if (*(const int32_t *)(self + HUDPANEL_OFF_OPEN)) {
        int32_t stop = *(const int32_t *)(self + HUDPANEL_OFF_STOP);

        if (w->x > stop) {
            w->x -= (int32_t)((double)*(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC
                              * *(const float *)AM2_IMAGE(ADDR_HUD_SLIDE_OPEN));
            if (w->x < stop)
                w->x = stop;
            *(int32_t *)(uintptr_t)(ADDR_BLIT_RECT + 8) = w->x;
        }
    } else if (w->x < *(const int32_t *)(uintptr_t)ADDR_SCREEN_W
                       - HudPanelWidth()) {
        w->x -= (int32_t)((double)*(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC
                          * *(const float *)AM2_IMAGE(ADDR_HUD_SLIDE_SHUT));
        if (w->x > *(const int32_t *)(uintptr_t)ADDR_SCREEN_W - HudPanelWidth())
            w->x = *(const int32_t *)(uintptr_t)ADDR_SCREEN_W - HudPanelWidth();
        *(int32_t *)(uintptr_t)(ADDR_BLIT_RECT + 8) = w->x;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        AM2_Widget *const *slot =
            (AM2_Widget *const *)(uintptr_t)ADDR_HUD_WIDGET_TABLE;
        const uint8_t     *rec = (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU);

        for (; rec < (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU_END);
             rec += AM2_BUILD_MENU_STRIDE, slot++) {
            int32_t id = *(const int32_t *)(rec + BUILD_MENU_OFF_ID);

            (*slot)->disabled =
                CanAffordUnit(id, *(const int32_t *)(uintptr_t)ADDR_OUR_POINTS)
                ? 0 : 1;
        }
    }

    WidgetScreenRect(w);
    WidgetUpdate(w);
}

/* 0x00414890. The radar's per-frame update: a click jumps the camera, and a
 * second of mouse silence captions the panel.
 *
 * CLICKING IT MOVES THE EYE. The cursor's offset inside the radar is scaled by
 * the map extent over the widget's own width and height, and the result goes
 * into ADDR_VIEW_TARGET with ADDR_VIEW_HOLD raised -- and HOLD is documented
 * as "skip the glide once", so the camera JUMPS rather than sliding. The
 * radar is a jump-to-here control, not a scrub.
 *
 * THE CURSOR IS A PACKED POINT and has to be read as two int16. MakePoint's
 * own comment gives the layout -- x in the low half, y in the high -- and the
 * original reads it back with `movsx` from +0 and +2. Signed, because a cursor
 * clamped at an edge can be negative. Reading it as two int32 would compile,
 * run, and put the view at a multiple of where it belongs, and no A/B here
 * would see it: no drive clicks the radar.
 *
 * THE CAPTION IS THE PANEL'S, NOT THE RADAR'S. It writes ADDR_HUD_WIDGET_B +
 * HUDPANEL_OFF_CAPTION, and the panel empties that buffer at the top of its
 * own update every frame -- so "Stratmap" is a one-frame string that this
 * function re-asserts for as long as the mouse stays still. That is why the
 * two of them have to be read together; either alone reads as a bug.
 *
 * The gates are the family's: no character handler, no input suppression, and
 * the panel open. Then the shared click arbitration -- claim
 * ADDR_MOUSE_GRAB if it is free and a button changed, and act only if we
 * are the claimant.
 */
void __cdecl HudRadarUpdateBody(AM2_Widget *w);

/* 0x00414620. A tooltip at the cursor, and the ORDER OF THE THREE STEPS IS THE
 * WHOLE THING TO GET RIGHT.
 *
 * ClearRegion runs BEFORE LockSurface, not inside it. Its own comment in
 * surface.h says why: it refuses to run while a lock is held, because Blt
 * needs the surface back first, and it is a NO-OP in that case rather than a
 * failure. Written in the intuitive lock-fill-draw-unlock order the fill would
 * silently do nothing and the text would land on whatever was underneath --
 * on a path no drive exercises, so nothing would catch it.
 *
 * TextExtent is called with a null `out` and its RETURN used, which font.h
 * records as the form that surfaced its signature: it went in as `void`,
 * every caller then passed a real `out` and ignored eax, and the typewriter's
 * word-wrap was the first to notice. This is the second caller of that form.
 *
 * Placement. The box is centred on the cursor and 6 wider than the text, then
 * pushed left if it would leave the bitmap area. It sits BELOW the cursor
 * normally and ABOVE it when the cursor is within 100 of the bottom -- the
 * comparison is `cursorY <= H - 100`, so "near the bottom" is the ELSE.
 */
void __cdecl DrawTooltip(const char *text, uint8_t colour)
{
    const int16_t *cur = (const int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
    int32_t        w   = TextExtent(text, 0, (int32_t *)0);
    int32_t        x   = cur[0] - w / 2;
    int32_t        y;
    AM2_Rect       box;

    if (x + w + AM2_TIP_PAD > *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W)
        x = *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W - w - AM2_TIP_PAD;

    if (cur[1] > *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H
                 - AM2_TIP_BOTTOM_MARGIN)
        y = cur[1] - AM2_TIP_ABOVE;
    else
        y = cur[1] + AM2_TIP_BELOW;

    RectSet(&box, x, y, x + w + AM2_TIP_PAD, y + AM2_TIP_HEIGHT);

    /* Outside the lock, and that is not a style choice -- see above. */
    ClearRegion((const RECT *)&box,
                *(const uint8_t *)(uintptr_t)ADDR_COLOUR_LAG_MID);

    if (!LockSurface(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET))
        return;

    DrawText(x + AM2_TIP_TEXT_DX, y - AM2_TIP_TEXT_DY, text, 0, 0, colour);

    UnlockSurface();
}

/* 0x004194E0. The panel's painter: the base, then at most one tooltip.
 *
 * In a NETWORK GAME it walks the build menu looking for the record whose
 * rectangle contains the cursor and shows that unit's name. Otherwise -- and
 * also when nothing is hovered -- it shows the panel's own caption if one is
 * set, title-cased in place first.
 *
 * IT WALKS THE SAME EIGHTEEN RECORDS AS THE GREYING LOOP FROM A DIFFERENT
 * FIELD. HudPanelUpdate reads BUILD_MENU_OFF_ID at +0x00 and steps from
 * ADDR_BUILD_MENU; this reads the rectangle at +0x28 and steps from 0x0C lower
 * so that its own +0 lands on it. Same table, same stride, two offsets.
 *
 * The caption is the one-frame buffer HudPanelUpdate empties at the top of
 * every update -- so what this draws is whatever was set THIS frame, by the
 * radar or by nothing.
 */
/* 0x00417440. The COMMANDS panel's painter: up to three icons in a row, the
 * selected one on a highlight.
 *
 * TWO OF THE FIVE FAILURE PATHS EXIT AND THREE CONTINUE, which the shape does
 * not show. A negative slot index, a null sprite and a failed inner
 * IntersectRect all skip to the next slot; the outer IntersectRect and --
 * surprisingly -- ClipRect END THE PAINT. So a slot that clips away entirely
 * drops the remaining ones, and writing all three as `continue` would look
 * right and silently lose them. Branch targets, not structure.
 *
 * ClipRect's own comment is why the second one is an exit rather than a skip:
 * "returns 0 when nothing is visible, in which case the outputs are only
 * partially written and must not be used". Nothing visible here means nothing
 * visible at all.
 *
 * The offsets are three int16 PAIRS and the loop begins INSIDE the first
 * record -- 0x004766FA, reading [esi-2] and [esi] -- so the table's base is
 * two bytes lower than the pointer the original starts with. Same trap as the
 * trig tables being indexed from their centres.
 *
 * The record's sprite is at +0x0C of ADDR_POINTER_MODES-minus-0x0C, which is
 * ADDR_HUD_CMD_SPRITES: the same seven records the commands destructor frees,
 * and the index in a slot selects one of them.
 */
/* 0x004155A0. The SARGE panel: six unit slots in a 3x2 grid, each an icon with
 * a count printed over it and a highlight when its flag is set.
 *
 * The same shape as HudCommandsPaint with three differences. Six records at
 * HUDSARGE_OFF_SLOTS rather than three at +0x58, stride 0x10 rather than 4;
 * ALL FIVE failure paths continue to the next slot, where the commands paint
 * exits on two of them; and each icon carries a number.
 *
 * ClipRect's x and y are INPUTS as well as outputs -- see HudCommandsPaint,
 * which cost four rounds to establish. Initialised from the origin here, and
 * the draw uses the SAVED origin. The original shows the same duplication:
 * frames 0x74/0x6c and 0x78/0x68, one copy of each per purpose.
 *
 * THE COUNT IS A SECOND LOCK/UNLOCK BRACKET. sprintf the number, measure it
 * with TextExtent's null-`out` form, and draw it RIGHT-ALIGNED in a cell
 * AM2_HUD_SARGE_CELL_W wide -- x is the cell's right edge minus the text
 * width, not the slot's left. Font 1, ink 0xCE.
 */
/* 0x00418A20. The top strip: the message line, then the base paint, then one
 * sprite. Three parts that share nothing but the widget.
 *
 * THE TEXT IS INSIDE A LOCK BRACKET AND THE FILL IS OUTSIDE IT -- ClearRegion
 * first, then LockSurface -- for the reason surface.h gives: it refuses to run
 * while a lock is held and is a NO-OP in that case rather than a failure.
 *
 * Two mutually exclusive things can be shown. While HUDLOG_OFF_TYPING is set
 * the strip shows the line being typed, with a '_' appended as a caret and
 * drawn in white; that is where the console's characters land, and it is why
 * this class's destructor clears g_charHandler. Otherwise it walks the message
 * log -- HUDLOG_OFF_COUNT rows of AM2_HUD_MSG_SIZE at HUDLOG_OFF_ROWS, each
 * with its own float x -- and draws them in ADDR_COLOUR_STALE, the same
 * latency-colour family the tooltip's background comes from.
 *
 * THAT LOOP WILL NOT RUN ON ANY DRIVE HERE, and orig.h says why: HudMessage
 * has 46 callers and none of them fires on a configuration this project can
 * drive. So a clean A/B is evidence about the typed line and the sprite, and
 * about the message rows only by reading.
 *
 * The float is done in double and cast, which air.cpp's FormationPoint
 * established for the original's _ftol: a C cast truncates toward zero exactly
 * as _ftol does.
 */
void __attribute__((thiscall)) HudTopPaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;
    RECT     hit;
    int32_t  x, y;

    ClearRegion(&clip, *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR);

    if (LockSurface(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET)) {
        if (*(const int32_t *)(self + HUDLOG_OFF_TYPING)) {
            char        line[96];
            const char *typed = (const char *)(self + HUDLOG_OFF_TYPED);
            size_t      n;

            strcpy(line, typed);
            n = strlen(line);
            line[n]     = AM2_HUD_CARET;
            line[n + 1] = '\0';

            DrawText(*(const int32_t *)(self + HUDLOG_OFF_TYPED_X)
                         + w->rect.left,
                     *(const int32_t *)(self + HUDLOG_OFF_TYPED_Y)
                         + w->rect.top,
                     line, 1, 0,
                     *(const uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE);
        } else {
            int32_t rows = *(const int32_t *)(self + HUDLOG_OFF_COUNT);
            int32_t i;

            for (i = 0; i < rows; i++) {
                const uint8_t *row = self + HUDLOG_OFF_ROWS
                                     + i * AM2_HUD_MSG_SIZE;

                DrawText((int32_t)((double)
                             *(const float *)(row + HUDMSG_OFF_X)
                             - *(const float *)(self + HUDLOG_OFF_SCROLL))
                             + w->rect.left
                             + *(const int32_t *)(self + HUDLOG_OFF_TYPED_X),
                         *(const int32_t *)(self + HUDLOG_OFF_TYPED_Y)
                             + w->rect.top,
                         (const char *)row, 1, 0,
                         *(const uint8_t *)(uintptr_t)ADDR_COLOUR_STALE);
            }
        }

        UnlockSurface();
    }

    WidgetPaint(w, clip);

    {
        AM2_Sprite *spr =
            *(AM2_Sprite *const *)(self + HUDLOG_OFF_BUTTON_SPRITE);
        AM2_Rect    box, part;

        if (!spr)
            return;

        box.left   = *(const int32_t *)(self + HUDLOG_OFF_BUTTON_X)
                     + w->rect.left;
        box.top    = w->rect.top;
        box.right  = spr->bounds.right + box.left;
        box.bottom = spr->bounds.bottom + box.top;

        if (!IntersectRect(&hit, (const RECT *)&box, &clip))
            return;

        x = box.left;
        y = box.top;

        if (!ClipRect(&spr->bounds, (const AM2_Rect *)&clip, &x, &y, &part))
            return;

        /* The ADJUSTED pair. The original reads back the very stack slots it
         * handed ClipRect as &x and &y. Both forms exist in this binary --
         * HudCommandsPaint below deliberately uses its saved origin instead --
         * so it has to be read per function rather than assumed. */
        DrawSpriteClipped(spr, x, y, &part, 0);
    }
}

/* 0x00418660. The top strip's own update, and the counterpart to the paint
 * above: everything the paint READS, this is what moves.
 *
 * Four things share the function and only the first is obvious.
 *
 * THE BLIP BUDGET. HudMessage adds min(len, 10) to HUDLOG_OFF_BLIPS for every
 * line it posts; this drains it one at a time, roughly every 136 ms, playing
 * sound 0 each time. So the radio chatter is per CHARACTER and a longer
 * message chatters for longer. That is what settled the field's name -- from
 * the writer alone it read as a statistic nothing consumed.
 *
 * THE ROW EASE. Row i's target x is the running sum of the widths and gaps of
 * the rows before it, and each row slides left toward its target at
 * AM2_HUD_SCROLL_PPS, clamped so it cannot overshoot. The accumulator is also
 * the total width, which the scroll step below needs, so the two are one loop
 * in the original and are kept as one here.
 *
 * THE SCROLL, which has two modes and they are mutually exclusive:
 *
 *   with HUDLOG_OFF_REWIND_AT live, the offset eases to 0 -- the rewind. The
 *   deadline is cleared once it passes, but the ease runs on that frame
 *   anyway, which is the original's order and not a tidy-up opportunity;
 *
 *   otherwise, and only if the rows are wider than HUDLOG_OFF_VIEW_W, it eases
 *   toward the x of the oldest row that still fits. That row is found by
 *   walking BACK from the newest subtracting widths, so the newest message is
 *   the one guaranteed to be on screen.
 *
 * THE REWIND BUTTON, which is where ADDR_MOUSE_GRAB earns its comment. The
 * strip claims a press on the button as `this + HUDLOG_OFF_BUTTON_SPRITE` and
 * a press anywhere else on itself as `this`, so one widget arbitrates two
 * targets through one global -- and the entry test at the very top, which
 * fires the frame AFTER a release, is how the button's grab is given back.
 *
 * Two things are reproduced rather than tidied. The typing branch re-tests
 * HUDLOG_OFF_TYPING after having already branched on it, which cannot fail.
 * And the sprite slot is cleared before the hit tests and refilled only on the
 * hover and held paths, so the release frame draws no button at all.
 *
 * The hit box is a constant AM2_HUD_BUTTON_W x AM2_HUD_BUTTON_H at the same
 * origin the paint draws the sprite from -- so the two agree only because the
 * art is that size. Naming both from here is what made that visible; the paint
 * had reached the pair through bare literals.
 */
void __attribute__((thiscall)) HudTopUpdate(AM2_Widget *w)
{
    uint8_t    *self    = (uint8_t *)w;
    AM2_Widget *button  = (AM2_Widget *)(self + HUDLOG_OFF_BUTTON_SPRITE);
    AM2_Widget *grab    = *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB;
    float      *scroll  = (float *)(self + HUDLOG_OFF_SCROLL);
    float       step    = *(const float *)(uintptr_t)ADDR_FRAME_DELTA_SEC
                          * AM2_HUD_SCROLL_PPS;
    RECT        hot;
    int32_t     i, n;
    float       acc;

    /* The frame after a press on the button was released. */
    if (grab == button) {
        *(uint32_t *)(self + HUDLOG_OFF_REWIND_AT) =
            Ticks() + AM2_HUD_REWIND_HOLD_MS;
        *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = NULL;
    }

    WidgetUpdate(w);

    if (*(const int32_t *)(self + HUDLOG_OFF_TYPING)) {
        if (PointInRect((const AM2_Rect *)&w->rect,
                        (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT))
            *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = w;
        else
            w->focusedChild = NULL;

        /* A RELEASE anywhere sends the line. The typing test below is the
         * original's and is already known true. */
        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED
            && *(const int32_t *)(self + HUDLOG_OFF_TYPING))
            orig_hud_chat_send(w);
        return;
    }

    if (*(const int32_t *)(self + HUDLOG_OFF_JUST_SENT)) {
        *(int32_t *)(self + HUDLOG_OFF_JUST_SENT) = 0;
    } else if (ActionKeyPressed(AM2_ACTION_CONSOLE)) {
        PlaySoundAt(0, 0, 0, 0, 0);
        g_charHandler = (am2_char_fn)(uintptr_t)ADDR_HUD_CHAT_CHAR;
        *(int32_t *)(self + HUDLOG_OFF_TYPING) = 1;
    } else {
        if (*(const int32_t *)(self + HUDLOG_OFF_BLIPS) > 0
            && Ticks() > *(const uint32_t *)(self + HUDLOG_OFF_BLIP_AT)) {
            uint32_t jitter;

            PlaySoundAt(0, 0, 0, 0, 0);
            /* rand first, then the clock: the original's order, and the LCG
             * state is the image's, so the sequence is shared. */
            jitter = (uint32_t)orig_rand() & AM2_HUD_BLIP_JITTER;
            *(uint32_t *)(self + HUDLOG_OFF_BLIP_AT) =
                jitter + Ticks() + AM2_HUD_BLIP_MS;
            *(int32_t *)(self + HUDLOG_OFF_BLIPS) -= 1;
        }

        acc = 0.0f;
        n   = *(const int32_t *)(self + HUDLOG_OFF_COUNT);
        for (i = 0; i < n; i++) {
            uint8_t *row = self + HUDLOG_OFF_ROWS + i * AM2_HUD_MSG_SIZE;
            float   *x   = (float *)(row + HUDMSG_OFF_X);

            if (*x > acc) {
                *x -= step;
                if (*x < acc)
                    *x = acc;
            }
            acc += (float)(*(const int32_t *)(row + HUDMSG_OFF_WIDTH)
                           + AM2_HUD_MSG_GAP);
        }

        if (*(const uint32_t *)(self + HUDLOG_OFF_REWIND_AT)) {
            if (Ticks() > *(const uint32_t *)(self + HUDLOG_OFF_REWIND_AT))
                *(uint32_t *)(self + HUDLOG_OFF_REWIND_AT) = 0;

            *scroll -= step;
            if (*scroll < 0.0f)
                *scroll = 0.0f;
        } else if (acc > (float)*(const int32_t *)(self + HUDLOG_OFF_VIEW_W)) {
            float target = (float)*(const int32_t *)(self + HUDLOG_OFF_VIEW_W);
            float first;

            for (i = n - 1; i >= 0; i--) {
                int32_t width = *(const int32_t *)
                    (self + HUDLOG_OFF_ROWS + i * AM2_HUD_MSG_SIZE
                     + HUDMSG_OFF_WIDTH);

                if (target - (float)width < 0.0)
                    break;
                target -= (float)(width + AM2_HUD_MSG_GAP);
            }
            if (++i < 0)
                i = 0;

            first = *(const float *)(self + HUDLOG_OFF_ROWS
                                     + i * AM2_HUD_MSG_SIZE + HUDMSG_OFF_X);
            if (*scroll > first) {
                float v = *scroll - step;
                *scroll = first > v ? first : v;
            } else if (*scroll < first) {
                float v = *scroll + step;
                *scroll = first < v ? first : v;
            }
        }
    }

    *(AM2_Sprite **)(self + HUDLOG_OFF_BUTTON_SPRITE) = NULL;

    if (!PointInRect((const AM2_Rect *)&w->rect,
                     (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT)) {
        w->focusedChild = NULL;
        return;
    }

    {
        int32_t left = w->rect.left + *(const int32_t *)(self
                                                 + HUDLOG_OFF_BUTTON_X);
        int32_t top  = w->rect.top;

        RectSet((AM2_Rect *)&hot, left, top,
                left + AM2_HUD_BUTTON_W, top + AM2_HUD_BUTTON_H);
    }

    grab = *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB;

    if (!PointInRect((const AM2_Rect *)&hot,
                     (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT)) {
        /* On the strip but not on the button: swallow the press anyway, so
         * the map underneath does not also see it. */
        if (!grab && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
            *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = w;
        return;
    }

    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON) {
        if (grab == button)
            *(uint32_t *)(self + HUDLOG_OFF_REWIND_AT) =
                Ticks() + AM2_HUD_REWIND_HOLD_MS;
        else
            *(AM2_Sprite **)(self + HUDLOG_OFF_BUTTON_SPRITE) =
                *(AM2_Sprite **)(self + HUDLOG_OFF_SPRITE_HOT);
        return;
    }

    if (!grab && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED) {
        grab = button;
        *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = button;
    }
    if (grab != button)
        return;

    *(uint32_t *)(self + HUDLOG_OFF_REWIND_AT) = Ticks() + AM2_HUD_REWIND_TAP_MS;
    *(AM2_Sprite **)(self + HUDLOG_OFF_BUTTON_SPRITE) =
        *(AM2_Sprite **)(self + HUDLOG_OFF_SPRITE_DOWN);
}

/* The two caption tables, written out rather than read from the image, which
 * is what game/scripttokens.h already does for the keyword table.
 *
 * THE ORDER OF THE VEHICLE ONE IS THE JUMP TABLE'S, NOT THE ARMS'. Read top to
 * bottom the arms give JEEP, TANK, H|T, CONV, BOAT, ???; the table at
 * 0x00419A18 puts ??? at 4 and BOAT at 5. Taking the layout would have swapped
 * the last two silently, and no drive here rides a boat. */
static const char *const kEdgeVehicle[AM2_VEHICLE_KINDS] = {
    "JEEP", "TANK", "H|T", "CONV", "???", "BOAT"
};

/* The item captions, indexed by the byte table below rather than by type.
 * Entry 24 is the default and is what an out-of-range type reaches too. */
static const char *const kEdgeItem[] = {
    "GREN", "FLAM", "BAZ",  "MORT", "HvMG", "RIFLE", "AUTO", "MINE",
    "EXPL", "FLAG", "MSWP", "MEDI", "AIRS", "PARA",  "RECO", "NOTE",
    "FLAK", "VULC", "SNIP", "DISG", "MAG",  "AERO",  "WREN", "M80",
    "???"
};

/* One byte per object type from AM2_ITEM_TYPE_FIRST to AM2_ITEM_TYPE_LAST.
 * The repeats are the original's: 15..19 all point at FLAG and 35..38 at DISG,
 * and 24 is the default for the types with no caption at all. */
static const uint8_t kEdgeItemIndex[AM2_ITEM_TYPE_LAST - AM2_ITEM_TYPE_FIRST + 1] = {
     0,  1,  2,  3, 24, 24,  4,  5,  6,  7,
     8, 24, 24,  9,  9,  9,  9,  9, 10, 24,
    24, 11, 12, 13, 14, 15, 16, 17, 18, 24,
    24, 24, 24, 19, 19, 19, 19, 20, 21, 22,
    23
};

/* Every bar in this widget is `value * 90 / max` with one integer divide. The
 * guard is the original's and it is on the DENOMINATOR being positive, not on
 * the numerator -- an object with zero max health would divide by zero and the
 * game would take the fault, so the check has to stay where it is. */
static int32_t EdgeBar(int32_t value, int32_t max)
{
    return value * AM2_HUD_BAR_WIDTH / max;
}

/* 0x004196E0. The edge strip's update: the selected trooper's status line, and
 * the panel's tab.
 *
 * It places itself with WidgetScreenRect FIRST, because everything below tests
 * the cursor against the rectangle that call produces -- the base update at
 * the tail does it again for the children.
 *
 * A click toggles ADDR_HUD_WIDGET_B's HUDPANEL_OFF_OPEN, arbitrated through
 * ADDR_MOUSE_GRAB the same way HudTopUpdate arbitrates its rewind button: claim
 * it if it is free and a button changed, act only while it is ours.
 *
 * Then the display. Five fields are reset unconditionally -- before the null
 * test, so an army with no object clears the strip rather than leaving the last
 * trooper's numbers on screen -- and then either:
 *
 *   RIDING: the vehicle's kind picks a caption and its health fills the second
 *   bar. Note the caption goes to A while the item path writes B, so a soldier
 *   in a jeep and a soldier holding a rifle light up different halves.
 *
 *   ON FOOT: walk the six inventory slots, stopping at the first empty one.
 *   The slot that matches UNIT_OFF_INVENTORY_SEL supplies caption B and the
 *   ammo; and then, FALLING THROUGH rather than as an else, any slot holding
 *   type 0x1C supplies caption A as "ARMOR" and the second bar. The
 *   fall-through is the original's -- the selected slot is tested for armour
 *   too -- and writing it as an else would quietly stop armour showing while
 *   it is the thing in hand.
 */
void __attribute__((thiscall)) HudEdgeUpdate(AM2_Widget *w)
{
    uint8_t       *self = (uint8_t *)w;
    const uint8_t *obj;
    int32_t        i;

    WidgetScreenRect(w);

    if (PointInRect((const AM2_Rect *)&w->rect,
                    (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT)) {
        AM2_Widget *grab    = *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB;
        int32_t     changed = *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED;

        if (!grab && changed) {
            grab = w;
            *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = w;
        }

        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            && changed && grab == w) {
            uint8_t *panel = (uint8_t *)*(AM2_Widget *const *)
                                 (uintptr_t)ADDR_HUD_WIDGET_B;

            PlaySoundAt(0, 0, 0, 0, 0);
            *(int32_t *)(panel + HUDPANEL_OFF_OPEN) =
                *(const int32_t *)(panel + HUDPANEL_OFF_OPEN) == 0;
        }
    }

    obj = (const uint8_t *)LookupOwnerObj(
              *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);

    *(int32_t *)(self + EDGE_OFF_HEALTH_PCT) = 0;
    *(char *)(self + EDGE_OFF_CAPTION_A)     = '\0';
    *(int32_t *)(self + EDGE_OFF_SECOND_PCT) = 0;
    *(char *)(self + EDGE_OFF_CAPTION_B)     = '\0';
    *(int32_t *)(self + EDGE_OFF_AMMO)       = -1;

    if (!obj) {
        WidgetUpdate(w);
        return;
    }

    if (*(const int16_t *)(obj + OBJ_OFF_MAX_HEALTH) > 0)
        *(int32_t *)(self + EDGE_OFF_HEALTH_PCT) =
            EdgeBar(*(const int16_t *)(obj + OBJ_OFF_HEALTH),
                    *(const int16_t *)(obj + OBJ_OFF_MAX_HEALTH));

    if (*(const uint32_t *)(obj + OBJ_OFF_RIDING)) {
        const uint8_t *veh = (const uint8_t *)LookupType3ByUID(
                                 *(const uint32_t *)(obj + OBJ_OFF_RIDING));
        uint32_t       kind;

        if (veh) {
            kind = *(const uint32_t *)(veh + VEHICLE_OFF_KIND);
            strcpy((char *)(self + EDGE_OFF_CAPTION_A),
                   kind < AM2_VEHICLE_KINDS ? kEdgeVehicle[kind] : "???");
            *(int32_t *)(self + EDGE_OFF_SECOND_PCT) =
                EdgeBar(*(const int16_t *)(veh + OBJ_OFF_HEALTH),
                        *(const int16_t *)(veh + OBJ_OFF_MAX_HEALTH));
        }
        WidgetUpdate(w);
        return;
    }

    for (i = 0; i < AM2_INVENTORY_SLOTS; i++) {
        uint32_t       uid = *(const uint32_t *)(obj + UNIT_OFF_INVENTORY + i * 4);
        const uint8_t *item;
        const uint8_t *type;

        if (!uid)
            break;

        item = (const uint8_t *)WeaponByUid(uid);
        if (!item)
            continue;

        type = *(const uint8_t *const *)(item + OBJ_OFF_FIELD_C0);

        if (i == *(const int32_t *)(obj + UNIT_OFF_INVENTORY_SEL)) {
            uint32_t kind = *(const uint32_t *)(type + ITEMTYPE_OFF_KIND);

            *(int32_t *)(self + EDGE_OFF_AMMO) =
                *(const int32_t *)(item + ITEM_OFF_AMMO);

            strcpy((char *)(self + EDGE_OFF_CAPTION_B),
                   kind - AM2_ITEM_TYPE_FIRST
                       <= (uint32_t)(AM2_ITEM_TYPE_LAST - AM2_ITEM_TYPE_FIRST)
                   ? kEdgeItem[kEdgeItemIndex[kind - AM2_ITEM_TYPE_FIRST]]
                   : "???");
        }

        /* Falls through from the branch above, deliberately. */
        if (*(const uint32_t *)(type + ITEMTYPE_OFF_KIND) == AM2_ITEM_TYPE_ARMOR) {
            strcpy((char *)(self + EDGE_OFF_CAPTION_A), "ARMOR");
            *(int32_t *)(self + EDGE_OFF_SECOND_PCT) =
                EdgeBar(*(const int32_t *)(item + ITEM_OFF_AMMO),
                        *(const int32_t *)(type + ITEMTYPE_OFF_CAPACITY));
        }
    }

    WidgetUpdate(w);
}

/* One vertical text element of the edge strip: a full-height box at the pen,
 * intersected with the clip, and the string drawn down the middle of it inside
 * a Lock/Unlock bracket. Returns whether the surface could be locked -- the
 * original ABANDONS THE WHOLE PAINT when a lock fails, rather than skipping
 * the one element, and every one of its four text blocks does that. */
static int32_t EdgeText(const AM2_Widget *w, int32_t pen, const char *text,
                        const RECT *clip)
{
    RECT     box;
    RECT     vis;
    int32_t  mid = w->rect.left + (w->rect.right - w->rect.left) / 2;

    RectSet((AM2_Rect *)&box, w->rect.left, pen,
            w->rect.right, w->rect.bottom);

    if (!IntersectRect(&vis, &box, clip))
        return 1;

    if (!LockSurface(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET))
        return 0;

    DrawTextVertical(mid, pen, text, 1, vis,
                     *(const uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE);
    UnlockSurface();
    return 1;
}

/* The strip's own sprite, drawn at the pen and clipped twice the way every
 * sprite in this tree is. Nothing is drawn if either clip rejects it, and the
 * caller advances the pen either way. */
static void EdgeSprite(const AM2_Widget *w, int32_t pen, AM2_Sprite *spr,
                       const RECT *clip)
{
    RECT     box;
    RECT     vis;
    AM2_Rect part;
    int32_t  x;
    int32_t  y;

    RectSet((AM2_Rect *)&box, w->rect.left, pen,
            w->rect.right, spr->bounds.bottom + pen);

    if (!IntersectRect(&vis, &box, clip))
        return;

    x = w->rect.left;
    y = pen;

    if (!ClipRect(&spr->bounds, (const AM2_Rect *)&vis, &x, &y, &part))
        return;

    DrawSpriteClipped(spr, x, y, &part, 0);
}

/* A bar, which is a filled rectangle and not a sprite: eight pixels wide at
 * AM2_HUD_BAR_X, growing UPWARD from a baseline AM2_HUD_BAR_BASE below the
 * pen. ClearRegion does the fill, so this must not be inside a lock -- see
 * surface.h, where a fill attempted while a lock is held is a silent no-op. */
static void EdgeBarFill(const AM2_Widget *w, int32_t pen, int32_t percent,
                        const RECT *clip)
{
    RECT box;
    RECT vis;

    RectSet((AM2_Rect *)&box,
            w->rect.left + AM2_HUD_BAR_X,
            pen + AM2_HUD_BAR_BASE - percent,
            w->rect.left + AM2_HUD_BAR_X + AM2_HUD_BAR_W,
            pen + AM2_HUD_BAR_BASE);

    if (!IntersectRect(&vis, &box, clip))
        return;

    ClearRegion(&vis, *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR);
}

/* 0x00419AC0. The edge strip's paint: draw, top to bottom, exactly what
 * HudEdgeUpdate filled.
 *
 * ONE VERTICAL PEN runs the whole function and every element advances it, so
 * this is a STACK and not a set of fixed positions -- an element the update
 * left empty takes no space and everything below it moves up. That is why the
 * captions are tested for an empty first byte rather than for a null pointer:
 * they are buffers in the widget, always present, sometimes empty.
 *
 * Bars are the one thing whose geometry is not obvious. They grow UPWARD from
 * a baseline AM2_HUD_BAR_BASE below the pen, so the rectangle's TOP is what
 * moves with the percentage. 94 and 90 differ by the four pixels of margin and
 * by nothing else.
 *
 * A FAILED LOCK ABANDONS THE WHOLE PAINT, not the one element. All four text
 * blocks do that in the original and it is reproduced: returning early leaves
 * the elements below undrawn for that frame, which is what it does.
 *
 * The ammo is clamped HERE and not by the update: over AM2_HUD_AMMO_MAX it
 * draws "**", otherwise "%02d" through the game's own sprintf -- the game's,
 * because the buffer crosses back into original code and crt.h already keeps
 * that seam.
 */
void __attribute__((thiscall)) HudEdgePaint(AM2_Widget *w, RECT clip)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Sprite *spr  = *(AM2_Sprite **)(self + EDGE_OFF_SPRITE);
    RECT        vis;
    int32_t     pen;

    if (!IntersectRect(&vis, (const RECT *)(uintptr_t)ADDR_SCREEN_CLIP, &clip))
        return;

    WidgetPaint(w, vis);

    pen = w->rect.top + 2;

    /* SARGE, then the strip sprite under it, then the trooper's health. */
    if (!EdgeText(w, pen, (const char *)AM2_IMAGE(AM2_HUD_STR_SARGE), &clip))
        return;
    pen += TextStackHeight((const char *)AM2_IMAGE(AM2_HUD_STR_SARGE), 1)
           + AM2_HUD_EDGE_GAP;

    EdgeSprite(w, pen, spr, &clip);
    EdgeBarFill(w, pen, *(const int32_t *)(self + EDGE_OFF_HEALTH_PCT), &clip);
    pen += spr->bounds.bottom + AM2_HUD_EDGE_GAP_BAR;

    /* The vehicle or the armour, if there is one, with its own bar. */
    if (*(const char *)(self + EDGE_OFF_CAPTION_A)) {
        const char *cap = (const char *)(self + EDGE_OFF_CAPTION_A);

        if (!EdgeText(w, pen, cap, &clip))
            return;
        pen += TextStackHeight(cap, 1) + AM2_HUD_EDGE_GAP;

        EdgeSprite(w, pen, spr, &clip);
        EdgeBarFill(w, pen, *(const int32_t *)(self + EDGE_OFF_SECOND_PCT),
                    &clip);
        pen += spr->bounds.bottom + AM2_HUD_EDGE_GAP_BAR;
    }

    /* The item in hand, and its ammo if it counts any. */
    if (!*(const char *)(self + EDGE_OFF_CAPTION_B))
        return;

    {
        const char *cap = (const char *)(self + EDGE_OFF_CAPTION_B);

        if (!EdgeText(w, pen, cap, &clip))
            return;
        pen += TextStackHeight(cap, 1) + AM2_HUD_EDGE_GAP;
    }

    if (*(const int32_t *)(self + EDGE_OFF_AMMO) < 0)
        return;

    {
        AM2_Sprite *ammo = *(AM2_Sprite **)(self + EDGE_OFF_AMMO_SPRITE);
        int32_t     n    = *(const int32_t *)(self + EDGE_OFF_AMMO);
        char        text[16];

        EdgeSprite(w, pen, ammo, &clip);
        pen += 3;

        if (n > AM2_HUD_AMMO_MAX)
            strcpy(text, (const char *)AM2_IMAGE(AM2_HUD_STR_AMMO_OVER));
        else
            am2_sprintf(text, (const char *)AM2_IMAGE(AM2_HUD_STR_AMMO_FMT), n);

        EdgeText(w, pen, text, &clip);
    }
}

/* 0x00414B50, vtable slot 1 of the radar. Two things: the VIEW BOX, and one
 * blip per registered object. Every function it reaches is now ours.
 *
 * THE VIEW BOX is ADDR_SECOND_RECT -- the visible world rectangle -- scaled
 * from map space onto the widget. Its right edge is clamped to the bitmap
 * width less AM2_RADAR_RIGHT_MARGIN and intersected with the widget, but ONLY
 * when ADDR_NET_GAME is clear: a network game gets the box unclamped and
 * unintersected. Reproduced; nothing read so far says why.
 *
 * A BLIP'S GATE is four tests deep and the third is the surprising one. Type
 * 2/3/8 AND flagged OBJ_FLAG_DESTROYED sends it down a rider check -- the
 * object must be type 2, must be riding something, and that vehicle must be
 * REVEALED and CONCEALED both -- while anything else falls straight through to
 * the object's own REVEALED/CONCEALED/BIT4 trio. So the destroyed flag selects
 * an entirely different test, which reads backwards until you follow the two
 * `je` targets and see they converge.
 *
 * TWO DIFFERENT BLINKS, and they are not the same clock arithmetic. An
 * ordinary blip picks between a colour PAIR with `~(clock + owner*8) >> 9 & 1`
 * -- per OBJECT, seeded from a POINTER field, so distinct objects land on
 * distinct phases and the blips do not flash in unison. The two animated
 * drawers instead share ONE global phase, (clock >> 8) % 3. A single blink
 * source would have been the natural guess and it is wrong.
 */
void __attribute__((thiscall)) HudRadarPaint(AM2_Widget *w, RECT clip)
{
    const AM2_Rect *view  = (const AM2_Rect *)(uintptr_t)ADDR_SECOND_RECT;
    int32_t         mapW  = *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X;
    int32_t         mapH  = *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y;
    const uint8_t  *pal   = (const uint8_t *)(uintptr_t)ADDR_RADAR_COLOURS;
    AM2_Rect        box;
    uint8_t        *obj;
    int32_t         phase;

    WidgetPaint(w, clip);

    box.left   = w->rect.left + view->left * w->w / mapW;
    box.top    = w->rect.top  + view->top  * w->h / mapH;
    box.right  = box.left + (view->right  - view->left) * w->w / mapW;
    box.bottom = box.top  + (view->bottom - view->top)  * w->h / mapH;

    if (!*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        AM2_Rect lim;
        int32_t  edge = *(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W
                        - AM2_RADAR_RIGHT_MARGIN;

        lim.left   = w->rect.left;
        lim.top    = w->rect.top;
        lim.right  = w->rect.right < edge ? w->rect.right : edge;
        lim.bottom = w->rect.bottom;

        IntersectRect((RECT *)&box, (const RECT *)&box, (const RECT *)&lim);
    }

    DrawRectFast(&box, *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR);

    phase = (int32_t)((*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                       >> AM2_RADAR_PHASE_SHIFT) % AM2_BLIP_PULSE_PHASES);

    for (obj = (uint8_t *)FirstItem(); obj; obj = (uint8_t *)NextItem()) {
        const AM2_Object *o = (const AM2_Object *)obj;
        uint32_t          flags;
        int32_t           x;
        int32_t           y;
        int32_t           colour;
        int32_t           blink;

        if (ObjIsTypeIn238(o) && (*(const uint32_t *)(obj + OBJ_OFF_FLAGS8)
                                & OBJ_FLAG_DESTROYED)) {
            const uint8_t *veh;
            uint32_t       ride;

            if (!ObjIsType2(o))
                continue;

            ride = *(const uint32_t *)(obj + OBJ_OFF_RIDING);
            if (!ride)
                continue;

            veh = (const uint8_t *)LookupByUID(ride);

            /* The object's OWN bit 4 short-circuits the vehicle test. The
             * original dereferences `veh` without checking it; reproduced. */
            if (!(*(const uint32_t *)(obj + OBJ_OFF_FLAGS8) & OBJ_FLAG_BIT4)) {
                uint32_t vf = *(const uint32_t *)(veh + OBJ_OFF_FLAGS8);

                if ((vf & OBJ_FLAG_REVEALED) && !(vf & OBJ_FLAG_CONCEALED))
                    continue;
            }
        }

        flags = *(const uint32_t *)(obj + OBJ_OFF_FLAGS8);

        if ((flags & OBJ_FLAG_CONCEALED) && !(flags & OBJ_FLAG_BIT4))
            continue;
        if (!(flags & OBJ_FLAG_REVEALED))
            continue;
        if (*(const int16_t *)(obj + OBJ_OFF_HEALTH) == 0)
            continue;

        x = *(const int16_t *)(obj + OBJ_OFF_POS)     * w->w / mapW
            + w->rect.left;
        y = *(const int16_t *)(obj + OBJ_OFF_POS + 2) * w->h / mapH
            + w->rect.top;

        colour = RadarBlipColour(o, &blink);

        if ((flags & OBJ_FLAG_BIT4) && !blink) {
            int32_t a = pal[colour * 2];
            int32_t b = pal[colour * 2 + 1];

            if (ObjType2Field548(o))
                DrawBlipPulse(x, y, a, b, phase);
            else
                DrawBlipSquare(x, y, a, b, phase);
        } else {
            /* Per-OBJECT blink, seeded from OBJ_OFF_OWNER -- which is a
             * POINTER, not a uid, so distinct objects land on distinct phases
             * and the blips do not all flash together. */
            uint32_t t = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                         + *(const uint32_t *)(obj + OBJ_OFF_OWNER) * 8;

            DrawBlip3(x, y, pal[((~t) >> AM2_RADAR_BLINK_SHIFT & 1)
                                + colour * 2]);
        }
    }

    UnlockSurface();
}

/* 0x00416DA0, vtable slot 1 of the squad panel. Twelve portraits in a 3x4
 * grid, each with optional decoration on top.
 *
 * THE GRID IS A TABLE, not arithmetic: x is 6, 50, 94 and y is 22, 60, 98,
 * 136, and the original walks it four bytes at a time from 0x004766CA reading
 * `[p-2]` for x and `[p]` for y -- so the first x sits two bytes BEFORE the
 * pointer the loop starts from, and the bound it stops at is one past the last
 * pair. Written as an ordinary indexed table here; the two are the same twelve
 * pairs and the odd pointer is a compiler artefact, not a layout.
 *
 * SQUAD_REC_WIDE CHANGES THREE THINGS AT ONCE and that is the whole shape of
 * the function. Set, the portrait comes from HUD_SQUAD_PAIR_HI rather than
 * _LO, a AM2_SQUAD_WIDE_W-wide backdrop is filled behind it, and the slot
 * stops after the portrait to call ADDR_HUD_SQUAD_DETAIL instead of drawing
 * its own bar and pips. Reading it as three independent flags would produce a
 * panel that is wrong in three ways at once.
 *
 * Everything below the portrait is skipped for a wide slot: the bar is
 * `SQUAD_REC_BAR_W` pixels from the left inset, and the pips are
 * `SQUAD_REC_ICONS` copies of ONE sprite at HUD_SQUAD_ICON_SPRITE, spaced
 * AM2_SQUAD_ICON_DX apart. Each is clipped independently and the pen advances
 * whether or not it drew, the same as every other clipped run in this tree.
 *
 * ADDR_HUD_SQUAD_DETAIL stays the original's -- 3,328 bytes, one caller, and
 * nothing about it is needed to know what this function does.
 */
void __attribute__((thiscall)) HudSquadPaint(AM2_Widget *w, RECT clip)
{
    uint8_t        *self = (uint8_t *)w;
    const int16_t  *grid = (const int16_t *)AM2_IMAGE(ADDR_HUD_SQUAD_SLOT_XY);
    RECT            vis;
    int32_t         slot;

    if (!IntersectRect(&vis, (const RECT *)(uintptr_t)ADDR_SCREEN_CLIP, &clip))
        return;

    for (slot = 0; slot < AM2_HUD_SQUAD_SLOTS; slot++) {
        const uint8_t *rec = self + HUD_SQUAD_RECS + slot * HUD_SQUAD_REC_SIZE;
        int32_t        idx = *(const int32_t *)(rec + SQUAD_REC_INDEX);
        int32_t        wide;
        AM2_Sprite    *spr;
        RECT           box;
        RECT           hit;
        AM2_Rect       part;
        int32_t        x;
        int32_t        y;
        int32_t        i;

        if (idx < 0)
            continue;

        spr = *(AM2_Sprite **)(self + HUD_SQUAD_PAIR_LO + idx * 4);
        if (!spr)
            continue;

        wide = *(const int32_t *)(rec + SQUAD_REC_WIDE);
        if (wide)
            spr = *(AM2_Sprite **)(self + HUD_SQUAD_PAIR_HI + idx * 4);

        x = grid[slot * 2]     + w->rect.left;
        y = grid[slot * 2 + 1] + w->rect.top;

        box.left   = x;
        box.top    = y;
        box.right  = x + spr->bounds.right;
        box.bottom = y + spr->bounds.bottom;

        /* The wide backdrop, behind the portrait rather than over it. */
        if (wide) {
            RECT back;

            /* From the portrait's RIGHT EDGE out to x + AM2_SQUAD_WIDE_W --
             * the original computes the far edge by taking spr->bounds.right
             * back off box.right to recover x and adding 131 to it, which is
             * easy to transcribe with the two ends swapped. Swapped, the rect
             * is inverted, IntersectRect rejects it, and the fill silently
             * does not happen: bootcamp came back with a 60x75 block of the
             * map showing through where the original had painted it out. */
            back.left   = box.right;
            back.top    = y;
            back.right  = box.right - spr->bounds.right + AM2_SQUAD_WIDE_W;
            back.bottom = box.bottom;

            if (IntersectRect(&hit, &back, &vis))
                ClearRegion(&hit,
                            *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR);
        }

        /* TWO NEARLY IDENTICAL EXITS, and they are not the same. This
         * intersect fails to 0x0041701C, which is the LOOP ADVANCE; the
         * ClipRect below fails to 0x00417036, which is the function's own
         * epilogue. Written as `return` for both -- the first slot whose box
         * fell outside the clip killed every slot after it, and bootcamp came
         * back 3,707 pixels wrong inside the squad panel. */
        if (!IntersectRect(&hit, &box, &vis))
            continue;

        {
            int32_t cx = box.left;
            int32_t cy = box.top;

            if (!ClipRect(&spr->bounds, (const AM2_Rect *)&hit, &cx, &cy, &part))
                return;

            if (*(const int32_t *)(rec + SQUAD_REC_HILITE))
                ClearRegion(&hit, AM2_HUD_SQUAD_HILITE);

            /* THE ADJUSTED PAIR, not the box. ClipRect's x and y are IN/OUT
             * and the original hands DrawSpriteClipped the very slots it wrote
             * back through -- rect.h says so and this function is the second
             * place in one session where passing the originals instead drew
             * the panel wrong. */
            DrawSpriteClipped(spr, cx, cy, &part, 0);
        }

        if (wide) {
            orig_hud_squad_detail(w, *(const int32_t *)(rec + SQUAD_REC_DETAIL_ARG));
            continue;
        }

        /* The bar under the portrait. */
        if (*(const int32_t *)(rec + SQUAD_REC_BAR_W) > 0) {
            RECT bar;

            bar.left   = x + AM2_SQUAD_BAR_X;
            bar.top    = y + AM2_SQUAD_BAR_TOP;
            bar.right  = x + *(const int32_t *)(rec + SQUAD_REC_BAR_W);
            bar.bottom = y + AM2_SQUAD_BAR_BOTTOM;

            if (IntersectRect(&hit, &bar, &vis))
                ClearRegion(&hit, *(const uint8_t *)(rec + SQUAD_REC_BAR_COLOUR));
        }

        /* The pips, all one sprite, spaced along the bottom. */
        for (i = 0; i < *(const int32_t *)(rec + SQUAD_REC_ICONS); i++) {
            AM2_Sprite *pip = *(AM2_Sprite **)(self + HUD_SQUAD_ICON_SPRITE);
            RECT        pbox;
            int32_t     px;
            int32_t     py;

            if (!pip)
                break;

            px = x + i * AM2_SQUAD_ICON_DX + AM2_SQUAD_ICON_X;
            py = y + AM2_SQUAD_ICON_Y;

            pbox.left   = px;
            pbox.top    = py;
            pbox.right  = px + pip->bounds.right;
            pbox.bottom = py + pip->bounds.bottom;

            if (!IntersectRect(&hit, &pbox, &vis))
                continue;

            {
                int32_t cx = px;
                int32_t cy = py;

                if (!ClipRect(&pip->bounds, (const AM2_Rect *)&hit,
                              &cx, &cy, &part))
                    continue;

                /* Same contract again, same adjusted pair. */
                DrawSpriteClipped(pip, cx, cy, &part, 0);
            }
        }
    }
}

/* Object type -> sarge sprite index, for types AM2_ITEM_TYPE_FIRST..LAST. The
 * original is a 41-entry jump table whose arms are `mov ecx, imm`, so this is
 * the table read through the arms rather than the arms read in layout order --
 * the same discipline the edge strip's vehicle table needed, and here the two
 * orders genuinely differ: entry 24 lands on the arm that sets 10 while the
 * arm before it sets 8. Zero is the default and several real types map to it. */
static const uint8_t kSargeSprite[AM2_ITEM_TYPE_LAST - AM2_ITEM_TYPE_FIRST + 1] = {
     2,  3,  1,  4,  0,  0, 16,  0,  5,  7,
     6,  0,  0, 26, 27, 28, 29, 30,  8,  0,
     0, 12, 10, 11,  9,  0, 13, 14, 15,  0,
     0,  0, 17, 18, 19, 20, 21, 22, 23, 24,
    25
};

/* 0x004150F0, vtable slot 2 of the sarge panel. It fills every record
 * HudSargePaint reads, in three passes over the same six slots.
 *
 * THE HOTKEYS come first and they short-circuit: bindings 0x15..0x1A, one per
 * slot, and the first that answers selects that weapon and skips both the rest
 * of the chain and the whole mouse pass. Written as a loop with a break, which
 * is the same thing the original's ladder of `jmp` does.
 *
 * THE MOUSE pass walks the same six cells, claims through ADDR_MOUSE_GRAB the
 * way every panel in this family does, and selects on RELEASE. Its second job
 * is the tooltip: after AM2_HUD_TOOLTIP_DWELL of no mouse activity it writes
 * the hovered item's name into ADDR_HUD_WIDGET_B's caption -- the same
 * one-frame idiom the radar uses for "Stratmap", where the panel empties the
 * buffer every frame and whoever is hovered re-asserts it.
 *
 * THE REFILL rewrites all six records from the inventory, and its selected
 * flag is 1 OR 2 rather than a boolean: 2 when ADDR_ITEM_IS_READY answers
 * non-zero. The paint only tests `> 0`, so a `!!` here would pass every check
 * in this tree and still be wrong -- the original computes it with
 * neg/sbb/neg/inc, which is `(r != 0) + 1`.
 */
void __attribute__((thiscall)) HudSargeUpdate(AM2_Widget *w)
{
    uint8_t        *self = (uint8_t *)w;
    const int16_t  *grid = (const int16_t *)AM2_IMAGE(ADDR_HUD_SARGE_OFFSETS);
    uint8_t        *obj;
    int32_t         i;

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER
        || *(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    WidgetUpdate(w);

    obj = (uint8_t *)LookupOwnerObj(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);
    if (!obj)
        return;

    /* Pass one: the hotkeys, first match wins and nothing else runs. */
    for (i = 0; i < AM2_HUD_SARGE_ROWS; i++) {
        if (ActionKeyPressed(AM2_ACTION_WEAPON_FIRST + i)) {
            orig_select_weapon(obj, i);
            goto refill;
        }
    }

    /* Pass two: the mouse, but only while the panel is open. */
    if (!*(const int32_t *)((const uint8_t *)*(AM2_Widget *const *)
                                (uintptr_t)ADDR_HUD_WIDGET_B
                            + HUDPANEL_OFF_OPEN))
        goto refill;

    for (i = 0; i < AM2_HUD_SARGE_ROWS; i++) {
        const uint8_t *rec = self + HUDSARGE_OFF_SLOTS + i * HUDSARGE_REC_STRIDE;
        int32_t        idx = *(const int32_t *)(rec + HUDSARGE_REC_INDEX);
        AM2_Sprite    *spr;
        AM2_Rect       cell;
        AM2_Widget    *grab;
        int32_t        changed;
        uint32_t       uid;

        if (idx < 0)
            continue;

        spr = *(AM2_Sprite **)(self + HUDSARGE_OFF_SPRITES + idx * 4);
        if (!spr)
            continue;

        cell.left   = grid[i * 2]     + w->rect.left;
        cell.top    = grid[i * 2 + 1] + w->rect.top;
        cell.right  = spr->bounds.right  + cell.left;
        cell.bottom = spr->bounds.bottom + cell.top;

        if (!PointInRect(&cell, (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT))
            continue;

        grab    = *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB;
        changed = *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED;

        if (!grab && changed) {
            grab = w;
            *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = w;
        }

        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            && changed && grab == w)
            orig_select_weapon(obj, i);

        /* The tooltip, and it is gated on STILLNESS rather than on the click.
         * ADDR_MOUSE_ACTIVITY is stamped from the clock on any movement or
         * button change, so this fires while the pointer rests on a cell. */
        if ((int32_t)(*(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                      - *(const uint32_t *)(uintptr_t)ADDR_MOUSE_ACTIVITY)
            > AM2_HUD_TOOLTIP_DWELL) {
            const uint8_t *item;

            uid = *(const uint32_t *)(obj + UNIT_OFF_INVENTORY + i * 4);
            if (!uid)
                continue;

            item = (const uint8_t *)WeaponByUid(uid);
            if (!item)
                continue;

            strcpy((char *)((uint8_t *)*(AM2_Widget *const *)
                                (uintptr_t)ADDR_HUD_WIDGET_B
                            + HUDPANEL_OFF_CAPTION),
                   ItemTypeName(
                       **(const uint32_t *const *)(item + OBJ_OFF_FIELD_C0)));
        }
    }

refill:
    /* Pass three: rebuild every record from the inventory. */
    for (i = 0; i < AM2_HUD_SARGE_ROWS; i++) {
        uint8_t       *rec = self + HUDSARGE_OFF_SLOTS + i * HUDSARGE_REC_STRIDE;
        uint32_t       uid = *(const uint32_t *)(obj + UNIT_OFF_INVENTORY + i * 4);
        const uint8_t *item;
        uint32_t       kind;
        int32_t        ammo;

        if (!uid) {
            *(int32_t *)(rec + HUDSARGE_REC_INDEX) = -1;
            continue;
        }

        item = (const uint8_t *)WeaponByUid(uid);
        if (!item) {
            *(int32_t *)(rec + HUDSARGE_REC_INDEX) = -1;
            continue;
        }

        kind = **(const uint32_t *const *)(item + OBJ_OFF_FIELD_C0);
        *(int32_t *)(rec + HUDSARGE_REC_INDEX) =
            kind - AM2_ITEM_TYPE_FIRST
                <= (uint32_t)(AM2_ITEM_TYPE_LAST - AM2_ITEM_TYPE_FIRST)
            ? kSargeSprite[kind - AM2_ITEM_TYPE_FIRST]
            : 0;

        ammo = *(const int32_t *)(item + ITEM_OFF_AMMO);
        if (ammo > AM2_HUD_SARGE_AMMO_MAX)
            ammo = AM2_HUD_SARGE_AMMO_MAX;
        *(int32_t *)(rec + HUDSARGE_REC_COUNT) = ammo;

        /* 1 or 2, never a boolean -- see the header comment. */
        *(int32_t *)(rec + HUDSARGE_REC_READY) =
            i == *(const int32_t *)(obj + UNIT_OFF_INVENTORY_SEL)
            ? (ItemIsReady(item) != 0) + 1
            : 0;
    }
}

/* 0x00433360, vtable slot 1 of the scrolling text list. One of the nine
 * functions still outstanding on CLAUDE.md's Lock/Unlock batch, and the
 * smallest of them.
 *
 * A RECORD NAMES A COLOUR RATHER THAN CARRYING ONE. Each is 0x104 bytes --
 * 0x100 of text and then an INDEX -- and the byte actually drawn comes from a
 * table of dwords at TEXTLIST_OFF_COLOURS whose low byte is taken. Reading
 * that index as the colour would give the right shape and the wrong palette,
 * and on a list of one colour it would look correct.
 *
 * THE LOCK IS PER ROW, not per paint: LockSurface and UnlockSurface bracket
 * each DrawTextClipped individually. That is a full bracket rather than one of
 * the halves this batch keeps turning up, and it means a lock failure
 * abandons the rest of the list -- the original returns rather than skipping
 * the row, which is reproduced.
 *
 * TWO BOUNDS, checked in different places and both needed. The loop's own
 * condition is the source's total count; inside it, a second test stops at
 * FIRST + VISIBLE. So a list longer than the window is cut by the inner test
 * and a window larger than the list by the outer one, and neither alone is
 * enough.
 */
void __attribute__((thiscall)) TextListPaint(AM2_Widget *w, RECT clip)
{
    uint8_t       *self = (uint8_t *)w;
    const uint8_t *src  = *(const uint8_t *const *)(self + TEXTLIST_OFF_SOURCE);
    RECT           vis;
    int32_t        row;

    if (!src)
        return;

    WidgetScreenRect(w);

    if (!IntersectRect(&vis, &clip, &w->rect))
        return;

    ClearRegion(&vis, *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR);

    src = *(const uint8_t *const *)(self + TEXTLIST_OFF_SOURCE);

    for (row = *(const int32_t *)(self + TEXTLIST_OFF_FIRST);
         row < *(const int32_t *)(src + TEXTLIST_SRC_COUNT);
         row++) {
        const uint8_t *rec;
        int32_t        first = *(const int32_t *)(self + TEXTLIST_OFF_FIRST);
        int32_t        idx;
        uint8_t        colour;
        int32_t        y;

        if (row >= first + *(const int32_t *)(self + TEXTLIST_OFF_VISIBLE))
            return;

        rec = *(const uint8_t *const *)(src + TEXTLIST_SRC_RECORDS)
              + row * TEXTLIST_REC_SIZE;

        idx    = *(const int32_t *)(rec + TEXTLIST_REC_COLOUR);
        colour = *(const uint8_t *)(self + TEXTLIST_OFF_COLOURS + idx * 4);

        y = w->rect.top + (row - first) * AM2_TEXT_LIST_ROW_H + AM2_TEXT_LIST_PAD;

        if (!LockSurface(g_drawTarget))
            return;

        DrawTextClipped(w->rect.left + AM2_TEXT_LIST_PAD, y,
                        (const char *)rec, AM2_TEXT_LIST_FONT, vis, colour);
        UnlockSurface();

        src = *(const uint8_t *const *)(self + TEXTLIST_OFF_SOURCE);
    }
}

/* 0x00454840, vtable slot 1 of the CHECKBOX. Another of the nine outstanding
 * Lock/Unlock functions, and it is the class's whole appearance.
 *
 * FOUR STATES, NOT TWO. The sprite and the ink are chosen together from
 * (focused, checked) -- focused meaning the PARENT's focusedChild is this
 * widget, which is why the parent has to be walked rather than a flag read.
 * The four sprites are dwords from 0x68 and the four inks are single BYTES
 * from 0x84, in the same order; they are not one array of pairs.
 *
 * Then two overrides, and the order between them matters:
 *
 *   CHECKBOX_OFF_FORCE_PLAIN, when the box is CHECKED, puts back the
 *   unfocused-checked pair even though the widget holds the focus -- it undoes
 *   the focus, not the check;
 *
 *   and a disabled widget takes ADDR_COLOUR_STALE for its INK ONLY, keeping
 *   whichever sprite was chosen. So a greyed-out checkbox still shows whether
 *   it is ticked, which a single "draw it grey" would have lost.
 *
 * The sprite is written into w->sprite and drawn by WidgetPaint; this function
 * never blits it itself. The caption is its own Lock/Unlock bracket after
 * that, at AM2_CHECKBOX_TEXT_X from the left, and is skipped entirely when the
 * pointer is null.
 */
void __attribute__((thiscall)) CheckboxPaint(AM2_Widget *w, RECT clip)
{
    uint8_t    *self    = (uint8_t *)w;
    int32_t     focused = w->parent && w->parent->focusedChild == w;
    int32_t     checked = *(const uint8_t *)(self + CHECKBOX_OFF_CHECKED);
    const char *caption;
    RECT        vis;
    uint8_t     ink;

    if (focused) {
        if (checked) {
            w->sprite = *(AM2_Sprite **)(self + CHECKBOX_OFF_SPR_ON_FOC);
            ink = *(const uint8_t *)(self + CHECKBOX_OFF_INK_ON_FOC);
        } else {
            w->sprite = *(AM2_Sprite **)(self + CHECKBOX_OFF_SPR_OFF_FOC);
            ink = *(const uint8_t *)(self + CHECKBOX_OFF_INK_OFF_FOC);
        }
    } else {
        if (checked) {
            w->sprite = *(AM2_Sprite **)(self + CHECKBOX_OFF_SPR_ON);
            ink = *(const uint8_t *)(self + CHECKBOX_OFF_INK_ON);
        } else {
            w->sprite = *(AM2_Sprite **)(self + CHECKBOX_OFF_SPR_OFF);
            ink = *(const uint8_t *)(self + CHECKBOX_OFF_INK_OFF);
        }
    }

    /* Undoes the FOCUS, not the check. */
    if (*(const uint8_t *)(self + CHECKBOX_OFF_FORCE_PLAIN) && checked) {
        w->sprite = *(AM2_Sprite **)(self + CHECKBOX_OFF_SPR_ON);
        ink = *(const uint8_t *)(self + CHECKBOX_OFF_INK_ON);
    }

    /* Ink only -- the sprite stays, so a disabled box still reads as ticked. */
    if (w->disabled)
        ink = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_STALE;

    WidgetPaint(w, clip);

    caption = *(const char *const *)(self + CHECKBOX_OFF_CAPTION);
    if (!caption)
        return;

    if (!IntersectRect(&vis, &clip, &w->rect))
        return;

    if (!LockSurface(g_drawTarget))
        return;

    DrawTextClipped(w->rect.left + AM2_CHECKBOX_TEXT_X, w->rect.top,
                    caption, AM2_TEXT_LIST_FONT, vis, ink);
    UnlockSurface();
}

/* The class's own destructor, still original: it restores the vtable, releases
 * both faces and runs the base's. It is the NEXT entry in docs/functions.tsv,
 * past this one's 320 bytes, so it is reached by address rather than dragged
 * into this batch. */
typedef void (__attribute__((thiscall)) *AM2_CountDtorFn)(AM2_Widget *);
#define orig_count_button_dtor \
    ((AM2_CountDtorFn)(uintptr_t)ADDR_COUNT_BUTTON_DTOR)

/* The COUNT BUTTON's own three functions -- constructor, deleting destructor
 * and activate handler. The class already had its paint here; this completes
 * it.
 *
 * NO WIDGET TREE THIS PROJECT CAN DUMP CONTAINS ONE, so all three are verified
 * by reading, exactly as the paint is. orig.h's note on VTABLE_COUNT_BUTTON
 * says the same and says why: two of the eight classes in that address band
 * appear in neither Boot Camp's tree nor MAP 01's.
 *
 * THE CONSTRUCTOR TAKES NINE ARGUMENTS and loads TWO faces from one sprite
 * index -- the caller's frame for the normal one and a fixed
 * AM2_COUNT_FRAME_OFF for the disabled one. The normal face is also stored as
 * the widget's own backdrop, so before the first paint the button already
 * draws something.
 *
 * ITS SEVENTH ARGUMENT IS CARRIED AND NEVER READ. +0x78 is written here and
 * read by nothing below the CRT line -- not the paint, not the activate, and
 * not the three slots this class inherits. Reproduced, because a constructor
 * that dropped it would differ from the original in the one place a debugger
 * would look; named ARG7 rather than given a meaning it has not been shown to
 * have.
 *
 * THE ACTIVATE HANDLER TOGGLES, REPAINTS AND THEN CALLS BACK, in that order,
 * and the repaint goes through the VTABLE rather than to CountButtonPaint
 * directly -- so a derived class would get its own. The callback at
 * COUNTBTN_OFF_ON_TOGGLE is optional and tested for null.
 *
 * The sound is PlaySoundAt with five zeros -- index 0 at the origin -- which
 * is the same call item.cpp already documents as the game's generic click.
 */
AM2_Widget *__attribute__((thiscall))
CountButtonConstruct(AM2_Widget *w, int32_t index, int32_t frame,
                     int32_t x, int32_t y, int32_t cw, int32_t ch,
                     int32_t arg7, int32_t count,
                     void (__cdecl *onToggle)(AM2_Widget *))
{
    uint8_t *self = (uint8_t *)w;

    ButtonBaseConstruct(w);

    w->vtable = (void *)AM2_IMAGE(VTABLE_COUNT_BUTTON);

    *(void **)(self + COUNTBTN_OFF_SPR) =
        PreloadArmySprite(AM2_COUNT_SPRITE_SET, index, frame, 0);
    *(void **)(self + COUNTBTN_OFF_SPR_OFF) =
        PreloadArmySprite(AM2_COUNT_SPRITE_SET, index, AM2_COUNT_FRAME_OFF, 0);

    w->sprite = *(AM2_Sprite **)(self + COUNTBTN_OFF_SPR);
    *(uint8_t *)(self + COUNTBTN_OFF_LIT) = 0;

    w->x = x;
    w->y = y;
    w->w = cw;
    w->h = ch;
    WidgetScreenRect(w);

    *(int32_t *)(self + COUNTBTN_OFF_COUNT) = count;
    *(int32_t *)(self + COUNTBTN_OFF_ARG7)  = arg7;
    w->activate = CountButtonActivate;
    *(void **)(self + COUNTBTN_OFF_ON_TOGGLE) = (void *)onToggle;

    return w;
}

void __cdecl CountButtonActivate(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    void   (*onToggle)(AM2_Widget *);

    *(uint8_t *)(self + COUNTBTN_OFF_LIT) =
        (uint8_t)(*(const uint8_t *)(self + COUNTBTN_OFF_LIT) == 0);

    PlaySoundAt(0, 0, 0, 0, 0);

    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);

    onToggle = *(void (**)(AM2_Widget *))(self + COUNTBTN_OFF_ON_TOGGLE);
    if (onToggle)
        onToggle(w);
}

AM2_Widget *__attribute__((thiscall))
CountButtonDelete(AM2_Widget *w, int32_t flags)
{
    orig_count_button_dtor(w);
    if (flags & 1)
        am2_free(w);
    return w;
}


/* 0x00418DC0, vtable slot 1 of VTABLE_COUNT_BUTTON -- a button that shows a
 * number. Another of the outstanding Lock/Unlock functions.
 *
 * FOUR PATHS, and they set two independent things: which sprite the widget
 * shows, and what colour is filled BEHIND it. Only two of the four fill at
 * all, which is why the fill is gated on its own flag rather than on a colour
 * being non-zero -- zero is a real palette entry.
 *
 *   disabled          the off sprite, ink ADDR_COLOUR_BELOW_BG, no fill
 *   lit               the on sprite, ink ADDR_BACKGROUND_COLOUR, fill
 *                     AM2_COUNT_FILL_LIT and one MORE if it also has focus
 *   focused, not lit  the on sprite, default ink, fill AM2_COUNT_FILL_FOCUS
 *   neither           the on sprite, default ink, no fill
 *
 * THE DEFAULT INK IS NOT DEAD CODE, though it reads as it. The original loads
 * ADDR_VIEW_RECT_COLOUR into the ink slot at the very top and then stores a
 * zero at what looks like the same offset four instructions later -- but a
 * `push edi` sits between them, so those are two different slots. Taken at
 * face value the load looks overwritten and the ink looks read uninitialised
 * on the two paths that never assign it; neither is so. Same shape as the
 * interleaved pops in DrawBlipPulse, one push instead of two.
 *
 * The count is formatted with the game's own sprintf, measured with
 * TextExtent, and drawn RIGHT-aligned: x is `rect.left + 41 - width`, so the
 * digits grow leftward from a fixed edge. Its font is 0, not the 1 every other
 * widget in this family uses.
 */
void __attribute__((thiscall)) CountButtonPaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;
    uint8_t  ink  = *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR;
    uint8_t  fill = 0;
    int32_t  do_fill = 0;
    int32_t  focused;
    RECT     vis;
    char     text[32];
    int32_t  width;

    if (w->disabled) {
        w->sprite = *(AM2_Sprite **)(self + COUNTBTN_OFF_SPR_OFF);
        ink = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG;
    } else {
        w->sprite = *(AM2_Sprite **)(self + COUNTBTN_OFF_SPR);
        focused = w->parent && w->parent->focusedChild == w;

        if (*(const uint8_t *)(self + COUNTBTN_OFF_LIT)) {
            ink     = *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR;
            do_fill = 1;
            fill    = (uint8_t)(AM2_COUNT_FILL_LIT + (focused ? 1 : 0));
        } else if (focused) {
            do_fill = 1;
            fill    = AM2_COUNT_FILL_FOCUS;
        }
    }

    if (!IntersectRect(&vis, &clip, &w->rect))
        return;

    if (do_fill)
        ClearRegion(&vis, fill);

    WidgetPaint(w, clip);

    am2_sprintf(text, (const char *)AM2_IMAGE(ADDR_STR_PCT_D),
                *(const int32_t *)(self + COUNTBTN_OFF_COUNT));
    width = TextExtent(text, 0, NULL);

    if (!LockSurface(g_drawTarget))
        return;

    DrawTextClipped(w->rect.left + AM2_COUNT_CELL_W - width,
                    w->rect.top + AM2_COUNT_TEXT_DY,
                    text, AM2_COUNT_FONT, vis, ink);
    UnlockSurface();
}

/* 0x00432A70, vtable slot 1 of VTABLE_MP_NAME -- one row of the multiplayer
 * player list. Another of the outstanding Lock/Unlock functions.
 *
 * IT NAMES ITSELF. "Not responding%s", "-- Computer --" and "-- Open --" are
 * the three states, and no reading was needed to tell them apart:
 *
 *   occupied and flagged in ADDR_PAUSE_FLAGS -- "Not responding" plus one DOT
 *   per AM2_MP_DOT_MS of silence, white on ADDR_HUD_MESSAGE_COLOUR;
 *   occupied and answering -- the player's name, ink and fill per slot;
 *   empty -- "-- Computer --" or "-- Open --" on AM2_PLAYER_ACTIVE.
 *
 * THE DOT COUNT IS `(elapsed * 6) / 45000`, not `elapsed / 7500`. Those agree
 * for every value that fits, and the original really does multiply first --
 * the magic-number divide it compiles to was checked against 45000 by running
 * the instruction sequence rather than by reading the constant. Written the
 * long way so the arithmetic is the same one.
 *
 * THE PAUSE MASK IS NOT ONLY ABOUT PAUSING. The bit tested is `0x800 << slot`,
 * so the four player slots own bits 11..14 of the same global that
 * PauseGame/UnPauseGame move. CLAUDE.md already says every `if (!pauseFlags)`
 * in the frame chain reads as "not paused"; this says the word covers a
 * dropped connection too.
 *
 * The three helpers below it stay the original's: one writes the ink into the
 * widget and two answer a slot's ink and fill.
 */
void __attribute__((thiscall)) MpNamePaint(AM2_Widget *w, RECT clip)
{
    /* MPNAME_OFF_TEXT IS A POINTER, and orig.h says so in as many words --
     * "const char *, the name shown". The original loads it and hands that to
     * sprintf as the DESTINATION; taking the address of the field instead
     * formats over MPNAME_OFF_FLAG, _INK and _PAPER and everything after them,
     * which killed the process the first time this ran. The field's own
     * comment was the evidence and it was read as `char[]` anyway. */
    uint8_t       *self = (uint8_t *)w;
    const uint8_t *comm = *(const uint8_t *const *)(uintptr_t)ADDR_COMM_OBJECT;
    int32_t        slot = *(const int32_t *)(self + MPBTN_OFF_ROW);
    const uint8_t *rec  = comm + slot * AM2_PLAYER_STRIDE;
    RECT           vis;
    char           dots[64];

    WidgetScreenRect(w);

    if (!IntersectRect(&vis, &clip, &w->rect))
        return;

    /* `test edx,edx; jbe` -- and `test` CLEARS CF, so that jbe is a jz. The
     * condition is `id != 0`, not `id > 0`: a -1 id takes the OCCUPIED branch
     * here even though AM2_PLAYER_ID's own comment calls 0 and -1 both "none".
     * Reproduced as written rather than as the comment reads. */
    if (*(const int32_t *)(rec + AM2_PLAYER_ID) != 0) {
        if (*(const uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS
            & (AM2_MP_PAUSE_BIT0 << slot)) {
            uint32_t now = orig_get_tick_count();
            int32_t  n   = (int32_t)((now * AM2_MP_DOT_NUM
                                      - *(const uint32_t *)(rec + AM2_PLAYER_HEARD)
                                        * AM2_MP_DOT_NUM)
                                     / AM2_MP_DOT_DEN);

            if (n > 0)
                memset(dots, '.', (size_t)n);
            dots[n] = '\0';

            am2_sprintf(*(char **)(self + MPNAME_OFF_TEXT),
                        (const char *)AM2_IMAGE(AM2_STR_NOT_RESPONDING), dots);
            MpNameSetInk(w, *(const uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE);
            *(uint8_t *)(self + MPNAME_OFF_PAPER) =
                *(const uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR;
        } else {
            am2_sprintf(*(char **)(self + MPNAME_OFF_TEXT),
                        (const char *)AM2_IMAGE(AM2_STR_PCT_S),
                        rec + COMM_OFF_PLAYERS + COMM_SLOT_OFF_NAME);
            MpNameSetInk(w, MpNameInk(slot));
            *(uint8_t *)(self + MPNAME_OFF_PAPER) = MpNamePaper(slot);
        }
    } else {
        am2_sprintf(*(char **)(self + MPNAME_OFF_TEXT),
                    (const char *)AM2_IMAGE(
                        *(const int32_t *)(rec + AM2_PLAYER_ACTIVE)
                        ? AM2_STR_COMPUTER_SLOT : AM2_STR_OPEN_SLOT));
        MpNameSetInk(w, *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR);
        *(uint8_t *)(self + MPNAME_OFF_PAPER) =
            *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR;
    }

    ClearRegion(&vis, *(const uint8_t *)(self + MPNAME_OFF_PAPER));

    if (!LockSurface(g_drawTarget))
        return;

    DrawTextClipped(w->rect.left, w->rect.top,
                    *(const char *const *)(self + MPNAME_OFF_TEXT),
                    *(const int32_t *)(self + MPNAME_OFF_FLAG),
                    vis, *(const uint8_t *)(self + MPNAME_OFF_INK));
    UnlockSurface();
}

void __attribute__((thiscall)) HudSargePaint(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    RECT           vis;
    int32_t        i;

    if (!IntersectRect(&vis, &clip, (const RECT *)AM2_IMAGE(ADDR_SCREEN_CLIP)))
        return;

    for (i = 0; i < AM2_HUD_SARGE_ROWS; i++) {
        const int16_t *off = (const int16_t *)AM2_IMAGE(ADDR_HUD_SARGE_OFFSETS)
                             + i * 2;
        const uint8_t *rec = self + HUDSARGE_OFF_SLOTS
                             + i * HUDSARGE_REC_STRIDE;
        int32_t        idx = *(const int32_t *)(rec + HUDSARGE_REC_INDEX);
        AM2_Sprite    *spr;
        AM2_Rect       box, part;
        RECT           hit;
        int32_t        x, y, count;
        char           text[16];

        if (idx < 0)
            continue;

        spr = *(AM2_Sprite *const *)(self + HUD_OFF_SPRITE0 + idx * 4);
        if (!spr)
            continue;

        box.left   = off[0] + w->rect.left;
        box.top    = off[1] + w->rect.top;
        box.right  = spr->bounds.right + box.left;
        box.bottom = spr->bounds.bottom + box.top;

        if (!IntersectRect(&hit, (const RECT *)&box, &vis))
            continue;

        x = box.left;
        y = box.top;

        if (!ClipRect(&spr->bounds, (const AM2_Rect *)&vis, &x, &y, &part))
            continue;

        if (*(const int32_t *)(rec + HUDSARGE_REC_HIGHLIGHT) > 0)
            ClearRegion((const RECT *)&hit, AM2_HUD_CMD_HIGHLIGHT);

        /* The ADJUSTED pair -- 0x00415686 reads back the slots it gave
         * ClipRect. This was `box.left, box.top` and agreed with the original
         * only because a sarge sprite is never partly outside the clip; the
         * squad panel, whose slots do straddle its edge, is where the same
         * mistake finally showed. */
        DrawSpriteClipped(spr, x, y, &part, 0);

        count = *(const int32_t *)(rec + HUDSARGE_REC_COUNT);
        if (count < 0)
            continue;

        if (!LockSurface(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_DRAW_TARGET))
            return;

        ((int32_t (__cdecl *)(char *, const char *, ...))
            AM2_IMAGE(ADDR_GAME_SPRINTF))(
                text, (const char *)AM2_IMAGE(ADDR_STR_PCT_D), count);

        DrawText(AM2_HUD_SARGE_CELL_W
                 - TextExtent(text, AM2_HUD_SARGE_FONT, (int32_t *)0)
                 + box.left,
                 box.top + AM2_HUD_SARGE_TEXT_DY,
                 text, AM2_HUD_SARGE_FONT, 0, AM2_HUD_SARGE_INK);

        UnlockSurface();
    }
}

void __attribute__((thiscall)) HudCommandsPaint(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    RECT           vis;
    int32_t        i;

    if (!IntersectRect(&vis, &clip, (const RECT *)AM2_IMAGE(ADDR_SCREEN_CLIP)))
        return;

    for (i = 0; i < AM2_HUD_CMD_SLOTS; i++) {
        const int16_t *off = (const int16_t *)AM2_IMAGE(ADDR_HUD_CMD_OFFSETS)
                             + i * 2;
        int32_t        slot = *(const int32_t *)(self + HUDCMD_OFF_SLOTS
                                                 + i * 4);
        AM2_Sprite    *spr;
        AM2_Rect       box, part;
        RECT           hit;
        int32_t        x, y;

        if (slot < 0)
            continue;

        spr = *(AM2_Sprite **)((uint8_t *)AM2_IMAGE(ADDR_HUD_CMD_SPRITES)
                               + slot * AM2_POINTER_MODE_SIZE);
        if (!spr)
            continue;

        box.left   = off[0] + w->rect.left;
        box.top    = off[1] + w->rect.top;
        box.right  = spr->bounds.right + box.left;
        box.bottom = spr->bounds.bottom + box.top;

        if (!IntersectRect(&hit, (const RECT *)&box, &vis))
            continue;

        /* Exits rather than continues -- see above. */
        /* CLIPRECT TAKES x AND y AS INPUTS AS WELL AS OUTPUTS, and missing
         * that is what cost four rounds here. It offsets the origin-anchored
         * source by them, clips in SCREEN space against `vis`, and writes back
         * coordinates adjusted for whatever it trimmed. Leaving them
         * uninitialised -- which is what I did -- adds garbage to
         * spr->bounds, puts the sprite nowhere near the clip, and returns 0:
         * the icons never drew at all, in every variant I tried.
         *
         * The original makes this visible by writing the origin into TWO
         * frame slots each (0x34/0x38 and 0x30/0x3c): one copy feeds ClipRect
         * and is modified, the other survives as the draw position. */
        x = box.left;
        y = box.top;

        if (!ClipRect(&spr->bounds, (const AM2_Rect *)&vis, &x, &y, &part))
            return;

        if (i == *(const int32_t *)(self + HUDCMD_OFF_SELECTED))
            ClearRegion((const RECT *)&hit, AM2_HUD_CMD_HIGHLIGHT);

        /* The SAVED origin, not ClipRect's adjusted x/y -- the original
         * pushes frame 0x34 and 0x30, the copies it kept back. */
        DrawSpriteClipped(spr, box.left, box.top, &part, 0);
    }
}

void __attribute__((thiscall)) HudPanelPaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;
    char    *cap;

    WidgetPaint(w, clip);

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        const uint8_t *rec;

        for (rec = (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU)
                   + BUILD_MENU_OFF_RECT;
             rec < (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU_END)
                   + BUILD_MENU_OFF_RECT - AM2_BUILD_MENU_STRIDE;
             rec += AM2_BUILD_MENU_STRIDE) {
            AM2_Rect box;

            RectSet(&box,
                    ((const int32_t *)rec)[0] + w->rect.left,
                    ((const int32_t *)rec)[1] + w->rect.top,
                    ((const int32_t *)rec)[2] + ((const int32_t *)rec)[0]
                        + w->rect.left,
                    ((const int32_t *)rec)[3] + ((const int32_t *)rec)[1]
                        + w->rect.top);

            if (PointInRect(&box,
                            (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT))
                DrawTooltip((const char *)(rec - BUILD_MENU_OFF_RECT
                                           + BUILD_MENU_OFF_NAME),
                            *(const uint8_t *)(uintptr_t)
                                ADDR_BACKGROUND_COLOUR);
        }
    }

    cap = (char *)(self + HUDPANEL_OFF_CAPTION);
    if (*cap) {
        TitleCaseName(cap);
        DrawTooltip(cap, *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR);
    }
}

void __attribute__((thiscall)) HudRadarUpdate(AM2_Widget *w)
{
    const AM2_Widget *panel =
        *(AM2_Widget *const *)(uintptr_t)ADDR_HUD_WIDGET_B;
    AM2_Widget *claim;
    int32_t     changed;

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER
        || *(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    WidgetUpdate(w);

    if (!*(const int32_t *)((const uint8_t *)panel + HUDPANEL_OFF_OPEN))
        return;

    if (!PointInRect((const AM2_Rect *)&w->rect, (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT))
        return;

    claim   = *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB;
    changed = *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED;

    if (!claim && changed) {
        claim = w;
        *(AM2_Widget **)(uintptr_t)ADDR_MOUSE_GRAB = w;
    }

    if ((*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON || changed)
        && claim == w
        && !*(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS) {
        const int16_t *cur = (const int16_t *)(uintptr_t)ADDR_CURSOR_POINT;
        int32_t       *tgt = (int32_t *)(uintptr_t)ADDR_VIEW_TARGET;

        ((int16_t *)tgt)[0] = (int16_t)
            ((cur[0] - w->rect.left)
             * *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X / w->w);
        ((int16_t *)tgt)[1] = (int16_t)
            ((cur[1] - w->y)
             * *(const int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y / w->h);

        *(int32_t *)(uintptr_t)ADDR_VIEW_HOLD    = 1;
        *(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET  = 0;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        - *(const int32_t *)(uintptr_t)ADDR_MOUSE_ACTIVITY > AM2_MOUSE_IDLE_MS)
        strcpy((char *)((uint8_t *)*(AM2_Widget **)
                            (uintptr_t)ADDR_HUD_WIDGET_B
                            + HUDPANEL_OFF_CAPTION),
                   (const char *)AM2_IMAGE(ADDR_STR_STRATMAP));
}

/* 0x0044E510. The film archive's destructor: give back the twelve thumbnail
 * PAIRS and chain to the dialog base.
 *
 * It walks pair[0] then pair[1] with a single pointer stepping eight, which is
 * the layout MOVIES_OFF_SPRITES already describes -- so this confirms that
 * reading from the other end rather than being a second guess at it. Twelve
 * pairs is three pages of four, and the destructor frees all three pages
 * whichever one is showing.
 *
 * The buttons that display them do NOT own them: MakeMovieButton writes
 * BUTTON_OFF_OWNS_SPRITES as 0 for exactly this reason. The screen owns all
 * 24 and this is where they go back. */
void __attribute__((thiscall)) MoviesDestruct(AM2_Widget *w)
{
    AM2_Sprite **pair;
    int32_t      i;

    w->vtable = (void *)AM2_IMAGE(VTABLE_MOVIES);

    pair = (AM2_Sprite **)((uint8_t *)w + MOVIES_OFF_SPRITES);
    for (i = 0; i < AM2_MOVIE_PAGE_SIZE * 3; i++, pair += 2) {
        ReleaseSprite(pair[0]);
        ReleaseSprite(pair[1]);
    }

    DialogDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) MoviesDelete(AM2_Widget *w, int32_t flags)
{
    MoviesDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00455BA0. The arrow scroll bar's destructor: both bitmaps back, then the
 * base widget.
 *
 * ONE SPRITE IS NULL-TESTED AND THE OTHER IS NOT, and that is the original's
 * asymmetry rather than a transcription slip. ReleaseSprite tests for null
 * itself -- ab.sh quit is where that was established -- so both paths are
 * safe and the difference changes no behaviour at all. It is reproduced
 * because which field a writer thought could be null is evidence about the
 * class, and making the two agree would quietly throw it away. */
void __attribute__((thiscall)) ArrowBarDestruct(AM2_Widget *w)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Sprite *spr;

    w->vtable = (void *)AM2_IMAGE(VTABLE_ARROWBAR);

    spr = *(AM2_Sprite **)(self + ARROWBAR_OFF_SPRITE0);
    if (spr)
        ReleaseSprite(spr);

    ReleaseSprite(*(AM2_Sprite **)(self + ARROWBAR_OFF_SPRITE1));

    WidgetDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) ArrowBarDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    ArrowBarDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

/* 0x00453830. The savegame list's destructor: give font 2 back, then the
 * dialog base.
 *
 * The class is named from its CONSTRUCTOR, which is the only place that says
 * what it is -- 0x00453280 sets the data directory to `save`, formats
 * `save\%s` and globs `*.sav`. Nothing in the destructor would have named it,
 * and naming it "the font-2 dialog" from what is in front of you is the
 * mistake this project keeps writing down.
 *
 * It frees a font without having been seen to build one. That is what the code
 * does; the constructor's font handling is not read yet, and a guess about it
 * would be worth less than the gap. */
void __attribute__((thiscall)) SaveListDestruct(AM2_Widget *w)
{
    w->vtable = (void *)AM2_IMAGE(VTABLE_SAVE_LIST);
    FreeFont(AM2_SAVE_LIST_FONT);
    DialogDestruct(w);
}

AM2_Widget *__attribute__((thiscall)) SaveListDelete(AM2_Widget *w,
                                                     int32_t flags)
{
    SaveListDestruct(w);
    if (flags & 1)
        am2_free(w);
    return w;
}

void __attribute__((thiscall)) WidgetRepaintThunk(AM2_Widget *w)
{
    WidgetRepaint(w);
}

void __attribute__((thiscall)) WidgetAddChild(AM2_Widget *w, AM2_Widget *child)
{
    AM2_Widget *last;

    if (!child)
        return;

    child->parent = w;

    last = w->firstChild;
    if (!last) {
        /* No prevSibling written here, and no nextSibling on the new child
         * either -- see the header. */
        w->firstChild = child;
        return;
    }
    while (last->nextSibling)
        last = last->nextSibling;
    last->nextSibling  = child;
    child->prevSibling = last;
}

AM2_Widget *__attribute__((thiscall)) WidgetLastSibling(AM2_Widget *w)
{
    while (w->nextSibling)
        w = w->nextSibling;
    return w;
}

void __attribute__((thiscall)) WidgetFocusNext(AM2_Widget *w, int32_t announce)
{
    AM2_Widget *cand = w->nextSibling;

    if (!cand) {
        AM2_Widget *parent = w->parent;

        if (parent)
            cand = parent->firstChild;
    }

    while (cand) {
        if (cand == w)
            break;                      /* all the way round; give up */
        if (cand->flag50 && !cand->disabled)
            break;                      /* eligible */
        if (cand->nextSibling) {
            cand = cand->nextSibling;
        } else {
            AM2_Widget *parent = cand->parent;

            if (!parent)
                return;
            cand = parent->firstChild;
        }
    }

    if (!cand || cand == w)
        return;
    if (!cand->flag50 || cand->disabled)
        return;
    ((AM2_WidgetFocusFn *)cand->vtable)[WIDGET_VSLOT_FOCUS](cand, announce);
}

void __attribute__((thiscall)) WidgetFocusPrev(AM2_Widget *w, int32_t announce)
{
    AM2_Widget *cand = w->prevSibling;

    if (!cand)
        cand = WidgetLastSibling(w);

    while (cand) {
        if (cand == w)
            break;
        /* 0x004C is NOT consulted here and IS going forwards -- see the
         * header. Reproduced, not reconciled. */
        if (cand->flag50)
            break;
        if (cand->prevSibling)
            cand = cand->prevSibling;
        else
            cand = WidgetLastSibling(cand);
    }

    if (!cand || cand == w)
        return;
    if (!cand->flag50)
        return;
    ((AM2_WidgetFocusFn *)cand->vtable)[WIDGET_VSLOT_FOCUS](cand, announce);
}

void __attribute__((thiscall)) WidgetUpdate(AM2_Widget *w)
{
    AM2_Widget *child;
    AM2_Widget *focus;

    WidgetScreenRect(w);

    for (child = w->firstChild; child; child = child->nextSibling)
        ((AM2_WidgetUpdateFn *)child->vtable)[WIDGET_VSLOT_UPDATE](child);

    /* Only the widget that holds keyboard focus reads the keyboard, and only
     * when it has something focused to act on. */
    if (!w->flag44)
        return;
    if (!w->focusedChild)
        return;

    /* KeyPressed rather than IsKeyDown: that array auto-repeats, so holding a
     * movement key keeps moving. */
    if (KeyPressed(AM2_DIK_UP)) {
        if (w->focusedChild)
            WidgetFocusPrev(w->focusedChild, 1);
        ConsumeKey(AM2_DIK_UP);
    }
    if (KeyPressed(AM2_DIK_DOWN)) {
        if (w->focusedChild)
            WidgetFocusNext(w->focusedChild, 1);
        ConsumeKey(AM2_DIK_DOWN);
    }
    if (KeyPressed(AM2_DIK_TAB)) {
        if (w->focusedChild)
            WidgetFocusNext(w->focusedChild, 1);
        ConsumeKey(AM2_DIK_TAB);
    }

    /* Either activation key CHANGING repaints the focused child, which is how
     * a button shows itself going down and coming back up. No consume here --
     * the release blocks below want to see the same edge. */
    if (KeyChanged(AM2_DIK_SPACE) || KeyChanged(AM2_DIK_RETURN)) {
        focus = w->focusedChild;
        ((AM2_WidgetPaintFn *)focus->vtable)[WIDGET_VSLOT_PAINT](focus,
                                                                 focus->rect);
    }

    if (!IsKeyDown(AM2_DIK_SPACE) && KeyChanged(AM2_DIK_SPACE)) {
        ConsumeKey(AM2_DIK_SPACE);
        focus = w->focusedChild;
        if (focus->activate)
            focus->activate(focus);
    }
    if (!IsKeyDown(AM2_DIK_RETURN) && KeyChanged(AM2_DIK_RETURN)) {
        ConsumeKey(AM2_DIK_RETURN);
        focus = w->focusedChild;
        if (focus->activate)
            focus->activate(focus);
    }
}

/* The cancel handler at 0x0060: cdecl, takes the widget. */
typedef void (__cdecl *AM2_CancelFn)(AM2_Widget *w);
#define WIDGET_OFF_CANCEL 0x60

/* AM2_DIK_ESCAPE now lives in orig.h -- frame.cpp tests it too. */

void __attribute__((thiscall)) WidgetUpdateCancel(AM2_Widget *w)
{
    AM2_CancelFn cancel =
        *(AM2_CancelFn *)((uint8_t *)w + WIDGET_OFF_CANCEL);

    if (cancel
        && !IsKeyDown(AM2_DIK_ESCAPE)
        && KeyChanged(AM2_DIK_ESCAPE)) {
        ConsumeKey(AM2_DIK_ESCAPE);
        cancel(w);
        return;
    }
    WidgetUpdate(w);
}

/* 0x004274D0. Still original: it is two globals and a rep movsd, and the
 * buffers are the input layer's rather than this module's. */
typedef void (__cdecl *AM2_LatchKeysFn)(void);

void __attribute__((thiscall)) WidgetTakeFocus(AM2_Widget *w, int32_t announce)
{
    AM2_Widget *parent = w->parent;
    AM2_Widget *had;

    if (!parent)
        return;

    had = parent->focusedChild;
    if (!had) {
        /* The parent's first child, NOT w -- see the header. */
        parent->focusedChild = parent->firstChild;
    } else {
        if (had == w)
            return;
        parent->focusedChild = w;
        ((AM2_WidgetRepaintFn *)had->vtable)[WIDGET_VSLOT_REPAINT](had);
    }

    w->flag44 = 1;

    PollInput();
    LatchKeyState();

    if (announce) {
        PlaySoundAt(1, 0, 0, 0, 0);
        ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
    }
}

void __attribute__((thiscall)) WidgetRepaint(AM2_Widget *w)
{
    AM2_Widget *target = w;

    /* First and unconditionally, before anything is decided -- which is where
     * the original puts it. Nothing the walk below reads is this field, so the
     * order is unobservable; matching it costs nothing and saves having to
     * have established that. */
    w->flag44 = 0;

    if (w->unknown48) {
        AM2_Widget *up = w->parent;

        /* The nearest ancestor with a sprite -- something with a backdrop
         * to repaint over. */
        while (up) {
            if (up->sprite) {
                target = up;
                break;
            }
            up = up->parent;
        }
    }

    /* One dereference, not two: the struct field IS the table address, so
     * indexing it gives the slot. Writing this as a nested cast off the
     * object is how the movie object's dispatch was got wrong -- see the
     * `obj -> table -> slot` note in CLAUDE.md. */
    ((AM2_WidgetPaintFn *)target->vtable)[WIDGET_VSLOT_PAINT](target, w->rect);
}

/* HudCmdConstruct -- original 0x00417040, one caller, thiscall. Builds the
 * HUD command bar: base-construct, take the vtable, preload seven sprites,
 * then set three empty command slots and the bar's rectangle.
 *
 * THE PRELOAD LOOP CAN ABANDON THE REST OF THE CONSTRUCTOR. A null from
 * PreloadArmySprite jumps straight to the epilogue, so the three slots are
 * never set to -1 and the rectangle is never written -- the widget comes back
 * with whatever WidgetConstruct left. That is an early exit out of the middle
 * of a constructor and it is reproduced; writing the tail unconditionally
 * would be the obvious tidy-up and would change what a failed load returns.
 *
 * The table is walked by its SECOND field: the original starts ESI at
 * 0x004761AC and reads [esi-4], so the record base is 0x004761A8 and the
 * bound 0x004762C4 yields seven. Written from the base with named fields.
 * ADDR_HUD_CMD_SPRITES is this same table reached at its sprite field, 0x0C
 * along, and three other functions walk it that way.
 *
 * The MSVC SEH prologue is not reproduced -- see the note on the widget
 * destructors. Nothing in this program throws.
 *
 * All seven records name sprite set 15; the indices are 0, 0, 1, 2, 3, 4, 1,
 * so two pairs share a sprite rather than there being seven distinct ones. */
AM2_Widget *__attribute__((thiscall)) HudCmdConstruct(AM2_Widget *w)
{
    uint8_t *rec = (uint8_t *)AM2_IMAGE(ADDR_HUD_CMD_SPEC);
    int32_t  i;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_COMMANDS);

    for (i = 0; i < AM2_HUD_CMD_SPRITES; i++, rec += AM2_HUD_CMD_SPR_STRIDE) {
        void *spr = PreloadArmySprite(
            *(const int32_t *)(rec + HUDCMDSPR_OFF_SET),
            *(const int32_t *)(rec + HUDCMDSPR_OFF_INDEX),
            *(const int32_t *)(rec + HUDCMDSPR_OFF_FRAME), 0);
        *(void **)(rec + HUDCMDSPR_OFF_SPRITE) = spr;
        if (!spr)
            return w;                 /* the tail below is SKIPPED */
    }

    ((int32_t *)((uint8_t *)w + HUDCMD_OFF_SLOTS))[0] = -1;
    ((int32_t *)((uint8_t *)w + HUDCMD_OFF_SLOTS))[1] = -1;
    ((int32_t *)((uint8_t *)w + HUDCMD_OFF_SLOTS))[2] = -1;

    w->x = 0;
    w->y = AM2_HUD_CMD_TOP;
    w->w = AM2_HUD_CMD_WIDTH;
    w->h = AM2_HUD_CMD_HEIGHT;
    return w;
}

/* HudEdgeConstruct -- original 0x004195B0, thiscall. The HUD edge strip: the
 * vertical panel pinned to the right of the screen.
 *
 * IT SIZES ITSELF FROM ITS OWN FIRST SPRITE. The width and height are the
 * sprite's bounds.right and bounds.bottom, and x is ADDR_SCREEN_W minus that
 * width, so the strip sits flush against the right edge whatever the sprite
 * turns out to be. VTABLE_HUD_EDGE_STRIP's comment records the result --
 * 624,21,640,480 -- which is the absolute rect this produces and not four
 * constants someone typed.
 *
 * Only y is a literal, 0x15.
 *
 * Like HudCmdConstruct, a null from the FIRST preload abandons the rest: the
 * two later sprites are never loaded and the rectangle is never set. The
 * sprite pointer is stored either way, so the field holds the null.
 *
 * All three come from sprite set 0x0A, indices 0, 0x0B and 0x0A, and all
 * three pass 1 as the fourth argument where HudCmdConstruct passes 0.
 *
 * The first sprite is stored TWICE -- into HUD_OFF_SPRITE0 and into the
 * widget's own AM2_Widget::sprite at 0x38, which WidgetPaint draws as the
 * backdrop. That second store is a real struct field and not a fourth slot,
 * which is why nothing new is named here. */
AM2_Widget *__attribute__((thiscall)) HudEdgeConstruct(AM2_Widget *w)
{
    uint8_t   *self = (uint8_t *)w;
    AM2_Sprite *spr;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_EDGE_STRIP);

    spr = (AM2_Sprite *)PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 0, 0, 1);
    *(void **)(self + HUD_OFF_SPRITE0) = spr;
    if (!spr)
        return w;

    w->sprite = spr;              /* its own backdrop, not a fourth slot */
    w->y = AM2_HUD_EDGE_TOP;
    w->x = *(const int32_t *)(uintptr_t)ADDR_SCREEN_W - spr->bounds.right;
    w->w = spr->bounds.right;
    w->h = spr->bounds.bottom;

    *(void **)(self + HUD_OFF_SPRITE1) =
        PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 0x0B, 0, 1);
    *(void **)(self + HUD_OFF_SPRITE2) =
        PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 0x0A, 0, 1);
    return w;
}

AM2_Widget *__attribute__((thiscall)) WidgetConstruct(AM2_Widget *w)
{
    AM2_Rect zero;

    w->vtable = (void *)AM2_IMAGE(VTABLE_WIDGET_BASE);

    /* Both rectangles are built in a temporary and copied, which is what a
     * `m_rect = Rect(0,0,0,0)` compiles to when Rect returns by value -- the
     * original calls RectSet twice into stack slots rather than writing the
     * eight zeroes directly. Kept in that shape; RectSet is ours. */
    RectSet(&zero, 0, 0, 0, 0);
    w->x = zero.left;
    w->y = zero.top;
    w->w = zero.right;
    w->h = zero.bottom;

    RectSet(&zero, 0, 0, 0, 0);
    w->rect.left   = zero.left;
    w->rect.top    = zero.top;
    w->rect.right  = zero.right;
    w->rect.bottom = zero.bottom;

    w->firstChild  = (AM2_Widget *)0;
    w->parent      = (AM2_Widget *)0;
    w->prevSibling = (AM2_Widget *)0;
    w->nextSibling = (AM2_Widget *)0;
    w->focusedChild = (AM2_Widget *)0;
    w->sprite      = 0;
    w->flag3C      = 1;
    /* 0x0040 is deliberately not written -- the original leaves it, and
     * whatever it is arrives from the allocator. */
    w->flag44      = 0;
    w->unknown48   = 0;
    w->disabled   = 0;
    w->flag50      = 1;
    w->activate    = 0;
    return w;
}

AM2_Widget *__attribute__((thiscall)) LabelConstruct(AM2_Widget *w,
                                                     const char *text,
                                                     int32_t x, int32_t y,
                                                     int32_t width,
                                                     int32_t height,
                                                     int32_t font,
                                                     int32_t ink,
                                                     int32_t paper)
{
    uint8_t *self = (uint8_t *)w;

    WidgetConstruct(w);

    *(const char **)(self + LABEL_OFF_TEXT)  = text;
    *(int32_t *)(self + LABEL_OFF_FONT)      = font;
    *(uint8_t *)(self + LABEL_OFF_INK)       = (uint8_t)ink;
    *(uint8_t *)(self + LABEL_OFF_PAPER)     = (uint8_t)paper;

    w->vtable = (void *)AM2_IMAGE(VTABLE_LABEL);
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;

    WidgetScreenRect(w);
    return w;
}

void __attribute__((thiscall)) WidgetScreenRect(AM2_Widget *w)
{
    const AM2_Widget *parent = w->parent;
    int32_t           left;
    int32_t           top;

    if (parent) {
        left = parent->rect.left + w->x;
        top  = parent->rect.top  + w->y;
    } else {
        left = w->x;
        top  = w->y;
    }
    w->rect.left   = left;
    w->rect.right  = left + w->w;
    w->rect.top    = top;
    w->rect.bottom = top  + w->h;
}

void __attribute__((thiscall)) LabelDraw(AM2_Widget *w, RECT clip)
{
    const uint8_t *self = (const uint8_t *)w;
    RECT           paint;

    WidgetScreenRect(w);

    /* IntersectRect answers zero for an empty result, which is the whole
     * off-screen test: a label scrolled out of its container paints nothing
     * and does not lock. */
    if (!IntersectRect(&paint, &clip, &w->rect))
        return;

    ClearRegion(&paint, *(const uint8_t *)(self + LABEL_OFF_PAPER));

    if (!LockSurface(g_drawTarget))
        return;

    DrawTextClipped(w->rect.left, w->rect.top,
                           *(const char *const *)(self + LABEL_OFF_TEXT),
                           *(const int32_t *)(self + LABEL_OFF_FONT),
                           paint,
                           *(const uint8_t *)(self + LABEL_OFF_INK));

    UnlockSurface();
}

void *__attribute__((thiscall)) RecordCtor(void *rec, int32_t value)
{
    int32_t *r = (int32_t *)rec;

    r[0] = 0;
    r[1] = 0;
    r[2] = value;
    return rec;
}

void __attribute__((thiscall)) RecordResetAlias(void *rec)
{
    RecordReset(rec);
}

int32_t __cdecl KeyNameIndexOf(uint8_t scancode)
{
    const uint8_t *rec = (const uint8_t *)(uintptr_t)ADDR_KEY_NAME_TABLE;
    int32_t        i   = 0;

    for (; rec < (const uint8_t *)(uintptr_t)ADDR_KEY_NAME_TABLE_END;
         rec += 8, i++)
        if (*rec == scancode)
            return i;
    return -1;
}

void __attribute__((thiscall)) ListAdd(void *list, const char *name,
                                       void *value)
{
    int32_t  *count = (int32_t *)list;
    uint8_t **base  = (uint8_t **)((uint8_t *)list + 4);
    uint8_t  *row;

    *base = (uint8_t *)am2_realloc(*base,
                                   (size_t)(*count + 1) * AM2_LIST_ROW_STRIDE);
    /* The count is re-read after the realloc, as the original does, and the
     * destination is computed from the NEW base. */
    row = *base + (size_t)*count * AM2_LIST_ROW_STRIDE;
    strcpy((char *)row, name);
    *(void **)(row + AM2_LIST_ROW_VALUE) = value;
    *count = *count + 1;
}

/* 0x004539A0, the other end of ListAdd: drop the OLDEST row.
 *
 * It does not memmove in place and it does not realloc. It allocates a fresh
 * array of the NEW size, copies rows 1..n into it, frees the old one and
 * swaps -- so the array is rebuilt on every trim, which for the menu message
 * log means once per line above a hundred.
 *
 * The count is decremented FIRST, and everything after it is computed from
 * the new count. That is what makes the loop copy `count` rows starting at
 * row 1 rather than `count - 1`.
 *
 * A list that owns its values frees the discarded row's pointer. When the
 * count reaches zero the allocation still happens, with a size of zero, and
 * the pointer it returns is stored -- reproduced rather than special-cased,
 * because a caller that then appends expects a block it can realloc. */
void __attribute__((thiscall)) ListDropOldest(void *list)
{
    int32_t  *count = (int32_t *)list;
    uint8_t **base  = (uint8_t **)((uint8_t *)list + 4);
    uint8_t  *fresh;
    int32_t   i;

    *count = *count - 1;

    if (*(const int32_t *)((const uint8_t *)list + AM2_LIST_OWNS_VALUES)) {
        void *owned = *(void **)(*base + AM2_LIST_ROW_VALUE);

        if (owned)
            am2_free(owned);
    }

    fresh = (uint8_t *)am2_malloc((size_t)*count * AM2_LIST_ROW_STRIDE);

    /* The count is re-read each time round, as the original does. */
    for (i = 0; i < *count; i++)
        memcpy(fresh + (size_t)i * AM2_LIST_ROW_STRIDE,
               *base + (size_t)(i + 1) * AM2_LIST_ROW_STRIDE,
               AM2_LIST_ROW_STRIDE);

    am2_free(*base);
    *base = fresh;
}

/* The OPTIONS dialog, declared by the table at ADDR_OPTION_TABLE. Both the
 * load and the apply walk it with a cursor 0x18 bytes INTO each record, which
 * is why the original reads the bit and the mask choice at +0 and +4 and the
 * widget index at -0x18; written out here from the record base instead. */

#define g_gameOverFlags   (*(uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS)
#define g_gameSetting22C  (*(uint32_t *)(uintptr_t)ADDR_GAME_SETTING_22C)
#define g_menuRequest     (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST)
#define g_defaultOwner    (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_levelCount      (*(const int32_t *)(uintptr_t)ADDR_LEVEL_TABLE_COUNT)
#define g_moviePage       (*(int32_t *)(uintptr_t)ADDR_MOVIE_PAGE)
#define g_movieCount      (*(int32_t *)(uintptr_t)ADDR_MOVIE_COUNT)
#define g_subState      (*(int32_t *)(uintptr_t)ADDR_MENU_MODE)
#define g_overlayDirty    (*(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)
typedef void (__cdecl *am2_void_fn)(void);
#define g_menuRequestSet  (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)
#define g_commObject      (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)

typedef void (__cdecl *AM2_SendPlayersFn)(int32_t which);

/* The checkbox a record names, reached through the header's parent -- the
 * dialog holds them in an array at 0x0064, the same shape as the CONTROLS
 * dialog's key rows. */
static AM2_Widget *OptionBox(AM2_Widget *parent, int32_t index)
{
    return *(AM2_Widget **)((uint8_t *)parent + OPTION_PARENT_BOXES
                            + index * 4);
}

void __cdecl OptionsDefaults(AM2_Widget *button)
{
    AM2_Widget    *parent = button->parent;
    const uint8_t *rec    = (const uint8_t *)(uintptr_t)ADDR_OPTION_TABLE;
    uint32_t       flags;
    uint32_t       other;

    ResetPairMask(&flags, &other);

    do {
        AM2_Widget *box =
            OptionBox(parent, *(const int32_t *)(rec + AM2_OPTION_OFF_WIDGET));
        uint32_t    bit = *(const uint32_t *)(rec + AM2_OPTION_OFF_BIT);
        uint32_t    mask = *(const int32_t *)(rec + AM2_OPTION_OFF_WHICH)
                           ? flags : other;

        box->disabled = 0;
        *((uint8_t *)box + CHECK_OFF_TICKED) = (mask & bit) != 0;
        ((AM2_WidgetPaintFn *)box->vtable)[WIDGET_VSLOT_PAINT](box, box->rect);
        rec += AM2_OPTION_STRIDE;
    } while (rec < (const uint8_t *)(uintptr_t)ADDR_OPTION_TABLE_END);
}

void __cdecl OptionsApply(AM2_Widget *button)
{
    AM2_Widget    *parent = button->parent;
    const uint8_t *rec    = (const uint8_t *)(uintptr_t)ADDR_OPTION_TABLE;
    uint32_t       flags  = 0;
    uint32_t       other  = 0;

    do {
        AM2_Widget *box =
            OptionBox(parent, *(const int32_t *)(rec + AM2_OPTION_OFF_WIDGET));

        if (*((const uint8_t *)box + CHECK_OFF_TICKED)) {
            uint32_t bit = *(const uint32_t *)(rec + AM2_OPTION_OFF_BIT);

            if (*(const int32_t *)(rec + AM2_OPTION_OFF_WHICH))
                flags |= bit;
            else
                other |= bit;
        }
        rec += AM2_OPTION_STRIDE;
    } while (rec < (const uint8_t *)(uintptr_t)ADDR_OPTION_TABLE_END);

    g_gameOverFlags  = flags;
    g_gameSetting22C = other;

    PlaySoundAt(2, 0, 0, 0, 0);
    g_menuRequest    = AM2_MENU_REQUEST_OPTIONS;
    g_menuRequestSet = 1;
    SendPlayerMsg(0);
    Announce("Options changed by host.");
}

void __cdecl OptionsRequest(void)
{
    PlaySoundAt(2, 0, 0, 0, 0);
    g_menuRequestSet = 1;
    g_menuRequest = *(const int32_t *)(g_commObject + COMM_OFF_IS_HOST)
                    ? AM2_MENU_REQUEST_OPTIONS
                    : AM2_MENU_REQUEST_OPTIONS_VIEW;
}

void __cdecl OptionsSyncGroup(AM2_Widget *header)
{
    AM2_Widget    *parent = header->parent;
    const uint8_t *rec    = (const uint8_t *)(uintptr_t)ADDR_OPTION_TABLE
                            + *(const int32_t *)((uint8_t *)header
                                                 + CHECK_OFF_GROUP)
                              * AM2_OPTION_STRIDE;
    int32_t        i;

    if (!*(const int32_t *)(rec + AM2_OPTION_OFF_GROUP))
        return;

    for (i = *(const int32_t *)(rec + AM2_OPTION_OFF_FIRST);
         i <= *(const int32_t *)(rec + AM2_OPTION_OFF_LAST);
         i++) {
        AM2_Widget *box    = OptionBox(parent, i);
        uint8_t     ticked = *((const uint8_t *)header + CHECK_OFF_TICKED);

        *((uint8_t *)box + CHECK_OFF_TICKED) = ticked;
        box->disabled = (ticked == 0);
        ((AM2_WidgetPaintFn *)box->vtable)[WIDGET_VSLOT_PAINT](box, box->rect);
    }
}

void __attribute__((thiscall)) MpDialogDestruct(AM2_Widget *w)
{
    DialogDestruct(w);
}

void __attribute__((thiscall)) OptionsUpdate(AM2_Widget *w)
{
    WidgetUpdateCancel(w);
}

/* ---- the menu screen factories ----------------------------------------- *
 *
 * RunFrame's menu-request handler dispatches through a 21-entry jump table at
 * 0x00426518; each arm is seven bytes -- `call <factory>; jmp end` -- and each
 * factory opens one screen. They are one SHAPE repeated: destroy whatever
 * dialog is currently the repaint object, allocate, construct on the
 * allocation, and store the CONSTRUCTOR'S RETURN.
 *
 * That last step is the load-bearing one, and it is the RecordCtor lesson
 * twenty-one times over: `mov [0x0065A058], eax` after the call. A
 * reconstruction of any of these constructors that returns void puts the
 * allocation's uninitialised idea of itself on screen -- or worse.
 *
 * They live here rather than in a screens.cpp of their own because they
 * operate on the widget tree and want its real types. A separate module names
 * no platform type at all, which tools/checksplit.py correctly refuses, and
 * the alternative -- a flat module with the vtable call written against
 * `void **` -- is exactly the private signature CLAUDE.md warns about.
 *
 * The MSVC SEH prologue each of them carries is not reproduced; see CLAUDE.md.
 */

/* 0x0065A058 under the spelling startgame.cpp already uses. winproc.cpp calls
 * the same address g_paintObject through an AM2_PaintObject *, which is the
 * same object seen through its other three vtable slots; a third spelling
 * would be a third thing for tools/checkglobals.py to report. */
#define g_paintObject (*(uint8_t **)(uintptr_t)ADDR_PAINT_OBJECT)
#define g_mpSession   (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)
/* Spelled exactly as surface.cpp and frame.cpp spell them -- checkglobals
 * refused a second name on either address, which is the rule working. */
#define g_primarySurface (*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_PRIMARY_SURFACE)
#define g_currentBitmap  (*(void **)(uintptr_t)ADDR_CURRENT_BITMAP)

typedef void *(__cdecl *AM2_OperatorNewFn)(uint32_t size);
typedef void *(__attribute__((thiscall)) *AM2_ScreenCtorFn)(void *obj,
                                                            const char *bmp);
typedef void (__cdecl *AM2_VoidFn)(void);

#define orig_operator_new     ((AM2_OperatorNewFn)AM2_IMAGE(ADDR_GAME_OPERATOR_NEW))

/* The build screen's START button. Read out of the image rather than guessed
 * -- "done" was the obvious name for a button at the bottom of a build menu
 * and it is not what the strings say. */
static const char *const kBuildStartSprites[3] = {
    "03_016_00_start.bmp", "03_016_01_start.bmp", "03_016_02_start.bmp",
};

/* Make a child of `size`, construct it with `ctor`, and hand it to the
 * parent -- the shape the panel repeats six times. A failed allocation adds
 * a NULL child rather than skipping the call, which is the original's. */
static AM2_Widget *HudAddChild(AM2_Widget *parent, uint32_t size,
                               AM2_Widget *(__attribute__((thiscall)) *ctor)(AM2_Widget *))
{
    AM2_Widget *c = (AM2_Widget *)orig_operator_new(size);

    c = c ? ctor(c) : (AM2_Widget *)0;
    WidgetAddChild(parent, c);
    return c;
}

/* HudPanelConstruct -- original 0x00418FB0, 905 bytes, thiscall. The HUD
 * panel: the parent every other HUD widget hangs off, and the largest of the
 * six constructors.
 *
 * ONE FLAG SPLITS IT COMPLETELY. In single player it adds the radar, the
 * Sarge panel, the squad grid and the command bar; in a network game it adds
 * the radar and then EIGHTEEN build buttons, a text edit and a button. The
 * two halves share only the radar.
 *
 * The same flag also picks the backdrop's frame and shifts the whole panel:
 * x is ADDR_SCREEN_W minus the sprite's width, and then 0x10 further left in
 * SINGLE player -- the inset is on the non-network path, which reads
 * backwards from the compare and is why it is spelled out here.
 *
 * The x it settles on is published to ADDR_HUD_PANEL_X and kept in
 * HUDPANEL_OFF_STOP, which is the position the panel slides open to.
 *
 * THE BUILD LOOP PAIRS EACH RECORD WITH THE PREVIOUS RECORD'S RECTANGLE --
 * `base + i*0x38 - 0x10` -- so Rifleman takes the standalone rect at
 * ADDR_BUILD_MENU_RECTS and every later button takes its predecessor's
 * +0x28. See that macro; the layout note there was wrong about which button
 * owned which rectangle until this loop was read.
 *
 * IT NULL-DEREFERENCES ON A FAILED ALLOCATION, in three places, and all
 * three are reproduced. When operator new returns null the pointer is zeroed
 * and then written through -- [0]+0x4C for a build button that cannot be
 * afforded, [0]+0x4C for the edit, [0]+0x44 for the button. The original
 * crashes there and so does this; the alternative is inventing a guard the
 * game does not have. */
AM2_Widget *__attribute__((thiscall)) HudPanelConstruct(AM2_Widget *w)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Sprite *spr;
    int32_t     net = *(const int32_t *)(uintptr_t)ADDR_NET_GAME;
    int32_t     x;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_PANEL);

    spr = (AM2_Sprite *)PreloadArmySprite(0x0B, 0, net != 0 ? 1 : 0, 1);
    *(void **)(self + HUD_OFF_SPRITE0) = spr;
    if (!spr)
        return w;
    w->sprite = spr;

    x = *(const int32_t *)(uintptr_t)ADDR_SCREEN_W - spr->bounds.right;
    if (net == 0)
        x -= AM2_HUD_PANEL_SP_INSET;   /* the inset is SINGLE player's */
    w->x = x;
    w->y = AM2_HUD_EDGE_TOP;
    w->w = spr->bounds.right;
    w->h = spr->bounds.bottom;
    *(int32_t *)(self + HUDPANEL_OFF_STOP) = x;
    *(int32_t *)(self + HUDPANEL_OFF_OPEN) = 1;
    *(int32_t *)(uintptr_t)ADDR_HUD_PANEL_X = x;

    HudAddChild(w, AM2_HUD_RADAR_BYTES, HudRadarConstruct);

    if (net == 0) {
        HudAddChild(w, AM2_HUD_SARGE_BYTES, HudSargeConstruct);
        HudAddChild(w, AM2_HUD_SQUAD_BYTES, HudSquadConstruct);
        HudAddChild(w, AM2_HUD_CMD_BYTES,   HudCmdConstruct);
        return w;
    }

    /* The network half, inline rather than factored: it is one contiguous
     * block in the original and a helper would put a seam where the two
     * null-deref paths live. */
    {
        const uint8_t *rec  = (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU);
        const uint8_t *rect = (const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU_RECTS);
        AM2_Widget   **slot = (AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_TABLE;
        int32_t        army = CommArmyOfSlot(
                                  *(void **)(uintptr_t)ADDR_COMM_OBJECT,
                                  (int32_t)g_defaultOwner);
        AM2_Widget    *btn;
        char          *points;
        int32_t        i;

        for (i = 0; i < AM2_BUILD_MENU_COUNT; i++) {
            int32_t id   = *(const int32_t *)(rec + BUILD_MENU_OFF_ID);
            int32_t kind = *(const int32_t *)(rec + BUILD_MENU_OFF_KIND);
            const int32_t *r = (const int32_t *)rect;

            btn = (AM2_Widget *)orig_operator_new(AM2_COUNT_BUTTON_BYTES);
            if (btn)
                btn = CountButtonConstruct(btn, kind, i,
                                           r[0], r[1], r[2], r[3],
                                           army, (int32_t)UnitTypeCost(id),
                                           (void (__cdecl *)(AM2_Widget *))
                                               AM2_IMAGE(ADDR_BUILD_ON_TOGGLE));
            WidgetAddChild(w, btn);
            *slot = btn;

            /* Writes through btn even when it is null -- the original's. */
            if (!CanAffordUnit(id, *(const int32_t *)(uintptr_t)ADDR_OUR_POINTS))
                *(int32_t *)((uint8_t *)btn + COUNTBTN_OFF_DISABLED) = 1;

            rec  += AM2_BUILD_MENU_STRIDE;
            rect += AM2_BUILD_MENU_STRIDE;
            slot++;
        }

        /* The points readout, formatted into the panel's own buffer. */
        points = (char *)(self + HUDPANEL_OFF_POINTS_TEXT);
        am2_sprintf(points, "%d", *(const int32_t *)(uintptr_t)ADDR_OUR_POINTS);

        btn = (AM2_Widget *)orig_operator_new(AM2_HUD_EDIT_BYTES);
        if (btn) {
            /* The edit takes its rectangle as FOUR ints, not a struct. The
             * original still routes them through RectSet and copies the
             * result back over the same four pushes, which is why the values
             * appear twice in the disassembly. */
            /* Both already named for other users -- the view rect's colour
             * and the background's. Reused rather than given a third and
             * fourth spelling; checkpatches refused the aliases. */
            int32_t ink   = *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR;
            int32_t paper = *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR;

            btn = EditConstruct(btn, points, 0x0C,
                                0x5A, 0x98, 0x26, 0x0E,
                                1, ink, ink, paper, 0, 0, 0);
        }
        *(void **)(self + HUDPANEL_OFF_POINTS_FIELD) = btn;
        *(int32_t *)((uint8_t *)btn + COUNTBTN_OFF_DISABLED) = 1;
        WidgetAddChild(w, *(AM2_Widget **)(self + HUDPANEL_OFF_POINTS_FIELD));

        btn = (AM2_Widget *)orig_operator_new(AM2_HUD_BUTTON_BYTES);
        if (btn) {
            AM2_Rect box;
            RectSet(&box, 0x1F, 0x1A6, 0x51, 0x20);
            btn = ButtonConstruct(btn, kBuildStartSprites[0], kBuildStartSprites[1],
                                  kBuildStartSprites[2], 1, box,
                                  (void (__cdecl *)(AM2_Widget *))
                                      AM2_IMAGE(ADDR_BUILD_START_HANDLER),
                                  (void (__cdecl *)(AM2_Widget *))0);
        }
        WidgetAddChild(w, btn);
        w->focusedChild = btn;
        *(int32_t *)((uint8_t *)btn + BUTTON_OFF_FLAG44) = 1;
    }
    return w;
}

/* HudSquadConstruct -- original 0x00415730, 247 bytes, thiscall. The squad
 * panel: twelve portrait slots, each with a lo and a hi sprite, and twelve
 * records the painter reads.
 *
 * THE TWELVE SPRITE INDICES ARE NOT 0..11. Two loops supply 1..7 and then
 * 10..14, so 8 and 9 are skipped -- the second loop's bound is written as
 * `esi - 3 < 0x0C`, which is what makes the gap easy to miss and impossible
 * to guess. Written as one loop over an index that steps over the gap, since
 * both halves are otherwise identical and the original's two bodies differ
 * only in their start and bound.
 *
 * The original walks BOTH arrays with a single cursor that reaches back 0x30
 * for the lo slot -- the "0x30 apart, walked together" HUD_SQUAD_PAIR_LO's
 * note already records -- and the second loop simply continues the cursor
 * where the first left it, at +0xA8. That continuity is why they are one
 * array of twelve and not two arrays of seven and five.
 *
 * The lo sprite comes from set 0x0E with flag 0 and the hi from set 0x11 with
 * flag 1; nothing is null-tested, like HudSargeConstruct and unlike the other
 * three.
 *
 * y and h are THE SAME CONSTANT, 0xE6, written from one register. */
AM2_Widget *__attribute__((thiscall)) HudSquadConstruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  i;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_SQUAD);

    /* Its own pip, and NOT from either pair set: 0x0A index 0x14. */
    *(void **)(self + HUD_SQUAD_ICON_SPRITE) =
        PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, AM2_HUD_SQUAD_ICON_INDEX,
                          0, 0);

    for (i = 0; i < AM2_HUD_SQUAD_SLOTS; i++) {
        /* 1..7, then 10..14 -- the original's two loops, one index. */
        int32_t idx = (i < AM2_HUD_SQUAD_SPLIT)
                    ? i + 1
                    : i + 1 + AM2_HUD_SQUAD_GAP;

        ((void **)(self + HUD_SQUAD_PAIR_LO))[i] =
            PreloadArmySprite(AM2_HUD_SQUAD_SET_LO, idx, 0, 0);
        ((void **)(self + HUD_SQUAD_PAIR_HI))[i] =
            PreloadArmySprite(AM2_HUD_SQUAD_SET_HI, idx, 0, 1);
    }

    for (i = 0; i < AM2_HUD_SQUAD_SLOTS; i++)
        *(int32_t *)(self + HUD_SQUAD_RECS
                     + (size_t)i * HUD_SQUAD_REC_SIZE + SQUAD_REC_INDEX) = -1;

    w->x = 0;
    w->y = AM2_HUD_SQUAD_TOP;
    w->w = AM2_HUD_SQUAD_WIDTH;
    w->h = AM2_HUD_SQUAD_TOP;      /* the same constant, one register */
    return w;
}

/* The radar's ten palette indices, in the order the constructor writes them:
 * five light/dark pairs indexed by RadarBlipColour's answer. Generated from
 * the destinations rather than the instruction order -- the original
 * interleaves the copies so no two adjacent ones belong to the same pair. */
static const uint32_t kRadarColourSrc[AM2_RADAR_COLOUR_PAIRS * 2] = {
    ADDR_COLOUR_LIGHT_GREEN, ADDR_COLOUR_DARK_GREEN,
    ADDR_COLOUR_CREAM,       ADDR_COLOUR_OLIVE,
    ADDR_COLOUR_LIGHT_BLUE,  ADDR_COLOUR_STEEL_BLUE,
    ADDR_COLOUR_LIGHT_GREY,  ADDR_COLOUR_DARK_GREY,
    ADDR_COLOUR_WHITE_B,     ADDR_COLOUR_BLACK,
};

/* HudRadarConstruct -- original 0x00414700, 259 bytes, thiscall. The radar
 * panel: load the map's own bitmap, size to it, and gather the blip palette.
 *
 * The bitmap is the MAP's. It chdirs to ADDR_MAP_FOLDER and builds
 * "<ADDR_MAP_NAME>.bmp", so every mission supplies its own minimap image and
 * the widget's width and height come from whatever that turns out to be.
 *
 * NO NULL TEST ON THE BITMAP. LoadBitmap's result goes into two fields and
 * is then dereferenced for its bounds without a check, unlike the three
 * preload-based HUD constructors which all guard. A missing map bitmap
 * faults here, and that is the original's behaviour.
 *
 * The ten colour copies are the interesting part and they are INTERLEAVED --
 * the compiler emits them so that no two adjacent instructions belong to one
 * destination pair, which hides the structure completely. Sorted by
 * destination they are five light/dark pairs: the four army colours with a
 * shade each, then white/black for the index 4 that a multiplayer soldier
 * kind 7 produces. See ADDR_RADAR_COLOURS. */
AM2_Widget *__attribute__((thiscall)) HudRadarConstruct(AM2_Widget *w)
{
    uint8_t    *self = (uint8_t *)w;
    uint8_t    *dst  = (uint8_t *)(uintptr_t)ADDR_RADAR_COLOURS;
    AM2_Sprite *bmp;
    char        name[AM2_RADAR_NAME_BYTES];
    int32_t     i;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_RADAR);

    SetGameDir((const char *)(uintptr_t)ADDR_MAP_FOLDER);
    am2_sprintf(name, "%s.bmp", (const char *)(uintptr_t)ADDR_MAP_NAME);

    bmp = (AM2_Sprite *)LoadBitmap(name, 1);
    *(void **)(self + HUD_OFF_SPRITE0) = bmp;
    w->sprite = bmp;

    w->x = AM2_HUD_RADAR_LEFT;
    w->y = AM2_HUD_RADAR_TOP;
    w->w = bmp->bounds.right;     /* no null test -- the original's */
    w->h = bmp->bounds.bottom;

    for (i = 0; i < AM2_RADAR_COLOUR_PAIRS * 2; i++)
        dst[i] = *(const uint8_t *)(uintptr_t)kRadarColourSrc[i];

    return w;
}

/* HudSargeConstruct -- original 0x00414DF0, 157 bytes, thiscall. The Sarge
 * panel: 31 portrait sprites and six selection slots.
 *
 * IT HAS NO EARLY EXIT, which is the whole reason it is worth saying so. The
 * other three HUD constructors each abandon the rest of their body on a null
 * preload; this one stores all 31 results unconditionally and never looks at
 * any of them. A family habit is only a habit once you have checked the
 * member that does not share it.
 *
 * The rectangle is PARENT-RELATIVE. VTABLE_HUD_SARGE's comment records the
 * absolute 480,169,624,249, and 624-480 is 0x90 with 249-169 is 0x50 -- the
 * width and height written here -- so the two agree and the parent is at
 * (480, 21), which is the top strip's y. Two notes written for different
 * functions lining up is the check that neither is wrong.
 *
 * ArmySpriteBase is called for its side effect and its answer discarded; the
 * original does not test it either. */
AM2_Widget *__attribute__((thiscall)) HudSargeConstruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    int32_t  i;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_SARGE);

    ArmySpriteBase();

    for (i = 0; i < AM2_HUD_SARGE_SLOTS; i++)
        ((void **)(self + HUDSARGE_OFF_SPRITES))[i] =
            PreloadArmySprite(AM2_HUD_SARGE_SPRITE_SET, i, 0, 0);

    for (i = 0; i < AM2_HUD_SARGE_SLOT_COUNT; i++)
        *(int32_t *)(self + HUDSARGE_OFF_SLOTS
                     + (size_t)i * AM2_HUD_SARGE_SLOT_SIZE) = -1;

    w->x = AM2_HUD_SARGE_LEFT;
    w->y = AM2_HUD_SARGE_TOP;
    w->w = AM2_HUD_SARGE_WIDTH;
    w->h = AM2_HUD_SARGE_HEIGHT;
    return w;
}

static const char *const kChatToSprites[4] = {
    "18_001_00_chatto.bmp", "18_001_01_chatto.bmp",
    "18_001_02_chatto.bmp", "18_001_03_chatto.bmp",
};

/* HudTopConstruct -- original 0x00417580, 490 bytes, thiscall. The HUD top
 * strip, which IS the chat log -- the tree already spells the class HudTop
 * (Destruct, Paint, Update) while spelling its fields HUDLOG_, and both are
 * this one widget. Named HudTop to match the three methods rather than the
 * offsets, since a class has one name. Named from its destructor
 * (0x00417770 tail-calls ADDR_HUD_TOP_DESTRUCT) and confirmed by its own
 * data: the checkbox it may add loads 18_001_0N_chatto.bmp.
 *
 * MULTIPLAYER CHANGES ITS SHAPE TWICE, from one flag read at the top and
 * again at the bottom. ADDR_NET_GAME picks the base sprite's FRAME (0 or 1)
 * and the button's x (2 or 0x10), and the typing cursor and the scroll width
 * are both derived from that x rather than being constants -- TYPED_X is
 * x + 0x19 and VIEW_W is 0x258 - x. So the widget is narrower and indented in
 * a network game, and one global decides it.
 *
 * THREE PRELOADS, THREE EARLY EXITS. Each stores its result and then bails to
 * the epilogue on null, so a failure part-way leaves the later fields
 * untouched -- the same mid-body exit HudCmdConstruct and HudEdgeConstruct
 * have. Third instance; it is the family's habit, not an accident.
 *
 * The row clear walks a CURSOR at ROWS + 0x50 and reaches BACK 0x50 for the
 * text byte, so a naive read puts the array at 0xBC. It is at
 * HUDLOG_OFF_ROWS, 0x6C, and the tree already had that name -- the arithmetic
 * agreeing with a macro written for another function is the check.
 *
 * The checkbox is added only in a network game AND only when the local
 * player's slot has a team, which is the same COMM_SLOT_OFF_TEAM test
 * CommTeamScore makes. Otherwise the child pointer is explicitly nulled --
 * not left, nulled, on a path of its own. */
AM2_Widget *__attribute__((thiscall)) HudTopConstruct(AM2_Widget *w)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Sprite *spr;
    int32_t     frame;
    int32_t     buttonX;
    int32_t     i;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_HUD_TOP_STRIP);

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME == 0) {
        frame = 0;
        buttonX = 2;
    } else {
        frame = 1;
        buttonX = 0x10;
    }
    *(int32_t *)(self + HUDLOG_OFF_BUTTON_X) = buttonX;

    spr = (AM2_Sprite *)PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 1, frame, 0);
    *(void **)(self + HUD_OFF_SPRITE0) = spr;
    if (!spr)
        return w;

    w->sprite = spr;

    *(int32_t *)(self + HUDLOG_OFF_TYPED_Y)  = 3;
    *(int32_t *)(self + HUDLOG_OFF_TYPED_X)  = buttonX + 0x19;
    *(int32_t *)(self + HUDLOG_OFF_VIEW_W)   = 0x258 - buttonX;
    *(int32_t *)(self + HUDLOG_OFF_FIELD_5B4) = 0x0A;
    *(int32_t *)(self + HUDLOG_OFF_TYPING)    = 0;
    *(int32_t *)(self + HUDLOG_OFF_JUST_SENT) = 0;
    *(void **)(self + HUDLOG_OFF_BUTTON_SPRITE) = 0;
    *(uint32_t *)(self + HUDLOG_OFF_REWIND_AT)  = 0;
    *(int32_t *)(self + HUDLOG_OFF_BLIPS)       = 0;
    *(uint32_t *)(self + HUDLOG_OFF_BLIP_AT)    = 0;

    for (i = 0; i < AM2_HUDLOG_ROWS; i++) {
        uint8_t *row = self + HUDLOG_OFF_ROWS
                     + (size_t)i * AM2_HUDLOG_ROW_STRIDE;
        row[HUDLOGROW_OFF_TEXT] = 0;
        *(int32_t *)(row + HUDLOGROW_OFF_FIELD_50) = 0;
    }

    *(int32_t *)(self + HUDLOG_OFF_SCROLL) = 0;
    w->x = 0;
    w->y = 0;
    w->w = spr->bounds.right;
    w->h = spr->bounds.bottom;
    self[HUDLOG_OFF_TYPED] = 0;
    *(int32_t *)(self + HUDLOG_OFF_COUNT) = 0;

    spr = (AM2_Sprite *)PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 2, 1, 1);
    *(void **)(self + HUDLOG_OFF_SPRITE_DOWN) = spr;
    if (!spr)
        return w;

    spr = (AM2_Sprite *)PreloadArmySprite(AM2_HUD_EDGE_SPRITE_SET, 2, 2, 1);
    *(void **)(self + HUDLOG_OFF_SPRITE_HOT) = spr;
    if (!spr)
        return w;

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME != 0) {
        const uint8_t *comm = *(const uint8_t **)(uintptr_t)ADDR_COMM_OBJECT;
        uint32_t       me   = g_defaultOwner;

        if (*(const int32_t *)(comm + COMM_OFF_PLAYERS
                               + (size_t)me * AM2_COMM_SLOT_STRIDE
                               + COMM_SLOT_OFF_TEAM) != 0) {
            AM2_Widget *cb = (AM2_Widget *)orig_operator_new(AM2_HUD_CHECKBOX_BYTES);

            if (cb)
                cb = CheckBoxConstruct(cb, kChatToSprites[0], kChatToSprites[1],
                                       kChatToSprites[2], kChatToSprites[3],
                                       2, 4, 0x0D, 0x0D, 0, 0, 0);
            *(void **)(self + HUD_A_OFF_CHECKBOX) = cb;
            WidgetAddChild(w, cb);
            return w;
        }
    }

    *(void **)(self + HUD_A_OFF_CHECKBOX) = 0;
    return w;
}


/* The half every factory opens with: whatever screen is up goes away first.
 *
 * The delete is vtable slot 0 with a flag of 1 -- the MSVC scalar deleting
 * destructor, which frees as well as destructs. The global is cleared INSIDE
 * the test, which matters only in that a null one is left alone rather than
 * written; reproduced because it is one instruction either way. */
static void CloseCurrentScreen(void)
{
    AM2_Widget *cur = (AM2_Widget *)g_paintObject;

    if (cur) {
        ((AM2_WidgetDeleteFn *)cur->vtable)[WIDGET_VSLOT_DTOR](cur, 1);
        g_paintObject = (uint8_t *)0;
    }
}

/* 0x0044DB90, one caller. The image has those same three instructions as a
 * FUNCTION as well as inline in the five factories, so it is patched and
 * simply calls the helper. Whether the original's source had one function that
 * MSVC inlined at five sites and left standing at a sixth, or six copies, is
 * not decidable from here -- what is decidable is that the bytes agree.
 *
 * Runs once on a driven Boot Camp mission: leaving the title screen. */
void __cdecl CloseScreen(void)
{
    CloseCurrentScreen();
}

/* 0x004135C0, two callers. Delete all three HUD widgets and clear the three
 * globals -- CloseScreen's shape three times over, on A, B and C in that
 * order.
 *
 * EACH IS TESTED SEPARATELY and each global is cleared INSIDE its own test, so
 * a null one is left alone rather than written. That matters for C, which
 * ADDR_HUD_WIDGET_C's own comment records as the optional one -- HudPaint and
 * HudUpdate both test it and neither tests the other two. Here all three are
 * tested, which is the safer thing and not what those two do.
 *
 * The order is A, B, C, which is the order they are painted and updated in,
 * not the order of their addresses -- C is at 0x004FCF4C and B at 0x004FCF54.
 * Written in the original's order.
 *
 * Runs once on a driven Boot Camp mission. Whether all three were non-null
 * that once is not established, so the C branch specifically is not claimed.
 */
void __cdecl FreeHudWidgets(void)
{
    AM2_Widget *w;

    w = *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A;
    if (w) {
        ((AM2_WidgetDeleteFn *)w->vtable)[WIDGET_VSLOT_DTOR](w, 1);
        *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A = (AM2_Widget *)0;
    }

    w = *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_B;
    if (w) {
        ((AM2_WidgetDeleteFn *)w->vtable)[WIDGET_VSLOT_DTOR](w, 1);
        *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_B = (AM2_Widget *)0;
    }

    w = *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_C;
    if (w) {
        ((AM2_WidgetDeleteFn *)w->vtable)[WIDGET_VSLOT_DTOR](w, 1);
        *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_C = (AM2_Widget *)0;
    }
}

/* 0x00413480, two callers -- the 0x046E setup-done handler and mission start.
 * The counterpart to FreeHudWidgets above: tear the three down, build three
 * more. See orig.h for why this is not the "lobby reset" its old name claimed.
 *
 * The latch either side of the teardown is the interesting part. It reads one
 * BYTE out of A's checkbox child before freeing anything and puts it back
 * afterwards, so the box stays ticked across a rebuild -- and only outside a
 * net game, since the guard tests ADDR_NET_GAME on the way in and the tail
 * runs only if the guard passed.
 *
 * Two faithfulness notes. The tail dereferences ADDR_HUD_WIDGET_A with NO null
 * test, although the `new` above it can answer null; that is the original's
 * and is reproduced rather than hardened, the same standing as LockSurface's
 * descriptor after a successful Restore. And the net-game arm calls the logger
 * with NO ARGUMENTS AT ALL -- not a transcription slip, the same idiom
 * frame.cpp already reproduces twice as orig_log_noargs.
 *
 * The third widget is built only when ADDR_NET_GAME is clear, which is where
 * ADDR_HUD_WIDGET_C's "may be null" comes from independently.
 *
 * Worth recording a tension rather than acting on it: OnSetupDone clears
 * ADDR_NET_GAME immediately before calling this, so at that call site the flag
 * is 0 and the single-player arm runs. orig.h notes the same global is raised
 * by 0x00411000 and lowered by the 0x046E handler, which reads more like "we
 * are in multiplayer SETUP" than "this is a network game". Twenty-one sites
 * read it and one caller is not evidence enough to rename it here. */
typedef void *(__attribute__((thiscall)) *AM2_HudCtorFn)(void *obj);
typedef void (__cdecl *AM2_NoArgLogFn)(void);



#define orig_log_noargs   ((AM2_NoArgLogFn)(uintptr_t)ADDR_LOG)

static AM2_Widget *NewHudWidget(uint32_t size, AM2_HudCtorFn ctor)
{
    void *obj = orig_operator_new(size);

    return obj ? (AM2_Widget *)ctor(obj) : (AM2_Widget *)0;
}

void __cdecl BuildHudWidgets(void)
{
    AM2_Widget *a;
    AM2_Widget *box;
    int32_t     ticked = 0;

    if (!*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        a = *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A;
        if (a) {
            box = *(AM2_Widget **)((uint8_t *)a + HUD_A_OFF_CHECKBOX);
            if (box && *((uint8_t *)box + CHECK_OFF_TICKED))
                ticked = 1;
        }
    }

    FreeHudWidgets();

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        int32_t slot = *(const int32_t *)(uintptr_t)ADDR_OUR_SLOT;

        *(int32_t *)(uintptr_t)ADDR_HUD_DIRTY = 0;
        *(int32_t *)(uintptr_t)ADDR_OUR_POINTS =
            ((const int32_t *)(uintptr_t)ADDR_ARMY_POINTS)[slot];
        orig_log_noargs();
    }

    *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A =
        NewHudWidget(AM2_HUD_A_BYTES, (AM2_HudCtorFn)HudTopConstruct);
    *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_B =
        NewHudWidget(AM2_HUD_B_BYTES, (AM2_HudCtorFn)HudPanelConstruct);

    if (!*(const int32_t *)(uintptr_t)ADDR_NET_GAME)
        *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_C =
            NewHudWidget(AM2_HUD_C_BYTES, (AM2_HudCtorFn)HudEdgeConstruct);

    if (ticked) {
        /* No null test on A -- the original's. */
        a = *(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A;
        box = *(AM2_Widget **)((uint8_t *)a + HUD_A_OFF_CHECKBOX);
        if (box)
            *((uint8_t *)box + CHECK_OFF_TICKED) = 1;
    }
}

/* And the half they close with. `new` answering null is checked at every one
 * of these sites -- VC6's does answer null rather than throwing, and the game
 * tests it -- so the global ends up null rather than holding a constructor's
 * idea of an uninitialised object. */
static void OpenScreen(uint32_t size, AM2_ScreenCtorFn ctor, const char *bmp)
{
    void *obj = orig_operator_new(size);

    g_paintObject = obj ? (uint8_t *)ctor(obj, bmp) : (uint8_t *)0;
}

void __cdecl OpenMpHost(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MP_PANEL_SIZE, (AM2_ScreenCtorFn)MpPanelConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_MPHOST_BMP));
    g_mpSession = AM2_MP_SESSION_HOST;
    RefreshMapSelection();
}

void __cdecl OpenMpJoin(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MP_PANEL_SIZE, (AM2_ScreenCtorFn)MpPanelConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_MPJOIN_BMP));
    g_mpSession = AM2_MP_SESSION_JOIN;
}

void __cdecl OpenMpOptions(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MP_OPTIONS_SIZE, (AM2_ScreenCtorFn)MpOptionsConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_MPHOSTOPTS_BMP));
}

void __cdecl OpenSelectMap(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MP_SELECT_MAP_SIZE, (AM2_ScreenCtorFn)SelectMapConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenSelectPlayer(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_SELECT_PLAYER_SIZE,
               (AM2_ScreenCtorFn)SelectPlayerConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenEnterName(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_ENTER_NAME_SIZE,
               (AM2_ScreenCtorFn)EnterNameConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenWarMenu(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_WAR_MENU_SIZE,
               (AM2_ScreenCtorFn)WarMenuConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenBattleName(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_BATTLE_NAME_SIZE,
               (AM2_ScreenCtorFn)BattleNameConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenBattleJoin(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_BATTLE_JOIN_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_BATTLE_JOIN_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenMovies(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MOVIES_SIZE,
               (AM2_ScreenCtorFn)MoviesConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenOptionsMenu(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_OPTIONS_MENU_SIZE,
               (AM2_ScreenCtorFn)OptionsMenuConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenControls(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_CONTROLS_SIZE,
               (AM2_ScreenCtorFn)ControlsDialogConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_CONTROLS_BMP));
}

void __cdecl OpenDifficulty(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_DIFFICULTY_SIZE,
               (AM2_ScreenCtorFn)DifficultyDialogConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenQuitConfirm(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_QUIT_CONFIRM_SIZE,
               (AM2_ScreenCtorFn)QuitDialogConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenReplayPrompt(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_REPLAY_PROMPT_SIZE,
               (AM2_ScreenCtorFn)ReplayDialogConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenDeletePlayer(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_DELETE_PLAYER_SIZE,
               (AM2_ScreenCtorFn)DelPlayerDialogConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

/* The two-argument form. Five of the twenty-one screens exist in two
 * contexts and their constructors take a backdrop AND a flag; the flag is 0
 * in a mission and 1 on the title screen. */
typedef void *(__attribute__((thiscall)) *AM2_ScreenCtor2Fn)(void *obj,
                                                             const char *bmp,
                                                             int32_t flag);

static void OpenScreen2(uint32_t size, AM2_ScreenCtor2Fn ctor,
                        const char *bmp, int32_t flag)
{
    void *obj = orig_operator_new(size);

    g_paintObject = obj ? (uint8_t *)ctor(obj, bmp, flag) : (uint8_t *)0;
}

#define g_gameState (*(int32_t *)(uintptr_t)ADDR_GAME_STATE)

/* Arm 6. The COMM CHANNEL SELECT screen, and the one factory that does
 * something before it allocates rather than around the branch: it asks the
 * comm object for a DirectPlay interface first, with a null connection --
 * which is the literal 0 that makes CommOnConnected unreachable in this
 * build. Our CommCreateDirectPlay, reached directly, so its counter cannot
 * move. */
void __cdecl OpenCommPanel(void)
{
    CloseCurrentScreen();
    CommCreateDirectPlay(g_commObject, (void *)0);
    OpenScreen(AM2_COMM_PANEL_SIZE,
               (AM2_ScreenCtorFn)CommPanelConstruct,
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

/* Arm 19. AUDIO CONTROLS -- the three volume sliders. The repaint comes
 * BEFORE the allocation on this one.
 *
 * RefreshScreen is OURS and is called directly, so its counter cannot move
 * from here -- the usual blind spot. It went in behind an `orig_` macro first
 * and tools/checkseams.py caught it, which is what that ratchet is for: an
 * `orig_` pointing at a reconstructed address is a lie about where control
 * goes, and this one would have made the seam look open when it is closed. */
void __cdecl OpenAudioOptions(void)
{
    CloseCurrentScreen();
    if (g_gameState == AM2_STATE_MISSION) {
        RefreshScreen();
        OpenScreen2(AM2_AUDIO_OPTIONS_SIZE,
                    (AM2_ScreenCtor2Fn)AudioDialogConstruct,
                    (const char *)AM2_IMAGE(ADDR_STR_AUDIO_BMP), 0);
    } else {
        OpenScreen2(AM2_AUDIO_OPTIONS_SIZE,
                    (AM2_ScreenCtor2Fn)AudioDialogConstruct,
                    (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP), 1);
    }
}

/* Arm 21. DELETE GAME, the same shape as AUDIO. */
void __cdecl OpenDeleteGame(void)
{
    CloseCurrentScreen();
    if (g_gameState == AM2_STATE_MISSION) {
        RefreshScreen();
        OpenScreen2(AM2_DELETE_GAME_SIZE,
                    (AM2_ScreenCtor2Fn)DeleteGameConstruct,
                    (const char *)AM2_IMAGE(ADDR_STR_DELGAME_BMP), 0);
    } else {
        OpenScreen2(AM2_DELETE_GAME_SIZE,
                    (AM2_ScreenCtor2Fn)DeleteGameConstruct,
                    (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP), 1);
    }
}

/* Arm 5. LOAD GAME, and the odd one out: the repaint happens AFTER the
 * screen is constructed and the global is published only then. Reproduced as
 * it is written rather than made to match its two siblings -- whether that
 * ordering matters is not something reading settles, and the screen is
 * reachable, so it can be measured rather than argued about. */
/* The last four arms of the sub-state painter table, and the four screens that
 * were still reached as bare addresses when that table was a list of integers.
 *
 * EVERY ONE IS NAMED FROM THE VTABLE ITS CONSTRUCTOR INSTALLS, not from the
 * bitmap it loads and not from the destructor it sits beside. The bitmap is
 * the trap: 0x00452F50 pushes "00_999_99_blank.bmp" and builds the GAME MENU,
 * because the name describes the backdrop rather than the dialog. Adjacency is
 * no better -- the linker's layout is not a fact about meaning.
 *
 * THE REFRESH IS NOT IN THE SAME PLACE IN ALL FOUR, and that is the whole
 * reason they are written out rather than made from one template. Overwrite
 * refreshes BEFORE it allocates, save game and the game menu refresh AFTER,
 * and the message screen does not refresh at all. OpenAudioOptions' own
 * comment already records this as a per-screen distinction someone was caught
 * by once; flattening it here would be the same mistake with four chances.
 *
 * RefreshScreen is ours, so none of these can move its counter -- the usual
 * blind spot, and one that gets deeper here: it goes from three of seven
 * callers reconstructed to six of seven. CLAUDE.md still lists it as
 * unexercised, which is that blind spot rather than a function that never
 * runs; ab.sh audiovol opens one of these screens every run. */
void __cdecl OpenSaveGame(void)
{
    CloseCurrentScreen();
    OpenScreen2(AM2_SAVE_LIST_SIZE, (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_SAVE_LIST_CTOR),
                (const char *)AM2_IMAGE(ADDR_STR_SAVEGAME_BMP), 0);
    RefreshScreen();
}

void __cdecl OpenOverwriteGame(void)
{
    CloseCurrentScreen();
    RefreshScreen();
    OpenScreen2(AM2_OVERWRITE_SIZE, (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_OVERWRITE_CTOR),
                (const char *)AM2_IMAGE(ADDR_STR_OVRGAME_BMP), 0);
}

void __cdecl OpenGameMenu(void)
{
    CloseCurrentScreen();
    OpenScreen2(AM2_GAMEMENU_SIZE, (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_GAMEMENU_CTOR),
                (const char *)AM2_IMAGE(ADDR_STR_BLANK_BMP), 0);
    RefreshScreen();
}

void __cdecl OpenMessage(void)
{
    CloseCurrentScreen();
    OpenScreen2(AM2_MESSAGE_SIZE, (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_MESSAGE_CTOR),
                (const char *)AM2_IMAGE(ADDR_MESSAGE_BMP_NAME), 0);
}

void __cdecl OpenLoadGame(void)
{
    CloseCurrentScreen();
    if (g_gameState == AM2_STATE_MISSION) {
        void *obj = orig_operator_new(AM2_LOAD_GAME_SIZE);
        uint8_t *screen = (uint8_t *)0;

        if (obj) {
            screen = (uint8_t *)LoadGameConstruct(
                (AM2_Widget *)obj,
                (const char *)AM2_IMAGE(ADDR_STR_LOADGAME_BMP), 0);
        }
        RefreshScreen();
        g_paintObject = screen;
    } else {
        OpenScreen2(AM2_LOAD_GAME_SIZE,
                    (AM2_ScreenCtor2Fn)LoadGameConstruct,
                    (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP), 1);
    }
}

/* 0x0044FAB0, thiscall. The OPTIONS menu: a backdrop and four buttons.
 *
 * The original unrolls the four blocks; they are written here as a table
 * because they differ in exactly three things -- the row, the three bitmaps
 * and the handler -- and four copies of eleven lines would hide that. Nothing
 * about the emitted work changes.
 *
 * The rectangle is passed to the button constructor BY VALUE, in the middle of
 * the argument list, and the original builds it in place: it pushes the four
 * numbers as the placeholders, hands RectSet a pointer to them, and then
 * overwrites the same four slots with what RectSet returns. `ret 0x28` is 40
 * bytes -- three bitmaps, a flag, sixteen bytes of rectangle, a handler and a
 * trailing zero -- which is what confirms the reading rather than the shape of
 * the pushes.
 *
 * The four numbers are not a screen rectangle in the (left, top, right,
 * bottom) sense the type says -- `right` would be 0x98 against a `left` of
 * 0xE7. They are (left, top, WIDTH, HEIGHT), and the button constructor is
 * what turns them into edges: `ctl widgets` puts the four buttons at
 * 231,160,383,185 and three rows below it, which is 0xE7 and 0xE7 + 0x98,
 * 0xA0 and 0xA0 + 0x19. Measured, not assumed -- RectSet stores what it is
 * given and cannot tell the difference.
 *
 * It returns `this`, which its factory stores. */


typedef struct {
    int32_t     top;
    void      (__cdecl *handler)(AM2_Widget *);
    uint32_t    bmp[3];
} AM2_MenuButton;

/* Rows 0xA0, 0xC8, 0xF0, 0x118 -- 40 apart. */
static const AM2_MenuButton kOptionsButtons[] = {
    { 0x00A0, kOnAudioButton,
      { 0x0048B8B0, 0x0048B8C4, 0x0048B8D8 } },  /* 03_120_0N_audio */
    { 0x00C8, kOnControlsButton,
      { 0x0048B868, 0x0048B880, 0x0048B898 } },  /* 03_121_0N_controls */
    { 0x00F0, kOnDifficultyButton,
      { 0x0048B814, 0x0048B830, 0x0048B84C } },  /* 03_126_0N_difficulty */
    { 0x0118, kOnMenuBack,
      { 0x0048B7D8, 0x0048B7EC, 0x0048B800 } },  /* 03_111_0N_back */
};

#define AM2_OPTIONS_BUTTON_LEFT   0xE7
#define AM2_OPTIONS_BUTTON_WIDTH  0x98
#define AM2_OPTIONS_BUTTON_HEIGHT 0x19
#define AM2_BUTTON_SIZE           0x78

AM2_Widget *__attribute__((thiscall)) OptionsMenuConstruct(AM2_Widget *w,
                                                           const char *bmp)
{
    uint32_t i;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_OPTIONS_MENU);
    w->flag44 = 1;

    for (i = 0; i < sizeof kOptionsButtons / sizeof kOptionsButtons[0]; i++) {
        const AM2_MenuButton *b = &kOptionsButtons[i];
        AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
        AM2_Rect    box;

        if (btn) {
            RectSet(&box, AM2_OPTIONS_BUTTON_LEFT, b->top,
                    AM2_OPTIONS_BUTTON_WIDTH, AM2_OPTIONS_BUTTON_HEIGHT);
            btn = ButtonConstruct(btn,
                                   (const char *)AM2_IMAGE(b->bmp[0]),
                                   (const char *)AM2_IMAGE(b->bmp[1]),
                                   (const char *)AM2_IMAGE(b->bmp[2]),
                                   0, box,
                                   (void (__cdecl *)(AM2_Widget *))
                                   AM2_IMAGE(b->handler), 0);
        }
        WidgetAddChild(w, btn);

        /* The FIRST button becomes the focused child, and only the first.
         * That one instruction is the whole difference between AUDIO coming
         * up highlighted and coming up plain -- 294 pixels, which no budget
         * that survives a cursor could catch, and `ctl widgets` reported it
         * as foc=2 against foc=-1. The generated loop dropped it because it
         * is the one line of the four blocks that is not repeated. */
        if (i == 0)
            w->focusedChild = btn;
    }

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnMenuBack;
    return w;
}

/* The three CONFIRM dialogs: CONFIRM GAME EXIT (0x0044EB50), the replay
 * prompt (0x0044EED0) and DELETE PLAYER (0x00450730). One body three times
 * over -- 685 bytes each, differing in the vtable, the panel bitmap, the OK
 * handler, the message and, for DELETE PLAYER alone, the CANCEL handler.
 *
 * The shape: the dialog gets ONE child, a panel, and everything visible is a
 * child of the PANEL -- the OK and CANCEL buttons, the typewriter message and
 * the blinking red dot beside it. The panel is also what carries the focus.
 *
 * Every `ret N` here was checked rather than inferred from the pushes.
 * 0x00454980 is `ret 0x18` -- bitmap, flag, sixteen bytes of rectangle;
 * 0x004566F0 is `ret 0x14` -- rectangle then message; 0x00456BC0 is
 * `ret 0x1C` -- two bitmaps, a flag and a rectangle. The rectangle is by
 * value in all three, as it is for the button.
 *
 * THREE STORES ARE UNGUARDED IN THE ORIGINAL and are reproduced that way.
 * `panel->flag44`, `panel->focusedChild` and the message's blinker field are
 * written after the allocation was tested and found null on the failure path,
 * so a genuine out-of-memory would fault here. VC6's `operator new` answers
 * null rather than throwing and the game checks it everywhere else, which is
 * what makes this an oversight rather than a convention -- but reproducing it
 * costs nothing and diverging would be a silent behavioural change. */
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_PanelCtorFn)(
    AM2_Widget *w, const char *bmp, int32_t flag, AM2_Rect box);


/* The four bitmaps every one of the three uses, and the one string. */
#define AM2_BMP_OK0    0x00487044u
#define AM2_BMP_OK1    0x00487058u
#define AM2_BMP_OK2    0x0048706Cu
#define AM2_BMP_CAN0   0x00486DECu
#define AM2_BMP_CAN1   0x00486E04u
#define AM2_BMP_CAN2   0x00486E1Cu
#define AM2_BMP_RED0   0x00487178u
#define AM2_BMP_RED1   0x0048718Cu


/* The handler by POINTER, always. There used to be a second form taking the
 * ADDRESS and applying AM2_IMAGE here, which is right for a handler still in
 * the image and a lie for one that is ours -- and invisible, because nothing
 * on the call line named an address. Every site passes a pointer now:
 * `kImageHandler(ADDR_X)` where the handler is still the original's, the
 * function itself where it is not. */
static AM2_Widget *MakeButton(int32_t left, int32_t top, uint32_t b0,
                                uint32_t b1, uint32_t b2,
                                void (__cdecl *handler)(AM2_Widget *))
{
    AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    AM2_Rect    box;

    if (!btn)
        return (AM2_Widget *)0;
    RectSet(&box, left, top, 0x51, 0x20);
    return ButtonConstruct(btn, (const char *)AM2_IMAGE(b0),
                            (const char *)AM2_IMAGE(b1),
                            (const char *)AM2_IMAGE(b2), 1, box, handler, 0);
}


static AM2_Widget *ConfirmDialogBuild(AM2_Widget *w, const char *bmp,
                                      uint32_t vtable, uint32_t panelBmp,
                                      void (__cdecl *okHandler)(AM2_Widget *),
                                      uint32_t message,
                                      void (__cdecl *cancelHandler)(AM2_Widget *))
{
    AM2_Widget *panel;
    AM2_Widget *text;
    AM2_Widget *dot;
    AM2_Rect    box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(vtable);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_ALPINE));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x98, 0x1A7, 0xB0);
        panel = PanelConstruct(panel, (const char *)AM2_IMAGE(panelBmp), 0,
                                box);
    }
    WidgetAddChild(w, panel);
    panel->flag44 = 1;

    {
        AM2_Widget *ok = MakeButton(0x149, 0x38, AM2_BMP_OK0, AM2_BMP_OK1,
                                      AM2_BMP_OK2, okHandler);

        WidgetAddChild(panel, ok);
        panel->focusedChild = ok;
    }

    WidgetAddChild(panel, MakeButton(0x149, 0x61, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                       AM2_BMP_CAN2, cancelHandler));

    text = (AM2_Widget *)orig_operator_new(AM2_TYPER_SIZE);
    if (text) {
        RectSet(&box, 0x28, 0x41, 0xF0, 0x34);
        text = TyperConstruct(text, box.left, box.top, box.right,
                              box.bottom,
                              (const char *)AM2_IMAGE(message));
    }
    WidgetAddChild(panel, text);

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, 0x23, 0x95, 0x11, 0x10);
        dot = MultiSpriteConstruct(dot, (const char *)AM2_IMAGE(AM2_BMP_RED0),
                                    (const char *)AM2_IMAGE(AM2_BMP_RED1), 1,
                                    box);
    }
    WidgetAddChild(panel, dot);

    *(AM2_Widget **)((uint8_t *)text + TYPER_OFF_BLINKER) = dot;
    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)cancelHandler;
    return w;
}

/* WarMenuConstruct -- original 0x0042EED0, thiscall, `ret 4`. START A WAR,
 * JOIN A WAR and CANCEL over a blank panel, and the screen COMM. CHANNEL
 * SELECT's SELECT reaches.
 *
 * IT WAS CALLED THE CD PROMPT AND THE NAME CAME FROM A BUTTON. `orig.h` and
 * `widget.h` both said this screen "pushes 'Copy Protection' and 'The
 * ARMYMEN2 CD must be in the drive to play Army Men II.'" It pushes neither.
 * Those two strings are in 0x0042F290, which is what the FIRST button fires,
 * and the constructor's own evidence is three bitmap triples called host,
 * join and cancel. Fourth or fifth instance of naming a function from a call
 * site rather than its body, and the cure was the same: read the callee.
 *
 * WHICH BUTTON IS WHICH IS SETTLED BY COMM_OFF_IS_HOST, not by the bitmap
 * names and not by the geometry. 0x0042F290 sets it to 1 and asks for
 * AM2_MENU_REQUEST_BATTLE_NAME; StartMultiplayerGame clears it and asks for
 * AM2_MENU_REQUEST_BATTLE_JOIN. The other two agree -- host.bmp on the one
 * that hosts, and y centres of 222 and 262, which are the coordinates
 * tools/ab.sh multi has been clicking as START A WAR and JOIN A WAR all
 * along.
 *
 * THE FIRST BUTTON IS THE DEFAULT FOCUS AND IS MARKED DIRTY. `w->focusedChild
 * = host` and `host->flag44 = 1`, in that order, after it is added.
 *
 * AND THE `flag44` WRITE IS NOT GUARDED. The original tests the allocation
 * before constructing the button and substitutes a null on failure, then
 * writes through that null two instructions later. Reproduced: VC6's operator
 * new answers null rather than throwing, so the path exists, and it is the
 * original's behaviour rather than something to tidy.
 *
 * DELIBERATE DEVIATION -- the MSVC structured-exception frame around the body,
 * with its four unwind state indices, is not reproduced. See the note on
 * StartMultiplayerGame in startgame.cpp: nothing in this program throws, VC6's
 * operator new returns null and the original tests it, so the registered frame
 * is never consulted. */
AM2_Widget *__attribute__((thiscall)) WarMenuConstruct(AM2_Widget *w,
                                                       const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *host;
    AM2_Widget *join;
    AM2_Widget *cancel;
    AM2_Rect    box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_WAR_MENU);

    /* Every dialog constructor in this family opens with this, and
     * ADDR_DIR_SCRATCH is uninitialised at load -- so unless something has
     * written it the call is SetGameDir(""), back to the base. Ours. */
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0xBD, 0x48, 0x106, 0x35);
        panel = PanelConstruct(panel,
                               (const char *)AM2_IMAGE(ADDR_STR_BLANK_BMP),
                               1, box);
    }
    WidgetAddChild(w, panel);

    host = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    if (host) {
        RectSet(&box, 0xF4, 0xD2, 0x98, 0x19);
        host = ButtonConstruct(host, (const char *)AM2_IMAGE(AM2_BMP_HOST0),
                               (const char *)AM2_IMAGE(AM2_BMP_HOST1),
                               (const char *)AM2_IMAGE(AM2_BMP_HOST2), 1, box,
                               kImageHandler(ADDR_ON_START_WAR), 0);
    }
    WidgetAddChild(w, host);
    w->focusedChild = host;
    host->flag44    = 1;   /* unguarded in the original -- see above */

    join = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    if (join) {
        RectSet(&box, 0xF4, 0xFA, 0x98, 0x19);
        join = ButtonConstruct(join, (const char *)AM2_IMAGE(AM2_BMP_JOIN0),
                               (const char *)AM2_IMAGE(AM2_BMP_JOIN1),
                               (const char *)AM2_IMAGE(AM2_BMP_JOIN2), 1, box,
                               kOnStartMultiplayer, 0);
    }
    WidgetAddChild(w, join);

    cancel = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    if (cancel) {
        RectSet(&box, 0xF4, 0x122, 0x98, 0x19);
        cancel = ButtonConstruct(cancel,
                                 (const char *)AM2_IMAGE(AM2_BMP_CANCEL0),
                                 (const char *)AM2_IMAGE(AM2_BMP_CANCEL1),
                                 (const char *)AM2_IMAGE(AM2_BMP_CANCEL2), 1,
                                 box, OnMenuBack, 0);
    }
    WidgetAddChild(w, cancel);

    return w;
}

AM2_Widget *__attribute__((thiscall)) QuitDialogConstruct(AM2_Widget *w,
                                                          const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_QUIT_DIALOG, 0x0048B76C,
                              kOnQuitOk, 0x0048B74C, kOnMenuBack);
}

AM2_Widget *__attribute__((thiscall)) ReplayDialogConstruct(AM2_Widget *w,
                                                            const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_REPLAY_DIALOG, 0x0048B7B0,
                              kOnReplayOk, 0x0048B780,
                              kOnMenuBack);
}

AM2_Widget *__attribute__((thiscall)) DelPlayerDialogConstruct(AM2_Widget *w,
                                                               const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_DELPLAYER_DIALOG, 0x0048B9C4,
                              kImageHandler(ADDR_ON_DELPLAYER_OK), 0x0048B984,
                              kOnDelPlayerCancel);
}

/* 0x0044E730, thiscall. The DIFFICULTY dialog -- the same panel-holds-
 * everything shape as the three confirm dialogs, with a LIST BOX where they
 * have a message.
 *
 * The rows are built with RecordCtor and ListAdd, both of ours, so the record
 * whose missing return killed the multiplayer path is now constructed by our
 * code on a screen the suite drives.
 *
 * Two fields are seeded from ADDR_DIFFICULTY and it is worth naming which:
 * LIST_OFF_SELECTED and LIST_OFF_HOT, so the dialog opens with the current
 * setting both selected AND highlighted rather than merely selected. That is
 * the green bar on Medium in a default install.
 *
 * The list is also stored on the DIALOG at 0x0064 -- the dialog reaches it
 * again twice before the constructor ends -- and the blinking dot is stored
 * on the LIST at 0x0094, not on the panel that owns it. */

#define g_difficulty      (*(int32_t *)(uintptr_t)ADDR_DIFFICULTY)

AM2_Widget *__attribute__((thiscall)) DifficultyDialogConstruct(
    AM2_Widget *w, const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *dot;
    void       *rows;
    AM2_Rect    box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_DIFFICULTY_DIALOG);

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x98, 0x1A7, 0xB0);
        panel = PanelConstruct(panel,
                                (const char *)AM2_IMAGE(ADDR_STR_DIFFICULTY_BMP),
                                0, box);
    }
    WidgetAddChild(w, panel);
    panel->flag44 = 1;

    rows = orig_operator_new(AM2_ROWS_SIZE);
    if (rows)
        rows = RecordCtor(rows, 0);
    ListAdd(rows, (const char *)AM2_IMAGE(ADDR_STR_EASY),   (void *)0);
    ListAdd(rows, (const char *)AM2_IMAGE(ADDR_STR_MEDIUM), (void *)1);
    ListAdd(rows, (const char *)AM2_IMAGE(ADDR_STR_HARD),   (void *)2);

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x28, 0x3F, 0xF4, 0x3A);
        list = ListBoxConstruct(list, box.left, box.top, box.right, box.bottom,
                                 rows, 0, 0, 1);
    }
    *(AM2_Widget **)((uint8_t *)w + DLG_OFF_LIST) = list;
    WidgetAddChild(panel, list);

    {
        AM2_Widget *sel = *(AM2_Widget **)((uint8_t *)w + DLG_OFF_LIST);

        ((AM2_WidgetFocusFn *)sel->vtable)[WIDGET_VSLOT_FOCUS](sel, 0);
        *(int32_t *)((uint8_t *)sel + LIST_OFF_HOT)      = g_difficulty;
        *(int32_t *)((uint8_t *)sel + LIST_OFF_SELECTED) = g_difficulty;
    }

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, 0x23, 0x95, 0x11, 0x10);
        dot = MultiSpriteConstruct(dot, (const char *)AM2_IMAGE(AM2_BMP_RED0),
                                    (const char *)AM2_IMAGE(AM2_BMP_RED1), 1,
                                    box);
    }
    WidgetAddChild(panel, dot);
    *(AM2_Widget **)((uint8_t *)*(AM2_Widget **)((uint8_t *)w + DLG_OFF_LIST)
                     + LIST_OFF_BLINKER) = dot;

    WidgetAddChild(panel, MakeButton(0x149, 0x38, AM2_BMP_OK0, AM2_BMP_OK1,
                                       AM2_BMP_OK2, kOnDifficultyOk));
    WidgetAddChild(panel, MakeButton(0x149, 0x61, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                       AM2_BMP_CAN2, kOnOptionsMenu));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnOptionsMenu;
    return w;
}

/* 0x00450E10, thiscall. The CONTROLS dialog: twenty-one key-capture rows and
 * three buttons, and the only screen in the game whose children come out of
 * TABLES rather than being written out one at a time.
 *
 * Three tables walk together. ADDR_KEY_BINDINGS is the scancode each row is
 * bound to -- a `uint8_t[][2]` walked one byte at a time with a stride of two,
 * which is why the loop bound is on that pointer and not on a counter.
 * ADDR_KEYROW_POSITIONS gives the row's x and y as int16s. And the row's index
 * into the key-name table, from our own KeyNameIndexOf, selects the caption
 * out of ADDR_KEY_NAME_TABLE's second field.
 *
 * The rows are then written into the dialog's own array at
 * KEYROW_PARENT_ROWS, which is what KeyRowUpdate walks when it clears a key
 * off whichever other row had it.
 *
 * `ret 0x2C` on the row constructor is 44 bytes: index, caption, sixteen of
 * rectangle, a flag and four colours. Three of those colours are pushed as
 * whole dwords from BYTE loads, so their top three bytes are stale stack --
 * the same matched-argument shape MakeBitmap has. It is safe for the same
 * reason: 0x00450C8E reads all three back as `mov al, byte ptr`, so the
 * garbage is never looked at, and passing a zero-extended byte is faithful. */
#define AM2_KEYROW_WIDTH   0x41
#define AM2_KEYROW_HEIGHT  0x0D

AM2_Widget *__attribute__((thiscall)) ControlsDialogConstruct(AM2_Widget *w,
                                                              const char *bmp)
{
    const uint8_t *binding = (const uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS);
    const int16_t *place   = (const int16_t *)AM2_IMAGE(ADDR_KEYROW_POSITIONS);
    const uint8_t *names   = (const uint8_t *)AM2_IMAGE(ADDR_KEY_NAME_TABLE);
    AM2_Widget   **rows;
    AM2_Widget    *first;
    AM2_Widget    *ok;
    AM2_Rect       box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_CONTROLS_DIALOG);
    rows = (AM2_Widget **)((uint8_t *)w + KEYROW_PARENT_ROWS);

    while (binding < (const uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS)
                     + KEYROW_ROW_COUNT * 2) {
        int32_t     index = KeyNameIndexOf(*binding);
        AM2_Widget *row   = (AM2_Widget *)orig_operator_new(AM2_KEYROW_SIZE);

        if (row) {
            RectSet(&box, place[0], place[1], AM2_KEYROW_WIDTH,
                    AM2_KEYROW_HEIGHT);
            row = KeyRowConstruct(row, index,
                                  *(const char *const *)(names + index * 8
                                                         + 4),
                                  box.left, box.top, box.right, box.bottom,
                                  1, g_whiteInk, g_hiliteColour,
                                  g_backgroundColour, g_backgroundColour);
        }
        WidgetAddChild(w, row);
        *rows++ = row;
        binding += 2;
        place += 2;
    }

    first = *(AM2_Widget **)((uint8_t *)w + KEYROW_PARENT_ROWS);
    ((AM2_WidgetFocusFn *)first->vtable)[WIDGET_VSLOT_FOCUS](first, 0);

    /* Same shape as the confirm dialogs' buttons, in a column at x 0x218:
     * flag 1, size 0x51 by 0x20. The `push 0` at the top of each block is the
     * TRAILING argument, not the flag -- reading it as the flag put the three
     * buttons one palette step off and cost 547 pixels on a frame whose
     * widget tree was identical, which is the failure `ctl widgets` cannot
     * see and the pixels can. */
    ok = MakeButton(0x218, 0xAD, AM2_BMP_OK0, AM2_BMP_OK1, AM2_BMP_OK2,
                      kOnControlsOk);
    WidgetAddChild(w, ok);
    ((AM2_WidgetFocusFn *)ok->vtable)[WIDGET_VSLOT_FOCUS](ok, 0);

    WidgetAddChild(w, MakeButton(0x218, 0xDF, AM2_BMP_DEFAULT0,
                                   AM2_BMP_DEFAULT1, AM2_BMP_DEFAULT2,
                                   kOnControlsDefault));
    WidgetAddChild(w, MakeButton(0x218, 0x112, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                   AM2_BMP_CAN2, kOnControlsCancel));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnControlsCancel;
    return w;
}

/* 0x00432320, thiscall. The MULTIPLAYER OPTIONS screen: 43 checkboxes out of
 * the declarative table at ADDR_OPTION_TABLE, then three buttons -- or one.
 *
 * Everything about the layout comes from the table; see orig.h for the record.
 * The original walks it with a cursor 4 bytes IN, so its field offsets all
 * read four low, and the loop bound is 0x00486BC8 where the table ends at
 * 0x00486BC4. Written here from the record base, which is why the numbers do
 * not match the disassembly on sight.
 *
 * Three things depend on being the host, and they are what the two panels in
 * the screenshots differ by. A non-host gets `disabled` set on every box, so
 * none can be focused; a non-host gets CANCEL alone, at the OK position; and
 * the group pass that disables a group whose header is unticked runs for the
 * host only.
 *
 * `ret 0x2C` on the checkbox constructor is 44 bytes -- four bitmaps, sixteen
 * of rectangle, a flag, the caption and the handler. */


#define OPT_REC(base, off) (*(const int32_t *)((base) + (off)))

AM2_Widget *__attribute__((thiscall)) MpOptionsConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    const uint8_t *rec;
    int32_t        record = 0;
    AM2_Widget   **boxes = (AM2_Widget **)((uint8_t *)w + OPTION_PARENT_BOXES);
    const uint8_t *comm  = g_commObject;
    int32_t        host  = *(const int32_t *)(comm + COMM_OFF_IS_HOST);
    AM2_Rect       box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_OPTIONS_MENU_MP);
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    for (rec = (const uint8_t *)AM2_IMAGE(ADDR_OPTION_TABLE);
         rec < (const uint8_t *)AM2_IMAGE(ADDR_OPTION_TABLE_END);
         rec += AM2_OPTION_STRIDE) {
        AM2_Widget *cb = (AM2_Widget *)orig_operator_new(AM2_CHECKBOX_SIZE);
        uint32_t    bit = (uint32_t)OPT_REC(rec, AM2_OPTION_OFF_BIT);
        uint32_t    mask;

        if (cb) {
            RectSet(&box, OPT_REC(rec, 0x04), OPT_REC(rec, 0x08), 0xC8, 0x0D);
            cb = CheckBoxConstruct(cb,
                                   (const char *)AM2_IMAGE(AM2_BMP_CHECK0),
                                   (const char *)AM2_IMAGE(AM2_BMP_CHECK1),
                                   (const char *)AM2_IMAGE(AM2_BMP_CHECK2),
                                   (const char *)AM2_IMAGE(AM2_BMP_CHECK3),
                                   box.left, box.top, box.right, box.bottom,
                                   record,
                                   *(const char *const *)(rec + 0x20),
                                   (void (__cdecl *)(AM2_Widget *))
                                   kOptionsSyncGroup);
        }
        boxes[OPT_REC(rec, AM2_OPTION_OFF_WIDGET)] = cb;
        cb->flag3C = 0;

        mask = OPT_REC(rec, AM2_OPTION_OFF_WHICH) ? g_gameOverFlags
                                                  : g_gameSetting22C;
        *((uint8_t *)cb + CHECK_OFF_TICKED) = (mask & bit) != 0;

        WidgetAddChild(w, cb);
        if (!host)
            cb->disabled = 1;
        record++;
    }

    if (host) {
        for (rec = (const uint8_t *)AM2_IMAGE(ADDR_OPTION_TABLE);
             rec < (const uint8_t *)AM2_IMAGE(ADDR_OPTION_TABLE_END);
             rec += AM2_OPTION_STRIDE) {
            AM2_Widget *head;
            int32_t     i;

            if (!OPT_REC(rec, AM2_OPTION_OFF_GROUP))
                continue;
            head = boxes[OPT_REC(rec, AM2_OPTION_OFF_WIDGET)];
            for (i = OPT_REC(rec, AM2_OPTION_OFF_FIRST);
                 i <= OPT_REC(rec, AM2_OPTION_OFF_LAST); i++)
                boxes[i]->disabled =
                    (*((const uint8_t *)head + CHECK_OFF_TICKED) == 0);
        }

        {
            AM2_Widget *ok = MakeButton(0x219, 0xAE, AM2_BMP_OK0, AM2_BMP_OK1,
                                        AM2_BMP_OK2, kOptionsApply);

            WidgetAddChild(w, ok);
            w->focusedChild = ok;
            ok->flag44 = 1;
        }
        WidgetAddChild(w, MakeButton(0x219, 0xE0, AM2_BMP_DEFAULT0,
                                     AM2_BMP_DEFAULT1, AM2_BMP_DEFAULT2,
                                     kOptionsDefaults));
        WidgetAddChild(w, MakeButton(0x219, 0x112, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, kOptionsRequest));
    } else {
        AM2_Widget *cancel = MakeButton(0x219, 0xAE, AM2_BMP_CAN0,
                                        AM2_BMP_CAN1, AM2_BMP_CAN2,
                                        kOptionsRequest);

        WidgetAddChild(w, cancel);
        w->focusedChild = cancel;
        cancel->flag44 = 1;
    }
    return w;
}

/* 0x0044F370, thiscall, `ret 8`. AUDIO CONTROLS -- three volume sliders.
 *
 * The only screen whose SHAPE depends on where it was opened from. In a
 * mission there is no panel at all: the dialog itself is the parent and the
 * three bars carry an offset of (0x89, 0x79), which is exactly where the panel
 * would have been. From the menus the panel is made, sits at that position,
 * and the offset becomes zero because the bars are then placed relative to it.
 * The original keeps the offset in two stack slots and reuses them afterwards,
 * which is what makes the listing hard to follow.
 *
 * A bar's POSITION comes from its volume by a magic-number division --
 * `imul 0x51EB851F` then `sar 5` and the sign correction, which is signed
 * division by 100. The volumes are DirectSound attenuations in hundredths of
 * a decibel, so (volume + 2000) / 100 is twenty-one steps from silence, and
 * the clamp below zero is what keeps a volume under -2000 on the end stop.
 *
 * The THUMB is x87: (pos / range) * (SPAN - the bar sprite's right edge),
 * truncated through _ftol. `long double` reproduces the 80-bit intermediate,
 * as it does for SetMaxHealth and Ticks; the division happens first and the
 * multiply second, which is the order fidiv/fmulp gives and not the order a
 * naive (pos * span) / range would.
 *
 * Only the FIRST bar becomes the parent's focused child. */

/* The three the AUDIO dialog reads to place its bars and its own handlers
 * write back; see OnVolumeEffects and friends below. */
#define g_volumeAtZero  (*(int32_t *)(uintptr_t)ADDR_VOLUME_AT_ZERO)
#define g_streamVolume  (*(int32_t *)(uintptr_t)ADDR_STREAM_VOLUME)
#define g_voiceVolume   (*(int32_t *)(uintptr_t)ADDR_VOLUME_VOICE)


/* onChange by POINTER, for the same reason MakeButton takes one: all three
 * of these handlers are reconstructed, and reaching them through the image
 * would be a seam tools/checkseams.py cannot see -- the AM2_IMAGE would be
 * applied here, to a parameter, with no ADDR_ name anywhere in the text. */
static AM2_Widget *MakeVolumeBar(AM2_Widget *parent, int32_t x, int32_t y,
                                 int32_t volume,
                                 void (__cdecl *onChange)(AM2_Widget *))
{
    AM2_Widget *bar = (AM2_Widget *)orig_operator_new(AM2_SCROLLBAR_SIZE);
    AM2_Rect    box;
    int32_t     pos;
    int32_t     travel;
    uint8_t    *b;

    if (bar) {
        RectSet(&box, x, y, 0xBA, 0x15);
        bar = ScrollBarConstruct(bar, box.left, box.top,
                                 box.right, box.bottom, parent,
                                 0x92);
    }
    WidgetAddChild(parent, bar);

    b   = (uint8_t *)bar;
    pos = (volume + AM2_VOLUME_FLOOR) / AM2_VOLUME_STEP;
    if (pos < 0)
        pos = 0;
    *(int32_t *)(b + SCROLLBAR_OFF_POS) = pos;

    travel = *(const int32_t *)(b + SCROLLBAR_OFF_SPAN)
             - (*(AM2_Sprite **)(b + SCROLLBAR_OFF_BAR))->bounds.right;
    *(int32_t *)(b + SCROLLBAR_OFF_SHIFT) =
        (int32_t)((long double)pos
                  / (long double)*(const int32_t *)(b + SCROLLBAR_OFF_RANGE)
                  * (long double)travel);
    *(int32_t *)(b + SCROLLBAR_OFF_FLAG50) = 0;
    *(uint32_t *)(b + SCROLLBAR_OFF_ONCHANGE) = (uint32_t)(uintptr_t)onChange;
    return bar;
}

AM2_Widget *__attribute__((thiscall)) AudioDialogConstruct(AM2_Widget *w,
                                                           const char *bmp,
                                                           int32_t flag)
{
    AM2_Widget **bars  = (AM2_Widget **)((uint8_t *)w + AUDIO_OFF_BARS);
    int32_t     *saved = (int32_t *)((uint8_t *)w + AUDIO_OFF_SAVED);
    AM2_Widget  *parent;
    int32_t      offX = 0x89;
    int32_t      offY = 0x79;
    AM2_Rect     box;

    ScreenBaseConstruct(w, bmp, flag);
    w->vtable = (void *)AM2_IMAGE(VTABLE_AUDIO_DIALOG);

    if (g_gameState == AM2_STATE_MISSION) {
        w->flag44 = 1;
        parent = w;
    } else {
        AM2_Widget *panel =
            (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);

        if (panel) {
            RectSet(&box, offX, offY, 0x16E, 0xED);
            panel = PanelConstruct(panel,
                                    (const char *)AM2_IMAGE(ADDR_STR_AUDIO_BMP),
                                    0, box);
        }
        WidgetAddChild(w, panel);
        panel->flag44 = 1;
        parent = panel;
        offX = 0;
        offY = 0;
    }

    bars[0] = MakeVolumeBar(parent, offX + 0x25, offY + 0x38,
                            g_volumeAtZero, OnVolumeEffects);
    parent->focusedChild = bars[0];
    bars[1] = MakeVolumeBar(parent, offX + 0x25, offY + 0x7D,
                            g_streamVolume, OnVolumeMusic);
    bars[2] = MakeVolumeBar(parent, offX + 0x25, offY + 0xC2,
                            g_voiceVolume, OnVolumeVoice);

    /* Kept so CANCEL can put them back. */
    saved[0] = g_volumeAtZero;
    saved[1] = g_streamVolume;
    saved[2] = g_voiceVolume;

    WidgetAddChild(parent, MakeButton(offX + 0x110, offY + 0x5E, AM2_BMP_OK0,
                                        AM2_BMP_OK1, AM2_BMP_OK2,
                                        kOnAudioOk));
    WidgetAddChild(parent, MakeButton(offX + 0x110, offY + 0x8B, AM2_BMP_CAN0,
                                        AM2_BMP_CAN1, AM2_BMP_CAN2,
                                        kOnAudioCancel));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnAudioCancel;
    return w;
}

/* This dialog's buttons are 0x4E wide where every other screen's are 0x51. */
static AM2_Widget *MakeWideButton(int32_t left, int32_t top, uint32_t b0,
                                  uint32_t b1, uint32_t b2,
                                  void (__cdecl *handler)(AM2_Widget *))
{
    AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    AM2_Rect    box;

    if (!btn)
        return (AM2_Widget *)0;
    RectSet(&box, left, top, 0x4E, 0x20);
    return ButtonConstruct(btn, (const char *)AM2_IMAGE(b0),
                            (const char *)AM2_IMAGE(b1),
                            (const char *)AM2_IMAGE(b2), 1, box, handler, 0);
}

/* 0x0042FB00, thiscall. ENTER BATTLE NAME -- two edit boxes, a green dot
 * beside each, and OK and CANCEL.
 *
 * The two fields are SEEDED from the saved names before any widget is made:
 * ADDR_SAVED_BATTLE_NAME into the dialog's own 0x0064 and
 * ADDR_SAVED_PLAYER_NAME into 0x0084, and the edit boxes then edit those
 * buffers in place. That is why HostBattle can read the names back out of
 * globals afterwards without the dialog handing them over.
 *
 * `ret 0x34` on the edit constructor is 52 bytes: the buffer, a maximum of
 * 0x18 characters, sixteen of rectangle, a flag, three colours, the handler
 * and two zeroes. The handler is kHostBattle -- the same function the OK
 * button gets -- so RETURN in either field starts the battle.
 *
 * The dot is stored ON THE EDIT BOX at 0x0070 and added to the PANEL, and
 * each box takes the accepted-character set from ADDR_EDIT_CHARSET_PTR: a
 * whitelist of letters, digits, space and punctuation rather than a length
 * limit. */

#define g_colourBelowBg (*(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG)
#define g_editCharset   (*(const char *const *)(uintptr_t)ADDR_EDIT_CHARSET_PTR)

/* One field: the box, then the dot that sits beside it. */
static AM2_Widget *MakeNameField(AM2_Widget *panel, char *buf, int32_t top,
                                 int32_t dotTop, int32_t focus)
{
    AM2_Widget *edit = (AM2_Widget *)orig_operator_new(AM2_EDIT_SIZE);
    AM2_Widget *dot;
    AM2_Rect    box;

    if (edit) {
        RectSet(&box, 0x26, top, 0xF4, 0x11);
        edit = EditConstruct(edit, buf, AM2_EDIT_MAX_CHARS, box.left,
                             box.top, box.right, box.bottom, 1,
                             g_hiliteColour, g_colourBelowBg,
                             g_backgroundColour,
                             (void (__cdecl *)(AM2_Widget *))
                             kHostBattle, 0, 0);
    }
    WidgetAddChild(panel, edit);
    /* Only the FIRST field gets the focus slot -- the original calls it once,
     * at 0x0042FC9C, and not at all in the second block. Calling it for both
     * left the wrong field marked dirty, which `ctl widgets` reported and no
     * pixel count would have. */
    if (focus)
        ((AM2_WidgetFocusFn *)edit->vtable)[WIDGET_VSLOT_FOCUS](edit, 0);
    *(const char **)((uint8_t *)edit + EDIT_OFF_CHARSET) = g_editCharset;

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, 0xB3, dotTop, 0x11, 0x10);
        dot = MultiSpriteConstruct(dot,
                                    (const char *)AM2_IMAGE(AM2_BMP_GREEN0),
                                    (const char *)AM2_IMAGE(AM2_BMP_GREEN1),
                                    1, box);
    }
    WidgetAddChild(panel, dot);
    *(AM2_Widget **)((uint8_t *)edit + EDIT_OFF_DOT) = dot;
    return edit;
}

AM2_Widget *__attribute__((thiscall)) BattleNameConstruct(AM2_Widget *w,
                                                          const char *bmp)
{
    AM2_Widget *panel;
    AM2_Rect    box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_BATTLE_NAME_DLG);

    strcpy((char *)w + 0x64,
               (const char *)AM2_IMAGE(ADDR_SAVED_BATTLE_NAME));
    strcpy((char *)w + 0x84,
               (const char *)AM2_IMAGE(ADDR_SAVED_PLAYER_NAME));
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x8F, 0x1A7, 0xC1);
        panel = PanelConstruct(panel,
                                (const char *)AM2_IMAGE(ADDR_STR_BATTLE_PANEL),
                                0, box);
    }
    WidgetAddChild(w, panel);
    /* The DIALOG's focused child is the panel, not a field. */
    w->focusedChild = panel;
    panel->flag44 = 1;

    MakeNameField(panel, (char *)w + 0x64, 0x39, 0x1B, 1);
    MakeNameField(panel, (char *)w + 0x84, 0x88, 0x6A, 0);

    WidgetAddChild(panel, MakeWideButton(0x14A, 0x44, AM2_BMP_OK0,
                                         AM2_BMP_OK1, AM2_BMP_OK2,
                                         kHostBattle));
    WidgetAddChild(panel, MakeWideButton(0x14A, 0x6D, AM2_BMP_CAN0,
                                         AM2_BMP_CAN1, AM2_BMP_CAN2,
                                         kOnMenuBack));
    return w;
}

/* 0x0042E9C0, thiscall. COMM. CHANNEL SELECT -- the connection list.
 *
 * The rows come from our own CommEnumConnections, into a record built by our
 * own RecordCtor with a flag of 1 where DIFFICULTY passes 0. Both are kept on
 * the dialog: the list at 0x0064 and the rows at 0x0068.
 *
 * The list box's third argument is a FUNCTION POINTER and it is ADDR_LOG.
 * That is not a mistake either way round: orig.h already records that the
 * linker folded an empty virtual and the stubbed varargs logger onto one
 * address, because both are a single `ret` byte. Passing it here means "no
 * callback", and it is passed as the literal address rather than as a null
 * because that is what the original does.
 *
 * The bar is the arrow-ended one -- `ret 0x24`, 36 bytes: rectangle, the list
 * it drives, two bitmaps, a maximum and a zero -- and the two point at each
 * other afterwards, the list at 0x007C and the bar at 0x0058. */


AM2_Widget *__attribute__((thiscall)) CommPanelConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *bar;
    void       *rows;
    AM2_Rect    box;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_COMM_PANEL);
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    rows = orig_operator_new(AM2_ROWS_SIZE);
    if (rows)
        rows = RecordCtor(rows, 1);
    *(void **)((uint8_t *)w + COMMPANEL_OFF_ROWS) = rows;
    CommEnumConnections(g_commObject, rows);

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x40, 0x62, 0x200, 0x11C);
        panel = PanelConstruct(panel,
                                (const char *)AM2_IMAGE(ADDR_STR_COMMPANEL_BMP),
                                0, box);
    }
    WidgetAddChild(w, panel);
    panel->flag44 = 1;

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x2A, 0x44, 0x11F, 0xAA);
        list = ListBoxConstruct(list, box.left, box.top, box.right,
                                box.bottom,
                                *(void **)((uint8_t *)w
                                           + COMMPANEL_OFF_ROWS),
                                (int32_t)AM2_IMAGE(ADDR_LOG), 0, 1);
    }
    *(AM2_Widget **)((uint8_t *)w + COMMPANEL_OFF_LIST) = list;
    WidgetAddChild(panel, list);
    {
        AM2_Widget *l = *(AM2_Widget **)((uint8_t *)w + COMMPANEL_OFF_LIST);

        ((AM2_WidgetFocusFn *)l->vtable)[WIDGET_VSLOT_FOCUS](l, 0);
    }

    bar = (AM2_Widget *)orig_operator_new(AM2_ARROWBAR_SIZE);
    if (bar) {
        RectSet(&box, 0x15E, 0x3C, 0x13, 0xBA);
        bar = ArrowBarConstruct(bar, box.left, box.top, box.right,
                                box.bottom, panel,
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                                0x92, 0);
    }
    WidgetAddChild(panel, bar);
    *(AM2_Widget **)((uint8_t *)*(AM2_Widget **)((uint8_t *)w
                                                 + COMMPANEL_OFF_LIST)
                     + LIST_OFF_ARROWBAR) = bar;
    *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST) =
        *(AM2_Widget **)((uint8_t *)w + COMMPANEL_OFF_LIST);

    WidgetAddChild(panel, MakeButton(0x19C, 0x6B, AM2_BMP_SELECT0,
                                     AM2_BMP_SELECT1, AM2_BMP_SELECT2,
                                     kStartSelectedGame));
    WidgetAddChild(panel, MakeButton(0x19C, 0x94, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, kOnMenuBack));
    return w;
}

/* 0x00451400, thiscall. SELECT PLAYER -- and the one screen whose rows come
 * off the FILESYSTEM rather than from a table or the comm object.
 *
 * It chdirs to `save` and walks it with the CRT's _findfirst / _findnext,
 * taking every entry that is a DIRECTORY and whose name does not begin with a
 * dot -- which is how "." and ".." are skipped without comparing whole names.
 * Each one becomes a row. Then, once the list exists, the FIRST row's name is
 * copied into the current-player string at ADDR_GAMEPROC_BLOCK, so opening
 * this screen selects a player whether or not anyone clicks.
 *
 * The glob is the literal "*" copied to a local first. The copy is the
 * original's -- _findfirst takes a const char * and would have been happy with
 * the constant -- and it is reproduced because a local buffer is what the
 * stack layout says was there. */
typedef int32_t (__cdecl *AM2_FindFirstFn)(const char *pattern, void *data);
typedef int32_t (__cdecl *AM2_FindNextFn)(int32_t handle, void *data);
typedef int32_t (__cdecl *AM2_FindCloseFn)(int32_t handle);
typedef void (__cdecl *AM2_ReadCampaignFn)(void);

/* The game's own sprintf -- the CRT inside the image, as crt.h explains. */
typedef int32_t (__cdecl *AM2_SprintfFn)(char *, const char *, ...);
#define orig_sprintf    ((AM2_SprintfFn)AM2_IMAGE(ADDR_GAME_SPRINTF))
#define orig_findfirst  ((AM2_FindFirstFn)AM2_IMAGE(ADDR_CRT_FINDFIRST))
#define orig_findnext   ((AM2_FindNextFn)AM2_IMAGE(ADDR_CRT_FINDNEXT))
#define orig_findclose  ((AM2_FindCloseFn)AM2_IMAGE(ADDR_CRT_FINDCLOSE))
#define g_currentPlayer ((char *)(uintptr_t)ADDR_GAMEPROC_BLOCK)

AM2_Widget *__attribute__((thiscall)) SelectPlayerConstruct(AM2_Widget *w,
                                                            const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *bar;
    void       *rows;
    AM2_Rect    box;
    char        pattern[264];
    uint8_t     found[0x11C];
    int32_t     handle;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_SELECT_PLAYER);
    ReadCampaignLevels();
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_SAVE_DIR));
    g_currentPlayer[0] = '\0';

    rows = orig_operator_new(AM2_ROWS_SIZE);
    if (rows)
        rows = RecordCtor(rows, 1);
    *(void **)((uint8_t *)w + COMMPANEL_OFF_LIST) = rows;

    strcpy(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_ALL));
    handle = orig_findfirst(pattern, found);
    if (handle != -1) {
        do {
            const char *name = (const char *)(found + AM2_FIND_OFF_NAME);

            if ((found[AM2_FIND_OFF_ATTRIB] & AM2_FIND_ATTR_DIR)
                    && name[0] != '.')
                ListAdd(*(void **)((uint8_t *)w + COMMPANEL_OFF_LIST), name,
                        (void *)0);
        } while (orig_findnext(handle, found) == 0);
        orig_findclose(handle);
    }

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x7D, 0x62, 0x186, 0x11C);
        panel = PanelConstruct(panel, (const char *)
                                AM2_IMAGE(ADDR_STR_SELECTPLAYER_BMP), 0, box);
    }
    WidgetAddChild(w, panel);
    w->focusedChild = panel;
    panel->flag44 = 1;

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x29, 0x43, 0x96, 0xAB);
        list = ListBoxConstruct(list, box.left, box.top, box.right,
                                box.bottom,
                                *(void **)((uint8_t *)w
                                           + COMMPANEL_OFF_LIST),
                                (int32_t)(uintptr_t)SelectPlayerRow,
                                0, 1);
    }
    WidgetAddChild(panel, list);
    ((AM2_WidgetFocusFn *)list->vtable)[WIDGET_VSLOT_FOCUS](list, 0);

    /* Opening the screen selects the first player. */
    {
        const int32_t *r = *(const int32_t **)((uint8_t *)w
                                               + COMMPANEL_OFF_LIST);

        if (r[0] > 0)
            strcpy(g_currentPlayer, *(const char *const *)(r + 1));
    }

    bar = (AM2_Widget *)orig_operator_new(AM2_ARROWBAR_SIZE);
    if (bar) {
        RectSet(&box, 0xD5, 0x3C, 0x13, 0xBA);
        bar = ArrowBarConstruct(bar, box.left, box.top, box.right,
                                box.bottom, panel,
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                                0x92, 0);
    }
    WidgetAddChild(panel, bar);
    *(AM2_Widget **)((uint8_t *)list + LIST_OFF_ARROWBAR) = bar;
    *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST) = list;

    WidgetAddChild(panel, MakeButton(0x123, 0x44, AM2_BMP_RECRUIT0,
                                     AM2_BMP_RECRUIT1, AM2_BMP_RECRUIT2,
                                       kOnRecruit));
    WidgetAddChild(panel, MakeButton(0x123, 0x6B, AM2_BMP_SELECT0,
                                     AM2_BMP_SELECT1, AM2_BMP_SELECT2,
                                       kOnSelectPlayer));
    WidgetAddChild(panel, MakeButton(0x123, 0x92, AM2_BMP_DELETE0,
                                     AM2_BMP_DELETE1, AM2_BMP_DELETE2,
                                       kOnDeletePlayer));
    WidgetAddChild(panel, MakeButton(0x123, 0xB9, AM2_BMP_BACK0,
                                       AM2_BMP_BACK1, AM2_BMP_BACK2,
                                       kOnMenuBack));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnMenuBack;
    return w;
}

/* 0x00454980, thiscall, `ret 0x18`. The PANEL: a widget whose whole job is to
 * hold a backdrop sprite. Eight of the reconstructed screens hang everything
 * else off one of these, so this is the most-used constructor in the family
 * and the cheapest to check -- every configuration that opens a dialog draws
 * it.
 *
 * It keeps the sprite TWICE, at 0x0038 and at 0x0058. 0x0038 is the base
 * class's own field, which WidgetPaint draws and WidgetRepaint walks the
 * parent chain looking for; 0x0058 is the panel's, and widget.h already
 * records that pairing. Reproduced rather than collapsed: something reads one
 * of them and nothing here establishes which.
 *
 * The rectangle arrives by value and goes straight into the base's four
 * offset fields, and WidgetScreenRect then turns it into the absolute one. */
AM2_Widget *__attribute__((thiscall)) PanelConstruct(AM2_Widget *w,
                                                     const char *bmp,
                                                     int32_t flag,
                                                     AM2_Rect box)
{
    AM2_Sprite *spr;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_PANEL);
    spr = PreloadSpriteName(bmp, flag, 1);
    *(AM2_Sprite **)((uint8_t *)w + PANEL_OFF_SPRITE) = spr;
    w->sprite = spr;
    w->x = box.left;
    w->y = box.top;
    w->w = box.right;
    w->h = box.bottom;
    WidgetScreenRect(w);
    *(int32_t *)((uint8_t *)w + PANEL_OFF_FLAG) = flag;
    return w;
}

/* 0x00450C50, thiscall, `ret 0x2C`. The key-capture ROW -- the class the
 * CONTROLS dialog has twenty-one of. A focus-highlighting label underneath,
 * with four colour bytes at 0x0064..0x0067 and the bound key's index at
 * 0x0068.
 *
 * **It passes an UNINITIALISED byte to the label constructor**, and that is
 * the original's, reproduced. `mov al, byte ptr [esi + 0x64]` at 0x00450C5D
 * reads the object's own 0x0064 BEFORE anything has written it -- the memory
 * is straight out of `operator new` -- and hands it over as the label's ink.
 *
 * It is harmless, and knowing WHY took looking rather than assuming. The
 * label's ink is at 0x0060, not 0x0064, so the garbage lands there; the focus
 * label overrides it with its own pair at 0x0064 and 0x0065, which this
 * function then writes from its arguments. Nothing reads 0x0060 on this
 * class. Reading it back is faithful and, through a `uint8_t *`, is not the
 * undefined behaviour it would be through a wider type.
 *
 * Note the seventh argument is the FONT and the tenth is used twice -- as the
 * label's paper and as the colour at 0x0066. */
AM2_Widget *__attribute__((thiscall)) KeyRowConstruct(AM2_Widget *w,
                                                      int32_t nameIndex,
                                                      const char *caption,
                                                      int32_t left, int32_t top,
                                                      int32_t width,
                                                      int32_t height,
                                                      int32_t font, int32_t ink,
                                                      int32_t ink2, int32_t ink3,
                                                      int32_t ink4)
{
    uint8_t *self = (uint8_t *)w;
    uint8_t  stale = self[FOCUSLABEL_OFF_INK];

    LabelConstruct(w, caption, left, top, width, height, font, stale, ink3);

    self[FOCUSLABEL_OFF_INK]  = (uint8_t)ink;
    w->vtable = (void *)AM2_IMAGE(VTABLE_KEYROW);
    *(int32_t *)(self + KEYROW_OFF_KEY) = nameIndex;
    self[FOCUSLABEL_OFF_INK2] = (uint8_t)ink2;
    self[FOCUSLABEL_OFF_INK3] = (uint8_t)ink3;
    self[FOCUSLABEL_OFF_INK4] = (uint8_t)ink4;
    return w;
}

/* 0x00454B00, thiscall, `ret 8`. The SCREEN BASE -- every one of the twenty
 * screens starts here, so this is the single most-executed constructor in the
 * menu layer and every configuration in the suite runs it.
 *
 * It is a PANEL over the whole screen with the dialog vtable stamped on top.
 * The rectangle is (0, 0, ADDR_SCREEN_W, ADDR_SCREEN_H) -- 640 by 480 read
 * from the image rather than written down, which is why a screen's backdrop
 * covers exactly the display and not a constant somebody chose. */
AM2_Widget *__attribute__((thiscall)) ScreenBaseConstruct(AM2_Widget *w,
                                                          const char *bmp,
                                                          int32_t flag)
{
    AM2_Rect box;

    RectSet(&box, 0, 0, *(const int32_t *)(uintptr_t)ADDR_SCREEN_W,
            *(const int32_t *)(uintptr_t)ADDR_SCREEN_H);
    PanelConstruct(w, bmp, flag, box);
    w->vtable = (void *)AM2_IMAGE(VTABLE_DIALOG);
    w->flag44 = 1;
    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = 0;
    return w;
}

/* 0x004540F0, thiscall, `ret 0x28`. The BUTTON: three sprites, a rectangle,
 * and two handlers.
 *
 * The three bitmaps are loaded by NAME through the same wrapper the panel
 * uses, and all three share the caller's flag. Only the first is tested: a
 * null normal-state bitmap sets 0x0048 as well, which WidgetRepaint reads as
 * "defer to an ancestor" -- so a button with no sprite of its own is drawn by
 * whatever contains it. The other two are loaded unconditionally, so a null
 * there would go in as a null sprite rather than being caught.
 *
 * The normal sprite is also copied to the base's own 0x0038, the field
 * WidgetPaint draws and WidgetRepaint walks the parent chain for. Same
 * doubling as the panel's, and reproduced for the same reason.
 *
 * 0x0075 goes in as 1 -- BUTTON_OFF_OWNS_SPRITES -- which is what makes
 * ButtonDestruct release all three. */
AM2_Widget *__attribute__((thiscall)) ButtonConstruct(AM2_Widget *w,
                                                      const char *b0,
                                                      const char *b1,
                                                      const char *b2,
                                                      int32_t flag,
                                                      AM2_Rect box,
                                                      void (__cdecl *onLeft)(AM2_Widget *),
                                                      void (__cdecl *onRight)(AM2_Widget *))
{
    uint8_t *self = (uint8_t *)w;

    ButtonBaseConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_BUTTON);

    if (b0) {
        *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL) =
            PreloadSpriteName(b0, flag, 1);
    } else {
        *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL) = (AM2_Sprite *)0;
        w->unknown48 = 1;
    }
    *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_FOCUS) =
        PreloadSpriteName(b1, flag, 1);
    *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_PRESSED) =
        PreloadSpriteName(b2, flag, 1);

    w->sprite = *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL);
    w->x = box.left;
    w->y = box.top;
    w->w = box.right;
    w->h = box.bottom;
    self[BUTTON_OFF_OWNS_SPRITES] = 1;
    WidgetScreenRect(w);

    *(void **)(self + BUTTON_OFF_ON_RIGHT) = (void *)onRight;
    *(void **)(self + BUTTON_OFF_ON_LEFT)  = (void *)onLeft;
    return w;
}

/* 0x00454C10, thiscall, `ret 0x34` -- thirteen stack arguments, which is the
 * longest list in the widget hierarchy: the buffer, the maximum, four of
 * rectangle, a font, three colours, the RETURN handler and two more that
 * every call site passes as zero.
 *
 * The seventh argument is the FONT, as it is for the key row, and not a flag.
 *
 * It installs the PERMISSIVE character set from 0x00485304 -- the one with
 * ` ~ ! @ # $ % ^ & in it -- and a caller wanting a narrower field overwrites
 * EDIT_OFF_CHARSET afterwards. That is exactly what ENTER BATTLE NAME does
 * with the letters-and-digits set at 0x00485308, so the two sets are a
 * default and an override rather than two unrelated tables. */
AM2_Widget *__attribute__((thiscall)) EditConstruct(AM2_Widget *w, char *buf,
                                                    int32_t maxChars,
                                                    int32_t left, int32_t top,
                                                    int32_t width,
                                                    int32_t height,
                                                    int32_t font,
                                                    int32_t inkFocus,
                                                    int32_t ink, int32_t paper,
                                                    void (__cdecl *onEnter)(AM2_Widget *),
                                                    int32_t a, int32_t b)
{
    uint8_t *self = (uint8_t *)w;

    WidgetConstruct(w);

    *(char **)(self + EDIT_OFF_TEXT)   = buf;
    *(int32_t *)(self + EDIT_OFF_MAX)  = maxChars;
    *(int32_t *)(self + EDIT_OFF_FONT) = font;
    self[EDIT_OFF_INK_FOCUS] = (uint8_t)inkFocus;
    self[EDIT_OFF_INK]       = (uint8_t)ink;
    self[EDIT_OFF_PAPER]     = (uint8_t)paper;
    *(int32_t *)(self + EDIT_OFF_ARG78) = a;

    w->vtable = (void *)AM2_IMAGE(VTABLE_EDIT);
    *(void **)(self + EDIT_OFF_ON_ENTER) = (void *)onEnter;
    *(int32_t *)(self + EDIT_OFF_ARG7C)  = b;
    *(int32_t *)(self + EDIT_OFF_CARET)  = 0;
    *(const char **)(self + EDIT_OFF_CHARSET) =
        *(const char *const *)(uintptr_t)ADDR_EDIT_CHARSET_DEFAULT;
    *(int32_t *)(self + EDIT_OFF_SCROLL) = 0;

    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    WidgetScreenRect(w);
    return w;
}

/* 0x00456BC0, thiscall, `ret 0x1C`. The two-sprite widget -- the flashing
 * "send" and "receive" dots beside a comms field, and the red dot on every
 * confirm dialog.
 *
 * A PANEL underneath, built from the FIRST bitmap, and then the SECOND
 * bitmap's sprite into sprites[0] with sprites[1] left null and the index
 * zeroed. The first bitmap's sprite is parked at 0x0060, which the painter
 * never reads: it indexes from MULTISPR_OFF_SPRITES. So an index of 0 shows
 * the second bitmap and an index of 1 shows nothing at all -- that null IS
 * the off half of the blink, not an unfilled slot.
 *
 * This looked at first like a contradiction with widget.h's note, which puts
 * the array at 0x0064 while the constructor writes 0x0060 and 0x0064. The
 * PAINTER settles it, and the painter is A/B-verified: it reads
 * `0x0064 + index * 4`. The note was right and the constructor simply has one
 * slot in front of the array. Reading the consumer beats reasoning from the
 * producer.
 *
 * The rectangle is written twice -- once by the panel and again here -- and
 * WidgetScreenRect runs after both. Reproduced. */
AM2_Widget *__attribute__((thiscall)) MultiSpriteConstruct(AM2_Widget *w,
                                                           const char *b0,
                                                           const char *b1,
                                                           int32_t flag,
                                                           AM2_Rect box)
{
    uint8_t *self = (uint8_t *)w;

    PanelConstruct(w, b0, flag, box);

    *(AM2_Sprite **)(self + MULTISPR_OFF_SPRITES + 4) = (AM2_Sprite *)0;
    *(int32_t *)(self + MULTISPR_OFF_INDEX) = 0;
    w->vtable = (void *)AM2_IMAGE(VTABLE_MULTISPRITE);
    *(AM2_Sprite **)(self + MULTISPR_OFF_SPRITE0) = w->sprite;
    *(AM2_Sprite **)(self + MULTISPR_OFF_SPRITES) =
        PreloadSpriteName(b1, flag, 1);

    w->x = box.left;
    w->y = box.top;
    w->w = box.right;
    w->h = box.bottom;
    *(int32_t *)(self + 0x50) = 0;
    WidgetScreenRect(w);
    return w;
}

/* 0x00454F90, thiscall, `ret 0x20`. The LIST BOX -- the DIFFICULTY rows, the
 * connection list and the saved-player list.
 *
 * **A row is fourteen pixels tall**, and that number appears nowhere else. It
 * comes out of a magic-number division: LIST_OFF_VISIBLE is
 * `(height - 4) / 14`, spelled `imul 0x92492493` then `sar 3` and the sign
 * correction.
 *
 * The constant alone does not say the divisor -- 0x92492493 serves 7, 14 and
 * 28 -- and what picks between them is the SHIFT. This one is 3 where 7 would
 * be 2, and reading the constant without the shift gave 7, twice as many rows
 * as fit, and seven map names on a lobby that shows four.
 *
 * The hot row starts at -1 -- nothing under the pointer -- and becomes 0 if
 * the rows it was handed are not empty. Note it tests the ROWS pointer and
 * then its count, so an empty list leaves the hot row at -1 and a list with
 * rows opens with the first one hot. The SELECTED row is 0 either way. */
AM2_Widget *__attribute__((thiscall)) ListBoxConstruct(AM2_Widget *w,
                                                       int32_t left,
                                                       int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       void *rows,
                                                       int32_t callback,
                                                       int32_t arg6C,
                                                       int32_t ownsRows)
{
    uint8_t *self = (uint8_t *)w;

    WidgetConstruct(w);

    w->x = left;
    w->y = top;
    w->w = right;
    w->h = bottom;
    w->vtable = (void *)AM2_IMAGE(VTABLE_LISTBOX);

    *(void **)(self + LIST_OFF_ROWS)       = rows;
    *(int32_t *)(self + LIST_OFF_OWNS_ROWS) = ownsRows;
    *(int32_t *)(self + LIST_OFF_ARG70)     = 0;
    *(int32_t *)(self + LIST_OFF_ARG6C)     = arg6C;
    *(int32_t *)(self + LIST_OFF_CALLBACK)  = callback;
    *(int32_t *)(self + LIST_OFF_HOT)       = -1;
    *(int32_t *)(self + LIST_OFF_SELECTED)  = 0;

    WidgetScreenRect(w);

    *(int32_t *)(self + LIST_OFF_VISIBLE) =
        (bottom - AM2_LIST_ROW_INSET) / AM2_LIST_ROW_HEIGHT;
    *(int32_t *)(self + LIST_OFF_TOP_ROW) = 0;

    *(int32_t *)(self + LIST_OFF_INK) = g_whiteInk;
    *(int32_t *)(self + LIST_OFF_INK_SEL) = g_backgroundColour;
    *(int32_t *)(self + LIST_OFF_INK_SEL_DOWN) = g_hiliteColour;
    *(int32_t *)(self + LIST_OFF_INK_HOT_SEL) =
        *(const uint8_t *)(uintptr_t)ADDR_LIST_INK_HOT_SEL;
    *(int32_t *)(self + LIST_OFF_ARG7C)  = 0;
    *(int32_t *)(self + LIST_OFF_BLINKER) = 0;

    if (rows && *(const int32_t *)rows > 0)
        *(int32_t *)(self + LIST_OFF_HOT) = 0;
    return w;
}

/* 0x00454640, thiscall, `ret 0x2C`. The CHECKBOX -- the 43 boxes on
 * MULTIPLAYER OPTIONS and nothing else in the game.
 *
 * Four sprites, all loaded with a literal flag of 1 rather than the caller's,
 * and the FIRST is copied to the base's 0x0038 as the button and panel do.
 *
 * **Its left-click action is the constructor's, not the caller's**:
 * ADDR_CHECKBOX_TOGGLE goes into BUTTON_OFF_ON_LEFT unconditionally and the
 * caller's handler goes to CHECK_OFF_ON_CHANGE. That is why clicking a plain
 * box only ticks it while a group header also disables its group -- both run
 * the same OptionsSyncGroup, and it is the RECORD INDEX at CHECK_OFF_GROUP
 * that decides whether there is a group to sync.
 *
 * Four hardcoded palette indices go in at 0x0084: 0xD4, 0xD4, 0xFB, 0xFB. */
AM2_Widget *__attribute__((thiscall)) CheckBoxConstruct(AM2_Widget *w,
                                                        const char *b0,
                                                        const char *b1,
                                                        const char *b2,
                                                        const char *b3,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t width,
                                                        int32_t height,
                                                        int32_t group,
                                                        const char *caption,
                                                        void (__cdecl *onChange)(AM2_Widget *))
{
    uint8_t *self = (uint8_t *)w;

    ButtonBaseConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_CHECKBOX);

    *(AM2_Sprite **)(self + CHECK_OFF_SPRITE_ON)  =
        PreloadSpriteName(b0, 1, 1);
    *(AM2_Sprite **)(self + CHECK_OFF_SPRITE_OFF) =
        PreloadSpriteName(b1, 1, 1);
    *(AM2_Sprite **)(self + CHECK_OFF_SPRITE_3)   =
        PreloadSpriteName(b2, 1, 1);
    *(AM2_Sprite **)(self + CHECK_OFF_SPRITE_2)   =
        PreloadSpriteName(b3, 1, 1);
    w->sprite = *(AM2_Sprite **)(self + CHECK_OFF_SPRITE_ON);

    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    self[CHECK_OFF_TICKED] = 0;
    WidgetScreenRect(w);

    *(int32_t *)(self + CHECK_OFF_GROUP)   = group;
    *(const char **)(self + CHECK_OFF_CAPTION) = caption;
    self[CHECK_OFF_INK0]     = 0xD4;
    self[CHECK_OFF_INK0 + 1] = 0xD4;
    self[CHECK_OFF_INK0 + 2] = 0xFB;
    self[CHECK_OFF_INK0 + 3] = 0xFB;
    *(uint32_t *)(self + BUTTON_OFF_ON_LEFT) = (uint32_t)(uintptr_t)CheckboxToggle;
    *(void **)(self + CHECK_OFF_ON_CHANGE) = (void *)onChange;
    self[CHECK_OFF_FLAG8C] = 0;
    return w;
}

/* 0x00455FF0, thiscall, `ret 0x18`. The SCROLL BAR -- the three volume
 * sliders and nothing else.
 *
 * It builds its own two arrows, and widget.h already records how: there is no
 * arrow constructor, so each one is a BUTTON with VTABLE_ARROW stamped over
 * the button's vtable afterwards. Reading this function is what confirms it.
 *
 * Each arrow is built with a NULL first bitmap, which is the path
 * ButtonConstruct tests for: it sets 0x0048 as well, and WidgetRepaint reads
 * that as "defer to an ancestor", so an arrow has no backdrop of its own and
 * the bar behind it is what gets redrawn. That is why the arrows are the one
 * place the null-bitmap branch is taken.
 *
 * The RANGE goes in as a literal 0x14 -- twenty steps -- which is the number
 * the AUDIO dialog divides a position by, and which matches
 * (volume + 2000) / 100 landing in 0..20. Two independent places agreeing on
 * twenty is better evidence than either alone.
 *
 * The right arrow sits at left + width - 9, computed with a `lea` rather than
 * stored. */
static AM2_Widget *MakeArrow(AM2_Widget *bar, int32_t x, int32_t y,
                             uint32_t b1, uint32_t b2,
                             void (__cdecl *handler)(AM2_Widget *))
{
    AM2_Widget *arrow = (AM2_Widget *)orig_operator_new(AM2_ARROW_SIZE);
    AM2_Rect    box;

    if (!arrow)
        return (AM2_Widget *)0;
    RectSet(&box, x, y, 9, 0x13);
    ButtonConstruct(arrow, (const char *)0, (const char *)AM2_IMAGE(b1),
                    (const char *)AM2_IMAGE(b2), 1, box,
                    (void (__cdecl *)(AM2_Widget *))AM2_IMAGE(handler),
                    (void (__cdecl *)(AM2_Widget *))0);
    arrow->vtable = (void *)AM2_IMAGE(VTABLE_ARROW);
    *(AM2_Widget **)((uint8_t *)arrow + ARROW_OFF_OWNER) = bar;
    return arrow;
}

AM2_Widget *__attribute__((thiscall)) ScrollBarConstruct(AM2_Widget *w,
                                                         int32_t left,
                                                         int32_t top,
                                                         int32_t width,
                                                         int32_t height,
                                                         AM2_Widget *parent,
                                                         int32_t span)
{
    uint8_t    *self = (uint8_t *)w;
    AM2_Widget *arrow;

    WidgetConstruct(w);
    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    w->vtable = (void *)AM2_IMAGE(VTABLE_SCROLLBAR);

    arrow = MakeArrow(w, w->x, w->y, AM2_BMP_LTARROW1, AM2_BMP_LTARROW2,
                      kOnArrowLeft);
    *(AM2_Widget **)(self + 0x5C) = arrow;
    WidgetAddChild(parent, arrow);
    *(int32_t *)((uint8_t *)*(AM2_Widget **)(self + 0x5C)
                 + ARROW_OFF_FLAG5C) = 1;

    arrow = MakeArrow(w, w->x + w->w - 9, w->y, AM2_BMP_RTARROW1,
                      AM2_BMP_RTARROW2, kOnArrowRight);
    *(AM2_Widget **)(self + 0x60) = arrow;
    WidgetAddChild(parent, arrow);
    *(int32_t *)((uint8_t *)*(AM2_Widget **)(self + 0x60)
                 + ARROW_OFF_FLAG5C) = 1;

    *(AM2_Sprite **)(self + SCROLLBAR_OFF_BAR) =
        PreloadSpriteName((const char *)AM2_IMAGE(AM2_BMP_HSCROLLBAR),
                             1, 1);
    WidgetScreenRect(w);

    *(int32_t *)(self + SCROLLBAR_OFF_SPAN)  = span;
    *(int32_t *)(self + 0x58)                = 0;
    w->unknown48                             = 0;
    *(int32_t *)(self + SCROLLBAR_OFF_POS)   = 0;
    *(int32_t *)(self + SCROLLBAR_OFF_RANGE) = AM2_SCROLLBAR_RANGE;
    *(int32_t *)(self + SCROLLBAR_OFF_SHIFT) = 0;
    *(int32_t *)(self + 0x68)                = 0;
    *(uint32_t *)(self + SCROLLBAR_OFF_ONCHANGE) = 0;
    return w;
}

/* 0x00455970, thiscall, `ret 0x24` -- NINE stack arguments. The VERTICAL bar
 * with arrows: the connection list's and the saved-player list's.
 *
 * The same shape as the horizontal scroll bar, transposed. Its arrows are
 * 0x13 wide by 9 tall where that one's are 9 by 0x13, the second sits at
 * top + height - 9 rather than left + width - 9, and both are again BUTTONs
 * with a NULL first bitmap and VTABLE_ARROW stamped over.
 *
 * The argument slots were worked out by tracking esp through the function
 * rather than by counting pushes. With nine arguments and five calls in the
 * middle -- one of which pops 0x28 -- reading `[esp + 0x58]` off the page
 * gets a different answer at every point, and three of these slots are read
 * twice at different depths. A dozen lines of Python beat an hour of care. */
static AM2_Widget *MakeVArrow(AM2_Widget *bar, AM2_Widget *parent,
                              int32_t x, int32_t y, uint32_t b1, uint32_t b2,
                              void (__cdecl *handler)(AM2_Widget *),
                              int32_t flag50)
{
    AM2_Widget *arrow = (AM2_Widget *)orig_operator_new(AM2_ARROW_SIZE);
    AM2_Rect    box;

    if (arrow) {
        RectSet(&box, x, y, 0x13, 9);
        ButtonConstruct(arrow, (const char *)0, (const char *)AM2_IMAGE(b1),
                        (const char *)AM2_IMAGE(b2), 1, box,
                        (void (__cdecl *)(AM2_Widget *))AM2_IMAGE(handler),
                        (void (__cdecl *)(AM2_Widget *))0);
        arrow->vtable = (void *)AM2_IMAGE(VTABLE_ARROW);
        *(AM2_Widget **)((uint8_t *)arrow + ARROW_OFF_OWNER) = bar;
    }
    WidgetAddChild(parent, arrow);
    *(int32_t *)((uint8_t *)arrow + ARROWBAR_OFF_FLAG50) = flag50;
    *(int32_t *)((uint8_t *)arrow + ARROW_OFF_FLAG5C)    = 1;
    return arrow;
}

AM2_Widget *__attribute__((thiscall)) ArrowBarConstruct(AM2_Widget *w,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t width,
                                                        int32_t height,
                                                        AM2_Widget *parent,
                                                        const char *b0,
                                                        const char *b1,
                                                        int32_t span,
                                                        int32_t flag50)
{
    uint8_t *self = (uint8_t *)w;

    WidgetConstruct(w);
    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    w->vtable = (void *)AM2_IMAGE(VTABLE_ARROWBAR);

    *(AM2_Widget **)(self + ARROWBAR_OFF_UP) =
        MakeVArrow(w, parent, w->x, w->y, AM2_BMP_UPARROW1, AM2_BMP_UPARROW2,
                   kOnArrowUp, flag50);
    *(AM2_Widget **)(self + ARROWBAR_OFF_DOWN) =
        MakeVArrow(w, parent, w->x, w->y + height - 9, AM2_BMP_DNARROW1,
                   AM2_BMP_DNARROW2, kOnArrowDown, flag50);
    *(int32_t *)(self + ARROWBAR_OFF_FLAG50) = flag50;

    *(AM2_Sprite **)(self + ARROWBAR_OFF_SPRITE0) =
        PreloadSpriteName(b0, 1, 1);
    *(AM2_Sprite **)(self + ARROWBAR_OFF_SPRITE1) =
        PreloadSpriteName(b1, 1, 1);
    WidgetScreenRect(w);

    *(int32_t *)(self + ARROWBAR_OFF_SPAN) = span;
    *(int32_t *)(self + 0x58) = 0;
    w->unknown48              = 0;
    *(int32_t *)(self + 0x70) = 0;
    *(int32_t *)(self + 0x6C) = 0;
    return w;
}

/* 0x004566F0, thiscall, `ret 0x14` -- rectangle then message. The TYPEWRITER,
 * and the WORD-WRAP is the constructor: it does not store the message, it
 * folds it into the 0x400-byte buffer at TYPER_OFF_TEXT with `|` between the
 * lines. widget.h already records that separator; this is where it is put in.
 *
 * The loop keeps two offsets. `start` is what has been committed and `cur` is
 * the end of the longest run known to fit. Each turn it finds the next space,
 * measures [start, space+1), and either remembers that as the new `cur` or --
 * if it is too wide -- commits [start, cur), appends the separator, and moves
 * `start` to `cur` WITHOUT moving `cur`, so the next measurement starts from
 * the same place and the word that did not fit is measured again against an
 * empty line.
 *
 * Two things follow that are worth stating rather than discovering. A word
 * wider than the line commits an EMPTY run and then measures the same word
 * again, so it ends up on a line of its own overflowing rather than being
 * broken. And the tail after the last space is measured once more, so a final
 * word too wide for what is left gets its own line too.
 *
 * The width test is `rect.right - rect.left - 12`, against the ABSOLUTE
 * rectangle -- which is why WidgetScreenRect runs before any of this.
 *
 * TextExtent is called with a NULL `out` and its RETURN used, which is the
 * only caller in the image that does. It was reconstructed as `void`; the
 * original accumulates into eax and the null-`out` branch falls through to
 * the `ret`, so eax is the answer. Fixed in font.cpp with this. */
typedef char *(__cdecl *AM2_StrchrFn)(const char *s, int32_t c);
typedef char *(__cdecl *AM2_StrncpyFn)(char *d, const char *s, uint32_t n);
#define orig_strchr  ((AM2_StrchrFn)AM2_IMAGE(ADDR_CRT_STRCHR))
#define orig_strncpy ((AM2_StrncpyFn)AM2_IMAGE(ADDR_CRT_STRNCPY))

AM2_Widget *__attribute__((thiscall)) TyperConstruct(AM2_Widget *w,
                                                     int32_t left, int32_t top,
                                                     int32_t width,
                                                     int32_t height,
                                                     const char *message)
{
    char    *text = (char *)((uint8_t *)w + TYPER_OFF_TEXT);
    char     line[AM2_TYPER_LINE_MAX];
    int32_t  start = 0;
    int32_t  cur   = 0;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_TYPER);
    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    WidgetScreenRect(w);
    text[0] = '\0';

    for (;;) {
        const char *space = orig_strchr(message + cur, ' ');
        int32_t     avail = w->rect.right - w->rect.left - AM2_TYPER_MARGIN;
        int32_t     next;

        if (!space)
            break;
        next = (int32_t)(space - message) + 1;
        orig_strncpy(line, message + start, (uint32_t)(next - start));
        line[next - start] = '\0';
        if (TextExtent(line, 1, (int32_t *)0) <= avail) {
            cur = next;
            continue;
        }
        orig_strncpy(line, message + start, (uint32_t)(cur - start));
        line[cur - start] = '\0';
        strcat(text, line);
        strcat(text, (const char *)AM2_IMAGE(ADDR_STR_LINE_BREAK));
        start = cur;
    }

    /* The tail after the last space, measured once more. */
    {
        int32_t avail = w->rect.right - w->rect.left - AM2_TYPER_MARGIN;

        strcpy(line, message + start);
        if (TextExtent(line, 1, (int32_t *)0) > avail) {
            orig_strncpy(line, message + start, (uint32_t)(cur - start));
            line[cur - start] = '\0';
            strcat(text, line);
            strcat(text, (const char *)AM2_IMAGE(ADDR_STR_LINE_BREAK));
            start = cur;
        }
    }
    strcat(text, message + start);

    *(uint32_t *)((uint8_t *)w + TYPER_OFF_LAST)    = 0;
    *(int32_t *)((uint8_t *)w + TYPER_OFF_SHOWN)    = 0;
    *(int32_t *)((uint8_t *)w + 0x50)               = 0;
    *(AM2_Widget **)((uint8_t *)w + TYPER_OFF_BLINKER) = (AM2_Widget *)0;
    return w;
}

/* 0x0044D730, cdecl. The TITLE SCREEN -- arm 1 of the menu table, and the one
 * arm that is not a factory: no separate constructor, it builds the whole
 * screen inline. Every configuration in the suite runs it, and `quit` ends on
 * it, so its final frame is this function's output compared at zero.
 *
 * Before any widget: it chdirs back, closes the comm object and drops
 * DirectPlay, and clears the session role and the two saved names. Coming to
 * the title screen is how a multiplayer session is torn down.
 *
 * **It also holds the binary patch that removes MULTI-PLAYER.** `0x0044D8FE`
 * is an ordinary `je` on the allocation in the retail compile and an `EB`
 * here, so that one button is skipped unconditionally -- see
 * docs/binarypatches.md. A reconstruction cannot honour a patch inside the
 * function it replaces, so this asks `restore_multiplayer()` instead. Both
 * routes read AM2_MULTIPLAYER, which is what keeps the two halves of an A/B
 * agreeing: the `orig` side still gets its button from the byte. */
typedef struct {
    int32_t  top;
    void   (__cdecl *handler)(AM2_Widget *);
    uint32_t bmp[3];
} AM2_TitleButton;

static const AM2_TitleButton kTitleButtons[] = {
    { 0x0082, kOnBootCamp,
      { 0x0048B620, 0x0048B638, 0x0048B650 } },  /* 03_102 bootcamp  */
    { 0x00AA, kOnSinglePlayer,
      { 0x0048B5D8, 0x0048B5F0, 0x0048B608 } },  /* 03_100 oneplay   */
    { 0x00D2, kOnMultiPlayer,
      { 0x0048B590, 0x0048B5A8, 0x0048B5C0 } },  /* 03_101 multiplay */
    { 0x00FA, kOnOptionsMenu,
      { 0x0048B548, 0x0048B560, 0x0048B578 } },  /* 03_103 options   */
    { 0x0122, kOnMovies,
      { 0x0048B500, 0x0048B518, 0x0048B530 } },  /* 03_104 movies    */
    { 0x014A, kOnCredits,
      { 0x0048B4B8, 0x0048B4D0, 0x0048B4E8 } },  /* 03_105 credits   */
    { 0x0172, kOnQuit,
      { 0x0048B47C, 0x0048B490, 0x0048B4A4 } },  /* 03_106 quit      */
};
#define AM2_TITLE_MULTIPLAYER_ROW 2

void __cdecl OpenTitleScreen(void)
{
    AM2_Widget *screen;
    uint32_t    i;

    CloseCurrentScreen();
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));
    CommClose();
    CommDropDirectPlay(g_commObject);
    g_mpSession = 0;
    *(char *)(uintptr_t)ADDR_GAMEPROC_BLOCK = '\0';
    *(char *)(uintptr_t)ADDR_GAMEPROC_STR_B = '\0';

    screen = (AM2_Widget *)orig_operator_new(AM2_TITLE_SCREEN_SIZE);
    if (screen)
        screen = ScreenBaseConstruct(screen, (const char *)
                                     AM2_IMAGE(ADDR_STR_SCREEN_BMP), 1);
    g_paintObject = (uint8_t *)screen;
    screen->flag44 = 1;

    for (i = 0; i < sizeof kTitleButtons / sizeof kTitleButtons[0]; i++) {
        const AM2_TitleButton *b = &kTitleButtons[i];
        AM2_Widget            *btn;

        if (i == AM2_TITLE_MULTIPLAYER_ROW && !restore_multiplayer())
            continue;
        btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
        if (btn) {
            AM2_Rect box;

            RectSet(&box, AM2_TITLE_BUTTON_LEFT, b->top,
                    AM2_TITLE_BUTTON_WIDTH, AM2_TITLE_BUTTON_HEIGHT);
            btn = ButtonConstruct(btn, (const char *)AM2_IMAGE(b->bmp[0]),
                                  (const char *)AM2_IMAGE(b->bmp[1]),
                                  (const char *)AM2_IMAGE(b->bmp[2]), 0, box,
                                  b->handler,
                                  (void (__cdecl *)(AM2_Widget *))0);
        }
        WidgetAddChild(screen, btn);
        /* Only the FIRST button, and only here: the original reloads the
         * screen from its global and writes 0x34 once, after BOOT CAMP goes
         * in. So the title screen opens with BOOT CAMP focused and every
         * other row unfocused -- which is the one line the seven blocks do
         * not share, and exactly the kind of line a loop swallows. */
        if (i == 0)
            screen->focusedChild = btn;
    }
}

/* ------------------------------------------------------------------ *
 * The OPTIONS menu's buttons, and the AUDIO dialog's three volume bars.
 *
 * Four of the five below are the same four instructions with one immediate
 * changed -- a menu sound, then a request code and its pending flag -- and
 * the codes are arm numbers in docs/screens.md: 1 the title, 15 CONTROLS,
 * 16 DIFFICULTY, 19 AUDIO. Checked against each other before sharing a
 * helper, which is the habit the title screen's focusedChild cost this
 * session: the four bodies really are identical apart from the code.
 * ------------------------------------------------------------------ */

static void RequestScreen(int32_t code)
{
    PlaySoundAt(2, 0, 0, 0, 0);
    g_menuRequest    = code;
    g_menuRequestSet = 1;
}

/* 0x0044E670. BACK, and the DIFFICULTY dialog's own way out. */
void __cdecl OnMenuBack(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_TITLE);
}

/* 0x0044FD40, 0x0044FD70, 0x0044FDA0: the OPTIONS menu's three entries. */
void __cdecl OnControlsButton(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_CONTROLS);
}

void __cdecl OnDifficultyButton(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_DIFFICULTY);
}

void __cdecl OnAudioButton(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_AUDIO);
}

/* 0x0044EE30. The OK on CONFIRM GAME EXIT: no screen is asked for at all,
 * the game simply goes to state 4. This is the only handler in the family
 * that ends the process, and `ab.sh quit` is the run that takes it. */
void __cdecl OnQuitOk(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    RequestState(4);
}

/* The AUDIO dialog's three bars share their arithmetic and differ only in
 * where the answer goes and what is played to demonstrate it.
 *
 * A bar has twenty-one positions and DirectSound wants hundredths of a
 * decibel of ATTENUATION, so the value is (pos - 20) * 100: 0 at the right
 * and -2000 at the left. The bottom of that range is then replaced by
 * DSBVOLUME_MIN -- silence is a special case, not the end of a ramp. The
 * original writes the -2000 out as a literal and compares against it, which
 * is why the test is on the computed value rather than on `pos == 0`. */
static int32_t BarVolume(const AM2_Widget *bar)
{
    int32_t pos = *(const int32_t *)((const uint8_t *)bar + SCROLLBAR_OFF_POS);
    int32_t vol = (pos - AM2_SCROLLBAR_RANGE) * 100;

    return vol == -2000 ? DSBVOLUME_MIN : vol;
}

/* 0x0044F2A0: sound effects, demonstrated by playing wave 0x27. */
void __cdecl OnVolumeEffects(AM2_Widget *w)
{
    g_volumeAtZero = BarVolume(w);
    PlaySoundAt(0x27, 0, 0, 0, 0);
}

/* 0x0044F2E0: the music stream, which needs no sample -- it is already
 * playing, and SetStreamVolume moves it. */
void __cdecl OnVolumeMusic(AM2_Widget *w)
{
    g_streamVolume = BarVolume(w);
    SetStreamVolume(0, 0);
}

/* 0x0044F320: voices, demonstrated by one line at random out of thirty
 * groups. This is the only route to SpeakLine that does not need a mission
 * -- everywhere else it is a unit reacting to something. */
void __cdecl OnVolumeVoice(AM2_Widget *w)
{
    g_voiceVolume = BarVolume(w);
    SpeakLine(orig_rand() % 30, g_defaultOwner);
}

/* ------------------------------------------------------------------ *
 * The OPTIONS dialogs' OK and CANCEL.
 *
 * Every dialog that can be opened from two places ends the same way, and it
 * is not a menu request in both: in a mission the OPTIONS screen is an
 * overlay, so the exit sets the in-game mode and marks the primary dirty
 * instead. ADDR_GAME_STATE == 2 is the test, and all three of these carry a
 * copy of it.
 * ------------------------------------------------------------------ */

static void ReturnToOptions(void)
{
    if (g_gameState == 2) {
        g_subState     = MENU_MODE_OPTIONS;
        g_overlayDirty = 1;
    } else {
        g_menuRequest    = AM2_MENU_REQUEST_OPTIONS_MENU;
        g_menuRequestSet = 1;
    }
}

/* 0x00451100. CONTROLS' CANCEL, and 0x00451150 calls it as its own last
 * step -- so OK is "write the keys back, save, then cancel". */
void __cdecl OnControlsCancel(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    ReturnToOptions();
}

/* 0x0044F8B0. AUDIO's CANCEL: put the three volumes back from the copy the
 * screen took when it was built, then leave.
 *
 * It reaches the screen by walking `parent` to the top rather than by a fixed
 * number of steps, which is what makes it work from both places -- and it
 * still tests the result for null afterwards, which cannot happen, since the
 * walk starts at the widget itself. Reproduced. */
void __cdecl OnAudioCancel(AM2_Widget *w)
{
    AM2_Widget *screen = w;
    const int32_t *saved;

    while (screen->parent)
        screen = screen->parent;

    PlaySoundAt(2, 0, 0, 0, 0);

    if (screen) {
        saved = (const int32_t *)((const uint8_t *)screen + AUDIO_OFF_SAVED);
        g_volumeAtZero = saved[0];
        g_streamVolume = saved[1];
        g_voiceVolume  = saved[2];
    }
    ReturnToOptions();
}

/* 0x0044F860. The three volumes together, each -2000 turned into silence the
 * way a bar's own handler does it, and then Options.cfg is rewritten.
 *
 * The writer stays original: it is CRT file I/O, which this port replaces
 * wholesale rather than function by function. */
void __cdecl ApplyVolumes(int32_t effects, int32_t music, int32_t voice)
{
    if (effects == -2000)
        effects = DSBVOLUME_MIN;
    if (music == -2000)
        music = DSBVOLUME_MIN;
    if (voice == -2000)
        voice = DSBVOLUME_MIN;
    g_volumeAtZero = effects;
    g_streamVolume = music;
    g_voiceVolume  = voice;
    SaveOptions();
}

/* 0x0044F930. AUDIO's OK: read the three bars, apply, save, leave.
 *
 * The screen is TWO parents up on the title screen and ONE in a mission --
 * the overlay has a level less nesting -- and the original spells that out
 * as a branch rather than walking to the top as CANCEL does. Two functions
 * beside each other solving the same problem differently, and both kept. */
void __cdecl OnAudioOk(AM2_Widget *w)
{
    AM2_Widget       *screen;
    AM2_Widget *const *bars;

    if (g_gameState == 2)
        screen = w->parent;
    else
        screen = w->parent->parent;

    PlaySoundAt(2, 0, 0, 0, 0);

    bars = (AM2_Widget *const *)((const uint8_t *)screen + AUDIO_OFF_BARS);
    ApplyVolumes(BarVolume(bars[0]), BarVolume(bars[1]), BarVolume(bars[2]));
    SetStreamVolume(0, 0);
    ReturnToOptions();
}

/* 0x0044EA80. DIFFICULTY's OK: the list's row, saved and asked for by the
 * OPTIONS menu again. Unlike the AUDIO pair this one does NOT check the game
 * state -- it always asks for menu request 14. */
void __cdecl OnDifficultyOk(AM2_Widget *w)
{
    const uint8_t *list;

    PlaySoundAt(2, 0, 0, 0, 0);
    list = *(const uint8_t *const *)((const uint8_t *)w->parent->parent
                                     + DLG_OFF_LIST);
    g_menuRequest    = AM2_MENU_REQUEST_OPTIONS_MENU;
    g_difficulty     = *(const int32_t *)(list + LIST_OFF_HOT);
    g_menuRequestSet = 1;
    SaveOptions();
}

/* 0x00451150. CONTROLS' OK: write all twenty-one rows back into the key
 * table, save, and then CANCEL -- the original literally calls 0x00451100 as
 * its last step, so "OK" is "apply, then leave the way CANCEL does".
 *
 * The key table is pairs of bytes and only the FIRST of each pair is written
 * here, which is what makes the stride 2 against an array of row pointers
 * with stride 4. */
void __cdecl OnControlsOk(AM2_Widget *w)
{
    uint8_t *binding = (uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS);
    AM2_Widget *const *rows =
        (AM2_Widget *const *)((const uint8_t *)w->parent + KEYROW_PARENT_ROWS);
    const AM2_KeyName *keys = (const AM2_KeyName *)AM2_IMAGE(ADDR_KEY_NAME_TABLE);
    int32_t i;

    for (i = 0; i < KEYROW_ROW_COUNT; i++) {
        int32_t k = *(const int32_t *)((const uint8_t *)rows[i] + KEYROW_OFF_KEY);
        binding[i * 2] = (uint8_t)keys[k].dik;
    }
    SaveOptions();
    PlaySoundAt(2, 0, 0, 0, 0);
    OnControlsCancel(w);
}

/* 0x004511A0. CONTROLS' DEFAULT: put every row back to the built-in binding
 * and repaint it. The defaults are one scancode per row at ADDR_KEY_DEFAULTS;
 * each is turned into a table INDEX by KeyNameIndexOf, which is the form the
 * row stores, and the row's label takes that entry's name.
 *
 * Note it does NOT save and does NOT leave: the table at ADDR_KEY_BINDINGS is
 * untouched until OK is pressed. */
void __cdecl OnControlsDefault(AM2_Widget *w)
{
    const uint8_t *defaults = (const uint8_t *)AM2_IMAGE(ADDR_KEY_DEFAULTS);
    AM2_Widget *const *rows =
        (AM2_Widget *const *)((const uint8_t *)w->parent + KEYROW_PARENT_ROWS);
    const AM2_KeyName *keys = (const AM2_KeyName *)AM2_IMAGE(ADDR_KEY_NAME_TABLE);
    int32_t i;

    for (i = 0; i < KEYROW_ROW_COUNT; i++) {
        AM2_Widget *row = rows[i];
        int32_t     k   = KeyNameIndexOf(defaults[i]);

        *(int32_t *)((uint8_t *)row + KEYROW_OFF_KEY) = k;
        *(const char **)((uint8_t *)row + LABEL_OFF_TEXT) = keys[k].name;
        ((AM2_WidgetPaintFn *)row->vtable)[WIDGET_VSLOT_PAINT](row, row->rect);
    }
    PlaySoundAt(2, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ *
 * The four arrows.
 *
 * Two pairs on two different classes, and only the shapes rhyme. UP and DOWN
 * belong to the ARROW BAR beside a list and move the list's first drawn row;
 * LEFT and RIGHT belong to the SCROLL BAR and move its own position, then
 * fire its onChange -- which is how clicking an arrow on the AUDIO dialog
 * reaches OnVolumeEffects.
 *
 * All four end by repainting through the nearest ancestor with a sprite, and
 * all four play wave 1 rather than the menus' wave 2.
 * ------------------------------------------------------------------ */

/* The thumb's offset along the bar, in both classes' own terms. The original
 * computes it in x87 and rounds through _ftol, which truncates toward zero;
 * both operands are non-negative here, so a C cast is the same function. */
static int32_t ThumbShift(int32_t pos, int32_t range, int32_t travel)
{
    return (int32_t)((double)pos / (double)range * (double)travel);
}

/* 0x004557F0 and 0x004558B0. The list scrolls by one row; the guards are the
 * two ends of the range and the DOWN one computes its own limit, count less
 * how many rows fit, rather than keeping it anywhere. */
static void ArrowBarScroll(AM2_Widget *arrow, int32_t delta)
{
    AM2_Widget *bar  = *(AM2_Widget **)((uint8_t *)arrow + ARROW_OFF_OWNER);
    AM2_Widget *list = *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST);
    uint8_t    *l;
    uint8_t    *b;
    const AM2_Sprite *thumb;
    int32_t     top;
    int32_t     visible;
    int32_t     count;

    if (!list)
        return;
    l = (uint8_t *)list;
    b = (uint8_t *)bar;
    top     = *(const int32_t *)(l + LIST_OFF_TOP_ROW);
    visible = *(const int32_t *)(l + LIST_OFF_VISIBLE);
    count   = **(const int32_t *const *)(l + LIST_OFF_ROWS);

    if (delta < 0 ? top <= 0 : top >= count - visible)
        return;

    PlaySoundAt(1, 0, 0, 0, 0);
    top += delta;
    *(int32_t *)(l + LIST_OFF_TOP_ROW) = top;
    ((AM2_WidgetPaintFn *)list->vtable)[WIDGET_VSLOT_PAINT](list, list->rect);

    thumb = *(const AM2_Sprite *const *)(b + ARROWBAR_OFF_SPRITE0);
    *(int32_t *)(b + ARROWBAR_OFF_SHIFT) =
        ThumbShift(top, count - visible,
                   *(const int32_t *)(b + ARROWBAR_OFF_SPAN)
                   - thumb->bounds.bottom);
    RepaintAncestor(bar, bar->rect);
}

void __cdecl OnArrowUp(AM2_Widget *w)
{
    ArrowBarScroll(w, -1);
}

void __cdecl OnArrowDown(AM2_Widget *w)
{
    ArrowBarScroll(w, 1);
}

/* 0x00455D60, thiscall, no stack arguments. The bar's other mover: not a step
 * but a JUMP TO THE END, and it exists because the chat log grows from
 * underneath. MenuMessage appends a line and calls this, so the newest line is
 * always the one on screen.
 *
 * It is ArrowBarScroll's tail with the step replaced by a clamp, and the
 * comparison is one-sided: it raises the top row to `count - visible` and
 * never lowers it. So the bar cannot follow a list that SHRANK -- nothing here
 * shrinks one, since the log is trimmed from the oldest end and the trim moves
 * the count and the content together.
 *
 * The consequence for a reader who has scrolled up is the ordinary one: the
 * next message pulls them back to the bottom. That is the original's
 * behaviour and not a defect of it.
 *
 * The name it went in under -- ADDR_CHATBOX_REFLOW -- was taken from the one
 * call site, which reaches it as `[chatbox + 0x7C]`. That field is
 * LIST_OFF_ARROWBAR and the object is the bar, not the box. Renamed, not
 * aliased. */
void __attribute__((thiscall)) ArrowBarFollowEnd(AM2_Widget *bar)
{
    uint8_t          *b = (uint8_t *)bar;
    AM2_Widget       *list = *(AM2_Widget **)(b + ARROWBAR_OFF_LIST);
    const AM2_Sprite *thumb;
    uint8_t          *l;
    int32_t           top;
    int32_t           visible;
    int32_t           count;

    if (!list)
        return;
    l = (uint8_t *)list;

    count   = **(const int32_t *const *)(l + LIST_OFF_ROWS);
    visible = *(const int32_t *)(l + LIST_OFF_VISIBLE);
    if (*(const int32_t *)(l + LIST_OFF_TOP_ROW) >= count - visible)
        return;
    *(int32_t *)(l + LIST_OFF_TOP_ROW) = count - visible;

    ((AM2_WidgetPaintFn *)list->vtable)[WIDGET_VSLOT_PAINT](list, list->rect);

    /* Every field is loaded again after the paint, the list pointer included.
     * Reproduced: the painter is a virtual and could have moved any of them,
     * and which loads the original chose to repeat is evidence about what it
     * thought could change. */
    list    = *(AM2_Widget **)(b + ARROWBAR_OFF_LIST);
    l       = (uint8_t *)list;
    top     = *(const int32_t *)(l + LIST_OFF_TOP_ROW);
    count   = **(const int32_t *const *)(l + LIST_OFF_ROWS);
    visible = *(const int32_t *)(l + LIST_OFF_VISIBLE);

    thumb = *(const AM2_Sprite *const *)(b + ARROWBAR_OFF_SPRITE0);
    *(int32_t *)(b + ARROWBAR_OFF_SHIFT) =
        ThumbShift(top, count - visible,
                   *(const int32_t *)(b + ARROWBAR_OFF_SPAN)
                   - thumb->bounds.bottom);
    RepaintAncestor(bar, bar->rect);
}

/* 0x00455ED0 and 0x00455F60. The scroll bar's own position, one step, and
 * then its onChange if it has one -- which the AUDIO dialog's three bars do,
 * so an arrow click is a volume change. Nothing here touches a list. */
static void ScrollBarStep(AM2_Widget *arrow, int32_t delta)
{
    AM2_Widget *bar = *(AM2_Widget **)((uint8_t *)arrow + ARROW_OFF_OWNER);
    uint8_t    *b   = (uint8_t *)bar;
    const AM2_Sprite *thumb;
    void      (__cdecl *onChange)(AM2_Widget *);
    int32_t     pos   = *(const int32_t *)(b + SCROLLBAR_OFF_POS);
    int32_t     range = *(const int32_t *)(b + SCROLLBAR_OFF_RANGE);

    /* The guard skips the MOVE and not the NOTIFICATION -- both arms fall
     * into the same tail in the original, so holding an arrow against the end
     * of the track keeps calling onChange with an unchanged position. */
    if (delta < 0 ? pos > 0 : pos < range) {
        PlaySoundAt(1, 0, 0, 0, 0);
        pos += delta;
        *(int32_t *)(b + SCROLLBAR_OFF_POS) = pos;

        thumb = *(const AM2_Sprite *const *)(b + SCROLLBAR_OFF_BAR);
        *(int32_t *)(b + SCROLLBAR_OFF_SHIFT) =
            ThumbShift(pos, range,
                       *(const int32_t *)(b + SCROLLBAR_OFF_SPAN)
                       - thumb->bounds.right);
        RepaintAncestor(bar, bar->rect);
    }

    onChange = *(void (__cdecl *const *)(AM2_Widget *))
                   (b + SCROLLBAR_OFF_ONCHANGE);
    if (onChange)
        onChange(bar);
}

void __cdecl OnArrowLeft(AM2_Widget *w)
{
    ScrollBarStep(w, -1);
}

void __cdecl OnArrowRight(AM2_Widget *w)
{
    ScrollBarStep(w, 1);
}

/* ------------------------------------------------------------------ *
 * SELECT PLAYER's three buttons, DELETE PLAYER's CANCEL, and the REPLAY
 * prompt's OK.
 *
 * The player's name lives in ADDR_GAMEPROC_BLOCK and an EMPTY one is what
 * these test: three of them measure it with strlen and refuse with wave 3,
 * the game's "no". Nothing else about the row is consulted -- SELECT PLAYER
 * does not look at the list at all, because clicking a row has already
 * copied the name in.
 * ------------------------------------------------------------------ */

#define g_loadPending     (*(int32_t *)(uintptr_t)ADDR_LOAD_PENDING)
#define g_missionRetry    (*(int32_t *)(uintptr_t)ADDR_MISSION_RETRY)
#define g_levelId         (*(int32_t *)(uintptr_t)ADDR_LEVEL_ID)
#define g_levelIndex      (*(int32_t *)(uintptr_t)ADDR_LEVEL_INDEX)
/* Named as army.cpp already names it, not `g_ourArmy` -- the address is the
 * same one and a second name for it is what the globals ratchet is for. */
#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
#define g_rulesChecksum    (*(uint32_t *)(uintptr_t)ADDR_RULES_CHECKSUM_VAL)
#define g_mpScriptChecksum (*(uint32_t *)(uintptr_t)ADDR_MP_SCRIPT_CHKSUM_VAL)
#define g_mapChecksum      (*(uint32_t *)(uintptr_t)ADDR_MAP_CHECKSUM_VAL)

typedef void *(__cdecl *am2_find_level_fn)(int32_t id);

/* 0x00451300. RECRUIT: no test at all, straight to ENTER NAME. */
void __cdecl OnRecruit(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_ENTER_NAME);
}

/* 0x00451330. DELETE: refuse with wave 3 when no player is selected. */
void __cdecl OnDeletePlayer(AM2_Widget *w)
{
    (void)w;
    if (!strlen(g_currentPlayer)) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }
    RequestScreen(AM2_MENU_REQUEST_DEL_PLAYER);
}

/* 0x00451380. SELECT: the name must be there AND level 1 must be in the
 * table, and BOTH failures give the same refusal. The level id it stores is
 * the literal 1 rather than the record's own field, which is where this
 * differs from the Boot Camp button. */
void __cdecl OnSelectPlayer(AM2_Widget *w)
{
    void *level = (void *)0;

    (void)w;
    if (strlen(g_currentPlayer))
        level = FindLevelRecord(1);
    if (!level) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }

    SelectLevel(level);
    g_levelId    = 1;
    g_levelIndex = 1;
    PlaySoundAt(2, 0, 0, 0, 0);
    g_menuRequest    = AM2_MENU_REQUEST_LOAD_GAME;
    g_menuRequestSet = 1;
}

/* 0x00450A10. DELETE PLAYER's CANCEL, with the same two exits as the OPTIONS
 * dialogs -- an in-game overlay mode or a menu request -- but a different
 * pair of codes: 0x1A and 3, back to SELECT PLAYER. */
void __cdecl OnDelPlayerCancel(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    if (g_gameState == 2) {
        g_subState     = AM2_MENU_MODE_DEL_PLAYER;
        g_overlayDirty = 1;
    } else {
        g_menuRequest    = AM2_MENU_REQUEST_SELECT_PLAYER;
        g_menuRequestSet = 1;
    }
}

/* 0x0044F1B0. The REPLAY prompt's OK -- "do you wish to reattempt your failed
 * mission?" -- and it is the route to LoadGame this project has been looking
 * for. With the second name set it raises ADDR_LOAD_PENDING, which the
 * state-2 entry consumes by validating the save and loading it; without it,
 * the mission simply restarts from level index 1.
 *
 * Both arms set ADDR_MISSION_RETRY, which the teardown turns into the
 * "Attempt# %d" line. */
void __cdecl OnReplayOk(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    if (*(const char *)(uintptr_t)ADDR_GAMEPROC_STR_B) {
        g_loadPending = 1;
        PlaySoundAt(2, 0, 0, 0, 0);
        RequestState(2);
    } else {
        RequestState(2);
        g_levelIndex = 1;
    }
    g_missionRetry = 1;
}

/* 0x00453940, thiscall. The three-field record's RESET: free every row's own
 * string, free the array, and clear the count and the pointer.
 *
 * The per-row free is gated on the third field and the loop on the count, but
 * the array itself is freed either way -- free(NULL) is a no-op and the
 * original leans on that rather than testing. The row stride is 0x104 and the
 * string it owns is the LAST four bytes of the row, which is the same layout
 * the comm panel's connection pointer uses. */
void __attribute__((thiscall)) RecordReset(void *rec)
{
    int32_t *r     = (int32_t *)rec;
    uint8_t *rows  = (uint8_t *)(uintptr_t)r[1];
    int32_t  i;

    if (r[2] && rows) {
        for (i = 0; i < r[0]; i++) {
            void *own = *(void **)(rows + (uint32_t)i * AM2_LIST_ROW_STRIDE
                                   + AM2_LIST_ROW_VALUE);
            if (own)
                am2_free(own);
        }
    }
    r[0] = 0;
    am2_free(rows);
    r[1] = 0;
}

/* 0x004512A0. SELECT PLAYER's row callback -- what a list box calls when its
 * selection moves. The dispatch is `callback(list, rows, selected)` and this
 * one ignores the list, which is why its arguments start at the SECOND slot.
 *
 * All it does is copy the chosen name into ADDR_GAMEPROC_BLOCK, which is why
 * the three buttons beside it never look at the list: by the time one is
 * pressed the name is already there. */
void __cdecl SelectPlayerRow(AM2_Widget *list, AM2_ListRows *rows,
                             int32_t selected)
{
    (void)list;
    if (!rows || selected < 0 || selected >= rows->count)
        return;
    strcpy(g_currentPlayer, rows->text + (uint32_t)selected * AM2_LIST_ROW_STRIDE);
    PlaySoundAt(2, 0, 0, 0, 0);
}

/* Both editing arms end the same way: the field's own change callback, then
 * a repaint through its slot 1. The global is re-read between the two,
 * because the callback is free to move the focus. */
static void EditNotifyAndRepaint(void)
{
    AM2_Widget *edit = g_focusedEdit;
    void      (__cdecl *onChange)(AM2_Widget *) =
        *(void (__cdecl *const *)(AM2_Widget *))((uint8_t *)edit
                                                 + EDIT_OFF_ARG78);

    if (onChange)
        onChange(edit);
    edit = g_focusedEdit;
    ((AM2_WidgetPaintFn *)edit->vtable)[WIDGET_VSLOT_PAINT](edit, edit->rect);
}

/* 0x0044D520. The text field's WM_CHAR consumer -- the function
 * EditTakeFocus installs into g_charHandler, and the whole of a field's
 * typing behaviour. WndProc hands it (ch, lParam low, lParam high) and it
 * reads only the first; the other two are the repeat count and the flags,
 * which nothing here wants.
 *
 * It works on the FOCUSED field rather than on an argument, re-reading the
 * global after anything that could have changed it. With no field focused it
 * says so -- "Error: Key handler not freed" -- which is the harness's own
 * diagnosis of a handler that outlived its widget.
 *
 * Four classes of character and they are tested in this order:
 *   printable 0x20..0x7F except '^', AND in the field's own character set
 *   backspace
 *   RETURN, which fires the field's on-enter
 *   TAB, which is silently swallowed; anything else is refused with wave 3.
 *
 * The printable arm has TWO refusals of its own, both wave 2: the text would
 * be wider than the field, or it is already at the field's maximum. The width
 * test runs BEFORE the character is added, on the text as it stands plus a
 * 12-pixel margin, so the field stops one character early rather than
 * overflowing and backing out. */
#define EDIT_TEXT_MARGIN 12

void __cdecl EditCharHandler(uint32_t ch, uint32_t lo, uint32_t hi)
{
    AM2_Widget *edit = g_focusedEdit;
    uint8_t    *self;
    char       *text;
    int32_t     len;

    (void)lo;
    (void)hi;
    if (!edit) {
        orig_log((const char *)AM2_IMAGE(ADDR_STR_KEY_HANDLER_LEAK));
        return;
    }

    /* A field with a blinker beside it restarts it on every keystroke, and
     * plays wave 0 while it does. The global is re-read afterwards because
     * the blinker's own code can move the focus. */
    if (*(AM2_Widget **)((uint8_t *)edit + EDIT_OFF_DOT)) {
        BlinkerStart(*(AM2_Widget **)((uint8_t *)edit + EDIT_OFF_DOT),
                     0x46, 1);
        PlaySoundAt(0, 0, 0, 0, 0);
        edit = g_focusedEdit;
    }

    self = (uint8_t *)edit;
    text = *(char **)(self + EDIT_OFF_TEXT);
    len  = (int32_t)strlen(text);

    if (ch >= 0x20 && ch < 0x80 && ch != '^'
        && strchr(*(const char *const *)(self + EDIT_OFF_CHARSET), (int)ch)) {
        int32_t width = TextExtent(*(const char *const *)(self + EDIT_OFF_TEXT),
                                   *(const int32_t *)(self + EDIT_OFF_FONT), 0)
                        + EDIT_TEXT_MARGIN;

        edit = g_focusedEdit;
        self = (uint8_t *)edit;
        if (width > edit->w
            || len >= *(const int32_t *)(self + EDIT_OFF_MAX) - 1) {
            PlaySoundAt(2, 0, 0, 0, 0);
            return;
        }
        text = *(char **)(self + EDIT_OFF_TEXT);
        text[len]     = (char)ch;
        text[len + 1] = '\0';
        EditNotifyAndRepaint();
        return;
    }

    if (ch == 8) {
        if (len <= 0) {
            PlaySoundAt(2, 0, 0, 0, 0);
            return;
        }
        text[len - 1] = '\0';
        EditNotifyAndRepaint();
        return;
    }

    if (ch == 0x0D) {
        void (__cdecl *onEnter)(AM2_Widget *) =
            *(void (__cdecl *const *)(AM2_Widget *))(self + EDIT_OFF_ON_ENTER);

        if (onEnter)
            onEnter(edit);
        return;
    }

    if (ch != 9)
        PlaySoundAt(3, 0, 0, 0, 0);
}

/* 0x004542F0, thiscall. The base BUTTON's constructor: the widget base, then
 * its own vtable, then three fields cleared. Every three-state button and
 * every checkbox derives from it, and it returns `this` the way an i386 MSVC
 * constructor does -- which is the mistake that killed the multiplayer path
 * for eleven commits, so tools/checkthis.py now refuses a `void` here. */
AM2_Widget *__attribute__((thiscall)) ButtonBaseConstruct(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;

    WidgetConstruct(w);
    w->vtable = (void *)AM2_IMAGE(VTABLE_BUTTON_BASE);
    *(int32_t *)(self + BUTTON_BASE_OFF_A) = 0;
    *(int32_t *)(self + BUTTON_BASE_OFF_B) = 0;
    *(int32_t *)(self + BUTTON_BASE_OFF_C) = 0;
    return w;
}

/* 0x00454760. Every checkbox's LEFT-click handler, written by the CONSTRUCTOR
 * and not by the caller -- which is why clicking a plain box just ticks it and
 * only a group header does anything else: the caller's handler is at 0x7C and
 * this is what calls it.
 *
 * The tick flips with `sete` on the old value, so it is a strict toggle and
 * not a set. Wave 1, not the menus' wave 2. */
void __cdecl CheckboxToggle(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    void   (__cdecl *onChange)(AM2_Widget *);

    *(self + CHECK_OFF_TICKED) = (uint8_t)(*(self + CHECK_OFF_TICKED) == 0);
    PlaySoundAt(1, 0, 0, 0, 0);
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);

    onChange = *(void (__cdecl *const *)(AM2_Widget *))
                   (self + CHECK_OFF_ON_CHANGE);
    if (onChange)
        onChange(w);
}

/* 0x0044DBB0, thiscall. SELECT MAP -- the campaign's level picker, and the
 * one screen whose list comes out of a PARSED FILE rather than the
 * filesystem or the comm object.
 *
 * It reparses campaign.txt on every open (the same ADDR_READ_CAMPAIGN_FILE
 * SELECT PLAYER calls) and then walks the level table by ID from 1, adding
 * each record's display name with a malloc'd copy of the id beside it. That
 * id is what the row callback reads back; the rows own it, which is why the
 * record is constructed with its third field set.
 *
 * The loop bound is re-read every iteration and the comparison is on
 * `i - 1`, so it runs for ids 1..count -- a table whose ids are not
 * contiguous would simply skip the gaps, since a missing record is a NULL
 * from the lookup and not an error. */
AM2_Widget *__attribute__((thiscall)) SelectMapConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *bar;
    void       *rows;
    AM2_Rect    box;
    int32_t     i;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_SELECT_MAP);
    ReadCampaignLevels();

    rows = orig_operator_new(AM2_ROWS_SIZE);
    if (rows)
        rows = RecordCtor(rows, 1);
    *(void **)((uint8_t *)w + COMMPANEL_OFF_LIST) = rows;

    for (i = 1; i - 1 < g_levelCount; i++) {
        uint8_t *rec = (uint8_t *)FindLevelRecord(i);

        if (rec) {
            int32_t *id = (int32_t *)am2_malloc(sizeof *id);

            *id = i;
            ListAdd(*(void **)((uint8_t *)w + COMMPANEL_OFF_LIST),
                    (const char *)(rec + LEVEL_OFF_NAME), id);
        }
    }

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x7D, 0x62, 0x186, 0x11C);
        panel = PanelConstruct(panel, (const char *)
                                AM2_IMAGE(ADDR_STR_SELECTMAP_BMP), 0, box);
    }
    WidgetAddChild(w, panel);
    w->focusedChild = panel;
    panel->flag44 = 1;

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x2A, 0x44, 0x95, 0xAA);
        list = ListBoxConstruct(list, box.left, box.top, box.right,
                                box.bottom,
                                *(void **)((uint8_t *)w
                                           + COMMPANEL_OFF_LIST),
                                (int32_t)(uintptr_t)SelectMapRow,
                                0, 1);
    }
    WidgetAddChild(panel, list);
    ((AM2_WidgetFocusFn *)list->vtable)[WIDGET_VSLOT_FOCUS](list, 0);

    bar = (AM2_Widget *)orig_operator_new(AM2_ARROWBAR_SIZE);
    if (bar) {
        RectSet(&box, 0xD5, 0x3C, 0x13, 0xBA);
        bar = ArrowBarConstruct(bar, box.left, box.top, box.right,
                                box.bottom, panel,
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                                0x92, 1);
    }
    WidgetAddChild(panel, bar);
    *(AM2_Widget **)((uint8_t *)list + LIST_OFF_ARROWBAR) = bar;
    *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST) = list;

    WidgetAddChild(panel, MakeButton(0x123, 0xAA, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, kOnMenuBack));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnMenuBack;
    return w;
}

/* 0x00451AF0, thiscall. ENTER NAME -- RECRUIT's dialog, and the simplest of
 * the screens that own an edit box: a panel, one field writing into the
 * screen's own buffer, OK, CANCEL and a green dot.
 *
 * It clears the buffer before building anything, so the field always opens
 * empty; the name that was there is not offered back. Its OK handler doubles
 * as the field's ON-ENTER, so RETURN in the field is the same as clicking OK
 * -- one address in two slots, which is the shape ENTER BATTLE NAME has too.
 *
 * The character set is ADDR_EDIT_CHARSET_PTR rather than the constructor's
 * default, installed AFTER the constructor exactly as ENTER BATTLE NAME
 * installs its own. */
AM2_Widget *__attribute__((thiscall)) EnterNameConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *edit;
    AM2_Widget *dot;
    AM2_Rect    box;
    char       *text = (char *)w + ENTER_NAME_OFF_TEXT;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_ENTER_NAME);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_SAVE_DIR));
    text[0] = '\0';

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0xAE, 0x1A7, 0x83);
        panel = PanelConstruct(panel, (const char *)
                                AM2_IMAGE(ADDR_STR_NAME_BMP), 0, box);
    }
    WidgetAddChild(w, panel);
    w->focusedChild = panel;
    panel->flag44 = 1;

    edit = (AM2_Widget *)orig_operator_new(AM2_EDIT_SIZE);
    if (edit) {
        RectSet(&box, 0x24, 0x39, 0xF7, 0x12);
        edit = EditConstruct(edit, text, AM2_ENTER_NAME_MAX, box.left,
                             box.top, box.right, box.bottom, 1,
                             g_hiliteColour, g_whiteInk, g_backgroundColour,
                             kImageHandler(ADDR_ON_ENTER_NAME_OK), 0, 0);
    }
    WidgetAddChild(panel, edit);
    ((AM2_WidgetFocusFn *)edit->vtable)[WIDGET_VSLOT_FOCUS](edit, 0);
    *(const char **)((uint8_t *)edit + EDIT_OFF_CHARSET) = g_editCharset;

    WidgetAddChild(panel, MakeButton(0x14A, 0x1F, AM2_BMP_OK0, AM2_BMP_OK1,
                                     AM2_BMP_OK2,
                                     kImageHandler(ADDR_ON_ENTER_NAME_OK)));
    WidgetAddChild(panel, MakeButton(0x14A, 0x48, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2,
                                     kOnEnterNameCancel));

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, 0x23, 0x68, 0x11, 0x10);
        dot = MultiSpriteConstruct(dot, (const char *)AM2_IMAGE(AM2_BMP_GREEN0),
                                    (const char *)AM2_IMAGE(AM2_BMP_GREEN1), 1,
                                    box);
    }
    WidgetAddChild(panel, dot);
    *(AM2_Widget **)((uint8_t *)edit + EDIT_OFF_DOT) = dot;

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)(uintptr_t)kOnEnterNameCancel;
    return w;
}

/* 0x0044FE50, thiscall. DELETE GAME -- and the ONE screen in the table that
 * is built two different ways.
 *
 * In a mission it has no panel at all: the children go straight onto the
 * screen and every rectangle carries the offset the panel would have
 * supplied, 0x6C by 0x98. On the title screen the panel is made, the offset
 * becomes zero, and the same four children go into it instead. So the
 * coordinates in the source are the SAME numbers either way and the parent
 * and the offset move together -- which is why reading it as two layouts
 * gets the arithmetic wrong.
 *
 * `flag` is passed straight through to the base rather than being consumed
 * here, which is what makes this constructor take two arguments where most
 * take one. */
AM2_Widget *__attribute__((thiscall)) DeleteGameConstruct(AM2_Widget *w,
                                                          const char *bmp,
                                                          int32_t flag)
{
    AM2_Widget *parent = w;
    AM2_Widget *ok;
    AM2_Widget *msg;
    AM2_Widget *dot;
    AM2_Rect    box;
    int32_t     dx = 0x6C;
    int32_t     dy = 0x98;

    ScreenBaseConstruct(w, bmp, flag);
    w->vtable = (void *)AM2_IMAGE(VTABLE_DELETE_GAME);

    if (g_gameState == 2) {
        w->flag44 = 1;
    } else {
        AM2_Widget *panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);

        if (panel) {
            RectSet(&box, 0x6C, 0x98, 0x1A7, 0xB0);
            panel = PanelConstruct(panel, (const char *)
                                    AM2_IMAGE(ADDR_STR_DELGAME_BMP), 0, box);
        }
        WidgetAddChild(w, panel);
        dx = 0;
        dy = 0;
        panel->flag44 = 1;
        parent = panel;
    }

    ok = MakeButton(dx + 0x149, dy + 0x38, AM2_BMP_OK0, AM2_BMP_OK1,
                    AM2_BMP_OK2, kImageHandler(ADDR_ON_DELGAME_OK));
    WidgetAddChild(parent, ok);
    parent->focusedChild = ok;

    WidgetAddChild(parent,
                   MakeButton(dx + 0x149, dy + 0x61, AM2_BMP_CAN0,
                              AM2_BMP_CAN1, AM2_BMP_CAN2,
                              kOnDelGameCancel));

    msg = (AM2_Widget *)orig_operator_new(AM2_TYPER_SIZE);
    if (msg) {
        RectSet(&box, dx + 0x28, dy + 0x41, 0xF0, 0x34);
        msg = TyperConstruct(msg, box.left, box.top, box.right, box.bottom,
                             (const char *)AM2_IMAGE(ADDR_STR_DELGAME_ASK));
    }
    WidgetAddChild(parent, msg);

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, dx + 0x23, dy + 0x95, 0x11, 0x10);
        dot = MultiSpriteConstruct(dot, (const char *)AM2_IMAGE(AM2_BMP_RED0),
                                    (const char *)AM2_IMAGE(AM2_BMP_RED1), 1,
                                    box);
    }
    WidgetAddChild(parent, dot);
    *(AM2_Widget **)((uint8_t *)msg + TYPER_OFF_BLINKER) = dot;

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)(uintptr_t)kOnDelGameCancel;
    return w;
}

/* 0x0044DFA0, thiscall. MOVIES -- twelve thumbnails in three pages of four,
 * and the only screen that builds its buttons out of SPRITES it loaded itself
 * rather than out of bitmap names.
 *
 * Each button is constructed with the scratch buffer as all three of its
 * bitmap names -- the same `ADDR_DIR_SCRATCH` every dialog hands SetGameDir,
 * whose contents are whatever was last written there -- and then the three
 * sprite fields are overwritten from the preloaded pair. So the names are
 * never used; the construction just has to not fail. `OWNS_SPRITES` is
 * cleared afterwards, which is what stops the destructor releasing sprites
 * the screen preloaded and still holds.
 *
 * The pair is (a, b) and the button takes b for NORMAL and a for both FOCUS
 * and PRESSED -- a, twice, from two separate reads of the same slot.
 *
 * The three later buttons are gated on how many movies are unlocked, and so
 * is the page button: with fewer than three there is nothing to page to. */
static AM2_Widget *MakeMovieButton(AM2_Widget *w, AM2_Widget *panel,
                                   int32_t left, int32_t top, int32_t slot,
                                   void (__cdecl *handler)(AM2_Widget *))
{
    AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    AM2_Sprite *const *pair;
    uint8_t    *self;
    AM2_Rect    box;
    int32_t     idx;

    if (btn) {
        RectSet(&box, left, top, 0x90, 0x90);
        btn = ButtonConstruct(btn,
                              (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                              (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                              (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH),
                              1, box, handler,
                              (void (__cdecl *)(AM2_Widget *))0);
    }

    idx  = g_moviePage * AM2_MOVIE_PAGE_SIZE + slot;
    self = (uint8_t *)btn;
    pair = (AM2_Sprite *const *)((const uint8_t *)w + MOVIES_OFF_SPRITES
                                 + (uint32_t)idx * 8);
    *(int32_t *)(self + MOVIE_BUTTON_OFF_INDEX)              = idx;
    *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL)        = pair[1];
    *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_FOCUS)         = pair[0];
    *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_PRESSED)       = pair[0];
    self[BUTTON_OFF_OWNS_SPRITES]                            = 0;
    WidgetAddChild(panel, btn);
    return btn;
}

AM2_Widget *__attribute__((thiscall)) MoviesConstruct(AM2_Widget *w,
                                                      const char *bmp)
{
    AM2_Widget  *panel;
    AM2_Widget **slots = (AM2_Widget **)((uint8_t *)w + MOVIES_OFF_BUTTONS);
    AM2_Sprite **spr   = (AM2_Sprite **)((uint8_t *)w + MOVIES_OFF_SPRITES);
    AM2_Rect     box;
    int32_t      i;
    int32_t      at = 0;

    ScreenBaseConstruct(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_MOVIES);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_ALPINE));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x43, 0x20, 0x1FA, 0x19F);
        panel = PanelConstruct(panel, (const char *)
                                AM2_IMAGE(ADDR_STR_MOVIES_BMP), 0, box);
    }
    WidgetAddChild(w, panel);
    panel->flag44 = 1;

    /* Four, then eight, and the two runs of ids are not contiguous. */
    for (i = 0; i < 4; i++) {
        spr[at++] = PreloadSprite(AM2_MOVIE_SET, AM2_MOVIE_INDEX_A + i, 0, 1, 1);
        spr[at++] = PreloadSprite(AM2_MOVIE_SET, AM2_MOVIE_INDEX_A + i, 1, 1, 1);
    }
    for (i = 0; i < 8; i++) {
        spr[at++] = PreloadSprite(AM2_MOVIE_SET, AM2_MOVIE_INDEX_B + i, 0, 1, 1);
        spr[at++] = PreloadSprite(AM2_MOVIE_SET, AM2_MOVIE_INDEX_B + i, 1, 1, 1);
    }

    slots[0] = MakeMovieButton(w, panel, 0x21, 0x35, 0, kOnMoviePlay);
    panel->focusedChild = slots[0];
    slots[1] = (AM2_Widget *)0;
    slots[2] = (AM2_Widget *)0;
    slots[3] = (AM2_Widget *)0;

    if (g_movieCount > 0)
        slots[1] = MakeMovieButton(w, panel, 0xD5, 0x35, 1,
                                   kOnMoviePlay);
    if (g_movieCount > 1)
        slots[2] = MakeMovieButton(w, panel, 0x21, 0xE9, 2,
                                   kOnMoviePlay);
    if (g_movieCount > 2)
        slots[3] = MakeMovieButton(w, panel, 0xD5, 0xE9, 3,
                                   kOnMoviePlay);

    WidgetAddChild(panel, MakeButton(0x197, 0xE8, AM2_BMP_BACK19_0,
                                     AM2_BMP_BACK19_1, AM2_BMP_BACK19_2,
                                     kOnMenuBack));
    if (g_movieCount > 2)
        WidgetAddChild(panel, MakeButton(0x197, 0xB6, AM2_BMP_EGG0,
                                         AM2_BMP_EGG1, AM2_BMP_EGG2,
                                         kOnMovieNextPage));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) = (uint32_t)(uintptr_t)kOnMenuBack;
    return w;
}

/* 0x004520E0, thiscall. LOAD GAME -- the campaign's save picker, and the
 * second of the two screens built two ways: no panel in a mission, with the
 * panel's offset (0x7D by 0x62) folded into every rectangle, and a panel
 * with a zero offset on the title screen. DELETE GAME is the other.
 *
 * Its list comes off the FILESYSTEM, like SELECT PLAYER's -- `save\<player>`
 * globbed for `*.sav` -- and it seeds its own copy of the chosen name from
 * the first row, so LOAD works without the row ever being clicked.
 *
 * Two things differ between the layouts beyond the offset. The screen's
 * focused child is the PANEL when there is one and the LIST when there is
 * not, and the second is written after the list exists rather than before.
 * Both are the original's, and reading only one of the two arms gets the
 * focus wrong on the other. */
AM2_Widget *__attribute__((thiscall)) LoadGameConstruct(AM2_Widget *w,
                                                        const char *bmp,
                                                        int32_t flag)
{
    AM2_Widget *parent = w;
    AM2_Widget *list;
    AM2_Widget *bar;
    void       *rows;
    AM2_Rect    box;
    char        path[264];
    char        pattern[264];
    uint8_t     found[0x11C];
    int32_t     handle;
    int32_t     dx = 0x7D;
    int32_t     dy = 0x62;

    ScreenBaseConstruct(w, bmp, flag);
    w->vtable = (void *)AM2_IMAGE(VTABLE_LOAD_GAME);
    *((char *)w + LOAD_GAME_OFF_NAME) = '\0';

    rows = orig_operator_new(AM2_ROWS_SIZE);
    if (rows)
        rows = RecordCtor(rows, 1);
    *(void **)((uint8_t *)w + COMMPANEL_OFF_LIST) = rows;

    orig_sprintf(path, (const char *)AM2_IMAGE(ADDR_STR_SAVE_PLAYER_FMT),
                 g_currentPlayer);
    SetGameDir(path);

    strcpy(pattern, (const char *)AM2_IMAGE(ADDR_STR_GLOB_SAV));
    handle = orig_findfirst(pattern, found);
    if (handle != -1) {
        do {
            ListAdd(*(void **)((uint8_t *)w + COMMPANEL_OFF_LIST),
                    (const char *)(found + AM2_FIND_OFF_NAME), (void *)0);
        } while (orig_findnext(handle, found) == 0);
        orig_findclose(handle);
    }

    if (g_gameState == 2) {
        w->flag44 = 1;
    } else {
        AM2_Widget *panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);

        if (panel) {
            RectSet(&box, 0x7D, 0x62, 0x186, 0x11C);
            panel = PanelConstruct(panel, (const char *)
                                    AM2_IMAGE(ADDR_STR_LOADGAME_BMP), 0, box);
        }
        WidgetAddChild(w, panel);
        w->focusedChild = panel;
        panel->flag44 = 1;
        dx = 0;
        dy = 0;
        parent = panel;
    }

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, dx + 0x2A, dy + 0x44, 0x95, 0xAA);
        list = ListBoxConstruct(list, box.left, box.top, box.right,
                                box.bottom,
                                *(void **)((uint8_t *)w
                                           + COMMPANEL_OFF_LIST),
                                (int32_t)(uintptr_t)LoadGameRow,
                                0, 1);
    }
    WidgetAddChild(parent, list);
    if (g_gameState == 2)
        w->focusedChild = list;
    ((AM2_WidgetFocusFn *)list->vtable)[WIDGET_VSLOT_FOCUS](list, 0);

    /* Opening the screen selects the first save, name and all. */
    {
        const int32_t *r = *(const int32_t **)((uint8_t *)w
                                               + COMMPANEL_OFF_LIST);

        if (r[0] > 0)
            strcpy((char *)w + LOAD_GAME_OFF_NAME,
                   *(const char *const *)(r + 1));
    }
    list->flag44 = 1;

    bar = (AM2_Widget *)orig_operator_new(AM2_ARROWBAR_SIZE);
    if (bar) {
        RectSet(&box, dx + 0xD5, dy + 0x3C, 0x13, 0xBA);
        bar = ArrowBarConstruct(bar, box.left, box.top, box.right,
                                box.bottom, parent,
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                                (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                                0x90, 0);
    }
    WidgetAddChild(parent, bar);
    *(AM2_Widget **)((uint8_t *)list + LIST_OFF_ARROWBAR) = bar;
    *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST) = list;

    WidgetAddChild(parent, MakeButton(dx + 0x123, dy + 0x44, AM2_BMP_NEW0,
                                      AM2_BMP_NEW1, AM2_BMP_NEW2,
                                      kOnLoadGameNew));
    WidgetAddChild(parent, MakeButton(dx + 0x123, dy + 0x6B, AM2_BMP_LOAD0,
                                      AM2_BMP_LOAD1, AM2_BMP_LOAD2,
                                      kOnLoadGameLoad));
    WidgetAddChild(parent, MakeButton(dx + 0x123, dy + 0x92,
                                      AM2_BMP_DELETE12_0, AM2_BMP_DELETE12_1,
                                      AM2_BMP_DELETE12_2,
                                      kOnLoadGameDelete));
    WidgetAddChild(parent, MakeButton(dx + 0x123, dy + 0xB9, AM2_BMP_BACK19_0,
                                      AM2_BMP_BACK19_1, AM2_BMP_BACK19_2,
                                      kOnLoadGameBack));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)(uintptr_t)kOnLoadGameBack;
    return w;
}

/* ------------------------------------------------------------------ *
 * The save-game family's buttons: ENTER NAME's CANCEL, and LOAD GAME's and
 * DELETE GAME's exits.
 *
 * Every one of them that can be opened from a mission has the two-armed
 * ending the OPTIONS dialogs have -- an overlay MODE in state 2 and a menu
 * REQUEST otherwise -- and the modes differ per screen. ENTER NAME's CANCEL
 * is the exception and has only the request, because RECRUIT is not reachable
 * from play.
 * ------------------------------------------------------------------ */

#define g_pendingDelete   ((char *)(uintptr_t)ADDR_PENDING_DELETE)

/* 0x00451AC0. ENTER NAME's CANCEL, and the screen's escape action. */
void __cdecl OnEnterNameCancel(AM2_Widget *w)
{
    (void)w;
    RequestScreen(AM2_MENU_REQUEST_SELECT_PLAYER);
}

/* 0x00452010. LOAD GAME's BACK, and its escape action. */
void __cdecl OnLoadGameBack(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    if (g_gameState == 2) {
        g_subState     = MENU_MODE_OPTIONS;
        g_overlayDirty = 1;
    } else {
        g_menuRequest    = AM2_MENU_REQUEST_SELECT_PLAYER;
        g_menuRequestSet = 1;
    }
}

/* 0x00451F10. LOAD GAME's DELETE: refuse with wave 3 if nothing is chosen,
 * otherwise copy the name where DELETE GAME will read it and open that
 * screen. The name is the SCREEN's copy at 0x68, which the constructor seeded
 * from the first row -- so DELETE works on a screen nobody has clicked. */
void __cdecl OnLoadGameDelete(AM2_Widget *w)
{
    const char *name = (const char *)g_paintObject + LOAD_GAME_OFF_NAME;

    (void)w;
    if (!g_paintObject)
        return;
    if (!strlen(name)) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }
    strcpy(g_pendingDelete, name);
    PlaySoundAt(2, 0, 0, 0, 0);
    if (g_gameState == 2) {
        g_subState     = AM2_MENU_MODE_DEL_GAME;
        g_overlayDirty = 1;
    } else {
        g_menuRequest    = AM2_MENU_REQUEST_DEL_GAME;
        g_menuRequestSet = 1;
    }
}

/* 0x00450180. DELETE GAME's CANCEL, and its escape action. It CLEARS the
 * pending name on the way out, so a cancelled delete cannot be completed by
 * the next screen -- and its mode is the only one in the family computed
 * rather than written: 0x1A when it was asked from 0x1D and 0x19 otherwise,
 * spelled `sete` and `add 0x19`. */
void __cdecl OnDelGameCancel(AM2_Widget *w)
{
    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    g_pendingDelete[0] = '\0';
    if (g_gameState == 2) {
        int32_t from = g_subState;

        g_overlayDirty = 1;
        g_subState = (from == AM2_MENU_MODE_DEL_GAME)
                         ? AM2_MENU_MODE_DEL_PLAYER
                         : AM2_MENU_MODE_AFTER_LOAD;
    } else {
        g_menuRequest    = AM2_MENU_REQUEST_LOAD_GAME;
        g_menuRequestSet = 1;
    }
}

/* 0x00451FB0. LOAD GAME's NEW: start the campaign from level 1 without
 * loading anything. It chdirs to `data` first, and unlike SELECT PLAYER --
 * which does the same lookup two screens earlier -- it stores the RECORD's
 * own id rather than the literal 1. */
void __cdecl OnLoadGameNew(AM2_Widget *w)
{
    void *level;

    (void)w;
    PlaySoundAt(2, 0, 0, 0, 0);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_DATA_DIR));
    level = FindLevelRecord(1);
    if (!level)
        return;

    PlaySoundAt(2, 0, 0, 0, 0);
    SelectLevel(level);
    g_levelId    = *(const int32_t *)level;
    g_levelIndex = 1;
    RequestState(2);
}

/* 0x00455C10, thiscall, `ret 0x10`. Repaint a widget through the nearest
 * ANCESTOR that owns a sprite -- and repaint it CLIPPED TO THIS WIDGET'S
 * rectangle, which is the whole point and the thing that makes it not
 * WidgetRepaint: only the area this widget covers is redrawn, by whoever owns
 * the background under it.
 *
 * With no such ancestor it paints itself instead. The clip rectangle it is
 * handed is ignored either way -- the signature exists so it can sit in a
 * paint slot, not because the argument is read. */
void __attribute__((thiscall)) RepaintAncestor(AM2_Widget *w, RECT clip)
{
    AM2_Widget *up = w->parent;

    (void)clip;
    while (up && !up->sprite)
        up = up->parent;

    if (up)
        ((AM2_WidgetPaintFn *)up->vtable)[WIDGET_VSLOT_PAINT](up, w->rect);
    else
        ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* 0x0044DEA0. SELECT MAP's row callback: the row's VALUE is a pointer to the
 * level id the constructor malloc'd, so this dereferences twice to get it,
 * looks the record up and starts that mission.
 *
 * It re-reads the row count after choosing the level and only then asks for
 * state 2 -- a second check of what it has already tested, which changes
 * nothing here and is reproduced rather than tidied. */
void __cdecl SelectMapRow(AM2_Widget *list, AM2_ListRows *rows,
                          int32_t selected)
{
    const int32_t *id;
    void          *level;

    (void)list;
    if (!rows || selected < 0 || selected >= rows->count)
        return;

    id = *(const int32_t *const *)(rows->text
                                   + (uint32_t)selected * AM2_LIST_ROW_STRIDE
                                   + AM2_LIST_ROW_VALUE);
    level = FindLevelRecord(*id);
    if (!level)
        return;

    PlaySoundAt(2, 0, 0, 0, 0);
    SelectLevel(level);
    g_levelId = *(const int32_t *)level;
    if (selected >= rows->count)
        return;
    g_levelIndex = 1;
    RequestState(2);
}

/* 0x00451EA0. LOAD GAME's row callback: copy the chosen save's name into the
 * SCREEN's own slot, which is what its DELETE and LOAD then read. The screen
 * comes from the paint object rather than from the list's ancestry. */
void __cdecl LoadGameRow(AM2_Widget *list, AM2_ListRows *rows,
                         int32_t selected)
{
    (void)list;
    if (!rows || selected < 0 || selected >= rows->count)
        return;
    if (!g_paintObject)
        return;
    strcpy((char *)g_paintObject + LOAD_GAME_OFF_NAME,
           rows->text + (uint32_t)selected * AM2_LIST_ROW_STRIDE);
    PlaySoundAt(2, 0, 0, 0, 0);
}

/* 0x0044E580. MOVIES' page button: bump the page, wrap past 2, and RETARGET
 * the four buttons that already exist rather than rebuilding them. Each takes
 * a new slot index and the two sprites for it, in the same NORMAL=b,
 * FOCUS=a, PRESSED=a pattern the constructor uses.
 *
 * It does not repaint. The buttons are marked by nothing and simply come out
 * differently the next time the screen is drawn, which is what makes this
 * cheap enough to do on a click. */
void __cdecl OnMovieNextPage(AM2_Widget *w)
{
    AM2_Widget  *panel;
    AM2_Widget  *screen;
    AM2_Widget **slots;
    int32_t      page;
    int32_t      i;

    if (!w)
        return;
    panel = w->parent;
    if (!panel)
        return;
    screen = panel->parent;
    if (!screen)
        return;

    PlaySoundAt(2, 0, 0, 0, 0);
    page = g_moviePage + 1;
    g_moviePage = page;
    if (page > 2) {
        page = 0;
        g_moviePage = 0;
    }

    slots = (AM2_Widget **)((uint8_t *)screen + MOVIES_OFF_BUTTONS);
    for (i = 0; i < AM2_MOVIE_PAGE_SIZE; i++) {
        AM2_Widget *btn = slots[i];
        int32_t     idx = i + page * AM2_MOVIE_PAGE_SIZE;
        AM2_Sprite *const *pair;
        uint8_t    *self;

        if (!btn)
            continue;
        pair = (AM2_Sprite *const *)((const uint8_t *)screen
                                     + MOVIES_OFF_SPRITES
                                     + (uint32_t)idx * 8);
        self = (uint8_t *)btn;
        *(int32_t *)(self + MOVIE_BUTTON_OFF_INDEX)        = idx;
        *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_NORMAL)  = pair[1];
        *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_FOCUS)   = pair[0];
        *(AM2_Sprite **)(self + BUTTON_OFF_SPRITE_PRESSED) = pair[0];
    }
}

/* 0x0044E610. A thumbnail's click: name the film from the button's own slot
 * and go to state 3, which is the movie player. It sets the overlay mode
 * afterwards, so state 3 arrives with the film already chosen. */
void __cdecl OnMoviePlay(AM2_Widget *w)
{
    const char *const *names = (const char *const *)
        AM2_IMAGE(ADDR_MOVIE_NAMES);
    int32_t idx;

    PlaySoundAt(2, 0, 0, 0, 0);
    idx = *(const int32_t *)((const uint8_t *)w + MOVIE_BUTTON_OFF_INDEX);
    strcpy((char *)(uintptr_t)ADDR_MOVIE_TO_PLAY, names[idx]);
    RequestState(3);
    *(int32_t *)(uintptr_t)ADDR_GAME_STATE_ARG = 1;
    g_subState = AM2_MENU_MODE_MOVIE;
}

/* 0x00452060. LOAD GAME's LOAD -- and this is the arm STATUS's open item 2
 * names as the one that fires: it copies the chosen save into
 * ADDR_GAMEPROC_STR_B, raises ADDR_LOAD_PENDING and asks for state 2, which
 * is the whole of the load request. What happens after that is the puzzle,
 * not this.
 *
 * The name is the SCREEN's copy, seeded by the constructor, so LOAD works
 * without a row ever being clicked; an empty one is refused with wave 3. */
void __cdecl OnLoadGameLoad(AM2_Widget *w)
{
    const char *name;

    (void)w;
    if (!g_paintObject)
        return;
    name = (const char *)g_paintObject + LOAD_GAME_OFF_NAME;
    if (!strlen(name)) {
        PlaySoundAt(3, 0, 0, 0, 0);
        return;
    }
    strcpy((char *)(uintptr_t)ADDR_GAMEPROC_STR_B, name);
    g_loadPending = 1;
    PlaySoundAt(2, 0, 0, 0, 0);
    RequestState(2);
}

void __cdecl OnMpColour(AM2_Widget *w);
void __cdecl OnMpTeamLeft(AM2_Widget *w);
void __cdecl OnMpTeamRight(AM2_Widget *w);
void __cdecl OnMpName(AM2_Widget *w);

/* ------------------------------------------------------------------ *
 * The three BUTTON classes the multiplayer host/join panel builds one of per
 * player row. All three derive from the base button and all three carry the
 * row they belong to in the base's 0x0058, which is how their handlers know
 * which player they are for.
 *
 * The panel itself is still the original's -- 4,497 bytes -- so these run in
 * the middle of a live path and `ab.sh multi` and `ab.sh mpoptions` compare
 * the result. Working inward from the leaves rather than starting at the
 * root is what makes that possible.
 * ------------------------------------------------------------------ */

/* 0x004329A0, thiscall `ret 0x24` -- NINE stack arguments, and the four in
 * the middle are the rectangle spelled out rather than passed by value, which
 * is what makes this one different from every other widget constructor here.
 *
 * It writes the rectangle into the base DIRECTLY and then calls
 * WidgetScreenRect, where the others go through RectSet first. Same result,
 * one fewer call. */
AM2_Widget *__attribute__((thiscall)) MpNameConstruct(AM2_Widget *w,
                                                      const char *text,
                                                      int32_t left, int32_t top,
                                                      int32_t width,
                                                      int32_t height,
                                                      int32_t flag,
                                                      uint8_t ink,
                                                      uint8_t paper,
                                                      int32_t row)
{
    uint8_t *self = (uint8_t *)w;

    ButtonBaseConstruct(w);
    *(const char **)(self + MPNAME_OFF_TEXT)  = text;
    *(int32_t *)(self + MPNAME_OFF_FLAG)      = flag;
    self[MPNAME_OFF_INK]                      = ink;
    self[MPNAME_OFF_PAPER]                    = paper;
    w->x = left;
    w->y = top;
    w->w = width;
    w->h = height;
    w->vtable = (void *)AM2_IMAGE(VTABLE_MP_NAME);
    WidgetScreenRect(w);
    *(uint32_t *)(self + BUTTON_OFF_ON_LEFT) = (uint32_t)(uintptr_t)OnMpName;
    *(int32_t *)(self + MPBTN_OFF_ROW) = row;
    return w;
}

/* 0x00432E20 and 0x00433030, thiscall `ret 0x0C`. Both are 18 by 20 at a
 * column the caller picks, and the only differences are the vtable, the
 * handlers, and whether the button repeats.
 *
 * The TEAM one has a RIGHT handler and auto-repeat where the COLOUR one has
 * neither, which is the shape difference; the names come from what the
 * handlers do, not from that. */
static AM2_Widget *MpSmallConstruct(AM2_Widget *w, int32_t left, int32_t top,
                                    int32_t row, uint32_t vtable,
                                    uint32_t onLeft, uint32_t onRight,
                                    int32_t repeats)
{
    uint8_t *self = (uint8_t *)w;
    AM2_Rect box;

    ButtonBaseConstruct(w);
    w->vtable = (void *)AM2_IMAGE(vtable);
    RectSet(&box, left, top, AM2_MP_SMALL_W, AM2_MP_SMALL_H);
    w->x = box.left;
    w->y = box.top;
    w->w = box.right;
    w->h = box.bottom;
    WidgetScreenRect(w);
    /* The handlers arrive as ready POINTERS, already through AM2_IMAGE where
     * they are still the original's: two of the three are ours now, and
     * applying the slide here would have sent those through the detour. */
    *(int32_t *)(self + MPBTN_OFF_ROW)        = row;
    *(uint32_t *)(self + BUTTON_OFF_ON_LEFT)  = onLeft;
    if (onRight) {
        *(uint32_t *)(self + BUTTON_OFF_ON_RIGHT) = onRight;
        *(int32_t *)(self + BUTTON_OFF_REPEATS) = repeats;
    }
    return w;
}

AM2_Widget *__attribute__((thiscall)) MpColourConstruct(AM2_Widget *w,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t row)
{
    return MpSmallConstruct(w, left, top, row, VTABLE_MP_COLOUR,
                            (uint32_t)(uintptr_t)OnMpColour, 0, 0);
}

AM2_Widget *__attribute__((thiscall)) MpTeamConstruct(AM2_Widget *w,
                                                         int32_t left,
                                                         int32_t top,
                                                         int32_t row)
{
    return MpSmallConstruct(w, left, top, row, VTABLE_MP_TEAM,
                            (uint32_t)(uintptr_t)OnMpTeamLeft,
                            (uint32_t)(uintptr_t)OnMpTeamRight, 1);
}

/* 0x00432EC0 and 0x004330E0: the COLOUR and TEAM buttons' left handlers, and
 * what NAMED those two classes. Both open with the same guard -- a row that is
 * not ours may only be touched by the host, and only within the player count
 * -- and both cycle a value and tell the network. What they do after that is
 * where they part.
 *
 * The colour one refuses row 3 outright, seeds the selection from the row
 * itself when it has never been set, and wraps not to zero but to `row + 1`.
 * Then, as HOST, it writes the army through CommSetArmyColour and repaints
 * ALL FOUR colour buttons; as a guest it sends SendColorMsg and repaints
 * nothing, because the answer has to come back from the host.
 *
 * The team one wraps at 12 to zero, stores only as host, and repaints just
 * ITSELF. A guest sends SendTeamMsg and, again, does not store -- so a guest
 * clicking either button changes nothing locally until the host says so. */

#define g_ourSlot     (*(const int32_t *)(uintptr_t)ADDR_OUR_SLOT)

typedef int32_t (__attribute__((thiscall)) *AM2_SetArmyColourFn)(void *comm,
                                                                 int32_t slot,
                                                                 int32_t army);
typedef void (__cdecl *AM2_SendIntFn)(int32_t v);
/* SendColorMsg and SendTeamMsg are ours -- msgslot.cpp -- so they are called
 * by name; CommSendPlayers is not. */

/* The guard both share: our own row is always ours to change. */
static int32_t MpRowEditable(void *comm, int32_t row)
{
    const uint8_t *c = (const uint8_t *)comm;

    if (g_ourSlot == row)
        return 1;
    if (!*(const int32_t *)(c + COMM_OFF_IS_HOST))
        return 0;
    /* The host may edit the rows PAST the human player count -- the computer
     * slots -- and not the rows belonging to other people.  `jl` returns. */
    return row >= *(const int32_t *)(c + COMM_OFF_PLAYER_COUNT);
}

void __cdecl OnMpColour(AM2_Widget *w)
{
    uint8_t *screen = g_paintObject;
    void    *comm   = g_commObject;
    int32_t  row    = *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW);
    int32_t *sel;
    int32_t  next;
    int32_t  army;
    int32_t  i;

    if (!MpRowEditable(comm, row))
        return;
    /* Row 3 is not colourable and the test is on the row read BEFORE the
     * guard, which is the same value either way. */
    if (row == 3)
        return;

    sel = (int32_t *)(screen + MP_PANEL_OFF_COLOUR_SEL);
    if (sel[row] == -1)
        sel[row] = row;

    row  = *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW);
    next = sel[row] + 1;
    if (next >= AM2_PLAYERS_MAX)
        next = row + 1;
    sel[row] = next;

    army = CommArmyOfSlot(comm, next);
    if (!*(const int32_t *)((const uint8_t *)comm + COMM_OFF_IS_HOST)) {
        SendColorMsg(army);
        return;
    }

    CommSetArmyColour(comm,
                         *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW),
                         army);
    PlaySoundAt(2, 0, 0, 0, 0);
    SendPlayerMsg(0);

    for (i = 0; i < AM2_PLAYERS_MAX; i++) {
        AM2_Widget *b = ((AM2_Widget **)(screen + MP_PANEL_OFF_COLOURS))[i];

        ((AM2_WidgetPaintFn *)b->vtable)[WIDGET_VSLOT_PAINT](b, b->rect);
    }
}

void __cdecl OnMpTeamLeft(AM2_Widget *w)
{
    void    *comm = g_commObject;
    int32_t  row  = *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW);
    int32_t *team;
    int32_t  next;

    if (!MpRowEditable(comm, row))
        return;

    team = (int32_t *)((uint8_t *)comm + (uint32_t)row * AM2_PLAYER_STRIDE
                       + COMM_ARMY_OFF_TEAM);
    next = *team + 1;
    if (next > AM2_MP_TEAM_MAX)
        next = 0;

    if (!*(const int32_t *)((const uint8_t *)comm + COMM_OFF_IS_HOST)) {
        SendTeamMsg(next);
        return;
    }

    *team = next;
    PlaySoundAt(2, 0, 0, 0, 0);
    SendPlayerMsg(0);
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* 0x00433190 -- the TEAM button's right half.  Identical to the left except
 * for the step: `dec` then `jns`, so it wraps at -1 to 12 rather than at 13
 * to 0.  Everything after the arithmetic is shared. */
void __cdecl OnMpTeamRight(AM2_Widget *w)
{
    void    *comm = g_commObject;
    int32_t  row  = *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW);
    int32_t *team;
    int32_t  next;

    if (!MpRowEditable(comm, row))
        return;

    team = (int32_t *)((uint8_t *)comm + (uint32_t)row * AM2_PLAYER_STRIDE
                       + COMM_ARMY_OFF_TEAM);
    next = *team - 1;
    if (next < 0)
        next = AM2_MP_TEAM_MAX;

    if (!*(const int32_t *)((const uint8_t *)comm + COMM_OFF_IS_HOST)) {
        SendTeamMsg(next);
        return;
    }

    *team = next;
    PlaySoundAt(2, 0, 0, 0, 0);
    SendPlayerMsg(0);
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* 0x00432D50 -- the NAME button.  It is not a spinner and it has no guest
 * path at all: the host check is unconditional, so a guest clicking a name
 * does nothing whatever.  What it toggles is whether the slot holds a
 * computer player, and when it turns one ON it names it -- which is where
 * the "Computer1", "Computer2" in a multiplayer roster come from.
 *
 * The row test is the one MpRowEditable makes, without the "or it is mine"
 * arm: a name may only be toggled on a row past the human player count. */
void __cdecl OnMpName(AM2_Widget *w)
{
    uint8_t *comm = (uint8_t *)g_commObject;
    int32_t  row;
    int32_t *active;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    row = *(const int32_t *)((uint8_t *)w + MPBTN_OFF_ROW);
    if (row < *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT))
        return;

    active = (int32_t *)(comm + (uint32_t)row * AM2_PLAYER_STRIDE
                         + AM2_PLAYER_ACTIVE);
    if (*active) {
        *active = 0;
    } else {
        *active = 1;
        /* Numbered from the first computer slot, not from the row. */
        orig_sprintf((char *)(comm + (uint32_t)row * AM2_PLAYER_STRIDE
                              + COMM_OFF_PLAYERS + COMM_SLOT_OFF_NAME),
                     (const char *)AM2_IMAGE(ADDR_FMT_COMPUTER_N),
                     row - *(const int32_t *)(comm + COMM_OFF_PLAYER_COUNT) + 1);
    }

    PlaySoundAt(2, 0, 0, 0, 0);
    SendPlayerMsg(0);
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* The game's own stdio, for the same reason crt.h gives for its allocator: a
 * FILE opened by the game's CRT cannot be read or closed by ours. */
typedef char *(__cdecl *AM2_FgetsFn)(char *buf, int32_t n, am2_FILE *fp);
#define orig_fgets_line (*(AM2_FgetsFn)AM2_IMAGE(ADDR_CRT_FGETS))

/* 0x00430480 -- the multiplayer panel's destructor, slot 0.
 *
 * Two sprite arrays and then the base. Neither array is a field of the panel:
 * both are GLOBALS, and their bounds are this function's loop limits rather
 * than anything declared -- five sprites from one and thirteen from the
 * other. The first array's limit is the address of the message log, which is
 * the next global along.
 *
 * The MSVC SEH prologue on it is not reproduced; see CLAUDE.md for why that
 * is safe in a program with no throw. */
void __attribute__((thiscall)) MpPanelDestruct(AM2_Widget *w)
{
    AM2_Sprite **p;

    w->vtable = (void *)AM2_IMAGE(VTABLE_MP_PANEL);

    for (p = (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_A;
         p < (AM2_Sprite **)(uintptr_t)ADDR_MENU_MSG_LIST; p++)
        ReleaseSprite(*p);

    for (p = (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B;
         p < (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B_END; p++)
        ReleaseSprite(*p);

    DialogDestruct(w);
}

/* 0x004316D0 -- the multiplayer panel's per-frame update, slot 2.
 *
 * The cancel key first, through the base, and then two sweeps.
 *
 * The first greys the COLOUR and TEAM buttons row by row, and the policy is
 * exactly the one their handlers guard on: a row holding a real player may be
 * edited only if it is OURS, and an empty row only by the HOST. Both buttons
 * of a row always agree. `disabled` is what `ctl widgets` prints as nofoc, so
 * this sweep is compared exactly rather than by pixels.
 *
 * The second pushes five numbers into text: the score limit into the panel's
 * own buffer, and each army's setting into the row's inner edit box. It does
 * that EVERY FRAME, which is why those four fields cannot be typed into --
 * anything a keystroke put there would be gone before it was drawn.
 *
 * The row loop is written with a byte cursor over the player array in the
 * original and a widget cursor beside it; both are indices here. */
void __attribute__((thiscall)) MpPanelUpdate(AM2_Widget *w)
{
    uint8_t     *self = (uint8_t *)w;
    AM2_Widget **colours = (AM2_Widget **)(self + MP_PANEL_OFF_COLOURS);
    AM2_Widget **teams   = (AM2_Widget **)(self + MP_PANEL_OFF_TEAMS);
    AM2_Widget **rows    = (AM2_Widget **)(self + MP_PANEL_OFF_ARMY_ROWS);
    const int32_t *setting = (const int32_t *)(uintptr_t)ADDR_ARMY_POINTS;
    int32_t      i;

    WidgetUpdateCancel(w);

    for (i = 0; i < AM2_PLAYERS_MAX; i++) {
        const uint8_t *comm = g_commObject;
        const uint8_t *rec  = comm + (uint32_t)i * AM2_PLAYER_STRIDE;
        int32_t        off;

        if (*(const uint32_t *)(rec + AM2_PLAYER_ID) > 0)
            off = (i == g_ourSlot) ? 0 : 1;
        else
            off = *(const int32_t *)(comm + COMM_OFF_IS_HOST) ? 0 : 1;

        colours[i]->disabled = off;
        teams[i]->disabled   = off;
    }

    orig_sprintf((char *)(self + MP_PANEL_OFF_SCORE_TEXT),
                 (const char *)AM2_IMAGE(ADDR_FMT_INT),
                 *(const int32_t *)(uintptr_t)ADDR_SCORE_LIMIT);

    for (i = 0; i < AM2_PLAYERS_MAX; i++) {
        uint8_t *inner = *(uint8_t **)((uint8_t *)rows[i] + AM2_MP_ROW_INNER);

        orig_sprintf(*(char **)(inner + EDIT_OFF_TEXT),
                     (const char *)AM2_IMAGE(ADDR_FMT_INT), setting[i]);
    }
}

/* 0x00430140 -- fill a list box from a text file in `rules/`, one row per
 * line, three callers each with a different filename.
 *
 * The row value is 3 for every line, which is the same slot ListAdd stores a
 * pointer in for the difficulty rows; here it is a plain constant and nothing
 * reads it back through this path.
 *
 * The EOF test is the original's, inline: it reads the MSVC FILE's `_flag`
 * for _IOEOF rather than calling feof, and it does it BEFORE the first fgets
 * as well as after each one. A file that opens already at EOF therefore
 * clears the list and adds nothing, which is a different answer from
 * "leave the list alone", and is why the reset comes first.
 *
 * The line keeps its newline: fgets is given the whole 0x100 buffer and
 * nothing trims. */
void __cdecl FillListFromRules(const char *path, void *panel)
{
    char      line[0x100];
    am2_FILE *fp;
    void     *rows;

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_RULES_DIR));

    fp = orig_fopen(path, (const char *)AM2_IMAGE(ADDR_STR_DEF_FILE_MODE));
    if (!fp)
        return;

    rows = *(void **)(*(uint8_t **)((uint8_t *)panel + MP_PANEL_OFF_TYPE_BOX)
                      + LISTBOX_OFF_ROWS);
    RecordReset(rows);

    while (!(*(const uint8_t *)((const uint8_t *)fp + AM2_FILE_OFF_FLAG)
             & AM2_FILE_EOF)) {
        if (!orig_fgets_line(line, (int32_t)sizeof(line), fp))
            break;
        ListAdd(rows, line, (void *)3);
    }

    orig_fclose(fp);
}

/* 0x00430330 -- the thumbnail when the map is not installed. The literal is
 * copied into a 0x100 local before being handed on, which is the original's
 * inlined strcpy and not something the callee needs; kept, because the callee
 * is still the original's and a pointer into the image is not what it is
 * given anywhere else. */
void __cdecl ShowBadMapPreview(AM2_Widget *preview)
{
    char name[0x100];

    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_BITMAPS_DIR));
    strcpy(name, (const char *)AM2_IMAGE(ADDR_STR_BAD_MP_PREV));

    MpPreviewSetBitmap(preview, name);
    ((AM2_WidgetPaintFn *)preview->vtable)[WIDGET_VSLOT_PAINT](
        preview, preview->rect);
}

/* 0x004301D0 -- what runs when the chosen map changes, on the host panel and
 * from three other places besides.
 *
 * The preview widget is only fetched when the repaint object exists AND the
 * game is on the menu AND the menu mode is the HOST panel: everywhere else it
 * stays null and the whole thumbnail half is skipped. That is not a guard
 * against a missing widget, it is how the same function serves the panel and
 * the three callers that have no panel at all.
 *
 * The two halves are then the same shape with opposite answers. If <map>.amm
 * is there: point the thumbnail at <map>_prev.bmp, repaint it, take the three
 * checksums, and mark OUR army as having the map. If it is not: clear the
 * thumbnail, play sound 3, and mark our army as not having it -- and the map
 * checksum, alone of the three, is zeroed rather than left.
 *
 * ADDR_LEVEL_INDEX is set to zero on both paths, which is "<map>.txt".
 *
 * The existence test chdirs into the map folder TWICE on the success path,
 * once before it and once after. The second is not redundant: FileExists
 * takes a bare name, so the first is what makes it look in the right place,
 * and everything after it opens files by bare name too. */
void __cdecl RefreshMapSelection(void)
{
    AM2_Widget *preview = (AM2_Widget *)0;
    char        path[0x100];

    if (g_paintObject != (uint8_t *)0
        && g_gameState == AM2_STATE_MENU
        && g_subState == AM2_MENU_MP_HOST)
        preview = ((AM2_Widget **)(g_paintObject + MP_PANEL_OFF_PREVIEW))[0];

    SetGameDir((const char *)AM2_IMAGE(ADDR_MAP_FOLDER));
    orig_sprintf(path, (const char *)AM2_IMAGE(ADDR_FMT_DOT_AMM),
                 (const char *)AM2_IMAGE(ADDR_MAP_NAME));

    if (!FileExists(path)) {
        if (preview)
            ShowBadMapPreview(preview);
        PlaySoundAt(3, 0, 0, 0, 0);
        g_mapChecksum = 0;
        *(int32_t *)((uint8_t *)g_commObject
                     + (uint32_t)g_defaultOwner * AM2_PLAYER_STRIDE
                     + COMM_ARMY_OFF_MAP_OK) = 0;
        g_levelIndex = 0;
        return;
    }

    SetGameDir((const char *)AM2_IMAGE(ADDR_MAP_FOLDER));

    if (preview) {
        orig_sprintf(path, (const char *)AM2_IMAGE(ADDR_FMT_PREV_BMP),
                     (const char *)AM2_IMAGE(ADDR_MAP_NAME));
        MpPreviewSetBitmap(preview, path);
        ((AM2_WidgetPaintFn *)preview->vtable)[WIDGET_VSLOT_PAINT](
            preview, preview->rect);
    }

    g_rulesChecksum    = RulesChecksum();
    g_mpScriptChecksum = MpScriptChecksum();
    g_mapChecksum      = MapChecksum();

    *(int32_t *)((uint8_t *)g_commObject
                 + (uint32_t)g_defaultOwner * AM2_PLAYER_STRIDE
                 + COMM_ARMY_OFF_MAP_OK) = 1;
    g_levelIndex = 0;
}

/* 0x00431CE0 -- the panel's chat line, and what happens when it is sent.
 *
 * Log it locally in OUR army's colour, broadcast it, empty the field, repaint.
 * The `system` argument to SendChatMsg is 0 here, so the sender byte is the
 * army rather than the announcement colour 4 -- this is a player talking, not
 * the game.
 *
 * The colour reaches MenuMessage as a dword whose upper three bytes are
 * whatever the caller's `ecx` held: the original stores AL into a stack local
 * and then reads the whole dword back. It cannot matter, because MenuMessage
 * masks to a byte before it uses it, and passing the byte is the same
 * function.
 *
 * **The field is not cleared, it is overwritten from ADDR_DIR_SCRATCH** -- a
 * shared char[] with eighty references across the image. It is empty here and
 * the effect is a clear, but that is a property of what ran before rather
 * than of this code, and it is reproduced as written rather than turned into
 * a `text[0] = 0` that would be a different function. */
void __cdecl OnChatEnter(AM2_Widget *w)
{
    uint8_t *self = (uint8_t *)w;
    char    *text = *(char **)(self + EDIT_OFF_TEXT);
    int32_t  army = CommArmyOfSlot((void *)g_commObject,
                                           (int32_t)g_defaultOwner);

    MenuMessage(text, (int32_t)(uint8_t)army, 0);
    SendChatMsg(text, 0);
    strcpy(text, (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));
    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* 0x00432C50 -- the INK a player row's name is drawn in, chosen by how the
 * connection is behaving. Our own row is always the default; everybody
 * else's degrades in three steps, and the tests are cumulative rather than
 * exclusive, so 1200 ms of latency picks the second colour and then the
 * third.
 *
 * The last step is not latency at all but SILENCE: the player's record
 * stamps GetTickCount at +0x70 on every packet, and 1250 ms without one
 * overrides whatever the average said.
 *
 * It returns a byte in AL and leaves the rest of EAX holding its own
 * working value, which is why the prototype is uint8_t. */
uint8_t __cdecl MpNameInk(int32_t row)
{
    const uint8_t *comm = g_commObject;
    uint32_t       id   = *(const uint32_t *)(comm
                                              + (uint32_t)row * AM2_PLAYER_STRIDE
                                              + AM2_PLAYER_ID);
    int32_t        ms   = PlayerLatency(id);
    uint8_t        ink  = *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR;
    const uint8_t *player;

    /* Re-read, as the original does: it loads the comm object again after
     * the call rather than keeping the register. */
    comm = g_commObject;
    id   = *(const uint32_t *)(comm + (uint32_t)row * AM2_PLAYER_STRIDE
                               + AM2_PLAYER_ID);

    if (id == *(const uint32_t *)(comm + COMM_OFF_OUR_PLAYER_ID))
        return ink;

    if ((uint32_t)ms > AM2_LATENCY_MID)
        ink = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_LAG_MID;
    if ((uint32_t)ms > AM2_LATENCY_BAD)
        ink = *(const uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR;

    player = (const uint8_t *)FindPlayerById(id);
    if (!player)
        return ink;

    if (orig_get_tick_count()
        - *(const uint32_t *)(player + AM2_PLAYER_LAST_SEEN) > AM2_SILENCE_BAD)
        ink = *(const uint8_t *)(uintptr_t)ADDR_COLOUR_STALE;

    return ink;
}

/* 0x00432CE0 -- the PAPER behind it, and it answers a different question: not
 * how the link is behaving but whether the player is ready.
 *
 * Only the HOST sees the "has not confirmed the map" colour, and only for a
 * row that has not. Everyone else, and every confirmed row, falls through to
 * the ready pair. */
uint8_t __cdecl MpNamePaper(int32_t row)
{
    const uint8_t *comm   = g_commObject;
    const uint8_t *player = comm + (uint32_t)row * AM2_PLAYER_STRIDE;

    if (*(const int32_t *)(comm + COMM_OFF_IS_HOST)
        && !*(const int32_t *)(player + COMM_ARMY_OFF_MAP_OK))
        return *(const uint8_t *)(uintptr_t)ADDR_COLOUR_NO_MAP;

    return *(const int32_t *)(player + COMM_ARMY_OFF_READY_TO_LOAD)
               ? *(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG
               : *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR;
}

#define g_hudWidgetA (*(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_A)
#define g_hudWidgetB (*(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_B)
#define g_hudWidgetC (*(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_C)

/* The FIFTH private typedef of the ten-argument creator, and the one that made
 * the count worth stating: it is CreateExplosion, reconstructed in item.cpp,
 * and all five are gone. */
#define orig_spawn_at_aim CreateExplosion

/* FreeAimSprites -- original 0x00412120, two callers. Releases both aim
 * sprite sets and clears the per-army state behind them.
 *
 * NEITHER LOOP BOUND IS A COUNT. Both walk from one global to the next four
 * bytes at a time, so the number of sprites is the DISTANCE between two
 * addresses: 0x004FC8C8 to 0x004FC8E0 is six, and 0x004FC920 to 0x004FC944 is
 * nine. Written the same way rather than as `< 6` and `< 9`, because the
 * bound is the neighbouring global and a literal would silently stop matching
 * if either moved.
 *
 * The two clear loops are the reason this is not just a teardown: the A side
 * zeroes LIVE and STAMP, and the B side zeroes LIVE, FRAME and STAMP -- one
 * more array, not a copy of the same shape. Each runs four times, one per
 * army, which is the one count here that IS a literal distance of 0x10.
 *
 * The original indexes those by a cursor running over the STAMP array and
 * reaching back 0x20 for LIVE, which is how the pairing is visible at all;
 * spelled here as the named arrays it is actually touching. */
void __cdecl FreeAimSprites(void)
{
    AM2_Sprite **spr;
    int32_t      i;

    for (spr = (AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_A;
         spr < (AM2_Sprite **)(uintptr_t)ADDR_AIM_LIVE_A; spr++)
        ReleaseSprite(*spr);

    for (i = 0; i < AM2_AIM_ARMIES; i++) {
        ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_A)[i]  = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_A)[i] = 0;
    }

    for (spr = (AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_B;
         spr < (AM2_Sprite **)(uintptr_t)ADDR_AIM_LIVE_B; spr++)
        ReleaseSprite(*spr);

    for (i = 0; i < AM2_AIM_ARMIES; i++) {
        ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_B)[i]  = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_FRAME_B)[i] = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_B)[i] = 0;
    }
}

/* AimStart -- original 0x00412230, one caller, which is FireWeapon. The A half
 * of an aim marker, and the pair is now complete: THIS ONE DOES THE DAMAGE and
 * AimStartB below draws the marker. Reading either alone would have made the
 * split look like duplication.
 *
 * The first eleven lines are AimStartB's, field for field on the A arrays: the
 * live flag, the point, the stamp only if clear, and a deadline that is a flat
 * AM2_AIM_LIFE_REMOTE_MS for a shot relayed from another player and
 * `2 * ADDR_AIM_LIFE_HALF_A - 1` for our own.
 *
 * THEN THE TWO DIVERGE, AND THE REMOTE CASE IS THE TELL. Where B carries on
 * and spawns its sprite whatever the session says, A RETURNS as soon as it has
 * written the remote deadline. So somebody else's shot gets a marker drawn for
 * it and does no damage here -- the damage is the shooter's own machine's job,
 * and this is the client refusing to double it. Two functions started from one
 * call site, and only the pair shows why.
 *
 * The damage sweep uses ObjectsHitByPoint, the PRECISE test -- rectangle and
 * then the sprite's own mask -- rather than the looser ObjectsAtPoint, and
 * walks its answer through OBJ_OFF_QUERY_NEXT giving every object
 * ADDR_AIM_DAMAGE with kind 1 and the firing uid as the attacker.
 *
 * The point is composed with SIXTEEN-BIT adds of the view origin onto the
 * stored point, so a marker near the edge of the world wraps rather than
 * clamping -- the same arithmetic the spirals and MoveStepPoint use on a
 * packed point, and the same reason.
 */
void __cdecl AimStart(uint32_t uid, int8_t army, uint32_t at)
{
    int32_t   i = army;
    int16_t  *pt = (int16_t *)((uint8_t *)AM2_IMAGE(ADDR_AIM_POINT_A)
                               + (uint32_t)i * 4);
    uint32_t  hit;
    uint8_t  *o;

    ((int32_t *)AM2_IMAGE(ADDR_AIM_LIVE_A))[i] = 1;
    *(uint32_t *)pt = at;

    if (!((int32_t *)AM2_IMAGE(ADDR_AIM_STAMP_A))[i])
        ((int32_t *)AM2_IMAGE(ADDR_AIM_STAMP_A))[i] =
            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)army)) {
        ((int32_t *)AM2_IMAGE(ADDR_AIM_DEADLINE_A))[i] =
            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
            + AM2_AIM_LIFE_REMOTE_MS;
        return;
    }

    ((int32_t *)AM2_IMAGE(ADDR_AIM_DEADLINE_A))[i] =
        *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
        + *(const int32_t *)(uintptr_t)ADDR_AIM_LIFE_HALF_A * 2 - 1;

    ((int16_t *)&hit)[0] =
        (int16_t)(*(const int16_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X + pt[0]);
    ((int16_t *)&hit)[1] =
        (int16_t)(pt[1] + *(const int16_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y);

    o = (uint8_t *)ObjectsHitByPoint(&hit,
                                     (void *)(uintptr_t)ADDR_OBJ_MAP_DESC);
    for (; o; o = *(uint8_t **)(o + OBJ_OFF_QUERY_NEXT))
        DamageObject(o, *(const int32_t *)(uintptr_t)ADDR_AIM_DAMAGE, 1,
                     uid, 0, 0);
}

/* AimStartB -- original 0x00412310, one caller, which is FireWeapon. The other
 * half of starting an aim marker, and orig.h has framed 0x00412230 and this as
 * "the two halves" since before either was read.
 *
 * It is per-ARMY and not per-unit: four parallel arrays indexed by the firing
 * object's OBJ_OFF_ARMY -- ADDR_AIM_LIVE_B, _POINT_B, _STAMP_B and
 * _DEADLINE_B -- so an army has one marker at a time and the next shot moves
 * it. The B set is nine sprites where A has six; that split was already
 * recorded and this is the writer for the B half of it.
 *
 * THE STAMP IS SET ONLY IF IT IS CLEAR and the deadline unconditionally. So
 * the marker remembers when the FIRST shot of a run landed while its expiry
 * keeps moving out with each later one -- a burst reads as one aim, and
 * nothing resets the stamp except whatever clears the array.
 *
 * TWO LIFETIMES AND THE SHORT ONE IS FOR OTHER PEOPLE. In a multiplayer
 * session where CommMustBroadcast refuses this army -- someone else's shot,
 * relayed to us -- the deadline is a flat AM2_AIM_LIFE_REMOTE_MS. Ours, and
 * every shot outside a session, gets `2 * ADDR_AIM_LIFE_HALF_B - 1`. So a
 * remote player's marker is on a fixed timer and our own is on the game's.
 *
 * The spawned marker's position is the stored point PLUS THE VIEW ORIGIN,
 * which is what makes it screen-relative; the point itself is stored raw.
 */
void __cdecl AimStartB(uint32_t uid, int8_t army, uint32_t at)
{
    int32_t   i = army;
    int16_t  *pt = (int16_t *)((uint8_t *)AM2_IMAGE(ADDR_AIM_POINT_B)
                               + (uint32_t)i * 4);
    int32_t   deadline;

    ((int32_t *)AM2_IMAGE(ADDR_AIM_LIVE_B))[i] = 1;
    *(uint32_t *)pt = at;

    if (!((int32_t *)AM2_IMAGE(ADDR_AIM_STAMP_B))[i])
        ((int32_t *)AM2_IMAGE(ADDR_AIM_STAMP_B))[i] =
            *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;

    if (*(const int32_t *)(uintptr_t)ADDR_MP_SESSION
        && !CommMustBroadcast(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                              (int16_t)army)) {
        deadline = *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                   + AM2_AIM_LIFE_REMOTE_MS;
    } else {
        deadline = *(const int32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS
                   + *(const int32_t *)(uintptr_t)ADDR_AIM_LIFE_HALF_B * 2 - 1;
    }
    ((int32_t *)AM2_IMAGE(ADDR_AIM_DEADLINE_B))[i] = deadline;

    orig_spawn_at_aim(pt[0] + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X,
                      pt[1] + *(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y,
                      AM2_AIM_SPAWN_KIND, i, uid,
                      *(const int32_t *)(uintptr_t)ADDR_AIM_SPAWN_ARG,
                      0, 0, 0, 0);
}

/* AimInit -- original 0x00412090, one caller.
 *
 * Preload both aim-marker sprite runs -- six frames of set 19 index 4, nine of
 * index 5 -- and clear the per-army state behind them.
 *
 * IT DOES NOT CLEAR EVERYTHING. Run A's live flags and stamps are zeroed and
 * its POINTS and DEADLINES are not; run B's live flags, frames and stamps are
 * zeroed and its points and deadlines are not. Those four arrays therefore
 * start as whatever `.bss` gives them, which is zero at load and whatever the
 * last mission left after that. Nothing reads a point or a deadline whose live
 * flag is clear, so it does not matter -- but "the init clears the state" is
 * not true of this function and a reader should not assume it.
 *
 * The zeroing loops reach two and three arrays from one cursor with a
 * displacement each, which is why the original walks a pointer rather than
 * indexing: -0x20 and 0 in the first, -0x20, 0 and +0x10 in the second.
 * Written as plain indexed loops, which is the same stores in the same order.
 *
 * The sprite runs are DIFFERENT LENGTHS -- six and nine -- and the state
 * arrays behind both are four, one per army. Reading either count as shared is
 * the easy mistake here.
 *
 * PreloadSprite's answer is stored without being checked, as it is at most of
 * its 37 call sites; a run that fails to load leaves null sprite pointers and
 * the painter tests them.
 */
void __cdecl AimInit(void)
{
    uint32_t i;

    for (i = 0; i < AM2_AIM_SPRITES_A; i++)
        ((AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_A)[i] =
            PreloadSprite(AM2_AIM_SET, AM2_AIM_INDEX_A, (int32_t)i,
                          AM2_AIM_PRELOAD_FLAGS, 1);

    for (i = 0; i < AM2_AIM_ARMIES; i++) {
        ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_A)[i]  = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_A)[i] = 0;
    }

    for (i = 0; i < AM2_AIM_SPRITES_B; i++)
        ((AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_B)[i] =
            PreloadSprite(AM2_AIM_SET, AM2_AIM_INDEX_B, (int32_t)i,
                          AM2_AIM_PRELOAD_FLAGS, 1);

    for (i = 0; i < AM2_AIM_ARMIES; i++) {
        ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_B)[i]  = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_FRAME_B)[i] = 0;
        ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_B)[i] = 0;
    }
}

/* AimMarkerAge -- original 0x00412190, one caller.
 *
 * Expire the aim markers and, for the LOCAL player only, drag its two points
 * along behind the cursor. Four armies, two tables, one pass. The tables are
 * mapdraw.cpp's DrawEffectLayer draws.
 *
 * THIS IS THE FUNCTION THAT MADE orig.h SAY "records of 0x64 bytes", and the
 * reconstruction is what settles that it does not. It handles both tables in
 * one unrolled body, so it reads `[eax]` and `[eax+0x64]` and 0x64 looks like
 * a stride; the bottom of its loop is `add eax, 4` against `cmp eax,
 * 0x004FC8F0`, which is four entries four bytes apart. Every offset it uses
 * lands exactly on a name given to those parallel arrays two commits ago --
 * +0x00, +0x10, +0x20, +0x30 for the A table and +0x64, +0x74, +0x84, +0x94,
 * +0xA4 for the B one -- which is the confirmation, and it is worth more than
 * the correction was.
 *
 * THE DEADLINE IS TESTED TWICE FOR OUR OWN ARMY, and reproduced. The local
 * arm expires the entry and then stamps the cursor into its point; the code
 * below it expires the entry again for every army including ours. The second
 * test always sees the flag the first one cleared, so it is dead for the
 * local player and load-bearing for everybody else -- one shared tail rather
 * than a bug.
 *
 * AND THE TWO B EXPIRIES ARE NOT THE SAME. The local one clears the flag and
 * the stamp; the shared one clears the flag, the stamp AND the random frame at
 * ADDR_AIM_FRAME_B. So a remote army's marker forgets which flicker frame it
 * was on and the local player's does not. Asymmetric in the original, and not
 * obviously deliberate; reproduced rather than tidied, as with the two state
 * handlers that check their flags in opposite orders.
 *
 * The stamp runs whether or not the entry just EXPIRED -- it sits after the
 * expiry and outside its `if`, but inside the live test above it. So a marker
 * dying this frame still gets one last cursor position written into it, and a
 * marker that was already dead gets none.
 */
void __cdecl AimMarkerAge(void)
{
    int32_t  cursorX = *(const int32_t *)(uintptr_t)ADDR_CURSOR_X;
    int32_t  cursorY = *(const int32_t *)(uintptr_t)ADDR_CURSOR_Y;
    uint32_t owner   = *(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;
    uint32_t now     = *(const uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS;
    uint32_t army;

    for (army = 0; army < AM2_COMM_SLOTS; army++) {
        uint8_t *a = (uint8_t *)(uintptr_t)ADDR_AIM_LIVE_A + army * 4;

        if (owner == army) {
            if (*(const int32_t *)a) {
                if (now >= *(const uint32_t *)(a + 0x30)) {
                    *(int32_t *)a          = 0;
                    *(int32_t *)(a + 0x20) = 0;
                }
                *(int16_t *)(a + 0x10) = (int16_t)cursorX;
                *(int16_t *)(a + 0x12) = (int16_t)cursorY;
            }
        }

        if (*(const int32_t *)a && now >= *(const uint32_t *)(a + 0x30)) {
            *(int32_t *)a          = 0;
            *(int32_t *)(a + 0x20) = 0;
        }

        if (owner == army) {
            if (*(const int32_t *)(a + 0x64)) {
                if (now >= *(const uint32_t *)(a + 0xA4)) {
                    *(int32_t *)(a + 0x64) = 0;
                    *(int32_t *)(a + 0x94) = 0;
                }
                *(int16_t *)(a + 0x74) = (int16_t)cursorX;
                *(int16_t *)(a + 0x76) = (int16_t)cursorY;
            }
        }

        if (*(const int32_t *)(a + 0x64) && now >= *(const uint32_t *)(a + 0xA4)) {
            *(int32_t *)(a + 0x64) = 0;
            *(int32_t *)(a + 0x84) = 0;   /* and the frame, unlike above */
            *(int32_t *)(a + 0x94) = 0;
        }
    }
}

/* 0x00414370, one caller -- the per-frame path. HudPaint's twin: the same
 * three top-level widgets, in the same order and with the same null test on
 * the third, but through vtable slot 2 rather than slot 1.
 *
 * Slot 2 takes no arguments at all, so this is the plain thiscall the paint
 * pass is not -- which is why the two functions look so different in the
 * disassembly despite doing the same walk.
 *
 * Two more steps follow the widgets, and the second is a tail JUMP rather than
 * a call, so it inherits this function's return. Both stay original; see
 * ADDR_HUD_MARKER_AGE for the little that is established about the second.
 *
 * Measured at 19,324 calls, beside HudPaint's 19,406 and ComposeFrame's
 * 19,492 -- the two HUD passes run once a frame each, as the pair suggests. */
void __cdecl HudUpdate(void)
{
    ((AM2_WidgetUpdateFn *)g_hudWidgetA->vtable)[WIDGET_VSLOT_UPDATE](
        g_hudWidgetA);
    ((AM2_WidgetUpdateFn *)g_hudWidgetB->vtable)[WIDGET_VSLOT_UPDATE](
        g_hudWidgetB);

    if (g_hudWidgetC)
        ((AM2_WidgetUpdateFn *)g_hudWidgetC->vtable)[WIDGET_VSLOT_UPDATE](
            g_hudWidgetC);

    HudPostUpdate();
    AimMarkerAge();
}

/* 0x004143A0, two callers, one of them the per-frame path. Point the drawing
 * at the back buffer and paint the three top-level HUD widgets through vtable
 * slot 1, each with its own absolute rectangle.
 *
 * The third is null-checked and the first two are not, which is the same
 * asymmetry 0x00414370 has for the update pass -- so it is the arrangement of
 * the HUD rather than a slip in one of them.
 *
 * A note on the original's stack, because it looks wrong and is not, and
 * because a previous session left this function alone over exactly that. The
 * first of the three does `sub esp, 0xc` and then writes SIXTEEN bytes of
 * rectangle. That reads as a four-byte overrun and an unbalanced epilogue --
 * until you notice that SetDrawTarget's pushed argument is never cleaned up.
 * The stale dword is still sitting there, the compiler counts it as the last
 * quarter of the struct, and the callee pops all sixteen. It balances exactly.
 * The other two reserve the full 0x10 because by then there is nothing stale
 * to reuse. None of that survives into C, where the argument is a value and
 * the accounting is the compiler's -- it is written down so the next reader
 * does not stop where I stopped.
 *
 * Measured at 19,177 calls against ComposeFrame's 19,257, and the HUD it
 * produces is correct by eye: minimap, the two panels, the portrait and its
 * stats, and the command bar all present. */
void __cdecl HudPaint(void)
{
    SetDrawTarget(*(LPDIRECTDRAWSURFACE *)(uintptr_t)ADDR_BACK_BUFFER);

    ((AM2_WidgetPaintFn *)g_hudWidgetA->vtable)[WIDGET_VSLOT_PAINT](
        g_hudWidgetA, g_hudWidgetA->rect);
    ((AM2_WidgetPaintFn *)g_hudWidgetB->vtable)[WIDGET_VSLOT_PAINT](
        g_hudWidgetB, g_hudWidgetB->rect);

    if (g_hudWidgetC)
        ((AM2_WidgetPaintFn *)g_hudWidgetC->vtable)[WIDGET_VSLOT_PAINT](
            g_hudWidgetC, g_hudWidgetC->rect);
}

/* 0x00431D70, two callers. Empties the menu message log.
 *
 * Fifteen bytes, and the whole of it is a null check and a TAIL-JUMP into
 * ADDR_RECORD_RESET with the list in ecx -- a thiscall, so the list is the
 * `this`. Written as an ordinary guarded call, which is equivalent because
 * nothing follows it.
 *
 * VERIFIED BY READING. Both callers are ours, so the counter cannot move; one
 * is the start-game path and the other the session reopen, and neither runs
 * on any drive here. */
void __cdecl ClearMenuMsgs(void)
{
    void *list = *(void **)(uintptr_t)ADDR_MENU_MSG_LIST;

    if (list)
        RecordReset(list);
}



/* 0x004269B0, one caller -- WndProc, when the application is activated.
 * Repaints whatever is on screen, because the surfaces may have been lost
 * while the game was in the background.
 *
 * IT PAINTS EVERYTHING TWICE, and that is the thing worth stating. The
 * dialog's paint slot is called twice with the same rectangle, and the bitmap
 * is drawn twice with the same arguments -- back to back, with NO change of
 * draw target between them. So it is not front-buffer-then-back; it is the
 * same surface twice.
 *
 * Reproduced, and NOT explained. A repaint that is idempotent loses nothing by
 * running twice, so this costs only time and no reading here says why the
 * author wanted it. Collapsing it to one call would very likely look
 * identical, which is exactly why it is left alone: "probably redundant" is
 * not a reason to remove something from a reconstruction.
 *
 * The refresh at ADDR_REFRESH_SCREEN happens only in game state 2 -- CLAUDE.md
 * lists that function as never having executed, and this is one of its seven
 * call sites.
 *
 * The dialog's rectangle is passed BY VALUE into vtable slot 1, which is the
 * same call WidgetRepaintSelf makes; AM2_WidgetPaintFn already spells it.
 *
 * VERIFIED BY READING. Its one caller is WndProc's activation message, and
 * nothing under Xvfb alt-tabs -- CLAUDE.md says as much where it explains why
 * RestoreTileSet is unreachable. */
void __cdecl OnAppActivated(void)
{
    *(int32_t *)(uintptr_t)ADDR_APP_ACTIVE = 1;
    RestoreLostSurfaces();

    if (*(const int32_t *)(uintptr_t)ADDR_GAME_STATE == 2)
        RefreshScreen();

    if (g_paintObject) {
        AM2_Widget *w = (AM2_Widget *)g_paintObject;

        SetDrawTarget(g_primarySurface);
        ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
        ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
    }

    if (g_currentBitmap) {
        void   *bmp = g_currentBitmap;
        int32_t w   = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_W);
        int32_t h   = *(const int32_t *)((uint8_t *)bmp + SPR_OFF_H);
        int32_t x   = (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W - w) >> 1;
        int32_t y   = (*(const int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H - h) >> 1;

        SetDrawTarget(g_primarySurface);
        DrawSprite((AM2_Sprite *)bmp, x, y, 0);
        DrawSprite((AM2_Sprite *)bmp, x, y, 0);
    }
}


/* 0x00432D40, thiscall, two instructions: the row name's ink setter. It is a
 * separate function rather than a store at the call site because the caller
 * computes the colour with MpNameInk and hands it straight over. */
void __attribute__((thiscall)) MpNameSetInk(AM2_Widget *w, uint8_t ink)
{
    ((uint8_t *)w)[MPNAME_OFF_INK] = ink;
}

/* 0x00454AD0, three callers, thiscall. Give the multiplayer map preview a new
 * bitmap.
 *
 * THE POINTER GOES IN TWO PLACES and both matter. PREVIEW_OFF_SPRITE is the
 * class's own slot, which is what the release at the top of the next call
 * finds; AM2_Widget::sprite is the base's, which is what WidgetPaint draws.
 * Storing only one would either leak or draw nothing, and which of the two is
 * not obvious from either line alone.
 *
 * The release comes FIRST and is unconditional -- ReleaseSprite tolerates a
 * null, which is what makes the first call safe. The load is not checked
 * either, so a missing bitmap leaves both slots null and the widget paints
 * nothing; ADDR_SHOW_BAD_PREVIEW is the caller-side answer to that.
 */
void __attribute__((thiscall)) MpPreviewSetBitmap(void *self, const char *name)
{
    uint8_t    *w = (uint8_t *)self;
    AM2_Sprite *spr;

    ReleaseSprite(*(AM2_Sprite **)(w + PREVIEW_OFF_SPRITE));

    spr = PreloadSpriteName(name, *(const int32_t *)(w + PREVIEW_OFF_FLAGS), 1);

    *(AM2_Sprite **)(w + PREVIEW_OFF_SPRITE) = spr;
    ((AM2_Widget *)w)->sprite = spr;
}

/* 0x0041A170, three callers. The width of ADDR_HUD_WIDGET_C, or 0 when there
 * is none.
 *
 * THE CALLERS ARE WHAT MAKE IT A WIDTH. On its own it is a subtraction of two
 * fields of a rect -- right less left -- which could as easily be a
 * coordinate; each caller computes ADDR_SCREEN_W minus this and clamps a
 * horizontal position to the result, which is "keep the thing left of the HUD
 * panel".
 *
 * THE NULL ANSWER OF 0 IS LOAD-BEARING. It makes that clamp the whole screen,
 * so a missing panel needs no special case at any of the three sites. A
 * reconstruction that returned the screen width for a null, which would look
 * more careful, would clamp to zero instead. */
int32_t __cdecl HudPanelWidth(void)
{
    const AM2_Widget *w =
        *(AM2_Widget *const *)(uintptr_t)ADDR_HUD_WIDGET_C;

    if (!w)
        return 0;

    return w->rect.right - w->rect.left;
}

/* 0x004144A0, forty-six callers -- the most-called thing left unreconstructed.
 * Append one line to the HUD's message log.
 *
 * THE SECOND ARGUMENT IS A COLOUR AND IT BECOMES PART OF THE STRING. A
 * non-zero colour writes '^' and the colour byte ahead of the text, so the row
 * holds "^<colour><text>" and the renderer's escape handling does the rest. A
 * zero colour writes the text alone -- there is no "default colour" escape.
 * Both seams this replaces already called the parameter a colour, which is
 * what the two stores confirm rather than establish.
 *
 * THE WIDTH IS MEASURED ON THE TEXT, NOT ON WHAT WAS STORED. TextExtent is
 * handed the caller's pointer, so the two escape bytes are not in the
 * measurement -- which happens to be right, since TextExtent skips '^' anyway,
 * but it is not why: the original simply never measures the composed string.
 *
 * SCROLLING DROPS THE OLDEST AND HAPPENS FIRST. At twelve rows it moves eleven
 * rows down by one and sets the count to eleven, so the new line always has
 * somewhere to go and the count is never bounded anywhere else.
 *
 * THE X POSITION CHAINS OFF THE PREVIOUS ROW: previous x, plus the previous
 * row's width, plus a gap, floored at 640 -- and an empty log starts at 640
 * outright. The floor is a maximum on how far LEFT a row can start, so the
 * first message of a burst sits at the right edge and each one after it steps
 * further right. Stored as a float and read back through an int truncation,
 * which is what the original's _ftol does and what a C cast does.
 *
 * THE RUNNING TOTAL COUNTS AT MOST TEN PER MESSAGE, whatever the length. The
 * original computes strlen TWICE on the short path, which is what a `min`
 * compiles to when the compiler cannot prove the string is unchanged; written
 * as the min it is.
 *
 * NOTHING BOUNDS THE COPY. A row gives the text 80 bytes before the x position
 * begins, and this is a plain copy of the caller's string. Reproduced.
 *
 * MEASURED AT 0 ON BOTH DRIVES, and with forty-six callers that is worth
 * saying rather than assuming. Boot Camp with movement and fire, and the
 * campaign through SELECT PLAYER into MAP 01, both leave it at 0; the counter
 * is not blind, since forty-four of the callers are still the original's and
 * reach it by address, and the two that are ours are the multiplayer chat
 * path. So the HUD message log is for events no drive here produces, and every
 * word above -- the x chaining, the float truncation, the escape composition,
 * the ten-character cap -- is verified by reading and by nothing else.
 */
void __cdecl HudMessage(const char *text, int32_t colour)
{
    uint8_t *h = *(uint8_t *const *)(uintptr_t)ADDR_HUD_WIDGET_A;
    uint8_t *rows;
    uint8_t *row;
    char    *dst;
    int32_t  n;
    int32_t  x;
    size_t   len;

    if (!h)
        return;

    rows = h + HUDLOG_OFF_ROWS;

    if (*(const int32_t *)(h + HUDLOG_OFF_COUNT) >= AM2_HUD_MSG_ROWS) {
        int32_t i;

        for (i = 0; i < AM2_HUD_MSG_ROWS - 1; i++)
            memcpy(rows + i * AM2_HUD_MSG_SIZE,
                   rows + (i + 1) * AM2_HUD_MSG_SIZE,
                   AM2_HUD_MSG_SIZE);
        *(int32_t *)(h + HUDLOG_OFF_COUNT) = AM2_HUD_MSG_ROWS - 1;
    }

    n   = *(const int32_t *)(h + HUDLOG_OFF_COUNT);
    row = rows + n * AM2_HUD_MSG_SIZE;

    if ((uint8_t)colour) {
        row[0] = '^';
        row[1] = (uint8_t)colour;
        dst    = (char *)row + 2;
    } else {
        dst = (char *)row;
    }

    strcpy(dst, text);

    *(int32_t *)(row + HUDMSG_OFF_WIDTH) = TextExtent(text, 1, (int32_t *)0);

    x = AM2_HUD_MSG_X_MIN;
    if (n > 0) {
        const uint8_t *prev = row - AM2_HUD_MSG_SIZE;
        int32_t        v    = (int32_t)*(const float *)(prev + HUDMSG_OFF_X)
                              + *(const int32_t *)(prev + HUDMSG_OFF_WIDTH)
                              + AM2_HUD_MSG_GAP;

        if (v >= AM2_HUD_MSG_X_MIN)
            x = v;
    }
    *(float *)(row + HUDMSG_OFF_X) = (float)x;

    *(int32_t *)(h + HUDLOG_OFF_COUNT) = n + 1;

    len = strlen(text);
    if (len > AM2_HUD_TOTAL_CAP)
        len = AM2_HUD_TOTAL_CAP;
    *(int32_t *)(h + HUDLOG_OFF_BLIPS) += (int32_t)len;
}

typedef int32_t (__cdecl *AM2_PointerPickFn)(void *obj);
typedef void (__cdecl *AM2_PointerActionFn)(void *obj, uint32_t at);

/* Our leader, the way every pointer handler in this band gets it -- including
 * the fallback that cannot run. Promoted to item.cpp once the medkit heal
 * needed it too; see OurLeaderUnit there for the dead-scan note. */
/* The tail FIVE handlers in this band share, byte for byte: mark the unit
 * firing, copy its +0x40, set the two ones, aim at ADDR_AIM_X/Y/Z and name the
 * target. `withMode` is the ONE thing that varies -- 0x00458E30 omits the
 * UNIT_OFF_FIRE_MODE store the other four make, which is a real difference and
 * not a compiler artefact: its two instructions are simply absent.
 *
 * Factored because the five are identical here and CLAUDE.md's warning is about
 * flattening differences, not about sharing what is genuinely the same. Every
 * difference the five have -- the kind test, the extra guard, this flag -- is
 * at a call site where it can be read.
 *
 * IT COSTS THE OFFSET CHECK, THOUGH, AND THAT IS WORTH STATING. checkoffsetuse
 * scans the NAMED function only, so moving the field writes in here leaves all
 * five reporting "C names 0" against eleven displacements the original touches
 * -- the tool cannot see any of it. The tail was checked BEFORE it was
 * factored, when SetWeaponTargetAimed had it inline: fourteen names against
 * eleven displacements, agreeing but for the SIB form of UNIT_OFF_INVENTORY,
 * the zero offset of ITEMTYPE_OFF_KIND, and three int16 stores where the
 * original does a dword and a word. That measurement is what covers these five,
 * and it is not repeatable now. Sharing a tail and keeping the tool's coverage
 * are in tension; this records which was chosen. */
static void RecordAimedFire(uint8_t *u, void *target, int32_t withMode)
{
    *(int32_t *)(u + UNIT_OFF_FIRE_ACTIVE) = 1;
    *(uint8_t *)(u + UNIT_OFF_FIRE_F40)    = *(const uint8_t *)(u + 0x40);
    *(int32_t *)(u + UNIT_OFF_FIRE_F588)   = 1;
    *(int32_t *)(u + UNIT_OFF_FIRE_F58C)   = 1;

    *(int16_t *)(u + UNIT_OFF_FIRE_X) = *(const int16_t *)(uintptr_t)ADDR_AIM_X;
    *(int16_t *)(u + UNIT_OFF_FIRE_Y) = *(const int16_t *)(uintptr_t)ADDR_AIM_Y;
    *(int16_t *)(u + UNIT_OFF_FIRE_Z) = *(const int16_t *)(uintptr_t)ADDR_AIM_Z;

    *(uint32_t *)(u + UNIT_OFF_FIRE_UID) =
        *(const uint32_t *)((const uint8_t *)target + OBJ_OFF_UID);

    if (withMode)
        *(int32_t *)(u + UNIT_OFF_FIRE_MODE) =
            *(const int32_t *)(u + OBJ_OFF_POSE);
}

/* The unit ADDR_WEAPON_OWNER_ID names and the kind of the weapon it holds, or
 * NULL. The five handlers all open with this and then test the kind. */
static uint8_t *AimedFireUnit(int32_t *kind)
{
    uint8_t       *u;
    const uint8_t *weapon;

    u = (uint8_t *)LookupByUID(
            *(const uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID);
    if (!u)
        return (uint8_t *)0;
    if (!ObjIsType2((const AM2_Object *)u))
        return (uint8_t *)0;

    weapon = (const uint8_t *)WeaponByUid(
        *(const uint32_t *)(u + UNIT_OFF_INVENTORY
            + (uint32_t)*(const int32_t *)(uintptr_t)ADDR_WEAPON_SLOT * 4));
    if (!weapon)
        return (uint8_t *)0;

    *kind = *(const int32_t *)
        (*(const uint8_t *const *)(weapon + OBJ_OFF_FIELD_C0)
         + ITEMTYPE_OFF_KIND);
    return u;
}

/* FOUR MORE COLUMN-1 HANDLERS, and they were transcribed from a DIFF rather
 * than read one at a time. Normalising the five bodies and diffing them against
 * 0x00458D70 leaves 47-58 instructions each at 0.76-0.84 similarity, and every
 * difference is one of three things: which kind the handler accepts, one extra
 * guard on 0x00458CB0, and whether UNIT_OFF_FIRE_MODE is written.
 *
 * That is the method CLAUDE.md prescribes for functions that look like one
 * function twice, and here it turned four separate reads into one diff and four
 * one-line differences. The kinds come from the caption table, which is the
 * program's own vocabulary for these numbers.
 *
 * The kind test SHAPE differs too and is reproduced: 0x00458D70 takes a RANGE
 * (DISG_0..DISG_3, four disguises sharing one handler) and these four each take
 * a single equality. */
void __cdecl SetWeaponTargetMedic(void *target, uint32_t at)
{
    int32_t  kind;
    uint8_t *u;

    (void)at;
    if (!target)
        return;
    u = AimedFireUnit(&kind);
    if (!u || kind != AM2_ITEM_KIND_MEDI)
        return;
    RecordAimedFire(u, target, 1);
}

void __cdecl SetWeaponTargetWrench(void *target, uint32_t at)
{
    int32_t  kind;
    uint8_t *u;

    (void)at;
    if (!target)
        return;
    u = AimedFireUnit(&kind);
    if (!u || kind != AM2_ITEM_KIND_WREN)
        return;
    RecordAimedFire(u, target, 1);
}

/* The only one with an extra refusal, and it sits between the type test and the
 * weapon lookup: the UNIT's own OBJ_OFF_SOLDIER_KIND must be under 6. */
void __cdecl SetWeaponTargetKind2A(void *target, uint32_t at)
{
    int32_t  kind;
    uint8_t *u;

    (void)at;
    if (!target)
        return;
    u = (uint8_t *)LookupByUID(
            *(const uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID);
    if (!u || !ObjIsType2((const AM2_Object *)u))
        return;
    if (*(const int32_t *)(u + OBJ_OFF_SOLDIER_KIND) >= 6)
        return;

    u = AimedFireUnit(&kind);
    if (!u || kind != AM2_ITEM_KIND_2A)
        return;
    RecordAimedFire(u, target, 1);
}

/* The minesweeper, and the one that does NOT write UNIT_OFF_FIRE_MODE -- the
 * two instructions the other four have are absent here, so whatever mode the
 * unit was in survives the order. */
void __cdecl SetWeaponTargetSweeper(void *target, uint32_t at)
{
    int32_t  kind;
    uint8_t *u;

    (void)at;
    if (!target)
        return;
    u = AimedFireUnit(&kind);
    if (!u || kind != AM2_ITEM_KIND_MSWP)
        return;
    RecordAimedFire(u, target, 0);
}

/* SetWeaponTargetAimed -- original 0x00458D70, 192 bytes. Column 1 -- the
 * ACTION -- of four consecutive weapon-handler records at 0x00489AB0..0x00489AE0,
 * whose column 0 is PointerPickBoard above.
 *
 * IT IS ADDR_SET_WEAPON_TARGET'S SIBLING and item.cpp already has that one.
 * Both record a fire request on the unit ADDR_WEAPON_OWNER_ID names, in the
 * same UNIT_OFF_FIRE_* block, and neither fires anything -- the block is for
 * whoever reads it next. Three things differ, and they are the function:
 *
 *   - It gates on the WEAPON KIND, which the other does not: the held weapon's
 *     ITEMTYPE_OFF_KIND must be in 0x23..0x26. That is the same field the
 *     handler table is indexed by, so this re-checks at runtime what the
 *     registration already implies.
 *   - It refuses a NULL target outright, where the other has a whole arm for
 *     firing at a bare point.
 *   - It aims at ADDR_AIM_X/Y/Z as well as naming the target, where the other
 *     zeroes the position when it has an object. So this is "fire at that
 *     thing, from this aim", and the sibling is "fire at that thing" or "fire
 *     at that spot".
 *
 * THOSE THREE GLOBALS WERE CALLED ADDR_PERF_WORD_A/B/C until this function was
 * read. They sit beside the performance-counter globals and InitTimer clears
 * all three, so they were named from the site that zeroes them -- and this is
 * one of seven readers that copy the triple into UNIT_OFF_FIRE_X, _Y and _Z.
 * Renamed; see orig.h.
 *
 * Not exercised. ADDR_SET_WEAPON_TARGET's own note records it measured at 0
 * because its call sites are the pointer-mode action paths and no drive here
 * installs a mode above 0; the same holds for this one. Verified by reading. */
void __cdecl SetWeaponTargetAimed(void *target, uint32_t at)
{
    uint8_t       *u;
    const uint8_t *weapon;

    (void)at;

    if (!target)
        return;

    u = (uint8_t *)LookupByUID(
            *(const uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID);
    if (!u)
        return;
    if (!ObjIsType2((const AM2_Object *)u))
        return;

    weapon = (const uint8_t *)WeaponByUid(
        *(const uint32_t *)(u + UNIT_OFF_INVENTORY
            + (uint32_t)*(const int32_t *)(uintptr_t)ADDR_WEAPON_SLOT * 4));
    if (!weapon)
        return;

    {
        int32_t kind = *(const int32_t *)
            (*(const uint8_t *const *)(weapon + OBJ_OFF_FIELD_C0)
             + ITEMTYPE_OFF_KIND);

        if (kind < AM2_ITEM_KIND_DISG_0 || kind > AM2_ITEM_KIND_DISG_3)
            return;
    }

    RecordAimedFire(u, target, 1);
}

/* The tail modes 4 and 5 share once the object is known to be a FRIEND: it must
 * be ours, and then either a vehicle with a free seat -- which shows a hint and
 * answers 0, exactly as PointerPickBoard does -- or a trooper, vehicle or roach
 * that is not already busy, which answers 1.
 *
 * The vehicle arm has a guard the others do not: it scans the SELECTION for a
 * type-3 that is already selected, and refuses if it finds one. So the board
 * hint is suppressed while a vehicle is under selection.
 *
 * Its OverlayPrepare force flag is 0 here where PointerPickBoard passes 1 for
 * the same row. Reproduced; nothing here says why they differ. */
static int32_t PointerFriendTail(uint8_t *o)
{
    int32_t type;

    if (*(const int8_t *)(o + OBJ_OFF_ARMY)
        != *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
        return 0;
    if (!o)
        return 0;

    type = *(const int32_t *)o;

    if (type == AM2_OBJ_TYPE_VEHICLE
        && !*(const int32_t *)(o + OBJ_OFF_FIELD_94)
        && *(const int32_t *)(o + OBJ_OFF_POSE_PENDING)
               < *(const int32_t *)(o + VEHICLE_OFF_SEATS)) {
        int32_t i;
        int32_t none = 1;

        if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            && GetTickCount()
                   - *(const uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS_MS
               >= AM2_CLICK_MS)
            return 0;

        for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT; i++) {
            const uint8_t *sel = (const uint8_t *)LookupByUID(
                (*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);

            if (sel && *(const int32_t *)sel == AM2_OBJ_TYPE_VEHICLE
                && (*(const uint32_t *)(sel + OBJ_OFF_FLAGS)
                    & OBJ_FLAG_SELECTED))
                none = 0;
        }
        if (!none)
            return 0;

        OverlayPrepare(AM2_OVERLAY_ROW_BOARD, 0);
        *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID =
            *(const uint32_t *)(o + OBJ_OFF_UID);
        return 0;
    }

    if (type < AM2_OBJ_TYPE_TROOPER)
        return 0;
    if (type > AM2_OBJ_TYPE_VEHICLE && type != AM2_OBJ_TYPE_ROACH)
        return 0;
    if (*(const int32_t *)(o + OBJ_OFF_FIELD_94))
        return 0;
    return 1;
}

/* PointerPickMode4 and PointerPickMode5 -- originals 0x00458EE0 and 0x004590F0,
 * 528 bytes each, the PICK slots of pointer modes 4 and 5.
 *
 * BOTH INLINE ArmyAlliedWithObj, which army.cpp has had as a function all
 * along -- the record-to-index mapping against ADDR_OBJ_TABLE_RECORDS is that
 * body, arm for arm. Written as calls, so that the only thing left in each of
 * these is what makes it itself.
 *
 * AND WHAT MAKES THEM DIFFER IS ONE `je` TARGET. Mode 4 hoists an
 * `obj->army == 4` refusal ABOVE the alliance test and sends it to the FAILURE
 * exit; mode 5 leaves that test inside ArmyAlliedWithObj, where army 4 returns
 * ALLIED. So a NEUTRAL object is refused outright by one and treated as a
 * friend by the other. Nothing but a diff shows that, and writing the two from
 * one mental model would have made them agree.
 *
 * `useRec3` is 0 in both: the original reads SAVED_OFF_TABLE_REC2 with no
 * OBJ_OFF_FIELD_530 test, which is the arm ArmyAlliedWithObj takes for 0.
 *
 * A FOE gets the enemy overlay row and answers 1. A FRIEND falls into the
 * shared tail above. The opening refusals are the same in both: destroyed or
 * concealed -- one `test` against OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED --
 * and zero health.
 *
 * Not exercised: no drive here installs a pointer mode above 0. Verified by
 * reading, and by the diff for the part that differs. */
int32_t __cdecl PointerPickMode4(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS)
        & (OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED))
        return 0;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
        return 0;
    if (*(const int8_t *)(o + OBJ_OFF_ARMY) == AM2_ARMY_NEUTRAL)
        return 0;   /* hoisted above the alliance test -- see above */

    if (ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                          o, 0))
        return PointerFriendTail(o);

    OverlayPrepare(AM2_OVERLAY_ROW_ENEMY, 1);
    *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID =
        *(const uint32_t *)(o + OBJ_OFF_UID);
    return 1;
}

int32_t __cdecl PointerPickMode5(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS)
        & (OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED))
        return 0;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
        return 0;

    /* No hoisted refusal: a neutral object reaches the alliance test, which
     * answers ALLIED for army 4. That is the whole difference from mode 4. */
    if (ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                          o, 0))
        return PointerFriendTail(o);

    OverlayPrepare(AM2_OVERLAY_ROW_ENEMY, 1);
    *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID =
        *(const uint32_t *)(o + OBJ_OFF_UID);
    return 1;
}

/* PointerPickMode6 -- original 0x00459300, 288 bytes, mode 6's PICK.
 *
 * THE SAME TWO REFUSALS AND THEN THE SHARED FRIEND TAIL, with no alliance test
 * at all: it goes straight to the tail, whose first act is to require the
 * object be OURS. So mode 6 offers nothing on an enemy, where modes 4 and 5
 * show the enemy overlay and answer 1.
 *
 * 288 bytes that come to three lines, because the tail was factored out for
 * modes 4 and 5 first. That is the return on the previous two commits rather
 * than a claim about this function being simple -- the original writes the
 * whole tail out again here.
 *
 * The one encoding difference is cosmetic and worth noting so a reader diffing
 * against the others does not stop on it: this tests OBJ_FLAG_SELECTED as
 * `test ch, 4` on the flags dword where modes 4 and 5 load 0x400 into a
 * register first. Same bit.
 *
 * Not exercised -- no drive installs a pointer mode above 0. */
int32_t __cdecl PointerPickMode6(void *obj)
{
    uint8_t *o = (uint8_t *)obj;

    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS)
        & (OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED))
        return 0;
    if (*(const int16_t *)(o + OBJ_OFF_HEALTH) == 0)
        return 0;

    return PointerFriendTail(o);
}

/* PointerPickMode0 -- original 0x00459420, 912 bytes, the PICK of pointer mode
 * 0 -- the DEFAULT mode, and the only one in this family a drive reaches.
 *
 * It ends in the same friend tail as modes 4, 5 and 6, so the length is mostly
 * the alliance block and that tail written out again. What is its own is the
 * ENEMY arm, which the other three do not have in this form.
 *
 * TWO REFUSALS THE OTHERS DO NOT HAVE, and the first is the interesting one: it
 * refuses an object ObjIsWatchedKind ACCEPTS. PointerPickWatchedItem exists to
 * offer exactly those, so the default pointer stands aside for the mode that
 * handles them. Then army 4 is refused outright, as in mode 4.
 *
 * AN ENEMY IS OFFERED ONLY IF OUR LEADER'S WEAPON REACHES IT, and which weapon
 * that is depends on whether the leader is riding: with OBJ_OFF_RIDING set it
 * is the VEHICLE's VEHICLE_OFF_WEAPON_UID, otherwise the leader's own selected
 * inventory slot. The reach is the weapon definition's ITEMTYPE_OFF_RANGE times
 * ADDR_WEAPON_RANGE_K3, which is 1.2.
 *
 * THE COMPARISON IS x87 AND THE SENSE IS EASY TO INVERT. `fild dist`,
 * `fild range`, `fmul 1.2`, `fcompp`, `test ah, 1` -- C0 is set when the
 * TOP of stack, the scaled range, is LESS than the distance. So the refusal is
 * `range * 1.2 < dist` and the offer is everything at or inside it. Written in
 * that order rather than flipped, so it can be read against the instructions.
 *
 * AND THE ENEMY ARM ANSWERS 0. It shows an overlay row and stores the hover
 * uid and then returns 0, like the vehicle hint in the friend tail -- so "the
 * pointer has something to say about this" and "the pointer will act on this"
 * are different, and only the friend tail's last exit is a yes.
 *
 * ONE GUARD IS A MENU TEST: with the mouse button down, the enemy arm is
 * skipped unless GetMenuRow() is 1. The other three picks have no equivalent.
 *
 * MEASURED AT 8,520 CALLS, which makes this the only PICK in the family that
 * runs at all -- the other five read 0 on the same drive. A live Boot Camp
 * mission with the pointer moved over units gives PointerPickMode0=8520 beside
 * PointerSelect=3, so it is asked many times a second about whatever is under
 * the cursor and answers for real.
 *
 * THAT IS COVERAGE AND NOT COMPARISON, and the difference matters here. The A/B
 * configurations stop at a dialog and never move the pointer over a unit, so a
 * clean bootcamp/campaign run does not compare this function at all -- it is
 * covered by the drive above and by reading. Which arm those 8,520 calls take
 * is also unmeasured: almost all of them are likely the early refusals, and the
 * weapon-reach arm is the interesting one. A five-figure counter is not
 * evidence that the branch you care about ran, which this project has recorded
 * before for ShotStrike. */
int32_t __cdecl PointerPickMode0(void *obj)
{
    uint8_t       *o = (uint8_t *)obj;
    uint8_t       *leader;
    const uint8_t *weapon;
    const uint8_t *ride;
    int32_t        owner;
    uint32_t       uid;

    if (ObjIsWatchedKind(o))
        return 0;
    if (*(const int8_t *)(o + OBJ_OFF_ARMY) == AM2_ARMY_NEUTRAL)
        return 0;

    owner = *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;
    if (owner == AM2_ARMY_NEUTRAL)
        return PointerFriendTail(o);
    if (ArmyAlliedWithObj(owner, o, 0))
        return PointerFriendTail(o);

    /* An enemy. */
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON && GetMenuRow() != 1)
        return 0;

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    if (*(const uint32_t *)(leader + OBJ_OFF_RIDING)) {
        ride = (const uint8_t *)LookupType3ByUID(
            *(const uint32_t *)(leader + OBJ_OFF_RIDING));
        uid = *(const uint32_t *)(ride + VEHICLE_OFF_WEAPON_UID);
        if (uid == 0)
            return 0;
    } else {
        uid = *(const uint32_t *)(leader + UNIT_OFF_INVENTORY
            + (uint32_t)*(const int32_t *)(leader + UNIT_OFF_INVENTORY_SEL) * 4);
    }

    weapon = (const uint8_t *)WeaponByUid(uid);
    if (!weapon)
        return 0;

    if ((double)*(const int32_t *)
            (*(const uint8_t *const *)(weapon + OBJ_OFF_FIELD_C0)
             + ITEMTYPE_OFF_RANGE)
            * *(const double *)(uintptr_t)ADDR_WEAPON_RANGE_K3
        < (double)ApproxDist((const AM2_Point *)(leader + OBJ_OFF_X),
                             (const AM2_Point *)(o + OBJ_OFF_X)))
        return 0;

    OverlayPrepare(AM2_OVERLAY_ROW_FIRE, 1);
    *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID =
        *(const uint32_t *)(o + OBJ_OFF_UID);
    return 0;   /* a hint, not a yes -- see above */
}

/* PointerPickHeal -- original 0x004597B0, the first of the two functions in the
 * 0x004597B0 entry. A PICK gated on a MENU ROW rather than on a pointer mode.
 *
 * ITS FIRST TWO GUARDS ARE THE SAME TEST TWICE, on two different globals: with
 * ADDR_MOUSE_BUTTON set it requires GetMenuRow() to be 0xB, and then with
 * ADDR_MOUSE_CHANGED set it requires the same thing again. Either can refuse
 * independently. Reproduced as the two tests the original makes rather than
 * collapsed into one, because they read different globals.
 *
 * WHAT IT OFFERS: a friendly TROOPER that is HURT -- health strictly below
 * OBJ_OFF_MAX_HEALTH -- riding whatever our leader is riding, and within a
 * reach threshold. That last condition is the interesting one: it compares
 * OBJ_OFF_RIDING on both, so a leader on foot can only heal someone on foot and
 * a leader in a vehicle only its passengers. Not "the same vehicle" as a
 * special case; the same field, which is 0 for both when neither is riding.
 *
 * The alliance test is ArmyAlliedWithObj again, and the refusal is the plain
 * one: not allied, no offer. This is the first pick in the family whose
 * alliance test has no hoisted guard around it.
 *
 * Unlike modes 4, 5 and 6 it does NOT use the shared friend tail -- it has its
 * own smaller set of conditions and answers 1 directly.
 *
 * Not exercised: it needs menu row 0xB, and no drive here opens that row. Its
 * counter is not blind, so that is checkable if a drive ever reaches it.
 *
 * AND checkoffsetuse IS NOISY HERE FOR A REASON WORTH KNOWING: this address's
 * functions.tsv entry is 1,072 bytes and holds TWO functions, so the tool's
 * "original" side is both of them. It reports OBJ_OFF_FIELD_9C among the
 * offsets this function does not name, and that offset belongs to the sibling
 * at 0x004599A0. A merged entry makes that tool over-report the same way it
 * makes coverage over-credit. */
int32_t __cdecl PointerPickHeal(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *leader;

    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
        && GetMenuRow() != AM2_MENU_ROW_HEAL)
        return 0;
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED
        && GetMenuRow() != AM2_MENU_ROW_HEAL)
        return 0;

    if (!ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                           o, 0))
        return 0;

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    if (*(const int16_t *)(o + OBJ_OFF_HEALTH)
        >= *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH))
        return 0;
    if (!o || *(const int32_t *)o != AM2_OBJ_TYPE_TROOPER)
        return 0;
    if (*(const uint32_t *)(leader + OBJ_OFF_RIDING)
        != *(const uint32_t *)(o + OBJ_OFF_RIDING))
        return 0;

    if (ApproxDist((const AM2_Point *)(leader + OBJ_OFF_X),
                   (const AM2_Point *)(o + OBJ_OFF_X))
        > *(const int32_t *)(uintptr_t)ADDR_PICK_REACH_6624EC)
        return 0;

    OverlayPrepare(AM2_MENU_ROW_HEAL, 1);
    return 1;
}

/* PointerPickRepair -- original 0x004599A0, the second function in the
 * 0x004597B0 entry, and PointerPickHeal's sibling: the same two menu-row
 * guards, on row 0xC instead of 0xB.
 *
 * OBJ_OFF_REPAIR_FRAME NAMED THIS ONE. The field at +0x9C already carried that
 * name, and this function is what reads it -- so "repair" is the tree's own
 * word rather than a guess from the row number.
 *
 * TROOPERS ARE REFUSED OUTRIGHT, which is what separates it from its sibling:
 * heal takes type 2 only, repair rejects type 2 and takes the rest.
 *
 * THE HEALTH TEST IS SKIPPED FOR A THIRD CLASS. An ITEM is offered when it is
 * already being repaired -- OBJ_OFF_REPAIR_FRAME set -- or hurt; a VEHICLE only
 * when hurt; and anything that is NEITHER an item nor a vehicle falls past both
 * tests and is offered whatever its health. That last arm is easy to miss:
 * the `else` has no final test, it just continues.
 *
 * IF OUR LEADER IS RIDING, THE TARGET MUST BE THE THING HE IS RIDING. The
 * leader's OBJ_OFF_RIDING is compared against the object's own uid, so a
 * passenger can repair his vehicle and nothing else. Where the sibling compares
 * OBJ_OFF_RIDING to OBJ_OFF_RIDING -- "we are in the same place" -- this one
 * compares it to the uid. Two lines that look alike and ask different
 * questions.
 *
 * ONE KIND IS REFUSED at the very end: an ITEM whose AAI record's
 * AAIREC_OFF_TYPE is 0x26. Named as a placeholder rather than borrowed from
 * AM2_ITEM_KIND_DISG_3, which is the same number in a different table.
 *
 * Not exercised: no drive here opens menu row 0xC. */
int32_t __cdecl PointerPickRepair(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *leader;
    uint32_t riding;

    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
        && GetMenuRow() != AM2_MENU_ROW_REPAIR)
        return 0;
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED
        && GetMenuRow() != AM2_MENU_ROW_REPAIR)
        return 0;

    if (*(const int8_t *)(o + OBJ_OFF_ARMY) == AM2_ARMY_NEUTRAL)
        return 0;
    if (!ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                           o, 0))
        return 0;

    if (o && *(const int32_t *)o == AM2_OBJ_TYPE_TROOPER)
        return 0;

    if (ObjIsItem((const AM2_Object *)o)) {
        if (!*(const int32_t *)(o + OBJ_OFF_REPAIR_FRAME)
            && *(const int16_t *)(o + OBJ_OFF_HEALTH)
                   >= *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH))
            return 0;
    } else if (o && *(const int32_t *)o == AM2_OBJ_TYPE_VEHICLE) {
        if (*(const int16_t *)(o + OBJ_OFF_HEALTH)
            >= *(const int16_t *)(o + OBJ_OFF_MAX_HEALTH))
            return 0;
    }
    /* Neither an item nor a vehicle: no health test at all -- see above. */

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    riding = *(const uint32_t *)(leader + OBJ_OFF_RIDING);
    if (riding && *(const uint32_t *)(o + OBJ_OFF_UID) != riding)
        return 0;

    if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_X),
                   (const AM2_Point *)(leader + OBJ_OFF_X))
        > *(const int32_t *)(uintptr_t)ADDR_PICK_REACH_662894)
        return 0;

    if (*(const int32_t *)o == AM2_OBJ_TYPE_ITEM
        && *(const int32_t *)(*(const uint8_t *const *)(o + OBJ_OFF_FIELD_94)
                              + AAIREC_OFF_TYPE) == AM2_AAI_TYPE_26)
        return 0;

    OverlayPrepare(AM2_MENU_ROW_REPAIR, 1);
    return 1;
}

/* PointerPickEnemyTrooper -- original 0x00459BE0, gated on menu row 0xA.
 *
 * IT INVERTS THE ALLIANCE TEST, which is what makes it worth reading rather
 * than skimming: every other pick in this band offers something to a FRIEND and
 * refuses a foe, or offers both. This one refuses the moment
 * ArmyAlliedWithObj answers yes. It is the only hostile-only pick here.
 *
 * The two army-4 refusals in front of it fall out of that rather than being
 * extra: ArmyAlliedWithObj answers ALLIED for army 4 on either side, so the
 * original's early `je fail` on each is the same answer reached sooner. Written
 * as the single call, with this note, because writing three tests that all mean
 * "allied, so no" would read as three conditions.
 *
 * WHAT IT OFFERS: an enemy TROOPER that is not our Sarge -- OBJ_OFF_SARGE clear
 * -- whose OBJ_OFF_SOLDIER_KIND is under 6, within reach of our leader.
 *
 * The original tests `type == 2` TWICE, once as a predicate and once as a bare
 * compare a few instructions later, when it already knows the answer. That is
 * the compiler expanding an inlined predicate and then testing the result;
 * written once here, since the second cannot fail.
 *
 * Not exercised: no drive here opens menu row 0xA. */
int32_t __cdecl PointerPickEnemyTrooper(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *leader;

    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
        && GetMenuRow() != AM2_MENU_ROW_0A)
        return 0;

    if (!o)
        return 0;
    if (*(const int32_t *)o != AM2_OBJ_TYPE_TROOPER)
        return 0;
    if (*(const int32_t *)(o + OBJ_OFF_SARGE))
        return 0;

    /* Allied -- including either side being army 4 -- means no. */
    if (ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                          o, 0))
        return 0;

    if (*(const int32_t *)(o + OBJ_OFF_SOLDIER_KIND) >= 6)
        return 0;

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    if (ApproxDist((const AM2_Point *)(o + OBJ_OFF_X),
                   (const AM2_Point *)(leader + OBJ_OFF_X))
        > *(const int32_t *)(uintptr_t)ADDR_PICK_REACH_6628C8)
        return 0;

    OverlayPrepare(AM2_MENU_ROW_0A, 1);
    return 1;
}

/* VehicleDismountAll -- original 0x00458930, 240 bytes, one caller: the
 * per-frame input handler, behind ActionKeyDown(0xD).
 *
 * BOTH ITS ARGUMENTS ARE IGNORED. The caller pushes two and cleans eight bytes,
 * and the body reads no stack slot at all -- its only input is
 * ADDR_OBJ_CTX_OBJ_A. Worth checking rather than assuming a mis-read, because a
 * function that ignores everything it is given is unusual; the body has exactly
 * one source and it is that global.
 *
 * WHAT IT DOES: empty the current vehicle, attaching everyone who leaves to
 * whoever is in seat 0. Seats from 1 upward first -- attach the occupant to the
 * driver, then eject -- and the loop reads the seat count FRESH each turn
 * through ADDR_FIELD_53C rather than counting down, so it follows whatever the
 * ejection did. Then seat 0 is ejected. Then everything on the vehicle's
 * OBJ_OFF_PTR_LIST is attached to the driver as well.
 *
 * THE DRIVER IS LOOKED UP ONCE, BEFORE ANYONE LEAVES, and used for every
 * attach afterwards -- including after seat 0 has itself been ejected. So the
 * followers follow a unit that is no longer in the vehicle, which is the point
 * rather than an oversight.
 *
 * THE LAST LOOP DOES NOT ALWAYS ADVANCE. A uid that no longer resolves is
 * removed from the list and the index is NOT incremented, because the removal
 * shifts the next entry down into it; a uid that does resolve is attached and
 * the index moves on. Written as the original has it -- an `i++` in the wrong
 * arm would skip an entry after every dead one.
 *
 * ListRemoveAt is THISCALL: the original does `lea ecx, [veh + OBJ_OFF_PTR_LIST]`
 * and pushes the index with no `add esp` after, which is callee cleanup.
 *
 * NOT ExitAllFromVehicle, which is 0x0045AE30 and names itself in its own log
 * lines. This one attaches the leavers to the driver and that one does not.
 *
 * Not exercised: its key is not one any drive here presses. */
void __cdecl VehicleDismountAll(void *a, void *b)
{
    uint8_t *veh;
    uint8_t *driver;
    int32_t  i;

    (void)a;
    (void)b;

    veh = *(uint8_t **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A;
    if (!veh)
        return;
    if (*(const int32_t *)veh != AM2_OBJ_TYPE_VEHICLE)
        return;
    if ((int32_t)Field53C(veh) <= 0)
        return;

    driver = (uint8_t *)LookupByUID(
        **(const uint32_t *const *)(veh + OBJ_OFF_FIELD_540));

    while ((int32_t)Field53C(veh) > 1) {
        uint8_t *o = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(veh + OBJ_OFF_FIELD_540))[1]);

        if (o && *(const int32_t *)o == AM2_OBJ_TYPE_TROOPER)
            ObjAttachTo(o, driver);

        if (!ExitOneFromVehicle(1, veh))
            break;
    }

    ExitOneFromVehicle(0, veh);

    for (i = 0; i < *(const int32_t *)(veh + SAVED_OFF_LIST_COUNT); ) {
        uint8_t *o = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(veh + SAVED_OFF_LIST))[i]);

        if (!o) {
            /* No i++ -- the removal shifts the next entry into this slot. */
            ListRemoveAt(veh + OBJ_OFF_PTR_LIST, i);
            continue;
        }
        ObjAttachTo(o, driver);
        i++;
    }
}

/* PointerActionMode6 -- original 0x00458810, 288 bytes, the ACTION slot of
 * pointer mode 6, whose PICK is PointerPickMode6 above.
 *
 * Order the whole SELECTION to move, in formation, without engaging: for each
 * selected unit, compute its formation slot, detach it from whatever it was
 * following, set OBJ_OFF_AI_MODE to 2 -- which orig.h's AI table calls `ignore`
 * -- and hand it the slot point.
 *
 * THE FOURTH ARGUMENT TO FormationSlotPoint IS THE ADDRESS OF THIS FUNCTION'S
 * OWN FIRST ARGUMENT SLOT, and the value is read back out of it afterwards. So
 * `target` is an in-out: it arrives as the object under the pointer and leaves
 * as the formation point, and it is THAT rewritten value the first PointActionA
 * is given. An argument slot reused as an out-param is a shape CLAUDE.md warns
 * about for esp tracking; here it also changes what the call below means.
 *
 * WHICH IS WHY THE TWO PointActionA CALLS DISAGREE. The first passes the
 * rewritten slot point; the second, in the trooper arm, passes the ORIGINAL
 * click point. Two calls to one function with different second arguments, ten
 * instructions apart, and the difference is invisible unless the out-param is
 * noticed.
 *
 * THE TROOPER ARM IS GATED ON A MENU ROW: with GetMenuRow() == 8 the unit's
 * OBJ_OFF_UID_56C takes ADDR_POINTER_HOVER_UID and it is ordered again at the
 * click point; otherwise that field is cleared. So the same action means
 * something extra while one menu row is up.
 *
 * The selection walk has the same don't-always-advance shape as
 * VehicleDismountAll: a uid that no longer resolves is removed from
 * ADDR_SELECTED_UIDS and the index is NOT incremented.
 *
 * Not exercised: no drive here installs a pointer mode above 0. */
void __cdecl PointerActionMode6(void *target, uint32_t at)
{
    uint8_t *ctx = *(uint8_t **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A;
    int32_t  type;
    int32_t  i;

    if (!ctx)
        return;
    if ((uint8_t *)target == ctx)
        return;

    type = *(const int32_t *)ctx;
    if (type < AM2_OBJ_TYPE_TROOPER)
        return;
    if (type > AM2_OBJ_TYPE_VEHICLE && type != AM2_OBJ_TYPE_ROACH)
        return;

    if (SelectIfOwn(target))
        return;

    for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT; ) {
        uint8_t *o = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);
        int32_t  ot;

        if (!o) {
            ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
            continue;   /* no i++ -- the removal shifts the next entry down */
        }

        ot = *(const int32_t *)o;
        if (ot >= AM2_OBJ_TYPE_TROOPER
            && (ot <= AM2_OBJ_TYPE_VEHICLE || ot == AM2_OBJ_TYPE_ROACH)) {
            /* The original hands FormationSlotPoint the address of its own
             * first argument slot; written as a local initialised from it,
             * which is the same dword and the same value in and out. */
            uint32_t slot = (uint32_t)(uintptr_t)target;

            FormationSlotPoint(i, at, o, &slot);

            ObjAttachTo(o, (void *)0);
            *(int32_t *)(o + OBJ_OFF_FIELD_EC) = 1;
            *(int32_t *)(o + OBJ_OFF_AI_MODE)  = AM2_AI_MODE_IGNORE;

            PointActionA(o, slot);

            if (*(const int32_t *)o == AM2_OBJ_TYPE_TROOPER) {
                if (GetMenuRow() == AM2_MENU_ROW_8) {
                    *(uint32_t *)(o + OBJ_OFF_UID_56C) =
                        *(const uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID;
                    PointActionA(o, at);
                } else {
                    *(uint32_t *)(o + OBJ_OFF_UID_56C) = 0;
                }
            }
        }
        i++;
    }
}

/* PointerActionMode4 -- original 0x00458400, 528 bytes, mode 4's ACTION, whose
 * PICK is PointerPickMode4 above. "Attack that, or move there."
 *
 * The frame and the loop are PointerActionMode6's, and the difference is what
 * happens per unit once its formation slot is known:
 *
 *   - NO TARGET, or an ALLIED one -- AI mode 0 and a move to the slot point.
 *   - AN ENEMY target -- AI mode 3, the target's uid into OBJ_OFF_FOLLOW_UID,
 *     and NO move order at all. The unit is given something to chase instead of
 *     somewhere to go.
 *
 * AI MODE 0 IS WRITTEN BEFORE THE BRANCH THAT MIGHT OVERWRITE IT WITH 3, not in
 * the arm that wants it. Reproduced in that order: it is one store either way,
 * but a reader matching the disassembly will find it above the test.
 *
 * ITS ARGUMENT SLOT BECOMES THE LOOP INDEX. `target` is copied into ebx at the
 * top and the slot is then reused to hold `i` across the calls -- the same
 * shape mode 6 has for a different purpose, and the reason the two look alike
 * in the disassembly and are not. Written as an ordinary local, since the
 * original has already taken its copy.
 *
 * THE FORMATION POINT GOES TO A REAL LOCAL HERE, not to an argument slot: the
 * original reserves one with a bare `push ecx` at entry and hands
 * FormationSlotPoint its address. Mode 6 hands over its argument slot instead.
 * Same call, different destination, and only one of the two is an in-out
 * argument.
 *
 * The two PointActionA calls disagree the same way mode 6's do -- the first
 * takes the formation slot, the second the original click point -- and the
 * trooper arm is gated on the same GetMenuRow() == 8.
 *
 * Not exercised: no drive here installs a pointer mode above 0. */
void __cdecl PointerActionMode4(void *target, uint32_t at)
{
    uint8_t *ctx = *(uint8_t **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A;
    uint8_t *tgt = (uint8_t *)target;
    int32_t  type;
    int32_t  i;

    if (!ctx)
        return;
    if (tgt == ctx)
        return;

    type = *(const int32_t *)ctx;
    if (type < AM2_OBJ_TYPE_TROOPER)
        return;
    if (type > AM2_OBJ_TYPE_VEHICLE && type != AM2_OBJ_TYPE_ROACH)
        return;

    if (SelectIfOwn(tgt))
        return;

    for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT; ) {
        uint8_t *o = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);
        uint32_t slot;
        int32_t  ot;

        if (!o) {
            ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
            continue;   /* no i++ -- the removal shifts the next entry down */
        }

        ot = *(const int32_t *)o;
        if (ot < AM2_OBJ_TYPE_TROOPER
            || (ot > AM2_OBJ_TYPE_VEHICLE && ot != AM2_OBJ_TYPE_ROACH)) {
            i++;
            continue;
        }

        FormationSlotPoint(i, at, o, &slot);
        ObjAttachTo(o, (void *)0);

        /* Written before the test that may replace it -- see above. */
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 0;

        if (tgt
            && !ArmyAlliedWithObj(
                   *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER, tgt, 0)) {
            *(int32_t *)(o + OBJ_OFF_AI_MODE)  = 3;
            *(uint32_t *)(o + OBJ_OFF_FOLLOW_UID) =
                *(const uint32_t *)(tgt + OBJ_OFF_UID);
        } else {
            PointActionA(o, slot);
        }

        if (*(const int32_t *)o == AM2_OBJ_TYPE_TROOPER) {
            if (GetMenuRow() == AM2_MENU_ROW_8) {
                *(uint32_t *)(o + OBJ_OFF_UID_56C) =
                    *(const uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID;
                PointActionA(o, at);
            } else {
                *(uint32_t *)(o + OBJ_OFF_UID_56C) = 0;
            }
        }
        i++;
    }
}

/* PointerActionMode5 -- original 0x00458620, 496 bytes, mode 5's ACTION and the
 * last of the three.
 *
 * AN ENEMY TARGET IS HANDED TO MODE 4'S ACTION. The original calls 0x00458400
 * outright and returns -- so "attack that" is not reimplemented here, it is
 * delegated. Only the friendly/no-target case is this function's own, and it is
 * a plain move: AI mode 1 and the formation slot.
 *
 * THE THREE ACTIONS DIFFER IN WHERE FormationSlotPoint WRITES, and that is worth
 * having in one place because the call site looks identical in all three:
 *
 *     mode 4  a dedicated local, reserved by a bare `push ecx` at entry
 *     mode 5  ARGUMENT 1 -- `at`
 *     mode 6  ARGUMENT 0 -- `target`
 *
 * So two of the three have an in-out argument, on different arguments, and the
 * third has none. In each case the ORIGINAL value survives in a register and is
 * used again afterwards, which is what makes the later PointActionA call differ
 * from the earlier one.
 *
 * AND THIS ONE ORDERS ITS CALLS DIFFERENTLY. Modes 4 and 6 do ObjAttachTo and
 * then PointActionA; this does PointActionA and then ObjAttachTo. Same three
 * calls, reversed, and reproduced -- detaching before or after the order is
 * given is not obviously equivalent and it is not ours to decide.
 *
 * AI mode 1 is set BEFORE FormationSlotPoint rather than after, again unlike
 * the other two. Written where the original has it.
 *
 * Not exercised: no drive here installs a pointer mode above 0. */
void __cdecl PointerActionMode5(void *target, uint32_t at)
{
    uint8_t *ctx = *(uint8_t **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A;
    uint8_t *tgt = (uint8_t *)target;
    int32_t  type;
    int32_t  i;

    if (!ctx)
        return;
    if (tgt == ctx)
        return;

    type = *(const int32_t *)ctx;
    if (type < AM2_OBJ_TYPE_TROOPER)
        return;
    if (type > AM2_OBJ_TYPE_VEHICLE && type != AM2_OBJ_TYPE_ROACH)
        return;

    if (tgt
        && !ArmyAlliedWithObj(*(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                              tgt, 0)) {
        PointerActionMode4(tgt, at);   /* an enemy: mode 4's job */
        return;
    }

    if (SelectIfOwn(tgt))
        return;

    for (i = 0; i < *(const int32_t *)(uintptr_t)ADDR_SELECTED_COUNT; ) {
        uint8_t *o = (uint8_t *)LookupByUID(
            (*(const uint32_t *const *)(uintptr_t)ADDR_SELECTED_ITEMS)[i]);
        uint32_t slot;
        int32_t  ot;

        if (!o) {
            ListRemoveAt((void *)(uintptr_t)ADDR_SELECTED_UIDS, i);
            continue;   /* no i++ -- the removal shifts the next entry down */
        }

        ot = *(const int32_t *)o;
        if (ot < AM2_OBJ_TYPE_TROOPER
            || (ot > AM2_OBJ_TYPE_VEHICLE && ot != AM2_OBJ_TYPE_ROACH)) {
            i++;
            continue;
        }

        /* Set before the call, unlike the other two. */
        *(int32_t *)(o + OBJ_OFF_AI_MODE) = 1;

        /* The original passes the address of its `at` argument slot; the
         * original value stays in a register and is used below. */
        slot = at;
        FormationSlotPoint(i, at, o, &slot);

        /* Ordered PointActionA then ObjAttachTo -- the reverse of modes 4
         * and 6. */
        PointActionA(o, slot);
        ObjAttachTo(o, (void *)0);

        if (*(const int32_t *)o == AM2_OBJ_TYPE_TROOPER) {
            if (GetMenuRow() == AM2_MENU_ROW_8) {
                *(uint32_t *)(o + OBJ_OFF_UID_56C) =
                    *(const uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID;
                PointActionA(o, at);
            } else {
                *(uint32_t *)(o + OBJ_OFF_UID_56C) = 0;
            }
        }
        i++;
    }
}

/* PointerPickWatchedItem -- original 0x00459EE0, 208 bytes, one reference: the
 * PICK slot of a record in the second {pick, action, kind, flags} table.
 *
 * Four refusals and then a yes. A null object; one that is not an ITEM of the
 * type id in ADDR_CREATE_WATCHED_KIND, which is what ObjIsWatchedKind answers;
 * one that is OBJ_FLAG_CONCEALED, so the pointer will not offer what the player
 * cannot see; and one further from our leader than the reach threshold.
 * Otherwise it shows an overlay row and answers 1.
 *
 * THE ROW IS 0x11 AND ITS NAME CAME FROM SOMEWHERE ELSE. orig.h calls it
 * AM2_OVERLAY_ROW_SELL, from the placement screen, where it is the cursor shown
 * over a unit that could be sold. The constant is right -- same row, same
 * cursor sheet -- but this table is the ORDER table, one of whose actions is
 * ADDR_SET_WEAPON_TARGET, so nothing here confirms the row means "sell" in this
 * context. Used by number, and the name is not being taken as evidence.
 *
 * The concealed test is `test ah, 2` on the dword at OBJ_OFF_FLAGS, which is
 * bit 9 -- OBJ_FLAG_CONCEALED. Written against the whole dword, which is the
 * same test.
 *
 * Not exercised: nothing in this band runs except mode 0's action, and this
 * table has no identified consumer. Verified by reading. */
int32_t __cdecl PointerPickWatchedItem(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *leader;

    if (!o)
        return 0;
    if (!ObjIsWatchedKind(o))
        return 0;
    if (*(const uint32_t *)(o + OBJ_OFF_FLAGS) & OBJ_FLAG_CONCEALED)
        return 0;

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    if (ApproxDist((const AM2_Point *)(leader + OBJ_OFF_X),
                   (const AM2_Point *)(o + OBJ_OFF_X))
        > *(const int32_t *)(uintptr_t)ADDR_PICK_REACH_662450)
        return 0;

    OverlayPrepare(AM2_OVERLAY_ROW_SELL, 1);
    return 1;
}

/* PointerPickBoard -- original 0x00459DA0, 320 bytes. The PICK half of four
 * consecutive records in the second {pick, action, kind, flags} table, whose
 * ACTION is 0x00458D70 in all four.
 *
 * "Can the pointer do this to that object": refuse a null one, refuse one whose
 * OBJ_OFF_ARMY is not ours, find our leader, then two arms.
 *
 * A VEHICLE WITH A FREE SEAT SHOWS A HINT AND STILL ANSWERS 0. Type 3,
 * OBJ_OFF_FIELD_94 clear, and OBJ_OFF_POSE_PENDING < VEHICLE_OFF_SEATS -- which
 * is the same pair EnterVehicle refuses on, seats used against seats, and is
 * what settles that reading rather than one comparison settling it. It calls
 * OverlayPrepare and stores the uid, and then returns 0 like every refusal. So
 * the vehicle case is a hover hint, never a yes; a reader who takes the
 * hint-setting as success gets it backwards.
 *
 * A TROOPER within ApproxDist of the leader shows a different overlay row and
 * answers 1. Everything else answers 0.
 *
 * THE HINT IS SUPPRESSED BY A HELD BUTTON. With ADDR_MOUSE_BUTTON set, the
 * overlay is skipped unless GetTickCount() less ADDR_MOUSE_PRESS_MS is under
 * AM2_CLICK_MS -- so a click-and-hold stops re-arming it, which is the same
 * 500 ms click-versus-drag window the rest of the image uses.
 *
 * THE REACH IT COMPARES AGAINST READS ZERO in the image and has no direct
 * writer, so on the face of it the trooper arm passes only at zero distance.
 * That is not established -- see ADDR_PICK_REACH_66275C for why a write through
 * a base pointer would be invisible to the scan that says so.
 *
 * NOT EXERCISED: nothing in this band runs except mode 0's action, and the
 * table this belongs to has no identified consumer yet. Verified by reading. */
int32_t __cdecl PointerPickBoard(void *obj)
{
    uint8_t *o = (uint8_t *)obj;
    uint8_t *leader;
    int32_t  type;

    if (!o)
        return 0;
    if (*(const int8_t *)(o + OBJ_OFF_ARMY)
        != *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)
        return 0;

    leader = OurLeaderUnit();
    if (!leader)
        return 0;

    type = *(const int32_t *)o;

    if (type == AM2_OBJ_TYPE_VEHICLE
        && !*(const int32_t *)(o + OBJ_OFF_FIELD_94)
        && *(const int32_t *)(o + OBJ_OFF_POSE_PENDING)
               < *(const int32_t *)(o + VEHICLE_OFF_SEATS)) {
        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
            || GetTickCount()
                   - *(const uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS_MS
                   < AM2_CLICK_MS) {
            OverlayPrepare(AM2_OVERLAY_ROW_BOARD, 1);
            *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID =
                *(const uint32_t *)(o + OBJ_OFF_UID);
        }
        return 0;   /* a hint, not a yes -- see above */
    }

    if (type != AM2_OBJ_TYPE_TROOPER)
        return 0;

    if (ApproxDist((const AM2_Point *)(leader + OBJ_OFF_X),
                   (const AM2_Point *)(o + OBJ_OFF_X))
        > *(const int32_t *)(uintptr_t)ADDR_PICK_REACH_66275C)
        return 0;

    OverlayPrepare(AM2_OVERLAY_ROW_REACH, 1);
    return 1;
}

/* PointerSelect -- original 0x00458ED0, sixteen bytes, and its only reference
 * is the ACTION slot of pointer mode 0. Drop the point and hand the object to
 * SelectIfOwn, which item.cpp already has.
 *
 * MODE 0 IS THE DEFAULT AND THIS ONE ACTUALLY RUNS, which is measured rather
 * than argued from the table. Three clicks on Sarge in a live Boot Camp
 * mission give `PointerSelect=3` -- one per click -- so this is what happens
 * when the player selects a unit, and it is the only member of this family
 * with a counter that moves. SelectIfOwn reads 0 beside it, because this now
 * calls it by name; that zero is the blind spot and is itself confirmation the
 * chain is ours.
 *
 * THE A/B DOES NOT COMPARE IT, though, and that is worth separating from the
 * liveness. `bootcamp` and `campaign` both stop at a dialog and neither clicks
 * a unit, so a clean run of those says only that nothing else regressed. What
 * covers this is the three-call measurement plus reading; comparing it would
 * need a configuration that clicks on the map.
 *
 * The original is a thunk and is written as one. It pushes only the first
 * argument, which is what says the second is dropped rather than passed on. */
void __cdecl PointerSelect(void *obj, uint32_t at)
{
    (void)at;
    SelectIfOwn(obj);
}

/* PointerDropItem -- original 0x00458A20, 144 bytes, and its only reference in
 * the image is the ACTION slot of pointer mode 3 in the table SetPointerMode
 * below indexes. Drop whatever our leader is holding.
 *
 * MODE 3 IS ONE OF THE FIRE-ONCE MODES SetPointerMode's comment names but could
 * not identify: its record is pick = 0, f14 = 0, action = this, which is
 * exactly the guard down there. So `SetPointerMode(3)` never becomes a cursor
 * state at all -- it runs this immediately with a null object and the zero
 * point, and puts the mode back to 0.
 *
 * WHICH MAKES THE LAST LINE HERE REDUNDANT, and it is reproduced anyway. This
 * function ends by calling SetPointerMode(0) itself, which the caller has
 * already arranged to do. Two independent resets, neither of which can observe
 * the other; written as found.
 *
 * BOTH ARGUMENTS ARE IGNORED. The original reads no stack slot at all, which
 * is what says the signature is the action slot's rather than something with a
 * shape of its own.
 *
 * THE SEARCH LOOP CANNOT RUN, AND IT IS NOT A BINARY PATCH. The function opens
 * by loading ADDR_DEFAULT_OWNER and then comparing it against itself --
 * `3b c0`, two bytes, so nothing was overwritten in place the way
 * docs/binarypatches.md's six were. The source compared a local against the
 * global it had just been assigned from and VC6 folded the second load, so the
 * `jne` is never taken and everything behind it is dead: a walk of that army's
 * ADDR_ARMY_OBJ_LISTS entry looking for the first live type-2 object with
 * OBJ_OFF_SARGE set. It is transcribed, because "the original has a fallback
 * that cannot run" is a different fact from "the original has none" -- the same
 * standing as ObjBlockWeight's vacuous range guard.
 *
 * Written as a local compared against the global rather than as `x != x`, which
 * is both what the source must have said and the only spelling a compiler will
 * take without a tautological-compare warning.
 *
 * AND THE DEAD ARM IS WHERE THE NULL CHECK IS. Its exit sets the object to NULL
 * and falls into the inventory read, so the original would dereference NULL
 * there -- but only on the path that cannot be reached. On the live path the
 * uid lookup's answer is dereferenced with no check at all, so a leader that
 * has just died takes the game down. Reproduced; it is the original's.
 *
 * NOT EXERCISED. SetPointerMode's own note records that a driven Boot Camp
 * mission reaches it exactly once, for mode 0 at mission start, and that every
 * mode above 0 needs the order-giving UI no drive here reaches. So this is
 * verified by reading. */
void __cdecl PointerDropItem(void *obj, uint32_t at)
{
    int32_t  owner = *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER;
    uint8_t *unit;
    int32_t  slot;

    (void)obj;
    (void)at;

    if (owner != *(const int32_t *)(uintptr_t)ADDR_DEFAULT_OWNER) {
        /* Dead -- see above. */
        const uint8_t *list;
        int32_t        i;

        unit = (uint8_t *)0;

        if (owner >= 0 && owner < AM2_COMM_SLOTS) {
            list = (const uint8_t *)
                ((void *const *)(uintptr_t)ADDR_ARMY_OBJ_LISTS)[owner];

            for (i = 0; i < *(const int32_t *)(list + LIST_OFF_COUNT); i++) {
                uint8_t *o = (uint8_t *)LookupByUID(
                    (*(const uint32_t *const *)(list + LIST_OFF_UIDS))[i]);

                if (o && *(const int32_t *)o == AM2_OBJ_TYPE_TROOPER
                    && *(const int32_t *)(o + OBJ_OFF_SARGE)) {
                    unit = o;
                    break;
                }
                /* The list pointer is re-read every turn, as it is in
                 * ArmyMessageFlush over the same table. */
                list = (const uint8_t *)
                    ((void *const *)(uintptr_t)ADDR_ARMY_OBJ_LISTS)[owner];
            }
        }
    } else {
        unit = (uint8_t *)LookupByUID(
            *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID);
    }

    slot = *(const int32_t *)(unit + UNIT_OFF_INVENTORY_SEL);
    if (slot > 0) {
        TrooperDropItem(unit, slot, *(const uint32_t *)(unit + OBJ_OFF_X));
        SetPointerMode(0);
    }
}

/* 0x00414430, ten callers. Put the pointer into one of seven modes: store the
 * index, then copy five fields out of that mode's 40-byte record into five
 * fixed globals.
 *
 * "Pointer mode" is a role name and it is OURS -- nothing in the image says
 * it. What grounds it is the three readers of what this installs, none of
 * which is in this function. ADDR_POINTER_PICK is called once per object
 * while walking OBJ_OFF_QUERY_NEXT, so it decides what may be picked;
 * ADDR_POINTER_ACTION is called with (object, point) only when
 * ADDR_MOUSE_BUTTON is clear, so it is what a release does; and
 * ADDR_POINTER_OVERLAY is handed straight to OverlayPrepare. Three different
 * users of three different fields, which is better than any one of them.
 *
 * THE FIVE STORES ARE NOT IN RECORD ORDER. Globals E0, E4, E8, EC, F4 take
 * record fields +0, +4, +0x10, +0x14, +0x0C -- so the overlay, which is the
 * LAST global written, comes from the field in the MIDDLE. Reading the stores
 * top to bottom and numbering as you go puts the overlay index into the wrong
 * global and swaps the two fields nothing here reads. This is the same trap
 * the weapon handler slots carry, and orig.h already had to record it once.
 *
 * TWO OF THE SEVEN MODES FIRE ONCE AND REVERT, which is the whole of the tail.
 * When a record has no pick function and no F14 but does have an action, the
 * action runs immediately with (NULL, ADDR_ZERO_POINT) and the mode goes back
 * to 0. Records 1 and 3 are shaped that way; record 2 has neither, so it
 * installs nothing callable and stays; 0, 4, 5 and 6 have a pick function and
 * persist. So "set the mode" and "do the thing now" are the same entry point,
 * and which one happens is a property of the TABLE rather than of the caller.
 *
 * The table is seven records because the eighth slot is where the string
 * "Rifleman" begins -- that is what bounds it, not a terminator.
 *
 * The zero it passes as the point is read from ADDR_ZERO_POINT rather than
 * written as an immediate, which is what the original does and what item.cpp
 * already reproduces elsewhere for the same global.
 *
 * MEASURED AT ONE CALL, and the coverage that buys is worth stating rather
 * than rounding up. A driven Boot Camp mission -- through both dialogs, then
 * movement and clicks on the map and on the COMMANDS panel -- reaches this
 * exactly once, which is mode 0 at mission start. So the A/B compares the
 * index store, the five copies and the guard NOT firing, and nothing else.
 * The two fire-once modes, the non-sequential store order, and every mode
 * above 0 are verified by reading. Reaching the rest needs the order-giving UI
 * that the callers at 0x00427B53, 0x00427B61 and 0x00427B6F sit behind, and no
 * drive here gets there.
 */
void __cdecl SetPointerMode(int32_t mode)
{
    const uint8_t *rec = (const uint8_t *)AM2_IMAGE(ADDR_POINTER_MODES)
                         + (size_t)mode * AM2_POINTER_MODE_SIZE;
    AM2_PointerPickFn   pick;
    AM2_PointerActionFn act;
    int32_t             f14;

    *(int32_t *)(uintptr_t)ADDR_POINTER_MODE = mode;

    pick = *(AM2_PointerPickFn const *)(rec + MODE_OFF_PICK);
    act  = *(AM2_PointerActionFn const *)(rec + MODE_OFF_ACTION);
    f14  = *(const int32_t *)(rec + MODE_OFF_F14);

    *(AM2_PointerPickFn *)(uintptr_t)ADDR_POINTER_PICK    = pick;
    *(AM2_PointerActionFn *)(uintptr_t)ADDR_POINTER_ACTION = act;
    *(int32_t *)(uintptr_t)ADDR_POINTER_F10 =
        *(const int32_t *)(rec + MODE_OFF_F10);
    *(int32_t *)(uintptr_t)ADDR_POINTER_F14 = f14;
    *(int32_t *)(uintptr_t)ADDR_POINTER_OVERLAY =
        *(const int32_t *)(rec + MODE_OFF_OVERLAY);

    if (!pick && !f14 && act) {
        act((void *)0, *(const uint32_t *)(uintptr_t)ADDR_ZERO_POINT);
        *(int32_t *)(uintptr_t)ADDR_POINTER_MODE = 0;
    }
}

/* 0x00413A30, four callers. Repaint one HUD widget if it has been marked, and
 * unmark it.
 *
 * THE FLAG IS CLEARED BEFORE THE PAINT, not after. A painter that marked the
 * HUD again would therefore keep its mark, where clearing afterwards would
 * lose it -- and with four callers, at least one of them is in a frame loop.
 * Order reproduced; nothing read so far says a painter does that.
 *
 * The widget is `table[index]`, and the three globals involved are tied
 * together by this function and nothing else -- see ADDR_HUD_REPAINT_ONE in
 * orig.h for what is and is not established about them.
 *
 * The byte at HUDWIDGET_OFF_FLAG70 is cleared too. The base AM2_Widget has
 * nothing named at that offset and the subclasses that use it hold an int32
 * there; this writes one BYTE and only ever zero, so it is not those.
 *
 * The rect goes into the paint slot BY VALUE, which is what
 * WidgetRepaintSelf's call does too. */
void __cdecl HudRepaintOne(void)
{
    AM2_Widget *w;

    if (*(const int32_t *)(uintptr_t)ADDR_HUD_DIRTY == 0)
        return;

    *(int32_t *)(uintptr_t)ADDR_HUD_DIRTY = 0;

    w = ((AM2_Widget *const *)(uintptr_t)ADDR_HUD_WIDGET_TABLE)
            [*(const int32_t *)(uintptr_t)ADDR_HUD_INDEX];

    *((uint8_t *)w + HUDWIDGET_OFF_FLAG70) = 0;

    ((AM2_WidgetPaintFn *)w->vtable)[WIDGET_VSLOT_PAINT](w, w->rect);
}

/* Rewrite the panel's points readout and repaint the widget that draws it --
 * the tail both arms of PlacementScreenClick share, written out twice in the
 * original and once here. The rect goes into the paint slot BY VALUE, the
 * same as HudRepaintOne's call above. */
static void HudShowPoints(void)
{
    AM2_Widget *panel = *(AM2_Widget *const *)(uintptr_t)ADDR_HUD_WIDGET_B;
    AM2_Widget *field;

    am2_sprintf((char *)panel + HUDPANEL_OFF_POINTS_TEXT,
                (const char *)AM2_IMAGE(ADDR_FMT_INT),
                *(const int32_t *)(uintptr_t)ADDR_OUR_POINTS);

    field = *(AM2_Widget **)((uint8_t *)panel + HUDPANEL_OFF_POINTS_FIELD);

    ((AM2_WidgetPaintFn *)field->vtable)[WIDGET_VSLOT_PAINT](field,
                                                             field->rect);
}

/* PlacementScreenClick -- original 0x00413BC0, one caller. The manual
 * placement screen's click handler, and the layer directly above the three
 * functions place.cpp already holds: IsPlacedUnit, PlacementAllowed and
 * RefundPlacedUnit are each reached from here and from nowhere else in the
 * image.
 *
 * IT HAS TWO MODES AND ADDR_HUD_DIRTY CHOOSES BETWEEN THEM. Set, the player
 * is BUYING and the click tries to put the selected unit down; clear, the
 * click is a SELL and takes whatever is under the pointer.
 *
 * ITS ARGUMENT IS NEVER READ, AND THE SLOT IS THE POINT. The caller pushes
 * one dword; the first thing this does is overwrite that slot with the world
 * position of the cursor -- ADDR_CURSOR_POINT plus the view origin -- and
 * every use below reads it back from there. The x is computed as a full-dword
 * add and then truncated to sixteen bits, so a carry out of x is discarded
 * rather than reaching y. Written the same way; the low sixteen bits of a sum
 * depend on nothing above them, so the dword add is not doing any work, but
 * reproducing the shape costs nothing either.
 *
 * THE FACING IS THIS FUNCTION'S OWN. ADDR_PLACE_FACING has no other reader or
 * writer anywhere in the image: the left and right action keys rotate it by
 * AM2_PLACE_FACING_STEP and it goes on to both PlacementAllowed and
 * MakePlacedUnit. It is a BYTE, so it wraps without a test.
 *
 * BUYING ASKS ONCE AND TELLS THE CURSOR EITHER WAY. PlacementAllowed decides,
 * and its answer goes straight to ADDR_PLACE_CURSOR_PREPARE as a 1 or a 0 --
 * so the cursor is told about a refusal as well as an acceptance. Only after
 * that does the mouse come into it.
 *
 * AND THE FOURTH ARGUMENT OF THAT CALL IS EASY TO LOSE. The army from
 * CommArmyOfSlot is pushed at 0x00413CA5, one instruction BEFORE the branch,
 * so it belongs to both arms of the call below -- a push on the common path
 * that reads like a leftover until the `add esp, 0x10` underneath it is
 * counted. Note the army is fetched even on the path that returns without
 * making the call at all.
 *
 * BUYING HAPPENS ON RELEASE AND SELLING ON PRESS. Both arms test the same two
 * globals and they test them in opposite senses: buying needs the button NOT
 * down and changed, selling needs it down and changed. Neither half is
 * remarkable alone and the pair is only visible with both in view.
 *
 * THE SELL ARM'S SHAPE IS THE BUY ARM'S INSIDE OUT: hit test, then a
 * predicate, then tell the cursor what was found -- OverlayPrepare with
 * AM2_OVERLAY_ROW_SELL or with row 0 -- then the mouse, then the transaction.
 *
 * BOTH ARMS END BY REPRINTING THE POINTS AND REPAINTING ONE WIDGET, and the
 * buy arm repaints the whole HUD for two more reasons on top: running out of
 * points for the type still selected, and ADDR_PLACE_FLAG_4FCF88 being
 * clear. */
void __cdecl PlacementScreenClick(uint32_t at)
{
    void   *obj;
    int32_t army;
    int32_t type;
    int32_t ok;

    /* The argument's own slot becomes the point -- see above. */
    ((int16_t *)&at)[0] =
        (int16_t)(*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X
                  + (int32_t)*(const uint32_t *)(uintptr_t)ADDR_CURSOR_POINT);
    ((int16_t *)&at)[1] =
        (int16_t)(*(const int16_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y
                  + *(const int16_t *)(uintptr_t)(ADDR_CURSOR_POINT + 2));

    obj = ObjectsHitByPoint(&at, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC);

    if (!*(const int32_t *)(uintptr_t)ADDR_HUD_DIRTY) {
        /* ---- selling ---- */
        if (!obj
            || !IsPlacedUnit(obj,
                             (int32_t)*(const uint32_t *)(uintptr_t)
                                 ADDR_DEFAULT_OWNER)) {
            OverlayPrepare(0, 0);
            return;
        }

        OverlayPrepare(AM2_OVERLAY_ROW_SELL, 0);

        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
            return;
        if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
            return;

        RefundPlacedUnit(obj,
                         (int32_t)*(const uint32_t *)(uintptr_t)
                             ADDR_DEFAULT_OWNER,
                         (int32_t *)(uintptr_t)ADDR_OUR_POINTS);

        HudShowPoints();
        return;
    }

    /* ---- buying ---- */
    if (ActionKeyDown(AM2_ACTION_LEFT))
        *(uint8_t *)(uintptr_t)ADDR_PLACE_FACING += AM2_PLACE_FACING_STEP;
    else if (ActionKeyDown(AM2_ACTION_RIGHT))
        *(uint8_t *)(uintptr_t)ADDR_PLACE_FACING -= AM2_PLACE_FACING_STEP;

    TakeNumberKey();

    army = CommArmyOfSlot(*(void **)(uintptr_t)ADDR_COMM_OBJECT,
                          (int32_t)*(const uint32_t *)(uintptr_t)
                              ADDR_DEFAULT_OWNER);

    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON1
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED1) {
        HudRepaintOne();
        return;
    }

    type = *(const int32_t *)((const uint8_t *)AM2_IMAGE(ADDR_BUILD_MENU)
                              + (uintptr_t)AM2_BUILD_MENU_STRIDE
                                    * (uintptr_t)*(const int32_t *)(uintptr_t)
                                          ADDR_HUD_INDEX
                              + BUILD_MENU_OFF_ID);

    ok = PlacementAllowed(at, type,
                          (int32_t)*(const uint32_t *)(uintptr_t)
                              ADDR_DEFAULT_OWNER,
                          *(const int32_t *)(uintptr_t)ADDR_OUR_POINTS,
                          *(const uint8_t *)(uintptr_t)ADDR_PLACE_FACING);

    PlaceCursorPrepare(*(const int32_t *)(uintptr_t)ADDR_HUD_INDEX
                          + AM2_PLACE_CURSOR_ROW_BASE,
                      ok ? 1 : 0,
                      *(const uint8_t *)(uintptr_t)ADDR_PLACE_FACING, army);

    if (!ok)
        return;

    /* Down here the button must be UP -- see the note above. */
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
        return;
    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
        return;

    orig_make_placed(at, type,
                     (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                     (int32_t *)(uintptr_t)ADDR_OUR_POINTS,
                     *(const uint8_t *)(uintptr_t)ADDR_PLACE_FACING,
                     *(const int32_t *)(uintptr_t)ADDR_NUMBER_KEY_SLOT,
                     (const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    HudShowPoints();

    if (*(const int32_t *)(uintptr_t)ADDR_OUR_POINTS
        < (int32_t)UnitTypeCost(type))
        HudRepaintOne();

    if (!*(const int32_t *)(uintptr_t)ADDR_PLACE_FLAG_4FCF88)
        HudRepaintOne();
}

/* The CRT's own atoi, reached by address: the port replaces the CRT with libc
 * wholesale and this is one narrow seam, the same standing src/game/crt.h's
 * allocator trio has. */
typedef int32_t (__cdecl *AM2_AtoiFn)(const char *);
#define orig_atoi ((AM2_AtoiFn)(uintptr_t)ADDR_CRT_ATOI)

/* MpCommitScore and MpCommitPoints -- original 0x004322B0 and 0x004322E0, one
 * caller each and both installed as an edit field's handler by `push imm32`.
 * The lobby's two typed numbers.
 *
 * THEY ARE THE SAME FUNCTION TWICE, forty-eight and sixty-four bytes: read the
 * edit child's text, `atoi` it, store it, broadcast with SendPlayerMsg. The
 * only difference is where the number lands -- ADDR_SCORE_LIMIT for one, and
 * ADDR_ARMY_POINTS indexed by the widget's own row for the other -- which is
 * why the original wrote them out rather than passing a destination.
 *
 * BOTH ARE GATED ON COMM_OFF_IS_HOST and neither says so to the user: a client
 * can type in the box and nothing at all happens, not even a repaint. That is
 * the original's, and it is the same host-only rule the four row buttons
 * beside them follow.
 *
 * orig.h already recorded that "0x00431E10 sets ADDR_ARMY_POINTS from a lobby
 * field through atoi". This is that field; the entry at 0x00431E10 is the
 * panel holding it. */
void __cdecl MpCommitScore(AM2_Widget *w)
{
    const uint8_t *comm = (const uint8_t *)g_commObject;
    const AM2_Widget *edit;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    edit = *(const AM2_Widget *const *)((const uint8_t *)w + MPFIELD_OFF_EDIT);
    *(int32_t *)(uintptr_t)ADDR_SCORE_LIMIT =
        orig_atoi(*(const char *const *)((const uint8_t *)edit
                                         + EDIT_OFF_TEXT));
    SendPlayerMsg(0);
}

void __cdecl MpCommitPoints(AM2_Widget *w)
{
    const uint8_t *comm = (const uint8_t *)g_commObject;
    const AM2_Widget *edit;
    int32_t           row;

    if (!*(const int32_t *)(comm + COMM_OFF_IS_HOST))
        return;

    edit = *(const AM2_Widget *const *)((const uint8_t *)w + MPFIELD_OFF_EDIT);
    /* The row is read AFTER the atoi, which matters not at all -- but the
     * original reads it there and nothing between the two touches it. */
    row  = *(const int32_t *)((const uint8_t *)w + MPFIELD_OFF_ROW);

    ((int32_t *)(uintptr_t)ADDR_ARMY_POINTS)[row] =
        orig_atoi(*(const char *const *)((const uint8_t *)edit
                                         + EDIT_OFF_TEXT));
    SendPlayerMsg(0);
}

/* SpinCommit, SpinUp and SpinDown -- original 0x00456580, 0x004565D0 and
 * 0x00456660, one caller each: the constructor two hundred bytes above, which
 * installs all three by `push imm32` alongside the spinner they belong to.
 *
 * A SPINNER IS AN EDIT BOX AND TWO ARROWS, and these are its three ways in. All
 * three read the same five fields off the spinner, which is what makes them one
 * class rather than three functions; the differences are worth stating because
 * a merged version would lose every one of them:
 *
 *   the arrows clamp against ONE end each, up against the maximum and down
 *   against the minimum, and neither clamps the other way -- up cannot fall
 *   below the minimum only because it was already at or above it;
 *
 *   the COMMIT clamps both ways, because it is the only one a user can hand an
 *   arbitrary number to;
 *
 *   and only the arrows repaint and play a sound. A typed value that gets
 *   clamped therefore keeps showing what was typed until something else
 *   repaints the box. Reproduced.
 *
 * The children keep the spinner at different offsets -- the arrows at 0x78 and
 * the edit at 0x7C -- which is read off the three bodies and not from the
 * constructor.
 */
static void SpinApply(AM2_Widget *spin, int32_t value)
{
    char *text = *(char *const *)((uint8_t *)
                     *(AM2_Widget *const *)((uint8_t *)spin + SPIN_OFF_EDIT)
                     + EDIT_OFF_TEXT);
    void (__cdecl *fn)(AM2_Widget *);

    orig_sprintf(text, (const char *)AM2_IMAGE(ADDR_FMT_INT), value);

    fn = *(void (__cdecl **)(AM2_Widget *))((uint8_t *)spin + SPIN_OFF_HANDLER);
    if (fn)
        fn(spin);
}

static void SpinRepaint(AM2_Widget *spin)
{
    AM2_Widget *edit =
        *(AM2_Widget *const *)((uint8_t *)spin + SPIN_OFF_EDIT);

    ((AM2_WidgetPaintFn *)edit->vtable)[WIDGET_VSLOT_PAINT](edit, edit->rect);
    PlaySoundAt(0, 0, 0, 0, 0);
}

void __cdecl SpinCommit(AM2_Widget *w)
{
    AM2_Widget *spin =
        *(AM2_Widget *const *)((uint8_t *)w + SPINCHILD_OFF_SPIN_EDIT);
    AM2_Widget *edit =
        *(AM2_Widget *const *)((uint8_t *)spin + SPIN_OFF_EDIT);
    int32_t     v = orig_atoi(*(const char *const *)((const uint8_t *)edit
                                                     + EDIT_OFF_TEXT));
    int32_t     hi = *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_MAX);
    int32_t     lo = *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_MIN);

    if (v > hi)
        v = hi;
    else if (v < lo)
        v = lo;

    SpinApply(spin, v);
}

void __cdecl SpinUp(AM2_Widget *w)
{
    AM2_Widget *spin =
        *(AM2_Widget *const *)((uint8_t *)w + SPINCHILD_OFF_SPIN_ARROW);
    AM2_Widget *edit =
        *(AM2_Widget *const *)((uint8_t *)spin + SPIN_OFF_EDIT);
    int32_t     v = orig_atoi(*(const char *const *)((const uint8_t *)edit
                                                     + EDIT_OFF_TEXT))
                  + *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_STEP);
    int32_t     hi = *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_MAX);

    if (v > hi)
        v = hi;

    SpinApply(spin, v);
    SpinRepaint(spin);
}

void __cdecl SpinDown(AM2_Widget *w)
{
    AM2_Widget *spin =
        *(AM2_Widget *const *)((uint8_t *)w + SPINCHILD_OFF_SPIN_ARROW);
    AM2_Widget *edit =
        *(AM2_Widget *const *)((uint8_t *)spin + SPIN_OFF_EDIT);
    int32_t     v = orig_atoi(*(const char *const *)((const uint8_t *)edit
                                                     + EDIT_OFF_TEXT))
                  - *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_STEP);
    int32_t     lo = *(const int32_t *)((const uint8_t *)spin + SPIN_OFF_MIN);

    if (v < lo)
        v = lo;

    SpinApply(spin, v);
    SpinRepaint(spin);
}

/* The idle tail, which the original writes out TWICE -- 0x004142E4 and
 * 0x00414334 are the same eight instructions, the compiler duplicating a tail
 * across a guard rather than two behaviours that happen to agree. Diffed
 * before collapsing, per the rule about near-identical bodies: they match
 * instruction for instruction, so the helper loses nothing.
 *
 * `ours` is the flag the caller computed: SLOT2 is an overlay row and it is
 * only consulted for something we control. It is read with a SIGNED test --
 * `test eax,eax; jl` -- which is what says it is a row index and not a
 * function pointer, and it defaults to -1. */
static void PointerIdleOverlay(int32_t ours)
{
    int32_t row = *(const int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT2;

    if (ours && row >= 0) {
        OverlayPrepare(row, 0);
        return;
    }
    OverlayPrepare(*(const int32_t *)(uintptr_t)ADDR_POINTER_OVERLAY, 0);
}

/* 0x00413E70, 1,280 bytes, one caller: the per-frame mouse dispatch, and the
 * function the whole pointer band below is actually reached from. It refuses
 * while a text field owns the keyboard or input is suppressed, clears the
 * hover uid, runs the selection click, turns the cursor into a world point,
 * maintains the drag rectangle, takes two ActionKeyDown arms, and then fans
 * out through six function-pointer slots.
 *
 * THE FAN-OUT WALKS A CHAIN, not a single object. ObjectsHitByPoint answers a
 * list threaded through OBJ_OFF_QUERY_NEXT and each node is tried in turn:
 * SLOT0 while `ours` is set, then SLOT3 as a plain FLAG that says "stop
 * looking at this node", then ADDR_POINTER_PICK -- the last only while
 * `aiming` is clear. The first slot to answer non-zero ends the walk AND
 * decides which trailing arm runs; falling off the end runs a third.
 *
 * ONE PREDICATE, TWO WINDOWS. Every arm asks the mouse the same two questions
 * -- did the button CHANGE this frame, and is it down -- and they are not the
 * same question in different arms. A changed-and-up button is a click and
 * calls the action slot; a changed-and-down button CLAIMS the mouse and calls
 * nothing; an unchanged button reaches the action slot only when the kind
 * record allows repeating, which is KINDREC_OFF_FIELD_24 and is the whole
 * reason that field had to be read.
 *
 * THE ACTION SLOT IS CALLED WITH A NULL OBJECT on the no-hit arms and with the
 * chain node on the hit arms -- same slot, same point, different first
 * argument -- so a reading that collapses them loses which one the pointer
 * found something under.
 *
 * The debug-explosion arm at the top is real code and cannot run: it is gated
 * on ADDR_OPT_4FD748, which ships as 0 and has no writer. Reproduced anyway,
 * because it is the original's behaviour and because ADDR_MOUSE_PRESS2_MS's
 * 200 ms window is the only place that constant appears. */
void __cdecl HudPostUpdate(void)
{
    uint32_t at;
    void    *leader;
    void    *obj;
    void    *weapon;
    int32_t  ours   = 0;   /* the context object is ours to command */
    int32_t  aiming = 0;   /* an aim overlay was prepared this frame */
    int32_t  repeat = 0;   /* the held weapon keeps acting while held */
    AM2_PointerPickFn   pick;
    AM2_PointerActionFn act;

    if (*(void *const *)(uintptr_t)ADDR_CHAR_HANDLER)
        return;
    if (*(const int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)
        return;

    *(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID = 0;
    SelectionClick();

    /* Cursor to world, exactly as PlacementScreenClick builds it. The x add is
     * a full 32-bit add of the packed dword with only the low half stored, so
     * a carry out of x is discarded; written as int16 arithmetic, which is
     * what that amounts to. */
    ((int16_t *)&at)[0] =
        (int16_t)(*(const int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X
                  + (int32_t)*(const uint32_t *)(uintptr_t)ADDR_CURSOR_POINT);
    ((int16_t *)&at)[1] =
        (int16_t)(*(const int16_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y
                  + *(const int16_t *)(uintptr_t)(ADDR_CURSOR_POINT + 2));

    /* The test is on the CURSOR against the view rectangle, not on the world
     * point just built -- ADDR_BLIT_RECT is in screen space. */
    if (!PointInRect((const AM2_Rect *)(uintptr_t)ADDR_BLIT_RECT,
                     (const AM2_Point *)(uintptr_t)ADDR_CURSOR_POINT)) {
        if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
            return;
        OverlayPrepare(0, 0);
        return;
    }

    if (*(const int32_t *)(uintptr_t)ADDR_NET_GAME) {
        PlacementScreenClick(at);
        return;
    }

    /* Take the grab the first frame the button moves, and only while nothing
     * else holds it. -1 rather than a widget: this layer is not a widget. */
    if (!*(void *const *)(uintptr_t)ADDR_MOUSE_GRAB
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
        *(int32_t *)(uintptr_t)ADDR_MOUSE_GRAB = -1;

    if (*(const int32_t *)(uintptr_t)ADDR_OPT_4FD748
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON1
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED1
        && orig_get_tick_count() - *(const uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS2_MS
               < AM2_DOUBLE_CLICK_MS)
        CreateExplosion(((const int16_t *)&at)[0], ((const int16_t *)&at)[1],
                        *(const int32_t *)(uintptr_t)ADDR_DEBUG_BLAST_KIND,
                        (int32_t)*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER,
                        *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID,
                        0xF, 0, 0, 0, 0);

    /* ---- the drag rectangle ------------------------------------------- */
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON1) {
        if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED1)
            *(uint32_t *)(uintptr_t)ADDR_DRAG_ANCHOR = at;

        /* Six is the dead zone: a drag shorter than that is a click, and the
         * rectangle is not published until it is crossed. Once ON it is not
         * recomputed here -- the test is on the flag, so the corners are the
         * ones the crossing frame wrote. */
        if (!*(const int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON
            && ApproxDist((const AM2_Point *)&at,
                          (const AM2_Point *)(uintptr_t)ADDR_DRAG_ANCHOR)
                   > AM2_DRAG_DEAD_ZONE) {
            int16_t ax = ((const int16_t *)(uintptr_t)ADDR_DRAG_ANCHOR)[0];
            int16_t ay = ((const int16_t *)(uintptr_t)ADDR_DRAG_ANCHOR)[1];
            int16_t px = ((const int16_t *)&at)[0];
            int16_t py = ((const int16_t *)&at)[1];

            *(int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON = 1;
            /* Signed 16-bit compares, sign-extended into the rect's int32s. */
            ((int32_t *)(uintptr_t)ADDR_VIEW_RECT)[0] = ax < px ? ax : px;
            ((int32_t *)(uintptr_t)ADDR_VIEW_RECT)[2] = ax > px ? ax : px;
            ((int32_t *)(uintptr_t)ADDR_VIEW_RECT)[1] = ay < py ? ay : py;
            ((int32_t *)(uintptr_t)ADDR_VIEW_RECT)[3] = ay > py ? ay : py;
        }
    }

    /* ---- the two key arms --------------------------------------------- */
    if (ActionKeyDown(AM2_ACTION_0A) || ActionKeyDown(AM2_ACTION_06)) {
        void *ctx = *(void *const *)(uintptr_t)ADDR_OBJ_CTX_OBJ_A;

        /* Everything AIMS except a kind-3 vehicle, which has its own gunner
         * handling. Note the refusal needs BOTH tests: a non-vehicle reaches
         * the arm through the first `je`, not past it. */
        if (!ObjIsType3((const AM2_Object *)ctx)
            || *(const int32_t *)((const uint8_t *)
                   *(void *const *)(uintptr_t)ADDR_OBJ_CTX_OBJ_A
                   + VEHICLE_OFF_KIND) != 3) {
            OverlayPrepare(AM2_OVERLAY_ROW_FIRE, 1);
            aiming = 1;
        }
    }
    if (ActionKeyDown(AM2_ACTION_EXIT_VEHICLE))
        VehicleDismountAll((void *)0, (void *)(uintptr_t)at);

    /* ---- what the leader is holding ----------------------------------- */
    leader = LookupOwnerObj(*(const uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER);
    if (leader) {
        int32_t sel = *(const int32_t *)((const uint8_t *)leader
                                         + UNIT_OFF_INVENTORY_SEL);

        weapon = WeaponByUid(*(const uint32_t *)((const uint8_t *)leader
                                                 + UNIT_OFF_INVENTORY
                                                 + (uint32_t)sel * 4));
        if (weapon) {
            /* obj -> type record -> kind, then the kind's own record. Three
             * loads, not two: OBJ_OFF_FIELD_C0 is a POINTER to the type. */
            const uint8_t *type =
                *(const uint8_t *const *)((const uint8_t *)weapon
                                          + OBJ_OFF_FIELD_C0);
            int32_t kind = *(const int32_t *)(type + ITEMTYPE_OFF_KIND);

            repeat = *(const int32_t *)((const uint8_t *)(uintptr_t)
                                            ADDR_MISSILE_DEFS
                                        + (uint32_t)kind * AM2_MISSILE_DEF_BYTES
                                        + MISSILEDEF_OFF_FIELD_24);
        }
    }

    /* We command the context object when it IS our leader, or when it is the
     * vehicle our leader is riding. Two separate tests, both writing the one
     * flag, and the second needs the leader lookup above. */
    {
        uint32_t ctx = *(const uint32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A;
        uint32_t us  = *(const uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID;

        if (us && ctx == us)
            ours = 1;
        if (leader
            && *(const uint32_t *)((const uint8_t *)leader + OBJ_OFF_RIDING)
                   == ctx)
            ours = 1;
    }

    /* ---- the fan-out --------------------------------------------------- */
    obj = ObjectsHitByPoint(&at, (const void *)(uintptr_t)ADDR_OBJ_MAP_DESC);
    for (; obj; obj = *(void *const *)((const uint8_t *)obj
                                       + OBJ_OFF_QUERY_NEXT)) {
        if (ours) {
            pick = *(AM2_PointerPickFn *)(uintptr_t)ADDR_WEAPON_FN_SLOT0;
            if (pick && pick(obj))
                goto hit_ours;
            /* A FLAG, not a function: SLOT3 set means this node is spoken for
             * and the generic pick must not see it. */
            if (*(const int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT3)
                continue;
        }
        pick = *(AM2_PointerPickFn *)(uintptr_t)ADDR_POINTER_PICK;
        if (pick && !aiming && pick(obj))
            goto hit_generic;
    }

    /* Nothing under the pointer. */
    if (ours && *(const int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT3) {
        if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
            goto click_ours;
        goto held_ours;
    }
    if (*(void *const *)(uintptr_t)ADDR_POINTER_F14 && !aiming
        && *(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
        goto click_generic;
    goto tail;

hit_ours:
    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
        goto held_ours;
click_ours:
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
        goto claim;
    act = *(AM2_PointerActionFn *)(uintptr_t)ADDR_WEAPON_FN_SLOT1;
    if (!act)
        goto claim;
    act(obj, at);
    goto tail;

held_ours:
    /* The button did not change, so this is a hold. Only a weapon whose kind
     * record allows repeating gets to act again. */
    if (!repeat)
        goto tail;
    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON) {
        PointerIdleOverlay(ours);
        return;
    }
    act = *(AM2_PointerActionFn *)(uintptr_t)ADDR_WEAPON_FN_SLOT1;
    if (act)
        act(obj, at);
    goto tail;

hit_generic:
    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)
        goto tail;
click_generic:
    if (*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)
        goto claim;
    act = *(AM2_PointerActionFn *)(uintptr_t)ADDR_POINTER_ACTION;
    if (!act)
        goto claim;
    act(obj, at);
    goto tail;

claim:
    *(int32_t *)(uintptr_t)ADDR_MOUSE_CLAIMED = 1;

tail:
    if (!*(const int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON
        && !*(const int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED) {
        PointerIdleOverlay(ours);
        return;
    }
    /* GetMenuRow is five bytes reading a global; the original calls it twice
     * and so does this. */
    if (GetMenuRow() == 3)
        return;
    if (GetMenuRow() == 1)
        return;
    PointerIdleOverlay(ours);
}

int widget_install(void)
{
    int rc = 0;

    rc |= patch_replace(ADDR_HUD_UPDATE, (const void *)HudUpdate,
                        "HudUpdate", 0);
    rc |= patch_replace(ADDR_SPIN_COMMIT, (const void *)SpinCommit,
                        "SpinCommit", 1);
    rc |= patch_replace(ADDR_SPIN_UP, (const void *)SpinUp, "SpinUp", 1);
    rc |= patch_replace(ADDR_SPIN_DOWN, (const void *)SpinDown, "SpinDown", 1);
    rc |= patch_replace(ADDR_MP_COMMIT_SCORE, (const void *)MpCommitScore,
                        "MpCommitScore", 1);
    rc |= patch_replace(ADDR_MP_COMMIT_POINTS, (const void *)MpCommitPoints,
                        "MpCommitPoints", 1);
    rc |= patch_replace(ADDR_HUD_MARKER_AGE, (const void *)AimMarkerAge,
                        "AimMarkerAge", 1);
    rc |= patch_replace(ADDR_AIM_INIT, (const void *)AimInit, "AimInit", 1);
    rc |= patch_replace(ADDR_HUD_PANEL_CONSTRUCT,
                        (const void *)HudPanelConstruct,
                        "HudPanelConstruct", 1);
    rc |= patch_replace(ADDR_HUD_SQUAD_CONSTRUCT,
                        (const void *)HudSquadConstruct,
                        "HudSquadConstruct", 1);
    rc |= patch_replace(ADDR_HUD_RADAR_CONSTRUCT,
                        (const void *)HudRadarConstruct,
                        "HudRadarConstruct", 1);
    rc |= patch_replace(ADDR_HUD_SARGE_CONSTRUCT,
                        (const void *)HudSargeConstruct,
                        "HudSargeConstruct", 1);
    rc |= patch_replace(ADDR_HUD_TOP_CONSTRUCT,
                        (const void *)HudTopConstruct,
                        "HudTopConstruct", 1);
    rc |= patch_replace(ADDR_HUD_EDGE_CONSTRUCT,
                        (const void *)HudEdgeConstruct,
                        "HudEdgeConstruct", 1);
    rc |= patch_replace(ADDR_HUD_CMD_CONSTRUCT, (const void *)HudCmdConstruct,
                        "HudCmdConstruct", 1);
    rc |= patch_replace(ADDR_FREE_AIM_SPRITES, (const void *)FreeAimSprites,
                        "FreeAimSprites", 0);
    rc |= patch_replace(ADDR_AIM_START_B, (const void *)AimStartB,
                        "AimStartB", 1);
    rc |= patch_replace(ADDR_AIM_START, (const void *)AimStart,
                        "AimStart", 1);
    rc |= patch_replace(ADDR_HUD_PAINT, (const void *)HudPaint,
                        "HudPaint", 0);

    rc |= patch_replace(ADDR_WIDGET_PAINT, (const void *)WidgetPaint,
                        "WidgetPaint", 6);
    rc |= patch_replace(ADDR_WIDGET_PAINT_FWD1,
                        (const void *)WidgetPaintFwd1, "WidgetPaintFwd1", 1);
    rc |= patch_replace(ADDR_WIDGET_PAINT_FWD2,
                        (const void *)WidgetPaintFwd2, "WidgetPaintFwd2", 18);
    rc |= patch_replace(ADDR_WIDGET_ADD_CHILD, (const void *)WidgetAddChild,
                        "WidgetAddChild", 1);
    rc |= patch_replace(ADDR_FIND_PRESSED_KEY, (const void *)FindPressedKey,
                        "FindPressedKey", 1);
    rc |= patch_replace(ADDR_KEYROW_UPDATE, (const void *)KeyRowUpdate,
                        "KeyRowUpdate", 1);
    rc |= patch_replace(ADDR_BLINKER_UPDATE, (const void *)BlinkerUpdate,
                        "BlinkerUpdate", 1);
    rc |= patch_replace(ADDR_BLINKER_START, (const void *)BlinkerStart,
                        "BlinkerStart", 1);
    rc |= patch_replace(ADDR_MULTI_SPRITE_PAINT,
                        (const void *)MultiSpritePaint,
                        "MultiSpritePaint", 1);
    rc |= patch_replace(ADDR_TOGGLE_PAINT, (const void *)TogglePaint,
                        "TogglePaint", 1);
    rc |= patch_replace(ADDR_LIST_DESTRUCT, (const void *)ListDestruct,
                        "ListDestruct", 1);
    rc |= patch_replace(ADDR_LIST_DELETE, (const void *)ListDelete,
                        "ListDelete", 1);
    rc |= patch_replace(ADDR_LIST_DRAW, (const void *)ListDraw,
                        "ListDraw", 1);
    rc |= patch_replace(ADDR_LIST_TAKE_FOCUS, (const void *)ListTakeFocus,
                        "ListTakeFocus", 1);
    rc |= patch_replace(ADDR_ICON_DESTRUCT, (const void *)IconDestruct,
                        "IconDestruct", 1);
    rc |= patch_replace(ADDR_ICON_DELETE, (const void *)IconDelete,
                        "IconDelete", 1);
    rc |= patch_replace(ADDR_BLINKER_DESTRUCT, (const void *)BlinkerDestruct,
                        "BlinkerDestruct", 1);
    rc |= patch_replace(ADDR_BLINKER_DELETE, (const void *)BlinkerDelete,
                        "BlinkerDelete", 1);
    rc |= patch_replace(ADDR_BUTTON_DESTRUCT, (const void *)ButtonDestruct,
                        "ButtonDestruct", 1);
    rc |= patch_replace(ADDR_BUTTON_DELETE, (const void *)ButtonDelete,
                        "ButtonDelete", 1);
    rc |= patch_replace(ADDR_BUTTON_UPDATE, (const void *)ButtonUpdate,
                        "ButtonUpdate", 4);
    rc |= patch_replace(ADDR_BUTTON_PAINT, (const void *)ButtonPaint,
                        "ButtonPaint", 2);
    rc |= patch_replace(ADDR_EDIT_DRAW, (const void *)EditDraw,
                        "EditDraw", 1);
    rc |= patch_replace(ADDR_EDIT_TAKE_FOCUS, (const void *)EditTakeFocus,
                        "EditTakeFocus", 1);
    rc |= patch_replace(ADDR_EDIT_REPAINT, (const void *)EditRepaint,
                        "EditRepaint", 1);
    rc |= patch_replace(ADDR_EDIT_DESTRUCT, (const void *)EditDestruct,
                        "EditDestruct", 1);
    rc |= patch_replace(ADDR_EDIT_DELETE, (const void *)EditDelete,
                        "EditDelete", 1);
    rc |= patch_replace(ADDR_EDIT_UPDATE, (const void *)EditUpdate,
                        "EditUpdate", 1);
    rc |= patch_replace(ADDR_WIDGET_DESTRUCT_THUNK,
                        (const void *)WidgetDestructThunk,
                        "WidgetDestructThunk", 1);
    rc |= patch_replace(ADDR_WIDGET_DELETE_ALT, (const void *)WidgetDeleteAlt,
                        "WidgetDeleteAlt", 3);
    rc |= patch_replace(ADDR_FOCUSLABEL_DESTRUCT,
                        (const void *)FocusLabelDestruct,
                        "FocusLabelDestruct", 1);
    rc |= patch_replace(ADDR_FOCUSLABEL_DELETE,
                        (const void *)FocusLabelDelete,
                        "FocusLabelDelete", 1);
    rc |= patch_replace(ADDR_FOCUSLABEL_DRAW, (const void *)FocusLabelDraw,
                        "FocusLabelDraw", 1);
    rc |= patch_replace(ADDR_FOCUSLABEL_TAKE_FOCUS,
                        (const void *)FocusLabelTakeFocus,
                        "FocusLabelTakeFocus", 1);
    rc |= patch_replace(ADDR_OPTIONS_DEFAULTS, (const void *)OptionsDefaults,
                        "OptionsDefaults", 1);
    rc |= patch_replace(ADDR_OPEN_MP_SELECT_MAP, (const void *)OpenSelectMap,
                        "OpenSelectMap", 0);
    rc |= patch_replace(ADDR_OPEN_SELECT_PLAYER, (const void *)OpenSelectPlayer,
                        "OpenSelectPlayer", 0);
    rc |= patch_replace(ADDR_OPEN_ENTER_NAME, (const void *)OpenEnterName,
                        "OpenEnterName", 0);
    rc |= patch_replace(ADDR_OPEN_WAR_MENU, (const void *)OpenWarMenu,
                        "OpenWarMenu", 0);
    rc |= patch_replace(ADDR_WAR_MENU_CTOR, (const void *)WarMenuConstruct,
                        "WarMenuConstruct", 1);
    rc |= patch_replace(ADDR_OPEN_BATTLE_NAME, (const void *)OpenBattleName,
                        "OpenBattleName", 0);
    rc |= patch_replace(ADDR_OPEN_BATTLE_JOIN, (const void *)OpenBattleJoin,
                        "OpenBattleJoin", 0);
    rc |= patch_replace(ADDR_OPEN_MOVIES, (const void *)OpenMovies,
                        "OpenMovies", 0);
    rc |= patch_replace(ADDR_OPEN_OPTIONS_MENU, (const void *)OpenOptionsMenu,
                        "OpenOptionsMenu", 0);
    rc |= patch_replace(ADDR_OPEN_CONTROLS, (const void *)OpenControls,
                        "OpenControls", 0);
    rc |= patch_replace(ADDR_OPEN_DIFFICULTY, (const void *)OpenDifficulty,
                        "OpenDifficulty", 0);
    rc |= patch_replace(ADDR_OPEN_QUIT_CONFIRM, (const void *)OpenQuitConfirm,
                        "OpenQuitConfirm", 0);
    rc |= patch_replace(ADDR_OPEN_REPLAY_PROMPT, (const void *)OpenReplayPrompt,
                        "OpenReplayPrompt", 0);
    rc |= patch_replace(ADDR_OPEN_DELETE_PLAYER, (const void *)OpenDeletePlayer,
                        "OpenDeletePlayer", 0);
    rc |= patch_replace(ADDR_ON_MOVIE_NEXT_PAGE,
                        (const void *)OnMovieNextPage,
                        "OnMovieNextPage", 0);
    rc |= patch_replace(ADDR_ON_MOVIE_PLAY, (const void *)OnMoviePlay,
                        "OnMoviePlay", 0);
    rc |= patch_replace(ADDR_ON_LOADGAME_LOAD, (const void *)OnLoadGameLoad,
                        "OnLoadGameLoad", 0);

    rc |= patch_replace(ADDR_REPAINT_ANCESTOR, (const void *)RepaintAncestor,
                        "RepaintAncestor", 1);
    rc |= patch_replace(ADDR_SELECT_MAP_ROW, (const void *)SelectMapRow,
                        "SelectMapRow", 0);
    rc |= patch_replace(ADDR_LOADGAME_ROW, (const void *)LoadGameRow,
                        "LoadGameRow", 0);

    rc |= patch_replace(ADDR_ON_ENTER_NAME_CANCEL,
                        (const void *)OnEnterNameCancel,
                        "OnEnterNameCancel", 0);
    rc |= patch_replace(ADDR_ON_LOADGAME_BACK, (const void *)OnLoadGameBack,
                        "OnLoadGameBack", 0);
    rc |= patch_replace(ADDR_ON_LOADGAME_DELETE,
                        (const void *)OnLoadGameDelete,
                        "OnLoadGameDelete", 0);
    rc |= patch_replace(ADDR_ON_DELGAME_CANCEL, (const void *)OnDelGameCancel,
                        "OnDelGameCancel", 0);
    rc |= patch_replace(ADDR_ON_LOADGAME_NEW, (const void *)OnLoadGameNew,
                        "OnLoadGameNew", 0);

    rc |= patch_replace(ADDR_ON_MP_COLOUR, (const void *)OnMpColour,
                        "OnMpColour", 1);
    rc |= patch_replace(ADDR_ON_MP_TEAM_LEFT, (const void *)OnMpTeamLeft,
                        "OnMpTeamLeft", 1);
    rc |= patch_replace(ADDR_ON_MP_TEAM_RIGHT, (const void *)OnMpTeamRight,
                        "OnMpTeamRight", 1);
    rc |= patch_replace(ADDR_ON_MP_NAME, (const void *)OnMpName,
                        "OnMpName", 1);
    rc |= patch_replace(ADDR_ON_CHAT_ENTER, (const void *)OnChatEnter,
                        "OnChatEnter", 0);
    rc |= patch_replace(ADDR_MP_NAME_INK, (const void *)MpNameInk,
                        "MpNameInk", 1);
    rc |= patch_replace(ADDR_MP_NAME_PAPER, (const void *)MpNamePaper,
                        "MpNamePaper", 1);
    rc |= patch_replace(ADDR_MP_NAME_SET_INK, (const void *)MpNameSetInk,
                        "MpNameSetInk", 3);
    rc |= patch_replace(ADDR_REFRESH_MAP_SEL, (const void *)RefreshMapSelection,
                        "RefreshMapSelection", 4);
    rc |= patch_replace(ADDR_SHOW_BAD_PREVIEW, (const void *)ShowBadMapPreview,
                        "ShowBadMapPreview", 1);
    rc |= patch_replace(ADDR_FILL_LIST_FROM_RULES,
                        (const void *)FillListFromRules,
                        "FillListFromRules", 3);
    rc |= patch_replace(ADDR_MP_PANEL_UPDATE, (const void *)MpPanelUpdate,
                        "MpPanelUpdate", 1);
    rc |= patch_replace(ADDR_MP_PANEL_DESTRUCT, (const void *)MpPanelDestruct,
                        "MpPanelDestruct", 1);

    rc |= patch_replace(ADDR_MP_PANEL_CTOR, (const void *)MpPanelConstruct,
                        "MpPanelConstruct", 2);
    rc |= patch_replace(ADDR_MP_NAME_CTOR, (const void *)MpNameConstruct,
                        "MpNameConstruct", 9);
    rc |= patch_replace(ADDR_MP_COLOUR_CTOR, (const void *)MpColourConstruct,
                        "MpColourConstruct", 3);
    rc |= patch_replace(ADDR_MP_TEAM_CTOR, (const void *)MpTeamConstruct,
                        "MpTeamConstruct", 3);

    rc |= patch_replace(ADDR_LOAD_GAME_CTOR, (const void *)LoadGameConstruct,
                        "LoadGameConstruct", 1);

    rc |= patch_replace(ADDR_MOVIES_CTOR, (const void *)MoviesConstruct,
                        "MoviesConstruct", 1);

    rc |= patch_replace(ADDR_DELETE_GAME_CTOR,
                        (const void *)DeleteGameConstruct,
                        "DeleteGameConstruct", 1);

    rc |= patch_replace(ADDR_ENTER_NAME_CTOR,
                        (const void *)EnterNameConstruct,
                        "EnterNameConstruct", 1);

    rc |= patch_replace(ADDR_MP_SELECT_MAP_CTOR,
                        (const void *)SelectMapConstruct,
                        "SelectMapConstruct", 1);

    rc |= patch_replace(ADDR_BUTTON_BASE_CTOR,
                        (const void *)ButtonBaseConstruct,
                        "ButtonBaseConstruct", 1);
    rc |= patch_replace(ADDR_CHECKBOX_TOGGLE, (const void *)CheckboxToggle,
                        "CheckboxToggle", 0);

    rc |= patch_replace(ADDR_EDIT_CHAR_HANDLER, (const void *)EditCharHandler,
                        "EditCharHandler", 0);

    rc |= patch_replace(ADDR_ON_APP_ACTIVATED, (const void *)OnAppActivated,
                        "OnAppActivated", 1);
    rc |= patch_replace(ADDR_CLEAR_MENU_MSGS, (const void *)ClearMenuMsgs,
                        "ClearMenuMsgs", 2);
    rc |= patch_replace(ADDR_RECORD_RESET, (const void *)RecordReset,
                        "RecordReset", 1);
    rc |= patch_replace(ADDR_SELECT_PLAYER_ROW, (const void *)SelectPlayerRow,
                        "SelectPlayerRow", 0);

    rc |= patch_replace(ADDR_ON_RECRUIT, (const void *)OnRecruit,
                        "OnRecruit", 0);
    rc |= patch_replace(ADDR_ON_DELETE_PLAYER, (const void *)OnDeletePlayer,
                        "OnDeletePlayer", 0);
    rc |= patch_replace(ADDR_ON_SELECT_PLAYER, (const void *)OnSelectPlayer,
                        "OnSelectPlayer", 0);
    rc |= patch_replace(ADDR_ON_DELPLAYER_CANCEL,
                        (const void *)OnDelPlayerCancel,
                        "OnDelPlayerCancel", 0);
    rc |= patch_replace(ADDR_ON_REPLAY_OK, (const void *)OnReplayOk,
                        "OnReplayOk", 0);

    rc |= patch_replace(ADDR_ON_ARROW_UP, (const void *)OnArrowUp,
                        "OnArrowUp", 0);
    rc |= patch_replace(ADDR_ON_ARROW_DOWN, (const void *)OnArrowDown,
                        "OnArrowDown", 0);
    rc |= patch_replace(ADDR_ARROWBAR_FOLLOW_END,
                        (const void *)ArrowBarFollowEnd,
                        "ArrowBarFollowEnd", 0);
    rc |= patch_replace(ADDR_ON_ARROW_LEFT, (const void *)OnArrowLeft,
                        "OnArrowLeft", 0);
    rc |= patch_replace(ADDR_ON_ARROW_RIGHT, (const void *)OnArrowRight,
                        "OnArrowRight", 0);

    rc |= patch_replace(ADDR_ON_CONTROLS_OK, (const void *)OnControlsOk,
                        "OnControlsOk", 0);
    rc |= patch_replace(ADDR_ON_CONTROLS_DEFAULT, (const void *)OnControlsDefault,
                        "OnControlsDefault", 0);
    rc |= patch_replace(ADDR_ON_CONTROLS_CANCEL, (const void *)OnControlsCancel,
                        "OnControlsCancel", 0);
    rc |= patch_replace(ADDR_ON_AUDIO_CANCEL, (const void *)OnAudioCancel,
                        "OnAudioCancel", 0);
    rc |= patch_replace(ADDR_APPLY_VOLUMES, (const void *)ApplyVolumes,
                        "ApplyVolumes", 0);
    rc |= patch_replace(ADDR_ON_AUDIO_OK, (const void *)OnAudioOk,
                        "OnAudioOk", 0);
    rc |= patch_replace(ADDR_ON_DIFFICULTY_OK, (const void *)OnDifficultyOk,
                        "OnDifficultyOk", 0);

    rc |= patch_replace(ADDR_ON_MENU_BACK, (const void *)OnMenuBack,
                        "OnMenuBack", 0);
    rc |= patch_replace(ADDR_ON_CONTROLS_BUTTON, (const void *)OnControlsButton,
                        "OnControlsButton", 0);
    rc |= patch_replace(ADDR_ON_DIFFICULTY_BUTTON,
                        (const void *)OnDifficultyButton,
                        "OnDifficultyButton", 0);
    rc |= patch_replace(ADDR_ON_AUDIO_BUTTON, (const void *)OnAudioButton,
                        "OnAudioButton", 0);
    rc |= patch_replace(ADDR_ON_QUIT_OK, (const void *)OnQuitOk, "OnQuitOk", 0);
    rc |= patch_replace(ADDR_ON_VOLUME_EFFECTS, (const void *)OnVolumeEffects,
                        "OnVolumeEffects", 0);
    rc |= patch_replace(ADDR_ON_VOLUME_MUSIC, (const void *)OnVolumeMusic,
                        "OnVolumeMusic", 0);
    rc |= patch_replace(ADDR_ON_VOLUME_VOICE, (const void *)OnVolumeVoice,
                        "OnVolumeVoice", 0);

    rc |= patch_replace(ADDR_OPEN_TITLE_SCREEN,
                        (const void *)OpenTitleScreen, "OpenTitleScreen", 0);
    rc |= patch_replace(ADDR_TYPER_CTOR, (const void *)TyperConstruct,
                        "TyperConstruct", 5);
    rc |= patch_replace(ADDR_ARROWBAR_CTOR, (const void *)ArrowBarConstruct,
                        "ArrowBarConstruct", 9);
    rc |= patch_replace(ADDR_SCROLLBAR_CTOR,
                        (const void *)ScrollBarConstruct, "ScrollBarConstruct",
                        6);
    rc |= patch_replace(ADDR_CHECKBOX_CTOR, (const void *)CheckBoxConstruct,
                        "CheckBoxConstruct", 11);
    rc |= patch_replace(ADDR_LISTBOX_CTOR, (const void *)ListBoxConstruct,
                        "ListBoxConstruct", 8);
    rc |= patch_replace(ADDR_TEXTLIST_CTOR, (const void *)TextListConstruct,
                        "TextListConstruct", 1);
    rc |= patch_replace(ADDR_TEXTLIST_DELETE, (const void *)TextListDelete,
                        "TextListDelete", 1);
    rc |= patch_replace(ADDR_MULTISPRITE_CTOR,
                        (const void *)MultiSpriteConstruct,
                        "MultiSpriteConstruct", 7);
    rc |= patch_replace(ADDR_EDIT_CTOR, (const void *)EditConstruct,
                        "EditConstruct", 13);
    rc |= patch_replace(ADDR_SCREEN_BASE_CTOR,
                        (const void *)ScreenBaseConstruct,
                        "ScreenBaseConstruct", 2);
    rc |= patch_replace(ADDR_BUTTON_CTOR, (const void *)ButtonConstruct,
                        "ButtonConstruct", 10);
    rc |= patch_replace(ADDR_KEYROW_CTOR, (const void *)KeyRowConstruct,
                        "KeyRowConstruct", 11);
    rc |= patch_replace(ADDR_PANEL_CTOR, (const void *)PanelConstruct,
                        "PanelConstruct", 6);
    rc |= patch_replace(ADDR_SELECT_PLAYER_CTOR,
                        (const void *)SelectPlayerConstruct,
                        "SelectPlayerConstruct", 1);
    rc |= patch_replace(ADDR_COMM_PANEL_CTOR,
                        (const void *)CommPanelConstruct,
                        "CommPanelConstruct", 1);
    rc |= patch_replace(ADDR_BATTLE_NAME_CTOR,
                        (const void *)BattleNameConstruct,
                        "BattleNameConstruct", 1);
    rc |= patch_replace(ADDR_AUDIO_OPTIONS_CTOR,
                        (const void *)AudioDialogConstruct,
                        "AudioDialogConstruct", 2);
    rc |= patch_replace(ADDR_MP_OPTIONS_CTOR,
                        (const void *)MpOptionsConstruct,
                        "MpOptionsConstruct", 1);
    rc |= patch_replace(ADDR_CONTROLS_CTOR,
                        (const void *)ControlsDialogConstruct,
                        "ControlsDialogConstruct", 1);
    rc |= patch_replace(ADDR_DIFFICULTY_CTOR,
                        (const void *)DifficultyDialogConstruct,
                        "DifficultyDialogConstruct", 1);
    rc |= patch_replace(ADDR_QUIT_CONFIRM_CTOR,
                        (const void *)QuitDialogConstruct,
                        "QuitDialogConstruct", 1);
    rc |= patch_replace(ADDR_REPLAY_PROMPT_CTOR,
                        (const void *)ReplayDialogConstruct,
                        "ReplayDialogConstruct", 1);
    rc |= patch_replace(ADDR_DELETE_PLAYER_CTOR,
                        (const void *)DelPlayerDialogConstruct,
                        "DelPlayerDialogConstruct", 1);
    rc |= patch_replace(ADDR_OPTIONS_MENU_CTOR,
                        (const void *)OptionsMenuConstruct,
                        "OptionsMenuConstruct", 1);
    rc |= patch_replace(ADDR_OPEN_COMM_PANEL, (const void *)OpenCommPanel,
                        "OpenCommPanel", 0);
    rc |= patch_replace(ADDR_OPEN_AUDIO_OPTIONS,
                        (const void *)OpenAudioOptions, "OpenAudioOptions", 0);
    rc |= patch_replace(ADDR_OPEN_DELETE_GAME, (const void *)OpenDeleteGame,
                        "OpenDeleteGame", 0);
    rc |= patch_replace(ADDR_OPEN_LOAD_GAME, (const void *)OpenLoadGame,
                        "OpenLoadGame", 0);
    rc |= patch_replace(ADDR_OPEN_MP_HOST, (const void *)OpenMpHost,
                        "OpenMpHost", 0);
    rc |= patch_replace(ADDR_OPEN_MP_JOIN, (const void *)OpenMpJoin,
                        "OpenMpJoin", 0);
    rc |= patch_replace(ADDR_OPEN_MP_OPTIONS, (const void *)OpenMpOptions,
                        "OpenMpOptions", 0);
    rc |= patch_replace(ADDR_OPTIONS_APPLY, (const void *)OptionsApply,
                        "OptionsApply", 1);
    rc |= patch_replace(ADDR_OPTIONS_REQUEST, (const void *)OptionsRequest,
                        "OptionsRequest", 1);
    rc |= patch_replace(ADDR_OPTIONS_SYNC_GROUP, (const void *)OptionsSyncGroup,
                        "OptionsSyncGroup", 1);
    rc |= patch_replace(ADDR_MP_DIALOG_DESTRUCT,
                        (const void *)MpDialogDestruct,
                        "MpDialogDestruct", 1);
    rc |= patch_replace(ADDR_OPTIONS_UPDATE,
                        (const void *)OptionsUpdate,
                        "OptionsUpdate", 1);
    rc |= patch_replace(ADDR_LIST_ADD, (const void *)ListAdd, "ListAdd", 3);
    rc |= patch_replace(ADDR_LIST_DROP_OLDEST, (const void *)ListDropOldest,
                        "ListDropOldest", 1);
    rc |= patch_replace(ADDR_KEY_NAME_INDEX_OF, (const void *)KeyNameIndexOf,
                        "KeyNameIndexOf", 1);
    rc |= patch_replace(ADDR_RECORD_CTOR, (const void *)RecordCtor,
                        "RecordCtor", 2);
    rc |= patch_replace(ADDR_LIST_ROWS_CLEANUP, (const void *)RecordResetAlias,
                        "RecordResetAlias", 1);
    rc |= patch_replace(ADDR_DIALOG_DESTRUCT, (const void *)DialogDestruct,
                        "DialogDestruct", 1);
    rc |= patch_replace(ADDR_DIALOG_DELETE, (const void *)DialogDelete,
                        "DialogDelete", 1);
    rc |= patch_replace(ADDR_OPEN_SAVE_GAME, (const void *)OpenSaveGame,
                        "OpenSaveGame", 0);
    rc |= patch_replace(ADDR_OPEN_OVERWRITE_GAME,
                        (const void *)OpenOverwriteGame,
                        "OpenOverwriteGame", 0);
    rc |= patch_replace(ADDR_OPEN_GAME_MENU, (const void *)OpenGameMenu,
                        "OpenGameMenu", 0);
    rc |= patch_replace(ADDR_OPEN_MESSAGE, (const void *)OpenMessage,
                        "OpenMessage", 0);
    rc |= patch_replace(ADDR_DRAW_TOOLTIP, (const void *)DrawTooltip,
                        "DrawTooltip", 2);
    rc |= patch_replace(ADDR_HUD_TOP_PAINT, (const void *)HudTopPaint,
                        "HudTopPaint", 1);
    rc |= patch_replace(ADDR_HUD_TOP_UPDATE, (const void *)HudTopUpdate,
                        "HudTopUpdate", 1);
    rc |= patch_replace(ADDR_HUD_EDGE_UPDATE, (const void *)HudEdgeUpdate,
                        "HudEdgeUpdate", 1);
    rc |= patch_replace(ADDR_HUD_EDGE_PAINT, (const void *)HudEdgePaint,
                        "HudEdgePaint", 1);
    rc |= patch_replace(ADDR_HUD_SARGE_PAINT, (const void *)HudSargePaint,
                        "HudSargePaint", 1);
    rc |= patch_replace(ADDR_HUD_CMD_PAINT, (const void *)HudCommandsPaint,
                        "HudCommandsPaint", 1);
    rc |= patch_replace(ADDR_HUD_PANEL_PAINT, (const void *)HudPanelPaint,
                        "HudPanelPaint", 1);
    rc |= patch_replace(ADDR_HUD_RADAR_UPDATE, (const void *)HudRadarUpdate,
                        "HudRadarUpdate", 1);
    rc |= patch_replace(ADDR_HUD_RADAR_PAINT, (const void *)HudRadarPaint,
                        "HudRadarPaint", 1);
    rc |= patch_replace(ADDR_HUD_SQUAD_PAINT, (const void *)HudSquadPaint,
                        "HudSquadPaint", 1);
    rc |= patch_replace(ADDR_HUD_SARGE_UPDATE, (const void *)HudSargeUpdate,
                        "HudSargeUpdate", 1);
    rc |= patch_replace(ADDR_TEXT_LIST_PAINT, (const void *)TextListPaint,
                        "TextListPaint", 1);
    rc |= patch_replace(ADDR_CHECKBOX_PAINT, (const void *)CheckboxPaint,
                        "CheckboxPaint", 1);
    rc |= patch_replace(ADDR_COUNT_BUTTON_PAINT, (const void *)CountButtonPaint,
                        "CountButtonPaint", 1);
    rc |= patch_replace(ADDR_COUNT_BUTTON_CTOR,
                        (const void *)CountButtonConstruct,
                        "CountButtonConstruct", 1);
    rc |= patch_replace(ADDR_COUNT_BUTTON_DELETE,
                        (const void *)CountButtonDelete,
                        "CountButtonDelete", 1);
    rc |= patch_replace(ADDR_COUNT_BUTTON_ACTIVATE,
                        (const void *)CountButtonActivate,
                        "CountButtonActivate", 1);
    rc |= patch_replace(ADDR_MP_NAME_PAINT, (const void *)MpNamePaint,
                        "MpNamePaint", 1);
    rc |= patch_replace(ADDR_HUD_PANEL_UPDATE, (const void *)HudPanelUpdate,
                        "HudPanelUpdate", 1);
    rc |= patch_replace(ADDR_HUD_SARGE_DESTRUCT,
                        (const void *)HudSargeDestruct,
                        "HudSargeDestruct", 1);
    rc |= patch_replace(ADDR_HUD_SARGE_DELETE, (const void *)HudSargeDelete,
                        "HudSargeDelete", 1);
    rc |= patch_replace(ADDR_HUD_CMD_DESTRUCT,
                        (const void *)HudCommandsDestruct,
                        "HudCommandsDestruct", 1);
    rc |= patch_replace(ADDR_HUD_CMD_DELETE, (const void *)HudCommandsDelete,
                        "HudCommandsDelete", 1);
    rc |= patch_replace(ADDR_HUD_TOP_DESTRUCT, (const void *)HudTopDestruct,
                        "HudTopDestruct", 1);
    rc |= patch_replace(ADDR_HUD_TOP_DELETE, (const void *)HudTopDelete,
                        "HudTopDelete", 1);
    rc |= patch_replace(ADDR_HUD_PANEL_DESTRUCT,
                        (const void *)HudPanelDestruct,
                        "HudPanelDestruct", 1);
    rc |= patch_replace(ADDR_HUD_PANEL_DELETE, (const void *)HudPanelDelete,
                        "HudPanelDelete", 1);
    rc |= patch_replace(ADDR_HUD_RADAR_DESTRUCT,
                        (const void *)HudRadarDestruct,
                        "HudRadarDestruct", 1);
    rc |= patch_replace(ADDR_HUD_RADAR_DELETE, (const void *)HudRadarDelete,
                        "HudRadarDelete", 1);
    rc |= patch_replace(ADDR_HUD_SQUAD_DESTRUCT,
                        (const void *)HudSquadDestruct,
                        "HudSquadDestruct", 1);
    rc |= patch_replace(ADDR_HUD_SQUAD_DELETE, (const void *)HudSquadDelete,
                        "HudSquadDelete", 1);
    rc |= patch_replace(ADDR_HUD_EDGE_DESTRUCT, (const void *)HudEdgeDestruct,
                        "HudEdgeDestruct", 1);
    rc |= patch_replace(ADDR_HUD_EDGE_DELETE, (const void *)HudEdgeDelete,
                        "HudEdgeDelete", 1);
    rc |= patch_replace(ADDR_MOVIES_DESTRUCT, (const void *)MoviesDestruct,
                        "MoviesDestruct", 1);
    rc |= patch_replace(ADDR_MOVIES_DELETE, (const void *)MoviesDelete,
                        "MoviesDelete", 1);
    rc |= patch_replace(ADDR_ARROWBAR_DESTRUCT, (const void *)ArrowBarDestruct,
                        "ArrowBarDestruct", 1);
    rc |= patch_replace(ADDR_ARROWBAR_DELETE, (const void *)ArrowBarDelete,
                        "ArrowBarDelete", 1);
    rc |= patch_replace(ADDR_SAVE_LIST_DESTRUCT, (const void *)SaveListDestruct,
                        "SaveListDestruct", 1);
    rc |= patch_replace(ADDR_SAVE_LIST_DELETE, (const void *)SaveListDelete,
                        "SaveListDelete", 1);
    rc |= patch_replace(ADDR_COMM_PANEL_DESTRUCT,
                        (const void *)CommPanelDestruct,
                        "CommPanelDestruct", 1);
    rc |= patch_replace(ADDR_COMM_PANEL_DELETE, (const void *)CommPanelDelete,
                        "CommPanelDelete", 1);
    rc |= patch_replace(ADDR_BATTLE_JOIN_DESTRUCT,
                        (const void *)BattleJoinDestruct,
                        "BattleJoinDestruct", 1);
    rc |= patch_replace(ADDR_BATTLE_JOIN_DELETE,
                        (const void *)BattleJoinDelete,
                        "BattleJoinDelete", 1);
    rc |= patch_replace(ADDR_MP_NAME_DESTRUCT, (const void *)MpNameDestruct,
                        "MpNameDestruct", 1);
    rc |= patch_replace(ADDR_MP_NAME_DELETE, (const void *)MpNameDelete,
                        "MpNameDelete", 1);
    rc |= patch_replace(ADDR_DLG_SELECTMAP_DESTRUCT,
                        (const void *)DlgSelectMapDestruct,
                        "DlgSelectMapDestruct", 1);
    rc |= patch_replace(ADDR_DLG_SELECTMAP_DELETE,
                        (const void *)DlgSelectMapDelete,
                        "DlgSelectMapDelete", 1);
    rc |= patch_replace(ADDR_DLG_DIFFICULTY_DESTRUCT,
                        (const void *)DlgDifficultyDestruct,
                        "DlgDifficultyDestruct", 1);
    rc |= patch_replace(ADDR_DLG_DIFFICULTY_DELETE,
                        (const void *)DlgDifficultyDelete,
                        "DlgDifficultyDelete", 1);
    rc |= patch_replace(ADDR_DLG_QUITGAME_DESTRUCT,
                        (const void *)DlgQuitGameDestruct,
                        "DlgQuitGameDestruct", 1);
    rc |= patch_replace(ADDR_DLG_QUITGAME_DELETE,
                        (const void *)DlgQuitGameDelete,
                        "DlgQuitGameDelete", 1);
    rc |= patch_replace(ADDR_DLG_REPLAY_DESTRUCT,
                        (const void *)DlgReplayDestruct,
                        "DlgReplayDestruct", 1);
    rc |= patch_replace(ADDR_DLG_REPLAY_DELETE,
                        (const void *)DlgReplayDelete,
                        "DlgReplayDelete", 1);
    rc |= patch_replace(ADDR_DLG_AUDIO_DESTRUCT,
                        (const void *)DlgAudioDestruct,
                        "DlgAudioDestruct", 1);
    rc |= patch_replace(ADDR_DLG_AUDIO_DELETE,
                        (const void *)DlgAudioDelete,
                        "DlgAudioDelete", 1);
    rc |= patch_replace(ADDR_DLG_OPTIONS_DESTRUCT,
                        (const void *)DlgOptionsDestruct,
                        "DlgOptionsDestruct", 1);
    rc |= patch_replace(ADDR_DLG_OPTIONS_DELETE,
                        (const void *)DlgOptionsDelete,
                        "DlgOptionsDelete", 1);
    rc |= patch_replace(ADDR_DLG_DELGAME_DESTRUCT,
                        (const void *)DlgDelGameDestruct,
                        "DlgDelGameDestruct", 1);
    rc |= patch_replace(ADDR_DLG_DELGAME_DELETE,
                        (const void *)DlgDelGameDelete,
                        "DlgDelGameDelete", 1);
    rc |= patch_replace(ADDR_DLG_OVERWRITE_DESTRUCT,
                        (const void *)DlgOverwriteDestruct,
                        "DlgOverwriteDestruct", 1);
    rc |= patch_replace(ADDR_DLG_OVERWRITE_DELETE,
                        (const void *)DlgOverwriteDelete,
                        "DlgOverwriteDelete", 1);
    rc |= patch_replace(ADDR_DLG_DELPLAYER_DESTRUCT,
                        (const void *)DlgDelPlayerDestruct,
                        "DlgDelPlayerDestruct", 1);
    rc |= patch_replace(ADDR_DLG_DELPLAYER_DELETE,
                        (const void *)DlgDelPlayerDelete,
                        "DlgDelPlayerDelete", 1);
    rc |= patch_replace(ADDR_DLG_CONTROLS_DESTRUCT,
                        (const void *)DlgControlsDestruct,
                        "DlgControlsDestruct", 1);
    rc |= patch_replace(ADDR_DLG_CONTROLS_DELETE,
                        (const void *)DlgControlsDelete,
                        "DlgControlsDelete", 1);
    rc |= patch_replace(ADDR_DLG_SELECTPLAYER_DESTRUCT,
                        (const void *)DlgSelectPlayerDestruct,
                        "DlgSelectPlayerDestruct", 1);
    rc |= patch_replace(ADDR_DLG_SELECTPLAYER_DELETE,
                        (const void *)DlgSelectPlayerDelete,
                        "DlgSelectPlayerDelete", 1);
    rc |= patch_replace(ADDR_DLG_NAMEENTRY_DESTRUCT,
                        (const void *)DlgNameEntryDestruct,
                        "DlgNameEntryDestruct", 1);
    rc |= patch_replace(ADDR_DLG_NAMEENTRY_DELETE,
                        (const void *)DlgNameEntryDelete,
                        "DlgNameEntryDelete", 1);
    rc |= patch_replace(ADDR_DLG_LOADGAME_DESTRUCT,
                        (const void *)DlgLoadGameDestruct,
                        "DlgLoadGameDestruct", 1);
    rc |= patch_replace(ADDR_DLG_LOADGAME_DELETE,
                        (const void *)DlgLoadGameDelete,
                        "DlgLoadGameDelete", 1);
    rc |= patch_replace(ADDR_DLG_MESSAGE_DESTRUCT,
                        (const void *)DlgMessageDestruct,
                        "DlgMessageDestruct", 1);
    rc |= patch_replace(ADDR_DLG_MESSAGE_DELETE,
                        (const void *)DlgMessageDelete,
                        "DlgMessageDelete", 1);
    rc |= patch_replace(ADDR_DLG_GAMEMENU_DESTRUCT,
                        (const void *)DlgGameMenuDestruct,
                        "DlgGameMenuDestruct", 1);
    rc |= patch_replace(ADDR_DLG_GAMEMENU_DELETE,
                        (const void *)DlgGameMenuDelete,
                        "DlgGameMenuDelete", 1);
    rc |= patch_replace(ADDR_TYPER_PAINT, (const void *)TyperPaint,
                        "TyperPaint", 1);
    rc |= patch_replace(ADDR_TYPER_UPDATE, (const void *)TyperUpdate,
                        "TyperUpdate", 1);
    rc |= patch_replace(ADDR_ARROW_DESTRUCT, (const void *)ArrowDestruct,
                        "ArrowDestruct", 1);
    rc |= patch_replace(ADDR_ARROW_DELETE, (const void *)ArrowDelete,
                        "ArrowDelete", 1);
    rc |= patch_replace(ADDR_MULTI_UPDATE_THUNK,
                        (const void *)MultiUpdateThunk,
                        "MultiUpdateThunk", 1);
    rc |= patch_replace(ADDR_SCROLLBAR_PAINT, (const void *)ScrollBarPaint,
                        "ScrollBarPaint", 1);
    rc |= patch_replace(ADDR_SCROLLBAR_DESTRUCT,
                        (const void *)ScrollBarDestruct,
                        "ScrollBarDestruct", 1);
    rc |= patch_replace(ADDR_SCROLLBAR_DELETE, (const void *)ScrollBarDelete,
                        "ScrollBarDelete", 1);
    rc |= patch_replace(ADDR_WIDGET_UPDATE_THUNK,
                        (const void *)WidgetUpdateThunk,
                        "WidgetUpdateThunk", 1);
    rc |= patch_replace(ADDR_WIDGET_REPAINT_THUNK,
                        (const void *)WidgetRepaintThunk,
                        "WidgetRepaintThunk", 2);
    rc |= patch_replace(ADDR_WIDGET_LAST_SIBLING,
                        (const void *)WidgetLastSibling,
                        "WidgetLastSibling", 3);
    rc |= patch_replace(ADDR_WIDGET_FOCUS_NEXT, (const void *)WidgetFocusNext,
                        "WidgetFocusNext", 4);
    rc |= patch_replace(ADDR_WIDGET_FOCUS_PREV, (const void *)WidgetFocusPrev,
                        "WidgetFocusPrev", 2);
    rc |= patch_replace(ADDR_WIDGET_UPDATE, (const void *)WidgetUpdate,
                        "WidgetUpdate", 21);
    rc |= patch_replace(ADDR_WIDGET_UPDATE_CANCEL,
                        (const void *)WidgetUpdateCancel,
                        "WidgetUpdateCancel", 17);
    rc |= patch_replace(ADDR_WIDGET_DESTRUCT, (const void *)WidgetDestruct,
                        "WidgetDestruct", 2);
    rc |= patch_replace(ADDR_WIDGET_DELETE, (const void *)WidgetDelete,
                        "WidgetDelete", 1);
    rc |= patch_replace(ADDR_LABEL_DESTRUCT, (const void *)LabelDestruct,
                        "LabelDestruct", 1);
    rc |= patch_replace(ADDR_LABEL_DELETE, (const void *)LabelDelete,
                        "LabelDelete", 1);
    rc |= patch_replace(ADDR_WIDGET_TAKE_FOCUS, (const void *)WidgetTakeFocus,
                        "WidgetTakeFocus", 30);
    rc |= patch_replace(ADDR_WIDGET_REPAINT, (const void *)WidgetRepaint,
                        "WidgetRepaint", 29);
    rc |= patch_replace(ADDR_WIDGET_CONSTRUCT, (const void *)WidgetConstruct,
                        "WidgetConstruct", 33);
    rc |= patch_replace(ADDR_LABEL_CONSTRUCT, (const void *)LabelConstruct,
                        "LabelConstruct", 2);
    rc |= patch_replace(ADDR_WIDGET_SCREEN_RECT, (const void *)WidgetScreenRect,
                        "WidgetScreenRect", 33);
    rc |= patch_replace(ADDR_LABEL_DRAW, (const void *)LabelDraw,
                        "LabelDraw", 2);
    rc |= patch_replace(ADDR_MP_PREVIEW_SETBITMAP,
                        (const void *)MpPreviewSetBitmap,
                        "MpPreviewSetBitmap", 3);
    rc |= patch_replace(ADDR_PLACE_SCREEN_CLICK,
                        (const void *)PlacementScreenClick,
                        "PlacementScreenClick", 1);
    rc |= patch_replace(ADDR_HUD_REPAINT_ONE, (const void *)HudRepaintOne,
                        "HudRepaintOne", 4);
    rc |= patch_replace(ADDR_HUD_PANEL_WIDTH, (const void *)HudPanelWidth,
                        "HudPanelWidth", 3);
    rc |= patch_replace(ADDR_SET_POINTER_MODE, (const void *)SetPointerMode,
                        "SetPointerMode", 10);
    rc |= patch_replace(ADDR_POINTER_DROP_ITEM,
                        (const void *)PointerDropItem,
                        "PointerDropItem", 0);
    rc |= patch_replace(ADDR_POINTER_SELECT, (const void *)PointerSelect,
                        "PointerSelect", 0);
    rc |= patch_replace(ADDR_POINTER_PICK_BOARD,
                        (const void *)PointerPickBoard,
                        "PointerPickBoard", 4);
    rc |= patch_replace(ADDR_POINTER_PICK_WATCHED,
                        (const void *)PointerPickWatchedItem,
                        "PointerPickWatchedItem", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_MODE4,
                        (const void *)PointerPickMode4,
                        "PointerPickMode4", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_MODE5,
                        (const void *)PointerPickMode5,
                        "PointerPickMode5", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_MODE6,
                        (const void *)PointerPickMode6,
                        "PointerPickMode6", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_MODE0,
                        (const void *)PointerPickMode0,
                        "PointerPickMode0", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_HEAL,
                        (const void *)PointerPickHeal,
                        "PointerPickHeal", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_REPAIR,
                        (const void *)PointerPickRepair,
                        "PointerPickRepair", 1);
    rc |= patch_replace(ADDR_POINTER_PICK_ENEMY_TROOPER,
                        (const void *)PointerPickEnemyTrooper,
                        "PointerPickEnemyTrooper", 1);
    rc |= patch_replace(ADDR_VEHICLE_DISMOUNT_ALL,
                        (const void *)VehicleDismountAll,
                        "VehicleDismountAll", 1);
    rc |= patch_replace(ADDR_POINTER_ACTION_MODE6,
                        (const void *)PointerActionMode6,
                        "PointerActionMode6", 1);
    rc |= patch_replace(ADDR_POINTER_ACTION_MODE4,
                        (const void *)PointerActionMode4,
                        "PointerActionMode4", 1);
    rc |= patch_replace(ADDR_POINTER_ACTION_MODE5,
                        (const void *)PointerActionMode5,
                        "PointerActionMode5", 1);
    rc |= patch_replace(ADDR_SET_WEAPON_TARGET_AIMED,
                        (const void *)SetWeaponTargetAimed,
                        "SetWeaponTargetAimed", 4);
    rc |= patch_replace(ADDR_SET_WEAPON_TARGET_MEDIC,
                        (const void *)SetWeaponTargetMedic,
                        "SetWeaponTargetMedic", 1);
    rc |= patch_replace(ADDR_SET_WEAPON_TARGET_WRENCH,
                        (const void *)SetWeaponTargetWrench,
                        "SetWeaponTargetWrench", 1);
    rc |= patch_replace(ADDR_SET_WEAPON_TARGET_KIND2A,
                        (const void *)SetWeaponTargetKind2A,
                        "SetWeaponTargetKind2A", 1);
    rc |= patch_replace(ADDR_SET_WEAPON_TARGET_SWEEPER,
                        (const void *)SetWeaponTargetSweeper,
                        "SetWeaponTargetSweeper", 1);
    rc |= patch_replace(ADDR_HUD_MESSAGE, (const void *)HudMessage,
                        "HudMessage", 46);
    rc |= patch_replace(ADDR_CLOSE_SCREEN, (const void *)CloseScreen,
                        "CloseScreen", 1);
    rc |= patch_replace(ADDR_BUILD_HUD_WIDGETS, (const void *)BuildHudWidgets,
                        "BuildHudWidgets", 2);
    rc |= patch_replace(ADDR_FREE_HUD_WIDGETS, (const void *)FreeHudWidgets,
                        "FreeHudWidgets", 2);
    rc |= patch_replace(ADDR_HUD_POST_UPDATE, (const void *)HudPostUpdate,
                        "HudPostUpdate", 1);
    return rc;
}

/* ---- MpPanelConstruct -------------------------------------------------- */

/* The spin control's class is NOT reconstructed -- 0x00456300 is still the
 * image's -- so it is the one seam this function needs.  Everything else it
 * calls is ours. */
typedef AM2_Widget *(__attribute__((thiscall)) *am2_mp_spin_fn)(
    AM2_Widget *w, int32_t left, int32_t top, int32_t width, int32_t height,
    int32_t value, int32_t lo, int32_t hi, int32_t step, AM2_Widget *parent,
    void (__cdecl *commit)(AM2_Widget *), int32_t c0, int32_t c1, int32_t c2,
    int32_t row);
#define orig_mp_spin ((am2_mp_spin_fn)(uintptr_t)ADDR_MP_SPIN_CTOR)

/* 0x00430530. The multiplayer host/join panel's constructor.
 *
 * ONE CLASS, TWO PANELS: both factories allocate 0x278 and call this, and
 * `bmp` is the only thing that differs between hosting and joining.
 *
 * THE MSVC SEH FRAME IS NOT REPRODUCED, per the standing decision -- the
 * original's `push -1; push <handler>; push fs:[0]` prologue and the unwind
 * state index it bumps at every child (frame slot +0x1BC, written 51 times)
 * exist only so a throw can destroy what has been built.  Nothing in this
 * program throws: VC6's operator new answers NULL and the game tests it,
 * which is the `if (w)` before every constructor below.
 *
 * The children go into THREE PARALLEL ARRAYS -- names, colours and teams,
 * four of each, contiguous by kind rather than by slot.  MpPanelDestruct
 * walks them that way. */
AM2_Widget *__attribute__((thiscall)) MpPanelConstruct(AM2_Widget *w,
                                                       const char *bmp)
{
    uint8_t   *p = (uint8_t *)w;
    AM2_Rect   r;
    int32_t    i;

    ScreenBaseConstruct(w, bmp, 1);
    *(const void **)p = (const void *)(uintptr_t)VTABLE_MP_PANEL;
    ReadMpMapList();

    /* The four player-colour swatches.  The loop's bound is the address of
     * the NEXT global, so the array's length is written nowhere. */
    {
        AM2_Sprite **slot = (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_A;
        char         name[0x20];

        for (i = 0; slot < (AM2_Sprite **)(uintptr_t)ADDR_MENU_MSG_LIST;
             slot++, i++) {
            am2_sprintf(name, (const char *)AM2_IMAGE(0x004871ACu), i);
            *slot = PreloadSpriteName(name, 1, 1);
        }
    }

    /* And the thirteen panel sprites, by set and index rather than by name. */
    {
        AM2_Sprite **slot = (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B;

        for (i = 0; slot < (AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B_END;
             slot++, i++)
            *slot = PreloadSprite(3, 0x17, i, 1, 1);
    }
    /* FOUR PLAYER ROWS, three widgets each, into three PARALLEL arrays.  The
     * original's y coordinates are a constant minus `this` plus the name
     * buffer pointer, which cancels to `base + slot * 32`. */
    for (i = 0; i < AM2_COMM_SLOTS; i++) {
        char       *name = (char *)(p + 0x64) + (size_t)i * 0x20;
        AM2_Widget **names   = (AM2_Widget **)(p + MP_PANEL_OFF_NAMES);
        AM2_Widget **colours = (AM2_Widget **)(p + MP_PANEL_OFF_COLOURS);
        AM2_Widget **teams   = (AM2_Widget **)(p + MP_PANEL_OFF_TEAMS);
        AM2_Widget  *child;

        name[0] = '\0';

        /* The row's own name: the comm slot's player, or `-- Open --`. */
        {
            const uint8_t *comm = *(const uint8_t **)(uintptr_t)ADDR_COMM_OBJECT;

            if (*(const int32_t *)(comm + COMM_OFF_PLAYER_SLOTS
                                   + (size_t)i * AM2_COMM_SLOT_STRIDE) != 0)
                am2_sprintf(name, (const char *)(uintptr_t)ADDR_STR_FMT_S,
                            comm + COMM_OFF_SLOT_NAME
                            + (size_t)i * AM2_COMM_SLOT_STRIDE);
            else
                am2_sprintf(name, (const char *)AM2_IMAGE(AM2_STR_OPEN_SLOT));
        }

        child = (AM2_Widget *)orig_operator_new(0x74);
        names[i] = child
                 ? MpNameConstruct(child, name, 0x18, 40 + i * 32, 0x58, 0x12,
                                   1,
                                   *(const uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR,
                                   *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR,
                                   i)
                 : 0;
        WidgetAddChild(w, names[i]);

        child = (AM2_Widget *)orig_operator_new(0x68);
        colours[i] = child ? MpColourConstruct(child, 0x86, 39 + i * 32, i) : 0;
        WidgetAddChild(w, colours[i]);

        child = (AM2_Widget *)orig_operator_new(0x68);
        teams[i] = child ? MpTeamConstruct(child, 0xBF, 37 + i * 32, i) : 0;
        WidgetAddChild(w, teams[i]);
    }

    /* THE MAP-TYPE LIST and its scrollbar.  The idiom for every fixed child
     * is the same: allocate, and if that succeeded build it with a rectangle
     * from RectSet followed by the class's own arguments; then store the
     * pointer in its field and add it -- adding a null child is what the
     * original does when the allocation fails, not a guard we supply. */
    {
        AM2_Widget **slot = (AM2_Widget **)(p + MP_PANEL_OFF_TYPE_BOX);
        AM2_Widget  *child;

        child = (AM2_Widget *)orig_operator_new(0x98);
        *slot = child ? ListBoxConstruct(child, 0x16, 0xC8, 0xFA, 0x5A,
                                         (void *)0, 0, 0, 1)
                      : 0;
        /* UNGUARDED, and that is the original: it stores the result -- null
         * or not -- and then writes three fields THROUGH it. On an
         * allocation failure this faults, which VC6 makes possible because
         * its operator new returns null rather than throwing. Reproduced,
         * like LockSurface's descriptor defect, rather than repaired. */
        *(int32_t *)((uint8_t *)*slot + 0x5C) = -1;
        *(int32_t *)((uint8_t *)*slot + 0x4C) = 1;
        *(int32_t *)((uint8_t *)*slot + 0x50) = 0;
        WidgetAddChild(w, *slot);

        child = (AM2_Widget *)orig_operator_new(0x78);
        if (child)
            ArrowBarConstruct(child, 0x121, 0xC2, 0x13, 0x68, w,
                              (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                              (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                              0x40, 1);
        WidgetAddChild(w, child);

        /* The list and its bar point at each other -- the list's +0x7C is the
         * bar and the bar's +0x58 is the list. A scrollbar built without that
         * back-link scrolls nothing and looks perfectly constructed. */
        *(AM2_Widget **)((uint8_t *)*slot + 0x7C) = child;
        *(AM2_Widget **)((uint8_t *)child + 0x58) = *slot;
        *(int32_t *)((uint8_t *)child + 0x50) = 0;
    }

    /* The two ready lamps, green and red, at the same size and 0x35 apart. */
    {
        AM2_Widget *child;

        child = (AM2_Widget *)orig_operator_new(0x80);
        if (child)
            MultiSpriteConstruct(child,
                                 (const char *)AM2_IMAGE(AM2_BMP_GREEN0),
                                 (const char *)AM2_IMAGE(AM2_BMP_GREEN1),
                                 1, *RectSet(&r, 0xA0, 0x139, 0x11, 0x10));
        *(AM2_Widget **)(p + MP_PANEL_OFF_BLINKER_0) = child;
        WidgetAddChild(w, child);
        *(int32_t *)((uint8_t *)*(AM2_Widget **)(p + MP_PANEL_OFF_BLINKER_0)
                     + 0x50) = 0;
    }

    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x80);

        if (child)
            MultiSpriteConstruct(child,
                                 (const char *)AM2_IMAGE(0x00485E30u),
                                 (const char *)AM2_IMAGE(0x00485E44u),
                                 1, *RectSet(&r, 0xD5, 0x139, 0x11, 0x10));
        *(AM2_Widget **)(p + MP_PANEL_OFF_BLINKER_1) = child;
        WidgetAddChild(w, child);
        *(int32_t *)((uint8_t *)*(AM2_Widget **)(p + MP_PANEL_OFF_BLINKER_1)
                     + 0x50) = 0;
    }

    /* The chat line: its buffer is a FIELD of the panel, seeded from the
     * directory scratch, and the edit writes into it in place. */
    strcpy((char *)(p + 0xE4), (const char *)(uintptr_t)ADDR_DIR_SCRATCH);
    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x80);

        if (child)
            EditConstruct(child, (char *)(p + 0xE4), 0x3C,
                          0x17, 0x153, 0xF7, 0x10, 1,
                          *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR,
                          *(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG,
                          *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR,
                          OnChatEnter,
                          0, 0);
        WidgetAddChild(w, child);
        *(AM2_Widget **)(p + 0x34) = child;
        *(AM2_Widget **)((uint8_t *)child + 0x70) =
            *(AM2_Widget **)(p + MP_PANEL_OFF_BLINKER_0);
        *(int32_t *)((uint8_t *)child + 0x44) = 1;
    }

    /* THE MESSAGE LIST'S ROWS ARE A GLOBAL SINGLETON, made once and kept.
     * Every panel after the first reuses it, which is why the log survives
     * closing and reopening the lobby. */
    if (*(void **)(uintptr_t)ADDR_MENU_MSG_LIST == 0) {
        void *rows = orig_operator_new(0x0C);

        *(void **)(uintptr_t)ADDR_MENU_MSG_LIST = rows ? RecordCtor(rows, 0)
                                                       : 0;
    }
    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x98);

        /* ADDR_LOG as the callback is not a mistake: it is a bare `ret` in
         * this build, so the list is built with a do-nothing notifier. */
        if (child)
            TextListConstruct(child, 0x14, 0x17B, 0xFC, 0x43,
                              *(void **)(uintptr_t)ADDR_MENU_MSG_LIST,
                              (int32_t)ADDR_LOG, 0);
        WidgetAddChild(w, child);
    }

    /* THE SCORE SPINNER is built ONLY when hosting -- a joiner has no such
     * child, so the two roles give the panel different child counts. */
    if (*(const int32_t *)(*(const uint8_t **)(uintptr_t)ADDR_COMM_OBJECT
                           + COMM_OFF_IS_HOST) != 0) {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x84);

        if (child)
            orig_mp_spin(child, 0x22B, 0x2F, 0x4A, 0x18,
                            *(const int32_t *)(uintptr_t)ADDR_SCORE_LIMIT,
                            0x64, 0x2706, 0x64, w,
                            MpCommitScore,
                            *(const uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR,
                            *(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG,
                            *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR,
                            0);
        WidgetAddChild(w, child);
    }

    /* The score limit as TEXT, in the panel's own buffer, shown by a label
     * that does not own it. */
    am2_sprintf((char *)(p + MP_PANEL_OFF_SCORE_TEXT),
                (const char *)(uintptr_t)ADDR_FMT_INT,
                *(const int32_t *)(uintptr_t)ADDR_SCORE_LIMIT);
    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x64);

        if (child)
            LabelConstruct(child, (const char *)(p + MP_PANEL_OFF_SCORE_TEXT),
                           0x22B, 0x35, 0x21, 0x0E, 2,
                           *(const uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG,
                           *(const uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR);
        WidgetAddChild(w, child);
        *(int32_t *)((uint8_t *)child + 0x50) = 0;
    }

    /* THE MAP PREVIEW, with a fallback that also moves the data directory.
     * Neither move is undone; the panel leaves the directory where the
     * fallback put it, unlike LoadMap which restores its own. */
    {
        char        path[0x104];
        AM2_Widget *child;

        SetGameDir((const char *)(uintptr_t)ADDR_MAP_FOLDER);
        am2_sprintf(path, (const char *)(uintptr_t)ADDR_FMT_PREV_BMP,
                    (const char *)(uintptr_t)ADDR_MAP_NAME);
        if (!FileExists(path)) {
            SetGameDir((const char *)(uintptr_t)ADDR_STR_BITMAPS_DIR);
            strcpy(path, (const char *)(uintptr_t)ADDR_STR_BAD_MP_PREV);
        }

        child = (AM2_Widget *)orig_operator_new(0x60);
        if (child)
            PanelConstruct(child, path, 1,
                           *RectSet(&r, 0x152, 0x110, 0xBA, 0xBA));
        *(AM2_Widget **)(p + MP_PANEL_OFF_PREVIEW) = child;
        WidgetAddChild(w, child);
        *(int32_t *)((uint8_t *)*(AM2_Widget **)(p + MP_PANEL_OFF_PREVIEW)
                     + 0x50) = 0;
    }

    /* START or READY -- ONE slot, two bitmaps, two handlers.  The host
     * starts the game and everyone else declares themselves ready, so the
     * two never coexist. */
    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x78);
        int32_t     host  = *(const int32_t *)(
                                *(const uint8_t **)(uintptr_t)ADDR_COMM_OBJECT
                                + COMM_OFF_IS_HOST);

        if (child) {
            if (host)
                ButtonConstruct(child,
                                (const char *)AM2_IMAGE(0x00476E7Cu),
                                (const char *)AM2_IMAGE(0x00476E90u),
                                (const char *)AM2_IMAGE(0x00476EA4u), 1,
                                *RectSet(&r, 0x21F, 0x11A, 0x51, 0x20),
                                (void (__cdecl *)(AM2_Widget *))(uintptr_t)
                                    0x00431850u,
                                0);
            else
                ButtonConstruct(child,
                                (const char *)AM2_IMAGE(0x0048713Cu),
                                (const char *)AM2_IMAGE(0x00487150u),
                                (const char *)AM2_IMAGE(0x00487164u), 1,
                                *RectSet(&r, 0x21F, 0x11A, 0x51, 0x20),
                                (void (__cdecl *)(AM2_Widget *))(uintptr_t)
                                    0x00431920u,
                                0);
        }
        WidgetAddChild(w, child);
    }

    /* OPTIONS and CANCEL, always present, at 0x322 and 0x363. Cancel passes a
     * NULL first bitmap where the other three buttons pass one, so it has no
     * released state of its own. */
    {
        AM2_Widget *child = (AM2_Widget *)orig_operator_new(0x78);

        if (child)
            ButtonConstruct(child,
                            (const char *)AM2_IMAGE(0x004870F4u),
                            (const char *)AM2_IMAGE(0x0048710Cu),
                            (const char *)AM2_IMAGE(0x00487124u), 1,
                            *RectSet(&r, 0x21F, 0x142, 0x51, 0x20),
                            (void (__cdecl *)(AM2_Widget *))(uintptr_t)
                                0x004319B0u,
                            0);
        WidgetAddChild(w, child);

        child = (AM2_Widget *)orig_operator_new(0x78);
        if (child)
            ButtonConstruct(child, 0,
                            (const char *)AM2_IMAGE(0x00486E04u),
                            (const char *)AM2_IMAGE(0x00486E1Cu), 1,
                            *RectSet(&r, 0x21F, 0x16B, 0x51, 0x20),
                            (void (__cdecl *)(AM2_Widget *))(uintptr_t)
                                0x004319E0u,
                            0);
        WidgetAddChild(w, child);
    }

    return w;
}
