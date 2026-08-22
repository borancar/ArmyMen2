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
    int32_t  unknown24;             /* 0x0024 */
    struct AM2_Widget *parent;      /* 0x0028  null for a top-level widget */
} AM2_Widget;

/* The label's own fields, offsets from the widget base. The paper colour is
 * NOT at the same offset in the edit box, which clears with the byte at 0x0066
 * -- the subclasses lay their own tails out independently, so these are the
 * label's and nothing else's. Ink and paper are adjacent bytes. */
#define LABEL_OFF_TEXT   0x58       /* const char * */
#define LABEL_OFF_FONT   0x5C       /* int32_t, index into the font table */
#define LABEL_OFF_INK    0x60       /* uint8_t, palette index the text is drawn in */
#define LABEL_OFF_PAPER  0x61       /* uint8_t, palette index the background is cleared to */

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

int widget_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_WIDGET_H */
