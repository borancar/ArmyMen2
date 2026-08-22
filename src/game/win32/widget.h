#ifndef AM2_WIDGET_H
#define AM2_WIDGET_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The menu widget hierarchy.
 *
 * The image carries an array of thirty-four FIVE-slot vtables running from
 * 0x0046FAB8 to 0x0046FD38, each referenced by exactly one constructor and one
 * destructor. So the menus are a class tree with five virtuals, and slot 1 is
 * the painter: the vtable at 0x0046FCAC has 0x00454F00 there and the one four
 * classes earlier, at 0x0046FC98, has 0x00454D20 -- the edit box, which is the
 * class that installs g_charHandler into 0x005125B8 when it takes focus.
 *
 * Only the base layout is established, and only because 0x00453BF0 walks it:
 * an offset within the parent, a size, the absolute rectangle those two
 * produce, and the parent pointer. Everything between 0x0024 and 0x0057 is
 * unread, so this struct stops at the parent rather than inventing names for
 * the gap, and the label's own fields are reached by offset below.
 */
typedef struct AM2_Widget {
    void    *vtable;                /* 0x0000 */
    int32_t  x;                     /* 0x0004  offset within the parent */
    int32_t  y;                     /* 0x0008 */
    int32_t  w;                     /* 0x000C */
    int32_t  h;                     /* 0x0010 */
    RECT     rect;                  /* 0x0014  absolute; WidgetScreenRect's output */
    struct AM2_Widget *firstChild;  /* 0x0024  head of the child list */
    struct AM2_Widget *parent;      /* 0x0028  null for a top-level widget */
    int32_t  unknown2C;             /* 0x002C */
    struct AM2_Widget *nextSibling; /* 0x0030  next child of the same parent */
    int32_t  unknown34;             /* 0x0034 */
    int32_t  unknown38;             /* 0x0038 */
    int32_t  flag3C;                /* 0x003C  constructed as 1 */
    int32_t  unknown40;             /* 0x0040  NOT written by the constructor */
    int32_t  flag44;                /* 0x0044  constructed as 0 */
    int32_t  unknown48;             /* 0x0048 */
    int32_t  unknown4C;             /* 0x004C */
    int32_t  flag50;                /* 0x0050  constructed as 1 */
    int32_t  unknown54;             /* 0x0054 */
} AM2_Widget;

/* The base class's vtable, stored by its constructor and restored by its
 * destructor. It is the twentieth of the thirty-three. */
#define VTABLE_WIDGET_BASE  0x0046FC20u
/* The label's vtable, twenty-sixth of the thirty-three. */
#define VTABLE_LABEL        0x0046FCACu

/* The label's own fields, offsets from the widget base. The paper colour is
 * NOT at the same offset in the edit box, which clears with the byte at 0x0066
 * -- the subclasses lay their own tails out independently, so these are the
 * label's and nothing else's. Ink and paper are adjacent bytes. */
#define LABEL_OFF_TEXT   0x58       /* const char * */
#define LABEL_OFF_FONT   0x5C       /* int32_t, index into the font table */
#define LABEL_OFF_INK    0x60       /* uint8_t, palette index the text is drawn in */
#define LABEL_OFF_PAPER  0x61       /* uint8_t, palette index the background is cleared to */

/* The five virtuals, in slot order. Reading the whole array at once is what
 * makes them nameable: slot 3 is 0x00454070 in 30 of the 33 classes and slot 4
 * is 0x00453FF0 in 29, so those two are the base's and the handful of others
 * are overrides.
 *
 *   0  destructor -- every class has its own; none is shared
 *   1  paint      -- 0x00454BA0 in 18, LabelDraw is one of the overrides
 *   2  click      -- 0x00454BD0 in 17
 *   3  focus      -- 0x00454070 in 30
 *   4  repaint    -- 0x00453FF0 in 29
 */
#define WIDGET_VSLOT_DTOR    0
#define WIDGET_VSLOT_PAINT   1
#define WIDGET_VSLOT_CLICK   2
#define WIDGET_VSLOT_FOCUS   3
#define WIDGET_VSLOT_REPAINT 4

/* Slot 1's signature: the clip rectangle by value, thiscall. */
typedef void (__attribute__((thiscall)) *AM2_WidgetPaintFn)(AM2_Widget *w,
                                                            RECT clip);

/* Original: 0x00453FF0, thiscall, slot 4 of 29 classes. Repaint this widget.
 *
 * Three things it does, in the original's order. The dirty flag at 0x0044 is
 * cleared FIRST and unconditionally, before anything is decided. Then, if
 * 0x0048 is set, it walks UP the parent chain for the first ancestor with
 * 0x0038 set and paints that one instead. And whichever object it ends up
 * painting, the clip rectangle is always THIS widget's -- so deferring to an
 * ancestor means "redraw the container, but only over me".
 *
 * The walk stops at the first match and falls through to painting self if it
 * reaches the top without one. */
void __attribute__((thiscall)) WidgetRepaint(AM2_Widget *w);

/* Original: 0x00453BF0, thiscall, 33 direct callers -- the shared base helper
 * of the whole hierarchy, and the reason the layout above is known rather than
 * guessed. Turns the widget's offset within its parent into the absolute
 * rectangle everything else clips and draws against.
 *
 * A widget with no parent is placed at its offset directly, which is the same
 * arithmetic against an origin of (0,0) and is written out separately in the
 * original rather than folded. Reproduced as two branches for that reason.
 *
 * Note what it does NOT do: it never walks further than one level up. The
 * parent's own rectangle is read as already correct, so a container has to
 * have been placed before its children are, and nothing here enforces that. */
void __attribute__((thiscall)) WidgetScreenRect(AM2_Widget *w);

/* Original: 0x00454F00, vtable slot 1 of the class constructed at 0x00454E70.
 * Paints a static text label: clip against the caller's rectangle, clear the
 * background, then draw the string.
 *
 * Takes its clip rectangle BY VALUE -- thiscall with sixteen bytes of stack
 * argument, `ret 0x10` -- and passes the CLIPPED rectangle down to the text
 * painter while drawing from the label's own unclipped origin.
 *
 * Note the order the original uses and this keeps: the background is cleared
 * BEFORE the surface is locked, because ClearRegion blits rather than writing
 * pixels itself, and the early return on a failed lock leaves the cleared
 * background on screen with no text over it.
 *
 * Reached both ways: the vtable slot at 0x0046FCB0, and one direct call from
 * 0x00450D31, which is the containing panel painting its own label. The
 * constructor takes (text, x, y, w, h, font, ink, paper) and is called from
 * 0x00430530 -- the colour/slot dialog -- and 0x00450C50, the panel under
 * OPTIONS -> CONTROLS. That second one is how it is exercised here: the
 * CONTROLS dialog is 78,174 calls of it, and every caption on that screen,
 * from "SARGE CONTROLS" to "EXIT VEHICLE", comes out of this function. */
void __attribute__((thiscall)) LabelDraw(AM2_Widget *w, RECT clip);

/* Original: 0x00453B00, thiscall, the base constructor every one of the
 * thirty-three classes chains to. Zeroes both rectangles through RectSet and
 * clears the tree links, then leaves 0x0040 alone -- the one field in the
 * range it touches that it does not write. Returns `this`, as an i386 MSVC
 * constructor does. */
AM2_Widget *__attribute__((thiscall)) WidgetConstruct(AM2_Widget *w);

/* Original: 0x00454E70, thiscall. The label's constructor: base first, then
 * its own four fields and the four rectangle components, then place itself.
 * The argument order is the original's and is not the field order --
 * (text, x, y, w, h, font, ink, paper). */
AM2_Widget *__attribute__((thiscall)) LabelConstruct(AM2_Widget *w,
                                                     const char *text,
                                                     int32_t x, int32_t y,
                                                     int32_t width,
                                                     int32_t height,
                                                     int32_t font,
                                                     int32_t ink,
                                                     int32_t paper);

int widget_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_WIDGET_H */
