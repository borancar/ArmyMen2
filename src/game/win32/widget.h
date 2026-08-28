#ifndef AM2_WIDGET_H
#define AM2_WIDGET_H

#include <stdint.h>
#include "../../inject/orig.h"
#include "../../inject/win32.h"
#include "../rect.h"   /* AM2_Rect, by value in the widget constructors */

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
    int32_t  disabled;              /* 0x004C  set disqualifies from focus, and
                                     * the multiplayer panel's update writes it
                                     * per row on exactly the policy its button
                                     * handlers guard on -- a row with a real
                                     * player is editable only if it is ours,
                                     * an empty one only by the host. So it is
                                     * "greyed out" and not merely a focus
                                     * rule; `ctl widgets` prints it as nofoc. */
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
/* The three-state button, and what its destructor restores. */
#define VTABLE_BUTTON       0x0046FC34u
/* The one-sprite icon, and the blinker that derives from it. */
#define VTABLE_ICON         0x0046FC70u
#define VTABLE_BLINKER      0x0046FD38u
#define VTABLE_LIST         0x0046FCC0u
/* The horizontal scroll bar. Its constructor loads 03_020_00_hscrollbar.bmp
 * and builds an ltarrow and an rtarrow child, so the class names itself. */
#define VTABLE_SCROLLBAR    0x0046FCFCu
/* The dialog base, one level under the icon. Fifteen classes derive from it
 * with a destructor that is the same two instructions, and each stamps its own
 * vtable from this list on the way past. */
#define VTABLE_DIALOG       0x0046FC84u
/* The typewriter message label. Six confirm dialogs build one. */
#define VTABLE_TYPER        0x0046FD24u
#define TYPER_OFF_TEXT      0x58    /* char[0x400], lines separated by `|` */
#define TYPER_OFF_LAST      0x458   /* uint32_t, GetTickCount at the last reveal */
#define TYPER_OFF_SHOWN     0x45C   /* int32_t, characters revealed so far */
#define TYPER_OFF_BLINKER   0x460   /* AM2_Widget *, ticked on every reveal */
#define TYPER_LINE_HEIGHT   12
#define TYPER_REVEAL_MS     100
#define TYPER_BLINK_MS      0x46    /* 70 ms, the same period the list box uses */
#define VTABLE_DLG_SELECTMAP             0x0046FAB8u
#define VTABLE_DLG_DIFFICULTY            0x0046FAE0u
#define VTABLE_DLG_QUITGAME              0x0046FAF4u
#define VTABLE_DLG_REPLAY                0x0046FB08u
#define VTABLE_DLG_AUDIO                 0x0046FB1Cu
#define VTABLE_DLG_OPTIONS               0x0046FB30u
#define VTABLE_DLG_DELGAME               0x0046FB44u
#define VTABLE_DLG_OVERWRITE             0x0046FB58u
#define VTABLE_DLG_DELPLAYER             0x0046FB6Cu
#define VTABLE_DLG_CONTROLS              0x0046FB94u
#define VTABLE_DLG_SELECTPLAYER          0x0046FBA8u
#define VTABLE_DLG_NAMEENTRY             0x0046FBBCu
#define VTABLE_DLG_LOADGAME              0x0046FBD0u
#define VTABLE_DLG_MESSAGE               0x0046FBE4u
#define VTABLE_DLG_GAMEMENU              0x0046FBF8u
#define SCROLLBAR_OFF_BAR   0x64   /* AM2_Sprite *, the bar the paint draws */
#define SCROLLBAR_OFF_SHIFT 0x6C   /* int32_t, added to the centred x */
#define SCROLLBAR_OFF_SPAN  0x70   /* int32_t, taken off the width first */
#define SCROLLBAR_OFF_FLAG50 0x50  /* int32_t, constructed 0 by the dialog */
#define SCROLLBAR_OFF_POS   0x74   /* int32_t, 0..RANGE */
#define SCROLLBAR_OFF_RANGE 0x78   /* int32_t, what POS is out of */
#define SCROLLBAR_OFF_ONCHANGE 0x7C /* void(__cdecl *)(AM2_Widget *) */
#define ICON_OFF_SPRITE     0x58

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

/* One entry of the CONTROLS dialog's key table. */
typedef struct AM2_KeyName {
    uint32_t    dik;    /* DirectInput scancode */
    const char *name;   /* what the dialog shows for it */
} AM2_KeyName;

/* The key-capture row -- the class the CONTROLS dialog has 21 of, and a
 * subclass of the focus-highlighting label, so its text is the label's at
 * 0x0058. Its own field is the bound key's INDEX into the key table. */
#define KEYROW_OFF_KEY       0x68   /* int32_t, index into the key table */
/* The parent holds its rows in an array at 0x0064. Twenty-one of them, which
 * is exactly what `ctl widgets` counts on that dialog. */
#define KEYROW_PARENT_ROWS   0x64
#define KEYROW_ROW_COUNT     21

/* Original: 0x00450C10. The index of the key that has just gone DOWN -- a
 * changed-and-now-down edge, walked over the whole 95-entry table -- or -1.
 * The first match wins, so a table order is a priority order. */
int32_t __cdecl FindPressedKey(void);

/* Original: 0x00450D50, thiscall, slot 2 of the key-capture row. Hover to
 * focus like the edit box, and then, ONLY while this row holds the focus,
 * capture whatever key is pressed: remember its index, put its name in the
 * label's own text pointer, and repaint.
 *
 * Then the part that makes it a key BINDING and not just a display: it walks
 * all twenty-one of the parent's rows and clears the same key off any OTHER
 * row that had it, setting that row back to "None". A key can be bound in one
 * place only, and this is where that is enforced. */
void __attribute__((thiscall)) KeyRowUpdate(AM2_Widget *w);

/* An indexed sprite widget: an array of sprites at 0x0064 chosen by a state at
 * 0x006C, with two vertical tweaks.
 *
 * The array cannot hold more than TWO. `0x0064 + index * 4` reaches 0x006C at
 * index 2, which is the index field itself -- so a third entry would read the
 * state as a sprite pointer. That is a fact about the layout, not a guess:
 * whatever this class is, it has at most two pictures. */
#define MULTISPR_OFF_SPRITES  0x64   /* AM2_Sprite *[2] */
#define MULTISPR_OFF_INDEX    0x6C   /* int32_t, which one */
#define MULTISPR_OFF_Y_BIAS   0x70   /* int32_t, added to the centred y */
#define MULTISPR_OFF_Y_INSET  0x74   /* int32_t, taken off the height first */
/* And a slot in FRONT of the array, which the constructor fills with the
 * first bitmap's sprite while putting the SECOND one in sprites[0]. The
 * painter never reads it -- it indexes from MULTISPR_OFF_SPRITES -- so with
 * an index of 0 the widget shows the second bitmap and with 1 it shows
 * nothing, because sprites[1] is left null. That is the blink. */
#define MULTISPR_OFF_SPRITE0  0x60   /* AM2_Sprite *, the first bitmap */

/* Original: 0x00455C80, thiscall. Place, choose the sprite, centre it in the
 * widget, intersect that against the caller's clip, clip the sprite to what is
 * left and draw.
 *
 * The centring is `left + (right - left - spriteWidth) / 2` horizontally and
 * the same vertically with 0x0074 removed from the height and 0x0075's
 * neighbour 0x0070 added back afterwards -- so the two fields bias the sprite
 * up or down within its box. Both halves shift right by one AFTER the
 * subtraction here, unlike WidgetPaint which halves each side first; the two
 * classes round differently and both are reproduced as written.
 *
 * VERIFIED ONLY AS FAR AS THE NULL CHECK. It runs 9,081 times on the
 * multiplayer path and its sprite is null every time: shifting the drawn
 * position by five pixels changes nothing, and so does returning outright
 * before the draw. So the placement and the array index are exercised and
 * everything past `if (!spr)` -- the centring, the two intersects and the blit
 * -- is verified by reading. A clean A/B here says less than it looks. */
void __attribute__((thiscall)) MultiSpritePaint(AM2_Widget *w, RECT clip);

/* The two-state indicator is a BLINKER, and its update says so: it flips
 * 0x006C on a timer, counts flashes down, and stops. On the multiplayer dialog
 * these are the small "send" dots beside the two name fields.
 *
 * 0x006C is the same field TogglePaint reads to choose its sprite, so the
 * blink IS the sprite swap. */
#define BLINK_OFF_ACTIVE     0x68   /* int32_t, non-zero while flashing */
#define BLINK_OFF_PERIOD     0x70   /* uint32_t, ms between flips */
#define BLINK_OFF_REMAINING  0x74   /* int32_t, flips left before it stops */
#define BLINK_OFF_ELAPSED    0x78   /* uint32_t, scratch: ms since the last flip */
#define BLINK_OFF_LAST       0x7C   /* uint32_t, GetTickCount at the last flip */

/* Original: 0x00456D40, thiscall, slot 2. Does nothing at all unless 0x0068
 * is set. Otherwise: measure the time since the last flip into 0x0078, give up
 * if the period has not passed, then flip 0x006C, zero 0x0078, and count
 * 0x0074 down. Reaching zero clears BOTH the active flag and the state, so a
 * blink always ENDS in the off sprite however many flips were asked for.
 * Finally repaint through slot 1.
 *
 * 0x0078 is written before the period test, so it holds the elapsed time even
 * on the frames that do nothing -- it is a readout, not a working value. */
void __attribute__((thiscall)) BlinkerUpdate(AM2_Widget *w);

/* Original: 0x00456DC0, thiscall. Start a blink: on, active, remember the
 * period and the number of flips, stamp the clock, and repaint immediately so
 * the first flash appears without waiting a period. */
void __attribute__((thiscall)) BlinkerStart(AM2_Widget *w, uint32_t periodMs,
                                            int32_t flips);

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

/* Original: 0x00455D50 -- one instruction, `jmp WidgetScreenRect`. Slot 2 of
 * the multi-sprite class, whose whole per-frame update is therefore
 * "recompute my absolute rectangle". */
void __attribute__((thiscall)) MultiUpdateThunk(AM2_Widget *w);

/* Original: 0x00456240, slot 1 of the scroll bar. Draw the bar sprite, and
 * only the bar -- it paints no children, where the base painter would.
 *
 * The two axes are NOT symmetric and the asymmetry is the original's. The x is
 * centred on the widget's width less SCROLLBAR_OFF_SPAN and then shifted by
 * SCROLLBAR_OFF_SHIFT, which is what moves the bar along its track; the y is
 * centred on the SPRITE's own height with nothing added. So only one axis can
 * scroll, which for a horizontal bar is the point.
 *
 * Both halves shift right by one AFTER the subtraction, where WidgetPaint's
 * centring halves each side BEFORE it -- a different rounding on odd values,
 * kept as each has it. */
void __attribute__((thiscall)) ScrollBarPaint(AM2_Widget *w, RECT clip);

/* Original: 0x004561E0 and 0x004561C0. The bar sprite IS null-tested before
 * being released, where the icon's otherwise identical destructor does not
 * test at all -- the two disagree and both are kept as written. The two arrow
 * children are not released here: they are children, so the base destructor
 * takes them with the rest of the list. */
void __attribute__((thiscall)) ScrollBarDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) ScrollBarDelete(AM2_Widget *w,
                                                      int32_t flags);

/* Original: 0x00455B70 and 0x00455B50, the scroll bar's arrow children.
 *
 * The arrow has no constructor: the scroll bar builds each one by calling the
 * BUTTON constructor and then stamping vtable 0x0046FCD4 over the button's.
 * Its destructor is a single `jmp` to the button's, so it re-stamps
 * VTABLE_BUTTON rather than its own -- which for a widget about to be freed
 * changes nothing, and is reproduced rather than corrected. */
void __attribute__((thiscall)) ArrowDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) ArrowDelete(AM2_Widget *w, int32_t flags);

/* Original: 0x00454B90 and 0x00454B70, the DIALOG base class -- one level
 * under the icon, whose destructor it jumps straight to. Every full-screen
 * dialog derives from it, so this pair runs whenever any of them closes. */
void __attribute__((thiscall)) DialogDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) DialogDelete(AM2_Widget *w,
                                                   int32_t flags);

/* The fifteen dialog subclasses whose destructor is the same two instructions:
 * stamp my own vtable, then jump to DialogDestruct. The image really does hold
 * fifteen separate copies -- MSVC emits one ~Dialog() per class and there is
 * nothing here for the linker to fold, since each stamps a different constant.
 *
 * They are written as a macro rather than fifteen transcriptions for the
 * reason CommEndSetup is written once: fifteen chances to mistype a vtable
 * address is not fifteen pieces of evidence. The names come from the bitmap
 * each constructor loads, which is the only thing that distinguishes them. */
#define AM2_DECLARE_DIALOG_DTOR(name)                                        \
    void __attribute__((thiscall)) name##Destruct(AM2_Widget *w);            \
    AM2_Widget *__attribute__((thiscall)) name##Delete(AM2_Widget *w,        \
                                                       int32_t flags)

AM2_DECLARE_DIALOG_DTOR(DlgSelectMap);
AM2_DECLARE_DIALOG_DTOR(DlgDifficulty);
AM2_DECLARE_DIALOG_DTOR(DlgQuitGame);
AM2_DECLARE_DIALOG_DTOR(DlgReplay);
AM2_DECLARE_DIALOG_DTOR(DlgAudio);
AM2_DECLARE_DIALOG_DTOR(DlgOptions);
AM2_DECLARE_DIALOG_DTOR(DlgDelGame);
AM2_DECLARE_DIALOG_DTOR(DlgOverwrite);
AM2_DECLARE_DIALOG_DTOR(DlgDelPlayer);
AM2_DECLARE_DIALOG_DTOR(DlgControls);
AM2_DECLARE_DIALOG_DTOR(DlgSelectPlayer);
AM2_DECLARE_DIALOG_DTOR(DlgNameEntry);
AM2_DECLARE_DIALOG_DTOR(DlgLoadGame);
AM2_DECLARE_DIALOG_DTOR(DlgMessage);
AM2_DECLARE_DIALOG_DTOR(DlgGameMenu);

/* Original: 0x004569A0, slot 1 of the typewriter label. Draw the revealed
 * prefix of the wrapped text, one call per `|`-separated line, each twelve
 * pixels below the last.
 *
 * The intersection with the caller's clip is computed and then only TESTED --
 * what is handed to the text drawer is the caller's rectangle unchanged, where
 * `ListDraw` passes the intersection. The two disagree and both are kept.
 *
 * The line buffer is a kilobyte of stack with no bound check on the count, so
 * a wrapped line longer than 1008 characters would run off it. Nothing the
 * game ships comes close; reproduced rather than guarded, because a guard
 * would be ours and would hide the original's shape.
 *
 * Note it walks exactly `shown` characters and never looks for the
 * terminator: the update is what keeps that count inside the string. */
void __attribute__((thiscall)) TyperPaint(AM2_Widget *w, RECT clip);

/* Original: 0x00456B20, slot 2. Reveal one more character every 100 ms until
 * the whole string is out, ticking the blinker and playing sound 0 on each --
 * which is the typing click. Then repaint through slot 1 and chain to the base
 * update.
 *
 * The elapsed test is `> 100`, not `>=`, and the reveal is one character per
 * tick however long the frame took -- so the effect runs slower on a machine
 * that cannot keep 10 fps rather than skipping ahead. The original's. */
void __attribute__((thiscall)) TyperUpdate(AM2_Widget *w);

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

/* The list box's own fields, all read by its painter. The row array at 0x0060
 * is {int32_t count; const char *strings;} followed by nothing this function
 * touches -- the strings are indexed by a 260-byte stride from `strings`. */
/* The row array: a count, then the base of the row records. A row's text is
 * `text + index * 260`. */
typedef struct AM2_ListRows {
    int32_t  count;
    char    *text;
} AM2_ListRows;

#define LIST_OFF_ROWS        0x60   /* AM2_ListRows * */
#define LIST_OFF_HOT         0x5C   /* int32_t, the row under the pointer */
#define LIST_OFF_OWNS_ROWS   0x64   /* int32_t; the array is freed only if set */
#define LIST_OFF_VISIBLE     0x78   /* int32_t, how many rows fit */
#define LIST_OFF_INK         0x80   /* uint8_t, an ordinary row */
#define LIST_OFF_INK_SEL     0x84   /* uint8_t, the selected row */
#define LIST_OFF_INK_SEL_DOWN 0x88  /* uint8_t, selected with the button down */
#define LIST_OFF_INK_HOT_SEL 0x8C   /* uint8_t, hot AND selected AND eligible */
#define LIST_OFF_CALLBACK    0x68   /* the row callback, or ADDR_LOG for none */
#define LIST_OFF_ARG6C       0x6C
#define LIST_OFF_ARG70       0x70   /* constructed 0 */
#define LIST_OFF_ARG7C       0x7C   /* constructed 0 */
#define LIST_ROW_STRIDE      0x104  /* 260 bytes per row record */
#define LIST_TEXT_INDENT     4

/* Slot 2 of the list box, 0x00455340, is NOT reconstructed. It is 2 KB and
 * branchy, and what has been read of it is in STATUS.md: a per-frame callback
 * at 0x006C, the row under the pointer computed as
 * `(cursorY - rect.top - 4) / 14 + topRow` -- the division by 14 is the
 * compiler's magic-number sequence, which is a second independent route to the
 * row height -- and a BlinkerStart(0x0094's widget, 70ms, 1) whenever that row
 * changes, which is the first use found for the blinker at all. */

/* Original: 0x00455090 / 0x00455070, thiscall -- the list box's destructor and
 * its deleting wrapper. Restore the vtable, and if 0x0064 says this list owns
 * its row array AND the array is there, run the array's own cleanup and give
 * the storage back. Then chain to the base.
 *
 * Both conditions are tested, so a list handed a shared array leaves it alone
 * and a list that owns a null one does nothing -- unlike the icon and blinker
 * destructors, which release their sprites with no null test at all. The two
 * conventions sit side by side in the same hierarchy.
 *
 * The SEH prologue is not reproduced; see CLAUDE.md. */
void __attribute__((thiscall)) ListDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) ListDelete(AM2_Widget *w, int32_t flags);

/* Original: 0x00455180, thiscall, slot 1 of the list box. Clear the whole list
 * to 0x00502AD9, then walk the visible rows drawing each one's text.
 *
 * The ink is chosen in three layers, and the later ones overwrite the earlier:
 * an ordinary row uses 0x0080; the SELECTED row, while the list has focus,
 * uses 0x0088 if the left button is down and 0x0084 if not; and the HOT row --
 * the one under the pointer -- uses 0x008C when it is also the selected,
 * focused, eligible row and the highlight colour otherwise. The hot row is
 * also filled with the highlight colour before its text goes down, which is
 * what makes the green bar in SELECT DIFFICULTY.
 *
 * One defect reproduced. The per-row rectangle is intersected against the clip
 * and the result reused, but a FAILED intersect only skips the fill -- the
 * text is still drawn, with whatever that rectangle held from the row before.
 * Same shape as the Restore defect in LockSurface, and kept for the same
 * reason. */
void __attribute__((thiscall)) ListDraw(AM2_Widget *w, RECT clip);

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
/* Whether this button OWNS its three sprites and must release them. A button
 * handed shared sprites leaves them alone. Evidenced by the destructor, which
 * is also the independent confirmation that 0x0068, 0x006C and 0x0070 are the
 * three sprites and nothing else -- it releases exactly those. */
#define BUTTON_OFF_OWNS_SPRITES   0x75   /* uint8_t */

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

/* Original: 0x00454A30 / 0x00454A10, thiscall -- the one-sprite icon's
 * destructor and its deleting wrapper. Restore the vtable, release the sprite
 * at 0x0058, chain to the base. The release is UNCONDITIONAL: no null test,
 * so ReleaseSprite is trusted to cope, which is worth knowing before writing a
 * guard the original does not have.
 *
 * Original: 0x00456CA0 / 0x00456C80 -- the blinker's pair, which chains to the
 * icon's rather than straight to the base, so the blinker derives from the
 * icon. It sets the widget's live sprite to the OFF one before releasing the
 * ON one; nothing here reads it afterwards, and it is reproduced because it is
 * what the original does. Its release is unconditional too.
 *
 * Neither SEH prologue is reproduced; see CLAUDE.md. */
void __attribute__((thiscall)) IconDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) IconDelete(AM2_Widget *w, int32_t flags);
void __attribute__((thiscall)) BlinkerDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) BlinkerDelete(AM2_Widget *w,
                                                    int32_t flags);

/* Original: 0x004541E0 and 0x004541C0, thiscall -- the three-state button's
 * destructor and the MSVC deleting wrapper over it. Restore the vtable,
 * release the three sprites if 0x0075 says this button owns them, and chain to
 * the base destructor.
 *
 * The original's SEH prologue is not reproduced; see CLAUDE.md for why that is
 * safe here and what it would cost if it were not. */
void __attribute__((thiscall)) ButtonDestruct(AM2_Widget *w);
AM2_Widget *__attribute__((thiscall)) ButtonDelete(AM2_Widget *w,
                                                   int32_t flags);

/* The edit box's own fields. Note the two ink bytes are the OPPOSITE way round
 * from the focus label's: here 0x0064 is the focused colour and 0x0065 the
 * unfocused one, and in FocusLabel it is 0x0065 focused and 0x0064 not.
 * Different classes, so there is no contradiction -- but it is the sharpest
 * illustration yet of why these tails must not be merged. */
#define EDIT_OFF_TEXT       0x58   /* char *, the current contents */
#define EDIT_OFF_FONT       0x60   /* int32_t */
#define EDIT_OFF_INK_FOCUS  0x64   /* uint8_t, ink while focused */
#define EDIT_OFF_INK        0x65   /* uint8_t, ink while not focused */
#define EDIT_OFF_PAPER      0x66   /* uint8_t, background */
#define EDIT_OFF_MAX        0x5C   /* int32_t, characters the field accepts */
#define EDIT_OFF_CARET      0x6C   /* int32_t, constructed 0 */
#define EDIT_OFF_SCROLL     0x70   /* int32_t, constructed 0 */
#define EDIT_OFF_ON_ENTER   0x74   /* void(__cdecl *)(AM2_Widget *) */
#define EDIT_OFF_ARG78      0x78
#define EDIT_OFF_ARG7C      0x7C

/* The original builds its string in 68 bytes of stack. Kept at that size
 * rather than rounded, because the caret is appended into it and the size is
 * the only thing standing between a long field and the return address. */
#define EDIT_DRAW_BUFFER    68

/* Original: 0x00454D20, thiscall, slot 1 of the edit box. Place, clip, clear
 * the background, COPY the text into a stack buffer, and -- if this field has
 * the focus -- append a literal `'_'` as the caret before drawing.
 *
 * The caret is why the text is copied at all: the field's own buffer is not
 * written, so the caret exists only in the painted image. And because 0x0044
 * is set by WidgetTakeFocus and cleared by WidgetRepaint, it toggles, which is
 * what makes the caret blink -- the same blinking CLAUDE.md recorded from the
 * other end when the multiplayer A/B came out 90 to 100 pixels apart.
 *
 * Reached through LockSurface like every other rasteriser here; a failed lock
 * leaves the cleared background with no text over it, as in LabelDraw. */
void __attribute__((thiscall)) EditDraw(AM2_Widget *w, RECT clip);

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

/* 0x00453910 and 0x00453930, both thiscall over the same three-field record.
 * The first is its constructor -- two zeros and the argument -- and the second
 * is one `jmp` to its reset at 0x00453940, which stays original.
 *
 * The vocabulary is split and neither half is ours to settle: orig.h calls
 * 0x00453910 the SESSION constructor and 0x00453930 the list box's ROW ARRAY
 * cleanup, both names taken from callers. One shape, two callers, two words.
 * Named here for what they do to the record. */
/* 0x00450BF0, two callers. The INDEX of the CONTROLS dialog's key record whose
 * scancode has this low byte, or -1. Only the low byte is compared, which is
 * all a DirectInput scancode is; the records are 8 bytes and the table's end
 * is a literal address in the original, not a count. */
/* The OPTIONS dialog. Its whole screen is the declarative table at
 * ADDR_OPTION_TABLE -- see orig.h for the record layout -- and these four are
 * everything that reads it.
 *
 * The checkbox's ticked flag is at 0x0078, which the two apply/load bodies and
 * the group sync all agree on. */
#define CHECK_OFF_TICKED  0x78   /* uint8_t */
/* And a group HEADER carries its own record index at 0x0080, which is how
 * OptionsSyncGroup finds the range it owns without searching the table. */
#define CHECK_OFF_GROUP   0x80   /* int32_t, index into ADDR_OPTION_TABLE */
#define CHECK_OFF_SPRITE_OFF   0x68
#define CHECK_OFF_SPRITE_ON    0x6C   /* also copied to the base 0x0038 */
#define CHECK_OFF_SPRITE_2     0x70
#define CHECK_OFF_SPRITE_3     0x74

/* Original: 0x00432710, the DEFAULTS button. It reads nothing back: it asks
 * ResetPairMask for the two manufactured default masks and fills all 43
 * checkboxes from them, re-enabling each one on the way. */
void __cdecl OptionsDefaults(AM2_Widget *button);

/* Original: 0x004327A0, and it names itself -- "Options changed by host."
 *
 * The apply. Build both masks from scratch out of what is ticked, so unticking
 * needs no handling at all and nothing reads the previous value. Then the
 * click sound, a menu request of 7, a broadcast of the player list, and the
 * line. */
void __cdecl OptionsApply(AM2_Widget *button);

/* Original: 0x00432830. Ask for the options menu: 7 if we are the host and 9
 * if we are not, which is the usual neg/sbb spelling of a boolean. */
void __cdecl OptionsRequest(void);

/* Original: 0x00432870. A group header was clicked, so push its own tick down
 * onto every checkbox in its range -- and set each one's 0x004C, which is the
 * "cannot be focused" field, to the OPPOSITE. A ticked header therefore both
 * ticks its group and locks it. */
void __cdecl OptionsSyncGroup(AM2_Widget *header);

/* Originals: 0x004326F0 and 0x00432700, one `jmp` each into DialogDestruct and
 * WidgetUpdateCancel. The alias shape: the OPTIONS dialog wants those two
 * behaviours under its own vtable slots and the linker gave it thunks. */
void __attribute__((thiscall)) MpDialogDestruct(AM2_Widget *w);
void __attribute__((thiscall)) OptionsUpdate(AM2_Widget *w);

/* The menu screen factories -- see widget.cpp for the shape they share.
 *
 * Original: 0x004317C0. The multiplayer HOST panel: the lobby with the player
 * rows, the game-type and map lists and the comms field. It takes the session
 * role with it (ADDR_MP_SESSION = 1) and then refreshes the panel. */
void __cdecl OpenMpHost(void);

/* Original: 0x00433480. The multiplayer JOIN panel. The SAME class and the
 * same 0x278 bytes as the host panel -- only the backdrop and the role differ,
 * which is the clearest statement in the image that MP_SESSION is host versus
 * client and not something else. */
void __cdecl OpenMpJoin(void);

/* Original: 0x00432910. MULTIPLAYER OPTIONS, the 43-checkbox screen the
 * OPTIONS button on the host panel opens. */
void __cdecl OpenMpOptions(void);

/* Original: 0x0044DF20. SELECT MAP -- the campaign's map chooser, a list and a preview. */
void __cdecl OpenSelectMap(void);

/* Original: 0x00451910. SELECT PLAYER -- the saved-player list, with RECRUIT and DELETE. */
void __cdecl OpenSelectPlayer(void);

/* Original: 0x00451E10. ENTER NAME -- the edit box RECRUIT opens. */
void __cdecl OpenEnterName(void);

/* Original: 0x0042F440. the CD prompt: its constructor pushes "Copy Protection" and
 * "The ARMYMEN2 CD must be in the drive to play Army Men II." This build
 * has the five CD checks patched to jump past their MessageBoxA, but this
 * screen is a different mechanism and is reached through the menu table. */
void __cdecl OpenCdPrompt(void);

/* Original: 0x0042FF60. ENTER BATTLE NAME -- two text fields and an OK, which ab.sh multi drives. */
void __cdecl OpenBattleName(void);

/* Original: 0x0042F880. CHOOSE A BATTLE -- the session browser. */
void __cdecl OpenBattleJoin(void);

/* Original: 0x0044E6A0. the movie viewer. */
void __cdecl OpenMovies(void);

/* Original: 0x0044FDD0. the OPTIONS menu itself -- the AUDIO, CONTROLS and DIFFICULTY buttons.
 * It pushes no caption and no backdrop of its own; its identity is in the
 * three button bitmaps its constructor loads. */
void __cdecl OpenOptionsMenu(void);

/* Original: 0x00451210. CONTROLS -- the twenty-one key-capture rows ab.sh controls drives. */
void __cdecl OpenControls(void);

/* Original: 0x0044EAD0. DIFFICULTY -- the Easy/Medium/Hard list. */
void __cdecl OpenDifficulty(void);

/* Original: 0x0044EE50. CONFIRM GAME EXIT: "Are you sure you want to quit?". */
void __cdecl OpenQuitConfirm(void);

/* Original: 0x0044F220. "Do you wish to reattempt your failed mission?". */
void __cdecl OpenReplayPrompt(void);

/* Original: 0x00450B70. "Caution: All saved games for this player will also be deleted!". */
void __cdecl OpenDeletePlayer(void);
/* Original: 0x0042EE40. COMM CHANNEL SELECT, and the one factory that does
 * something before it allocates: CommCreateDirectPlay with a null connection. */
void __cdecl OpenCommPanel(void);

/* Original: 0x0044F9E0. AUDIO CONTROLS -- the three volume sliders. */
void __cdecl OpenAudioOptions(void);

/* Original: 0x00450250. DELETE GAME. */
void __cdecl OpenDeleteGame(void);

/* Original: 0x00452680. LOAD GAME. Its repaint comes after the construction
 * where its two siblings put it before the allocation; see widget.cpp. */
void __cdecl OpenLoadGame(void);

/* Original: 0x0044FAB0, thiscall. The OPTIONS menu's constructor: a backdrop
 * and four buttons, AUDIO / CONTROLS / DIFFICULTY / BACK. See widget.cpp for
 * why the rectangle is passed by value in the middle of the argument list and
 * how `ret 0x28` confirms the reading. Returns `this`. */
AM2_Widget *__attribute__((thiscall)) OptionsMenuConstruct(AM2_Widget *w,
                                                           const char *bmp);

/* Originals: 0x0044EB50, 0x0044EED0, 0x00450730. The three CONFIRM dialogs --
 * CONFIRM GAME EXIT, the replay prompt and DELETE PLAYER. One body three
 * times, differing in the vtable, the panel bitmap, the OK handler, the
 * message and (for DELETE PLAYER alone) the CANCEL handler. See widget.cpp
 * for the shape and for the three stores the original leaves unguarded. */
AM2_Widget *__attribute__((thiscall)) QuitDialogConstruct(AM2_Widget *w,
                                                          const char *bmp);
AM2_Widget *__attribute__((thiscall)) ReplayDialogConstruct(AM2_Widget *w,
                                                            const char *bmp);
AM2_Widget *__attribute__((thiscall)) DelPlayerDialogConstruct(AM2_Widget *w,
                                                               const char *bmp);

/* Original: 0x0044E730, thiscall. The DIFFICULTY dialog: the confirm-dialog
 * shape with a list box where they have a message. See widget.cpp for which
 * two of the list's fields the current difficulty seeds. */
AM2_Widget *__attribute__((thiscall)) DifficultyDialogConstruct(
    AM2_Widget *w, const char *bmp);

/* Original: 0x00450E10, thiscall. The CONTROLS dialog -- twenty-one
 * key-capture rows out of three parallel tables, then three buttons. See
 * widget.cpp for the tables and for why three of the row constructor's
 * arguments carry stale upper bytes in the original. */
AM2_Widget *__attribute__((thiscall)) ControlsDialogConstruct(AM2_Widget *w,
                                                              const char *bmp);

/* Original: 0x00432320, thiscall. MULTIPLAYER OPTIONS -- 43 checkboxes out of
 * the declarative table, then three buttons for a host and one for anyone
 * else. See widget.cpp for the three things that depend on being the host. */
AM2_Widget *__attribute__((thiscall)) MpOptionsConstruct(AM2_Widget *w,
                                                         const char *bmp);

/* Original: 0x0044F370, thiscall, `ret 8`. AUDIO CONTROLS -- three volume
 * sliders. The only screen whose SHAPE depends on where it was opened from:
 * see widget.cpp. */
AM2_Widget *__attribute__((thiscall)) AudioDialogConstruct(AM2_Widget *w,
                                                           const char *bmp,
                                                           int32_t flag);

/* Original: 0x0042FB00, thiscall. ENTER BATTLE NAME. The two fields are
 * seeded from the saved names and edit the dialog's own buffers in place,
 * which is why HostBattle can read them back out of globals. See widget.cpp. */
AM2_Widget *__attribute__((thiscall)) BattleNameConstruct(AM2_Widget *w,
                                                          const char *bmp);

/* Original: 0x0042E9C0, thiscall. COMM. CHANNEL SELECT -- the connection
 * list, filled by our own CommEnumConnections. Its list box takes ADDR_LOG as
 * a callback, which is the folded empty-virtual address; see widget.cpp. */
AM2_Widget *__attribute__((thiscall)) CommPanelConstruct(AM2_Widget *w,
                                                         const char *bmp);

/* Original: 0x00451400, thiscall. SELECT PLAYER -- the one screen whose rows
 * come off the FILESYSTEM. See widget.cpp. */
AM2_Widget *__attribute__((thiscall)) SelectPlayerConstruct(AM2_Widget *w,
                                                            const char *bmp);

/* Original: 0x00454980, thiscall, `ret 0x18`. The PANEL -- a widget whose
 * whole job is to hold a backdrop sprite, and what eight of the reconstructed
 * screens hang everything else off. It keeps the sprite twice, at 0x0038 and
 * 0x0058; see widget.cpp. */
AM2_Widget *__attribute__((thiscall)) PanelConstruct(AM2_Widget *w,
                                                     const char *bmp,
                                                     int32_t flag,
                                                     AM2_Rect box);

/* Original: 0x00450C50, thiscall, `ret 0x2C`. The key-capture ROW. It passes
 * an UNINITIALISED byte to the label constructor as the ink, which is the
 * original's and is harmless for a reason worth reading in widget.cpp. */
AM2_Widget *__attribute__((thiscall)) KeyRowConstruct(AM2_Widget *w,
                                                      int32_t nameIndex,
                                                      const char *caption,
                                                      int32_t left, int32_t top,
                                                      int32_t width,
                                                      int32_t height,
                                                      int32_t font, int32_t ink,
                                                      int32_t ink2, int32_t ink3,
                                                      int32_t ink4);

/* Original: 0x00454B00, thiscall, `ret 8`. The SCREEN BASE -- a panel over the
 * whole 640x480 with the dialog vtable on top. Every screen starts here. */
AM2_Widget *__attribute__((thiscall)) ScreenBaseConstruct(AM2_Widget *w,
                                                          const char *bmp,
                                                          int32_t flag);

/* Original: 0x004540F0, thiscall, `ret 0x28`. The BUTTON: three sprites by
 * name, a rectangle by value, and two handlers. Only the first bitmap is
 * tested for null; see widget.cpp. */
AM2_Widget *__attribute__((thiscall)) ButtonConstruct(AM2_Widget *w,
                                                      const char *b0,
                                                      const char *b1,
                                                      const char *b2,
                                                      int32_t flag,
                                                      AM2_Rect box,
                                                      void (__cdecl *onLeft)(AM2_Widget *),
                                                      void (__cdecl *onRight)(AM2_Widget *));

/* Original: 0x00454C10, thiscall, `ret 0x34` -- THIRTEEN stack arguments:
 * the buffer, the maximum, four of rectangle, a font, three colours, the
 * RETURN handler and two more.
 *
 * It installs the PERMISSIVE character set at 0x00485304 -- the one with
 * `~!@#$%^& in it -- and a caller that wants a narrower field overwrites
 * EDIT_OFF_CHARSET afterwards, which is exactly what ENTER BATTLE NAME does
 * with the letters-and-digits set at 0x00485308. */
AM2_Widget *__attribute__((thiscall)) EditConstruct(AM2_Widget *w, char *buf,
                                                    int32_t maxChars,
                                                    int32_t left, int32_t top,
                                                    int32_t width,
                                                    int32_t height,
                                                    int32_t font, int32_t inkFocus,
                                                    int32_t ink, int32_t paper,
                                                    void (__cdecl *onEnter)(AM2_Widget *),
                                                    int32_t a, int32_t b);

/* Original: 0x00456BC0, thiscall, `ret 0x1C`. The two-sprite widget -- the
 * flashing "send" and "receive" dots. A PANEL underneath, then the SECOND
 * bitmap into sprites[0] and the first parked at MULTISPR_OFF_SPRITE0. */
AM2_Widget *__attribute__((thiscall)) MultiSpriteConstruct(AM2_Widget *w,
                                                           const char *b0,
                                                           const char *b1,
                                                           int32_t flag,
                                                           AM2_Rect box);

/* Original: 0x00454F90, thiscall, `ret 0x20`. The LIST BOX. It works out how
 * many rows fit by dividing by SEVEN -- a magic-number division that is the
 * only place the row height appears. Its hot row starts at -1 and becomes 0
 * if the rows it was given are not empty. */
AM2_Widget *__attribute__((thiscall)) ListBoxConstruct(AM2_Widget *w,
                                                       int32_t left,
                                                       int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       void *rows,
                                                       int32_t callback,
                                                       int32_t arg6C,
                                                       int32_t ownsRows);

/* Original: 0x00454640, thiscall, `ret 0x2C`. The CHECKBOX -- four sprites,
 * a rectangle, its record index, a caption and a change handler.
 *
 * Its LEFT-click action is written by the constructor, not by the caller:
 * ADDR_CHECKBOX_TOGGLE. The caller's handler goes to CHECK_OFF_ON_CHANGE. */
AM2_Widget *__attribute__((thiscall)) CheckBoxConstruct(AM2_Widget *w,
                                                        const char *b0,
                                                        const char *b1,
                                                        const char *b2,
                                                        const char *b3,
                                                        int32_t left, int32_t top,
                                                        int32_t width,
                                                        int32_t height,
                                                        int32_t group,
                                                        const char *caption,
                                                        void (__cdecl *onChange)(AM2_Widget *));

/* Original: 0x00455FF0, thiscall, `ret 0x18`. The SCROLL BAR -- the three
 * volume sliders. It builds its own two arrows, each a BUTTON with a NULL
 * first bitmap and VTABLE_ARROW stamped over the button's, which is the one
 * place the null-bitmap branch is taken. Its RANGE is a literal twenty. */
AM2_Widget *__attribute__((thiscall)) ScrollBarConstruct(AM2_Widget *w,
                                                         int32_t left,
                                                         int32_t top,
                                                         int32_t width,
                                                         int32_t height,
                                                         AM2_Widget *parent,
                                                         int32_t span);

/* Original: 0x00455970, thiscall, `ret 0x24` -- nine stack arguments. The
 * VERTICAL bar with arrows: the connection list's and the player list's. The
 * horizontal scroll bar transposed, with the same two-arrows-from-buttons
 * trick. */
AM2_Widget *__attribute__((thiscall)) ArrowBarConstruct(AM2_Widget *w,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t width,
                                                        int32_t height,
                                                        AM2_Widget *parent,
                                                        const char *b0,
                                                        const char *b1,
                                                        int32_t span,
                                                        int32_t flag50);

/* Original: 0x004566F0, thiscall, `ret 0x14`. The TYPEWRITER, and the
 * WORD-WRAP is the constructor: it folds the message into TYPER_OFF_TEXT with
 * `|` between the lines rather than storing it. See widget.cpp for the two
 * offsets the loop keeps and what a too-wide word does. */
AM2_Widget *__attribute__((thiscall)) TyperConstruct(AM2_Widget *w,
                                                     int32_t left, int32_t top,
                                                     int32_t width,
                                                     int32_t height,
                                                     const char *message);

/* Original: 0x0044D730. The TITLE SCREEN -- the one arm of the menu table
 * that builds its screen inline rather than calling a constructor. It also
 * holds the binary patch that removes MULTI-PLAYER, which a reconstruction
 * cannot honour; see widget.cpp. */
void __cdecl OpenTitleScreen(void);

/* The OPTIONS menu's four buttons and CONFIRM GAME EXIT's OK: 0x0044E670,
 * 0x0044FD40, 0x0044FD70, 0x0044FDA0 and 0x0044EE30. */
void __cdecl OnMenuBack(AM2_Widget *w);
void __cdecl OnControlsButton(AM2_Widget *w);
void __cdecl OnDifficultyButton(AM2_Widget *w);
void __cdecl OnAudioButton(AM2_Widget *w);
void __cdecl OnQuitOk(AM2_Widget *w);

/* The AUDIO dialog's three bars: 0x0044F2A0, 0x0044F2E0, 0x0044F320. Each is
 * the bar's onChange, so the argument is the scroll bar itself. */
void __cdecl OnVolumeEffects(AM2_Widget *w);
void __cdecl OnVolumeMusic(AM2_Widget *w);
void __cdecl OnVolumeVoice(AM2_Widget *w);

/* The OPTIONS dialogs' OK and CANCEL: 0x00451100, 0x0044F8B0, 0x0044F930
 * and 0x0044EA80, with 0x0044F860 the three-volume apply underneath. */
void __cdecl OnControlsCancel(AM2_Widget *w);
void __cdecl OnAudioCancel(AM2_Widget *w);
void __cdecl OnAudioOk(AM2_Widget *w);
void __cdecl OnDifficultyOk(AM2_Widget *w);
void __cdecl ApplyVolumes(int32_t effects, int32_t music, int32_t voice);

/* CONTROLS' OK and DEFAULT: 0x00451150 and 0x004511A0. */
void __cdecl OnControlsOk(AM2_Widget *w);
void __cdecl OnControlsDefault(AM2_Widget *w);

/* The four arrows: 0x004557F0, 0x004558B0 on the arrow bar beside a list,
 * and 0x00455ED0, 0x00455F60 on the scroll bar. The argument is the ARROW;
 * the bar it belongs to is at ARROW_OFF_OWNER. */
void __cdecl OnArrowUp(AM2_Widget *w);
void __cdecl OnArrowDown(AM2_Widget *w);
void __cdecl OnArrowLeft(AM2_Widget *w);
void __cdecl OnArrowRight(AM2_Widget *w);

/* SELECT PLAYER's three buttons (0x00451300, 0x00451330, 0x00451380),
 * DELETE PLAYER's CANCEL (0x00450A10) and the REPLAY prompt's OK
 * (0x0044F1B0). */
void __cdecl OnRecruit(AM2_Widget *w);
void __cdecl OnDeletePlayer(AM2_Widget *w);
void __cdecl OnSelectPlayer(AM2_Widget *w);
void __cdecl OnDelPlayerCancel(AM2_Widget *w);
void __cdecl OnReplayOk(AM2_Widget *w);

/* Original: 0x00453940, thiscall. The three-field record's reset -- free each
 * row's own string, free the array, clear the count. */
void __attribute__((thiscall)) RecordReset(void *rec);

/* Original: 0x004512A0. SELECT PLAYER's row callback. A list box dispatches
 * `callback(list, rows, selected)`; this one ignores the first. */
void __cdecl SelectPlayerRow(AM2_Widget *list, AM2_ListRows *rows,
                             int32_t selected);

/* Original: 0x0044D520. The WM_CHAR consumer EditTakeFocus installs, and the
 * whole of a text field's typing behaviour. It works on the FOCUSED field
 * rather than on an argument. */
void __cdecl EditCharHandler(uint32_t ch, uint32_t lo, uint32_t hi);

/* Original: 0x004542F0, thiscall. The base button's constructor -- returns
 * `this`, as every i386 MSVC constructor does. */
AM2_Widget *__attribute__((thiscall)) ButtonBaseConstruct(AM2_Widget *w);

/* Original: 0x00454760. Every checkbox's left-click handler, installed by the
 * constructor. Toggles the tick, repaints, then calls the caller's own
 * handler at CHECK_OFF_ON_CHANGE if there is one. */
void __cdecl CheckboxToggle(AM2_Widget *w);

/* Original: 0x0044DBB0, thiscall. SELECT MAP -- the campaign's level picker,
 * and the one screen whose list comes out of a PARSED FILE. */
AM2_Widget *__attribute__((thiscall)) SelectMapConstruct(AM2_Widget *w,
                                                         const char *bmp);

/* Original: 0x00451AF0, thiscall. ENTER NAME -- RECRUIT's dialog, and the
 * simplest screen that owns an edit box. */
AM2_Widget *__attribute__((thiscall)) EnterNameConstruct(AM2_Widget *w,
                                                         const char *bmp);

/* Original: 0x0044FE50, thiscall. DELETE GAME -- the one screen in the table
 * built two ways: a panel on the title screen, and straight onto the screen
 * with the panel's offset folded into every rectangle when in a mission. */
AM2_Widget *__attribute__((thiscall)) DeleteGameConstruct(AM2_Widget *w,
                                                          const char *bmp,
                                                          int32_t flag);

/* Original: 0x0044DFA0, thiscall. MOVIES -- twelve thumbnails in three pages
 * of four, and the only screen that builds its buttons out of SPRITES it
 * loaded itself rather than out of bitmap names. */
AM2_Widget *__attribute__((thiscall)) MoviesConstruct(AM2_Widget *w,
                                                      const char *bmp);

/* Original: 0x004520E0, thiscall. LOAD GAME -- the campaign's save picker,
 * and the second of the two screens built two ways. */
AM2_Widget *__attribute__((thiscall)) LoadGameConstruct(AM2_Widget *w,
                                                        const char *bmp,
                                                        int32_t flag);

/* The three button classes the multiplayer host/join panel builds one of per
 * player row: 0x004329A0, 0x00432E20 and 0x00433030. All three derive from
 * the base button and carry their row in the base's 0x0058. */
AM2_Widget *__attribute__((thiscall)) MpNameConstruct(AM2_Widget *w,
                                                      const char *text,
                                                      int32_t left, int32_t top,
                                                      int32_t width,
                                                      int32_t height,
                                                      int32_t flag,
                                                      uint8_t ink,
                                                      uint8_t paper,
                                                      int32_t row);
AM2_Widget *__attribute__((thiscall)) MpColourConstruct(AM2_Widget *w,
                                                        int32_t left,
                                                        int32_t top,
                                                        int32_t row);
AM2_Widget *__attribute__((thiscall)) MpTeamConstruct(AM2_Widget *w,
                                                         int32_t left,
                                                         int32_t top,
                                                         int32_t row);

/* Their left handlers, 0x00432EC0 and 0x004330E0 -- and what named the two
 * classes. A guest changes nothing locally: it sends and waits. */
void __cdecl OnMpColour(AM2_Widget *w);
void __cdecl OnMpTeamLeft(AM2_Widget *w);
void __cdecl OnMpTeamRight(AM2_Widget *w);
void __cdecl OnMpName(AM2_Widget *w);

/* 0x00431CE0, the panel's chat line: log, broadcast, empty, repaint. */
void __cdecl OnChatEnter(AM2_Widget *w);

/* The two colours a player row's name is drawn in. The ink says how the link
 * is behaving, the paper says whether the player is ready; both return a
 * palette index in AL. */
uint8_t __cdecl MpNameInk(int32_t row);
uint8_t __cdecl MpNamePaper(int32_t row);
void __attribute__((thiscall)) MpNameSetInk(AM2_Widget *w, uint8_t ink);

/* The map thumbnail and what keeps it, the checksums and the ready flag in
 * step with the chosen map. RefreshMapSelection is called from three places
 * that have no panel at all, which is why the widget half is conditional. */
void __cdecl ShowBadMapPreview(AM2_Widget *preview);

/* 0x00430140. Clear a list box's rows and refill them from a text file in
 * `rules/`, one row per line, newline and all. */
void __cdecl FillListFromRules(const char *path, void *panel);

/* 0x004316D0, slot 2 of the multiplayer panel: grey the row buttons on the
 * same policy their handlers guard on, then push five numbers into text. */
void __attribute__((thiscall)) MpPanelUpdate(AM2_Widget *w);

/* 0x00430480, slot 0: release the panel's two GLOBAL sprite arrays and chain
 * to the dialog base. */
void __attribute__((thiscall)) MpPanelDestruct(AM2_Widget *w);
void __cdecl RefreshMapSelection(void);

/* The save-game family's buttons: 0x00451AC0, 0x00452010, 0x00451F10,
 * 0x00450180 and 0x00451FB0. */
void __cdecl OnEnterNameCancel(AM2_Widget *w);
void __cdecl OnLoadGameBack(AM2_Widget *w);
void __cdecl OnLoadGameDelete(AM2_Widget *w);
void __cdecl OnDelGameCancel(AM2_Widget *w);
void __cdecl OnLoadGameNew(AM2_Widget *w);

/* Original: 0x00455C10, thiscall `ret 0x10`. Repaint through the nearest
 * ancestor that owns a sprite, CLIPPED TO THIS WIDGET's rectangle -- which
 * is what makes it not WidgetRepaint. The clip argument is ignored. */
void __attribute__((thiscall)) RepaintAncestor(AM2_Widget *w, RECT clip);

/* The two remaining row callbacks: 0x0044DEA0 and 0x00451EA0. */
void __cdecl SelectMapRow(AM2_Widget *list, AM2_ListRows *rows,
                          int32_t selected);
void __cdecl LoadGameRow(AM2_Widget *list, AM2_ListRows *rows,
                         int32_t selected);

/* MOVIES' two handlers and LOAD GAME's LOAD: 0x0044E580, 0x0044E610 and
 * 0x00452060. */
void __cdecl OnMovieNextPage(AM2_Widget *w);
void __cdecl OnMoviePlay(AM2_Widget *w);
void __cdecl OnLoadGameLoad(AM2_Widget *w);

int32_t __cdecl KeyNameIndexOf(uint8_t scancode);

/* 0x00453A30, 16 callers, thiscall. Append one named entry to a list object:
 * {int32 count; row *base}, where a row is 0x104 bytes -- a name of up to
 * 0x100 and a dword beside it.
 *
 * Every append reallocs to exactly count+1 rows, so filling a list of n costs
 * n reallocs and n copies of everything before it; nothing rounds up. And the
 * name is copied with no bound at all, so a name of 0x100 or more runs into
 * the value and past the row. Both are the original's, and the shipped lists
 * are short. */
void __attribute__((thiscall)) ListAdd(void *list, const char *name,
                                       void *value);

/* 0x004539A0. Drop row zero, by rebuilding the array one row shorter. */
void __attribute__((thiscall)) ListDropOldest(void *list);

/* 0x00453910, thiscall, 11 callers: the three-field record's constructor --
 * two zeroes and the caller's value.
 *
 * It RETURNS `this`, and that is not decoration. The body opens `mov eax, ecx`
 * and every i386 MSVC constructor does; the caller at 0x00451473 stores the
 * result straight into the dialog's 0x0064, and the list it builds there is
 * what ListAdd then writes through.
 *
 * Declared `void` this returned the value argument instead -- so the SELECT
 * PLAYER and COMM CHANNEL dialogs got a list pointer of 1 and the process
 * died inside ListAdd. See STATUS.md; nothing static caught it and the two
 * A/B configurations that reach those dialogs are the only witnesses. */
void *__attribute__((thiscall)) RecordCtor(void *rec, int32_t value);
void __attribute__((thiscall)) RecordResetAlias(void *rec);

/* 0x00431D70. Empties the menu message log -- a null check and a thiscall
 * tail-jump into RecordReset, and nothing else. */
void __cdecl ClearMenuMsgs(void);

/* 0x004269B0. WndProc's activation handler: repaints the dialog and the
 * bitmap, each TWICE with no change of draw target between. */
void __cdecl OnAppActivated(void);

/* 0x00454AD0, thiscall. Give the multiplayer map preview a new bitmap; the
 * pointer goes in the class's own slot AND the base's sprite. */
void __attribute__((thiscall)) MpPreviewSetBitmap(void *self, const char *name);

/* 0x00413A30, four callers. Repaint one HUD widget if it has been marked. */
void __cdecl HudRepaintOne(void);

/* 0x0041A170, three callers. The width of the HUD panel, or 0 when there is
 * none -- and the 0 is what lets the callers clamp without a special case. */
int32_t __cdecl HudPanelWidth(void);

/* 0x00414430. Put the pointer into one of seven modes, installing that mode's
 * pick predicate, release action and overlay row. Two of the seven run their
 * action at once and revert to mode 0. */
void __cdecl SetPointerMode(int32_t mode);

/* 0x004144A0. Append one line to the HUD's message log. A non-zero colour is
 * written as a '^' escape ahead of the text. */
void __cdecl HudMessage(const char *text, int32_t colour);

/* 0x0044DB90. Delete whatever screen is up and clear the global -- the same
 * three instructions the five factories open with, as a function. */
void __cdecl CloseScreen(void);

int widget_install(void);

#ifdef __cplusplus
}
#endif

/* Original: 0x00414370. Update the three top-level HUD widgets through vtable
 * slot 2, then run two further steps. */
void __cdecl HudUpdate(void);

/* Original: 0x004143A0. Paint the three top-level HUD widgets to the back
 * buffer through vtable slot 1. */
void __cdecl HudPaint(void);

#endif /* AM2_WIDGET_H */
