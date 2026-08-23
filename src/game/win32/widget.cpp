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
#define orig_mp_options_ctor  ((AM2_ScreenCtorFn)AM2_IMAGE(ADDR_MP_OPTIONS_CTOR))
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
    OpenScreen(AM2_MP_OPTIONS_SIZE, orig_mp_options_ctor,
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_SELECT_PLAYER_CTOR),
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_BATTLE_NAME_CTOR),
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_OPTIONS_MENU_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenControls(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_CONTROLS_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_CONTROLS_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_CONTROLS_BMP));
}

void __cdecl OpenDifficulty(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_DIFFICULTY_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_DIFFICULTY_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenQuitConfirm(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_QUIT_CONFIRM_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_QUIT_CONFIRM_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenReplayPrompt(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_REPLAY_PROMPT_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_REPLAY_PROMPT_CTOR),
               (const char *)AM2_IMAGE(ADDR_STR_SCREEN_BMP));
}

void __cdecl OpenDeletePlayer(void)
{
    CloseCurrentScreen();
    OpenScreen(AM2_DELETE_PLAYER_SIZE,
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_DELETE_PLAYER_CTOR),
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
               (AM2_ScreenCtorFn)AM2_IMAGE(ADDR_COMM_PANEL_CTOR),
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
                    (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_AUDIO_OPTIONS_CTOR),
                    (const char *)AM2_IMAGE(ADDR_STR_AUDIO_BMP), 0);
    } else {
        OpenScreen2(AM2_AUDIO_OPTIONS_SIZE,
                    (AM2_ScreenCtor2Fn)AM2_IMAGE(ADDR_AUDIO_OPTIONS_CTOR),
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
