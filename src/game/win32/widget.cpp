/* The menu widget hierarchy -- see widget.h for the shape of the class tree.
 *
 * This module is the first piece of it. What is here is one painter; the
 * thirty-four constructors, the four other virtuals per class and the
 * containers that own them are all still original and reached by address. */

#include "widget.h"
#include "surface.h"
#include "../rect.h"
#include "../misc.h"   /* IsKeyDown, KeyChanged */
#include "sprite.h"
#include "frame.h"
#include "audio.h"
#include "dplay.h"   /* CommCreateDirectPlay -- reconstructed */
#include "../gamedir.h" /* SetGameDir -- reconstructed */
#include "../image.h"
#include "../crt.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

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
typedef void (__cdecl *AM2_DrawTextClippedFn)(int32_t x, int32_t y,
                                              const char *text, int32_t font,
                                              RECT clip, int32_t colour);
#define orig_draw_text_clipped \
    ((AM2_DrawTextClippedFn)(uintptr_t)ADDR_DRAW_TEXT_CLIPPED)

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
#define AM2_DIK_RETURN 0x1C
#define AM2_DIK_SPACE  0x39
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
 * through. The handler itself is still original -- it is the field's typing
 * behaviour, not its lifecycle. */
#define g_focusedEdit (*(AM2_Widget **)(uintptr_t)ADDR_FOCUSED_EDIT)
/* Spelled exactly as winproc.cpp spells it, type and all, so the two stay one
 * definition rather than a drift -- checkglobals refused the first attempt,
 * which had it as a bare void *. */
typedef void (__cdecl *am2_char_fn)(uint32_t ch, uint32_t lo, uint32_t hi);
#define g_charHandler (*(am2_char_fn *)(uintptr_t)ADDR_CHAR_HANDLER)

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

    if (w->parent && orig_mouse_moved && !w->unknown4C) {
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
        orig_draw_text_clipped(rowRect.left + LIST_TEXT_INDENT, rowRect.top,
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

    if (!w->parent || w->unknown4C) {
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

    orig_draw_text_clipped(w->rect.left, w->rect.top, buf,
                           *(const int32_t *)(self + EDIT_OFF_FONT),
                           paint, ink);

    UnlockSurface();
}

void __attribute__((thiscall)) EditTakeFocus(AM2_Widget *w, int32_t announce)
{
    g_focusedEdit = w;
    g_charHandler = (am2_char_fn)AM2_IMAGE(ADDR_EDIT_CHAR_HANDLER);
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
    if (w->parent && orig_mouse_moved && !w->unknown4C) {
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

    orig_draw_text_clipped(w->rect.left, y, line, 1, clip, ink);
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

/* One per dialog class. See widget.h for why this is a macro. */
#define AM2_DIALOG_DTOR(name, vt)                                        \
    void __attribute__((thiscall)) name##Destruct(AM2_Widget *w)             \
    {                                                                        \
        w->vtable = (void *)AM2_IMAGE(vt);                                   \
        DialogDestruct(w);                                                   \
    }                                                                        \
    AM2_Widget *__attribute__((thiscall)) name##Delete(AM2_Widget *w,        \
                                                       int32_t flags)        \
    {                                                                        \
        name##Destruct(w);                                                   \
        if (flags & 1)                                                       \
            am2_free(w);                                                     \
        return w;                                                            \
    }

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
        if (cand->flag50 && !cand->unknown4C)
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
    if (!cand->flag50 || cand->unknown4C)
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

#define AM2_DIK_ESCAPE 1

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
    w->unknown4C   = 0;
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

    orig_draw_text_clipped(w->rect.left, w->rect.top,
                           *(const char *const *)(self + LABEL_OFF_TEXT),
                           *(const int32_t *)(self + LABEL_OFF_FONT),
                           paint,
                           *(const uint8_t *)(self + LABEL_OFF_INK));

    UnlockSurface();
}

typedef void (__attribute__((thiscall)) *AM2_RecordResetFn)(void *rec);
#define orig_record_reset \
    ((AM2_RecordResetFn)(uintptr_t)ADDR_SESSION_RESET)

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
    orig_record_reset(rec);
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

/* The OPTIONS dialog, declared by the table at ADDR_OPTION_TABLE. Both the
 * load and the apply walk it with a cursor 0x18 bytes INTO each record, which
 * is why the original reads the bit and the mask choice at +0 and +4 and the
 * widget index at -0x18; written out here from the record base instead. */

#define g_gameOverFlags   (*(uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS)
#define g_gameSetting22C  (*(uint32_t *)(uintptr_t)ADDR_GAME_SETTING_22C)
#define g_menuRequest     (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST)
#define g_menuRequestSet  (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)
#define g_commObject      (*(uint8_t **)(uintptr_t)ADDR_COMM_OBJECT)

typedef void (__cdecl *AM2_SendPlayersFn)(int32_t which);
#define orig_comm_send_players \
    ((AM2_SendPlayersFn)(uintptr_t)ADDR_COMM_SEND_PLAYERS)

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

        box->unknown4C = 0;
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
    orig_comm_send_players(0);
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
        box->unknown4C = (ticked == 0);
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

typedef void *(__cdecl *AM2_OperatorNewFn)(uint32_t size);
typedef void *(__attribute__((thiscall)) *AM2_ScreenCtorFn)(void *obj,
                                                            const char *bmp);
typedef void (__cdecl *AM2_VoidFn)(void);

#define orig_operator_new     ((AM2_OperatorNewFn)AM2_IMAGE(ADDR_GAME_OPERATOR_NEW))
#define orig_mp_panel_ctor    ((AM2_ScreenCtorFn)AM2_IMAGE(ADDR_MP_PANEL_CTOR))
#define orig_mp_panel_refresh ((AM2_VoidFn)AM2_IMAGE(ADDR_MP_PANEL_REFRESH))

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
    OpenScreen(AM2_MP_PANEL_SIZE, orig_mp_panel_ctor,
               (const char *)AM2_IMAGE(ADDR_STR_MPHOST_BMP));
    g_mpSession = AM2_MP_SESSION_HOST;
    orig_mp_panel_refresh();
}

void __cdecl OpenMpJoin(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_MP_PANEL_SIZE, orig_mp_panel_ctor,
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
    OpenScreen(AM2_MP_SELECT_MAP_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_MP_SELECT_MAP_CTOR),
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_ENTER_NAME_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenCdPrompt(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_CD_PROMPT_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_CD_PROMPT_CTOR),
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_MOVIES_CTOR),
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
                    (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_DELETE_GAME_CTOR),
                    (const char *)AM2_IMAGE(ADDR_STR_DELGAME_BMP), 0);
    } else {
        OpenScreen2(AM2_DELETE_GAME_SIZE,
                    (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_DELETE_GAME_CTOR),
                    (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP), 1);
    }
}

/* Arm 5. LOAD GAME, and the odd one out: the repaint happens AFTER the
 * screen is constructed and the global is published only then. Reproduced as
 * it is written rather than made to match its two siblings -- whether that
 * ordering matters is not something reading settles, and the screen is
 * reachable, so it can be measured rather than argued about. */
void __cdecl OpenLoadGame(void)
{
    CloseCurrentScreen();
    if (g_gameState == AM2_STATE_MISSION) {
        void *obj = orig_operator_new(AM2_LOAD_GAME_SIZE);
        uint8_t *screen = (uint8_t *)0;

        if (obj) {
            AM2_ScreenCtor2Fn ctor =
                (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_LOAD_GAME_CTOR);
            screen = (uint8_t *)ctor(obj, (const char *)
                                     AM2_IMAGE(ADDR_STR_LOADGAME_BMP), 0);
        }
        RefreshScreen();
        g_paintObject = screen;
    } else {
        OpenScreen2(AM2_LOAD_GAME_SIZE,
                    (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_LOAD_GAME_CTOR),
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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ScreenBaseCtorFn)(
    AM2_Widget *w, const char *bmp, int32_t flag);
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ButtonCtorFn)(
    AM2_Widget *w, const char *b0, const char *b1, const char *b2,
    int32_t flag, AM2_Rect box, void (__cdecl *handler)(AM2_Widget *),
    int32_t last);

#define orig_screen_base_ctor \
    ((AM2_ScreenBaseCtorFn)AM2_IMAGE(ADDR_SCREEN_BASE_CTOR))
#define orig_button_ctor ((AM2_ButtonCtorFn)AM2_IMAGE(ADDR_BUTTON_CTOR))

typedef struct {
    int32_t     top;
    uint32_t    handler;
    uint32_t    bmp[3];
} AM2_MenuButton;

/* Rows 0xA0, 0xC8, 0xF0, 0x118 -- 40 apart. */
static const AM2_MenuButton kOptionsButtons[] = {
    { 0x00A0, ADDR_ON_AUDIO_BUTTON,
      { 0x0048B8B0, 0x0048B8C4, 0x0048B8D8 } },  /* 03_120_0N_audio */
    { 0x00C8, ADDR_ON_CONTROLS_BUTTON,
      { 0x0048B868, 0x0048B880, 0x0048B898 } },  /* 03_121_0N_controls */
    { 0x00F0, ADDR_ON_DIFFICULTY_BUTTON,
      { 0x0048B814, 0x0048B830, 0x0048B84C } },  /* 03_126_0N_difficulty */
    { 0x0118, ADDR_ON_MENU_BACK,
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

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_OPTIONS_MENU);
    w->flag44 = 1;

    for (i = 0; i < sizeof kOptionsButtons / sizeof kOptionsButtons[0]; i++) {
        const AM2_MenuButton *b = &kOptionsButtons[i];
        AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
        AM2_Rect    box;

        if (btn) {
            RectSet(&box, AM2_OPTIONS_BUTTON_LEFT, b->top,
                    AM2_OPTIONS_BUTTON_WIDTH, AM2_OPTIONS_BUTTON_HEIGHT);
            btn = orig_button_ctor(btn,
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

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(ADDR_ON_MENU_BACK);
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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_TyperCtorFn)(
    AM2_Widget *w, AM2_Rect box, const char *message);
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_MultiSpriteCtorFn)(
    AM2_Widget *w, const char *b0, const char *b1, int32_t flag, AM2_Rect box);

#define orig_panel_ctor  ((AM2_PanelCtorFn)AM2_IMAGE(ADDR_PANEL_CTOR))
#define orig_typer_ctor  ((AM2_TyperCtorFn)AM2_IMAGE(ADDR_TYPER_CTOR))
#define orig_multisprite_ctor \
    ((AM2_MultiSpriteCtorFn)AM2_IMAGE(ADDR_MULTISPRITE_CTOR))

/* The four bitmaps every one of the three uses, and the one string. */
#define AM2_BMP_OK0    0x00487044u
#define AM2_BMP_OK1    0x00487058u
#define AM2_BMP_OK2    0x0048706Cu
#define AM2_BMP_CAN0   0x00486DECu
#define AM2_BMP_CAN1   0x00486E04u
#define AM2_BMP_CAN2   0x00486E1Cu
#define AM2_BMP_RED0   0x00487178u
#define AM2_BMP_RED1   0x0048718Cu

static AM2_Widget *MakeButton(int32_t left, int32_t top, uint32_t b0,
                              uint32_t b1, uint32_t b2, uint32_t handler)
{
    AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    AM2_Rect    box;

    if (!btn)
        return (AM2_Widget *)0;
    RectSet(&box, left, top, 0x51, 0x20);
    return orig_button_ctor(btn, (const char *)AM2_IMAGE(b0),
                            (const char *)AM2_IMAGE(b1),
                            (const char *)AM2_IMAGE(b2), 1, box,
                            (void (__cdecl *)(AM2_Widget *))AM2_IMAGE(handler),
                            0);
}

static AM2_Widget *ConfirmDialogBuild(AM2_Widget *w, const char *bmp,
                                      uint32_t vtable, uint32_t panelBmp,
                                      uint32_t okHandler, uint32_t message,
                                      uint32_t cancelHandler)
{
    AM2_Widget *panel;
    AM2_Widget *text;
    AM2_Widget *dot;
    AM2_Rect    box;

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(vtable);
    SetGameDir((const char *)AM2_IMAGE(ADDR_STR_ALPINE));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x98, 0x1A7, 0xB0);
        panel = orig_panel_ctor(panel, (const char *)AM2_IMAGE(panelBmp), 0,
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
        text = orig_typer_ctor(text, box, (const char *)AM2_IMAGE(message));
    }
    WidgetAddChild(panel, text);

    dot = (AM2_Widget *)orig_operator_new(AM2_MULTISPRITE_SIZE);
    if (dot) {
        RectSet(&box, 0x23, 0x95, 0x11, 0x10);
        dot = orig_multisprite_ctor(dot, (const char *)AM2_IMAGE(AM2_BMP_RED0),
                                    (const char *)AM2_IMAGE(AM2_BMP_RED1), 1,
                                    box);
    }
    WidgetAddChild(panel, dot);

    *(AM2_Widget **)((uint8_t *)text + TYPER_OFF_BLINKER) = dot;
    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(cancelHandler);
    return w;
}

AM2_Widget *__attribute__((thiscall)) QuitDialogConstruct(AM2_Widget *w,
                                                          const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_QUIT_DIALOG, 0x0048B76C,
                              ADDR_ON_QUIT_OK, 0x0048B74C, ADDR_ON_MENU_BACK);
}

AM2_Widget *__attribute__((thiscall)) ReplayDialogConstruct(AM2_Widget *w,
                                                            const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_REPLAY_DIALOG, 0x0048B7B0,
                              ADDR_ON_REPLAY_OK, 0x0048B780,
                              ADDR_ON_MENU_BACK);
}

AM2_Widget *__attribute__((thiscall)) DelPlayerDialogConstruct(AM2_Widget *w,
                                                               const char *bmp)
{
    return ConfirmDialogBuild(w, bmp, VTABLE_DELPLAYER_DIALOG, 0x0048B9C4,
                              ADDR_ON_DELPLAYER_OK, 0x0048B984,
                              ADDR_ON_DELPLAYER_CANCEL);
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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ListBoxCtorFn)(
    AM2_Widget *w, AM2_Rect box, void *rows, int32_t a, int32_t b, int32_t c);

#define orig_listbox_ctor ((AM2_ListBoxCtorFn)AM2_IMAGE(ADDR_LISTBOX_CTOR))
#define g_difficulty      (*(const int32_t *)(uintptr_t)ADDR_DIFFICULTY)

AM2_Widget *__attribute__((thiscall)) DifficultyDialogConstruct(
    AM2_Widget *w, const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *dot;
    void       *rows;
    AM2_Rect    box;

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_DIFFICULTY_DIALOG);

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x98, 0x1A7, 0xB0);
        panel = orig_panel_ctor(panel,
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
        list = orig_listbox_ctor(list, box, rows, 0, 0, 1);
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
        dot = orig_multisprite_ctor(dot, (const char *)AM2_IMAGE(AM2_BMP_RED0),
                                    (const char *)AM2_IMAGE(AM2_BMP_RED1), 1,
                                    box);
    }
    WidgetAddChild(panel, dot);
    *(AM2_Widget **)((uint8_t *)*(AM2_Widget **)((uint8_t *)w + DLG_OFF_LIST)
                     + LIST_OFF_BLINKER) = dot;

    WidgetAddChild(panel, MakeButton(0x149, 0x38, AM2_BMP_OK0, AM2_BMP_OK1,
                                     AM2_BMP_OK2, ADDR_ON_DIFFICULTY_OK));
    WidgetAddChild(panel, MakeButton(0x149, 0x61, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, ADDR_ON_DIALOG_CANCEL));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(ADDR_ON_DIALOG_CANCEL);
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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_KeyRowCtorFn)(
    AM2_Widget *w, int32_t nameIndex, const char *caption, AM2_Rect box,
    int32_t flag, int32_t ink, int32_t inkFocus, int32_t bg0, int32_t bg1);

#define orig_keyrow_ctor ((AM2_KeyRowCtorFn)AM2_IMAGE(ADDR_KEYROW_CTOR))
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

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_CONTROLS_DIALOG);
    rows = (AM2_Widget **)((uint8_t *)w + KEYROW_PARENT_ROWS);

    while (binding < (const uint8_t *)AM2_IMAGE(ADDR_KEY_BINDINGS)
                     + KEYROW_ROW_COUNT * 2) {
        int32_t     index = KeyNameIndexOf(*binding);
        AM2_Widget *row   = (AM2_Widget *)orig_operator_new(AM2_KEYROW_SIZE);

        if (row) {
            RectSet(&box, place[0], place[1], AM2_KEYROW_WIDTH,
                    AM2_KEYROW_HEIGHT);
            row = orig_keyrow_ctor(row, index,
                                   *(const char *const *)(names + index * 8
                                                          + 4),
                                   box, 1, g_whiteInk, g_hiliteColour,
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
                    ADDR_ON_CONTROLS_OK);
    WidgetAddChild(w, ok);
    ((AM2_WidgetFocusFn *)ok->vtable)[WIDGET_VSLOT_FOCUS](ok, 0);

    WidgetAddChild(w, MakeButton(0x218, 0xDF, AM2_BMP_DEFAULT0,
                                 AM2_BMP_DEFAULT1, AM2_BMP_DEFAULT2,
                                 ADDR_ON_CONTROLS_DEFAULT));
    WidgetAddChild(w, MakeButton(0x218, 0x112, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                 AM2_BMP_CAN2, ADDR_ON_CONTROLS_CANCEL));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(ADDR_ON_CONTROLS_CANCEL);
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
 * the screenshots differ by. A non-host gets `unknown4C` set on every box, so
 * none can be focused; a non-host gets CANCEL alone, at the OK position; and
 * the group pass that disables a group whose header is unticked runs for the
 * host only.
 *
 * `ret 0x2C` on the checkbox constructor is 44 bytes -- four bitmaps, sixteen
 * of rectangle, a flag, the caption and the handler. */
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_CheckBoxCtorFn)(
    AM2_Widget *w, const char *b0, const char *b1, const char *b2,
    const char *b3, AM2_Rect box, int32_t flag, const char *caption,
    void (__cdecl *handler)(AM2_Widget *));

#define orig_checkbox_ctor ((AM2_CheckBoxCtorFn)AM2_IMAGE(ADDR_CHECKBOX_CTOR))

#define OPT_REC(base, off) (*(const int32_t *)((base) + (off)))

AM2_Widget *__attribute__((thiscall)) MpOptionsConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    const uint8_t *rec;
    AM2_Widget   **boxes = (AM2_Widget **)((uint8_t *)w + OPTION_PARENT_BOXES);
    const uint8_t *comm  = g_commObject;
    int32_t        host  = *(const int32_t *)(comm + COMM_OFF_IS_HOST);
    AM2_Rect       box;

    orig_screen_base_ctor(w, bmp, 1);
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
            cb = orig_checkbox_ctor(cb,
                                    (const char *)AM2_IMAGE(AM2_BMP_CHECK0),
                                    (const char *)AM2_IMAGE(AM2_BMP_CHECK1),
                                    (const char *)AM2_IMAGE(AM2_BMP_CHECK2),
                                    (const char *)AM2_IMAGE(AM2_BMP_CHECK3),
                                    box, 0,
                                    *(const char *const *)(rec + 0x20),
                                    (void (__cdecl *)(AM2_Widget *))
                                    AM2_IMAGE(ADDR_OPTIONS_SYNC_GROUP));
        }
        boxes[OPT_REC(rec, AM2_OPTION_OFF_WIDGET)] = cb;
        cb->flag3C = 0;

        mask = OPT_REC(rec, AM2_OPTION_OFF_WHICH) ? g_gameOverFlags
                                                  : g_gameSetting22C;
        *((uint8_t *)cb + CHECK_OFF_TICKED) = (mask & bit) != 0;

        WidgetAddChild(w, cb);
        if (!host)
            cb->unknown4C = 1;
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
                boxes[i]->unknown4C =
                    (*((const uint8_t *)head + CHECK_OFF_TICKED) == 0);
        }

        {
            AM2_Widget *ok = MakeButton(0x219, 0xAE, AM2_BMP_OK0, AM2_BMP_OK1,
                                        AM2_BMP_OK2, ADDR_OPTIONS_APPLY);

            WidgetAddChild(w, ok);
            w->focusedChild = ok;
            ok->flag44 = 1;
        }
        WidgetAddChild(w, MakeButton(0x219, 0xE0, AM2_BMP_DEFAULT0,
                                     AM2_BMP_DEFAULT1, AM2_BMP_DEFAULT2,
                                     ADDR_OPTIONS_DEFAULTS));
        WidgetAddChild(w, MakeButton(0x219, 0x112, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, ADDR_OPTIONS_REQUEST));
    } else {
        AM2_Widget *cancel = MakeButton(0x219, 0xAE, AM2_BMP_CAN0,
                                        AM2_BMP_CAN1, AM2_BMP_CAN2,
                                        ADDR_OPTIONS_REQUEST);

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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ScrollBarCtorFn)(
    AM2_Widget *w, AM2_Rect box, AM2_Widget *parent, int32_t max);

#define g_volumeAtZero (*(const int32_t *)(uintptr_t)ADDR_VOLUME_AT_ZERO)
#define g_streamVolume  (*(const int32_t *)(uintptr_t)ADDR_STREAM_VOLUME)
#define g_voiceVolume   (*(const int32_t *)(uintptr_t)ADDR_VOLUME_VOICE)

/* The base constructor again, with the flag as a REAL argument rather than
 * the literal 1 every other screen passes -- this one is handed its caller's.
 * Same address, so the same detour; a second typedef only because the
 * signature differs. */
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ScreenBaseCtor2Fn)(
    AM2_Widget *w, const char *bmp, int32_t flag);
#define orig_screen_base_ctor2 \
    ((AM2_ScreenBaseCtor2Fn)AM2_IMAGE(ADDR_SCREEN_BASE_CTOR))

#define orig_scrollbar_ctor \
    ((AM2_ScrollBarCtorFn)AM2_IMAGE(ADDR_SCROLLBAR_CTOR))

static AM2_Widget *MakeVolumeBar(AM2_Widget *parent, int32_t x, int32_t y,
                                 int32_t volume, uint32_t onChange)
{
    AM2_Widget *bar = (AM2_Widget *)orig_operator_new(AM2_SCROLLBAR_SIZE);
    AM2_Rect    box;
    int32_t     pos;
    int32_t     travel;
    uint8_t    *b;

    if (bar) {
        RectSet(&box, x, y, 0xBA, 0x15);
        bar = orig_scrollbar_ctor(bar, box, parent, 0x92);
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
    *(uint32_t *)(b + SCROLLBAR_OFF_ONCHANGE) = (uint32_t)AM2_IMAGE(onChange);
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

    orig_screen_base_ctor2(w, bmp, flag);
    w->vtable = (void *)AM2_IMAGE(VTABLE_AUDIO_DIALOG);

    if (g_gameState == AM2_STATE_MISSION) {
        w->flag44 = 1;
        parent = w;
    } else {
        AM2_Widget *panel =
            (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);

        if (panel) {
            RectSet(&box, offX, offY, 0x16E, 0xED);
            panel = orig_panel_ctor(panel,
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
                            g_volumeAtZero, ADDR_ON_VOLUME_EFFECTS);
    parent->focusedChild = bars[0];
    bars[1] = MakeVolumeBar(parent, offX + 0x25, offY + 0x7D,
                            g_streamVolume, ADDR_ON_VOLUME_MUSIC);
    bars[2] = MakeVolumeBar(parent, offX + 0x25, offY + 0xC2,
                            g_voiceVolume, ADDR_ON_VOLUME_VOICE);

    /* Kept so CANCEL can put them back. */
    saved[0] = g_volumeAtZero;
    saved[1] = g_streamVolume;
    saved[2] = g_voiceVolume;

    WidgetAddChild(parent, MakeButton(offX + 0x110, offY + 0x5E, AM2_BMP_OK0,
                                      AM2_BMP_OK1, AM2_BMP_OK2,
                                      ADDR_ON_AUDIO_OK));
    WidgetAddChild(parent, MakeButton(offX + 0x110, offY + 0x8B, AM2_BMP_CAN0,
                                      AM2_BMP_CAN1, AM2_BMP_CAN2,
                                      ADDR_ON_AUDIO_CANCEL));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(ADDR_ON_AUDIO_CANCEL);
    return w;
}

/* This dialog's buttons are 0x4E wide where every other screen's are 0x51. */
static AM2_Widget *MakeWideButton(int32_t left, int32_t top, uint32_t b0,
                                  uint32_t b1, uint32_t b2, uint32_t handler)
{
    AM2_Widget *btn = (AM2_Widget *)orig_operator_new(AM2_BUTTON_SIZE);
    AM2_Rect    box;

    if (!btn)
        return (AM2_Widget *)0;
    RectSet(&box, left, top, 0x4E, 0x20);
    return orig_button_ctor(btn, (const char *)AM2_IMAGE(b0),
                            (const char *)AM2_IMAGE(b1),
                            (const char *)AM2_IMAGE(b2), 1, box,
                            (void (__cdecl *)(AM2_Widget *))AM2_IMAGE(handler),
                            0);
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
 * and two zeroes. The handler is ADDR_HOST_BATTLE -- the same function the OK
 * button gets -- so RETURN in either field starts the battle.
 *
 * The dot is stored ON THE EDIT BOX at 0x0070 and added to the PANEL, and
 * each box takes the accepted-character set from ADDR_EDIT_CHARSET_PTR: a
 * whitelist of letters, digits, space and punctuation rather than a length
 * limit. */
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_EditCtorFn)(
    AM2_Widget *w, char *buf, int32_t maxChars, AM2_Rect box, int32_t flag,
    int32_t ink, int32_t ink2, int32_t ink3,
    void (__cdecl *handler)(AM2_Widget *), int32_t a, int32_t b);

#define orig_edit_ctor ((AM2_EditCtorFn)AM2_IMAGE(ADDR_EDIT_CTOR))
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
        edit = orig_edit_ctor(edit, buf, AM2_EDIT_MAX_CHARS, box, 1,
                              g_hiliteColour, g_colourBelowBg,
                              g_backgroundColour,
                              (void (__cdecl *)(AM2_Widget *))
                              AM2_IMAGE(ADDR_HOST_BATTLE), 0, 0);
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
        dot = orig_multisprite_ctor(dot,
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

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_BATTLE_NAME_DLG);

    strcpy((char *)w + 0x64,
               (const char *)AM2_IMAGE(ADDR_SAVED_BATTLE_NAME));
    strcpy((char *)w + 0x84,
               (const char *)AM2_IMAGE(ADDR_SAVED_PLAYER_NAME));
    SetGameDir((const char *)AM2_IMAGE(ADDR_DIR_SCRATCH));

    panel = (AM2_Widget *)orig_operator_new(AM2_PANEL_SIZE);
    if (panel) {
        RectSet(&box, 0x6C, 0x8F, 0x1A7, 0xC1);
        panel = orig_panel_ctor(panel,
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
                                         ADDR_HOST_BATTLE));
    WidgetAddChild(panel, MakeWideButton(0x14A, 0x6D, AM2_BMP_CAN0,
                                         AM2_BMP_CAN1, AM2_BMP_CAN2,
                                         ADDR_ON_MENU_BACK));
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
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_ArrowBarCtorFn)(
    AM2_Widget *w, AM2_Rect box, AM2_Widget *list, const char *b0,
    const char *b1, int32_t max, int32_t zero);

#define orig_arrowbar_ctor ((AM2_ArrowBarCtorFn)AM2_IMAGE(ADDR_ARROWBAR_CTOR))

AM2_Widget *__attribute__((thiscall)) CommPanelConstruct(AM2_Widget *w,
                                                         const char *bmp)
{
    AM2_Widget *panel;
    AM2_Widget *list;
    AM2_Widget *bar;
    void       *rows;
    AM2_Rect    box;

    orig_screen_base_ctor(w, bmp, 1);
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
        panel = orig_panel_ctor(panel,
                                (const char *)AM2_IMAGE(ADDR_STR_COMMPANEL_BMP),
                                0, box);
    }
    WidgetAddChild(w, panel);
    panel->flag44 = 1;

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x2A, 0x44, 0x11F, 0xAA);
        list = orig_listbox_ctor(list, box,
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
        bar = orig_arrowbar_ctor(bar, box, panel,
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
                                     ADDR_START_SELECTED_GAME));
    WidgetAddChild(panel, MakeButton(0x19C, 0x94, AM2_BMP_CAN0, AM2_BMP_CAN1,
                                     AM2_BMP_CAN2, ADDR_ON_MENU_BACK));
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

#define orig_findfirst  ((AM2_FindFirstFn)AM2_IMAGE(ADDR_CRT_FINDFIRST))
#define orig_findnext   ((AM2_FindNextFn)AM2_IMAGE(ADDR_CRT_FINDNEXT))
#define orig_findclose  ((AM2_FindCloseFn)AM2_IMAGE(ADDR_CRT_FINDCLOSE))
#define orig_read_campaign \
    ((AM2_ReadCampaignFn)AM2_IMAGE(ADDR_READ_CAMPAIGN_FILE))
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

    orig_screen_base_ctor(w, bmp, 1);
    w->vtable = (void *)AM2_IMAGE(VTABLE_SELECT_PLAYER);
    orig_read_campaign();
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
        panel = orig_panel_ctor(panel, (const char *)
                                AM2_IMAGE(ADDR_STR_SELECTPLAYER_BMP), 0, box);
    }
    WidgetAddChild(w, panel);
    w->focusedChild = panel;
    panel->flag44 = 1;

    list = (AM2_Widget *)orig_operator_new(AM2_LISTBOX_SIZE);
    if (list) {
        RectSet(&box, 0x29, 0x43, 0x96, 0xAB);
        list = orig_listbox_ctor(list, box,
                                 *(void **)((uint8_t *)w
                                            + COMMPANEL_OFF_LIST),
                                 (int32_t)AM2_IMAGE(ADDR_SELECT_PLAYER_ROW),
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
        bar = orig_arrowbar_ctor(bar, box, panel,
                                 (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR0),
                                 (const char *)AM2_IMAGE(AM2_BMP_SCROLLBAR1),
                                 0x92, 0);
    }
    WidgetAddChild(panel, bar);
    *(AM2_Widget **)((uint8_t *)list + LIST_OFF_ARROWBAR) = bar;
    *(AM2_Widget **)((uint8_t *)bar + ARROWBAR_OFF_LIST) = list;

    WidgetAddChild(panel, MakeButton(0x123, 0x44, AM2_BMP_RECRUIT0,
                                     AM2_BMP_RECRUIT1, AM2_BMP_RECRUIT2,
                                     ADDR_ON_RECRUIT));
    WidgetAddChild(panel, MakeButton(0x123, 0x6B, AM2_BMP_SELECT0,
                                     AM2_BMP_SELECT1, AM2_BMP_SELECT2,
                                     ADDR_ON_SELECT_PLAYER));
    WidgetAddChild(panel, MakeButton(0x123, 0x92, AM2_BMP_DELETE0,
                                     AM2_BMP_DELETE1, AM2_BMP_DELETE2,
                                     ADDR_ON_DELETE_PLAYER));
    WidgetAddChild(panel, MakeButton(0x123, 0xB9, AM2_BMP_BACK0,
                                     AM2_BMP_BACK1, AM2_BMP_BACK2,
                                     ADDR_ON_MENU_BACK));

    *(uint32_t *)((uint8_t *)w + DLG_OFF_ESCAPE) =
        (uint32_t)AM2_IMAGE(ADDR_ON_MENU_BACK);
    return w;
}

int widget_install(void)
{
    int rc = 0;

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
    rc |= patch_replace(ADDR_OPEN_CD_PROMPT, (const void *)OpenCdPrompt,
                        "OpenCdPrompt", 0);
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
    rc |= patch_replace(ADDR_KEY_NAME_INDEX_OF, (const void *)KeyNameIndexOf,
                        "KeyNameIndexOf", 1);
    rc |= patch_replace(ADDR_SESSION_CTOR, (const void *)RecordCtor,
                        "RecordCtor", 2);
    rc |= patch_replace(ADDR_LIST_ROWS_CLEANUP, (const void *)RecordResetAlias,
                        "RecordResetAlias", 1);
    rc |= patch_replace(ADDR_DIALOG_DESTRUCT, (const void *)DialogDestruct,
                        "DialogDestruct", 1);
    rc |= patch_replace(ADDR_DIALOG_DELETE, (const void *)DialogDelete,
                        "DialogDelete", 1);
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
    return rc;
}
