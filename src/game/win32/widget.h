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
    struct AM2_Widget *prevSibling; /* 0x002C  the sibling walk runs both ways;
                                     * 0x00453E20 follows this one backwards */
    struct AM2_Widget *nextSibling; /* 0x0030  next child of the same parent */
    struct AM2_Widget *focusedChild;/* 0x0034  which child has the focus.
                                     * A pointer, established by
                                     * WidgetTakeFocus: it stores `this` and
                                     * `firstChild` into it and dispatches a
                                     * vtable slot on what it reads back. */
    struct AM2_Sprite *sprite;      /* 0x0038  the widget's own backdrop, or
                                     * null. WidgetPaint draws it and reads
                                     * its bounds; WidgetRepaint walks up for
                                     * the nearest ancestor that has one. */
    int32_t  flag3C;                /* 0x003C  constructed as 1. Set centres the
                                     * sprite in the widget; clear puts it at
                                     * the top left. */
    int32_t  unknown40;             /* 0x0040  NOT written by the constructor */
    int32_t  flag44;                /* 0x0044  constructed as 0 */
    int32_t  unknown48;             /* 0x0048 */
    int32_t  unknown4C;             /* 0x004C  set disqualifies from focus */
    int32_t  flag50;                /* 0x0050  constructed as 1; CLEAR
                                     * disqualifies from focus, so it reads as
                                     * "can be focused" -- but that is from the
                                     * two focus walkers only, and nothing has
                                     * been seen to clear it yet */
    void   (__cdecl *activate)(struct AM2_Widget *w);
                                    /* 0x0054  fired by SPACE or RETURN on the
                                     * focused child. Constructed null. */
} AM2_Widget;

/* The base class's vtable, stored by its constructor and restored by its
 * destructor. It is the twentieth of the thirty-three. */
#define VTABLE_WIDGET_BASE  0x0046FC20u
/* The label's vtable, twenty-sixth of the thirty-three. */
#define VTABLE_LABEL        0x0046FCACu
/* The edit box, twenty-fifth of the thirty-three. */
#define VTABLE_EDIT         0x0046FC98u

/* The label's own fields, offsets from the widget base. The paper colour is
 * NOT at the same offset in the edit box, which clears with the byte at 0x0066
 * -- the subclasses lay their own tails out independently, so these are the
 * label's and nothing else's. Ink and paper are adjacent bytes. */
#define LABEL_OFF_TEXT   0x58       /* const char * */
#define LABEL_OFF_FONT   0x5C       /* int32_t, index into the font table */
#define LABEL_OFF_INK    0x60       /* uint8_t, palette index the text is drawn in */
#define LABEL_OFF_PAPER  0x61       /* uint8_t, palette index the background is cleared to */

/* The focus-highlighting label's own fields. It keeps TWO colour pairs and
 * copies the applicable one into the plain label's ink and paper above before
 * delegating, which is why those two are rewritten every frame. */
#define FOCUSLABEL_OFF_INK         0x64   /* uint8_t, ink while not focused */
#define FOCUSLABEL_OFF_INK_FOCUS   0x65   /* uint8_t, ink while focused */
#define FOCUSLABEL_OFF_PAPER       0x66   /* uint8_t, paper while not focused */
#define FOCUSLABEL_OFF_PAPER_FOCUS 0x67   /* uint8_t, paper while focused */

/* The five virtuals, in slot order. Reading the whole array at once is what
 * makes them nameable: slot 3 is 0x00454070 in 30 of the 33 classes and slot 4
 * is 0x00453FF0 in 29, so those two are the base's and the handful of others
 * are overrides.
 *
 *   0  destructor -- every class has its own; none is shared
 *   1  paint      -- 0x00454BA0 in 18, LabelDraw is one of the overrides
 *   2  update     -- 0x00454BD0 in 17, over a base of 0x00453E80
 *   3  focus      -- 0x00454070 in 30
 *   4  repaint    -- 0x00453FF0 in 29
 *
 * Slot 2 was called "click" here for one commit, from a glance at 0x00454BD0
 * that saw a function pointer being called and stopped. Reading its callees
 * says otherwise: the three queries around it are IsKeyDown, KeyChanged and a
 * consume, the scancode is 1 -- ESCAPE -- and `!down && changed` is the key
 * being RELEASED. It is the per-frame update, and 0x00454BD0 is the override
 * that gives a dialog its cancel key before doing the ordinary thing. The
 * base at 0x00453E80 places the widget and recurses into its children, which
 * is why WidgetScreenRect runs a million and a half times.
 */
#define WIDGET_VSLOT_DTOR    0
#define WIDGET_VSLOT_PAINT   1
#define WIDGET_VSLOT_UPDATE  2
#define WIDGET_VSLOT_FOCUS   3
#define WIDGET_VSLOT_REPAINT 4

/* Slot 1's signature: the clip rectangle by value, thiscall. */
typedef void (__attribute__((thiscall)) *AM2_WidgetPaintFn)(AM2_Widget *w,
                                                            RECT clip);

/* Slot 0's signature: the MSVC scalar deleting destructor. Bit 0 of the flag
 * says "and free the storage"; the object is returned, as i386 MSVC does. */
typedef AM2_Widget *(__attribute__((thiscall)) *AM2_WidgetDeleteFn)(
    AM2_Widget *w, int32_t flags);

/* Original: 0x00453BC0, thiscall. The base destructor: restore the base
 * vtable, then destroy every child through ITS slot 0 with the free bit set,
 * walking the sibling chain. The next pointer is read before the child is
 * destroyed, which is what makes deleting from inside the walk safe.
 *
 * Children are freed; the widget itself is not. That is the deleting
 * destructor's job and it is the caller who decides. */
void __attribute__((thiscall)) WidgetDestruct(AM2_Widget *w);

/* Original: 0x00453BA0, thiscall, slot 0 of the base class. */
AM2_Widget *__attribute__((thiscall)) WidgetDelete(AM2_Widget *w,
                                                   int32_t flags);

/* Original: 0x00454EF0, thiscall. The label's destructor. It owns nothing --
 * the text it points at is not its to free -- so it is the vtable store and a
 * tail call to the base. */
void __attribute__((thiscall)) LabelDestruct(AM2_Widget *w);

/* Original: 0x00454ED0, thiscall, slot 0 of the label. */
AM2_Widget *__attribute__((thiscall)) LabelDelete(AM2_Widget *w,
                                                  int32_t flags);

/* Slot 4's signature. */
typedef void (__attribute__((thiscall)) *AM2_WidgetRepaintFn)(AM2_Widget *w);

/* Original: 0x00454070, thiscall, slot 3 of 30 classes. Move the focus within
 * this widget's parent to this widget, and answer nothing.
 *
 * The `announce` argument gates the visible half: without it the focus moves
 * silently, with it the menu click sound plays and this widget repaints
 * itself. Either way the previously focused sibling is repainted, because
 * that is what takes its highlight off.
 *
 * One branch is odd and is reproduced rather than tidied. When the parent had
 * NO focused child, the original sets the parent's focus to its FIRST CHILD
 * rather than to the widget that is taking focus -- so the very first call on
 * a fresh parent focuses whichever child was added first, not the caller.
 * That may well be deliberate, since a fresh dialog wants its first control
 * highlighted, but it is not what the function's name would lead you to
 * expect and nothing here should smooth it over.
 *
 * The input pair after it is PollInput followed by latching the key buffer,
 * which together mean "and do not let the keystroke that got us here be seen
 * again". */
void __attribute__((thiscall)) WidgetTakeFocus(AM2_Widget *w, int32_t announce);

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

/* Slot 3's signature. */
typedef void (__attribute__((thiscall)) *AM2_WidgetFocusFn)(AM2_Widget *w,
                                                            int32_t announce);

/* Original: 0x00453C40, thiscall. The base painter, and what 18 of the 33
 * vtables reach through two levels of forwarding thunk.
 *
 * If the widget has a sprite: place it, work out where the sprite goes,
 * intersect the widget's rectangle with the caller's clip and then with the
 * 640x480 screen, clip the sprite against that, and draw. Any of the three
 * failing drops the sprite -- but NOT the children, which are painted either
 * way, and are painted with the CALLER's clip rather than the intersected one.
 *
 * A widget with no sprite does not even place itself; the whole first half is
 * skipped and it goes straight to its children.
 *
 * The centring halves each side before subtracting -- `(w >> 1) - (sw >> 1)`,
 * not `(w - sw) >> 1` -- which differs by a pixel whenever exactly one of the
 * two is odd. Kept as written. */
void __attribute__((thiscall)) WidgetPaint(AM2_Widget *w, RECT clip);

/* Original: 0x00454A90 and 0x00454BA0, thiscall, 48 bytes each. Two levels of
 * forwarding onto WidgetPaint, and 0x00454BA0 is the one 18 of the 33 vtables
 * carry in slot 1 -- so most of the menu reaches the base painter through
 * both of these.
 *
 * Each does nothing but copy its sixteen-byte clip rectangle onto the stack
 * again and tail-call the next, which is what an override that only calls its
 * base compiles to. The names describe position in the chain and nothing more:
 * the two classes that declared them are not recoverable from the image, and
 * inventing class names for them would be a guess where the addresses are a
 * fact. */
void __attribute__((thiscall)) WidgetPaintFwd1(AM2_Widget *w, RECT clip);
void __attribute__((thiscall)) WidgetPaintFwd2(AM2_Widget *w, RECT clip);

/* Original: 0x00454AC0 and 0x00455100 -- two more shared virtuals, one
 * instruction each.
 *
 * Both are `jmp` tail calls, onto the base update and the base repaint, which
 * is what an override that only calls its base compiles to when the signature
 * has nothing to copy.
 *
 * A THIRD one is deliberately absent. One vtable's slot 2 is 0x0045CAA0, a
 * bare `ret`, which reads like a class whose update does nothing -- and that
 * address is ADDR_LOG, stubbed to `ret` in this build and patched by the
 * harness to capture the game's output -- vtable 0x0046FD10 slot 2, measured.
 * Why one address serves both is inferred and not checked: an empty virtual
 * and a stubbed varargs logger are both a single `c3`, which is what
 * identical-COMDAT folding merges. Reconstruct
 * it and the game runs perfectly with its log silenced, which blinds exactly
 * the half of the A/B that would have told you. */
void __attribute__((thiscall)) WidgetUpdateThunk(AM2_Widget *w);
void __attribute__((thiscall)) WidgetRepaintThunk(AM2_Widget *w);

/* Original: 0x00453D50, thiscall. Append a child to the end of this widget's
 * child list and point it back at this widget as its parent.
 *
 * The first child is linked with its `prevSibling` left ALONE -- only the
 * append path writes one -- so this depends on the constructor having zeroed
 * it. It never writes the new child's `nextSibling` either. Both are what the
 * original does and both are safe only because WidgetConstruct clears them. */
void __attribute__((thiscall)) WidgetAddChild(AM2_Widget *w,
                                              AM2_Widget *child);

/* Original: 0x00453D90, thiscall. Follow the sibling chain from this widget to
 * its end and answer the last one -- which is `this` when there is no next.
 * It is the wrap for moving backwards: a widget with no previous sibling is
 * the first child, so the last of its own chain is the last child. */
AM2_Widget *__attribute__((thiscall)) WidgetLastSibling(AM2_Widget *w);

/* Original: 0x00453DB0 and 0x00453E20, thiscall. Move the focus to the next or
 * the previous ELIGIBLE sibling and dispatch slot 3 on it. Both wrap: forwards
 * runs off the end into the parent's first child, backwards runs off the front
 * into the last of the chain. Both stop if they come all the way round to the
 * widget they started from, and both then re-test the candidate before
 * dispatching -- redundantly for the eligible case, which is the original's
 * shape and is kept.
 *
 * They do NOT agree on what eligible means, and that is reproduced rather than
 * tidied. Forwards requires 0x0050 set AND 0x004C clear; backwards looks only
 * at 0x0050 and never reads 0x004C at all. So a widget with 0x004C set is
 * skipped going down and landed on going up. Nothing in the menus shipped here
 * has been seen to set 0x004C, so which of the two is the bug is not
 * established -- only that they differ. */
void __attribute__((thiscall)) WidgetFocusNext(AM2_Widget *w, int32_t announce);
void __attribute__((thiscall)) WidgetFocusPrev(AM2_Widget *w, int32_t announce);

/* Original: 0x00453E80, thiscall, 21 direct callers -- the base per-frame
 * update and the whole keyboard interface of a dialog.
 *
 * Place myself, then update every child through THEIR slot 2, which is the
 * recursion that makes WidgetScreenRect the busiest function in the tree.
 * Then, only if this widget is the one holding keyboard focus and it has a
 * focused child:
 *
 *   UP                move focus to the previous eligible sibling
 *   DOWN, TAB         move it to the next
 *   SPACE or RETURN   changing at all repaints the focused child, so the
 *                     button shows itself pressed and released
 *   SPACE, RETURN     on RELEASE, fire the focused child's activate handler
 *
 * The three movement keys use KeyPressed, which is the auto-repeating array,
 * so holding UP scrolls. The two activation keys use `!IsKeyDown && KeyChanged`
 * -- release -- and consume, so a press cannot fire twice.
 *
 * The focused child is re-read before each block because the movement keys can
 * change it. It is NOT re-checked for null in the last three, and the original
 * dereferences it there unguarded; reproduced. */
void __attribute__((thiscall)) WidgetUpdate(AM2_Widget *w);

/* Original: 0x00454BD0, thiscall, slot 2 of 17 classes. The per-frame update
 * for a widget that has a cancel handler: if ESCAPE has just been RELEASED,
 * consume it and call the handler at 0x0060 with `this`; otherwise fall
 * through to the base update.
 *
 * The three conditions are checked in the original's order and each one short-
 * circuits to the fall-through, so a widget with no handler never queries the
 * keyboard at all. The consume is what stops one press firing on two frames.
 *
 * Note 0x0060 is the handler HERE and the ink colour in a label -- the label's
 * vtable has the base update in slot 2 rather than this, which is what makes
 * that safe and is the clearest evidence the subclasses lay their own tails
 * out independently. */
void __attribute__((thiscall)) WidgetUpdateCancel(AM2_Widget *w);

/* Original: 0x00456970 over 0x00456990 -- a second scalar deleting destructor
 * and the `jmp` thunk it calls, which lands on the base destructor. Slot 0 in
 * three vtables. Identical in shape to WidgetDelete over WidgetDestruct; the
 * original has two because two different classes declared one. */
AM2_Widget *__attribute__((thiscall)) WidgetDeleteAlt(AM2_Widget *w,
                                                      int32_t flags);
void __attribute__((thiscall)) WidgetDestructThunk(AM2_Widget *w);

/* The label subclass that highlights when it holds the focus -- vtable
 * 0x0046FB80, and what the CONTROLS panel builds its captions from.
 *
 * It adds nothing but a second colour pair. Its painter picks between them on
 * 0x0044, copies the winner into the plain label's ink and paper, and
 * delegates to LabelDraw -- which is why those two bytes are rewritten on
 * every frame rather than set once by the constructor.
 *
 * The two branches write the pair in opposite orders, ink-then-paper when
 * focused and paper-then-ink when not. Nothing can observe that; it is kept
 * because it is what the original does and tidying it would be inventing.
 *
 * Its destructor tail-calls the label's, which is what establishes the
 * inheritance, and its slot 3 is a plain forward to WidgetTakeFocus. */
AM2_Widget *__attribute__((thiscall)) FocusLabelDelete(AM2_Widget *w,
                                                       int32_t flags);
void __attribute__((thiscall)) FocusLabelDestruct(AM2_Widget *w);
void __attribute__((thiscall)) FocusLabelDraw(AM2_Widget *w, RECT clip);
void __attribute__((thiscall)) FocusLabelTakeFocus(AM2_Widget *w,
                                                   int32_t announce);

/* A two-state indicator: one flag and two sprites. The flag being at 0x006C
 * and the sprites at 0x0060 and 0x0064 is measured; calling it a TOGGLE is the
 * obvious reading of "one bit picks one of two pictures" and nothing here
 * evidences what it toggles. */
#define TOGGLE_OFF_STATE        0x6C   /* int32_t, non-zero picks the ON sprite */
#define TOGGLE_OFF_SPRITE_OFF   0x60   /* AM2_Sprite * */
#define TOGGLE_OFF_SPRITE_ON    0x64   /* AM2_Sprite * */

/* Original: 0x00456D00, thiscall. Choose the sprite from the flag, store it in
 * the widget's own sprite field, and paint exactly as the base does -- the
 * same shape as ButtonPaint, with one bit instead of three states. */
void __attribute__((thiscall)) TogglePaint(AM2_Widget *w, RECT clip);

/* The list box. Its rows are 14 pixels tall and start 4 below the widget's
 * top, which is read off the arithmetic in ListTakeFocus rather than assumed:
 * `top + 14 * (0x58 - 0x74) + 4`, with the row 14 tall.
 *
 * What that arithmetic establishes is that 0x0058 is the row being singled out
 * and 0x0074 is the first row on screen. Calling them the SELECTED row and the
 * SCROLL ORIGIN is the obvious reading and is not otherwise evidenced here.
 * 0x0058 is the text pointer in a label -- the tails differ, as ever. */
#define LIST_OFF_SELECTED    0x58   /* int32_t, negative means none */
#define LIST_OFF_TOP_ROW     0x74   /* int32_t, first row drawn */
#define LIST_ROW_HEIGHT      14
#define LIST_ROW_TOP_MARGIN  4

/* Original: 0x00455110, thiscall, slot 3 of the list box. Place, and if a row
 * is selected AND the caller asked for the change to be announced, repaint
 * just that ONE ROW -- clipped to its own strip rather than the whole list --
 * before taking focus the ordinary way.
 *
 * With no row selected, or with announce clear, it is exactly the base. */
void __attribute__((thiscall)) ListTakeFocus(AM2_Widget *w, int32_t announce);

/* A button's handlers and its auto-repeat state. 0x005C and 0x0060 are the
 * font and the ink in a LABEL -- the subclass tails do not agree and must not
 * be merged into one struct. */
#define BUTTON_OFF_ON_LEFT   0x54   /* void(__cdecl *)(AM2_Widget *) */
#define BUTTON_OFF_REPEATS   0x5C   /* int32_t, non-zero enables auto-repeat */
#define BUTTON_OFF_DEADLINE  0x60   /* uint32_t, GetTickCount of the next repeat */
#define BUTTON_OFF_ON_RIGHT  0x64   /* void(__cdecl *)(AM2_Widget *) */

/* Original: 0x00454310, thiscall, slot 2 in four vtables -- a button's mouse
 * handling, and the only widget update that reads the pointer.
 *
 * Common to both modes: place, give up if there is no parent or 0x004C is set,
 * then compute `the cursor is inside me` into 0x0040. That field is the one
 * WidgetConstruct deliberately never writes, because this computes it before
 * anything reads it. Outside, the repeat deadline is cleared and nothing else
 * happens. Inside, and if the mouse moved at all, the button takes focus.
 *
 * Then the two modes, which do NOT agree with each other and are reproduced
 * as they are:
 *
 *   0x005C clear -- a plain button. The left handler fires on RELEASE and the
 *   right handler on RELEASE, and a press only repaints.
 *
 *   0x005C set -- auto-repeat. The LEFT handler does NOT fire on first press,
 *   only the deadline is armed; it fires on release, and again every time the
 *   clock passes the deadline while the button is held. The RIGHT handler DOES
 *   fire on first press. That asymmetry is the original's.
 *
 * The delays are 250 ms to the first repeat and 150 ms between, read through
 * GetTickCount. With neither button down, the deadline and the hover flag are
 * both cleared.
 *
 * Every arm ends the same way -- repaint self through slot 1 with its own
 * rectangle, then run the base update -- which is what makes a kilobyte out of
 * this much logic. */
void __attribute__((thiscall)) ButtonUpdate(AM2_Widget *w);

/* A three-state button's sprites. The dialog builders load them in triples --
 * `03_007_00_ok.bmp`, `03_007_01_ok.bmp`, `03_007_02_ok.bmp` -- which is the
 * independent confirmation that these three offsets are normal, focused and
 * pressed in that order. */
#define BUTTON_OFF_SPRITE_NORMAL  0x68   /* AM2_Sprite *, not focused */
#define BUTTON_OFF_SPRITE_FOCUS   0x6C   /* AM2_Sprite *, focused, not pressed */
#define BUTTON_OFF_SPRITE_PRESSED 0x70   /* AM2_Sprite *, being pressed */

/* Original: 0x00454270, thiscall, slot 1 in two vtables. Choose which of the
 * three sprites this button shows, store it in the widget's own sprite field,
 * and then paint exactly as the base does.
 *
 * A button that is not its parent's focused child is always the normal
 * sprite. The focused one is PRESSED if the cursor is inside it and the left
 * button is either down or has just changed, or -- whether or not the cursor
 * is anywhere near it -- if RETURN is held. That second route is what makes a
 * keyboard-selected button look pressed while the key is down, and it is
 * checked on both paths rather than only the no-hover one.
 *
 * A button with no parent picks nothing and keeps whatever sprite it had. */
void __attribute__((thiscall)) ButtonPaint(AM2_Widget *w, RECT clip);

/* The edit box -- vtable 0x0046FC98, and the class CLAUDE.md names when it says
 * porting g_charHandler means porting the text-field system.
 *
 * Its whole extra behaviour is one piece of global state. Taking focus records
 * itself in 0x0065A05C and installs 0x0044D520 as the WM_CHAR consumer that
 * WndProc calls; losing focus, or being destroyed, clears BOTH -- but only if
 * it is still the one recorded, which is what stops a stale field wiping a
 * newer field's handler.
 *
 * That check appears twice, in the repaint and in the destructor, and both are
 * needed: a field can be repainted while another has focus, and can be
 * destroyed while another has focus.
 *
 * Its update is hover-to-focus and nothing else, which is how clicking a text
 * field gives it the caret and the keyboard. */
AM2_Widget *__attribute__((thiscall)) EditDelete(AM2_Widget *w, int32_t flags);
void __attribute__((thiscall)) EditDestruct(AM2_Widget *w);
void __attribute__((thiscall)) EditTakeFocus(AM2_Widget *w, int32_t announce);
void __attribute__((thiscall)) EditRepaint(AM2_Widget *w);
void __attribute__((thiscall)) EditUpdate(AM2_Widget *w);

int widget_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_WIDGET_H */
