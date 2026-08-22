/* The menu widget hierarchy -- see widget.h for the shape of the class tree.
 *
 * This module is the first piece of it. What is here is one painter; the
 * thirty-four constructors, the four other virtuals per class and the
 * containers that own them are all still original and reached by address. */

#include "widget.h"
#include "surface.h"
#include "../rect.h"
#include "../image.h"
#include "../../inject/patch.h"

#include <stdint.h>
#include <stddef.h>

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
    w->unknown2C   = 0;
    w->nextSibling = (AM2_Widget *)0;
    w->unknown34   = 0;
    w->unknown38   = 0;
    w->flag3C      = 1;
    /* 0x0040 is deliberately not written -- the original leaves it, and
     * whatever it is arrives from the allocator. */
    w->flag44      = 0;
    w->unknown48   = 0;
    w->unknown4C   = 0;
    w->flag50      = 1;
    w->unknown54   = 0;
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
