/* The menu widget hierarchy -- see widget.h for the shape of the class tree.
 *
 * This module is the first piece of it. What is here is one painter; the
 * thirty-four constructors, the four other virtuals per class and the
 * containers that own them are all still original and reached by address. */

#include "widget.h"
#include "surface.h"
#include "../rect.h"
#include "sprite.h"
#include "frame.h"
#include "audio.h"
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
#define orig_is_key_down  ((AM2_KeyQueryFn)(uintptr_t)ADDR_IS_KEY_DOWN)
#define orig_key_changed  ((AM2_KeyQueryFn)(uintptr_t)ADDR_KEY_CHANGED)
#define orig_consume_key  ((AM2_ConsumeKeyFn)(uintptr_t)ADDR_CONSUME_KEY)
#define orig_key_pressed  ((AM2_KeyQueryFn)(uintptr_t)ADDR_KEY_PRESSED_FN)

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

        if (orig_key_changed(dik) && orig_is_key_down(dik))
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

void __attribute__((thiscall)) TogglePaint(AM2_Widget *w, RECT clip)
{
    uint8_t *self = (uint8_t *)w;

    if (*(const int32_t *)(self + TOGGLE_OFF_STATE))
        w->sprite = *(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_ON);
    else
        w->sprite = *(AM2_Sprite **)(self + TOGGLE_OFF_SPRITE_OFF);

    WidgetPaint(w, clip);
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
            else if (orig_is_key_down(AM2_DIK_RETURN))
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
    if (orig_key_pressed(AM2_DIK_UP)) {
        if (w->focusedChild)
            WidgetFocusPrev(w->focusedChild, 1);
        orig_consume_key(AM2_DIK_UP);
    }
    if (orig_key_pressed(AM2_DIK_DOWN)) {
        if (w->focusedChild)
            WidgetFocusNext(w->focusedChild, 1);
        orig_consume_key(AM2_DIK_DOWN);
    }
    if (orig_key_pressed(AM2_DIK_TAB)) {
        if (w->focusedChild)
            WidgetFocusNext(w->focusedChild, 1);
        orig_consume_key(AM2_DIK_TAB);
    }

    /* Either activation key CHANGING repaints the focused child, which is how
     * a button shows itself going down and coming back up. No consume here --
     * the release blocks below want to see the same edge. */
    if (orig_key_changed(AM2_DIK_SPACE) || orig_key_changed(AM2_DIK_RETURN)) {
        focus = w->focusedChild;
        ((AM2_WidgetPaintFn *)focus->vtable)[WIDGET_VSLOT_PAINT](focus,
                                                                 focus->rect);
    }

    if (!orig_is_key_down(AM2_DIK_SPACE) && orig_key_changed(AM2_DIK_SPACE)) {
        orig_consume_key(AM2_DIK_SPACE);
        focus = w->focusedChild;
        if (focus->activate)
            focus->activate(focus);
    }
    if (!orig_is_key_down(AM2_DIK_RETURN) && orig_key_changed(AM2_DIK_RETURN)) {
        orig_consume_key(AM2_DIK_RETURN);
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
        && !orig_is_key_down(AM2_DIK_ESCAPE)
        && orig_key_changed(AM2_DIK_ESCAPE)) {
        orig_consume_key(AM2_DIK_ESCAPE);
        cancel(w);
        return;
    }
    WidgetUpdate(w);
}

/* 0x004274D0. Still original: it is two globals and a rep movsd, and the
 * buffers are the input layer's rather than this module's. */
typedef void (__cdecl *AM2_LatchKeysFn)(void);
#define orig_latch_key_state ((AM2_LatchKeysFn)(uintptr_t)ADDR_LATCH_KEY_STATE)

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
    orig_latch_key_state();

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
    rc |= patch_replace(ADDR_MULTI_SPRITE_PAINT,
                        (const void *)MultiSpritePaint,
                        "MultiSpritePaint", 1);
    rc |= patch_replace(ADDR_TOGGLE_PAINT, (const void *)TogglePaint,
                        "TogglePaint", 1);
    rc |= patch_replace(ADDR_LIST_TAKE_FOCUS, (const void *)ListTakeFocus,
                        "ListTakeFocus", 1);
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
