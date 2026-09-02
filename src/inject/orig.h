/* Typed pointers to functions still living inside the original ArmyMen2.exe.
 *
 * Relocations are stripped from the executable, so it can only ever map at its
 * preferred base of 0x400000 and these absolute addresses are stable.
 *
 * IMPORTANT: ArmyMen2.exe statically links the MSVC 6 CRT. Its FILE handles,
 * heap and locale state are entirely separate from the msvcrt.dll that our
 * injected DLL links against. Never pass a game FILE* to our own fread/fclose,
 * or a game pointer to our own free(). Route through the game's own CRT via the
 * declarations below until the owning subsystem is itself reconstructed.
 */

#ifndef AM2_ORIG_H
#define AM2_ORIG_H

#include <stdint.h>
#include <stddef.h>

/* ---- addresses ------------------------------------------------------- */

#define AM2_IMAGE_BASE      0x00400000u

/* Game code */
#define ADDR_CHECK_SAVE_TAG 0x004235D0u  /* BOOL(FILE*,uint32_t,const char*,int32_t) */
/* The mirror, and the reason CheckSaveTag has a tag to check. 46 callers --
 * three times CheckSaveTag's, because a section that is written once may be
 * bracketed by several markers. It is `fwrite(&tag, 4, 1, fp)` and nothing
 * else; the destination-aliasing subtlety CheckSaveTag has does not arise
 * when the value is going out rather than coming in. */
#define ADDR_WRITE_SAVE_TAG 0x00423680u  /* void(FILE*, uint32_t) */
/* 0x00428760, two callers, both inside one save function. Write a script-name
 * reference: either the "no name" tag alone, or the name tag, the length and
 * the string. */
#define ADDR_SAVE_SCRIPT_NAME 0x00428760u  /* int32_t(am2_FILE *, void *rec) */
/* 0x004287E0, one caller -- the load half of the pair, and it reads exactly
 * what the save half wrote: a tag, then for a real name a length and that many
 * bytes. */
#define ADDR_LOAD_SCRIPT_NAME 0x004287E0u  /* int32_t(am2_FILE *, void *rec) */
/* 0x0043F910, one caller, and that caller is LoadScriptName. Lower-case the
 * name, find it in the script name table and bind the record to it -- and when
 * the name is already taken, make a fresh one by appending "_1", "_2", ... off
 * the format at 0x00485D90.
 *
 * IT LOWER-CASES THE CALLER'S BUFFER IN PLACE. This macro said `const char *`
 * for as long as the function was reached through it, which is the same
 * mistake ADDR_BOX_ACTION's `int32_t` was: a prototype nothing on the C side
 * had to satisfy. */
#define ADDR_SCRIPT_UNIQUE_NAME 0x0043F910u /* void(void *rec, char *name) */
#define AM2_SCRIPT_UNIQUE_BUF 0x40u   /* the stack buffer it builds names in */
#define AM2_STR_UNIQUE_SUFFIX 0x00485D90u  /* "%s_%d" */
#define AM2_SAVED_NAME_MAX    0x100u  /* LoadScriptName's stack buffer */
#define AM2_SAVETAG_NAME      0x06660669u
#define AM2_SAVETAG_NO_NAME   0x06660670u
/* The record SaveScriptName is handed carries the table index at +0x0C, and
 * the value the name is to stand for at +0x04 -- which is what 0x0043F910
 * writes into the table entry, so it is a name-table VALUE and named as one
 * rather than guessed at. */
#define SCRIPT_REF_OFF_NAME_INDEX 0x0Cu
#define SCRIPT_REF_OFF_VALUE      0x04u
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
/* RETURNS ITS ARGUMENT -- see rect.h, which is the authority here. This comment
 * said `void` and that is the stale remnant of the first reading: it happened
 * to work because the compiler also chose eax to hold `r`, by luck and not by
 * contract, and a different optimisation level would have broken all 186 call
 * sites at once. Prefer the reconstructed declaration over the note beside an
 * address; the header is maintained with the code. */
#define ADDR_RECT_SET       0x0042E1C0u  /* AM2_Rect *(AM2_Rect*,int32 x4) */
#define ADDR_CLAMP          0x0042E180u  /* int32_t(int32_t v, int32_t lo, int32_t hi) */
#define ADDR_POINT_IN_RECT  0x0042E1F0u  /* int32_t(const AM2_Rect*, const AM2_Point*) */

#define ADDR_CLIP_RECT      0x0042E220u  /* int32_t(src, clip, int32*, int32*, out) */

/* Text rendering. */
#define ADDR_DRAW_TEXT      0x00446930u  /* void(x,y,str,font,?,colour) */
/* 0x00446AB0: the clipped sibling of the above, and the one the menu widgets
 * use. Same leading arguments, then the clip rectangle BY VALUE and the
 * colour. `^` in the string is an escape that rewrites the colour argument. */
#define ADDR_DRAW_TEXT_CLIPPED 0x00446AB0u /* void(x,y,str,font,RECT,colour) */
/* 0x00453BC0 / 0x00453BA0, thiscall: the base widget's destructor and the
 * MSVC scalar deleting destructor around it, which is vtable slot 0.
 * 0x00454EF0 / 0x00454ED0 are the label's pair. */
#define ADDR_WIDGET_DESTRUCT    0x00453BC0u /* void(AM2_Widget *) */
#define ADDR_WIDGET_DELETE      0x00453BA0u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_LABEL_DESTRUCT     0x00454EF0u /* void(AM2_Widget *) */
#define ADDR_LABEL_DELETE       0x00454ED0u /* AM2_Widget *(AM2_Widget *, int32_t) */
/* The three per-key input queries, all indexed by DirectInput scancode and all
 * masked to 8 bits by the callee. 0x00427450 answers "is it down now" from the
 * top bit of the current buffer; 0x00427470 answers "did it change" by xoring
 * current against previous; 0x004274A0 CONSUMES one, copying the current byte
 * over the previous one and clearing that key's entry in ADDR_KEY_PRESSED, so
 * the edge is not seen twice. The idiom `!IsKeyDown(k) && KeyChanged(k)` is
 * the key being RELEASED, which is what the menus act on. */
#define ADDR_KEY_PRESSED_FN     0x00427430u /* int32_t(int32_t dik) */
#define ADDR_IS_KEY_DOWN        0x00427450u /* int32_t(int32_t dik) */
#define ADDR_KEY_CHANGED        0x00427470u /* int32_t(int32_t dik) */
#define ADDR_CONSUME_KEY        0x004274A0u /* void(int32_t dik) */
/* The key BINDINGS: two DirectInput scancodes per action, a primary and an
 * alternate, and 0x004274F0 answers "is either of this action's keys down".
 * The first four actions bind a letter and an arrow -- W/UP, S/DOWN, A/LEFT,
 * D/RIGHT -- and the rest bind one key with a zero beside it, which reads as
 * no key and is tested anyway. 26 callers, so this is how the game asks about
 * a control rather than about a key. The names are ours. */
#define ADDR_KEY_BINDINGS       0x004854BCu /* uint8_t[][2], scancode pairs */
/* 0x004275B0, four callers, and the THIRD member of the binding-aware key
 * family: 0x004274F0 is "either bound key is down", 0x00427530 is "just
 * pressed", and this one is "just RELEASED" -- for each of the two bound
 * scancodes it wants the key NOT down and its bit changed between the two poll
 * buffers.
 *
 * It nearly went in as ADDR_ACTION_KEY_DOWN, which is already the name of
 * 0x004274F0. Grepping the ADDRESS is not enough; the NAME has to be grepped
 * too, or two different functions end up under one name -- which is worse than
 * an alias, and which no ratchet here would have caught. */
#define ADDR_ACTION_KEY_RELEASED 0x004275B0u /* int32_t(int32_t action) */
/* The action that shows the mission's info bitmap. The Boot Camp dialog names
 * it on screen: "HIT F1 DURING GAME FOR MORE INFO". */
#define AM2_ACTION_SHOW_INFO    0x14
/* 0x00424CA0, one caller -- the per-frame path. In-mission input: escape, the
 * info bitmap, and mouse edge scrolling. */
#define ADDR_MISSION_INPUT      0x00424CA0u /* void(void) */
/* Set while something else owns the input; the mission handler gives way to it
 * exactly as it does to g_charHandler. */
#define ADDR_INPUT_SUPPRESS     0x00511E44u /* int32_t */
/* What the info action does in a network game instead of pausing. */
#define ADDR_SHOW_INFO_MP       0x00462A40u /* void(void) */
/* The flag that function toggles -- an INFO OVERLAY the map painter tests at
 * two sites. It is the whole of the function's effect. */
#define ADDR_INFO_OVERLAY_ON    0x0048C7D8u /* int32_t, 0 or 1 */
#define AM2_SCROLL_MARGIN       3           /* pixels from the edge */
#define AM2_STATE_ESCAPE_MENU   0x17
#define AM2_SUBSTATE_INFO_BITMAP 0x16
/* The built-in bindings, one scancode per row and no second column, which is
 * why DEFAULT writes only the first byte of each pair -- as does OK. */
#define ADDR_KEY_DEFAULTS       0x0048AE80u /* uint8_t[21] */
#define ADDR_ACTION_KEY_DOWN    0x004274F0u /* int32_t(int32_t action) */
/* 0x00427530, 19 callers: the same two keys, but "just pressed" rather than
 * "down" -- a key counts only if it is down AND its bit differs between the
 * two poll buffers. */
#define ADDR_ACTION_KEY_PRESSED 0x00427530u /* int32_t(int32_t action) */
/* 0x00453E80, thiscall, 21 callers: the base widget's per-frame update. Places
 * itself through WidgetScreenRect and walks its children calling THEIR slot 2,
 * which is what makes WidgetScreenRect the busiest function in the tree.
 * 0x00454AC0 is a one-instruction `jmp` thunk onto it. */
#define ADDR_WIDGET_UPDATE      0x00453E80u /* void(AM2_Widget *) */
/* 0x00453DB0 and 0x00453E20, thiscall taking an `announce` flag: move the
 * focus to the next / previous eligible sibling, wrapping through the parent's
 * child list, skipping anything whose 0x0050 is clear or whose 0x004C is set,
 * and finishing by dispatching slot 3 on whatever they land on. */
/* 0x00453C40, thiscall: the base widget's painter -- draw my own sprite if I
 * have one, then paint every child. 0x00454A90 and 0x00454BA0 are two levels
 * of forwarding thunk onto it, and 0x00454BA0 is what 18 vtables carry. */
#define ADDR_WIDGET_PAINT       0x00453C40u /* void(AM2_Widget *, RECT) */
#define ADDR_WIDGET_PAINT_FWD1  0x00454A90u /* void(AM2_Widget *, RECT) */
#define ADDR_WIDGET_PAINT_FWD2  0x00454BA0u /* void(AM2_Widget *, RECT) */
/* 0x00453D50, thiscall: append a child to a widget's child list. */
#define ADDR_WIDGET_ADD_CHILD   0x00453D50u /* void(AM2_Widget *, AM2_Widget *) */
#define ADDR_WIDGET_LAST_SIBLING 0x00453D90u /* AM2_Widget *(AM2_Widget *) */
#define ADDR_WIDGET_FOCUS_NEXT  0x00453DB0u /* void(AM2_Widget *, int32_t) */
#define ADDR_WIDGET_FOCUS_PREV  0x00453E20u /* void(AM2_Widget *, int32_t) */
#define ADDR_WIDGET_UPDATE_THUNK 0x00454AC0u /* void(AM2_Widget *) */
/* 0x00455100: the same one-instruction shape onto WidgetRepaint, slot 4 in
 * two vtables. */
#define ADDR_WIDGET_REPAINT_THUNK 0x00455100u /* void(AM2_Widget *) */
/* 0x00456970 and 0x00456990: a second scalar deleting destructor over a `jmp`
 * thunk onto the base destructor. Slot 0 in three vtables. */
#define ADDR_WIDGET_DELETE_ALT  0x00456970u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_WIDGET_DESTRUCT_THUNK 0x00456990u /* void(AM2_Widget *) */
/* The game's own import slot for GetTickCount, called through rather than
 * imported, so a flat-ish module needs no Win32 declaration for it. */
#define IAT_GET_TICK_COUNT      0x0046F084u
/* The CONTROLS dialog's key table: 95 records of {uint32_t scancode, const
 * char *name} from 0x0048AF28 to 0x0048B220. The scancodes are DirectInput's
 * -- record 1 is 8 for '7' -- and the names are what the dialog shows. */
#define ADDR_KEY_NAME_TABLE     0x0048AF28u
#define ADDR_KEY_NAME_TABLE_END 0x0048B220u
/* 0x00450BF0, two callers: the INDEX of the record whose scancode has this low
 * byte, or -1. Only the low byte is compared, which is all a DirectInput
 * scancode is. */
#define ADDR_KEY_NAME_INDEX_OF  0x00450BF0u  /* int32_t(uint8_t scancode) */
/* The string a key row shows when it is bound to nothing. There is a second
 * copy of "None" at 0x0048A134 inside the table itself; this is the one the
 * duplicate-clearing loop stores. */
#define ADDR_STR_NONE           0x0048AF1Cu
/* 0x00450C10: index of the key that has just gone down, or -1. */
#define ADDR_FIND_PRESSED_KEY   0x00450C10u /* int32_t(void) */
/* 0x00450D50, thiscall, slot 2 of the key-capture row. */
#define ADDR_KEYROW_UPDATE      0x00450D50u /* void(AM2_Widget *) */
/* 0x00455C80, thiscall, slot 1: picks a sprite out of a small array by an
 * index, centres it in the widget, and draws it clipped. */
#define ADDR_MULTI_SPRITE_PAINT 0x00455C80u /* void(AM2_Widget *, RECT) */
/* 0x00455090 / 0x00455070: the list box's destructor and its deleting wrapper.
 * The row array is freed only when 0x0064 says the list owns it, and 0x00453930
 * is the array's own cleanup, called before the storage goes back. */
#define ADDR_LIST_DESTRUCT      0x00455090u /* void(AM2_Widget *) */
#define ADDR_LIST_DELETE        0x00455070u /* AM2_Widget *(AM2_Widget *, int32_t) */
/* One `jmp` to ADDR_RECORD_RESET, which is the same three-field record under
 * a different vocabulary -- 0x00453910 is its constructor and 0x00453940 its
 * reset, and the list box's row array is one of the things it holds. Two names
 * for one shape, both the program's callers' rather than the program's. */
#define ADDR_LIST_ROWS_CLEANUP  0x00453930u /* thiscall void(void *), one jmp */
/* 0x00455180, thiscall, slot 1 of the list box: clear, then draw every visible
 * row with an ink chosen from the row's state. */
#define ADDR_LIST_DRAW          0x00455180u /* void(AM2_Widget *, RECT) */
/* The two palette bytes it reads are both already named: the background is
 * ADDR_BACKGROUND_COLOUR and the highlight is ADDR_VIEW_RECT_COLOUR. */
/* 0x00454A30 / 0x00454A10: the one-sprite icon's destructor and its deleting
 * wrapper -- vtable 0x0046FC70. The sprite is at 0x0058, which is the TEXT
 * pointer in a label; the tails disagree as usual. */
#define ADDR_ICON_DESTRUCT      0x00454A30u /* void(AM2_Widget *) */
#define ADDR_ICON_DELETE        0x00454A10u /* AM2_Widget *(AM2_Widget *, int32_t) */
/* 0x00456CA0 / 0x00456C80: the blinker's, which chains to the icon's. */
#define ADDR_BLINKER_DESTRUCT   0x00456CA0u /* void(AM2_Widget *) */
#define ADDR_BLINKER_DELETE     0x00456C80u /* AM2_Widget *(AM2_Widget *, int32_t) */
/* 0x004541E0 and 0x004541C0: the three-state button's destructor and the
 * deleting wrapper over it. The destructor carries an MSVC SEH prologue which
 * is deliberately not reproduced -- see CLAUDE.md. */
#define ADDR_BUTTON_DESTRUCT    0x004541E0u /* void(AM2_Widget *) */
#define ADDR_BUTTON_DELETE      0x004541C0u /* AM2_Widget *(AM2_Widget *, int32_t) */
/* 0x00456D40 and 0x00456DC0: the two-state indicator is a BLINKER. The first
 * is its slot 2 -- flip on a timer, count the flashes down, stop. The second
 * starts one, taking the period and the number of flashes. */
#define ADDR_BLINKER_UPDATE     0x00456D40u /* void(AM2_Widget *) */
#define ADDR_BLINKER_START      0x00456DC0u /* void(AM2_Widget *, uint32_t, int32_t) */
/* 0x00456D00, thiscall, slot 1 of a two-state indicator: one flag chooses
 * between two sprites, then the base painter draws it. */
#define ADDR_TOGGLE_PAINT       0x00456D00u /* void(AM2_Widget *, RECT) */
/* 0x00455110, thiscall, slot 3 of the list box: repaint the current row, then
 * take focus normally. */
#define ADDR_LIST_TAKE_FOCUS    0x00455110u /* void(AM2_Widget *, int32_t) */
/* 0x00454310, thiscall, slot 2 in four vtables: a button's mouse handling,
 * with optional auto-repeat while a button is held. */
#define ADDR_BUTTON_UPDATE      0x00454310u /* void(AM2_Widget *) */
/* 0x00454270, thiscall, slot 1 in two vtables: a button that picks one of
 * three sprites -- normal, focused, pressed -- and then paints normally. */
#define ADDR_BUTTON_PAINT       0x00454270u /* void(AM2_Widget *, RECT) */
/* The edit box -- vtable 0x0046FC98, the class CLAUDE.md names as the owner of
 * g_charHandler. 0x0065A05C holds whichever one currently has the focus, and
 * 0x0044D520 is the WM_CHAR consumer it installs while it does. */
#define ADDR_FOCUSED_EDIT       0x0065A05Cu /* AM2_Widget *, or null */
#define ADDR_STR_KEY_HANDLER_LEAK 0x00476E5Cu /* "Error: Key handler not freed\n" */
#define ADDR_EDIT_CHAR_HANDLER  0x0044D520u /* void(wparam, lo, hi) */
#define ADDR_EDIT_DELETE        0x00454CA0u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_EDIT_DESTRUCT      0x00454CC0u /* void(AM2_Widget *) */
#define ADDR_EDIT_TAKE_FOCUS    0x00454CE0u /* void(AM2_Widget *, int32_t) */
#define ADDR_EDIT_REPAINT       0x00454D00u /* void(AM2_Widget *) */
#define ADDR_EDIT_UPDATE        0x00454E20u /* void(AM2_Widget *) */
/* The multiplayer panel's chat line, installed by pointer from the panel
 * constructor at 0x00430B87 and referenced nowhere else. */
#define ADDR_ON_CHAT_ENTER      0x00431CE0u /* void(AM2_Widget *) */
#define ADDR_EDIT_DRAW          0x00454D20u /* void(AM2_Widget *, RECT) */
/* The label subclass that highlights when focused -- vtable 0x0046FB80, and
 * the class the CONTROLS panel builds its captions from. */
#define ADDR_FOCUSLABEL_DELETE    0x00450CC0u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_FOCUSLABEL_DESTRUCT  0x00450CE0u /* void(AM2_Widget *) */
#define ADDR_FOCUSLABEL_DRAW      0x00450CF0u /* void(AM2_Widget *, RECT) */
#define ADDR_FOCUSLABEL_TAKE_FOCUS 0x00450D40u /* void(AM2_Widget *, int32_t) */
/* There is NO third one. A widget vtable slot points at 0x0045CAA0, which is
 * a bare `ret`, and that reads like a class whose update does nothing -- but
 * 0x0045CAA0 is ADDR_LOG, thirty lines above, stubbed to `ret` in the retail
 * build and patched by src/inject/gamelog.c to capture the game's output. The
 * linker folded the two: an empty virtual and a stubbed varargs logger are the
 * same single byte, and identical-COMDAT folding gives them one address.
 * Patching it silenced every game message while the game ran perfectly. */
/* 0x00454BD0, thiscall, slot 2 of 17 classes: the base update with a cancel
 * key in front of it. */
#define ADDR_WIDGET_UPDATE_CANCEL 0x00454BD0u /* void(AM2_Widget *) */
/* 0x004274D0: copy the 256-byte current key buffer over the previous one, so
 * every edge test that follows sees no change. Called where the game wants the
 * keystroke that got it here not to be seen again. */
#define ADDR_LATCH_KEY_STATE    0x004274D0u /* void(void) */
/* 0x00454070, thiscall, vtable slot 3 of 30 of the 33 widget classes. Move
 * focus to this widget within its parent. */
#define ADDR_WIDGET_TAKE_FOCUS  0x00454070u /* void(AM2_Widget *, int32_t) */
/* 0x00453FF0, thiscall, vtable slot 4 of 29 of the 33 widget classes. Repaint:
 * clear the dirty flag, pick what to draw, and dispatch through slot 1. */
#define ADDR_WIDGET_REPAINT     0x00453FF0u /* void(AM2_Widget *) */
/* 0x00453B00, thiscall. The base constructor of the menu widget hierarchy;
 * every one of the thirty-three class constructors chains to it. */
#define ADDR_WIDGET_CONSTRUCT   0x00453B00u /* AM2_Widget *(AM2_Widget *) */
/* 0x00454E70, thiscall. The static label's constructor, taking
 * (text, x, y, w, h, font, ink, paper) -- eight stack dwords, `ret 0x20`. */
#define ADDR_LABEL_CONSTRUCT    0x00454E70u
/* 0x00453BF0, thiscall, 33 callers. Absolute rectangle of a menu widget from
 * its offset within its parent; no parent means the offset is absolute. */
#define ADDR_WIDGET_SCREEN_RECT 0x00453BF0u /* void(AM2_Widget *) */
/* 0x00455D50, thiscall: one instruction, `jmp 0x00453BF0`. It is slot 2 --
 * update -- of the multi-sprite class at 0x0046FCE8, so that class's whole
 * per-frame update is "recompute my absolute rectangle". The same shape as
 * ADDR_WIDGET_UPDATE_THUNK and ADDR_WIDGET_REPAINT_THUNK. */
#define ADDR_MULTI_UPDATE_THUNK 0x00455D50u /* void(AM2_Widget *) */
/* The horizontal scroll bar -- vtable 0x0046FCFC, and it names itself in the
 * three bitmaps its constructor loads: 03_020_00_hscrollbar.bmp for the bar
 * itself and 03_021/03_022 ltarrow and rtarrow for the two arrow children it
 * builds. 0x7C bytes. Slot 2 is the base update and slots 3 and 4 are the
 * base's, so only these two are its own. */
#define ADDR_SCROLLBAR_DELETE   0x004561C0u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_SCROLLBAR_DESTRUCT 0x004561E0u /* void(AM2_Widget *) */
#define ADDR_SCROLLBAR_PAINT    0x00456240u /* void(AM2_Widget *, RECT) */
/* The scroll bar's two arrow children, vtable 0x0046FCD4. Slots 1 and 2 are
 * the button's own paint and update, and the scroll bar's constructor builds
 * each arrow by calling the BUTTON constructor and then stamping this vtable
 * over it -- so the arrow has no constructor of its own. Its destructor is one
 * instruction, `jmp` to the button's, which means it stamps VTABLE_BUTTON and
 * never its own. Read, not tidied. */
#define ADDR_ARROW_DELETE       0x00455B50u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_ARROW_DESTRUCT     0x00455B70u /* void(AM2_Widget *) */
/* The TYPEWRITER message label, vtable 0x0046FD24. Its constructor word-wraps
 * the text it is given into a `|`-separated buffer at 0x0058; its update
 * reveals one more character every 100 ms, ticking the blinker and playing a
 * click as it goes; its painter draws the revealed prefix line by line. Six
 * confirm dialogs build it -- QUIT GAME, REPLAY, DELETE GAME, the overwrite
 * confirm, DELETE PLAYER and the plain message box -- so CONFIRM GAME EXIT is
 * one, and `tools/ab.sh quit` reaches it. */
#define ADDR_TYPER_PAINT        0x004569A0u /* void(AM2_Widget *, RECT) */
#define ADDR_TYPER_UPDATE       0x00456B20u /* void(AM2_Widget *) */
/* The palette index the matcher fills with white; palette.cpp already carries
 * the address in its named-colour table, and this is the name for it. */
#define ADDR_COLOUR_WHITE       0x004FD768u /* uint8_t */
/* The DIALOG base class, vtable 0x0046FC84 -- one level under the icon, whose
 * destructor it jumps straight to. Every full-screen dialog in the game
 * derives from it. Slots 1 and 2 are 0x00454BA0 and 0x00454BD0, the two
 * forwarding thunks already reconstructed. */
#define ADDR_DIALOG_DELETE      0x00454B70u /* AM2_Widget *(AM2_Widget *, int32_t) */
#define ADDR_DIALOG_DESTRUCT    0x00454B90u /* void(AM2_Widget *) */

/* The fifteen dialog subclasses whose destructor is the SAME two instructions
 * -- stamp my own vtable, jump to the dialog base's. Each is named from the
 * bitmap its constructor loads; the id is the value 0x00511DBC holds while it
 * is up, which the dispatcher at 0x00426400 turns into a call. Three more
 * dialogs are NOT in this list because their destructors carry an SEH frame
 * and do real work: 0x0044E510 (the film archive), 0x00453830 and 0x00455BA0. */
/* SelectMap -- 02_004_00_selectmap.bmp */
#define ADDR_DLG_SELECTMAP_DELETE         0x0044DE70u
#define ADDR_DLG_SELECTMAP_DESTRUCT       0x0044DE90u
/* Difficulty -- 02_014_00_difficulty.bmp */
#define ADDR_DLG_DIFFICULTY_DELETE        0x0044EA50u
#define ADDR_DLG_DIFFICULTY_DESTRUCT      0x0044EA70u
/* QuitGame -- 02_009_00_quitgame */
#define ADDR_DLG_QUITGAME_DELETE          0x0044EE00u
#define ADDR_DLG_QUITGAME_DESTRUCT        0x0044EE20u
/* Replay -- 02_015_00_replay */
#define ADDR_DLG_REPLAY_DELETE            0x0044F180u
#define ADDR_DLG_REPLAY_DESTRUCT          0x0044F1A0u
/* Audio -- 02_013_00_audio.bmp */
#define ADDR_DLG_AUDIO_DELETE             0x0044F830u
#define ADDR_DLG_AUDIO_DESTRUCT           0x0044F850u
/* Options -- 03_120/121/126 audio, controls, difficulty and back */
#define ADDR_DLG_OPTIONS_DELETE           0x0044FD10u
#define ADDR_DLG_OPTIONS_DESTRUCT         0x0044FD30u
/* DelGame -- 02_011_00_delgame.bmp */
#define ADDR_DLG_DELGAME_DELETE           0x00450150u
#define ADDR_DLG_DELGAME_DESTRUCT         0x00450170u
/* Overwrite -- "Are you sure you want to overwrite savefile '%s'?" */
#define ADDR_DLG_OVERWRITE_DELETE         0x004505B0u
#define ADDR_DLG_OVERWRITE_DESTRUCT       0x004505D0u
/* Its screen, and its constructor -- which is what identifies BOTH, since the
 * constructor installs VTABLE_DLG_OVERWRITE. Sitting next to the destructor in
 * the image is not evidence and is not what was used. */
#define ADDR_OVERWRITE_CTOR      0x00450320u /* thiscall, ret 8 */
#define ADDR_OPEN_OVERWRITE_GAME 0x004506A0u /* void(void) */
#define AM2_OVERWRITE_SIZE       0x64u
#define ADDR_STR_OVRGAME_BMP     0x0048B96Cu /* "02_018_00_ovrgame.bmp" */
/* DelPlayer -- 02_010_00_delplayer */
#define ADDR_DLG_DELPLAYER_DELETE         0x004509E0u
#define ADDR_DLG_DELPLAYER_DESTRUCT       0x00450A00u
/* Controls -- 03_002 default, beside OK and CANCEL */
#define ADDR_DLG_CONTROLS_DELETE          0x004510D0u
#define ADDR_DLG_CONTROLS_DESTRUCT        0x004510F0u
/* SelectPlayer -- 02_005_00_selectplayer.bmp */
#define ADDR_DLG_SELECTPLAYER_DELETE      0x004518E0u
#define ADDR_DLG_SELECTPLAYER_DESTRUCT    0x00451900u
/* NameEntry -- 02_006_00_name.bmp */
#define ADDR_DLG_NAMEENTRY_DELETE         0x00451DE0u
#define ADDR_DLG_NAMEENTRY_DESTRUCT       0x00451E00u
/* LoadGame -- 02_007_00_loadgame.bmp */
#define ADDR_DLG_LOADGAME_DELETE          0x00452650u
#define ADDR_DLG_LOADGAME_DESTRUCT        0x00452670u
/* Message -- 03_029 red, and an OK -- a plain message box */
#define ADDR_DLG_MESSAGE_DELETE           0x00452960u
#define ADDR_DLG_MESSAGE_DESTRUCT         0x00452980u
#define ADDR_MESSAGE_CTOR        0x00452750u /* thiscall, ret 8 */
#define ADDR_OPEN_MESSAGE        0x00452990u /* void(void) */
#define AM2_MESSAGE_SIZE         0x64u
/* The only one of the five whose bitmap is a GLOBAL rather than a literal --
 * whoever raises the message writes the name here first. */
/* The text `showfailure` and `showpda` copy the action's own string into,
 * beside the bitmap name they set with it. Named from those two writers. */
#define ADDR_MESSAGE_TEXT        0x00511EA4u /* char[] */
#define ADDR_MESSAGE_BMP_NAME    0x005122A4u /* char[] */
/* GameMenu -- 03_123/124/125 load, return, save and abort */
#define ADDR_DLG_GAMEMENU_DELETE          0x00452E20u
#define ADDR_DLG_GAMEMENU_DESTRUCT        0x00452E40u
/* Its screen. THE BITMAP IS "00_999_99_blank.bmp" AND THE SCREEN IS NOT BLANK:
 * the name describes the BACKDROP the dialog is built on, and the class is the
 * in-mission game menu -- ADDR_GAMEMENU_CTOR installs VTABLE_DLG_GAMEMENU.
 * Naming this one from the string it pushes would have been the call-site
 * mistake exactly. */
#define ADDR_GAMEMENU_CTOR       0x00452AA0u /* thiscall, ret 8 */
#define ADDR_OPEN_GAME_MENU      0x00452F50u /* void(void) */
#define AM2_GAMEMENU_SIZE        0x68u
#define ADDR_STR_BLANK_BMP       0x00486F90u /* "00_999_99_blank.bmp" */
/* 0x00454F00, thiscall, vtable slot 1 of the vtable at 0x0046FCAC. The static
 * label's painter -- reached BOTH through that slot and by one direct call
 * from the panel that owns the label. */
#define ADDR_LABEL_DRAW        0x00454F00u /* void(AM2_Widget *, RECT) */
/* Runtime font generation: GDI-render a character, then RLE it. */
#define ADDR_ENCODE_GLYPH   0x004464C0u  /* uint32_t(uint8_t*,int32,int32,int32) */
#define ADDR_RENDER_GLYPH   0x004465E0u  /* uint32_t(int32,char,HFONT,AM2_Rle16*,int32) */
#define ADDR_CREATE_GAME_FONT 0x00446450u /* HFONT(const char *face, int32 h, uint16 style) */
/* This was ADDR_FONT_SURFACE, named for the use font.cpp makes of it -- it
 * GDI-renders glyphs onto it -- and the comment here already said the name was
 * wrong. InitDirectDraw settles it: the surface taken off the primary with
 * DDSCAPS_BACKBUFFER when fullscreen, and a plain offscreen surface of the
 * same size when windowed, where there is no flipping chain to take one from.
 * It is what the lock target starts out pointing at, and PresentFrame is what
 * blits it to the primary. It is the back buffer, and it is named that now. */
#define ADDR_BACK_BUFFER   0x004FE08Cu  /* IDirectDrawSurface *, the back buffer */
/* The three top-level HUD widgets, in the order they are painted and updated.
 * The third is optional -- both 0x004143A0 and 0x00414370 test it for null and
 * the first two they do not. Named for their position in that sequence; what
 * each one IS has not been established here. */
#define ADDR_HUD_WIDGET_A  0x004FCF00u  /* AM2_Widget * */
#define ADDR_HUD_WIDGET_B  0x004FCF54u
/* Set to 1 in the same two instructions as ADDR_HUD_DIRTY, by 0x00418F78, and
 * read once -- by PlacementScreenClick, after a unit has gone down, to decide
 * whether to repaint the panel. One writer, one reader, and neither says what
 * it MEANS, so it keeps a field-numbered name. */
#define ADDR_PLACE_FLAG_4FCF88 0x004FCF88u  /* int32_t */
/* The facing a placed unit gets. PlacementScreenClick owns it outright --
 * the only function in the image that reads or writes it -- rotating it by
 * AM2_PLACE_FACING_STEP on two action keys and handing it to both
 * PlacementAllowed and MakePlacedUnit. A BYTE, so it wraps on its own. */
#define ADDR_PLACE_FACING      0x004FCF8Cu  /* uint8_t */
#define AM2_PLACE_FACING_STEP  4
/* 0x00413BC0, one caller. The manual placement screen's click handler, and
 * the layer above IsPlacedUnit, PlacementAllowed and RefundPlacedUnit.
 * Reconstructed in win32/widget.cpp rather than the flat place.cpp, because
 * its tail dispatches a widget's paint slot with a RECT by value. */
#define ADDR_PLACE_SCREEN_CLICK 0x00413BC0u  /* void(uint32_t) */
/* 0x004127B0, and its only two references are the two arms of the function
 * above -- so it is that function's private helper and nothing else's.
 *
 * IT IS A CURSOR, which its first instructions settle: argument 1 goes
 * straight into ADDR_MENU_ROW, the same global OverlayPrepare picks a row
 * with, and the rest of the body looks a soldier, vehicle or turret animation
 * up by kind, heading and army colour. So the ghost unit the placement screen
 * hangs off the pointer is the MENU CURSOR wearing a different sprite, not a
 * layer of its own -- and this bypasses OverlayPrepare's one-row-per-
 * millisecond throttle by writing the row itself.
 *
 * Argument 2 is the yes/no PlacementAllowed just answered, and the caller
 * passes it on a refusal as well as an acceptance, which is presumably how
 * the cursor turns red. Arguments 3 and 4 are the facing and the army. */
#define ADDR_PLACE_CURSOR_PREPARE 0x004127B0u /* void(row, ok, facing, army) */
/* The build menu's eighteen entries sit at ADDR_MENU_ROW 0x13 and up, past
 * the nineteen rows ADDR_MENU_SPRITES holds -- which is the arithmetic and
 * not a claim about what rows 0x13.. contain, since the helper above builds
 * those sprites itself rather than reading them out of that table. */
#define AM2_PLACE_CURSOR_ROW_BASE 0x13
/* The cursor row the placement screen shows over a unit it could sell. Row 0
 * is what it asks for over anything else, including empty ground. */
#define AM2_OVERLAY_ROW_SELL      0x11
/* Two more rows, each from the one pick that asks for it: the vehicle-boarding
 * hint and the "your leader can reach this" hint. Named from their use sites,
 * which is all the evidence there is -- the rows themselves are just indices
 * into the cursor sheet OverlayPrepare selects from. */
#define AM2_OVERLAY_ROW_ENEMY     4   /* what a pick shows over a foe */
#define AM2_OVERLAY_ROW_FIRE      1   /* mode 0's, over a foe in range */
#define AM2_OVERLAY_ROW_BOARD     8
#define AM2_OVERLAY_ROW_REACH     0x12
/* The click-versus-drag window, in GetTickCount ticks. Every test of it in the
 * image is `GetTickCount() - ADDR_MOUSE_PRESS_MS < this`. */
#define AM2_CLICK_MS              500
/* Actions 0..3 are up, down, left and right -- W/S/A/D with the arrow keys as
 * the alternates, read straight out of the binding table at 0x004854BC. The
 * placement screen rotates its facing with the last two. */
#define AM2_ACTION_LEFT           2
#define AM2_ACTION_RIGHT          3
#define ADDR_HUD_WIDGET_C  0x004FCF4Cu  /* may be null */
/* THE SEVEN HUD CLASSES, and every name here comes from what the widget dump
 * shows the node DRAWING -- not from its constructor, its geometry alone, or
 * the class next to it. `ctl widgets` in a live mission prints one line per
 * node with its rectangle, and the screenshot says what is in that rectangle;
 * COMMANDS is the game's own caption on the box at 480,430.
 *
 * They are their own family, NOT the thirty-three menu vtables at
 * 0x0046FAB8..0x0046FD38: slot 3 is 0x004170E0 across this group where the
 * menu classes share 0x00454070. Two of the eight in the band never appear in
 * either Boot Camp's tree or MAP 01's, so 0x0046F930 stayed unnamed for a
 * while. It is named now, from its PAINT rather than from a tree it is not in:
 * a button that draws a COUNT right-aligned in a 0x29-wide cell. Its slots 2,
 * 3 and 4 are the menu button's and the menu base's, so despite the address
 * band it belongs to that family and not to this one -- which is what the
 * slot-3 observation above already implied.
 *
 * MAP 01's HUD is identical to Boot Camp's -- same vtables, rectangles and
 * sprite ids -- so this layout is not per-map. */
/* A button with a COUNT, drawn right-aligned in AM2_COUNT_CELL_W. In neither
 * widget tree, so the paint below is verified by reading.
 *
 * READ ITS TWO COLOUR SLOTS WITH THE PUSH IN MIND. `al` is loaded from
 * ADDR_VIEW_RECT_COLOUR and stored, and a few instructions later a zero is
 * stored at what LOOKS like the same offset -- but a `push edi` sits between
 * them, so they are two different slots: the ink, defaulted to that colour,
 * and the fill, defaulted to zero. Read at face value the default ink appears
 * to be dead code and the ink appears to be read uninitialised on two of the
 * four paths, neither of which is true. */
#define VTABLE_COUNT_BUTTON    0x0046F930u
#define ADDR_COUNT_BUTTON_PAINT 0x00418DC0u /* thiscall void(w, RECT) */
#define COUNTBTN_OFF_SPR       0x68u   /* AM2_Sprite *, the normal face */
#define COUNTBTN_OFF_SPR_OFF   0x6Cu   /* AM2_Sprite *, when disabled */
#define COUNTBTN_OFF_LIT       0x70u   /* uint8_t, fills behind the sprite */
#define COUNTBTN_OFF_COUNT     0x80u   /* int32_t, formatted with "%d" */
/* The two fields between the flag and the count, both named by the class's
 * three own functions and by nothing else. +0x74 is a CALLBACK the activate
 * handler fires after it has toggled and repainted, and the null test in front
 * of it is what makes it optional. +0x78 is written by the constructor from
 * its seventh argument and read by NOTHING below the CRT line -- neither the
 * paint, the activate, nor the three inherited slots -- so it is carried and
 * not used, and the name says only where it comes from. */
#define COUNTBTN_OFF_ON_TOGGLE 0x74u   /* void (*)(AM2_Widget *), may be null */
#define COUNTBTN_OFF_ARG7      0x78u   /* int32_t, written and never read */
/* The class's own three functions. The constructor takes NINE arguments and
 * the widget dump has never seen an instance, so all three are verified by
 * reading -- see the note on VTABLE_COUNT_BUTTON above. */
#define ADDR_COUNT_BUTTON_CTOR     0x00418C20u  /* thiscall w *(w, 9 args) */
#define ADDR_COUNT_BUTTON_DELETE   0x00418CE0u  /* thiscall w *(w, flags) */
#define ADDR_COUNT_BUTTON_ACTIVATE 0x00418D00u  /* void(AM2_Widget *) */
#define ADDR_COUNT_BUTTON_DTOR     0x00418D60u  /* thiscall void(w) */
#define AM2_COUNT_SPRITE_SET   0x10   /* PreloadArmySprite's first argument */
#define AM2_COUNT_FRAME_OFF    4      /* the disabled face's frame */
#define AM2_COUNT_FILL_LIT     0xE3    /* +1 when it also has the focus */
#define AM2_COUNT_FILL_FOCUS   0xE9    /* focused but not lit */
#define AM2_COUNT_CELL_W       0x29    /* 41; the count is RIGHT-aligned in it */
#define AM2_COUNT_TEXT_DY      0x10
#define AM2_COUNT_FONT         0
#define VTABLE_HUD_TOP_STRIP   0x0046F908u  /* 0,0,640,21 */
#define VTABLE_HUD_PANEL       0x0046F944u  /* 480,21,624,480 -- HUD_WIDGET_B */
#define VTABLE_HUD_RADAR       0x0046F8B8u  /* 486,31,618,163 */
#define VTABLE_HUD_SARGE       0x0046F8CCu  /* 480,169,624,249 */
#define VTABLE_HUD_SQUAD       0x0046F8E0u  /* 480,251,624,481 */
#define VTABLE_HUD_COMMANDS    0x0046F8F4u  /* 480,430,624,478 */
#define VTABLE_HUD_EDGE_STRIP  0x0046F960u  /* 624,21,640,480 */
/* Their destructor pairs. Five distinct bodies, which is why they are written
 * out rather than made from the AM2_CLASS_DTOR macro. */
#define ADDR_HUD_TOP_DELETE    0x00417770u  /* thiscall obj *(obj, flags) */
#define ADDR_HUD_TOP_DESTRUCT  0x00417790u  /* thiscall void(obj) */
#define ADDR_HUD_PANEL_DELETE  0x00419340u
#define ADDR_HUD_PANEL_DESTRUCT 0x00419360u
/* Vtable slot 1 of the radar. It draws two things: the VIEW BOX, which is
 * ADDR_SECOND_RECT scaled from map space onto the widget, and one blip per
 * registered object.
 *
 * The view box's right edge is clamped to ADDR_BITMAP_AREA_W - 22 before the
 * intersect, and only when ADDR_NET_GAME is clear -- so a network game gets an
 * unclamped box. Reproduced; nothing here says why.
 *
 * A blip's gate is four tests deep and the third is the surprising one: the
 * rider check runs only for a type-2/3/8 object that is ALSO flagged
 * OBJ_FLAG_DESTROYED. Then the object's own REVEALED/CONCEALED/BIT4 trio and a
 * non-zero health.
 *
 * The blink for an ordinary blip is per OBJECT, not global: the colour index
 * is `~(clock + uid*8) >> 9 & 1`, so each object alternates on its own phase.
 * The two animated drawers instead share ONE global phase, (clock >> 8) % 3. */
#define ADDR_HUD_RADAR_PAINT   0x00414B50u  /* thiscall void(obj, RECT) */
#define AM2_RADAR_RIGHT_MARGIN 0x16  /* 22, taken off the bitmap width */
#define AM2_RADAR_BLINK_SHIFT  9     /* of ~(clock + uid*8) */
#define AM2_RADAR_PHASE_SHIFT  8     /* of the clock, then %3 */
#define ADDR_HUD_RADAR_DELETE  0x00414810u
#define ADDR_HUD_RADAR_DESTRUCT 0x00414830u
#define ADDR_HUD_SQUAD_DELETE  0x00415830u
#define ADDR_HUD_SQUAD_DESTRUCT 0x00415850u
#define ADDR_HUD_EDGE_DELETE   0x00419650u
#define ADDR_HUD_EDGE_DESTRUCT 0x00419670u
/* The EDGE STRIP is the selected trooper's status line, and its update is what
 * says so: it looks the local army's object up, fills a health percentage, and
 * then shows EITHER the vehicle he is riding or the item in his hand.
 *
 * A click anywhere on it TOGGLES ADDR_HUD_WIDGET_B's HUDPANEL_OFF_OPEN, so the
 * strip is also the panel's tab. It arbitrates that press through
 * ADDR_MOUSE_GRAB exactly as the top strip does. */
#define ADDR_HUD_EDGE_UPDATE   0x004196E0u  /* thiscall void(obj) */
/* Vtable slot 1 of the same class: it draws, top to bottom, exactly what the
 * update above fills. One vertical pen runs the whole function and every
 * element advances it, so the layout is a STACK and not a set of fixed
 * positions -- an element that is absent takes no space.
 *
 * Bars grow UPWARD from a baseline at pen + AM2_HUD_BAR_BASE: the rectangle is
 * `top = pen + 94 - percent`, which is why the bar width constant is 90 and
 * the baseline 94. Four pixels of margin, and nothing else in it.
 *
 * The ammo is clamped for display rather than by the update: over 99 it draws
 * AM2_HUD_AMMO_OVER, otherwise "%02d". */
#define ADDR_HUD_EDGE_PAINT    0x00419AC0u  /* thiscall void(obj, RECT) */
#define EDGE_OFF_SPRITE        0x5Cu   /* AM2_Sprite *, drawn under each label */
#define EDGE_OFF_AMMO_SPRITE   0x60u   /* AM2_Sprite *, drawn above the count */
#define AM2_HUD_BAR_BASE       0x5E    /* 94: the bar's baseline below the pen */
#define AM2_HUD_BAR_X          4       /* inset from the strip's left */
#define AM2_HUD_BAR_W          8       /* left+4 .. left+12 */
#define AM2_HUD_EDGE_GAP       8       /* after a text element */
#define AM2_HUD_EDGE_GAP_BAR   0x10    /* 16, after a bar */
#define AM2_HUD_AMMO_MAX       99
#define AM2_HUD_STR_SARGE      0x00476FA8u
#define AM2_HUD_STR_AMMO_FMT   0x00476F9Cu  /* "%02d" */
#define AM2_HUD_STR_AMMO_OVER  0x00476FA4u  /* "**" */
#define EDGE_OFF_HEALTH_PCT    0x64u   /* int32_t, the trooper's own bar */
#define EDGE_OFF_CAPTION_A     0x68u   /* char[], the vehicle or ARMOR */
#define EDGE_OFF_SECOND_PCT    0x88u   /* int32_t, whatever A names */
#define EDGE_OFF_CAPTION_B     0x8Cu   /* char[], the item in hand */
#define EDGE_OFF_AMMO          0xACu   /* int32_t, -1 when there is none */
/* Every bar is a percentage of ninety, which is the drawn width: the original
 * computes it as `value * 90 / max` with one `idiv` and no float. */
#define AM2_HUD_BAR_WIDTH      90
/* OBJ_OFF_FIELD_C0's record, two fields of it. The first dword is the one that
 * block already describes as indexing ADDR_WEAPON_HANDLERS; it is also what
 * picks the caption here, which is what names 41 of its values. */
#define ITEMTYPE_OFF_KIND      0x00u
/* Two more of that record, read by UnitWeaponInfo: +0x10 is spread into a min
 * and a max range and +0x14 goes straight into SIGHTC_OFF_DAMAGE. Its +4 was
 * ITEMTYPE_OFF_COOLDOWN here for a third time until checkoffsets refused it --
 * the definition further down is the one that stands. */
#define ITEMTYPE_OFF_RANGE     0x10u  /* int32, the nominal range */
#define ITEMTYPE_OFF_DAMAGE    0x14u  /* int32 */
#define ITEMTYPE_OFF_CAPACITY  0x18u  /* denominator for the ARMOR bar */
#define ITEM_OFF_AMMO          0xCCu  /* int32_t, numerator for both */
#define AM2_ITEM_TYPE_ARMOR    0x1Cu
/* The vehicle captions, and THE JUMP TABLE'S ORDER IS NOT THE ARMS' ORDER.
 * Laid out top to bottom the arms read JEEP, TANK, H|T, CONV, BOAT, ???; the
 * table at 0x00419A18 says kind 4 is ??? and kind 5 is BOAT. Reading the
 * bodies and numbering as you go gets the last two backwards, which is the
 * trap CLAUDE.md already records for the sub-state table. */
#define AM2_VEHICLE_KINDS      6
/* The item captions: a 25-entry jump table at 0x00419A30 reached through a
 * 41-BYTE index table at 0x00419A94, covering object type 2 through 42 with
 * entry 24 as the ??? default. The two tile exactly -- 0x419A30 + 25*4 is
 * 0x419A94 -- which is the check that neither base is off by one.
 *
 * Several types share a caption: 15..19 are all FLAG and 35..38 all DISG, and
 * types 6, 7, 13, 14, 21, 22 and 31..34 map to the default. */
#define AM2_ITEM_TYPE_FIRST    2
#define AM2_ITEM_TYPE_LAST     42
/* The other two, whose destructors do more than release a fixed set of slots
 * and which carry the MSVC SEH frame for it. */
#define ADDR_HUD_SARGE_DELETE  0x00414E90u
#define ADDR_HUD_SARGE_DESTRUCT 0x00414EB0u
#define ADDR_HUD_CMD_DELETE    0x004170F0u
#define ADDR_HUD_CMD_DESTRUCT  0x00417110u
/* The Sarge panel's own sprites: 31 slots from HUD_OFF_SPRITE0, filled by its
 * constructor from sprite set 13 indices 0..30. */
#define AM2_HUD_SARGE_SLOTS    31
/* The commands panel's sprites live in the pointer-mode table, 0x0C below the
 * mode fields, and are walked to the table's end rather than by a count. */
#define ADDR_HUD_CMD_SPRITES     0x004761B4u
#define ADDR_HUD_CMD_SPRITES_END 0x004762CCu
/* THE BUILD MENU, declared rather than built -- the same idiom as the OPTIONS
 * dialog's 43-record table. Eighteen records of 0x38: an id into
 * ADDR_UNIT_TYPES, a kind, a name, and the button's rectangle.
 *
 * THE ID IS NOT THE INDEX and that is the trap. This table is in MENU order
 * and ADDR_UNIT_TYPES is in DATA order, and five of the eighteen differ --
 * grenadier and flamethrower are promoted, bazooka demoted, jeep and halftrack
 * swapped. Indexing the type table by position gets those five wrong and
 * produces a menu that looks entirely right while greying the wrong buttons.
 * The original passes the id field for exactly this reason.
 *
 * RECORD 17 (Mine) HAS A VALID ID AND NAME AND A TAIL THAT IS NOT A RECTANGLE:
 * where 0..16 hold {50,190,43,27} and so on, it holds 16-bit pairs of the same
 * column values. The loop below reads only the id, so it does not care; noted
 * because whoever builds buttons from these rectangles will. */
#define ADDR_BUILD_MENU        0x004762D0u  /* 18 records of 0x38 */
#define ADDR_BUILD_MENU_END    0x004766C0u
#define AM2_BUILD_MENU_STRIDE  0x38u
#define BUILD_MENU_OFF_ID      0x00u  /* into ADDR_UNIT_TYPES, NOT the index */
#define BUILD_MENU_OFF_KIND    0x04u
#define BUILD_MENU_OFF_NAME    0x08u  /* char[0x18] */
#define BUILD_MENU_OFF_RECT    0x28u  /* int32 x, y, w, h -- 43x27 throughout */
/* ARMY_POINTS[OUR_SLOT], cached when the HUD is built rather than re-derived
 * eighteen times a frame. */
#define ADDR_OUR_POINTS        0x004FCF9Cu  /* int32_t */
/* The panel slides at 320 pixels a second, the sign carried in the constant
 * rather than in the code: +320 opening, -320 closing. FRAME_DELTA_SEC scales
 * it, so this is frame-rate independent and has to stay float. */
#define ADDR_HUD_SLIDE_OPEN    0x0046F95Cu  /* float, +320.0 */
#define ADDR_HUD_SLIDE_SHUT    0x0046F958u  /* float, -320.0 */
#define HUDPANEL_OFF_OPEN      0x5Cu  /* int32, which direction to slide */
#define HUDPANEL_OFF_STOP      0x64u  /* int32, the open position */
/* NOT A FLAG, which is what this was called when HudPanelUpdate was written
 * from seeing `self[0x8C] = 0` and nothing else. It is a CAPTION BUFFER, and
 * three other sites say so: 0x00419576 and 0x00419EC5 test its first byte for
 * emptiness -- the "is this string set" idiom -- and 0x0041957C, 0x00419F7F
 * and 0x00419F95 take its ADDRESS with `lea`, which is what you do with a
 * buffer and never with a flag.
 *
 * Its lifetime is ONE FRAME. HudPanelUpdate empties it as its first action, so
 * a caption survives only if something re-sets it the same frame; the radar's
 * update writes "Stratmap" into it after a second of mouse inactivity. Note
 * the radar writes the PANEL's field -- ADDR_HUD_WIDGET_B + 0x8C -- and not
 * its own, which is what settles that this offset belongs to one class rather
 * than to the family. HUD_SQUAD_PAIR_HI is 0x8C on a different class. */
/* The points readout, and the panel's CONSTRUCTOR is what binds the pair:
 * it sprintfs ADDR_OUR_POINTS into +0x6C with "%i", then makes an edit widget
 * over that buffer with a length of 0xC and stores it at +0x68. The placement
 * screen does the same two steps in the other order every time it spends or
 * refunds points -- rewrite the buffer, then repaint the widget through its
 * own paint slot -- and those three sites are the only ones in the image that
 * touch either field. */
#define HUDPANEL_OFF_POINTS_FIELD 0x68u  /* AM2_Widget *, drawn from below */
#define HUDPANEL_OFF_POINTS_TEXT  0x6Cu  /* char[0xC] */
#define HUDPANEL_OFF_CAPTION   0x8Cu  /* char[], emptied every update */
#define ADDR_HUD_PANEL_UPDATE  0x004193C0u  /* thiscall void(obj) */
#define ADDR_HUD_RADAR_UPDATE  0x00414890u  /* thiscall void(obj) */
#define ADDR_HUD_PANEL_PAINT   0x004194E0u  /* thiscall void(obj, RECT) */
#define ADDR_HUD_CMD_PAINT     0x00417440u  /* thiscall void(obj, RECT) */
/* Where the three command icons sit inside the panel: three int16 PAIRS, x
 * then y, at 6/50/94 across one row at y=22 -- the same three columns the
 * build menu uses. The loop pointer starts at 0x004766FA and reads [esi-2] and
 * [esi], so it begins INSIDE the first record rather than at its base, which
 * is the trig-table trap in miniature. */
#define ADDR_HUD_CMD_OFFSETS   0x004766F8u  /* int16 x,y [3] */
#define ADDR_HUD_CMD_OFF_END   0x00476706u
#define HUDCMD_OFF_SLOTS       0x58u  /* int32[3], -1 for an empty slot */
#define HUDCMD_OFF_SELECTED    0x64u  /* int32, which slot is highlighted */
#define AM2_HUD_CMD_SLOTS      3
#define AM2_HUD_CMD_HIGHLIGHT  0xEE
#define ADDR_HUD_SARGE_PAINT   0x004155A0u  /* thiscall void(obj, RECT) */
/* Vtable slot 2 of the same panel, and what fills every record the paint
 * reads. Three passes over the same six slots, in this order:
 *
 *   the HOTKEYS -- bindings 0x15..0x1A, one per slot, each selecting that
 *   weapon through ADDR_SELECT_WEAPON. The first that answers wins and the
 *   rest of the chain is skipped;
 *
 *   the MOUSE, over the same six cells: claim through ADDR_MOUSE_GRAB, select
 *   on release, and -- after a full second of mouse stillness -- write the
 *   item's name into ADDR_HUD_WIDGET_B's caption. That is the same one-frame
 *   tooltip idiom the radar uses for "Stratmap", so the panel's caption is
 *   re-asserted every frame by whoever is hovered;
 *
 *   and the REFILL, which rewrites all six records from the inventory.
 *
 * The refill's sprite comes from a 41-entry jump table over object type 2..42.
 * Its selected-slot flag is 1 or 2, never a bare boolean: 2 when
 * ADDR_ITEM_IS_READY answers non-zero, which is what the paint's `> 0` test is
 * indifferent to and what a `!!` would have flattened. */
#define ADDR_HUD_SARGE_UPDATE  0x004150F0u  /* thiscall void(obj) */
#define ADDR_SELECT_WEAPON     0x00414F20u  /* void(obj *, int32 slot) */
/* A WEAPON COOLDOWN, which is what the sarge panel's 1-or-2 flag distinguishes:
 * `clock - ITEM_OFF_LAST_USE` against the type's ITEMTYPE_OFF_COOLDOWN, so 1
 * means the weapon has recharged. Null item answers 0 and a null type record
 * answers 0 as well -- by returning the null it just loaded, which is a
 * shortcut worth reproducing rather than tidying into an explicit 0. */
#define ADDR_ITEM_IS_READY     0x0045F2D0u  /* int32(const obj *), 48 bytes */
/* +0xC4 on an ITEM is a timestamp, and orig.h already calls the same offset
 * OBJ_OFF_FOLLOW_UID on another kind of object. One number, two meanings, and
 * an alias between them would be a name that lies -- so this is its own. */
#define ITEM_OFF_LAST_USE      0xC4u   /* uint32_t, stamped from the clock */
#define ITEMTYPE_OFF_COOLDOWN  0x04u   /* uint32_t, milliseconds */
/* Three instructions: `return ADDR_ITEM_TYPE_NAMES[kind]`. Named rather than
 * reconstructed here -- it is one caller away and nothing about the update
 * depends on more than its shape. */
#define ADDR_ITEM_TYPE_NAME    0x004600E0u  /* const char *(int32 kind) */
#define ADDR_ITEM_TYPE_NAMES   0x0048C480u  /* const char *[] */
#define AM2_ACTION_WEAPON_FIRST 0x15  /* .. 0x1A, one per sarge slot */
#define AM2_HUD_SARGE_AMMO_MAX  99
#define AM2_HUD_TOOLTIP_DWELL   0x3E8 /* 1000 ms of stillness before the name */
#define HUDSARGE_REC_READY      0x0Cu /* 0 unselected, else 1 or 2 */
/* SIX slots in a 3x2 grid: x at 6/50/94, y at 21 and 49 -- the same three
 * columns the build menu and the command row use. int16 PAIRS, and the loop
 * pointer starts at 0x004766B2 reading [ebx-2] and [ebx], so the table BASE is
 * two bytes lower. Named for the base, not for where the loop begins. */
#define ADDR_HUD_SARGE_OFFSETS 0x004766B0u  /* int16 x,y [6] */
#define ADDR_HUD_SARGE_OFF_END 0x004766CAu
#define AM2_HUD_SARGE_ROWS     6
/* One record per slot, 0x10 apart, from HUDSARGE_OFF_SLOTS. */
#define HUDSARGE_OFF_SPRITES   0x58u  /* AM2_Sprite *[31], what INDEX picks */
#define HUDSARGE_OFF_SLOTS     0xD4u  /* {index, ?, count, flag}[6] */
#define HUDSARGE_REC_STRIDE    0x10u
#define HUDSARGE_REC_INDEX     0x00u  /* into the 31 sprites at +0x58; -1 empty */
#define HUDSARGE_REC_COUNT     0x08u  /* printed over the icon when >= 0 */
#define HUDSARGE_REC_HIGHLIGHT 0x0Cu  /* > 0 fills the cell first */
#define ADDR_STR_PCT_D         0x00476A1Cu /* "%d" */
#define AM2_HUD_SARGE_FONT     1
#define AM2_HUD_SARGE_INK      0xCE
#define AM2_HUD_SARGE_CELL_W   0x2B  /* the count is right-aligned in this */
#define AM2_HUD_SARGE_TEXT_DY  15
/* 0x00414620, two callers and both are that paint. A TOOLTIP: measure the
 * text, centre it on the cursor, clamp it to the bitmap area, fill a box and
 * draw the string in it. One of the 29 functions that bracket LockSurface --
 * and a whole one, unlike the line drawers, which lock and leave the unlock to
 * their caller. */
#define ADDR_DRAW_TOOLTIP      0x00414620u  /* void(const char *, uint8_t) */
/* 0x00446930, cdecl, six arguments and `ret 0`. NOT DrawTextClipped, which is
 * 0x00446AB0 and reconstructed -- the names are close and the functions are
 * not. This one stays in the image. */
#define AM2_TIP_PAD            6   /* box is text + 6 wide */
#define AM2_TIP_HEIGHT         12
#define AM2_TIP_ABOVE          10  /* cursor - 10 when near the bottom */
#define AM2_TIP_BELOW          18  /* cursor + 18 otherwise */
#define AM2_TIP_BOTTOM_MARGIN  100 /* "near the bottom" is within this */
#define AM2_TIP_TEXT_DX        3
#define AM2_TIP_TEXT_DY        1
/* What the radar writes into the panel's caption when the mouse goes quiet. */
#define ADDR_STR_STRATMAP      0x00476A10u  /* "Stratmap" */
#define AM2_MOUSE_IDLE_MS      0x3E8        /* one second */
/* Three sprite slots, shared by the top and edge strips. */
#define HUD_OFF_SPRITE0        0x58u
#define HUD_OFF_SPRITE1        0x5Cu
#define HUD_OFF_SPRITE2        0x60u
/* The squad panel's twelve PAIRS -- a slot each side of 0x30 apart, walked
 * together -- plus one sprite of its own at HUD_OFF_SPRITE0. */
#define HUD_SQUAD_PAIR_LO      0x5Cu
#define HUD_SQUAD_PAIR_HI      0x8Cu
#define AM2_HUD_SQUAD_SLOTS    12
/* Vtable slot 1 of the squad panel: twelve slots in a 3x4 grid, each a portrait
 * with optional decoration. The grid comes from a table of int16 PAIRS rather
 * than from arithmetic -- x is 6, 50, 94 and y is 22, 60, 98, 136 -- and the
 * loop walks it by four bytes from 0x004766CA reading `[p-2]` and `[p]`, so
 * the table's first x sits two bytes BEFORE the pointer it starts from.
 *
 * Each slot has a 0x20-byte record; twelve of them run from +0xBC. The record
 * decides everything drawn over the portrait, and SQUAD_REC_WIDE is the one
 * that changes the portrait itself -- set, the sprite comes from
 * HUD_SQUAD_PAIR_HI instead of _LO, a wide backdrop is filled behind it, and
 * the slot's own drawing stops there in favour of ADDR_HUD_SQUAD_DETAIL. */
#define ADDR_HUD_SQUAD_PAINT   0x00416DA0u  /* thiscall void(obj, RECT) */
#define ADDR_HUD_SQUAD_SLOT_XY 0x004766C8u  /* int16[12][2], x then y */
#define ADDR_HUD_SQUAD_DETAIL  0x00416340u  /* thiscall void(obj, int32) */
#define HUD_SQUAD_ICON_SPRITE  0x58u   /* AM2_Sprite *, the pip drawn per icon */
#define HUD_SQUAD_RECS         0xBCu   /* the twelve records start here */
#define HUD_SQUAD_REC_SIZE     0x20u
#define SQUAD_REC_INDEX        0x00u  /* int32, into the sprite pair; <0 skips */
#define SQUAD_REC_DETAIL_ARG   0x04u  /* int32, handed to ADDR_HUD_SQUAD_DETAIL */
#define SQUAD_REC_WIDE         0x08u  /* int32; see above -- it changes three
                                       * separate things at once */
#define SQUAD_REC_HILITE       0x10u  /* int32, fill the portrait box first */
#define SQUAD_REC_BAR_W        0x14u  /* int32, > 0 draws a bar under it */
#define SQUAD_REC_BAR_COLOUR   0x18u  /* uint8_t */
#define SQUAD_REC_ICONS        0x1Cu  /* int32, how many pips along the bottom */
#define AM2_SQUAD_WIDE_W       0x83   /* 131, the wide backdrop's width */
#define AM2_SQUAD_BAR_X        3
#define AM2_SQUAD_BAR_TOP      0x1A
#define AM2_SQUAD_BAR_BOTTOM   0x1D
#define AM2_SQUAD_ICON_X       7
#define AM2_SQUAD_ICON_DX      6
#define AM2_SQUAD_ICON_Y       0x1E
#define AM2_HUD_SQUAD_HILITE   0xEE
/* 0x004135C0, two callers. Delete all three of those through vtable slot 0
 * with the scalar-delete flag and clear each global -- the same shape
 * ADDR_CLOSE_SCREEN has for the screen, three times over. Reconstructed. */
#define ADDR_FREE_HUD_WIDGETS    0x004135C0u  /* void(void) */
/* 0x0041A170, three callers. The WIDTH of ADDR_HUD_WIDGET_C -- its screen
 * rect's right less its left -- or 0 when there is no such widget.
 *
 * The callers are what say it is a width rather than a coordinate: each
 * computes ADDR_SCREEN_W minus this and clamps a horizontal position to it,
 * which is "keep the thing left of the HUD panel". The null answer of 0 makes
 * that clamp the whole screen, so a missing panel is not a special case
 * anywhere else. */
#define ADDR_HUD_PANEL_WIDTH   0x0041A170u  /* int32_t(void) */
/* 0x00413A30, four callers. Repaint one HUD widget if it has been marked, and
 * unmark it.
 *
 * Three globals and the function is the only thing that ties them together: a
 * flag at 0x004FCF84, an index at 0x004FCF50, and a table of widget pointers
 * at 0x004FCF04 that the index reaches into. ADDR_HUD_WIDGET_A is 0x004FCF00,
 * four bytes before the table, so either the widgets are one array and the
 * three named singles are entries in it, or they are scattered and this reads
 * past one of them. Not established, and the index's range is not either.
 *
 * It also clears a BYTE at the widget's +0x70 before painting. The base
 * AM2_Widget has nothing named there and the subclasses that do use the offset
 * are int32; this writes one byte and only ever zero. */
#define ADDR_HUD_REPAINT_ONE   0x00413A30u  /* void(void) */
#define ADDR_HUD_DIRTY         0x004FCF84u  /* int32_t */
#define ADDR_HUD_INDEX         0x004FCF50u  /* int32_t, into the table below */
/* 0..7, latched by TakeNumberKey from the 1..8 keys, and its only reader is
 * PlacementScreenClick.
 *
 * WHAT IT SELECTS IS SETTLED NOW. This comment used to say the reader "pushes
 * it into a formatting call beside ADDR_OUR_POINTS, ADDR_HUD_INDEX and
 * ADDR_DIR_SCRATCH", which was a reading of the argument block from the
 * outside and got two of the three wrong: the call is MakePlacedUnit, not a
 * formatting call; ADDR_HUD_INDEX is not an argument at all -- the unit TYPE
 * derived from it is; and the slot lands in the `group` parameter. So the
 * 1..8 keys choose which GROUP a placed unit joins, which is the same column
 * the shipped <map>_<colour>_place.txt lines carry as their fifth field. The
 * name still says how it is written, and now the comment says what it is
 * for. */
#define ADDR_NUMBER_KEY_SLOT   0x004FCF90u  /* int32_t 0..7 */
/* 0x00413A80, one caller. Eight arms, DIK 2..9 -- the 1..8 keys -- each
 * latching its index into the above on a fresh press. Reconstructed. */
#define ADDR_TAKE_NUMBER_KEY   0x00413A80u  /* void(void) */
#define ADDR_HUD_WIDGET_TABLE  0x004FCF04u  /* AM2_Widget *[] */
#define HUDWIDGET_OFF_FLAG70   0x70u        /* uint8_t, cleared before paint */
/* 0x004143A0, two callers. Paint all three through vtable slot 1. */
#define ADDR_HUD_PAINT     0x004143A0u  /* void(void) */
/* 0x00414370, one caller -- the per-frame path. The same three widgets through
 * vtable slot 2, then two further steps. */
#define ADDR_HUD_UPDATE    0x00414370u  /* void(void) */
/* 0x00414430, ten callers: the POINTER MODE table and the six globals it
 * installs out of it. Seven 40-byte records at 0x004761B8 -- the eighth slot
 * is where the string "Rifleman" starts, which is what bounds the table.
 *
 * "Pointer" is a ROLE NAME and is ours. What grounds it is the three readers
 * of what this installs, none of which is in this function: ADDR_POINTER_PICK
 * is called per object while walking OBJ_OFF_QUERY_NEXT, so it decides what
 * the pointer may pick; ADDR_POINTER_ACTION is called with (object, point)
 * only when ADDR_MOUSE_BUTTON is clear, so it is what a release does; and
 * ADDR_POINTER_OVERLAY is handed straight to OverlayPrepare, whose own bound
 * is 0..0x12 and whose seven values here are 3, 0, 3, 0, 5, 6, 7.
 *
 * THE FIVE STORES ARE NOT IN RECORD ORDER, which is the same trap
 * ADDR_WEAPON_FN_SLOT2 and SLOT3 carry twenty lines up. Globals E0, E4, E8,
 * EC, F4 take record fields +0, +4, +0x10, +0x14, +0x0C. Reading the stores
 * top to bottom and numbering as you go puts the overlay in the wrong global
 * and swaps the two unread fields. Named by what they RECEIVE.
 *
 * Reconstructed, and reached exactly ONCE on a driven Boot Camp mission --
 * mode 0 at mission start. Every mode above 0 is verified by reading. */
#define ADDR_POINTER_MODES     0x004761B8u  /* 7 records of 40 bytes */
#define AM2_POINTER_MODE_SIZE  40
#define AM2_POINTER_MODES      7
/* AN INLINED "OUR LEADER" HELPER WHOSE FALLBACK IS DEAD, EIGHT TIMES OVER.
 *
 * The pointer-mode handlers all need the unit the player is commanding, and
 * they all get it from the same inlined block:
 *
 *     owner = ADDR_DEFAULT_OWNER;
 *     if (owner != ADDR_DEFAULT_OWNER)   -- scan that army's object list for
 *                                           the first live type-2 object with
 *                                           OBJ_OFF_SARGE set
 *     else                               -- LookupByUID(ADDR_OUR_LEADER_UID)
 *
 * VC6 folds the second load, so the compare becomes `cmp eax, eax` and the
 * scan can never run. A byte scan for that load followed by that compare finds
 * EIGHT sites -- 0x00457E61, 0x00457F7F, 0x00458A20, 0x00458ACB, 0x00459534,
 * 0x004598C0, 0x00459B12, 0x00459F0C -- and there are more where the load sits
 * further from the compare.
 *
 * NONE OF THEM IS A BINARY PATCH. The compare is two bytes, so nothing was
 * overwritten in place the way docs/binarypatches.md's six were; this is one
 * source-level construct compiled the same way everywhere it appears.
 *
 * Worth having written down before the rest of this family is transcribed: the
 * scan is a third of the bytes in some of these functions and none of it can
 * execute, and the NULL check lives inside it, so every one of these
 * dereferences the uid lookup unguarded. */
/* 0x00458A20: mode 3's action, and mode 3 is one of the fire-once records --
 * pick and f14 both zero, so SetPointerMode runs it rather than installing it.
 * Reconstructed as PointerDropItem. */
#define ADDR_POINTER_DROP_ITEM 0x00458A20u  /* void(void *obj, uint32_t at) */
/* 0x00458ED0: mode 0's action -- sixteen bytes that drop the second argument
 * and forward the first to SelectIfOwn. Mode 0 is the DEFAULT pointer mode and
 * the one SetPointerMode's note records a driven mission actually reaching, so
 * unlike the rest of this family it is on a live path. */
#define ADDR_POINTER_SELECT    0x00458ED0u  /* void(void *obj, uint32_t at) */
/* A SECOND {pick, action, kind, flags} TABLE, 16-byte records at 0x00489AB0
 * and around it, distinct from the pointer-mode table above. Empty slots are
 * {0, 0, -1, 0}; four consecutive records share {0x00459DA0, 0x00458D70, 3, 0}
 * and two share {0, 0x00446E70, 1, 1}. Its base and its consumer are NOT
 * established -- the records were found from the functions' xrefs rather than
 * from a loop, which is the wrong way round and is why nothing here is named
 * for it yet. */
/* THE PICKS' REACH THRESHOLDS, and these are placeholders on purpose.
 *
 * Each pointer PICK compares ApproxDist against one of five globals --
 * 0x00662450, 0x006624EC, 0x0066275C, 0x00662894, 0x006628C8 -- and each has
 * exactly ONE toucher, the pick that reads it. All five read 0 in the image
 * and sit in the BSS tail of `.data`, so if nothing writes them every one of
 * these reach tests passes only at zero distance.
 *
 * THAT IS NOT ESTABLISHED, and the reason is the scan's limit rather than the
 * evidence. `refs_to` finds an address as a literal dword, which catches
 * `mov [0x66275C], eax` -- but the five are spaced irregularly and look like
 * FIELDS of records reached through a base pointer, and a write through a base
 * is invisible to it. So "no direct writer" is what was measured; "no writer"
 * is not. Settling it means finding the record they belong to.
 *
 * Named for what they do at their one use and nothing more. */
#define ADDR_PICK_REACH_662450   0x00662450u  /* int32_t */
#define ADDR_PICK_REACH_66275C   0x0066275Cu  /* int32_t */
#define ADDR_PICK_REACH_6624EC   0x006624ECu  /* int32_t */
#define ADDR_PICK_REACH_662894   0x00662894u  /* int32_t */
#define ADDR_PICK_REACH_6628C8   0x006628C8u  /* int32_t */
#define ADDR_POINTER_PICK_BOARD  0x00459DA0u  /* int32_t(void *obj) */
#define ADDR_POINTER_PICK_WATCHED 0x00459EE0u /* int32_t(void *obj) */
/* 0x00458D70, column 1 of the four weapon-handler records at 0x00489AB0..AE0 --
 * ADDR_SET_WEAPON_TARGET's sibling, for the kinds below. */
#define ADDR_SET_WEAPON_TARGET_AIMED 0x00458D70u /* void(void *, uint32_t) */
/* Its four siblings, same table column, one kind each. Transcribed from a diff
 * against 0x00458D70 rather than read separately -- see widget.cpp. */
#define ADDR_SET_WEAPON_TARGET_MEDIC   0x00458B50u /* void(void *, uint32_t) */
#define ADDR_SET_WEAPON_TARGET_WRENCH  0x00458C00u
#define ADDR_SET_WEAPON_TARGET_KIND2A  0x00458CB0u
#define ADDR_SET_WEAPON_TARGET_SWEEPER 0x00458E30u
/* The weapon kinds those four records cover are AM2_ITEM_KIND_DISG_0..DISG_3,
 * which were already in this file. They went in here for one commit as
 * AM2_WEAPON_KIND_AIMED_LO/HI -- a second name for 0x23 and 0x26 under a NEW
 * prefix, which is the exact blind spot this file describes: checkoffsets
 * compares within a prefix and has nothing to compare a new one against. Found
 * by decoding the caption table for the sibling handlers' kinds and seeing the
 * names already there. Grep the VALUE, not the prefix. */
/* 0x00459DA0, 320 bytes. Reconstructed as PointerPickBoard. It is the PICK of those four records: refuse a null object,
 * refuse one whose army byte is not ADDR_DEFAULT_OWNER, find our leader
 * through the dead-fallback helper described above, and then two arms.
 *
 * A VEHICLE WITH A FREE SEAT -- type 3, OBJ_OFF_FIELD_94 clear, and
 * OBJ_OFF_POSE_PENDING < VEHICLE_OFF_SEATS, which is the same pair EnterVehicle
 * refuses on and is what settles the reading: seats used against seats. It
 * shows overlay row 8 and stores the object's uid, and then ANSWERS 0 -- so the
 * vehicle arm is a hover hint and never a yes.
 *
 * A TROOPER within ApproxDist of the leader shows overlay row 0x12 and answers
 * 1. Everything else answers 0.
 *
 * THE HINT IS SUPPRESSED WHILE THE BUTTON HAS BEEN DOWN A WHILE: with
 * ADDR_MOUSE_BUTTON set, the overlay is skipped unless GetTickCount() less a
 * timestamp is under 500. So a click-and-hold stops re-arming the hint.
 *
 * Its three unnamed globals are named now: ADDR_MOUSE_PRESS_MS and
 * ADDR_POINTER_HOVER_UID from their writer/reader pairs, and the reach
 * threshold as a placeholder -- see above. */
#define MODE_OFF_PICK          0x00u  /* int32(obj) -- may the pointer take it */
#define MODE_OFF_ACTION        0x04u  /* void(obj, packed point) on release */
#define MODE_OFF_OVERLAY       0x0Cu  /* OverlayPrepare's row, 0..0x12 */
#define MODE_OFF_F10           0x10u  /* installed, unread here */
#define MODE_OFF_F14           0x14u  /* installed, and part of the guard */
#define ADDR_POINTER_MODE      0x004FCF80u  /* int32_t, the current index */
#define ADDR_POINTER_PICK      0x005122E0u
#define ADDR_POINTER_ACTION    0x005122E4u
#define ADDR_POINTER_F10       0x005122E8u
#define ADDR_POINTER_F14       0x005122ECu
#define ADDR_POINTER_OVERLAY   0x005122F4u
#define ADDR_SET_POINTER_MODE  0x00414430u  /* void(int32_t mode) */
/* The two steps ADDR_HUD_UPDATE runs after the widgets, each with exactly one
 * caller -- this one -- so these names cannot be wrong about anything else.
 * They are still roles rather than recovered names: neither says what it is,
 * and neither pushes a string.
 *
 * The second is the tail JUMP, and a little is established about it: it walks
 * the two AIM MARKER tables below, clearing those whose deadline has passed
 * and stamping ADDR_CURSOR_X/Y into the local player's point. So it ages a
 * table of cursor-anchored, time-limited entries. What they are FOR is not
 * established.
 *
 * THIS SAID "records of 0x64 bytes at 0x004FC8E0" AND THAT WAS WRONG, which
 * is worth keeping because the mistake is a good one. The function handles
 * both tables in one unrolled body, so it reads `[eax]` and `[eax+0x64]`, and
 * 0x64 looks exactly like a record stride. It is not: `add eax, 4` at the
 * bottom, with `cmp eax, 0x004FC8F0`, says four entries four bytes apart, and
 * 0x64 is the displacement of the SECOND TABLE. Two parallel arrays read as
 * one array of structs. **Take a stride from the loop step, never from the
 * largest displacement in the body** -- the same rule that says to read a
 * table's bounds from the loop rather than from the data. */
/* The two globals its debug-explosion arm reads, both with exactly ONE toucher
 * -- which is the case this file calls "a table with one consumer is a table
 * you cannot name", so both are placeholders. 0x004FD748 sits next to
 * ADDR_OPT_PETER, ships as 0, and gates the arm; 0x00476FB4 sits next to
 * ADDR_FOG_OF_WAR, ships as 0x79, and is the KIND that arm hands
 * CreateExplosion. A developer switch and the explosion it drops, on the
 * evidence of one use each. */
#define ADDR_OPT_4FD748          0x004FD748u  /* int32_t, ships 0 */
#define ADDR_DEBUG_BLAST_KIND    0x00476FB4u  /* int32_t, ships 0x79 */
/* 0x00413E70, 1,280 bytes, one caller. SURVEYED AND NOT RECONSTRUCTED. The
 * per-frame mouse dispatch: it is what clears ADDR_POINTER_HOVER_UID at the top
 * of each frame and then runs the whole selection and pointer layer.
 *
 * IT IS LIVE, unlike almost everything else left -- which is what makes it the
 * one remaining function an A/B can actually compare rather than merely fail to
 * regress.
 *
 * ALL THIRTEEN OF ITS CALLEES ARE ALREADY RECONSTRUCTED: SelectionClick,
 * PointInRect, OverlayPrepare, PlaceScreenClick, CreateExplosion, ApproxDist,
 * ActionKeyDown, ObjIsType3, VehicleDismountAll, LookupOwnerObj, WeaponByUid,
 * ObjectsHitByPoint and GetMenuRow. So nothing under it needs reading first.
 *
 * AND ITS "UNNAMED" GLOBALS ARE MOSTLY FIELDS OF NAMED ONES, which is worth
 * knowing before anyone opens a naming session for them: 0x004FCF74, 0x004FCF78
 * and 0x004FCF7C are ADDR_VIEW_RECT's top, right and bottom -- it is an
 * AM2_Rect -- and 0x0048546E and 0x004FCF6A are the high halves of the packed
 * points ADDR_CURSOR_POINT and ADDR_DRAG_ANCHOR. That leaves three genuinely
 * new, and all three are named now: ADDR_OPT_4FD748 and ADDR_DEBUG_BLAST_KIND
 * above, and ADDR_MOUSE_PRESS2_MS -- which turned out to be half of a SECOND
 * press point/time pair, written beside the first by the same handler.
 *
 * THE FAN-OUT WALKS A CHAIN. ObjectsHitByPoint answers a list threaded through
 * +0x68, and the loop tries each object against the slots in turn: SLOT0 while
 * a leader flag is set, then SLOT3, then ADDR_POINTER_PICK -- the last only
 * while an overlay has not already been claimed. A slot answering non-zero
 * ends the walk and selects which of the trailing arms runs.
 *
 * The body is a dispatcher: refuse while a text field has focus or input is
 * suppressed; clear the hover uid; SelectionClick; turn the cursor into a world
 * point and bail if it is outside the view; the placement screen's own click
 * path; drag-rectangle maintenance; two ActionKeyDown arms; and then a fan-out
 * through the pointer and weapon function-pointer slots -- SLOT0..SLOT3,
 * ADDR_POINTER_PICK and ADDR_POINTER_ACTION -- which is where the band
 * reconstructed above this is actually reached from.
 *
 * What it needs is a branch-by-branch transcription of about 320 instructions;
 * what it does not need is any more reading of its neighbours. */
#define ADDR_HUD_POST_UPDATE 0x00413E70u  /* void(void), 1280 bytes */
#define ADDR_HUD_MARKER_AGE  0x00412190u  /* void(void), the tail jump */

/* ---- The AIM MARKERS -----------------------------------------------------
 *
 * Two tables of four -- one entry per army -- driven by an arm of the weapon
 * dispatcher at 0x0045F460, aged by ADDR_HUD_MARKER_AGE, and drawn by
 * ADDR_DRAW_EFFECT_LAYER. Everything below is parallel arrays with a stride of
 * FOUR, indexed by army; see the correction above for why that needs saying.
 *
 * What the effect looks like is fully mapped and what it is FOR is not. The
 * draw does three things per live entry: it re-reads a 112x112 block of the
 * offscreen surface through a displacement table, writes it back to the draw
 * target -- a refraction, the pixels are moved rather than tinted -- and then
 * puts two sprites over it. The local player's point follows the CURSOR,
 * which is what makes "aim" the reading; it is a reading and not a fact.
 *
 * Sprite set 19 again, the air-support set, indices 4 and 5. Six frames of
 * index 4 stepped every 50 ms and stopped at the last, and index 5 drawn as
 * entry 0 plus one of entries 5..8 chosen at random. */
/* 0x00412090, one caller. Preload both aim-marker sprite runs and clear the
 * per-army state -- some of it. */
#define ADDR_AIM_INIT            0x00412090u  /* void(void) */
#define AM2_AIM_SPRITES_A        6u
#define AM2_AIM_SPRITES_B        9u
#define AM2_AIM_ARMIES           4u
#define AM2_AIM_SET              0x13  /* the air-support sprite set */
#define AM2_AIM_INDEX_A          4
#define AM2_AIM_INDEX_B          5
#define AM2_AIM_PRELOAD_FLAGS    0x1000
#define ADDR_AIM_SPRITES_A       0x004FC8C8u  /* AM2_Sprite *[6], set 19 idx 4 */
/* 0x00412120, two callers. ReleaseSprite over all six of those, by walking
 * 0x004FC8C8 to 0x004FC8E0 four bytes at a time -- so the count is the
 * DISTANCE between two globals rather than a literal, and it is 6. Named from
 * the body: nothing else in it says what the sprites are for. */
#define ADDR_FREE_AIM_SPRITES    0x00412120u  /* void(void) */
#define ADDR_AIM_LIVE_A          0x004FC8E0u  /* int32_t[4], per army */
#define ADDR_AIM_POINT_A         0x004FC8F0u  /* {int16 x, int16 y}[4] */
#define ADDR_AIM_STAMP_A         0x004FC900u  /* int32_t[4], game-clock ms */
#define ADDR_AIM_DEADLINE_A      0x004FC910u  /* int32_t[4] */
#define ADDR_AIM_SPRITES_B       0x004FC920u  /* AM2_Sprite *[9], set 19 idx 5 */
#define ADDR_AIM_LIVE_B          0x004FC944u  /* int32_t[4] */
#define ADDR_AIM_POINT_B         0x004FC954u  /* {int16 x, int16 y}[4] */
#define ADDR_AIM_FRAME_B         0x004FC964u  /* int32_t[4], 5..8, re-rolled */
#define ADDR_AIM_STAMP_B         0x004FC974u  /* int32_t[4] */
#define ADDR_AIM_DEADLINE_B      0x004FC984u  /* int32_t[4] */
/* 112 x 112 entries of {int16 dx, int16 dy}, relative to the block's top-left
 * corner. 50,176 bytes, which is exactly the span up to the next named datum,
 * and 112 is the block's width -- so the table tiles the block exactly. */
#define ADDR_AIM_DISPLACE_MAP    0x00478CDCu
#define AM2_AIM_BLOCK            112
#define AM2_AIM_BOX_LEFT         0x2F   /* off ADDR_AIM_POINT_A */
#define AM2_AIM_BOX_TOP          (-0xD8)
#define AM2_AIM_BOX_RIGHT        0x9F
#define AM2_AIM_BOX_BOTTOM       (-0x68)
#define AM2_AIM_STEP_MS          50     /* one frame of the A sprites */
#define AM2_AIM_B_STEP_MS        150    /* and of the B ones */
#define AM2_AIM_FRAMES_A         5      /* the index stops here */
#define AM2_AIM_B_OFF_X          0x8C   /* where the B sprites go */
#define AM2_AIM_B_OFF_Y          (-0x87)
#define AM2_AIM_B_OFF_X2         0x96
#define AM2_AIM_B_OFF_Y2         (-0x7D)
#define AM2_AIM_B_FIRST          5      /* the random frame is 5 + rand()%4 */
#define AM2_AIM_B_HOLD           7      /* 1 in 7 re-rolls it */
/* 0x00412230 and 0x00412310, the two halves of starting one. */
#define ADDR_AIM_START           0x00412230u  /* void(int8_t army, uint32 pt) */
#define ADDR_AIM_START_B         0x00412310u  /* void(uid, int8_t, uint32) */
/* Two globals AimStartB reads and nothing else here explains. The first is
 * doubled and reduced by one to make the marker's deadline, so it is half a
 * lifetime in milliseconds; the second goes straight into ADDR_SPAWN_AT's
 * sixth argument. Both names are ours, from that one use each. */
#define ADDR_AIM_LIFE_HALF_B     0x00662854u  /* int32_t */
/* The A half's own pair. The lifetime works the same way -- doubled, less one
 * -- and the damage is what AimStart deals to everything at the point, where
 * the B half spawns a sprite instead. Renamed the B one to match the moment
 * the A one turned up; a bare LIFE_HALF would have been ambiguous within a
 * commit of being written. */
#define ADDR_AIM_LIFE_HALF_A     0x00662820u  /* int32_t */
#define ADDR_AIM_DAMAGE          0x00662838u  /* int32_t */
#define ADDR_AIM_SPAWN_ARG       0x0066286Cu  /* int32_t */
#define AM2_AIM_SPAWN_KIND       0x7E   /* what the marker is spawned as */
#define AM2_AIM_LIFE_REMOTE_MS   0x3E8  /* 1000, when the army is not ours */
/* One record per font, 524 bytes apart -- BuildFont computes the stride as
 * ((f<<6)+f)*2+f then <<2, which is 131 dwords and not the 133 this said
 * before. Within a record: +0 the total encoded size, +4 a uint16 offset for
 * each of the 256 characters, +0x204 the pointer to the encoded glyphs. */
#define ADDR_FONT_STRIDE    524u
/* The same stride in the units each table is indexed in: 262 uint16 for the
 * offsets and 131 dwords for the bases. text.cpp had the second as 133 for as
 * long as it existed -- see the note there for the three things that settle
 * it. Named so the two cannot drift apart again. */
#define AM2_FONT_OFFSET_STEP  262u
#define AM2_FONT_BASE_STEP    131u
#define ADDR_GLYPH_SIZE     0x006598D0u  /* uint32_t, total encoded bytes */
#define ADDR_GLYPH_OFFSETS  0x006598D4u  /* uint16_t[256] */
/* The entry for ' ' inside that table -- 0x006598D4 + 0x20 * 2 -- which is
 * what TextExtent reads to get a line height without measuring anything. */
#define ADDR_GLYPH_OFFSET_SPACE 0x00659914u
/* 0x004468A0, nine callers: how wide a string is in a font, and how tall a
 * line is. The width is the sum of each glyph's own width; '^' is skipped as
 * an escape and anything below 0x1F is skipped as a control. The height does
 * not depend on the string at all -- it is the height of the SPACE glyph. */
#define ADDR_TEXT_EXTENT    0x004468A0u  /* void(const char *, int32, int32[2]) */
#define ADDR_FONT_BASES     0x00659AD4u  /* uint8_t *, the encoded glyphs */
/* 0x00446E00, three callers, all in the HUD. TextExtent's vertical twin: the
 * same walk over the string, summing the glyph record's SECOND uint16 minus
 * three instead of its first.
 *
 * That field is the HEIGHT, and a probe of the running game settles it rather
 * than a reading -- across "SARGE" the first field is 7, 7, 8, 9, 7 and the
 * second is 12 for every one of them, 14 for every one in font 1. A per-glyph
 * width varies with the glyph; a line height does not. TextExtent already
 * treats the same field as a line height, for the space glyph alone.
 *
 * So the answer is a stack of characters rather than a run of them, and the
 * one caller read confirms it: the result feeds a running VERTICAL coordinate
 * in a HUD panel.
 *
 * ITS ESCAPE HANDLING DIFFERS FROM TextExtent'S. Here `^` consumes the
 * character after it as well; there the character after it counts. Two
 * functions over the same strings disagreeing about the escape is worth
 * knowing before assuming either is the other's shape. */
#define ADDR_TEXT_STACK_HEIGHT 0x00446E00u  /* int32(const char *, int32 font) */
/* 0x00446C50, four callers and ALL FOUR are the edge strip's paint: the
 * DRAWING half of the pair above. Same signature as DrawTextClipped, same
 * double clip in the same order, same '^' escape written over its own colour
 * argument -- and two differences that are the whole of what makes it
 * vertical.
 *
 * Each glyph is CENTRED on x rather than started at it: the pen x is
 * `x - width/2`, recomputed per glyph, so a stack of glyphs of different
 * widths lines up down a 16-pixel bar. And the pen advances DOWN by the
 * glyph's height minus three -- exactly what ADDR_TEXT_STACK_HEIGHT sums, so
 * measuring and drawing agree by construction rather than by coincidence.
 *
 * Like its horizontal sibling it advances the pen even when the glyph clipped
 * away entirely, which keeps a partly off-screen stack aligned with a visible
 * one. */
#define ADDR_DRAW_TEXT_VERTICAL 0x00446C50u /* void(x,y,str,font,RECT,colour) */
#define AM2_GLYPH_STACK_KERN    3           /* taken off each glyph's height */
#define ADDR_FONT_DESCS     0x004897E8u  /* {const char *face; int32 h; uint16 style}[] */
#define ADDR_BUILD_FONT     0x004466E0u  /* int32_t(int32_t fontIndex) */
/* 0x00446840 and 0x00446880: give one font's glyph bytes back and clear its
 * two table entries, and do that for all three. The second is entry 7 of
 * ShutdownSubsystems' ordered teardown, so naming it takes another of those
 * out of the "a name per entry would be a guess per entry" bucket. */
#define ADDR_FREE_FONT      0x00446840u  /* void(int32_t fontIndex) */
#define ADDR_FREE_ALL_FONTS 0x00446880u  /* void(void), three of them */
/* THE SAVEGAME LIST, and it names itself in its constructor rather than in its
 * destructor. 0x00453280 sets the data directory to `save`, formats `save\%s`
 * and globs `*.sav` -- so the class is the list of savegame FILES, which is a
 * different thing from the DLG_LOADGAME, DLG_DELGAME and DLG_OVERWRITE dialogs
 * already here, and a much larger one: 0x340 of stack and two arguments.
 *
 * Its destructor frees FONT 2 and chains to the dialog base. That it frees a
 * font it was not seen to build is recorded as what the code does and not
 * explained away -- the constructor's font handling has not been read. */
#define VTABLE_SAVE_LIST         0x0046FC0Cu
#define ADDR_OPEN_SAVE_GAME      0x00453890u /* void(void) */
#define AM2_SAVE_LIST_SIZE       0xACu
#define ADDR_STR_SAVEGAME_BMP    0x0048BC98u /* "02_008_00_savegame.bmp" */
#define ADDR_SAVE_LIST_CTOR      0x00453280u /* thiscall, two arguments */
#define ADDR_SAVE_LIST_DELETE    0x00453810u /* thiscall obj *(obj, flags) */
#define ADDR_SAVE_LIST_DESTRUCT  0x00453830u /* thiscall void(obj) */
#define AM2_SAVE_LIST_FONT       2
#define AM2_FONT_COUNT      3
/* 0x00446830, four callers. A cdecl wrapper for the line above and nothing
 * else -- both take one argument and it is passed straight through. */
#define ADDR_BUILD_FONT_ALIAS 0x00446830u  /* int32_t(int32_t fontIndex) */
#define ADDR_GAME_MALLOC    0x004647F8u  /* the game's own CRT malloc */
#define ADDR_GAME_FREE      0x004646A9u  /* the game's own CRT free */
#define ADDR_SCREEN_CLIP    0x00485310u  /* AM2_Rect -- text and sprites share it */
/* 0x0041CBA0. A clipped vertical line straight into the locked framebuffer.
 * It names itself nowhere, so this is a role name; two callers. One of the 29
 * functions in CLAUDE.md's Lock/Unlock bracket list. */
#define ADDR_DRAW_VLINE     0x0041CBA0u  /* void(x, y0, y1, colour) */
#define ADDR_DRAW_HLINE     0x0041CC40u  /* void(y, x0, x1, colour) */
/* The rectangle outline both of them serve: two vertical edges then two
 * horizontal ones, all four INCLUSIVE of `right` and `bottom` -- which the
 * clipping inside the line drawers treats as exclusive. One caller. */
#define ADDR_DRAW_RECT      0x0041CDC0u  /* void(const AM2_Rect *, colour) */
/* 0x0041CCE0, one caller -- the radar's paint. The SAME outline as DrawRect
 * above and a completely different implementation: where that one calls the
 * two line drawers four times, this one clips once against ADDR_SCREEN_CLIP
 * and writes the framebuffer directly, `rep stosd` along the top and bottom
 * rows and then a stride loop down the two sides.
 *
 * Its edges are INCLUSIVE, the same as DrawRect's, but arrived at differently:
 * the run length is `right - left + 1` and the side loop is `<=`, where
 * DrawRect gets there by handing the exclusive-clipping line drawers a bumped
 * bound. Two ways to the same rectangle.
 *
 * Another half-bracket: it Locks and never Unlocks, exactly as DrawVLine and
 * DrawHLine do, so the pairing belongs to whoever called it. */
#define ADDR_DRAW_RECT_FAST 0x0041CCE0u  /* void(const AM2_Rect *, colour) */
/* 0x0041C7F0, one caller -- the radar's paint, for a stationary blip. A 3x3
 * block of one colour straight into the locked framebuffer.
 *
 * It DECREMENTS both coordinates first, so the caller's point is the block's
 * CENTRE rather than its top left, and then bounds-tests the decremented pair
 * against the bitmap area less two. That pair of facts is the whole function:
 * a blip one pixel from the right edge is rejected outright rather than
 * clipped, because there is no clipping here at all.
 *
 * Another half-bracket -- Locks, never Unlocks. */
#define ADDR_DRAW_BLIP3     0x0041C7F0u  /* void(x, y, colour) */
/* 0x004149B0, one caller -- the radar's paint. WHAT COLOUR IS THIS BLIP, and
 * does it blink. The return indexes ADDR_RADAR_COLOURS; the out parameter says
 * which of the caller's two drawing paths to take.
 *
 * It starts from the object's ARMY through CommArmyOfSlot and then overrides
 * that in three ways, in this order:
 *
 *   a type-4 object whose type record's first dword is 16..19 takes 0..3 --
 *   the same four values, so those four item kinds are drawn as if they were
 *   armies;
 *   an item whose key's field A is 0x2B takes field B minus 994, again 0..3;
 *   a type-2 object in a multiplayer session with soldier kind 7 returns 4
 *   outright, and otherwise, if OBJ_OFF_FIELD_530 is not 5, returns THAT and
 *   raises the out flag.
 *
 * Both of its jump tables are in layout order, which is worth recording only
 * because the edge strip's vehicle table two files away is not -- the trap is
 * real but it is not universal, so the table still has to be read either way.
 *
 * It calls ObjIsType2 twice in a row on the same object inside the same
 * branch, the second call unable to answer differently. Reproduced. */
#define ADDR_RADAR_BLIP_COLOUR 0x004149B0u /* int32(const obj *, int32 *out) */
#define ADDR_RADAR_COLOURS   0x004FCF5Cu  /* uint8_t[][2], indexed by that */
#define AM2_RADAR_KIND_FIRST 16   /* type-4 kinds 16..19 map to armies 0..3 */
#define AM2_RADAR_KEY_FIRST  994  /* item key field B, likewise */
#define AM2_RADAR_KEY_TAG    0x2B /* the field A an item must carry first */
#define AM2_RADAR_COLOUR_MP7 4    /* soldier kind 7 in a network game */
#define AM2_RADAR_FIELD530_NONE 5 /* the value that means "no override" */
#define AM2_BLIP3_SIZE      3
/* 0x0041CA50, one caller -- the radar's paint, for an object whose flag 0x10
 * is set and which is not blinking. A THREE-FRAME PULSE, the frame chosen by
 * the caller from the game clock, drawn from two colours:
 *
 *   phase 0   centre in A, the radius-2 ring in B
 *   phase 1   the radius-1 plus in A, centre in B
 *   phase 2   the ring in A, the plus in B, and NO centre at all
 *
 * So A travels outward -- centre, plus, ring -- with B one step behind it, and
 * the centre goes dark on the last frame. Any phase outside 0..2 draws
 * nothing.
 *
 * Unlike ADDR_DRAW_BLIP3 it does NOT decrement: the caller's point is already
 * the centre. Its bounds test is the matching one, x-2 and y+2 against the
 * bitmap area, and it is a rejection rather than a clip.
 *
 * READ ITS ARGUMENT OFFSETS WITH THE EPILOGUE IN MIND. The compiler
 * interleaves `pop edi` and `pop esi` into the middle of two of the three
 * arms, so the same `[esp+0x10]` means arg3 before them and arg4 after. Taken
 * at face value the two colours come out swapped on exactly those arms.
 *
 * Another half-bracket -- Locks, never Unlocks. */
#define ADDR_DRAW_BLIP_PULSE 0x0041CA50u /* void(x,y,colourA,colourB,phase) */
#define AM2_BLIP_PULSE_PHASES 3
/* 0x0041C8A0, one caller -- the same radar path, one branch over. It is
 * STRUCTURALLY IDENTICAL to ADDR_DRAW_BLIP_PULSE above: same bounds test, same
 * lock, same three phases moving colour A outward with B behind it, same
 * interleaved pops in the same two arms. Only the two shapes differ -- a 5x5
 * SQUARE outline where the pulse has a diamond, and a 3x3 ring where it has a
 * plus.
 *
 * ADDR_OBJ_TYPE2_FIELD548 picks between them: non-zero takes the diamond, zero
 * takes this. Both were read out separately rather than one assumed from the
 * other, which is what makes sharing their phase structure safe. */
#define ADDR_DRAW_BLIP_SQUARE 0x0041C8A0u /* void(x,y,colourA,colourB,phase) */
/* 0x00413610. The one caller of DrawRect: it takes a rectangle stored in the
 * SAME space as ADDR_VIEW_ORIGIN, subtracts that origin to get screen
 * coordinates, and outlines it. A full Lock/Unlock bracket, unlike the two line
 * drawers below it.
 *
 * docs/functions.tsv gives the entry 256 bytes; the function ends at
 * 0x00413684 and another begins at 0x00413690. Merged, like so many here.
 *
 * The rectangle, its enable flag and its colour are touched by exactly three
 * functions -- this one and 0x004137D0 and 0x00413E70, all in the same
 * neighbourhood and none of which names itself. So the role name says what it
 * DOES; whether the box is a drag-selection, a highlight or something else is
 * not established. */
#define ADDR_DRAW_VIEW_RECT     0x00413610u  /* void(void) */
#define ADDR_VIEW_RECT_ON       0x004FCF58u  /* int32_t, gates the draw */
#define ADDR_VIEW_RECT          0x004FCF70u  /* AM2_Rect in view space */
#define ADDR_VIEW_RECT_COLOUR   0x004FE089u  /* uint8_t */

/* ---- DirectDraw ------------------------------------------------------
 *
 * The game does its own software rasterising, but it does it straight into a
 * locked DirectDraw surface. LockSurface (0x0041B9A0) fills a 0x6C-byte
 * DDSURFACEDESC via IDirectDrawSurface::Lock (vtable[25]), retrying through
 * Restore (vtable[27]) on DDERR_SURFACELOST (0x887601C2), and publishes
 * lpSurface and lPitch into the globals below. The sprite dispatcher calls
 * Unlock (vtable[32]) when it is done.
 *
 * So the blitters below are the game's own code and safe to replace. What must
 * NOT be touched is ddraw itself -- these write into surface bits that
 * DirectDraw owns and may move or lose between frames.
 */
/* Putting the finished frame on the screen: a Flip when fullscreen, a BltFast
 * from the back buffer when windowed, because a window has no flipping chain.
 * Gated by 0x004FA030 -- clear it and the game runs with nothing appearing. */
#define ADDR_PRESENT_FRAME  0x0041AC60u  /* void(void) */
/* Run at the top of every frame: any surface that has been lost -- to an
 * alt-tab or a mode change -- is restored before anything draws. */
#define ADDR_RESTORE_LOST   0x0041A8B0u  /* void(void) */
/* Force what is drawn onto the screen now, outside the normal frame rhythm. */
#define ADDR_REFRESH_SCREEN 0x0044D6D0u  /* void(void), 7 call sites */
/* Small DirectDraw wrappers that take their object as an argument, which is why
 * tools/comcalls.py cannot name the interface and why they were invisible in
 * docs/boundary.md until someone went looking. */
#define ADDR_RELEASE_PALETTE   0x0041B6A0u  /* void(void *holder) */
#define ADDR_SET_PALETTE_RANGE 0x0041B720u  /* void(PALETTEENTRY*, first, last) */

/* The reserved-palette animation, all of it. ADDR_CYCLE_PALETTE re-uploads
 * entries 1..8 from one of six variants of the tileset palette, stepping
 * through a PING-PONG sequence -- 0,1,2,3,4,5,4,3,2,1 -- so the ramp runs up
 * and back down instead of snapping. Count and interval live beside the
 * sequence and are read every time; the shipped image has 10 and 160 ms.
 *
 * The palette array is .bss, filled when the tileset loads, and its stride is
 * 513 dwords -- what `shl 9; add` computes, reproduced rather than re-derived
 * from a guess at the element type. */
#define ADDR_CYCLE_PALETTE       0x0042B1A0u  /* void(void) */
#define ADDR_PALETTE_CYCLE_SEQ   0x00486138u  /* uint32_t[], the ping-pong */
#define ADDR_PALETTE_CYCLE_INDEX 0x00486160u  /* int32_t, cursor into it */
#define ADDR_PALETTE_CYCLE_COUNT 0x00486164u  /* int32_t, sequence length */
#define ADDR_PALETTE_CYCLE_INTERVAL 0x00486168u /* uint32_t, ms between steps */
#define ADDR_PALETTE_CYCLE_STAMP 0x00514F70u  /* uint32_t, last step's clock */
#define ADDR_TILESET_PALETTES    0x00503108u  /* PALETTEENTRY[][513] */
#define AM2_TILESET_PALETTE_BYTES (513u * 4u)
#define AM2_TILESET_PALETTES     6u   /* palette0.bmp .. palette5.bmp */
/* 0x0042B120, one caller. chdir to ADDR_MAP_BLOCK and load all six of them. */
#define ADDR_LOAD_TILESET_PALETTES 0x0042B120u /* void(void) */
/* The six file names, 16 bytes apart and laid out BACKWARDS: palette5.bmp is
 * at the low address and palette0.bmp at the high one, so walking forward from
 * ADDR_STR_PALETTE0 goes DOWN. Index i is ADDR_STR_PALETTE0 - i * 0x10. */
#define ADDR_STR_PALETTE0        0x00486288u  /* "palette0.bmp" */
#define AM2_PALETTE_NAME_STRIDE  0x10u
#define ADDR_SET_SURF_COLORKEY 0x0041B970u  /* void(surface *, uint8_t key) */
#define PALETTE_HOLDER_OFF     0x800u       /* where the DD palette hangs */

/* Copy a bottom-up 8-bit bitmap into a locked surface, optionally remapping
 * every byte through a 256-entry table. Stays original: pure pixel work with no
 * boundary in it, and shared by four callers.
 *
 * The last argument is a pointer, and every caller points it at a value it then
 * reads again afterwards -- so treat it as in/out and do not cache the value
 * across the call. Whether this routine actually writes through it was not
 * traced; passing the address and re-reading is faithful either way. */
#define ADDR_MAKE_BITMAP     0x0041BE80u  /* int32(src, pixels, dest, remap) */
#define ADDR_ENCODE_BIG      0x0041BBC0u  /* the >= 60000 pixel encoder */
#define ADDR_ENCODE_SMALL    0x0041BD20u
#define AM2_ENCODE_SCRATCH   0x30D58u     /* the encoders' stack buffer */
#define ADDR_ACTIVE_PALETTE  0x00477A58u  /* NULL means no remapping */
/* The state 3 entry action and the three things below it that had no names.
 * State 3 is the MOVIE state, which is what the greyscale palette, the movie
 * filename and the Smacker object below settle between them. */
#define ADDR_STR_GREYSCALE_BMP   0x0048523Cu  /* "avi\\greyscale.bmp" */
/* 0x0041ADB0, four callers. Clears the PRIMARY and the BACK BUFFER to
 * ADDR_BACKGROUND_COLOUR -- two ClearSurface calls and nothing else. */
#define ADDR_CLEAR_BOTH_SURFACES 0x0041ADB0u  /* void(void) */
/* 0x0042E770, four callers. Builds a movie filename into the caller's buffer
 * from a short name -- it knows "sml", "act1", "act2", "portal" and appends
 * ".smk". Named from those literals, not from a call site. */
#define ADDR_MOVIE_BUILD_NAME    0x0042E770u  /* void(char *dst, const char *) */
/* 0x0042E5E0, four callers. Plays one: it picks the directory ("3do" and
 * "credits" go somewhere different from everything else), then `operator new`s
 * 0xC4 bytes and constructs the Smacker class at 0x00444FC0 into
 * ADDR_CURRENT_MOVIE. The `new` and that constructor are what identify it. */
#define ADDR_PLAY_MOVIE          0x0042E5E0u  /* void(const char *, int32_t) */
/* The record MakeBitmap fills in. Not an AM2_Sprite despite the resemblance. */
#define BMP_OFF_SURFACE      0x00u
#define BMP_OFF_WIDTH        0x04u
#define BMP_OFF_HEIGHT       0x08u
/* The two dwords MakeBitmap copies straight out of the DIB header, from
 * biXPelsPerMeter and biYPelsPerMeter -- which is where this game's tools
 * smuggle the hot spot. Each packs two int16: the low half is the hot spot and
 * the high half is the field AM2_Sprite calls attachX/attachY. Confirmed against
 * the shipped bitmaps, which carry a REAL resolution there (2834 for 72 dpi,
 * 5038 for 128), so the +/-2048 clamp in SpriteReloadNamed fires on every one
 * of them and the hot spot comes out (0,0). Firing is measured; MATTERING is
 * not -- deleting the clamp changes no pixel of the one configuration that
 * runs the code. See the note there. */
#define BMP_OFF_HOT_X        0x0Cu   /* {hotX, attachX} */
#define BMP_OFF_HOT_Y        0x10u   /* {hotY, attachY} */
#define BMP_OFF_FLAGS        0x14u
#define BMP_OFF_KEY          0x18u   /* in: byte count, out: transparent index */
#define AM2_BMP_RECORD_SIZE  0x1Cu
/* The BMP record from a file NAME: open, read one DIB chunk, hand it to
 * MakeBitmap, free the pixels. Its only caller is SpriteReloadNamed. */
#define ADDR_LOAD_BITMAP_DESC 0x004230F0u  /* int32(const char *, void *) */
#define ADDR_STR_BITMAP_OPEN_FAIL 0x004789C4u /* "Couldn't open bitmap file!\n" */
#define BMP_FLAG_NO_COLORKEY 0x0001u
#define BMP_FLAG_SYSMEM      0x0040u
#define BMP_FLAG_RESERVE10   0x0080u /* SET => reserve the first ten entries */
#define BMP_FLAG_SOFTWARE    0x1000u
#define BMP_SOFTWARE_LIMIT   0xEA60  /* 60000 pixels */
#define BMP_RESERVED_ENTRIES 10
#define ADDR_STR_BMP_NO_VIDMEM 0x00478280u
#define ADDR_STR_BMP_NO_SURF   0x00478304u
#define ADDR_STR_BMP_NO_LOCK   0x004782E4u
/* 0x0041BA90, four callers, seven arguments. Copy a DIB's pixels into a
 * locked surface, optionally through a 256-byte remap table, and hand back the
 * transparent index.
 *
 * The source stride is `(width + 3) & ~3` -- DIB rows are dword-aligned -- and
 * the sign of `height` is the DIB convention: POSITIVE means bottom-up, so the
 * walk starts at the last row and steps backwards, and negative means top-down
 * and steps forwards. That pair is what identifies the format.
 *
 * The key byte is written only when ADDR_ACTIVE_PALETTE is set, and it is the
 * FIRST destination pixel -- so the transparent index is whatever ended up in
 * the top-left corner. Its seventh argument is BMP_OFF_KEY of the caller's
 * record, which that field's own comment already describes as "out: the
 * transparent index" -- and the store is a BYTE, so the rest of that field
 * survives. Reconstructed; its counter is 0 because all four callers are ours
 * and call by name. */
#define ADDR_BLIT_BITMAP_IN  0x0041BA90u  /* int32(dst,stride,src,w,h,lut,key*) */
#define ADDR_CREATE_BITMAP   0x00423D90u  /* surface *(FILE*, ...) */
#define ADDR_RELOAD_BITMAP   0x00424280u  /* int32(surface*, FILE*, ...) */
/* Two stores: clear ADDR_MENU_SAVED_VALID and put the argument in
 * ADDR_MENU_ENABLED. It said "stays original" with no reason beside it, which
 * turned out to mean "not yet" rather than a decision. */
#define ADDR_REFRESH_GATE   0x00412DE0u  /* void(int32) */
/* The three painters ADDR_REFRESH_DRAW reaches that had no names. All three
 * Lock and Unlock a surface and draw sprites; what distinguishes them is what
 * else they touch, which is all these names claim.
 *
 * 0x00409070 is the AIR layer, and it is reconstructed -- the block at
 * ADDR_AIR_PATH_TURN_X has the flight path it turned out to draw.
 *
 * 0x004123D0 is reconstructed: the AIM MARKERS, one per army. See the block
 * at ADDR_AIM_SPRITES_A. "Randomised full-screen drawing on a timer" was a
 * fair description of the shape and wrong about the scale -- the randomness
 * picks one of four flicker frames, and the drawing is a 112x112 block.
 *
 * ADDR_DRAW_SELECTION was the third and is reconstructed; see the block at
 * ADDR_SELECTED_ITEMS for what it turned out to be. */
#define ADDR_AIR_FRAME_DRAW      0x00409070u  /* void(void) */
#define ADDR_DRAW_EFFECT_LAYER   0x004123D0u  /* void(void) */
#define ADDR_DRAW_SELECTION      0x00462120u  /* void(void) */
#define ADDR_REFRESH_DRAW   0x00424BF0u  /* void(void), stays original */
#define ADDR_MAP_CACHE_SURFACE 0x00514E94u /* IDirectDrawSurface *, the painted map */
#define ADDR_PAINT_MAP_TILES   0x0042D580u /* void(const AM2_Rect *tiles) */
#define ADDR_MAP_TILES         0x00514EB8u /* uint16 *, one index per tile */
/* The scenario table, and all three names are ours -- what fixes the family is
 * the "Scenario" the parser at 0x0043DC10 scans for. Records are 0x40 bytes:
 * four dwords, then four 0x0C-byte sub-entries each ending in a malloc'd
 * string. The count is a WORD and the loop bound is re-read from it. */
#define ADDR_SCENARIOS           0x00656334u  /* uint8_t *, count * 0x40 */
#define ADDR_SCENARIO_COUNT      0x00656332u  /* uint16_t */
/* Zeroed by the parser on entry and by the teardown on exit, and read by
 * nothing in the image. Same standing as ZeroUnread50C34C's global. */
#define ADDR_SCENARIO_UNREAD     0x00656330u  /* int32_t */
#define AM2_SCENARIO_BYTES       0x40u
#define AM2_SCENARIO_PARTS       4u
#define SCENARIO_OFF_PARTS       0x10u
#define SCENARIO_PART_BYTES      0x0Cu
#define SCENARIO_PART_OFF_COUNT  0x04u  /* uint16_t, rows in the array below */
/* +0x08 was SCENARIO_PART_OFF_NAME until ParseScenarioPart was read. It is
 * not a name: it is a malloc'd array of SCENARIO_PART_OFF_COUNT records of
 * SCEN_ROW_BYTES each, and FreeScenarios frees it as one block. The old name
 * came from the free alone, which cannot tell an array from a string. */
#define SCENARIO_PART_OFF_ROWS   0x08u  /* uint8_t *, count * SCEN_ROW_BYTES */
/* One row of a scenario part, as ParseScenarioPart writes it and 0x0043DDA0
 * reads it back. +0x00 is a kind, which the reader compares against 0x8005;
 * +0x04 is two uint16 taken off the wire as x then y, which the reader stores
 * into a packed point global; +0x10 is an amount the reader floors at 1. Of
 * the rest, +0x11 is a flag the parser writes NEGATED and +0x12 one it
 * defaults to 1, so neither means on the wire what it means in the record.
 * +0x64 and +0x68 come off no wire at all -- the parser seeds them from
 * ADDR_ZERO_POINT and zero. */
#define SCEN_ROW_BYTES           0x6Cu
#define SCEN_ROW_OFF_KIND        0x00u
#define SCEN_ROW_OFF_POS         0x04u  /* int16_t x, int16_t y */
#define SCEN_ROW_OFF_AMOUNT      0x10u
#define SCEN_ROW_OFF_FLAG        0x11u  /* set when the wire byte was ZERO */
#define SCEN_ROW_OFF_FIELD_12    0x12u  /* wire byte, forced to 1 if zero */
#define SCEN_ROW_OFF_NAME        0x13u  /* to +0x64, so 0x51 bytes, unbounded */
#define SCEN_ROW_OFF_AT          0x64u  /* packed point, seeded to the origin */
#define SCEN_ROW_OFF_FIELD_68    0x68u
/* 0x0043DD30, one caller. Free the four row arrays each scenario owns, then
 * the table, then clear both globals. */
#define ADDR_FREE_SCENARIOS      0x0043DD30u  /* void(void) */
/* 0x0043DC10, one caller -- the map loader. Parse the scenario table out of
 * the map file's buffer: a count, then one 0x10-byte "Scenario<n>" header and
 * four parts per record. Reconstructed. */
#define ADDR_PARSE_SCENARIOS     0x0043DC10u  /* int32(uint8_t **, int32 *) */
/* 0x0043DAA0, one caller. Fill one 0x0C-byte part from the buffer and answer
 * how many bytes it took. Reconstructed. */
#define ADDR_PARSE_SCENARIO_PART 0x0043DAA0u  /* int32(void *part, void *at) */
#define ADDR_STR_SCENARIO        0x00487C20u  /* "Scenario", memcmp'd, 8 bytes */
#define AM2_SCENARIO_TAG_BYTES   8u
#define AM2_SCENARIO_HDR_BYTES   0x10u
#define SCENARIO_HDR_OFF_DIGIT   8u    /* '1'..'4'; the record it selects */
/* The byte beside it, indexed by a TILE INDEX rather than by a map square: 27
 * sites read it and nothing here writes it. The name is ours. */
#define ADDR_TILE_ATTRS        0x00514EBCu /* uint8_t *, one per tile index */
/* The MAP'S CELL WEIGHTS, and the name is evidenced rather than guessed:
 * ObjClearFootprint walks a vehicle mask's points and does `add byte, 0xF1`
 * on this table -- subtract fifteen -- with the adder doing the opposite, and
 * MarkOpenTile tests it against exactly that fifteen. So a covered cell is one
 * whose weight has reached AM2_CELL_WEIGHT_STEP. Per tile index, like the two
 * tables beside it. */
#define ADDR_CELL_WEIGHTS      0x00514EC0u /* uint8_t *, one per tile index */
/* A THIRD per-tile byte array, and the only one of the three the map FILE
 * supplies: 0x0042C761 mallocs width * height and freads straight into it,
 * with a second path that allocates and zeroes when the file has none. One
 * accessor returns the byte for a tile and CanPlaceAt requires every cell of a
 * placement to match a value the caller passes.
 *
 * THE VALUE IS ESTABLISHED NOW, AND IT IS AN ARMY. PlacementAllowed computes
 * `(CommArmyOfSlot(comm, slot) + 1) * 0x10` and uses it twice -- as the `kind`
 * it hands every CanPlaceAt, and again on the placement's own tile in its
 * final test. So the byte is 0x10 for army 0, 0x20 for army 1, 0x30 and 0x40
 * for 2 and 3, and a tile whose byte is something else belongs to somebody
 * else. A slot with no army answers -1, which makes the product 0; what 0
 * means is arithmetic here and is not read anywhere as a claim.
 *
 * Writer and reader, one function apart, which is the pairing this project
 * asks for before believing a layout -- the note above stood for months on
 * the reader alone. */
#define ADDR_TILE_KIND         0x00514ED4u /* uint8_t *, one per tile index */
#define AM2_TILE_KIND_ARMY_STEP  0x10   /* (army + 1) * this is the tile byte */
/* 0x0042BCF0, one caller. Seal the map's four edges with a full cell weight,
 * then walk every tile once: block what is marked open, mark what is blocked,
 * and flag everything outside a five-tile margin. */
#define ADDR_SEAL_MAP_EDGES    0x0042BCF0u /* void(void) */
#define AM2_EDGE_MARGIN        5
#define AM2_CELL_WEIGHT_STEP   0x0Fu       /* what one footprint point is worth */
#define OBJ_OFF_TILE           0x1Au       /* uint16_t, indexes the above */
/* Scratch, and only ObjTileChanged writes it: the tile the object was in
 * before that function recomputed OBJ_OFF_TILE from its position. Stored as a
 * DWORD from a zero-extended uint16, and compared as one. Not the map
 * object's ROW_OFF_X, which is the same offset in a different structure. */
#define OBJ_OFF_PREV_TILE      0x1Cu       /* uint32_t */
/* 0x00429570, six callers: that byte for the tile an object stands on, sign
 * extended. 0x00429CE0 next door is a plain cdecl forwarder for
 * ADDR_ITEM_PRE_DESTROY. Both names are ours. */
/* 0x00429590, 24 callers. How high an object stands: the byte at +0x65 is an
 * absolute floor when it is non-zero, and otherwise the tile's own attribute
 * byte is used; either way the signed byte at +0x64 is added. So +0x64 is an
 * offset and +0x65 an override, and neither name is the program's. */
#define ADDR_OBJ_HEIGHT        0x00429590u /* int32_t(const void *obj) */
/* 0x0042A820, five callers. The ground height at a point, raised by anything
 * standing on it: the tile's own attribute, then every ITEM in that cell whose
 * +0x65 is higher. The name is ours.
 *
 * It RETURNS A BYTE and only a byte -- the last instruction is `mov al, bl`
 * over an eax that still holds the masked tile index, so the upper bits are
 * not an answer. Log2Mask's problem, and the same rule: read `al`.
 *
 * 0x0042A550 is what it walks with, still original: it collects every object
 * in the cell a point falls in and chains them through OBJ_OFF_QUERY_NEXT --
 * a name that was already in this file, on the same offset, and which the
 * ratchet caught being invented a second time with an identical value. The
 * field is scratch: a query overwrites it, so the chain lives only until the
 * next call. */
#define ADDR_HEIGHT_AT_POINT   0x0042A820u /* uint8_t(uint32_t packedPoint) */
#define ADDR_OBJECTS_AT_POINT  0x0042A550u /* void *(const AM2_Point*, desc) */
/* 0x0042A1B0, five callers -- the precise hit test. An object qualifies when
 * the point is inside its own OBJ_OFF_HIT_RECT and, if it has one, inside its
 * per-pixel OBJ_OFF_HIT_MASK. ADDR_OBJECTS_AT_POINT asks a looser question of
 * the same cell, building a box out of four offsets at +0x7C..+0x88.
 *
 * IT WENT IN AS "the mouse pick" AND IT IS NOT ONE. That came from three of
 * its callers sitting in the 0x0041xxxx HUD band -- naming a function from
 * its call site, the mistake this file warns about six times over, made while
 * quoting the warning. Not one of the five passes a cursor: two build the
 * point from a world origin at 0x00514E14 plus a table offset, one from
 * PointOfTile, and one from a float projection. It answers "what is at this
 * world point", and nothing about it knows where the pointer is.
 *
 * Both chain their answers through OBJ_OFF_QUERY_NEXT and both walk the cell
 * with the descriptor's COLS as the bound in each direction -- see
 * MapDescInit, which sizes the grid that way. */
#define ADDR_OBJECTS_HIT_BY_POINT 0x0042A1B0u /* void *(const AM2_Point*, desc) */
/* The third member of that family is ADDR_WALK_CELL_AT_POINT, further down --
 * the same cell walk and the same two hit tests with a caller-supplied
 * predicate between them. All three walk the cell themselves; none is built
 * on either of the others. */
#define OBJ_OFF_HIT_RECT       0x30u  /* AM2_Rect, in world units */
#define OBJ_OFF_HIT_MASK       0x78u  /* non-null means test the bitmask too */
/* The four ObjectsAtPoint adds to the object's own position to make its
 * looser box. Only that function reads them, and only when OBJ_OFF_HIT_MASK
 * is null -- so they are the fallback shape for an object with no bitmask. */
#define OBJ_OFF_BOX_LEFT       0x7Cu  /* int32_t, added to OBJ_OFF_POS.x */
/* 0x00438F80, two callers. Offset the object's box by its position and hand
 * the result to 0x00438DF0 -- but only when the first row's sprite has a
 * software image; without one it answers 1 and does nothing. */
#define ADDR_OBJ_BOX_ACTION      0x00438F80u  /* int32_t(void *obj, AM2_TileMask *) */
/* The SCRATCH both box markers fill, and its fifth argument is a POINTER --
 * this macro said `int32_t` for as long as the function was reached through
 * it, because both callers of ObjBoxAction pass a stack buffer and nothing on
 * the C side ever looked. The record is a tile rectangle padded by two on
 * every side, then one byte per tile inside it, row-major.
 *
 * Two values go in: 2 over the whole padded rectangle and 3 over the box
 * itself. The one reader tests BIT 0, so the margin exists to be walked over
 * rather than acted on, and nothing ever writes a 0 -- its `test al, al`
 * guard cannot fire on anything this function produces. */
#define ADDR_BOX_ACTION          0x00438DF0u  /* int32_t(l,t,r,b, AM2_TileMask *) */
#define TILEMASK_OFF_RECT        0x00u  /* AM2_Rect, in TILES, padded by two */
#define TILEMASK_OFF_CELLS       0x10u  /* uint8_t[], row-major over that rect */
#define AM2_TILEMASK_MARGIN      2      /* tiles added on every side */
#define AM2_TILEMASK_PAD_CELL    2      /* what the margin is filled with */
#define AM2_TILEMASK_BOX_CELL    3      /* and the box; bit 0 is what is read */
/* The frame ItemTeardown reserves for one, through __chkstk. CanPlaceAt
 * reserves 0x1018 for the same structure; the difference is frame alignment,
 * not layout, and this is the larger of the two so it holds either. */
#define AM2_TILEMASK_BYTES       0x1020u
/* 0x004389D0, 1056 bytes, two callers -- ObjBoxAction's twin for an object
 * that HAS an OBJ_OFF_HIT_MASK, which on every map this project can drive is
 * all of them. Still original; region.cpp records that ObjBoxAction and
 * BoxAction both read 0 on a full combat run for exactly that reason. */
#define ADDR_OBJ_HIT_MASK_ACTION 0x004389D0u  /* int32_t(void *obj, void *) */
/* READ AND NOT RECONSTRUCTED, recorded the way LoadType2, CreateTrooper and
 * CreateVehicle were before they were taken -- all three of which came back
 * and went in once the neighbouring arithmetic had been done twice.
 *
 * WHAT IS SETTLED. Its only callee is Clamp: 1,056 bytes of arithmetic and one
 * function call, which is why it looks harder than it is. Two refusals, both
 * answering 0 -- no OBJ_OFF_HIT_MASK at all, and a mask whose cell pointer at
 * +0x0C is null. The mask header is four int16s, a left, a top, a width and a
 * height, with that pointer at +0x0C.
 *
 * IT CLAMPS TWICE WHERE BoxAction CLAMPS ONCE: first the object's
 * OBJ_OFF_HIT_RECT offset by the mask's own rect against ADDR_MAP_EXTENT_X/Y
 * with a SIXTEEN-unit margin, then the same values shifted down four against
 * ADDR_MAP_TILES_W/H with the usual AM2_TILEMASK_MARGIN of two. Then it writes
 * the four TILEMASK_OFF_RECT edges in BoxAction's own order -- right, bottom,
 * left, top -- and memsets the cells to ZERO where BoxAction fills them with
 * AM2_TILEMASK_PAD_CELL.
 *
 * THERE IS A SECOND NEIGHBOUR TABLE AND THIS IS WHAT BUILDS IT. Twenty dwords
 * at 0x00554B84, immediately past ADDR_TILE_RING4's four, computed from the
 * TILEMASK's stride the way ADDR_TILE_NEIGHBOURS is computed from the map's.
 * So the game keeps two of these and BuildTileDeltas fills only one of them.
 * The inner loop ORs 3 into the cell it lands on and 2 into each of the
 * twenty, which is the same "this cell and its ring" shape ShiftTileCover has.
 *
 * WHAT IS NOT: the walk itself. It tests one BIT per cell through the table at
 * 0x00487814 -- 0x80, 0xC0 ... 0xFF, 0x7F, 0x3F ... 0x00, the ordinary
 * bits-from-position-n mask -- and it selects between an eight-step and a
 * sixteen-step stride on a PARITY test of the row against a value saved
 * earlier. Which is the whole of the mask walk, and it is not read. */
#define ADDR_TILEMASK_NEIGHBOURS 0x00554B84u  /* int32_t[20], built per call */
#define AM2_TILEMASK_RING        20   /* the 5x5 block less its corners and centre */
/* TWO eight-entry tables laid end to end, and the index runs 1..8 into each,
 * so BOTH walk one past their own. HIGH is 80 C0 E0 ... FF and LOW is
 * 7F 3F 1F ... 00; index 8 into HIGH reads LOW's first entry (0x7F) and index
 * 8 into LOW reads whatever is at 0x00487824. See ObjHitMaskAction for why the
 * index is one past the entry the split wants.
 *
 * IT WENT IN AS *THREE* TABLES AND THAT WAS WRONG. The eight 0xFF bytes past
 * LOW read as a deliberate all-ones table sized to absorb exactly that
 * overrun, which is a tidy story and not what they are: 0x00487824 is a plain
 * int32_t that RegionFindPath reads and WRITES -- ADDR_REGION_SEARCH_STATE
 * below. It is -1 in the image and the only value ever stored is -1, so the
 * byte the overrun reads really is 0xFF; but that is where a global happens to
 * sit, not a table anyone laid out. Found by reading the OTHER toucher, which
 * is the rule this file states for layouts and applies just as well to a
 * table's extent. */
#define ADDR_BIT_FROM_N          0x00487814u  /* uint8_t[16]: HIGH then LOW */
#define AM2_BIT_FROM_N_LOW       8    /* what to add for the second half-byte */
/* 0x00487824, touched only by RegionFindPath, which compares it against -1 to
 * choose between starting a search and resuming one -- and whose only write to
 * it is -1. So the resume arm cannot be reached: nothing in the image can put
 * any other value there. Recorded rather than acted on; it is the same
 * standing as the copy-protection branches. */
#define ADDR_REGION_SEARCH_STATE 0x00487824u  /* int32_t, always -1 */
/* The per-object hit bitmask OBJ_OFF_HIT_MASK points at. Four int16s and a
 * pointer; the rows run TOP-DOWN and the row stride is the width in bits
 * rounded up to a dword. misc.cpp's ObjMaskBitAt reads the same record. */
#define OBJMASK_OFF_ORIGIN_X     0x00u  /* int16_t */
#define OBJMASK_OFF_ORIGIN_Y     0x02u  /* int16_t */
#define OBJMASK_OFF_WIDTH        0x04u  /* int16_t, in bits */
#define OBJMASK_OFF_HEIGHT       0x06u  /* int16_t, in rows */
#define OBJMASK_OFF_BITS         0x0Cu  /* uint8_t *, null means "no mask" */
/* THE TWO READERS DISAGREE ABOUT THE SIGN OF THE ORIGIN, and this is recorded
 * rather than resolved. ObjMaskBitAt places the mask's top-left at
 * `hitRect.topLeft - origin` (0x004353CC: `movsx ecx,[edx]; sub ecx,[esi+0x30]`)
 * and ObjHitMaskAction at `hitRect.topLeft + origin` (0x00438A13:
 * `movsx ecx,[edi]; add ecx,[ebp+0x30]`). Each is internally consistent -- the
 * row index each computes agrees with the origin each assumed -- so they can
 * only both be right where the origin is zero. Neither is "corrected" here;
 * both are transcribed as the image has them. */
/* The scratch record's size, from the one caller that puts it on the STACK:
 * CanPlaceAt reserves 0x1018 bytes and the cells run from TILEMASK_OFF_CELLS
 * to the end of that frame less two locals. BoxAction clamps into the map, so
 * nothing here bounds the fill by this figure -- it is the caller's frame that
 * says how big the array is. */
#define AM2_TILEMASK_CELLS       0x1000u
#define SPR_FLAG_SOFTWARE_BITS   0x1Cu  /* the subset of 0x3C these two test */
#define OBJ_OFF_BOX_TOP        0x80u  /* int32_t, added to .y */
#define OBJ_OFF_BOX_RIGHT      0x84u  /* int32_t, added to .x */
#define OBJ_OFF_BOX_BOTTOM     0x88u  /* int32_t, added to .y */
/* Bit 17 of OBJ_OFF_FLAGS, read in one place: ObjectsAtPoint takes it as
 * "ask ADDR_OBJ_ROWS_MASK_AT instead of the box or the bitmask". */
#define OBJ_FLAG_ROWS_MASK     0x20000u
/* 0x00435440, one caller. "Is this point on me", answered against the
 * object's SPRITE rather than a bitmask -- where ADDR_OBJ_MASK_BIT_AT tests
 * one bitmask. Refuses an object with fewer than one row, a row with no
 * sprite, or a sprite with no image, and refuses a point outside the row's
 * rectangle before any mask is consulted.
 *
 * IT TESTS THE FIRST ROW ONLY. This comment used to say it walks
 * OBJ_OFF_ROWS and tests every row, and the body does not: there is one load
 * of `rows`, one `[rows + ROW_OFF_SPRITE]`, and no loop. The row COUNT is
 * read, but only to refuse an object that has none. Reconstructed, and the
 * claim was corrected by writing it. */
#define ADDR_OBJ_ROWS_MASK_AT  0x00435440u /* int32_t(obj, const AM2_Point *) */
/* The mask test itself is ADDR_OBJ_MASK_BIT_AT, further down and already
 * reconstructed -- a second name went on it here and both the alias ratchet
 * and checkseams said so in the same run. */
/* 0x00459FB0, four callers. A uid to a UNIT: the lookup, then types 2, 3 and
 * 8 accepted and everything else refused. That set is exactly ObjIsType2,
 * ObjIsType3 and ObjIsType8 -- trooper, vehicle, roach -- which is why the
 * name is `unit` rather than something structural. Uid 0 is refused before
 * the lookup, unsigned. */
#define ADDR_UNIT_BY_UID       0x00459FB0u /* void *(uint32_t uid) */
#define OBJ_OFF_HEIGHT_ADJ     0x64u       /* int8_t, always added */
#define OBJ_OFF_HEIGHT_SET     0x65u       /* uint8_t, replaces the tile's */
#define ADDR_OBJ_TILE_ATTR     0x00429570u /* int32_t(const void *obj) */
/* 0x00429540, three callers: the same byte, taken by tile INDEX rather than by
 * object. The index is masked to 16 bits here where its neighbour reads a word
 * -- the same value, arrived at differently. */
#define ADDR_TILE_ATTR_AT      0x00429540u /* int32_t(uint32_t tile) */
#define ADDR_ITEM_PRE_DESTROY_ALIAS 0x00429CE0u /* void(obj, int32_t) */
/* Log2Mask of ADDR_MAP_EXTENT_X, written beside ADDR_MAP_ROW_SHIFT in
 * LoadMap's MHDR arm -- the same derivation one scale up, tiles against
 * world units. Named from the writer, which is the only toucher this file
 * has seen. */
#define ADDR_MAP_EXTENT_SHIFT    0x00514DD8u  /* int32_t */
#define ADDR_MAP_ROW_SHIFT     0x00514DE4u /* int32, log2 of the map's width */
/* The camera doubles as the top-left of the visible-tile rectangle: the four
 * dwords from ADDR_CAMERA_X are used as a RECT to clip against. */
#define ADDR_VISIBLE_TILES     0x00514EA8u /* AM2_Rect, in tiles */
#define MAP_TILE_SIZE          16
#define MAP_SHEET_COLUMNS      0x1F   /* mask; the sheet is 32 tiles wide */
#define ADDR_FREE_MAP_SURFACES 0x0042D390u /* void(void) */
/* 0x0042D3D0, two callers -- the level teardown and the map loader, which
 * clears before it fills. Free every per-map allocation: the region array with
 * each region's own link list first, then twelve pointers in a row, each
 * guarded and each cleared after. Reconstructed. */
#define ADDR_FREE_MAP_LAYERS   0x0042D3D0u /* void(void) */
/* The menu's sprite table: 190 AM2_Sprite* laid out as 19 rows of 10, one more
 * slot just past the end, and a surface of its own. */
#define ADDR_FREE_MENU_SPRITES 0x00412F80u /* void(void) */
#define ADDR_MENU_SPRITES      0x004FCAACu /* AM2_Sprite *[190] */
/* One past ADDR_MENU_SPRITES, and a slot in its own right: OverlayPrepare
 * writes the chosen row's FIRST sprite here and DrawMenuCursor draws whatever
 * is in it, which is why surface.cpp calls it g_cursorSprite. The name is the
 * address's arithmetic, not its job. */
#define ADDR_MENU_SPRITES_END  0x004FCDA4u
/* The clock reading the last prepare was throttled against -- see
 * ADDR_OVERLAY_PREPARE, which does at most one row change per millisecond
 * unless it is a net game or the caller forces it. */
#define ADDR_MENU_ROW_STAMP    0x004FCDF0u /* uint32_t */
#define ADDR_MENU_SURFACE      0x004FCDF4u /* IDirectDrawSurface * */
/* The animated menu cursor, and the save-under it needs so the frame beneath
 * it survives. The LAST function in the image with any COM dispatch. */
#define ADDR_DRAW_MENU_CURSOR    0x00412FE0u  /* void(void) */
#define ADDR_MENU_ENABLED        0x004FCEF8u  /* int32_t; nothing drawn while 0 */
/* Where 0x00426F40 accumulates the mouse deltas into an absolute pointer,
 * clamped to the screen. The menu cursor is drawn here. */
#define ADDR_CURSOR_X            0x00485464u  /* int32_t */
#define ADDR_CURSOR_Y            0x00485468u  /* int32_t */
#define ADDR_MENU_ROW            0x004FCAA8u  /* int32_t, row into the sprite grid */
/* 0x00412DD0, 21 callers and five bytes of body: `mov eax, [ADDR_MENU_ROW]`.
 * A getter, which is why so many things use it rather than the global. */
#define ADDR_GET_MENU_ROW        0x00412DD0u  /* int32_t(void) */
#define ADDR_MENU_ANIM_FRAME     0x004FCDECu  /* int32_t 0..9, -1 stops the cycle */
#define ADDR_MENU_ANIM_NEXT      0x004FCDE8u  /* uint32_t, tick the next frame is due */
#define MENU_ANIM_PERIOD         0xC8u        /* 200 ms */
/* Signed on purpose: the original compares with `jl`, and the frame index
 * can legitimately be -1, which is how a row says it does not animate. */
#define MENU_ANIM_FRAMES         0x0A
/* Non-zero once something has been saved under the cursor. */
#define ADDR_MENU_SAVED_VALID    0x004FCEFCu  /* int32_t */
#define ADDR_MENU_SAVED_RECT     0x004FCA98u  /* AM2_Rect, where it came from */
/* This frame's cursor rectangle and last frame's. */
#define ADDR_MENU_CURSOR_RECT    0x004FCDD8u  /* AM2_Rect */
#define ADDR_MENU_CURSOR_PREV    0x004FCDC8u  /* AM2_Rect */
/* Two optional overlays drawn with the cursor, and the flags and offsets they
 * carry. */
#define ADDR_MENU_OVERLAY_A      0x004FCDA8u  /* AM2_Sprite * */
#define ADDR_MENU_OVERLAY_B      0x004FCDACu  /* AM2_Sprite * */
/* THE COLOUR TABLE each of those three is drawn with, and the names were
 * "MODE" and two "FLD"s because the only reader looked at them was
 * DrawMenuCursor, which tests one for non-zero and copies it into the sprite's
 * SPR_OFF_MODE. A boolean test names a boolean.
 *
 * PlaceCursorPrepare is the WRITER and it puts a POINTER in all three: record
 * `CommArmyOfSlot(comm, defaultOwner)` of ADDR_OBJ_TABLE_RECORDS, which is
 * 0x100 bytes per army and is the same block ADDR_ARMY_INK indexes; or
 * ADDR_FLAME_RECORD, 256 bytes InitMenuScreen fills; or ADDR_PALETTE_GLYPHS,
 * 256 bytes derived from the live palette. Three different 256-byte tables in
 * one slot is a remap, not a mode.
 *
 * What DrawSprite does with SPR_OFF_MODE further down is still unread, so the
 * claim here is exactly what the writer puts there and no more. */
#define ADDR_MENU_INK            0x004FCDB0u  /* uint8_t[256] *, or NULL */
#define ADDR_MENU_OVERLAY_A_INK  0x004FCDB4u
#define ADDR_MENU_OVERLAY_B_INK  0x004FCDB8u
#define ADDR_MENU_CURSOR_DX      0x004FCDBCu  /* int16_t pair */
#define ADDR_MENU_OVERLAY_A_DX   0x004FCDC0u
#define ADDR_MENU_OVERLAY_B_DX   0x004FCDC4u
/* The rectangle the paint object reports, and the DDBLTFX the Blts pass. */
/* The fixed rectangle inside the menu surface that the save-under occupies.
 * It is a RECT and not a DDBLTFX -- it is passed as Blt's source when
 * restoring and as its destination when saving. */
#define ADDR_MENU_SAVE_SLOT      0x00476198u  /* RECT */
#define MENU_ROW_DIRECT          0x13         /* at or above, lock and draw directly */
/* 0x00426CD0, 15 callers. Milliseconds since InitTimer, from the performance
 * counter when there is one and from GetTickCount when there is not. The two
 * answers are not the same clock and nothing reconciles them; which one a run
 * gets is decided once, at startup. */
#define ADDR_TICKS               0x00426CD0u  /* uint32_t(void) */
/* Sprite fields the cursor code reads: two int16 hotspot pairs and the
 * width/height, plus the mode slot DrawSprite consults. */
#define SPR_OFF_FORMAT           0x08u   /* AM2_Sprite::format; 0 = a surface */
#define SPR_OFF_FLAGS            0x0Cu   /* AM2_Sprite::flags */
#define SPR_OFF_IMAGE            0x10u   /* AM2_Sprite::image, the union */
#define SPR_OFF_W                0x1Cu
#define SPR_OFF_H                0x20u
#define SPR_OFF_HOTX             0x24u   /* int16_t */
#define SPR_OFF_HOTY             0x26u   /* int16_t */
#define SPR_OFF_OVX              0x28u   /* int16_t */
#define SPR_OFF_OVY              0x2Au   /* int16_t */
#define SPR_OFF_MODE             0x34u
/* Holds a POINTER to the map sprite record -- reaching anything in it is two
 * dereferences. In that record: +0x10 the surface, +0x1c and +0x20 its width
 * and height, +8 a flag. PaintMapTiles and RestoreTileSet both read it. */
#define ADDR_MAP_SURFACE    0x00514E90u
/* Tail-called after a map restore -- but named for what it *is*, not for the
 * one call site. It went in as ADDR_ON_MAP_RESTORED; its own error strings say
 * `RestoreTileSet`, and it reloads the tileset from disk. */
#define ADDR_RESTORE_TILESET 0x0042C0E0u /* void(void) */
/* The `.atl` reader's inputs. The name is formatted into "%s.atl"; the path is
 * only probed, and DataPathExists' answer is discarded. */
/* Non-zero reserves the first ten palette entries, as BMP_FLAG_RESERVE10 does
 * for MakeBitmap -- the same convention reached a different way. */
#define ADDR_TILESET_RESERVE 0x00511CC8u /* int32_t */
/* 0x00422FF0: reads one DIB chunk from an open stream into the 0x428-byte
 * bitmap header MakeBitmap also reads -- ten dwords then a 256-entry palette --
 * and returns the pixels in a buffer the caller frees. */
#define ADDR_READ_DIB_CHUNK  0x00422FF0u
#define ADDR_FTELL           0x00464DC0u  /* long(FILE *) */
#define ADDR_STR_TOO_MANY_COLOURS 0x004789A8u /* "Too many colors in bitmap.\n" */
#define ADDR_STR_DIB_MALLOC_FAIL  0x00478988u /* "malloc failed in ReadBitmap()\n" */
/* 0x00423200: open a file, read one DIB chunk from it, and flip the result
 * top-down into a fresh buffer. The two header fields it needs are the row
 * count and the total byte size. */
#define ADDR_LOAD_DIB_FLIPPED 0x00423200u /* void *(const char *, void *, uint16_t *) */
#define DIB_OFF_BLOCKS        0x08u   /* int32_t, ReverseBlocks' block count */
#define DIB_OFF_SIZE          0x14u   /* int32_t, total bytes -- the listed size */
#define ADDR_MSG_TILESET_OPEN 0x00486310u /* "Unable to open tileset" */
#define ADDR_MSG_TILESET_LOCK 0x004862ECu /* "Error on Lock in RestoreTileSet()" */
#define ADDR_MSG_TILESET_LOAD 0x004862D4u /* "Error in loadtileset()" */
#define ADDR_FMT_ATL          0x00486328u /* "%s.atl" */
#define ADDR_PRESENT_ENABLED 0x004FA030u /* int32_t */
#define ADDR_LOCK_SURFACE   0x0041B9A0u  /* int32_t(IDirectDrawSurface*) */
#define ADDR_UNLOCK_SURFACE 0x0041BA40u  /* int32_t(void) */
#define ADDR_SURFACE_LOCKED 0x004FDF80u  /* int32_t; non-zero while a lock is held */
/* The surface drawing is aimed at. TWO functions write it, which is why the
 * old name -- ADDR_LOCKED_SURFACE, "currently locked" -- was only half right:
 * SetDrawTarget designates it, long before and quite independently of any
 * lock, and LockSurface also stores whatever it has just locked. UnlockSurface
 * reads it to know what to release, and LockSurface reads it to answer "is the
 * surface already held the one you are asking for", which is the check behind
 * "another surface already locked!". */
#define ADDR_DRAW_TARGET    0x00507128u  /* IDirectDrawSurface *, the draw target */
#define ADDR_PRIMARY_SURFACE 0x00502AD4u /* IDirectDrawSurface *, Restore target */
#define ADDR_SCREEN_PITCH   0x00502AD0u  /* int32_t, DDSURFACEDESC.lPitch */
#define ADDR_FRAMEBUFFER    0x004FE1A8u  /* uint8_t *, DDSURFACEDESC.lpSurface */
/* Applied when the locked surface is the primary one. */
#define ADDR_ORIGIN_DX      0x00485330u  /* int32_t */
#define ADDR_ORIGIN_DY      0x00485334u  /* int32_t */

/* The software RLE blitter family. All four are the same routine differing
 * only in row-offset width and fill policy -- see src/game/blit.c. */
#define ADDR_BLIT_GLYPH     0x0041C710u  /* solid fill,   16-bit offsets */
#define ADDR_BLIT_COPY16    0x0041C2B0u  /* copy source,  16-bit offsets */
#define ADDR_BLIT_COPY32    0x0041C1C0u  /* copy source,  32-bit offsets */
#define ADDR_BLIT_REMAP16   0x0041C3A0u  /* copy via LUT, 16-bit offsets */

/* Sprite drawing. The dispatcher is not reconstructed yet; it fans out to the
 * four blitters above plus 0x0041C480 (656 bytes, unread) and 0x00445EB0,
 * which is not a blitter at all but a fallback chain that calls
 * IDirectDrawSurface::Restore on the sprite's own surface. */
#define ADDR_DRAW_SPRITE         0x00445FF0u  /* void(AM2_Sprite*,x,y,mode) */
#define ADDR_DRAW_SPRITE_CLIPPED 0x00446070u  /* void(spr,x,y,const AM2_Rect*,mode) */
#define ADDR_BLIT_OVERLAY        0x0041C480u  /* __fastcall(x,y,data,AM2_Rect) */
#define ADDR_RESTORE_CHAIN       0x00445EB0u  /* void(AM2_Sprite*) */
/* Sprite lifetime, and this comment was wrong in three ways until the lookup
 * was read rather than described. The registry is a count at 0x006598C0, a
 * CAPACITY at 0x006598BC, an AM2_Sprite* table indexed by SLOT at 0x006598C4
 * -- which is ADDR_SPRITE_TABLE below -- and a separate table of {id, slot}
 * PAIRS at 0x006598C8. The lookup is a BINARY SEARCH over the pairs, not a
 * walk over the sprites, and what it returns is the slot. */
#define ADDR_SPRITE_REG_COUNT    0x006598C0u  /* int32_t */
#define ADDR_SPRITE_REG_PAIRS    0x006598C8u  /* {uint32 id; int32 slot} * */
#define ADDR_SPRITE_REG_CAP      0x006598BCu  /* int32_t, grown 50 at a time */
/* The open sprite FILE, closed by the registry teardown and by nothing else --
 * two references in the image and both are in that one function. */
#define ADDR_SPRITE_FILE         0x006598B8u  /* am2_FILE * */
/* 0x00445F40, three callers. Close that file, force-release every registered
 * sprite, free both tables and zero the count and capacity. It went in as the
 * placeholder ADDR_FREE_445F40 when only its caller was read; the alias
 * ratchet is what stopped a second name landing beside it. */
#define ADDR_FREE_SPRITE_REGISTRY 0x00445F40u /* void(void) */

#define AM2_SPRITE_REG_GROW      50
#define ADDR_FILL_SOUND_BUFFER   0x0040C440u  /* int32(buf, const void *, uint32) */
#define ADDR_STR_SND_LOCK_FAIL   0x00474E6Cu  /* "Unable to lock sound buffer\n" */
#define ADDR_STR_SND_NO_ARGS     0x00474E44u  /* "Fill sound buffer missing arguments\n" */
#define ADDR_BLIT_CENTRED        0x00445500u  /* thiscall void(this, surface *) */
#define BLIT_SRC_OFF_SURFACE     0x04u   /* the source, inside `this` */
#define BLIT_SRC_OFF_DESC        0x1Cu   /* -> {?, width, height} */
#define ADDR_DRAW_SEQ_BAR        0x004624A0u  /* void(x,y,colour,value,base) */
/* One byte, three uses and formerly two names: the sequence bar's unfilled
 * colour, the fill AttachPalette is handed, and the list box's background. All
 * three were local descriptions of the same thing -- the palette index this
 * game fills with -- so ADDR_SEQ_BAR_BG and ADDR_PIXEL_FORMAT_BYTE are one
 * name now. "Pixel format byte" was the actively misleading one; it is not a
 * pixel format. */
#define ADDR_BACKGROUND_COLOUR   0x00502AD9u  /* uint8_t */
#define ADDR_STR_SEQ_BLT_FAIL    0x0048CBE8u  /* "Couldn't Blt Seq Pixels\n" */
/* SEQ is the program's own word for this band, and that string is where it
 * comes from -- so the record walker below is named with it rather than with
 * something invented. What a "seq" IS remains unestablished.
 *
 * 0x00461870 walks one context's table: 48-byte records, a kind at +0x00
 * dispatched through an eight-arm jump table, a gate at +0x08 that skips the
 * record when it is not positive, and a next index at +0x2C. Every arm RETURNS
 * the next index, so the walk is index-chained rather than sequential, and a
 * skipped record supplies its own successor. Arm 1 does nothing but continue.
 *
 * 0x00461930 is the whole of the per-frame step: run it over two contexts. */
#define ADDR_SEQ_RUN             0x00461870u  /* void(void *ctx) */
/* Its seven steppers, kind by kind. Kinds 2 and 3 share one, and kind 1 has
 * no stepper at all -- see misc.cpp on why that is a hang rather than a
 * no-op. All take (index, record, context) and answer the next index. */
#define ADDR_SEQ_STEP0           0x00461150u
/* Reconstructed. Kinds 2 and 3 share it: bump the row's ROW_OFF_STAMP_54 and
 * retire the record once that has passed 1, so these two live for exactly two
 * frames. */
#define ADDR_SEQ_STEP2           0x00461310u  /* kinds 2 AND 3 */
/* 0x00460EC0, eleven callers: take one seq record out of the chain. Every
 * stepper that can finish calls it, and it answers the index to carry on
 * from. Stays original. */
#define ADDR_SEQ_RETIRE          0x00460EC0u  /* int32_t(void *ctx, void *rec) */
/* The number of frames per variant in ADDR_SEQ_SPRITES_7, which kinds 6 and 7
 * share: kind 7 takes entry 0 and kind 6 takes `variant * 8`. */
#define AM2_SEQ_VARIANT_STRIDE   0x0048CB8Cu  /* int32_t, reads 8 */
#define ADDR_SEQ_STEP4           0x004613E0u
/* Reconstructed. Kind 5 is an EMITTER: every ADDR_SEQ_EMIT_MS it adds a kind
 * 4 into the OTHER context, at its own point jittered by -4, 0 or +4 in x,
 * and it retires when its elapsed time passes SEQ_OFF_LIFE. */
#define ADDR_SEQ_STEP5           0x004614D0u
/* 0x00461350, one caller, 144 bytes: a fourth adder, and the only one that
 * fills ADDR_SEQ_CTX_B rather than ctx A. Kind 4. Stays original. */
/* Reconstructed. Its three constants are globals rather than literals, which
 * is the only reason they can be named at all. */
#define ADDR_SEQ_ADD_KIND4       0x00461350u  /* void(const int32_t *at, int32_t) */
/* The kind-4 constants, now that its stepper says what each is for: the two
 * SEQ_OFF_FIELD_0C/_10 sources are a per-step DRIFT in x and y, and the life
 * is the interval BETWEEN steps rather than a total -- the stepper zeroes the
 * row's elapsed each time it fires. */
#define ADDR_SEQ_K4_DRIFT_X      0x0048CBD8u  /* int32_t, reads 3 */
#define ADDR_SEQ_K4_DRIFT_Y      0x0048CBDCu  /* int32_t, reads -1 */
#define ADDR_SEQ_K4_STEP_MS      0x0048CBC4u  /* int32_t, reads 120 */
/* How many steps each of the four cells holds for, indexed by ROW_OFF_CELL. */
#define ADDR_SEQ_K4_HOLD         0x0048CBC8u  /* int32_t[], reads 2,3,4,5 */
/* Added to ROW_OFF_Y_ADJUST every step, so it also floats. */
#define ADDR_SEQ_K4_RISE         0x0048CBE0u  /* int16_t, reads 3 */
#define AM2_SEQ_K4_CELLS         4
#define AM2_SEQ_KIND4            4
#define AM2_SEQ_EMIT_ARG         0x0F
/* How often kind 5 emits, in milliseconds. Two readers, both in that one
 * stepper: the test and the subtraction that pays for it. */
#define ADDR_SEQ_EMIT_MS         0x0048CBE4u  /* int32_t, reads 300 */
#define ADDR_SEQ_STEP6           0x00461560u
#define ADDR_SEQ_STEP7           0x00461700u
#define ADDR_SEQ_RUN_BOTH        0x00461930u  /* void(void) */
#define ADDR_SEQ_CTX_A           0x00664580u
/* 0x00461120, two callers. The sequence subsystem coming down: the same
 * releaser over CTX_B then CTX_A, then two more calls in the same band. Named
 * for the two globals it names, which is as far as the body goes. */
#define ADDR_FREE_SEQ_CONTEXTS   0x00461120u  /* void(void) */
#define ADDR_SEQ_CTX_B           0x006640B0u
#define ADDR_RELEASE_SPRITE      0x00445D80u  /* void(AM2_Sprite *) */
#define ADDR_CLEAR_SPRITE        0x00445E40u  /* void(AM2_Sprite *) */
#define ADDR_SPRITE_SLOT_OF      0x00445990u  /* int32(uint32 id); <0 when absent */
#define ADDR_SPRITE_TABLE        0x006598C4u  /* AM2_Sprite ** */
#define ADDR_STR_RELEASE_WRONG   0x00489768u  /* "Error in release: Wrong sprite!\n" */
#define ADDR_STR_RELEASE_MISSING 0x00489740u  /* "Error in release: Sprite not found!\n" */
/* The three ways a sprite's pixels are put back after its surface is restored.
 * All stay original; which one applies is decided in RestoreSpriteSurface. */
#define ADDR_SPRITE_RELOAD_NAMED 0x004456B0u  /* int32(spr, const char *, flags) */
/* The switch reads the other way round from what these names suggest, and the
 * comments here said so backwards for as long as they existed. ADDR_OPT_DF is
 * 1 in the image and `-df` CLEARS it, so the packed-data-file arm is the
 * DEFAULT and the loose-file arm is what the switch selects. */
#define ADDR_SPRITE_REBUILD_DF   0x004243B0u  /* int32(spr, flags), the default */
#define ADDR_SPRITE_REBUILD_ALT  0x00445C00u  /* int32(spr, flags), under -df */
#define ADDR_STR_RESTORE_FAIL_S  0x004897ACu  /* "unable to restore sprite %s.\n" */
#define ADDR_STR_RESTORE_FAIL_X  0x0048978Cu  /* "unable to restore sprite %x.\n" */
#define ADDR_OVERLAY_PALETTE     0x004FE1A4u  /* void *, set before the overlay blit */
#define ADDR_DEFAULT_PALETTE     0x004FE084u  /* void *, used when the sprite has none */

/* Map repainting. World space is 1/256-tile fixed point; the camera origin is
 * scaled by 16 when converting to screen space. */
#define ADDR_SET_DRAW_TARGET     0x0041AC40u  /* void(LPDIRECTDRAWSURFACE) */
#define ADDR_REDRAW_MAP_REGION   0x0041CF90u  /* void(const AM2_Rect*) */
#define ADDR_BLIT_MAP_BACKDROP   0x0042D9B0u  /* void(AM2_Rect by value) */
/* A second offscreen surface, the same size again, and NOT the back buffer --
 * that is ADDR_BACK_BUFFER. This was ADDR_BACK_SURFACE for a long time, with a
 * comment here saying the name was wrong and had been kept anyway because it
 * was already spread across mapdraw.cpp. A comment saying a name is wrong is
 * not a correction, it is a note that the correction was declined; it is
 * renamed now. RedrawMapRegion locks [0x00503100], which is what settles it. */
#define ADDR_OFFSCREEN_SURFACE   0x00503100u  /* IDirectDrawSurface *, offscreen */
#define ADDR_MAP_DESC            0x00514F20u  /* map descriptor; +4 is a row count */
/* The descriptor's own two, and the row initialiser beside them.
 *
 * MapDescInit sizes the grid `cols << shift` entries where `shift` is
 * Log2Mask(cols) -- so the grid is SQUARE IN COLS and MAPDESC_OFF_ROWS is
 * only the early reject's bound. That is why RowRegisterAll clamps its bottom
 * edge to cols - 1 and not to rows - 1, which maprow.cpp already flags as
 * looking like a mistake. Sized exactly: the largest cell RowRegisterAll can
 * reach is ((cols-1) << shift) + cols - 1, one short of the allocation.
 *
 * RowInit's 0x60 bytes are AM2_OBJ_ROW_STRIDE, and the three fields it writes
 * afterwards are the sprite, the packed position and the palette -- which is
 * what says its argument is a map object rather than anything else that size. */
#define ADDR_MAP_DESC_INIT       0x0041D210u  /* void(desc, int32 w, int32 h) */
#define ADDR_MAP_DESC_FREE       0x0041D270u  /* void(desc) */
#define ADDR_ROW_INIT            0x0040A050u  /* void(row, sprite, x, y) */
/* 0x00434DA0, six callers. Build a ROW SET: allocate `count` rows in one
 * block, place each at a spec's offset from a base, size its entry buffer from
 * the spec, and copy a bounding rect in. Reconstructed.
 *
 * The set header is 0x20 bytes -- a zeroed dword, the count, the block, a
 * second zeroed dword, and the rect. A spec is four int32: x, y, w, h.
 * 11 calls on a driven Boot Camp mission. */
#define ADDR_BUILD_ROW_SET       0x00434DA0u  /* void(set,count,specs,dx,dy,rect) */
#define ROWSET_OFF_COUNT         0x04u
#define ROWSET_OFF_ROWS          0x08u
#define ROWSET_OFF_RECT          0x10u
#define AM2_ROW_SPEC_BYTES       0x10u
/* 0x00434C90, two callers -- BuildRowSet's sibling, one entry earlier, with
 * the same 0x20-byte header and a different source. Where that one takes a
 * count and an array of four-int32 specs, this takes a DEF record and reads
 * the count, the specs and the rect out of it; and its specs are TWELVE bytes
 * rather than sixteen. */
#define ADDR_BUILD_ROWS_FROM_DEF 0x00434C90u /* void(set,def,x,y,objFlags) */
#define ROWSET_OFF_DEF           0x00u  /* BuildRowSet zeroes this; this one
                                         * stores the def it was built from */
#define ROWSET_OFF_SUBREC        0x0Cu  /* def + 0x10, or NULL */
#define DEFROWS_OFF_COUNT        0x08u
#define DEFROWS_OFF_SPECS        0x0Cu
#define DEFROWS_OFF_SUBREC       0x10u
#define DEFROWS_OFF_HAS_SUBREC   0x1Cu
#define DEFROWS_OFF_RECT         0x20u
#define DEFSPEC_OFF_SPRITE       0x00u  /* AM2_Sprite *, to RowInit */
#define DEFSPEC_OFF_DX           0x04u  /* int16 */
#define DEFSPEC_OFF_DY           0x06u  /* int16 */
#define DEFSPEC_OFF_DEPTH        0x08u  /* int16 -> ROW_OFF_FIELD_26 */
#define AM2_DEFSPEC_BYTES        0x0Cu
/* The left and top of the visible-area rectangle -- these are its first two
 * fields, not two loose globals: RedrawMapRegion is called with 0x00514E14
 * itself as its AM2_Rect *, which the trace shows plainly, so the four edges
 * run 0x514E14..0x514E20.
 *
 * Distinct from the camera below, which is in tiles: BlitMapBackdrop subtracts
 * the camera from the SOURCE rectangle and these from the DESTINATION point,
 * and they are not the same offset. Also distinct from ADDR_ORIGIN_DX/DY,
 * which shift for a windowed primary. */
/* The SCREEN SHAKE, and it is the view rectangle that shakes: ScrollDecay
 * adds its X offset to left and right and its Y offset to top and bottom, so
 * the whole rectangle moves and nothing is scaled. The phases are floats and
 * the steps are integers, which is why the decay is done on the x87 stack.
 *
 * The timer is counted down by the per-frame delta beside the game clock, and
 * the amplitude fades linearly over the last 1024 ms and not before. */
#define ADDR_SHAKE_TIME          0x00514E64u  /* int32_t, ms remaining */
#define ADDR_SHAKE_PHASE_X       0x00514E68u  /* float */
#define ADDR_SHAKE_STEP_X        0x00514E6Cu  /* int32_t, sign flips at a limit */
#define ADDR_SHAKE_PHASE_Y       0x00514E70u  /* float */
#define ADDR_SHAKE_STEP_Y        0x00514E74u  /* int32_t */
#define ADDR_SHAKE_AMPLITUDE     0x00514E78u  /* int32_t, pixels */
/* 0x0042B2E0, one caller. Start a shake, taking the MAXIMUM of each field
 * against whatever is already running -- so a new shake can only strengthen
 * one in progress, never cut it short. Its one caller reads a preset out of
 * the four-record table below and pushes all four fields. */
#define ADDR_START_SHAKE         0x0042B2E0u  /* void(ms,stepX,stepY,amp) */
/* Four 16-byte presets, {ms, stepX, stepY, amplitude}: the first is all
 * zeroes and the other three are 250/25/12/2, 500/16/35/2 and 750/45/17/2.
 * The amplitude is 2 for every one that does anything, so the presets differ
 * in duration and in which axis dominates, and not in how far the screen
 * moves. */
#define ADDR_SHAKE_PRESETS       0x00486170u  /* int32_t[4][4] */
/* 0x0042B360, one caller, and the gate in front of ADDR_START_SHAKE: it turns
 * a blast's position and strength into one of the four presets, or into
 * nothing. The distance is measured from the CENTRE OF THE VIEW rectangle,
 * computed rather than stored, so the screen shakes by how far the blast is
 * from what the player is looking at.
 *
 * The falloff has a FLAT TOP: inside AM2_SHAKE_NEAR the multiplier is the 1.0
 * at 0x0046F2D8 and the strength passes through unscaled, and beyond it the
 * far branch throws that 1.0 away with an `fstp st(0)` and uses
 * (AM2_SHAKE_FAR - dist) / 512 instead, reaching zero exactly at the far
 * radius. Every value in that chain is a dyadic rational -- 1/512 is 2^-9 and
 * the operands are small integers -- so the x87 arithmetic is exact and the
 * _ftol truncation is reproducible without any 80-bit concern.
 *
 * THE PRESET INDEX IS CHECKED AT THE BOTTOM END ONLY. The strength is clamped
 * to AM2_SHAKE_STRENGTH_MAX and the table has four entries, so a strength
 * above 3 arriving inside the near radius indexes past the table and reads
 * whatever follows it. Reproduced; the clamp is what bounds how far past. */
#define ADDR_SHAKE_AT            0x0042B360u  /* void(const AM2_Point *, int32) */
#define AM2_SHAKE_STRENGTH_MAX   10
#define AM2_SHAKE_NEAR           0x140   /* full strength within this */
#define AM2_SHAKE_FAR            0x340   /* nothing at all beyond this */
#define AM2_SHAKE_FALLOFF        (1.0 / 512.0)  /* the double at 0x0046F9A8 */
#define AM2_SHAKE_PRESETS        4
#define SHAKE_PRESET_BYTES       0x10u
/* Not a shake constant, despite living in this block and being read by the
 * shake: it is the frame delta in SECONDS, the twin of ADDR_FRAME_DELTA_MS at
 * 0x00511E08. ADDR_FRAME_CLOCK_STEP writes it as delta_ms * 0.001, and
 * ADDR_TAKE_MENU_REQUEST forces it to 0x3D872B02 -- 0.066, which is the same
 * 66 ms the delta itself is clamped to -- in a network game. It was
 * ADDR_FRAME_DELTA_SEC, a name off the one call site that reads it; the shake
 * integrates its phase per second and so wants exactly this. */
#define ADDR_FRAME_DELTA_SEC     0x00511E10u  /* float, seconds */
#define ADDR_FRAME_DELTA_MS      0x00511E08u  /* int32_t, beside the clock */
#define AM2_SHAKE_FADE_MS        0x400        /* the linear fade window */
#define ADDR_VIEW_ORIGIN_X       0x00514E14u  /* int32_t */
/* 0x0042B5A0, one caller -- the per-frame path. Move the camera toward its
 * target, clamp both to the map, and derive every rectangle that follows. */
#define ADDR_VIEW_UPDATE         0x0042B5A0u  /* void(void) */
#define ADDR_VIEW_TARGET         0x00514E08u  /* AM2_Point, what the eye chases */
#define ADDR_VIEW_SNAP           0x00511E34u  /* int32_t, jump rather than glide */
#define ADDR_VIEW_HOLD           0x00511E38u  /* int32_t, skip the glide once */
#define ADDR_VIEW_SPEED          0x004852E0u  /* int32_t, units per second */
#define ADDR_VIEW_CLIPPED        0x00514E54u  /* AM2_Rect, the intersection */
#define ADDR_VIEW_ORIGIN_Y       0x00514E18u  /* int32_t */
/* The other two edges of the same rectangle. ADDR_VIEW_UPDATE writes all four
 * together and then pushes 0x00514E14 straight into IntersectRect as a RECT,
 * which is what settles that the four dwords are one rectangle in left, top,
 * right, bottom order rather than two points. ShakeAt takes the midpoint of
 * each pair. */
#define ADDR_VIEW_FAR_X          0x00514E1Cu  /* int32_t, the right edge */
#define ADDR_VIEW_FAR_Y          0x00514E20u  /* int32_t, the bottom edge */
/* Last frame's copies, written by ComposeFrame at the end of every frame and
 * read by the dirty-rectangle merge to find what has scrolled. The listener
 * point is saved the same way and in the same block. */
#define ADDR_LISTENER_POS_PREV   0x00514E10u  /* AM2_Point */
#define ADDR_VIEW_RECT_PREV      0x00514E24u  /* AM2_Rect */
#define ADDR_SECOND_RECT         0x00514E34u  /* AM2_Rect, saved alongside */
#define ADDR_SECOND_RECT_PREV    0x00514E44u  /* AM2_Rect */
/* Where the finished frame lands on the back buffer, and the region of the
 * offscreen surface it comes from -- the same four numbers used both ways. */
#define ADDR_BLIT_RECT           0x00485320u  /* AM2_Rect */
/* Set by three places that invalidate the whole view, cleared once the frame
 * that honoured it has been composed. */
#define ADDR_FULL_REDRAW         0x00512460u  /* int32_t */
#define ADDR_COMPOSE_FRAME       0x0042DA30u  /* void(void) */
/* ComposeFrame's callees. 0x0042B420 decays a scroll counter and 0x0041DCE0
 * clears three word counters; both stay original.
 *
 * 0x0042D6D0 went in as ADDR_DRAW_SCENE, guessed from where ComposeFrame calls
 * it. Reading the body says otherwise: it recentres the camera on the view,
 * clamps it to the map, scrolls the CACHE surface onto itself by the tile
 * delta and repaints the strips that exposed. Same shape as ScrollView one
 * level down, in tiles rather than pixels. Renamed, and that is the fourth
 * call-site name this project has had to correct. */
#define ADDR_SCROLL_DECAY        0x0042B420u  /* void(void) */
#define ADDR_SCROLL_MAP_CACHE    0x0042D6D0u  /* void(void) */
/* The view's size in tiles -- camera right and bottom are these plus the
 * camera origin, which is how ADDR_VISIBLE_TILES's last two fields are kept. */
#define ADDR_VIEW_TILES_W        0x00514EA0u  /* int32_t */
#define ADDR_VIEW_TILES_H        0x00514EA4u  /* int32_t */
/* The map's size in tiles; the camera is clamped to one less than each. */
#define ADDR_MAP_TILES_W         0x00514DDCu  /* int32_t */
#define ADDR_MAP_TILES_H         0x00514DE0u  /* int32_t */
/* The map's extent in PIXELS, which TileOfPoint bounds a point against before
 * shifting it down by four. ADDR_OBJECTS_IN_RECT's comment already named them
 * in prose. */
#define ADDR_MAP_EXTENT_X        0x00514DD0u  /* int32_t */
#define ADDR_MAP_EXTENT_Y        0x00514DD4u  /* int32_t */
/* 0x0042B290, 45 callers: a point to a tile index, or 0 for anything off the
 * map. Sixteen pixels to the tile, and the row stride is ADDR_MAP_TILES_W.
 *
 * This is one of the three readings that settle what 0x00514DDC is. It had two
 * names, ADDR_MAP_TILES_W and ADDR_MAP_HEIGHT, and CLAUDE.md recorded that
 * both pairs could not be right. Width wins, on three counts:
 *
 *   - TileOfPoint multiplies the ROW by it, so it is the column count;
 *   - PointOfTile masks the column with it and takes the row with
 *     ADDR_MAP_ROW_SHIFT, which only lines up if it is the width;
 *   - ADDR_MAP_ROW_SHIFT's own established name is log2 of the WIDTH.
 *
 * Against that, ScriptPad divides its index by 0x00514DE0 to recover x, which
 * would make THAT the width. It is one reading against three, and it is a
 * reading that cannot be caught: every shipped map seen so far is square --
 * Boot Camp's is 256 x 256 tiles, read out of the running game -- so dividing
 * by the height gives the right answer anyway. ADDR_MAP_HEIGHT and
 * ADDR_MAP_WIDTH are gone; the TILES_W/TILES_H pair is what is left. */
#define ADDR_TILE_OF_POINT       0x0042B290u  /* int32_t(AM2_Point) */
/* 0x0042E390, seven callers. The TILES A LINE CROSSES: a Bresenham walk from
 * one packed point to another, in tile space, writing each tile index into a
 * uint16 array and a count. Reconstructed.
 *
 * The step is +/- 1 across and +/- ADDR_MAP_TILES_W down, added to the tile
 * index directly rather than recomputed from coordinates -- so the walk never
 * calls TileOfPoint again after the first point, and an index that runs off
 * the row wraps into the next one rather than being clipped. Nothing bounds
 * the output array either. 35 calls on a driven Boot Camp mission -- measured
 * before ADDR_BEGIN_MOVE_TO was reconstructed; it is blind now. */
#define ADDR_TRACE_TILE_LINE     0x0042E390u  /* void(uint32,uint32,uint16*,int32*) */
#define AM2_TILE_SHIFT           4
/* 0x0042B250, six callers: the inverse, and it CENTRES -- the point it returns
 * is the middle of the tile, eight pixels in on each axis. The column comes
 * out with an AND against ADDR_MAP_TILES_W * 16 - 16, which is a modulo only
 * because the width is a power of two, and the row with a shift by
 * ADDR_MAP_ROW_SHIFT. */
#define ADDR_POINT_OF_TILE       0x0042B250u  /* uint32_t(int32_t tile) */
/* 0x0042DEB0, 30 callers. The 8-bit heading from one point to another, out of
 * the two reverse trig tables: the ratio of the smaller delta to the larger,
 * scaled by 512, indexes whichever table matches which delta was larger. 0 is
 * straight up and 0x80 straight down.
 *
 * Only AL carries the answer. The two table paths leave the division's
 * quotient in the upper 24 bits, and the dx == 0 path leaves a clean 0 or
 * 0x80 -- which is itself the argument that nothing may read above AL. */
#define ADDR_ANGLE_BETWEEN       0x0042DEB0u  /* uint8_t(const AM2_Point *,
                                               *         const AM2_Point *) */
/* Two more copies of the same arithmetic, which is how it was identified.
 * 0x0042DE50 takes the deltas already subtracted, and 0x0042DF20 answers the
 * distance AND the heading through two out-pointers -- the distance is
 * ApproxDist's formula exactly, so the original inlines both of them there. */
#define ADDR_ANGLE_OF_DELTA      0x0042DE50u  /* uint8_t(int32 dx, int32 dy) */
#define ADDR_DIST_AND_ANGLE      0x0042DF20u  /* void(a, b, int32 *, uint8 *) */
#define ADDR_MERGE_DIRTY         0x0041D060u  /* void(void) */
/* The rectangle the view is clipped against before anything is compared --
 * the map's extent on screen. */
/* THREE RECTS, AND TWO OF THEM ARE NAMED AT THE FIELD LEVEL. LoadMap's MHDR
 * arm builds all three with RectSet and copies each one's four dwords out, so
 * their extents are visible together for the first time:
 *
 *   0x00514DE8  ADDR_MAP_BOUNDS        (0, 0, extentX, extentY)
 *   0x00514DF8  ADDR_MAP_BOUNDS_LEFT.. (0x40, 0x40, extentX-0x40, extentY-0x40)
 *   0x00514EA8  ADDR_VISIBLE_TILES     (1, 1, viewTilesW+1, viewTilesH+1)
 *
 * ADDR_CAMERA_Y IS NOT A CAMERA. It is 0x00514EAC, which is ADDR_VISIBLE_TILES
 * + 4 -- the TOP of that rect, written by the same RectSet as the other three
 * fields. The name predates knowing the global is a rectangle, and reading it
 * as a camera position is wrong in a way nothing would catch: it holds a
 * plausible small number either way.
 *
 * ADDR_MAP_BOUNDS_LEFT/TOP/RIGHT/BOTTOM are one rect under four names, which
 * is the same thing done deliberately and is merely verbose. Neither is
 * renamed here -- that is a change to every use at once and gets its own
 * commit, which is now the third time this file has said so. */
#define ADDR_MAP_BOUNDS          0x00514DE8u  /* AM2_Rect */
/* Walks the registered dirty rectangles at 0x00508AC4 and repaints the ones
 * meeting the given region. Its only import is IntersectRect. */
#define ADDR_REPAINT_DIRTY_LIST  0x0041D000u  /* void(const AM2_Rect *) */
/* The map's OBJECT painter: collect, sort, draw.
 *
 * 0x0041E440 walks a grid of cells over the world, hands every object it
 * finds to the depth sort, and then draws the sorted list. The grid is
 * described by {cols, rows, shift, cells} at ADDR_MAP_DESC and measured as
 * {16, 16, 4, ...} on both Boot Camp and the campaign's first map -- a fixed
 * spatial hash, not the map's tile extent, with the shift exactly log2(cols).
 *
 * World coordinates are shifted right by 8 to index it, so each cell is 256
 * units square.
 *
 * The sort is 0x0041E160, into at most 500 twelve-byte nodes at 0x00507350.
 * Its head index lives in 0x0050B1D8 and its count in 0x0050B1D4, and the
 * walker clears both on entry. */
/* Was ADDR_DRAW_MAP_TILES, which is what its one call site sits next to and
 * not what it does: the tile painter is 0x0042D580 and this draws the objects
 * ON the tiles. Renamed, not aliased. */
#define ADDR_DRAW_MAP_OBJECTS    0x0041E440u  /* void(const AM2_Rect *, desc, int32) */
#define ADDR_DEPTH_INSERT        0x0041E160u  /* int32_t(void *obj, const AM2_Rect *) */
#define ADDR_DRAW_MAP_OBJECT     0x0040A090u  /* void(void *obj, const AM2_Rect *) */
#define ADDR_DEPTH_COUNT         0x0050B1D4u  /* int32_t, 0..500 */
#define ADDR_DEPTH_HEAD          0x0050B1D8u  /* int32_t, -1 when empty */
#define ADDR_DEPTH_FIELD_DC      0x0050B1DCu  /* int32_t, cleared with them */
#define ADDR_DEPTH_NODES         0x00507350u  /* {obj, ?, next}[500] */
#define AM2_DEPTH_NODE_SIZE      12u
#define DEPTH_OFF_OBJ            0x00u
#define DEPTH_OFF_PREV           0x04u
#define DEPTH_OFF_NEXT           0x08u
#define ADDR_DEPTH_CURSOR        0x00507348u  /* the last node inserted */
#define ADDR_DEPTH_COMPARE       0x0041D740u  /* int32_t(void *a, void *b) */
/* 0x0041DB90, one caller -- ADDR_ROW_UPDATE. Move one node back into depth
 * order after its object's depth has changed, by walking outward in whichever
 * direction the comparison says and re-linking it there. */
#define ADDR_DEPTH_RESORT        0x0041DB90u  /* void(node *, node **head) */
/* 0x0041D8F0, two callers. The list PRIMITIVE under ADDR_DEPTH_INSERT: put a
 * node that is not in the list into its sorted place. Distinct from
 * ADDR_DEPTH_INSERT (0x0041E160), which takes an object and a world rectangle
 * and is the layer above -- the names are close because the two really are one
 * operation split in two, and the address had to be grepped to notice. */
#define ADDR_DEPTH_LINK          0x0041D8F0u  /* void(node *, node **head) */
/* 0x0041D980, one caller. The counterpart of ADDR_ROW_UNREGISTER_ALL: put
 * every cell the row's CURRENT rectangle covers into the grid, from entries
 * assumed not to be linked anywhere. Shares its cell arithmetic, and its
 * COLS-for-ROWS clamp, with ADDR_ROW_UPDATE. */
#define ADDR_ROW_REGISTER_ALL    0x0041D980u  /* void(row *, desc *) */
/* 0x0041D2B0, six callers. Give a row its entry buffer, sized from a width and
 * a height in world units, fill the entries in, work out the row's rectangle
 * from its sprite and register it. Returns the buffer's size in bytes. */
#define ADDR_ROW_ALLOC           0x0041D2B0u  /* int32_t(w, h, row *, desc *) */
/* What the comparator reads. The bounds at OBJ_OFF_BOUNDS end at +0x1B and
 * these follow: a screen position, a LAYER that only counts when both objects
 * have a positive one, and a per-object SLOPE that projects a horizontal
 * distance onto the vertical axis -- which is how two objects at different x
 * are ordered by which is in front rather than by y alone. */
/* The screen position is ROW_OFF_X and ROW_OFF_Y, further down. It had a
 * second pair of names here -- OBJ_OFF_SCREEN_X/Y, same offsets, same struct,
 * same meaning -- and the offset ratchet could not see the duplication
 * because the two prefixes differ. Retired in favour of the ROW_ pair, which
 * is what the rest of the tree calls them, and which frees OBJ_OFF_ 0x1C for
 * the GAME object's own field at that offset. Two structs overlap here and
 * only one of them is a map object. */
/* The object's hit box as four OFFSETS from its own position -- left, top,
 * right, bottom. OBJ_OFF_HIT_RECT at 0x30 is the same box with the position
 * added, and ItemSetBox is what writes both from one pair of corners. The
 * sixteen bytes an item create message carries are exactly these; see
 * MSG_CREATE_OFF_BLOCK. Named from the writer, the old OBJ_OFF_CREATE_BLOCK
 * having been named from a memcpy. */
#define OBJ_OFF_BOX_OFFSETS      0x20u
#define AM2_OBJ_BOX_BYTES        0x10u
#define OBJ_OFF_DEPTH_LAYER      0x26u  /* int16_t, only when > 0 on both */
#define OBJ_OFF_DEPTH_SLOPE      0x28u  /* float */
/* The rest of a map object, as the drawer reads it. The lut and the palette
 * are the object's own and are written INTO the shared sprite immediately
 * before it is drawn, so a sprite used by several objects carries whichever
 * one drew last. */
#define MAPOBJ_OFF_FLAGS         0x00u  /* bit 0 clear means do not draw */
#define MAPOBJ_OFF_SPRITE        0x04u  /* AM2_Sprite * */
#define MAPOBJ_OFF_LUT           0x2Cu  /* -> the sprite's +0x34 */
#define MAPOBJ_OFF_PALETTE       0x30u  /* -> the sprite's +0x38 */
#define MAPOBJ_FLAG_VISIBLE      0x01u
#define ADDR_FLOAT_ZERO          0x0046F928u  /* 0.0f */
#define AM2_DEPTH_MAX            0x1F4        /* 500 nodes */
/* Two object flags that override the comparison entirely: everything with
 * 0x40 sorts before everything with 0x20, whatever the comparator says.
 * The rule is read from both sides -- a 0x20 object is inserted AFTER the
 * first 0x40 it finds, and a 0x40 object BEFORE the first 0x20 -- and with
 * neither present both fall through to the ordinary compare. */
#define OBJ_DEPTH_FLAG_BACK      0x40u
#define OBJ_DEPTH_FLAG_FRONT     0x20u
#define MAPDESC_OFF_COLS         0x00u
#define MAPDESC_OFF_ROWS         0x04u
#define MAPDESC_OFF_SHIFT        0x08u
#define MAPDESC_OFF_CELLS        0x0Cu
#define CELL_NODE_OFF_OBJ        0x00u
#define CELL_NODE_OFF_NEXT       0x08u
#define AM2_CELL_SHIFT           8    /* world units per cell, as a shift */
#define AM2_SUBDIVIDE_FLOOR      0x20 /* below this a split stops recursing */
#define OBJ_OFF_BOUNDS           0x0Cu /* AM2_Rect; +0x0C left, +0x10 top */
/* THE SAME OFFSET IN A GAME OBJECT is its NAME-TABLE INDEX, and it is spelled
 * without the _OFF_ family so the alias ratchet is not asked to accept two
 * OBJ_OFF_ names on 0x0C -- the rectangle above belongs to a different
 * structure.
 *
 * Three readers agree. item.cpp raises every object event with it as
 * EventNotify's `num1`; SetObjScriptState indexes kScriptNames with it to say
 * which object has no script; SendItemCreate indexes the same table to put
 * the object's NAME into the message. It carried the first of those as its
 * name -- AM2_OBJ_EVENT_NUM_OFF -- until the other two turned up. */
#define AM2_OBJ_NAME_IDX_OFF     0x0Cu
/* The dirty list is 20-byte records: a RECT, then the index of the PREVIOUS
 * record and the index of the next. Record ZERO is the sentinel, so both of
 * its links are addressed as globals -- base + 0x10 and base + 0x12.
 *
 * NONE OF THE THREE IS A COUNTER, and this comment said two of them were.
 * 0x00508AD6 fell first: it is records[0].next, the list head, which the walk
 * in ADDR_REPAINT_DIRTY_LIST makes plain and a reset-them-together sweep could
 * not. 0x00508AD4 is the same trick one field earlier -- records[0].prev --
 * and the unlink inside 0x0041DF00 is what settles it, writing
 * `records[rec.next].prev = rec.prev` through exactly that address.
 *
 * That leaves 0x00508AC0, which really is a global and really is not a count:
 * the same unlink assigns it `rec.prev` when it removes the LAST record, so
 * it is the TAIL index. It doubles as the allocator -- AddDirtyRect takes
 * tail + 1 -- which is why it looked like a count, and why a record freed
 * from the end is handed straight back out.
 *
 * All three renamed rather than aliased. */
#define AM2_DIRTY_RECORD_SIZE    20u
#define DIRTY_OFF_PREV           0x10u
#define DIRTY_OFF_NEXT           0x12u
#define ADDR_DIRTY_RECTS         0x00508AC4u  /* the records */
#define ADDR_DIRTY_TAIL          0x00508AC0u  /* uint16_t, and the allocator */
#define ADDR_DIRTY_PREV_HEAD     0x00508AD4u  /* uint16_t, records[0].prev */
#define ADDR_DIRTY_HEAD          0x00508AD6u  /* uint16_t, records[0].next */
#define ADDR_RESET_DIRTY_LIST    0x0041DCE0u  /* void(void) */
/* 0x0041DD00, seven callers. Append one rectangle. Overflow at
 * AM2_DEPTH_MAX sets ADDR_FULL_REDRAW instead of growing anything, so the
 * frame repaints whole rather than losing a rectangle. */
#define ADDR_ADD_DIRTY_RECT      0x0041DD00u  /* void(l, t, r, b) */
#define ADDR_CAMERA_X            0x00514EA8u  /* int32_t */
#define ADDR_CAMERA_Y            0x00514EACu  /* int32_t */

/* Display palette calibration. Paints a ramp of every palette index onto the
 * primary surface and reads it back with GDI to learn what actually displays.
 * The colour matcher stays original -- it is pure arithmetic. */
#define ADDR_CALIBRATE_PALETTE   0x0041AFC0u  /* void(uint32_t *palette[512]) */
/* Scan the palette from `from` for the entry closest to a colour, by the
 * metric at ADDR_COLOUR_DISTANCE. Went in twice, as ADDR_NEAREST_PAL_INDEX too. */
#define ADDR_NEAREST_PAL_INDEX   0x0041B7C0u  /* uint8_t(const uint32_t*,uint32_t,uint32_t) */
/* 0x0041B820, 25 callers: the same thing with the three channels apart. It
 * packs them into its own FIRST ARGUMENT SLOT and passes the dword, so the top
 * byte is whatever the red argument's byte 3 was -- stale, and it does not
 * matter, since the matcher masks. */
#define ADDR_NEAREST_PAL_RGB     0x0041B820u  /* uint8_t(pal, r, g, b, from) */
#define ADDR_COLOUR_DISTANCE     0x0041B760u  /* int32_t(const uint32_t *a,
                                               * const uint32_t *b) */
#define AM2_COLOUR_DIST_MAX      0x2FFFD      /* the sentinel it starts from */

/* The GDI half of the palette. The game is 8-bit, so what it can actually show
 * is negotiated with Windows rather than chosen. */
#define ADDR_REALIZE_PALETTE     0x0041AF00u  /* void(const uint32_t *palette) */
/* SetGamePalette -- creates the DirectDraw palette and builds every remap
 * table the software blitters use. 0x0041B132 is the image's ONLY
 * CreatePalette, so until this was reconstructed the display palette was the
 * one DirectX object the port did not create. */
#define ADDR_SET_GAME_PALETTE    0x0041B0E0u  /* void(uint8_t *palette) */
/* Rebuilds the remap tables of all three sprite sets against the palette that
 * has just been loaded. The name is from the call site; the body is a sweep
 * over the sets, which is what the comment now says. */
#define ADDR_PALETTE_LOADED      0x00423C50u  /* void(void), run afterwards */
/* The fixed 256-entry table handed to CreatePalette when windowed, where the
 * desktop owns the real palette and the game may not set it. */
#define ADDR_GDI_PALETTE         0x00477E6Cu  /* PALETTEENTRY[256] */
/* Where the palette is copied wholesale once it is installed: 0x201 dwords,
 * which is the 256 entries plus the DirectDraw palette pointer after them. */
#define ADDR_PALETTE_COPY        0x005022C8u
/* Remap tables, one byte per palette index, all rebuilt by SetGamePalette. */
#define ADDR_REMAP_IDENTITY      0x004FD764u  /* uint8_t *, index -> itself */
#define ADDR_REMAP_DARK          0x004FE084u  /* uint8_t *, 70% brightness */
#define ADDR_REMAP_BRIGHT        0x00507230u  /* uint8_t *, +0x80 or 70% */
#define ADDR_REMAP_TINT          0x0047826Cu  /* uint8_t *, two colours by parity */
/* Four more, at 40/50/60/85% brightness, whose 256-byte blocks sit back to
 * back from 0x00502CEC. */
#define ADDR_REMAP_SHADES        0x004FE2B0u  /* uint8_t *[4] */
#define ADDR_REMAP_SHADE_STORE   0x00502CECu
/* Palette indices of the colours the engine asks for by name. */
#define ADDR_COLOUR_TABLE_BASE   0x004FE084u
#define ADDR_SNAPSHOT_PALETTE    0x00445170u  /* void(void) */
/* A LOGPALETTE living in .data with palVersion and palNumEntries already set
 * to 0x300 and 256; only the 256 entries after them are ever written. */
#define ADDR_LOGPALETTE          0x00477A60u
#define ADDR_LOGPALETTE_ENTRIES  0x00477A64u  /* PALETTEENTRY[256] */
#define ADDR_SYSTEM_PALETTE      0x006564A0u  /* PALETTEENTRY[256], read back from GDI */

/* ---- DirectPlay -------------------------------------------------------
 *
 * The last outward channel, and the least visible one. The game imports no
 * networking library at all -- no ws2_32, no wsock32, no dplayx, and not even
 * the strings -- because its multiplayer transport is DirectPlay obtained
 * through COM. These two CoCreateInstance sites are the whole of it.
 *
 * Reconstructed in src/game/win32/dplay.cpp. The GUIDs are the game's own copies.
 */
#define ADDR_COMM_CREATE_DPLAY   0x0040DD20u  /* thiscall int32(this, void *conn) */
/* Comm teardown: destroy the four mutex-guarded message lists, wake the packet
 * thread, wait for it and close the handles. */
#define ADDR_COMM_SHUTDOWN       0x004020A0u  /* void(void) */
/* Two of the four are named now, and reading FlushDelayedSends is what did it.
 * 0x0048D8E8 was ADDR_MSG_LIST_A as well as ADDR_MSG_LIST_POOL -- one address
 * under two names, with the second one right; the letter is retired rather
 * than kept beside it. And 0x004F8780 is the DELAYED SEND QUEUE: MsgListInsert
 * puts a node into it in ascending MSGNODE_OFF_KEY order and FlushDelayedSends
 * drains it while GetTickCount has reached that key, so for this list the key
 * is a millisecond deadline.
 *
 * THREE OF THE FOUR NOW. 0x0048D8D8 is the SEND QUEUE, and the program says
 * so in its own words: DestroyFlow logs "sendqueue Size = %d" of it and
 * "Adding to freelist from sendque Buffer seq %d". What settles the letter is
 * having both ends -- SendGameMsg puts a kind-0x0B packet in, keyed on its
 * sequence and flagged with the recipient's bit, and ProcessResendQueue takes
 * it out again for whoever has not acknowledged. B stays a letter. */
#define ADDR_MSG_LIST_B          0x004F48C8u
#define ADDR_MSG_LIST_SENDQ      0x0048D8D8u
/* The one packet ProcessResendQueue stages a resend in: MsgListTakeFlags
 * copies a message body here, the checksum is recomputed over it in place,
 * and CommSend is handed this address. A single shared buffer, so nothing
 * may hold a pointer into it across calls. */
#define ADDR_RESEND_BUF          0x004FAE68u
#define ADDR_MSG_LIST_DELAYED    0x004F8780u
#define ADDR_COMM_EVENT          0x0048D8F8u  /* HANDLE, signalled to stop the thread */
#define ADDR_COMM_EVENT_2        0x0048D8FCu  /* HANDLE */
#define ADDR_PACKET_THREAD       0x004F48D8u  /* HANDLE */
#define ADDR_CREATE_LOBBY        0x0040DDD0u  /* int32 __stdcall(LPDIRECTPLAYLOBBY3A *) */
#define ADDR_CLSID_DIRECTPLAY    0x0046F6D8u  /* CLSID_DirectPlay */
#define ADDR_IID_DIRECTPLAY4A    0x0046F6C8u  /* IID_IDirectPlay4A */
#define ADDR_CLSID_DPLAY_LOBBY   0x0046F778u  /* CLSID_DirectPlayLobby */
#define ADDR_IID_DPLAY_LOBBY3A   0x0046F768u  /* IID_IDirectPlayLobby3A */
/* Both thiscall on the comm object, both taking nothing but `this`. */
#define ADDR_COMM_DROP_DPLAY     0x0040EA40u  /* thiscall int32(this) */
/* The DirectPlay lobby launch, reached when another application starts the
 * game through DirectPlay rather than the user starting it. */
#define ADDR_COMM_LOBBY_START    0x0040ED10u  /* thiscall int32(this) */
#define ADDR_READ_MP_MAPS        0x0043ECC0u  /* void(void) -- ReadMpMapList */
#define ADDR_COMM_CREATE_PLAYER  0x0040DE10u  /* thiscall int32(this,name,evt,data,len) */
/* Reconstructed. It answers 1 when the id already had a record or has just
 * been given one, and 0 when all six slots are taken -- the prototype here
 * said `void` and every caller ignores the result, so nothing observed the
 * difference. */
#define ADDR_COMM_REGISTER_SELF  0x004027F0u  /* int32_t(uint32_t id) */
#define ADDR_DEFAULT_PLAYER_EVT  0x004F48C0u  /* HANDLE, used when none is given */
#define COMM_OFF_PLAYER_MADE     0x3E4u
#define COMM_OFF_JOINED          0x3DCu
#define COMM_SLOT_OFF_TAKEN      0x050u   /* the field StartSelectedGame sets */
#define ADDR_COMM_MARK_LOBBIED   0x0040F130u  /* void(void); sets comm+0x404 */
/* 0x0040F140, three callers, the counterpart: clear the same field and tail-
 * jump to CommDropDirectPlay. THISCALL -- every caller loads ecx with the comm
 * object first -- and the store still goes through the global rather than
 * through `this`, which is reproduced. */
#define ADDR_COMM_DROP_SESSION   0x0040F140u  /* thiscall int32(this) */
/* 0x00410F70, two callers, neither of which reads a result -- so `void` is
 * right even though the body ends in a TAIL JUMP to CommEnumPlayers, which
 * does return one. Fetch the session description, log three of its fields when
 * COMM_OFF_VERBOSE is set, publish the current player count, and enumerate.
 *
 * The three fields are DPSESSIONDESC2's dwMaxPlayers, dwCurrentPlayers and
 * lpszSessionNameA at +0x28, +0x2C and +0x30 -- which is what confirms
 * COMM_OFF_SESSION_DESC really is that structure and not something shaped like
 * it. Three offsets agreeing with the SDK is better than one. */
#define ADDR_ON_LOBBY_SLAVE      0x00410F70u  /* void(void) */
#define ADDR_FMT_SESSION_MAX     0x00475DACu  /* "Session Max Players: %d\n" */
#define ADDR_FMT_SESSION_CUR     0x00475D90u  /* "Session Cur Players: %d\n" */
#define ADDR_FMT_SESSION_NAME    0x00475D7Cu  /* "Session Name: %s\n" */
#define COMM_OFF_LOBBY_BUF       0x3F0u   /* DPLCONNECTION, 0x800 bytes */
#define COMM_OFF_IS_HOST         0x3D8u   /* from DPCAPS_ISHOST */
#define COMM_OFF_SESSION_DESC    0x3E8u   /* the fetched DPSESSIONDESC2 */
#define COMM_OFF_LOBBIED         0x3F8u
#define COMM_OFF_LOBBY_STARTING  0x3FCu
#define LOBBY_CONN_BUF_SIZE      0x800u
#define ADDR_STR_LOBBY_START     0x004756A4u
#define ADDR_STR_LOBBY_NOMEM     0x0047566Cu
#define ADDR_STR_LOBBY_GCS_FAIL  0x0047563Cu
#define ADDR_STR_LOBBY_E_SMALL   0x00475624u
#define ADDR_STR_LOBBY_E_IFACE   0x00475608u
#define ADDR_STR_LOBBY_E_OBJECT  0x004755F0u
#define ADDR_STR_LOBBY_E_PARAMS  0x004755D8u
#define ADDR_STR_LOBBY_E_MEMORY  0x004755C4u
#define ADDR_STR_LOBBY_CONNECT   0x00475548u
#define ADDR_STR_LOBBY_CONNRET   0x0047552Cu
#define ADDR_STR_LOBBY_AS_HOST   0x00475504u
#define ADDR_STR_LOBBY_AS_SLAVE  0x004754DCu
#define COMM_OFF_LOBBY           0x3F4u   /* IDirectPlayLobby3A; the store at
                                           * 0x0040ED3C names it */
#define COMM_OFF_SEND_BUF        0x3E8u   /* game heap */
#define COMM_OFF_RECV_BUF        0x3F0u   /* game heap */
/* IT NAMES ITSELF TWICE -- "DestroyFlow: Flow queue for Player %x not found"
 * and "...for me (%x) not found" -- where ADDR_REMOVE_PLAYER was a name off a
 * call site. It reclaims a departing player's queued messages by SIMULATING
 * ACKS for them, and can UNPAUSE the game if that refills the pool.
 * Reconstructed. */
#define ADDR_DESTROY_FLOW        0x004029B0u  /* int32(uint32 id), 7 callers */
/* The pause mask and its pair of accessors, and all three name themselves:
 * 0x004267C0 logs "PauseGame: %x (set: %x)" and 0x00426800 logs
 * "UnPauseGame: %x (reset: %x)". They went in as event flags, which is what
 * they look like where the frame chain tests them -- but the tests read "is
 * the game paused", and each bit is a reason it is. */
#define ADDR_PAUSE_GAME          0x004267C0u  /* void(uint32 bits); ORs in */
#define ADDR_UNPAUSE_GAME        0x00426800u  /* void(uint32 bits); ANDs out */
#define ADDR_STR_PAUSE_GAME      0x00485250u  /* "PauseGame: %x (set: %x)\n" */
#define ADDR_STR_UNPAUSE_GAME    0x0048526Cu  /* "UnPauseGame: %x (reset: %x)\n" */
#define COMM_DROP_EVENT_MASK     0x1E78F0u    /* what the teardown clears */
#define ADDR_STR_RELEASING_COMM  0x00475434u  /* "Releasing Comm Connection \n" */
/* The once-only latch on the NO BUFFERS report. CommNoBuffers logs and posts
 * WM_CLOSE the FIRST time buffers run out and does nothing on every call after,
 * so this is what stops a failing session spraying the log while it comes
 * down. Cleared with the connection, which is what re-arms it.
 *
 * It was ADDR_COMM_UNKNOWN_4F48E0 until CommNoBuffers was read. Renamed rather
 * than aliased, so nothing goes on carrying the old spelling. */
#define ADDR_COMM_NO_BUFFERS_LATCH 0x004F48E0u  /* int32_t */
#define ADDR_STR_NO_BUFFERS      0x00473D34u  /* "COMM ERROR: NO BUFFERS\n" */
#define ADDR_COMM_CONNECTED      0x0040E660u  /* thiscall int32(this) */
#define COMM_OFF_CAPS            0x42Cu   /* DPCAPS, filled by GetCaps */
#define COMM_OFF_BUFFER_MAX      0x410u   /* set to 0x400 by CommConstruct */
#define COMM_OFF_BUFFER_DEFAULT  0x428u   /* set to 0x3E4 by CommConstruct */
/* ArmyMessageFlush's rate limit, and CommConstruct writes both: 100 and 1000
 * milliseconds. The packet goes out when it is big enough AND +0x420 has
 * passed, or when +0x424 has passed regardless, or when the payload exceeds
 * COMM_OFF_BUFFER_DEFAULT -- 996 of the 1024-byte COMM_OFF_BUFFER_MAX, so 28
 * bytes of headroom. That third arm is what orig.h means where it says
 * ArmyMessageSend calls the flush "whenever the packet fills". */
#define COMM_OFF_SEND_STAMP      0x408u   /* GetTickCount at the last send */
#define COMM_OFF_COALESCE_MS     0x420u   /* 100 */
#define COMM_OFF_MAX_HOLD_MS     0x424u   /* 1000 */
/* The player slots ArmyMessageFlush walks, COMM_OFF_PLAYER_COUNT of them at a
 * stride of 0x70. Each begins with the player id; -1 marks an empty slot and
 * COMM_OFF_OUR_PLAYER_ID is skipped, so a flush sends to everyone else. */
/* The rest of one comm player slot, named from SendPlayerMsg, which copies
 * each of them into a message record. The slot stride is AM2_COMM_SLOT_STRIDE
 * and COMM_OFF_PLAYER_SLOTS below is the id; these are its neighbours. Only
 * the name is evidenced -- it is logged with %s -- so the others keep
 * field-numbered names. */
#define COMM_OFF_SLOT_FIELD_210  0x210u  /* logged as the third %d */
#define COMM_OFF_PLAYER_SLOTS    0x214u
#define COMM_OFF_SLOT_NAME       0x218u  /* char[], logged with %s */
#define COMM_OFF_SLOT_FIELD_258  0x258u
#define COMM_OFF_SLOT_FIELD_25C  0x25Cu
#define COMM_OFF_SLOT_FIELD_270  0x270u
/* Stamped from GetTickCount as a player is admitted, and read nowhere that has
 * been looked at. Named for what puts it there. */
#define COMM_OFF_SLOT_JOINED_MS  0x26Cu  /* uint32_t */
#define COMM_OFF_SLOT_FIELD_278  0x278u  /* cleared for every slot but ours */
#define AM2_COMM_SLOT_STRIDE     0x70u
#define ADDR_STR_CAPS_HEAD       0x00475400u
#define ADDR_STR_CAPS_PACKET     0x004753E8u
#define ADDR_STR_CAPS_HEADER     0x004753D0u
#define ADDR_STR_CAPS_LATENCY    0x004753B8u
#define ADDR_STR_CAPS_TIMEOUT    0x004753A0u
#define ADDR_STR_CAPS_GUAR_YES   0x0047537Cu
#define ADDR_STR_CAPS_GUAR_NO    0x00475354u
#define ADDR_STR_CAPS_BUFFERS    0x0047531Cu
#define COMM_OFF_DPLAY           0x3ECu       /* IDirectPlay4A * inside the comm object */
/* Thin wrappers over the IDirectPlay4A the comm object holds. All three answer
 * 1 for success, and all three do nothing at all when there is no session. */
/* The comm object itself: a single global built by a C++ constructor that the
 * CRT runs before main, with the matching destructor handed to atexit. Both
 * are thiscall on the object at ADDR_COMM_OBJECT, and between them they hold
 * the game's ENTIRE registry surface -- one RegCreateKeyExA and one
 * RegCloseKey, and there is no third registry call anywhere in the image. */
#define ADDR_COMM_GLOBAL         0x004FA480u  /* the object itself; ADDR_COMM_OBJECT points at it */
/* The static-initialiser group in front of them. This image has exactly TWO
 * static C++ globals, and both are built by the same four-stub shape the MSVC
 * front end emits -- found by scanning for `push imm32; call ADDR_CRT_ATEXIT`,
 * which returns these two and nothing else, rather than by guessing:
 *
 *   entry     call <ctor thunk> ; jmp <registrar>     -- in the CRT init table
 *   ctor      mov ecx, <object> ; jmp <constructor>
 *   registrar push <dtor thunk> ; call atexit
 *   dtor      mov ecx, <object> ; jmp <destructor>
 *
 * The entry is reached only as a dword in the initialiser table, never by a
 * call, so `refs_to` finds it and `xrefs` does not -- the one shape this
 * project has had to learn twice.
 *
 * ALL OF IT EXECUTES, which is the opposite of what this comment said when it
 * was written. The reasoning was that the CRT runs the init table before
 * am2hook.dll can patch anything, so the constructor half would be past by the
 * time our code existed. A TRACE=1 run says otherwise, and says it in two
 * adjacent lines: "am2hook: 1311 patch(es) installed" is immediately followed
 * by "trace CommGlobalInit#1". The harness is in before the initterm, so both
 * entries run under our patches -- CommGlobalInit and SelListInit read 1 each.
 *
 * The four thunks under them read 0 for the ordinary reason: each entry calls
 * its own thunk and registrar by name, so no patched entry is crossed. The
 * destructors are blind for a second reason on top -- our registrar hands
 * atexit OUR thunk rather than the image address, so the CRT calls it directly
 * at exit. That the exit path really does reach reconstructed code is visible
 * anyway: after ReportLeaks the log's next format string resolves inside the
 * DLL rather than the image.
 *
 * Recorded at length because the wrong version was plausible, was written
 * down as fact, and took one probe to refute. Ask whether the new code runs
 * before reasoning about whether it can. */
#define ADDR_COMM_GLOBAL_INIT    0x0040DB40u  /* cdecl, from the init table */
#define ADDR_COMM_GLOBAL_CTOR    0x0040DB50u  /* cdecl void *(void) */
#define ADDR_COMM_GLOBAL_ATEXIT  0x0040DB60u  /* cdecl int32_t(void) */
#define ADDR_COMM_GLOBAL_DTOR    0x0040DB70u  /* cdecl void(void) */
#define ADDR_COMM_CONSTRUCT      0x0040DB80u  /* thiscall void *(this) */
#define ADDR_COMM_DESTRUCT       0x0040DCC0u  /* thiscall void(this) */
/* Called by the constructor, all three left original. */
/* Brings the packet subsystem up: four message lists, 400 buffers, two events
 * and the packet thread. It went in as "mirrors CommShutdown", guessed from the
 * call site; its own error string says "Error launching packet thread". */
#define ADDR_START_PACKET_THREAD 0x004021A0u  /* int32_t(void) */
/* Two 120-entry state arrays on the comm object, at +0x420 and +0x600, six
 * setters differing only in array and value. See src/game/msgslot.h -- nothing
 * in the image READS either array. */
#define ADDR_MSGSLOT_A1          0x004032C0u  /* void(comm, seq) */
#define ADDR_MSGSLOT_A0          0x004032F0u
#define ADDR_MSGSLOT_A2          0x00403320u
#define ADDR_MSGSLOT_B1          0x00403350u
#define ADDR_MSGSLOT_B0          0x00403380u
#define ADDR_MSGSLOT_B2          0x004033B0u
#define ADDR_MSG_FIELD_12        0x00401040u  /* uint32_t(const void *msg) */
/* Comm object bookkeeping, all on the same record. The 32-dword ring at +0x3A0
 * is written by one and averaged by the other. */
#define ADDR_RING_PUSH_32      0x00402E50u  /* void(comm, uint32_t sample) */
#define ADDR_COMM_REMOVE_KEYED 0x00402DB0u  /* void(comm, uint32_t key) */
#define ADDR_COMM_MEAN_32        0x00402E90u  /* int32_t(const void *comm) */
/* Bit 0 and bit 1 of the word at an object's +0. The tests return the masked
 * value, 1 or 2, not a boolean -- see src/game/objflag.h. */
#define ADDR_OBJ_FLAG_SET0       0x0040A010u  /* void(void *obj) */
#define ADDR_OBJ_FLAG_CLEAR0     0x0040A020u
#define ADDR_OBJ_FLAG_BIT0       0x0040A030u  /* uint32_t(const void *obj) */
#define ADDR_OBJ_FLAG_BIT1       0x0040A040u
#define ADDR_MSG_LIST_INIT       0x00401000u  /* int32_t(void *list) */
/* Its own log string calls it AddMsg. The role name here says the same thing
 * and is already used by dplay.cpp, so it stays -- see the AddMsg note further
 * down for the list layout. */
#define ADDR_MSG_LIST_ADD        0x00401050u  /* void(void *list, void *node) */
#define ADDR_EVENT_CLOSE         0x00402170u  /* void(void *holder) */
/* The four lists, in the order they are created. */
#define ADDR_MSG_LIST_POOL       0x0048D8E8u  /* the free-buffer pool */
/* 400 records of 0x28 bytes, each pointing at 0x400 bytes of buffer. */
#define ADDR_PACKET_RECORDS      0x004F48F8u
#define ADDR_PACKET_BUFFERS      0x0048D978u
#define ADDR_PACKET_BUFFERS_END  0x004F1978u
#define PACKET_RECORD_STRIDE     0x28u
#define PACKET_REC_OFF_SIZE      0x10u
#define PACKET_REC_OFF_DATA      0x20u
#define PACKET_BUFFER_BYTES      0x400u
/* Two auto-reset events, the second kept in two places. */
#define ADDR_PACKET_EVENT_A      0x0048D8F8u
#define ADDR_PACKET_EVENT_B      0x0048D8FCu
#define ADDR_PACKET_EVENT_B2     0x004F48C0u
#define ADDR_PACKET_STATE        0x004F877Cu  /* int32_t, set to 2 */
#define ADDR_PACKET_SLOT_RESET   0x00402750u  /* void(int32_t), six times */
/* THE THREAD CALLS ITSELF THE *RECEIVE* THREAD, in the one message it logs on
 * the way out: " Receive thread got event 0 ". The PACKET in this macro's name
 * came from its creator -- StartPacketThread, ADDR_PACKET_THREAD -- rather than
 * from itself, which is the naming-from-a-call-site shape one level out. The
 * macro keeps its name because the creator cluster shares it; the
 * reconstruction is RecvThreadProc, after the string.
 *
 * It carried "stays original" here with NO reason beside it, which this file
 * elsewhere calls out as meaning "not yet". Reconstructed, and NOT patched:
 * its only reference in the image is StartPacketThread's CreateThread call and
 * that function is ours, so a detour would install a jump nothing reaches.
 * Fourth entry in tools/coverage.py's REGISTERED, beside WndProc,
 * AudioTimerProc and MedkitHealOne -- and the second of the four to be a
 * callback handed to the OS rather than to the game. */
#define ADDR_PACKET_THREAD_PROC  0x00401F00u  /* int32_t __cdecl(void *) */
/* Its neighbour in the same entry, still original: RecvThreadProc hands it each
 * received node and appends the node to ADDR_MSG_LIST_B when it answers 0.
 * Placeholder -- nothing read so far says what it decides. */
#define ADDR_RECV_MSG_4014C0     0x004014C0u  /* int32_t(void *node) */
/* Where the drop path receives a packet it has no node for, so the transport
 * does not keep re-delivering it. */
#define ADDR_RECV_SCRATCH        0x004F8790u
/* RecvThreadProc's four messages, all of them its own. */
#define ADDR_FMT_RECV_NO_NODE    0x004737E8u /* " ????? m = %x  freelist..." */
#define ADDR_FMT_RECV_NOW_NODE   0x004737C0u /* " ????? NOW m = %x ..." */
#define ADDR_FMT_RECV_NO_BUFFERS 0x00473774u /* "No Recieve Buffers free..." */
#define ADDR_FMT_RECV_DUMPING    0x0047374Cu /* "Dumping incoming message..." */
#define ADDR_STR_RECV_GOT_EVENT_0 0x0047372Cu /* " Receive thread got event 0 " */
#define ADDR_PACKET_THREAD_ID    0x004F8B90u  /* DWORD */
#define ADDR_STR_THREAD_FAILED   0x0047384Cu  /* "Error launching packet thread" */
#define ADDR_GAME_SRAND          0x00464416u  /* void(uint32_t) */
#define ADDR_GAME_RAND           0x00464420u  /* int32_t(void) */
/* 0x0040FD40, one caller -- and it fills no table. It writes the two-dword
 * HEADER of twenty separate messages that live in .bss, each one a
 * {kind, size} pair, and nothing else. The records are scattered across
 * 0x004F48E8..0x004FC8A8 and four of them already had names -- ADDR_MSG_CHAT,
 * ADDR_MSG_GAME_START, ADDR_MSG_COLOR, ADDR_MSG_TEAM -- so the layout is
 * confirmed rather than assumed: chat is kind 3 at 0x108 bytes, which is a
 * 256-byte line and a header.
 *
 * THE KIND IS NOT UNIQUE. Four different records take kind 0x0B, at sizes
 * 0x400, 0x014, 0x014 and 0x400, so the kind selects a HANDLER and the record
 * is which conversation it belongs to. A survey that assumed one record per
 * kind would come up four short.
 *
 * Reconstructed as a table and a loop rather than forty assignments, the same
 * shape win32/palette.cpp uses for the colours it looks up by name. Twenty
 * invented record names would be worse than none. */
#define ADDR_COMM_INIT_DEFAULTS  0x0040FD40u  /* void(void) */
#define COMMMSG_OFF_KIND         0x00u   /* int32_t */
#define COMMMSG_OFF_SIZE         0x04u   /* int32_t, the whole record */
#define ADDR_COMM_RESET_STATE    0x0040F380u  /* thiscall void(this) */

/* The six window messages WndProc used to hand back to the original. They are
 * comm traffic -- players joining and leaving, the host migrating, the session
 * ending -- reached through PostMessage from the DirectPlay callbacks.
 *
 * Names carry only what the BODY shows. Four of these name themselves in their
 * own format strings and are named accordingly; the rest say what was observed
 * and nothing more, because naming a function from the one call site that
 * happens to be in front of you is the mistake this file has already recorded
 * three times. */
#define ADDR_COMM_DRAIN_MSGS     0x00402690u  /* void(void), walks the msg list */
/* 0x00410090, 912 bytes, one caller -- the drain above. The DirectPlay SYSTEM
 * message handler, and it names its own cases: "DPSYS_HOST Size=%d, from=%x",
 * "CreatePlayer to=%x, name = %s id = %x", "DestroyPlayer Id=%x, to = %x",
 * "SESSIONLOST from=%x, to = %x" and, for anything else, "UnHandled System
 * Message %x %d". Stays original. */
#define ADDR_COMM_SYSTEM_MSG     0x00410090u  /* void(msg, size, from, to);
                                               * reconstructed */
/* The busy wait CommSystemMessage does after admitting the fourth player: one
 * second of GetTickCount in a loop, with nothing pumped and nothing drawn. */
#define AM2_COMM_JOIN_SETTLE_MS  0x3E8
/* The longest player name CommSystemMessage will copy into a slot; a name at
 * or past it is dropped and the slot keeps whatever was there. */
#define AM2_COMM_NAME_MAX        0x40
/* Its five format strings, which are what name its five cases. */
#define ADDR_STR_SYS_DESTROY_PLAYER 0x00475A2Cu /* "DestroyPlayer Id=%x, to = %x" */
#define ADDR_STR_SYS_CREATE_PLAYER  0x00475A04u /* "CreatePlayer to=%x, name = %s id = %x" */
#define ADDR_STR_SYS_SESSION_LOST   0x004759E4u /* "SESSIONLOST   from=%x, to = %x" */
#define ADDR_STR_SYS_UNHANDLED      0x004759C0u /* "UnHandled System Message %x %d " */
#define ADDR_STR_SYS_HOST           0x00475998u /* "DPSYS_HOST Size=%d, from=%x, to = %x" */
/* What it ORs into the session description's flags before handing it back.
 * Kept as the literal the image carries rather than spelled out as two
 * DPSESSION_ bits, because which two is a guess and the sum is not. */
#define AM2_SESSION_FULL_FLAGS   0x21
#define ADDR_COMM_NO_BUFFERS     0x00403280u  /* void(void), "COMM ERROR: NO BUFFERS" */
#define ADDR_COMM_PLAYER_SLOT    0x0040F320u  /* thiscall int32(this,id), 16 bytes */
/* Three tiny thiscall accessors for one per-player field, 0x020C. The names
 * are ours, from who sets what: ReceivePlayerMsg writes 1 into every record
 * that is NOT ours and 0 into the one that is, and CommPlayerLeft clears it.
 * So the field says a remote player is in that slot.
 *
 * The query is three-valued and only its first branch reads that field: an
 * OCCUPIED slot answers with it, an empty slot answers "am I not the host"
 * from 0x025C instead, and a slot that is neither answers -1. It also takes
 * its slot as a SIGNED WORD, alone in this family. */
#define ADDR_COMM_SLOT_REMOTE    0x0040F5A0u /* thiscall int32(this, int16 slot) */
/* 0x0040F560, thiscall. "Must I tell the other players what this army just
 * did?" -- the question every action with a network side asks first, and the
 * name is ours from the three answers it gives.
 *
 * No multiplayer session at all answers NO, which is the reading that settles
 * it: under "is this army mine" a single-player game would have to answer yes.
 * Army 4 -- the neutral one -- answers "am I the host". Everything else
 * answers "is that slot NOT remote", which is CommSlotRemote inverted, so it
 * inherits that function's three-valued oddity: a slot answering -1 is
 * truthy, and this turns it into a 0. */
#define ADDR_COMM_MUST_BROADCAST 0x0040F560u /* thiscall int32(this, int16 army) */

/* The air-support queue IS the block air.cpp saves, so these are offsets into
 * ADDR_AIR_SAVE_BLOCK rather than addresses of their own -- naming the first
 * dword separately would have put a second name on that address, which is
 * what checkpatches refused.
 *
 * Four parallel arrays of thirty, filled by DoAirSupport (0x00409710, which
 * refuses at thirty) and drained from the head.
 *
 * IT DID NOT CLOSE THE BLOCK, WHICH THIS COMMENT USED TO CLAIM. AIR_OFF_EXTRA
 * ends at 0x01EC and AIR_OFF_FLAG_A begins at 0x0240, leaving 84 bytes the
 * layout said nothing about. They are a SECOND queue, and 0x004093E7 gives its
 * bound outright as `cmp eax, 8; jge` -- eight dwords, eight dwords, eight
 * words, and then the two flags, which tiles the span exactly. The push at
 * 0x004093F0 fills it from the head of the main queue: the point out of
 * AIR_OFF_WHERE, a 1, and a word from 0x0042A7A0. 0x00408EC0 drains it the
 * same way AIR_POP drains the main one.
 *
 * A layout that does not tile is one where a base is wrong -- the rule the
 * trig tables taught. Here it was not a wrong base but a missing field, and
 * the comment asserting closure is what hid it. */
#define AIR_OFF_ACTIVE           0x000u  /* int32_t, 1 while one is running */
#define AIR_OFF_PENDING          0x004u  /* int32_t, cleared as one retires */
#define AIR_OFF_COUNT            0x008u  /* int32_t, at most 30 */
#define AIR_OFF_WHERE            0x00Cu  /* packed point[30] */
#define AIR_OFF_KIND             0x084u  /* int32_t[30], 2 or 3 */
#define AIR_OFF_FROM             0x0FCu  /* uint32_t[30], the uid asking */
#define AIR_OFF_EXTRA            0x174u  /* int32_t[30], what 0x00409680 found */
#define AIR_OFF_PASS_COUNT       0x1ECu  /* int32_t, at most 8 */
#define AM2_AIR_PASS_SLOTS       8      /* AirDeliver's own `cmp eax, 8` */
/* TWO OF THESE THREE WERE NAMED FROM THE WRITER AND ARE CORRECTED BY THE
 * READER. AirPassesDraw is the reader, and it is the only one.
 *
 * +0x1F0 was AIR_OFF_PASS_LIVE, "written 1", which is all the push shows. It
 * is a TIMER: the drawer adds ADDR_FRAME_DELTA_MS to it every frame, divides
 * it by half of ADDR_AIR_PASS_MS to pick a sprite, and retires the pass when
 * it reaches the whole of it. The 1 is just where it starts.
 *
 * +0x230 was AIR_OFF_PASS_TAG, "from 0x0042A7A0", which is UidArmy -- so the
 * writer already said what it is and the name did not. The drawer hands it to
 * CommMustBroadcast, whose parameter is an army, and to the strike, which
 * compares it against ADDR_DEFAULT_OWNER. Three readings, one answer. */
#define AIR_OFF_PASS_TIMER       0x1F0u  /* int32_t[8], ms, starts at 1 */
#define AIR_OFF_PASS_WHERE       0x210u  /* int32_t[8], from AIR_OFF_WHERE */
#define AIR_OFF_PASS_ARMY        0x230u  /* int16_t[8], from UidArmy */
/* How long one pass takes, in milliseconds. The drawer runs the twenty
 * ADDR_AIR_SPRITES_3 frames TWICE over it -- once in each half -- and lifts
 * the sprite by a second AM2_AIR_PASS_LIFT during the first half, so the two
 * halves are the approach and the departure. */
#define ADDR_AIR_PASS_MS         0x00473F34u  /* int32_t, 3000 */
#define AM2_AIR_PASS_FRAMES      20
#define AM2_AIR_PASS_LIFT        0x72   /* taken off Y once, or twice */
/* 0x00409540, 320 bytes, one caller -- what a pass does when its timer runs
 * out. The name is descriptive; the function names itself nowhere. */
#define ADDR_AIR_PASS_STRIKE     0x00409540u  /* void(uint32 at, int32 army) */
#define AM2_AIR_PASS_MAX         8
#define AIR_OFF_FLAG_A           0x240u  /* int32_t, set and cleared together */
#define AIR_OFF_FLAG_B           0x244u  /* int32_t, with the above */
#define AM2_AIR_MAX              30
#define AM2_AIR_SOUND            0x2Eu
/* 0x00408FF0. Retire the head of the queue and start whatever is behind it.
 *
 * Its log says "EndMission  AirSupport.count decreasing to: %d" -- and
 * "EndMission" is a PREFIX that DoAirSupport's own count line shares, not this
 * function's name. DoAirSupport names itself separately in the line above it.
 * So the name here is ours, from the body. */
#define ADDR_AIR_POP             0x00408FF0u  /* void(void) */
#define ADDR_AIR_BEGIN           0x00408F80u  /* void(void) */

/* ---- The air-support RUN -------------------------------------------------
 *
 * ADDR_AIR_FRAME_DRAW is the animation, and it turns out to be a flight path
 * with three legs plus two other things drawn beside it. Everything below was
 * read out of the nine one-line derivations at 0x00408AB0..0x00408D16 -- each
 * behind its own `jmp` thunk, each computing one path parameter from the
 * constants -- rather than guessed from the drawing code.
 *
 * The path, with the numbers those derivations produce:
 *
 *   leg 1, 0..800 ms    (110, 520) -> (300, 320)   in from off the bottom
 *   leg 2, 800..1200    a parabola through (340, 240) back to (300, 160)
 *   leg 3, 1200..2000   (300, 160) -> (-6, -100)   away off the top
 *
 * Both ends are off a 640x480 screen, which is what settles that this is an
 * aircraft entering and leaving rather than an effect placed on the map. The
 * middle leg bulges RIGHT by 40 pixels and comes back: a bank, not a turn.
 *
 * TWO OF THESE ARE COMPUTED AT STARTUP AND THE FILE'S VALUES ARE NOT THEIRS.
 * ADDR_AIR_PATH_TURN_Y_IN and _OUT read 300 and 0 in the image and 320 and
 * 160 once 0x00408AF0 and 0x00408B20 have run. Reading a table out of the
 * binary answers about the file; ask who WRITES it before treating a number
 * as a constant. */
/* The two waypoints that are genuinely constant -- nothing writes either. */
#define ADDR_AIR_PATH_TURN_X     0x00473F6Cu  /* int16_t, 300: leg 1 ends */
#define ADDR_AIR_PATH_AWAY_X     0x00473F70u  /* int16_t, 300: leg 3 starts */
#define ADDR_AIR_PATH_MID_Y      0x00473F60u  /* int32_t, 240: the bank's centre */
#define ADDR_AIR_PATH_APEX_X     0x00473F64u  /* int32_t, 340: its rightmost X */
#define ADDR_AIR_PATH_HALF_Y     0x00473F68u  /* int32_t, 80: half its height */
#define ADDR_AIR_PATH_IN_Y       0x00473F48u  /* int32_t, 520: below the screen */
#define ADDR_AIR_PATH_OUT_Y      0x00473F4Cu  /* int32_t, -100: above it */
/* Written by 0x00408AF0 and 0x00408B20 as MID_Y +/- HALF_Y. */
#define ADDR_AIR_PATH_TURN_Y_IN  0x00473F6Eu  /* int16_t, 320 at run time */
#define ADDR_AIR_PATH_TURN_Y_OUT 0x00473F72u  /* int16_t, 160 at run time */
/* The leg boundaries, in milliseconds of the run. */
#define ADDR_AIR_LEG1_MS         0x00473F40u  /* int32_t, 800 */
#define ADDR_AIR_LEG2_MS         0x00473F44u  /* int32_t, 1200 */
#define ADDR_AIR_RUN_MS          0x00473F3Cu  /* int32_t, 2000 */
/* Phase 1's period: how long the summoning object cycles its frames for. */
#define ADDR_AIR_CYCLE_MS        0x00473F38u  /* int32_t, 1000 */
/* Phase 2 -- the gauge that slides ADDR_AIR_SPRITES_2 across the top -- has a
 * duration and a line of its own, and 0x00473F28 is the double 0.43 that
 * gives that line its slope. */
#define ADDR_AIR_GAUGE_MS        0x00473F30u  /* int32_t, 2500 */
#define ADDR_AIR_GAUGE_X0        0x00473F20u  /* int32_t, -278 */
#define ADDR_AIR_GAUGE_Y0        0x00473F24u  /* int32_t, 320 */
#define ADDR_AIR_GAUGE_SLOPE     0x00473F28u  /* double, 0.43 */
/* The nine derived parameters, each named for the leg it belongs to. */
#define ADDR_AIR_LEG1_X0         0x004F96ACu  /* int32_t, 110 */
#define ADDR_AIR_LEG1_DX         0x004F96B0u  /* int32_t, TURN_X - LEG1_X0 */
#define ADDR_AIR_LEG1_DY         0x004F93C8u  /* int32_t, IN_Y - TURN_Y_IN */
#define ADDR_AIR_LEG2_MS_SPAN    0x004F93D0u  /* int32_t, LEG2_MS - LEG1_MS */
#define ADDR_AIR_LEG2_DY         0x004F96A4u  /* int32_t, TURN_Y_IN - TURN_Y_OUT */
#define ADDR_AIR_LEG2_DIVISOR    0x004F93CCu  /* float, the parabola's */
#define ADDR_AIR_LEG3_X1         0x004F96B4u  /* int32_t, -6 */
#define ADDR_AIR_LEG3_DX         0x004F93D4u  /* int32_t, AWAY_X - LEG3_X1 */
#define ADDR_AIR_LEG3_DY         0x004F96A8u  /* int32_t, TURN_Y_OUT - OUT_Y */
/* The hot spot of each of ADDR_AIR_SPRITES_6's eleven frames, {x, y} int16
 * pairs, subtracted from the path point before the draw. Eleven pairs for
 * eleven sprites is what ties the two tables together. */
#define ADDR_AIR_FRAME_HOTSPOTS  0x00473FB0u  /* int16_t[11][2] */
#define AM2_AIR_FRAMES           11
/* "Air frame %d, pt %d,%d\n" -- printed on leg 2 only, and the reason the
 * frame index is worth calling a frame index. */
#define ADDR_MSG_AIR_FRAME       0x00474024u
/* 0x004093D0, one caller, 80 bytes: what the run DELIVERS when it is over.
 * It dispatches on AIR_OFF_KIND -- 0 one way, 1 pushes onto the eight-slot
 * pass sub-queue, anything else does nothing. Note that is 0 and 1 where
 * AIR_OFF_KIND's own comment says "2 or 3"; both readings are from live code
 * and the field evidently carries more than two values. Stays original. */
#define ADDR_AIR_DELIVER         0x004093D0u  /* void(void); reconstructed */
/* The two constants its scatter uses. The slope turns the random X offset into
 * a Y one -- the twelve blasts fall along a line rather than in a disc, which
 * is what makes a strafing run look like a run. The kinds are six explosion
 * codes and the arm picks one per blast at random. */
#define ADDR_AIR_STRIKE_SLOPE    0x00473F28u  /* double, 0.43 */
#define ADDR_AIR_STRIKE_KINDS    0x00473FDCu  /* int32_t[6] */
#define AM2_AIR_STRIKE_KINDS     6
#define AM2_AIR_STRIKE_BLASTS    12   /* plus one at the centre afterwards */
#define AM2_AIR_STRIKE_SPREAD    0x140 /* the X offset's range, centred by */
#define AM2_AIR_STRIKE_HALF      0xA0  /* ... this, which is also Y's range */
#define AM2_AIR_STRIKE_Y_BIAS    0x50
#define AM2_AIR_STRIKE_JITTER    0x12C /* the delay's random part, and the */
#define AM2_AIR_STRIKE_BASE_MS   0x1E0 /* ... base it is added to */
#define AM2_AIR_STRIKE_SLIDE     3     /* the X offset's weight in the delay */
#define AM2_AIR_STRIKE_EXTRA     0x1E  /* SpawnAt's sixth argument */
/* 0x00408E50, 304 bytes, one caller -- ADDR_AIR_FRAME_DRAW, first thing.
 * Walks the eight-slot pass sub-queue and draws ADDR_AIR_SPRITES_3 at each
 * live one, advancing its timer. Stays original. */
#define ADDR_AIR_PASSES_DRAW     0x00408E50u  /* void(void) */
/* The sound the summoning object makes as it reaches frame 2, one per cycle.
 * AM2_AIR_SOUND above is 0x2E and this is 0x2F, so they are two different
 * sounds and not one name for one thing. */
#define AM2_AIR_CYCLE_SOUND      0x2F
#define AM2_AIR_CYCLE_FRAMES     3     /* wraps past this, and skips frame 1 */
#define AM2_AIR_CYCLE_STEP_MS    100
/* The two reveal radii RevealNearby is given, by AIR_OFF_KIND. */
#define AM2_AIR_REVEAL_NEAR_2    0x1388
#define AM2_AIR_REVEAL_FAR_2     0x4E20
#define AM2_AIR_REVEAL_NEAR_3    0x0140
#define AM2_AIR_REVEAL_FAR_3     0x1388
#define ADDR_AIR_CLEAR           0x00408FD0u  /* void(void) */
#define ADDR_COMM_SET_REMOTE     0x0040F600u /* thiscall void(this, int32 slot) */
#define ADDR_COMM_CLEAR_REMOTE   0x0040F620u /* thiscall void(this, int32 slot) */
#define COMM_ARMY_OFF_REMOTE     0x20Cu
#define COMM_ARMY_OFF_WAS_HERE   0x25Cu   /* what the query falls back to */
/* 0x0040F1C0, thiscall, one caller. CommSlotForArmy's shape with a different
 * answer: find the slot holding an army colour and return that slot's
 * COMM_ARMY_OFF_WAS_HERE rather than its index. No match answers 0, which is
 * also what an empty slot's field holds -- so the two are indistinguishable,
 * as they are in CommSlotForArmy. Name ours. */
#define ADDR_COMM_WAS_HERE_FOR_ARMY 0x0040F1C0u /* thiscall int32(this, army) */
/* 0x0040F520, thiscall, one caller. BYTES SENT IN THE LAST 100 MS: sum
 * COMM_OFF_STAT_SIZES over the slots whose COMM_OFF_STAT_TIMES is that
 * recent. CommSend fills both, one slot per packet round a 30-entry ring at
 * COMM_OFF_STAT_INDEX, so the pair is a sliding window over the send rate.
 *
 * Written up first as "a windowed sum of counters whose meaning is not
 * established" -- and both arrays were ALREADY NAMED, thirty lines apart in
 * this file, by the reporter that prints them. The offset ratchet is what
 * said so. Grep the offset as well as the address. */
#define ADDR_COMM_RECENT_TOTAL   0x0040F520u  /* thiscall int32(this) */
#define AM2_STAT_SLOTS           30
#define AM2_RATE_WINDOW_MS       100
/* 0x0043B7C0, one caller -- the per-frame path, and only in a network game.
 * Walk the four army records; any that WAS here but now has no player has its
 * units laid out again, which is the AI taking the abandoned army over. The
 * stride is AM2_PLAYER_STRIDE and the records begin at the comm object. */
#define ADDR_AI_TAKE_ABANDONED   0x0043B7C0u  /* void(void) */
/* 0x0043B700, one caller. THE FILE IS NOT AN .aai, which is what this comment
 * claimed for as long as it existed. BuildPlacementPath below formats
 * "%s_%s_place.txt", and thirty-six such files ship under the multiplayer map
 * directories -- so what is read here is the map's unit PLACEMENT list for one
 * army colour, through DefParseInfoFile, with "Couldn't parse %s!" if it will
 * not read. Named from the callee rather than from the layer above it, which
 * is the rule this file already carries and which was not applied here. */
#define ADDR_LOAD_ARMY_PLACEMENT 0x0043B700u  /* void(int32_t slot) */
/* 0x0043A560, one caller. sprintf(dest, "%s_%s_place.txt", map, colour) where
 * colour is green/tan/blue/grey chosen from the comm slot's own army index --
 * the COMM_ARMY_OFF_COLOUR field read inline, WITHOUT CommArmyOfSlot's
 * `slot == 4` special case. Anything above 3 leaves the colour NULL and the
 * CRT writes "(null)". Returns dest. */
#define ADDR_PLACEMENT_PATH      0x0043A560u  /* char *(char *dest, int32 slot) */
/* The four army colours, in the order the jump table at 0x0043A5C4 puts them:
 * 0 green, 1 tan, 2 blue, 3 grey. "green" lives with the other early strings
 * and the other three sit together; that split is the linker's. */
#define ADDR_STR_GREEN           0x00476A68u
#define ADDR_STR_TAN             0x00485148u
#define ADDR_STR_BLUE            0x00485140u
#define ADDR_STR_GREY            0x00485138u
#define ADDR_FMT_PLACE_FILE      0x00487B68u  /* "%s_%s_place.txt" */
#define ADDR_STR_PLACE_NO_NAME   0x00485B00u  /* "-", the files own placeholder */
/* The placement table: the records, how many, and how many fit. Freed and
 * rebuilt for each army. The grow is 32 to start and +8 a time. */
#define ADDR_PLACEMENTS          0x00654C7Cu  /* AM2_Placement * */
#define ADDR_PLACEMENT_COUNT     0x00654C80u  /* int32_t */
#define ADDR_PLACEMENT_CAP       0x00654C84u  /* int32_t */
#define ADDR_FREE_PLACEMENTS     0x0043B3D0u  /* void(void) */
#define ADDR_ADD_PLACEMENT       0x0043B410u  /* void(const AM2_Placement *) */
/* 0x0043B490 -- the `place` line parser, still original. It is not called by
 * name anywhere: the .aai keyword table at 0x00477484 holds its address
 * against "place", id 0x63, so the only reference to it in the image is that
 * data slot. It is also what settles the record's layout, since it fills the
 * block AddPlacement copies. */
#define ADDR_PARSE_PLACE_LINE    0x0043B490u  /* int32(?, char *line) */
#define ADDR_CAN_AFFORD_UNIT     0x0043A690u  /* int32(int32 type, int32 pts) */
/* The two halves of laying one record down, both still original and both
 * shared with the manual placement screen at 0x00413BC0. The first answers
 * whether the unit may go there; the second puts it there and takes the cost
 * out of the points, which is why it gets the budget BY ADDRESS. */
#define ADDR_PLACEMENT_ALLOWED   0x0043A810u  /* int32(where,type,slot,pts,facing) */
/* ITS EIGHTEEN-WAY SWITCH IS INDEXED BY ADDR_UNIT_TYPES, one arm per record,
 * and the table above is what makes the arms readable:
 *
 *   0..4   the five soldiers, and 17, the mine -- BlockWeightAt on the point
 *   5..9   the five vehicles -- MaskBlockWeight, and the kind each passes is
 *          its own record's +0x08: tank 1, jeep 0, halftrack 2, truck 3,
 *          ptboat 5. NOT a scrambled mapping, which is what it looks like
 *          until the table is dumped
 *   10..16 the buildings -- EnsureSpriteAaiRecord then CanPlaceAt, with the
 *          same six set ids ADDR_SPRITE_KEY_FOR_KIND and
 *          ADDR_UNIT_KIND_MATCHES use, selected by record +0x08 minus nothing
 *          at all: arm 10+k uses kind k
 *
 * AND IT EXPLAINS WHY THOSE TWO FUNCTIONS SHARE ARM ZERO THREE WAYS. Kinds 0,
 * 1 and 2 are riflepill, bazookapill and mgpill, and all three are set 0x26 --
 * three buildings on one sprite set. The jump tables could only show that the
 * share exists; the record table says what it is. */
#define ADDR_MAKE_PLACED_UNIT    0x0043ACF0u  /* void(where,type,slot,&pts,facing,
                                               * group,name); reconstructed */
/* The health a pillbox's occupant is given, read by nothing else in the
 * image. 55, where a rank-0 trooper gets what ADDR_RANK_RECORDS says. */
#define ADDR_PILLBOX_TROOPER_HEALTH 0x00473E44u /* int16_t, 55 */
/* What MakePlacedUnit hands CreateWeapon as the pillbox occupant's weapon
 * kind, by pillbox kind: riflepill 9, bazookapill 4, mgpill 8. The original
 * computes the last two with a `dec`/`neg`/`sbb`/`and 4`/`add 4` chain rather
 * than a table, which is the same 0-or-4 idiom it uses elsewhere. */
#define AM2_PILLBOX_WEAPON_RIFLE 9
#define AM2_PILLBOX_WEAPON_BAZOOKA 4
#define AM2_PILLBOX_WEAPON_MG    8
/* UNIT_TYPE_OFF_KIND 7 is the one placement type that gets ItemPostCreate and
 * a conceal check instead of an occupant. What it IS is not established -- the
 * arm is reached by elimination, after the trooper, vehicle and pillbox
 * classes have all been ruled out. */
#define AM2_PLACE_KIND_POST_CREATE 7
#define AM2_COMM_ARMY_COUNT      4
#define ADDR_COMM_FIND_PLAYER    0x0040F330u  /* thiscall int32(this,id), -1 if absent */
#define ADDR_COMM_REMOVE_PLAYER  0x0040F640u  /* thiscall int32(this,id) --
                                               * "Remove Player numPlayers now = %d" */
#define ADDR_COMM_PLAYER_LEFT    0x0040F790u  /* thiscall int32(this,id), 272 bytes */
/* The three pause reasons a departing player releases, one set per slot. They
 * ARE `0x800 << slot`, `0x10 << slot` and `0x20000 << slot` -- and the
 * original does not compute them: all twelve are spelled out as literals in
 * four arms. Reproduced that way, because a formula would be a claim the
 * binary does not make. */
#define AM2_PAUSE_LEFT_A         0x800u    /* << slot */
#define AM2_PAUSE_LEFT_B         0x10u     /* << slot */
#define AM2_PAUSE_LEFT_C         0x20000u  /* << slot */
/* void(void) -- "Sending EndSetupMessage". The end-of-setup scan, and the same
 * block is INLINED at the end of both ready handlers, so the image holds three
 * identical copies of it. That is what an inline member function looks like
 * once MSVC has declined to inline it at one site out of three. */
#define ADDR_COMM_END_SETUP      0x00410CE0u
/* IT NAMES ITSELF: "SendPlayerMsg for %d  Players: 
". The old name here was
 * off a call site. The HOST's game-setup broadcast -- map checksum, version,
 * the two names and a four-entry roster -- gated on COMM_OFF_DPLAY existing
 * and on being the host. Reconstructed. */
#define ADDR_SEND_PLAYER_MSG     0x00411270u  /* void(int32_t) */
#define ADDR_COMM_SESSION_OVER   0x0040FB70u  /* thiscall void(this), tail-calls 0x40FAA0 */
/* 0x00426A90. Shows the multiplayer end screen. Its argument is a RESULT
 * CODE, not a boolean: 0 won, 1 lost, 2 the host left, and anything else
 * leaves the bitmap alone. MissionNetworked only ever passes 0 or 1. */
#define ADDR_SHOW_MP_RESULT      0x00426A90u  /* void(int32 result) */
#define AM2_MP_RESULT_WON        0
#define AM2_MP_RESULT_LOST       1
#define AM2_MP_RESULT_HOST_LEFT  2
#define ADDR_STR_MP_WON          0x004852A8u  /* "mpwon.bmp" */
#define ADDR_STR_MP_LOST         0x0048529Cu  /* "mplost.bmp" */
#define ADDR_STR_MP_HOST_LEFT    0x0048528Cu  /* "mphostleft.bmp" */
/* 0x0040FB80, thiscall, and CORRECTED. This said "formats a string and hands
 * it to ADDR_COMM_SEND_PROPERTY -- 48 bytes and nothing else in it". The
 * function is EIGHT bytes: `push 2; call ADDR_COMM_SEND_PROPERTY; ret`. The
 * sprintf belongs to 0x0040FB90, which is a different function.
 *
 * That reading came from sweeping the size docs/functions.tsv gives the entry,
 * which is a MERGE -- the same trap ClearMenuMsgs turned up, and this time it
 * produced a wrong comment rather than only a mis-ranked candidate. Sweeping a
 * byte range is only safe once the range is known to be one function. */
#define ADDR_COMM_PUBLISH_RESULT 0x0040FB80u  /* thiscall void(this) */
#define AM2_COMM_PROPERTY_RESULT 2
/* 0x0040FA00, thiscall. Removes our player and reopens the session, and says
 * so itself: "Set Session Failed to reopen Session". ShowMpResult TAIL-JUMPS
 * to it, so it is the last thing the end screen does. */
#define ADDR_COMM_REOPEN_SESSION 0x0040FA00u  /* thiscall void(this) */
/* The bits ShowMpResult clears on the way in. */
#define AM2_MP_RESULT_UNPAUSE    0x1E78F0u
/* Sets the fog flag, and its argument is INVERTED against it: a non-zero
 * argument turns fog OFF. It also reveals every type 2/3/8 object on the way
 * in, so turning fog on starts from a clean slate rather than from whatever
 * the last frame left. WndProc's setup-done handler passes bit 18 of the game
 * flags, which makes fog a negotiated multiplayer option. */
#define ADDR_SET_FOG_OF_WAR      0x004295C0u  /* void(int32_t noFog) */
/* 0x00413480, 320 bytes, two callers. It went in as ADDR_LOBBY_RESET, from the
 * 0x046E setup-done handler that calls it -- and it is not a lobby anything.
 * It is the other half of ADDR_FREE_HUD_WIDGETS: free the three HUD widgets,
 * build three new ones. Every global it touches was already named for the HUD
 * by whoever wrote the free half, which is what settles it, and the second
 * caller is mission start, where it is followed by two more HUD helpers.
 * RENAMED rather than aliased. Eighth instance of a name taken from a call
 * site instead of a body.
 *
 * The three classes are unnamed and stay original, reached by address. Sizes
 * come from the `push` in front of each operator new.
 *
 * ADDR_HUD_WIDGET_C is built only when ADDR_NET_GAME is CLEAR, which is
 * independent confirmation of that global's own "may be null" comment and of
 * the free half's note that HudPaint and HudUpdate test C and not the other
 * two.
 *
 * HUD_A_OFF_CHECKBOX is a child widget, and three separate touchers say it is
 * a CHECKBOX rather than anything else: A's constructor news one and builds it
 * with ADDR_CHECKBOX_CTOR before handing it to WidgetAddChild; 0x004184E3
 * independently reads the same child and gates a loop on its +0x78; and
 * CHECK_OFF_TICKED is already 0x78, a uint8_t. So the latch around the rebuild
 * preserves the box's TICKED state -- and only outside a net game. */
#define ADDR_BUILD_HUD_WIDGETS   0x00413480u  /* void(void), 320 bytes */
#define ADDR_HUD_A_CTOR          0x00417580u  /* thiscall void *(this) */
#define ADDR_HUD_B_CTOR          0x00418FB0u
#define ADDR_HUD_C_CTOR          0x004195B0u
#define AM2_HUD_A_BYTES          0x5BCu
#define AM2_HUD_B_BYTES          0xCCu
#define AM2_HUD_C_BYTES          0xB0u
#define HUD_A_OFF_CHECKBOX       0x5B8u  /* AM2_Widget *, the checkbox child */
/* The HUD's message LOG, inside ADDR_HUD_WIDGET_A. Twelve rows of 88 bytes at
 * +0x6C, a live count at +0x594, and a running total of characters at +0x59C.
 * A row is text at +0, an x position as a FLOAT at +0x50, and the text's width
 * at +0x54 -- so the text has 80 bytes and nothing checks that.
 *
 * +0x59C went in as a "running total of characters" because that is what
 * HudMessage adds to it. HudTopUpdate is what says why: it DRAINS it, one a
 * frame at AM2_HUD_BLIP_MS with a sound each, so it is a budget of radio
 * blips and a longer message chatters for longer. Renamed accordingly. */
#define HUDLOG_OFF_ROWS          0x6Cu
#define HUDLOG_OFF_COUNT         0x594u
#define HUDLOG_OFF_BLIPS         0x59Cu  /* int32_t, chatter still owed */
#define AM2_HUD_MSG_SIZE         0x58u   /* 88 */
#define AM2_HUD_MSG_ROWS         12
#define HUDMSG_OFF_X             0x50u   /* float */
#define HUDMSG_OFF_WIDTH         0x54u   /* int32_t, TextExtent of the TEXT */
#define AM2_HUD_MSG_X_MIN        0x280   /* 640: never further left than this */
#define AM2_HUD_MSG_GAP          0x20    /* added between one row and the next */
#define AM2_HUD_TOTAL_CAP        10      /* per message, whatever its length */
/* The TYPED line, in the same widget as the log above it. When the flag is set
 * the strip shows what is being typed rather than the scrolling messages, with
 * a '_' appended as a caret -- which is where the console's characters land,
 * and why this class's destructor clears g_charHandler.
 *
 * The scroll offset is subtracted from every row's HUDMSG_OFF_X, so it is the
 * whole of the sideways crawl. */
#define HUDLOG_OFF_TYPING        0x48Cu  /* int32_t, non-zero while typing */
#define HUDLOG_OFF_TYPED         0x494u  /* char[], the line so far */
#define HUDLOG_OFF_SCROLL        0x598u  /* float, taken off each row's x */
#define HUDLOG_OFF_TYPED_X       0x5A8u  /* int32_t, relative to the widget */
#define HUDLOG_OFF_TYPED_Y       0x5ACu
#define AM2_HUD_CARET            '_'
/* The REWIND BUTTON, an 18x21 box at the strip's left + HUDLOG_OFF_BUTTON_X.
 * Its sprite slot is the one HudTopPaint draws, and the paint reached both
 * through bare literals until the update named them.
 *
 * The hit box is a CONSTANT 18x21 while the drawing is the sprite's own
 * bounds, so the two agree only because the art matches; nothing enforces it.
 *
 * The slot is cleared every frame and re-filled from HOT while the cursor is
 * over it, or from DOWN while the press is ours -- so a frame that neither
 * hovers nor holds draws no button at all, including the release frame. */
#define HUDLOG_OFF_SPRITE_HOT    0x5Cu   /* AM2_Sprite *, cursor over it */
#define HUDLOG_OFF_SPRITE_DOWN   0x60u   /* AM2_Sprite *, and held */
#define HUDLOG_OFF_BUTTON_SPRITE 0x64u   /* AM2_Sprite *, what paint draws */
#define HUDLOG_OFF_BUTTON_X      0x68u   /* int32_t, from the strip's left */
#define AM2_HUD_BUTTON_W         0x12    /* 18 */
#define AM2_HUD_BUTTON_H         0x15    /* 21, the strip's own height */
/* Set by HudChatSend and consumed by the update, which skips its whole input
 * section for that one frame -- so the release that sent the line cannot also
 * be read as a press on the strip. */
#define HUDLOG_OFF_JUST_SENT     0x490u  /* int32_t, one frame only */
#define HUDLOG_OFF_BLIP_AT       0x5A0u  /* uint32_t, ADDR_TICKS deadline */
/* While this is a live deadline the scroll offset eases back to 0, so the
 * button REWINDS the log. A press sets it 100 ms out and is renewed every
 * frame the button is held; the release sets it 2,000 ms out, which is the
 * whole of how long the rewind coasts after letting go. */
#define HUDLOG_OFF_REWIND_AT     0x5A4u  /* uint32_t */
#define HUDLOG_OFF_VIEW_W        0x5B0u  /* int32_t, past which it scrolls */
#define AM2_HUD_SCROLL_PPS       230.0f  /* pixels a second, both directions */
#define AM2_HUD_BLIP_MS          0x78    /* 120, plus 0..31 of jitter */
#define AM2_HUD_BLIP_JITTER      0x1F
#define AM2_HUD_REWIND_TAP_MS    0x64    /* 100, while the button is held */
#define AM2_HUD_REWIND_HOLD_MS   0x7D0   /* 2000, after it is released */
/* Binding 0x13 -- scancode 0x0E, BACKSPACE by default. It opens the strip's
 * console, which is the MULTIPLAYER CHAT line and not the cheat entry: what
 * the send at ADDR_HUD_CHAT_SEND does with the finished line is HudMessage it
 * locally and broadcast it to the other players. The cheat runner is a
 * separate path at ADDR_CHEAT_ENTRY. */
#define AM2_ACTION_CONSOLE       0x13
/* thiscall. Ends chat entry: clears the typing flag and ADDR_CHAR_HANDLER,
 * raises HUDLOG_OFF_JUST_SENT, posts the line to the log and sends it to every
 * comm player. Reached from the update on a mouse RELEASE while typing; the
 * char handler has its own RETURN path at 0x0041864C. */
#define ADDR_HUD_CHAT_SEND       0x00418480u  /* void(obj) */
#define ADDR_HUD_CHAT_CHAR       0x004185C0u  /* the ADDR_CHAR_HANDLER slot */
#define ADDR_HUD_TOP_UPDATE      0x00418660u  /* thiscall void(obj) */
#define ADDR_HUD_TOP_PAINT       0x00418A20u  /* thiscall void(obj, RECT) */
/* Reconstructed. 46 callers and NONE of them runs on any drive here -- 0 on
 * Boot Camp and 0 on the campaign -- so it is verified by reading. */
#define ADDR_HUD_MESSAGE         0x004144A0u  /* void(const char *, int32), 384 bytes */
#define ADDR_MENU_MESSAGE        0x00431C30u  /* void(const char *, int32, int32) */
/* What MenuMessage reaches. The log itself is the string-list class already
 * named at ADDR_LIST_ADD; the trim below it drops the OLDEST record and frees
 * its string when the list owns one.
 *
 * The last step is a BLINKER -- ADDR_BLINKER_START, already named from the
 * widget vtable survey -- with a period of 100 and 20 flashes, and the third
 * argument picks WHICH of the panel's two indicators flashes. That is what
 * the argument is for: Announce passes 0 and the host-migrated handler
 * passes 1, so a message about the game itself and a message about the
 * session light different lamps. */
/* Two arrays of sprites the multiplayer panel owns, and their bounds are the
 * destructor's loop bounds rather than anything declared: the first ends
 * exactly where ADDR_MENU_MSG_LIST begins, which is the same "the next global
 * is the limit" shape the registration table has -- so that bound is spelled
 * with the name already on the address rather than a second one. */
#define ADDR_MP_PANEL_SPRITES_A  0x00516118u  /* AM2_Sprite *[5] */
#define ADDR_MP_PANEL_SPRITES_B  0x00515FA0u  /* AM2_Sprite *[13] */
#define ADDR_MP_PANEL_SPRITES_B_END 0x00515FD4u
#define ADDR_MP_PANEL_DESTRUCT   0x00430480u  /* thiscall void(this) */
#define VTABLE_MP_PANEL          0x0046FA20u
/* A SCROLLING LIST OF COLOURED TEXT LINES, five slots like every widget here:
 * its own destructor and paint, the base's update, focus and repaint. Built at
 * 0x00433290 from 0x00430C95, on the screen that also loads
 * 03_010_00_scrollbar.bmp and the red and green button pairs -- so it is the
 * list those scroll, and the name is from what the paint DRAWS rather than
 * from that screen.
 *
 * Its source at +0x60 is {count, records} and a record is 0x104 bytes: 0x100
 * of text and then an INDEX, not a colour. The colour comes from a table of
 * dwords at +0x80 whose low byte is taken, so the records name a palette
 * entry rather than carrying one. */
#define VTABLE_TEXT_LIST         0x0046FA84u
#define ADDR_TEXT_LIST_PAINT     0x00433360u  /* thiscall void(obj, RECT) */
#define TEXTLIST_OFF_SOURCE      0x60u  /* {int32 count; void *records} */
#define TEXTLIST_OFF_FIRST       0x74u  /* int32, the top visible row */
#define TEXTLIST_OFF_VISIBLE     0x78u  /* int32, how many fit */
#define TEXTLIST_OFF_COLOURS     0x80u  /* int32[], low byte used */
#define TEXTLIST_SRC_COUNT       0x00u
#define TEXTLIST_SRC_RECORDS     0x04u
#define TEXTLIST_REC_SIZE        0x104u
#define TEXTLIST_REC_COLOUR      0x100u /* int32 index into +0x80 */
#define AM2_TEXT_LIST_ROW_H      14     /* (row - first) * 14 + 4 */
#define AM2_TEXT_LIST_PAD        4
#define AM2_TEXT_LIST_FONT       1
#define ADDR_MENU_MSG_LIST       0x0051612Cu  /* the message log, a string list */
#define AM2_MENU_MSG_MAX         0x64         /* trimmed above this many lines */
#define AM2_BLINK_PERIOD         0x64
#define AM2_BLINK_FLASHES        0x14
#define MP_PANEL_OFF_CHATBOX     0x21Cu
#define MP_PANEL_OFF_BLINKER_0   0x250u
#define MP_PANEL_OFF_BLINKER_1   0x254u
#define AM2_MENU_MODE_NO_CHAT    8
/* Was ADDR_CHAT_APPEND, which is what Announce's second call LOOKS like from
 * where it sits and not what the body does: it stamps a static message record
 * and hands it to SendGameMsg. Appending the line locally is what the FIRST
 * call does. Renamed, not aliased. */
#define ADDR_SEND_CHAT_MSG       0x00411E90u  /* void(char *, int32), 128 bytes */
/* 0x00411F10, one caller -- the same chat record sent to ONE player rather
 * than broadcast. It truncates at 254, copies the text to the record's +9,
 * takes the sender's ink from ADDR_ARMY_INK with white as the fallback, and
 * hands the record to SendGameMsg addressed to the slot's DirectPlay id.
 * Reconstructed. */
#define ADDR_SEND_CHAT_TO        0x00411F10u  /* void(char *text, int32 slot) */
#define AM2_CHAT_TEXT_MAX        0xFF   /* longer than this is cut at 0xFE */
#define MSG_CHAT_OFF_INK         0x08u  /* uint8_t, the sender's colour */
#define MSG_CHAT_OFF_TEXT        0x09u
/* SendChatTo's ink is byte 1 of ADDR_OBJ_TABLE_RECORDS, and naming it
 * ADDR_ARMY_INK at 0x004F9ACD gave that table a second base one batch ago --
 * the fourth time this session a field pointer has been named as though it
 * were a table, and the first of the four that was mine to begin with. The
 * records are 0x100 bytes from 0x004F9ACC and AM2_OBJ_TABLE_REC_SIZE already
 * spelled the stride; ADDR_ARMY_INK's own AM2_ARMY_INK_STRIDE was a second
 * spelling of that too. Both withdrawn.
 *
 * IT SETTLES ONE BYTE OF A RECORD ORIG.H CALLS UNESTABLISHED. Two readers
 * below the CRT line take `CommArmyOfSlot(comm, n) << 8` and then +1 off that
 * base, and the second hands the answer straight to a HUD text call as its
 * colour -- so byte 1 is an ink, and the records are indexed by ARMY. What the
 * other 255 hold is still open. */
#define OBJ_TABLE_REC_OFF_INK    0x01u  /* uint8_t, the army's text colour */
/* NOT a sprite anything. 0x00457820 walks every object an army owns and calls
 * the SECOND argument on each -- `call ebp`, where ebp is that argument. It
 * went in as ADDR_SPRITE_DROP_NAMED from a call site passing 0x0045A030; that
 * is a function too, not a sprite. See src/game/army.h.
 *
 * "THE ONE CALL SITE" WAS WRONG AND THIS NOTE SAID IT TWICE. There are SEVEN
 * -- 0x40AB76, 0x417DBB, 0x44857D, 0x448802, 0x448A94, 0x448CDD and 0x457988
 * -- each verified by decoding its `e8` displacement back to this address
 * rather than by trusting a tool. The rename was right and the count beside
 * it was never re-measured; seven distinct callbacks through one helper is
 * better evidence for the name than the single site ever was. */
#define ADDR_FOR_EACH_ARMY_OBJECT 0x00457820u /* void(army, void(*)(void*)) */
/* 0x004074A0, four callers. One observer against one object: range, bearing,
 * and then -- if the observer belongs to us -- reveal what it saw for two
 * seconds. The output record is filled BEFORE the ownership test, so a caller
 * gets the sighting's numbers whether or not the reveal happens. */
#define ADDR_CONSIDER_SIGHTING 0x004074A0u /* void(seen, out, const void *) */
#define AM2_SIGHT_CONE         3      /* |AngleDelta| must be at most this */
#define AM2_REVEAL_MS          0x7D0  /* 2000 */
#define SIGHT_OFF_OBSERVER     0x10u
#define SIGHT_OFF_RANGE        0x14u
#define SIGHT_OFF_BEARING      0x18u  /* uint8_t */
/* THE WEAPON BLOCK, and two of these three names were wrong for as long as
 * they existed. They were taken from ConsiderSighting, which only TESTS them
 * -- `if (!ctx[0x30]) return; if (!ctx[0x40]) return;` -- where a pointer and
 * a flag both read as "enabled". That is the identical mistake orig.h already
 * records fixing on the SIGHTC family, where +0x40 and +0x54 were ENABLED_40
 * and ENABLED_54 until UnitWeaponInfo was read. Made twice, on neighbouring
 * records, and caught the same way: by finding the WRITER.
 *
 * 0x00407D70's tail is that writer, and it settles all five:
 *
 *     out[0x30] = WeaponByUid(obj[0x550]);         the weapon OBJECT
 *     if (!weapon) { out[0x34] = 0; ... return; }
 *     out[0x34] = rec[ITEMTYPE_OFF_KIND];
 *     out[0x38] = rec[ITEMTYPE_OFF_RANGE] * 0.75;  the range it WANTS
 *     out[0x3C] = rec[ITEMTYPE_OFF_RANGE] * 1.1;   the range it stops at
 *     out[0x40] = (now - weapon[0xC4]) > rec[4];   the cooldown has elapsed
 *
 * So ConsiderSighting's two guards mean "no weapon, no sighting" and "the
 * weapon is not ready", which is a coherent rule the old names hid entirely.
 *
 * MAX_RANGE IS CONFIRMED TO THE BYTE rather than merely kept: the constant at
 * +0x3C is ADDR_WEAPON_RANGE_HI, the same named 1.1 that UnitWeaponInfo uses
 * to produce SIGHTC_OFF_MAX_RANGE. The low factors differ -- 0.9 there, 0.75
 * here -- which is a real difference between the two builders and not a
 * misreading of one. */
#define SIGHT_OFF_WEAPON       0x30u  /* obj *, null when unarmed */
#define SIGHT_OFF_KIND         0x34u  /* int32, ITEMTYPE_OFF_KIND */
#define SIGHT_OFF_WANT_RANGE   0x38u  /* range * ADDR_SIGHT_RANGE_WANT */
#define SIGHT_OFF_MAX_RANGE    0x3Cu  /* range * ADDR_WEAPON_RANGE_HI */
#define SIGHT_OFF_READY        0x40u  /* int32, 1 when the cooldown is up */
/* 0.75, and it has no counterpart in the SIGHTC path -- UnitWeaponInfo uses
 * ADDR_WEAPON_RANGE_LO, which is 0.9. Named separately for that reason. */
#define ADDR_SIGHT_RANGE_WANT  0x0046F2F8u  /* double, 0.75 */
#define SIGHT_OFF_SEED         0x2Cu  /* uint8_t, a fresh GameRand each build */
/* 0x00407D70, one caller -- the 0x00407F80 dispatcher, whose `sub esp, 0x44`
 * is this record's LENGTH. Build the sight record an AI mode arm reads:
 * resolve the leader and the target, measure to each, ask ADDR_SIGHT_SCAN
 * what is in view, and describe the held weapon.
 *
 * ITS TWIN IS 0x00408060 AND THEY ARE LESS ALIKE THAN THEY LOOK. That one
 * fills a 0x40-byte record -- `rep stos` of 0x10 dwords against this one's
 * 0x11 -- and differs in three ways, not one: it measures from the object's
 * raw OBJ_OFF_POS where this measures from ADDR_OBJ_ANCHOR_POINT; it resolves
 * a FORMATION point for the leader where this copies the leader's position;
 * and it writes fixed ranges where this reads the weapon. Its tail fields sit
 * one dword lower for want of the KIND field, which is exactly the four bytes
 * between 0x40 and 0x44.
 *
 * A DEAD LEADER CLEARS THE TARGET UID, not the follow uid its two neighbour
 * arms clear. Both builders do it identically, so it is the original's
 * behaviour and not a slip in one place -- which is what a single sighting
 * would have suggested. Reproduced.
 *
 * Reconstructed as AiBuildContext; ADDR_AI_BUILD_CONTEXT below is its
 * address, and was already on it. */
#define OBJ_OFF_FIELD_110      0x110u  /* ADDR_SIGHT_SCAN's fifth argument */
#define OBJ_OFF_FIELD_114      0x114u  /* and its fourth */
#define AM2_SIGHT_DROP         (OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED)

/* The rest of the SAME RECORD, which the AI arms call a context and
 * ConsiderSighting calls a sight. 0x00407D70 builds it, 0x00407F80 hands it to
 * whichever mode arm runs, and AiStepDefend hands it straight on to
 * ConsiderSighting -- so it is one structure and the offsets above are the
 * ones it reads.
 *
 * THIS BLOCK BRIEFLY HAD AN AICTX_ PREFIX OF ITS OWN, which made
 * SIGHT_OFF_OBSERVER, _RANGE and _BEARING a second time under new names.
 * checkoffsets could not see it, exactly as CLAUDE.md says of the ROW_/OBJ_
 * pair: the family-alias rule compares within a prefix and a new prefix is
 * invisible to it. What caught it was the next function read, one commit
 * later, passing the record to ConsiderSighting.
 *
 * +0x00 and +0x10 are objects resolved from the uids at OBJ_OFF_FOLLOW_UID
 * and OBJ_OFF_TARGET_UID, each dropped if it is gone or flagged. +0x14 and
 * +0x18 are the range and bearing to the second of those, from
 * ADDR_DIST_AND_ANGLE. +0x1C is what 0x00403B40 answered, with its range and
 * bearing beside it at +0x20 and +0x24; AiStepDefend promotes that triple
 * into +0x10/+0x14/+0x18 wholesale. +0x28 is ApproxDist from the object's
 * position to the destination point it remembers, computed only when that
 * point is non-zero. */
#define SIGHT_OFF_LEADER       0x00u  /* object *, from OBJ_OFF_FOLLOW_UID */
#define SIGHT_OFF_LEAD_RANGE   0x04u  /* to SIGHT_OFF_DEST, not to the leader */
#define SIGHT_OFF_LEAD_BEARING 0x08u  /* uint8_t */
#define SIGHT_OFF_DEST         0x0Au  /* packed point: where this unit belongs */
#define SIGHT_OFF_FOUND        0x1Cu  /* object *, what 0x00403B40 answered */
#define SIGHT_OFF_FOUND_RANGE  0x20u
#define SIGHT_OFF_FOUND_BEARING 0x24u /* uint8_t */
#define SIGHT_OFF_DEST_DIST    0x28u
#define AM2_AI_ARRIVED_DIST    0x20   /* nearer than this counts as arrived */
#define AM2_AI_TURN_DELAY_MS   0x82u
#define AM2_AI_FOLLOW_SLACK    0xF0   /* how far out of formation before moving */
/* 0x00407F80, the AI mode dispatcher: build the context with 0x00407D70, then
 * an eight-entry jump table at 0x0040803C on OBJ_OFF_AI_MODE. Indices 1, 4 and
 * 5 share one arm, so there are six handlers for eight modes. Read the table,
 * not the layout. */
#define ADDR_AI_STEP           0x00407F80u  /* void(obj, out, int32) */
#define ADDR_AI_JUMP_TABLE     0x0040803Cu
#define ADDR_AI_BUILD_CONTEXT  0x00407D70u  /* void(obj, ctx *) */
#define AM2_AI_CONTEXT_BYTES   0x44u
/* The region the object is standing in, written by AiStep every frame from
 * ADDR_REGION_OF_CELL indexed by OBJ_OFF_TILE. A byte zero-extended into a
 * word, which is why it is a uint16_t. */
#define OBJ_OFF_REGION         0xDCu  /* uint16_t */
/* READ NOW, after sitting here as "nothing in it says what it is and this
 * file will not guess from a call site". It turns a DESTINATION into a
 * HEADING, and it is the step every AI arm but one shares because that is the
 * one thing they all need. Nine call sites. Reconstructed as AiRouteToward;
 * see region.cpp.
 *
 * ITS THIRD ARGUMENT IS NEVER READ. Every caller pushes four and the body
 * touches frame+4, +8 and +16; frame+12 -- the sight context -- is not
 * among them. Fourth unused parameter in this tree. */
#define ADDR_AI_ROUTE_TOWARD   0x00407190u  /* void(obj, out, ctx, int32) */
/* Its two remembered regions, both int16 and both named from writers.
 * PREV_REGION is set to 0xFFFF in two places and stored into after a compare
 * in a third, so it is "the region we were in last time, or none yet";
 * GOAL_REGION takes the region the destination is in, written only by this
 * function and its twin at 0x004082C5. */
#define OBJ_OFF_PREV_REGION    0xDEu   /* int16_t, -1 for none */
#define OBJ_OFF_GOAL_REGION    0xE0u   /* int16_t */
/* AiRouteToward's own thresholds. ARRIVED ends the walk and clears the
 * destination; NEAR_WAYPOINT is what advances the cursor past a waypoint
 * already reached, and REPLAN is how far the target may drift before the path
 * is planned again. The two budgets PlanPathTo gets differ by three orders of
 * magnitude: the short one when the object has no region, the long one when
 * it has and the destination does too. */
/* AM2_AI_ARRIVED_DIST is 0x20 and is already defined further up, beside the
 * SIGHT_OFF_ block -- I defined it a SECOND time here and checkoffsets
 * refused it, which is the third time that ratchet has caught its author.
 * The 0x20 the waypoint cursor advances on is the same number, and gets no
 * name of its own for the same reason. */
#define AM2_AI_REPLAN_DIST       0x30
#define AM2_AI_PLAN_BUDGET_SHORT 0x30
#define AM2_AI_PLAN_BUDGET_LONG  0xC350
/* The two distances the tail grades the approach by: closer than SLOW it sets
 * one flag, closer than STOP it sets a second as well. */
#define AM2_AI_APPROACH_SLOW     0x48
#define AM2_AI_APPROACH_STOP     0x40
/* THE THIRD AND FOURTH AI-MODE DISPATCHERS, and their one shared caller says
 * which unit each is for. 0x0044B9FE branches on OBJ_OFF_SARGE: set, it calls
 * 0x00407020; clear, 0x004062B0. So one is Sarge's per-frame AI step and the
 * other every other trooper's -- not "armed and unarmed", which is what the
 * extra prologue looks like until the caller is read.
 *
 * BOTH BUILD THEIR OWN SIGHTC RECORD ON THE STACK. `sub esp,0x58` and
 * SIGHTC_OFF_READY at 0x54 fit exactly, and UnitWeaponInfo -- which fills six
 * SIGHTC fields -- is handed that frame, which is what settles the record as
 * SIGHTC rather than the SIGHT_OFF_ one the 0x00407F80 dispatcher's arms use.
 * Two dispatcher families, two different context layouts.
 *
 * The dispatch is on OBJ_OFF_AI_MODE through an eight-entry table, and the
 * table is the only honest source: three indices share the default arm.
 *
 *     0        -> ADDR_BIG_4057D0
 *     1, 4, 5  -> ADDR_BIG_405220        (5 is `evade`)
 *     2        -> AiWalkStep             (`ignore`)
 *     3        -> AiApproachLeader      (`follow`, see below)
 *     6        -> ADDR_AI_406B30         (`attack`)
 *     7        -> ADDR_CALL_405220       (`defend`, a thunk on the default)
 *
 * Reading the bodies top to bottom gives 2, 0, 3, 6, 7, default and gets four
 * of the eight wrong. Same shape as the 0x0040803C table, different arms.
 *
 * MODE 3 IS FOLLOW, and two functions reconstructed the same day say so
 * together. Type2PlayerStep sets OBJ_OFF_AI_MODE to 3 on every other selected
 * unit before attaching it to the one the player is commanding; arm 3 is
 * AiApproachLeader, whose whole job is to walk to SIGHTC_OFF_LEADER. Neither
 * reading says it alone -- CLAUDE.md's list of modes (attack 6, defend 7,
 * ignore 2, evade 5) has no 3 in it, because no shipped script writes one.
 * It is set by the SELECTION, not by a script.
 *
 * SARGE'S EXTRA PROLOGUE IS AN ITEM PICKUP: SelectBestWeapon, then a candidate
 * out of SIGHTC_OFF_FOUND that ObjIsType4 confirms is a weapon, its
 * position into the goal point, its +4 into OBJ_OFF_PICKUP_AFTER, the point
 * settled into a walkable region, and the distance recorded at SIGHTC +0x38.
 *
 * THE TROOPER HAS AN EXTRA TAIL SARGE DOES NOT, mapping OBJ_OFF_FIELD_540 and
 * SIGHTC_OFF_FIELD_00 onto SIGHTCOUT_OFF_STATE. It reads that field against
 * 0, 1 and 2 -- a third independent site agreeing it is 0..2. Reconstructed.
 */
#define ADDR_SARGE_AI_STEP     0x00407020u  /* void(obj, out), 368 bytes */
#define ADDR_TROOPER_AI_STEP   0x004062B0u  /* void(obj, out), 432 bytes */
/* Three arms and the context builder, all left original and reached by
 * address. 0x00404730 is the one worth a note: two OBJ_OFF_ entries already
 * record that it resolves OBJ_OFF_FOLLOW_UID and OBJ_OFF_UID_56C through the
 * uid lookup and stores the objects, so it is what FILLS the record every arm
 * below then reads. Named by offset because its body has not been read. */
/* The THIRD sight builder, and the trooper's. Its 0x58 record is the one
 * orig.h calls SIGHTC, and reading it explains the whole family:
 *
 *   roach    0x00408060  rep stos 0x10 dwords = 0x40
 *   vehicle  0x00407D70  rep stos 0x11 dwords = 0x44
 *   trooper  0x00404730  rep stos 0x16 dwords = 0x58
 *
 * WHY SIGHTC IS 0x14 BYTES LONGER. Its head shifts by four, because the class
 * from ClassifyByCode74 sits at +0 where SIGHT keeps the leader. Its weapon
 * block shifts by sixteen, because it also carries the vehicle at +0x2C, that
 * vehicle's distance at +0x30, and a second destination distance at +0x38.
 * 0x44 + 0x14 = 0x58, exactly.
 *
 * IT VALIDATES FAR MORE THAN ITS TWO SIBLINGS. They test flags & 0x204 as one
 * composite and drop; this splits the bits. A CONCEALED or dead leader is
 * dropped, and a DESTROYED one only when it is a non-riding trooper or a
 * vehicle -- a destroyed anything-else is kept. Its target block additionally
 * rejects an item whose health is negative and a weapon outright.
 *
 * Reconstructed as TrooperBuildContext. */
#define ADDR_TROOPER_BUILD_CONTEXT 0x00404730u /* void(obj, ctx, int32 sarge) */
/* READ. The arm SargeAiStep and TrooperAiStep both run: what to do about
 * SIGHTC_OFF_LEADER, which can be an ITEM to walk to as easily as a soldier
 * to follow. Four arms -- the cross product of "is it an item" with "are we
 * allied with it" -- doubled by a counter at OBJ_OFF_FIELD_110 that never
 * resets, so an object un-allied even once behaves differently from then on.
 * Reconstructed as AiApproachLeader. */
#define ADDR_AI_APPROACH_LEADER 0x00405DB0u  /* void(obj, out, ctx) */
/* SURVEYED AND NOT RECONSTRUCTED. 1,264 bytes over a 0x818-byte frame, one
 * caller, and the same callee set as AiAttackBody plus AiKeepRange and
 * AiHitReact -- so it is another sight-test-and-act step.
 *
 * IT USES THE SIGHTC_ CONTEXT WHERE AiAttackBody USES SIGHT_, which is the
 * distinction that block's warning exists for. Its fields land on OBSERVER
 * 0x14, RANGE 0x18, FOUND 0x20 and the range band at 0x4C/0x50 -- the 0x58-byte
 * record AM2_SIGHTC_BYTES names. Reading it with the other family shifts
 * everything by four and still compiles.
 *
 * IT SHARES EXACTLY ONE BLOCK WITH AiAttackBody and the share is verified
 * rather than assumed. Normalised, 125 of its 363 instructions match, which is
 * 35% and means "related", not "twin"; but ONE run of 38 --
 * 0x00406DBC..0x00406E58 against 0x004078C0..0x0040795C -- is identical in
 * every register, global, immediate and displacement, differing only in eight
 * BRANCH TARGETS. That is the tile-count-to-distance conversion, the clamp to
 * the rank sight range, and the three-band minimum update. Diffed at operand
 * level on purpose: a normalising diff maps every image address to a
 * placeholder, so two blocks reading DIFFERENT globals compare equal -- the
 * trap VehicleBlockWeight already cost this project once.
 *
 * SO THE SIGHT-CACHE UPDATE CAN BE FACTORED, with evidence, when this lands.
 *
 * THE HEAD IS A THREE-BAND RANGE DECISION and the middle threshold is worth
 * looking at before transcribing. It tests SIGHTC_OFF_RANGE against
 * SIGHTC_OFF_WANT_RANGE and then against SIGHTC_OFF_DAMAGE, taking AiKeepRange
 * in the band between them and closing the distance below it. Comparing a
 * distance against a DAMAGE reads wrong; it is coherent if the weapon's damage
 * doubles as a minimum standoff, which for a grenade or a bazooka is exactly
 * what an AI would want. Not established either way -- UnitWeaponInfo really
 * does write ITEMTYPE_OFF_DAMAGE there, so the name is not obviously the
 * error. Read the third consumer before deciding. */
#define ADDR_AI_406B30         0x00406B30u  /* void(obj, out, ctx) */
#define AM2_SIGHTC_BYTES       0x58   /* the frame both dispatchers reserve */
/* 0x00407BF0, one caller -- the `ignore` arm, mode 2. Reconstructed. */
#define ADDR_AI_STEP_IGNORE    0x00407BF0u  /* void(obj, out, const void *) */
/* 0x00407640, one caller -- the `defend` arm, mode 7. Reconstructed. */
#define ADDR_AI_STEP_DEFEND    0x00407640u  /* void(obj, out, void *) */
/* 0x00407C80, one caller -- the `follow` arm, mode 3. Reconstructed. */
#define ADDR_AI_STEP_FOLLOW    0x00407C80u  /* void(obj, out, void *) */
/* 0x00407560, one caller -- the arm modes 1, 4 and 5 share, 5 being `evade`.
 * ADDR_AI_STEP_DEFEND with the turn test moved into the shared tail, so it
 * turns while still walking. Reconstructed. */
#define ADDR_AI_STEP_TRACK     0x00407560u  /* void(obj, out, void *) */
#define SIGHTOUT_OFF_HIT       0x04u
#define SIGHTOUT_OFF_X         0x18u  /* int16 */
#define SIGHTOUT_OFF_Y         0x1Au  /* int16 */
#define SIGHTOUT_OFF_YADJ      0x1Cu  /* int16, from OBJ_OFF_ROW0_Y_ADJUST */
#define SIGHTOUT_OFF_UID       0x20u
/* 0x00408580, one caller -- ConsiderSighting's sibling, and near enough to it
 * that the differences are the point. Same three shared record fields (the
 * observer at +0x10, the range at +0x14, the bearing at +0x18), same
 * ObjIsOurs gate, same two-second reveal.
 *
 * FOUR THINGS DIFFER, and two of them are the record itself. Its enable is a
 * SINGLE field at +0x3C where the other tests +0x30 and +0x40; its maximum
 * range is at +0x38 where the other's is at +0x3C -- so +0x3C is a range in
 * one and an enable in the other, and the two cannot be one layout. Its cone
 * is EIGHT rather than three. And it has a tail the other has not.
 *
 * The out record is a different shape too: x, y and the y-adjust at +0x0C,
 * +0x0E and +0x10 against +0x18, +0x1A and +0x1C, though both put the uid at
 * +0x1C and +0x20 respectively and both use +0x04 as the hit flag. */
#define ADDR_CONSIDER_SIGHTING_B 0x00408580u /* void(seen, out, const void *) */
#define AM2_SIGHT_CONE_B         8
#define SIGHTB_OFF_MAX_RANGE     0x38u
#define SIGHTB_OFF_ENABLED       0x3Cu
#define SIGHTBOUT_OFF_BEARING    0x00u  /* uint8_t, compared then updated */
#define SIGHTBOUT_OFF_X          0x0Cu
#define SIGHTBOUT_OFF_Y          0x0Eu
#define SIGHTBOUT_OFF_YADJ       0x10u
#define SIGHTBOUT_OFF_STATE      0x14u  /* written 4 when a hit is committed */
#define SIGHTBOUT_OFF_UID        0x1Cu
#define AM2_SIGHTB_STATE_HIT     4
/* 0x00404F40, four callers -- the THIRD member of the family and the richest.
 * Same skeleton as the other two: enables, a range below a maximum, a bearing
 * cone, the output record, ObjIsOurs, the two-second reveal.
 *
 * ITS MAXIMUM RANGE HAS A MAGIC VALUE AND IT CUTS BOTH WAYS. When the record's
 * maximum is exactly 0x1000, a bearing OUTSIDE the cone is forgiven -- and the
 * reveal is SUPPRESSED. So that value means "sees in every direction but tells
 * nobody", which is what a passive sensor looks like, and it is one constant
 * doing two opposite-seeming jobs.
 *
 * A THIRD RECORD LAYOUT, and the offsets nearly line up with the first
 * without doing so: observer, range and bearing sit at +0x14, +0x18, +0x1C
 * against ConsiderSighting's +0x10, +0x14, +0x18 -- a clean shift of four --
 * while the enables move +0x10 and +0x14 and the maximum +0x14. So it is not
 * one record with a longer header; the three are related and distinct. */
#define ADDR_CONSIDER_SIGHTING_C 0x00404F40u /* void(seen, out, const void *) */
#define SIGHTC_OFF_OBSERVER      0x14u
#define SIGHTC_OFF_RANGE         0x18u
#define SIGHTC_OFF_BEARING       0x1Cu  /* uint8_t */
/* SIX FIELDS UnitWeaponInfo FILLS IN ONE GO, which is what named the two that
 * were called ENABLED_40 and ENABLED_54 -- names off the one site that TESTS
 * them, where a pointer and a flag both read as "enabled". +0x40 is the weapon
 * OBJECT, and it is set to null and returned on when there is none; +0x54 is
 * whether the weapon's cooldown has elapsed. */
#define SIGHTC_OFF_WEAPON        0x40u  /* obj *, null when unarmed */
#define SIGHTC_OFF_KIND          0x44u  /* 3 enables the state bump */
#define SIGHTC_OFF_DAMAGE        0x48u  /* int32, from the weapon record */
#define SIGHTC_OFF_MAX_RANGE     0x50u
#define SIGHTC_OFF_READY         0x54u  /* int32, 1 when the cooldown is up */
/* Three more of the same record, from AiKeepRange. +0x4C is the range the unit
 * WANTS to be at: it repositions only when SIGHTC_OFF_RANGE is at or inside
 * it, and it is the distance RandomPointToward steps. Distinct from
 * SIGHTC_OFF_MAX_RANGE at 0x50, which is the range beyond which
 * ConsiderSightingC stops looking. +0x00 and +0x3C gate the pose write and
 * nothing read so far says what either holds. */
/* IT IS THE FOUND OBJECT, and AiApproachLeader is what says so: it copies
 * +0x20, +0x24 and +0x28 into SIGHTC_OFF_OBSERVER, _RANGE and _BEARING and
 * stamps the found object's uid into OBJ_OFF_TARGET_UID -- which is exactly
 * AM2_ROACH_PROMOTE_FOUND, the block region.cpp already writes out four times
 * from SIGHT_OFF_FOUND, four bytes lower in the other base. So the old name
 * was right that it gates a turn and could not say why: a non-null found
 * object is something to turn toward. Renamed, not aliased, and the two
 * fields beside it named with it. */
#define SIGHTC_OFF_FOUND         0x20u  /* object *, and gates AiWalkStep */
/* AND FOUR MORE OF THIS FAMILY WERE ALREADY DEFINED, one screen down --
 * _FOUND_RANGE, _FOUND_BEARING, _LEAD_RANGE and _DEST, with a comment saying
 * "plus the four-byte head shift", which is the SAME relationship I had just
 * derived from scratch out of AiApproachLeader. checkoffsets refused all four
 * as identical redefinitions. Fourth time today that ratchet has caught its
 * author, and the first where what it caught was a rediscovery rather than a
 * duplicate name: the fact was in the file and the grep that would have found
 * it was for the OFFSET, not for the prefix. */
/* AiApproachLeader's distance bands. FAR is where it stops thinking and just
 * walks; NEAR ends the approach; CLOSE and the 0x20 either side of it are the
 * two-way test that decides whether to walk or to turn and shoot. */
#define AM2_AI_LEAD_FAR          0xF0
#define AM2_AI_LEAD_NEAR         8
#define AM2_AI_LEAD_CLOSE        0xC
#define AM2_AI_LEAD_SPREAD       0x20
#define SIGHTC_OFF_DEST_DIST     0x34u  /* to OBJ_OFF_FIELD_C0, as +0x28 is
                                         * in the vehicle record */
#define SIGHTC_OFF_WANT_RANGE    0x4Cu
/* Left named by offset, and here is the evidence that has accumulated. It
 * gates the pose write in AiKeepRange, and AiHitReact uses it as
 * `value * 2 + bit` into ADDR_HIT_POSE_BY_CLASS, which has exactly SIX usable
 * entries -- so it is 0, 1 or 2. That is the same range ADDR_WEAPON_POSE_INDEX
 * gets from ClassifyByCode74, which tools/posecheck.py enumerates as "the
 * object's class (0, 1 or 2)". Suggestive rather than settled: nothing seen so
 * far WRITES it. */
/* SETTLED, and the guess above was right. 0x00404730 -- the trooper's context
 * builder, the third of the family -- opens with `ctx[0] = ClassifyByCode74(obj)`.
 * So the field IS that classifier's 0, 1 or 2, established by the WRITER
 * rather than by two consumers agreeing on a range. The paragraph above
 * reasoned it out and said plainly that nothing wrote it; something does. */
#define SIGHTC_OFF_FIELD_00      0x00u
/* SETTLED: TrooperBuildContext's last act is `ctx[0x3C] = (uint8_t)GameRand()`,
 * so this is a fresh roll per build -- the SIGHTC counterpart of
 * SIGHT_OFF_SEED. AiHitReact comparing it against 4 and 0x10 is therefore a
 * PROBABILITY GATE, 4/256 and 16/256, not a threshold on anything measured. */
#define SIGHTC_OFF_SEED          0x3Cu  /* uint8_t, thresholds 4 and 0x10 */
#define AM2_AI_KEEP_RANGE_MS     0x1388 /* 5000, the reposition interval */
#define AM2_AI_REACHED_DIST      0xC    /* nearer than this and the walk ends */
/* AiTrooperStep's three of its own, none of which is any of the four above --
 * worth stating, because a family that already has ARRIVED (0x20), REPLAN
 * (0x30) and REACHED (0xC) invites reusing one. It arrives at 4, treats a
 * waypoint as reached at 0x10, and refuses to re-plan for 500 ms, which is
 * ADDR_PATH_RETRY_MS's shipped value written here as an immediate rather than
 * read from it. */
#define AM2_AI_TROOPER_ARRIVED   4
#define AM2_AI_WAYPOINT_DIST     0x10
#define AM2_AI_TROOPER_RETRY_MS  0x1F4
/* 0x004049C0, TWENTY-SIX call sites across the 0x00405xxx..0x00406xxx band and
 * one in 0x0044AD40 -- the step the trooper AI shares, as ADDR_AI_407190 is
 * the vehicle one. Nothing in it says what it is. */
#define ADDR_AI_TROOPER_STEP     0x004049C0u  /* void(obj, out, void *ctx) */
/* SURVEYED AND NOT RECONSTRUCTED, and the first thing to know is that it has a
 * SIBLING already written: AiRouteToward in region.cpp. Same destination field,
 * same OBJ_OFF_ROUTE_GOAL/OBJ_OFF_MOVE_TO pair, same `w[0] = AngleBetween(...)`
 * and `*(int32_t *)(w + 8) = state` tail. This one is the longer, region-aware
 * version. It must be DIFFED against that one rather than written fresh -- the
 * rule this file states for SettlePointInRegion and ItemLinkCells, both of
 * which were siblings of something already reconstructed and both of whose
 * comments held the answer to what looked odd in the new function.
 *
 * ITS OFFSETS ARE OVERLOADED BY TYPE and 0xC0 is the example. OBJ_OFF_FIELD_C0
 * is documented as a POINTER to an item type record, read that way by
 * SelectInventorySlot and cleared by a destroy handler -- and here, on a
 * trooper, the same offset is a packed POINT handed to ApproxDist and
 * PointsEqual. Both readings are of live code. The structural name is
 * therefore right and must stay: this is the type-6 overload one more time.
 *
 * WHAT IT DOES, in outline. Refuse if the goal is zero, or nearer than 4.
 * Re-plan when the goal has moved, the retry deadline has passed, or the
 * waypoint list is exhausted -- through BeginMoveTo for a straight run and
 * PlanPathTo for a real route -- with a region step in front: if the goal's
 * region differs from ours, RegionSolvePair the pair unless the matrix is
 * already stamped, take the next hop out of ADDR_REGION_NEXT and aim at that
 * region's own point through NearestAllowedTile. Then advance the waypoint
 * index past every waypoint within 0x10, and finally write the facing and a
 * state into the caller's record.
 *
 * THE STATE COMES OUT OF ONE OF TWO THREE-ENTRY TABLES, indexed by the AI
 * context's SIGHTC_OFF_FIELD_00 -- ClassifyByCode74's 0, 1 or 2. They differ
 * in ONE entry, which is the whole reason there are two of them, and the
 * tables TILE, 0x004750B4 running to exactly 0x004750C0. */
/* EIGHT CONSECUTIVE int32[3] TABLES RUN FROM 0x00475090 TO 0x004750F0, all
 * indexed by ClassifyCode74's 0, 1 or 2, and 0x0044A420 reaches five of them.
 * The tidy reading is one 8x3 array with a single base -- and taking it would
 * have put a second base over the storage these two names already hold, which
 * is the mistake this file warns about at the top under a different shape.
 * The tree committed to separate int32[3] names first; the rest follow it.
 *
 *   0x00475090  5, 7, 6     0x0047509C  5, 4, 4
 *   0x004750A8 10,11, 6     0x004750B4  2, 8, 9  (below)
 *   0x004750C0  3, 8, 9     0x004750CC 18, 8, 9
 *   0x004750D8 13,15,17     0x004750E4 12,14,16
 *
 * THE FRAME, from tools/espmap.py, because this one needs it. Origin at
 * esp+24; the three arguments are slots +0x1C (the trooper), +0x20 (the
 * weapon) and +0x24 (the output record). Two 4-byte points are local: P at
 * +0x10 is the cursor's world position, Q at +0x0C takes ObjOverlayY(obj)
 * added to P's y.
 *
 * AND SLOT +0x1C IS REUSED AS A LOCAL ONCE ITS ARGUMENT IS CONSUMED. The
 * trooper is loaded into esi at the second instruction and the slot then
 * holds the WEAPON CODE, which `cmp [esp+0x20], 0x14` reads two hundred bytes
 * later at a different esp. That is one of the three shapes CLAUDE.md lists
 * as making these bodies unreadable, and reading the later access as an
 * argument would put the trooper pointer where a small integer belongs.
 * espmap resolves both to +0x1C, which is what makes them visibly one slot.
 *
 * THE THIRD ARGUMENT HAS NO FIELD FAMILY, AND THAT IS A DECISION THE TREE
 * ALREADY MADE. StepType2 passes the same edi to this function and then to
 * Type2PlayerStep, so they write ONE record -- and Type2PlayerStep is
 * reconstructed and reaches it as bare `w + 4` and `w + 8`, naming nothing.
 *
 * So the question "is it a sight record" does not have to be answered to
 * write this. It was worth asking, because SIGHT_OFF_RANGE is a dword at
 * 0x14 and this function writes a POINT there, so the obvious family is
 * refuted -- and SIGHTCOUT_OFF_X at 0x14 fits a point exactly, which is
 * precisely the trap: a family whose offsets line up is not evidence of
 * anything. Opening one here would be a claim about the record's identity
 * that nothing supports, made attractive by two numbers agreeing.
 *
 * The sibling that writes the same record declined to name its fields, and
 * this follows it. Bare offsets with a comment beat an invented family, and
 * where a neighbouring reconstruction has already chosen, the tie goes to it.
 *
 * Their width was worth checking rather than inferring: the first table's
 * three used entries are followed by three more before the next base, so
 * 3-wide and 6-wide fit the layout equally. What settles it is the CLASSIFIER
 * -- item.cpp already records ClassifyCode74 answering 0, 1 or 2 -- and not
 * the spacing. Read the index's range from the function that produces it. */
#define ADDR_AI_MOVE_STATE       0x004750B4u  /* int32[3]: 2, 8, 9 */
#define ADDR_AI_MOVE_STATE_ALT   0x004750C0u  /* int32[3]: 3, 8, 9 */
#define AM2_AI_MOVE_STATES       3
/* Selects between them, and nothing read so far writes it -- so which arm a
 * trooper takes is not established, only that class 0 is the only class the
 * choice can reach. */
#define OBJ_OFF_MOVE_STATE_ALT   0xF0u
/* Soldier kind 7 overrides both tables with 3, which is ADDR_AI_MOVE_STATE_ALT
 * entry 0 -- so the override is "use the alt table's answer" spelled as a
 * literal, not a fourth state. */
#define AM2_AI_KIND_FORCES_ALT   7
/* 0x004060D0, one caller, inside the trooper step chooser at 0x0044B990. A
 * trooper AI step of the usual shape -- build the 0x58 context, act, set the
 * output state -- with two things in it that no other member of the family
 * has. The name is OURS and describes the distinguishing act, the way
 * SettlePointInRegion's does.
 *
 * WALK, THEN ATTACH. While SIGHTC_OFF_DEST_DIST is over AM2_AI_REACHED_DIST it
 * copies the destination into OBJ_OFF_FIELD_C0 and hands off to
 * ADDR_AI_TROOPER_STEP, which is AiStepIgnore's shape exactly. On arrival it
 * clears the destination, picks an object out of an army's list, and if that
 * object is within 0x400 calls ADDR_OBJ_ATTACH_TO.
 *
 * AND THE PICK READS THE WRONG LIST. The count and the modulus come from the
 * army in `edi` -- which is the object's own half the time and a random 0..3
 * the other half -- while the array actually indexed belongs to obj[+0x10].
 * When they differ and the chosen army holds more objects, this reads past the
 * end of the uid list. Reproduced: it is the original's behaviour, it is not
 * reachable by any test this project can run, and it is exactly the class of
 * defect the LockSurface Restore path is kept for.
 *
 * IT KILLS THE UNIT ON A TIMEOUT. Fifteen seconds after OBJ_OFF_DEADLINE_58,
 * with the destination reached, the object takes AM2_AI_TIMEOUT_DAMAGE from
 * its own army's owner object. With a multiplayer session up and
 * CommMustBroadcast answering no, it broadcasts by hand and suppresses the
 * automatic one; otherwise it damages plainly.
 *
 * THE SIGNED %4 IS DEAD. `sar/and 0x80000003/jns/dec/or/inc` is MSVC's signed
 * modulo, but GameRand never returns a negative, so the correction arm cannot
 * run and `& 3` would be equivalent. The idiom says the source wrote a signed
 * `% 4`; it is not a hazard here and is not written up as one.
 *
 * ONE READ IS UNALIGNED. `mov eax, [esp+0x1a]` takes a dword at ctx+0x0E,
 * across the two-byte gap after SIGHT_OFF_DEST. Kept at 0x0E rather than
 * tidied to a neighbouring field. Reconstructed. */
#define ADDR_AI_STEP_ATTACH      0x004060D0u  /* void(obj, out) */
#define AM2_AI_IDLE_TIMEOUT_MS   0x3A98  /* 15,000 */
#define AM2_AI_ATTACH_RANGE      0x400
#define AM2_AI_TIMEOUT_DAMAGE    0x2710  /* 10,000 */
/* The output state, chosen on a SECOND and shorter timer against the same
 * stamp: over ten seconds gives 0x27 and at or under it gives 0x29. The
 * threshold is 10,000 MILLISECONDS and happens to equal AM2_AI_TIMEOUT_DAMAGE
 * exactly; it is named separately because a constant that is numerically
 * right can still be the wrong constant, and reusing that one would say the
 * state depends on how much damage the unit takes. */
#define AM2_AI_STATE_MS          0x2710  /* 10,000 ms */
#define AM2_AI_STATE_STALE       0x27    /* idle longer than that */
#define AM2_AI_STATE_RECENT      0x29    /* and not */
#define SIGHTC_OFF_DEST          0x0Eu  /* packed point; read UNALIGNED by
                                        * AiStepAttach, which is ordinary
                                        * once you know it is a point */
/* SIGHT_OFF_FOUND_RANGE and _BEARING, plus the four-byte head shift. */
#define SIGHTC_OFF_FOUND_RANGE   0x24u
#define SIGHTC_OFF_FOUND_BEARING 0x28u
#define SIGHTC_OFF_VEHICLE       0x2Cu  /* obj *, LookupType3ByUID */
#define SIGHTC_OFF_VEHICLE_DIST  0x30u
/* The distance to the point in OBJ_OFF_SCRIPT_FRAME, which is that dword's
 * OTHER reading -- a packed point in this band and a script frame number in
 * objscript.cpp. It went in as SIGHTC_OFF_PICKUP_DIST from SargeAiStep,
 * which stores a weapon's position there; TrooperBuildContext measures to
 * whatever is there, so the general name is the right one. */
#define SIGHTC_OFF_DEST_DIST_B   0x38u
#define SIGHTC_OFF_LEADER        0x04u  /* obj *, from OBJ_OFF_FOLLOW_UID */
#define SIGHTC_OFF_LEAD_RANGE    0x08u  /* to SIGHTC_OFF_DEST */
#define SIGHTC_OFF_LEAD_BEARING  0x0Cu  /* uint8_t */
/* 0x00405100, six call sites in five functions. Reconstructed. */
#define ADDR_AI_KEEP_RANGE       0x00405100u  /* void(obj, out, void *ctx) */
/* 0x00405050, TEN call sites across the same band. The trooper family's
 * reaction to being hit: it returns at once unless OBJ_OFF_HIT_DIR is set, and
 * otherwise picks a pose from OBJ_OFF_SOLDIER_KIND, SIGHTC_OFF_KIND and
 * SIGHTC_OFF_SEED. Reading it is what confirmed that the record these
 * functions carry really is the SIGHTC one -- it tests 0x44 against 3, which
 * SIGHTC_OFF_KIND already documented, and reads 0x3C beside it. */
#define ADDR_AI_HIT_REACT        0x00405050u  /* void(obj, out, void *ctx) */
/* AiHitReact reads RANK_REC_OFF_THRESHOLD out of ADDR_RANK_RECORDS -- 32, 48,
 * 56, 64, 80, 96, 112, 128, rising with rank. This comment used to say it read
 * "the first dword", which is what put the table's base four bytes late; see
 * the record's own note. */
/* Six poses, 12..17, indexed by `SIGHTC_OFF_FIELD_00 * 2 + (byte >= 0x80)`.
 * The two entries past them are not part of the table -- 0x4FA0C0 and 999 --
 * which is what bounds it at six and puts that field's range at 0..2. */
#define ADDR_HIT_POSE_BY_CLASS   0x00475198u  /* int32_t[6] */
#define AM2_POSE_HIT_HEAVY       7
#define AM2_POSE_KIND7           0x2B
/* 0x00405D30, two callers. Reconstructed. */
#define ADDR_AI_WALK_STEP        0x00405D30u  /* void(obj, out, void *ctx) */
#define AM2_SIGHT_OMNI_RANGE     0x1000
#define SIGHTCOUT_OFF_BEARING    0x04u  /* uint8_t, compared not written */
#define SIGHTCOUT_OFF_STATE      0x08u  /* 2 becomes 3 */
#define SIGHTCOUT_OFF_HIT        0x0Cu
#define SIGHTCOUT_OFF_SEEN       0x10u
#define SIGHTCOUT_OFF_X          0x14u
#define SIGHTCOUT_OFF_Y          0x16u
#define SIGHTCOUT_OFF_YADJ       0x18u
#define SIGHTCOUT_OFF_UID        0x1Cu

/* Non-zero means fog is ON -- objects get concealed. It was ADDR_AI_CONTROLLED,
 * a name taken from a call site; the cheat strings that drive it are about
 * seeing, not about who is playing. */
/* ITS POLARITY IS INVERTED AGAINST ITS NAME, and this said "1 = fog on".
 * ADDR_TOGGLE_FOG_OF_WAR settles it: it flips this flag and then, for every
 * enemy object, calls ADDR_OBJ_REVEAL when the flag is NON-ZERO and
 * ADDR_OBJ_CONCEAL when it is ZERO. So non-zero is the REVEALED state -- fog
 * OFF -- and zero is fog on.
 *
 * ObjConceal agrees: it declines unless the flag is zero or its caller forces
 * it. SetFogOfWar agrees too, once its own argument is read the right way
 * round: a non-zero argument stores 0 here, which is fog ON.
 *
 * Three readers, one answer. The old comment made SetFogOfWar look like it
 * revealed everything while turning fog on, which was recorded at the time as
 * "counter-intuitive" rather than as a sign the polarity was wrong. */
#define ADDR_FOG_OF_WAR       0x00476FB0u  /* int32_t, 0 = fog ON */
#define ADDR_PAUSE_FLAGS         0x005122FCu  /* uint32_t, one bit per reason */
/* Raised by 0x00411000 and lowered by the 0x046E handler, and read from 21
 * places -- the lobby, the overlay, TakeMenuRequest and the mission code. The
 * shape of a "this is a network game" flag; named for what is observed rather
 * than for the one call site that happened to be in front of me. */
#define ADDR_NET_GAME            0x00511DD4u  /* int32_t */
#define ADDR_GAME_OVER_FLAGS     0x00515FD8u  /* uint32_t, bit 18 selects the AI mode */
#define ADDR_HUD_MESSAGE_COLOUR  0x00507234u  /* uint8_t, colour for ADDR_HUD_MESSAGE */
/* The callback the line above hands to it when a player leaves: for a type 2
 * object, clear +0x57C if it still has health and put +0xE4 -- the AI mode --
 * to 6, which orig.h already records as `attack`. So the departed player's
 * units are handed to the AI, which is what the message beside it says. */
#define ADDR_OBJ_TO_AI           0x0045A030u  /* void(void *obj) */
/* 0x00459FE0, two callers, and 0x0045A030's neighbour -- functions.tsv runs
 * the two together into one 144-byte entry, so reconstructing either alone
 * would mark both done. The weapon object a unit or vehicle is holding. */
#define ADDR_HELD_WEAPON_OBJ     0x00459FE0u  /* void *(const void *obj) */
#define ADDR_STR_ALLRIGHT_WAV    0x00474194u  /* "AllRight.wav" */
#define ADDR_STR_HOST_NOW        0x00474178u  /* "Player %s is now the host." */
#define ADDR_STR_LEFT_AI         0x004741ECu  /* "Player %s has left the game - now AI" */
#define ADDR_STR_LEFT_GAME       0x004741CCu  /* "Player %s has left the game." */
#define ADDR_STR_SET_SESSION_FAIL 0x004741A4u /* "Set Session Failed to reopen Session" */
#define ADDR_STR_DESTROYPLAYER   0x00474220u  /* "DESTROYPLAYER Win Message ..." */

/* THE PLAYER RECORD STARTS AT 0x020C AND THIS SAID 0x0218, which is its NAME
 * field -- "the name is the first field", from the two "Player %s" messages
 * that were the only readers anyone had looked at. CommRemovePlayer settles
 * it: its compaction loop moves fields at 0x0C BELOW 0x218 down a slot, and
 * CommConstruct writes the slot index at 0x210. Fields before the "first"
 * field are the tell, and it is the fourth time this session that a base named
 * from one consumer has turned out to be a field pointer -- see
 * ADDR_RANK_RECORDS and ADDR_SOLDIER_NAMES.
 *
 * The layout, from CommConstruct, CommRemovePlayer and the absolute names
 * already scattered through this file:
 *
 *   +0x00  cleared on construction
 *   +0x04  the slot INDEX; CommConstruct writes i
 *   +0x08  AM2_PLAYER_ID, the DirectPlay id
 *   +0x0C  the NAME
 *   +0x4C  the TEAM; ResetLevelState allies any two that match
 *   +0x50  non-zero while the slot is occupied
 *   +0x64  COMM_ARMY_OFF_READY_TO_LOAD
 *   +0x68  COMM_ARMY_OFF_READY
 *   0x70 bytes to the next
 *
 * AM2_PLAYER_ID, COMM_ARMY_OFF_READY and COMM_ARMY_OFF_READY_TO_LOAD are the
 * same fields spelled absolutely, which works because they are used as
 * `comm + slot * 112 + OFFSET` and slot 0's record starts at 0x20C. They are
 * left alone; folding them in is a separate job with its own A/B.
 *
 * AND THE FIELD NAMES ALREADY EXISTED, under the COMM_SLOT_OFF_ prefix --
 * INDEX 0x004, ID 0x008, NAME 0x00C, TAKEN 0x050, UNACKED 0x058, HEARD 0x060,
 * every one of them an offset from 0x20C and every one of them right. So the
 * family was correct, the private bases were correct, and the single shared
 * BASE was the only thing wrong. Only the team is new.
 *
 * Three COMM_PLAYER_OFF_ names were nearly added beside them in this very
 * edit, which is the third time this session a duplicate has been created
 * under a NEW PREFIX -- the one shape tools/checkoffsets.py cannot see, and
 * the rule this file already states. Writing the rule down did not prevent it;
 * grepping the OFFSET would have. */
#define COMM_OFF_PLAYERS         0x20Cu
#define COMM_SLOT_OFF_TEAM       0x04Cu  /* ResetLevelState allies on it */
#define AM2_COMM_PLAYERS         4
#define COMM_PLAYER_STRIDE       112u
#define COMM_OFF_VERBOSE         0x418u   /* non-zero: log every DESTROYPLAYER */
/* The PEER record PeerShouldNack works on -- the argument 0x004014C0 passes
 * through, and NOT the comm object: the verbose flag it consults comes from
 * the global at ADDR_COMM_OBJECT while everything else comes from this.
 *
 * FOUR OF THESE ARE NAMED BY THE PROGRAM. Its own format string reads
 * "Nacking %6d to %x at %d  >?  %d  ( %d ) Interval = %d Latency = %d
 * count = %d nackinterval = %d", and the arguments line up: the two running
 * sums divided by their counts are the INTERVAL and the LATENCY, the record's
 * third dword is the COUNT, and the capped latency is the NACKINTERVAL. */
#define PEER_OFF_ID              0x00u   /* "%x" in both messages */
#define PEER_OFF_INTERVAL_N      0x04u
#define PEER_OFF_FIELD_38        0x38u
#define PEER_OFF_FIELD_40        0x40u   /* below half of 0x38 seeds count 1 */
#define PEER_OFF_LATENCY_N       0x30u
#define PEER_OFF_LATENCY_SUM     0x4Cu
#define PEER_OFF_INTERVAL_SUM    0x58u
#define PEER_OFF_NACK_COUNT      0xB8u
#define PEER_OFF_NACKS           0xBCu   /* {seq, time, count}[] */
#define NACKREC_OFF_SEQ          0x00u
#define NACKREC_OFF_TIME         0x04u
#define NACKREC_OFF_COUNT        0x08u
#define AM2_NACKREC_STRIDE       12u
#define AM2_NACK_RECS_MAX        0x3C    /* 60, and the last slot is reused */
#define AM2_NACK_INTERVAL_CAP    0xBB8   /* 3000 ms */
/* 0x00402C30, one caller. Should a NACK go out for this sequence number?
 * Reconstructed. */
#define ADDR_PEER_SHOULD_NACK    0x00402C30u  /* int32(peer, uint32 seq) */
#define ADDR_STR_NACKING         0x00473C30u
#define ADDR_STR_NACK_FULL       0x00473C94u
/* The IAT slot the original CALLS THROUGH for GetTickCount. Reached this way
 * rather than by importing the symbol, for two reasons: it is what the
 * original does -- `call dword ptr [0x0046F084]` -- and air.cpp is on the flat
 * side of the split, where naming a Win32 declaration is what
 * tools/checksplit.py exists to refuse. */
#define ADDR_IAT_GET_TICK_COUNT  0x0046F084u  /* uint32_t (__stdcall *)(void) */
/* Where the registry key and the application GUID live in the image. Neither is
 * restated here -- the game's own copies are used, as with the DirectPlay
 * CLSIDs. The GUID is {2777D2A2-89D1-11D2-A387-00C04F79DCEB}. */
/* The copy-protection dialog's two strings. Every one of the five CD checks
 * uses this pair; see docs/binarypatches.md, and note that all five checks are
 * patched to unconditional in this build so none of them can appear. */
#define ADDR_CD_REQUIRED_TEXT    0x00475578u  /* "The ARMYMEN2 CD must be..." */
#define ADDR_CD_REQUIRED_CAPTION 0x004755B4u  /* "Copy Protection" */
#define ADDR_REGISTRY_KEY        0x004751E8u  /* "Software\\The 3DO Company\\Army Men II" */
#define ADDR_APP_GUID            0x0046F8A8u  /* GUID, the DirectPlay application id */
#define ADDR_COMM_CLOSE          0x0040DCF0u  /* int32_t(void) */
#define ADDR_COMM_INIT_CONN      0x0040DD90u  /* thiscall int32(this, conn) */
#define ADDR_COMM_SET_SESSION    0x0040E630u  /* thiscall int32(this, desc, flags) */
#define ADDR_COMM_GET_SESSION    0x0040E5A0u  /* thiscall int32(this) */
#define COMM_OFF_SESSION_BUF     0x3E8u       /* the description, on the game heap */

/* ---- streaming audio ---------------------------------------------------
 *
 * A looping DirectSound buffer kept fed by a multimedia timer, reading through
 * src/game/win32/wavefile.cpp. Reconstructed in src/game/win32/audio.cpp.
 */
#define ADDR_STOP_AUDIO_STREAM   0x0040D5D0u  /* void(void) */
/* 0x0040C9F0, InitAudio's counterpart and the tenth of ShutdownSubsystems'
 * ordered teardown entries. Gated on exactly the flag InitAudio sets, which is
 * what grounds the name: nothing in the body says "audio" that its three
 * callees do not say already, and that flag has exactly one writer.
 *
 * It does NOT clear the flag, so a second call would run the whole thing
 * again. Safe rather than sloppy -- FreeWaveSounds clears each slot after
 * freeing it for that reason, and its own comment says so. */
#define ADDR_SHUTDOWN_AUDIO      0x0040C9F0u  /* void(void) */
#define ADDR_START_AUDIO_STREAM  0x0040D680u  /* void(void *track, int32) */
#define ADDR_AUDIO_ENABLED       0x004FA468u  /* int32_t; nothing runs while clear */
#define ADDR_AUDIO_BUFFER        0x004FA404u  /* IDirectSoundBuffer *, the stream */
#define ADDR_AUDIO_BUFFER_2      0x004FA440u  /* IDirectSoundBuffer *, the one played */
#define ADDR_STOP_ALL_SOUNDS     0x0040D730u  /* void(void) */
#define ADDR_FREE_SOUND          0x0040C6E0u  /* void(AM2_Sound *) */
/* 56 fixed slots of 16 bytes, then 17 pointers to allocated sounds. */
#define ADDR_SOUND_SLOTS         0x004FA040u
#define ADDR_SOUND_SLOTS_END     0x004FA3C0u
#define ADDR_SOUND_DYNAMIC       0x004FA3C0u  /* == SLOTS_END; the tables abut */
/* Inclusive: the original's loop is `cmp edi,0x4FA400 / jle`, so the entry AT
 * this address is processed too -- 17 pointers, not 16. */
#define ADDR_SOUND_DYNAMIC_LAST  0x004FA400u
/* Bulk operations over those two tables. */
#define ADDR_FREE_DYN_SOUNDS     0x0040B800u  /* void(void) */
/* 0x0040C7A0, one caller. Free every FIXED sound slot -- ADDR_SOUND_SLOTS to
 * ADDR_SOUND_SLOTS_END at SOUND_SLOT_STRIDE, each through ADDR_FREE_SOUND and
 * then cleared -- and finish by calling ADDR_FREE_DYN_SOUNDS for the dynamic
 * ones. So it is the half of the audio teardown that owns its slots, where
 * StopAllSounds treats the same array as borrowed. Reconstructed. */
#define ADDR_FREE_WAVE_SOUNDS    0x0040C7A0u  /* void(void) */
#define ADDR_UPDATE_3D_AUDIO     0x0040BCF0u  /* void(void) */
#define ADDR_STOP_NAMED_SOUND    0x0040B860u  /* void(const char *, int32) */
#define SOUND_DYNAMIC_MAX_INDEX  0x10         /* inclusive; 17 slots */

/* The audio save section. Its tag is the only one outside the 0x0666xxxx
 * family. The saver stores, per dynamic slot, exactly the arguments it would
 * take to call PlayDynamicSound again -- looping, position, priority, owner --
 * behind a length-prefixed name, or a bare zero length if the slot is not
 * fully populated.
 *
 * Its loop bound is EXCLUSIVE: `cmp ebp,0x4FA400 / jl`, so it covers 16 slots.
 * FreeDynamicSounds uses `jle` over the same table and covers 17. The two
 * really do disagree; reproduce each as written rather than making them
 * agree. */
#define ADDR_SAVE_AUDIO_SECTION  0x0040BDF0u  /* int32_t(FILE *) */
#define ADDR_LOAD_AUDIO_SECTION  0x0040BF00u  /* int32_t(FILE *) */
#define AM2_SAVETAG_AUDIO        0x01326413u
#define SOUND_DYNAMIC_SAVED      16           /* exclusive bound, see above */
#define ADDR_STR_AUDIO_CPP      0x00474D7Cu  /* "C:\\ArmyMen2\\source\\audio.cpp" */
/* The ear -- and also the CAMERA CENTRE, which is the same point and was not
 * recorded here before. ADDR_VIEW_UPDATE derives the view rectangles from it
 * and moves it toward ADDR_VIEW_TARGET a little each frame, so the listener
 * sits wherever the view is looking. One address, two true readings, and it
 * keeps the one name it already had. Distinct from ADDR_CAMERA_X/Y, which are
 * the same idea in TILES. */
#define ADDR_LISTENER_POS        0x00514E0Cu  /* AM2_Point, the ear */
/* The ZERO POINT. It is .bss and 103 sites read it; NOTHING in the image
 * writes it, so it holds {0,0} for the life of the process and every one of
 * those readers is asking for "no position" rather than for a configured
 * default. It carried two names before this, ADDR_ZERO_POINT and
 * ADDR_ZERO_POINT, and both were taken from a call site -- audio's,
 * where a sound has neither owner nor place, and the pad centroid's. Neither
 * is wrong about its own caller and both are wrong about the global. Found
 * while reading a third user in air.cpp, which returns it for a null object. */
#define ADDR_ZERO_POINT          0x005125A0u  /* AM2_Point, always {0,0} */
/* Eight bytes on, and the same kind of thing: three call sites push it as
 * ADDR_BUILD_ROW_SET's bounding `rect` and nothing in the image writes it, so
 * it is a permanently zero AM2_Rect in .bss. */
#define ADDR_ZERO_RECT           0x005125A8u  /* AM2_Rect, always all zero */
/* 0x00456E20, one caller. Split a slot number into a band code, an index
 * within the band, and a heading byte off ADDR_SLOT_HEADINGS. */
#define ADDR_SLOT_BAND_HEADING   0x00456E20u  /* void(int32,int32*,int32*,
                                               *      uint8_t *) */
#define ADDR_SLOT_HEADINGS       0x0065A068u  /* uint8_t[], filled at runtime */
/* The three per-slot runtime tables, 64 entries each, and they TILE exactly --
 * ADDR_SLOT_HEADINGS uint8[64] runs to ADDR_SLOT_POSITIONS, which runs to
 * ADDR_SLOT_IS_VEHICLE, which runs to ADDR_TURRET_ANIMS. Four boundaries fix
 * the count at 64 independently of the `slot >= 0x40` guard in the code. */
#define ADDR_SLOT_POSITIONS      0x0065A0A8u  /* uint32[64], packed points */
#define ADDR_SLOT_IS_VEHICLE     0x0065A1A8u  /* int32[64], type == 3 */
#define AM2_SLOT_MAX             0x40
/* 0x00456EA0, three callers. Where a formation slot sits, and it CACHES its
 * own answer: the tail writes ADDR_SLOT_POSITIONS and ADDR_SLOT_HEADINGS for
 * the slot before returning. So slots resolve in order, each reading the
 * parent's cached entry, and the vehicle rule below propagates DOWN the tree
 * rather than being a local test on two objects.
 *
 * Three regimes. Slot 0 takes the leader's own position and an AngleBetween.
 * Slots 1..8 read a parent, an angle and a distance from ADDR_SLOT_RECS.
 * Slots 9 and up are procedural: ADDR_SLOT_BAND_HEADING splits the slot into a
 * band and an index, and the distance is band * AM2_SLOT_RING_STEP +
 * AM2_SLOT_RING_BASE.
 *
 * In every regime the distance DOUBLES when the slot or its parent holds a
 * vehicle -- `isVehicle[parent] || isVehicle[slot]`. */
#define ADDR_FORMATION_SLOT_POINT 0x00456EA0u /* void(slot, pos, obj, out) */
#define ADDR_SLOT_RECS           0x0048BD90u  /* 8-byte records, slots 1..8 */
#define SLOT_REC_OFF_PARENT      0u   /* int32, the slot this hangs off */
#define SLOT_REC_OFF_ANGLE       4u   /* uint8, added to the parent's heading */
#define SLOT_REC_OFF_DIST        6u   /* int16 */
#define AM2_SLOT_REC_BYTES       8u
#define AM2_SLOT_RING_STEP       48
#define AM2_SLOT_RING_BASE       32
#define ADDR_VOLUME_AT_ZERO      0x00512318u  /* int32_t, volume at no distance */
#define SOUND_REC_OFF_POS        0x10u   /* AM2_Point */
#define SOUND_REC_OFF_OWNER      0x14u   /* uid; the object making the sound */
#define SOUND_REC_OFF_ACTIVE     0x18u
#define OBJ_OFF_POS              0x12u   /* AM2_Point inside a game object */
/* The copy ADDR_OBJ_FRAME_STEP makes of it on the way in, before any type
 * handler can move the object -- so a handler that reads it sees where the
 * object was LAST frame. Written unconditionally, ahead of every guard in that
 * function, which is what makes it reliable for a handler that never runs. */
#define OBJ_OFF_PREV_POS         0x16u   /* AM2_Point */
#define SOUND_3D_CUTOFF          0x320   /* silence beyond this */
#define SOUND_3D_FALLOFF         3       /* volume lost per unit */
#define ADDR_INIT_WAVE_SOUNDS    0x0040C710u  /* int32(void) */
#define ADDR_LOAD_WAVE_SOUND     0x0040C530u  /* int32(void **slot, ds, name) */
#define ADDR_READ_WAVE_FILE      0x0040C340u  /* int32(0,name,&fmt,&data,&len,&raw) */
#define SOUND_RECORD_SIZE        0x20u    /* what a slot points at */
#define SOUND_REC_OFF_BUFFER     0x00u    /* IDirectSoundBuffer * */
#define SOUND_REC_OFF_NAME       0x04u    /* strdup of the wave's name */
#define SOUND_REC_OFF_STATE      0x08u
#define ADDR_STR_WAVE_NOMEM_DATA 0x00474D44u
#define ADDR_STR_WAVE_NOMEM_NAME 0x00474D18u
#define ADDR_STR_WAVE_NOLOAD     0x00474CF8u
#define ADDR_STR_WAVE_SEEK_END   0x00474E24u  /* "Error seeking end of file" */
#define ADDR_STR_WAVE_EMPTY      0x00474E0Cu  /* "Empty sound file %s" */
#define ADDR_STR_WAVE_PARSE      0x00474DF0u  /* "Error parsing wave file %s" */
/* 0x0040C220, one caller. Given the whole file in memory, find the format,
 * the sample bytes and their length. ReadWaveFile does the I/O and this does
 * the RIFF walking; the split is why the reader could be reconstructed long
 * before the chunk layout was read. Reconstructed. */
#define ADDR_PARSE_WAVE          0x0040C220u  /* int32(void *,fmt**,void**,DWORD*) */
#define AM2_RIFF_TAG_RIFF        0x46464952u  /* 'RIFF' */
#define AM2_RIFF_TAG_WAVE        0x45564157u  /* 'WAVE' */
#define AM2_RIFF_TAG_FMT         0x20746D66u  /* 'fmt ' */
#define AM2_RIFF_TAG_DATA        0x61746164u  /* 'data' */
#define AM2_WAVEFMT_MIN          0x0Eu        /* a shorter fmt chunk is refused */
#define ADDR_STR_WAVE_NOT_RIFF   0x00474DD0u
#define ADDR_STR_WAVE_NOT_WAVE   0x00474DBCu
#define ADDR_STR_WAVE_BAD_HDR    0x00474D9Cu
#define ADDR_STR_WAVE_NOBUFFER   0x00474CD0u
#define ADDR_STR_WAVE_NOFILL     0x00474CA4u
#define ADDR_WAVE_NAMES          0x00474360u  /* const char *[32] */
#define ADDR_WAVE_NAMES_END      0x00474440u
#define ADDR_WAVE_DIR            0x004852CCu  /* const char *, probed first */
#define ADDR_STR_WAVE_INIT_FAIL  0x00474E8Cu  /* "Unable to initialize wave %d\n" */
#define SOUND_SLOT_STRIDE        0x10u
#define SOUND_SLOT_OFF_BUFFER    0x00u   /* IDirectSoundBuffer * */
#define SOUND_SLOT_OFF_BYTES     0x04u   /* DSBCAPS.dwBufferBytes */
#define SOUND_SLOT_OFF_VOLUME    0x08u   /* what it was last set to */
#define SOUND_SLOT_OFF_STARTED   0x0Cu   /* GetTickCount when it last began */
#define SOUND_FIXED_SLOTS        0x38    /* 56 */
#define ADDR_PLAY_SOUND_AT       0x0040C040u /* void(idx,flags,?,x,y) */
#define ADDR_PLAY_DYNAMIC_SOUND  0x0040B8F0u /* void(name,loop,?,x,y,slot,pri,owner) */
/* 0x0040BFF0, 35 callers. One of a group's voice lines, at random, and only
 * when the owner is ours. It goes out on slot 0x10, which orig.h already
 * records as a voice slot.
 *
 * The groups are THIRTY 20-byte records based at ADDR_WAVE_NAMES_END -- the
 * count first, then up to four wave names. This comment said 0x00474444 and
 * four bytes late is exactly the error SpeakItemPickupLine's author (me) then
 * made independently, so it is worth saying how the base is settled rather
 * than asserted: SpeakLine divides by `[esi*4 + 0x474440]`, the record below
 * the table has four names where a late base allows one, 0x00474440 is
 * already ADDR_WAVE_NAMES_END so the two tables TILE, and at that base the
 * table is exactly thirty entries -- which is exactly OnVolumeVoice's
 * rand() % 30.
 *
 * A late base is HARD to see because it is self-consistent: name0 of group g
 * really is at 0x474444 + 20g, so every name resolves, and the field that
 * looks like the group's count is the next group's.
 *
 * Groups 0..24 hold one line each -- the item and HQ announcements. Groups
 * 25..29 hold three or four, which is why SpeakLine has a rand() % count at
 * all: announcements are fixed and reactions vary. */
#define AM2_SPEAK_AEROSOL          0 
#define AM2_SPEAK_AIRSTRIKE        1 
#define AM2_SPEAK_AUTORIFLE        2 
#define AM2_SPEAK_BAZOOKA          3 
#define AM2_SPEAK_DISGUISE         4 
#define AM2_SPEAK_EXPLOSIVES       5 
#define AM2_SPEAK_FLAKJACK         6 
#define AM2_SPEAK_FLAMETHROWER     7 
#define AM2_SPEAK_GRENADES         8 
#define AM2_SPEAK_HEAVYMACGUN      9 
#define AM2_SPEAK_M80S             10
#define AM2_SPEAK_MAGNIFYINGGLASS  11
#define AM2_SPEAK_MEDKIT           12
#define AM2_SPEAK_MINES            13
#define AM2_SPEAK_MINESWEEPER      14
#define AM2_SPEAK_MORTAR           15
#define AM2_SPEAK_PARATROOPERS     16
#define AM2_SPEAK_RECONN           17
#define AM2_SPEAK_SNIPERRIFLE      18
#define AM2_SPEAK_VULCANGUN        19
#define AM2_SPEAK_WRENCH           20
#define AM2_SPEAK_MOREAMMO         21
#define AM2_SPEAK_HQAIRSTRIKE      22
#define AM2_SPEAK_HQREINFORCEMENTS 23
#define AM2_SPEAK_HQRECONN         24
#define AM2_SPEAK_UOOH             25  /* 3 lines */
#define AM2_SPEAK_HITSSPOT         26  /* 3 lines */
#define AM2_SPEAK_AAH              27  /* 4 lines */
#define AM2_SPEAK_OVERHERE         28  /* 4 lines */
#define AM2_SPEAK_FREEZE           29  /* 3 lines */
/* 0x00448380, two callers, both of which name themselves in their own log
 * strings: TrooperPickupItem and TrooperHostApprovedPickupItem. So this is
 * what a trooper SAYS when it picks something up, and "pickup" is the
 * program's word rather than an inference from the call site.
 *
 * It dispatches through TWO tables -- the argument less 2, bounded at 0x28,
 * into a 41-entry BYTE index at 0x0044850C, then a dword jump table at
 * 0x004484C0. Reading the nineteen arms top to bottom numbers them
 * 7, 15, 2, 13, 5, 1, 0, ... so the mapping has to come from the tables. The
 * byte table is there so ids can SHARE an arm: sixteen reach the default and
 * ids 0x23..0x26 all mean Disguise. */
#define ADDR_SPEAK_ITEM_PICKUP     0x00448380u  /* void(int32 item, int32 own) */
#define ADDR_TROOPER_PICKUP_ITEM   0x00448540u  /* void(trooper, item, slot) */
#define AM2_STR_TROOPER_PICKUP     0x0048A288u /* "TrooperPickupItem %x\n" */
#define AM2_STR_TROOPER_PICKUP_2   0x0048A270u /* "TrooperPickupItem 2 %x\n" */
/* THE FIVE SPEECH GROUPS IT ASKS FOR ARE ALL ALREADY NAMED, in the
 * AM2_SPEAK_ table above -- 9, 2, 0x13, 0x15 and 0x1A are HEAVYMACGUN,
 * AUTORIFLE, VULCANGUN, MOREAMMO and HITSSPOT. I had invented five AM2_LINE_
 * names for them before grepping the VALUES; nothing would have refused those,
 * since checkoffsets only watches *_OFF_* macros. Fifth rediscovery of the
 * session and the one with the best payoff, because the existing names ANSWER
 * a question the numbers could not:
 *
 * THE THREE KINDS THE SWAP ARM HAS A LINE FOR ARE IDENTIFIED BY THAT LINE.
 * A picked-up weapon of kind 8 makes the trooper say HEAVYMACGUN, kind 10
 * AUTORIFLE and kind 0x1D VULCANGUN -- which is the same evidence a function
 * named from its own log string has. Every other kind is swapped in silence.
 * Note the kind is NOT the speech id: kind 8 speaks group 9, and group 8 is
 * GRENADES. */
#define AM2_WEAPON_KIND_HEAVYMACGUN 8
#define AM2_WEAPON_KIND_AUTORIFLE   10
#define AM2_WEAPON_KIND_VULCANGUN   0x1D
/* 0x004488C0, 608 bytes, one caller. IT NAMES ITSELF seven times:
 * TrooperHostApprovedPickupItem, the HOST half of the pickup pair whose other
 * half is ADDR_TROOPER_REMOTE_PICKUP. The host CONSUMES and ANNOUNCES --
 * DestroyByType and a SpeakLine on every arm -- where the remote respawns and
 * says nothing; and the host sets no pickup cooldown and plays no sound,
 * because the local path already did. Reconstructed. */
#define ADDR_TROOPER_HOST_APPROVED 0x004488C0u /* void(troop,item,slot,ammo) */
/* MSVC's rand, the LCG at 0x00464420 with its state in 0x0048CC1C. Named
 * here because game code that draws from it must draw from THIS one -- the
 * sequence is the image's, and libc's would leave it standing still. */
#define ADDR_SPEAK_LINE          0x0040BFF0u /* void(int32 group, int32 owner) */
#define ADDR_VOICE_GROUPS        0x00474440u /* {int32 count; const char *[4]} */
#define AM2_VOICE_GROUP_STRIDE   20
#define AM2_VOICE_SLOT           0x10
#define ADDR_VOS_DIR             0x00474D70u /* "audio\\vos" */
#define ADDR_VOLUME_VOICE        0x00512320u /* used for slots 0 and 16 */
#define SOUND_REC_OFF_OWNER_DS   0x08u   /* the IDirectSound it was made from */
#define SOUND_REC_OFF_PRIORITY   0x0Cu
#define SOUND_REC_OFF_LOOPING    0x1Cu
#define SOUND_VOICE_SLOT_HI      0x10    /* slot 16, like slot 0, is a voice */
/* 0x00457750, 21 callers. Reconstructed in src/game/army.cpp -- the name was
 * already here and is kept, being the more careful of the two readings. */
#define ADDR_LOOKUP_OWNER_OBJ    0x00457750u /* void *(uint32 owner) */
#define SOUND_DYN_OFF_BUFFER     0x00u
#define SOUND_DYN_OFF_DATA       0x04u
#define ADDR_RELEASE_SOUND_BUFS  0x0040C7D0u  /* void(void), 8 call sites */
#define ADDR_INIT_DIRECTSOUND    0x0040C800u  /* int32_t(void); 1 on success */
#define ADDR_SET_STREAM_VOLUME   0x0040CE90u  /* void(int32 pan) */
#define ADDR_DIRECTSOUNDCREATE   0x00463390u  /* jmp [0x0046F01C] */
/* Three consecutive dwords, one object each, and our own InitDirectSound is
 * what settles which is which: DirectSoundCreate fills the first,
 * CreateSoundBuffer the second, and QueryInterface(IID_IDirectSound3DListener)
 * the third. They carried a second set of names -- ADDR_DSOUND_BUF_A/B/C --
 * taken from the SHAPE of ReleaseSoundBuffers, three Releases in a row, rather
 * than from what it releases. It releases one buffer, one listener and the
 * DEVICE. Those names are gone, and so is ADDR_MOVIE_DSOUND, which was a third
 * name for the first of these. */
#define ADDR_DSOUND              0x004FA46Cu  /* IDirectSound * */
#define ADDR_DS_PRIMARY          0x004FA470u  /* the primary sound buffer */
#define ADDR_DS_LISTENER         0x004FA474u  /* IDirectSound3DListener * */
#define ADDR_IID_DS3D_LISTENER   0x0046F3E8u
#define ADDR_STREAM_VOLUME       0x0051231Cu  /* int32_t, the wanted volume */
#define ADDR_AUDIO_TIMER_ID      0x004FA408u
#define ADDR_AUDIO_TIMER_RUN     0x004FA464u
#define ADDR_AUDIO_PERIOD        0x004FA448u  /* divided by 4 for timeBeginPeriod */
#define ADDR_AUDIO_IN_CALLBACK   0x004FA478u  /* set while the refill is running */
/* StartAudioStream's second argument, and RefillAudioBuffer shows what it is:
 * non-zero means rewind and keep going at the end of the file. */
#define ADDR_AUDIO_LOOPING       0x004FA45Cu
#define ADDR_REFILL_AUDIO        0x0040CD20u  /* void(void) */
/* The streaming buffer's size in bytes. It went in as ADDR_AUDIO_REFILL_BYTES
 * because RefillAudioBuffer hands it to Lock as the byte count -- which it does
 * only because that one fills the whole buffer in a single go. AudioTimerProc
 * uses it as the wrap modulus and as "how much to rewrite after a restore",
 * which is what it actually is. Another name taken from one call site. */
#define ADDR_AUDIO_BUFFER_SIZE  0x004FA444u  /* uint32_t */
#define ADDR_AUDIO_READ_FAILED   0x004FA458u
#define ADDR_AUDIO_AT_END        0x004FA460u
#define ADDR_AUDIO_VALID_BYTES   0x004FA454u  /* good data before the silence */
#define ADDR_AUDIO_DATA_CHUNK    0x004FA418u  /* MMCKINFO, the `data` chunk */
#define ADDR_AUDIO_RIFF_CHUNK    0x004FA42Cu  /* MMCKINFO, the RIFF */
#define ADDR_AUDIO_CURSOR_A      0x004FA450u  /* both cleared after a refill */
#define ADDR_AUDIO_CURSOR_B      0x004FA44Cu
#define ADDR_AUDIO_HMMIO         0x004FA414u  /* HMMIO, closed by WaveCloseReadFile */
#define ADDR_AUDIO_WAVEFORMAT    0x004FA410u  /* WAVEFORMATEX * */
/* The streaming refill callback. RECONSTRUCTED as AudioTimerProc in
 * src/game/win32/audio.cpp, but registered rather than patched -- StartAudioStream's
 * timeSetEvent is the only reference to this address in the image, and that
 * call is ours, so a detour here would be jumped to by nobody. Kept as an
 * address because the harness fingerprints it and because a probe may want to
 * hand the original back to timeSetEvent. */
#define ADDR_AUDIO_TIMER_PROC    0x0040D020u  /* LPTIMECALLBACK */
/* Opens the .WAV and creates the streaming buffer -- reconstructed as
 * OpenAudioStream. It went in as ADDR_AUDIO_PREPARE, from the one call site
 * in StartAudioStream, before anyone read the body. */
#define ADDR_OPEN_AUDIO_STREAM   0x0040CED0u  /* int32_t(const char *) */
/* Prefixes the install directory at 0x0051235C onto a relative path and answers
 * whether it is there. 82 callers and nothing audio-specific about it -- it was
 * ADDR_AUDIO_CHECK_PATH, named from the first call site it was seen at, which
 * is the mistake CLAUDE.md warns about. Stays original. */
#define ADDR_AUDIO_PATH_ARG      0x004852D4u

/* ---- Smacker video ----------------------------------------------------
 *
 * smackw32.dll has no SDK header and no import library, so its entry points are
 * reached through the game's own IAT slots -- the only place their addresses
 * exist. Reconstructed in src/game/win32/movie.cpp; the movie object is thiscall and
 * deliberately opaque, with only the fields actually touched named there.
 */
#define ADDR_MOVIE_STOP          0x00445120u  /* thiscall void(this) */
#define ADDR_MOVIE_SET_VOLUME    0x00445280u  /* thiscall void(this, int32) */
#define ADDR_MOVIE_VTABLE        0x0046FAB4u  /* stamped into the object */
#define ADDR_MOVIE_SOUND_READY   0x006598A8u  /* int32_t; set once Smacker has sound */
#define ADDR_MOVIE_OPEN          0x00444FC0u  /* thiscall this(this,name,w,h,big) */
#define ADDR_MOVIE_MAKE_SURFACE  0x00445690u  /* surface *(w, h), stays original */
#define ADDR_IAT_SMACK_OPEN      0x0046F2C8u
#define ADDR_IAT_SMACK_DDTYPE    0x0046F2CCu
#define ADDR_IAT_SMACK_USE_DSOUND 0x0046F2C4u
#define ADDR_MOVIE_DRAW_FRAME    0x004453C0u  /* thiscall void(this, arg) */
#define ADDR_MOVIE_FINISHED      0x00445600u  /* void(void); posts WM_USER */
#define ADDR_MOVIE_START         0x004451F0u  /* thiscall int32(this, arg) */
/* Lives inside what docs/functions.tsv reports as one 160-byte function at
 * 0x00445320 -- the inventory merged the two. It is a separate function. */
#define ADDR_MOVIE_POLL          0x00445390u  /* thiscall int32(this) */
#define ADDR_IAT_SMACK_WAIT      0x0046F2BCu
#define ADDR_MOVIE_APPLY_PALETTE 0x00445320u  /* thiscall void(this, surface) */
#define ADDR_MOVIE_CURRENT       0x006568A0u  /* the movie being played, or null */
/* A SECOND movie pointer, and not the same one: ADDR_STATE_LEAVE owns this
 * and MovieForget clears the other. Whatever put a movie here is responsible
 * for stopping and deleting it when the state is left. */
#define ADDR_STATE_MOVIE         0x00515F98u  /* the state's movie, or null */
/* The four one-liners around ADDR_MOVIE_CURRENT. Between them they are the
 * whole of how the rest of the game touches a playing film: one sets it, one
 * steps it every frame in states 0 and 3, and two put it down. */
#define ADDR_MOVIE_SET_CURRENT   0x00445620u  /* void(void *movie) */
#define ADDR_MOVIE_END_CURRENT   0x00445650u  /* void(void) */
#define ADDR_MOVIE_FORGET        0x00445670u  /* void(void) */
#define ADDR_MOVIE_TIMER_PROC    0x004455E0u  /* the timer callback, stays original */
#define ADDR_GAME_DELETE         0x004648F5u  /* the game's own operator delete */
/* Posted to the window to advance the game state machine: 0x400 when a movie
 * finishes, 0x402 when one could not be started. Both land in the same handler,
 * which is why src/game/win32/winproc.cpp forwards them together. */
#define AM2_WM_STATE_ADVANCE     0x0400u
#define AM2_WM_STATE_ABORT       0x0402u
#define ADDR_MOVIE_PALETTE_OWNER 0x00477A58u  /* void **; +0x800 is a DD palette */
#define ADDR_IAT_SMACK_TO_BUFFER 0x0046F2B0u
#define ADDR_IAT_SMACK_DO_FRAME  0x0046F2B4u
#define ADDR_IAT_SMACK_NEXT_FRAME 0x0046F2B8u
#define ADDR_IAT_SMACK_CLOSE     0x0046F2C0u
#define ADDR_IAT_SMACK_VOLUMEPAN 0x0046F2ACu

/* .WAV reading through WINMM's multimedia file services -- the only file I/O
 * in the game that does not go through the CRT. Reconstructed in
 * src/game/win32/wavefile.cpp; these are the DirectX SDK sample's names. */
#define ADDR_WAVE_OPEN_FILE      0x0040CA10u  /* MMRESULT(char*,HMMIO*,WAVEFORMATEX**,MMCKINFO*) */
#define ADDR_WAVE_START_DATA     0x0040CBB0u  /* MMRESULT(HMMIO*,MMCKINFO*,MMCKINFO*) */
#define ADDR_WAVE_READ_FILE      0x0040CBF0u  /* MMRESULT(HMMIO,uint32,uint8*,MMCKINFO*,uint32*) */
#define ADDR_WAVE_CLOSE_FILE     0x0040CCE0u  /* MMRESULT(HMMIO*,WAVEFORMATEX**) */

/* Error reporting. Both format into the game's own static buffers and put a
 * message box up; both return 0, which is what lets callers `return` them. */
#define ADDR_FATAL_ERROR         0x0041E750u  /* int32_t(const char *fmt, ...) */
#define ADDR_ERROR_TEXT          0x0050B5E0u  /* char[], the formatted message */
#define ADDR_ERROR_TEXT_DD       0x0050B1E0u  /* char[], the same with an HRESULT */
#define ADDR_VSPRINTF            0x00465A45u  /* the game's own CRT vsprintf */

/* ---- application, window and message loop -----------------------------
 *
 * The outermost layer of the process: WinMain parses the command line, brings
 * up the window and DirectDraw, then runs a PeekMessage loop that ticks a frame
 * whenever the queue is empty.
 *
 * 0x00507344 is the single most useful global here. `-w` sets it, and it gates
 * every windowed-mode behaviour in the game -- the window gets a border and is
 * repositioned, DirectDraw asks for a palettized primary, and CalibratePalette
 * runs. Without it the game is fullscreen and none of that happens, which is
 * why CalibratePalette never fires under the harness as it is normally driven.
 */
#define ADDR_WIN_MAIN            0x0040B360u  /* int32 __stdcall(inst,prev,cmd,show) */
#define ADDR_INIT_APPLICATION    0x0040B600u  /* int32_t(HINSTANCE, int32_t nCmdShow) */
#define ADDR_PUMP_MESSAGE        0x0040B280u  /* int32_t(MSG *) -- 0 on WM_QUIT */
#define ADDR_POSITION_WINDOW     0x0040B070u  /* void(void) */
/* The window procedure. Reconstructed in src/game/win32/winproc.cpp, but NOT patched:
 * it is reached only through the WNDCLASS field that InitApplication fills in,
 * so pointing that at our own leaves the original intact and callable. The
 * messages that are pure comm and game logic are forwarded straight back to it
 * rather than reconstructed. Nothing else in the image refers to this address. */
#define ADDR_WND_PROC            0x0040A6B0u  /* LRESULT CALLBACK(HWND,UINT,WPARAM,LPARAM) */

/* State the window procedure reads. */
#define ADDR_DIRECTDRAW          0x004FDF78u  /* IDirectDraw * */
#define ADDR_PAINT_OBJECT        0x0065A058u  /* see winproc.cpp -- not COM */
/* 0x0044DB90, one caller. The same three instructions widget.cpp's five screen
 * factories open with, as a function of its own: delete whatever is in
 * ADDR_PAINT_OBJECT through vtable slot 0 with the scalar-delete flag, and
 * clear the global -- inside the test, so a null one is left alone rather than
 * written. Reconstructed. */
#define ADDR_CLOSE_SCREEN        0x0044DB90u  /* void(void) */
/* The two text fields of the ENTER BATTLE NAME dialog, inside the paint
 * object: the session's name and the hosting player's. */
#define DLG_OFF_BATTLE_NAME      0x064u
#define DLG_OFF_PLAYER_NAME      0x084u
/* Where HostBattle keeps the two names after the session is up. */
#define ADDR_SAVED_PLAYER_NAME   0x00516094u  /* char[] */
#define ADDR_SAVED_BATTLE_NAME   0x005160D5u  /* char[] */
/* ADDR_SAVE_OPTIONS is already defined further down, from the menu side --
 * checkoffsets refused the second one. The strings and constants it needs are
 * here because this is where the file's CONTENTS are described. */
#define ADDR_STR_OPTIONS_CFG     0x0048B448u  /* "Options.cfg" */
#define ADDR_MODE_W              0x0048B3CCu  /* "w" -- TEXT mode, not "wb" */
#define ADDR_STR_OPTIONS_NOWRITE 0x0048B420u
#define ADDR_CRT_CHMOD           0x00466359u  /* int32(const char *, int32) */
#define AM2_CHMOD_RW             0x180        /* _S_IREAD | _S_IWRITE */
#define ADDR_CRT_FFLUSH          0x00465A96u  /* int32(FILE *) */
#define ADDR_KEY_BINDINGS_END    0x004854E6u  /* the write loop's bound */
#define ADDR_HOST_BATTLE         0x0042FFF0u  /* void(void) */
#define ADDR_APP_ACTIVE          0x004FA02Cu  /* int32_t; RunFrame ticks only if set */
#define ADDR_CHAR_HANDLER        0x005125B8u  /* void(*)(wparam, lo, hi), may be null */
#define ADDR_GAME_STATE          0x00511DA4u  /* int32_t, 0..4 */
/* The values, measured by a probe in PollKeyboard: 0 at startup, 1 on the
 * menus, 2 in a mission. The screen factories test for 2 and nothing else. */
#define AM2_STATE_MENU           1
#define AM2_STATE_MISSION        2
#define ADDR_GAME_STATE_ARG      0x00511DB4u  /* int32_t */
/* The per-game-over-state ACTION TABLE, and the base is 0x0048654C rather than
 * the 0x00486550 this used to carry. Both are real: the record is 12 bytes and
 * TWO of its three fields are function pointers, so the two call sites in the
 * image read different COLUMNS of one table.
 *
 *   0x0040A9F0, in WndProc:      call [eax*4 + 0x486550]   -- column 1
 *   0x00426623, in StateEnter0:  call [edx*4 + 0x48654C]   -- column 0
 *
 * Both scale the index the same way, `lea eax,[eax+eax*2]` then `*4`, so the
 * stride is 12 and the difference is exactly one field. Naming 0x00486550 the
 * table and calling its first dword "a function" worked only because winproc
 * never read any other field; it put the base one column late, which is the
 * same shape of error as a mis-centred trig table.
 *
 * And the two callers are not independent: WndProc's arm is guarded on the
 * game state being 0, which is the very state StateEnter0 is entering. So one
 * record holds what to do on entering state 0 and what to do when a message
 * arrives while in it. */
#define ADDR_STATE_ACTIONS       0x0048654Cu  /* AM2_StateAction[] */
/* One definition, because BOTH readers were reaching this table and only one
 * of them had a struct for it. */
typedef void (__cdecl *am2_state_action_fn)(void);
typedef struct {
    am2_state_action_fn onEnter;    /* StateEnter0 calls this */
    am2_state_action_fn onMessage;  /* WndProc calls this, while state == 0 */
    uint32_t            id;
} AM2_StateAction;
#define ADDR_ON_APP_ACTIVATED    0x004269B0u  /* void(void) */
/* Three of the four functions in the 96-byte run around ADDR_GAME_OVER_STATE
 * were already named -- ADDR_CLEAR_GAME_OVER at 0x0042E580, reconstructed as
 * ClearGameOver in winmain.cpp, and ADDR_SET_GAME_OVER at 0x0042E5A0. So the
 * getter here is GameOverState and not, as it nearly went in, CurrentEndState.
 *
 * The one thing not settled is what the index MEANS: winproc.cpp uses this
 * answer to select an entry of ADDR_STATE_ACTIONS, while the family calls it
 * game over. An end-of-mission outcome that picks an end screen would be both;
 * nothing here decides it, and the names describe the record rather than what
 * it is for. */
#define ADDR_STATE_LEAVE_ALIAS   0x0042E590u  /* void(void), one jmp */
#define ADDR_GAME_OVER_SAVED     0x00515F8Cu  /* int32_t[3], beside the state */
#define ADDR_GAME_OVER_SOURCE    0x00511E14u  /* int32_t[3], where they come from */
#define ADDR_CURRENT_STATE       0x0042E5D0u  /* int32_t(void), indexes the above */
#define ADDR_STATE_LEAVE         0x0042E720u  /* void(void) */
/* The game is a five-state machine driven by RunFrame, and changing state is
 * two functions rather than one -- which the old name ADDR_STATE_ENTER hid.
 *
 * REQUEST stores the wanted state and raises a pending flag; it does NOT change
 * the current state. COMMIT, a separate function, moves the pending state into
 * the live one and lowers the flag.
 *
 * That flag is what makes the level teardown reachable: the state-2 handler at
 * ADDR_STATE2_FRAME checks it and tail-jumps to ADDR_LEVEL_TEARDOWN, so the
 * teardown runs when a transition is
 * requested while the game is ALREADY in state 2 -- that is, on leaving a
 * level. It is the only route to StopAllSounds, which is why no amount of
 * entering Boot Camp reaches it: entering is a transition INTO the state, and
 * only leaving triggers the teardown. */
#define ADDR_REQUEST_STATE       0x00424AD0u  /* void(int32_t) */
#define ADDR_COMMIT_STATE        0x00424AF0u  /* void(void) */
#define ADDR_STATE_PENDING       0x00511DACu  /* int32_t, a change is wanted */
#define ADDR_STATE_WANTED        0x00511DB0u  /* int32_t, -1 when none */
/* The other way the pending flag goes up, and the one that matters for the
 * teardown: 0x00425EE0 consumes a MENU REQUEST -- the same ADDR_MENU_REQUEST /
 * ADDR_MENU_REQUEST_SET pair StartSelectedGame and HostBattle write -- moves
 * the code to 0x00511DBC and raises the flag. So the in-game route to the
 * level teardown is a menu request raised while the game is in state 2. */
#define ADDR_TAKE_MENU_REQUEST   0x00425EE0u  /* void(void) */

/* ---- the mission-script interpreter --------------------------------------
 *
 * The game's missions are readable text under data/<map>/, and the engine
 * parses them at load. That makes this subsystem the one part of the
 * reconstruction whose names can be taken from the program's own vocabulary
 * rather than invented: docs/scripttokens.md lists all 141 keywords, and the
 * interpreter's own log strings say what it is doing.
 *
 * The chain, from WinMain down:
 *
 *   WinMain -> RunFrame -> ADDR_STATE2_FRAME -> ADDR_LOAD_LEVEL_SCRIPT
 *     -> ADDR_READ_SCRIPT -> ADDR_SCRIPT_NEXT_TOKEN
 *       -> ADDR_SCRIPT_NEXT_TOKEN -> IsBlank, IsScriptDelim
 *
 * ADDR_LOAD_LEVEL_SCRIPT builds "<map><n>.txt" when the level index is
 * positive and "<map>.txt" otherwise -- kitchen1.txt, kitchen2.txt -- resets
 * the context, declares the five score variables the scripts can read, and
 * parses. It logs "reading script %s:" and then "worked!" or "FAILED!", which
 * is how these three are named rather than guessed.
 *
 * The two already-reconstructed helpers at the bottom of that chain were
 * ported from their bodies alone, before any of this was known: IsBlank is
 * space/tab/CR and IsScriptDelim accepts exactly the first character of each
 * of tokens 1..13. They are the lexer's character classes. */
#define ADDR_LOAD_LEVEL_SCRIPT   0x00425060u  /* void(void) */
#define ADDR_READ_SCRIPT         0x00444CD0u  /* int32_t(const char *, ctx *) */
/* Parse one typed line and run it -- the other caller of the tokeniser, and
 * NOT part of the ReadScript path, which was recorded here wrongly for several
 * commits. ReadScript tokenises with NextToken directly and never calls this.
 *
 * It names itself nowhere, so it has no name here either; the earlier
 * `ParseLine` was invented, like `ParseScriptFile` before it. What it is for
 * comes from its only caller instead: 0x00417B80 carries "Cheat!!!",
 * "I am the Juggernaut!", "I can fly!" and "Aye aye Captain!", so the typed
 * line is a cheat code and this is what executes one. */
#define ADDR_SCRIPT_RUN_LINE     0x00444C40u
/* The cheat runner. It matches the typed line against a table of 41 words at
 * ADDR_CHEAT_WORDS and dispatches through a 39-entry jump table; anything it
 * does not recognise falls through to ADDR_SCRIPT_RUN_LINE, which is why that
 * function is reachable at all. Entry 0, "when all else fails...", is the
 * master switch and is compared separately before the table is walked.
 *
 * THE 41ST ENTRY IS "xxx" AND NOTHING CAN MATCH IT. The walk starts at
 * ADDR_CHEAT_WORDS+4 and its bound is `cmp edi, 0x004767A4; jl`, so the last
 * pointer compared is 0x004767A0 -- "patton's speach" -- and 0x004767A4,
 * which really does hold a pointer to the string "xxx", is where the loop
 * STOPS rather than an entry it tests. So the table is 1 master + 39 cheats
 * + 1 sentinel, and typing xxx does nothing.
 *
 * That is why the declaration says [41] while the prose above it said 40:
 * both were counting something real and neither said which. Dumping the
 * table settled it in one command, and it is the same lesson the rest of
 * this function taught four times -- decode the whole thing rather than
 * reason about its ends.
 *
 * AND `mov ecx, [ADDR_ARMY_TABLE]` IN THE MASTER ARM IS NOT A DEAD LOAD.
 * It sits between the argument push and the call and nothing in the arm reads
 * ecx afterwards, which is exactly what a dead load looks like -- and
 * ADDR_COMM_ARMY_OF_SLOT is THISCALL, so ecx is `this`. The one-line note
 * that had been drafted calling it unused would have been the same mistake
 * CLAUDE.md records for CreateVehicle, where forgetting the convention made
 * the whole function unreadable. The prototype in this file said `thiscall`
 * all along; the arm was read before the callee's declaration was.
 *
 * AND THE "Cheat!!!" REPLY'S COLOUR IS BYTE 1 OF AN OBJECT-TABLE RECORD,
 * which the grep-the-address rule caught before a name was invented for it.
 * The master arm does `CommArmyOfSlot(g_defaultOwner)`, shifts the answer
 * left by 8 and reads `[eax + 0x004F9ACD]`. A stride of 0x100 and a base one
 * past a round number is precisely the ADDR_ARMY_INK shape this file already
 * records: 0x004F9ACC is ADDR_OBJ_TABLE_RECORDS and AM2_OBJ_TABLE_REC_SIZE is
 * already 0x100, so the read is byte 1 of the army's own record and NOT a
 * table of its own. Writing it as a new base would have been the fifth
 * instance of the commonest mistake in this project.
 *
 * Two of the arms are what identified the fog of war -- see the block at
 * OBJ_FLAG_REVEALED. Word 3 is "spidey senses tingling", which prints
 * "I see everything!" and reveals every object; word 4 is "moleman", which
 * prints "I bury my head 'neath the sand." and conceals them. A cheat word
 * naming what its handler does is the strongest kind of evidence in this
 * image, and it is worth checking this table before inventing a name for
 * anything it reaches. */
/* HOW TO REACH IT, half-solved. Binding index 0x13 -- scancode 0x0E,
 * BACKSPACE by default -- opens the console: 0x004186B3 tests that key and,
 * when it is newly pressed, installs 0x004185C0 as ADDR_CHAR_HANDLER.
 * Verified in a live Boot Camp mission by tapping `key 0x0e tap` and reading
 * the global back: it holds 0x004185C0 afterwards and 0 before.
 *
 * What does NOT work yet is typing into it. `ctl "type ..."` posts the
 * WM_CHARs the handler should accumulate and `\r` posts the 0x0D that
 * 0x0041864C tests for, and ScriptRunLine's counter stays at 0 -- so the line
 * never reaches ADDR_SCRIPT_RUN_LINE and the cheat table is still undriven.
 * Somewhere between the handler and the submit the characters are being
 * dropped; that is where to look next.
 *
 * The cheats are also gated on ADDR_MP_SESSION being zero, so they are
 * single-player only -- 0x0041859F is inside that test. */
/* SURVEYED AND NOT RECONSTRUCTED. 2,304 bytes, 833 instructions, and its
 * content is a TABLE: it walks 39 phrase pointers from 0x00476708 to
 * 0x004767A4 comparing the typed line against each, then dispatches through a
 * 39-entry jump table at 0x004183E4. Thirty-nine phrases, thirty-nine distinct
 * arms, one to one. docs/cheats.md is generated from both by tools/cheats.py.
 *
 * THE ARMS ARE NOT IN DISPATCH ORDER, AND THE REASON IS THE ARM ITSELF. Entry
 * 31, "god of gamblers", is at 0x00417C3A -- BEFORE the jump instruction and
 * ahead of entry 0 -- because it JUMPS BACK INTO THE DISPATCH: it prints "No
 * luck whatsover.", rolls GameRand() % 0x27, and if the result is in range
 * re-enters the table with it. A cheat that fires a random cheat, which is why
 * the compiler put its body where the backward branch is cheapest.
 *
 * So reading the bodies from the top and numbering as you go gives "santini"
 * the wrong one -- the thirty-nine-way version of what DirtyCollect's
 * eighty-one arms and WeaponClassOf's four both cost -- and the arm that
 * causes it is also the one arm that can reach every other.
 *
 * NINETEEN OF THE THIRTY-NINE SHARE A TAIL, and the nineteenth is the one
 * that OWNS it. Entries 11 through 28 are the item-granting cheats: each
 * pushes SEVEN dwords of its own -- a kind, the caller's position, a -1 and
 * four zeros -- and jumps to 0x0041827C, which pushes an item key, calls
 * KeyLookupTriple, then pushes three more and calls 0x0045F0C0 before the one
 * epilogue they all use.
 *
 * 0x0041827C IS NOT A HELPER. It is the middle of arm 29, `rubber cement`,
 * which starts at 0x0041825C, pushes its own seven dwords ending with
 * `push 0x17` at 0x0041827A, and then simply FALLS INTO 0x0041827C because
 * the two are adjacent. So arm 29 is a nineteenth user with kind 0x17 and
 * ammo -1, and it is the only one that reaches the code without a jump.
 *
 * That is exactly the failure CLAUDE.md already records twice as AN ARM THAT
 * ENDS INSIDE ANOTHER -- UnitKindMatches kind 3 jumping into kind 4, and
 * PlacementAllowed arm 16 jumping into arm 15. Here it is the other way
 * round: nothing jumps OUT of arm 29, eighteen other arms jump INTO it, and
 * a scan for `jmp 0x41827c` finds eighteen and misses the arm the address is
 * physically inside. Searching for the jump finds the callers; only decoding
 * the bytes ABOVE the target finds the owner.
 *
 * So the arm's arguments are built ACROSS TWO CALLS -- the shape CLAUDE.md
 * records for CreateVehicle, where pairing each push with the nearest call
 * gets both wrong. Seven pushed in the arm, three in the tail, three consumed
 * by the first call and 0x20 cleaned at the end.
 *
 * So they are the eighteen cheats that hand the player a weapon, and the tail
 * is one CreateWeapon call reached eighteen ways.
 *
 * AND THE NINETEEN ARE ONE HELPER WITH TWO PARAMETERS. Extracted side by
 * side their pushes are identical bar two slots: a KIND (2, 3, 4, 0x1D, 0xB,
 * 0xC, 0x14, 0x24, 0x26, 0x25, 0x1C, 0x27, 0x28, 0x2A, 0x1E, 0x18, 0x1A,
 * 0x19, and 0x17 for arm 29) and an AMMO count, which is -1 for eleven of
 * them and 0x2C, 0xD, 0xC or 3 for the rest. Everything else -- four zeros
 * and the caller's position -- is the same in all nineteen. So the
 * transcription is one helper and nineteen one-line cases, which is what
 * reading them side by side buys over reading them in sequence.
 *
 * THE COLOUR BYTES ARE COLOURS, and reading HudMessage settled it in one
 * look. There are TEN of them, not the six this note claimed one commit ago:
 * the six were what a partial sample of the arms happened to touch, and
 * decoding all thirty-nine adds 0x00502AD9, 0x00507234, 0x00502CE5 and
 * 0x0050712C. Six carry five or six arms each and four carry one or two.
 * A count taken from some of the arms is a count of those arms.
 * Its second argument is an int32 the function uses as `(uint8_t)colour`, so
 * the upper three bytes are dead and the arms' `mov cl, byte ptr [global]`
 * into an otherwise untouched register is not sloppiness -- it is the only
 * part that is read.
 *
 * Four of the ten globals were already named colours: ADDR_COLOUR_LAG_MID,
 * ADDR_COLOUR_WHITE, ADDR_COLOUR_STALE and ADDR_VIEW_RECT_COLOUR. So each
 * cheat's reply is printed in a palette index the game keeps somewhere else,
 * and the arms differ in which. 0x004FDF7C and 0x004FD760 are the two that
 * still have no name.
 *
 * The open question was worth writing down rather than guessing past: this
 * function is COLD -- nothing types a cheat on any drive -- so a wrong second
 * argument would never have surfaced, and the reading that made the arms
 * easiest to write happens to be the right one only because HudMessage says
 * so.
 *
 * Counting epilogues is what produced the wrong claim: thirty-nine arms and
 * twenty-one `ret`s, and eighteen `jmp`s that look like eighteen more until
 * the target is decoded. The only control flow between arms is that plus the
 * one backward jump. The failure the
 * no-match path at 0x00417C60 shares with it -- both reach ScriptRunLine --
 * is a fallthrough and not a jump: an unmatched line is handed to the script
 * runner, which is how "trigger greenwins" works without being a cheat arm at
 * all.
 *
 * tools/espmap.py reports six references in unreached code here, and the
 * indirect jump is why: it follows branches and calls, not jump tables. That
 * is a limitation worth knowing rather than a decode failure -- the arms are
 * reachable, just not from where the walker can see.
 *
 * All twenty-one of its callees are named. Two of the cheats run SCRIPT LINES
 * through ADDR_SCRIPT_RUN_LINE -- "trigger greenwins" and "trigger tanwins" --
 * so the cheat layer and the mission scripts meet here. */
#define ADDR_CHEAT_ENTRY         0x00417B80u
#define ADDR_CHEAT_ENABLED       0x004FCF94u  /* int32_t, "when all else fails..." */
#define ADDR_CHEAT_WORDS         0x00476704u  /* const char *[41] */
#define ADDR_CHEAT_JUMP_TABLE    0x004183E4u  /* void *[39], indexed word - 1 */
#define ADDR_SCRIPT_NEXT_TOKEN   0x0043F450u  /* the tokeniser; stops at // */
#define ADDR_SCRIPT_RESET        0x0043F2F0u  /* void(ctx *) */
#define ADDR_SCRIPT_LOOKUP_TOKEN 0x0043EEE0u  /* int32_t(const char *word) */

/* Where the ORIGINAL's keyword table is: 185 {const char *, int32_t} pairs,
 * the bounds being ScriptLookupToken's own -- it walks from the first and
 * stops at the second. The reconstruction no longer reads it; the table is
 * written out in game/scripttokens.h and generated from here, so these stay
 * as the record of where it came from and as what tools/scripttokens.py
 * reads. Nothing in the image writes it. */
#define ADDR_SCRIPT_TOKENS       0x00487C90u
#define ADDR_SCRIPT_TOKENS_END   0x00488258u
#define ADDR_SCRIPT_ADD_TOKEN    0x0043F370u  /* void(ctx, kind, value, line) */
#define ADDR_SCRIPT_WORD_BUF     0x00656354u  /* char[0x40], the scratch word */
#define ADDR_SCRIPT_FREE_TOKEN   0x0043F000u  /* void(token *) */
#define ADDR_SCRIPT_GROW_TOKENS  0x0043F340u  /* void(ctx *) */
#define ADDR_SCRIPT_PARSE_NUMBER 0x0043EF70u  /* int32_t(const char *, int32_t *, float *) */
#define ADDR_SCRIPT_TOKEN_NAME   0x0043EF40u  /* const char *(int32_t id) */
#define ADDR_SCRIPT_FIND_NAME    0x0043F670u  /* int32_t(const char *) */
#define ADDR_SCRIPT_TOKEN_TEXT   0x00444A90u  /* char *(tok *, char *out) */
#define ADDR_SCRIPT_IS_STMT      0x00444B80u  /* int32_t(ctx *, int32_t *at) */

/* The script's name table -- declared variables and objects. Sixteen bytes an
 * entry, the first field a `char *`. Written at runtime, so the offline test
 * cannot reach it; only the in-process check can. */
#define ADDR_SCRIPT_NAME_CAP     0x00656460u
#define ADDR_SCRIPT_NAME_COUNT   0x00656464u
#define ADDR_SCRIPT_NAMES        0x00656468u
#define AM2_SCRIPT_NAME_SIZE     0x10u
#define ADDR_SCRIPT_ADD_NAME     0x0043F7A0u  /* AddNameTableName */

/* The name table's value accessors, all three named by their own messages.
 * The two SetVarValue addresses say the same name because they are almost
 * certainly two overloads of it -- one taking an index, one a name. */
#define ADDR_GET_VAR_VALUE       0x00443E40u  /* int32_t(int32_t, int32_t *) */
#define ADDR_SET_VAR_VALUE       0x00443E90u  /* int32_t(int32_t, int32_t) */
/* 0x00443F10, one caller. Read a script variable, add to it, write it back --
 * and answer whether both halves succeeded. */
#define ADDR_ADD_TO_VAR          0x00443F10u  /* int32_t(int32_t, int32_t) */
#define ADDR_SET_VAR_BY_NAME     0x00443ED0u  /* int32_t(const char *, int32_t) */
#define ADDR_SCRIPT_ALLOC_UID    0x0041E7F0u  /* int32_t(void) */
#define ADDR_NEXT_UID            0x00511DF4u

/* The seven kind names, indexed by kind -- "Unknown", "Control Character",
 * "Reserved", "Integer", "Float", "String", "Name". The handlers pass entries
 * of this array straight into their "expected token of type %s" message. */
#define ADDR_SCRIPT_KIND_NAMES   0x00487C74u

/* A declared name's type, as AddNameTableName takes it. 0 allocates a fresh
 * uid; 1..3 store the one passed. Anything else is an error and stores it
 * anyway. Type 3 is what `variable` declares. */
#define AM2_NAME_TYPE_OBJECT     0   /* `object`; takes a fresh uid   */
#define AM2_NAME_TYPE_PAD        1   /* `pad`                         */
#define AM2_NAME_TYPE_REF        2   /* a name used before declaring  */
#define AM2_NAME_TYPE_INTEGER    3   /* `variable`                    */
/* 0x0040F960, thiscall, three callers. One army's score: the slot's army
 * colour picks one of the four ADDR_SVAR_*SCORE name indices and GetVarValue
 * resolves it. Name ours; the four globals it indexes already had theirs.
 *
 * It zeroes its local BEFORE calling, and that is load-bearing: GetVarValue
 * writes through the pointer for a non-positive index and for a variable, and
 * NOT for a name that is not a variable -- that path complains and leaves the
 * caller's memory alone. */
#define ADDR_GET_ARMY_SCORE      0x0040F960u  /* thiscall int32(this, slot) */

/* Whether this is a multiplayer session, and which end of it. Zero is single
 * player; four places write it and only ever 1 or 2 -- the MULTI-PLAYER host
 * screen (0x004317C0, "Host is ready - waiting for players") writes 1, the
 * join screen (0x00433480) writes 2, and host migration writes 1 because you
 * have just become the host.
 *
 * Both readers this project has traced only test it against zero:
 * LoadLevelScript reads the rules and per-army AI scripts when it is set, and
 * ReadScript prints its summary when it is not. Ninety more sites read it, so
 * treat 1-versus-2 as unsettled -- the title screen also writes 2, which a
 * plain host/client reading does not explain, and nothing here needed to know.
 *
 * The two names this address carried before, ADDR_SCRIPT_QUIET and
 * ADDR_COMM_HOST_CHANGED, were each a guess from one call site. */
#define ADDR_MP_SESSION          0x00511DA0u
#define AM2_MP_SESSION_HOST      1
#define AM2_MP_SESSION_JOIN      2

/* The menu SCREEN FACTORIES. RunFrame's menu-request handler dispatches
 * through a 21-entry jump table at 0x00426518; each arm is seven bytes,
 * `call <factory>; jmp end`, and each factory opens one screen. See
 * src/game/win32/screens.h for the shape they share. */
#define ADDR_OPEN_MP_HOST        0x004317C0u  /* void(void) */
#define ADDR_OPEN_MP_JOIN        0x00433480u  /* void(void) */
#define ADDR_OPEN_MP_OPTIONS     0x00432910u  /* void(void) */
/* The host and join panels are ONE class -- both factories allocate 0x278 and
 * call the same constructor, differing only in backdrop and role. */
#define ADDR_MP_PANEL_CTOR       0x00430530u  /* thiscall obj *(obj, bmp) */
/* SURVEYED, and it is the last entry in docs/functions.tsv below the CRT.
 * 4,512 bytes, ONE exit (`ret 4`, so thiscall with a single stack argument),
 * THIRTY distinct callees of which one is unnamed, and NO indirect dispatch
 * at all -- no jump table, no vtable call, nothing to dump first.
 *
 * It is a widget constructor and it looks like one: a 0x1A4 frame and
 * TWENTY-EIGHT string literals, all of them bitmap names --
 * `03_010_00_scrollbar.bmp`, `03_028_01_green.bmp`, `03_013_0%i_color.bmp`
 * with a format digit for the four player colours -- plus the one non-bitmap
 * string `-- Open --`, which is what an empty player row reads.
 *
 * IT OPENS WITH THE MSVC SEH PROLOGUE and this file's standing decision
 * applies: `push -1; push <handler>; push fs:[0]; mov fs:[0], esp` is NOT
 * reproduced. Nothing in this program throws, VC6's operator new answers NULL
 * rather than throwing and the game tests it, so the registered frame is
 * never consulted. The cost is stated where that decision is recorded: if
 * something DID unwind through here the base destructor would be skipped --
 * a leak, not a wrong answer.
 *
 * The one unnamed callee, 0x00456300, is another thiscall constructor with
 * its own SEH frame, so this is a class building a sub-object rather than
 * calling a helper.
 *
 * EVERY CONSTRUCTOR IT CALLS IS ALREADY RECONSTRUCTED. MpNameConstruct,
 * MpColourConstruct, MpTeamConstruct, RecordCtor, ListBoxConstruct,
 * TextListConstruct, ArrowBarConstruct, MultiSpriteConstruct,
 * PanelConstruct, ButtonConstruct, ScreenBaseConstruct and WidgetAddChild --
 * twelve of twelve, each with a header written when it was done.
 *
 * So the last function below the CRT line composes nothing but code this port
 * already owns, and its transcription needs no seam and no new prototype.
 * That is what a boundary looks like when it has been worked outward-in for
 * long enough: the thing left over is the one that depends on everything
 * else.
 *
 * THE TWENTY CHILDREN, extracted by pairing each operator new's SIZE with the
 * constructor that follows it rather than by reading the body:
 *
 *   in the four-slot player loop:  MpName 0x74, MpColour 0x68, MpTeam 0x68
 *   four list groups:              Record 0x0C + Listbox 0x98 + Arrowbar 0x78
 *                                  (the second group uses TextList, not Listbox)
 *   then:                          MultiSprite 0x80, Panel 0x60, Button 0x78 x2
 *
 * So the panel is three widgets per player, four scrolling lists with their
 * own scrollbars, and four pieces of fixed furniture. Nineteen static
 * constructor sites and twenty WidgetAddChild calls, because the loop's three
 * are one site each and run four times.
 *
 * That the SECOND list group uses TextList where the other three use Listbox
 * is the sort of thing a reader normalises away -- four groups that look
 * identical, one with a different class. The extraction pairs sizes with
 * constructors and shows it without a judgement call, which is the same
 * argument as generating DirtyCollect's case labels from its table.
 *
 * THE CHILD IDIOM, WHICH IS THE WHOLE BODY TWENTY TIMES OVER:
 *
 *     w = operator new(size);
 *     if (w) <Class>Ctor(w, rect, ...);       // thiscall, rect BY VALUE
 *     WidgetAddChild(this, w, 0);
 *
 * The rectangle goes by VALUE, not by pointer: the original does
 * `sub esp, 0x10` and copies four dwords into the hole before the call. That
 * is the shape CLAUDE.md already lists as checkoffsetuse's dominant blind
 * spot in this family -- AM2_Widget's x/y/w/h turning up in every constructor
 * because a RECT passed by value is copied a dword at a time. Expect that
 * tool to report those four offsets here and to be right about it.
 *
 * The `mov byte ptr [esp+0x1BC], N` between the allocations is the SEH unwind
 * state index, counting up as each child is constructed so the handler knows
 * how many to destroy. Not reproduced, per the standing decision above.
 *
 * ITS PLAYER-ROW LOOP IS A SECOND CONSUMER OF THE COMM PLAYER RECORD, and it
 * agrees with the names already here. It walks the four slots reading
 * COMM_OFF_PLAYER_SLOTS at +0x214 and COMM_OFF_SLOT_NAME at +0x218, and
 * writes either the player's name or the literal `-- Open --` into the row.
 *
 * That matters more than it looks. COMM_OFF_PLAYERS is the macro this file
 * records as having been RE-BASED twelve bytes, needing an audit of
 * twenty-six use sites, one of which was missed and silently wrote a computer
 * player's name twelve bytes early. A second consumer landing exactly on
 * +0x214 and +0x218 is independent evidence the corrected base is right --
 * which is what "look for a second toucher before believing a layout" asks
 * for, arriving a long time after the layout was settled.
 *
 * ITS CALL PROFILE IS THE WHOLE DESIGN: 26 operator new, 20 RectSet and 20
 * WidgetAddChild. So it builds TWENTY children, and the body is one idiom
 * twenty times -- allocate, construct, place with a rectangle, add to the
 * parent. The classes are counted too: 4 records, 4 arrow bars, 4 buttons,
 * 3 list boxes, 2 multi-sprites and the rest.
 *
 * AND ITS SPRITE LOOPS ARE BOUNDED BY THE NEXT GLOBAL, twice. The first runs
 * a pointer from ADDR_MP_PANEL_SPRITES_A until it reaches ADDR_MENU_MSG_LIST,
 * filling each slot with `03_013_0%i_color.bmp` for the four player colours;
 * the second runs from ADDR_MP_PANEL_SPRITES_B to its own _END. That is the
 * same shape as the registration table's nine buckets and LoadMap's four
 * reveal grids -- an array whose length is written nowhere and is simply the
 * distance to whatever the linker put next. Third instance, and the reason to
 * check what follows a table before believing a count. */
#define AM2_MP_PANEL_SIZE        0x278u
#define ADDR_MP_OPTIONS_CTOR     0x00432320u  /* thiscall obj *(obj, bmp) */
#define AM2_MP_OPTIONS_SIZE      0x110u
/* Called after the host panel is built, and only by that factory: it repaints
 * the panel from the current session. Still original. */
#define ADDR_STR_MPHOST_BMP      0x004871C4u  /* "01_001_00_mphost.bmp" */
#define ADDR_STR_MPJOIN_BMP      0x00487408u  /* "01_002_00_mpjoin.bmp" */
#define ADDR_STR_MPHOSTOPTS_BMP  0x004873C0u  /* "01_001_03_mphostoptions.bmp" */
/* The plain screen factories: close, allocate, construct, store. Sizes and
 * constructors are docs/screens.md's, which reads them out of the image. */
#define ADDR_STR_SCREEN_BMP      0x00485224u  /* "01_000_00_screen.bmp" */
#define ADDR_STR_CONTROLS_BMP    0x0048B9DCu  /* "01_003_00_controls.bmp" */
/* The five factories whose constructors take TWO arguments -- a backdrop and
 * a flag -- because the screen exists in two contexts. `cmp [ADDR_GAME_STATE],
 * 2` picks between them: in a mission the screen gets its own backdrop and a
 * flag of 0 and the frame beneath it must be repainted, so RefreshScreen is
 * called; from the title it gets the shared backdrop and a flag of 1 and no
 * repaint is needed.
 *
 * WHERE the repaint goes is not the same in all of them, and it is not
 * cosmetic: AUDIO and DELETE GAME call it BEFORE allocating, LOAD GAME calls
 * it AFTER constructing and only then publishes the screen. */
#define ADDR_OPEN_COMM_PANEL     0x0042EE40u  /* void(void) */
#define ADDR_COMM_PANEL_CTOR     0x0042E9C0u  /* thiscall obj *(obj, bmp) */
#define AM2_COMM_PANEL_SIZE      0x6Cu
#define ADDR_OPEN_AUDIO_OPTIONS  0x0044F9E0u  /* void(void) */
#define ADDR_AUDIO_OPTIONS_CTOR  0x0044F370u  /* thiscall obj *(obj, bmp, f) */
#define AM2_AUDIO_OPTIONS_SIZE   0x7Cu
#define ADDR_STR_AUDIO_BMP       0x0048B7C4u  /* "02_013_00_audio.bmp" */
#define ADDR_OPEN_DELETE_GAME    0x00450250u  /* void(void) */
#define ADDR_DELETE_GAME_CTOR    0x0044FE50u  /* thiscall obj *(obj, bmp, f) */
#define AM2_DELETE_GAME_SIZE     0x64u
#define ADDR_STR_DELGAME_BMP     0x0048B918u  /* "02_011_00_delgame.bmp" */
#define VTABLE_DELETE_GAME       0x0046FB44u
#define ADDR_STR_DELGAME_ASK     0x0048B8ECu  /* "Are you sure ... delete this
                                               * game?" */
/* Still the original's, and the CANCEL doubles as the screen's escape. */
#define ADDR_ON_DELGAME_OK       0x004501E0u
#define ADDR_ON_DELGAME_CANCEL   0x00450180u
#define ADDR_OPEN_LOAD_GAME      0x00452680u  /* void(void) */
/* The pieces a screen constructor is built from. */
#define ADDR_SCREEN_BASE_CTOR    0x00454B00u /* thiscall w *(w, bmp, int32) */
#define ADDR_BUTTON_CTOR         0x004540F0u /* thiscall w *(w, b0,b1,b2, f,
                                              * AM2_Rect by value, handler,
                                              * int32) -- ret 0x28 */
#define VTABLE_OPTIONS_MENU      0x0046FB30u
/* The OPTIONS menu's four button handlers. Each of the first three raises a
 * menu request, and the number it raises is the ARM INDEX of the screen it
 * opens -- 15 CONTROLS, 16 DIFFICULTY, 19 AUDIO. That is the table in
 * docs/screens.md confirming its own indexing three times over. */
#define ADDR_ON_CONTROLS_BUTTON   0x0044FD40u /* menu request 15 */
#define ADDR_ON_DIFFICULTY_BUTTON 0x0044FD70u /* menu request 16 */
#define ADDR_ON_AUDIO_BUTTON      0x0044FDA0u /* menu request 19 */
/* The shared BACK handler: it computes its request rather than writing a
 * constant, and a dialog also stores it at 0x0060 as its escape action. */
#define ADDR_ON_MENU_BACK         0x0044E670u
#define DLG_OFF_ESCAPE            0x60u

/* The three CONFIRM dialogs -- CONFIRM GAME EXIT, the replay prompt and
 * DELETE PLAYER -- are one body three times over. They differ in five things
 * and nothing else: the vtable, the panel's bitmap, the OK handler, the
 * message, and (for DELETE PLAYER alone) the CANCEL handler.
 *
 * The shape is worth knowing before reading any of them: the panel is a child
 * of the dialog, and everything else is a child of the PANEL. */
#define VTABLE_QUIT_DIALOG       0x0046FAF4u
#define VTABLE_REPLAY_DIALOG     0x0046FB08u
#define VTABLE_DELPLAYER_DIALOG  0x0046FB6Cu
#define ADDR_ON_QUIT_OK          0x0044EE30u
#define ADDR_ON_REPLAY_OK        0x0044F1B0u
#define ADDR_ON_DELPLAYER_OK     0x00450A60u
#define ADDR_ON_DELPLAYER_CANCEL 0x00450A10u
#define ADDR_STR_ALPINE          0x0048B710u /* "alpine", the data dir they chdir to */

/* The other three widget constructors these need. Each takes its rectangle by
 * value like the button's, and each `ret` confirms the argument list:
 * 0x18 is bmp + flag + 16, 0x14 is 16 + message, 0x1C is two bitmaps + flag
 * + 16. */
#define ADDR_PANEL_CTOR          0x00454980u /* thiscall w *(w, bmp, f, rect) */
#define AM2_PANEL_SIZE           0x60u
#define ADDR_TYPER_CTOR          0x004566F0u /* thiscall w *(w, rect, msg) */
#define AM2_TYPER_SIZE           0x464u
#define ADDR_MULTISPRITE_CTOR    0x00456BC0u /* thiscall w *(w, b0, b1, f, rect) */
#define AM2_MULTISPRITE_SIZE     0x80u

/* The DIFFICULTY dialog, 0x0044E730. Same panel-holds-everything shape as the
 * three confirm dialogs, with a LIST BOX where they have a message. */
#define VTABLE_DIFFICULTY_DIALOG 0x0046FAE0u
#define ADDR_ON_DIFFICULTY_OK    0x0044EA80u
/* The CANCEL handler here, and also what the dialog stores as its escape
 * action -- and the title screen's OPTIONS button, which is what its body
 * says it is: a sound and menu request 14. Named from the body rather than
 * from this call site, where it read as a cancel. Distinct from
 * ADDR_ON_MENU_BACK, which the OPTIONS menu uses. */
#define ADDR_ON_OPTIONS_MENU     0x0044D490u
#define ADDR_LISTBOX_CTOR        0x00454F90u /* thiscall w *(w, rect, rows,
                                              * int32, int32, int32) */
#define AM2_LISTBOX_SIZE         0x98u
#define AM2_ROWS_SIZE            0x0Cu       /* the three-field record */
#define ADDR_STR_DIFFICULTY_BMP  0x0048B730u /* "02_014_00_difficulty.bmp" */
#define ADDR_STR_EASY            0x0048B728u
#define ADDR_STR_MEDIUM          0x0048B720u
#define ADDR_STR_HARD            0x0048B718u
/* Where the dialog keeps its list, and where the list keeps its blinker. */
#define DLG_OFF_LIST             0x64u
#define LIST_OFF_BLINKER         0x94u

/* The CONTROLS dialog, 0x00450E10. Twenty-one key-capture rows built from
 * three parallel tables and then three buttons. */
#define VTABLE_CONTROLS_DIALOG   0x0046FB94u
#define ADDR_KEYROW_POSITIONS    0x0048AEC8u /* int16 x, y per row */
#define ADDR_KEYROW_CTOR         0x00450C50u /* thiscall, ret 0x2C */
#define AM2_KEYROW_SIZE          0x6Cu
#define ADDR_ON_CONTROLS_OK      0x00451150u
#define ADDR_ON_CONTROLS_DEFAULT 0x004511A0u
#define ADDR_ON_CONTROLS_CANCEL  0x00451100u
#define AM2_BMP_DEFAULT0         0x0048730Cu
#define AM2_BMP_DEFAULT1         0x00487324u
#define AM2_BMP_DEFAULT2         0x0048733Cu

/* The MULTIPLAYER OPTIONS constructor, 0x00432320. */
#define VTABLE_OPTIONS_MENU_MP   0x0046FA34u /* the HOST OPTIONS vtable */

/* The AUDIO CONTROLS dialog, 0x0044F370 -- `ret 8`, two stack arguments. */
#define VTABLE_AUDIO_DIALOG      0x0046FB1Cu
#define ADDR_SCROLLBAR_CTOR      0x00455FF0u /* thiscall, ret 0x18 */
#define AM2_SCROLLBAR_SIZE       0x80u
#define ADDR_ON_VOLUME_EFFECTS   0x0044F2A0u
#define ADDR_ON_VOLUME_MUSIC     0x0044F2E0u
#define ADDR_ON_VOLUME_VOICE     0x0044F320u
#define ADDR_ON_AUDIO_OK         0x0044F930u
/* 0x0044F860: store three volumes, each -2000 turned into DSBVOLUME_MIN,
 * then tail-jump the Options.cfg writer. Only AUDIO's OK reaches it. */
#define ADDR_APPLY_VOLUMES       0x0044F860u  /* void(fx, music, voice) */
/* 0x0044CFA0: rewrite Options.cfg. This said "left original -- it is CRT
 * file I/O, and this port replaces the CRT wholesale rather than function by
 * function", which was the wrong reading of its own rule: the CRT is what
 * this function CALLS, not what it is. Every other file writer in the tree
 * goes through orig_fopen and orig_fwrite for the seam crt.h describes, and
 * so does this one now. Reconstructed. */
#define ADDR_SAVE_OPTIONS        0x0044CFA0u  /* void(void) */
#define ADDR_ON_AUDIO_CANCEL     0x0044F8B0u
/* Three bars at 0x0064..0x006C, and the volumes they started from at
 * 0x0070..0x0078 so CANCEL can put them back. */
#define AUDIO_OFF_BARS           0x64u
#define AUDIO_OFF_SAVED          0x70u
/* The volumes are DirectSound attenuations in hundredths of a decibel, so a
 * bar position is (volume + 2000) / 100 -- twenty-one steps from silence. */
#define AM2_VOLUME_FLOOR         2000
#define AM2_VOLUME_STEP          100

/* ENTER BATTLE NAME, 0x0042FB00. Two edit boxes seeded from the two saved
 * names, each with a green dot beside it, then OK and CANCEL. */
#define VTABLE_BATTLE_NAME_DLG   0x0046FA0Cu
#define ADDR_EDIT_CTOR           0x00454C10u /* thiscall, ret 0x34 */
#define AM2_EDIT_SIZE            0x80u
#define AM2_EDIT_MAX_CHARS       0x18u
/* A char * to the set of characters an edit box accepts:
 * " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!..." --
 * so the field is a whitelist and not a length limit. */
#define ADDR_EDIT_CHARSET_PTR    0x00485308u /* const char ** */
#define EDIT_OFF_CHARSET         0x68u
#define EDIT_OFF_DOT             0x70u
/* The byte immediately below ADDR_BACKGROUND_COLOUR. The edit box is handed
 * both; what distinguishes them is not established. */
#define ADDR_COLOUR_BELOW_BG     0x00502AD8u /* uint8_t */
#define AM2_BMP_GREEN0           0x0048701Cu /* 03_028_0N_green.bmp */
#define AM2_BMP_GREEN1           0x00487030u
#define ADDR_STR_BATTLE_PANEL    0x00487094u /* "02_002_00_host.bmp" */

/* COMM. CHANNEL SELECT, 0x0042E9C0 -- the connection list. */
#define VTABLE_COMM_PANEL        0x0046F9D0u
#define ADDR_STR_COMMPANEL_BMP   0x00486EACu /* "02_001_00_commpanel.bmp" */
/* Its destructor pair, the same two instructions as the fifteen dialogs --
 * stamp my own vtable, jump to the dialog base's. Reached whenever the
 * channel list closes, which the `multi` and `mpoptions` drives both do. */
#define ADDR_COMM_PANEL_DELETE   0x0042ECC0u /* thiscall obj *(obj, flags) */
#define ADDR_COMM_PANEL_DESTRUCT 0x0042ECE0u /* thiscall void(obj) */
/* The scroll bar WITH arrows, as against the bare one the volume sliders use.
 * `ret 0x24` is 36: rectangle, the list it drives, two bitmaps, a maximum and
 * a zero. */
#define ADDR_ARROWBAR_CTOR       0x00455970u /* thiscall, ret 0x24 */
#define AM2_ARROWBAR_SIZE        0x78u
#define AM2_BMP_SCROLLBAR0       0x00486E7Cu /* 03_010_0N_scrollbar.bmp */
#define AM2_BMP_SCROLLBAR1       0x00486E94u
#define AM2_BMP_SELECT0          0x00486E34u /* 03_005_0N_select.bmp */
#define AM2_BMP_SELECT1          0x00486E4Cu
#define AM2_BMP_SELECT2          0x00486E64u
/* The list and its bar point at each other. */
#define LIST_OFF_ARROWBAR        0x7Cu
#define ARROWBAR_OFF_LIST        0x58u
/* Where this dialog keeps them: the list at 0x0064 and the rows at 0x0068. */
#define COMMPANEL_OFF_LIST       0x64u
#define COMMPANEL_OFF_ROWS       0x68u

/* SELECT PLAYER, 0x00451400 -- the saved-player list, and the one screen whose
 * rows come off the FILESYSTEM rather than from a table or the comm object. */
#define VTABLE_SELECT_PLAYER     0x0046FBA8u
#define ADDR_READ_CAMPAIGN_FILE  0x0043EC80u /* void(void): DefParseInfoFile
                                              * on "campaign.txt", with the
                                              * same "Couldn't parse %s!" as
                                              * ReadMpMapsFile */
#define ADDR_STR_SAVE_DIR        0x004851CCu /* "save" */
#define ADDR_STR_GLOB_ALL        0x0048BAA0u /* "*" */
#define ADDR_STR_SELECTPLAYER_BMP 0x0048BA84u
#define ADDR_ON_RECRUIT          0x00451300u
#define ADDR_ON_SELECT_PLAYER    0x00451380u
#define ADDR_ON_DELETE_PLAYER    0x00451330u
#define ADDR_SELECT_PLAYER_ROW   0x004512A0u /* the list's row callback */
#define AM2_BMP_RECRUIT0         0x0048BA3Cu
#define AM2_BMP_RECRUIT1         0x0048BA54u
#define AM2_BMP_RECRUIT2         0x0048BA6Cu
#define AM2_BMP_DELETE0          0x0048B9F4u
#define AM2_BMP_DELETE1          0x0048BA0Cu
#define AM2_BMP_DELETE2          0x0048BA24u
#define AM2_BMP_BACK0            0x0048B6BCu
#define AM2_BMP_BACK1            0x0048B6D0u
#define AM2_BMP_BACK2            0x0048B6E4u
/* MSVC's _findfirst / _findnext / _findclose, and the two fields of
 * _finddata_t this screen reads. */
#define ADDR_CRT_FINDFIRST       0x00465BA3u
#define ADDR_CRT_FINDNEXT        0x00465C6Du
#define ADDR_CRT_FINDCLOSE       0x00465D32u
#define AM2_FIND_OFF_ATTRIB      0x00u
#define AM2_FIND_OFF_NAME        0x14u
#define AM2_FIND_ATTR_DIR        0x10u

/* The PANEL, 0x00454980 -- a widget whose whole job is to hold a backdrop
 * sprite, and the container eight of the reconstructed screens hang
 * everything else off. `ret 0x18`: bitmap, flag, rectangle by value. */
#define VTABLE_PANEL             0x0046FC70u
#define ADDR_PRELOAD_SPRITE_NAME 0x00445CF0u /* AM2_Sprite *(name, f, 1) --
                                              * splits the name and calls
                                              * PreloadSprite */
#define PANEL_OFF_SPRITE         0x58u       /* the same sprite as 0x0038 */
#define PANEL_OFF_FLAG           0x5Cu

/* The key-capture ROW, 0x00450C50 -- a focus-highlighting label with four
 * colour bytes and the bound key's index. `ret 0x2C` is 44: the index, the
 * caption, sixteen of rectangle, a font and four colours. */
#define VTABLE_KEYROW            0x0046FB80u
#define FOCUSLABEL_OFF_INK2      0x65u
#define FOCUSLABEL_OFF_INK3      0x66u
#define FOCUSLABEL_OFF_INK4      0x67u

/* The SCREEN BASE, 0x00454B00 -- every one of the twenty screens starts here.
 * It is a PANEL over the whole 640x480, then the dialog vtable on top. */
/* 0x004542F0: the base BUTTON's constructor -- WidgetConstruct, its own
 * vtable, and three fields cleared. Every three-state button and every
 * checkbox derives from it, and it RETURNS `this`, as i386 MSVC does. */
#define ADDR_BUTTON_BASE_CTOR    0x004542F0u /* thiscall void *(this) */
#define VTABLE_BUTTON_BASE       0x0046FC48u
#define BUTTON_BASE_OFF_A        0x5Cu
#define BUTTON_BASE_OFF_B        0x60u
#define BUTTON_BASE_OFF_C        0x64u
#define VTABLE_EDIT              0x0046FC98u
#define VTABLE_MULTISPRITE       0x0046FD38u
#define VTABLE_LISTBOX           0x0046FCC0u
#define VTABLE_CHECKBOX          0x0046FC5Cu
/* Its paint, and the class is FOUR STATES rather than two: the sprite and the
 * ink are picked together from (focused, checked), where focused means the
 * PARENT's focusedChild is this widget. The four sprites run from 0x68 --
 * straight after the button base's three at BUTTON_BASE_OFF_A..C -- and the
 * four inks are single BYTES at 0x84..0x87, which is why they are not a
 * parallel array of dwords.
 *
 * Then two overrides in order. CHECKBOX_OFF_FORCE_PLAIN, when the box is
 * checked, puts back the UNFOCUSED-checked pair even though the widget has the
 * focus. And a disabled widget takes ADDR_COLOUR_STALE for its ink, keeping
 * whichever sprite was chosen -- so a greyed checkbox still shows its state.
 *
 * ADDR_COLOUR_STALE is that address's SECOND reading: it went in as the comm
 * list's "silent over 1250 ms" grey. Both are a greyed-out ink and they may be
 * the same idea; recorded rather than renamed, because nothing here settles
 * which name is the general one. */
#define ADDR_CHECKBOX_PAINT      0x00454840u /* thiscall void(w, RECT) */
#define CHECKBOX_OFF_SPR_ON      0x68u  /* checked, not focused */
#define CHECKBOX_OFF_SPR_OFF     0x6Cu
#define CHECKBOX_OFF_SPR_ON_FOC  0x70u
#define CHECKBOX_OFF_SPR_OFF_FOC 0x74u
#define CHECKBOX_OFF_CHECKED     0x78u  /* uint8_t */
#define CHECKBOX_OFF_INK_ON      0x84u  /* uint8_t, and the same order */
#define CHECKBOX_OFF_INK_OFF     0x85u
#define CHECKBOX_OFF_INK_ON_FOC  0x86u
#define CHECKBOX_OFF_INK_OFF_FOC 0x87u
#define CHECKBOX_OFF_CAPTION     0x88u  /* const char *, or null */
#define CHECKBOX_OFF_FORCE_PLAIN 0x8Cu  /* uint8_t; see above */
#define AM2_CHECKBOX_TEXT_X      0x13   /* 19, from the widget's left */
#define VTABLE_ARROW             0x0046FCD4u /* stamped over the button's */
/* The scroll bar's two arrow handlers, and the three bitmaps it loads. */
#define ADDR_ON_ARROW_LEFT       0x00455ED0u
#define ADDR_ON_ARROW_RIGHT      0x00455F60u
#define AM2_BMP_HSCROLLBAR       0x0048BD10u /* 03_020_00_hscrollbar.bmp */
#define AM2_BMP_RTARROW1         0x0048BD2Cu
#define AM2_BMP_RTARROW2         0x0048BD44u
#define AM2_BMP_LTARROW1         0x0048BD5Cu
#define AM2_BMP_LTARROW2         0x0048BD74u
#define ARROW_OFF_OWNER          0x78u /* back to the bar */
#define ARROW_OFF_FLAG5C         0x5Cu
#define AM2_ARROW_SIZE           0x7Cu
#define AM2_SCROLLBAR_RANGE      0x14u /* twenty steps, and what the AUDIO
                                        * dialog divides its position by */

/* The VERTICAL bar with arrows, 0x00455970 -- the connection list's and the
 * player list's. Nine stack arguments: rectangle, parent, two bitmaps, a span
 * and a flag the arrows get too. */
#define VTABLE_ARROWBAR          0x0046FCE8u
#define ADDR_ON_ARROW_UP         0x004557F0u
#define ADDR_ON_ARROW_DOWN       0x004558B0u
#define AM2_BMP_UPARROW1         0x0048BCE0u
#define AM2_BMP_UPARROW2         0x0048BCF8u
#define AM2_BMP_DNARROW1         0x0048BCB0u
#define AM2_BMP_DNARROW2         0x0048BCC8u
#define ARROWBAR_OFF_UP          0x5Cu
#define ARROWBAR_OFF_DOWN        0x60u
#define ARROWBAR_OFF_FLAG50      0x50u /* the arrows get the same value */
#define ARROWBAR_OFF_SPRITE0     0x64u
#define ARROWBAR_OFF_SPRITE1     0x68u
/* The bar's destructor pair. It releases both sprites and chains to the base
 * widget -- and it null-tests SPRITE0 and NOT SPRITE1. ReleaseSprite handles
 * null itself, so both are safe and the asymmetry is the original's. It is
 * reproduced rather than smoothed: which of two fields a writer thought could
 * be null is evidence about the class, and making them agree destroys it. */
#define ADDR_ARROWBAR_DELETE     0x00455B80u /* thiscall obj *(obj, flags) */
#define ADDR_ARROWBAR_DESTRUCT   0x00455BA0u /* thiscall void(obj) */
#define ARROWBAR_OFF_SHIFT       0x70u /* int32_t, where the thumb sits */
#define ARROWBAR_OFF_SPAN        0x74u
/* Was ADDR_CHATBOX_REFLOW, from the one call site, which reaches it as
 * `[chatbox + 0x7C]`. That field is LIST_OFF_ARROWBAR and the object on the
 * other end of it is the BAR. Renamed, not aliased. */
#define ADDR_ARROWBAR_FOLLOW_END 0x00455D60u /* thiscall void(AM2_Widget *) */
/* 0x00455C10, thiscall `ret 0x10`: repaint a widget through the nearest
 * ANCESTOR that owns a sprite, ignoring the clip rectangle it is handed and
 * using that ancestor's own. Near-twin of WidgetRepaint (0x00453FF0) without
 * the 0x48 test and without clearing 0x44, so it is a separate function and
 * not an alias. The four arrow handlers end in it. */
#define ADDR_REPAINT_ANCESTOR    0x00455C10u /* void(AM2_Widget *, RECT) */

/* The TYPEWRITER, 0x004566F0 -- and the word-wrap IS the constructor. */
#define ADDR_CRT_STRCHR          0x004663B0u /* char *(const char *, int) */
#define ADDR_STR_LINE_BREAK      0x0048BD8Cu /* "|", not a newline */
#define AM2_TYPER_MARGIN         12          /* taken off the usable width */
#define AM2_TYPER_LINE_MAX       0x10Cu      /* the constructor's scratch */

/* The TITLE SCREEN, 0x0044D730 -- arm 1 of the menu table, and the one arm
 * that is not a factory: it builds the whole screen inline.
 *
 * It also holds the binary patch that removes MULTI-PLAYER. `0x0044D8FE` is
 * an ordinary `je` on the allocation in the retail compile and an `EB` here,
 * so the button is skipped unconditionally; docs/binarypatches.md has the
 * byte. A reconstruction cannot honour a patch inside the function it
 * replaces, so ours asks restore_multiplayer() instead. */
#define ADDR_OPEN_TITLE_SCREEN   0x0044D730u  /* void(void) */
#define ADDR_ON_BOOT_CAMP        0x0044D3F0u
#define ADDR_ON_SINGLE_PLAYER    0x0044D2E0u
#define ADDR_ON_MULTI_PLAYER     0x0044D380u
#define ADDR_ON_MOVIES           0x0044D3C0u
#define ADDR_ON_CREDITS          0x0044D4C0u
#define ADDR_ON_QUIT             0x0044D4F0u
#define AM2_TITLE_SCREEN_SIZE    0x64u
#define AM2_TITLE_BUTTON_LEFT    0xE7
#define AM2_TITLE_BUTTON_WIDTH   0x98
#define AM2_TITLE_BUTTON_HEIGHT  0x19
/* The seven handlers' menu request codes, which are arm numbers in
 * docs/screens.md: MOVIES asks for 13, OPTIONS for 14, QUIT for 17,
 * MULTI-PLAYER for 6 (COMM CHANNEL SELECT), and SINGLE PLAYER for 3
 * (SELECT PLAYER) or 2 (SELECT MAP). CREDITS asks for no screen at all --
 * it sets the game-over reason to 4 and requests state 0. */
#define AM2_MENU_REQUEST_TITLE        0x01u
#define AM2_MENU_REQUEST_CONTROLS     0x0Fu
#define AM2_MENU_REQUEST_DIFFICULTY   0x10u
#define AM2_MENU_REQUEST_AUDIO        0x13u
#define AM2_MENU_REQUEST_MOVIES       0x0Du
#define AM2_MENU_REQUEST_OPTIONS_MENU 0x0Eu
#define AM2_MENU_REQUEST_QUIT         0x11u
#define AM2_MENU_REQUEST_COMM_PANEL   0x06u
#define AM2_MENU_REQUEST_SELECT_MAP   0x02u
#define AM2_MENU_REQUEST_SELECT_PLAYER 0x03u
#define AM2_MENU_REQUEST_ENTER_NAME    0x04u
#define AM2_MENU_REQUEST_LOAD_GAME     0x05u
#define AM2_MENU_REQUEST_DEL_PLAYER    0x14u
/* THREE MORE, AND THE JUMP TABLE IS WHAT SAYS WHICH IS WHICH. The arms at
 * 0x00426435.. are laid out 1, 2, 3, 4, 5, 6, 10, 11, 12, 7, 8, 9, 13 -- so
 * reading them top to bottom and numbering as you go puts the war menu, the
 * battle name and the battle browser three places early. Taken from the table
 * at 0x00426518, exactly as CLAUDE.md says to. */
#define AM2_MENU_REQUEST_WAR_MENU     0x0Au
#define AM2_MENU_REQUEST_BATTLE_NAME  0x0Bu
#define AM2_MENU_REQUEST_BATTLE_JOIN  0x0Cu
#define AM2_MENU_MODE_DEL_PLAYER       0x1Au
#define AM2_GAME_OVER_CREDITS         0x04u
/* 0x0043ED00: reload the Boot Camp level table -- it frees whatever is there,
 * chdirs to `shared` and parses "bootcamp.txt", naming the file in
 * "Couldn't parse %s!" if that fails. */
#define ADDR_LOAD_BOOTCAMP_LEVELS 0x0043ED00u  /* void(void) */
/* The two tables those three readers fill and 0x0043E8B0 frees, each a
 * {base, count, capacity} triple. The SECOND is the registry
 * ADDR_SCRIPT_LIST_FIND searches -- which that macro's comment could not name
 * when it was written, because the searcher alone does not say who fills it.
 * The reset does: both are loaded from the same `.txt` by DefParseInfoFile. */
#define ADDR_LEVEL_TABLE_CAP      0x00656340u  /* int32_t */
#define ADDR_NAME_TABLE_BASE      0x00656344u  /* the ADDR_SCRIPT_LIST_FIND one */
#define ADDR_NAME_TABLE_COUNT     0x00656348u
#define ADDR_NAME_TABLE_CAP       0x0065634Cu
#define ADDR_FREE_LEVEL_TABLES    0x0043E8B0u  /* void(void) */
#define ADDR_STR_CAMPAIGN_TXT     0x00487C34u  /* "campaign.txt" */
#define ADDR_STR_MPMAPS_TXT       0x00487C44u  /* "mpmaps.txt" */
#define ADDR_STR_BOOTCAMP_TXT     0x00487C50u  /* "bootcamp.txt" */
#define ADDR_FMT_COULDNT_PARSE    0x00473D7Cu  /* "Couldn't parse %s!\n" */
/* 0x0043E1F0: bsearch the level table -- 0x30C-byte records at
 * ADDR_LEVEL_TABLE, count at ADDR_LEVEL_TABLE_COUNT -- for the one whose
 * first field is the id given. Returns the record or NULL. */
#define ADDR_FIND_LEVEL_RECORD    0x0043E1F0u  /* void *(int32_t id) */
#define ADDR_LEVEL_TABLE          0x00656338u  /* the 0x30C-byte records */
#define ADDR_LEVEL_TABLE_COUNT    0x0065633Cu  /* int32_t */
/* 0x0043ED40, two callers, five bytes of body: read that count and return it.
 * Both callers are in the menu band beside CloseScreen. Reconstructed. */
#define ADDR_LEVEL_COUNT         0x0043ED40u  /* int32_t(void) */
#define AM2_LEVEL_RECORD_SIZE     0x30Cu
/* 0x0043E160, one caller. Append one level record, allocating the table on
 * first use and growing it when it is full. */
#define ADDR_ADD_LEVEL_RECORD     0x0043E160u  /* void(const void *record) */
#define AM2_LEVEL_TABLE_FIRST     12  /* what the first malloc holds */
#define AM2_LEVEL_TABLE_GROW      6   /* records added each time it is full */
/* 0x0043ED50: copy the names out of a level record into the globals the
 * loader reads -- ADDR_MAP_NAME, ADDR_MAP_FOLDER and two more. */
#define ADDR_SELECT_LEVEL         0x0043ED50u  /* void(void *record) */
/* The seven strings and one flag it copies, and the globals they land in.
 * The strings are 0x40 bytes apart at both ends and copied with an unbounded
 * strcpy, so the record's own field size is the only bound there is.
 *
 * The one at +0x288 is worth knowing about: it lands in the buffer
 * StopNamedSound's only call site guards on, which CLAUDE.md records as
 * staying all-zero for an entire Boot Camp mission. It stays zero because
 * THIS is what fills it and Boot Camp's level record leaves it empty -- not
 * because nothing writes it. */
/* THE GAME'S OWN DATA FILE NAMES THESE, and five of them used to be spelled
 * after their offsets because nobody had looked. campaign.txt, bootcamp.txt
 * and mpmaps.txt open with a comment line that is the column header:
 *
 *   #command map_name map_text folder victorycin "load screen" loadmusic
 *            briefing briefsfx stratmap "cycle?" losemovie1 losemovie2
 *            losemovie3
 *
 * and DefMapLine consumes the columns in exactly that order. So the names
 * below are the program's own vocabulary rather than ours, on the same footing
 * as the script keywords -- which is the strongest kind of name this project
 * gets. LEVEL_OFF_STR_1C4, _204, _248, _2C8 and LEVEL_OFF_RESERVE10 are gone.
 *
 * Two of them settle open questions elsewhere. `briefsfx` is the column that
 * fills the buffer StopNamedSound's only call site guards on, and bootcamp.txt
 * writes `none` there -- which DefMapLine maps to the empty ADDR_DIR_SCRATCH.
 * That is WHY the buffer is empty all mission, which CLAUDE.md had as an
 * observation without a cause. And `cycle?` is what feeds ADDR_TILESET_RESERVE,
 * the gate palette.cpp had to POKE to reach the palette-cycling body: Boot
 * Camp's line says FALSE, so the shimmer is off for that whole map by the
 * data's own choice rather than by anything the port does. */
#define LEVEL_OFF_MAP_NAME        0x004u  /* map_name -> ADDR_MAP_NAME; NOT
                                           * LEVEL_OFF_NAME, which is map_text,
                                           * the DISPLAY name at +0x44 */
#define LEVEL_OFF_FOLDER          0x084u  /* folder -> ADDR_MAP_FOLDER */
#define LEVEL_OFF_LOAD_MUSIC      0x1C4u  /* loadmusic -> ADDR_LEVEL_STR_A */
#define LEVEL_OFF_LOAD_SCREEN     0x204u  /* load screen -> ADDR_LEVEL_STR_B */
#define LEVEL_OFF_CYCLE           0x244u  /* cycle? -> ADDR_TILESET_RESERVE */
#define LEVEL_OFF_BRIEFING        0x248u  /* briefing -> ADDR_LEVEL_STR_C */
#define LEVEL_OFF_BRIEF_SFX       0x288u  /* briefsfx -> ADDR_LEVEL_SOUND_NAME */
#define LEVEL_OFF_STRAT_MAP       0x2C8u  /* stratmap -> ADDR_LEVEL_STR_D */
#define LEVEL_OFF_ID              0x000u  /* the bsearch key: count + 1 */
/* 0x0043E230, six callers, 144 bytes. The linear by-name search over the LEVEL
 * table -- lower-cases its argument in place, then compares LEVEL_OFF_MAP_NAME.
 * This is the function map.cpp tried to call FindLevelByName once and could
 * not, because it was reading 0x0043E900, which is the other table. */
#define ADDR_FIND_LEVEL_BY_NAME   0x0043E230u  /* void *(char *mapName) */
/* 0x0043E2C0, 1,520 bytes, referenced only from the parser table at 0x00477468
 * as the handler for `MAP` -- command 0x60. */
#define ADDR_DEF_MAP_LINE         0x0043E2C0u  /* int32_t(int32_t cmd, char *) */
#define ADDR_STR_NONE_LOWER       0x0048540Cu  /* "none"; ADDR_STR_NONE is
                                                * "None" and a different
                                                * string in a different table */
#define ADDR_STR_DATA_BACKSLASH   0x00487C2Cu  /* "data\\"; ADDR_STR_DATA_DIR
                                                * is "data" with no separator */
#define ADDR_LEVEL_STR_A          0x00511C48u  /* char[0x40] */
#define ADDR_LEVEL_STR_B          0x00511C88u  /* char[0x40] */
#define ADDR_LEVEL_STR_C          0x00511D18u  /* char[0x40] */
#define ADDR_LEVEL_STR_D          0x00511CD8u  /* char[0x40] */
#define ADDR_LEVEL_SOUND_NAME     0x00511D58u  /* char[0x40] */
/* The chosen level's id, written beside ADDR_LEVEL_INDEX by everything that
 * picks one: the Boot Camp button, SELECT MAP's OK and the state-2 entry. */
#define ADDR_LEVEL_ID             0x00511D98u  /* int32_t */
/* Set by the "Aye aye Captain!" cheat at 0x00417CAA and read in exactly one
 * place: SINGLE PLAYER, which with it set and either SHIFT held asks for
 * SELECT MAP instead of SELECT PLAYER. The cheat is a level select. */
#define ADDR_CHEAT_LEVEL_SELECT   0x004FCF98u  /* int32_t */
#define AM2_DIK_LCONTROL          0x1Du
#define AM2_DIK_RCONTROL          0x9Du
#define AM2_DIK_LSHIFT            0x2Au
#define AM2_DIK_RSHIFT            0x36u
/* Every checkbox's LEFT-click handler, written by the constructor and not by
 * the caller: the toggle. The caller's handler goes to 0x007C instead, which
 * is why clicking a plain box just ticks it and only a GROUP HEADER does
 * anything else. */
#define ADDR_CHECKBOX_TOGGLE     0x00454760u
#define CHECK_OFF_ON_CHANGE      0x7Cu
#define CHECK_OFF_INK0           0x84u  /* 0xD4, 0xD4, 0xFB, 0xFB -- hardcoded */
#define CHECK_OFF_CAPTION        0x88u
#define CHECK_OFF_FLAG8C         0x8Cu
/* The fourth colour the list box seeds itself with, beside ADDR_COLOUR_WHITE,
 * ADDR_BACKGROUND_COLOUR and ADDR_VIEW_RECT_COLOUR. Named from where it is
 * used rather than from anything it says about itself. */
#define ADDR_LIST_INK_HOT_SEL    0x00502ACCu /* uint8_t */
/* A list row is FOURTEEN pixels tall, which is not written anywhere -- it
 * comes out of the constructor's magic division: LIST_OFF_VISIBLE is
 * (height - 4) / 14, spelled `imul 0x92492493` and `sar 3`.
 *
 * The magic number alone does not say the divisor. 0x92492493 is the constant
 * for 7 AND for 14 and for 28; what picks between them is the SHIFT, and this
 * one is 3 where 7 would be 2. Reading the constant and not the shift gave 7,
 * every list drew twice as many rows as it had room for, and `ab.sh
 * mpoptions` showed seven map names where the original shows four. */
#define AM2_LIST_ROW_HEIGHT      14
#define AM2_LIST_ROW_INSET       4
/* The edit box's DEFAULT character set -- wider than the one ENTER BATTLE
 * NAME then installs over it, and including ` ~ ! @ # $ % ^ &. */
#define ADDR_EDIT_CHARSET_DEFAULT 0x00485304u /* const char ** */
#define ADDR_CHECKBOX_CTOR       0x00454640u /* thiscall, ret 0x2C */
#define AM2_CHECKBOX_SIZE        0x90u
#define AM2_BMP_CHECK0           0x00487354u /* 03_017_0N_check.bmp */
#define AM2_BMP_CHECK1           0x00487368u
#define AM2_BMP_CHECK2           0x0048737Cu
#define AM2_BMP_CHECK3           0x00487390u
/* A buffer in .bss that 56 sites pass as an argument, and every one of the
 * dialog constructors hands it to SetGameDir. It is uninitialised at load, so
 * unless something has written it the call is SetGameDir("") -- back to the
 * base path. Whether anything ever writes it is NOT established; the name
 * says what it is rather than what it means. */
#define ADDR_DIR_SCRATCH         0x004F96B8u /* char[] */
#define ADDR_LOAD_GAME_CTOR      0x004520E0u  /* thiscall obj *(obj, bmp, f) */
#define AM2_LOAD_GAME_SIZE       0xA8u
#define ADDR_STR_LOADGAME_BMP    0x0048BB38u  /* "02_007_00_loadgame.bmp" */
#define VTABLE_LOAD_GAME         0x0046FBD0u
#define ADDR_STR_SAVE_PLAYER_FMT 0x004851E4u  /* "save\\%s" */
#define ADDR_STR_GLOB_SAV        0x0048BB50u  /* "*.sav" */
/* The screen's copy of the chosen save's name, seeded from the first row. */
#define LOAD_GAME_OFF_NAME       0x68u
/* Its four buttons and the row callback, all still the original's. */
#define ADDR_LOADGAME_ROW        0x00451EA0u
#define ADDR_ON_LOADGAME_NEW     0x00451FB0u
#define ADDR_ON_LOADGAME_LOAD    0x00452060u
#define ADDR_ON_LOADGAME_DELETE  0x00451F10u
#define ADDR_ON_LOADGAME_BACK    0x00452010u
/* The save DELETE GAME is about to remove -- LOAD GAME's DELETE copies the
 * chosen name in, DELETE GAME's CANCEL clears it, and its OK reads it. */
#define ADDR_PENDING_DELETE      0x00659F58u /* char[] */
#define ADDR_STR_DATA_DIR        0x0048BAB8u /* "data" */
/* The in-mission overlay mode DELETE GAME asks with. Its CANCEL goes back to
 * AM2_MENU_MODE_DEL_PLAYER (0x1A) when it came from here and to 0x19
 * otherwise -- computed with `sete` and `add 0x19` rather than written.
 *
 * TWO names for 0x1A and two for 0x17 were nearly added here. They are one
 * screen each: LOAD GAME's BACK targets the same 0x17 the OPTIONS dialogs do,
 * and DELETE GAME's CANCEL the same 0x1A DELETE PLAYER's does. Both existing
 * names come from the first CALL SITE seen and may be under-specific -- the
 * mode is a sub-state index into the table at 0x00426230, and what each arm
 * shows is not established here. A possibly-narrow name beats a second name
 * on one value. */
#define AM2_MENU_MODE_DEL_GAME   0x1D
#define AM2_MENU_MODE_AFTER_LOAD 0x19
#define AM2_MENU_REQUEST_DEL_GAME 0x15u
#define AM2_BMP_NEW0             0x0048BAFCu /* 03_011_0N_new.bmp */
#define AM2_BMP_NEW1             0x0048BB10u
#define AM2_BMP_NEW2             0x0048BB24u
#define AM2_BMP_LOAD0            0x0048BAC0u /* 03_004_0N_load.bmp */
#define AM2_BMP_LOAD1            0x0048BAD4u
#define AM2_BMP_LOAD2            0x0048BAE8u
#define AM2_BMP_DELETE12_0       0x0048B9F4u /* 03_012_0N_delete.bmp */
#define AM2_BMP_DELETE12_1       0x0048BA0Cu
#define AM2_BMP_DELETE12_2       0x0048BA24u
#define ADDR_OPEN_MP_SELECT_MAP  0x0044DF20u  /* void(void) */
#define ADDR_MP_SELECT_MAP_CTOR  0x0044DBB0u  /* thiscall obj *(obj, bmp) */
#define VTABLE_SELECT_MAP        0x0046FAB8u
#define ADDR_STR_SELECTMAP_BMP   0x0048B668u /* "02_004_00_selectmap.bmp" */
/* 0x0044DEA0: SELECT MAP's row callback, still the original's. */
#define ADDR_SELECT_MAP_ROW      0x0044DEA0u
/* A level record's display name, which is what the list shows. The record's
 * first field is the id ADDR_FIND_LEVEL_RECORD keys on; this is 0x44 in. */
#define LEVEL_OFF_NAME           0x44u
/* 0x0043ED40 is a one-instruction accessor for it, called twice per
 * iteration of the SELECT MAP loop; reading the global says the same. */
#define AM2_MP_SELECT_MAP_SIZE   0x68u
#define ADDR_OPEN_SELECT_PLAYER  0x00451910u  /* void(void) */
#define ADDR_SELECT_PLAYER_CTOR  0x00451400u  /* thiscall obj *(obj, bmp) */
#define AM2_SELECT_PLAYER_SIZE   0x68u
#define ADDR_OPEN_ENTER_NAME     0x00451E10u  /* void(void) */
#define ADDR_ENTER_NAME_CTOR     0x00451AF0u  /* thiscall obj *(obj, bmp) */
#define AM2_ENTER_NAME_SIZE      0x84u
#define VTABLE_ENTER_NAME        0x0046FBBCu
#define ADDR_STR_NAME_BMP        0x0048BAA4u /* "02_006_00_name.bmp" */
/* The screen's own name buffer -- the edit box writes straight into it, and
 * the screen is 0x84 bytes so it has 0x20 to work with against a limit of
 * 0x18. Still the original's two handlers either side of it. */
#define ENTER_NAME_OFF_TEXT      0x64u
#define AM2_ENTER_NAME_MAX       0x18
#define ADDR_ON_ENTER_NAME_OK    0x00451990u
#define ADDR_ON_ENTER_NAME_CANCEL 0x00451AC0u
/* THE WAR MENU -- START A WAR, JOIN A WAR, CANCEL. This was ADDR_OPEN_CD_PROMPT
 * and the constructor below was ADDR_CD_PROMPT_CTOR, on the strength of
 * "Copy Protection" and "The ARMYMEN2 CD must be in the drive to play Army
 * Men II." Those two strings are in 0x0042F290, which is the START A WAR
 * BUTTON's handler; the constructor pushes neither, and what it does push is
 * three bitmap triples called host, join and cancel. Named from a call site
 * once more, and the screen is what COMM. CHANNEL SELECT's SELECT reaches.
 *
 * ITS TWO BUTTONS DIFFER BY ONE FIELD AND IT IS COMM_OFF_IS_HOST. START A WAR
 * asks for AM2_MENU_REQUEST_BATTLE_NAME with it set; JOIN A WAR asks for
 * AM2_MENU_REQUEST_BATTLE_JOIN with it clear. That is what settles which
 * bitmap belongs to which, rather than the y coordinates -- though those
 * agree: 222 and 262 are the two centres tools/ab.sh multi already clicks. */
#define ADDR_OPEN_WAR_MENU       0x0042F440u  /* void(void) */
#define ADDR_WAR_MENU_CTOR       0x0042EED0u  /* thiscall obj *(obj, bmp) */
#define AM2_WAR_MENU_SIZE        0x64u
#define VTABLE_WAR_MENU          0x0046F9E4u
/* Still original: it is one of the three MessageBoxA sites docs/boundary.md
 * reports, and the CD check in front of it is patched to jump past. */
#define ADDR_ON_START_WAR        0x0042F290u  /* void(AM2_Widget *) */
#define AM2_BMP_HOST0            0x00486F54u /* 03_107_0N_host.bmp */
#define AM2_BMP_HOST1            0x00486F68u
#define AM2_BMP_HOST2            0x00486F7Cu
#define AM2_BMP_JOIN0            0x00486F18u /* 03_108_0N_join.bmp */
#define AM2_BMP_JOIN1            0x00486F2Cu
#define AM2_BMP_JOIN2            0x00486F40u
#define AM2_BMP_CANCEL0          0x00486ED0u /* 03_110_0N_cancel.bmp */
#define AM2_BMP_CANCEL1          0x00486EE8u
#define AM2_BMP_CANCEL2          0x00486F00u
#define ADDR_OPEN_BATTLE_NAME    0x0042FF60u  /* void(void) */
#define ADDR_BATTLE_NAME_CTOR    0x0042FB00u  /* thiscall obj *(obj, bmp) */
#define AM2_BATTLE_NAME_SIZE     0xA4u
#define ADDR_OPEN_BATTLE_JOIN    0x0042F880u  /* void(void) */
#define ADDR_BATTLE_JOIN_CTOR    0x0042F4C0u  /* thiscall obj *(obj, bmp) */
#define AM2_BATTLE_JOIN_SIZE     0x88u
#define VTABLE_BATTLE_JOIN       0x0046F9F8u
#define ADDR_BATTLE_JOIN_DELETE  0x0042F850u /* thiscall obj *(obj, flags) */
#define ADDR_BATTLE_JOIN_DESTRUCT 0x0042F870u /* thiscall void(obj) */
#define ADDR_OPEN_MOVIES         0x0044E6A0u  /* void(void) */
#define ADDR_MOVIES_CTOR         0x0044DFA0u  /* thiscall obj *(obj, bmp) */
#define VTABLE_MOVIES            0x0046FACCu
/* Its destructor pair, and one of the four this file used to list as "an SEH
 * frame and real work" without saying what the work was. It releases the
 * twelve thumbnail PAIRS at MOVIES_OFF_SPRITES -- 24 sprites, pair[0] then
 * pair[1], stepping eight -- and then chains to the dialog base. The layout it
 * walks is the one MOVIES_OFF_SPRITES already recorded, which is what makes
 * this a confirmation rather than a second reading. */
#define ADDR_MOVIES_DELETE       0x0044E4F0u /* thiscall obj *(obj, flags) */
#define ADDR_MOVIES_DESTRUCT     0x0044E510u /* thiscall void(obj) */
#define ADDR_STR_MOVIES_BMP      0x0048B6F8u /* "02_012_00_movies.bmp" */
/* Twelve thumbnail PAIRS at 0x0064, three pages of four, and the four button
 * pointers after them at 0x00C4. The sprite ids are set 3, indices 0xC8..0xCB
 * and then 0xD2..0xD9 -- a gap the screen does not care about, because the
 * pairs land in twelve contiguous slots either way. */
#define MOVIES_OFF_SPRITES       0x0064u
#define MOVIES_OFF_BUTTONS       0x00C4u
#define AM2_MOVIE_PAGE_SIZE      4
#define AM2_MOVIE_SET            3
#define AM2_MOVIE_INDEX_A        0xC8   /* four */
#define AM2_MOVIE_INDEX_B        0xD2   /* eight */
/* The button's own slot for which movie it shows; 0x0044E610 reads it. */
#define MOVIE_BUTTON_OFF_INDEX   0x58u
/* Which page of four is showing, 0 to 2, wrapped by 0x0044E580. */
#define ADDR_MOVIE_PAGE          0x0065A060u /* int32_t */
/* How many movies have been unlocked. The screen shows the first button
 * unconditionally and each of the other three only if this is past 0, 1, 2. */
#define ADDR_MOVIE_COUNT         0x00512328u /* int32_t */
#define ADDR_ON_MOVIE_PLAY       0x0044E610u
#define ADDR_ON_MOVIE_NEXT_PAGE  0x0044E580u
/* The twelve movie filenames, indexed by the button's own slot. */
#define ADDR_MOVIE_NAMES         0x0048AE98u /* const char *[] */
#define ADDR_MOVIE_TO_PLAY       0x00511B08u /* char[], the chosen filename */
/* The in-mission overlay mode the movie player runs under. Equal to
 * AM2_MENU_REQUEST_MOVIES by coincidence and NOT the same constant: one
 * indexes the 21-arm menu table and the other the 13-arm sub-state table. */
#define AM2_MENU_MODE_MOVIE      0x0Du
#define AM2_BMP_BACK19_0         0x0048B6BCu /* 03_019_0N_back.bmp */
#define AM2_BMP_BACK19_1         0x0048B6D0u
#define AM2_BMP_BACK19_2         0x0048B6E4u
#define AM2_BMP_EGG0             0x0048B680u /* 03_031_0N_egg.bmp */
#define AM2_BMP_EGG1             0x0048B694u
#define AM2_BMP_EGG2             0x0048B6A8u
#define AM2_MOVIES_SIZE          0xD4u
#define ADDR_OPEN_OPTIONS_MENU   0x0044FDD0u  /* void(void) */
#define ADDR_OPTIONS_MENU_CTOR   0x0044FAB0u  /* thiscall obj *(obj, bmp) */
#define AM2_OPTIONS_MENU_SIZE    0x64u
#define ADDR_OPEN_CONTROLS       0x00451210u  /* void(void) */
#define ADDR_CONTROLS_CTOR       0x00450E10u  /* thiscall obj *(obj, bmp) */
#define AM2_CONTROLS_SIZE        0xB8u
#define ADDR_OPEN_DIFFICULTY     0x0044EAD0u  /* void(void) */
#define ADDR_DIFFICULTY_CTOR     0x0044E730u  /* thiscall obj *(obj, bmp) */
#define AM2_DIFFICULTY_SIZE      0x68u
#define ADDR_OPEN_QUIT_CONFIRM   0x0044EE50u  /* void(void) */
#define ADDR_QUIT_CONFIRM_CTOR   0x0044EB50u  /* thiscall obj *(obj, bmp) */
#define AM2_QUIT_CONFIRM_SIZE    0x64u
#define ADDR_OPEN_REPLAY_PROMPT  0x0044F220u  /* void(void) */
#define ADDR_REPLAY_PROMPT_CTOR  0x0044EED0u  /* thiscall obj *(obj, bmp) */
#define AM2_REPLAY_PROMPT_SIZE   0x64u
#define ADDR_OPEN_DELETE_PLAYER  0x00450B70u  /* void(void) */
#define ADDR_DELETE_PLAYER_CTOR  0x00450730u  /* thiscall obj *(obj, bmp) */
#define AM2_DELETE_PLAYER_SIZE   0x64u

/* A `char *` to the string "unknown", parked immediately after the keyword
 * table -- it points at 0x0048825C, which is the table's own end address. */
#define ADDR_SCRIPT_UNKNOWN_STR  0x00488258u

/* The game's own statically linked MSVC CRT. Blocks cross between our code and
 * the original's, so the allocator has to be the same one; see game/crt.h. */
#define ADDR_CRT_MALLOC          0x004647F8u
#define ADDR_CRT_REALLOC         0x004646D8u
#define ADDR_CRT_GETCWD          0x00465DB5u  /* _getcwd(buf, max) */
#define ADDR_CRT_CHDIR           0x00465ED0u  /* _chdir: SetCurrentDirectoryA
                                               * then GetCurrentDirectoryA */
#define ADDR_CRT_FREE            0x004646A9u
#define ADDR_SCRIPT_DECLARE_VAR  0x0043F7A0u  /* handle(const char *, kind, init) */
/* MissionEnded, and it was ADDR_SCRIPT_FIND_FILE -- a name from the one thing
 * anybody had looked at, the "%s%d.txt" it builds. Probing for the next
 * sub-mission is its FIRST test and not its job: past that it advances the
 * campaign, saves the platoon to save\default.cof, unlocks a movie in
 * Options.cfg, picks the film to play and asks for the state that plays it.
 * Renamed, not aliased.
 *
 * ITS ONE ARGUMENT IS "LOST", and the paths are what say so: zero with a level
 * in hand takes the advance, and anything else takes the arm that chooses one
 * of three loss movies. */
#define ADDR_MISSION_ENDED       0x00421890u  /* void(int32_t lost) */
/* The two the record carries for it, and the field the campaign's movie
 * unlock is maxed from. The trio at +0x104 is three 0x40-byte names picked
 * from at random; +0xC4 is the single one a win plays. */
#define LEVEL_OFF_WIN_MOVIE      0x0C4u  /* char[0x40] */
#define LEVEL_OFF_LOSE_MOVIES    0x104u  /* char[3][0x40] */
#define AM2_LEVEL_MOVIE_BYTES    0x40u   /* the stride of that array */
#define LEVEL_OFF_MOVIE_INDEX    0x308u  /* int32_t, maxed into ADDR_MOVIE_COUNT */
#define AM2_LOSE_MOVIE_COUNT     3
#define AM2_LEVEL_STR_BYTES      0x40u  /* the record's string field size */
/* The probe buffer MissionEnded builds "<tileset><n>.txt" in, and the
 * _finddata_t beside it. Both are the original's stack sizes, taken off the
 * `sub esp, 0x218` and the two `lea`s into it. */
#define AM2_LEVEL_PROBE_BUF      0x10
#define AM2_FIND_DATA_BYTES      0x110
#define ADDR_STR_GRAVE_MOVIE     0x00478810u /* "grave", the fallback */
#define ADDR_STR_LEVEL_FILE_FMT  0x00478818u /* "%s%d.txt" */
/* What the loss path leaves in ADDR_MENU_REQUEST. Nothing else in the image
 * writes it, and the dispatch table's arms stop at 0x15, so this is a request
 * the menu router will not recognise -- reproduced without an explanation. */
#define AM2_MENU_REQUEST_LOST    0x12u

/* ReadScript names itself: "ReadScript: Could not open %s for ...". It fopen's
 * the file, fgets a line at a time, tokenises, and dispatches on the first
 * token of each statement.
 *
 * A token is 12 bytes: kind at +0, id at +8. The dispatch tests kind == 2,
 * which the kind array calls "Reserved" -- a keyword -- and then switches on
 * the id, which is the number docs/scripttokens.md lists against each word. So
 * every one of these handlers is named by the language rather than by us, and
 * the five of them are the whole top-level grammar: exactly the statements a
 * mission file contains. */
/* The parse context and the token record, read out of the `variable` handler
 * at 0x00443F70 -- 368 bytes and every field it touches is one of these.
 *
 *   ctx + 0x04   token count
 *   ctx + 0x08   token array
 *   token + 0x00 kind      (the array at 0x00487C74 names them)
 *   token + 0x04 text      the word as it appeared, used in error messages
 *   token + 0x08 id        the number docs/scripttokens.md lists
 *
 * Every handler is called as handler(int32_t *pos, ctx *), where *pos is the
 * index of the keyword and the handler advances it over what it consumes.
 *
 * One thing to know before writing a caller: an UNQUOTED identifier tokenises
 * as kind 5, which the kind array calls "String". `variable stopcloning 0`
 * requires kind 5 for the name. So "String" here means a word, not a quoted
 * literal, and kind 6 "Name" is something else again. */
#define AM2_SCRIPT_TOKEN_SIZE    12u
/* AddToken stores its second argument at +0x00, its fourth at +0x04 and the
 * value at +0x08 -- so the middle field is the LINE NUMBER, not the text. The
 * earlier labels here (TOK_TEXT/TOK_ID) were read off a call site rather than
 * out of the body, which is the mistake this project keeps making. */
#define AM2_SCRIPT_TOK_KIND      0x00u
#define AM2_SCRIPT_TOK_LINE      0x04u
#define AM2_SCRIPT_TOK_VALUE     0x08u
#define AM2_SCRIPT_CTX_CAPACITY  0x00u
#define AM2_SCRIPT_CTX_COUNT     0x04u
#define AM2_SCRIPT_CTX_TOKENS    0x08u

#define ADDR_SCRIPT_PRELOADSPRITE 0x00444900u  /* keyword 25 */
/* What the preloadsprite statement drives. Named from the filenames its
 * callee at 0x004457E0 builds -- "%02d_%03d_%02d_*.bmp" and the matching
 * ".sha" -- so the statement's three integers are a sprite identity triple. */
/* 0x00414AD0 and 0x00414B00, and the pair only makes sense together. The
 * first turns the player's own army into a SPRITE INDEX offset -- army times
 * 100, with anything above 3 clamped to 0, so army 4 shares army 0's block.
 * The second adds that offset to an index and calls PreloadSprite, retrying
 * with the raw index when the load failed AND the offset was non-zero. So the
 * sprite table has a hundred indices per army and a shared block beneath, and
 * army 0 never retries because its offset is already zero. Reconstructed --
 * and 70 calls on a driven Boot Camp mission cover none of that, because the
 * player is army 0 there and the offset is 0. */
#define ADDR_ARMY_SPRITE_BASE     0x00414AD0u  /* int32_t(void) */
#define ADDR_PRELOAD_ARMY_SPRITE  0x00414B00u  /* void *(set,index,frame,flags) */
#define AM2_ARMY_SPRITE_BLOCK     100
#define ADDR_PRELOAD_SPRITE       0x00445B00u
/* 0x00445AD0, 15 callers: the same thing addressed by a PACKED KEY. It splits
 * the key into PackKey's three fields and passes them as the first three
 * arguments, which is what ties the key format to the sprite lookup. */
#define ADDR_PRELOAD_SPRITE_KEY   0x00445AD0u  /* sprite *(key, int32, int32) */
/* What PreloadSprite calls once it has decided the sprite is not loaded. The
 * first builds the two filenames above, chdirs with ADDR_SET_DATA_DIR, and
 * fills the record; the second grows the registry and puts the record in it.
 * Both stay original -- they are the bitmap loader and the table, not the
 * cache decision this port is taking over. */
#define ADDR_SPRITE_LOAD_TRIPLE   0x004457E0u  /* int32(spr,a,b,c,flags) */
/* What ADDR_SPRITE_LOAD_TRIPLE hands the work to when ADDR_OPT_DF is set,
 * which is every run that does not pass -df: the same {set, index, frame} out
 * of the packed .dat rather than off the disk. It seeks and validates -- its
 * own two messages are "Error seeking to location %d in data file." and
 * "Error in validating object in data file." */
#define ADDR_SPRITE_LOAD_DF       0x00423FE0u  /* int32(spr,set,idx,frm,flags) */
/* The .sha half of a loose sprite: a 1-bit DIB read into spr->overlay. Named
 * from its own "ERROR: %s not in 1-bit mode\n". */
/* READ, and it does more than load: it RUN-LENGTH ENCODES the 1-bit DIB into
 * the sprite's overlay, one stream with a {width, height} header, a table of
 * per-row 16-bit offsets, and alternating zero/one run lengths capped at 255.
 * The whole thing is built in a 32 KB stack buffer and then copied into a
 * malloc of exactly the length used. Reconstructed; see win32/sprite.cpp.
 *
 * Its three failure messages are its own: "ERROR: %s not in 1-bit mode",
 * "ERROR: invalid file size in %s.", and silence for a file that will not
 * open. It answers the encoded length, or 0. */
#define ADDR_LOAD_SHADOW_BMP      0x00423300u  /* int32(const char *, spr) */
/* Where the .sha's own header fields land. The DIB is read as raw offsets,
 * the way LoadDibFlipped reads its own, so that nothing here has to name a
 * Win32 structure -- and the two that matter are not where a bitmap reader
 * would look for them: biSizeImage divided by biHeight is the STRIDE, and
 * biXPelsPerMeter and biYPelsPerMeter carry the sprite's HOT SPOT. The
 * AM2_Sprite comment already records that smuggling from the other end. */
#define BMPFILE_OFF_BITS           10u   /* bfOffBits */
#define BMPINFO_OFF_WIDTH          4u
#define BMPINFO_OFF_HEIGHT         8u
#define BMPINFO_OFF_BITCOUNT       14u
#define BMPINFO_OFF_SIZEIMAGE      20u
#define BMPINFO_OFF_XPELS          24u
#define BMPINFO_OFF_YPELS          28u
#define AM2_BMPFILE_HDR_BYTES      14u
#define AM2_BMPINFO_HDR_BYTES      0x28u
/* The clamp the hot spot gets on the way in: anything outside this either way
 * is dropped to zero, per axis. Signed word compares. */
#define AM2_SHA_HOT_LIMIT          0x800
/* What a .sha leaves the sprite as. */
#define AM2_SPR_FORMAT_SHADOW      4
#define SPR_FLAG_HAS_OVERLAY       0x20u
#define AM2_RLE_RUN_MAX            0xFF
#define AM2_STR_SHA_NOT_1BIT       0x00478A34u /* "ERROR: %s not in 1-bit mode\n" */
#define AM2_STR_SHA_BAD_SIZE       0x00478A10u /* "ERROR: invalid file size in %s.\n" */
/* One directory name per sprite SET, indexed by the set number. Sets 0..19 are
 * the fixed ones -- "00-cursors", "01-title", "10-dash", "19-other" -- and
 * 20 and up are the map's own art, which is why those get the loaded map's
 * directory in front. Entry 24 is ADDR_DIR_SCRATCH, so one set's directory is
 * whatever was last written there. */
#define ADDR_SPRITE_SET_DIRS      0x00489554u  /* const char *[] */
#define ADDR_STR_FMT_DIR_SUB      0x00489738u  /* "%s\\%s" */
#define ADDR_STR_FMT_S            0x004852B4u  /* "%s" */
#define ADDR_STR_GLOB_BMP         0x00489720u  /* "%02d_%03d_%02d_*.bmp" */
#define ADDR_STR_GLOB_SHA         0x00489708u  /* "%02d_%03d_%02d_*.sha" */
#define ADDR_STR_SPRITE_MISSING   0x004896ECu  /* "Sprite file not found %s\n" */
#define ADDR_SPRITE_REGISTER      0x004459E0u  /* void(spr, id) */
#define ADDR_SCRIPT_PAD           0x004440E0u  /* keyword 26 */

/* Pads -- the script's trigger regions. Two tables: one of pad records in
 * definition order, and one indexed by the pad NUMBER a script gives, which
 * several pads may share. */
#define ADDR_PADS                 0x00516198u  /* AM2_Pad[], stride 72 */
#define ADDR_PAD_COUNT            0x00511DF8u
/* 0x00437A50, one caller -- the per-frame path. Walks every pad and pushes the
 * repeat deadline of each one that has a period forward, once the game clock
 * has passed it. The stride is 72 and the two fields are AM2_Pad's +0x38 and
 * +0x3C, which is how the loop's bare 0x005161D4 resolves. */
#define ADDR_PAD_ADVANCE_DEADLINES 0x00437A50u  /* void(void) */
/* 0x00425E70, one caller -- also per-frame. Re-resolves the three object
 * context slots from their uids. */
#define ADDR_REFRESH_OBJ_CTX      0x00425E70u  /* void(void) */
/* The third member of the ADDR_OBJ_CTX_* triple. What distinguishes it from
 * VAL and VAL_PREV is not established; it is named for its position. */
#define ADDR_OBJ_CTX_VAL_A        0x00511E24u
#define ADDR_OBJ_CTX_OBJ_A        0x005122C8u
#define ADDR_PAD_NUMBERS          0x0051F198u  /* AM2_PadNumber[], stride 76 */
#define ADDR_PAD_FINALISE         0x004375A0u  /* void(AM2_Pad *, int32_t) */
/* The two halves of the pad walk, 0x004376C0 and 0x00437770, called from
 * ADDR_OBJ_TILE_HOOK when an object's tile changes -- once for the pad
 * numbers it has just entered and once for those it has just left. Mirror
 * images, which is what makes the item count certain. */
#define ADDR_PAD_NUMBER_ENTER     0x004376C0u  /* void(obj, AM2_PadNumber *) */
#define ADDR_PAD_NUMBER_LEAVE     0x00437770u
#define ADDR_STR_PAD_UNDERFLOW    0x004877C0u
/* AM2_Pad's fields, named from what PadFinalise does with them. The pad is a
 * TRIGGER: a value, an operator and a threshold that ScriptCompare answers,
 * an "inside" flag, and two event ids with a uid slot each -- one for
 * entering and one for leaving. The two halves are mirror images, which is
 * what makes the pairing certain: entering clears the LEAVE uid and arms the
 * enter event, leaving does the opposite. */
#define PAD_OFF_ID                0x00u
#define PAD_OFF_COMPARED          0x0Cu  /* 1 once a <, = or > was parsed */
#define PAD_OFF_SPECIFIC          0x10u  /* ObjMatchesSel's `byName` */
#define PAD_OFF_TRIGGER           0x14u  /* its selector: flags, or a name */
#define AM2_PAD_STRIDE            72u
#define AM2_PAD_NUMBER_STRIDE     76u
/* An AM2_PadNumber as ObjTileHook reads it: a count, then that many int16 pad
 * ids, and a pointer at +0x48 that must be non-null before the ids are walked.
 * The eight is ADDR_PAD_BIT_TABLE's length, and the bit layer has exactly one
 * bit per pad-number slot. */
#define PADNUM_OFF_COUNT          0x00u  /* int16_t */
#define PADNUM_OFF_IDS            0x02u  /* int16_t[], into ADDR_PADS */
#define PADNUM_OFF_PADS           0x48u  /* null means nothing to walk */
#define AM2_PAD_BITS              8
#define PAD_OFF_CMP_OP            0x18u  /* ScriptCompare's middle argument */
#define PAD_OFF_CMP_A             0x1Cu  /* the threshold the script wrote */
/* ScriptCompare's THIRD argument, and the program names it: the leave half of
 * the pad walk logs "pad # %d thinks there are less than zero items on it"
 * when decrementing it goes negative. So a pad's trigger is its threshold
 * compared against HOW MANY ITEMS ARE STANDING ON IT, and this field is that
 * count -- bumped by PadNumberEnter and dropped by PadNumberLeave, once per
 * matching object. It was PAD_OFF_CMP_B, which was a position and not a
 * meaning. Renamed, not aliased. */
#define PAD_OFF_ITEM_COUNT        0x30u
#define PAD_OFF_EVENT_ENTER       0x20u
#define PAD_OFF_EVENT_LEAVE       0x24u
#define PAD_OFF_UID_ENTER         0x28u
#define PAD_OFF_UID_LEAVE         0x2Cu
/* The damage a pad does to whatever is standing on it, read by ObjTileHook:
 * DamageObject(obj, PAD_OFF_DAMAGE, PAD_OFF_DAMAGE_KIND, 0, 0, 0), gated on
 * the amount being non-zero and on ADDR_GAME_CLOCK_MS having passed
 * PAD_OFF_DAMAGE_DUE. The argument order is the call's, not a guess -- the
 * amount is pushed second and the kind third. */
#define PAD_OFF_DAMAGE            0x34u
#define PAD_OFF_DAMAGE_DUE        0x3Cu  /* mission ms */
#define PAD_OFF_DAMAGE_KIND       0x40u
#define PAD_OFF_INSIDE            0x44u
#define AM2_PAD_NOTIFY_ENTER      3
#define AM2_PAD_NOTIFY_LEAVE      2

/* The two map layers the centroid scan reads, one byte per cell. The first
 * holds a pad number per cell and serves numbers 8 and above; the second holds
 * a bitmask and serves 0..7, with ADDR_PAD_BIT_TABLE giving the bit. */
#define ADDR_MAP_PAD_LAYER        0x00514EC8u
#define ADDR_MAP_PADBIT_LAYER     0x00514EC4u
#define ADDR_PAD_BIT_TABLE        0x00486444u  /* int32_t[8], 1<<n */
#define ADDR_SCRIPT_VARIABLE      0x00443F70u  /* keyword 133 */
#define ADDR_SCRIPT_IF            0x004432F0u  /* keyword 44 */

/* The sub-parsers `if` drives. All four stay the original's. */
#define ADDR_SCRIPT_SCAN_FOR      0x00442F10u  /* int32_t(ctx,from,want,stop) */
#define ADDR_SCRIPT_PARSE_EVENT   0x0043FF90u  /* (ctx,at,&a,&b,&c) */
#define ADDR_SCRIPT_HIT_TARGET    0x0043FAB0u  /* (ctx,at,&mask) */
#define ADDR_SCRIPT_LOCATION      0x004409F0u  /* (ctx,at,action,quiet) */

/* The action record is a struct now -- AM2_ScriptAction in game/script.h --
 * with its layout asserted against these offsets at compile time. */
#define ADDR_SCRIPT_ORDER_TARGET  0x0043FCF0u  /* (ctx,at,&form,&val,&army) */
#define ADDR_SCRIPT_PARSE_EVENTS  0x00440600u  /* (ctx,at,cond) */

/* The uids the four army keywords and `me` stand for. Not in keyword order,
 * and the first arm of the resolver's jump table serves id 15 -- which no
 * entry in the keyword table produces, so it cannot be reached. */
/* "unreachable" was true of the RESOLVER's jump table -- no keyword produces
 * id 15 -- and false of the global itself. Two functions, 0x0041F570 and
 * 0x0041F5C0, compare a name index against it directly and take a special
 * path when it matches. So the id is unreachable and the NAME is not. */
#define ADDR_SVAR_ID15            0x00656474u  /* not reachable via the table */
#define ADDR_SVAR_GREEN           0x00656484u
#define ADDR_SVAR_TAN             0x00656498u
#define ADDR_SVAR_BLUE            0x00656454u
#define ADDR_SVAR_GREY            0x0065646Cu
/* ADDR_SVAR_ME at least is a name-table INDEX rather than a uid: ResolveUid
 * bounds its argument against ADDR_SCRIPT_NAME_COUNT and then compares it
 * against this. The group comment above says "uids"; that is right for what a
 * type-0 entry's value holds and wrong for this global. */
#define ADDR_SVAR_ME              0x00656458u
/* The other three DeclareBuiltinNames makes, and these are type 3 -- ordinary
 * script INTEGERS -- where the six above are type 2. Their values come from
 * the game rather than from a literal: the difficulty, ADDR_FAST_MACHINE, and
 * a zero that LoadDefaultCof later overwrites with the trooper count. */
#define ADDR_SVAR_DIFFICULTY      0x0065649Cu
#define ADDR_SVAR_SYSTEMSPEED     0x00656470u
#define ADDR_SVAR_NUMGREEN        0x00656350u
/* The six names' own strings. `all` is the one ADDR_SVAR_ID15 holds the index
 * of, which is what makes that global's comment concrete: the id is
 * unreachable through the resolver's jump table and the NAME is `all`. */
#define ADDR_STR_SVAR_ALL         0x00488900u
/* The four colour names are ADDR_STR_GREEN, ADDR_STR_TAN, ADDR_STR_BLUE and
 * ADDR_STR_GREY, already in this file -- the alias ratchet refused a second
 * set the moment they were added, which is the rule doing its job on the very
 * commit that documents it. */
#define ADDR_STR_SVAR_ME          0x004887C8u
#define ADDR_STR_DIFFICULTY       0x00488898u
#define ADDR_STR_SYSTEMSPEED      0x0048888Cu
/* The uids the five keywords stand for. `all` and `green` are the SAME value;
 * see script.cpp. */
#define AM2_SVAR_UID_ALL          0x64
#define AM2_SVAR_UID_GREEN        0x64
#define AM2_SVAR_UID_TAN          0xC8
#define AM2_SVAR_UID_BLUE         0x190
#define AM2_SVAR_UID_GREY         0x12C
#define AM2_SVAR_UID_ME           1
/* 0x0043F6D0, one caller -- the state-2 ENTRY, beside LoadDefaultCof. Empty
 * the script name table and put the nine built-in names back. Reconstructed. */
#define ADDR_DECLARE_BUILTIN_NAMES 0x0043F6D0u  /* void(void) */
#define ADDR_SCRIPT_PARSE_VALUE   0x00443010u  /* (ctx,at,&a,&b,&c) */
#define ADDR_SCRIPT_PARSE_ACTION  0x00440D70u  /* (ctx,at,uint8_t[0x48]) */

/* Head of the condition list; each record links through its +0x30. */
#define ADDR_SCRIPT_CONDITIONS    0x00510214u

/* event.cpp's registration table and the three things DeclareRuleVars does to
 * it. The table is 1024 buckets of 16-byte nodes at 0x005101F0, chained
 * through +0x0C and keyed on the first two arguments of the register call.
 * All four stay original -- what is reconstructed is the declaring, not the
 * table. */
/* The registration table: NINE buckets at 0x005101F0, ending exactly where
 * ADDR_SCRIPT_CONDITIONS begins, which is how the count is known -- the
 * teardown's loop bound says so. It went in here as 1024, invented.
 *
 * A bucket holds a chain of 16-byte entries {key0, key1, handlers, next}, and
 * each entry a chain of 16-byte handlers {fn, arg, owns, next}. `owns` is the
 * sixth argument to the register call: set, the teardown frees `arg` as well
 * as the node. DeclareRuleVars passes 0 for it, so the conditions it registers
 * are not freed by the table that points at them. */
#define ADDR_EVENT_TABLE         0x005101F0u
#define AM2_EVENT_BUCKETS        9
#define AM2_EVENT_NO_KEY         (-2)         /* key0 == -2 registers nothing */
#define ADDR_EVENT_REGISTER      0x0041EE70u  /* void(bucket,k0,k1,fn,arg,owns) */
#define ADDR_EVENT_CLEAR_ALL     0x004223D0u  /* void(void), frees every node */
/* 0x00422450. Drop the whole script/event state in one go: the name table,
 * the condition list, the registration table, and one flag. All three callees
 * are already ours, so this is pure orchestration -- but the ORDER is the
 * content, and it is names, then conditions, then registrations. Role name;
 * one caller. */
#define ADDR_RESET_SCRIPT_STATE  0x00422450u  /* void(void) */
#define ADDR_SCRIPT_STATE_FLAG   0x00511DFCu  /* cleared by the reset above */
/* 0x0041FEA0. Look a uid up and, if it resolves, hand the object to
 * 0x00428DA0 -- 96 bytes with twenty-two callers, unnamed. Both role names:
 * neither says anything about itself. */
#define ADDR_EVT_OBJ_ACTION      0x0041FEA0u  /* void(uint32_t uid) */
/* 0x0041FE70. Look a uid up and deploy the object. DeployItem names itself --
 * "DeployItem(resurrection): uid:%x, health:%d" -- and takes the point to
 * deploy at; passing 0 there means "where it already is". */
#define ADDR_EVT_DEPLOY_ITEM     0x0041FE70u  /* void(uint32_t, uint32_t) */
#define ADDR_DEPLOY_ITEM         0x00428CA0u  /* void(obj, point, int32, int32) */
/* The two per-type arms ADDR_DEPLOY_ITEM dispatches to, and the message it
 * sends afterwards. Two of the three name themselves: "DeployTrooper: uid:%x,
 * pos=(%d,%d)" and "itemDeployMessageSend: uid=%x, pos=(%d,%d), facing=%d".
 * The vehicle one does not, and takes its name from the dispatch index in the
 * same way ADDR_DAMAGE_VEHICLE does -- a method that has now been confirmed
 * twice by a sibling that does carry a string. */
#define ADDR_DEPLOY_TROOPER      0x00449250u  /* type 2 */
/* Reconstructed. The type-2 twin of ADDR_DEPLOY_VEHICLE and near-identical to
 * it, which is exactly why the DIFFERENCES matter -- writing the second from
 * the first erases all three.
 *
 *   - it clears ten dwords from OBJ_OFF_SIGHT_OUT_T2 (0x57C) where the vehicle
 *     clears from OBJ_OFF_FIELD_578. Not arbitrary: each deploy clears ITS OWN
 *     TYPE'S sight-output block, and the two records start four bytes apart.
 *   - RowUpdate takes force 1 here and 0 there.
 *   - an OBJ_OFF_SARGE guard decides whether rank and repair-frame are cleared.
 *
 * It names itself -- "DeployTrooper: uid:%x, pos=(%d,%d)" -- behind
 * COMM_OFF_VERBOSE, so unlike the vehicle its identity needs no inference from
 * the dispatch index.
 *
 * The tail is a real rule rather than bookkeeping: past the `resurrect` gate,
 * a deployed OBJ_OFF_SARGE belonging to ADDR_DEFAULT_OWNER calls SelectUnit on
 * itself when ADDR_SELECTED_COUNT is not positive -- a fresh Sarge selects
 * himself if nothing else is selected.
 *
 * And it carries the SAME dead store as its sibling: OBJ_OFF_AI_MODE = 6 for a
 * foreign owner, overwritten by an unconditional = 1 later with no read
 * between. Two siblings with one dead assignment is a template artefact, not a
 * misreading -- which is what makes reproducing it the right call. */
#define ADDR_FMT_DEPLOY_TROOPER  0x0048A4ACu
        /* "DeployTrooper: uid:%x, pos=(%d,%d)\n" -- and the uid it
         * prints is OBJ_OFF_OWNER at +4, which the log calls a uid. */
#define OBJ_OFF_WEAPON_UID       0x54Cu  /* handed to WeaponByUid */
#define OBJ_OFF_FIELD_568        0x568u
#define OBJ_OFF_FIELD_574        0x574u  /* int16_t, the facing widened */
#define OBJ_OFF_FIELD_580        0x580u
#define OBJ_OFF_FIELD_584        0x584u
#define OBJ_OFF_FIELD_588        0x588u
#define OBJ_OFF_FIELD_58C        0x58Cu
#define OBJ_OFF_FIELD_598        0x598u
#define AM2_DEPLOY_KIND_DONE     5      /* the kind reassignment's no-op value */
#define ADDR_DEPLOY_VEHICLE      0x0045B9F0u  /* type 3 */
/* Reconstructed. Place a vehicle: clear OBJ_FLAG_DESTROYED, take the nearest clear
 * point for its facing, stamp the tile height, relink row 0 and every sub-part
 * (stride AM2_OBJ_ROW_STRIDE, each offset by row 0's sprite attach point --
 * the same idiom ObjMoveAlongFacing uses), clear the destination to
 * ADDR_ZERO_POINT, reset the type-2 fields, wipe ten dwords of the AI sight
 * block from OBJ_OFF_FIELD_578, then re-stamp the facing into four fields and
 * finish with SetKindFrames and ObjSetFootprint.
 *
 * ORDER MATTERS AND READS BACKWARDS: the ten-dword clear covers 0x578..0x5A0,
 * and the facing is stamped into 0x578 and 0x579 AFTERWARDS. Writing the
 * stamps first, which is how the function reads in prose, zeroes them.
 *
 * AND THE ATTACK MODE IS DEAD CODE. A vehicle whose owner is not the local
 * army gets OBJ_OFF_AI_MODE = 6 -- attack -- and then the unconditional
 * `= 1` a few stores later overwrites it, with no call or read in between.
 * `edi` is 1 from well before the branch and is not reassigned. Reproduced,
 * not fixed; no A/B could ever see it, since both sides do the same thing.
 * Same family as PlaySoundAt's PointsEqual branch that never fires. */
#define OBJ_OFF_FACING_COPY2       0x579u  /* beside OBJ_OFF_FIELD_578 */
#define AM2_DEPLOY_CLEAR_DWORDS    10      /* 0x578..0x5A0 */
#define ADDR_PLACE_OBJ           0x00429220u  /* everything else; name ours */
#define ADDR_ITEM_DEPLOY_MSG     0x0042AA50u  /* void(obj, int32 resurrect) */
/* 0x0041FBE0 and 0x0041FC10 are the same shim twice: uid >= 1000, look it up,
 * and if ObjIsType2 call one function on it -- 0x00448170 for the first and
 * 0x00448220 for the second. Neither callee names itself and object type 2 is
 * one of the three CLAUDE.md still lists as unidentified, so these are role
 * names and the pair is distinguished only by which callee it reaches. */
#define ADDR_EVT_TYPE2_ACTION_A  0x0041FBE0u  /* void(uint32_t uid) */
#define ADDR_EVT_TYPE2_ACTION_B  0x0041FC10u  /* void(uint32_t uid) */
#define ADDR_TYPE2_ACTION_A      0x00448170u  /* void(void *obj) */
#define ADDR_TYPE2_ACTION_B      0x00448220u  /* void(void *obj) */
/* A third of the same twin, with an argument to pass on. */
#define ADDR_EVT_TYPE2_ACTION_C  0x0041FBA0u  /* void(uint32_t, int32_t) */
/* 0x0041FC80, one caller, and the `dropitem` action's handler: find a weapon
 * uid among the trooper's inventory slots 1..5 and TrooperDropItem it at a
 * point. Both uids are refused below AM2_UID_COUNTER_START. */
#define ADDR_EVT_DROP_ITEM       0x0041FC80u  /* void(uint32,uint32,uint32) */
/* 0x0041FDB0, one caller. Attach every object an army owns -- optionally only
 * those whose ADDR_OBJ_FIELD_A matches -- to one target object. */
#define ADDR_EVT_ARMY_ATTACH     0x0041FDB0u  /* void(army, filter, uid) */
#define AM2_ATTACH_ANY           (-1)         /* the filter that matches all */
#define ADDR_TYPE2_ACTION_C      0x004480E0u  /* void(void *obj, int32_t) */
/* 0x0041F6E0. The one that does NOT null-check: it passes whatever LookupByUID
 * returned straight on. 0x00428370 has eight callers and no name. */
#define ADDR_EVT_OBJ_SET         0x0041F6E0u  /* void(uint32_t, int32_t) */
#define ADDR_HEAL_OBJECT         0x00428370u  /* void(obj, int32 pct, void *src) */
/* 0x0041F710. The most guarded member of the family: uid threshold, pointer,
 * a flag bit CLEAR at +8, and a positive int16 at +0x62, all before it acts.
 * Its callee has nineteen callers and no name. */
#define ADDR_EVT_GUARDED_ACTION  0x0041F710u  /* void(uint32_t, int32, int32) */
/* 0x00428140, 560 bytes, NINETEEN callers. Damage one object.
 *
 * It was ADDR_DAMAGE_OBJECT, which this file admitted was a role rather than
 * a recovered name. The body settles it: it dispatches on the object's type
 * through a jump table at 0x0042834C to a per-type damage handler, calls
 * ADDR_DAMAGE_BROADCAST -- a name that was already here -- and, when the
 * health it just reduced has reached zero, runs the death sequence. One of the
 * nineteen callers is the "suicide kings" cheat, which sets health to 1 and
 * then calls this.
 *
 * None of the ten functions below names itself in a string, so all ten names
 * are OURS and describe roles. The four per-type ones are the safest of them:
 * their evidence is the jump table's index, not a call site. Types 4 to 7 have
 * no handler and fall straight to the common tail.
 *
 * Arguments: the object, an amount, a kind, the attacker's uid, a fourth value
 * passed only to the per-type handler, and a fifth that suppresses the
 * multiplayer broadcast when non-zero. */
#define ADDR_DAMAGE_OBJECT       0x00428140u  /* void(obj,int32,int32,uid,
                                               *      int32,int32) */
#define ADDR_DAMAGE_ITEM         0x004356C0u  /* type 1. Reconstructed. */
/* Three object flags DamageItem is the only reader of, named for the
 * MECHANISM rather than for what it is probably modelling. Damage kind 1 --
 * whatever it is -- clamps the armour to zero, is refused outright by
 * IMMUNE_KIND1, and on an object carrying SEQ_ON_KIND1 starts a kind-7
 * sequence lasting (health + 4) * 250 and sets SEQ_STARTED so it starts only
 * once. Calling them fireproof, flammable and burning would read better and
 * assert more than the body does. */
#define OBJ_FLAG_IMMUNE_KIND1    0x4000u
#define OBJ_FLAG_SEQ_ON_KIND1    0x1000u
#define OBJ_FLAG_SEQ_STARTED     0x400000u
#define AM2_SEQ_LIFE_PER_HP      250
#define AM2_SEQ_LIFE_BIAS        4
/* The set a destroyed item must be in before any of the spawn tail runs. */
#define AM2_DAMAGE_ITEM_SET      0x1D
/* What a destroyed item leaves behind. A WATCHED kind spawns 0x81 with the
 * owner's uid; anything else spawns one of three kinds by the record's index,
 * with a life to match -- 0x14 and 0x15 get their own and everything else
 * shares the third. */
#define AM2_SPAWN_WATCHED        0x81
#define AM2_SPAWN_INDEX_A        0x14
#define AM2_SPAWN_INDEX_B        0x15
#define AM2_SPAWN_KIND_A         0x8A
#define AM2_SPAWN_LIFE_A         0x78
#define AM2_SPAWN_KIND_B         0x83
#define AM2_SPAWN_LIFE_B         0x96
#define AM2_SPAWN_KIND_C         0x8B
#define AM2_SPAWN_LIFE_C         0x32
/* ONE READER IN THE WHOLE IMAGE AND NO WRITER ANYWHERE. DamageItem passes it
 * to SpawnAt as the sixth argument, and it is .bss, so it is always zero --
 * which makes the multiplayer test beside it observably dead, since the arm
 * that avoids this global passes a literal zero instead. Named for its
 * address because nothing establishes a meaning; same standing as
 * MISSILE_OFF_GROUND. */
#define ADDR_UNUSED_662288       0x00662288u  /* int32_t, never written */
/* 0x00435650, one caller -- and that caller is ADDR_DAMAGE_ITEM itself, so
 * the two are mutually recursive. Damage an item and then every item in the
 * chain hanging off it, OBJ_OFF_CHAIN_UID then OBJ_OFF_CHAIN_NEXT_UID. */
#define ADDR_DAMAGE_ITEM_CHAIN   0x00435650u  /* void(obj,int32,int32,
                                               *      int32,uint32) */
#define ADDR_DAMAGE_TROOPER      0x00447A40u  /* type 2 -- and this one
                                               * is NOT a guess: it logs
                                               * "DamageTrooper: droping armor
                                               * uid:%x". Reconstructed. */
#define ADDR_STR_DROPPING_ARMOR  0x0048A248u  /* the misspelling is the
                                               * original's */
/* Its own constants. The soldier kind is left as a NUMBER: 3 gets a damage
 * discount here and is the only kind the gore roll applies to, and neither
 * says what it is -- the three kinds LoadType2 gives their own arm are 6, 7
 * and 8, and this is none of them. */
#define AM2_SOLDIER_KIND_3       3
#define AM2_SPEECH_SARGE_HURT    0x19
#define AM2_SND_HURT             4
#define AM2_SND_HURT_KIND7       0x35
#define AM2_EXPL_TROOPER_GORE    0x95
/* The gore roll: the hit must arrive within this many eighths of a turn of the
 * facing, and then one draw in 256 -- or, when the damage kind is 1 or 3, any
 * angle at all and ten draws in 256. */
#define AM2_GORE_ANGLE           0x40
#define AM2_GORE_ODDS            1
#define AM2_GORE_ODDS_KIND13     0xA
/* Half the time, near enough, the hurt sound goes out with flag bit 1 set.
 * Same VALUE as AM2_HIT_EFFECT_CHANCE and a different question, so a different
 * name -- the lesson three collapsed duplicates in this file already teach in
 * the other direction. */
#define AM2_TROOPER_SOUND_ODDS   0x40
/* Five (dx, dy) int16 pairs: on the spot, then forty units each way. Sarge's
 * inventory is dropped one item per step, and the loop is bounded by the
 * inventory rather than by the table -- a sixth item walks off the end. */
#define ADDR_DROP_RING           0x00489DE8u  /* int16[5][2] */
#define AM2_DROP_RING_STEPS      5
/* The slot the death drop empties, over and over: dropping compacts the array,
 * so slot 1 refills until the inventory is empty. */
#define AM2_TROOPER_DROP_SLOT    1
#define ADDR_DAMAGE_VEHICLE      0x0045B4D0u  /* type 3 -- reconstructed */
/* 0x00461BA0, two callers, and they are DamageTrooper and DamageVehicle. The
 * mark a hit leaves at a point: both damage handlers roll for it the same way
 * and pass the same four things. Name ours, from those two call sites agreeing
 * -- which is two more than most of this file's role names get. */
#define ADDR_SPAWN_HIT_EFFECT    0x00461BA0u /* void(const AM2_Point *, slot,
                                              *      dir, height) */
#define AM2_HIT_EFFECT_CHANCE    0x40   /* of 255, and only above 1 damage */
/* Written by the CHEAT RUNNER at 0x00417B80, twice, and read by exactly the
 * two damage handlers -- where it makes our own army take no damage at all in
 * a single-player game. "I am the Juggernaut!" is one of the four cheat lines
 * this image carries, and this is what it sets. */
#define ADDR_CHEAT_INVULNERABLE  0x00512358u  /* int32_t */
#define ADDR_DAMAGE_ROACH        0x0043D280u  /* type 8 */
/* The two event kinds these notifiers raise. Both come from the literal each
 * pushes as EventNotify's `type`, and the pair is what makes them readable:
 * 5 is raised after damage, 6 after a heal. */
#define AM2_EVENT_KILLED         4
#define AM2_EVENT_DAMAGED        5
#define AM2_EVENT_HEALED         6
/* Kind 7, and the script's own vocabulary is what names it. The five two-party
 * event keywords are `killed` (57), `hit` (58), `healed` (59), `pickedup` (61)
 * and `dropped` (62), and four notifier bodies of one template sit
 * consecutively at 0x00427E10, 0x00427E80, 0x00427EF0 and 0x00427F60 raising
 * kinds 5, 6, 7 and 8, with kind 4 at 0x00427FD0. Kinds 4, 5 and 6 are already
 * killed/hit/healed, so 7 and 8 are the remaining two keywords in order.
 *
 * The call sites settle both rather than leaving either to the ordering.
 * Every caller of 0x00427EF0 is a pickup and each names itself --
 * "TrooperPickupItem %x", "TrooperHostApprovedPickupItem %x" and
 * "TrooperRemotePickupItem %x" -- and the ONE caller of 0x00427F60 names
 * itself "TrooperDropItem  %x". So kind 8 is not an elimination either. */
#define AM2_EVENT_PICKED_UP      7
#define AM2_EVENT_DROPPED        8
/* Called twice by ADDR_DAMAGE_OBJECT and by nothing else. Reconstructed. */
#define ADDR_NOTIFY_DAMAGED      0x00427E10u  /* void(obj, void *attacker) */
/* 0x00427D40, fifteen callers. The event MASK for an object: one bit per army,
 * chosen from the object's owner through the comm slot lookup at 0x0040F190,
 * then narrowed by its type. It is what ADDR_NOTIFY_DAMAGED and
 * ADDR_NOTIFY_HEALED pass to EventNotify's maskA and maskB, and event.h
 * already calls those parameters masks -- which is what grounds this name. */
#define ADDR_OBJ_EVENT_MASK      0x00427D40u  /* int32_t(const void *obj) */
/* The death sequence, in the order ADDR_DAMAGE_OBJECT runs it. The first two
 * name THEMSELVES -- "TriggerItemDestroyed, item uid=%x, by uid = %x" and
 * "Send Death Message: uid %x, army %d" -- so neither is ours. The first is
 * reconstructed and is the third member of the kind 4/5/6 notifier family. */
#define ADDR_TRIGGER_ITEM_DESTROYED 0x00427FD0u /* void(obj, void *attacker) */
#define ADDR_SEND_DEATH_MESSAGE  0x0042A930u  /* void(obj, uid, int32) */
#define ADDR_OBJ_DEATH_CLEANUP   0x00428070u  /* void(obj) */
/* The counterpart of ADDR_SELECT_UNIT, which sits 0x60 above it. */
/* 0x00427BA0, nine callers. Deselect EVERYTHING: walk ADDR_SELECTED_UIDS,
 * clear OBJ_FLAG_SELECTED on each object that still resolves, drop the ones
 * that do not, then empty the list and tell ADDR_ON_SELECTION_CHANGED.
 * Reconstructed; 2 calls on a driven Boot Camp mission. */
#define ADDR_DESELECT_ALL        0x00427BA0u  /* void(void) */
#define ADDR_DESELECT_UNIT       0x00427C80u  /* void(obj) */
/* The count word of ADDR_SELECTED_UIDS, which is {capacity, count, items}. */
#define ADDR_SELECTED_COUNT      0x0051230Cu  /* int32_t */
/* The SELECTION MARKERS: a caret over the army's leader and a health bar under
 * every selected unit. It is also where dead and concealed selections are
 * DROPPED -- a paint that edits the list it is drawing, which is worth knowing
 * before treating it as read-only.
 *
 * ITS THREE BAR COLOURS ARE EACH ALREADY NAMED FROM SOMEWHERE ELSE:
 * ADDR_HUD_MESSAGE_COLOUR under a quarter health, ADDR_COLOUR_LAG_MID under a
 * half, ADDR_VIEW_RECT_COLOUR above it. So the same three bytes serve the comm
 * latency readout and a health traffic light. Recorded rather than renamed --
 * they are not adjacent in memory, so calling them a palette would be a guess.
 *
 * ONE LOCK AND TWO UNLOCKS, and that is the original's. Only the leader's
 * sprite is drawn into locked bits; every bar is a ClearRegion, which BLITS
 * and must not be inside a lock. The second UnlockSurface is a no-op, since
 * UnlockSurface is gated on ADDR_SURFACE_LOCKED. */
#define ADDR_SELECTED_ITEMS      0x00512310u  /* uint32_t *, the uids */
/* An ARRAY of preloaded sprites and its count, built by 0x00463060 through
 * PreloadSprite and freed by 0x00463200. Entry 0 is the leader's caret; the
 * multiplayer set is the pair at 0x0048CBB8/0x0048CBBC, allocated only when
 * ADDR_MP_SESSION is set. */
#define ADDR_MARK_SPRITES        0x0048CBB0u  /* AM2_Sprite ** */
#define ADDR_MARK_SPRITE_COUNT   0x0048CBB4u  /* int32_t */
#define AM2_MARK_LEADER          0        /* the entry drawn over the leader */
#define AM2_LEADER_MARK_DY       0x0C     /* below the leader's own point */
/* The `test [obj+8], 0x204` that drops an entry. Both bits already have names
 * one screen apart in this file -- OBJ_FLAG_DESTROYED and OBJ_FLAG_CONCEALED
 * -- so this is spelled out of them rather than given a third. The flag
 * cleared on the way out is OBJ_FLAG_SELECTED, likewise already named. */
#define AM2_MARK_GONE            (OBJ_FLAG_DESTROYED | OBJ_FLAG_CONCEALED)
#define AM2_MARK_TROOPER_W       0x18     /* a type-2's bar, 6 px below it */
#define AM2_MARK_TROOPER_DY      6
#define AM2_MARK_WIDE_W          0x3C     /* everything else */
#define AM2_MARK_BIG_DY          0x30     /* VEHICLE_OFF_KIND 5 */
/* Otherwise the drop comes off the sprite: how far its bounds reach below the
 * hot spot, rounded DOWN to a multiple of 8, plus 8. */
#define AM2_MARK_DY_MASK         0xFF8u
#define AM2_MARK_DY_BIAS         8
/* Set by ADDR_DESTROY_OBJ_COMMON; every reader treats it as "already gone". */
#define OBJ_FLAG_DESTROYED       0x04u
/* Promoted out of army.cpp, which had it as a local, so item.cpp can use the
 * same one rather than a second copy that could drift. 7 is the only value
 * anything compares it against, and army.h records what that means: a type 2
 * carrying 7 is never friendly in a multiplayer session. NOT the AI mode --
 * that is +0xE4, per ADDR_EVT_SET_AI_MODE.
 *
 * (That paragraph is still accurate; what it was WRONG about was the field's
 * name, which the note below settles.) */
/* RESOLVED. This was OBJ_OFF_MP_ROLE, recorded as probably a soldier kind and
 * left alone for want of a witness. ADDR_SET_SOLDIER_KIND is that witness and
 * it is unambiguous: the value it stores here is the same value it uses to
 * index ADDR_SOLDIER_ANIMS -- `lea eax,[kind*8 + 0x659F00]` -- and hangs off
 * the object's first row as the animation set. It is a KIND.
 *
 * "MP role" came from the one comparison anything makes against it, `== 7`,
 * plus army.h's note that a type 2 carrying 7 is never friendly in
 * multiplayer. That is still true; 7 is simply one kind of soldier, and
 * SetSoldierKind gives it 1.5x health and a random name from a 64-entry
 * table, which is what a special unit looks like.
 *
 * Renamed rather than aliased: ten use sites outside this file. */
#define OBJ_OFF_SOLDIER_KIND     0x544u
#define ADDR_SET_SOLDIER_KIND    0x00449570u  /* void(obj, kind) -- the writer */
/* 0x0045B000, three callers. The OTHER writer of OBJ_OFF_SOLDIER_KIND: set it
 * and put the object's rows on the frames that go with it, unless a row is
 * mid-animation. */
#define ADDR_SET_KIND_FRAMES     0x0045B000u  /* void(void *obj, int32_t) */
/* Eight int32 frame ids indexed by kind: 81, 81, 80, 82, 83, 85, 4, 5. Past
 * the eighth is string data, and nothing bounds the index. */
#define ADDR_KIND_FRAMES         0x0048BE30u  /* int32_t[8] */
#define AM2_KIND_FRAMES          8
#define AM2_SECOND_ROW_FRAME     0x50  /* what row 1 always gets, no table */
/* 0x00449660, sixteen callers. It was ADDR_UNIT_ACTION, "void(obj, action) --
 * 44 arms", and that was wrong about the argument and about the arms.
 *
 * The argument is a WEAPON CODE -- the dword an item's OBJ_OFF_FIELD_C0 record
 * points at, the same value HeldWeaponCode returns and the same one
 * SelectInventorySlot already indexes ADDR_WEAPON_HANDLERS with. Every caller
 * computes it the same way: put a weapon in the unit's hand, read the code,
 * call this.
 *
 * And there are SEVEN arms, not 44. The 44 is the size of a byte index table
 * at 0x00449728 which collapses `code` into one of eight jump-table slots at
 * 0x00449708; six of those slots differ only in the constant they hand
 * ADDR_SET_SOLDIER_KIND, one is the default, and one is a bare `ret`. Counting
 * a dense switch's index table as arms is how 44 got written down.
 *
 * The mapping is code 2->2, 3->3, 4->1, 5->4, 20->5, 43->7, code 0 writes
 * NOTHING at all, and everything else -- including anything above 43, since
 * the bound is unsigned -- gives kind 0. Kind 6 is never produced.
 *
 * Reconstructed, 6 calls on a driven Boot Camp mission. It takes
 * ADDR_SET_SOLDIER_KIND's counter to 0 with it: that is now called by name
 * from here and nowhere else. */
#define ADDR_SOLDIER_KIND_FOR_WEAPON 0x00449660u /* void(unit, int32 code) */
/* 0x004069B0, one caller. Pick the inventory slot whose weapon has the highest
 * ADDR_MAP_CODE, record it in UNIT_OFF_INVENTORY_SEL, and apply the soldier
 * kind that goes with it. */
#define ADDR_SELECT_BEST_WEAPON  0x004069B0u  /* void(void *unit) */
/* 0x00406AB0, SelectBestWeapon's twin: the same six-slot walk scored by
 * ADDR_WEAPON_RANK instead of ADDR_MAP_CODE, and it does NOT apply a soldier
 * kind afterwards. */
#define ADDR_SELECT_RANKED_WEAPON 0x00406AB0u /* void(void *unit) */
/* The scorer is ADDR_MAP_CODE_18_28, already reconstructed as MapCode18To28
 * -- 0x18 -> 8, 0x19 -> 2, 0x1A -> 1, 0x27 -> 4, 0x28 -> 6, everything else
 * 0. Do not give it a second name here; that is what the alias ratchet
 * refused. */
/* What SetSoldierKind reaches. The frame setter compares against the row's
 * current frame and returns early unless forced; the pose table is int32 and
 * ships {1, 1, 5, 3, ...}. */
/* Reconstructed in maprow.cpp. 11,698 calls on a driven Boot Camp mission.
 * Four of the frame values are not frames: -2 does nothing, -1 means the
 * frame already set, 0 clears bit 0 of the row and returns. */
#define ADDR_SET_ANIM_FRAME      0x0040A1A0u  /* void(row, int16 frame, int32) */
/* Reconstructed in item.cpp. Its two tables are inside its own 208 bytes: an
 * ARM index, one byte per weapon code 1..0x2B, and the four-entry jump table
 * it selects. Three of the four arms answer from the object's class and its
 * OBJ_OFF_FIELD_578; the fourth falls through to ADDR_POSE_BY_CLASS, which is
 * also where a null weapon and an out-of-range code go. */
#define ADDR_WEAPON_POSE_INDEX   0x004494A0u  /* int32(obj, weapon) */
/* NOT ADDR_WEAPON_POSE_FRAMES, which is 0x1A0 bytes earlier. This one is
 * indexed by ClassifyByCode74's answer -- 0, 1 or 2 -- and gives 1, 4 and 6:
 * the poses a unit takes with no weapon to hold. The entries past the third
 * are never reached, so its length is not established. */
#define ADDR_POSE_BY_CLASS       0x00475180u  /* int32_t[] */
/* Set with a weapon in hand, the pose is one of a higher set. Which is why
 * OBJ_OFF_FIELD_578 is worth a second look: it selects between {1, 4} and
 * {0x19, 0x1A} for one family of weapons and {1, 4} and {0x1F} for another,
 * and it does nothing at all for class 2, which always answers 6. */
#define AM2_POSE_CLASS2          6
#define AM2_POSE_STAND           1
#define AM2_POSE_STAND_ARMED     0x19
#define AM2_POSE_KNEEL           4
#define AM2_POSE_KNEEL_ARMED_A   0x1A
#define AM2_POSE_KNEEL_ARMED_B   0x1F
#define AM2_WEAPON_CODE_MAX      0x2B
#define ADDR_WEAPON_POSE_FRAMES  0x00474FE0u  /* int32[] */
/* Kind 7's extra: a 64-entry table of names filled at runtime. The health
 * multiplier that used to be named here was the shared double below. */
#define ADDR_KIND7_NAMES         0x0050712Cu  /* char *[] */
/* The double 1.5, in .rdata, and it is SHARED. It went in as
 * AM2_KIND7_HEALTH_SCALE because kind 7's health was the first thing seen to
 * multiply by it; RegionFindPath weights its A* heuristic by the same address,
 * where a health name would be a lie. A pooled floating-point literal is not a
 * concept and must not be named for one of its users -- the linker folds equal
 * constants, so any name taken from a single use site is one more use away from
 * being wrong. Named for its VALUE. */
#define AM2_CONST_1_5            0x0046FA98u  /* double 1.5, several users */
#define AM2_KIND7_NAME_COUNT     64
/* Fields of the object's FIRST ROW that carry its animation. */
#define ROW_OFF_ANIM_CUR         0x40u   /* AM2_AnimTable * */
/* 0x0040A130, two callers. The reader the ROW_OFF_FIELD_2C block above
 * predicts: look an animation up in the row's current table by id and answer
 * its AM2_Anim::field4, DOUBLED when the row's lut is ADDR_ROW_LUT_DOUBLES. */
#define ADDR_ROW_ANIM_FIELD4     0x0040A130u  /* int16_t(row *, uint16_t id) */
#define ROW_OFF_ANIM_NEXT        0x48u   /* AM2_AnimTable *, taken up next */
/* An int16, and StepRowAnim shows it is TWO THINGS by range. Below 1000 it is
 * the frame id SetAnimFrame matched on and the stepper ignores it. At exactly
 * 1000 the stepper takes up ROW_OFF_ANIM_NEXT_ID; above 1000 it counts DOWN
 * one per step and does nothing else. So values over 1000 are a delay in
 * FRAMES before the queued animation starts, and 1000 is that delay expired. */
#define ROW_OFF_FRAME            0x4Cu   /* int16_t */
#define AM2_ROW_DELAY_BASE       0x3E8   /* the delay's zero point */
#define AM2_ROW_ANIM_HOLD        (-2)    /* ANIM_NEXT_ID: stay on the last cell */
/* Two more, and SetAnimFrame is what establishes both: it searches the table
 * at ROW_OFF_ANIM_CUR for an entry whose id is ROW_OFF_FRAME, stores that
 * entry's AM2_Anim here, and resets the cell index to 0. So +0x44 is the
 * animation actually PLAYING, chosen out of the table four bytes below it,
 * and +0x51 is how far into its cells the row has got.
 *
 * RowAnimFinished reads both back and agrees: it compares +0x51 against
 * anim->frames - 1 and indexes anim->cells with it. */
#define ROW_OFF_ANIM_PLAYING     0x44u   /* AM2_Anim * */
#define ROW_OFF_ANIM_NEXT_ID     0x4Eu   /* int16_t, the entry's `next` */
/* One byte per FRAME ID, added to a row's heading when that frame is taken up.
 * It is 0 almost everywhere: of the entries this reads, only index 19 is
 * non-zero and it is 0xC0 -- three quarters of a turn on an 8-bit heading. So
 * one animation is drawn facing backwards and the table exists for it. */
#define ADDR_FRAME_HEADING_BIAS  0x004740CCu  /* uint8_t[] */
/* A byte LUT, filled at 0x0040A5C2 and 0x0040A5D7 and compared by ADDRESS at
 * six sites including this one. A row whose MAPOBJ_OFF_LUT is THIS lut gets
 * ROW_OFF_FIELD_3C doubled; every other lut takes the value unchanged. What
 * makes it special is not established -- the name says which one it is. */
#define ADDR_ROW_LUT_DOUBLES     0x004F9EDCu  /* uint8_t[] */
#define ROW_OFF_CELL             0x51u   /* uint8_t, index into anim->cells */
/* Two bytes ADDR_ROW_FACE_SPRITE adds together and hands to RoundTo8 with the
 * animation's directionBits, so their sum is an 8-BIT HEADING. Which is which
 * is not established -- the step at ADDR_STEP_ROW_ANIM reads both as well and
 * neither is written here -- so they are named for the pair they form. */
#define ROW_OFF_HEADING          0x50u   /* uint8_t */
#define ROW_OFF_HEADING_BIAS     0x5Cu   /* uint8_t, added to the above */
/* 0x0040A310, three callers -- and they are the type 2, 3 and 8 frame
 * steppers, so this runs per unit per frame. Point the row's sprite at the
 * animation cell for the heading it is facing.
 *
 * The cell chosen is `(dir + 1) * frames - 1`, which is the LAST frame of
 * that direction rather than the first. The sprite is only swapped when it
 * differs from the one the row already has. */
#define ADDR_ROW_FACE_SPRITE     0x0040A310u  /* void(void *row) */
/* 0x0041D3D0, three callers. Put a new sprite on a row, rebuilding its cell
 * buffer only when the new sprite needs MORE cells than the old one did.
 *
 * The cell count is the arithmetic RowAlloc uses -- and NOT the same types.
 * RowAlloc multiplies two int8 and stores a byte; this multiplies two int32
 * and compares the result against that byte. A sprite big enough to overflow
 * the byte therefore looks larger here than the row can ever record, so it
 * rebuilds every time. Both are the original's; see maprow.cpp.
 *
 * A NULL sprite returns without touching anything, which is what makes its
 * callers' unguarded use safe. */
#define ADDR_ROW_SET_SPRITE      0x0041D3D0u  /* void(row, sprite, desc) */
/* 0x0040A2D0, six callers. Whether the row's animation has reached its last
 * cell AND that cell's hold has elapsed -- so it is a "the animation is over"
 * test rather than a per-cell tick. Three ways out and all of them 0: bit 0
 * of the row's flags clear, no animation playing, or the cell index short of
 * the last. The name is ours.
 *
 * IT IDENTIFIES AM2_AnimCell's FIRST FIELD. anim.h says of `field0` that the
 * loader only ever copies it; this adds it to ROW_OFF_STAMP_54 and compares
 * the sum against ADDR_GAME_CLOCK_MS, so it is a hold in milliseconds. */
#define ADDR_ROW_ANIM_FINISHED   0x0040A2D0u  /* int32_t(const void *row) */
/* 0x00428E00, seven callers. Step every one of the object's rows, then copy
 * the FIRST row's +0x3C into the object's +0x44 -- an int16 sign-extended
 * into a dword. Note the object's +0x44 and a row's +0x44 are different
 * fields of different structures; only the row's is an animation. */
#define ADDR_STEP_OBJ_ROWS       0x00428E00u  /* void(void *obj) */
#define ADDR_STEP_ROW_ANIM       0x0040A380u  /* void(void *row) */
#define ROW_OFF_FIELD_3C         0x3Cu   /* int16_t -> OBJ_OFF_FIELD_44 */
/* int32_t, copied from ROW_OFF_FIELD_3C above. ObjMoveAlongFacing multiplies
 * it by ADDR_FRAME_DELTA_SEC and projects the result through Cos8/Sin8 into
 * the position, so it is a SPEED in units per second and the animation is
 * what supplies it. Left named for its offset: twenty sites across two files
 * read it, and renaming them is its own change with its own verification. */
#define OBJ_OFF_FIELD_44         0x44u
/* 0x0042B210, five callers. ADDR_POINT_OF_TILE's two-pointer twin: the same
 * arithmetic, written back as a pair of int32 rather than packed into one
 * dword. Both add 8, which is the centre of a 16-pixel tile. */
#define ADDR_TILE_TO_XY          0x0042B210u  /* void(int32 tile,int32*,int32*) */
/* Four fields SetSoldierKind writes and nothing read so far explains. 0x578 is
 * cleared for EVERY kind; the other three belong to kind 7. */
#define OBJ_OFF_FIELD_578        0x578u  /* int32_t, cleared unconditionally */
/* TWO name indices, four bytes apart, and both live -- ten sites each. +0x5A8
 * is the kind-7 one, into ADDR_KIND7_NAMES, which SetSoldierKind writes. +0x5AC
 * is a different table entirely: ADDR_SOLDIER_NAMES, 62 records of
 * {const char *name; int32_t taken} holding "R. Pavey", "D. Lee",
 * "J. Wildblood" and the rest of the team -- the developers, as it turns out.
 * ADDR_TAKE_SOLDIER_NAME hands out an index into it.
 *
 * THE RECORD BASE IS 0x00489BF8 AND THIS MACRO USED TO BE 0x00489BFC, the
 * taken column. Both readings index correctly, because the stride is 8 either
 * way and TakeSoldierName only ever touches the flag -- so the error was
 * invisible until SoldierNameOf needed the name at +0 of the same record. The
 * base is the base now, and TakeSoldierName adds the offset. */
#define OBJ_OFF_FIELD_5A8        0x5A8u  /* int32_t, the random name index */
#define OBJ_OFF_NAME_INDEX       0x5ACu  /* into ADDR_SOLDIER_NAMES */
#define ADDR_SOLDIER_NAMES       0x00489BF8u  /* {const char *; int32 taken} */
#define SOLDIER_NAME_OFF_NAME    0x00u
#define SOLDIER_NAME_OFF_TAKEN   0x04u
#define AM2_SOLDIER_NAME_BYTES   0x08u
#define AM2_SOLDIER_NAMES        0x3E         /* 62 of them */
/* 0x00447570, one caller. Take an unused soldier name: start at a random
 * index, walk forward to the first free one, mark it and return it. If every
 * name is taken it returns the STARTING index without marking, so the caller
 * gets a name already in use rather than a failure. Reconstructed. */
#define ADDR_TAKE_SOLDIER_NAME   0x00447570u  /* int32_t(void) */
/* 0x004475C0, two callers, both in the HUD. Copy a type 2's personal name out
 * of ADDR_SOLDIER_NAMES into the caller's buffer, or leave it empty. */
#define ADDR_SOLDIER_NAME_OF     0x004475C0u  /* void(char *, const void *) */
#define AM2_ANIM_TABLE_BYTES     8u      /* ADDR_SOLDIER_ANIMS' stride */
#define AM2_MP_ROLE_SEVEN        7
/* Both read only by ADDR_OBJ_DEATH_CLEANUP and neither established further:
 * the field gates the first delayed event, the flag suppresses the second. */
#define OBJ_OFF_FIELD_94         0x94u
/* Three more UpdateTrooperAction reads: how many enforced turns have been
 * charged, the running total of how far they turned it, and when the medic
 * tent last healed. All named structurally -- one consumer each. */
#define OBJ_OFF_FIELD_D4         0xD4u   /* uint8, incremented per turn */
#define OBJ_OFF_FIELD_D6         0xD6u   /* int16, wrapped into +/-0x100 */
#define OBJ_OFF_FIELD_5B0        0x5B0u  /* game-clock ms */
/* 0x004572A0, two callers. Reset a type 2's transient state.
 *
 * WHAT IT CLEARS WAS ALREADY NAMED, and the offset ratchet is what said so:
 * six of the eleven fields had meaningful names already -- the four script
 * fields, OBJ_OFF_FIELD_C0's weapon record, OBJ_OFF_FOLLOW_UID, and the
 * OBJ_OFF_HIT_DIR / OBJ_OFF_HIT_TIME pair. Naming them a second time by
 * offset would have made a coherent function look like eleven unknowns.
 * Only four needed new names, and they are named by offset because nothing
 * read so far says what they hold. */
/* The uid of what this object is ENGAGING, beside OBJ_OFF_FOLLOW_UID's uid of
 * what it is following. Named from a writer/reader pair rather than from one
 * side: ADDR_AI_BUILD_CONTEXT resolves it with LookupUid into the record's
 * +0x10, and AiStepDefend writes the uid of whatever 0x00403B40 found into it
 * in the same breath as promoting that object into +0x10. It was
 * OBJ_OFF_FIELD_CC, named by offset because nothing read then said what it
 * held. */
#define OBJ_OFF_TARGET_UID       0xCCu   /* uint32_t */
/* Cleared by ResetObjOnCof and compared against ADDR_GAME_CLOCK_MS by two
 * steppers in the 0x004057xx band, which is what makes "deadline" the reading
 * rather than the offset. */
#define OBJ_OFF_DEADLINE_D0      0xD0u
#define OBJ_OFF_FACING_COPY      0xF8u   /* stamped from OBJ_OFF_FACING */
#define OBJ_OFF_FIELD_FC         0xFCu
#define OBJ_OFF_FIELD_100        0x100u
/* THE POINT A ROUTE WAS LAST PLANNED TO, and it was OBJ_OFF_ROUTE_GOAL --
 * "0x103 dwords, cleared wholesale" -- because the only toucher looked at was
 * a bulk memset. A wholesale clear tells you the EXTENT of something and
 * nothing about the field it starts at, which is the same weakness this file
 * already records for a name taken from a `free`.
 *
 * Two readers say what it is. AiRouteToward writes the destination here in the
 * same breath as OBJ_OFF_MOVE_TO, and AiTrooperStep compares it against the
 * goal at OBJ_OFF_FIELD_C0 with PointsEqual to decide whether the route it has
 * is still the route it wants. So it is a packed point, and the memset that
 * named it merely happens to begin here.
 *
 * It also TILES with what follows: the goal, then the retry deadline at +0x11C,
 * then the waypoint list at +0x120. */
#define OBJ_OFF_ROUTE_GOAL       0x118u  /* packed point */
#define AM2_OBJ_TAIL_DWORDS      0x103u
#define ADDR_RESET_TYPE2_FIELDS  0x004572A0u  /* void(void *obj) */
/* And 0x94 is type-dependent too, on the same evidence as 0xA0 above.
 * ADDR_STEP_TYPE1_4 dereferences it -- `mov eax,[obj+0x94]; cmp [eax],0x2A` --
 * and misc.cpp's MeetsAllThree does the same for 0x1F, so for those objects it
 * is a POINTER to a record whose first dword is a code. Type2ActionC stores
 * the literal 1 into it, which is not a pointer. Different type arms of the
 * same union, recorded where the offset is rather than argued in one file. */
/* Incremented from Type2ActionC's argument and stored; 0x0044BBA0 answers
 * whether it is positive. Structural until something says what it counts. */
#define OBJ_OFF_FIELD_5A4        0x5A4u  /* int32_t */
/* 0x0044BBA0, four callers: false unless the object is a type 2 AND its
 * OBJ_OFF_FIELD_5A4 is positive. Type2ActionA refuses when it is true, so
 * whatever that counter tracks is a reason not to re-arm. */
#define ADDR_TYPE2_FIELD5A4_SET  0x0044BBA0u /* int32_t(const void *obj) */
/* The weapon Type2ActionA gives out, and the action code it then runs -- the
 * same 0x2B in both places, which is what ties the two together. */
#define AM2_WEAPON_KEY_2B        0x2B
#define OBJ_FLAG_8000            0x8000u
/* The two delays that cleanup schedules, in game-clock milliseconds. */
#define AM2_DEATH_DELAY_SHORT    0xBB8      /* 3,000 ms */
#define AM2_DEATH_DELAY_LONG     0x493E0    /* 300,000 ms */
/* The death message: 16 bytes, type 0x23. */
#define AM2_MSG_DEATH            0x23
#define AM2_MSG_DEATH_BYTES      0x10
#define OBJ_OFF_FLAGS8           8u    /* bit 2 blocks the action above */
#define OBJ_FLAG8_BLOCKED        4u
#define OBJ_OFF_COUNT62          0x62u /* int16; must be > 0 */

/* Two more of the "On" shape: take a uid, substitute the object's own position
 * for a point, and call the twin that takes the point directly. event.h
 * already records the pattern for EvtPlaySoundAt/EvtPlaySoundOn -- these are
 * two more pairs, and the "At" halves are themselves small and unreconstructed
 * (0x0041F820, 0x0041F8B0, and 0x0041F780 for a third pair). */
#define ADDR_EVT_AT_OBJ_POS_A    0x0041F880u  /* void(int32, uint32, int32) */
#define ADDR_EVT_AT_OBJ_POS_B    0x0041F970u  /* void(int32,int32,uint32,int32) */
/* The "At" halves are not plain point-takers, which the "On" wrappers above
 * made them look like. Each takes a UID as well, and a `relative` flag: when
 * it is set the object's own position is ADDED to the point rather than
 * replacing it. That flag is AM2_ScriptAction.relative -- the leading `+` on a
 * script's coordinates -- so this is where that syntax is honoured. */
#define ADDR_AT_POINT_A          0x0041F820u  /* void(uid, point, relative) */
/* NOT a peer of the two shims above, despite the matching address band: it
 * resolves an ARMY and walks every object that army owns. */
#define ADDR_EVT_ARMY_AT_POINT   0x0041F8B0u  /* void(army,filter,point,rel) */
#define ADDR_ARMY_OBJ_LISTS      0x004F9ECCu  /* one list per comm slot */
#define ADDR_LIST_REMOVE_AT      0x0042A750u  /* thiscall(list, index) */
#define LIST_OFF_COUNT           4u
#define LIST_OFF_UIDS            8u
#define ADDR_AT_POINT_C          0x0041F780u  /* void(uid, point, relative) */
/* The "On" wrapper for AT_POINT_C, exactly the shape of EvtAtObjPosA. */
#define ADDR_EVT_AT_OBJ_POS_C    0x0041F7F0u  /* void(uid, uid, int32) */
/* Two 32-byte shims that pass the ADDRESS of their first argument to a
 * function above the nominal CRT line -- game code, per tools/crt.py, not
 * library. Neither callee names itself. */
#define ADDR_EVT_BY_REF_A        0x0041FD10u  /* void(int32_t, int32_t) */
#define ADDR_EVT_BY_REF_B        0x0041FD30u  /* void(int32_t, int32_t) */
/* Two of the SEQ family -- see ADDR_SEQ_RUN for where the word comes from.
 * Both add one 48-byte record to ADDR_SEQ_CTX_A at a map point, and they
 * differ only in the KIND they stamp, the sprite they take and their default
 * lifetime. A third, 0x00461660, is the same shape with kind 6, and kind 7
 * calls it with a random 0..2.
 *
 * The names were ADDR_BY_REF_ACTION_A and _B, from the one thing their call
 * sites showed: a point passed by reference. What they DO is add a seq, and
 * that is now the name. What a seq is FOR is still not established -- but
 * "adds a record of kind 5" is a fact about the function where "by ref" was a
 * fact about its argument list. */
#define ADDR_SEQ_ADD_KIND5       0x00462000u  /* void(pt*, owner, life) */
#define ADDR_SEQ_ADD_KIND7       0x00462080u  /* void(pt*, uid, 0, 0, life) */
#define ADDR_SEQ_ADD_KIND6       0x00461660u  /* void(pt*, int32_t) */
#define ADDR_SEQ_ALLOC           0x00461070u  /* void *(void *ctx) */
/* The context's own fields, as far as the allocator and the walker read them.
 * Records are ONE-BASED: SeqAlloc increments the count before using it as an
 * index, and the walker starts from record 0's next, so entry 0 is the head
 * rather than a record. */
#define SEQ_CTX_OFF_COUNT        0x00u  /* int32_t */
#define SEQ_CTX_OFF_RECORDS      0x10u  /* the 48-byte records */
#define AM2_SEQ_RECORD_SIZE      48
/* Its fields, all read out of the three adders and the walker. The record is
 * 48 bytes, which is what makes +0x2C the last of them. */
#define SEQ_OFF_KIND             0x00u  /* int32_t, an 8-arm jump table */
#define SEQ_OFF_FLAG4            0x04u  /* uint8_t, zeroed by kinds 5 and 7 */
/* The walker skips the record when it is ZERO -- not when it is "not
 * positive", which is what this said. `test edx,edx; jbe` is `je`, because
 * test clears the carry; CLAUDE.md already records that idiom and this is a
 * second instance of being caught by it.
 *
 * AND IT IS NOT A FLAG. Kind 5's stepper adds the frame delta to it every
 * frame and subtracts ADDR_SEQ_EMIT_MS whenever it passes that, so it is a
 * millisecond accumulator driving an emitter. The adders' `= 1` is not "true"
 * -- it is "start just above zero so the walker does not skip me". Which is
 * why the name is kept: what the WALKER does with it is still a gate. */
#define SEQ_OFF_GATE             0x08u  /* int32_t, milliseconds */
#define SEQ_OFF_FIELD_0C         0x0Cu  /* int32_t, zeroed */
#define SEQ_OFF_FIELD_10         0x10u  /* int32_t, zeroed by kind 5 only */
/* A DURATION in milliseconds, which kind 5 compares against the row's
 * ROW_OFF_STAMP_54 -- and that field is elapsed milliseconds there and a
 * FRAME COUNT in kind 2's stepper. One field, two units, by kind. */
#define SEQ_OFF_LIFE             0x14u  /* int32_t, the caller's or a default */
#define SEQ_OFF_ROW              0x1Cu  /* the AM2 row this seq draws through */
#define SEQ_OFF_OWNER            0x24u  /* int32_t: an army for 5, a uid for 7 */
/* The index chain, and the two halves disagree about its width: the adders
 * zero a DWORD here and the walker reads it back with `movsx word`. So it is
 * an int16 with two bytes of padding that the adders happen to clear. Read as
 * the walker reads it. */
#define SEQ_OFF_NEXT             0x2Cu  /* int16_t */
#define AM2_SEQ_KIND5            5
#define AM2_SEQ_KIND6            6
#define AM2_SEQ_KIND7            7
#define AM2_SEQ_LIFE5            0x17318   /* 95,000 */
#define AM2_SEQ_LIFE7            0x2710    /* 10,000 */
#define AM2_SEQ_LIFE6            0x3E8     /* 1,000 */
/* Added to the scaled terrain attribute for kind 6's ROW_OFF_FIELD_26. */
#define AM2_SEQ_DEPTH_BIAS       0x3F2
#define AM2_SEQ_ROW26_5          0x3E8     /* what each stamps at ROW_OFF_26 */
#define AM2_SEQ_ROW26_7          1
/* The two preloaded sprite arrays they draw with, in the same block as
 * ADDR_MARK_SPRITES and reached the same way -- the global holds a pointer to
 * the array and entry 0 is what these take. */
#define ADDR_SEQ_SPRITES_5       0x0048CBA8u  /* AM2_Sprite ** */
#define ADDR_SEQ_SPRITES_7       0x0048CB98u  /* AM2_Sprite ** */
#define ADDR_POINT_ACTION_A      0x004582F0u  /* void(obj, point) */
/* 0x00428F80, two callers -- adjacent sites in one function. Move an object
 * to a point, taking every one of its rows with it.
 *
 * THE SECONDARY ROWS ARE OFFSET BY THE FIRST SPRITE'S +0x28 AND +0x2A, which
 * is what those two fields are for: sprite.h had them as fileA and fileB,
 * "what they MEAN is still not established", and this adds them to rows 1..n
 * as an X and a Y. An attachment offset -- where a turret sits on its body.
 *
 * A NULL SPRITE ON ROW 0 ABANDONS THE WHOLE FUNCTION, not just the row loop:
 * the branch goes to the epilogue, so ObjTileChanged and the notify below it
 * are skipped too. The object has already been moved by then. */
#define ADDR_POINT_ACTION_C      0x00428F80u  /* void(obj, AM2_Point) */
/* ItemTeardown's MIRROR, one function earlier in the image: same gate
 * inverted, same type switch, same mask walk, ADDING the height where that one
 * subtracts and SETTING OBJ_FLAG_FOOTPRINT_ON where that one clears it. The
 * one thing it has that the other does not is a damage pass over everything
 * standing where the footprint lands. Its SECOND argument is never read.
 * Reconstructed; see region.cpp. */
#define ADDR_OBJ_AFTER_MOVE      0x00439000u  /* void(obj, unused, damage) */
#define AM2_CRUSH_DAMAGE_KIND    4
#define OBJ_OFF_ROW0_Y_ADJUST    0x42u  /* int16, copied to ROW_OFF_Y_ADJUST */
#define SPRITE_OFF_ATTACH_X      0x28u  /* int16, AM2_Sprite::attachX */
#define SPRITE_OFF_ATTACH_Y      0x2Au  /* int16, AM2_Sprite::attachY */
#define OBJ_OFF_X                0x12u
#define OBJ_OFF_Y                0x14u
/* 26 callers, and suppressed when the multiplayer session flag is set and the
 * comm object agrees, or when a state word reads 0x22. Named for what it is
 * observed to do from here -- announce an event -- and not from any one of
 * those callers. */
/* event.cpp's object setters: reach an object by uid and write one field.
 * They share a shape -- reject a uid below AM2_UID_COUNTER_MIN, look it up,
 * sometimes check the type or the null, then store. The differences between
 * them are exactly which of those guards each one has, so they are worth
 * keeping separate rather than folding into one helper. */
#define ADDR_EVT_SET_FIELD_540   0x0041FAB0u  /* void(uid, int32), type 2 only */
/* StepType8 says what it is for a ROACH: the object's own copy of
 * OBJ_OFF_FACING, refreshed from it at the top of every step and then passed
 * BY ADDRESS to four callees -- so one of them can turn the roach by writing
 * the copy, and the object's facing is only read into it again next frame.
 * Kept as FIELD_540 rather than renamed: ADDR_EVT_SET_FIELD_540 writes the
 * same offset for a type 2, which is not a roach, and one offset with two
 * uses wants one name and two readings. */
#define OBJ_OFF_FIELD_540        0x540u
/* 0x0041FD50. Takes TWO uids, requires both above the threshold and both to
 * resolve, clears field 0x540 on the first if it is type 2, and hands the pair
 * to 0x00458070 -- 640 bytes with twenty callers, unnamed.
 *
 * That "clear 0x540 first, but only for type 2" step is the third sighting:
 * EvtAtPointA does it, ADDR_EVT_SET_FIELD_540 exists to write it, and this is
 * a third. Whatever the field holds, several actions reset it before giving
 * the object something new to do. */
#define ADDR_EVT_OBJ_PAIR        0x0041FD50u  /* void(uint32_t, uint32_t) */
/* Was ADDR_OBJ_PAIR_ACTION, a placeholder its own neighbour admitted was not
 * a name -- "640 bytes with twenty callers, unnamed". Read now; see the block
 * further down. Renamed rather than aliased. */
#define ADDR_OBJ_ATTACH_TO       0x00458070u  /* void(subject, target) */
/* 0x0041FA10. EvtArmyAtPoint's sibling: the same CommSlotForArmy -> list walk,
 * the same +8 flag gate and ObjFieldA filter, the same prune-and-do-not-
 * advance for a dead uid -- plus a type test the other one does not have, and
 * a different action. It pushes field 0xE4 into 0xE8 before overwriting it,
 * which is the one-deep save EvtPushObjCtx does with globals. */
#define ADDR_EVT_ARMY_SET_FIELD  0x0041FA10u  /* void(army, filter, value) */
/* The AI MODE, which two comments in this file already called by that name
 * while the macro did not. 0x00407F80 dispatches an eight-entry jump table
 * on it and tests/actions-reference.txt gives the numbers the scripts
 * write: attack 6, defend 7, ignore 2, evade 5. */
/* The AI modes the eight-arm table dispatches on. The numbers come from
 * tests/actions-reference.txt -- the shipped scripts' own setaimode operands --
 * and orig.h's reading of the jump table agrees with them. Only the two this
 * band writes are named here; the rest are still literals at their use sites. */
#define AM2_AI_MODE_IGNORE       2
#define AM2_AI_MODE_ATTACK       6
#define OBJ_OFF_AI_MODE          0xE4u
#define OBJ_OFF_AI_MODE_PREV     0xE8u  /* the previous value of it */
#define ADDR_EVT_SET_MODE_F0     0x0041FAE0u  /* void(uid, int32), +0xF0, type 2/3/8 */
#define ADDR_EVT_SET_MODE_94     0x0041FB10u  /* void(uid, int32), +0x94, type 2/3/8 */
#define ADDR_EVT_SET_FLAG810     0x0041FB40u  /* void(uid, int32), flags 0x810 */
#define ADDR_EVT_SET_OWNER       0x0041FB80u  /* void(uid, int8), +0x10 */
#define ADDR_EVT_SET_BYTE40      0x00420020u  /* void(uid, int8), +0x40 */
#define ADDR_EVT_SET_BYTE530     0x00420040u  /* void(uid, int8), +0x530, type 3 */
#define ADDR_LOAD_SCRIPT_COND    0x0041EC70u  /* void(FILE *, cond *) */
#define ADDR_LOAD_EVENT_SECTION  0x004225E0u  /* int32_t(FILE *) */
/* map.cpp's savegame section: one fixed 236-byte block and nothing else, which
 * is why the pair is 48 and 64 bytes. The block is ADDR_MAP_BLOCK. */
/* event.cpp's OTHER section: a tag, then the block's own length as a second
 * tag, then 16008 bytes straight out of 0x0050C368. The length goes out
 * through WriteSaveTag and comes back through CheckSaveTag, which is why
 * 0x00003E88 sits in docs/savetags.tsv looking like a twelfth section tag.
 * It is not one. */
/* pad.cpp's savegame section, and the reset the loader opens with. The two
 * blocks are ADDR_PAD_NUMBERS and ADDR_PADS, already named; the sizes here are
 * confirmed three ways -- the fwrite lengths, the fread lengths, and the reset's
 * `rep stosd` counts of 0x1300 and 0x2400 dwords. */
/* script.cpp's savegame section, and the table free the loader opens with.
 * The section exists in this shape because AM2_ScriptName begins with a
 * POINTER: the struct cannot go out whole, so the name travels as a length and
 * then the bytes, and only the 12 fields after the pointer are written raw. */
/* air.cpp's savegame section: a tag and one 584-byte block, the same shape as
 * map.cpp's. The section map puts air at 588 bytes, which is 4 + 0x248. */
/* gameproc.cpp's savegame section: no tag of its own -- SaveGame writes
 * 0x06660666 before calling -- just the block LENGTH and then 1080 bytes from
 * 0x00511A68.
 *
 * The loader is the interesting half. Five things inside that block survive a
 * load: two strings, and the three audio volumes, which are already named
 * ADDR_VOLUME_AT_ZERO, ADDR_STREAM_VOLUME and ADDR_VOLUME_VOICE. They are
 * stashed on the stack, overwritten by the read, and put back -- so loading a
 * save does not reset what the player set. */
#define ADDR_SAVE_GAMEPROC       0x00426850u  /* int32_t(FILE *) */
#define ADDR_LOAD_GAMEPROC       0x00426880u  /* int32_t(FILE *) */
/* The two ends of the savegame format. SaveGame writes each section's tag and
 * calls its saver; LoadGame checks the outer tag, resets the token context and
 * runs the eleven loaders in the same order, closing the file on both exits. */
/* The section's own tag, written by SaveGame rather than by the saver. Three
 * readers check it: LoadGame, and 0x00425950 when it validates a save before
 * the mission starts. */
#define AM2_SAVETAG_GAMEPROC     0x06660666u
#define ADDR_LOAD_GAME           0x00425A10u  /* int32_t(FILE *) */
/* Set when entering a mission should LOAD its save rather than start it
 * fresh, and cleared by the state-2 entry as soon as it has acted. That entry
 * is the only reader that matters: with both names set and this raised, it
 * calls 0x00425950, which validates the save and leads to LoadGame.
 *
 * TWO screens raise it: the GAME SELECT PANEL's LOAD arm, and the REPLAY
 * prompt's OK -- "do you wish to reattempt your failed mission?". Neither is
 * the answer to STATUS.md's open item 2, which is that the flag is set, read
 * as SET at 0x00425360, and read as 0 again by 0x004255CB. */
#define ADDR_LOAD_PENDING        0x00511DD8u  /* int32_t */
/* "default.cof is present." ADDR_STATE2_ENTER runs _findfirst on that name and
 * sets this to 1 when it is found, then _findclose; seven references and that
 * is the only writer. The file does NOT ship with the GOG install, so this
 * reads 0 for the whole of any run here -- which makes ResetObjOnCof, its only
 * consumer that does real work, dead on this data set. Not patched out of the
 * binary like the CD checks; simply never satisfied. */
#define ADDR_HAVE_DEFAULT_COF    0x00511DDCu  /* int32_t */
#define ADDR_STR_DEFAULT_COF     0x004851C0u  /* "default.cof" */
#define ADDR_STR_UNIT_CPP        0x0048BDD8u  /* "C:\\ArmyMen2\\source\\unit.cpp" */
#define ADDR_STR_NUMGREEN        0x004888F4u  /* the script variable it sets */
/* docs/00-recon.md already maps this tag to unit.cpp, from the survey of every
 * CheckSaveTag site; this is the call it was recovered from. */
#define AM2_SAVETAG_COF          0x06660668u  /* CheckSaveTag's expected tag */
#define AM2_COF_TAG_LINE         0x142        /* the line CheckSaveTag reports */
/* 0x00457320, one caller -- the state-2 ENTRY, so this runs on going into a
 * level. Read `save\default.cof`: every object in it, healed to full, and the
 * troopers counted into the script variable `numgreen`. Reconstructed.
 *
 * THE FILE DOES NOT SHIP, per ADDR_HAVE_DEFAULT_COF above, so everything past
 * the fopen is unreachable on this data set. */
#define ADDR_LOAD_DEFAULT_COF    0x00457320u  /* int32_t(void) */
/* 0x00457070, one caller (0x00421AAA) -- the WRITE half of the pair above,
 * and the only thing in the image that produces `save\default.cof`. Walk the
 * default owner's object list and write every surviving trooper, each
 * followed by its six inventory slots.
 *
 * IT IS NOT A PURE WRITER. Before anything reaches the file it retires the
 * unit: ADDR_OBJ_DROP_ALT_RECORD puts it in state 5, Type238Action awards the
 * level's completion score, the position is reset to ADDR_ZERO_POINT, and a
 * rider is dismounted -- OBJ_OFF_RIDING cleared and its first map row made
 * drawable again through ObjFlagSet0. So this is the end-of-level roster
 * writer, not a snapshot, and calling it twice would award the score twice.
 *
 * THE TYPE FILTER IS THREE TESTS DEEP AND ONLY THE LAST ONE BITES. It admits
 * types 2, 3 and 8, then requires OBJ_OFF_FIELD_94 to be zero, then requires
 * the type to be exactly 2 -- so vehicles and roaches are excluded twice over.
 * Reproduced as written rather than folded, because the wider test is what
 * the original asks and a fold would hide that it once meant more.
 *
 * A MISSING OBJECT IS REMOVED FROM THE LIST AND THE INDEX DOES NOT ADVANCE,
 * which is correct for a compacting ListRemoveAt and is the one place the
 * walk can loop on the same slot. Both the list pointer and its count are
 * re-read from the globals every iteration.
 *
 * ITS ONE CALLER NAMES THE OCCASION. 0x00421AAA saves the best difficulty
 * through SaveOptions, finds the record for levelId + 1, calls this,
 * increments ADDR_LEVEL_ID, sets ADDR_LEVEL_INDEX to 1 and hands the next
 * record to SelectLevel -- the advance-to-the-next-mission path. The award
 * uses the level id BEFORE that increment, so it is for the level just
 * finished.
 *
 * Unreachable on this data set for the same reason as the load half. */
#define ADDR_SAVE_DEFAULT_COF    0x00457070u  /* int32_t(void) */
#define ADDR_MODE_WB             0x004851E0u  /* "wb" -- beside "default.cof" */
/* 0x00457220, three callers. Clear an object's hit record, give it a random
 * phase, detach it, and hand a non-Sarge kind-3 trooper stance 2 -- all of it
 * behind ADDR_HAVE_DEFAULT_COF. */
#define ADDR_RESET_OBJ_ON_COF    0x00457220u  /* void(void *obj) */
#define AM2_COF_PHASE_MAX        0x1F4        /* 500 */
/* Raised beside it and consumed by the level teardown, which bumps
 * ADDR_ATTEMPT_COUNT and logs "Attempt# %d". */
#define ADDR_MISSION_RETRY       0x0051232Cu  /* int32_t */
#define ADDR_ATTEMPT_COUNT       0x00512330u  /* int32_t */
/* Reconstructed as SaveGame -- the write half of the savegame, and the mirror
 * of the already-reconstructed LoadGame. It refuses unless a level is loaded
 * and a filename was given, ensures `save\<level>\` EXISTS, chdirs into it,
 * opens the file "wb", writes the outer tag and then eleven section writers in
 * a fixed order, each guarded: any failure abandons the rest.
 *
 * THE ORDER IS CONFIRMED TWICE INDEPENDENTLY. LoadGame calls the eleven
 * loaders in it, and CLAUDE.md's savegame oracle reports its per-section
 * results in the same sequence -- gameproc, map, pad, objscript, script,
 * eventblock, conds, event, item, air, audio.
 *
 * BOTH ITS EXITS CLOSE THE FILE. The ten `je 0x0042591C` are not returns:
 * that address is `push esi; call fclose`, and 0x00425930 is the same with a
 * 1 instead of a 0. Writing them as a bare `return 0` would leak the handle on
 * every failure path. Its three genuine early returns are fall-through rets.
 *
 * Every section writer is already reconstructed, so this needs no orig_ seam
 * at all -- only includes from the modules that own each section. */
#define ADDR_SAVE_GAME           0x00425790u  /* int32_t(const char *name) */
/* _mkdir: CreateDirectoryA(path, NULL), -1 on failure. Named from
 * docs/imports.tsv, which puts KERNEL32!CreateDirectoryA at 0x00465F5C inside
 * it -- the disassembly only shows a call through an IAT slot. */
#define ADDR_CRT_MKDIR           0x00465F56u  /* int32_t(const char *path) */
#define ADDR_CRT_ATEXIT          0x00465011u  /* int32_t(void (*)(void)) */
#define AM2_ATTR_SUBDIR          0x10   /* _A_SUBDIR in the find data */
/* 0x00444EF0, two callers, one of them the per-frame path. Raise the level's
 * "startupN" script event and then autosave. */
#define ADDR_MISSION_STARTUP     0x00444EF0u  /* void(void) */
#define ADDR_STR_STARTUP_FMT     0x00489540u  /* "startup%d" */
#define ADDR_STR_MISSION_SAV_FMT 0x0048952Cu  /* "map%d_mission%d.sav" */
#define AM2_MISSION_NAME_BYTES   0x50  /* int32_t(const char *) */
#define ADDR_GAMEPROC_BLOCK      0x00511A68u  /* also a string; see below */
#define AM2_GAMEPROC_SAVE_SIZE   0x438u       /* 1080 bytes, and its own tag */
#define ADDR_GAMEPROC_STR_B      0x00511B88u  /* a second string inside it */
#define ADDR_STR_GAMEPROC_CPP    0x004851ECu  /* "C:\\ArmyMen2\\source\\gameproc.cpp" */
/* 0x00425950, one caller. Open the current save for reading, verify its
 * gameproc section, rewind it, and hand back the open FILE -- or close it and
 * answer NULL. The two names it needs are both inside ADDR_GAMEPROC_BLOCK. */
#define ADDR_OPEN_SAVE_FOR_LOAD  0x00425950u  /* am2_FILE *(void) */
#define AM2_SAVE_PATH_BYTES      0x100u       /* its stack buffer */
#define AM2_GAMEPROC_TAG_LINE    0x528        /* 1320, the __LINE__ it passes */

#define ADDR_SAVE_AIR_SECTION    0x00409840u  /* int32_t(FILE *) */
#define ADDR_LOAD_AIR_SECTION    0x00409870u  /* int32_t(FILE *) */
#define ADDR_AIR_SAVE_BLOCK      0x004F945Cu
#define AM2_AIR_SAVE_SIZE        0x248u       /* 584 bytes */
#define AM2_SAVETAG_AIR          0x06660010u
#define ADDR_STR_AIR_CPP         0x004740B0u  /* "C:\\ArmyMen2\\source\\air.cpp" */

#define ADDR_FREE_SCRIPT_NAMES   0x0043F030u  /* void(void), 3 callers */
#define ADDR_SAVE_SCRIPT_SECTION 0x0043F0A0u  /* int32_t(FILE *) */
#define ADDR_LOAD_SCRIPT_SECTION 0x0043F150u  /* int32_t(FILE *) */
#define ADDR_STR_SCRIPT_CPP      0x004888A4u  /* "C:\\ArmyMen2\\source\\script.cpp" */
#define AM2_SAVETAG_SCRIPT       0x06660002u

#define ADDR_SAVE_EVENT_SECTION  0x00422470u  /* int32_t(FILE *) */
#define ADDR_SAVE_PAD_SECTION    0x00437A90u  /* int32_t(FILE *) */
#define ADDR_LOAD_PAD_SECTION    0x00437AE0u  /* int32_t(FILE *) */
#define ADDR_RESET_PADS          0x004373C0u  /* void(void), 2 callers */
#define ADDR_RESET_PADS_ALIAS    0x004373F0u  /* void(void), one jmp to it */
#define ADDR_STR_PAD_CPP         0x004877F8u  /* "C:\\ArmyMen2\\source\\pad.cpp" */
#define AM2_SAVETAG_PAD          0x06660005u
#define AM2_PAD_NUMBERS_BYTES    0x4C00u      /* 256 entries of 76 */
#define AM2_PADS_BYTES           0x9000u      /* 512 entries of 72 */

#define ADDR_SAVE_EVENT_BLOCK    0x0041E9E0u  /* int32_t(FILE *) */
#define ADDR_LOAD_EVENT_BLOCK    0x0041EA20u  /* int32_t(FILE *) */
#define ADDR_EVENT_BLOCK         0x0050C368u
#define AM2_EVENT_BLOCK_SIZE     0x3E88u      /* 16008 bytes, and its own tag */
#define AM2_SAVETAG_EVENT_BLOCK  0x06660006u

#define ADDR_SAVE_MAP_SECTION    0x0042DB40u  /* int32_t(FILE *) */
#define ADDR_LOAD_MAP_SECTION    0x0042DB70u  /* int32_t(FILE *) */
/* The block the map loader owns, and its first field is a STRING: the level
 * loader at ADDR_LOAD_MAP zeroes 416 bytes here and then strcpy's in the map
 * directory it was just handed, which is what the sprite and mask paths put in
 * front of a set number 20 or above. Only the first 236 go into a savegame,
 * so the block is larger than the part map.cpp writes out. Named for the block
 * rather than for either use -- both call sites had a name for it and each was
 * silent about the other. */
#define ADDR_MAP_BLOCK           0x00514D90u  /* char[] first, 416 bytes total */
#define AM2_MAP_SAVE_SIZE        0xECu        /* 236 bytes, the saved prefix */
#define AM2_SAVETAG_MAP          0x06660009u
#define ADDR_STR_MAP_CPP         0x00486410u  /* "C:\\ArmyMen2\\source\\map.cpp" */
/* 0x0042DBB0, "Checksum of %s " and "is %x " -- its own name. Seven callers,
 * and it sits immediately after the two map save/load functions, which is the
 * evidence for filing it in map.cpp: the band alone only says
 * map.cpp..objscript.cpp. */
#define ADDR_CHECKSUM              0x0042DBB0u  /* uint32_t(const char *) */

#define ADDR_SAVE_SCRIPT_CONDS   0x0041EC20u  /* int32_t(FILE *) */
#define ADDR_LOAD_SCRIPT_CONDS   0x0041EDD0u  /* int32_t(FILE *) */
#define AM2_SAVETAG_CONDS        0x06660003u  /* event.cpp's other tag */
/* The three handlers a saved event registration is restored with. Two are in
 * the objscript band and take a pad; the third takes a 16-byte record read
 * straight from the file. Reached by address -- they are original code. */
#define ADDR_EVT_PAD_HANDLER_A   0x00437570u
#define ADDR_EVT_PAD_HANDLER_B   0x00437540u
#define ADDR_EVT_RECORD_HANDLER  0x0041F3E0u
/* The one that BUILDS such a record. EventTriggerDelayed mallocs 16 bytes,
 * fills it with (type, num, uid, removeevent), starts a timer and registers
 * ADDR_EVT_RECORD_HANDLER against the timer's id with that record as the
 * argument -- passing 1 for `owns`, so the teardown frees it. Named by its own
 * log string, "EventTriggerDelayed: type %d, num: %d, uid: %x, removeevent:
 * %d, delay: %d". */
#define ADDR_EVENT_TRIGGER_DELAYED 0x0041F410u
/* 0x0041F4A0, twenty-six callers -- the front door of the event system. It
 * chooses between the delayed and the immediate trigger on whether `delay` is
 * positive, and refuses two ways first:
 *
 *   - in a multiplayer session, only the HOST raises events (ADDR_MP_SESSION
 *     set and COMM_OFF_IS_HOST clear means return), which is the authority
 *     rule stated in one line;
 *   - and never while ADDR_MENU_MODE is 0x22. That is 34, the ESCAPE
 *     sub-state CLAUDE.md records ordinary play as never being in -- so the
 *     game stops raising events the moment that menu arm is entered.
 *
 * Note the delayed path DROPS four of its arguments: EventTriggerDelayed takes
 * no masks and no second num/uid pair. A delayed event therefore cannot carry
 * what an immediate one can.
 *
 * The address already had a name -- ADDR_EVENT_NOTIFY, which is what CLAUDE.md
 * calls "the notify". That one is kept: it describes the same body just as
 * well, and renaming an established name for a synonym is churn. */
#define AM2_SUBSTATE_ESCAPE        0x22
/* 0x21 is 33, ordinary play -- what ADDR_MENU_MODE reads through a whole
 * mission and what Substate22 puts back when the info bitmap is dismissed.
 * ADDR_MENU_MODE's own comment already said "0x21 = back to play"; this is
 * that number given a name so a use site does not have to carry it. */
#define AM2_SUBSTATE_PLAY          0x21
/* DirectInput scancode 1. Promoted out of widget.cpp, which had it as a local:
 * the widget layer and the in-mission paused frame both test it, and both test
 * it as RELEASED -- !IsKeyDown && KeyChanged. One definition, not two. */
#define AM2_DIK_ESCAPE             1
#define AM2_DIK_RETURN             0x1Cu
#define AM2_DIK_SPACE              0x39u
#define AM2_DIK_F1                 0x3Bu
/* The timer table: 1,000 records of {start, period, count, id} at 0x0050C370,
 * with the live count at 0x0050C36C. A slot is FREE when its id is zero, which
 * is why the scan walks the id field at 0x0050C37C rather than the record. */
/* 0x0041E950, one caller -- the per-frame path. Fire every timer that is due,
 * at most once every 100 ms. */
#define ADDR_TIMER_TICK            0x0041E950u /* void(void) */
/* 0x00424B20, one caller -- the per-frame path. THE GAME CLOCK: measure the
 * frame, clamp it, add it to ADDR_GAME_CLOCK_MS and publish it in both units.
 * This is what makes that clock tick. */
#define ADDR_FRAME_CLOCK_STEP      0x00424B20u /* void(void) */
/* 0x004035F0, one caller -- the per-frame path. Sixteen bytes that zero the
 * two counters below, and both of them are vestigial.
 *
 * The whole image holds exactly THREE references to the pair: the two writes
 * here, and one read of the first inside 0x00403B40, which refuses to do
 * anything when it is above 10. Nothing anywhere increments either. So the
 * first is always zero and that guard can never fire, and the second is
 * written and never read at all.
 *
 * Named for the mechanism -- cleared together, once a frame -- rather than for
 * what they were meant to count, which nothing surviving in the image says. */
#define ADDR_CLEAR_FRAME_COUNTS    0x004035F0u /* void(void) */
/* THE SIGHTING SCAN'S PER-FRAME BUDGET, and its cap is 10 -- the same number
 * ADDR_PATH_MAX_SEARCHES uses for the pathfinder, in the same shape: a counter
 * the frame resets and a `> 10` at the very top that answers 0 without doing
 * anything. Two subsystems throttled identically and independently. */
#define ADDR_PERFRAME_COUNT_A      0x004F93B8u /* int32_t, gates 0x00403B40 */
#define AM2_SIGHT_SCANS_PER_FRAME  10
#define ADDR_PERFRAME_COUNT_B      0x004F93BCu /* int32_t, never read */
#define AM2_PERFRAME_COUNT_LIMIT   10
#define ADDR_LAST_TICK_MS          0x00511E0Cu /* uint32_t, from ADDR_TICKS */
/* Selects a FIXED 16 ms step instead of measuring. Below the CRT line it is
 * READ three times and written nowhere, so it is always zero and the fixed
 * path never runs -- the same shape as ADDR_SECOND_DEADLINE, one step further
 * on: there the writes had no reader, here the reads have no writer. */
#define ADDR_FIXED_STEP            0x00512598u /* int32_t, always 0 */
#define AM2_FIXED_STEP_MS          0x10        /* 16 ms */
#define AM2_FRAME_DELTA_CAP_MS     0x42        /* 66 ms, about 15 fps */
#define ADDR_MS_TO_SEC             0x0046F980u /* const float, 0.001 */
#define AM2_TIMER_TICK_MS          0x64        /* 100 ms between sweeps */
/* The last sweep's timestamp is the dword at 0x0050C368 -- which already has a
 * name, ADDR_EVENT_BLOCK, because it is also the first dword of the 16,008-byte
 * block the savegame writes out. Both readings are right and it is one address,
 * so it keeps the one name: the block is {lastSweep, count, timers[1000]} and
 * its base IS the timestamp field. */
#define ADDR_TIMER_COUNT           0x0050C36Cu /* int32_t */
#define ADDR_TIMER_TABLE           0x0050C370u /* AM2_Timer[1000] */
#define ADDR_TIMER_TABLE_ID_END    0x005101FCu /* one past the last id field */
#define AM2_TIMER_MAX              1000
#define AM2_TIMER_LOWPRI_LIMIT     0x384       /* 900 */
#define AM2_TIMER_SLOW_LIMIT       0x3B6       /* 950 */
#define AM2_TIMER_SLOW_PERIOD      0x3A98      /* 15000 ms */
#define AM2_TIMER_REFUSED          (-101)
#define AM2_TIMER_NO_ROOM          (-100)
/* 0x0041E800, one caller: put every timer slot's id back to zero and clear the
 * count and the dword before it, which empties the table without walking the
 * records. The name is ours. */
#define ADDR_RESET_TIMERS          0x0041E800u  /* void(void) */
#define ADDR_CREATE_TIMER          0x0041E820u  /* "CreateTimer", 304 B */
/* The network half of the event system, and the two sides confirm each other:
 * EventMessageSend packs a 40-byte message and EventMessageReceive unpacks the
 * same offsets and hands them to EventTriggerImmediate. Each names itself in
 * its own log string, and EventTriggerImmediate's names the trailing argument
 * Receive passes as 1 -- "remote". */
#define ADDR_EVENT_MESSAGE_SEND    0x0041F150u
#define ADDR_EVENT_MESSAGE_RECV    0x0041F320u
#define ADDR_EVENT_TRIGGER_IMMED   0x0041EF80u  /* 464 B, 3 callers */
/* Runs one action of an `if` -- (cond, index, arg). Thirty-two bytes and four
 * callers: it does nothing but index the 0x48-byte action array and hand the
 * result to the executor, which is a third independent confirmation of
 * AM2_ScriptAction's size (the parser writes it, the saver copies it, this
 * strides it). Role name. */
#define ADDR_COND_RUN_ACTION       0x00421410u
/* 0x0041F520, 80 bytes and FIFTY-THREE callers. Resolves a script name index
 * to the uid it stands for, with `me` taken from the caller's context instead.
 * It names itself nowhere -- its only string is the complaint "Bad ME" -- so
 * this is a role name. */
#define ADDR_RESOLVE_UID           0x0041F520u
/* 0x00421430, "Tried to switch on invalid state." -- which names the
 * condition, not the function. It runs an `if` statement's action list the way
 * its `mode` says to. Role name. */
#define ADDR_COND_RUN_ACTIONS      0x00421430u
/* The predicate it walks a bucket with is FilterMatches, 0x0041EF20, which
 * this port already owns -- so no new name here. That is what gives the two
 * pass-through fields of an event a meaning: they are the maskA/maskB
 * arguments, the sets the event belongs to. */
/* 0x004105F0, "ArmyMessageSend" from its own three error strings -- 304 bytes
 * and 20 callers, so it is the transport the whole game sends through. */
#define ADDR_ARMY_MESSAGE_SEND     0x004105F0u
/* 0x0044C250, one caller, and it names itself: "Trooper Fire Send, trooper:
 * %d,  face:%d, pos (%d,%d,%d), loctarg %x, globTarg %x, weap %d, seq:%d".
 * A 28-byte army message of kind 0x17 -- the trooper, its target, where it is
 * and which way it faces -- and it clears two of the trooper's fields on the
 * way out. */
#define ADDR_TROOPER_FIRE_SEND     0x0044C250u  /* void(trooper, target) */
#define AM2_MSG_TROOPER_FIRE       0x17
#define AM2_MSG_TROOPER_FIRE_LEN   0x1C
/* The per-player flow record FindPlayerById answers with. ArmyMessageFlush is
 * where the sequence is bumped -- once per flush, not once per recipient -- and
 * it stamps the same value into the packet before the send loop, so every
 * player in one flush is told the same number. FLOW_OFF_READY gates the flush
 * entirely: no flow queue for ourselves yet and nothing goes out. */
/* +0x0C IS THE SEQUENCE HE HAS, and the writer is what settled it. It was
 * FLOW_OFF_FIELD_0C on the strength of ProcessResendQueue alone, which merely
 * refuses to resend a packet older than it -- one reader is not a meaning, as
 * the comment here used to say. SendGameMsg sets it, once, on the first packet
 * ever sent to a player: `if (heHas == 0 && seq > 1) heHas = seq - 1`, and
 * logs "SendGameMsg, first message to %x, hehas set to %d". DestroyFlow agrees
 * from a third direction, simulating acks for seq from heHas+1 upwards. The
 * spelling is the program's, run together exactly as it writes it.
 *
 * +0x04 keeps its number: ProcessResendQueue and SendGameMsg both only COPY it
 * into a packet, and nothing yet read here writes it. */
/* DrainMsgList is handed `flow + 0x78`, and that function takes a message
 * list -- so this offset IS the flow's queue. Named from its one use, which
 * is enough here only because the callee's own type says what it is. */
#define FLOW_OFF_QUEUE             0x78u
#define FLOW_OFF_FIELD_04          0x04u
#define FLOW_OFF_HE_HAS            0x0Cu
#define FLOW_OFF_READY             0x88u
#define FLOW_OFF_SEQUENCE          0x94u
/* The trooper fields this touches. Only the two positions and the facing are
 * evidenced by the log line beside them; the rest are named for what the
 * message does with them. */
#define TROOPER_OFF_FACING         0x40u   /* uint8_t, logged as `face` */
#define TROOPER_OFF_POS_X          0x590u  /* int16_t, logged as pos.x */
#define TROOPER_OFF_POS_Y          0x592u
#define TROOPER_OFF_POS_Z          0x594u
#define TROOPER_OFF_LOCAL_TARGET   0x598u  /* logged as `loctarg` */
#define TROOPER_OFF_WEAPON         0x568u  /* logged as `weap` */
#define TROOPER_OFF_FIRE_FLAG      0x529u
#define TROOPER_OFF_CLEAR_A        0x57Cu  /* both zeroed as the shot goes */
#define TROOPER_OFF_CLEAR_B        0x588u
/* AppendTroopState's SEND CACHE: three (value, timestamp) pairs, one per
 * field it delta-encodes. A field goes on the wire when it differs from the
 * value here, and the timestamp is what the two thresholds are measured
 * against -- so each field throttles independently. Named from the body:
 * each is written immediately after its own field is appended. */
#define TROOPER_OFF_SENT_POS       0x5B4u  /* packed point, last transmitted */
#define TROOPER_OFF_SENT_POS_T     0x5B8u  /* the seq it went out on */
#define TROOPER_OFF_SENT_FACING    0x5BCu  /* uint8_t */
#define TROOPER_OFF_SENT_FACING_T  0x5C0u
#define TROOPER_OFF_SENT_POSE      0x5C4u  /* int32_t, OBJ_OFF_POSE's copy */
#define TROOPER_OFF_SENT_POSE_T    0x5C8u
#define TROOPER_OFF_LAST_SEQ       0x5CCu  /* where the sequence is kept */
/* 0x00410820, "SendGamePause from %x  Pause =%s  Flags=%x". Eight callers.
 * It fills two fields of a message that lives in .bss at 0x004FAA50 and hands
 * it to SendGameMsg -- the header is set up elsewhere, since nothing in the
 * file image backs that address. */
#define ADDR_MSG_GAME_PAUSE        0x004FAA50u
#define MSG_PAUSE_OFF_PAUSE        8u
#define MSG_PAUSE_OFF_FLAGS        0x0Cu
#define ADDR_STR_TRUE              0x00475C20u
#define ADDR_STR_FALSE             0x00475C18u
/* DefParseBoolean's other ten spellings, and the two above are the first of
 * each set -- shared with the pause logger, which is why they were already
 * named and why grepping the address before inventing a name mattered here.
 *
 * Two contiguous runs in the order the compiler laid the literals out, which
 * is NOT the order the function tests them: it asks TRUE, true, True, T, t, 1
 * and then FALSE, false, False, F, f, 0. Nothing turns on the order -- the
 * twelve are distinct -- but reading the addresses top to bottom gives the
 * reverse of the code, which is the string-table version of the jump-table
 * trap. */
#define ADDR_STR_BOOL_0            0x0047795Cu /* "0" */
#define ADDR_STR_BOOL_LF           0x00477960u /* "f" */
#define ADDR_STR_BOOL_UF           0x00477964u /* "F" */
#define ADDR_STR_BOOL_FALSE_CAP    0x00477968u /* "False" */
#define ADDR_STR_BOOL_FALSE_LOW    0x00477970u /* "false" */
#define ADDR_STR_BOOL_1            0x00477978u /* "1" */
#define ADDR_STR_BOOL_LT           0x0047797Cu /* "t" */
#define ADDR_STR_BOOL_UT           0x00477980u /* "T" */
#define ADDR_STR_BOOL_TRUE_CAP     0x00477984u /* "True" */
#define ADDR_STR_BOOL_TRUE_LOW     0x0047798Cu /* "true" */
#define ADDR_STR_BAD_BOOLEAN       0x00477944u /* "Bad or missing Boolean\n" */
/* 0x0041A2D0, three callers -- the third of DefParseNumber's family, sitting
 * between the integer form and DefParseInfoFile in the same band. Twelve
 * inlined strcmps and nothing else. */
#define ADDR_DEF_PARSE_BOOLEAN     0x0041A2D0u /* int32(int32 *out, const char *) */
/* The outgoing packet. 0x004FAA68 is its base and 0x004FAA6C is base+4 -- the
 * packet's own length field, which doubles as the write cursor. Flush resets
 * it to 0x14, so the packet header is twenty bytes and the first message lands
 * at base+0x14. */
#define ADDR_ARMY_PACKET           0x004FAA68u
#define ADDR_ARMY_PACKET_LEN       0x004FAA6Cu
#define AM2_ARMY_PACKET_HDR        0x14u
/* base+8 is the sequence, which ArmyMessageFlush copies out of our own flow
 * record on its way past. The "Sending Flow Packet seq %d" trace prints the
 * record's copy, so the two are the same number by construction. */
#define ADDR_ARMY_PACKET_SEQ       0x004FAA70u
/* Every message on this transport opens with the same eight bytes: a length, a
 * kind, and a uid. ArmyMessageSend reads the third as one -- it logs
 * UidArmy(UidOnWire(msg->uid)) -- which is how the field is known to be a uid
 * and not the padding EventMessageSend's always-zero write suggested. */
/* 0x0044C0F0, five callers. A 28-byte army message of kind 0x18 carrying two
 * objects: each one's uid through UidOnWire, the SECOND one's position, a byte
 * and a dword the caller supplies.
 *
 * THERE IS A HOLE AT +0x10. Every other dword of the 0x1C bytes is written and
 * that one is not, so four bytes of the sender's stack go out on the wire.
 * Reproduced; a memset would be tidier and would change what the receiver sees
 * on a machine where the stack held something else.
 *
 * The name is structural, and the RECEIVER is reconstructed now -- see
 * RecvTroopPair in commmsg.cpp. It confirms this layout exactly and settles
 * the argument order: it passes +0x18 before +0x14, which is the byte before
 * the field, matching this function's own (a, b, int8, int32). It also says
 * the second object must be a TYPE 4 -- a weapon -- and hands the pair to a
 * 576-byte function that stamps a two-second deadline on the first and plays
 * a sound at the second. Still not enough to name the message; enough to say
 * the pair is a trooper and a weapon. */
#define ADDR_SEND_PAIR_MSG         0x0044C0F0u  /* void(a, b, int8, int32) */
#define AM2_MSG_PAIR               0x18
#define AM2_MSG_PAIR_LEN           0x1C
#define MSG_PAIR_OFF_A             0x04u  /* UidOnWire(a->uid) */
#define MSG_PAIR_OFF_B             0x08u  /* UidOnWire(b->uid) */
#define MSG_PAIR_OFF_POS           0x0Cu  /* b's OBJ_OFF_POS */
#define MSG_PAIR_OFF_HOLE          0x10u  /* never written */
#define MSG_PAIR_OFF_ARG           0x14u
#define MSG_PAIR_OFF_BYTE          0x18u  /* int32, sign-extended from int8 */
#define AM2_ARMY_MSG_HDR           8u
#define AM2_ARMY_MSG_EVENT         0x0020u   /* the kind word EventMessageSend
                                              * stamps at offset 2 */
/* Gates the event logging in EventTriggerDelayed, EventMessageSend and
 * EventMessageReceive -- all three read it before calling the logger, and the
 * logger is stubbed to `ret` in this build, so it is inert either way.
 * COMM_OFF_VERBOSE, defined once further down; this was a second name for it
 * and a third stood beside StartSelectedGame. */
/* The event.cpp section's own tag and the path string CheckSaveTag is given.
 * docs/savetags.tsv lists all fifteen; this is the one at event.cpp:3274. */
#define AM2_SAVETAG_EVENT        0x06660004u
#define ADDR_STR_EVENT_CPP       0x004783F8u  /* "C:\\ArmyMen2\\source\\event.cpp" */

/* Chunk tags inside the event.cpp save section. */
/* "another record follows", and it is NOT event-specific -- the item section
 * uses the same marker, and so does every other section that stores a list.
 * It went in as AM2_EVTSAVE_RECORD when only event.cpp used it, and a second
 * name for the same value appeared the moment item.cpp needed one. One name. */
#define AM2_SAVE_RECORD_MARK     0x06660000u
#define AM2_EVTSAVE_PAD_A        0x06670004u
#define AM2_EVTSAVE_PAD_B        0x06670005u
#define AM2_EVTSAVE_OWNED        0x06670006u
/* (fp, cond), NOT (cond, fp). This comment said the latter, which is the order
 * the reconstruction had before the campaign A/B caught it -- so the fix landed
 * in the code and the wrong order stayed here describing it. Confirmed twice
 * over: LoadScriptConditions calls the loader as (fp, cond), and
 * SaveScriptConditions pushes cond then fp, which is (fp, cond) in cdecl. */
#define ADDR_SAVE_SCRIPT_COND    0x0041EB00u  /* void(FILE *, const cond *) */
#define ADDR_EVENT_DEFAULT_NAME  0x0041F200u  /* void(kind, number, char *out) */
#define ADDR_FREE_SCRIPT_CONDS   0x0041EA80u  /* void(void), frees the list */
#define ADDR_EVT_PLAY_SOUND_AT   0x0041F680u  /* void(name, point, slot, pri, loop) */
#define ADDR_EVT_PLAY_SOUND_ON   0x0041F6B0u  /* void(name, owner, slot, pri, loop) */
#define ADDR_EVT_SET_WORD60      0x0041F750u  /* void(uid, int32), clamped */
#define ADDR_EVT_SET_AI_MODE     0x0041F9B0u  /* void(uid, mode), +0xE4/+0xE8 */
#define ADDR_EVT_SET_ALLIED      0x0041FF20u  /* void(a, b) -> matrix 1 */
#define ADDR_EVT_CLEAR_ALLIED    0x0041FF40u  /* void(a, b) -> matrix 0 */
/* The 4x4 alliance matrix, not "marks": 0x00424E80 fills it with the identity
 * and then allies any two comm players on the same team, and AllyFlag reads it
 * to answer whether two armies are on the same side. The two writers above
 * were named from their shape alone. See src/game/army.h. */
#define ADDR_ALLY_MATRIX         0x00511E60u  /* int32_t[4][4] */
/* 0x00457B30, seven callers, every one of them creating a unit. Set an
 * object's MAX health (+0x60) from its type's value, scaled by the difficulty
 * -- and every caller then sets the current health at +0x62 from what this
 * left there, which is what fixes the two fields.
 *
 * The two globals it reads name themselves. 0x00512324 is written through
 * `Options.cfg` and indexes a three-float table, and the function returns
 * without doing anything when it is above 1 -- so it is the difficulty, 0 to
 * 2. 0x00512330 is logged as "Attempt# %d": the level loader increments it on
 * each retry and 0x00421890 clears it when the campaign moves on. Every
 * retry takes five health off each enemy, divided by 2*difficulty + 2. */
#define ADDR_SET_MAX_HEALTH      0x00457B30u  /* void(void *obj, int32_t) */
/* The DIFFICULTY dialog is both ends of this: the constructor seeds the
 * list's 0x58 and 0x5C from it and OK writes 0x5C back. 0x5C is the HOT row
 * rather than the selected one, which is what the original reads; the list's
 * click handler sets both. */
#define ADDR_DIFFICULTY          0x00512324u  /* int32_t, 0..2 */
#define ADDR_LEVEL_ATTEMPT       0x00512330u  /* int32_t -- "Attempt# %d" */
#define ADDR_DIFFICULTY_SCALE    0x00489870u  /* float[3] = 4.0, 2.0, 1.5 */
#define ADDR_ENEMY_HEALTH_SHARE  0x0046FD50u  /* double 0.33 */
#define ADDR_FTOL                0x00464490u  /* MSVC _ftol, truncating */
#define OBJ_OFF_MAX_HEALTH       0x60u        /* int16_t, beside +0x62 */
#define AM2_MAX_HEALTH_CAP       0x190        /* 400: above this, leave alone */
#define AM2_HEALTH_PER_ATTEMPT   5
#define ADDR_ALLY_FLAG           0x0040F230u  /* stdcall int32_t(a, b) */
/* Three target predicates in one functions.tsv entry, 0x60, 0x60 and 0x30
 * bytes. Reconstructing any one of them alone would have marked all three
 * done, so all three are written. */
#define ADDR_OBJ_IS_OURS         0x00403600u /* int32(void *obj, int32 allies) */
#define ADDR_OBJ_IS_LIVE_TARGET  0x00403660u /* int32_t(void *obj) */
#define ADDR_OBJ_IS_HITTABLE     0x004036C0u /* int32_t(void *obj) */
/* 0x004574D0, eleven callers. Whether two objects are on the same side, which
 * is AllyFlag above with four exceptions layered over it. Reconstructed. Its
 * third argument chooses which of the second object's two OBJ_TABLE_RECORDS
 * pointers to consult -- SAVED_OFF_TABLE_REC3 or SAVED_OFF_TABLE_REC2.
 * Two calls on a driven Boot Camp mission; its two multiplayer arms are
 * unreachable here, since ADDR_MP_SESSION is 0 on every drive. */
#define ADDR_OBJS_ARE_ALLIED     0x004574D0u  /* int32(void*, void*, int32) */
/* 0x00457620, six callers. The same question with an ARMY on the left instead
 * of an object: identical to ADDR_OBJS_ARE_ALLIED from its `army == 4` test
 * onward, and differing only in where the army comes from. The image holds two
 * bodies rather than a call, so both are reconstructed; a change to one is a
 * change to the other. Reconstructed, and 80 calls on a driven Boot Camp
 * mission against ADDR_OBJS_ARE_ALLIED's 1. */
#define ADDR_ARMY_ALLIED_WITH_OBJ 0x00457620u /* int32(int32 army, void*, int32) */
/* Read by 0x0045B7E0 and by 0x0044AE68, both as a plain non-zero test on the
 * VIEWER rather than on the object being judged, and nothing here writes it.
 * Structural name; what it means is not established. */
#define OBJ_OFF_FIELD_10C        0x10Cu
/* 0x0045B7E0, three callers, 336 bytes -- the third BlockWeight variant. Same
 * accumulate-to-15 walk as ADDR_BLOCK_WEIGHT_CHAIN over a chain the caller
 * supplies, with ADDR_OBJ_BLOCK_WEIGHT's per-object test INLINED and one extra
 * arm for a type 2. The terrain term is AM2_TILE_BLOCKS, as
 * ADDR_BLOCK_WEIGHT_AT's is, and there is no height step. Reconstructed, and
 * it is the one the game uses: 2,039,745 calls on a driven Boot Camp mission
 * against BlockWeightAt's 8 and BlockWeightChain's 0. */
#define ADDR_BLOCK_WEIGHT_TROOPS 0x0045B7E0u /* int32(void*,uint32,void*,uint32) */
#define ADDR_ARMIES_ALLIED       0x00457720u  /* int32_t(a, b), 4 means all */
#define ADDR_OBJ_IS_FRIENDLY     0x004577C0u  /* int32_t(const void *obj) */
/* The uid of OUR army's leader; 0x00424E80 clears it and three others set it.
 * The name is ours, from ArmyLeader's one use of it. */
#define ADDR_OUR_LEADER_UID      0x00511E4Cu  /* uint32_t */
#define ADDR_EVENT_NOTIFY        0x0041F4A0u  /* see EVENT_RAISE note */
/* 0x004203A0, fourteen callers. Works out the point an action refers to,
 * which it can express three ways: a pair of VARIABLES, a literal, or -- when
 * the literal's x is zero -- the position of the object the action targets.
 * That last is the same "a zero x means use its own position" idiom
 * EvtDeployItem and ScriptResurrectItem both use, met a third time. */
#define ADDR_ACTION_POINT        0x004203A0u  /* uint32_t(act, uint32_t me) */
/* 0x00421750. Evaluate an `if`'s testvar comparisons -- all of them must pass.
 * Its jump table confirms script.h's operator codes from the far side: 0 '=',
 * 1 '<>', 2 '<', 3 '>', 4 '<=', 5 '>=', in that order, and the 0x1C stride is
 * AM2_ScriptTest's size. Six callers. */
#define ADDR_EVAL_COND_TESTS     0x00421750u  /* int32_t(AM2_ScriptCond *) */
/* 0x00421590, two callers. Evaluate one side of a testvar comparison. Eight
 * kinds through a jump table, and THREE OF THEM ARE NAMED BY THE GAME'S OWN
 * VOCABULARY rather than by me: kind 5 asks whether a unit holds a weapon uid,
 * which is `hasitem`; kind 7 is AllyFlag, which is `isally`; kind 8 starts
 * from a comm slot's COMM_SLOT_OFF_TEAM, which is `teamscore`. All three are
 * in the 185 keywords docs/scripttokens.md lists, and a testvar operand
 * evaluator is exactly where they would be implemented.
 *
 * Anything past 8 falls through to returning the operand unchanged, which is
 * how a LITERAL is expressed. Reconstructed. */
#define ADDR_EVAL_OPERAND        0x00421590u  /* int32_t(kind, a, b) */
#define AM2_OPERAND_VARIABLE     1   /* a name table entry's value */
#define AM2_OPERAND_ITEM_FRAME   2
#define AM2_OPERAND_HEALTH       3
#define AM2_OPERAND_TROOP_STATE  4   /* OBJ_OFF_FIELD_530; 5 when not a troop */
#define AM2_OPERAND_HASITEM      5
#define AM2_OPERAND_SLOT_TAKEN   6   /* 1 when the slot has a player, else 2 */
#define AM2_OPERAND_ISALLY       7
#define AM2_OPERAND_TEAMSCORE    8
/* 0x0040F990, thiscall. Its first act is to read the slot's
 * COMM_SLOT_OFF_TEAM, and EvalOperand's kind 8 answers whatever it returns --
 * which is what makes `teamscore` the keyword it belongs to. Still original. */
#define ADDR_COMM_TEAM_SCORE     0x0040F990u  /* thiscall int32(comm, slot) */
/* 0x0041FF60. Point an object at one of the 256-byte records at 0x004F9ACC
 * and propagate it with SetFieldInAll. What those records HOLD is not
 * established -- "state" was a guess, and AM2_OBJ_STATE_REC_SIZE already means
 * AM2_ObjState's sixteen bytes, which is a different thing entirely. Kinds 2 and 3
 * are handled identically except for WHICH pointer is written -- +0x4C0 and
 * +0x4C8 of the sub-record at obj+0x6C -- and any other kind is refused with
 * "Warning: check if this script command works with this object type!", which
 * names the condition rather than the function. Role name. */
#define ADDR_SCRIPT_SET_OBJ_TABLE 0x0041FF60u  /* void(uint32_t, int32_t) */
#define ADDR_OBJ_TABLE_RECORDS    0x004F9ACCu
#define AM2_OBJ_TABLE_REC_SIZE    0x100
#define OBJ_OFF_SUBRECORD         0x6Cu
#define SUBREC_OFF_TABLE_KIND2    0x4C0u
#define SUBREC_OFF_TABLE_KIND3    0x4C8u
/* 0x0041FC40. A fourth of the "look it up and act if the type fits" twins, and
 * the only one that admits types 2, 3 AND 8 rather than type 2 alone. */
#define ADDR_EVT_TYPE238_ACTION   0x0041FC40u  /* void(uint32_t, int32_t) */
/* The RANK table and the two steps around it. Records are 28 bytes and the
 * base is 0x00473DD4, not the 0x00473DD8 the threshold is read through: the
 * function loads `[rank*28 + 0x473DD8]` for the experience needed and
 * `[rank*28 + 0x473DD4]` for what it hands the second step, which is the same
 * record's first two fields. Taking the higher address as the base would put
 * the whole table one field late with every value still looking sensible.
 *
 * 0x00457BC0 reaches ADDR_COMM_MUST_BROADCAST, so promoting is a thing the
 * network hears about. The second step already had a name and a better one --
 * ADDR_SET_MAX_HEALTH -- which the alias ratchet pointed out. That is what
 * makes the promotion block coherent: it raises the ceiling from the rank
 * record and only then grows the current health toward it.
 *
 * The counter it accumulates is OBJ_OFF_REPAIR_FRAME, and that name is right
 * for the type it was read on. This is the third field that is TYPE-DEPENDENT,
 * after 0xA0 and 0x94: HealObject reads 0x9C on an ITEM (types 1 and 4) as a
 * repair frame, and Type238Action accumulates experience in it on a TROOPER
 * (type 2). Both readings are of live code and neither is wrong; the object is
 * a union past its header. Recorded rather than aliased, as with the other
 * two. */
/* THE RANK TABLE HAD THREE BASES AND NONE OF THEM WAS THE RECORD. It was
 * ADDR_RANK_TABLE here at 0x00473DD4 and ADDR_RANK_RECORDS at 0x00473DD0, and
 * UnitWeaponInfo reads 0x00473DCC -- three consumers, three bases, one stride
 * of 28. They are three FIELDS of one record, and the record starts at
 * 0x00473DCC, which is the only base on which every field is a clean monotone
 * series:
 *
 *   +0   float  2.5 2.2 2.0 1.8 1.6 1.4 1.2 1.0   fire scale
 *   +4   int32   32  48  56  64  80  96 112 128   AiHitReact's threshold
 *   +8   int32   30  35  45  50  55  60  65  70   base max health
 *   +12  int32   40 100 180 300 460 660 920 1000  experience for the rank
 *
 * The +8 reading is confirmed by its consumer and by a number this project
 * already measured: it is handed to SetMaxHealth, and CLAUDE.md records the
 * leader's max health as 140 on a correct build -- 70, the rank-7 entry, times
 * the 2.0 difficulty scale.
 *
 * It is the ADDR_SOLDIER_NAMES mistake three times over. A consumer that
 * touches ONE field indexes correctly from any base that puts that field where
 * it expects, and nothing can see the error until a second consumer wants an
 * earlier field. Here the third consumer wanted the first field of all.
 *
 * Every address stays exactly what it was: each site moved from base+0 to the
 * corrected base plus its own offset, so this renames and cannot re-aim. */
/* AND TWELVE BYTES EARLIER AGAIN, which the paragraph above predicted. A
 * FOURTH consumer wanted an earlier field: 0x00404730 indexes
 * `*(int32 *)(0x00473DC0 + rank * 28) << 1` and forgets a target beyond that
 * distance. So the record starts at 0x00473DC0 and the four fields named above
 * are its LAST four.
 *
 * THE GIVEAWAY WAS ALREADY IN THIS FILE: a 28-byte stride with only 16 bytes
 * of named fields. Three dwords were unaccounted for and they are in FRONT.
 *
 * MONOTONICITY CANNOT SETTLE A BASE, and that is what the paragraph above used.
 * "The only base on which every field is a clean monotone series" was true of
 * the four fields known then; 0x473DC0 gives three more in front of them --
 * 280..320, 48..76, 120..190. A base-plus-a-field is monotone too.
 *
 * TILING SETTLES IT, in one subtraction. 0x473DC0 + 8 ranks * 28 bytes is
 * 0x473EA0, which is exactly ADDR_FORMATION_SLOTS; 0x473DCC + 224 would run
 * twelve bytes into that table. Below, 0x473DB0 holds 480, 1000, 3000, 0 --
 * unrelated constants ending in a zero. It tiles from here and from nowhere
 * else, which is the trig-table argument this project already states.
 *
 * Confirmed a fourth way by a number measured in the running game: +20 at rank
 * 7 is 70, and tools/objdump.py reads the leader's max health as 140 with the
 * 2.0 difficulty scale.
 *
 * Every absolute address is unchanged -- all four call sites index as
 * `base + rank * RANK_REC_BYTES + RANK_REC_OFF_*`, so the base moved back
 * twelve and each offset gained twelve. Measured, not assumed: region.o's and
 * item.o's .text are byte-identical across the change. */
#define ADDR_RANK_RECORDS        0x00473DC0u  /* 28-byte records, rank 0..7 */
/* 0x004036F0, one caller, in the same band. What one object does to another's
 * VIEW: take the blocker's silhouette as seen from the viewer, and for every
 * heading it subtends record how far the view is obstructed, in three height
 * bands. The output is the buffer below and not a return value -- its epilogue
 * sets no eax at all. Reconstructed.
 *
 * A SIXTEEN-ENTRY QUADRANT TABLE picks the silhouette from four comparisons.
 * Eight of the sixteen codes are geometrically impossible with a sane box, or
 * mean the viewer is INSIDE it, and all eight share one exit; the other eight
 * name two extreme corners. Generated from the image rather than transcribed,
 * which is what DirtyCollect's eighty-one arms forced. */
#define ADDR_ADD_SIGHT_BLOCKER   0x004036F0u  /* void(viewer, blocker) */
/* Sixty-four sixteen-byte records, one per heading rounded down to a multiple
 * of four. Three int16 distances -- the three HEIGHT BANDS -- and a generation
 * stamp: a record whose stamp is stale is overwritten and one that is current
 * takes the MINIMUM, so several blockers in one direction leave the nearest.
 * A blocker ABOVE the viewer fills all three bands, one LEVEL with it fills
 * the middle and the far, and one BELOW fills only the far; the bands it does
 * not fill get the viewer's own rank sight range, which is "not obstructed".
 * +0x06 is never touched by this function and has no name. */
#define ADDR_SIGHT_BLOCK_BY_DIR  0x004F8FB8u  /* record[64] */
#define SIGHTDIR_OFF_LOW         0x00u  /* int16 */
#define SIGHTDIR_OFF_MID         0x02u
#define SIGHTDIR_OFF_HIGH        0x04u
#define SIGHTDIR_OFF_STAMP       0x08u  /* int32, against the generation */
/* A SECOND stamp on the same record, and the two gate different things:
 * this one says the tile line has already been walked for this heading, the
 * one above says the three minima are current. AiAttackBody checks them
 * separately and only skips the trace on the first. */
#define SIGHTDIR_OFF_TRACE_STAMP 0x0Cu  /* int32 */
#define AM2_SIGHT_DIR_STRIDE     16u
#define AM2_SIGHT_DIR_STEP       4      /* headings per record */
/* THE INDEX IS THE BEARING TO THE TARGET, NOT THE VIEWER'S FACING, and three
 * reconstructions had it wrong until a fourth member of the family was read.
 * Both are bytes in the same eighth-of-a-turn units and both are live in
 * registers where the index is computed, so `facing` reads perfectly; the
 * original indexes with the register it has just handed AngleDelta as its
 * SECOND argument, which is AngleOfDelta's answer.
 *
 * It matters because the record says how far the view is blocked in a
 * DIRECTION: the one the target lies in. Keyed on the facing, a unit gets the
 * occlusion for wherever its hull happens to point.
 *
 * No check could see it -- the offsets are identical, the band is cold, and
 * `checkoffsetuse` reports byte-for-byte the same before and after. What found
 * it was reading a FOURTH function that does the same thing. */
#define ADDR_SIGHT_GENERATION    0x004F93C0u  /* int32_t */
/* A blocker this far under the viewer is ignored outright, before anything
 * else is computed. */
#define AM2_SIGHT_OVERHEAD       0x18
/* And within this much either way it counts as LEVEL rather than below. */
#define AM2_SIGHT_BAND_STEP      8
/* Added to the silhouette distance before the range test, so a blocker just
 * past the rank's sight range still fails it. */
#define AM2_SIGHT_DIST_PAD       0x10
/* AiAttackBody's tile walk. The running maximum starts below anything a tile
 * attribute can hold, and the count of tiles it got through becomes a distance
 * as `i * SPAN + BASE`. The buffer is the rest of its 0x81C frame. */
#define AM2_AI_SIGHT_FLOOR       (-0x7F)
#define AM2_AI_SIGHT_TILE_SPAN   20
#define AM2_AI_SIGHT_TILE_BASE   16
#define AM2_AI_SIGHT_LINE_MAX    1016
/* AiEngageStep's two arrival thresholds, and they are NOT AM2_AI_ARRIVED_DIST
 * (0x20) or AM2_AI_REACHED_DIST (0xC despite the coincidence): it compares
 * SIGHTC_OFF_DEST_DIST_B against 8 and SIGHTC_OFF_DEST_DIST against 0xC, two
 * different fields against two different numbers. */
#define AM2_AI_ENGAGE_DEST_B     8
#define AM2_AI_ENGAGE_DEST       0xC
/* AiPatrolStep's detour: 360 units toward a kind-7 sighting, once every
 * AM2_AI_KEEP_RANGE_MS. The only use of RandomPointAhead in the AI band. */
#define AM2_AI_PATROL_DETOUR     0x168
#define RANK_REC_BYTES           28u
/* 280 285 290 295 300 305 310 320, doubled at the one site that reads it --
 * so 560..640, the range past which a trooper forgets its target. */
#define RANK_REC_OFF_SIGHT_RANGE 0u    /* int32; 0x00404730 */
/* AddSightBlocker (0x004036F0) is the second reader of both, and it settles
 * what RANK_REC_OFF_FIELD_04 is: it takes the LOW BYTE as an angular
 * half-width and clips a blocker's silhouette to it, so it is the rank's SIGHT
 * ARC in the same eighth-of-a-turn units AngleDelta answers in. Left
 * field-numbered anyway: 48 through 76 is a wide arc for a heading unit and
 * one reader is not a meaning -- but it is the first thing that USES it, where
 * everything before only wrote or copied it. */
#define RANK_REC_OFF_FIELD_04    4u    /* int32; 48 52 56 60 64 68 72 76 */
#define RANK_REC_OFF_FIELD_08    8u    /* int32; 120 130 140 150 160 170 180 190 */
#define RANK_REC_OFF_FIRE_SCALE  12u   /* float; UnitWeaponInfo */
#define RANK_REC_OFF_THRESHOLD   16u   /* int32; AiHitReact halves it */
#define RANK_REC_OFF_MAX_HEALTH  20u   /* int32; handed to SetMaxHealth */
#define RANK_REC_OFF_XP          24u   /* experience needed for this rank */
/* Reconstructed. Bump the rank and, at ranks 3, 5 and 7, hand a type 2 a new
 * weapon -- 0x0A, 0x08 and 0x1D, three ids in no order and with rank 4 and 6
 * giving nothing. The promotion itself happens for every type; only the
 * weapon is a trooper's. */
#define ADDR_RANK_PROMOTE        0x00457BC0u  /* void(obj) */
/* AM2_RANK_MAX is beside OBJ_OFF_RANK, where it belongs -- I defined it a
 * second time here and checkoffsets refused an identical redefinition, which
 * is legal C and says nothing. */
#define AM2_RANK_WEAPON_3        0x0A
#define AM2_RANK_WEAPON_5        0x08
#define AM2_RANK_WEAPON_7        0x1D
#define AM2_RANK_WEAPON_GROUP    0x2D   /* KeyLookupTriple's first argument */
/* Flag bit 1 on the weapon being replaced. Set as it is let go and read
 * nowhere this function can see. */
#define OBJ_FLAG_REPLACED        2u
/* 0x98 IS TYPE-DEPENDENT, the same way 0x94 and 0xA0 are. For a trooper it is
 * the rank, an int32 in 0..7. For an ITEM -- types 1 and 4 -- HeightAtPoint
 * reads its low byte and uses only the SIGN, as "this thing raises the ground
 * you stand on"; a rank could never make that byte negative. One name, both
 * readings, which is what OBJ_OFF_FIELD_94 already does rather than putting a
 * second name on the offset. */
/* A THIRD MEANING, and the field is left with the first name. On a type-5
 * SHOT this holds the SHOOTER's uid: ShotStrike compares it against each
 * candidate's own uid to skip the unit that fired. So 0x98 is a rank on a
 * trooper, a flag byte on an item and a uid on a shot -- read the type before
 * reading the field. */
#define OBJ_OFF_RANK             0x98u  /* int32_t 0..7, or an item's flag byte */
#define AM2_RANK_MAX             7
#define ADDR_TYPE238_ACTION       0x00457CD0u  /* void(void *obj, int32_t) */
/* 0x00417B10, one caller, at the tail of it. Award 300 experience to every
 * live type 2 the player's own army owns. */
#define ADDR_AWARD_OWN_ARMY_XP    0x00417B10u  /* void(void) */
#define AM2_ARMY_XP_AWARD         0x12C        /* 300 */
/* 0x0044BBD0, two callers: put 1 in the dword at +0x548 and then run the line
 * above with 0x2710. Both names are ours. */
#define ADDR_SET_LEADS_AND_ACT    0x0044BBD0u  /* void(void *obj) */
#define AM2_LEADS_ACTION_ARG      0x2710
/* 0x0045AFB0 and 0x0045AFE0: the first entry of the list at VEHICLE_OFF_PTR_LIST
 * resolved to an object, and the same run through ObjType2Field548. Null when
 * there is no object or the list is empty. */
#define ADDR_LIST_FIRST_OBJ       0x0045AFB0u  /* void *(const void *obj) */
#define ADDR_LIST_FIRST_FIELD548  0x0045AFE0u  /* uint32_t(const void *obj) */

/* 0x0041F570 and 0x0041F5C0. A pair over one object flag bit and one global,
 * and they are CROSSED rather than symmetric:
 *
 *              name == ID15                     any other name
 *   0x41F570   flag := 1, and SET the bit on    CLEAR the bit on the
 *              the object at ADDR_EVT_ID15_UID  resolved object
 *   0x41F5C0   flag := 0                        SET the bit on the
 *                                               resolved object
 *
 * So each sets the bit in one arm and the other clears it, and only 0x41F570
 * touches an object on the ID15 path at all. Reproduced exactly; a symmetric
 * reading would be wrong in three of the four cells. */
#define ADDR_EVT_FLAG40_CLEAR     0x0041F570u  /* void(int32_t, uint32_t) */
#define ADDR_EVT_FLAG40_SET       0x0041F5C0u  /* void(int32_t, uint32_t) */
#define ADDR_EVT_ID15_FLAG        0x00511E48u
#define ADDR_EVT_ID15_UID         0x00511E20u
#define OBJ_FLAG8_BIT40           0x40u

/* 0x0041F600 and 0x0041F650. The same thing twice -- chdir to `bitmaps`, drop
 * the bitmap at 0x005122C4 and load a new one -- except that the first also
 * sets the in-mission sub-state to 0x16, raises a flag, and calls
 * SendGamePause(1, AM2_EVENT_FLAG_8).
 *
 * That settles what pause reason 8 IS: a full-screen bitmap is up. frame.cpp
 * calls SendGamePause(0, AM2_EVENT_FLAG_8) -- un-pause, reason 8 -- and this
 * is the matching set. CLAUDE.md records the event flags as the pause mask;
 * this names one of its bits.
 *
 * The two correspond to the script keywords `showbitmap` (65) and
 * `showbitmapnopause` (66). That correspondence is INFERRED from which one
 * pauses, not traced through the action dispatcher, and is labelled as such. */
#define ADDR_EVT_SHOW_BITMAP      0x0041F600u  /* void(const char *) */
#define ADDR_EVT_SHOW_BITMAP_NP   0x0041F650u  /* void(const char *) */
#define ADDR_CURRENT_BITMAP       0x005122C4u
/* The width and height the map-wait bitmap is centred in. They hold 640 and
 * 480 -- exactly what ADDR_SCREEN_W and ADDR_SCREEN_H hold, at different
 * addresses -- so nothing available here distinguishes the two pairs. Named
 * for the use rather than merged with the other pair on a guess. */
#define ADDR_BITMAP_AREA_W        0x00485318u  /* int32_t, 640 */
/* Sprite SET 19 is the AIR SUPPORT set, and three independent things say so.
 * ADDR_AIR_FRAME_DRAW draws ADDR_AIR_SPRITES_2 at 0x004091A4 and reads the
 * air block two instructions later; the reset below zeroes AIR_OFF_COUNT and
 * the eight-slot sub-queue while reloading exactly these sprites; and the
 * free's memset ends at 0x004F96A4, which is ADDR_AIR_SAVE_BLOCK + 0x248, the
 * block's last byte. That last one is arithmetic rather than reading, and it
 * is what turns "something else lives in this block" into a fact.
 *
 * The names were ADDR_SPRITES_19_*, recorded when the set was unidentified.
 * The set and index each array holds are still in the comment because they are
 * what the loader passes; what changed is that the family now has a name.
 *
 * ADDR_AIR_SPRITES_EDGE is NOT part of the arrays. It sits immediately past
 * them and one below the air block, and 0x00408E00 computes it as
 * ADDR_BITMAP_AREA_W plus twice the first sprite's bounds.right -- the length
 * of the track the gauge at 0x00409166 slides that sprite along. */
#define ADDR_AIR_SPRITES_2       0x004F93D8u  /* AM2_Sprite *, set 19 index 2 */
#define ADDR_AIR_SPRITES_3       0x004F93DCu  /* AM2_Sprite *[20], index 3 */
#define ADDR_AIR_SPRITES_6       0x004F942Cu  /* AM2_Sprite *[11], index 6 */
#define ADDR_AIR_SPRITES_EDGE    0x004F9458u  /* int32_t, the gauge track */
#define AM2_AIR_SPRITE_SET       0x13
/* Dwords the free zeroes: 179, where it releases 32. 0x004F93D8 + 179*4 is
 * 0x004F96A4, and ADDR_AIR_SAVE_BLOCK + 0x248 is 0x004F96A4 -- so the sweep
 * takes the sprite arrays AND the whole air-support state, exactly, with
 * nothing over either end. */
#define AM2_AIR_SPRITES_CLEAR    0xB3
/* 0x00408D20 loads them and 0x00408DA0 frees them; 0x00408E40 is one `jmp` to
 * the free, the same shape as ADDR_FREE_SPRITE_LIST_ALIAS, and is the ONLY way
 * in -- the free itself has no other reference. 0x00408E00 is the reset that
 * drives both. Reconstructed, all four. */
#define ADDR_LOAD_AIR_SPRITES    0x00408D20u  /* void(void) */
#define ADDR_FREE_AIR_SPRITES    0x00408DA0u  /* void(void) */
#define ADDR_FREE_AIR_SPRITES_ALIAS 0x00408E40u /* void(void), one jmp */
#define ADDR_RESET_AIR_SUPPORT   0x00408E00u  /* void(void) */
#define ADDR_BITMAP_AREA_H        0x0048531Cu  /* int32_t, 480 */
/* The pause bits that mean "the map is still loading", which is what puts the
 * wait bitmap up. Four bits, 17 through 20, tested as a group. */
#define AM2_PAUSE_MAP_WAIT        0x1E0000u
/* The transport's own reason for pausing: the send pool has run dry.
 * DestroyFlow clears it once reclaiming a departing player's buffers has put
 * the pool back above AM2_FLOW_UNPAUSE_FREE, so a player LEAVING is one of
 * the things that can restart a stalled game. */
#define AM2_PAUSE_NO_BUFFERS      0x8000u
#define AM2_FLOW_UNPAUSE_FREE     0x12C    /* 300 free buffers */
#define AM2_PLAYER_RECORDS        6        /* 0x004F1980..0x004F48C0 */
/* 0x00462600, 1088 bytes. Whatever the paused mission frame drives before it
 * considers the wait bitmap; stays original and unnamed, since nothing here
 * says what it is. */
#define ADDR_PAUSED_FRAME_STEP    0x00462600u  /* void(void) */

#define ADDR_FREE_BITMAP          0x00446410u  /* void(void **slot) */
/* Reconstructed. Its point is AM2_Sprite::source: an ABSOLUTE path, built from
 * the current directory, so a surface lost after the game has chdir'd into a
 * map directory can still be rebuilt. */
#define ADDR_LOAD_BITMAP          0x004462F0u  /* void *(const char *, int32) */
#define ADDR_STR_LOAD_SPRITE_FAIL 0x004897CCu  /* "Unable to load sprite %s\n" */
#define ADDR_CRT_ATOI             0x004660A7u  /* int32_t(const char *) */
#define ADDR_PARSE_SPRITE_NAME    0x0042E310u  /* int32_t(name, int32*x3) */
/* The pseudo-SET a bitmap loaded by plain filename is registered under. The
 * id is PreloadSprite's own packing with set 99 and frame 0, so a name that
 * does not parse still lands in the same key space as one that does. */
#define AM2_SPRITE_SET_BY_NAME    99
#define ADDR_STR_BITMAPS_DIR      0x00478670u  /* "bitmaps" */
#define ADDR_STR_MAPWAIT_BMP      0x0048520Cu  /* "mapwait.bmp" */
/* 0x16 is 22, which is AM2_SUBSTATE_BASE -- the FIRST arm of the thirteen-entry
 * sub-state table, and one of the nine that repaint when the overlay is dirty
 * and then call DrawMenuOverlay. The flag it raises alongside is
 * ADDR_OVERLAY_DIRTY, already named, which is exactly that dirty bit. So
 * `showbitmap` selects the overlay arm and marks it needing paint, and
 * CLAUDE.md's reading of that table is confirmed from a caller. */
#define AM2_SUBSTATE_BITMAP       0x16

/* 0x0041FFD0. Pushes a one-deep "current object" context: three globals are
 * copied into three companions before being overwritten. Nothing here says
 * what the context is FOR, so all six keep positional names. */
#define ADDR_EVT_PUSH_OBJ_CTX     0x0041FFD0u  /* void(uint32_t uid) */
#define ADDR_OBJ_CTX_OBJ          0x005122CCu
#define ADDR_OBJ_CTX_OBJ_PREV     0x005122D0u
#define ADDR_OBJ_CTX_VAL          0x00511E28u
#define ADDR_OBJ_CTX_VAL_PREV     0x00511E2Cu
#define ADDR_OBJ_CTX_SET          0x00511E3Cu
#define ADDR_OBJ_CTX_SET_PREV     0x00511E40u
/* 0x00421C40. Routes the end of a mission: in single player straight to
 * ADDR_SCRIPT_FIND_FILE, which builds "%s%d.txt" and loads the next script,
 * and in a multiplayer session to 0x00421800 instead -- which takes an extra
 * argument the local path drops. Role name; ADDR_MP_SESSION is the only test.
 *
 * The local arm already had a name and it is kept. Reading 0x00421C40 alone
 * suggested "the single-player way of handling a condition", which is what
 * ADDR_COND_LOCAL would have said; the callee's own "%s%d.txt" says it is the
 * mission loader, and that is the better description. */
#define ADDR_ADVANCE_MISSION     0x00421C40u  /* void(int32_t, int32_t) */
#define ADDR_MISSION_NETWORKED   0x00421800u  /* void(int32_t, int32_t) */

/* The callbacks DeclareRuleVars registers. The first two are six-byte
 * wrappers over one shared handler differing only in a 0 or a 1, which is what
 * makes "army" and "team" safe to say; the other three are named for the
 * global their uid is stored in, because their bodies are not identified. */
/* Direct string literals, not pointers to them: DeclareRuleVars pushes these
 * addresses straight into ScriptNameUid. */
#define ADDR_NAME_GREENWINS      0x00478880u
#define ADDR_NAME_TANWINS        0x00478878u
#define ADDR_NAME_BLUEWINS       0x0047886Cu
#define ADDR_NAME_GREYWINS       0x00478860u
#define ADDR_NAME_GREENTEAMWINS  0x00478850u
#define ADDR_NAME_TANTEAMWINS    0x00478844u
#define ADDR_NAME_BLUETEAMWINS   0x00478834u
#define ADDR_NAME_GREYTEAMWINS   0x00478824u

#define ADDR_EVT_ARMY_WINS       0x00422250u
#define ADDR_EVT_TEAM_WINS       0x00422260u
#define ADDR_EVT_RULE_A          0x00422270u
#define ADDR_EVT_RULE_B          0x00422310u
#define ADDR_EVT_RULE_C          0x004223A0u
#define ADDR_EVT_CONDITION       0x00421E80u  /* every `if` in the script */
#define ADDR_RULE_UID_A          0x00510218u
#define ADDR_RULE_UID_B          0x0051021Cu
#define ADDR_RULE_UID_C          0x00510220u
/* Names recovered from the error strings the functions print about
 * themselves. None is reconstructed yet; recorded so the names are here when
 * they are, rather than being guessed at from a call site again. */
/* Its third argument to DeployItem is 1 where EvtDeployItem passes 0, and that
 * is the resurrect flag -- which is why the callee's own line reads
 * "DeployItem(resurrection)". Two functions, one flag, and the string
 * explains itself once both are read. */
#define ADDR_SCRIPT_RESURRECT_ITEM 0x0041FEC0u  /* void(uint32_t, uint32_t) */
#define AM2_DEPLOY_RESURRECT       1
#define ADDR_SCRIPT_SET_OBJ_BITMAP 0x00420060u
/* 0x004371A0. Advance one object along its object script by one frame, if its
 * deadline has passed. Two of its own log strings name it and its callee:
 * "UpdateObjectScript: bad state index" and "ChangeObjectFrame failed in
 * UpdateObjectScript". The comment here used to read "also ChangeObjectFrame",
 * which put two names on one address -- they are separate functions and the
 * second is 0x004351C0, reached from nine places. */
#define ADDR_UPDATE_OBJECT_SCRIPT  0x004371A0u  /* int32_t(void *obj) */
#define ADDR_CHANGE_OBJECT_FRAME   0x004351C0u  /* int32_t(obj, frame, int32) */
/* What it does the work through: 0x00434F20 reaches ADDR_DEF_FIND_OBJ_REC,
 * ADDR_ITEM_TEARDOWN and ADDR_PRELOAD_SPRITE, so it re-resolves the object's
 * record and loads the sprite for the new frame. Named for that; nothing in
 * it names itself.
 *
 * The two fields ChangeObjectFrame unpacks out of the type record's +8 and
 * hands it. The same dword StepType1And4 compares whole against
 * ADDR_WATCHED_TYPE_ID, so it is packed rather than scalar. */
#define ADDR_APPLY_OBJ_FRAME     0x00434F20u  /* int32(obj, a, b, frame, int32) */
#define AM2_OBJREC_SHIFT_A       7u
#define AM2_OBJREC_MASK_A        0x3FFu
#define AM2_OBJREC_SHIFT_B       0x13u
#define AM2_OBJREC_MASK_B        0x7Fu
/* Bit 23 of OBJ_OFF_FLAGS. A chained object carrying it is SKIPPED by
 * ChangeObjectFrame -- but the walk continues past it. */
#define OBJ_FLAG_NO_FRAME        0x800000u
/* 0x00427E80, and its ONLY caller is ADDR_HEAL_OBJECT below -- all three call
 * sites are inside it -- so the name cannot be wrong about anything else. It
 * raises event kind 6 through ADDR_EVENT_NOTIFY for the object that was
 * healed, and for a second object too when one is supplied; a null second
 * argument takes the shorter arm and names only the first. */
#define ADDR_NOTIFY_HEALED       0x00427E80u  /* void(void *obj, void *src) */
/* 0x00427EF0, three callers, all of them the pickup family above. The fourth
 * member of the notifier template: it raises AM2_EVENT_PICKED_UP for the first
 * argument, and for the second as well when one is supplied.
 *
 * The argument order is the template's -- the first becomes the event's first
 * (num, uid, mask) triple and the second the second, the same way
 * ADDR_NOTIFY_DAMAGED puts the victim first and the attacker second, which is
 * what `hit <a> by <b>` and `pickedup <a> by <b>` read as. Note the callers
 * pass them in the OPPOSITE order from their own parameter list: each pushes
 * its second argument first. Reading those callers, the object that arrives
 * here first is the ITEM -- it is the one whose uid is logged, whose
 * OBJ_OFF_FLAGS gains bit 1, and which is stamped with a two-second deadline
 * at +0xC8 -- and the second is the trooper, whose army is what the AI sweep
 * beside the call is given. */
#define ADDR_NOTIFY_PICKED_UP    0x00427EF0u  /* void(void *item, void *taker) */
/* 0x00427F60, ONE caller, and the same template again with the literal 8 --
 * the two bodies differ in that byte and in nothing else.
 *
 * That caller is TrooperDropItem, off its own two log lines, and it is what
 * confirms the argument order for BOTH halves of the pair rather than only
 * this one. The object it passes first is the one whose uid it logs, and its
 * last act after the notify is to write army 4 -- the neutral army -- into
 * that object's OBJ_OFF_ARMY -- an item going ownerless as it leaves the
 * trooper's hands. So first is the item and second the trooper here too,
 * which is `dropped <a> by <b>`. */
#define ADDR_NOTIFY_DROPPED      0x00427F60u  /* void(void *item, void *dropper) */
/* 0x00428370, eight callers. Heal `obj` by `pct` percent of its MAXIMUM
 * health, then notify. The percentage is clamped to 0..100 first, and the
 * result to the maximum, and an object already at or below zero health is
 * never touched -- healing does not resurrect.
 *
 * An ITEM -- ADDR_OBJ_IS_ITEM, types 1 and 4 -- ignores the percentage
 * entirely and goes to full health. Which of its two arms runs depends on
 * OBJ_OFF_REPAIR_FRAME: positive means the item also gets
 * ADDR_CHANGE_OBJECT_FRAME(obj, 0, 0) and is repaired unconditionally, while
 * zero or less repairs only an item that is alive and not already full.
 *
 * One of the eight callers is the "doctor doctor" cheat's per-object callback
 * at 0x00417A90, which is what confirms the reading: its message is
 * "Avoid the agony...". */
/* Positive means the item has a repair frame. ADDR_HEAL_OBJECT was the only
 * reader this reconstruction had read; LoadType1 is a second, and it agrees
 * -- it replays the field through ChangeObjectFrame with flag 0 when it is
 * positive, which is the same "positive means there is one" test. */
#define OBJ_OFF_REPAIR_FRAME     0x9Cu  /* int32_t */
/* Runs one parsed action against an owner. 4096 bytes in event.cpp with three
 * callers, and it names itself nowhere -- so this is a ROLE, not a recovered
 * source name, and it stays that way until the body says otherwise. */
#define ADDR_RUN_SCRIPT_ACTION     0x00420410u  /* void(action *, void *owner) */
/* SURVEYED, and it is the most uniform thing left. 4,096 bytes, SEVENTY-TWO
 * exits, SIXTY-EIGHT distinct callees and NOT ONE of them unnamed, one jump
 * table and no other indirect control flow.
 *
 * The dispatch is `eax = act->code; dec; cmp 0x39; ja default;
 * jmp [eax*4 + 0x0042131C]` -- so codes 1..0x3A index a DIRECT table of 58
 * entries, and all 58 targets are DISTINCT. No shared arms, no byte index,
 * no arm ending inside another to find. That is the opposite of every
 * dispatch this project has met: WeaponClassOf scrambled its order,
 * SpriteKeyForKind shared three slots, TrooperFire's nineteen indices had two
 * arms, and 0x0044A420's seventeen had two. Fifty-eight for fifty-eight is
 * worth stating precisely because it is the case the rule does NOT have to
 * guard against.
 *
 * At ~70 bytes an arm the work is breadth rather than depth, and the arms are
 * NAMED already: script.cpp's own ScriptParseActionRecon assigns act->code in
 * 39 places from the keyword, and that parser is verified against
 * tests/actions-reference.txt over 9,934 records. So the keyword for each of
 * the 58 codes is recoverable from our own source rather than guessed.
 *
 * Three callers: ScriptRunLine, one in the objscript band, and its own tail.
 * The name is a ROLE and stays one until the body says otherwise.
 *
 * AND THE ARMS NAME THEMSELVES THROUGH THEIR CALLEES, so the keyword table is
 * not needed after all. Code 1 calls ADDR_HUD_MESSAGE, code 2
 * ADDR_EVT_SHOW_BITMAP, code 3 ADDR_EVT_SHOW_BITMAP_NP -- which are
 * `showmessage`, `showbitmap` and `showbitmapnopause` in the script
 * vocabulary, in that order. 49 of the 58 arms contain a named call.
 *
 * Two failed attempts before that, both worth not repeating: the parser's
 * keyword-to-code mapping is NOT extractable by pattern, because
 * ScriptParseActionRecon assigns act->code in 39 places through several small
 * {id, code} tables inside case groups, and a regex for the obvious form
 * finds four of them. And tests/actions-reference.txt carries the code as
 * field 5 of each record but no keyword at all.
 *
 * The arms average TWENTY instructions and the shape repeats: fetch a field
 * of the action record, refuse if it is null, call one named function,
 * return through the common epilogue. 1,166 instructions over 58 arms.
 *
 * THE PROLOGUE IS A DEFERRAL AND IT IS THE ONLY SUBTLE PART. An action whose
 * uid is not -2 does NOT run here at all unless its code is 0x1A: it is
 * handed to EventNotify and executed later. Both branches of that hand-off
 * are the same call --
 *
 *     EventNotify(act->uid2, act->uid, owner, 0, 0, 0, 0, delay, 0, 0)
 *
 * -- differing only in the delay, which comes from GetVarValue(act->xvar)
 * when xvar is positive and from act->delay otherwise. Read as two calls
 * they look like two behaviours; they are one call and a choice of delay.
 *
 * AND GetVarValue WRITES INTO ARGUMENT 1's SLOT. `lea ecx, [esp+0x70]` two
 * pushes in resolves to the incoming `act` pointer's stack slot, reused once
 * the pointer is in esi. Reading the two dwords after it as a pair -- which
 * is what the displacements invite -- gives the variable's value and the
 * OWNER argument, and calling that a two-field result would put the owner
 * where a delay belongs. Third argument-slot reuse in three functions.
 *
 * TWO ARMS SHARE THE THREE-PATH POINT BLOCK, NOT THREE. 0x1F ends in
 * EvtAtPointC / EvtAtObjPosC and 0x18 in the A variants. 0x19 does NOT have
 * the block at all: after the same AI-mode head it calls EvtObjPair on two
 * resolved uids and returns, with no point, no variables and no third path.
 *
 * That was written down here as three sharers on the strength of 0x19's
 * CALL LIST -- EvtObjPair and EvtArmyAttach looked like a third callee pair
 * in the same slot. Dumping the arm shows a 0x54-byte body with no point
 * handling in it. A list of what a function calls is not a description of
 * its shape, which is the same mistake as counting epilogues to count arms.
 *
 * Three uses of one shape with a substituted callee pair is the case for a
 * parameterised helper -- and this file records the argument against merging
 * on resemblance three times over, so the test is whether the three blocks
 * are identical in STRUCTURE and differ only in which functions they call.
 * They are, and they do. Writing them out is still the safer choice while the
 * function is unverified: a helper taking two function pointers hides which
 * pair each arm uses at exactly the place a reader checks it.
 *
 * `moveto` HAS THREE PATHS, NOT A PAIR. A named variable pair gives the
 * point; failing that, an action carrying NO point at all moves one object to
 * ANOTHER OBJECT through EvtAtObjPosC; failing that, the literal point the
 * script wrote goes to EvtAtPointC.
 *
 * The middle test is `fireweapon`'s three-part guard used to SELECT instead
 * of to refuse -- relative zero and the point's low word zero. So the same
 * predicate appears in this one function as a refusal in two arms and as a
 * branch in a third, which is a good reason to write it out each time rather
 * than hoist it into a helper whose name would have to mean both.
 *
 * Reading the `jle` and assuming the else is "use the literal point" gets two
 * of the three right and silently drops the object-to-object case, which is
 * the one a script uses to say `moveto <thing>` rather than `moveto x,y`.
 *
 * `createtrooper` ENDS WITH THE CHEAT CONSOLE'S WEAPON-ATTACH, instruction
 * for instruction: KeyLookupTriple for the item key, CreateWeapon into the
 * scratch name, then set the weapon's army byte, put its uid in the unit's
 * OBJ_OFF_WEAPON_UID, SoldierKindForWeapon on the weapon's +0xC0 chain and
 * SendTrooperSetWeapon to publish it. Four calls in a fixed order.
 *
 * That sequence was written once already, in cheat.cpp's CheatSwapWeapon,
 * and finding it here is the "grep for the SHAPE" rule paying off a third
 * time -- after SettlePointInRegion turning out to be NearestAllowedTile's
 * spiral and ItemLinkCells being RowRegisterAll's registration. The two are
 * NOT merged: the cheat's version swaps a weapon on an existing unit and has
 * the old-weapon flag arm this one has no use for.
 *
 * `createvehicle` MAPS act->army TWO DIFFERENT WAYS IN ONE CALL. Its `table`
 * argument is CommArmyOfSlot(comm, act->army) unconditionally; its `army`
 * argument is CommSlotForArmy(comm, act->army) under multiplayer and the raw
 * act->army otherwise. So one field feeds two parameters through two
 * OPPOSITE conversions, and the conditional is on only one of them.
 *
 * The names make it look like a mistake and it is not: CommArmyOfSlot and
 * CommSlotForArmy are inverses, and this file records that they are separate
 * functions with 47 and several callers respectively. Reproduced as written.
 *
 * `createroach` PASSES ZERO AS ITS KIND, which is not act->n0 or anything
 * else the action carries -- a literal `push 0` before the name. So a script
 * cannot choose a roach kind through this action even though CreateRoach
 * takes one. Worth recording because supplying act->n0 there is the natural
 * completion and would be an invention.
 *
 * FOUR ARMS GUARD ON `(int16_t)point == 0` AND NOT ON THE DWORD. Codes 0x0E,
 * 0x10, 0x31 and 0x32 all take ActionPoint's packed answer and test `ax`, so
 * a point at x == 0 is refused whatever its y. Same width question as
 * `fireweapon`'s third test, from the other direction -- there the field is
 * read narrow, here the RESULT is.
 *
 * `fireweapon` REFUSES UNLESS THE ACTION NAMES NO POINT, by three separate
 * tests: act->xvar must not be positive, act->relative must be zero, and
 * act->u.pos.x -- a WORD, tested with `cmp word ptr [esi+0x20], 0` -- must be
 * zero too. Only then does it fire at an object. Writing that as one
 * "has a point" predicate over the packed dword would test the wrong width
 * on the third and accept actions the original refuses.
 *
 * Its four arguments are three ResolveUid calls and act->n0, and they are not
 * in push order: FireWeaponAtObject(weaponUid, unitUid, heading, targetUid)
 * takes the SUBJECT resolved with me=0 as the weapon, the ARMY field resolved
 * with the owner as the unit, and the TARGET as the target. The me=0 on one
 * of three otherwise identical calls is the kind of thing only the header's
 * parameter names make readable.
 *
 * CODES 0x18 AND 0x2A ARE THE SAME PAIR, and 0x18 has a tail. Both choose
 * between EvtArmySetField(army, subject, n0) and
 * EvtSetAiMode(ResolveUid(subject, owner), n0) on act->extra -- so extra
 * picks the TARGET, a whole army or one object, and not the value. 0x2A stops
 * there. 0x18 goes on to build a point from two variables and hand the whole
 * thing to AtPointA.
 *
 * Worth noting because the shared half is 20 instructions and the tail is
 * another 40: the temptation is to write 0x2A as a call to 0x18's helper with
 * the tail skipped, which is right until someone changes one of them.
 *
 * THREE ARMS LOOK LIKE ONE HELPER AND ARE THREE DIFFERENT THINGS. Codes
 * 0x22, 0x23 and 0x24 all open `if (act->xvar ...) GetVarValue(...)` and all
 * three differ:
 *
 *   0x22 `addtovar`      -- the shared shape: variable if xvar > 0, else n0.
 *   0x23 `setvar`        -- tests xvar for NON-ZERO, not positive, and
 *                           REFUSES outright if the read fails instead of
 *                           falling back to n0.
 *   0x24 `setobjbitmap`  -- reads the variable and THROWS IT AWAY. Both
 *                           branches call ScriptSetObjBitmap with act->n0;
 *                           a positive xvar only buys a GetVarValue whose
 *                           answer nothing uses.
 *
 * Factoring all three onto the helper is the obvious tidy-up and it would be
 * wrong twice: it would make 0x23 accept a failed read and would make 0x24
 * pass the variable. Neither is visible without following the else branch,
 * which for all three is a separate block reached by a forward jump.
 *
 * Second arm here to discard a call's result, after playsoundon.
 *
 * SEVERAL ARMS TAKE A VALUE FROM A VARIABLE OR FROM act->n0, and the two
 * halves are not the same width. When act->xvar is positive the value is
 * GetVarValue's full dword; otherwise the original loads `al` alone and jumps
 * back into the common path, so the dword it pushes carries whatever was in
 * the rest of eax.
 *
 * That is only harmless because the callees take an int8_t --
 * EvtSetByte40 and EvtSetByte530 both do, and their headers say so because
 * they were reconstructed first. Written as a shared helper returning int32
 * with the CAST at the call, which reproduces both halves without
 * reproducing the garbage.
 *
 * Same shape as Log2Mask leaving its argument in the upper three bytes of
 * eax, which this file records as needing a byte_ret flag in the vector
 * harness. Second instance, and both were only visible from the callee's
 * declared parameter.
 *
 * `setdamagepad` TOUCHES TWO ARRAYS WITH TWO STRIDES, and the second is
 * reached through the first. Code 0x3A writes PAD_OFF_DAMAGE, +0x38 and
 * PAD_OFF_DAMAGE_KIND of ADDR_PADS[subject] -- stride 72 -- and then, only
 * when the damage is positive, marks ADDR_PAD_NUMBERS[pad->+0x04] -- stride
 * 76 -- by writing 1 into PADNUM_OFF_PADS.
 *
 * The index for the second array is the PAD's own +0x04, not the subject.
 * Reusing the subject there would index the wrong record with the wrong
 * stride and be wrong twice over, and the arm's `lea eax,[eax+eax*8]` twice
 * over is what distinguishes them -- 9*8=72 for one and 19*4=76 for the
 * other. Both bases already exist in this file, so the offsets are fields
 * and no new base was introduced.
 *
 * TWO ARMS READ A FIELD NARROWER THAN IT IS DECLARED, and both would be
 * invisible if written as the whole dword. Code 0x34 takes the new owner with
 * `mov dl, byte ptr [esi+0x38]` -- ONE BYTE of act->n0, and EvtSetOwner's
 * parameter is an int8_t, which is the corroboration. Codes 0x31 and 0x32
 * test `ax` rather than eax after ActionPoint, so a point whose LOW WORD is
 * zero is refused while one whose high word is zero is not.
 *
 * A packed point is two int16s and testing the whole dword is the natural
 * thing to write; it accepts points the original rejects, at the x==0 column
 * of the map only. Small, real, and nothing in the suite would see it.
 *
 * AND `restorecamerafocus` IS NOT THE INVERSE OF `setcamerafocus`. Code 0x1E
 * writes OBJ_CTX_OBJ from OBJ_CTX_OBJ_PREV and OBJ_CTX_VAL from
 * OBJ_CTX_VAL_PREV -- restoring two fields -- and then writes
 * OBJ_CTX_SET_PREV from OBJ_CTX_SET, which is the other direction. Two
 * restores and one SAVE, in one six-instruction arm.
 *
 * Written out rather than tidied into a three-way swap, because a symmetric
 * reading is what an eye supplies and it would be wrong about the third
 * field. The same shape as the pause/unpause pair and the states 0-and-3
 * ordering this file already records: reproduce the asymmetry, note it, do
 * not decide it was a mistake.
 *
 * AND ONE ARM DISCARDS A CALL'S RESULT, which the push order disguises.
 * Code 7, `playsoundon`, calls ActionPoint and then overwrites eax with n0
 * before anything reads it -- so the point is computed and thrown away, and
 * EvtPlaySoundOn gets ResolveUid's answer instead. Pairing the pushes with
 * the nearest call reads ActionPoint's result as a THIRD argument to
 * ResolveUid, which takes two. The signature in event.h is what settles it.
 *
 * Reproduced rather than dropped: the call may not be pure, and this project
 * has already recorded ObjInitCommon's discarded first ClassifyCode74 result
 * as the same shape.
 *
 * The record needs no new offsets: AM2_ScriptAction in script.h is already
 * fully named from the parser side, and every field the runner touches --
 * uid, uid2, delay, code, subject, text, item, n0, n1, army, extra -- is in
 * it. A runner and its parser sharing one struct is worth more than either
 * having its own. */

/* The four object fields the object-script runner uses, all read out of
 * UpdateObjectScript's body rather than guessed at a call site. */
#define OBJ_OFF_SCRIPT_ID        0xB0u   /* 1-based index into the table; 0 = none */
#define OBJ_OFF_SCRIPT_STATE     0xB4u
/* UNRESOLVED, and recorded rather than renamed. Two functions WRITE a POINT
 * into this field -- Type2ActionB puts ADDR_ZERO_POINT there and PointActionA
 * puts the point ADDR_NEAREST_ALLOWED_TILE hands back -- while two READ it
 * as an int32 and compare it against a script value (objscript.cpp's state
 * compare and event.cpp's testvar). Both cannot be describing the same thing.
 *
 * Two independent writers agreeing it is a position is the stronger half, but
 * the readers are not obviously wrong either, and nothing here settles which.
 *
 * A THIRD KIND OF EVIDENCE ARRIVED WITH THE AI STEP, and it is a reader
 * rather than a writer, which is what the earlier tally was short of.
 * ADDR_AI_BUILD_CONTEXT tests the field with `cmp word` -- two bytes, not
 * four -- and then hands `&obj[0xB4]` to ADDR_APPROX_DIST as its second
 * `const AM2_Point *`, against the object's own OBJ_OFF_POS as the first,
 * storing the answer at SIGHT_OFF_DEST_DIST. AiStepIgnore then treats that
 * distance as "how far to the place I am going" and clears the field to
 * ADDR_ZERO_POINT on arrival. A packed point is the only reading under which
 * all of that means anything.
 *
 * Still not renamed. Four functions now say point and two still say script
 * value, and the two are not obviously reading the wrong object -- the honest
 * state is a field the game overloads or a name this file has not found, not
 * a majority vote. The byte pattern is identical either way, so no code
 * depends on the answer yet. */
/* Named structurally: nothing read so far says what any of the three is. The
 * word at 0xB2 is cleared beside the script id, 0xEC is set to whether 0xF4 is
 * positive, and 0xE4 is the AI mode OBJ_OFF_AI_MODE names. */
#define OBJ_OFF_FIELD_B2         0xB2u   /* uint16_t */
#define OBJ_OFF_FIELD_EC         0xECu   /* int32_t, 0 or 1 */
#define OBJ_OFF_FIELD_F4         0xF4u   /* int32_t; only its sign is read */
/* 0x0043A0A0, six callers. The nearest tile the object's POINT RULE will
 * accept, starting from one it is given, with the point written back through
 * the caller's pointer.
 *
 * The old comment said "what it is FOR is not established, and the object
 * argument's role least of all". Both are settled now: the object chooses the
 * rule through ADDR_SET_POINT_RULE, and when the given tile is already
 * acceptable the function is a no-op that only fills in the point. When it is
 * not, a four-direction SPIRAL walks outward until the rule accepts, refusing
 * candidates that are off the map or in a different region.
 *
 * IT RETURNS THE TILE, in ax, and the old signature said void. Callers ignore
 * it, which is why nobody noticed. Reconstructed; its counter reads 1 because
 * four of the six callers are ours, and whether the spiral ever runs is not
 * established. */
#define ADDR_NEAREST_ALLOWED_TILE 0x0043A0A0u /* uint16(obj, tile, uint32 *) */
#define ADDR_SPIRAL_DX           0x0048782Cu  /* int32_t[4]: 0, 1, 0, -1 */
#define ADDR_SPIRAL_DY           0x0048783Cu  /* int32_t[4]: -1, 0, 1, 0 */
/* A SECOND spiral table, the same four directions in a different layout:
 * {int32 dx, int32 dy} interleaved, stride 8, where the pair above is two
 * separate arrays. NearestClearPoint reads only the LOW WORD of each with
 * `mov ax, word ptr` and shifts it up four, which is exact for the 0 and +/-1
 * the table holds and gives a step of 16 world units. Not an alias of the
 * pair above -- a different address and a different shape -- and worth knowing
 * before assuming this image has one spiral table. */
#define ADDR_SPIRAL_STEP         0x00485340u  /* {int32 dx, int32 dy}[4] */
#define AM2_SPIRAL_STEP_STRIDE   8u
#define AM2_SPIRAL_STEP_SHIFT    4    /* dx << 4 == 16 world units */
/* 0x004579C0, two callers. Reconstructed. */
#define ADDR_NEAREST_CLEAR_POINT 0x004579C0u  /* void(uint32 from, AM2_Point*) */
/* 0x0045B930, one caller. The same spiral for a VEHICLE: walk out until the
 * vehicle can stand at the point facing the way it faces. Reconstructed. */
#define ADDR_NEAREST_CLEAR_VEHICLE_POINT 0x0045B930u /* void(veh, facing,
                                                      *      uint32, pt *) */
/* 0x0045BC70, three callers. How blocked is that vehicle at that point facing
 * that way -- the vehicle counterpart of ADDR_ROACH_MASK_WEIGHT, and it reads
 * VEHICLE_OFF_KIND off its first argument. Nothing in it says what it is; the
 * name is from what its callers do with the answer. */
#define ADDR_VEHICLE_BLOCK_WEIGHT 0x0045BC70u /* int32(veh, facing, uint32,
                                               *       int32) */
#define AM2_VEHICLE_CLEAR_WEIGHT  0x1E  /* below this and the vehicle fits */
#define AM2_VEHICLE_FACING_BITS   5     /* the facing is rounded to 32 */
#define AM2_BLOCK_CLEAR          0x0F  /* a weight below this is passable */
/* VehicleBlockWeight's own constants, all of them literals in its two loops.
 * The three speeds are OBJ_OFF_FIELD_44, which ADDR_STEP_OBJ_ROWS fills from
 * the row's animation -- so these are all "how fast is it moving". The two
 * CRUSH speeds differ between the boat's loop and everyone else's, which is
 * the sort of thing a merged loop loses. */
#define AM2_VEHICLE_HIT_COOLDOWN   0x64  /* ms added to OBJ_OFF_DEADLINE_58 */
#define AM2_BOAT_CRUSH_SPEED       0x28  /* above this a BOAT does damage/3 */
#define AM2_VEHICLE_CRUSH_SPEED    0x3C  /* above this everything else does */
#define AM2_VEHICLE_NOISY_SPEED    0x50  /* above this it always makes a noise */
#define AM2_VEHICLE_NOISE_ODDS     0x10  /* else 16 of 256 GameRand draws do */
#define AM2_VEHICLE_NOISY_RANK     0x0F  /* an ITEM under this stays quiet */
#define AM2_VEHICLE_BLOCKED_WEIGHT 0x40  /* the final sample's verdict */
#define AM2_DAMAGE_KIND_RUN_OVER   4
/* The three crush sounds, picked on the vehicle kind. Named from the caption
 * table at 0x00419A18, which is the program's own vocabulary for these
 * numbers: JEEP, TANK, H|T, CONV, ???, BOAT for kinds 0 to 5. */
#define AM2_SND_CRUSH_TANK         0x23
#define AM2_SND_CRUSH_JEEP         0x24
#define AM2_SND_CRUSH_OTHER        0x25
#define AM2_VEHICLE_KIND_JEEP      0
#define AM2_VEHICLE_KIND_TANK      1
#define OBJ_OFF_SCRIPT_FRAME     0xB8u
#define OBJ_OFF_SCRIPT_NEXT      0xBCu   /* deadline, compared against 0x00511E04 */
/* A second deadline on the same clock, at +0x58. It had "0x004355D0 is the
 * only thing that reads it" here, and that is no longer true: ObjCollidesWith
 * reads it too -- an object of your own army is collided with only once five
 * seconds have passed since it -- and 0x0045BC70 STAMPS it, with the clock
 * plus 100, on the object it has just collided with. So it is a per-object
 * cooldown as well as the deadline 0x004355D0 uses to set OBJ_FLAG_OVERDUE.
 * Both names ours; the field has two users on two timescales. */
#define OBJ_OFF_DEADLINE_58      0x58u
#define AM2_COLLIDE_OWN_DELAY    0x1388  /* 5,000 ms before your own blocks */
/* 0x0045B700, two callers, both in 0x0045BC70. Does `from` run into `obj`?
 * The per-object half of a collision test, and the arms are stated in
 * item.cpp. Reconstructed. */
#define ADDR_OBJ_COLLIDES_WITH   0x0045B700u  /* int32_t(void *from, void *obj) */
#define OBJ_FLAG_OVERDUE         0x02u
/* The eight per-TYPE frame steppers ADDR_OBJ_FRAME_STEP dispatches to. Named
 * by the type they serve, because the jump table at 0x00428564 is what
 * establishes that and none of them names itself -- swept for pushed string
 * literals and not one carries any.
 *
 * TYPES 1 AND 4 SHARE AN ARM. The table has 0x004284F9 twice, so reading the
 * eight bodies top to bottom and numbering as you go gets everything after
 * type 3 wrong. Take the order from the table, as with the sub-state arms.
 *
 * Type 7's is ADDR_OBJ_MARK_IF_OVERDUE, which already had a name from its own
 * body -- a useful check that these really are per-frame handlers. */
#define ADDR_STEP_TYPE1_4        0x00433EC0u  /* void(obj); types 1 AND 4 */
/* What ADDR_STEP_TYPE1_4 reaches, named from what each does with its
 * arguments -- none of the four names itself and none carries a string.
 *
 * 0x00429040 calls RoundTo8, Cos8, Sin8, ftol and RowUpdate, so it moves an
 * object along a facing and re-links its map rows. 0x00422860 calls
 * GameMalloc and TileOfPoint and takes TEN arguments, so it makes something
 * new at a place. What either is FOR is not established.
 *
 * 0x00516164 holds the literal 0xE80609, written once, and the stepper
 * compares it against the +8 field of the record at OBJ_OFF_FIELD_94 -- so it
 * is a type id being matched, not a count. 0x006622BC is read at exactly one
 * site, this one, and passed straight through as an argument. */
#define ADDR_OBJ_MOVE_ALONG_FACING 0x00429040u /* void(obj, int32, int32, int32) */
/* CreateExplosion, and it was ADDR_SPAWN_AT with the note above saying "what
 * either is FOR is not established". It is established now, and by the
 * WRITER/READER PAIR this file keeps recommending rather than by either alone:
 * every field it fills already carries a BLAST_OFF_ name given by StepType6,
 * which reads them back. It hands ObjInitCommon type 6, puts
 * ADDR_EXPLOSION_ANIMS on the row it builds, and the "Duck and cover!" cheat
 * fires two hundred of them across the view. Renamed, not aliased. */
#define ADDR_CREATE_EXPLOSION      0x00422860u /* void *(x,y,kind,army,src,
                                                *        damage,delay,unused,
                                                *        uid,facing) */
/* The kinds are indices into explosions.ani, straight through to
 * SetAnimFrame, and the switch covers exactly this range -- anything outside
 * takes the default arm. */
#define AM2_EXPL_KIND_FIRST        0x78
#define AM2_EXPL_KIND_LAST         0x95
/* THE ONE PAIR. Kind 0x85 spawns a 0x86 four units BELOW itself and then sits
 * two units ABOVE where it was asked for, and the two take depth keys either
 * side of 1000 where everything else takes 10000. So the pair is drawn as one
 * effect in two layers; the rect it damages is translated by the ORIGINAL y in
 * both, not the shifted one. */
#define AM2_EXPL_KIND_PAIR_LOW     0x85
#define AM2_EXPL_KIND_PAIR_HIGH    0x86
#define AM2_EXPL_PAIR_DY           4
#define AM2_EXPL_PAIR_SELF_DY      (-2)
#define AM2_EXPL_DEPTH_DEFAULT     0x2710  /* 10000 */
#define AM2_EXPL_DEPTH_UNDER       0x3E7   /* 999 */
#define AM2_EXPL_DEPTH_OVER        0x3E9   /* 1001 */
/* The four rects, and only this function reads any of them. The first is the
 * object's own box; the other three are blast areas of radius 16, 24 and 32
 * that the arms choose between, and the default one is written before the
 * switch so an arm that picks nothing still leaves the 16. */
#define ADDR_EXPLOSION_BOX         0x004788E0u /* AM2_Rect (-24,-24,24,24) */
#define ADDR_EXPLOSION_AREA_16     0x004788F0u /* also the pre-switch default */
#define ADDR_EXPLOSION_AREA_24     0x00478900u
#define ADDR_EXPLOSION_AREA_32     0x00478910u
#define ADDR_EXPLOSION_ROW_SPEC    0x00478920u /* int32[4], (0,0,48,48) */
/* What each arm puts in BLAST_OFF_MODE. StepType6 takes the spawn path at 5 or
 * more, so 5, 7 and 9 all spawn and 0 does not -- and the three that spawn are
 * exactly the three that also arm BLAST_OFF_SOUND_PENDING. */
#define AM2_BLAST_MODE_16          5
#define AM2_BLAST_MODE_24          7
#define AM2_BLAST_MODE_32          9
#define AM2_BLAST_MODE_NONE        0
/* Kind 0x7B's arm makes its kind-7 object only where (x & 0x0B) == 1 -- one x
 * in eight, deterministic on position rather than random. Reproduced; nothing
 * else in the image tests a coordinate this way. */
#define AM2_EXPL_SCATTER_MASK      0x0Bu
#define AM2_EXPL_SCATTER_VALUE     1
/* Added to the tile's terrain attribute to make the object's height. */
#define AM2_EXPL_HEIGHT_BIAS       0x20
#define AM2_EXPLOSION_BYTES        0xBCu
/* 0x00417890, one caller, and that caller prints "Duck and cover!" one
 * instruction earlier -- so this is the cheat's effect: 200 SpawnAt calls at
 * random points across the view, six kinds, each with a random delay. */
#define ADDR_SPAWN_RANDOM_BARRAGE  0x00417890u /* void(void) */
#define AM2_BARRAGE_COUNT          0xC8   /* 200 */
#define AM2_BARRAGE_KINDS          6
#define AM2_BARRAGE_SPAN_X         0x26C  /* 620 */
#define AM2_BARRAGE_SPAN_Y         0x1E0  /* 480 */
#define AM2_BARRAGE_DELAY_MAX      0xFA0  /* 4000 */
#define AM2_BARRAGE_ARG6           0xC8
#define ADDR_WATCHED_TYPE_ID       0x00516164u /* int32_t, ships 0xE80609 */
#define ADDR_SPAWN_EXTRA_6622BC    0x006622BCu /* int32_t, one reader */
/* Bit 7 of OBJ_OFF_FLAGS. The stepper acts on it and then clears it, so it is
 * a one-shot request; what requests it is not established. */
#define OBJ_FLAG_BIT7              0x80u
/* The three periods ADDR_STEP_TYPE1_4 measures, all against ADDR_GAME_CLOCK_MS
 * -- literals in the image, named here so the use sites read. */
#define AM2_REVEAL_PERIOD_MS       0x76Cu   /* 1900 ms between reveals */
#define AM2_FRAME_PERIOD_MS        0x15Eu   /* 350 ms between frames */
#define AM2_FUSE_MS                0x2710u  /* 10000 ms, then it spawns */
#define AM2_REVEAL_NEAR            0x800    /* ADDR_REVEAL_NEARBY's two radii */
#define AM2_REVEAL_FAR             0xBB8
#define AM2_SPAWN_KIND_8B          0x8B     /* what it spawns when the fuse ends */
/* The row's own timestamp, which the frame advance is timed off rather than
 * the object's -- they are different clocks for different things. */
#define ROW_OFF_STAMP_54           0x54u    /* uint32_t */
/* The row's other stamp, four bytes on: RoachStepAllowed compares
 * ADDR_GAME_CLOCK_MS against it and refuses a turn within
 * AM2_ROACH_TURN_HOLD_MS of it, so it is when the row last turned. */
#define ROW_OFF_TURN_STAMP        0x58u    /* uint32_t, game-clock ms */
/* Reconstructed as StepType2. The trooper's per-frame step: a sound prelude,
 * the output record initialised once, the reveal expired, and then either the
 * death sequence or one of three AI arms.
 *
 * ITS OUTPUT RECORD IS AT +0x57C AND THE VEHICLE'S IS AT +0x578. Both are the
 * SIGHTCOUT layout and the SIGHTCOUT_OFF_ names are relative to whichever base
 * the caller passes -- so obj+0x580 is this one's BEARING and StepType3's
 * STATE. Read the base before reading a field. With +0x57C every write here
 * lands coherently: OBJ_OFF_FACING into the BEARING byte, WeaponPoseIndex into
 * STATE, zeros into HIT and UID.
 *
 * THE PRELUDE'S TWO SOUNDS SHARE ONE CALL SITE. The kind-7 branch pushes five
 * arguments and JUMPS to the other branch's call, so pairing pushes with the
 * nearest call gives one site five arguments and the other none. */
#define ADDR_STEP_TYPE2          0x0044B7D0u  /* void(obj) */
#define OBJ_OFF_SIGHT_OUT_T2     0x57Cu  /* StepType2's SIGHTCOUT base */
#define AM2_SND_KIND7            0x33
#define AM2_SND_FIELD5A4         0x2D
/* Two of StepType2's callees, still original and named by offset. */
/* RECONSTRUCTED as TrooperFire, and it names itself: "FIRE  trooper: %x
 * weapon: %x  ammo: %d". Given a trooper, the weapon in hand and the sight
 * record the AI has just filled in, take the shot.
 *
 * ITS 19-ARM JUMP TABLE AT 0x0044A360 HAS TWO ARMS. Codes 0x14..0x26 index a
 * byte table selecting one of two targets, so the switch FILTERS rather than
 * dispatches: seven codes leave the trooper's state alone and everything else
 * -- including every code outside the range, which the `ja` sends to the same
 * arm -- ends it. Read the table, never the arms.
 *
 * ITS ANSWER TO 0x00449AB0 IS THROWN AWAY: `mov eax, [esp+0x30]` overwrites
 * eax on the instruction after the call. */
#define ADDR_TROOPER_FIRE        0x00449FD0u  /* void(obj, weapon, sight) */
/* Called from TrooperFire with (obj, weapon, sight, ready) and its int32_t
 * answer discarded. 1,088 bytes dispatching on the weapon's ITEMTYPE_OFF_KIND
 * through a 42-entry table, so it runs for its side effects; not read. */
/* READ AND RECONSTRUCTED as SelectFirePose: which pose the trooper takes to
 * fire this weapon. It writes SIGHTCOUT_OFF_STATE and answers 1 on every path
 * past its four refusals, which is why TrooperFire discards the answer -- the
 * side effect is the function.
 *
 * FORTY-THREE INDICES AND EIGHT ARMS. Counting the bodies gives eight and
 * counting the kinds gives forty-three; only the byte table at 0x00449EC4 says
 * which goes where, and out-of-range joins the twenty-four-kind default rather
 * than getting an arm of its own.
 *
 * ONE GROUPING FALLS OUT OF THE CAPTION TABLE EXACTLY: the five kinds sharing
 * the AM2_POSE_KNEEL_ARMED_B arm are AIRS, PARA, RECO, MAG and AERO, which is
 * precisely the five ObjCodeUnmapped answers 0 for. Two tables in two
 * functions agreeing on one set of five is better evidence than either. */
#define ADDR_SELECT_FIRE_POSE    0x00449AB0u  /* int32_t(obj, wpn, out, ready) */
/* Nine poses count as BRACED -- 0x19, 0x1C, 0x13, 0x1A, 0x1D, 0x14, 0x06,
 * 0x1E, 0x15 -- and the set is tested identically in five of its arms, which
 * is what makes it a set rather than a chain of compares. Soldier kind 7
 * counts as braced whatever its pose. The list lives in item.cpp as a table;
 * naming nine poses that nothing else reads would be nine guesses. */
/* The poses it writes that had no name. The two pairs are named from the ARM
 * they belong to, which the caption table establishes -- kind 2 is GREN and
 * kinds 8, 9, 10, 29 and 30 are HvMG, RIFLE, AUTO, VULC and SNIP -- and the
 * RAISE pair from the fact that every "not braced" arm lands on those same two
 * whatever the weapon is. The last three are named for their NUMBER: nothing
 * read so far says what they are. */
#define AM2_POSE_GRENADE_STAND   0x13
#define AM2_POSE_GRENADE_KNEEL   0x14
#define AM2_POSE_RAISE_STAND     0x16
#define AM2_POSE_RAISE_KNEEL     0x17
#define AM2_POSE_GUN_STAND       0x1C
#define AM2_POSE_GUN_KNEEL       0x1D
#define AM2_POSE_CLASS2_ARMED    0x1E
#define AM2_POSE_FLAME_KNEEL     8
#define AM2_POSE_FLAME_CLASS2    9
#define AM2_POSE_CARRY           5
/* 0x00449EF0 IS ALREADY ObjCodeUnmapped and already reconstructed, and it was
 * about to get a second name here -- ADDR_WEAPON_TURNS_TO_AIM, from the one
 * thing its only caller does with it. checkpatches refused the build, which is
 * the fifth near-miss of this shape and the same argument the ratchet has
 * earned before: the rule is to grep the ADDRESS, and knowing the offsets does
 * not substitute for it.
 *
 * What TrooperFire adds is what the answer MEANS. Its table says 0 for kinds
 * 0x18, 0x19, 0x1A, 0x27 and 0x28, which the caption table at 0x00419A94 names
 * AIRS, PARA, RECO, MAG and AERO, and 1 for everything else including every
 * code out of range. The caller uses it to decide whether the trooper TURNS to
 * face the point the sight named -- so an air strike or a paradrop does not
 * swing the soldier round, and a rifle does. */
/* The sight record's weapon OVERRIDE: non-zero means fire this uid rather than
 * what the caller passed. TrooperFire's first act. */
#define SIGHTCOUT_OFF_WEAPON_UID 0x20u
/* OBJ_OFF_FIELD_530's "not a troop" value, which AM2_OPERAND_TROOP_STATE
 * already documents from the other end -- the testvar operand answers 5 for
 * anything that is not a troop. TrooperFire WRITES it, which is the writer
 * that sentence never had. */
#define AM2_TROOP_STATE_NONE     5
/* Subtracted from the target's ObjHeight to place the shot: the original does
 * `sub al, 6` on the low byte and sign-extends THAT, so a height under six
 * wraps negative rather than clamping. */
#define AM2_FIRE_HEIGHT_DROP     6
/* Cleared on a REMOTE trooper's shot, where our own broadcasts instead. The
 * one bit of WEAPON_OFF_FLAGS this path touches. */
#define AM2_WEAPON_FLAG_FIRED    0x08000000u
/* The item kinds TrooperFire names. All seven come from the caption table at
 * 0x00419A94, which is the program's own vocabulary for these numbers -- the
 * four-letter strings it draws in the HUD. */
#define AM2_ITEM_KIND_MSWP       0x14
#define AM2_ITEM_KIND_16         0x16   /* the caption table's ??? default */
#define AM2_ITEM_KIND_MEDI       0x17
#define AM2_ITEM_KIND_DISG_0     0x23
#define AM2_ITEM_KIND_DISG_1     0x24
#define AM2_ITEM_KIND_DISG_2     0x25
#define AM2_ITEM_KIND_DISG_3     0x26
#define AM2_ITEM_KIND_WREN       0x29
/* PAST THE CAPTION INDEX TABLE, which is 41 bytes and so covers kinds 0..0x28
 * only. So this one has no caption to be named from and neither, strictly, does
 * WREN above it -- that name predates this note and is left alone rather than
 * re-litigated here. Named for its value. */
#define AM2_ITEM_KIND_2A         0x2A
/* +0x04 IS THE UID, and OBJ_OFF_OWNER is a second name on it that this does
 * not resolve. TrooperFire logs the dword there as `trooper: %x` and
 * `weapon: %x`, and TrooperFireSend hands the same field to UidOnWire -- so
 * uid is what it is here. Whether objscript.cpp's "owner", which reads the
 * same offset as a pointer, is a third reading of the uid or a genuine union
 * arm has NOT been settled; the alias is recorded rather than argued. */
#define OBJ_OFF_UID              0x04u
/* IT NAMES ITSELF. "UpdateTrooperAction: asking for an item (2); oldweapon:
 * %s; maxammo: %d" is pushed from inside this function and is the only
 * reference to that string in the image, and the message opens `Name:` --
 * which is the self-naming pattern, not a message about some OTHER function
 * the way "ERROR: SetObjScriptState was called with %s" is. So the program's
 * word for 0x0044AFB0 is UpdateTrooperAction and ADDR_UPDATE_TROOPER_ACTION was ours.
 *
 * Its sibling 0x0044A420 carries no such string, so the "(2)" numbers the
 * request sites rather than the functions -- there is no "(1)" anywhere. */
#define ADDR_UPDATE_TROOPER_ACTION 0x0044AFB0u /* void(obj, weapon, out) */
/* ARGUMENT 1 IS THE WEAPON, not the int32 the old prototype guessed. It goes
 * straight through to TrooperFire, whose signature orig.h already records as
 * void(obj, weapon, sight) -- so the third argument is the sight record and
 * the second is a pointer. Settled by tools/espmap.py naming the slots rather
 * than by reading displacements. */
/* Per-CLASS, indexed by ClassifyCode74's 0, 1 or 2 and written to
 * OBJ_OFF_FIELD_64 at the very end: 28, 18, 12. */
#define ADDR_TROOPER_CLASS_VALUE  0x00489840u  /* int32[3] */
/* UpdateTrooperAction's own constants, all of them measured rather than
 * shared with a neighbour: the settle window, the arc outside which a turn is
 * enforced and the step it turns by, how long a turn is refused for, the
 * height difference that counts as standing on something, and the medic
 * tent's rate and amount. */
#define AM2_TROOPER_SETTLE_MS    0x96
#define AM2_TROOPER_TURN_ARC     0x40
#define AM2_TROOPER_TURN_STEP    0x10
#define AM2_TROOPER_TURN_MS      0x3E8
#define AM2_TROOPER_STEP_UP      0x10
#define AM2_TROOPER_HEAL         0x14
#define AM2_TROOPER_HEAL_MS      0x1F4
#define AM2_SND_MINE_FOUND       3
/* What a request asks for when the item type declares no capacity at all. */
#define AM2_TROOPER_NO_CAPACITY  0x3E7
#define AM2_STR_ASKING_FOR_ITEM  0x0048A534u
#define AM2_STR_YES              0x0048A5B0u
#define AM2_STR_NO               0x0048A5ACu
/* Read with the state as its index by the row-animation test at the head of
 * UpdateTrooperAction -- the same table ADDR_WEAPON_POSE_FRAMES names, reached
 * for a different purpose. Named once; this is a note, not a second name. */
/* SURVEYED AND NOT RECONSTRUCTED. 2,080 bytes, 632 instructions, four callers
 * -- all four inside StepType2, which reaches it as a TAIL rather than as one
 * arm of several.
 *
 * IT IS LIVE, AND IT IS THE ONLY THING LEFT THAT AN A/B CAN DISCRIMINATE ON.
 * StepType2 runs per type-2 object per frame and region.cpp already records
 * what happens when this call is skipped: a dropped weapon stays at 0,0 where
 * the original leaves it at the trooper's feet, caught by `bootcamp`'s object
 * dump as ONE LINE with the pixels and the log identical on both sides. So
 * unlike every AI function reconstructed before it, a mistake here is visible
 * -- and visible exactly, with no budget.
 *
 * ALL TWENTY-EIGHT CALLEES ARE NAMED, and its frame is clean under
 * tools/espmap.py once that tool learned about thiscall cleanup -- which it
 * learned FROM this function, whose `push ecx; mov ecx, ADDR_COMM_OBJECT;
 * call` produced all ten of the disagreements it first reported.
 *
 * WHAT THE HEAD DOES. Classify the object; a dead one goes straight to the
 * tail. Outside a multiplayer session -- or inside one, for an army the comm
 * object does not own -- it asks how long since OBJ_OFF_DEADLINE_D0 and
 * remembers OBJ_OFF_FACING in OBJ_OFF_FIELD_580 when that is under 0x96
 * milliseconds.
 *
 * AND THIS FUNCTION IS ONE OF THAT FIELD'S FOURTEEN WRITERS: it stamps the
 * clock there whenever the facing it settles on differs from the one the
 * object had. So the head's test is "has this trooper been pointing the same
 * way for a sixth of a second", a TURN SETTLE TIME -- not "was it hit
 * recently", which this block claimed on a first reading and which belongs to
 * OBJ_OFF_HIT_TIME at 0x108. A field with fourteen writers does not get its
 * meaning from one of them, and the correction cost one decoded scan.
 *
 * FOUR OF ITS FIELDS ALREADY HAD NAMES AND ALL FOUR CONFIRM THE READING.
 * 0x65 is OBJ_OFF_HEIGHT_SET, which is what makes the step-up below a height
 * comparison rather than a coincidence; 0x64 is OBJ_OFF_HEIGHT_ADJ, so
 * ADDR_TROOPER_CLASS_VALUE's 28/18/12 are per-class height adjustments; 0x564
 * is OBJ_OFF_HELD_WEAPON_UID, so the clear at the end of the walk means
 * "nothing was underfoot, forget what you were holding"; and 0x538 is
 * OBJ_OFF_POSE. Grepping the OFFSETS rather than inventing OBJ_OFF_FIELD_65
 * and friends turned four blanks into four sentences.
 *
 * AND ONE OF THEM CONTRADICTS ITS NAME. 0xD8 is OBJ_OFF_STUCK_COUNT, "int32_t,
 * refused moves in a row" -- a reading taken from AiRouteToward, which only
 * tests it for non-zero. This function writes ADDR_GAME_CLOCK_MS + 1000 into
 * it and compares it against the clock, which is a DEADLINE and not a count.
 * Both uses are live and a boolean test cannot tell them apart. Recorded
 * rather than renamed: the rename wants AiRouteToward re-read first, and this
 * file has enough one-consumer names in it already without adding a
 * one-consumer correction.
 *
 * A TROOPER STEPS UP ONTO WHAT IT IS STANDING ON, and the whole behaviour is
 * one stack slot used twice. The slot is zeroed at entry and again when the
 * facing sweep gives up; in the pickup walk it takes the HEIGHT
 * (OBJ_OFF_FIELD_65) of any item underfoot whose height is within 0x10 of the
 * trooper's own; and at the very end it is the value handed to
 * ObjMoveAlongFacing -- but only when the trooper's own height already matches
 * the tile attribute under it.
 *
 * Read as two locals -- a "move speed" and an "item height" -- the connection
 * disappears and the move gets a constant zero. tools/espmap.py is what says
 * they are one slot; the displacements are 0x1c and 0x20 at the two sites and
 * differ only because of an outstanding push.
 *
 * ITS TAIL SWITCHES ON A RELOADED FIELD, NOT ON A RETURN VALUE. StepObjRows
 * answers nothing; the three-way test after it reads the ROW's +0x4C -- the
 * animation frame -- out of OBJ_OFF_ROWS, which the call has just advanced.
 * Reading `eax` there would give the switch a value StepObjRows never sets,
 * and it is exactly the shape SelectFirePose's note warns about: ask what a
 * function's output actually IS before comparing anything.
 *
 * IT ALSO KEEPS A TURN ACCUMULATOR. OBJ_OFF_FIELD_D6 takes the AngleDelta of
 * every enforced turn and is wrapped into +/-0x100 by two SEPARATE tests, the
 * second re-reading the field after the first may have changed it. Beside it
 * OBJ_OFF_FIELD_D8 is stamped at clock + 1000, and the sweep refuses to
 * enforce a turn until that has passed -- so a trooper can be pushed around
 * once a second and only so far. */

/* WHEN THE WAY AHEAD IS BLOCKED IT SWEEPS ALTERNATE FACINGS, and the sweep is
 * a TABLE rather than arithmetic. Each attempt calls AnimStepPoint for the
 * candidate heading, ObjectsAtPoint for what is there, and BlockWeightRoute
 * for what it would cost; anything under 15 is walkable and ends the search.
 *
 * The headings come from ADDR_STEP_FACING_SWEEP, whose entries are added
 * CUMULATIVELY to the base facing: +32, then -64, then +96, so the facings
 * actually tried are base+32, base-32, base+64 -- alternating and widening.
 * Read as absolute offsets rather than deltas it is a different and much
 * narrower search.
 *
 * HOW MANY IT TRIES DEPENDS ON WHOSE TROOPER IT IS: four when the object
 * belongs to ADDR_DEFAULT_OWNER and OBJ_OFF_FIELD_10C is clear, seven
 * otherwise. So the player's own soldier gives up sooner than everything else
 * on the map. */
#define ADDR_STEP_FACING_SWEEP   0x00489E00u  /* int32[], low byte used */
#define AM2_STEP_SWEEP_PLAYER    4
#define AM2_STEP_SWEEP_OTHER     7
/* Under this a route is walkable and the sweep stops. */
#define AM2_STEP_ROUTE_OK        0xF
/* The two StepType2 runs INSTEAD of the AI arms when the object is Sarge and
 * belongs to the default owner -- the trooper the player commands. Named by
 * the function they are called from and their offset: "player control" is read
 * off that gate, not off their bodies, and neither has been read. */
#define ADDR_STEP2_44A420        0x0044A420u  /* void(obj, weapon, out) */
/* SURVEYED, and it is the most favourable of the four left. 2,336 bytes with
 * TWO exits, and all NINETEEN distinct callees already named -- the key
 * queries, the overlay row pair, the trig and distance leaves, the tile and
 * height helpers. Nothing has to be read before it.
 *
 * ITS SWITCH IS A FILTER, NOT A DISPATCH, and dumping it was three lines
 * where reading the arms would have been three hundred. `jmp [ecx*4 +
 * 0x0044AD1C]` looks like a seventeen-way jump on the weapon code; the byte
 * index at 0x0044AD24 covers codes 0x18..0x28 and holds only 0 and 1. Five
 * codes -- 0x18, 0x19, 0x1A, 0x27, 0x28 -- reach 0x0044AAB9; the other
 * twelve reach 0x0044ACDB, which is also where the `ja` sends every code
 * outside the range.
 *
 * BOTH ARMS ARE TINY AND THE SURVEY SAID ONE WAS 546 BYTES. That figure was
 * the SPAN from the arm's entry to the next label, and the span is full of
 * blocks the arm never reaches -- the GetMenuRow paths, which arrive from a
 * branch four hundred bytes earlier. Following the arm instead: 0x0044AAB9
 * writes the cursor's world point into the output and jumps to 0x0044ACDB,
 * which is the COMMON TAIL every path reaches, so it is 35 bytes and a
 * fallthrough. The filter's whole effect is "for these five codes, also
 * report where the cursor is pointing".
 *
 * Measuring an arm by the distance to the next thing that looks like a label
 * is the same error as counting epilogues to count arms, which this file
 * records one function earlier. Follow the branch. Same shape as
 * TrooperFire's nineteen-index two-arm table, and the second instance is what
 * makes it a pattern in this image rather than a curiosity.
 *
 * AND THE TWO `call ebp` ARE GetTickCount. `mov ebp, [0x0046F084]` is an IAT
 * slot, which reads as a function-pointer field until the address is looked
 * up -- and this file already records air.cpp calling that same slot directly
 * SO THAT IT CAN STAY FLAT. A clock read names no Win32 type and CLAUDE.md
 * counts it incidental, so this belongs on the flat side too despite calling
 * an import. Worth checking before filing a module: an indirect call is not
 * evidence of a callback and an import is not evidence of boundary work. */
/* READ NOW, and renamed off its body rather than off the gate that reaches
 * it. It walks the player's trooper toward the point at OBJ_OFF_FIELD_C0,
 * boards a vehicle when one is claimed and close enough, picks the pose the
 * held weapon wants, and then drags the REST OF THE SELECTION along -- which
 * is the part the old offset-name could not hint at. Reconstructed. */
#define ADDR_TYPE2_PLAYER_STEP   0x0044AD40u  /* void(obj, out) */
/* Its two boarding thresholds. Under NEAR it always boards; between NEAR and
 * FAR only a vehicle whose OBJ_OFF_TABLE_REC_KIND is 5, which is the one kind
 * that will be walked to from further off. */
#define AM2_BOARD_NEAR           0x40
#define AM2_BOARD_FAR            0x5A
/* NOT AM2_AI_REACHED_DIST, which is 0xC and is the OTHER arm's threshold in
 * this same function: the arm that is already walking stops at 0xC, the arm
 * that is not starts only past 8. Two numbers, two arms, and reaching for the
 * existing name would have been off by four. */
#define AM2_WALK_START_DIST      8
/* How often the walker re-aims. The same 200 ms AM2_FLAME_PERIOD_MS and
 * AM2_FIELD_530_DELAY_MS use elsewhere; a third structure, a third name. */
#define AM2_WALK_TURN_MS         0xC8
/* The weapon code that takes the second pose. Everything else takes 2. */
#define AM2_POSE_WEAPON_CODE     0x14
#define AM2_POSE_INDEX_SPECIAL   0x26
#define AM2_POSE_INDEX_DEFAULT   2
/* How long after the last mouse press the view stops being snapped. */
#define AM2_VIEW_SNAP_MS         0x1F4
/* +0x04 ALREADY HAD A NAME AND THE RATCHET SAID SO. Type2PlayerStep seeds
 * ClassifyByCode74's answer there before handing the context to the walker,
 * and that answer is 0, 1 or 2 -- a class code, not a pointer -- where
 * SIGHTC_OFF_LEADER is `obj *, from OBJ_OFF_FOLLOW_UID`. Both readings are of
 * live code. Either the field is a fourth type-dependent one or the leader
 * name is a call-site name; nothing here settles it, so this is RECORDED and
 * the existing name is what the reconstruction writes through. Same trade as
 * OBJ_OFF_FORMATION_SLOT's, and the check refused the alias before it could
 * be committed -- which is the argument for the ratchet, again. */
/* Reconstructed as StepType3. The vehicle's per-frame step, and the mirror of
 * StepType2: the same reveal-expiry prologue, then either a death sequence or
 * the AI, then two converging tails.
 *
 * ITS OUTPUT RECORD IS AT +0x578 WHERE StepType2's IS AT +0x57C. Both are the
 * SIGHTCOUT layout, so obj+0x580 is this one's STATE and that one's BEARING.
 * Read the base before reading a field.
 *
 * NOT ONE OF ITS TWENTY JUMP TARGETS IS AN EPILOGUE -- checked by decoding the
 * first instruction at each. Every conditional jump goes to more code, and the
 * function has TWO sequential converging tails: ADDR_STEP3_45C8D0 falls
 * through into a second block that ends at ADDR_STEP3_45CB30. Writing any of
 * those jumps as a `return` is the defect StepType2 carried three times.
 *
 * ITS DEATH TABLE IS THE JUMP-TABLE TRAP AT ITS WORST. Six indices on
 * OBJ_OFF_TABLE_REC_KIND, five distinct arms (2 and 3 share), and the sound
 * constants run 0x1F, 0x20, 0x21, 0x22 IN LAYOUT ORDER -- which looks like
 * confirmation the arms are in index order. They are 1, 0, {2,3}, 5. Take the
 * order from the table at 0x0045D954, never from the bodies. */
#define ADDR_STEP_TYPE3          0x0045D660u  /* void(obj) */
#define ADDR_STEP3_JUMP_TABLE    0x0045D954u  /* six entries, five arms */
/* Two callees, still original and named by offset: their bodies are unread and
 * every path through StepType3 reaches both. */
#define ADDR_STEP3_45C8D0        0x0045C8D0u  /* void(obj, out) */
#define ADDR_STEP3_45CB30        0x0045CB30u  /* void(obj, out) */
#define OBJ_OFF_FIELD_59C        0x59Cu  /* gates the record init, once */
/* The destruction sounds, one per vehicle kind. Kind 4 makes none -- it goes
 * straight to the row finish and DestroyByType. */
#define AM2_SND_VEH_KIND0        0x20
#define AM2_SND_VEH_KIND1        0x1F
#define AM2_SND_VEH_KIND23       0x21
#define AM2_SND_VEH_KIND5        0x22
#define AM2_VEH_TURN_LIMIT       30     /* outside +/-30 puts it in state 4 */
#define AM2_VEH_STATE_TURNING    4
#define ADDR_STEP_TYPE5          0x0043C110u  /* void(obj) */
#define ADDR_STEP_TYPE6          0x00422B90u  /* void(obj) */
#define ADDR_OBJ_MARK_IF_OVERDUE 0x004355D0u /* void(void *obj) -- type 7 */
/* Reconstructed. The roach's per-frame step, and the five functions below are
 * everything it calls that had no name. They are given ROLE names from where
 * they sit in this one function, which is the weakest kind of naming and is
 * said so plainly: two run only while the roach is alive, two run on EVERY
 * path including the dead ones, and one runs once as a dead roach is finally
 * destroyed. Nothing here reads their bodies. */
#define ADDR_STEP_TYPE8          0x0043D980u  /* void(obj) */
/* NO LONGER A ROLE NAME, and the block above predicted it would need reading:
 * "they are given ROLE names from where they sit in this one function, which
 * is the weakest kind of naming ... Nothing here reads their bodies."
 *
 * Read now, and it is exactly AiStep's shape one type over: set one field of
 * the output record, build the roach's 0x40-byte sight context, run the
 * behaviour that consumes it, and record which region the roach is standing
 * in. Its `sub esp, 0x40` is what fixes RoachBuildContext's record length.
 *
 * The second argument is the OUTPUT RECORD, not a facing. The name here came
 * from a caller; ADDR_AI_408640 takes it as `out` alongside the context, and
 * 0x0045D660 initialises the same record inside the object at
 * OBJ_OFF_FIELD_578, where +0x04, +0x08, +0x0C, +0x10 and +0x14 land exactly
 * on the SIGHTCOUT_OFF_ names. Byte 1 of it is the heading, which is what
 * "facing" was seeing. Reconstructed. */
#define ADDR_ROACH_ALIVE_STEP_A  0x00408A60u  /* void(obj, void *out) */
/* The roach's behaviour, still original. It takes (obj, out, ctx), reads the
 * context RoachBuildContext filled -- SIGHT_OFF_RANGE against the record's
 * +0x34 in its first two instructions -- and writes back the object's
 * destination, its follow uid and its target uid. Named by offset because its
 * body has not been read. */
/* Reconstructed as RoachBehaviour. The roach's decision half, the third of
 * the band's step/build/behave triples, and the one that shows what the SIGHT
 * record is FOR.
 *
 * THE RECORD HOLDS {object, range, bearing} THREE TIMES -- leader at +0x00,
 * observer at +0x10, found at +0x1C -- and the behaviour PROMOTES one triple
 * into the observer slot depending on what the unit decides to engage. This
 * function does it FOUR times: the found triple in its near, far and no-leader
 * arms, and the leader triple in the arm that follows a leader. That is why
 * ConsiderSighting reads observer/range/bearing and nothing else -- the
 * promotion has already chosen for it.
 *
 * orig.h already records AiStepDefend inlining the same block twice and keeps
 * both, because "the original does it twice" is a fact about the original.
 * Four here, written out four times for the same reason.
 *
 * Its 0x818-byte frame is a TILE LINE buffer: it traces line of sight with
 * ADDR_TRACE_TILE_LINE. Its opening test compares SIGHT_OFF_RANGE against
 * ROACHCTX_OFF_WANT_RANGE -- "is my target further than the distance I want to
 * be at" -- which splits the far arm from the near one.
 *
 * 26 jump targets and not one is an epilogue: a single exit, like the rest of
 * the band. */
#define ADDR_ROACH_BEHAVIOUR     0x00408640u  /* void(obj, out, ctx) */
/* NOT A "REACHABILITY HELPER" -- it is ADDR_AI_ROUTE_TOWARD's TWIN, 518 of
 * 784 bytes byte-identical, and the old note described its first six
 * instructions. Same five stages, same region routing, same waypoint cursor.
 * All three call sites are ADDR_ROACH_BEHAVIOUR, so this is the roach's copy;
 * reconstructed as RoachRouteToward. Four differences and no more:
 *
 *   a cursor that has run out RESETS the path to a zero-length one rather
 *   than heading straight off, and that seeder is the one of the three that
 *   does NOT write OBJ_OFF_ROUTE_GOAL;
 *   it arrives at 0x18 where the trooper arrives at AM2_AI_ARRIVED_DIST;
 *   it reports through out+0x14 -- 1 arrived, 2 heading -- where the trooper
 *   uses out+8 for a pose and out+0x14 only as a slow-down flag;
 *   and it has no fourth argument, no second copy of the bearing, and no
 *   approach grading at all.
 *
 * Reading the DIFFERENCES rather than the function is what made it an hour's
 * work; the diff of the two disassemblies is 21 lines. */
#define ADDR_ROACH_ROUTE_TOWARD  0x00408210u  /* void(obj, out, ctx) */
#define AM2_ROACH_ARRIVED_DIST   0x18
/* 0x00408060, one caller -- ADDR_ROACH_ALIVE_STEP_A, whose `sub esp, 0x40` is
 * this record's LENGTH. The roach's half of the sight-context idea: the same
 * structure ADDR_AI_BUILD_CONTEXT fills for a vehicle, four bytes shorter.
 *
 * THE TWO RECORDS DIVERGE AT 0x34, NOT AT 0x40, and that is worth stating
 * because the lengths suggest otherwise. The vehicle record carries a weapon
 * KIND at 0x34 and this one does not, so everything after shifts down a dword:
 *
 *     vehicle   0x30 weapon  0x34 kind  0x38 want  0x3C max   0x40 ready
 *     roach     0x30 weapon  ----       0x34 want  0x38 max   0x3C ready
 *
 * ESTABLISHED FOUR WAYS, none of them a guess. The two callers reserve 0x44
 * and 0x40; the two builders `rep stos` 0x11 and 0x10 dwords; only the vehicle
 * one writes +0x40; and this one's constants are 48 and 70, which are the
 * vehicle builder's own 0.75 and 1.1 over a default range of 64 -- 48/0.75 is
 * exactly 64 and 64*1.1 truncates to 70. A consumer agrees independently:
 * 0x00408640 compares SIGHT_OFF_RANGE against this record's +0x34, and a
 * comparison against a range is what a range field is for.
 *
 * IT DIFFERS FROM ITS TWIN IN THREE WAYS, not one. It measures from the raw
 * OBJ_OFF_POS where the vehicle builder measures from ObjAnchorPoint; it
 * resolves a FORMATION point when the leader is a type 2, 3 or 8 where that
 * one copies the leader's position; and it writes these fixed ranges where
 * that one reads the weapon.
 *
 * IT CARRIES THE ORIGINAL'S OWN ASSERTION. After DistAndAngle gives it the
 * target's bearing it recomputes the same bearing with AngleBetween and logs
 * "Bad!" if they disagree. Dead in practice, and reproduced.
 *
 * Reconstructed as RoachBuildContext. */
#define ADDR_ROACH_BUILD_CONTEXT 0x00408060u  /* void(obj, ctx *) */
#define AM2_ROACH_CONTEXT_BYTES  0x40
/* The three tail fields, one dword below the vehicle record's. A prefix of
 * their own because they are a different structure -- the same reason
 * SIGHTC_OFF_ has one -- and NOT the AICTX_ mistake, which gave new names to
 * the SAME record's offsets. The head, 0x00 to 0x2C, is shared and keeps the
 * SIGHT_OFF_ names. */
#define ROACHCTX_OFF_WANT_RANGE  0x34u
#define ROACHCTX_OFF_MAX_RANGE   0x38u
#define ROACHCTX_OFF_READY       0x3Cu
#define AM2_ROACH_WANT_RANGE     48    /* 64 * ADDR_SIGHT_RANGE_WANT */
#define AM2_ROACH_MAX_RANGE      70    /* 64 * ADDR_WEAPON_RANGE_HI */
#define AM2_ROACH_READY_MS       0x2EE /* 750, against OBJ_OFF_DEADLINE_58 */
#define ADDR_STR_BAD             0x00473F18u  /* "Bad!\n" */
#define ADDR_ROACH_ALIVE_STEP_B  0x0043D5B0u  /* void(obj, uint8_t *facing) */
#define ADDR_ROACH_STEP_TAIL_A   0x0043D750u  /* void(obj, uint8_t *facing) */
/* NOT a "step tail", and the note above predicted this: the five names in this
 * block are role names taken from one call site, "the weakest kind of naming",
 * and nothing had read their bodies. 0x0043C8D0 is the exact partner of
 * ADDR_OBJ_CLEAR_ROACH_FOOTPRINT -- eighty-seven instructions each, differing
 * in four places: the flag gate inverted, +15 instead of -15,
 * ADDR_TILE_COVER_ADD instead of _SUB, and the flag set instead of cleared.
 * Renamed. Two of the other four in this block are still role names. */
#define ADDR_OBJ_SET_ROACH_FOOTPRINT 0x0043C8D0u  /* void(obj) */
#define ADDR_ROACH_ROW_FINAL     0x00461EA0u  /* void(row), before the destroy */
#define AM2_ROACH_ALIVE_SOUND    0x30
#define OBJ_OFF_OWNER            0x04u   /* what a frame's actions are run against */
#define ADDR_SET_OBJ_SCRIPT_STATE  0x004372A0u
#define ADDR_DEF_PARSE_INFO_FILE   0x0041A5F0u
/* NOT DefGameParse -- 0x00424590 is docs/functions.tsv's merged 784-byte
 * entry, and the handler with the "DefGameParse:" string starts at 0x00424780.
 * Same mistake, same cause, as ADDR_DEF_LINK_PARSE two commits ago.
 *
 * AND THE NAME IT GOT INSTEAD RECORDED A FACT ABOUT functions.tsv RATHER THAN
 * ABOUT THE FUNCTION. It went in as ADDR_DEF_GAME_ENTRY, which says only
 * "the merged entry DefGameParse is somewhere inside". The function at that
 * address is the PACKED half of LoadMask: PackKey on the three arguments,
 * SpriteSetForKey on the result, then the archive lookup -- and its one
 * caller is LoadMask's non-`-df` branch. Renamed, not aliased; nothing
 * referenced the old name. */
#define ADDR_LOAD_MASK_PACKED      0x00424590u  /* void(out, set, idx, frame) */
/* 0x00435280, two callers. Fill a mask record from the packed archive, or --
 * under `-df` -- by globbing `<map>\<set>\masks\%02d_%03d_%02d_*.msk` for a
 * loose file. Reconstructed. */
#define ADDR_LOAD_MASK             0x00435280u  /* void(out, set, idx, frame) */
#define ADDR_STR_FMT_MASK_DIR      0x00487448u  /* "%s\\%s\\masks" */
#define ADDR_STR_GLOB_MSK          0x00487430u  /* "%02d_%03d_%02d_*.msk" */
#define AM2_SPRITE_SET_MAP_FIRST   0x14  /* below this LoadMask's -df arm gives up */
/* The record both halves fill. Only the loose arm is read here, so the two
 * words are named for WHERE THEY COME FROM rather than for what they mean:
 * the DIB descriptor's +0x04 and its DIB_OFF_BLOCKS. */
#define MASKREC_OFF_ZERO_A         0x00u  /* written 0 */
#define MASKREC_OFF_ZERO_B         0x02u  /* written 0 */
#define MASKREC_OFF_DESC4          0x04u  /* uint16, from the descriptor's +4 */
#define MASKREC_OFF_DESC_BLOCKS    0x06u  /* uint16, from DIB_OFF_BLOCKS */
#define MASKREC_OFF_LOADER_OUT     0x08u  /* uint16, LoadDibFlipped writes it */
#define MASKREC_OFF_BITS           0x0Cu  /* void *, the flipped pixels */
/* The three stack buffers LoadMask's loose arm uses. The frame is 0x5E0 and
 * the two paths through it are a 0x50-byte directory, a 0x50-byte pattern, a
 * _finddata_t and a DIB descriptor; the sizes are what the offsets between
 * them allow rather than what any field says. */
#define AM2_MASK_PATH_MAX          0x50u
#define AM2_FINDDATA_BYTES         0x118u
#define AM2_DIB_DESC_BYTES         0x1Cu
#define ADDR_DEF_OBJ_PARSE         0x00435B60u
/* docs/functions.tsv merges THREE things into the 768-byte entry here:
 * DefObjParse itself (0x00435B60, a jump table over sixteen tokens), the table
 * at 0x00435BD8, and a separate OBJ-line parser at 0x00435C20 that zeroes a
 * 56-byte record and calls DefObjParse to fill its first field.
 *
 * That matters for naming. DefObjParse's own default arm is `or eax,-1; ret`
 * and logs NOTHING; the string "DefObjParse: Bad object Constant Type" is at
 * 0x00435C4C, inside the CALLER. So the self-naming sweep attributed the name
 * through a merge, and was right only by luck -- the caller happens to name
 * the callee it just complained about. */
#define ADDR_DEF_OBJ_LINE          0x00435C20u  /* the OBJ-line parser */
/* Its sink, analogous to DefAddLink: "duplicate record in object.aai file
 * %d-%d". Still original. */
#define ADDR_DEF_ADD_OBJ_REC       0x00435980u  /* void(const int32_t *rec) */
#define AM2_DEF_OBJ_REC_DWORDS     14
#define ADDR_DEF_OBJ_REC_CAP       0x00516178u
/* Frees BOTH def tables and zeroes all six globals. Role name; three callers.
 * The counts and capacities go to zero before the frees, and the pointers
 * after, so a caller that faults inside free() still sees consistent zeros. */
#define ADDR_DEF_FREE_TABLES       0x00435E60u  /* void(void) */

/* The object-definition files (.aai) have a command vocabulary of their OWN --
 * it is not docs/scripttokens.md's. 0x5F is LINK there; in the script table 95
 * is `sniper`, which is what makes conflating the two easy and wrong. */
#define ADDR_DEF_LINK_PARSE        0x004360C0u  /* int32_t(int32_t, char *) */
#define AM2_DEF_CMD_LINK           0x5F
/* Name -> index over a table of 12-byte entries at 0x00476FE0 {name, value,
 * ptr}, -1 when absent; DefObjParse is handed the index. Role name: it names
 * itself nowhere. */
#define ADDR_DEF_NAME_INDEX        0x0041A640u  /* int32_t(const char *) */
#define ADDR_DEF_NAME_TABLE        0x00476FE0u
/* Twelve bytes an entry: {const char *name, int32_t value, void *handler}.
 * The handler is passed the entry's VALUE, not its index. Those coincide over
 * the range that matters -- entries 77..96 all have value == index -- but not
 * generally: entry 1 is "trooperlevel1" with value 45. An earlier note here
 * said "the index IS the command id", which was true where it was looked at
 * and wrong as a rule.
 *
 * Reading the table by name settles the vocabulary outright. Entries 79..94
 * are rocks, bush, trees, ground, fence, wall, bridge, barrel, building,
 * pillbox, aagun, tent, garage, radar, miscellaneous, powerups -- exactly
 * DefObjParse's sixteen tokens -- and entry 95 is literally "link". An entry
 * with an empty name ends the table. */
#define AM2_DEF_KEYWORD_STRIDE     12
#define ADDR_CRT_STRTOL            0x00465198u
#define ADDR_CRT_STRTOD            0x004653B7u
/* 0x0041A290. DefParseNumber's FLOAT twin, sixty-four bytes further on: same
 * null check, same "consumed something" test, same complaint -- strtod and a
 * float store instead of strtol and an int32. */
#define ADDR_DEF_PARSE_FLOAT       0x0041A290u  /* int32_t(float *, const char *) */
/* 0x0041A5F0 is DefParseInfoFile, by its own string; 0x0041A6B0 is the line
 * dispatcher it drives, and that one names itself nowhere. Both still
 * original. */
#define ADDR_DEF_DISPATCH_LINE     0x0041A6B0u  /* int32_t(FILE *) */
#define ADDR_CRT_FGETS             0x004655B8u
/* The ONE place the game's FILE is not opaque. 0x00430140 tests the MSVC 6
 * `_flag` field for _IOEOF inline rather than calling feof, and reproducing
 * the loop means reading the same byte -- so the two offsets are here with
 * the reason, rather than a struct that pretends to be an _iobuf. */
#define AM2_FILE_OFF_FLAG          0x0Cu
#define AM2_FILE_EOF               0x10u
#define ADDR_CRT_STRLWR           0x0046D7D6u  /* the plain ASCII _strlwr */
#define ADDR_STR_DEF_FILE_MODE     0x004779F4u  /* "rt" */
#define AM2_DEF_LINE_MAX           0x140        /* both line buffers */

/* DefGameParse, by its own "DefGameParse: Bad Game Constant Type". It is the
 * handler for twenty .aai keywords, and docs/functions.tsv merges it into the
 * 784-byte entry at 0x00424590 -- the fourth merged entry found in this work.
 *
 * Twelve of the twenty arms share ONE target, `xor eax,eax; ret`:
 * vehicle_danger, vehicle_standoff, trooper_turn_rate, trooper_pose_rate,
 * trooper_slide_rate, defense_radius, attack_radius, attack_hunt,
 * follow_radius, follow_engaged_radius, gravity and scroll_speed are parsed
 * and then DISCARDED. Only the eight roach_* constants are stored, into the
 * eight consecutive dwords at 0x00487BA8. The keywords still parse, so the
 * files are still valid; the values simply do nothing in this build. */
#define ADDR_DEF_GAME_PARSE        0x00424780u  /* int32_t(int32_t, char *) */
#define ADDR_GAME_CONSTANTS        0x00487BA8u  /* the eight roach_* dwords */
#define AM2_DEF_CMD_GAME_FIRST     0x3B
#define AM2_DEF_CMD_ROACH_FIRST    0x47
#define AM2_DEF_CMD_GAME_LAST      0x4E
/* strtol into *out, 0 on failure. Its one string is "Bad or missing number",
 * which names the condition and not the function -- 48 callers. */
#define ADDR_DEF_PARSE_NUMBER      0x0041A250u  /* int32_t(int32_t *, const char *) */
#define ADDR_DEF_SEPARATORS        0x00477A4Cu  /* " \t\n;," */
/* The link table: 20-byte records, count first. CountLinksWithParent walks it
 * comparing the parent key at +0, which is how the record's first field is
 * known to be that key. */
#define ADDR_DEF_LINKS             0x0051617Cu
#define ADDR_DEF_LINK_COUNT        0x00516180u
#define ADDR_DEF_COUNT_LINKS       0x00435FA0u  /* int32_t(int32_t parentkey) */
/* Appends one link, refusing a duplicate. Role name -- its only string is
 * "duplicate link record in object.aai file". */
#define ADDR_DEF_ADD_LINK          0x00435EE0u  /* void(const AM2_DefLink *) */
/* 0x00435FD0 names itself nowhere; this is a ROLE. After every LINK line is
 * parsed it sorts the table with ComparePair (already ours) through the CRT's
 * qsort, then walks the distinct parent keys: each is unpacked with KeyFieldA
 * and KeyFieldB -- confirming from the far side that DefLinkParse packs
 * (type, number) -- and either has its link count stored into the AAI record
 * or produces "Object AAI record not found for link %02d-%-3d". */
#define ADDR_DEF_CHECK_LINKS       0x00435FD0u  /* void(void) */
#define ADDR_DEF_FIND_OBJ_REC      0x00435AC0u  /* void *(int32,int32,int32) */
/* The object records the .aai files define: 56 bytes each, sorted, and keyed
 * on their first three dwords -- which is what CompareTriple compares and why
 * the lookup builds a 56-byte partial record as its search key. */
#define ADDR_DEF_OBJ_RECS          0x00516170u
#define ADDR_DEF_OBJ_REC_COUNT     0x00516174u
#define AM2_DEF_OBJ_REC_SIZE       0x38u
/* A THIRD def table, in the same shape as the two above and parsed with the
 * same machinery -- ADDR_DEF_SEPARATORS and ADDR_DEF_PARSE_NUMBER -- by
 * 0x0044CD70, whose rejection message is "Bad Trooper Type". Its records are
 * 32 bytes, keyed on their FIRST dword alone, which is what
 * ADDR_COMPARE_DWORD compares and what 0x0044CD70 bsearches with before it
 * appends. That parser writes small integers (6, 7, ...) into that first
 * dword, so the key is the trooper type its own message names.
 *
 * Fifty records to begin with -- 0x640 is 50 * 32 -- then twenty more at a
 * time, which is AM2_DEF_LINK_INITIAL and AM2_DEF_LINK_GROW exactly. The
 * three globals are data, count, capacity, the same order as
 * ADDR_DEF_OBJ_RECS and NOT the order the uid remap table uses. Two halves
 * reconstructed and both run at startup -- 8 appends and 1 free on a Boot Camp
 * drive, so the A/B compares them; see below for the third. */
#define ADDR_DEF_TROOPER_RECS      0x00659F4Cu
#define ADDR_DEF_TROOPER_COUNT     0x00659F50u
#define ADDR_DEF_TROOPER_CAP       0x00659F54u
#define AM2_DEF_TROOPER_REC_SIZE   0x20u
#define ADDR_DEF_ADD_TROOPER_REC   0x0044CCC0u  /* void(const void *rec) */
#define ADDR_DEF_FREE_TROOPER_RECS 0x0044CF70u  /* void(void) */

/* ---- Six small teardown steps -------------------------------------------
 *
 * Four of these are called from the level teardown at 0x00425300 and are the
 * shape ShutdownSubsystems already showed: a step that frees one thing and
 * then TAIL-JUMPS to the logger with no arguments, so the log line is the
 * game's own and the step has no name of its own. Reconstructed together
 * because each is under thirty-two bytes and none is worth a commit.
 *
 * ADDR_ZERO_50C34C IS WRITE-ONLY. Its global has exactly ONE reference in the
 * whole image -- this store -- by a decoded scan and by a raw dword scan
 * both. So the game zeroes it on every teardown and never reads it. Named for
 * the address because there is nothing else to name it for. */
#define ADDR_ZERO_50C34C         0x0041E740u  /* void(void) */
#define ADDR_UNREAD_50C34C       0x0050C34Cu  /* int32_t, written here only */
/* `if (arg->[0x20]'s first dword == 0x31) comm[0x3E0] = 1`, and answers 0
 * either way -- its caller ignores the answer. */
#define ADDR_NOTE_KIND_31        0x00402670u  /* int32_t(void *) */
#define COMM_OFF_SAW_KIND_31     0x3E0u
#define AM2_KIND_31              0x31
/* A three-argument pass-through to 0x00405220, which is 1,424 bytes and has
 * two other callers of its own. The wrapper exists so two call sites can
 * reach it without repeating the argument list; nothing is added. */
#define ADDR_CALL_405220         0x004057B0u  /* void(int32, int32, int32) */
#define ADDR_BIG_405220          0x00405220u  /* void(int32, int32, int32) */
/* Free one thing, then log -- and that one thing is the sprite registry, so
 * the placeholder ADDR_FREE_445F40 this used to sit beside has a real name
 * now: ADDR_FREE_SPRITE_REGISTRY, up with the rest of the sprite lifetime.
 * The teardown's own name stays a literal; it is a wrapper and 0x00445FE0 is
 * all that is known about it. */
#define ADDR_TEARDOWN_445F40     0x00445FE0u  /* void(void) */
/* Free four, then log. Two of the four are already named. */
#define ADDR_TEARDOWN_DEF_TABLES 0x004033E0u  /* void(void) */
#define ADDR_FREE_LIST_662024    0x0045EDF0u  /* void(void), two callers */
#define ADDR_FREE_LIST_662928    0x004607D0u  /* void(void), two callers */
/* Two steps, the second a tail jump. Both halves are inside one merged
 * functions.tsv entry, which is why the second has no entry of its own. */
#define ADDR_TEARDOWN_40A4B0     0x0040A690u  /* void(void) */

/* ---- Six more small ones ------------------------------------------------
 *
 * TWO MORE THREE-ARGUMENT PASS-THROUGHS, which makes four in this tree
 * counting ADDR_CALL_405220. Each forwards its three arguments to one large
 * function and adds nothing -- the compiler did not make these, since a
 * thunk that only moves arguments is what a source-level wrapper compiles
 * to. Worth knowing before reading one as a place where something happens. */
#define ADDR_CALL_4057D0         0x00405D10u  /* void(int32, int32, int32) */
/* SURVEYED AND NOT RECONSTRUCTED, and it comes as a PAIR with ADDR_BIG_405220
 * -- 1,344 and 1,424 bytes, adjacent, and 132 of their ~375 instructions are
 * ONE SHARED RUN: 0x00405973..  against 0x0040542F.., identical in every
 * register, global, immediate AND CALL TARGET, differing only in branch
 * displacements. Checked with call targets compared rather than normalised,
 * which matters: normalising them would make a call to a different function
 * compare equal, and this file already records what normalising image
 * addresses cost in VehicleBlockWeight.
 *
 * THAT RUN IS THE WHOLE SIGHT TEST -- turret facing, ObjTileAttr, the arc and
 * range checks against the rank record, the heading cache, the tile-line walk
 * and the three-band height test -- and it reads NO CONTEXT FIELD AT ALL, only
 * the object, the target, the rank record and the cache. So it factors for
 * this pair as a helper of (obj, target, rank), the way AiSightTrace already
 * factors its inner 38 for AiAttackBody and AiEngageStep. It does NOT factor
 * across all four: against 0x00406B30 the same region breaks into runs of 11,
 * 25, 28, 11 and 11, and against 0x00407710 into a single 28. Four functions
 * doing the same thing, two of them doing it identically.
 *
 * THE ARGUMENTS ARE THE OTHER WAY ROUND from AiAttackBody and AiEngageStep:
 * here esi is the OBJECT and edi the context, there esi is the context. Both
 * are void(obj, out, ctx) and the register choice is the compiler's, but a
 * reading carried over from the neighbours puts every field on the wrong
 * structure and still compiles.
 *
 * AND THE FOUR "SCRIPT" DWORDS AT 0xB0..0xBC ARE PACKED POINTS HERE. The head
 * copies OBJ_OFF_SCRIPT_STATE into OBJ_OFF_SCRIPT_ID or clears it to
 * ADDR_ZERO_POINT, and hands OBJ_OFF_SCRIPT_NEXT to ApproxDist, which takes an
 * AM2_Point *. That is the overload AiStepDefend already relies on for
 * OBJ_OFF_SCRIPT_STATE, extended to the whole block -- and the reason all four
 * keep names taken from UpdateObjectScript rather than from either use. */
#define ADDR_BIG_4057D0          0x004057D0u
/* Not a nameless pass-through any more: 0x00407BD0 is the AI mode
 * dispatcher's `attack` arm, mode 6, and all it does is forward its three
 * arguments to 0x00407710. That body is shared with mode 0, which reaches it
 * directly, so it is named for neither. Reconstructed in region.cpp beside the
 * other five arms. */
#define ADDR_AI_STEP_ATTACK      0x00407BD0u  /* void(obj, out, void *ctx) */
#define ADDR_AI_ATTACK_BODY      0x00407710u  /* mode 6 through the above, and
                                               * mode 0 directly */
/* SURVEYED AND NOT RECONSTRUCTED. 1,216 bytes over a 0x81C-byte frame, which
 * is a TraceTileLine buffer -- so it is a line-of-sight test as well as a step.
 *
 * ITS CONTEXT IS SIGHT_OFF_*, NOT SIGHTC_OFF_*, and the two families sit FOUR
 * BYTES APART: OBSERVER/RANGE/BEARING are 0x10/0x14/0x18 in one and
 * 0x14/0x18/0x1C in the other. Taking the wrong one shifts every field by one
 * slot and still compiles, still indexes, and still looks right. What settles
 * it is that the body copies {0x1C, 0x20, 0x24} into {0x10, 0x14, 0x18} --
 * FOUND, FOUND_RANGE and FOUND_BEARING promoted into OBSERVER, RANGE and
 * BEARING, which is a sentence under SIGHT_OFF_* and nonsense under SIGHTC_.
 * The 0x28 the head compares against 0x20 is SIGHT_OFF_DEST_DIST, so the first
 * thing it decides is whether it is far enough from where it belongs to walk.
 *
 * AND 0x00473DC4 IS NOT A TABLE BASE. It is indexed `[rank * 28 + 0x473DC4]`
 * and has eight references of its own, which is exactly what a base looks
 * like -- but ADDR_RANK_RECORDS is 0x00473DC0 and this is its
 * RANK_REC_OFF_FIELD_04. 0x00473DC8 beside it is FIELD_08. One grep of the
 * address before writing a name; the alternative was the sixth instance of
 * the commonest mistake in this project.
 *
 * THAT MAKES IT A THIRD READER FOR FIELD_04 AND THE TWO FIELDS ARE NOT ALIKE,
 * which the survey first said they were and the transcription corrected.
 * FIELD_04 is compared against `abs(AngleDelta(facing, bearing))`, so it is an
 * angle in eighth-of-a-turn units -- the second independent consumer reading
 * it as an arc, which is the evidence AddSightBlocker's note said it lacked.
 * FIELD_08 is compared against an ApproxDistXY, so it is a DISTANCE, and
 * 120..190 sits sensibly below FIELD_00's 280..320 sight range. Both are read
 * off the SAME `rank * 28` base four bytes apart, which is exactly how one
 * plausible sentence covered both and hid the difference.
 *
 * IT IS THE FOURTH MEMBER OF AiStepIgnore'S FAMILY and shares three blocks
 * with it -- the SIGHT_OFF_DEST_DIST head, the OBJ_OFF_HIT_DIR turn, and the
 * delayed turn toward what the context found. AiPromoteFound in region.cpp is
 * the promotion it does three times, and OBJ_OFF_SCRIPT_STATE is read as a
 * POINT here exactly as AiStepDefend and AiStepTrack already read it.
 *
 * THE BLOCK MAP, written down because three separate readings of it were
 * wrong on the way to this one and the function is cold, so nothing but
 * reading will ever catch a fourth:
 *
 *   0x407710  head: past DEST_DIST, route and promote; else clear the point
 *   0x407790  no leader -> 0x407B2A
 *   0x40779E  facing = OBJ_OFF_FACING, REPLACED by OBJ_OFF_FIELD_530 for a
 *             type 3 with more than one row -- one slot, written twice
 *   0x4077C1  range and bearing to the leader; |AngleDelta| against FIELD_04
 *   0x407831  outside the arc: inside FIELD_08 -> engage, else 0x407A26
 *   0x407848  the heading cache; trace the tile line unless already stamped
 *   0x40794A  three-band minimum, then the band test by relative height
 *   0x407980  ENGAGE, and its two arms are NOT symmetric -- see below
 *   0x407A06  cache miss: plain range test against FIELD_00
 *   0x407A26  out of sight: face and walk, promote, RETURN
 *   0x407A85  in sight and close enough: record the target and FALL THROUGH
 *   0x407AAB  hit; then the delayed turn on LEAD_BEARING; then ObjIsType2 and
 *             a walk toward the leader
 *   0x407B2A  no leader at all: hit; promote and turn on BEARING; then
 *             OBJ_OFF_FOLLOW_UID from the observer
 *   0x407BAE  ConsiderSighting, on every path
 *
 * THE TWO TAILS ARE DIFFERENT FUNCTIONS AND LOOK LIKE ONE. 0x407AAB turns on
 * SIGHT_OFF_LEAD_BEARING and ends with an ObjIsType2 test and a walk; 0x407B2A
 * turns on SIGHT_OFF_BEARING, promotes, and writes OBJ_OFF_FOLLOW_UID. Both
 * open with the identical OBJ_OFF_HIT_DIR block, which is what makes merging
 * them tempting and wrong.
 *
 * AND THE ENGAGE ARMS ARE NOT AN if/else. The far arm chases and RETURNS; the
 * near arm records the target and falls into 0x407AAB, so a unit already at
 * the range it wants still reacts to a hit and still turns. Writing the near
 * arm as the far one's `else` with a shared tail loses that.
 *
 * ONE ARM ENDS INSIDE ANOTHER: the HIGH band's compare at 0x4079F6 jumps back
 * to 0x40797A to borrow the LOW band's `jg`, so following the bodies without
 * following the branches gives it no exit at all.
 *
 * Its callees are all named and all reconstructed bar ConsiderSighting:
 * AiRouteToward, ObjTileAttr, ObjHeight, AngleDelta, ApproxDistXY,
 * AngleOfDelta, TraceTileLine, ObjIsType2, ObjIsType3. */
/* Four calls and a tail jump, all five to the def tables: sort the trooper
 * records, two unnamed, then DefCheckLinks, then one more. The order is the
 * fact. */
#define ADDR_DEF_FINISH          0x0041A230u  /* void(void) */
#define ADDR_DEF_STEP_460290     0x00460290u
#define ADDR_DEF_STEP_45EBC0     0x0045EBC0u
/* Two of DefFinish's five are qsorts, and they are the same function with
 * different tables: 0x00435A50 sorts ADDR_DEF_OBJ_RECS by ADDR_COMPARE_TRIPLE
 * and 0x0044CD40 sorts ADDR_DEF_TROOPER_RECS by ADDR_COMPARE_DWORD. Both end
 * by tail-jumping to the logger with no arguments.
 *
 * 0x00435A50 went in as ADDR_DEF_STEP_435A50 one commit ago, when DefFinish
 * could only say "the third of five". checkpatches refused the second name
 * the moment it was reconstructed, which is the alias ratchet doing exactly
 * what it is for: a placeholder name is fine until the thing has a real one. */
#define ADDR_DEF_SORT_OBJ_RECS   0x00435A50u  /* void(void) */
/* 0x0044C550, one caller. Walk the four comm slots and run 0x0044C480 on
 * every one that is occupied AND that CommMustBroadcast accepts. */
#define ADDR_TELL_EACH_SLOT      0x0044C550u  /* void(void) */
/* 0x0044C480. It is the SENDER of the kind-0x16 batch whose receiver,
 * ADDR_RECV_TROOP_16, was reconstructed months earlier: it walks one comm
 * slot's army object list and appends each live trooper's state to one
 * message, flushing whenever another record would not fit.
 *
 * The buffer is 0x12C bytes and the room it reserves for the next record is
 * TEN. THAT IS A BOUND, settled by reconstructing the function it guesses
 * about: the head is four bytes, the pose byte is always present, and the
 * position and facing add at most three and one -- so a record is FIVE bytes
 * at least and NINE at most. Ten is correct and tight by one. */
#define ADDR_TELL_ONE_SLOT       0x0044C480u  /* void(int32_t slot) */
#define ADDR_APPEND_TROOP_STATE  0x0044BC10u  /* void(msg *, void *obj) */
#define AM2_TROOP_BATCH_MAX      0x12Cu  /* the whole message buffer */
#define AM2_TROOP_BATCH_SLACK    0x0Au   /* what it keeps free for one more */
/* The same number as AM2_MSG_TROOP_FIRST above, and not by accident: the
 * batch is the lowest code the trooper dispatcher handles, so the range's
 * lower bound and this kind are one value seen two ways. Spelled separately
 * because a message kind and a range bound are different claims. */
#define AM2_MSG_TROOP_BATCH      0x16u
/* 0x00453AB0, thiscall with one argument, `ret 4`. Grow a list of 260-byte
 * records by one and then SEARCH it for a key at +0x100, answering the index
 * or -1.
 *
 * The count is bumped BEFORE the realloc and the search runs over the new
 * count, so the last record it compares is the one just allocated and never
 * written. Reading uninitialised memory, and the original's. */
#define ADDR_LIST_GROW_FIND      0x00453AB0u  /* thiscall int32(this, key) */
/* 0x004013B0, one caller. Print one message list under its own mutex --
 * "List: ", then "(%d %d)" per node, then a newline. A debug dump, and the
 * only place those three strings are used. */
#define ADDR_DUMP_MSG_LIST       0x004013B0u  /* void(void *list) */
#define ADDR_STR_LIST_HEAD       0x00473110u  /* "List: " */
#define ADDR_STR_LIST_NODE       0x00473108u  /* "(%d %d)" */
#define ADDR_STR_NEWLINE         0x00473104u  /* "\n" */
/* 0x00417AB0, one caller. Walk a uid list, drop the entries that no longer
 * resolve, and run ADDR_TYPE2_ACTION_A on every live type 2 that is not
 * destroyed. The list is a {capacity, count, items} triple. */
#define ADDR_TYPE2_ACTION_ALL    0x00417AB0u  /* void(void) */
#define ADDR_TYPE2_ACTION_LIST   0x004F9ED0u  /* {cap, count, uids} */
/* 0x00423620, two callers, both save-game dialogs. Does this file begin with
 * a save tag? Two are accepted: AM2_SAVETAG_GAMEPROC and one more. */
#define ADDR_FILE_HAS_SAVE_TAG   0x00423620u  /* int32_t(const char *path) */
#define AM2_SAVETAG_ALT          0x06660668u
#define AM2_GROWLIST_STRIDE      0x104u
#define AM2_GROWLIST_KEY         0x100u
/* Walk the objects in the cell a point falls in and chain the ones that
 * qualify through OBJ_OFF_QUERY_NEXT, answering the head. 0x0044A3A0 is one
 * of its two callers.
 *
 * THIS COMMENT SAID "a CALLBACK instead of a chain" AND IT IS BOTH. The
 * callback is a FILTER -- its answer decides whether the object joins the
 * chain -- and the chain is built and returned exactly as
 * ADDR_OBJECTS_HIT_BY_POINT's is. Reconstructing it is what settled that;
 * the return type below was `void` for the same reason. */
#define ADDR_WALK_CELL_AT_POINT  0x0042A110u  /* void *(pt*, desc, keep) */
#define ADDR_WALK_CELL_WRAPPER   0x0044A3A0u  /* void *(void *unused, uint32 pt) */
#define ADDR_WALK_CELL_CALLBACK  0x0044A380u
/* Drain one message list into another, head first, until it is empty. */
#define ADDR_DRAIN_MSG_LIST      0x00401210u  /* void(void *list) */
/* Reset the host-battle state: empty both saved names, put 1000 in the value
 * below and reset the pair mask. Its two names are ADDR_SAVED_PLAYER_NAME and
 * ADDR_SAVED_BATTLE_NAME, which HostBattle fills -- so this is what undoes
 * it. */
#define ADDR_RESET_HOST_STATE    0x0042F140u  /* void(void) */
#define ADDR_HOST_MASK_A         0x00516078u  /* uint32_t */
#define ADDR_HOST_MASK_B         0x0051607Cu  /* uint32_t */
#define ADDR_HOST_VALUE_3E8      0x00516090u  /* int32_t, set to 1000 */
/* NOT A FREE. It BUILDS every palette remap the game owns, and orig.h already
 * half knew: ADDR_ROW_LUT_DOUBLES is described as "filled at 0x0040A5C2 and
 * 0x0040A5D7", which are both inside this function. Four things in one pass:
 *
 *   the four ARMY remaps at ADDR_OBJ_TABLE_RECORDS -- so an object's "table"
 *   is a 256-byte palette remap, which is how a script recolours a unit and
 *   what AM2_OBJ_TABLE_REC_SIZE has been measuring all along;
 *   four more at 0x004F96CC, built from the same bases without asking the
 *   palette at all;
 *   sixty-four blocks of randomised ten-colour rows, six rows each, with a
 *   table of pointers to them;
 *   and the grey ramp at ADDR_ROW_LUT_DOUBLES.
 *
 * Every remap is IDENTITY above index 10 and only the first ten entries are
 * ever changed, which is what makes a palette with a reserved block at the
 * bottom remappable at all. Reconstructed. */
#define ADDR_BUILD_REMAP_TABLES  0x0040A4B0u  /* void(void) */
/* One palette index per army, {206, 216, 186, 196}, and every remap this
 * builder writes starts from one of them. */
#define ADDR_ARMY_PAL_BASE       0x00474174u  /* uint8_t[4] */
/* The second set of four, built without the palette: entry i takes
 * base + (i >> 1), so ten entries come out as five doubled steps. */
#define ADDR_ARMY_RAMP_TABLES    0x004F96CCu  /* uint8_t[4][0x100] */
/* Sixty-four blocks of 0x100 bytes and the table of pointers into them. Each
 * block holds six rows of ten, and each ROW picks a random army base -- so
 * these are the randomised colour variations, decided once at startup. */
#define ADDR_VARIATION_BLOCKS    0x004FE2C0u  /* uint8_t[64][0x100] */
#define ADDR_VARIATION_END       0x005022C0u
#define ADDR_VARIATION_TABLE     0x00507130u  /* uint8_t *[64] */
#define AM2_REMAP_COLOURS        10     /* entries below this are remapped */
#define AM2_VARIATION_ROWS       6
#define AM2_ARMY_PAL_BASES       4
/* The `from` every NearestPalIndexRGB call in the builder passes. CLAUDE.md
 * records that argument arriving as 0, 9, 10, 60 and 100 over 8,498 calls;
 * this is where the 100 comes from. */
#define AM2_PAL_REMAP_FROM       100
#define ADDR_FREE_40A5F0         0x0040A5F0u  /* void(void) */
/* 0x0044CD40 is the third: qsort the table with ADDR_COMPARE_DWORD and then
 * TAIL-JUMP to 0x0045CAA0. That address is ADDR_LOG, which src/inject/gamelog.c
 * patches -- so naming it in src/game would be a seam checkseams refuses, and
 * rightly: the jump is to a bare `ret` that identical-COMDAT folding has merged
 * with the stubbed logger, exactly the case CLAUDE.md describes for vtable slot
 * 2. Left original rather than reconstructed around, since reproducing it needs
 * a decision about what that folded function WAS. */
#define ADDR_DEF_SORT_TROOPER_RECS 0x0044CD40u  /* void(void). Reconstructed. */
#define DEF_OBJ_REC_OFF_LINKS      0x0Cu        /* where the count is stored */
/* An int16 the height handler ADDS to the row's depth key. So an object type
 * can sit in front of or behind another at the same ground height, which is
 * the one thing a scaled height alone cannot express. */
#define DEF_OBJ_REC_OFF_DEPTH      0x20u        /* int16_t */
/* SIX MORE OF THAT RECORD, every one named from the OBJECT FIELD it lands in
 * -- the same method the AAI record's own note describes, and here it applies
 * twice over, because ApplyObjFrame does for a def-obj record what
 * InitObjFromAai does for an AAI one. The two records are NOT the same shape
 * and there is no uniform shift between them, but the SEQUENCE of meanings is
 * identical: flags, health, height adjust, rank, then the dword every row's
 * own +0x28 is filled from.
 *
 * FLAGS IS ASSIGNED HERE AND OR'd THERE. InitObjFromAai combines AAI_OFF_OR_FLAGS
 * with its caller's and ORs the result in; this writes +0x10 straight over
 * OBJ_OFF_FLAGS, so a frame change can turn a flag OFF where a create cannot.
 * Worth stating because the two functions otherwise read as the same code.
 *
 * FIELD_30 KEEPS A FIELD-NUMBERED NAME AND MAKES NO CLAIM: its one use is as
 * ObjAfterMove's third argument, and that function is still original, so a
 * name would be taken from a call site. Same standing as MISSILE_OFF_GROUND.
 *
 * AND NOTHING IS ADDED AT 0x20. The list-record's third dword is copied from
 * there as a DWORD, which straddles DEF_OBJ_REC_OFF_DEPTH and the two bytes
 * after it; what those two bytes are has not been read, so the existing int16
 * name stays and the site casts. */
#define DEF_OBJ_REC_OFF_FLAGS         0x10u  /* -> OBJ_OFF_FLAGS, assigned */
#define DEF_OBJ_REC_OFF_HEALTH        0x14u  /* int16, -> max AND current */
#define DEF_OBJ_REC_OFF_HEIGHT_ADJ    0x18u  /* int8 -> OBJ_OFF_HEIGHT_ADJ */
#define DEF_OBJ_REC_OFF_RANK          0x1Cu  /* int8 -> OBJ_OFF_RANK */
#define DEF_OBJ_REC_OFF_ROW_FIELD28   0x24u  /* -> every ROW_OFF_FIELD_28 */
#define DEF_OBJ_REC_OFF_FIELD_30      0x30u  /* ObjAfterMove's third argument */
/* Set 0x16 loads its sprites with an extra bit. Every other set here, and
 * EnsureSpriteAaiRecord's PreloadSprite beside it, pass a bare 0x1000 --
 * spelled as a literal there and left as one here to match; ApplyObjFrame is
 * the only site in the image that adds 0x80, and only for that one set. What
 * the bit does has not been read, so the SET gets the name and the flag does
 * not. */
#define AM2_SET_WITH_EXTRA_LOAD_FLAG  0x16
/* Subtracted from the scaled height to make a row's ROW_OFF_FIELD_26. It is
 * 1000, which is also AM2_SEQ_LIFE6 -- two unrelated constants that happen to
 * share a value, and writing one for the other is how a name goes wrong
 * silently. Its own name. */
#define AM2_DEPTH_BASE             0x3E8
#define ADDR_CRT_QSORT             0x004660B2u
#define ADDR_CRT_BSEARCH           0x00466280u
/* One spelling for the game's bsearch, because two modules want it now.
 * defparse.cpp had it privately; map.cpp's level lookup uses the same. */
typedef void *(__cdecl *AM2_BsearchFn)(const void *key, const void *base,
                                       uint32_t n, uint32_t size,
                                       const void *cmp);
/* The link table's capacity, and how it grows: 50 records to begin with, then
 * twenty MORE RECORDS at a time -- not twenty bytes. Both numbers are the
 * original's. */
#define ADDR_DEF_LINK_CAP          0x00516184u
#define AM2_DEF_LINK_INITIAL       0x32
#define AM2_DEF_LINK_GROW          0x14
#define ADDR_CRT_STRTOK            0x0046551Cu  /* the game's own; the state is
                                                 * shared with DefObjParse, so
                                                 * libc's would be wrong */
/* NOT DefLinkParse. 0x00436080 is a 52-byte wrapper that searches the link
 * table -- docs/functions.tsv merges the two and tools/merges.py does not
 * split them, so this address carried the wrong name until the body was read.
 * The function with the three "DefLinkParse:" strings is 0x004360C0, above. */
#define ADDR_DEF_LINK_SEARCH       0x00436080u  /* AM2_DefLink *(int32,int32) */
/* Every PreloadSprite/PreloadSpriteKey call in the placement cursor passes
 * this, and so does EnsureSpriteAaiRecord -- see AM2_SET_WITH_EXTRA_LOAD_FLAG
 * for the one site that adds a bit to it. */
#define AM2_PLACE_SPRITE_SET       0x1000

#define ADDR_SCRIPT_OBJECT        0x00436D60u  /* keywords 139 and 140 --
                                               * GenerateObjScriptFromTokens,
                                               * from its own error string */

/* APPENDS an object-script record and returns it -- not the accessor it looks
 * like from the call site. It grows the array at 0x00516188 twenty entries at
 * a time, zeroing the new ones, and increments the count at 0x0051618C, which
 * is the same global the attach below then reads. So the id stamped onto each
 * object is the count AFTER this call, and the record is 20 bytes.
 *
 * +0 is 0 for `object` and 1 for `objclass`; +4 is a dword name index in the
 * first case and two 16-bit class fields in the second. */
#define ADDR_NEW_OBJ_SCRIPT       0x00437130u
#define ADDR_OBJ_SCRIPTS          0x00516188u
#define ADDR_OBJ_SCRIPT_CAP       0x00516190u
#define AM2_OBJ_SCRIPT_REC_SIZE   20u

/* The object-script save section. The saver walks four levels -- script,
 * state, frame, action -- writing each record whole, and the sizes it uses
 * (0x14, 0x10, 0x14, 0x48) confirm objscript.h's struct layout independently
 * of how those structs were derived. The count doubles as a data value: it
 * goes out through WriteSaveTag, like every other length in this format. */
#define ADDR_SAVE_OBJSCRIPT_SECTION 0x00436280u  /* int32_t(FILE *) */
#define ADDR_LOAD_OBJSCRIPT_SECTION 0x004364A0u  /* int32_t(FILE *) */
/* Frees every level of the object-script table and is what the loader calls
 * first. Three callers. Reconstructed, so the loader calls it directly. */
#define ADDR_FREE_OBJ_SCRIPTS       0x004368D0u  /* void(void) */
#define ADDR_STR_OBJSCRIPT_CPP      0x0048758Cu  /* "C:\\ArmyMen2\\source\\objscript.cpp" */
#define AM2_SAVETAG_OBJSCRIPT       0x06660008u
#define AM2_OBJ_STATE_REC_SIZE      16u
#define AM2_OBJ_FRAME_REC_SIZE      20u

/* 0x00440700. Resolve a name token to its index in the name table, declaring
 * it if need be -- it reaches both ScriptFindName and AddNameTableName.
 * int32_t(ctx *, int32_t *at, int32_t *out, int32_t). */
#define ADDR_SCRIPT_RESOLVE_NAME  0x00440700u

/* 0x00436C20. One attribute statement inside an object block. The block ends
 * where ScriptIsStatementStart says the next top-level statement begins. */
#define ADDR_SCRIPT_OBJ_STATE     0x00436C20u  /* `state <name>` */
#define ADDR_SCRIPT_OBJ_FRAME     0x004369E0u  /* `frame <int> <int>` */
#define ADDR_OBJ_FRAME_NEW_ACTION 0x00437010u  /* 72-byte entries */
#define ADDR_OBJ_STATE_NEW_FRAME  0x00437070u  /* 20-byte entries */
#define ADDR_OBJ_SCRIPT_NEW_STATE 0x004370D0u  /* 16-byte entries */
#define ADDR_SCRIPT_COMPARE       0x004374F0u  /* int32_t(a, op, b) */
#define ADDR_SCRIPT_NAME_UID      0x0043F9F0u  /* int32_t(const char *) */
#define ADDR_SCRIPT_INT_OR_VAR    0x00442F80u  /* (ctx,at,&val,&isliteral) */
#define ADDR_SCRIPT_OBJECT_UID    0x0043FF00u  /* (ctx,at,&zero,&uid) */
#define ADDR_SCRIPT_ARMY_COLOUR   0x00440930u  /* int32_t(ctx,at) */
/* Not a table of its own: 0x004751B0 is ADDR_COMM_OBJECT under a second name,
 * which is how it came to look like one. The army lives in the player record
 * on the comm object, so this alias is kept only for the `this` these two
 * accessors want and says what it really is. */
#define ADDR_ARMY_TABLE           0x004751B0u  /* == ADDR_COMM_OBJECT */
#define ADDR_COMM_SLOT_FOR_ARMY   0x0040F250u  /* thiscall int32_t(this, army) */

/* Object lookup and iteration. The two iterators take no arguments: they walk
 * whatever the record at ADDR_SCRIPT_OBJ_TARGET selects, which the objclass
 * branch has just filled in. */
/* 0x0044BA60, 14 callers. A cdecl wrapper for the line above, passing its one
 * argument straight through -- the same shape as ADDR_BUILD_FONT_ALIAS. */
#define ADDR_OBJ_BY_UID_ALIAS     0x0044BA60u  /* obj *(uint32_t uid) */
/* 0x0045EE80: the same lookup, but insisting the object is kind 4. It names
 * what that means itself -- "uid wasn't a weapon!" -- so kind 4 is a WEAPON
 * and this is WeaponByUid. Answers null, having complained, for any other
 * kind. */
#define ADDR_WEAPON_BY_UID        0x0045EE80u  /* obj *(int32_t uid) */
/* 0x0045F460, 3,200 bytes and four callers, all of them the little glue
 * functions below. THE ONE THING WORTH KNOWING BEFORE READING THEM is what
 * its last three arguments do, because two of them default:
 *
 *   - argument 6 is the GROUND HEIGHT at the shot's destination, and when it
 *     is zero the low word of argument 3 -- the shooter's own height -- is
 *     used instead;
 *   - argument 5 is the destination POINT, and when it is zero and argument 7
 *     is not null the target object's own position is used instead.
 *
 * So a caller may supply the point, or the target, or both, and the four
 * below are exactly the four ways of doing that. Argument 5 and argument 6
 * are one 8-byte structure pushed by value; its top two bytes are never
 * written by any caller, so nothing correct can read them. */
#define ADDR_FIRE_WEAPON          0x0045F460u  /* int32_t(weapon, unit, ...) */
/* The four ways a script asks for a shot: an explicit weapon or the unit's
 * own, at a point or at another object. All four take a HEADING that is
 * computed from the geometry when it arrives NEGATIVE, and the two with an
 * explicit weapon lend it the firing unit's army for the duration of the
 * call and put the old one back afterwards. */
#define ADDR_FIRE_WEAPON_AT_POINT  0x004200F0u /* void(wpn,unit,head,at) */
#define ADDR_FIRE_WEAPON_AT_OBJECT 0x004201A0u /* void(wpn,unit,head,uid) */
#define ADDR_UNIT_FIRE_AT_POINT    0x00420260u /* void(unit,head,at) */
#define ADDR_UNIT_FIRE_AT_OBJECT   0x00420300u /* void(unit,head,uid) */
#define ADDR_STR_NOT_A_WEAPON     0x0048C6E0u  /* "uid wasn't a weapon!\n" */
#define AM2_OBJ_TYPE_WEAPON       4
/* 0x00447990, "RemoveInventoryItem": take one slot out of a unit's six-entry
 * weapon inventory. 0x00449860 selects a slot -- it writes the index to
 * 0x0568 and looks the weapon up -- and is otherwise unread. */
/* 0x00410B70, "ReceiveEndSetupMsg": log, then post AM2_WM_SETUP_DONE to the
 * window. Sixty-four bytes and no state of its own -- the handshake's whole
 * receive side is telling the message pump something arrived. */
#define ADDR_RECEIVE_END_SETUP_MSG 0x00410B70u /* void(void) */
/* 0x00410E90, "ReceiveGameReadyToLoadMsg". Host-only. The field it sets names
 * itself in the log: "Setting m_ArmyReadyToLoad[%d] to %s", so 0x0270 of the
 * 112-byte per-army record is m_ArmyReadyToLoad. */
#define ADDR_RECV_READY_TO_LOAD    0x00410E90u /* void(msg *, int32_t dpid) */
/* 0x00410BB0, "ReceiveGameReadyMsg". Records m_ArmyReady and, on the host,
 * sends the end-of-setup message once every occupied slot is ready.
 * 0x004FC3A8 is the record it sends. */
#define ADDR_RECV_GAME_READY       0x00410BB0u /* void(msg *, int32_t dpid) */
#define ADDR_MSG_END_SETUP         0x004FC3A8u
/* 0x00410D90, "SendGameReadyToLoadMsg". The client half of the pair whose host
 * half is 0x00410E90. 0x004FAA18 is the record it sends, value at +8. */
#define ADDR_SEND_READY_TO_LOAD    0x00410D90u /* void(int32_t) */
#define ADDR_MSG_READY_TO_LOAD     0x004FAA18u
/* 0x00410A10, "SendGameReadyMsg\n %s". The local half of 0x00410BB0: it marks
 * OUR OWN slot ready -- the id comes from the comm object, not from a message
 * -- sends the record at 0x004FAA28 and then runs the same end-of-setup scan.
 * Its one caller is 0x00418FA1. */
#define ADDR_SEND_GAME_READY       0x00410A10u /* void(int32_t) */
#define ADDR_MSG_GAME_READY        0x004FAA28u /* value at +8 */
/* 0x00411000, "SendGameStartMsg". Host only, and the one place the shared
 * random seed is chosen: the host reads the clock, keeps the value and sends
 * it, so every machine's RNG starts from the same number. */
#define ADDR_SEND_GAME_START       0x00411000u /* void(void) */
#define ADDR_MSG_GAME_START        0x004FC5F0u /* record: +4 len, +8 players, +0xC id */
#define ADDR_GAME_SEED             0x00512314u /* int32_t, the shared seed */
#define ADDR_GAME_SEED_SENT        0x004FC780u /* int32_t, the copy that goes out */
#define ADDR_CRT_TIME              0x00465052u /* int32_t(int32_t *) -- GetLocalTime */
#define COMM_OFF_STARTED           0x400u      /* non-zero once the game is running */
#define AM2_SESSION_FLAGS_START    0x21u       /* or'd into the description */
/* The same pair CommReopenSession clears again, and the offset it clears them
 * at: DPSESSIONDESC2.dwFlags. Not restated from the SDK header -- dplay.cpp
 * reaches the description as an opaque pointer, because the game passes it
 * around as one. */
#define AM2_DPSESSION_OFF_FLAGS    0x04u
/* Four player slots, and the count every loop over them uses. */
#define AM2_COMM_SLOTS             4
#define COMM_ARMY_OFF_READY        0x274u   /* m_ArmyReady, its own name */
#define COMM_ARMY_RECORD_SIZE      112u
#define COMM_ARMY_OFF_READY_TO_LOAD 0x270u
#define ADDR_REMOVE_INVENTORY_ITEM 0x00447990u /* void(AM2_Object *, int32_t) */
/* 0x00448D60, self-named in both its log lines -- "TrooperDropItem  %x" and
 * "TrooperDropItem  %x  ammo: %d". Still original; EvtDropItem reaches it. */
#define ADDR_TROOPER_DROP_ITEM   0x00448D60u /* void(unit,int32 slot,uint32) */
/* 0x0044C150, and it names itself twice: "<--Trooper Drop Item Send: Trooper:
 * %x, item: %x,  slot: %d, quant: %d" and the "-->... Sent" beside it. The
 * message TrooperDropItem and UseInventoryItem send when a drop has to be
 * told to the others. Reconstructed. */
#define ADDR_TROOPER_DROP_ITEM_SEND 0x0044C150u /* void(unit,item,slot,q,pt) */
#define MSG_DROP_OFF_TROOPER      0x04u  /* UidOnWire(unit->uid) */
#define MSG_DROP_OFF_ITEM         0x08u  /* UidOnWire(item->uid) */
#define MSG_DROP_OFF_AT           0x0Cu  /* the caller's point, unchanged */
#define MSG_DROP_OFF_REQUEST      0x10u  /* the literal 3, always */
#define MSG_DROP_OFF_QUANT        0x14u
#define MSG_DROP_OFF_SLOT         0x18u  /* the byte argument, sign-extended */
#define AM2_MSG_DROP_ITEM_LEN     0x1Cu
/* The `request` field is a four-value protocol and the program names all four
 * of them, in TrooperWantItemSend's own log lines: WANT_PICKUP, WANT_DROP,
 * DO_PICKUP, DO_DROP. So TrooperDropItemSend's literal 3 is not a magic
 * number at all -- it is DO_DROP, the "this has happened" half of the pair
 * whose "may I" half is WANT_DROP. */
#define AM2_WANT_PICKUP           0
#define AM2_WANT_DROP             1
#define AM2_DO_PICKUP             2
#define AM2_DO_DROP               3
/* 0x00449760, one caller, and it names itself twice: "UseInventoryItem" and
 * "UseInventoryItem: droping item:%x". Spend one charge of an inventory slot.
 * Reconstructed. */
#define ADDR_USE_INVENTORY_ITEM   0x00449760u /* void(unit, int32_t slot) */
/* The uid of the last thing this unit dropped. Only TrooperDropItem writes it
 * and nothing read so far reads it, so the name is what the write says and no
 * more. */
#define UNIT_OFF_LAST_DROPPED     0x564u
#define ADDR_SELECT_INVENTORY_SLOT 0x00449860u /* void(AM2_Object *, int32_t) */
/* 0x004498F0, one caller. Cycle a trooper to its next inventory slot, on the
 * action key or on the middle mouse button being RELEASED. Reconstructed. */
#define ADDR_NEXT_INVENTORY_SLOT 0x004498F0u  /* void(AM2_Object *) */
#define AM2_ACTION_NEXT_WEAPON   11    /* ADDR_ACTION_KEY_PRESSED's argument */
/* ADDR_VEHICLE_DISMOUNT_ALL's gate in 0x00413E70, which is what names it --
 * the CONTROLS dialog's own caption for the binding is "EXIT VEHICLE". */
#define AM2_ACTION_EXIT_VEHICLE  0x0D
/* TWO DIFFERENT actions both put the pointer into aim mode: 0x00413E70 tests
 * them with an OR and takes the same arm either way.  WHICH two is not
 * established and cannot be read off the image -- the CONTROLS captions come
 * out of a data file, not a table in .rdata, so there is nothing to index.
 * Numbered rather than named, the way OBJ_OFF_FIELD_C0 is. */
#define AM2_ACTION_0A            0x0A
#define AM2_ACTION_06            0x06
#define AM2_MOUSE_MIDDLE         2     /* into ADDR_MOUSE_BUTTON/_CHANGED */
/* 0x00403B40, five callers, and what it ANSWERS is what several of them keep:
 * AiStepDefend promotes it into SIGHT_OFF_FOUND, and NextInventorySlot runs it
 * for its effect and ignores the answer. Nothing in it says what it is. */
/* SURVEYED AND NOT RECONSTRUCTED. 1,888 bytes over a 0x84C frame, and it is
 * the SIGHTING SCAN -- what fills SIGHT_OFF_FOUND, which is the field every
 * AI step in this family promotes. All twenty-three of its callees are named.
 *
 * IT CALLS ADDR_LOG WITH ONE ARGUMENT AND NO FORMAT STRING, twice, pushing the
 * object. That is a THIRD role for 0x0045CAA0, which this file already records
 * as both the retail build's stubbed varargs logger and one widget class's
 * empty vtable slot -- three unrelated functions folded onto one `ret` by
 * identical-COMDAT folding, which is the best evidence yet for that
 * explanation, since a logger, an empty virtual and a per-object hook have
 * nothing else in common.
 *
 * IT IS SAFE TO REPRODUCE, checked rather than assumed: src/inject/gamelog.c
 * patches that address and would take the object pointer as a format string,
 * but its safe_format walks the string by hand precisely because "_vsnprintf
 * means any byte pair that looks like %s dereferences a garbage pointer". So
 * the call renders object bytes as text and cannot fault. Both halves of an
 * A/B would render the same bytes, and nothing reaches it anyway.
 *
 * IT DOES NOT KEEP BOTH FACINGS, AND THE CLAIM THAT IT DID WAS THE EXACT ERROR
 * tools/espmap.py WAS BUILT FOR -- made one commit before building it, and
 * caught by it immediately afterwards. The two stores are `mov byte
 * [esp+0x40], al` and `mov byte [esp+0x3c], cl`, which look like two slots and
 * are one: the first has an outstanding `push esi` for the ObjIsType3 call in
 * front of it, so both land on the same dword. A type 3's turret facing
 * OVERWRITES the hull's here exactly as it does in the four AI steps, and the
 * "one slot written twice" note carries over unchanged.
 *
 * Worth leaving the correction visible rather than quietly editing the claim:
 * the displacement was read without counting the pushes, which is the one
 * thing a hand reading of a large frame cannot be trusted to do, and the
 * second sight test forty instructions later does the same thing again. */

/* IT IS WHERE THE HEADING CACHE COMES FROM, which closes the loop on
 * ADDR_SIGHT_BLOCK_BY_DIR's two stamps. This function INCREMENTS
 * ADDR_SIGHT_GENERATION and then feeds every object it rejects to
 * AddSightBlocker, which is what writes the three per-heading minima; the four
 * AI steps reconstructed above only ever READ that cache and compare their own
 * stamp against the generation. So the scan invalidates and refills, the steps
 * consult, and the "trace stamp" and "minima stamp" being separate is the
 * seam between them.
 *
 * SEVEN ARGUMENTS, FOUR OF THEM OUT-PARAMS, and all four are zeroed at entry
 * before anything is looked at -- (int32 *, uint8 *, int32 *, int32 *), which
 * is the {object, bearing, ?, count} the callers read back into
 * SIGHT_OFF_FOUND and its neighbours.
 *
 * TWO PHASES, AND THE FIRST ONE EDITS THE LIST IT WALKS. ObjectsInRect answers
 * a chain threaded through OBJ_OFF_QUERY_NEXT, with one of TWO predicates
 * chosen by argument 5 -- 0x004036C0 or 0x00403660. The walk then UNLINKS each
 * object it wants into a second list and hands everything else to
 * AddSightBlocker, so the same pass both selects candidates and builds the
 * occlusion data the selection will be tested against. A reading that treats
 * it as a filter followed by a scan gets the order wrong.
 *
 * ANYTHING OURS AND OF TYPE 2, 3 OR 8 IS REVEALED on the way past, with a
 * 0x7D0-millisecond stamp written to its +0x5C. That is a side effect of
 * looking, not of finding.
 *
 * KIND 7 IS SCORED DIFFERENTLY from everything else: AngleBetween and a flat
 * 0x3E8 where the rest go through the arc and range machinery. */

/* ITS SIGNATURE, resolved with tools/espmap.py rather than by eye. Six
 * arguments, four of them out-params, all four cleared before anything is
 * looked at:
 *
 *   void *SightScan(void *obj, int32 *range, uint8 *bearing,
 *                   int32 *nAllied, int32 *nOther, int32 flags)
 *
 * `flags` picks between the two ObjectsInRect predicates and gates the type-4
 * arm; the answer is the object chosen, which is what the callers put in
 * SIGHT_OFF_FOUND with `range` and `bearing` beside it.
 *
 * THE TWO PREDICATES ALREADY HAD NAMES AND THEY ARE BETTER THAN THE ONES I
 * NEARLY GAVE THEM. `flags` non-zero selects ADDR_OBJ_IS_HITTABLE and zero
 * selects ADDR_OBJ_IS_LIVE_TARGET -- which say what each one ASKS, where
 * "predicate A" and "predicate B" would have said only which argument picks
 * them. checkpatches refused all three names in one edit, this address
 * included; the rule about grepping an address before naming it was cited
 * repeatedly in this session's own commits and then broken three times in a
 * single hunk.
 *
 * THE SEARCH BOX IS SQUARE AND INT32. Four dwords at the frame's +0x48..+0x54
 * -- an AM2_Rect, whose fields really are int32 -- built as the position plus
 * and minus RANK_REC_OFF_SIGHT_RANGE on both axes. Written in the order
 * right, left, bottom, top, which is not the struct's order and is worth not
 * transcribing from the store sequence.
 *
 * A UNIT ADOPTS WHAT ITS ALLIES ARE SHOOTING AT, and that is invisible unless
 * the branches are followed rather than the bodies. The allied arm looks like
 * a counting-and-skipping block -- bump *nAllied, validate the ally's
 * OBJ_OFF_TARGET_UID, drop it if the target is dead or flagged -- and THREE of
 * its exits jump back INTO the kind-7 arm's scoring tail at 0x00403DCD with
 * the ALLY'S TARGET in the candidate slot. So the scan scores that object as
 * though it had found it itself.
 *
 * Read as bodies, the arm ends in `continue` five times and the behaviour
 * disappears; the unit would only ever engage what it saw for itself. It is
 * the "AN ARM CAN END INSIDE ANOTHER" shape three times in one block, and the
 * candidate slot being written from two different registers in two different
 * arms is the tell.
 *
 * Only AI mode 1 takes the last of those three, and only inside
 * AM2_AI_PATROL_DETOUR of the ally's target.
 *
 * THE EXIT WRITES THE BEARING TWICE AND THE FIRST WRITE IS DEAD. The primary
 * arm stores the bearing the scan recorded and then immediately overwrites it
 * with AngleBetween from ObjAnchorPoint to the winner -- so the answer is
 * measured from the object's ANCHOR, not from wherever the scan measured it.
 * The fallback arm returns the recorded bearing and does not do this. Two
 * exits, two different bearings, and the difference is one call. */
#define AM2_SIGHT_SCAN_FAR       0x1000       /* the initial best range */
#define AM2_SIGHT_KIND7_RANGE    0x3E8        /* kind 7 is scored flat */
/* Two seconds, written to OBJ_OFF_REVEALED_UNTIL by whatever looked. */
#define AM2_SIGHT_REVEAL_MS      0x7D0
#define ADDR_SIGHT_SCAN           0x00403B40u  /* int32(obj, a, b, c, d, e) */
/* 0x00448880, two callers, 64 bytes. The first dword of the OBJ_OFF_FIELD_C0
 * record of whatever sits in UNIT_OFF_INVENTORY_SEL -- the same value
 * SaveType2 writes as its tag and ThingCode switches on. Reconstructed. Both
 * callers compare it against 20 and neither is read yet. Runs 12,293 times in
 * a Boot Camp mission driven with movement and fire, so it is A/B covered. */
#define ADDR_HELD_WEAPON_CODE      0x00448880u /* int32_t(void *unit) */
/* 0x00448E60, three callers, 160 bytes. How much one object obstructs, for a
 * viewer and a reference point. Reconstructed.
 *
 * The blocking reading is the CALLERS', not a guess about the body: all three
 * walk the object chain at a map point, accumulate this, and stop once the
 * total reaches 15. 0x0043CF70 goes further and settles the vocabulary --
 * after the chain it adds 15 more for a tile whose ADDR_TILE_FLAGS byte has
 * 0x80 set, and 15 more again when two tile HEIGHTS differ by more than 16.
 * The same 15 and the same 16 this function uses, applied to the terrain.
 *
 * Its third argument is never read. Three independent callers push four
 * dwords, so the signature is four; the unused one is the original's.
 * Counter measured at 0 on a driven Boot Camp mission -- not blind, just not
 * reached, so it is verified by reading. */
#define ADDR_OBJ_BLOCK_WEIGHT      0x00448E60u /* int32_t(void*,void*,int32,uint32) */
#define AM2_BLOCK_FULL             15   /* the callers' own threshold */
/* A height difference LARGER than this blocks: both readers take the absolute
 * difference of two tile heights and add AM2_BLOCK_FULL when it exceeds this.
 * The comment here used to say "a step this size stops it blocking", which is
 * the polarity backwards; BlockWeightAt and BlockWeightDamaging agree. */
#define AM2_BLOCK_HEIGHT_STEP      0x10
/* 0x00448F00, three callers, 176 bytes. The TOTAL obstruction between an
 * object and a map point: every object standing at that point through
 * ADDR_OBJ_BLOCK_WEIGHT, then the tile's own blocking bit, then a height step
 * between the two tiles. Reconstructed.
 *
 * The two tile-indexed byte tables it consults are ADDR_TILE_ATTRS, which is
 * the height, and ADDR_TILE_FLAGS below. Runs eight times on a driven Boot
 * Camp mission, with the object loop never entered. */
#define ADDR_BLOCK_WEIGHT_AT       0x00448F00u /* int32_t(void*,uint32,uint32) */
/* The byte table beside ADDR_TILE_ATTRS and indexed the same way, by tile
 * index. 23 sites; one allocates it, width times height, and the rest read or
 * OR into it. Bits 0x01, 0x04, 0x08 and 0x80 are in use, and 0x80 is set by
 * 0x0042BD9E for a tile whose ADDR_MAP_TILES neighbour reads 15 or more. The
 * name is ours and says only what the table is indexed by. */
#define ADDR_TILE_FLAGS            0x00514ED0u /* uint8_t *, one per tile index */
#define AM2_TILE_BLOCKS            0x80u  /* the bit BlockWeightAt reads */
/* Bit 0, and its polarity is the OPPOSITE of AM2_TILE_BLOCKS: BlockWeightChain
 * penalises a tile whose bit 0 is CLEAR. So the two bits are asked different
 * questions -- 0x80 set blocks, 0x01 clear blocks -- and the reading that bit
 * 0 means "open" follows from that polarity and from nothing else. Eleven of
 * the 23 sites on ADDR_TILE_FLAGS test this bit. */
#define AM2_TILE_OPEN              0x01u
/* Bits 2 and 3, and MarkOpenTile (0x0043A4F0) is the only thing that sets
 * either. It ORs 0x04 into a tile with no WEIGHTED neighbour and 0x08 into one
 * with fewer than two COVERED neighbours, over the same twenty deltas the
 * cover pair walks.
 *
 * FINDPATH IS WHAT READS THEM, and the polarity is the point: a CLEAR
 * NO_WEIGHT_NEAR costs a tile 3 and a clear LITTLE_COVER_NEAR costs it 1,
 * because the bits are set for the ABSENCE of the thing they name. So the
 * tile-level A* steers units into the open and away from cover, three times
 * as hard away from obstacles as from cover. The names still say what sets
 * them; what uses them is no longer unknown. */
/* Bit 1, and SealMapEdges is its only writer: set for every tile OUTSIDE a
 * five-tile margin. Its margin is computed from the map's HEIGHT on both
 * axes -- see that function -- so on a non-square map the x band is wrong. */
#define AM2_TILE_NEAR_EDGE         0x02u
#define AM2_TILE_NO_WEIGHT_NEAR    0x04u
#define AM2_TILE_LITTLE_COVER_NEAR 0x08u
/* 0x0045B690, two callers, 112 bytes. The same accumulation as
 * ADDR_BLOCK_WEIGHT_AT with two differences: the object chain is GIVEN rather
 * than queried -- both callers run ADDR_OBJECTS_AT_POINT themselves -- and the
 * terrain term is AM2_TILE_OPEN rather than AM2_TILE_BLOCKS, with no height
 * step at all. Reconstructed.
 *
 * One caller passes a literal 0 as the object, so the no-viewer arm of
 * ADDR_OBJ_BLOCK_WEIGHT is reached from here and not only in principle. The
 * other picks between this and 0x0045B7E0 on a value being 5. Counter measured
 * at 0 -- not blind, just unreached -- so it is verified by reading. */
/* 0x00448FB0, three call sites, all inside ADDR_UPDATE_TROOPER_ACTION. The sixth member
 * of the block-weight family: the walk inlined WITH BlockWeightAt's height
 * step, no trooper arm, a strictly-greater distance test where Troops uses >=,
 * and a fourth argument that is an OUT-POINTER rather than the family's `ref`
 * -- it reports whether the object's current route leg falls inside anything,
 * and on saturation the route advances or is dropped. */
#define ADDR_BLOCK_WEIGHT_ROUTE  0x00448FB0u  /* int32(obj,at,chain,int32*) */
#define ADDR_BLOCK_WEIGHT_CHAIN    0x0045B690u /* int32_t(void*,uint32,void*,uint32) */
/* The UID REMAP TABLE, and what it is comes from the one function that READS
 * it. 0x004276F0 walks a unit's six UNIT_OFF_INVENTORY slots and, for each,
 * scans this table for a record whose first dword equals the uid the slot
 * holds, then writes the record's SECOND dword back into the slot. So the
 * records are (from, to) pairs of uids and the table is a rename map -- which
 * is what a load needs, since a saved uid means nothing in the new session.
 *
 * A growable array in the shape this image uses everywhere: capacity, count,
 * and a pointer to `capacity` records of eight bytes, grown ten records at a
 * time. Reading it as {count, capacity, data} instead would be invisible until
 * the first realloc; the compare at 0x0042768B is `count < capacity`, which is
 * what settles the order. Both halves reconstructed. */
#define ADDR_UID_REMAP_CAP         0x00513080u  /* int32_t, records allocated */
#define ADDR_UID_REMAP_COUNT       0x00513084u  /* int32_t, records used */
#define ADDR_UID_REMAP             0x00513088u  /* uint32_t (*)[2] */
/* 0x004276F0, one caller. Walk every registered object and put each type 2's
 * six inventory uids through the remap table -- the savegame fixup, gated on
 * a flag the loader sets and this clears. */
#define ADDR_REMAP_INVENTORY_UIDS  0x004276F0u  /* void(void) */
#define OBJ_FLAG_NEEDS_REMAP       0x04000000u
/* Bit 24. ObjCollidesWith is the only reader found: an object carrying it is
 * collided with unconditionally by a vehicle of kind 1 or 2, before any of
 * the type or alliance questions. Named structurally -- nothing yet says what
 * it means, only when it matters. */
#define OBJ_FLAG_BIT24             0x01000000u
/* The other flag RemapInventoryUids clears is OBJ_FLAG_SELECTED, further
 * down and named long ago -- a freshly loaded object is not selected. */
#define AM2_UID_REMAP_GROW         10   /* records added per grow */
/* 0x00427650, two callers -- both in 0x00457370's band, which is where a load
 * puts the table back. Free the records and zero all three globals. */
#define ADDR_UID_REMAP_CLEAR       0x00427650u  /* void(void) */
/* 0x00427680, two callers. Append one (from, to) pair, growing first when the
 * table is full. Both call sites build a replacement object and pass
 * (old->uid, new->uid), which is what fixes the pair order. Counters measured
 * at 0 on Boot Camp and on the campaign -- not blind, just unreached. */
#define ADDR_UID_REMAP_ADD         0x00427680u  /* void(uint32 from, uint32 to) */
/* A unit's weapon inventory: six uids, the one in hand, and a spare field the
 * removal always clears. */
#define UNIT_OFF_INVENTORY        0x54Cu  /* int32_t[6], uids */
#define UNIT_OFF_INVENTORY_LAST   0x560u  /* the sixth entry */
#define UNIT_OFF_INVENTORY_SEL    0x568u  /* int32_t, which slot is in hand */
/* 0x004045E0, 336 bytes, three callers -- read but not reconstructed. Fill a
 * six-dword struct describing the weapon a unit is HOLDING: take
 * UNIT_OFF_INVENTORY[UNIT_OFF_INVENTORY_SEL], resolve it to an object, and
 * write {weapon, kind, damage, min, max, ready} into the caller's buffer at
 * +0x40..+0x54. A null weapon zeroes the first, the fourth and the sixth and
 * returns, which is the shape to reproduce first.
 *
 * Kind 0x2B takes a fixed range of (r - 4, r + 2); a zero range gives 0x1000
 * both ways; everything else runs the range through three doubles at
 * 0x0046F2E0..0x0046F2F0 on the x87 stack, with kind 3 scaling the input
 * first. Kind 3 also computes `ready` from ADDR_GAME_CLOCK_MS against the
 * weapon's +0xC4, as an sbb/neg pair -- a comparison written as arithmetic. */
#define ADDR_UNIT_WEAPON_INFO     0x004045E0u  /* void(unit, sightc) */
/* Its three range constants and the two kinds it special-cases. The band is
 * plus or minus ten percent of the record's nominal range, and kind 3 scales
 * the range up by a fifth before spreading it. */
#define ADDR_WEAPON_RANGE_LO      0x0046F2E8u  /* double, 0.9 */
#define ADDR_WEAPON_RANGE_HI      0x0046F2E0u  /* double, 1.1 */
#define ADDR_WEAPON_RANGE_K3      0x0046F2F0u  /* double, 1.2 */
#define AM2_WEAPON_KIND_FIXED     0x2B  /* range is (r - 4, r + 2) flat */
#define AM2_WEAPON_KIND_TIMED     3     /* cooldown unscaled by rank */
#define AM2_WEAPON_RANGE_NONE     0x1000
/* The weapon HANDLER table and the four globals SelectInventorySlot installs
 * out of it. Each record is 16 bytes and the index is the first dword of the
 * weapon's OBJ_OFF_FIELD_C0, so that field is a pointer to a type record
 * rather than the scalar its structural name suggests.
 *
 * ONLY THE FIRST TWO FIELDS ARE FUNCTION POINTERS, and this said all four were
 * -- "established by the readers, which do `mov eax,[global]; test eax,eax;
 * call eax`". That is true of SLOT0 and SLOT1 and of neither of the others.
 * SLOT2's readers do `test eax,eax; jl`, which is a SIGNED test and is not a
 * question you can ask a function pointer; SLOT3's treat it as a flag. The
 * table agrees: columns 2 and 3 hold small integers, column 2 defaulting to -1
 * and column 3 to 0, with values like 3, 0x0E, 0x0F, 0x10 and 1. Two of the
 * four names were generalised from the two readers that were looked at.
 *
 * THE MAPPING IS NOT SEQUENTIAL, which is the one thing worth getting right
 * here: slot 2 goes to 0x005122F0 and slot 3 to 0x005122DC. The globals are
 * not contiguous -- 0x005122E0..EC sit between them, and at least 0x005122E0
 * is another handler this function does not write -- so reading the four
 * stores as "in order" swaps the last two. Named by SLOT so the swap is
 * visible at the use site. */
#define ADDR_WEAPON_HANDLERS     0x00489880u  /* 16-byte records, 4 fns each */
/* The soldier CLASS NAMES, indexed by the weapon code -- "Rifleman",
 * "Grenadier", "Flamethrower", "Bazookaman", "Mortarman", "Machine Gunner" --
 * with "Sarge" in the entry immediately BEFORE the table, which 0x0044BAF0
 * reaches when OBJ_OFF_SARGE is set. Codes 0, 6, 7 and 8 point at
 * ADDR_DIR_SCRATCH instead of a literal, so those have no fixed name.
 *
 * THIS IS WHAT SETTLES OBJ_OFF_SARGE. ObjType2Field548's comment says only
 * "the dword at +0x548, but only for a type 2", and LookupOwnerObj's says
 * "`ArmyLeader` was the name I nearly gave it. What the +0x548 test means is
 * not established, so the claim is not made." It is established now: a unit
 * with that field set is called Sarge, so LookupOwnerObj really does find the
 * army's leader. */
#define ADDR_UNIT_NAME_SARGE     0x00489B40u  /* the entry before the table */
#define ADDR_UNIT_CLASS_NAMES    0x00489B44u  /* const char *[] by weapon code */
#define OBJ_OFF_SARGE            0x548u
/* 0x0044BAF0, one caller. A unit's class name: "Sarge" when OBJ_OFF_SARGE is
 * set, otherwise the name for the code of the weapon it holds, and entry 0
 * when it holds none. Reconstructed. */
#define ADDR_UNIT_CLASS_NAME     0x0044BAF0u  /* const char *(void *unit) */
/* The moment an item may be picked up again, stamped by the pickup path --
 * TrooperPickupItem writes ADDR_GAME_CLOCK_MS plus two seconds here. It is
 * read by exactly one thing, ADDR_CAN_PICK_UP, which refuses until it has
 * passed. Two functions, one field, and the pair is what names it. */
#define OBJ_OFF_PICKUP_AFTER     0xC8u
/* 0x004337C0, one caller. Whether an item can be picked up: it must be a
 * weapon, its OBJ_OFF_PICKUP_AFTER must have passed, and the code its
 * OBJ_OFF_FIELD_C0 record holds must be 0x1F, 0x20 or 0x21. Reconstructed. */
#define ADDR_CAN_PICK_UP         0x004337C0u  /* int32_t(void *obj) */
/* 0x00402F00, one caller. A random value that AVERAGES its first argument:
 * `100 - spread` draws of `rand() % (centre * 2)`, divided by that count. A
 * spread of 0 short-circuits and returns the centre unchanged; a spread of 100
 * or more clamps the count to one draw, which is the noisiest it gets. So the
 * argument is an inverse tightness and not a range. Reconstructed. */
#define ADDR_RANDOM_AROUND       0x00402F00u  /* int32(centre, spread) */
#define ADDR_WEAPON_FN_SLOT0     0x005122D4u
#define ADDR_WEAPON_FN_SLOT1     0x005122D8u
#define ADDR_WEAPON_FN_SLOT2     0x005122F0u  /* int32_t, -1 by default */
#define ADDR_WEAPON_FN_SLOT3     0x005122DCu  /* int32_t, a 0/1 flag */
/* Who selected, and which slot. Written here and by 0x00424F20, read by the
 * HUD and the two 0x458xxx sites. */
#define ADDR_WEAPON_OWNER_ID     0x00511E58u  /* uint32_t, the unit's uid */
#define ADDR_WEAPON_SLOT         0x00511E5Cu  /* int32_t */
/* 0x00446E70, six call sites and FIVE TABLE SLOTS -- it is column 1 of the
 * weapon handler records at 0x00489A00, A10, A20, AF0 and B00, so several
 * weapon kinds share it. Column 1 is what ADDR_WEAPON_FN_SLOT1 receives, and
 * that global is called as `(object, packed point)` -- the same shape
 * ADDR_POINTER_ACTION has.
 *
 * It records a fire request on the unit ADDR_WEAPON_OWNER_ID names: a block of
 * fields at +0x57C..+0x598, filled one way for a target OBJECT and another for
 * a bare POINT. Nothing here fires anything; the block is for whoever reads it
 * next. Reconstructed, and measured at 0: its six call sites are the
 * pointer-mode action paths, and no drive here installs a mode above 0. */
#define ADDR_SET_WEAPON_TARGET   0x00446E70u  /* void(void *obj, uint32 at) */
#define UNIT_OFF_FIRE_ACTIVE     0x57Cu
#define UNIT_OFF_FIRE_F40        0x580u   /* uint8_t, a copy of the unit's +0x40 */
#define UNIT_OFF_FIRE_MODE       0x584u   /* the pose, or 0x1F for a point */
/* Stamped with ADDR_GAME_CLOCK_MS by TroopSubParse whenever it changes either
 * of the two fields above, alongside setting UNIT_OFF_FIRE_ACTIVE. */
#define UNIT_OFF_FIRE_STAMP      0x5A0u   /* uint32_t */
/* TroopSubParse keeps UNIT_OFF_FIRE_F588 and _F58C only for fire modes
 * 0x1C..0x1E and clears both for anything else -- including the 0x1F that
 * means "a point". So that pair belongs to three modes and to no others. */
#define AM2_FIRE_MODE_KEEPS_LO   0x1C
#define AM2_FIRE_MODE_KEEPS_HI   0x1E
/* The header word TroopSubParse reads is BIG-ENDIAN, byte by byte, and splits
 * into a 29-bit uid and three flag bits -- one per optional field. The army
 * is then shifted into those same top three bits to make the lookup key. */
#define AM2_TROOPSUB_UID_MASK    0x1FFFFFFFu
#define AM2_TROOPSUB_ARMY_SHIFT  29
#define AM2_TROOPSUB_HAS_POS     0x80000000u
#define AM2_TROOPSUB_HAS_FACING  0x40000000u
#define AM2_TROOPSUB_HAS_MODE    0x20000000u
/* 0x00447950, one caller. Choose a unit's UNIT_OFF_FIRE_MODE between two more
 * values: 0x25 when it has OBJ_OFF_FIELD_5A4 and its OBJ_OFF_DEADLINE_58 is
 * more than fifteen seconds behind the game clock, and 1 otherwise. So that
 * field carries at least four different things -- a pose, 0x1F for a point,
 * and these two -- which is why it is named for the field and not for a
 * meaning. Reconstructed. */
#define ADDR_PICK_FIRE_MODE      0x00447950u  /* void(void *obj) */
#define AM2_FIRE_MODE_STALE      0x25
#define AM2_FIRE_MODE_FRESH      1
#define AM2_FIRE_STALE_MS        0x3A98   /* 15000 */
#define UNIT_OFF_FIRE_F588       0x588u
#define UNIT_OFF_FIRE_F58C       0x58Cu
#define UNIT_OFF_FIRE_X          0x590u   /* int16_t */
#define UNIT_OFF_FIRE_Y          0x592u   /* int16_t */
#define UNIT_OFF_FIRE_Z          0x594u   /* int16_t, always written zero */
#define UNIT_OFF_FIRE_UID        0x598u   /* the target's uid, or 0 */
/* The WEAPON's uid, written only by RecvTrooperFire -- and what settles it is
 * that the same value is handed to WeaponByUid and then searched for among
 * the six UNIT_OFF_INVENTORY slots. Two uses, both weapon-shaped. */
#define UNIT_OFF_FIRE_WEAPON_UID 0x59Cu
#define AM2_FIRE_MODE_POINT      0x1F
#define AM2_INVENTORY_SLOTS       6
#define AM2_OBJ_KIND_WEAPON       4

/* The object-script count, and what gets written into every object the
 * statement selects -- read AFTER ADDR_NEW_OBJ_SCRIPT has incremented it. */
#define ADDR_CURRENT_OBJ_SCRIPT   0x0051618Cu
#define AM2_OBJ_SCRIPT            0xB0u
#define AM2_OBJ_SCRIPT_PC         0xB4u
#define AM2_OBJ_SCRIPT_WAIT       0xB8u
#define AM2_OBJ_SCRIPT_STATE      0xBCu

/* The handlers describe their own statements, in their own error messages.
 * The pad handler carries "Duplicate pad name.", "Illegal Pad Number",
 * "Unexpected symbol in pad definition should be '<=>'" and, best of all,
 * "Pad can't have both specific item and generic trigger" -- so a pad is a
 * unique name, a range-checked number, and EITHER a specific item OR a
 * generic trigger, optionally with a comparison and a count. That is the
 * syntax the missions use, read out of the binary rather than inferred from
 * the examples.
 *
 * It also reports the token KIND by name when it rejects something --
 * "'%s' found, but expected token of type %s" -- which is what the array at
 * 0x00487C74 is for.
 *
 * The others do the same. `if` carries "Missing 'after' in if-statement.",
 * "Missing 'of' in if-repeat statement.", "Missing 'then' in if-statement.",
 * "Incomplete testvar clause.", "Unrecognized operator in testvar clause."
 * and "TIMEABSOLUTE time must be positive" -- with "Exptected" misspelled,
 * which is a good sign these are verbatim. `variable` carries "Duplicate
 * variable name."
 *
 * `object` carries "Invalid token in GenerateObjScriptFromTokens", which is a
 * FUNCTION NAME out of the original source -- the only one recovered so far
 * that was not inferred. */

#define ADDR_SCRIPT_CONTEXT      0x00656478u  /* what reset and parse are given */
/* The path ADDR_TAKE_MENU_REQUEST re-reads on a script reload -- the level's
 * own script file, kept so the reload does not have to rebuild the name. */
#define ADDR_SCRIPT_RELOAD_PATH  0x00511BC8u  /* char[] */
#define ADDR_MAP_NAME            0x00511A88u  /* char[], "kitchen" */
#define ADDR_LEVEL_INDEX         0x00511D9Cu  /* int32_t; 0 means "<map>.txt" */

/* LoadLevelScript's world. */
/* Returns int32_t, not void -- 1 if it got there and 0 if it did not. The
 * declaration here said void for as long as script.cpp was the only caller
 * that mattered, because that one ignores the answer. */
#define ADDR_SET_DATA_DIR        0x00422DE0u  /* int32_t(const char *subdir) */
#define ADDR_FILE_EXISTS         0x00422D80u  /* int32_t(const char *) */
#define ADDR_RULES_CHECKSUM      0x004303B0u  /* uint32_t(void) */
#define ADDR_MP_SCRIPT_CHECKSUM  0x00430400u  /* uint32_t(void) */
#define ADDR_MAP_CHECKSUM        0x00430450u  /* uint32_t(void) */
#define ADDR_AMM_CHECKSUM        0x0042C350u  /* uint32_t(const char *map) */
/* The three IFF tags it walks, in the order it requires them. */
#define AM2_IFF_FORM             0x4D524F46u  /* 'FORM' */
#define AM2_IFF_MAP              0x2050414Du  /* 'MAP ' */
#define AM2_IFF_CSUM            0x4D555343u  /* 'CSUM' */
#define AM2_IFF_TILE             0x454C4954u  /* 'TILE' */
#define AM2_AMM_NAME_BYTES       0x40u
/* NOT ADDR_SCRIPT_FIND_NAME, which is 0x0043F670 over a different table.
 * This one lower-cases its argument IN PLACE and searches the second of the
 * two triples FreeLevelTables owns -- the one at ADDR_NAME_TABLE_BASE, loaded
 * from the same `.txt` as the level records by the same reader.
 *
 * ITS RECORDS ARE 0xCC BYTES and the LEVEL records are 0x30C, which is what
 * stops the two tables being one thing under two names -- I renamed this to
 * `FindLevelByName` on the strength of the "same reader" note and the
 * redefinition of AM2_LEVEL_RECORD_SIZE is what caught it. Two tables, two
 * strides, one file.
 *
 * ITS RETURN IS THE RECORD, not an index or a boolean: the tail computes
 * `base + i * 0xCC`. The old macro typed it `int32_t`, which worked only
 * because its one reconstructed caller tests it for truth.
 *
 * It builds the table lazily -- a zero count calls ADDR_READ_MP_MAPS first,
 * so the first search is also the load. */
#define ADDR_SCRIPT_LIST_FIND    0x0043E900u  /* void *(char *name) */
#define AM2_NAME_RECORD_SIZE     0xCCu
/* AND THE 0xCC IS ACCOUNTED FOR, FIELD BY FIELD. The `rules` line handler
 * zeroes 0x33 dwords and writes three tokens at +0, +0x40 and +0x80; the
 * appender the `rulemap` handler calls uses +0xC0, +0xC4 and +0xC8 as a
 * {capacity, count, items} triple over 0x40-byte entries. 3 * 0x40 + 12 is
 * 0xCC exactly, which is the same kind of proof a file format gives when it
 * consumes its input to the last byte -- a mis-sized field could not tile.
 *
 * ScriptListFind compares against +0, so NAME is the searchable one. The
 * other two are what the `rules` line carries beside it, the third
 * TitleCaseName'd on the way in.
 *
 * 0x40 IS AM2_AMM_NAME_BYTES' 0x40 and AM2_SCRIPT_UNIQUE_BUF's, grepped
 * before this name went in. Three buffers of one size in three structures;
 * one name each, not one name shared. */
#define NAMEREC_OFF_NAME         0x00u
#define NAMEREC_OFF_NAME2        0x40u
#define NAMEREC_OFF_NAME3        0x80u
#define NAMEREC_OFF_CAP          0xC0u
#define NAMEREC_OFF_COUNT        0xC4u
#define NAMEREC_OFF_MAPS         0xC8u
#define AM2_NAMEREC_FIELD        0x40u
/* 0x0043EA30, one caller -- the `rulemap` handler below. Append one 0x40-byte
 * name to a name record's own list, on the SAME twelve-then-six policy
 * AddLevelRecord and AddNameRecord use for the two tables above; the
 * constants are AM2_LEVEL_TABLE_FIRST and AM2_LEVEL_TABLE_GROW and this is
 * the third user of them. Reconstructed. */
#define ADDR_NAMEREC_ADD_MAP     0x0043EA30u  /* void(rec, const char *) */
/* The two `.txt` line handlers beside it, and their names are the PROGRAM'S:
 * the keyword table at 0x00477448 is {name, value, handler} triples and these
 * two are `rules` (value 0x61) and `rulemap` (0x62), with `link` -> the LINK
 * parser and `place` -> ADDR_PARSE_PLACE_LINE either side of them as the two
 * anchors that fix the layout. A first reading took the rows four bytes out
 * and made `map` the LINK parser's keyword; those two anchors are what said
 * so. Both reconstructed. */
#define ADDR_DEF_RULES_LINE      0x0043EAC0u  /* int32(cmd, char *line) */
#define ADDR_DEF_RULEMAP_LINE    0x0043EBD0u  /* int32(cmd, char *line) */
/* 0x0043E9A0, one caller -- AddLevelRecord's twin for the OTHER table the same
 * `.txt` fills, with 0xCC-byte records instead of 0x30C-byte ones. Same first
 * twelve, same growth of six, same absence of any allocation check. */
#define ADDR_ADD_NAME_RECORD     0x0043E9A0u  /* void(const void *record) */
/* FileExists opens "r" where the checksum opens ADDR_MODE_RB, which on the
 * platform this came from is not a distinction without a difference. */
#define ADDR_STR_MODE_R          0x00478950u  /* "r" */
#define ADDR_STR_RULES_DIR       0x004774ACu  /* "rules" */
#define ADDR_STR_AAI_DIR         0x00473D9Cu  /* "aai" */
#define ADDR_FMT_DOT_TXT         0x00485178u  /* "%s.txt" */
#define ADDR_FMT_DOT_AMM         0x00486330u  /* "%s.amm" */
#define ADDR_FMT_PREV_BMP        0x004870A8u  /* "%s_prev.bmp" */
#define ADDR_STR_GAME_AAI        0x004870E8u  /* "game.aai" */
#define ADDR_STR_OBJECT_AAI      0x004870DCu  /* "object.aai" */
#define ADDR_STR_TROOP_AAI       0x004870D0u  /* "troop.aai" */
#define ADDR_STR_VEHICLE_AAI     0x00473D64u  /* "vehicle.aai" */
/* The image carries the .aai names TWICE. The lowercase set above, at
 * 0x004870xx, and the mixed-case set LoadDefTables uses, here -- and the two
 * are not interchangeable, since the loader passes these literals straight to
 * the CRT's fopen. Note ADDR_STR_VEHICLE_AAI is in THIS set even though it is
 * spelled lowercase; only four of the five needed a new name.
 *
 * Found by the compiler, not by grepping: the addresses were clean and the
 * NAMES were taken. Grep both before defining one. */
#define ADDR_STR_DEF_TROOP_AAI   0x00473D90u  /* "Troop.aai" */
#define ADDR_STR_DEF_WEAPON_AAI  0x00473D70u  /* "Weapon.aai" */
#define ADDR_STR_DEF_OBJECT_AAI  0x00473D58u  /* "Object.aai" */
#define ADDR_STR_DEF_GAME_AAI    0x00473D4Cu  /* "Game.aai" */
/* 0x00403400, one caller -- the level load, which also builds the HUD. It
 * names itself through the five files it parses. Free every definition table,
 * SetDataDir("aai"), parse the five, then SetDataDir(ADDR_MAP_FOLDER) and
 * parse Object.aai AGAIN -- so a tileset directory may override the global
 * object definitions. Then DefFinish, and rebuild two runtime tables.
 *
 * Both rebuilds PERMUTE, which is the part a memcpy would silently destroy.
 * The missile copy moves five of thirteen fields (0x10 and 0x14 swap;
 * 0x1C -> 0x24 -> 0x20 -> 0x1C), and the rank copy is a projection of a
 * 0x20-byte parsed record onto a 28-byte runtime one in which NOTHING stays
 * in place. Each destination entry is cleared immediately before it is
 * filled, not once up front, so a definition the bsearch cannot find reads as
 * zeros rather than as the previous level's.
 *
 * The rank permutation is confirmed rather than asserted: the one field it
 * computes as a float lands on RANK_REC_OFF_FIRE_SCALE, which is the record's
 * only float, and the value is a percentage scaled by 0.01 with a floor of
 * 1.0. */
#define ADDR_LOAD_DEF_TABLES     0x00403400u  /* void(void) */
#define ADDR_MISSILE_DEF_FIND    0x004602C0u  /* rec *(int32 id), a bsearch */
/* RENAMED INTO THE FAMILY IT BELONGS TO. It was ADDR_RANK_DEF_FIND, named
 * from its one caller -- LoadDefTables' rank loop -- while the three other
 * functions on the same table are DEF_ADD_TROOPER_REC, DEF_SORT_TROOPER_RECS
 * and DEF_FREE_TROOPER_RECS. Add, sort, find, free, and one of the four was
 * spelled in a different vocabulary. Renamed, not aliased. */
#define ADDR_DEF_FIND_TROOPER_REC 0x0044CD70u  /* rec *(int32 level), bsearch */
/* AND I WAS ABOUT TO RENAME THE OTHER FOUR THE OTHER WAY. Nothing but this
 * bsearch's caller reads the table, that caller fills ADDR_RANK_RECORDS with
 * it for ids 0..7, and DefAddTrooperRec appends exactly EIGHT records on a
 * real drive -- which reads as "the table is rank records and `trooper` is a
 * call-site guess". It is not a guess. The .aai keyword table at
 * ADDR_DEF_NAME_TABLE has eight entries pointing at the parser above,
 * `trooperlevel1` through `trooperlevel8`, values 45..52. So TROOPER is the
 * PROGRAM'S OWN WORD for these records and rank is ours.
 *
 * That is the ReadScript lesson pointing the opposite way: there, prose kept
 * a name I invented over one the image supplies. Here I nearly replaced the
 * image's word with mine on evidence that was entirely consistent with it.
 * Dumping the keyword table cost one command. */
/* The loop ends at 0x00662920, which is already ADDR_RESPAWN_KINDS -- the two
 * tables TILE, so the bound is a count rather than a second name for that
 * address. (0x662920 - ADDR_MISSILE_DEFS) / AM2_MISSILE_DEF_BYTES == 44.) */
#define AM2_MISSILE_DEF_COUNT    44
#define AM2_RANK_COUNT           8
/* The parsed record is 8 dwords and its size is AM2_DEF_TROOPER_REC_SIZE,
 * which is what the four functions on that table already use. AM2_RANK_SRC_BYTES
 * was a second spelling of the same 0x20 defined here and used nowhere;
 * removed rather than left as the kind of duplicate this file keeps finding. */
#define ADDR_F_ONE_HUNDREDTH     0x0046F2DCu  /* float 0.01 */
#define ADDR_F_ONE               0x0046F2D8u  /* float 1.0 */
/* The parsed rank record, keyed at +0 and projected onto the runtime one. */
#define RANK_SRC_OFF_MAX_HEALTH  0x04u
#define RANK_SRC_OFF_FIELD_04    0x08u
#define RANK_SRC_OFF_SIGHT_RANGE 0x0Cu
#define RANK_SRC_OFF_FIELD_08    0x10u
#define RANK_SRC_OFF_FIRE_PCT    0x14u
#define RANK_SRC_OFF_THRESHOLD   0x18u
#define RANK_SRC_OFF_XP          0x1Cu
#define ADDR_STR_WEAPON_AAI      0x004870C4u  /* "weapon.aai" */
#define ADDR_RULES_CHECKSUM_VAL  0x00511CD4u  /* uint32_t, the rules total */
#define ADDR_MP_SCRIPT_CHKSUM_VAL 0x00511CD0u /* uint32_t */
#define ADDR_MAP_CHECKSUM_VAL    0x00511CCCu  /* uint32_t */
/* 0x00454AD0, three callers, thiscall. Give the multiplayer map preview a new
 * bitmap: release whatever it is holding, load the named one, and store it in
 * BOTH its own slot and the widget base's sprite -- the second is what makes
 * WidgetPaint draw it, the first is what the release above finds next time.
 * Two fields, one pointer, and the class owns it. */
#define ADDR_MP_PREVIEW_SETBITMAP 0x00454AD0u /* thiscall(void *, const char *) */
#define PREVIEW_OFF_SPRITE       0x58   /* AM2_Sprite *, the class's own copy */
#define PREVIEW_OFF_FLAGS        0x5C   /* int32_t, passed to the loader */
#define ADDR_SHOW_BAD_PREVIEW    0x00430330u  /* void(AM2_Widget *) */
#define ADDR_STR_BAD_MP_PREV     0x004870B4u  /* "bad_mp_prev.bmp" */
/* Was ADDR_MP_PANEL_REFRESH, which is where OpenMpHost calls it and not what
 * it does: three of its four callers have no panel at all. Renamed, not
 * aliased. */
#define ADDR_REFRESH_MAP_SEL     0x004301D0u  /* void(void) */
#define ADDR_FILL_LIST_FROM_RULES 0x00430140u /* void(const char *, void *) */
#define ADDR_GAME_DIR            0x0051235Cu  /* char[], the install directory */
#define ADDR_STR_AVI_DIR         0x004852C8u  /* const char **, -> "avi" */
/* The movie names MovieBuildName knows, and the two suffixes it appends. The
 * four it compares against are the ones that are ALREADY the right file: a
 * name not among them gets "sml" when the small set is in use.
 *
 * Its four call sites pass "3do", "act1", "credits" and ADDR_MOVIE_TO_PLAY,
 * the buffer a mission's script fills -- so "act2" and "portal" are reached from
 * script data and not from any literal in the image. */
#define ADDR_STR_MOVIE_3DO       0x0048658Cu  /* "3do" */
#define ADDR_STR_MOVIE_CREDITS   0x00486584u  /* "credits" */
#define ADDR_STR_MOVIE_ACT2      0x004865A4u  /* "act2" */
#define ADDR_STR_MOVIE_PORTAL    0x0048659Cu  /* "portal" */
#define ADDR_STR_MOVIE_SMALL     0x00486598u  /* "sml" */
#define ADDR_STR_MOVIE_EXT       0x00486590u  /* ".smk" */
/* operator new's argument in PlayMovie, and the volume floor below which the
 * film is played silent. */
#define AM2_MOVIE_SIZE           0xC4
#define AM2_MOVIE_VOLUME_FLOOR   (-2000)
#define ADDR_STR_PATH_SEP        0x00478984u  /* "\\" */
#define ADDR_MAP_FOLDER          0x00511AC8u  /* the map's own directory */
#define ADDR_RULES_DIR_STR       0x00485110u  /* a `char *` to "rules"    */
#define ADDR_SCORE_LIMIT         0x00515FF0u  /* seeds `gamescorelimit`   */
/* The two lobby fields that commit a typed number, 0x004322B0 and 0x004322E0,
 * and they are the same forty-eight bytes twice over: read the edit child's
 * text, atoi it, store it, and broadcast with SendPlayerMsg. Both are gated on
 * COMM_OFF_IS_HOST, so a client typing in the box changes nothing -- which is
 * what makes them a matched pair rather than one function with a flag. The
 * only difference is where the number lands: ADDR_SCORE_LIMIT for one, and
 * ADDR_ARMY_POINTS indexed by the widget's own row for the other.
 *
 * orig.h already recorded that "0x00431E10 sets it from a lobby field through
 * atoi"; this is that field, and the entry at 0x00431E10 is the panel holding
 * it. */
#define ADDR_MP_COMMIT_SCORE     0x004322B0u  /* void(AM2_Widget *) */
#define ADDR_MP_COMMIT_POINTS    0x004322E0u  /* void(AM2_Widget *) */
#define MPFIELD_OFF_EDIT         0x58u  /* the edit child holding the digits */
#define MPFIELD_OFF_ROW          0x70u  /* which army, for the points field */
/* The SPINNER: an edit box showing a number with an up and a down arrow. Three
 * handlers share it -- 0x00456580 when a typed value is committed, 0x004565D0
 * and 0x00456660 for the two arrows -- and all three read the same five fields
 * off it, which is what makes them one class rather than three functions.
 *
 * The two arrows do the same thing in opposite directions and clamp against
 * opposite ends, and neither clamps the other way: up cannot go below the
 * minimum only because it was already at or above it. The commit handler is
 * the one that clamps BOTH ways, because it is the only one a user can hand an
 * arbitrary number to.
 *
 * And only the arrows repaint and play a sound; the commit does neither, so a
 * typed value that gets clamped shows its new value on the next repaint from
 * somewhere else. Reproduced. */
#define SPIN_OFF_EDIT            0x58u  /* the edit box child */
#define SPIN_OFF_MIN             0x64u
#define SPIN_OFF_MAX             0x68u
#define SPIN_OFF_STEP            0x6Cu  /* what one arrow press moves */
#define SPIN_OFF_HANDLER         0x80u  /* void(spinner *), or null */
/* Back-pointers to the spinner, and the two CHILDREN keep it at different
 * offsets -- the arrows at 0x78 and the edit at 0x7C. Read off the three
 * bodies; which field the constructor writes is not established here. */
#define SPINCHILD_OFF_SPIN_ARROW 0x78u
#define SPINCHILD_OFF_SPIN_EDIT  0x7Cu
#define ADDR_SPIN_COMMIT         0x00456580u  /* void(AM2_Widget *) */
#define ADDR_SPIN_UP             0x004565D0u
#define ADDR_SPIN_DOWN           0x00456660u
#define ADDR_DECLARE_RULE_VARS   0x00421C70u  /* greenwins, tanwins, ...  */

/* The five score variables' names, as `char *` in the image. Only the four
 * army ones keep the index AddNameTableName returns; `gamescorelimit`'s is
 * discarded, because scripts reach it by name and nothing updates it. */
#define ADDR_NAME_SCORE_LIMIT    0x00487C60u
#define ADDR_NAME_GREENSCORE     0x00487C64u
#define ADDR_NAME_TANSCORE       0x00487C68u
#define ADDR_NAME_BLUESCORE      0x00487C6Cu
#define ADDR_NAME_GREYSCORE      0x00487C70u
#define ADDR_SVAR_GREENSCORE     0x00656488u
#define ADDR_SVAR_TANSCORE       0x0065648Cu
#define ADDR_SVAR_BLUESCORE      0x00656490u
#define ADDR_SVAR_GREYSCORE      0x00656494u

/* One entry per player in the comm object, stride 0x70. */
#define AM2_PLAYER_STRIDE        0x70u
#define AM2_PLAYERS_MAX          4
#define AM2_PLAYER_ARMY          0x210u
/* 0x0040F190, 47 callers -- CommSlotForArmy's inverse, and the same special
 * case: slot 4 answers 4 without touching the object. Everything else reads
 * AM2_PLAYER_ARMY out of that slot's record. */
#define ADDR_COMM_ARMY_OF_SLOT   0x0040F190u /* thiscall int32(this, slot) */
#define AM2_PLAYER_ID            0x214u   /* the DirectPlay id; 0 or -1 is none */
/* 0x0040F2F0, one caller, thiscall and `ret 4`. AM2_PLAYER_ID of a slot, with
 * slot -1 answering 0 rather than indexing backwards. The stride comes out as
 * `(slot * 8 - slot) << 4`, which is AM2_PLAYER_STRIDE. Reconstructed. */
#define ADDR_COMM_PLAYER_ID      0x0040F2F0u  /* thiscall int32(comm, slot) */
/* Two per-player flags past AM2_PLAYER_ACTIVE, and their CALLER is what names
 * them. 0x00431850 tests AM2_PLAYER_AGREED across every player and, when one
 * says no, shows "Not everybody has the same version/map/rules." -- so that
 * flag is the checksum handshake's answer. Then it sets AM2_PLAYER_READY on
 * ADDR_DEFAULT_OWNER's own slot and tests it across every player, which is
 * what a ready-check is. Neither name comes from the fields themselves. */
#define AM2_PLAYER_READY         0x270u
#define AM2_PLAYER_AGREED        0x278u
/* 0x0040F8A0 and 0x0040F8E0, one caller each, thiscall and no stack argument.
 * The same loop over COMM_OFF_PLAYER_COUNT slots differing only in which flag
 * it tests: every player whose AM2_PLAYER_ID is neither 0 nor -1 must have it
 * set, and an empty table answers yes. Reconstructed; both measured at 0,
 * since their caller is on the multiplayer start path. */
#define ADDR_ALL_PLAYERS_AGREED  0x0040F8A0u  /* thiscall int32(comm) */
#define ADDR_ALL_PLAYERS_READY   0x0040F8E0u  /* thiscall int32(comm) */
#define AM2_PLAYER_ACTIVE        0x25Cu
/* Slot i's own index field, four bytes before its army. What
 * ADDR_COMM_FIND_PLAYER hands back rather than the loop counter. */
#define AM2_PLAYER_INDEX         0x20Cu
#define AM2_COMM_VERBOSE         0x418u   /* gates the per-script logging */
/* This went in as ADDR_COMM_PLAYER_IS_AI, read off the one call site that
 * skips an AI script when it answers yes -- and it means the opposite. The
 * field it tests is +0x214, which is the id ADDR_COMM_FIND_PLAYER scans for,
 * so a slot answers yes while a real networked player still holds it. That
 * also explains "Player %s has left the game - now AI": losing the player
 * clears the id, and the slot becomes the AI's. Fourth time a name has been
 * taken from a call site and been wrong. */
#define ADDR_COMM_SLOT_HAS_PLAYER 0x0040F200u /* thiscall int32_t(this, slot) */
#define ADDR_MP_SCRIPT_NAME      0x00511C08u  /* char[], the multiplayer script */

/* The three names that used to live here -- GREENSCORE at 0x0065648C and so on
 * -- were shifted by one. They were read as though each store followed its own
 * call, and the stores lag: `mov [0x656488], eax` sits after the SECOND
 * AddNameTableName, so 0x656488 holds greenscore and not gamescorelimit.
 * Five names are declared and four indices kept; the one dropped is
 * gamescorelimit's. See ADDR_SVAR_GREENSCORE above.
 */

/* Token kinds, from the name array at 0x00487C74 indexed by kind. The score
 * variables are declared as kind 3, which is Integer -- so the argument that
 * looked like a magic 3 is a type. */
#define AM2_TOKEN_UNKNOWN        0
#define AM2_TOKEN_CONTROL_CHAR   1
#define AM2_TOKEN_RESERVED       2
#define AM2_TOKEN_INTEGER        3
#define AM2_TOKEN_FLOAT          4
#define AM2_TOKEN_STRING         5
#define AM2_TOKEN_NAME           6

/* Kind 7 has no entry in the kind-name array at all -- index 7 there is the
 * keyword table's first row. Nothing the tokeniser produces carries it; it is
 * written by the statement handlers, which rewrite a String naming something
 * into a reference to the name table. ScriptTokenText resolves it. */
#define AM2_TOKEN_NAMEREF        7

/* Which form an `if` took, recorded in the condition's first field. Derived
 * from the arms, not from a table -- the keyword each corresponds to is in the
 * name. */
#define AM2_IF_PLAIN             0
#define AM2_IF_ALLOF             1
#define AM2_IF_INORDER           2
#define AM2_IF_COUNT             3
#define AM2_IF_REPEAT            4
#define AM2_IF_TIMEABSOLUTE      5
#define AM2_IF_AFTER             6
#define AM2_IF_BUTNOT_KEYWORD    7
#define AM2_IF_BUTNOT_STRING     8

/* Which of the two forms an object script was declared with. */
#define AM2_OBJSCRIPT_OBJECT     0
#define AM2_OBJSCRIPT_CLASS      1

/* What an `if` event term is, in the first of its three values.
 *
 * The whole enum is confirmed twice over: the parser assigns these codes, and
 * 0x0041F200 formats a placeholder name per kind and its strings line up one
 * for one -- Event_PadDeactivated for 2, Event_PadActivated for 3, and so on
 * down to Event_ItemDropped for 8. Two functions written for different
 * purposes agreeing is better evidence than either alone.
 *
 * Kind 0 was AM2_EVT_NAME here, inferred. The game's own string calls it
 * Event_Control, so it is AM2_EVT_CONTROL now -- the program's word beats
 * ours. Kind 1 is produced by nothing, and 0x0041F200 gives it an empty name
 * rather than a placeholder, which is the same fact from the other side. */
#define AM2_EVT_CONTROL          0   /* just an object, whatever happens  */
#define AM2_EVT_PADOFF           2
#define AM2_EVT_PADON            3
#define AM2_EVT_KILLED           4
#define AM2_EVT_HIT              5
#define AM2_EVT_HEALED           6
#define AM2_EVT_PICKEDUP         7
#define AM2_EVT_DROPPED          8

/* What one side of a `testvar` comparison is. */
#define AM2_VAL_LITERAL          0
#define AM2_VAL_VARIABLE         1
#define AM2_VAL_GETDMGLVL        2
#define AM2_VAL_GETHEALTH        3
#define AM2_VAL_GETDISGUISE      4
#define AM2_VAL_HASITEM          5
#define AM2_VAL_ISCOLORINGAME    6
#define AM2_VAL_ISALLY           7
#define AM2_VAL_TEAMSCORE        8

/* Who an `order` or `setaimode` is aimed at. */
#define AM2_ORDER_NAME           0   /* a named object                    */
#define AM2_ORDER_ARMY           1   /* a whole army                      */
#define AM2_ORDER_GROUP          2   /* an army's `group <n>`             */

/* What follows `then`. */
#define AM2_THEN_NONE            0
#define AM2_THEN_RANDOM          1
#define AM2_THEN_SEQUENTIAL      2
#define AM2_THEN_ONOBJSTATE      3

/* A `testvar` comparison operator. Note this is NOT the encoding a pad uses
 * for the same three of these -- a pad writes 0/1/2 for =/</> while testvar
 * writes 0/2/3, with 1 taken by <>. Two encodings for one idea, in one file. */
#define AM2_CMP_EQ               0
#define AM2_CMP_NE               1
#define AM2_CMP_LT               2
#define AM2_CMP_GT               3
#define AM2_CMP_LE               4
#define AM2_CMP_GE               5

/* A pad's comparison, and what ScriptCompare takes. */
#define AM2_PADCMP_EQ            0
#define AM2_PADCMP_LT            1
#define AM2_PADCMP_GT            2

/* Who a `hit`, `killed`, `healed`, `pickedup` or `dropped` event is about,
 * packed into the top bits of one value by ScriptHitTarget.
 *
 * The army bits are one each. The class bits are not: `sarge` is three bits
 * and `trooper` is two of those same three, so a trooper mask is a subset of a
 * sarge mask -- the scheme is a hierarchy rather than a set of flags, which is
 * why `sarge` matching implies `trooper` matching and not the other way round.
 *
 * ALL_ARMIES is what an unrecognised army word gives, and it is exactly the
 * base bit with all four armies OR'd in. */
#define AM2_HIT_ANY          0x80000000u
#define AM2_HIT_GREEN        0x40000000u
#define AM2_HIT_TAN          0x20000000u
#define AM2_HIT_BLUE         0x10000000u
#define AM2_HIT_GREY         0x08000000u
#define AM2_HIT_ALL_ARMIES   0xF8000000u
#define AM2_HIT_ITEM         0x04000000u
#define AM2_HIT_SARGE        0x01C00000u
#define AM2_HIT_TROOPER      0x01400000u
#define AM2_HIT_VEHICLE      0x00200000u

/* What sets a pad off, in the pad record's trigger field. A different and
 * unrelated encoding from the hit masks above -- low bits, one per class, and
 * the armies sit in the middle of the class bits rather than above them. */
#define AM2_PADTRIG_EVERYTHING   0x0001u
#define AM2_PADTRIG_SARGE        0x0002u
#define AM2_PADTRIG_UNIT         0x0004u
#define AM2_PADTRIG_TROOPER      0x0008u
#define AM2_PADTRIG_TANK         0x0010u
#define AM2_PADTRIG_VEHICLE      0x0020u
#define AM2_PADTRIG_GREEN        0x0040u
#define AM2_PADTRIG_TAN          0x0080u
#define AM2_PADTRIG_BLUE         0x0100u
#define AM2_PADTRIG_GREY         0x0200u
#define AM2_PADTRIG_CONVOY       0x0400u
#define AM2_PADTRIG_BOAT         0x0800u
#define AM2_PADTRIG_GROUNDVEH    0x1000u
#define AM2_PADTRIG_NPC          0x2000u

#define ADDR_HINSTANCE           0x00512580u  /* HINSTANCE */
/* Not HINSTANCE-related at all, despite sitting beside it: DetectCpuSpeed sets
 * it, and it means "this machine is fast enough". WinMain clears it first. */
#define ADDR_FAST_MACHINE        0x00512584u  /* int32_t */
#define ADDR_HWND                0x0051245Cu  /* HWND, the one game window */
/* IAT slots, reached through the game's own import table rather than by
 * importing the symbols into am2hook.dll -- which keeps the flat modules free
 * of Win32 types and matches what device.cpp does for the DirectX thunks.
 *
 * All three are "incidental" by the rule in CLAUDE.md: waiting on a handle,
 * releasing a mutex and posting a message operate on things somebody else
 * created, so a module using them is not boundary code. */
/* 0x004012C0, one caller. Find the node whose MSGNODE_OFF_KEY matches, copy
 * its body out, and answer the destination or NULL -- all under the list's
 * own mutex. */
#define ADDR_MSG_LIST_COPY_BY_KEY  0x004012C0u  /* void *(list,key,void *dst) */
#define IAT_WAIT_FOR_SINGLE_OBJECT 0x0046F080u
#define IAT_RELEASE_MUTEX          0x0046F060u
#define IAT_POST_MESSAGE_A         0x0046F1CCu
#define AM2_WM_CLOSE               0x10
/* The six private window messages, named for what POSTS them rather than for
 * the case labels they used to share. They live here and not in winproc.cpp
 * because the comm side posts them and the window side handles them, and one
 * constant in two files is one constant too many. See the sender list in
 * winproc.cpp.
 *
 * 0x0500 is NOT comm traffic: AudioTimerProc posts it, and it shared a case
 * label with the rest only because WndProc forwarded all six together. */
#define AM2_WM_PACKETS_READY       0x0464u
#define AM2_WM_NO_BUFFERS          0x046Bu
#define AM2_WM_PLAYER_GONE         0x046Cu
#define AM2_WM_HOST_CHANGED        0x046Du
#define AM2_WM_SETUP_DONE          0x046Eu
#define AM2_WM_STREAM_DONE         0x0500u
/* AND A SEVENTH THAT NOTHING LISTENS FOR. CommSystemMessage posts it after a
 * player joins, and it is the only site in the image that does -- but WndProc
 * has cases for the six above and none for this, so DefWindowProc eats it. The
 * sweep that found the other six went from the RECEIVER's switch, which is
 * exactly why this one was not in the list. Named from its sender, and the
 * fact that it is unhandled is the interesting half. */
#define AM2_WM_PLAYER_JOINED       0x0468u

/* 0x00402720. Posts WM_CLOSE to the game window and says so. Sets a flag
 * first, and the ORDER matters: the flag is raised before the log line, so a
 * reader of the log knows it was already set. */
#define ADDR_EXIT_GAME_POST_CLOSE  0x00402720u  /* void(void) */
#define ADDR_EXIT_GAME_FLAG        0x004F8778u

/* ADDR_MSG_LIST_ADD, "AddMsg: Impossible List Size %d". Appends a node to the
 * tail of
 * a mutex-guarded doubly-linked list. The list is {mutex, head, tail, count}
 * and a node is {prev, next}; the complaint fires above 400 or below zero and
 * does NOT stop the append. Twelve callers, and multi-threaded -- CLAUDE.md
 * warns that a mistake here is a race rather than a crash. */
#define MSGLIST_OFF_MUTEX          0x00u
#define MSGLIST_OFF_HEAD           0x04u
#define MSGLIST_OFF_TAIL           0x08u
#define MSGLIST_OFF_COUNT          0x0Cu
#define MSGNODE_OFF_PREV           0x00u
#define MSGNODE_OFF_NEXT           0x04u
/* Both named from DumpMsgList, which prints them, and both corrected from
 * MsgListCopyByKey (0x004012C0), which uses them for what they are: +0x14 is
 * the KEY it matches on, +0x20 is the start of the message BODY and +0x24 is
 * its length. DumpMsgList's second number is the dword at +8 of the body, one
 * dereference further than the first, which is why "owner" looked plausible
 * from the dump alone. Name a field from the code that acts on it. */
/* MSGNODE_OFF_FROM and _TO are already defined further down, with the delayed
 * send path. They went in here a second time and the grep that should have
 * come first caught it; RecvThreadProc confirms both, handing CommReceive
 * &node[8] and &node[0xC] as its `from` and `to`. A LIST header's 0x08 and
 * 0x0C are its tail and count -- different structure, same offsets. */
#define MSGNODE_OFF_KEY            0x14u
#define MSGNODE_OFF_BODY           0x20u
#define MSGNODE_OFF_BODY_LEN       0x24u
/* The other thing a node is searched on. MsgListTakeFlags (0x00401330) tests
 * it against ADDR_MSG_WANTED_FLAGS, and CLEARS the bits it matched before
 * copying the body out -- so the search consumes what it finds and a second
 * call cannot answer the same node twice. */
#define MSGNODE_OFF_FLAGS          0x18u
/* The destination player, named by the message FlushDelayedSends prints on a
 * failure -- "DPlaySend Failure %x to %x size %d" takes the result, this
 * field, and MSGNODE_OFF_BODY_LEN. It is also CommSend's first argument. */
#define MSGNODE_OFF_TO             0x0Cu
#define ADDR_MSG_WANTED_FLAGS      0x004F8B98u  /* uint32_t */
/* 0x00401330, two callers, and the exact shape of MsgListCopyByKey with the
 * key test replaced by a mask test. Answers the bits it took, or 0. */
#define ADDR_MSG_LIST_TAKE_FLAGS   0x00401330u  /* int32_t(list, void *dst) */
/* 0x00401150, one caller. Insert a node into the list in ascending
 * MSGNODE_OFF_KEY order, under the list's own mutex. */
#define ADDR_MSG_LIST_INSERT       0x00401150u  /* void *(list, void *node) */
#define AM2_MSGLIST_SANE_MAX       0x190   /* 400 */
/* 0x00401410, three callers. Unlink a node the caller already holds, wherever
 * it sits -- the general removal MsgListRemHead is the head-only case of. It
 * names itself "RemMsg" in all three of its complaints. Reconstructed. */
#define ADDR_MSG_LIST_REMOVE       0x00401410u  /* void(void *list, void *n) */
/* 0x004010C0, "RemHead: Impossible List Size %d". Unlinks and answers the head
 * node, or null. The same sanity complaint as the append, and a second one --
 * "Empty List!" -- that fires ONLY for ADDR_MSG_LIST_POOL, because an empty
 * pool means the game has run out of message buffers and an empty ordinary
 * queue is unremarkable. */
#define ADDR_MSG_LIST_REM_HEAD     0x004010C0u /* void *(void *list) */
/* 0x00401240, five callers. Walk the same list under the same mutex, find the
 * node whose PACKET_REC_OFF_KEY matches, set or clear bits in its
 * PACKET_REC_OFF_FLAGS, and answer the node -- or null, having released the
 * mutex either way.
 *
 * The bits argument is used BOTH WAYS from one register: `not esi` is computed
 * before the walk, so the clear arm ands with the complement. One argument,
 * two masks, decided by the third.
 *
 * The name is structural. It is the third member of this family and the only
 * one that searches; what the key and the flags MEAN is not established. */
#define ADDR_MSG_LIST_SET_FLAG     0x00401240u /* void *(list, key, set, bits) */
#define PACKET_REC_OFF_KEY         0x14u
#define PACKET_REC_OFF_FLAGS       0x18u
/* Both DirectPlay enumerations pass this same handle as their lpContext, and
 * CommSend posts to it -- so an "lpContext" in this game is the window. */
#define ADDR_APP_MUTEX           0x004FA034u  /* HANDLE "ArmyMenMutex" */
#define ADDR_LAST_MESSAGE        0x004F9FE4u  /* uint32_t, last dispatched message */
#define ADDR_SCREEN_W            0x004852D8u  /* int32_t */
#define ADDR_SCREEN_H            0x004852DCu  /* int32_t */
/* AM2_Rect. ADDR_ORIGIN_DX and ADDR_ORIGIN_DY above are its first two members:
 * PositionWindow writes all four, as the client area in screen coordinates
 * when windowed and as (0,0,w,h) when not. */
#define ADDR_SCREEN_RECT         0x00485330u

/* Command-line switches, all set to 1 when the flag is present. Recovered from
 * WinMain's strstr chain; three are developer names. */
#define ADDR_OPT_WINDOWED        0x00507344u  /* -w */
#define ADDR_OPT_NO_INTRO        0x004FA038u  /* -nointro */
#define ADDR_OPT_TRACE_PF        0x0050C35Cu  /* -tracePF */

/* What -tracePF traces: the region graph, which is this game's pathfinding
 * structure. The switch is the evidence for that -- the region log lines are
 * gated on it and nothing else is.
 *
 * ADDR_REGION_OF_CELL is one byte per map cell giving its region id, where 0
 * means "no region". ADDR_REGIONS is an array of 44-byte records; the two
 * fields reached here are a link COUNT at +8, a byte, and a pointer to the
 * links at +0x0C. A link is six bytes. */
#define ADDR_REGION_OF_CELL        0x00514ECCu  /* uint8_t * */
/* 0x0043A450, two callers. The region a tile is in -- and when the tile has
 * none, borrow one from a neighbour and cache it on the tile. */
#define ADDR_TILE_REGION_OR_BORROW 0x0043A450u /* uint16_t(uint16_t tile) */
/* Eight tile-index deltas, DOUBLED: entries 0..7 are the ring and 8..15 repeat
 * them, so a walk starting anywhere in 0..7 runs forward for eight steps
 * without a wrap test and stops when the VALUE comes back to where it began.
 * Built at map load beside ADDR_TILE_NEIGHBOURS; the seventeenth dword the
 * builder writes is not part of that scheme and nothing here reads it. */
#define ADDR_TILE_RING8            0x0053C480u /* int32_t[17] */
#define ADDR_REGIONS               0x00514EF0u
#define AM2_REGION_SIZE            44
/* The two words in front of it, written in the same three instructions by
 * BuildRegionGraph and read nowhere yet: the region's own id, and the TILE
 * that first claimed it. Named from that writer. */
#define REGION_OFF_ID              0u   /* int16_t */
#define REGION_OFF_TILE            2u   /* int16_t */
#define REGION_OFF_ACTIVE          4u   /* int32_t; 0x0042BAD4 sets it to 1 */
#define REGION_OFF_NLINKS          8u   /* uint8_t */
#define REGION_OFF_LINKS           0x0Cu
#define ADDR_ADD_REGION_LINK       0x0042B860u  /* void(int32_t, int32_t) */
/* 0x0042B9A0, one caller -- the state-2 entry. THE FUNCTION THAT BUILDS
 * EVERYTHING THE ROUTING READS: it walks the map's interior, drops the region
 * off any tile whose weight has reached AM2_BLOCK_FULL, grows and activates
 * the region array, links each tile to the four neighbours in a different
 * region, and finishes by allocating the two stride-squared matrices
 * ADDR_REGION_NEXT and ADDR_REGION_COST and stamping the generation to 1.
 * So AiRouteToward's all-pairs tables exist because of this. Reconstructed.
 *
 * It shares its log line with ADDR_ACTIVATE_REGION, which orig.h already
 * notes: "Activating Region %d" is logged here at 0x0042BAFB beside the very
 * same store. */
#define ADDR_BUILD_REGION_GRAPH    0x0042B9A0u  /* void(void) */
#define AM2_STR_ACTIVATING_REGION  0x004862BCu  /* "Activating Region %d\n" */
/* The map's outermost two tiles are skipped in both directions, so the sweep
 * runs 2..w-3 and 2..h-3 and every link it makes has four neighbours to look
 * at without a bounds test. NOT AM2_EDGE_MARGIN, which is five and belongs to
 * SealMapEdges. */
#define AM2_REGION_MARGIN          2
/* THE REGION ROUTING TABLES. Two square byte matrices of the same stride, and
 * 0x00406460 is what makes them legible: it indexes both as
 * `m[from * stride + to]`, tests the first against a sentinel byte, and walks
 * the second one hop at a time until it arrives.
 *
 * So ADDR_REGION_COST records whether a pair has been solved and
 * ADDR_REGION_NEXT is a next-hop table: the region to step to when you are in
 * `from` and want `to`. Classic all-pairs routing, stored as bytes because a
 * map has fewer than 256 regions.
 *
 * ADDR_REGION_STAMP WAS CALLED ADDR_REGION_UNSET AND MEANT THE OPPOSITE. A
 * cost entry EQUAL to it is the solved one: 0x00406492 is `cmp cl, al; je`
 * past the solve, and ADDR_REGION_SOLVE_PAIR writes the byte into the entry at
 * 0x00438389 and 0x004383A5 as its last act. It is a GENERATION counter, not a
 * sentinel value -- ActivateRegion and InactivateRegion increment it whenever
 * a region's REGION_OFF_ACTIVE actually changes, which invalidates every
 * previously stamped pair in one byte write instead of clearing the matrix.
 * The old name described how RegionHops reads it and got the polarity
 * backwards; the reconstruction had the comparison inverted with it. */
#define ADDR_REGION_STRIDE         0x00514EECu  /* int16_t, both matrices' */
#define ADDR_REGION_NEXT           0x00514EF4u  /* uint8_t *, the next hop */
#define ADDR_REGION_STAMP          0x00514EF8u  /* uint8_t, the solved generation */
#define ADDR_REGION_COST           0x00514EFCu  /* uint8_t * */
/* It stamps on BOTH exits -- path found and no path -- which is what makes
 * "stamped" mean "answered" rather than "pending", and it opens by reading
 * REGION_OFF_ACTIVE of `to` and returning when that is clear.
 *
 * THE NO-PATH EXIT IS NOT SYMMETRIC, AND THIS COMMENT SAID IT WAS. It clears
 * next[from][to] and next[to][from] -- those two really are a pair -- and then
 * writes the stamp into cost[from][to] TWICE, at 0x00438389 and 0x004383A5,
 * from two separate reloads of both globals. `imul ecx, edi` and
 * `imul eax, edi` -- the same register, checked in the bytes, 0FAFCF and
 * 0FAFC7. cost[to][from] is never touched on that exit.
 *
 * So an unreachable pair is answered one way round only, and the reverse
 * direction keeps its old generation and gets solved again from the other
 * side. The symmetry the old sentence described is real but lives on the FOUND
 * exit, where two nested double loops fill both directions of the path. */
#define ADDR_REGION_SOLVE_PAIR     0x00438300u  /* void(from, to) */
/* 0x00437E70, 1,168 bytes, one caller -- the search RegionSolvePair drives.
 * Answers non-zero with the path written into the caller's int16 array and its
 * length through the fourth argument, zero when there is none. Still original;
 * the pair-filling above is what is reconstructed. */
#define ADDR_REGION_FIND_PATH      0x00437E70u /* int32(from,to,int16*,int32*) */
#define AM2_REGION_PATH_MAX        256   /* the caller's buffer, 0x200 bytes */
/* The rest of the region record, which only the search reads. Everything up to
 * REGION_OFF_LINKS is shape; these eight are the A* working set, rewritten on
 * every search and meaningful only while REGION_OFF_STAMP matches the current
 * generation. */
#define REGION_OFF_G               0x10u  /* int32_t, cost from the start */
#define REGION_OFF_H               0x14u  /* int32_t, ApproxDistXY * 1.5 */
#define REGION_OFF_DEPTH           0x18u  /* int32_t, hops from the start */
#define REGION_OFF_STAMP           0x1Cu  /* uint16_t, the search generation */
#define REGION_OFF_STATE           0x1Eu  /* uint8_t: 1 open, 2 closed */
#define REGION_OFF_PARENT          0x20u  /* AM2_Region *, the came-from */
#define REGION_OFF_PREV            0x24u  /* open-list links, sorted by g+h */
#define REGION_OFF_NEXT            0x28u
/* The record size is AM2_REGION_SIZE, already defined above; a second name
 * for it was written here and deleted before it landed. */
#define AM2_REGION_LINK_SIZE       6      /* the stride the link walk adds */
#define AM2_REGION_STATE_OPEN      1
#define AM2_REGION_STATE_CLOSED    2
#define AM2_REGION_STEP_WEIGHT     2      /* what a step's distance is scaled by */
#define AM2_REGION_DEPTH_MAX       0xFE   /* longer than this answers "no path" */
/* The search's own globals: one generation counter, the goal, the head of the
 * open list, the node being expanded, the neighbour being relaxed, and two
 * cursors the sorted insertion walks with. */
#define ADDR_REGION_GENERATION     0x00654C2Cu  /* int32_t, low 16 stamped */
#define ADDR_REGION_GOAL           0x00523DC0u  /* AM2_Region * */
#define ADDR_REGION_OPEN_HEAD      0x00523DC4u  /* AM2_Region * */
#define ADDR_REGION_CURRENT        0x00654C28u  /* AM2_Region * */
#define ADDR_REGION_NEIGHBOUR      0x00523D9Cu  /* AM2_Region * */
#define ADDR_REGION_WALK           0x00523DD4u  /* AM2_Region *, insert + path */
#define ADDR_REGION_INSERT_PREV    0x00523D98u  /* AM2_Region * */
/* Read only on the resume arm that cannot be reached -- see
 * ADDR_REGION_SEARCH_STATE. Named so the transcription can mention them. */
#define ADDR_REGION_RESUME_NODE    0x0053C4C8u  /* AM2_Region * */
#define ADDR_REGION_RESUME_GOALID  0x00523DCCu  /* int32_t */
/* READ AND DELIBERATELY LEFT ORIGINAL, with the reading recorded so it need
 * not be redone. 416 bytes, a 0x204-byte stack frame -- a path array -- and
 * three 2D byte tables indexed as `stride * a + b` off ADDR_REGION_STRIDE,
 * written in two nested loops. The `from == to` and unreachable arms fall out
 * early and stamp the cost entries; the found arm walks the path array
 * backwards filling ADDR_REGION_NEXT for every prefix.
 *
 * Not attempted late in a session: an off-by-one in a 2D byte index is exactly
 * the mistake this file catalogues, and NOTHING here would catch it -- region
 * routing feeds the AI, which no drive reaches, so a wrong next-hop table
 * would pass every configuration in tools/ab.sh. It wants the reading done
 * fresh and an oracle to check it against, not a careful afternoon. */
/* 0x0042BC70 and 0x0042BCB0, one caller each and adjacent: the script's
 * `activateregion` (token 149) and `inactivateregion` (150). Each range-checks
 * the id against ADDR_REGION_STRIDE, writes REGION_OFF_ACTIVE, and bumps
 * ADDR_REGION_STAMP -- but ONLY when the flag actually changed. The name is
 * the game's own twice over: the token table, and "Activating Region %d" which
 * 0x0042BAFB logs beside the very same store. Reconstructed. */
#define ADDR_ACTIVATE_REGION       0x0042BC70u  /* void(int32_t region) */
#define ADDR_INACTIVATE_REGION     0x0042BCB0u  /* void(int32_t region) */
/* 0x00406460, one caller. How many hops from one region to another, 0 when
 * they are the same and -1 when the walk falls off. Its third argument says
 * whether an unsolved pair may be solved on the spot: with it clear, an
 * unknown pair is -1 rather than an answer. */
#define ADDR_REGION_HOPS           0x00406460u  /* int32(from, to, solve) */
/* 0x004066B0, two callers. Whether two OBJECTS are in the same region or in
 * neighbouring ones -- their tiles' regions, through ADDR_REGION_HOPS, and
 * true for a hop count of 0 or 1. */
#define ADDR_REGIONS_NEAR          0x004066B0u  /* int32(a, b, solve) */
/* 0x0042B7F0, three callers. Of all the links `region` has to `to`, the index
 * of the MIDDLE one -- count them, then walk again and stop at the
 * ceiling-halfth. -1 when there are none.
 *
 * Two regions usually touch along a run of cells, so this is choosing where to
 * cross rather than merely whether a crossing exists. The name is ours; the
 * middle is the whole content of the function. */
#define ADDR_MIDDLE_REGION_LINK    0x0042B7F0u  /* int32(int32 region, int32 to) */
#define ADDR_OPT_TRACE_VEH       0x0050C360u  /* -traceVEH */
#define ADDR_OPT_TRACE_WIN       0x0050C354u  /* -tracewin */
#define ADDR_OPT_DBG             0x0050C358u  /* -dbg */
#define ADDR_OPT_ROB             0x004FD73Cu  /* -rob; same as ADDR_DEBUG_ITEMLIST */
#define ADDR_OPT_PETER           0x004FD744u  /* -peter */
#define ADDR_OPT_DAN             0x004FD740u  /* -dan */
#define ADDR_OPT_DF              0x0047894Cu  /* -df clears it; 1 by default */
/* -bm sets it to 1 and -sm to 0, and it was ADDR_OPT_MUSIC for months on that
 * pairing alone. It is BIG MOVIES, and three independent things now say so:
 *
 *   the switches themselves, -bm and -sm;
 *   MovieBuildName, which appends "sml" to a movie's filename when this is 0
 *   and the machine is slow, and does not when it is 1;
 *   SetGameDir, which sets it after successfully entering the `avi`
 *   directory -- i.e. when the full-size movies are actually present.
 *
 * The third was already in this file as a puzzle: "one of the two readings is
 * wrong and it is not yet established which". As "music" the avi latch makes
 * no sense at all; as "big movies" it is the obvious thing to latch. What
 * settled it was reading a function that USES the flag rather than one that
 * writes it. */
#define ADDR_OPT_BIG_MOVIES      0x005125C4u  /* -bm sets, -sm clears */
/* -nm, and it is NO MOVIES. Two references in the whole image: the switch
 * parse and PlayMovie's gate, which refuses to open a film when it is set.
 * A flag read in exactly one place, by a function that decides whether to
 * play a film, needs no second witness. It was ADDR_OPT_NM. */
#define ADDR_OPT_NO_MOVIES       0x0051259Cu  /* -nm */
#define ADDR_OPT_MAP_NAME        0x004F9FECu  /* char[], the text after -map: */
/* The comm subsystem object; -debugComm, -traceComm and -logComm set flags
 * inside it rather than in globals of their own. */
#define ADDR_COMM_OBJECT         0x004751B0u  /* void ** */
/* Reset to 0 before an enumeration and used by the callback as the slot it
 * fills next, so after EnumPlayers returns it is how many were found. */
/* How many players the enumeration has seen, which is also the SLOT it is
 * filling: the callback at 0x0040E0B0 multiplies it by the 112-byte comm slot
 * stride and indexes the slot array with it, and stores it as "our slot" when
 * the player it just saw is us. It carried a second name, ADDR_JOIN_CONTEXT,
 * invented from the two sites that only ever write ZERO to it -- which is why
 * neither could settle what it was. Find the reader. */
#define ADDR_COMM_ENUM_COUNT     0x004751B4u  /* int32_t */
/* EnumPlayers' DPENUMPLAYERSCALLBACK2. Left original -- it is the other side
 * of the same enumeration and touches no import. */
#define ADDR_ENUM_PLAYERS_CB     0x0040E0B0u
#define ADDR_COMM_ENUM_PLAYERS   0x0040E200u  /* int32_t(void) */
/* A player slot's index field, which the session carries as dwUser1. 0x63
 * is what a reset slot holds -- no player. */
#define COMM_SLOT_OFF_INDEX      0x004u
#define COMM_SLOT_INDEX_NONE     0x63u

/* The menu's "do this next" pair. StartSelectedGame writes a request code into
 * the first and raises the flag in the second; the menu loop acts on it. The
 * codes seen so far are 1 (refused), 0xA (joined a session) and 0xB (start a
 * local game). */
#define ADDR_MENU_REQUEST        0x00511DC8u  /* int32_t, the code */
/* Two of the codes it takes, and the project already knew both from the other
 * end: tools/ab.sh reaches the host panel by poking 7 and the join panel by
 * poking 9, and says so in its own comment. Substate34Escape picks between
 * exactly those two on COMM_OFF_IS_HOST, which is independent corroboration
 * of what that suite had been assuming. */
#define AM2_MENU_REQ_MP_HOST     7
#define AM2_MENU_REQ_MP_JOIN     9
/* The in-mission SUB-STATE, which frame.cpp dispatches its thirteen-arm table
 * on: 0x21 is ordinary play and 0x22 is the ESCAPE arm. It carried a second
 * name, ADDR_MENU_REQUEST_TAKEN, taken from the function that WRITES it after
 * consuming a menu request -- and CLAUDE.md went on calling it "the in-mission
 * sub-state ADDR_MENU_REQUEST_TAKEN", which is the misreading and its own
 * refutation in one sentence. */
#define ADDR_MENU_MODE           0x00511DBCu  /* int32_t; 0x21 = back to play */
/* Stamped from ADDR_TICKS when a level load completes, beside the game clock
 * being reset to zero. Read by nothing below the CRT line. */
#define ADDR_CLOCK_BASE_MS       0x00511E00u  /* uint32_t */
/* Asks for the level script to be re-read on the next frame. */
#define ADDR_SCRIPT_RELOAD       0x00511DCCu  /* int32_t */
/* Set while that reload is in progress, and only under -dbg. */
#define ADDR_SCRIPT_RELOADING    0x0050C350u  /* int32_t */
/* Counts frames since a network game started, to ten, and then checks once for
 * abandoned armies. Not a timer: it counts FRAMES. */
#define ADDR_NET_SETTLE_COUNT    0x0051234Cu  /* int32_t */
#define AM2_NET_SETTLE_FRAMES    10
/* The float ADDR_FRAME_DELTA_SEC is pinned to in a network game: 0.066, the
 * same 66 ms the measured delta is clamped to. */
#define AM2_NET_FRAME_DELTA_SEC  0x3D872B02u
/* The mode is the menu-request arm number, so 7 is the multiplayer HOST
 * panel -- the same 7 ab.sh pokes to open it. */
#define AM2_MENU_MP_HOST         7
#define ADDR_OVERLAY_DIRTY       0x00511DC0u  /* int32_t; the primary needs saving */
#define ADDR_DRAW_MENU_OVERLAY   0x00425AF0u  /* void(void) */
#define ADDR_OVERLAY_PREPARE     0x00412D30u  /* void(int32, int32) */
#define MENU_MODE_PLAYING        0x21
/* The in-mission OPTIONS overlay. Every dialog that can be opened from
 * either place -- CONTROLS, AUDIO -- ends by asking for this when
 * ADDR_GAME_STATE is 2 and for menu request 14 when it is not. */
#define MENU_MODE_OPTIONS        0x17
/* The multiplayer session object, and the two routines either side of it --
 * which are NOT a session's. They are the three-field record {count, rows,
 * ownsRows} that a list box's row array also is, named from this call site
 * before the bodies were read. Renamed to what the bodies do; RecordCtor and
 * RecordReset are what widget.cpp calls them. */
#define ADDR_SESSION_OBJECT      0x00516130u  /* void *, made on demand */
#define ADDR_RECORD_CTOR        0x00453910u  /* thiscall void *(this, int32) */
#define ADDR_RECORD_RESET       0x00453940u  /* thiscall void(this) */
/* Fills a 0x50-byte DPSESSIONDESC2 -- the app GUID from the comm object lands
 * at +0x18, which is guidApplication -- and asks DirectPlay to enumerate the
 * sessions matching it. Slot 13 is EnumSessions, not Open; it was briefly
 * ADDR_COMM_OPEN_SESSION on the strength of the descriptor alone, before the
 * slot was counted. Invisible to tools/comcalls.py because the interface lives
 * inside the comm object rather than in a global. */
#define ADDR_COMM_ENUM_SESSIONS  0x0040E3B0u  /* thiscall int32(this, void *) */
#define ADDR_COMM_JOIN_SESSION   0x0040E7B0u  /* thiscall int32(this, const GUID *) */
#define ADDR_COMM_RECEIVE        0x0040E8A0u  /* thiscall int32(this,from,to,flags,buf,len) */
#define ADDR_COMM_SLOT_OF_ID     0x0040F160u  /* thiscall int32(this, DPID) */
#define ADDR_STR_FLOW_UNPAUSE    0x00473A84u  /* "FLOW UNPAUSE nfree = %d\n" */
/* Receive keeps its own statistics, laid out like the send side's. */
#define COMM_OFF_RX_INDEX        0x008u
#define COMM_OFF_RX_TIMES        0x0FCu   /* uint32[30] */
#define COMM_OFF_RX_SIZES        0x174u   /* uint32[30] */
#define COMM_OFF_RX_MAX          0x1ECu
#define COMM_OFF_RX_BYTES        0x1F4u
#define COMM_OFF_RX_PACKETS      0x1FCu
#define COMM_SLOT_OFF_HEARD      0x060u   /* GetTickCount of the last packet */
#define COMM_FLOW_PAUSED_BIT     0x8000u
#define COMM_FLOW_FREE_OK        0x12C    /* 300 free entries is enough */
#define COMM_UNACKED_CLEAR       0x0F     /* alarm clears below this */
#define COMM_MSG_TYPE_ACK        0x0B
#define ADDR_COMM_SEND_PROPERTY  0x0040FAA0u  /* thiscall int32(this, uint32) */
#define ADDR_GUID_NULL           0x0046FD98u  /* all zeroes, as guidPlayer */
#define ADDR_GUID_GAME_PROPERTY  0x0046F888u  /* {BDD4B95F-D35C-11D0-...} */
#define ADDR_STR_CREATE_PLAYER_FAIL 0x00475248u
#define ADDR_STR_NUM_PLAYERS     0x00475230u
#define ADDR_STR_OPEN_FAILED     0x00475410u  /* " Open Session Failed returned %x \n" */
#define ADDR_ENUM_SESSIONS_CB    0x0040E280u  /* LPDPENUMSESSIONSCALLBACK2 */
/* The service-provider browser and its callback. The callback drops two
 * providers by name before adding the rest -- "Play on HEAT" and "Play on
 * Mplayer", both matchmaking services that no longer exist -- and its `ret 0x18`
 * matches LPDPENUMCONNECTIONSCALLBACK's six arguments exactly. */
#define ADDR_COMM_ENUM_CONNECTIONS 0x0040E530u /* thiscall int32(this, void *) */
#define ADDR_ENUM_CONNECTIONS_CB 0x0040E460u  /* LPDPENUMCONNECTIONSCALLBACK */
#define ADDR_CONNECTION_LIST     0x004FA900u  /* void *, what that callback fills */
/* int32_t, which player slot is OURS. It was ADDR_HOST_SLOT, named from
 * CommOpenSession -- where the machine opening the session IS the host, so the
 * two readings agree and the wrong one looked right.
 *
 * 0x0040E117 settles it: the player-created handler writes this only when the
 * new player's id equals the comm object's OWN id. CommRemovePlayer confirms
 * it, decrementing this and ADDR_DEFAULT_OWNER together when a lower slot
 * leaves. And the host-migration message reads it to fill "Player %s is now
 * the host", which names US -- correct, because that handler runs on the
 * machine that has just taken over. */
#define ADDR_OUR_SLOT            0x004FA904u
/* Creates the DirectPlay session -- slot 24 is Open, with DPOPEN_CREATE. */
#define ADDR_COMM_OPEN_SESSION   0x0040DFC0u  /* thiscall int32(this, const char *) */
/* Appends one named entry to a list object. 16 callers.
 *
 * The object is {int32 count; record *base} and a record is 0x104 bytes: a
 * name of up to 0x100 and a dword beside it. Every append reallocs to exactly
 * count+1 records, so adding n entries is n reallocs; nothing here rounds up.
 * The name is copied with no bound at all. */
#define ADDR_LIST_ADD            0x00453A30u  /* thiscall void(this, const char *, void *) */
#define AM2_LIST_ROW_STRIDE      0x104u
/* The dword beside the name, which the row OWNS when the record's third
 * field says so: RecordReset frees it per row. The comm panel keeps its
 * DirectPlay connection here. */
#define AM2_LIST_ROW_VALUE       0x100u
/* +8 is whether the list OWNS the value pointers: ListDropOldest frees the
 * one it is discarding only when it is set, and only the DIFFICULTY dialog's
 * rows carry plain integers there. */
#define AM2_LIST_OWNS_VALUES     8u
#define ADDR_LIST_DROP_OLDEST    0x004539A0u  /* thiscall void(this) */
#define ADDR_STR_COMPUTER_ONLY   0x00475300u  /* "Play Against Computer Only" */

/* The packet transmit and the three helpers its watchdog uses. */
#define ADDR_COMM_SEND           0x0040EB70u  /* thiscall int32(this,id,flags,buf,len) */
/* Kept as PLAYER rather than renamed to FLOWQ, having checked both. The two
 * accessors below call what this returns a "Flowq" -- "No Flowq for %X" -- and
 * so does 0x004014C0 ("Interrupt Level Can't find FlowQ for %x"). But CommSend
 * logs "DPLAY ERROR: INVALID PLAYER IN SEND TO ID %x" for the very same id, so
 * both words are the program's own: it is a player's record, and the
 * flow-control code calls that record a FlowQ. One thing, two vocabularies,
 * and no reason to prefer either name.
 *
 * It scans six 0x7E0-byte records at 0x004F1980 and does NOT stop at the first
 * match -- eax is overwritten each time -- so the LAST match wins. */
#define ADDR_FIND_PLAYER_BY_ID   0x00402990u  /* void *(uint32 id); NULL when unknown */
#define ADDR_PLAYER_LATENCY      0x00402EC0u  /* int32_t(uint32 id), ms */
/* The panel's row-name colours. The first three are reused from elsewhere in
 * the image -- one palette index serves several features -- and the names
 * they already carry are kept rather than aliased. */
#define ADDR_COLOUR_LAG_MID      0x004FE092u  /* uint8_t, latency over 750 ms */
#define ADDR_COLOUR_STALE        0x004FE090u  /* uint8_t, silent over 1250 ms */
#define ADDR_COLOUR_NO_MAP       0x00502CE5u  /* uint8_t, has not confirmed */
#define AM2_LATENCY_MID          0x2EEu       /* 750 */
#define AM2_LATENCY_BAD          0x3E8u       /* 1000 */
#define AM2_SILENCE_BAD          0x4E2u       /* 1250 */
#define AM2_PLAYER_LAST_SEEN     0x70u        /* GetTickCount at the last packet */
#define ADDR_MP_NAME_INK         0x00432C50u  /* uint8_t(int32_t row) */
#define ADDR_MP_NAME_PAPER       0x00432CE0u  /* uint8_t(int32_t row) */
#define ADDR_MP_NAME_SET_INK     0x00432D40u  /* thiscall void(this, uint8_t) */
#define ADDR_PLAYER_RECORDS      0x004F1980u  /* six of them, 0x7E0 apart */
#define AM2_PLAYER_RECORD_BYTES  0x7E0u
/* The two fields of a player record ADDR_PACKET_SLOT_RESET does anything with
 * beyond zeroing. Everything else it touches it simply clears. */
#define PLAYER_REC_OFF_OWN_BIT   0x14u  /* uint32_t, set to 1 << slot */
#define PLAYER_REC_OFF_MSGS      0x78u  /* the list ADDR_MSG_LIST_INIT takes */
/* Four more, all from CommRegisterSelf, which is the function that BUILDS a
 * record rather than resetting one. +0x18 is a second bit mask, `1 << (slot +
 * 4)`, and it is OR'd into ADDR_MSG_WANTED_FLAGS -- so the four-bit shift is
 * what keeps the two mask families apart in one word. +0x5C is stamped from
 * GetTickCount as the record is made. +0x88 is written 1 and then immediately
 * overwritten with the comm object's COMM_OFF_IS_HOST, which is a store the
 * original makes and this reproduces. +0x94 is written 1 and nothing here
 * reads it. */
#define PLAYER_REC_OFF_WANT_BIT  0x18u  /* uint32_t, 1 << (slot + 4) */
#define PLAYER_REC_OFF_MADE_AT   0x5Cu  /* uint32_t, GetTickCount */
#define PLAYER_REC_OFF_IS_HOST   0x88u
#define PLAYER_REC_OFF_FLAG_94   0x94u
/* The two 120-dword arrays at the end of a record, cleared together by one
 * loop that walks them in step. */
#define PLAYER_REC_OFF_RING_A    0x420u  /* int32_t[120] */
#define PLAYER_REC_OFF_RING_B    0x600u  /* int32_t[120] */
#define AM2_PLAYER_RING_LEN      0x78    /* 120 */
/* The mask of occupied player slots, OR'd with PLAYER_REC_OFF_OWN_BIT as each
 * record is made. ADDR_MSG_WANTED_FLAGS takes the other one. */
#define ADDR_PLAYER_SLOT_MASK    0x004F48DCu  /* uint32_t */
/* CommRegisterSelf's own two lines. The first is gated on COMM_OFF_VERBOSE. */
#define ADDR_STR_FLOWQ_MAKING    0x00473A5Cu /* "Creating Flow Queue for Player id %x" */
#define ADDR_STR_FLOWQ_FAILED    0x00473A3Cu /* "Create FlowQ failed for ID %x" */
#define AM2_PLAYER_WANT_SHIFT    4

/* Two masks out of that record, each named by its own error message. */
#define ADDR_GET_PLAYER_MASK     0x00402BD0u  /* uint32_t(uint32_t id) -- +0x14 */
#define ADDR_GET_RESEND_MASK     0x00402C00u  /* uint32_t(uint32_t id) -- +0x18 */

/* The comm layer's outgoing message hub, named by its own
 * "SendGameMsg, first message to %x, hehas set to %d". 928 bytes and 14
 * callers, so it is a hub rather than a helper; two of those callers are
 * reconstructed below and reach it through here. Among its other messages is
 * "Error Send can't find Flow for Player %x", which is the same player/FlowQ
 * synonym ADDR_FIND_PLAYER_BY_ID records. */
#define ADDR_SEND_GAME_MSG       0x004022D0u  /* int32_t(void *msg, int32 to,
                                               * int32 flags); reconstructed */
/* Its three arguments, settled by its callers rather than by its body.
 * `to` is a DirectPlay id -- ArmyMessageFlush passes one player slot's id and
 * SendGamePause passes 0, which is DPID_ALLPLAYERS, and the function's second
 * refusal is `to == -1`. `flags` is the DirectPlay send flags word:
 * ArmyMessageFlush hands it COMM_OFF_SEND_FLAGS, SendGamePause hands it 1,
 * and bit 0 is DPSEND_GUARANTEED -- which is exactly the bit that exempts a
 * packet from the loss emulation below. Three independent facts, one reading.
 *
 * WHAT IT REFUSES, in order: not joined, `to == -1`, and COMM_OFF_SAW_KIND_31.
 * That third one is the reader NoteKind31 never had: a kind-0x31 message
 * arriving latches the flag, and from then on every send answers
 * DPERR_SESSIONLOST. So 0x3E0 is "the session is over", and the writer alone
 * could not have said so.
 *
 * KIND 0x0B IS THE RELIABLE ONE. A packet whose COMMMSG_OFF_KIND is 0x0B is
 * copied into the SEND QUEUE keyed on its PACKET_OFF_SEQ, with GetPlayerMask's
 * bit for the recipient in MSGNODE_OFF_FLAGS -- and if that sequence is
 * already queued only the bit is added, so one buffer serves every recipient
 * of one flush. ProcessResendQueue is the other end. Everything else goes
 * straight out. The store at 0x0040FE50 is what gives the outgoing army
 * packet that kind, and ADDR_RESEND_BUF gets it too, which is what makes the
 * pair a conversation. */
#define AM2_COMMMSG_KIND_FLOW    0x0Bu

/* The per-player flow fields the send path writes. All four are named from
 * this function, which is their only writer. */
#define FLOW_OFF_ACK_SENT        0x10u  /* the FLOW_OFF_FIELD_04 we last told him */
#define FLOW_OFF_SENT_AT         0x1Cu  /* GetTickCount at the last send */
#define FLOW_OFF_SENT_PACKETS    0x28u  /* incremented per send */
#define FLOW_OFF_SENT_BYTES      0x2Cu  /* PACKET_OFF_LEN accumulated */
/* THE LATENCY EMULATION, and its two halves have opposite standing.
 *
 * The LOSS pair is DEAD CODE in this image. A decoded scan of every store in
 * the file finds not one write to either offset on a player record -- the
 * twelve hits at +0xA4/+0xA8 are all other structures or `esp` -- so both read
 * zero forever and neither drop arm can be taken. Recorded as a fact about
 * the build rather than left as a puzzle: the code is reproduced because it is
 * there, not because it can run.
 *
 * The LAG pair is live and HOST-DRIVEN: RecvFlowControl copies two fields of
 * an arriving message into them, on clients only, alongside the send flags.
 * They were FLOWQ_OFF_A and FLOWQ_OFF_B -- letters, because their one
 * writer could only say "two more fields". Here they are
 * RandomAround's centre and its spread,
 * added to GetTickCount to give a due time on ADDR_MSG_LIST_DELAYED, and the
 * failure beside them says "Latency Emulation is Out of Send Buffers". The
 * reader is what names them; the writer never could. */
#define FLOW_OFF_LOSS_BURST      0xA4u  /* non-zero: drop by count, not by roll */
#define FLOW_OFF_LOSS_PCT        0xA8u  /* percent of packets to drop */
#define FLOW_OFF_LAG_MS          0xB0u  /* RandomAround's centre; 0 disables */
#define FLOW_OFF_LAG_SPREAD      0xB4u  /* RandomAround's spread */
/* GameRand() % 100, stored on every send and READ BY NOTHING -- this store is
 * its only reference in the image, by a decoded scan and by a raw dword scan
 * both. The same standing as ADDR_UNREAD_50C34C, and the reason the random
 * loss arm would still be observable if anything ever wrote FLOW_OFF_LOSS_PCT:
 * the roll is taken whether or not it is used. */
#define ADDR_SEND_ROLL           0x004F8BA8u  /* int32_t, write-only */
#define AM2_SEND_ROLL_MOD        100
/* Two more fields of a pooled packet record, both written only on the delayed
 * path. The stamp is a GetTickCount taken as the node is filled; the sender is
 * our own player id, the counterpart of MSGNODE_OFF_TO. */
#define MSGNODE_OFF_STAMP        0x1Cu
#define MSGNODE_OFF_FROM         0x08u
/* The three results it returns that ProcessResendQueue's ladder does not
 * carry. E_FAIL and E_OUTOFMEMORY are the SDK's names for the first two; they
 * are written out here for the same reason the five beside them are. */
#define AM2_E_FAIL               0x80004005u
#define AM2_E_OUTOFMEMORY        0x8007000Eu
#define AM2_DPERR_SESSIONLOST    0x887700AAu
/* Bit 0 of the flags word. The one bit SendGameMsg tests, and it exempts a
 * packet from the loss emulation -- which is exactly what a guaranteed send
 * should mean. */
#define AM2_DPSEND_GUARANTEED    0x1

/* Two static message records in .bss -- zero at load, filled in at 0x0040FE04
 * and 0x0040FE14 -- each with the value the sender writes at +8. */
#define ADDR_MSG_COLOR           0x004FC898u
#define ADDR_MSG_TEAM            0x004FC8A8u
/* The chat record is not one of the value pair: it carries a SENDER byte at
 * +8 and the text inline from +9, and the eight bytes before that are set up
 * in .data and never written at runtime. */
#define ADDR_MSG_CHAT            0x004FA910u
#define AM2_MSG_CHAT_SENDER      8u
#define AM2_MSG_CHAT_TEXT        9u
/* No orig_ macro for ADDR_COMM_ARMY_OF_SLOT: it is reconstructed, so call
 * misc.h's CommArmyOfSlot. There WAS one, spread over three lines, and
 * checkseams could not see it -- see join_continuations there. */

/* The two senders themselves. */
#define ADDR_SEND_COLOR_MSG      0x004119C0u  /* void(int32_t colour) */
#define ADDR_SEND_TEAM_MSG       0x00411AC0u  /* void(int32_t team) */
/* The receive halves of the same three, all host-only, all taking
 * (msg, dpid) as ReceiveGameReadyMsg does. Their one caller each is the
 * message dispatcher at 0x0040FF00. */
#define ADDR_RECV_MAP_MSG        0x004118F0u  /* void(msg *, int32_t dpid) */
#define ADDR_RECV_COLOR_MSG      0x00411A20u  /* void(msg *, int32_t dpid) */
#define ADDR_RECV_TEAM_MSG       0x00411B20u  /* void(msg *, int32_t dpid) */
/* 0x00411880, "SendMapMsg from %x   Error = %d". Returns the send result, and
 * returns 1 without sending at all if this machine is the host -- the host has
 * no one to tell. 0x004FB770 is the record, value at +8.
 *
 * TWO parameters, and the body reads only the first: all three call sites push
 * two and 0x0041116E cleans exactly eight. What the second holds differs by
 * site -- twice it is the comm object's 0x03E4, which the body reads out of the
 * global anyway, and once it is the player id.
 *
 * And the value is a RESULT CODE, not a map index. ReceiveStartGameMsg sends 7
 * and 0x00411830 sends 5, while ReceivedMapMsg switches on 0..8 and calls 4
 * nominal. The name says Map; what travels is how the map check went. */
#define ADDR_SEND_MAP_MSG        0x00411880u  /* int32_t(int32_t, int32_t) */
#define ADDR_MSG_MAP             0x004FB770u
/* 0x0040F280, thiscall: give a slot an army colour, swapping with whoever
 * already had it. Returns the slot, or -1 if the colour is negative. */
#define ADDR_COMM_SET_ARMY_COLOUR 0x0040F280u /* int32_t(this, slot, colour) */
#define COMM_ARMY_OFF_COLOUR     0x210u   /* int32_t, swapped by the above */
#define COMM_ARMY_OFF_TEAM       0x258u   /* int32_t, what ReceivedTeamMsg sets */
#define COMM_ARMY_OFF_MAP_OK     0x278u   /* int32_t, what ReceivedMapMsg sets */
/* 0x0040FEA0, "Unknown message type %d". The receive side's dispatcher: an
 * eighteen-arm jump table on the message's first dword, reached from
 * 0x004026D5. The name is ours -- it has no string that names itself, only the
 * one that names its default arm.
 *
 * Gated on 0x0404 of the comm object, which is NOT the 0x0400 that
 * COMM_OFF_STARTED and COMM_OFF_LOCAL both already name. */
#define ADDR_COMM_DISPATCH_MSG   0x0040FEA0u  /* void(msg *, int32_t dpid) */
#define COMM_OFF_MSGS_ENABLED    0x404u
/* Where type 1 leaves what it received: the value, and the checksum of the
 * whole record. */
#define ADDR_LAST_MSG_VALUE      0x004F48F0u  /* int32_t */
#define ADDR_LAST_MSG_CHECKSUM   0x004F48F4u  /* uint32_t */
/* The chat arm's colour table, indexed by the sender byte shifted left eight
 * -- so 256-byte records, of which it reads only the first. */
#define ADDR_CHAT_COLOUR_TABLE   0x004F9AD0u
/* The dispatcher's remaining callees, each named from its own strings. */
/* 0x00410720, "Get Packed  %x bytes seq %d Chksum %x " and "Receive Checksum
 * Error from %x seq %d". One arriving packet: check it, then walk the messages
 * inside it. The header is twenty bytes -- the same 0x14 ArmyMessageFlush
 * resets the outgoing packet's length to -- and each part opens with a
 * uint16_t length. */
#define ADDR_RECV_PACKET         0x00410720u  /* void(packet *, int32_t dpid) */
#define PACKET_OFF_LEN           4u
#define PACKET_OFF_SEQ           8u
#define PACKET_OFF_CHECKSUM      0x0Cu
/* +0x10, stamped from the destination flow's +0x04 just before a resend. The
 * only writer is ProcessResendQueue and the only reader is the far end, so
 * this is named for what it is FED, not for what it means. */
#define PACKET_OFF_ACK           0x10u
#define PACKET_HEADER_SIZE       0x14u
#define COMM_ARMY_OFF_CHKSUM_ERRS 0x260u  /* int32_t, one per player */
/* 0x0040FBB0, "Unknown Army Msg Item Type %d, msgtype:%d, item uid: %x;
 * msgsize: %d" -- the handler for ONE message out of a packet, which is a
 * different dispatcher from CommDispatchMessage and takes the slot rather than
 * the id. Its only caller is the packet walker above. */
#define ADDR_RECV_ARMY_MSG       0x0040FBB0u  /* void(msg *, int32_t slot, int32_t seq) */
/* 0x0040F920, thiscall. Is the army that owns this uid still in play? Army 4
 * -- the one every colour lookup treats as neutral -- always answers yes;
 * anything else answers with 0x025C of its record, which is the field
 * CommSlotRemote falls back to for an empty slot. */
#define ADDR_ARMY_IN_PLAY        0x0040F920u  /* thiscall int32(this, uint32 uid) */
/* 0x0042A7C0. The object kind behind a uid, which is what the army-message
 * dispatcher switches on: 2 is a trooper, 3 a vehicle, 4 the game itself. */
#define ADDR_UID_OBJ_KIND        0x0042A7C0u  /* int32_t(uint32_t uid) */
/* 0x0042ACE0. Answers non-zero to SWALLOW a message before it is dispatched. */
#define ADDR_ARMY_MSG_FILTER     0x0042ACE0u  /* int32_t(msg *, int32_t army) */
/* Both self-named. "troopMessageReceive: got eTROOPER_DROP_ITEM_MESSAGE" and
 * "Unknown Vehicle Message of type %d Received". */
#define ADDR_TROOP_MESSAGE_RECV  0x0044C590u  /* void(msg *, int32_t army) */
/* Reconstructed. Thirteen arms over kinds 0x16..0x22, of which SEVEN --
 * 0x1A..0x20 -- fall to the unknown log; the same shape as the vehicle
 * dispatcher and the same reading, that two families share one number space.
 *
 * TWO OF ITS ARMS NAME THEIR MESSAGE IN THE PROGRAM'S OWN VOCABULARY, which
 * is worth more than the handler addresses: kind 0x21 logs
 * "got eTROOPER_DROP_ITEM_MESSAGE" and 0x22 "got eTROOPER_SET_WEAPON_MESSAGE".
 * The `e` prefix is the original's enum convention. 0x22 also settles a note
 * left elsewhere in this file, which had AM2_MSG_TROOPER_WEAPON as "handled
 * somewhere else entirely" -- this is somewhere else.
 *
 * Only the FIRST arm takes the army, exactly as in the vehicle half. */
/* Reconstructed. Kind 0x16 is a BATCH: the header's length bounds a run of
 * variable-length sub-records starting at +8, each parsed by
 * ADDR_TROOP_SUB_PARSE, which answers the pointer past itself. That is why
 * this is the only arm of the trooper dispatcher that takes the army -- it
 * hands it to every sub-record. */
#define ADDR_RECV_TROOP_16       0x0044CC90u  /* void(msg *, int32_t army) */
#define ADDR_TROOP_SUB_PARSE     0x0044BEA0u  /* void *(const void *, int32) */
#define ADDR_RECV_TROOPER_FIRE   0x0044CB20u  /* 0x17, already named */
/* Reconstructed. Kind 0x18 carries two uids and two dwords: the first uid
 * must resolve, the second must resolve AND be a type 4 -- a WEAPON, per
 * ADDR_OBJ_IS_TYPE4's own error string -- and then the pair goes to
 * ADDR_TROOPER_PAIR_APPLY. */
#define ADDR_RECV_TROOP_PAIR     0x0044C960u  /* 0x18, already named */
/* 0x00448B20, 576 bytes, one caller: what a kind 0x18 message does with its
 * pair. IT NAMES ITSELF, six times, in log lines that all open
 * `TrooperRemotePickupItem` -- the old name here, ADDR_TROOPER_PAIR_APPLY,
 * described the message pairing and said so honestly ("the name is the
 * pairing, not the effect ... none of it is read yet").
 *
 * AND THAT NOTE HAD THE TWO OBJECTS THE WRONG WAY ROUND. It said the stamp
 * goes into "the trooper's +0xC8" and the sound plays "at the weapon's
 * position". It is the other way: `now + 2000` lands on the ITEM -- and
 * +0xC8's own name, OBJ_OFF_PICKUP_AFTER, says what that is -- while the
 * sound plays where the TROOPER is. NotifyPickedUp(item, taker) taking
 * (arg2, arg1) is what pins the two down. Reconstructed. */
#define ADDR_TROOPER_REMOTE_PICKUP 0x00448B20u /* void(troop,item,slot,ammo) */
#define AM2_PICKUP_HOLD_MS       0x7D0   /* 2000, into OBJ_OFF_PICKUP_AFTER */
#define AM2_SND_PICKUP           0x37
#define AM2_ITEM_KIND_HOT_TARGET 0x0E    /* destroyed outright when taken */
#define AM2_ITEM_KIND_MEDKIT     0x16    /* heals the whole ARMY, not the taker */
/* What the medkit applies to every object ForEachArmyObject reaches, passed as
 * that helper's callback. RECONSTRUCTED as MedkitHealOne, and NOT patched: all
 * four references are the reconstructed pickup paths, so a detour would install
 * a jump nothing in the image can reach. Listed in tools/coverage.py's
 * REGISTERED beside WndProc and AudioTimerProc, for the same reason.
 *
 * It went in for one build under a second name, ADDR_MEDKIT_HEAL, which
 * checkpatches refused as a 22nd alias -- the address had been named here all
 * along, with a comment that already said what the function does. Grep the
 * ADDRESS first; the ratchet is the mechanism, not the backstop. */
#define ADDR_MEDKIT_HEAL_ONE     0x00458AB0u  /* void(void *obj) */
/* Modes 4 and 5's PICK slots. Named for their table index rather than for a
 * purpose: what they do is clear (below) but which order the mode represents is
 * not, and the index is the part that is measured. */
#define ADDR_POINTER_PICK_MODE4  0x00458EE0u  /* int32_t(void *obj) */
#define ADDR_POINTER_PICK_MODE5  0x004590F0u  /* int32_t(void *obj) */
#define ADDR_POINTER_PICK_MODE6  0x00459300u  /* int32_t(void *obj) */
#define ADDR_POINTER_PICK_MODE0  0x00459420u  /* int32_t(void *obj) */
/* 0x004597B0. A pick gated on a MENU ROW rather than a pointer mode: offer to
 * heal a hurt friendly trooper riding whatever our leader rides. */
#define ADDR_POINTER_PICK_HEAL   0x004597B0u  /* int32_t(void *obj) */
#define AM2_MENU_ROW_HEAL        0x0B  /* the row it demands, and the overlay */
/* 0x004599A0, its sibling in the same entry: the repair pick. */
#define ADDR_POINTER_PICK_REPAIR 0x004599A0u  /* int32_t(void *obj) */
#define AM2_MENU_ROW_REPAIR      0x0C
/* 0x00459BE0. The only HOSTILE-only pick in this band: an enemy trooper that is
 * not our Sarge, soldier kind under 6, within reach. The row is a placeholder
 * -- what the order actually is has not been established. */
#define ADDR_POINTER_PICK_ENEMY_TROOPER 0x00459BE0u /* int32_t(void *obj) */
#define AM2_MENU_ROW_0A          0x0A
/* 0x00458930, one caller, behind ActionKeyDown(0xD). Empty the current vehicle
 * and attach everyone who leaves to whoever was in seat 0. Its two arguments
 * are pushed by the caller and read by nothing. */
#define ADDR_VEHICLE_DISMOUNT_ALL 0x00458930u /* void(void *, void *) */
/* 0x00458810, the ACTION of pointer mode 6: order the selection to move in
 * formation with AI mode `ignore`. Its FIRST ARGUMENT IS IN-OUT --
 * FormationSlotPoint is handed the address of that slot and rewrites it. */
#define ADDR_POINTER_ACTION_MODE6 0x00458810u /* void(void *, uint32_t) */
/* 0x00458400, mode 4's ACTION: attack an enemy target, or move to the formation
 * slot. Unlike mode 6's it hands FormationSlotPoint a real local rather than
 * its own argument slot -- and it reuses that slot for the loop index instead. */
#define ADDR_POINTER_ACTION_MODE4 0x00458400u /* void(void *, uint32_t) */
/* 0x00458620, mode 5's ACTION. Delegates an ENEMY target to mode 4's action and
 * keeps only the move case. FormationSlotPoint writes into its `at` argument
 * here, where mode 4 gives a real local and mode 6 gives `target`. */
#define ADDR_POINTER_ACTION_MODE5 0x00458620u /* void(void *, uint32_t) */
#define AM2_MENU_ROW_8           8  /* the row its trooper arm is gated on */
/* The one AAI type PointerPickRepair refuses. A placeholder rather than
 * AM2_ITEM_KIND_DISG_3, which is the same number in a different table -- the
 * kind index and AAIREC_OFF_TYPE are not the same namespace. */
#define AM2_AAI_TYPE_26          0x26
/* ArmyAlliedWithObj IS INLINED INTO THE POINTER PICKS, and army.cpp has had it
 * as a function since long before. The block that appears in 0x00458EE0,
 * 0x004590F0, 0x00459300, 0x00459420, 0x004597B0, 0x004599A0 and 0x00459BE0 --
 * the one that maps an object's table record back to an index by comparing it
 * against ADDR_OBJ_TABLE_RECORDS + 0, 0x100, 0x200 and 0x300 -- is that
 * function's body: the two army-4 returns, the multiplayer kind-7 refusal, the
 * useRec3 choice, the CommArmyOfSlot compare and both AllyFlag calls, in order.
 *
 * Identified by STRUCTURE and not by a similarity score. Normalising registers
 * and diffing gave 0.52 with a nine-instruction run, which is suggestive and
 * proves nothing -- the boundaries were guessed. Reading army.cpp's C beside
 * the block settles it, arm for arm.
 *
 * THE SEVEN COPIES ARE NOT ALL THE SAME, and that is the part worth having
 * before any of them is written. 0x00458EE0 hoists an `obj->army == 4` refusal
 * ABOVE the block and sends it to the FAILURE exit; 0x004590F0 leaves it where
 * ArmyAlliedWithObj has it, where army 4 returns ALLIED. So mode 4's pick
 * refuses a neutral object outright and mode 5's treats it as allied -- one
 * `je` target apart, and invisible to anything but a diff.
 *
 * That is why these are worth writing as calls to ArmyAlliedWithObj with the
 * hoisted guards written out, rather than seven transcriptions of one body. */

/* IT NAMES ITSELF -- "-->Trooper Want Item Received" -- so this is the
 * kind-0x19 receiver for TrooperWantItemSend, and the two together are the
 * whole pickup protocol. Reconstructed as RecvTrooperWantItem; see
 * armymsg.cpp. Its five strings: */
#define ADDR_RECV_TROOPER_WANT_ITEM 0x0044C680u  /* void(void *msg) */
#define ADDR_STR_WANT_RECV_HDR     0x0048ACF8u
/* ADDR_STR_RECV_DROP_GONE already names 0x0048ACC0 -- the two receivers
 * share the one "but we handled it" line. Grepped the ADDRESS, and the
 * ratchet caught it when I had only grepped the name. */
#define ADDR_STR_TELL_PICKUP       0x0048AC9Cu
#define ADDR_STR_TELL_DROP         0x0048AC78u
#define ADDR_STR_REQ_PICKUP_OK     0x0048AC48u
#define ADDR_STR_REQ_PICKUP_DENY   0x0048AC20u
#define ADDR_STR_REQ_DROP          0x0048ABF8u
/* Reconstructed. The receiver for the message TrooperDropItemSend sends, so
 * the sender, this and TrooperDropItem itself are one closed group now. Four
 * log lines, all gated on COMM_OFF_VERBOSE. */
#define ADDR_RECV_TROOP_DROP_ITEM 0x0044C9C0u /* eTROOPER_DROP_ITEM_MESSAGE */
#define ADDR_STR_RECV_DROP_HDR    0x0048ADACu
#define ADDR_STR_RECV_DROP_GONE   0x0048ACC0u
#define ADDR_STR_RECV_DROP_DONE   0x0048AD80u
#define ADDR_STR_RECV_DROP_MINE   0x0048AD54u
/* 0x0044BFA0, two callers -- one of them the kind-0x19 RECEIVER, which
 * re-sends. The same 0x1C-byte record TrooperDropItemSend fills, under kind
 * 0x19 instead of 0x21, with two differences: the point comes from the ITEM's
 * own position rather than from the caller, and the request is an ARGUMENT
 * rather than the literal DO_DROP. Reconstructed. */
#define ADDR_TROOPER_WANT_ITEM_SEND 0x0044BFA0u /* void(troop,item,req,slot,q) */
#define AM2_MSG_TROOPER_WANT_ITEM 0x19u
#define ADDR_STR_WANT_SEND_HDR    0x0048A900u
#define ADDR_STR_WANT_PICKUP      0x0048A8DCu
#define ADDR_STR_WANT_DROP        0x0048A8B8u
#define ADDR_STR_DO_PICKUP        0x0048A898u
#define ADDR_STR_DO_DROP          0x0048A878u
/* Reconstructed. The receiver for eTROOPER_SET_WEAPON_MESSAGE, and the twin
 * of armymsg.cpp's SendTrooperSetWeapon: put the weapon's uid in the
 * trooper's inventory SLOT, set the soldier kind from the weapon's code, and
 * select that slot. Its three log lines are NOT gated on COMM_OFF_VERBOSE,
 * unlike the two Vehicle Exit ones. */
#define ADDR_RECV_TROOP_SET_WEAPON 0x0044C3E0u /* eTROOPER_SET_WEAPON_MESSAGE */
#define MSG_SETWEAPON_OFF_TROOPER  0x04u
#define MSG_SETWEAPON_OFF_WEAPON   0x08u
#define MSG_SETWEAPON_OFF_SLOT     0x18u  /* the inventory slot, 0..5 */
#define ADDR_STR_RECV_SETW_LINK    0x0048AB14u
#define ADDR_STR_RECV_SETW_NO_TROOP 0x0048AAD8u
#define ADDR_STR_RECV_SETW_NO_WEAP 0x0048AA9Cu
#define AM2_MSG_TROOPER_DROP_ITEM 0x21u
#define AM2_MSG_TROOP_FIRST       0x16u
#define AM2_MSG_TROOP_LAST        0x22u
#define ADDR_STR_UNKNOWN_TROOP_MSG 0x0048AB5Cu
#define ADDR_STR_GOT_DROP_ITEM     0x0048ABC0u
#define ADDR_STR_GOT_SET_WEAPON    0x0048AB88u
#define ADDR_VEHICLE_MSG_RECV    0x0045E590u  /* void(msg *, int32_t army) */
/* Reconstructed. Eleven arms over message kinds 0x1B..0x25, and the four in
 * the middle -- 0x20 through 0x23 -- fall to the "unknown" log rather than to
 * a handler. That is corroboration rather than a gap: orig.h already records
 * AM2_MSG_TROOPER_WEAPON (0x22) as "handled somewhere else entirely", and
 * AM2_MSG_DEATH is 0x23. Two message families share one number space and this
 * dispatcher owns only its own end of it.
 *
 * The seven handlers stay original and are reached by address. Only the FIRST
 * takes the army; the other six take the message alone, which is why the
 * dispatcher's second parameter looks unused at six of its seven call sites.
 *
 * Kind 0x25 is the one this project can name: ADDR_VEHICLE_DROP_OCCUPANT
 * sends it. The rest are numbered because the number is what is established. */
#define ADDR_RECV_VEHICLE_1B     0x0045EB10u  /* void(msg *, int32_t army) */
#define ADDR_RECV_VEHICLE_1C     0x0045E980u  /* void(msg *) */
#define ADDR_RECV_VEHICLE_1D     0x0045E630u
#define ADDR_RECV_VEHICLE_1E     0x0045E810u
#define ADDR_RECV_VEHICLE_1F     0x0045E860u
#define ADDR_RECV_VEHICLE_24     0x0045EA30u
#define ADDR_RECV_VEHICLE_EXIT   0x0045EAA0u  /* the twin of the sender */
/* 0x0045ADD0, 60 bytes, one caller -- the receive side of a unit leaving a
 * vehicle, and the exact mirror of what army.cpp does locally: find the uid in
 * the vehicle's VEHICLE_OFF_PTR_LIST and drop that slot, clear the unit's
 * OBJ_OFF_RIDING, and give it the vehicle's OBJ_OFF_HEIGHT_SET so it steps out
 * at the right height. Reconstructed. */
#define ADDR_VEHICLE_TAKE_OUT    0x0045ADD0u  /* void(uint32_t uid, void *veh) */
#define ADDR_STR_VEH_EXIT_RECV   0x0048C424u
#define AM2_MSG_VEHICLE_FIRST    0x1Bu
#define AM2_MSG_VEHICLE_LAST     0x25u
#define ADDR_STR_UNKNOWN_VEH_MSG 0x0048C2ACu
/* Who won, written only by the two game-over arms of the army dispatcher. */
#define ADDR_GAME_WINNER         0x00512300u  /* int32_t */
/* Gates the WON arm: with this clear, a win is recorded exactly as a loss.
 * 0x0044D110 is the only writer, and it sets and clears it in three places. */
#define ADDR_WIN_ENABLED         0x00512304u  /* int32_t */
#define AM2_MENU_REQUEST_GAME_OVER 0x22u
#define ADDR_RECV_START_GAME_MSG 0x00411100u  /* "ReceiveStartGameMsg for %d Players.  Seed is %d " */
/* 0x004114E0, "ReceivePlayerMsg for %d Players. I reckoned there were %d
 * Players ". The host's whole view of the lobby, arriving at a client. */
#define ADDR_RECV_PLAYER_MSG     0x004114E0u  /* void(msg *, int32_t dpid) */
/* Four int32_t, one per slot, and the loop that fills them stops on reaching
 * ADDR_SCORE_LIMIT -- the NEXT global -- rather than on a count. The same
 * shape as the registration table walking up to ADDR_SCRIPT_CONDITIONS.
 *
 * IT IS A POINTS BUDGET, which this comment used to say was unestablished.
 * LoadArmyPlacement settles it and needs nothing else to: the slot's entry is
 * what CanAffordUnit compares a unit type's cost against, and what
 * ADDR_MAKE_PLACED_UNIT subtracts that cost from as each unit goes down. That
 * agrees with the two facts already recorded -- 0x00431E10 sets it from a
 * lobby field through atoi, and ReceivePlayerMsg carries it to every player,
 * which is what a per-army army-size setting has to do. Renamed rather than
 * aliased. */
#define ADDR_ARMY_POINTS         0x00515FE0u  /* int32_t[4] */
/* An options bitmask, carried in the player message beside ADDR_GAME_OVER_FLAGS
 * and set from the OPTIONS checkboxes. One use of it is now known: CanAffordUnit
 * ANDs it with a unit type's UNIT_TYPE_OFF_GAME_MASK, so a type declares which
 * game types it may be placed in. That is one use of seven and not enough to
 * rename on. */
#define ADDR_GAME_SETTING_22C    0x00515FDCu  /* int32_t, beside ADDR_GAME_OVER_FLAGS */
/* The OPTIONS dialog is DECLARED rather than built: a 43-record table at
 * 0x004865B8, 0x24 bytes each, is the whole screen. Every record is one
 * checkbox -- where it sits, what it is called, which bit of which mask it
 * owns, and whether it heads a group.
 *
 *   +0x00 int32   index of the checkbox in the parent's widget array
 *   +0x04 int32   x; 0x48 and 0x13B are the two columns, and a group
 *                 header sits ten pixels left of its own column
 *   +0x08 int32   y; 0x28 and then 0x11 per row
 *   +0x0C int32   non-zero: this record heads a group
 *   +0x10 int32   first widget of the group
 *   +0x14 int32   last widget of the group, inclusive
 *   +0x18 int32   the bit this checkbox owns
 *   +0x1C int32   which mask that bit is in -- non-zero picks
 *                 ADDR_GAME_OVER_FLAGS, zero picks ADDR_GAME_SETTING_22C
 *   +0x20 char *  the label
 *
 * The left column is records 0..21 and the right 22..42, and the split at
 * +0x1C follows the columns exactly: one global per column. The five group
 * headers are "POWER-UPS", "MISCELLANEOUS", "TROOPERS", "ASSETS" and the
 * record at 28.
 *
 * A widget index is NOT a record index -- the last group runs to 42, past the
 * end of the table -- so the parent's array is the longer of the two. */
/* The end is 0x00486BC4, and the literal in the image is 0x00486BDC.
 *
 * They differ because the original walks with a cursor 0x18 bytes INTO each
 * record, so its bound is 0x18 past the last record's base. Taking that
 * literal as a record bound runs one record past the table and lands in the
 * label strings -- "Heavy MG Pillbox" read as a widget index, which is a wild
 * pointer and froze the frame loop on the first click of DEFAULT. */
#define ADDR_OPTION_TABLE        0x004865B8u
#define ADDR_OPTION_TABLE_END    0x00486BC4u  /* one past the last record */
#define AM2_OPTION_COUNT         43
#define AM2_OPTION_STRIDE        0x24u
#define AM2_OPTION_OFF_WIDGET    0x00u
#define AM2_OPTION_OFF_GROUP     0x0Cu
#define AM2_OPTION_OFF_FIRST     0x10u
#define AM2_OPTION_OFF_LAST      0x14u
#define AM2_OPTION_OFF_BIT       0x18u
#define AM2_OPTION_OFF_WHICH     0x1Cu
/* The parent holds the checkboxes in an array at 0x0064, the same shape the
 * CONTROLS dialog uses for its key rows. */
#define OPTION_PARENT_BOXES      0x64u
/* 0x00432710: the DEFAULTS button. It does not read the current settings at
 * all -- it calls ResetPairMask, which manufactures the two default masks, and
 * fills every checkbox from those. */
#define ADDR_OPTIONS_DEFAULTS    0x00432710u  /* void(AM2_Widget *button) */
/* 0x004327A0, and it names itself: "Options changed by host." The apply --
 * build both masks from scratch out of what is ticked, so unticking is handled
 * by simply not contributing and nothing reads the previous value. */
#define ADDR_OPTIONS_APPLY       0x004327A0u  /* void(AM2_Widget *button) */
/* 0x00432830: ask for the options menu -- 7 if we are the host and 9 if we
 * are not, computed with the usual neg/sbb. */
#define ADDR_OPTIONS_REQUEST     0x00432830u  /* void(void) */
/* 0x00432870: a group header was clicked, so push its own tick down onto
 * every checkbox in its range, and disable them while it is set. */
#define ADDR_OPTIONS_SYNC_GROUP  0x00432870u  /* void(AM2_Widget *header) */
#define AM2_MENU_REQUEST_OPTIONS      7
#define AM2_MENU_REQUEST_OPTIONS_VIEW 9
/* The two in front of the group are one `jmp` each to ADDR_DIALOG_DESTRUCT and
 * ADDR_WIDGET_UPDATE_CANCEL, both reconstructed -- the alias shape.
 *
 * The first is NOT the options dialog's alone, which is what naming it from
 * its neighbours would have said. Slot 0 of BOTH 0x0046FA0C (ENTER BATTLE
 * NAME) and 0x0046FA34 (HOST OPTIONS) is 0x0042FF40, the shared scalar
 * deleting destructor, and that is what calls it -- so it is the destructor
 * for the whole multiplayer dialog pair. The second really is options-only:
 * it appears in one vtable, as slot 2 of 0x0046FA34. */
/* The three BUTTON classes the multiplayer host/join panel builds one of per
 * player row. All three derive from the base button (ADDR_BUTTON_BASE_CTOR)
 * and all three carry the row they belong to in the base's 0x0058, which is
 * how their handlers know which player they are for.
 *
 * The names came from the SHAPES first -- a string with two inks, and two
 * 18x20 buttons distinguished only by one having a RIGHT handler and
 * auto-repeat. Reading the HANDLERS settled them properly: the one at column
 * 134 cycles a player's colour and its non-host path is SendColorMsg, and the
 * one at 191 cycles a team and sends SendTeamMsg. So they are COLOUR and TEAM
 * and the shape names ("toggle", "spinner") are gone. */
#define ADDR_MP_NAME_CTOR      0x004329A0u /* thiscall, ret 0x24 */
#define VTABLE_MP_NAME         0x0046FA48u
/* Its PAINT, and the row has three states with a different text source each:
 *
 *   an occupied slot flagged in ADDR_PAUSE_FLAGS shows "Not responding"
 *   followed by one DOT per AM2_MP_DOT_MS of silence -- the count is
 *   `(GetTickCount() - AM2_PLAYER_HEARD) * 6 / 45000`, which is 7,500 ms a
 *   dot written so the multiply keeps the division exact;
 *
 *   an occupied slot that is answering shows the name at COMM_OFF_PLAYERS,
 *   with its ink and fill from the two per-slot helpers;
 *
 *   and an empty one shows "-- Computer --" or "-- Open --" on
 *   AM2_PLAYER_ACTIVE.
 *
 * The bit tested is `0x800 << slot`, so the four slots occupy pause-mask bits
 * 11..14 -- which is what says that mask is not only about pausing.
 *
 * ADDR_HUD_MESSAGE_COLOUR is that address's SECOND reading: it went in as the
 * colour ADDR_HUD_MESSAGE draws with, and here it is the row fill for a silent
 * player. Recorded rather than renamed. */
#define ADDR_MP_NAME_PAINT     0x00432A70u /* thiscall void(w, RECT) */
/* Its ink, paper and the setter are ADDR_MP_NAME_INK, ADDR_MP_NAME_PAPER and
 * ADDR_MP_NAME_SET_INK, all three already reconstructed; the fields are
 * MPBTN_OFF_ROW and the MPNAME_OFF_* block further down. None of that was
 * grepped for before a second set of names was written beside it, and all
 * three ratchets said so at once -- checkpatches on the aliases, checkseams
 * because the "helpers" were OURS, checkoffsets on the duplicated fields.
 *
 * MPNAME_OFF_FLAG earns a note rather than a rename: this paint passes it as
 * DrawTextClipped's FONT, and 1 -- which the block already records the panel
 * writing -- is the font every other widget in this family uses. */
#define AM2_PLAYER_HEARD       0x26Cu /* uint32_t, GetTickCount when last seen */
#define AM2_MP_PAUSE_BIT0      0x800u /* shifted by the slot */
#define AM2_MP_DOT_NUM         6      /* the exact (x*6)/45000 the game uses */
#define AM2_MP_DOT_DEN         45000
#define AM2_STR_NOT_RESPONDING 0x004873ECu /* "Not responding%s" */
#define AM2_STR_PCT_S          0x004852B4u /* "%s" */
#define AM2_STR_COMPUTER_SLOT  0x004873DCu /* "-- Computer --" */
#define AM2_STR_OPEN_SLOT      0x004871A0u /* "-- Open --" */
#define ADDR_ON_MP_NAME        0x00432D50u
/* The name button's own destructor pair. It derives from the BASE WIDGET and
 * not from the dialog, so this one jumps to ADDR_WIDGET_DESTRUCT -- which is
 * what says the three row buttons are widgets in a panel rather than dialogs
 * of their own. */
#define ADDR_MP_NAME_DELETE    0x00432A40u /* thiscall obj *(obj, flags) */
#define ADDR_MP_NAME_DESTRUCT  0x00432A60u /* thiscall void(obj) */
#define AM2_MP_NAME_SIZE       0x74u
#define MPBTN_OFF_ROW          0x58u   /* which player row, in every one */
#define MPNAME_OFF_TEXT        0x68u   /* const char *, the name shown */
#define MPNAME_OFF_FLAG        0x6Cu   /* int32_t, 1 from the panel */
#define MPNAME_OFF_INK         0x70u   /* uint8_t */
#define MPNAME_OFF_PAPER       0x71u   /* uint8_t */

#define ADDR_MP_COLOUR_CTOR    0x00432E20u /* thiscall, ret 0x0C */
#define VTABLE_MP_COLOUR       0x0046FA5Cu
#define ADDR_ON_MP_COLOUR      0x00432EC0u
#define AM2_MP_COLOUR_SIZE     0x68u

#define ADDR_MP_TEAM_CTOR   0x00433030u /* thiscall, ret 0x0C */
#define VTABLE_MP_TEAM      0x0046FA70u
#define ADDR_ON_MP_TEAM_LEFT   0x004330E0u
#define ADDR_ON_MP_TEAM_RIGHT  0x00433190u
/* Both small ones are the same 18 by 20 at a column the caller picks. */
#define AM2_MP_SMALL_W         0x12
#define AM2_MP_SMALL_H         0x14
/* The panel's four arrays of one widget per player row, and the fourth is
 * the per-row colour SELECTION the colour handler cycles. */
#define MP_PANEL_OFF_TYPE_BOX  0x204u   /* the list box a rules file fills */
#define LISTBOX_OFF_ROWS       0x60u    /* its string list */
/* Two of the list box's own fields, both settled by the three sites that READ
 * them rather than by the constructor that writes them.
 *
 * +0x5C is the SELECTED ROW: the base constructor writes -1, one site writes a
 * row index into it, and the painter compares the row it is drawing against
 * it. -1 for "none" and a row index otherwise.
 *
 * +0x4C is non-zero for a list that cannot be interacted with. All three
 * readers are `test eax,eax; jne <skip>` and what each skips says the same
 * thing: the selected-row colour, the highlight, and the whole keyboard arm of
 * the update. TextListCtor sets it to 1, which is what a message log wants. */
#define LISTBOX_OFF_READ_ONLY  0x4Cu    /* non-zero: no selection, no keys */
#define LISTBOX_OFF_SELECTED   0x5Cu    /* int32 row index, -1 for none */
/* 0x00433290, one caller, and the TEXT LIST -- the message log's list box.
 * A thiscall constructor that runs ADDR_LISTBOX_CTOR, takes VTABLE_TEXT_LIST,
 * copies five palette indices into TEXTLIST_OFF_COLOURS and marks itself read
 * only. Its one caller passes ADDR_LOG as the row callback, which in this
 * build is a bare `ret`, so the list has no per-row hook at all.
 *
 * 0x00433330 is the scalar deleting destructor and 0x00433350 is a
 * one-instruction `jmp` to the base destructor at 0x00455090 -- the class adds
 * no teardown of its own. */
#define ADDR_TEXTLIST_CTOR     0x00433290u
#define ADDR_TEXTLIST_DELETE   0x00433330u
#define ADDR_TEXTLIST_DESTRUCT 0x00433350u
/* The four palette slots TextListCtor reads that had no name. Named by the
 * COLOUR they hold, which is a fact -- SetGamePalette's own table in
 * win32/palette.cpp matches each address to an RGB triple -- and not by a role,
 * which neither of their two readers establishes: one is this constructor
 * copying them into a widget and the other copies them into a second table. */
#define ADDR_COLOUR_DARK_GREEN 0x004FDF74u  /* 32 71 26 */
/* Both named from win32/palette.cpp's own (address, r, g, b) table rather
 * than from the cheat arms that read them -- the arms say only "a colour",
 * and SetGamePalette is the writer. Reading the writer's disassembly instead
 * would have got them wrong: the compiler interleaves, so the `mov byte
 * [global], al` that follows a push block belongs to the call BEFORE it, not
 * the one those pushes are building. */
#define ADDR_COLOUR_BLUE       0x004FDF7Cu  /* 00 00 FF */
#define ADDR_COLOUR_DARK_BLUE  0x004FD760u  /* 00 00 AC */
#define ADDR_COLOUR_OLIVE      0x004FE1AEu  /* 65 57 30 */
#define ADDR_COLOUR_STEEL_BLUE 0x004FE088u  /* 32 5D 8A */
#define ADDR_COLOUR_DARK_GREY  0x00502CE4u  /* 54 54 54 */
#define MP_PANEL_OFF_PREVIEW   0x218u   /* the map thumbnail widget */
#define MP_PANEL_OFF_NAMES     0x220u
#define MP_PANEL_OFF_COLOURS   0x230u
#define MP_PANEL_OFF_TEAMS     0x240u
#define MP_PANEL_OFF_COLOUR_SEL 0x268u
#define MP_PANEL_OFF_SCORE_TEXT 0x1E4u  /* char[], the score limit as text */
#define MP_PANEL_OFF_ARMY_ROWS  0x258u  /* AM2_Widget *[4], each wrapping an edit */
#define AM2_MP_ROW_INNER        0x58u   /* the row's edit, whose text is at +0x58 */
#define ADDR_MP_PANEL_UPDATE    0x004316D0u /* thiscall void(this) */
#define ADDR_FMT_INT            0x00476A04u /* "%i" */
#define AM2_MP_TEAM_MAX        12

#define ADDR_MP_DIALOG_DESTRUCT 0x004326F0u /* thiscall void(AM2_Widget *) */
#define ADDR_OPTIONS_UPDATE   0x00432700u /* thiscall void(AM2_Widget *) */
/* The pair a client checks before agreeing to play: a constant in .rdata and a
 * value computed at run time. SendPlayerMsg puts both in the record and this
 * compares them; a mismatch says "has a different version of the game". They
 * are read in those two places and nowhere else. */
#define ADDR_GAME_VERSION        0x00475894u  /* int32_t, 1 in this build */
#define ADDR_DATA_CHECKSUM       0x004FC8B4u  /* int32_t */
/* 0x00431E10, "%s does not have rules." and "%s.txt" -- loads a map's rules
 * and answers with the code SendMapMsg reports. */
#define ADDR_CHECK_MAP_RULES     0x00431E10u  /* int32_t(int32, int32, int32) */
/* Offsets into the player message. The per-player records start at 0x00A8 and
 * are 0x60 apart; the original addresses them from 0x00AC, so the id reads as
 * rec[-4]. */
#define MSG_PLAYER_COUNT         0x08u
#define MSG_PLAYER_CONNECTED     0x0Cu
#define MSG_PLAYER_HAS_MAP       0x14u
#define MSG_PLAYER_SCORE_LIMIT   0x18u
#define MSG_PLAYER_MAP_NAME      0x1Cu
#define MSG_PLAYER_LEVEL_NAME    0x5Cu
#define MSG_PLAYER_MAP_SUM       0x9Cu
#define MSG_PLAYER_RULE_SUM      0xA0u
#define MSG_PLAYER_RULE_ARG      0xA4u
#define MSG_PLAYER_RECORDS       0xA8u
#define MSG_PLAYER_STRIDE        0x60u
/* Fields inside one of those records, named from SendPlayerMsg, which is the
 * writer. It addresses them through a cursor sitting 0x58 INTO each record --
 * the reader's own note above says the same of its base, one offset further
 * on -- so the displacements run [ebx-0x58] to [ebx+4] and none of them is a
 * negative field. */
#define MSGREC_OFF_ID            0x00u  /* the player id */
#define MSGREC_OFF_FIELD_04      0x04u  /* logged as the third %d */
#define MSGREC_OFF_ZERO          0x08u  /* written 0, never read here */
#define MSGREC_OFF_POINTS        0x10u  /* that army's ADDR_ARMY_POINTS entry */
#define MSGREC_OFF_NAME          0x14u  /* the player name, strcpy'd in */
#define MSGREC_OFF_FIELD_54      0x54u
#define MSGREC_OFF_FIELD_58      0x58u
#define MSGREC_OFF_FIELD_5C      0x5Cu
/* The message SendPlayerMsg fills. NOT 0x004FC3CC, which is the first address
 * that function touches and is 0x14 bytes in: +0x00..+0x07 are an
 * AM2_ArmyMsgHdr the send fills, and SendGameMsg is handed this. */
#define ADDR_PLAYER_MSG          0x004FC3B8u
#define MSG_PLAYER_OVER_FLAGS    0x228u
#define MSG_PLAYER_SETTING_22C   0x22Cu
#define MSG_PLAYER_VERSION       0x230u
#define MSG_PLAYER_CHECKSUM      0x234u
#define REC_PLAYER_ID            0x00u
#define REC_PLAYER_COLOUR        0x04u
#define REC_PLAYER_SETTING       0x10u
#define REC_PLAYER_NAME          0x14u
#define REC_PLAYER_TEAM          0x54u
#define REC_PLAYER_F270          0x58u
#define REC_PLAYER_WAS_HERE      0x5Cu
/* 0x00410890, "RemoteGamePause from %x; playerIndex== %d paused = %d
 * pauseflags = %x (%x) (msg Pause=%x)". One pause bit per player per reason,
 * and the two reasons have their own blocks: 0x0008 in the message's flags
 * selects 0x10<<slot and 0x10000 selects 0x20000<<slot. */
#define ADDR_RECV_GAME_PAUSE     0x00410890u  /* void(msg *, int32_t dpid) */
#define MSG_PAUSE_FLAG_A         0x8u        /* picks the 0x10 block */
#define MSG_PAUSE_FLAG_B         0x10000u    /* picks the 0x20000 block */
#define PAUSE_BIT_A_SLOT0        0x10u
#define PAUSE_BIT_B_SLOT0        0x20000u
/* 0x00411BD0. The self-naming sweep credited it with "TIMING OUT PLAYER",
 * which belongs to 0x00411C20 -- the two share a functions.tsv entry. This one
 * carries no string at all, so the name is ours, from the body: the host
 * dictating how we send. It stores the message's value as the comm object's
 * SEND FLAGS -- the third argument ArmyMessageFlush hands SendGameMsg -- and
 * two more fields into our own flow record. Client only. */
#define ADDR_RECV_FLOW_CONTROL   0x00411BD0u /* void(msg *, int32_t dpid) */
#define COMM_OFF_SEND_FLAGS      0x414u
/* 0x00411C20, "TIMING OUT PLAYER %d %s" -- a different function, still
 * original, and one of the AM2_WM_PLAYER_GONE senders. */
#define ADDR_CHECK_PLAYER_TIMEOUT 0x00411C20u
/* 0x0469, and NOTHING handles it. ReceiveStartGameMsg is the only sender in
 * the image and WndProc has no case for it, so DefWindowProc eats it. Named
 * from what posts it, as the other six are. */
#define AM2_WM_START_GAME          0x0469u
/* The seed the host chose, as it arrives: 0x0190 into the start-game record.
 * SendGameStartMsg is where it was picked. */
#define MSG_START_OFF_SEED         0x190u
#define ADDR_GET_PAUSE_FLAGS     0x00426840u  /* uint32(void) */
#define ADDR_STR_SEND_BADPLAYER  0x004754ACu
#define ADDR_STR_SEND_BADPARAM   0x00475478u
#define ADDR_STR_SEND_NOENTRY    0x00475450u
/* Comm object fields the send path uses. Slots are at COMM_SLOT_BASE with
 * stride 0x70; +0x08 is the player id and +0x58 the unacknowledged counter. */
#define COMM_OFF_STAT_INDEX      0x004u   /* ring cursor, 0..29 */
#define COMM_OFF_STAT_TIMES      0x00Cu   /* uint32[30] */
#define COMM_OFF_STAT_SIZES      0x084u   /* uint32[30] */
#define COMM_OFF_STAT_MAX        0x1F0u
#define COMM_OFF_STAT_BYTES      0x1F8u
#define COMM_OFF_STAT_PACKETS    0x200u
/* The bandwidth counters, all seven named from ADDR_COMM_REPORT_STATS' own
 * format strings rather than from the reset that clears them:
 * " SEND    BANDWIDTH (%6d samples) MAX was %6d;  %6d (%3d%%) exceeded design
 * spec(%d)", and the same line for RECEIVE. So the three per direction are a
 * sample count, the largest seen, and how many of those samples went over --
 * the percentage is computed, not stored, and the spec is the literal 2880.
 *
 * The STAT_ prefix is send and RX_ is receive, which is the vocabulary the
 * scalars above already use. */
#define COMM_OFF_STAT_BW_MAX     0x458u   /* int32_t, largest send sample */
#define COMM_OFF_RX_BW_MAX       0x45Cu   /* int32_t, largest receive sample */
#define COMM_OFF_STAT_BW_SAMPLES 0x460u   /* int32_t */
#define COMM_OFF_RX_BW_SAMPLES   0x464u   /* int32_t */
#define COMM_OFF_STAT_BW_OVER    0x468u   /* int32_t, samples over the spec */
#define COMM_OFF_RX_BW_OVER      0x46Cu   /* int32_t */
#define AM2_COMM_BW_DESIGN_SPEC  0xB40    /* 2880, printed as "design spec" */
/* Stamped from GetTickCount by the reset and read only by the report, which
 * divides the elapsed milliseconds by 1000 and clamps the result up to 1. */
#define COMM_OFF_STATS_SINCE     0x40Cu   /* uint32_t, ms */
/* Both stat rings are 30 samples, and the four arrays TILE: 0x00C, 0x084,
 * 0x0FC and 0x174 are 0x78 apart, which is 30 dwords, and the last ends
 * exactly where COMM_OFF_RX_MAX begins. A layout that tiles is the check that
 * no base is off. */
#define AM2_COMM_STAT_SAMPLES    30
/* 0x0040F400, thiscall, three callers. Prints the four statistics lines the
 * reset above clears. Each line is gated on its own denominator, which is what
 * makes the seven unsigned divisions below it safe, and the elapsed seconds
 * are clamped UP to 1 for the same reason. */
#define ADDR_COMM_REPORT_STATS   0x0040F400u  /* thiscall void(this) */
#define ADDR_STR_SEND_BANDWIDTH  0x00475808u
#define ADDR_STR_RECV_BANDWIDTH  0x004757B0u
#define ADDR_STR_SENT_PACKETS    0x00475740u
#define ADDR_STR_RECV_PACKETS    0x004756D0u
/* The two thresholds AppendTroopState throttles on, and they are measured in
 * SEQUENCES rather than milliseconds -- the counter is the sending player's
 * +0x94, which TROOPER_OFF_LAST_SEQ is named for -- so a slow machine
 * throttles by the same amount as a fast one. COARSE is the shorter of the
 * two: soon after a send only a LARGE change gets through, and after the
 * longer INTERVAL any change does. */
#define COMM_OFF_SEND_COARSE     0x478u  /* uint32_t */
#define COMM_OFF_SEND_INTERVAL   0x47Cu  /* uint32_t; forced to 1 for Sarge */
#define COMM_OFF_OUR_PLAYER_ID   0x3CCu
#define COMM_OFF_PLAYER_COUNT    0x3D0u
/* Six fields CommRemovePlayer clears or stamps on its way out, and the only
 * thing established about them is that they move together: 0x35C, 0x360 and
 * 0x364 have ten, ten and nine touchers each, 0x368 is a byte, and 0x3A8 and
 * 0x3AC have one apiece. 0x360 takes the departing slot's own index, so the
 * group is plausibly a record of who just left -- plausibly, and the names say
 * only where they are. Reading the other nineteen touchers is a job of its
 * own. */
#define COMM_OFF_FIELD_35C       0x35Cu
#define COMM_OFF_FIELD_360       0x360u   /* the departing slot's index */
#define COMM_OFF_FIELD_364       0x364u
#define COMM_OFF_FIELD_368       0x368u   /* uint8_t */
#define COMM_OFF_FIELD_3A8       0x3A8u
#define COMM_OFF_FIELD_3AC       0x3ACu
#define COMM_OFF_LOCAL           0x400u   /* set when the game is offline */
#define COMM_SLOT_OFF_NAME       0x00Cu   /* 0x40-byte string; CommConstruct
                                           * clears it, StartSelectedGame writes
                                           * "Computer%d" into it */
#define COMM_SLOT_OFF_ID         0x008u
#define COMM_SLOT_OFF_UNACKED    0x058u
/* The two ready flags as offsets into the record, beside the absolute
 * COMM_ARMY_OFF_READY_TO_LOAD and COMM_ARMY_OFF_READY. Same fields; those two
 * are 0x20C higher and are used as `comm + slot * 112 + OFFSET`. */
#define COMM_SLOT_OFF_READY_TO_LOAD 0x064u
#define COMM_SLOT_OFF_READY         0x068u
#define COMM_SLOT_OFF_FIELD_5C      0x05Cu
/* "Remove Player numPlayers now = %d \n" */
#define ADDR_STR_REMOVE_PLAYER   0x00475860u
#define COMM_STAT_RING           30u
#define ADDR_SESSION_LIST        0x004FA908u  /* void *, what the callback fills */
#define COMM_OFF_APP_GUID        0x3D4u       /* GUID *, set by CommConstruct */
/* Calls ADDR_RECORD_RESET on the object at 0x0051612C when there is one. */
/* Fifteen bytes: if ADDR_MENU_MSG_LIST is non-null, tail-jump to
 * ADDR_RECORD_RESET with it in ecx. That is a thiscall on the list, so this
 * empties the menu message log and does nothing else.
 *
 * It was ADDR_DROP_OBJ_51612C -- a placeholder named after the address it
 * reads, from a time when neither the global nor the callee had a name. Both
 * do now, so the placeholder is retired rather than left to be re-derived.
 * Two use sites.
 *
 * Note docs/functions.tsv gives this entry 160 bytes, which is a MERGE: the
 * function ends at 0x00431D7F and 0x00431D80 is a different one. */
#define ADDR_CLEAR_MENU_MSGS     0x00431D70u  /* void(void) */
#define ADDR_GAME_OPERATOR_NEW   0x00464900u  /* void *(size_t); MSVC operator new */
#define ADDR_START_MULTIPLAYER   0x0042F310u  /* void(void), a button handler */
#define ADDR_MP_DATA_PROBE       0x0048700Cu  /* "data\\mpalpine" */
#define ADDR_DATA_MISSING_TEXT   0x00486FA4u  /* "...multi-player with a compact installation." */
#define ADDR_DATA_MISSING_CAPTION 0x00486FFCu /* "Data Missing" */
#define ADDR_MENU_REQUEST_SET    0x00511DC4u  /* int32_t, non-zero when one is pending */

/* Copies the pending settings block at 0x00516xxx over the active one at
 * 0x00515Fxx, resets the comm slots and copies the two player-name strings.
 * Named for what its body does; stays original. */
#define ADDR_APPLY_GAME_SETTINGS 0x0042F170u  /* void(void) */
#define ADDR_FMT_COMPUTER_N      0x00486EC4u  /* "Computer%d" */
#define ADDR_START_SELECTED_GAME 0x0042ECF0u  /* void(void), a button handler */
#define COMM_OFF_TRACE           0x470u
#define COMM_OFF_LOG             0x474u

/* Startup and shutdown steps WinMain drives. Named where the imports or the
 * COM calls inside them say what they are, left at their address where they do
 * not -- these are pure game logic and stay in the original image. */
#define ADDR_CHECK_BASE_PATH     0x00422DB0u  /* getcwd, complains past 255 chars */
#define ADDR_DETECT_CPU_SPEED    0x0040B2B0u  /* void(void); logs "system speed" */
#define ADDR_SLOW_MACHINE        0x005125C0u  /* int32_t, the inverse of the above */
#define ADDR_WINCPUID_FN         0x004F9FE0u  /* cached cpuinf32.dll exports */
#define ADDR_CPUNORMSPEED_FN     0x004F9FE8u
#define ADDR_INIT_TIMER          0x00426C50u  /* QueryPerformanceFrequency/Counter */
/* Variadic, and always returns 0 -- which is why both device bring-up routines
 * can `return ReportError(...)` and mean "failed". */
#define ADDR_REPORT_ERROR        0x0041E7A0u  /* int32_t(HRESULT, const char *fmt, ...) */

/* ---- device bring-up --------------------------------------------------
 *
 * The two functions that create every DirectDraw and DirectInput object the
 * game owns. Both are reconstructed in src/game/win32/device.cpp.
 *
 * They call DirectDrawCreate and DirectInputCreateA through the game's own
 * import thunks rather than through ours. For DirectDraw that is only tidiness;
 * for DirectInput it is required, because src/inject/dinput_hook.c works by
 * patching the game's IAT slot, and an import of our own would walk straight
 * past the hook and take the harness's input injection with it.
 */
#define ADDR_INIT_INPUT          0x00426D30u  /* int32_t(HWND); 1 on success */
#define ADDR_INIT_DIRECTDRAW     0x0041AA10u  /* HRESULT(HWND); 0 on success */
#define ADDR_DIRECTDRAWCREATE    0x00463396u  /* jmp [0x0046F00C] */
#define ADDR_DIRECTINPUTCREATE   0x00464410u  /* jmp [0x0046F014] -- the hooked slot */

/* DirectDraw. The game holds both interface generations: it queries v2 off v1
 * and then uses whichever one has the SetDisplayMode it wants, three arguments
 * on v1 and five on v2. */
#define ADDR_DIRECTDRAW2         0x004FE098u  /* IDirectDraw2 * */
#define ADDR_IID_DIRECTDRAW2     0x0046F338u  /* the game's own copy of the IID */
/* Both reconstructed in src/game/win32/surface.cpp.
 *
 * ClearSurface was called ADDR_ATTACH_PALETTE for one commit, guessed from its
 * call site in InitDirectDraw. It is nothing of the kind: vtable slot 5 is Blt,
 * and it is a colour fill. */
#define ADDR_CREATE_OFFSCREEN    0x0041B850u  /* surface *(w, h, caps, int32 key) */
#define ADDR_CLEAR_SURFACE       0x0041AD30u  /* int32_t(surface *, uint32_t colour) */
#define ADDR_CLEAR_REGION        0x0041CE20u  /* void(const RECT *, uint8_t) */
/* 0x00425000, nine callers. The loading bar: a rectangle from x=0xB0 to
 * 0xB0 + percent * 288 / 100, y 0x197 to 0x19F, filled on the PRIMARY surface
 * in ADDR_VIEW_RECT_COLOUR. The divide by 100 is MSVC's reciprocal multiply by
 * 0x51EB851F with a shift of 5. The name is ours. */
#define ADDR_PROGRESS_BAR        0x00425000u  /* void(int32_t percent) */
#define AM2_PROGRESS_X0          0xB0
#define AM2_PROGRESS_Y0          0x197
#define AM2_PROGRESS_Y1          0x19F
#define AM2_PROGRESS_WIDTH       288

/* DirectInput. The GUIDs and data formats are the game's own copies in .rdata,
 * so nothing here needs dxguid. */
#define ADDR_DINPUT              0x00512FD0u  /* IDirectInputA * */
#define ADDR_DI_MOUSE            0x00512FD4u  /* IDirectInputDeviceA * */
#define ADDR_DI_KEYBOARD         0x00512FD8u  /* IDirectInputDeviceA * */
#define ADDR_DI_DEVICE_3         0x00512FDCu  /* a third device, never created here */
#define ADDR_DI_MOUSE_ACQUIRED   0x00512FE0u  /* int32_t */
#define ADDR_SHUTDOWN_INPUT      0x00426EA0u  /* void(void) */
#define ADDR_ACQUIRE_MOUSE       0x00426F20u  /* void(void) */
#define ADDR_GUID_SYS_MOUSE      0x0046F5A8u
#define ADDR_GUID_SYS_KEYBOARD   0x0046F5B8u
#define ADDR_DF_MOUSE            0x0046FD80u  /* DIDATAFORMAT c_dfDIMouse */
#define ADDR_DF_KEYBOARD         0x0046FD68u  /* DIDATAFORMAT c_dfDIKeyboard */
#define ADDR_DIPROP_BUFFER_SIZE  0x004854F8u  /* DIPROPDWORD, the buffered-input size */
/* The keyboard's double buffer: two 256-byte state arrays and the two pointers
 * PollKeyboard SWAPS each poll, so which array is current alternates and the
 * pointers are the only way to know. Nothing to do with the mouse cursor,
 * which is what the names they went in under -- ADDR_KEYS_NOW_PTR and _B --
 * read as; "cursor" meant a cursor into a buffer, and next to ADDR_CURSOR_X
 * that is a trap. Renamed, not aliased. */
#define ADDR_KEYS_NOW_PTR        0x005127C8u  /* uint8_t *, this poll */
#define ADDR_KEYS_PREV_PTR       0x005127CCu  /* uint8_t *, the one before */
#define ADDR_KEYS_BUFFER_A       0x005125C8u  /* uint8_t[256] */
#define ADDR_KEYS_BUFFER_B       0x005126C8u
#define AM2_KEY_STATES           256
#define AM2_KEY_DOWN             0x80u        /* DirectInput's down bit */
/* Auto-repeat state, one entry per DIK scancode. PollKeyboard writes both and
 * 0x00427430 reads the second -- `KeyPressed(dik)` is `g_keyPressed[dik & 0xff]`,
 * which is how the array's length and purpose were established. */
#define ADDR_KEY_REPEAT_AT       0x005127D0u  /* uint32_t[256], GetTickCount due */
#define ADDR_KEY_PRESSED         0x00512BD0u  /* int32_t[256] */
/* Mouse state, all written by PollMouse and read by the menus and the game.
 * The deltas are cleared at the top of every poll and the buffered events
 * accumulated into them; 0x00426F40 turns them into an absolute cursor. */
#define ADDR_MOUSE_DX            0x00485458u  /* int32_t, this poll */
#define ADDR_MOUSE_DY            0x0048545Cu  /* int32_t */
#define ADDR_MOUSE_DZ            0x00485460u  /* int32_t, the wheel */
#define ADDR_MOUSE_BUTTON        0x00485470u  /* int32_t[3], 1 while down */
#define ADDR_MOUSE_CHANGED       0x0048547Cu  /* int32_t[3], differs from last */
/* int32_t[3]. Menu code sets one when it takes responsibility for a click
 * (0x004142C8); PollMouse clears it when the button comes back up. */
#define ADDR_MOUSE_CLAIMED       0x00485488u
/* Not only movement, despite the name it went in under: 0x00426F40 also sets
 * it when button 0 or button 1 CHANGES, so it is "the mouse did something". */
#define ADDR_MOUSE_MOVED         0x00485494u  /* int32_t */
#define ADDR_CURSOR_POINT        0x0048546Cu  /* two int16 -- the clamped
                                               * cursor, x then y, which is
                                               * what a press records */
/* Three {point, tick} pairs, one per button, stamped when that button goes
 * down. The point is the packed dword above, not a pair of ints. */
#define ADDR_MOUSE_PRESS         0x00485498u
/* The TIME the button went down, beside the point it went down at. The pair is
 * written together by the mouse handler at 0x00426FD8 and read together by
 * every click-versus-drag test in the image -- `GetTickCount() - this < 500`
 * and then ApproxDist(ADDR_MOUSE_PRESS, ADDR_CURSOR_POINT). Named from that
 * writer/reader pair; eleven sites touch it and naming it from any one of them
 * would have been the call-site mistake. */
#define ADDR_MOUSE_PRESS_MS      0x0048549Cu  /* uint32_t, GetTickCount ticks */
/* A SECOND press point and time, for the other button. The mouse handler at
 * 0x00427003 writes this pair exactly as 0x00426FD8 writes the one above, and
 * 0x00413E70 reads the time with a 200 ms window where the first pair's readers
 * use AM2_CLICK_MS. Two pairs, two windows; named from the writer/reader pair
 * the same way. */
#define ADDR_MOUSE_PRESS2        0x004854A0u  /* packed point */
#define ADDR_MOUSE_PRESS2_MS     0x004854A4u  /* uint32_t */
#define AM2_DOUBLE_CLICK_MS      200
/* The uid of the object the pointer is OVER, or 0. Cleared once a frame at
 * 0x00413E92, immediately before the mouse-selection interface runs, and set
 * by every pointer PICK that shows a hover overlay -- fourteen setters and one
 * clear, which is what makes it a hover slot rather than a selection. */
#define ADDR_POINTER_HOVER_UID   0x004854B8u  /* uint32_t */
#define ADDR_MOUSE_ACTIVITY      0x004854B0u  /* set from ADDR_GAME_CLOCK_MS on
                                               * any movement or button change */
/* The click ARBITER, and "nothing here reads it" was wrong -- 32 sites do.
 * PollMouse zeroes it as button 0 goes down, so every press starts unowned;
 * each HUD panel's update then does `if (!grab && mouseChanged) grab = me`
 * and acts only while `grab == me`. The first widget to look at a press owns
 * it and everyone downstream sees a grab that is not theirs and stays quiet,
 * which is how one click reaches one panel and not the map underneath.
 *
 * The token is an ADDRESS and not always the widget's: HudTopUpdate stores
 * `this + HUDLOG_OFF_BUTTON_SPRITE` when the press is on its rewind button
 * and plain `this` when it is anywhere else on the strip, so one widget
 * arbitrates two targets through one global. */
#define ADDR_MOUSE_GRAB          0x004854B4u  /* AM2_Widget *, or a field of one */
/* Read from 157 places and written from three, all of them in the state
 * machine -- ADDR_TAKE_MENU_REQUEST is one. What it MEANS is not established;
 * this records only that the mouse stamps it whenever there is input.
 *
 * It IS a clock, in milliseconds, and it took three goes to establish that.
 *
 * UpdateObjectScript skips an object while `obj[0xBC] >= this` and on
 * advancing sets `obj[0xBC] = frame->a + this`, which reads exactly like a
 * deadline against a rising clock -- so "clock" went in first. A live probe
 * then said no: 0x1F4 (500), unchanging, for twelve seconds of Boot Camp while
 * ComposeFrame climbed. That looked decisive and the name was changed to
 * ADDR_INPUT_CONTEXT.
 *
 * The probe was taken with a DIALOG up. Measured again in play with both Boot
 * Camp dialogs cleared, it reads 6344, 9427, 12509, 15595 on samples three
 * seconds apart -- about 1,027 units a second, which is milliseconds. It is 0
 * on the title screen and holds still while the game is paused, which is why a
 * dialog makes it look frozen.
 *
 * Two other users agree. CreateTimer treats it as `now`: a relative start is
 * added to it and an already-elapsed schedule is compared against it. And
 * ADDR_MOUSE_ACTIVITY is stamped FROM it whenever there is input, which is a
 * timestamp and needs a clock to be one.
 *
 * The lesson is about the probe, not the name. A value sampled only while the
 * game is paused cannot be shown to tick, and "it does not tick" was recorded
 * as a fact for months on exactly that evidence. Say what state the game was
 * in when a global was sampled. */
#define ADDR_GAME_CLOCK_MS       0x00511E04u  /* uint32_t, mission ms */
#define ADDR_MOUSE_EVENT         0x00426F40u  /* void(void), after every event */
/* PollInput is `call PollMouse; jmp PollKeyboard` and nothing else. */
#define ADDR_POLL_INPUT          0x00427420u  /* void(void) */
#define ADDR_POLL_MOUSE          0x00427070u  /* void(void) */
#define ADDR_POLL_KEYBOARD       0x004272D0u  /* void(void) */
/* The globals and literals the WinMain chain touches. Read out of the bodies
 * rather than guessed; see src/game/win32/winmain.cpp for what each does. */
#define ADDR_STR_BASE_PATH_LONG  0x00478954u  /* "The base path is longer..." */
#define ADDR_PERF_FREQ           0x00512350u  /* int64, QueryPerformanceFrequency */
#define ADDR_PERF_START          0x00512578u  /* int64, the startup counter */
#define ADDR_PERF_PERIOD         0x00512570u  /* double, milliseconds per tick */
/* THREE int16 THAT ARE NOT TIMER STATE, and the old names said they were.
 * They sat beside ADDR_PERF_FREQ and friends and InitTimer clears all three, so
 * they went in as ADDR_PERF_WORD_A/B/C -- named from the site that ZEROES them,
 * which this file already calls the weakest possible toucher.
 *
 * They have seven readers. Five are weapon-handler actions in the pointer band
 * and two more sit at 0x0045D0F6 and 0x0045D6F0, and every one copies the
 * triple straight into UNIT_OFF_FIRE_X, _Y and _Z. So they are the AIM POINT a
 * fire order is given, cleared once at startup. Renamed from the readers. */
#define ADDR_AIM_X               0x00512568u  /* int16_t -> UNIT_OFF_FIRE_X */
#define ADDR_AIM_Y               0x0051256Au  /* int16_t -> UNIT_OFF_FIRE_Y */
#define ADDR_AIM_Z               0x0051256Cu  /* int16_t -> UNIT_OFF_FIRE_Z */
#define ADDR_DBL_MS_PER_SEC      0x0046F990u  /* 1000.0 */
#define ADDR_DBL_MAX_PERIOD      0x0046F988u  /* 1.0 -- worse than 1 kHz loses */
#define ADDR_STR_HIGH_PERF       0x00485438u  /* "Using High Performance Counter\n" */
/* Was ADDR_SHUTDOWN_OBJ, which named the ONE teardown call site rather than
 * the object. It is a {capacity, count, items} ptr list of object UIDs, capped
 * at 64 and deduplicated on the way in, and adding to it also sets bit 0x400
 * on the object. Its callers are the HUD, the script and the unit code, and
 * the consumer at 0x00427990 walks it with our own army -- so "the selected
 * group" is the reading. ShutdownSubsystems empties it, which is all the old
 * name knew. */
#define ADDR_SELECTED_UIDS       0x00512308u  /* {capacity, count, items} */
/* 0x004137D0, one caller. The whole mouse-selection interface: a three-way
 * branch on the two globals below picks between letting a rubber band GO,
 * UPDATING one, and a plain CLICK, and inside the last two a CONTROL key
 * turns select into toggle. Its toggle inlines ToggleSelect's logic rather
 * than calling it, and differs: CONTROL is tested AFTER the pick here and
 * first there, and the non-toggle path also calls SetObjContext. */
#define ADDR_SELECTION_CLICK     0x004137D0u  /* void(void) */
/* The rubber band's other corner, in world space, and the flag that says one
 * is being dragged. ADDR_VIEW_RECT_ON says a band EXISTS; this says the mouse
 * is still down on it. Both are named from 0x004137D0's branch on them. */
#define ADDR_DRAG_ANCHOR         0x004FCF68u  /* two int16, world space */
#define ADDR_DRAG_ACTIVE         0x00485474u  /* int32_t */
/* Gates the plain-click path only -- the two drag paths do not read it. */
#define ADDR_CLICK_ENABLED       0x00485480u  /* int32_t */
/* The band is not published until the pointer has moved more than six from
 * the anchor, by ApproxDist.  Below that a drag is a click. */
#define AM2_DRAG_DEAD_ZONE       6
/* The predicate ObjectsInRect and WalkCellAtPoint are handed to decide what a
 * click may pick. Still original, and passed by address. */
#define ADDR_SELECTABLE_PRED     0x00413690u
/* And ADDR_SELECTED_UIDS is a MEMBER of the gameproc block rather than a
 * global of its own: it is ADDR_GAMEPROC_BLOCK + GAMEPROC_OFF_SELECTED, which
 * is what the second static-initialiser group builds. The constructor runs
 * InitPtrList on it and the destructor ClearPtrListAlias, both of which this
 * port already owns, so the group is four stubs plus the two bodies that do
 * the offset arithmetic. See the comm group above for why only half of it can
 * execute here. */
#define GAMEPROC_OFF_SELECTED    0x8A0u
#define ADDR_SEL_LIST_INIT       0x00424890u  /* cdecl, from the init table */
#define ADDR_SEL_LIST_CTOR_THUNK 0x004248A0u  /* cdecl void *(void) */
#define ADDR_SEL_LIST_ATEXIT     0x004248B0u  /* cdecl int32_t(void) */
#define ADDR_SEL_LIST_DTOR_THUNK 0x004248C0u  /* cdecl void(void) */
#define ADDR_SEL_LIST_DTOR       0x004248D0u  /* thiscall void(block) */
#define ADDR_SEL_LIST_CTOR       0x004248E0u  /* thiscall void *(block) */
#define AM2_MAX_SELECTED         0x40
/* Bit 8, and all three of the target predicates at 0x00403600..0x004036F0
 * open by answering "yes" for it without looking at anything else -- health,
 * destroyed and army are all skipped. So it is an override, and what sets it
 * is not established. Named for the bit. */
#define OBJ_FLAG_BIT8            0x100u
/* Two readers now, doing the same thing: MoveStepPoint and
 * ObjMoveAlongFacing both round the heading to one of the animation's
 * directions when this is set -- RoundTo8(facing, bits) << (8 - bits), the
 * shift anim.h predicts where it says a consumer gets from an 8-bit heading
 * to a direction "with a shift instead of a divide". Promoted off its bit
 * name on the strength of the second reader; RENAMED, not aliased. */
#define OBJ_FLAG_SNAP_HEADING    0x20u
#define OBJ_FLAG_SELECTED        0x400u
/* 0x00458380, four callers. Select one object if it is ours and selectable,
 * clearing the existing selection first unless a CONTROL key is held. */
#define ADDR_SELECT_IF_OWN       0x00458380u  /* int32_t(void *obj) */
/* 0x004578A0, one caller. Call a function for every selected object, dropping
 * the entries that no longer resolve or have been destroyed on the way past.
 * Its two removal paths do NOT agree about advancing -- see the source. */
#define ADDR_FOR_EACH_SELECTED   0x004578A0u  /* void(void (*)(void *)) */
/* 0x00413710, one caller. Add an object to the selection or take it out,
 * clearing the selection first unless CONTROL is held. */
#define ADDR_TOGGLE_SELECT       0x00413710u  /* void(void *obj) */
/* 0x00457A60, three callers. Point the object-context globals at one object
 * and set the pointer mode from what it is. */
#define ADDR_SET_OBJ_CONTEXT     0x00457A60u  /* void(void *obj) */
#define AM2_POINTER_MODE_SARGE   0
#define AM2_POINTER_MODE_OTHER   4
#define ADDR_SELECT_UNIT         0x00427CE0u  /* void(void *obj) -- NOT
                                               * SelectObject: wingdi.h has that
                                               * name and dllmain.c sees it */
/* It is also what CHOOSES the pointer mode, which its name did not say.
 *
 * FOUR OF ITS EIGHT JUMP-TABLE ARMS ARE DEAD, and it takes reading every
 * caller to see it. The dispatch is `cmp eax, 7; ja; jmp [eax*4 + 0x427B7C]`
 * on its single argument -- and all EIGHT callers pass ADDR_ZERO_POINT, which
 * is .bss nothing in the image writes. So the index is always 0, the call is
 * always ADDR_SET_POINTER_MODE(4), and the arms for modes 5 and 6 cannot be
 * reached through here at all. That does not make those modes unreachable:
 * three of SetPointerMode's ten callers push a variable. It narrows where they
 * could come from, which is the useful half.
 *
 * The mode is set only when a GATE survives the walk. It starts at 1 and is
 * cleared when two selected units disagree on OBJ_OFF_AI_MODE,
 * with 8 as the wildcard the first unit replaces; an empty selection returns
 * before the dispatch. So "the pointer mode follows the selection" is really
 * "follows a selection that agrees with itself" -- which is why a drive that
 * selects three units can still leave SetPointerMode at 1, and why three
 * reconstructed functions behind the weapon path read 0. Still original. */
#define ADDR_ON_SELECTION_CHANGED 0x00427990u /* void(uint32_t packedPoint) */
#define ADDR_FREE_SPRITE_LIST    0x004098B0u  /* what the alias jumps to */
#define ADDR_MAP_NAME_DEFAULT    0x00485108u  /* const char **, used when empty */
#define ADDR_MP_SCRIPT_DEFAULT   0x0048510Cu  /* const char ** */
#define ADDR_SPRITE_SET_LOAD     0x004239B0u  /* void(const char *) */
#define ADDR_SPRITE_SET_FREE     0x00423970u  /* void(void *set) */
#define ADDR_SPRITE_SET_TITLE    0x00510A40u
#define ADDR_SPRITE_SET_SHARED   0x00510230u
#define ADDR_SPRITE_SET_THIRD    0x00511250u
/* Inside a sprite set: the palette it was loaded with, and TWO remap tables
 * built from it. The second is the first with entries 0..9 left as the
 * identity -- the same "reserve the first ten" convention BMP_FLAG_RESERVE10
 * and ADDR_TILESET_RESERVE name from the other side. */
#define SPRITE_SET_OFF_PALETTE   0x20Cu  /* uint32_t[256] */
#define SPRITE_SET_OFF_REMAP     0x60Cu  /* uint8_t[256] */
#define SPRITE_SET_OFF_REMAP10   0x70Cu  /* uint8_t[256], first ten identity */
/* And the archive itself: the two names it is opened from, the open file, and
 * a directory into it sorted by key, which ADDR_SPRITE_DIR_INDEX halves. Each
 * entry is {key, offset}. The whole record is 0x80C bytes. */
#define SPRITE_SET_OFF_FOLDER    0x000u  /* char[0x100], chdir'd into first */
#define SPRITE_SET_OFF_NAME      0x100u  /* char[0x100], the .dat itself */
#define SPRITE_SET_OFF_FILE      0x200u  /* am2_FILE * */
#define SPRITE_SET_OFF_DIR_COUNT 0x204u  /* int32_t */
#define SPRITE_SET_OFF_DIR       0x208u  /* {uint32_t key, offset} * */
#define ADDR_SPRITE_SET_FOR_KEY  0x00423940u  /* void *(uint32_t key) */
/* Which of the three records a set NAME means, and what file id its archive
 * must start with. Returns nonzero when the record already names that file,
 * so nothing has to be reopened. */
#define ADDR_SPRITE_SET_RESOLVE  0x004236A0u  /* int32(name, void**, uint32*) */
#define AM2_DAT_ID_OBJECTS       0x81920666u
#define AM2_DAT_ID_TITLE         0x81920667u
#define AM2_DAT_ID_SHARED        0x81920668u
#define ADDR_STR_DAT_TITLE       0x00478AB4u  /* "title.dat" */
#define ADDR_STR_DAT_SHARED      0x00478AA0u  /* "shared.dat" */
#define ADDR_STR_DAT_OBJECTS     0x00478A88u  /* "objects.dat" */
#define ADDR_STR_FMT_OBJECTS_DIR 0x00478A94u  /* "%s\\objects" */
#define ADDR_STR_DAT_OPEN_FAIL   0x00478AF4u  /* "Unable to open object data
                                               *  file <%s>\n" */
#define ADDR_STR_DAT_BAD_ID      0x00478AC8u  /* "Invalid file id in object
                                               *  data file <%s>\n" */
#define ADDR_SPRITE_DIR_INDEX    0x00423D50u  /* int32_t(void *, uint32_t) */
#define ADDR_STR_DF_SEEK_FAIL    0x00478C1Cu  /* "Error seeking to location %d
                                               *  in data file.\n" */
#define ADDR_STR_DF_BAD_OBJECT   0x00478BF0u  /* "Error in validating object in
                                               *  data file.\n" */
#define AM2_PALETTE_RESERVED     10
#define ADDR_STR_SET_TITLE       0x00478AC0u  /* "title" */
#define ADDR_STR_SET_SHARED      0x00478AACu  /* "shared" */
/* Renamed from ADDR_FILL_PALETTE, which was invented -- no such string exists
 * anywhere in the image. The body builds a 3-3-2 palette into the caller's
 * buffer: PALETTEENTRY quads at +0 and COLORREFs at +0x400, 256 of each. */
#define ADDR_BUILD_RGB332        0x0041ADE0u  /* void(void *out) */
#define ADDR_GAME_OVER_STATE     0x00515F88u  /* int32, cleared at startup */
#define ADDR_FRAME_PRE           0x0040AF70u  /* before the state handler */
#define ADDR_STATE_ENTERED       0x00511DA8u  /* int32; set on a transition, and
                                               * the handler clears it after it
                                               * has run the entry action once */
#define ADDR_STATE0_TICK         0x00511A60u  /* GetTickCount at state 0 entry */
#define ADDR_STATE_ENTER_ONCE    0x00511DD0u  /* int32; state 2 only, and it
                                               * RETURNS after clearing it */
#define AM2_SUBSTATE_BASE        22           /* what the 13-entry table at
                                               * 0x00426230 is indexed from */
#define ADDR_SUBSTATE_TABLE      0x00426230u
/* The frame chain's own callees, all still original. */
#define ADDR_COMM_FRAME_PRE_A    0x00411C20u  /* "TIMING OUT PLAYER" */
/* Its own log strings call it ArmyMessageFlush, so it is renamed rather than
 * aliased -- the old name came from the one call site in RunFrame, and knowing
 * the body means knowing what that step IS: the frame's post-work flushes the
 * outgoing message packet. ArmyMessageSend calls it too, whenever the packet
 * fills. Returns zero when it could not send. */
#define ADDR_ARMY_MESSAGE_FLUSH  0x00410420u  /* int32_t(int32_t) */
/* Was ADDR_COMM_FRAME_POST_B, which is a name off the one call site in the
 * frame chain and says only when it runs. The body says what it IS: drain the
 * delayed send queue, sending every node whose deadline GetTickCount has
 * reached and returning its buffer to the pool. Renamed rather than aliased,
 * the same as ADDR_ARMY_MESSAGE_FLUSH above. */
#define ADDR_FLUSH_DELAYED_SENDS 0x00402F50u  /* void(void) */
/* The five DirectPlay results it names, as literals rather than through
 * dplay.h: air.cpp is on the FLAT side of the split and including an SDK
 * header there would fail tools/checksplit.py. The spelling in the comment is
 * the SDK's, so the values can be checked against it. */
#define AM2_DPERR_BUSY           0x8877010Eu
#define AM2_DPERR_INVALIDOBJECT  0x88770082u
#define AM2_E_INVALIDARG         0x80070057u  /* the game prints it as DPLAY */
#define AM2_DPERR_INVALIDPLAYER  0x88770096u
#define AM2_DPERR_SENDTOOBIG     0x887700E6u
/* IT NAMES ITSELF three times -- "Entering ProcessResendQueue",
 * "RESENDING %d to %x size %d", "Exiting ProcessResendQueue". The old name
 * here said where it sits in the frame, not what it does. Reconstructed. */
#define ADDR_PROCESS_RESEND_QUEUE 0x00403050u  /* void(void) */
#define AM2_COMM_MIN_BUFFERS     10           /* below this, COMM ERROR: NO BUFFERS */
#define AM2_COMM_OFF_ACTIVE      0x3DCu       /* gates all the comm frame work */
#define ADDR_STATE_LEAVE_COMMON  0x00426640u  /* states 0 and 3 tail-jump here */
#define ADDR_STATE_FRAME_COMMON  0x00426650u  /* and then here */
#define ADDR_STATE0_ENTER        0x004265F0u
#define ADDR_STATE3_ENTER        0x004266F0u
#define ADDR_STATE1_LEAVE        0x004263E0u
/* Reconstructed. Entering state 1 -- the title screen: clear both surfaces,
 * chdir to 01-title, load its palette from the screen bitmap, build two
 * fonts, and start title.wav. Then decide which menu to open. */
#define ADDR_STATE1_ENTER        0x004262E0u
#define ADDR_DIR_TITLE_PTR       0x004852D0u  /* const char *, "01-title" */
#define ADDR_STR_TITLE_WAV       0x00485218u
/* Was ADDR_INIT_DIGIT_TABLE, "fills 0x004FCDF8" -- one of the five things it
 * does. It frees the old menu sprites, builds TWO byte tables, loads the digit
 * sprites when the game is in state 2, resets the menu cursor's two rects and
 * the animation clock, and makes the menu's offscreen surface. Renamed for the
 * whole of it. */
#define ADDR_INIT_MENU_SCREEN    0x00412E00u  /* void(void) */
/* The two tables it builds, both 256 bytes and both indexed by a byte.
 *
 * The first is the identity except for its first ten entries, which come out
 * 0x50 LOWER -- so 0..9 map to 0xB0..0xB9 and everything else maps to itself.
 * That is a digit turned into the font's glyph for it.
 *
 * The second is a BRIGHTNESS table over the live palette: for each index it
 * takes the largest of the three channels, divides by 25, subtracts that from
 * ten, clamps to 0..9 and applies the same 0x50 bias. So a palette entry
 * becomes the digit glyph that stands for how dark it is, brightest first.
 *
 * AND THE FIRST TABLE SHARES ITS ADDRESS WITH ADDR_FLAME_RECORD, which is not
 * a naming error but a real overlap. The alias ratchet refused a second name
 * and reading both settled it: InitMenuScreen writes 256 bytes at 0x004FCDF8,
 * and the "Flame On!" cheat hands the same address to SetFieldInAll as the
 * weapon record every unit is pointed at. Both readings are of live code and
 * both are evidenced. So the buffer is used two ways by two subsystems that
 * cannot be up at once -- a menu is not a cheat mid-mission -- and whichever
 * ran last owns it. ADDR_FLAME_RECORD keeps the name, being the older and
 * equally evidenced one, and the glyph table is spelled through it. */
#define ADDR_PALETTE_GLYPHS      0x004FC998u  /* uint8_t[256] */
#define AM2_GLYPH_DIGIT_BIAS     0x50
#define AM2_GLYPH_SHADE_STEP     25    /* channel units per glyph */
#define AM2_MENU_SPRITE_ROWS     0x13  /* the loop's bound, INCLUSIVE */
/* The menu modes this arm can leave behind. 7 and 9 are the two multiplayer
 * screens a LOBBY launch goes straight to; 1 is the title's own. */
#define AM2_MENU_MODE_TITLE      1
#define AM2_MENU_MODE_LOBBY_HOST 7
#define AM2_MENU_MODE_LOBBY_JOIN 9
/* Reconstructed. The title state's menu step: tear down whatever dialog is
 * up, open the one ADDR_MENU_MODE now names through a 21-arm jump table, and
 * paint it. The table is the definitive list of menu SCREENS -- mode N opens
 * arm N -- and it is what confirms State1Enter's four honoured requests are
 * the host lobby, the join lobby, MOVIES and the replay prompt. */
#define ADDR_STATE1_MENU         0x00426400u
#define AM2_MENU_MODE_MAX        21   /* the jump table's last arm */
#define AM2_DLG_SLOT_DELETE      0    /* the scalar deleting destructor */
#define AM2_DLG_SLOT_PAINT       1
#define AM2_DLG_SLOT_UPDATE      2
#define AM2_DLG_OFF_RECT         0x14u
#define ADDR_STATE1_COMMON       0x00426270u
#define ADDR_MOVIE_FRAME_STEP    0x00445630u  /* states 0 and 3, per frame */
#define ADDR_STATE2_ENTER        0x00425300u
#define ADDR_SUBSTATE22          0x00425C10u
/* Sub-state 33's other arm: the one that runs while the game is PAUSED, where
 * ADDR_TAKE_MENU_REQUEST runs when it is not. Named for the body rather than
 * for its position in the table -- it was ADDR_SUBSTATE33_ALT. */
#define ADDR_MISSION_PAUSED_FRAME 0x00425CD0u  /* void(void) */
/* Four keys, one idiom, and the name this used to carry came from a call site
 * rather than from the body. It asks whether any of SPACE, F1, ESCAPE or
 * RETURN has just been RELEASED -- `!IsKeyDown && KeyChanged` -- and CONSUMES
 * the one that had, so the answer is destructive and cannot be asked twice.
 *
 * It was ADDR_EVENT_FLAG_8_TEST, after the pause flag its callers clear when
 * it says yes. That describes what two of the four callers do with the answer,
 * not what the function decides. Renamed rather than aliased; four use sites,
 * all in frame.cpp. */
#define ADDR_DISMISS_KEY_RELEASED 0x00424900u  /* int32_t(void) */
/* Its own log string calls it SendGamePause, so it is renamed rather than
 * aliased -- the old name came from this one call site. Knowing the body makes
 * the call site legible: frame.cpp passes (0, AM2_EVENT_FLAG_8), which is
 * "un-pause, reason 8" told to the other players. CLAUDE.md already records
 * that the event flags ARE the pause mask; this is the send half. */
#define ADDR_SEND_GAME_PAUSE     0x00410820u  /* void(int32 pause, int32 mask) */
#define AM2_EVENT_FLAG_8         8
#define ADDR_FRAME_POST          0x0040AFA0u  /* after it, reached by tail jump */
#define ADDR_STATE0_FRAME        0x004266B0u
#define ADDR_STATE1_FRAME        0x00426570u
#define ADDR_STATE3_FRAME        0x00426760u
#define ADDR_STATE4_FRAME        0x00426790u
#define ADDR_COMM_OFF_LOBBY      0x3FCu       /* on the comm object */
#define ADDR_COMM_OFF_SKIP_INTRO 0x3F8u
#define ADDR_SET_GAME_OVER       0x0042E5A0u  /* void(int32) */
#define ADDR_LEAK_COUNT          0x0050C344u  /* int32 */
#define ADDR_LEAK_TOTAL          0x0050C340u  /* int32 */
#define ADDR_LEAK_RECORDS        0x0050C348u  /* the 16-byte records */
#define ADDR_STR_LEAK_HEADER     0x0047834Cu  /* "Unreleased memory (%d) blocks:\n" */
#define ADDR_STR_LEAK_ROW        0x0047832Cu  /* "%08d bytes  file: %s  line: %d\n" */
#define ADDR_CRT_STRNCPY         0x00465610u

/* The trig tables and the constants that build them. The store index runs one
 * ahead of the loop counter in the original, so the cos table really starts at
 * 0x00515784 and not at the 0x00515780 the first fstp encodes. */
/* 0x0042DD70 and 0x0042DDC0, 17 callers each: one masked index into the table
 * below and an `fld`, so they return a FLOAT in st(0). The names are ours; the
 * tables were named long before, by trig.cpp, which builds them. */
#define ADDR_COS8                0x0042DD70u  /* float(int32_t heading) */
#define ADDR_SIN8                0x0042DDC0u  /* float(int32_t heading) */
#define ADDR_TRIG_COS            0x00515784u  /* float[256] */
#define ADDR_TRIG_SIN            0x00514F80u  /* float[256], scaled by -0.85 */
/* These two are the CENTRES, not the starts: the original indexes them with a
 * signed ratio running -512..512, so the table occupies base-512..base+512.
 * Taking them for starts puts each one 512 bytes late, and the sin one then
 * lands on top of half the cos table -- which is how it was found. The four
 * are contiguous and in this order: atanS 0x00515380, cos 0x00515784,
 * atanC 0x00515B84, with sin at 0x00514F80 ending exactly where atanS begins. */
#define ADDR_TRIG_ATAN_COS       0x00515D84u  /* int8[1025] centred here */
#define ADDR_TRIG_ATAN_SIN       0x00515580u  /* int8[1025] centred here */
#define AM2_TRIG_ATAN_RANGE      512
#define ADDR_DBL_TWO_PI          0x0046F9C8u  /* 6.283185307 */
#define ADDR_DBL_ONE_256         0x0046F9C0u  /* 0.00390625 */
#define ADDR_DBL_SIN_SCALE       0x0046F9B8u  /* -0.85, the isometric squash */
#define ADDR_DBL_512             0x0046F9B0u  /* 512.0 */
#define ADDR_DBL_ZERO            0x0046F920u  /* 0.0 */

#define ADDR_SHUTDOWN_SUBSYSTEMS   0x0040B220u  /* void(void) */
/* Look for the game CD: walk the logical drives, find one that is a CD-ROM and
 * whose volume label is ARMYMEN2, and remember where it is. */
#define ADDR_FIND_GAME_CD        0x00426B50u  /* int32_t(void) */
#define ADDR_CD_PRESENT          0x00512588u  /* int32_t */
#define ADDR_CD_FOUND_FLAG       0x00512594u  /* int32_t, set alongside it */
#define ADDR_CD_PATH             0x00512464u  /* char[], the drive root */
#define ADDR_CD_LABEL            0x004852B8u  /* "ARMYMEN2" */
#define ADDR_GAME_STRICMP        0x00465F90u  /* the game's own CRT */
#define ADDR_GAME_SPRINTF        0x00464CE2u
#define ADDR_RESET_TO_TITLE      0x004249C0u
#define ADDR_BUILD_TRIG_TABLES      0x0042DC30u
#define ADDR_FREE_SPRITE_LIST_ALIAS      0x00409920u
#define ADDR_INIT_AUDIO      0x0040C9B0u
#define ADDR_CLEAR_GAME_OVER      0x0042E580u
#define ADDR_START_INTRO         0x0040B7A0u  /* honours -nointro */
#define ADDR_RUN_FRAME           0x0040B000u  /* one tick; state machine of 5 */
/* RunFrame is a state machine. It returns at once unless ADDR_APP_ACTIVE is
 * set, then restores lost surfaces, polls input, and dispatches on
 * ADDR_GAME_STATE through the table below.
 *
 * State 2 is the level teardown, and it tail-JUMPS to 0x004256F0 rather than
 * calling it -- which is the only route to StopAllSounds. Those audio teardown
 * functions therefore need a mission to END; no amount of quitting from the
 * title screen reaches them. */
#define ADDR_RUN_FRAME_TABLE     0x0040B050u  /* void(*[5])(void), by ADDR_GAME_STATE */
/* Named from the RunFrame jump table at 0x0040B050, whose third entry this is,
 * and not from the one thing it does that anybody had looked at. It runs EVERY
 * FRAME of a mission. Two things come out of it:
 *
 *   - if ADDR_STATE_PENDING is set it tail-jumps to ADDR_LEVEL_TEARDOWN, which
 *     is a DIFFERENT function and the one that calls StopAllSounds;
 *   - otherwise it dispatches on ADDR_MENU_MODE, biased by 22, over a
 *     13-entry table at 0x00426230 -- the in-mission sub-states. */
#define ADDR_STATE2_FRAME        0x004260C0u  /* void(void), state 2 per frame */
/* The actual teardown, and it was called ADDR_STATE2_FRAME's address for as
 * long as anyone had written the name down. Reached ONLY by the tail jump at
 * 0x004260C9 -- no call site anywhere -- which is why a reachability scan that
 * looks for `call` and `push imm32` reports it as dead code. It calls
 * ADDR_STOP_ALL_SOUNDS as its second instruction-level call. */
/* Two of LevelTeardown's twenty-five callees have no name and get a
 * placeholder rather than a guess, which is the ShutdownSubsystems precedent:
 * the ORDER is the fact worth keeping and a name each would be a guess each.
 * 0x0040A6A0 is a one-instruction `jmp` into the middle of another entry;
 * 0x00463360 is eight chained calls in the band above the nominal CRT line. */
#define ADDR_TEARDOWN_40A6A0     0x0040A6A0u  /* void(void) */
#define ADDR_TEARDOWN_463360     0x00463360u  /* void(void) */
#define ADDR_LEVEL_TEARDOWN      0x004256F0u  /* void(void), on leaving a level */
/* Sub-state 34 of ADDR_STATE2_FRAME's table, and the only in-mission code that
 * reads a key and raises a menu request. The test is `!IsKeyDown(ESC) &&
 * KeyChanged(ESC)`, i.e. ESCAPE on RELEASE. It does nothing during ordinary
 * play because the sub-state is not 34 then -- measured, not assumed. */
#define ADDR_SUBSTATE34_ESCAPE   0x00425DA0u  /* void(void) */
#define ADDR_FREE_SPRITE_SETS     0x00423D20u
#define ADDR_SHUTDOWN_DDRAW      0x0041A950u  /* void(void) */
#define ADDR_DD_CLIPPER          0x00507340u  /* IDirectDrawClipper * */
#define ADDR_REPORT_LEAKS        0x0041E690u  /* "Unreleased memory (%d) blocks:" */
#define ADDR_FREE_MEM_TRACKER    0x0041E710u

/* Packed map key: A(7) | gap(2) | B(10) | C(7). */
/* 0x00434290 and 0x004346E0. A sorted array of {key, value} dwords, searched
 * by halving: the count is at 0x00516148 and the array at 0x00516150. The
 * three-argument one packs its key with exactly PackKey's arithmetic and hands
 * it straight over, which is what ties the two families together. Both names
 * are ours; -1 means no such key. */
/* 0x00434060, eight callers. Make a list: a 0x30-byte header with an owner, a
 * count and a pointer, plus a copy of `count` twelve-byte records.
 *
 * The copy is done FIELD BY FIELD -- int32, int16, int16, int32 -- rather than
 * as twelve bytes, so the record's shape is visible in the instructions even
 * though nothing here reads a field. That is the only evidence for the layout
 * and it is worth keeping in the reconstruction rather than collapsing to a
 * memcpy that would agree and say nothing.
 *
 * A count of zero or less answers NULL having allocated nothing. Neither
 * allocation is checked. The header is 0x30 bytes and only three of its twelve
 * dwords are written; the rest are zeroed and unexplained. */
#define ADDR_MAKE_RECORD_LIST    0x00434060u  /* void *(count, src, owner) */
#define LISTHDR_OFF_OWNER        0x00u
#define LISTHDR_OFF_COUNT        0x08u
#define LISTHDR_OFF_RECORDS      0x0Cu
#define AM2_LISTHDR_BYTES        0x30u
#define AM2_LIST_RECORD_BYTES    0x0Cu
#define LISTHDR_OFF_INDEX        0x04u   /* its slot in ADDR_RECORD_LISTS */
/* A WHOLE MASK RECORD IS EMBEDDED HERE, and finding that retired a name.
 *
 * +0x1C went in as LISTHDR_OFF_EXTRA, with "what it points at is not
 * established", because the only reader in sight was the free. Its second
 * reader -- 0x0043A6D0, testing it to choose between the two markers -- got it
 * renamed to LISTHDR_OFF_HIT_MASK, on the strength of playing the same part
 * OBJ_OFF_HIT_MASK plays one structure over. Both readings were of a CALL
 * SITE, and neither was of the callee.
 *
 * ListMaskAction is the callee and it settles it: it does `lea edi,[hdr+0x10]`
 * and then reads [edi], [edi+2], [edi+4], [edi+6] and [edi+0xC] -- which is
 * OBJMASK_OFF_* exactly. So the list header CONTAINS an object mask at +0x10,
 * and +0x1C is that record's bits pointer rather than a field of the header at
 * all. The name is gone; the field is reached as LISTHDR_OFF_MASK plus
 * OBJMASK_OFF_BITS, which is what it is.
 *
 * Third time a name off a call site has been wrong about its own field, and
 * the cure was the same each time: read the callee. */
#define LISTHDR_OFF_MASK         0x10u  /* an OBJMASK_OFF_* record, embedded */
/* The four edges of the header's own box, in world units, added to the point
 * the caller supplies. The same fallback shape OBJ_OFF_BOX_LEFT and the three
 * after it give an object, and read by the same one function. They are inside
 * the 0x30 header that ADDR_MAKE_RECORD_LIST "zeroes and leaves unexplained";
 * four of those nine dwords are explained now. */
#define LISTHDR_OFF_BOX_LEFT     0x20u
#define LISTHDR_OFF_BOX_TOP      0x24u
#define LISTHDR_OFF_BOX_RIGHT    0x28u
#define LISTHDR_OFF_BOX_BOTTOM   0x2Cu
/* The first field of a twelve-byte list record is a SPRITE, which is the only
 * thing anything has yet read out of one: ListBoxAction takes the FIRST
 * record's and asks whether it has a software image, exactly as ObjBoxAction
 * takes the first ROW's. Neither loops. */
#define LISTREC_OFF_SPRITE       0x00u
/* 0x00438F10, one caller. ObjBoxAction for a record-list header instead of an
 * object: the same three exits, the same 0x1C flag test, the same box offset
 * by a point -- except the point is an ARGUMENT here rather than a field. */
#define ADDR_LIST_BOX_ACTION     0x00438F10u /* int32_t(uint32 at, hdr, mask) */
/* Its bitmask twin, the one the embedded mask's bits pointer selects --
 * the same relation ADDR_OBJ_MASK_ACTION has to ObjBoxAction one structure
 * over. Named at last, by the function that chooses between the two. */
#define ADDR_LIST_MASK_ACTION    0x004385A0u /* int32_t(uint32 at, hdr, mask) */
/* 0x0043A6D0, six callers. Could the thing at key-table slot `slot` stand at
 * world point `at`? Its bounds check is what says ADDR_KEY_TABLE_COUNT counts
 * ADDR_AAI_RECORDS as well. Reconstructed in region.cpp. */
#define ADDR_CAN_PLACE_AT        0x0043A6D0u /* int32_t(uint32, int32, int32) */
/* 0x00434C40, one caller, and that caller walks every entry of
 * ADDR_RECORD_LISTS -- so this is ADDR_MAKE_RECORD_LIST's counterpart. Free
 * the header's two pointers and then the header. Reconstructed. */
#define ADDR_FREE_RECORD_LIST    0x00434C40u  /* void(void *list) */
/* 0x00434100, one caller -- the READ half of ADDR_ADD_RECORD_LIST, and the
 * same halving search over ADDR_RECORD_LIST_INDEX with the same unsigned
 * compare. The slot for an owner, or -1. Reconstructed. */
#define ADDR_FIND_RECORD_LIST    0x00434100u  /* int32_t(uint32_t owner) */
/* 0x00434150, eight callers: register one of ADDR_MAKE_RECORD_LIST's headers
 * under its LISTHDR_OFF_OWNER, and hand back the slot it went into.
 *
 * Two parallel structures, which is what makes it worth a name of its own.
 * ADDR_RECORD_LISTS is an unsorted array of the headers, appended to, and the
 * slot index is written back into the header. ADDR_RECORD_LIST_INDEX is a
 * SORTED array of {owner, slot} pairs, binary-searched on entry so a duplicate
 * owner is refused with -1, and memmove'd open to keep it sorted. So lookups
 * are by owner and iteration is by slot, and both stay valid.
 *
 * The owner keys are compared UNSIGNED -- the search uses `jae`.
 * 151 calls on a driven Boot Camp mission, one per ADDR_MAKE_RECORD_LIST. */
#define ADDR_ADD_RECORD_LIST     0x00434150u  /* int32_t(void *list) */
/* 0x004344A0, seven callers, and every one of them hands the result straight
 * to 0x004345A0 -- the same make-then-register pair ADDR_MAKE_RECORD_LIST and
 * ADDR_ADD_RECORD_LIST form one entry above.
 *
 * It builds a 0x40-byte record from seven arguments and, when the type is not
 * negative, SEEDS ten more fields from the object.aai record for that type and
 * key through ADDR_DEF_FIND_OBJ_REC. So the record is an instance of a
 * definition; what it is an instance OF is not established here, and the name
 * says only where the fields come from.
 *
 * The key is split with PackKey's arithmetic -- low 7 bits and the next 10 --
 * which is the same split ADDR_KEY_LOOKUP_TRIPLE performs, and is what ties
 * this to the .aai vocabulary rather than to a table of its own.
 * Reconstructed; 151 calls on a driven Boot Camp mission, matching
 * ADDR_ADD_RECORD_LIST exactly. */
#define ADDR_MAKE_AAI_RECORD     0x004344A0u  /* void *(type,key,slot,4 more) */
/* 0x00434700, 1,120 bytes, one caller. READ IN PART AND NOT RECONSTRUCTED,
 * recorded the way LoadType2, CreateTrooper, CreateVehicle, RegionFindPath and
 * 0x00459DA0 were -- every one of which went in quickly once the reading was
 * finished rather than being restarted from scratch.
 *
 * WHAT IS SETTLED. It calls FreeAaiTables and then builds SIX built-in AAI
 * records, each by the same shape:
 *
 *     spr  = PreloadSprite(set, index, frame, 0x1000, addref);
 *     rec  = { spr, int16, int16, dword };        -- AM2_LIST_RECORD_BYTES
 *     g_k  = KEY;                                 -- one global per record
 *     list = MakeRecordList(1, &rec, (void *)KEY);
 *     AddRecordList(list);
 *     RectSet(&r, l, t, right, bottom);
 *     AddAaiRecord(MakeAaiRecord(-1, g_k, (int32_t)list,
 *                                r.left, r.top, r.right, r.bottom));
 *
 * The rect reaches MakeAaiRecord as its last FOUR arguments -- the compiler
 * writes them into reserved stack above a pushed `list` rather than pushing
 * them, which is why the call looks like three arguments in the disassembly and
 * is seven. objtype.h already declares it that way.
 *
 * Three of the six keys are 0x980000, 0x980100 and 0x980080, each also stored
 * into a global of its own -- 0x00516158, 0x00516168, 0x0051615C.
 *
 * THE esp COUNT DOES ADD UP; I COUNTED IT WRONG. A previous note here said the
 * pushes before one `add esp, 0x28` came to nine against a cleanup of ten, and
 * left the function on that basis. There are FOURTEEN outstanding at that point
 * -- PreloadSprite's five, MakeRecordList's three, AddRecordList's one and
 * RectSet's five -- and the cleanup is PARTIAL, clearing ten and leaving four
 * of PreloadSprite's standing. Batched cdecl cleanup that does not clear
 * everything, which is a shape this file already warns about and I read as a
 * contradiction instead.
 *
 * Tracked mechanically rather than by eye, the six blocks come out as:
 *
 *   #  PreloadSprite(set,index,frame)  key         RectSet(l,t,r,b)  type
 *   0  0x13, 0, 0                      0x980000    -0x14,-5,0x3C,0x23   -1
 *   1  (reuses block 0's sprite)       0x980100     0, 0, 1, 1          -1
 *   2  0x13, 1, 0                      0x980080    -2,-2, 2, 2          -1
 *   3  0x2D, ?, 0 and 0x2D, 0x0A, 0    (from esi)  -0x10,-0x10,0x10,0x10 0x2D
 *   4  0x1D, 0x0B, 0                   0xE80580    -0x10,-0x10,0x10,0x10 0x1D
 *   5  0x1D, 0x0C, 9                   0xE80609    -0x10,-0x10,0x10,0x10 0x1D
 *
 * THE LAST THREE PASS A REAL TYPE, not -1, so they are seeded from the
 * object.aai record for their (type, key) where the first three are not -- and
 * 0xE80609 is the value ADDR_CREATE_WATCHED_KIND's neighbour at 0x00516164 is
 * already recorded as holding, so this is where it comes from.
 *
 * WHAT IS STILL NOT DONE: blocks 3, 4 and 5 each call RectSet TWICE and block 3
 * preloads twice, so their per-block structure is not the simple one above.
 * That is the remaining reading. */
#define ADDR_BUILD_AAI_BUILTINS  0x00434700u  /* void(void) */
/* The five key globals it fills, one per singleton record -- and TWO OF THEM
 * ALREADY HAD NAMES, which is the better result. 0x00516160 is
 * ADDR_CREATE_WATCHED_KIND and 0x00516164 is ADDR_WATCHED_TYPE_ID: the pair
 * ObjIsWatchedKind matches an item's record against. So this function is where
 * they come from, and the "watched kind" the default pointer stands aside for
 * is the last built-in AAI record this builds. Reached from the reader's end
 * months ago and from the writer's end here; the two agree.
 *
 * The other three are new. Named for the key each holds, since nothing yet
 * says what the record is FOR. */
#define ADDR_AAI_KEY_980000      0x00516158u  /* int32_t */
#define ADDR_AAI_KEY_980080      0x0051615Cu
#define ADDR_AAI_KEY_980100      0x00516168u
#define AM2_AAI_RECORD_BYTES     0x40u
#define AAIREC_OFF_TYPE          0x00u   /* 0x2E when the argument is negative */
#define AAIREC_OFF_KEY           0x08u
/* Subtracted from every hit before it is applied, and clamped to zero for
 * damage kind 1 -- so it is the record's ARMOUR. Named from DamageItem, its
 * only reader. */
/* What an item does to whatever is standing where its footprint lands.
 * ObjAfterMove takes it when its caller passes zero, and hands it to
 * DamageObject with kind 4. Named from that use, its only one. */
#define AAIREC_OFF_CRUSH_DAMAGE  0x38u   /* int16_t */
#define AAIREC_OFF_ARMOUR        0x3Au   /* int16_t */
/* TWO PREFIXES ARE ON THIS ONE RECORD, which is the shape checkoffsets cannot
 * see and CLAUDE.md already warns about under ITEM_OFF_LAST_USE. AAIREC_OFF_
 * has KEY at 8, SLOT at 0x0C and LIST_SLOT at 0x10; AAI_OFF_ has DEF_INDEX at
 * 0x10, BOX at 0x14, OR_FLAGS at 0x28, HEALTH at 0x2C. LIST_SLOT and
 * DEF_INDEX are the SAME DWORD under two names in two families, and no
 * ratchet can refuse that. Recorded here rather than merged: settling it means
 * deciding which prefix survives across a dozen sites, and this commit is not
 * the place. */
/* Seeded to -1 by ADDR_MAKE_AAI_RECORD and OVERWRITTEN with the slot by
 * ADDR_ADD_AAI_RECORD. It went in as MINUS_ONE, named from the maker alone,
 * which is naming a field from one of its two writers -- the same failure as
 * naming a function from one call site. */
#define AAIREC_OFF_SLOT          0x0Cu
#define AAIREC_OFF_LIST_SLOT     0x10u   /* into ADDR_RECORD_LISTS, 0 for none */
#define ADDR_RECORD_LIST_CAP     0x00516138u  /* int32_t, slots allocated */
#define ADDR_RECORD_LIST_COUNT   0x0051613Cu  /* int32_t, slots used */
#define ADDR_RECORD_LISTS        0x00516140u  /* void **, one per slot */
#define ADDR_RECORD_LIST_INDEX   0x00516154u  /* {owner, slot} pairs, sorted */
#define AM2_RECORD_LIST_GROW     0x11         /* 17 slots per grow */
#define ADDR_KEY_LOOKUP          0x00434290u  /* int32_t(uint32_t key) */
#define ADDR_KEY_LOOKUP_TRIPLE   0x004346E0u  /* int32_t(a, b, c) */
/* 0x004342E0, 448 bytes, ten callers. Find the AAI record for one sprite
 * triple and BUILD it when it is not there. Its head is KeyLookupTriple
 * inlined -- the same PackKey arithmetic, computed rather than called -- which
 * is also what says the three arguments are a sprite set, index and frame:
 * they go on to PreloadSprite in that order. */
#define ADDR_ENSURE_SPRITE_AAI_REC 0x004342E0u  /* int32(set, index, frame) */
#define ADDR_KEY_TABLE_COUNT     0x00516148u  /* int32_t */
#define ADDR_KEY_TABLE           0x00516150u  /* {uint32 key, int32 value}[] */
/* 0x004345A0, seven callers -- the register half of ADDR_MAKE_AAI_RECORD, and
 * structurally the same function as ADDR_ADD_RECORD_LIST on a different pair
 * of tables. It keys on the record's AAIREC_OFF_KEY rather than on an owner,
 * writes the slot back into AAIREC_OFF_SLOT, and grows by 19 rather than 17.
 *
 * ADDR_KEY_TABLE is the sorted half, which is what ADDR_KEY_LOOKUP and
 * ADDR_KEY_LOOKUP_TRIPLE search -- so this is where the entries they find come
 * from, and ADDR_AAI_RECORDS is what a found value indexes. Reconstructed. */
#define ADDR_ADD_AAI_RECORD      0x004345A0u  /* int32_t(void *rec) */
/* 0x00434B60, two callers. The teardown for both of those tables and the two
 * index arrays beside them. */
#define ADDR_FREE_AAI_TABLES     0x00434B60u  /* void(void) */
#define ADDR_AAI_RECORD_CAP      0x00516144u  /* int32_t, slots allocated */
#define ADDR_AAI_RECORDS         0x0051614Cu  /* void **, one per slot */
#define AM2_AAI_RECORD_GROW      0x13         /* 19 slots per grow */
/* 0x00430120, 12 callers: put a line on the menu and append it to the chat
 * log, in that order. The name is ours. */
#define ADDR_ANNOUNCE            0x00430120u  /* void(const char *) */
/* THE UNIT-TYPE TABLE, AND ITS BASE WAS 0x20 BYTES LATE. It went in here as
 * twelve records at 0x004878B8, `{value, bit, isTrooper, isVehicle, index,
 * char name[20]}`, with the name list starting at bazookaman. Every one of
 * those fields is a real field and the record was still wrong: it straddles
 * TWO real records, so the three flags and the name in each row belong to the
 * unit AFTER the cost and the mask beside them. The reading was internally
 * consistent, which is why it survived -- rifleman was simply missing from the
 * front and nothing looked odd.
 *
 * What settles it is the `place` line parser at 0x0043B490, which walks the
 * names itself: it starts at 0x004878A4 with "rifleman", steps 0x28 and stops
 * at 0x00487B74. So the base is 0x00487898, the name is at +0x0C, and there
 * are EIGHTEEN records rather than twelve -- medtent, garage, radar, aagun and
 * mine were past the end of the old reading. The bound is its own check: the
 * table ends exactly where "%s_%s_place.txt" begins, and 0x00487B74 is that
 * string's "txt" read as a nineteenth name.
 *
 *   +0x00 int32   trooper, 1 for the five soldiers
 *   +0x04 int32   vehicle, 1 for tank/jeep/halftrack/truck/ptboat
 *   +0x08 int32   kind within that class -- OBJ_OFF_SOLDIER_KIND's numbering,
 *                 rifleman 1, grenadier 2, flamerman 3, bazookaman 4,
 *                 mortarman 5
 *   +0x0C char[20] name
 *   +0x20 int32   cost in ADDR_ARMY_POINTS, 10 (mine) to 500 (tank). The old
 *                 comment guessed this correctly from its range alone
 *   +0x24 int32   game-type mask, one distinct bit per type
 */
#define ADDR_UNIT_TYPES         0x00487898u  /* 18 records */
/* 0x0043B0A0, one caller -- the manual placement screen. Does this object
 * count as one of that army's placed units? Reconstructed. */
#define ADDR_IS_PLACED_UNIT     0x0043B0A0u  /* int32(obj, int32 army) */
/* 0x0043AAB0, an eight-arm jump table on its SECOND argument, and now READ
 * rather than named from a call site -- the body agrees with the name, which
 * is not how these usually end. It is the membership half of
 * ADDR_SPRITE_KEY_FOR_KIND: same table, same bound, same six set ids in the
 * same order, and the first candidate of every arm is exactly the one key that
 * function answers with. The arms differ in how many MORE they try, kind 3 is
 * one short because its third test tail-jumps into kind 4's fourth, and kind 5
 * reaches a set the sibling never mentions. Reconstructed; see place.cpp. */
#define ADDR_UNIT_KIND_MATCHES  0x0043AAB0u  /* int32(code, kind, slot) */
#define AM2_UNIT_TYPE_STRIDE    0x28u
#define AM2_UNIT_TYPE_COUNT     18
#define UNIT_TYPE_OFF_TROOPER   0x00u
#define UNIT_TYPE_OFF_VEHICLE   0x04u
#define UNIT_TYPE_OFF_KIND      0x08u
#define UNIT_TYPE_OFF_NAME      0x0Cu
#define UNIT_TYPE_OFF_COST      0x20u
#define UNIT_TYPE_OFF_GAME_MASK 0x24u
/* The three BUILDING kinds that share sprite set 0x26 -- riflepill,
 * bazookapill and mgpill, which is why SpriteKeyForKind and UnitKindMatches
 * both give 0, 1 and 2 one arm. RefundPlacedUnit treats them as one case and
 * asks the occupant which of the three to charge for. */
#define AM2_PILLBOX_KIND_FIRST  0
#define AM2_PILLBOX_KIND_LAST   2
/* How far past a pillbox's own uid it looks for the trooper inside. Three,
 * and it works because CreateItem allocates a composite's children straight
 * after its parent. */
#define AM2_PILLBOX_UID_SCAN    3
/* 0x0043B160, one caller -- the manual placement screen. Take a placed unit
 * back off the layout: find its ADDR_UNIT_TYPES record, destroy it, and add
 * UNIT_TYPE_OFF_COST back to the caller's points. Reconstructed. */
#define ADDR_REFUND_PLACED_UNIT 0x0043B160u  /* void(obj, slot, int32 *pts) */
#define ADDR_UNIT_TYPE_COST 0x0043A5E0u  /* uint32_t(int32_t type) */
#define ADDR_PACK_KEY       0x00433810u
/* 0x0043A5F0, one caller. A packed sprite key for a selector in 0..7: five of
 * the eight arms call PackKey with their own set id and their own arithmetic
 * on the second argument, one answers a global, and the top three selectors
 * share a single arm. */
#define ADDR_SPRITE_KEY_FOR_KIND 0x0043A5F0u /* int32_t(int32_t sel, int32_t) */
#define ADDR_KEY_FIELD_A    0x00433830u
#define ADDR_KEY_FIELD_B    0x00433840u
#define ADDR_KEY_FIELD_C    0x00433850u

/* THE OBJECT TYPES, at offset 0, which FreeItem and DestroyByType both switch
 * on and ObjIsItem tests. CLAUDE.md called 2, 3 and 8 unidentified; the answer
 * was already spread across the tree and simply never assembled. Evidence per
 * row, best first:
 *
 *   1  item      ObjIsItem answers yes for 1 and 4
 *   2  TROOPER   FreeItem's arm for 2 logs "DestroyTrooper %x" -- the
 *                program's own name, not ours
 *   3  VEHICLE   two independent routes: FreeItem's arm is DestroyVehicle,
 *                and the type-3 destroy handler clears a footprint out of
 *                ADDR_VEHICLE_MASK, indexed by a kind at obj+0x52C
 *   4  WEAPON    its arm logs "DestroyWeapon, %x"
 *   8  ROACH     the type-8 destroy handler clears a footprint out of
 *                ADDR_ROACH_MASK, which has no kind index because there is
 *                one roach
 *
 * 5, 6 and 7 remain unread; 5, 6 and 8 share FreeItem's common arm with 1, so
 * that grouping says nothing about what they are. The footprint evidence for 3
 * and 8 is evidence and not proof -- each clearer has other callers -- but it
 * agrees with the FreeItem arm in the one case where both speak. */

/* Object type predicates; all accept NULL and answer 0. */
/* The body is `type == 1 || type == 4` with a NULL check, so this comment is
 * the function and not a guess. It also carried ADDR_OBJ_TAKES_SCRIPT, which
 * was objscript.cpp's name for the same predicate because the objects that
 * take a script are exactly types 1 and 4 -- true, and a fact about the
 * CALLER. Removed, along with three more dead second names on this family's
 * addresses: ADDR_OBJ_BY_UID, ADDR_FIRST_SCRIPT_OBJ and ADDR_NEXT_SCRIPT_OBJ.
 * objscript.cpp stopped using all four some time ago and the macros outlived
 * their last use. */
#define ADDR_OBJ_IS_ITEM    0x00433860u  /* types 1, 4 */
#define ADDR_OBJ_IS_TYPE2   0x00457470u
#define ADDR_OBJ_IS_TYPE3   0x00457490u
#define ADDR_OBJ_IS_TYPE8   0x004574B0u  /* int32_t(const AM2_Object *) */
#define ADDR_OBJ_IS_TYPE4   0x0045EEB0u
/* 0x0042AAE0, one caller, which stores the answer into a word field of a
 * record it is packing. Classify a weapon uid: 0 if it does not resolve or is
 * not a type 4, otherwise a small code taken through a FOUR-ARM JUMP TABLE
 * whose arms are not in the order they are laid out -- see AM2_WEAPON_CLASS. */
#define ADDR_WEAPON_CLASS_OF 0x0042AAE0u  /* int32_t(uint32_t uid) */

/* The lookup and the type test in one. Eight callers. */
#define ADDR_LOOKUP_TYPE3_BY_UID 0x0045D970u  /* AM2_Object *(uint32_t uid) */
#define ADDR_FIELD_53C         0x0045AFA0u  /* uint32_t(const void *) */
#define ADDR_ADD_BYTE_SAT      0x0045F440u  /* int32_t(base, add) */
#define ADDR_COMPARE_DWORD     0x0043E150u  /* int32_t(const void*, const void*) */
#define ADDR_COPY_BYTE_IF_SET  0x00408560u  /* void(unused, uint8_t*, const void*) */
#define ADDR_SCALE_32_BLOCKS   0x0042E4F0u  /* int32_t(int32_t) */
#define ADDR_TITLE_CASE        0x0042E510u  /* void(char *) */
#define ADDR_RESET_PAIR_MASK   0x0042F120u  /* void(uint32_t*, uint32_t*) */
#define ADDR_IS_KIND_7         0x00435640u  /* int32_t(const void *) */
#define ADDR_IS_BLANK          0x0043EE80u  /* int32_t(uint8_t) -- no '\n' */
#define ADDR_IS_SCRIPT_DELIM   0x0043EEA0u  /* int32_t(uint8_t) */
/* Exchange the first and third bytes of a packed colour, leaving the second
 * and clearing the fourth: a DIB entry is 0x00RRGGBB and the matcher above
 * reads 0x00BBGGRR. It was also ADDR_SWAP_COLOUR_BYTES, which named it for the
 * caller that hands it a bitmap's palette rather than for what it does. */
#define ADDR_SWAP_COLOUR_BYTES 0x0041AE90u  /* uint32_t(uint32_t) */
/* The palette loader, its file half and its expansion half. The destination
 * is 0x800 bytes: 256 dwords of working colour and 256 more behind them. */
#define ADDR_LOAD_PALETTE_FILE 0x0041B6D0u  /* int32_t(const char *, void *) */
#define ADDR_READ_BMP_PALETTE  0x00422F60u  /* int32_t(const char *, BITMAPINFO *) */
#define ADDR_EXPAND_PALETTE    0x0041AEB0u  /* void(void *, const BITMAPINFO *) */
#define AM2_PALETTE_ENTRIES    0x100
#define AM2_PALETTE_PRISTINE   0x400u       /* the second table, in bytes */
#define ADDR_NULL_STUB_4       0x004170E0u  /* void __stdcall(uint32_t) */
#define ADDR_NULL_STUB         0x0042E170u  /* void(void) */
#define ADDR_RETURN_ZERO       0x0042E980u  /* int32_t(void) */
#define ADDR_RETURN_ONE        0x004354F0u  /* int32_t(void) */
#define ADDR_REVERSE_BLOCKS    0x004231A0u  /* int32_t(dst, src, total, count) */
#define ADDR_COMPARE_PAIR      0x00435EB0u  /* int32_t(const void*, const void*) */
#define ADDR_MAP_CODE          0x00406920u  /* int32_t(int32_t) */
#define ADDR_COMPARE_TRIPLE    0x00435A80u  /* int32_t(const void*, const void*) */
#define ADDR_TYPES_COMPATIBLE  0x00433570u  /* int32_t(int32_t a, int32_t b) */
/* 0x004335F0, two callers. Can this unit pick this weapon up, and into which
 * of its six slots -- `slot` out, and a second out set to 1 for the one case
 * that means "already yours".
 *
 * EIGHT SEPARATE EXITS, not the converging tail its neighbours in this family
 * use; each pops independently with its own side effect, so counting them
 * before writing is what stops eight wrong gotos.
 *
 * Its two "29-entry jump tables" are nothing of the sort. Both have TWO arms
 * and an IDENTICAL byte index, so they encode a PREDICATE over item types --
 * ids 1, 7, 8, 9, 10 and 29 take one arm and everything else the other -- with
 * the same predicate applied to the held weapon and to the candidate. Counting
 * the table entries said "two 29-way dispatches"; READING them said "one
 * six-element set, used twice".
 *
 * A CONDITIONAL WHOSE ARMS ARE IDENTICAL. At 0x00433714 a
 * `cmp [weapon + OBJ_OFF_TARGET_UID], -1; jg` splits into two blocks that both
 * compute `other->OBJ_OFF_TARGET_UID > 0`. The test decides nothing.
 * Reproduced, not fixed -- the second dead conditional found today, after the
 * AI_MODE = 6 both deploy siblings overwrite. */
#define ADDR_CAN_PICK_UP_WEAPON 0x004335F0u /* int32(weapon,unit,int32*,int32*) */
#define OBJ_OFF_HELD_WEAPON_UID  0x564u  /* the one already in hand */
#define AM2_WEAPON_SLOTS         6
/* The predicate those two tables encode, read from the image rather than
 * transcribed: the byte index at 0x00433770 is 29 entries over `kind - 1`, and
 * a zero means the special set -- kinds 1, 7, 8, 9, 10 and 29. The copy at
 * 0x00433798 is byte-identical, which is what says the two dispatches share
 * one predicate. A kind outside 1..29 fails the original's unsigned bound and
 * takes the ordinary arm. */
#define ADDR_PICKUP_KIND_INDEX   0x00433770u  /* uint8_t[29] */
#define AM2_ITEM_KIND_IS_SPECIAL(kind) \
    ((uint32_t)((kind) - 1) <= 0x1Cu \
     && ((const uint8_t *)AM2_IMAGE(ADDR_PICKUP_KIND_INDEX))[(kind) - 1] == 0)
/* 0x00406800, one caller. Which inventory slot should take this weapon, and
 * may it be taken at all? The slot goes to an out-parameter and the answer is
 * the return: -1 means the kind needs no slot, -2 that all six are full. */
#define ADDR_PICK_WEAPON_SLOT  0x00406800u /* int32(cand, unit, int32 *slot) */
/* 0x00406720, two callers, and PickWeaponSlot's only caller. Should this unit
 * take this weapon -- and if its inventory is full, which one does it drop to
 * make room? Answers the weapon's ADDR_THING_CODE, or 0 for a refusal. */
#define ADDR_TRY_TAKE_WEAPON   0x00406720u /* int32_t(void *cand, void *unit) */
#define AM2_SLOT_NONE_NEEDED   (-1)
#define AM2_SLOT_ALL_FULL      (-2)
/* RoachAliveStepB is what makes this pair legible. The first is handed one of
 * eight compass directions and the roach's step window, and the second turns a
 * turn back into one of the eight -- so the "14" in both names is the step
 * window's ROACHSTEP_OFF_STATE offset and the pair are the roach's facing in
 * and out. Names kept, since neither body has been read. */
#define ADDR_SET_FACING_14     0x0043D450u  /* void(dir, obj, step) */
#define ADDR_SET_FACING_08     0x0045C5E0u
#define ADDR_IS_KIND_10_17     0x0044BBF0u  /* int32_t(int32_t) */
#define ADDR_IS_KIND_14_22     0x00433500u  /* int32_t(int32_t) */
#define ADDR_CLASSIFY_CODE74   0x0040D7E0u  /* int32_t(const void *obj) */
/* 0x004499A0, one caller. Is the object's first row on a frame from which its
 * weapon may act? A dense switch over the weapon kind, 1..0x2B, through a
 * 43-byte index table at 0x00449A78 and an eight-slot jump table at
 * 0x00449A58 -- and most kinds have no rule and answer 1. */
#define ADDR_WEAPON_FRAME_READY 0x004499A0u /* int32_t(void *obj, void *wpn) */
#define AM2_WPN_FRAME_KINDS     0x2Bu
#define ADDR_KIND_IN_SET_A     0x0045EE20u  /* int32_t(int32_t kind) */
#define ADDR_KIND_IN_SET_B     0x00433520u  /* int32_t(int32_t kind) */
/* The 1bpp source bitmap the RLE mask is encoded FROM, and the encoder's own
 * bit test. Rows run bottom-up, as a DIB's do. */
#define ADDR_BITMAP_BIT_SET  0x004232C0u  /* int32(base, x, y, height, stride) */


#define ADDR_MASK_PIXEL_SOLID  0x0041CF20u  /* int32_t(x, y, const void *mask) */
#define ADDR_MASK_PIXEL_SOLID32 0x0041CEC0u  /* same, dword row table */
#define ADDR_OBJ_MASK_BIT_AT   0x00435390u  /* int32_t(obj, const AM2_Point *) */
#define ADDR_OBJ_NEXT_KIND538 0x0040D880u  /* int32_t(obj, int32_t want) */
#define ADDR_COLLAPSE_DELTAS  0x00439CC0u  /* void(uint16_t *, int32_t *) */
#define ADDR_REMAP_RLE_RUNS   0x00423EE0u  /* void(rle, unused, wide, table) */
#define ADDR_XOR_CHECKSUM      0x00402700u  /* uint32_t(const void *record) */
#define ADDR_CHAIN_FIELD_14    0x004010B0u  /* uint32_t(const void *p) */
#define ADDR_LIST_PUSH_FRONT   0x00429F20u  /* void(void *node, void **head) */
#define ADDR_LIST_UNLINK       0x0041DAD0u  /* void(void *node, void **head) */
/* 0x0041DD90, the dirty-rectangle collector -- IntersectRect into a 500-entry
 * list with an overflow flag, and it touches no row flag at all. Named here
 * because ADDR_ROW_UNREGISTER_ALL calls it before it unlinks anything, so the
 * region a row occupied is marked for repaint while the row still knows where
 * it was. Stays original. */
#define ADDR_DIRTY_COLLECT     0x0041DD90u  /* void(const AM2_Rect *) */
/* A row's own rectangle, which is what it hands the collector. */
#define ROW_OFF_RECT           0x0Cu
/* 0x00435440, one caller. Is a point on a SOLID pixel of the object's first
 * row -- rectangle first, then the sprite's own mask. */
#define ADDR_REMAP_BYTES       0x0041BB60u  /* void(dst, src, table, count) */
#define ADDR_SET_FIELD_IN_ALL  0x00434E90u  /* int32_t(void *record, void *v) */
/* The "Flame On!" cheat's three globals, named from the two cheat arms that
 * write them -- 0x00417E20 sets the flag and clears the clock, 0x00417EF0
 * clears the flag. The record is what SetFieldInAll points the leader's
 * weapon field at; "Flame Off!" restores what the object had saved at +0x52C,
 * which is what says the two are the same kind of thing. */
/* 0x00417930, one caller -- and that caller is the cheat dispatcher two
 * functions along from the Flame arms, so this is a cheat's effect too:
 * twenty-five armed enemies appear inside the visible view. Reconstructed. */
#define ADDR_PORTAL_SPAWN        0x00417930u  /* void(void) */
#define ADDR_STR_PORTAL_WAV      0x00476B04u  /* "portal2_8bit.wav" */
#define ADDR_STR_ONE_LETTER      0x00476B00u  /* "a" -- the trooper's name */
#define AM2_PORTAL_COUNT         0x19   /* 25 of them */
#define AM2_PORTAL_SPAN_X        0x26C  /* the random point's range, added to */
#define AM2_PORTAL_SPAN_Y        0x1CC  /* ... the view origin */
#define AM2_PORTAL_EFFECT        0x85
#define AM2_PORTAL_ACTION        0xC8   /* handed to ADDR_TYPE238_ACTION */
#define ADDR_FLAME_ON          0x004FCFA0u  /* int32_t */
#define ADDR_FLAME_NEXT_MS     0x004FCFA4u  /* uint32_t, game-clock ms */
#define ADDR_FLAME_RECORD      0x004FCDF8u
#define AM2_FLAME_PERIOD_MS    0xC8         /* 200 ms between bursts */
#define AM2_FLAME_EFFECT       0x14A
/* 0x00417810, one caller -- the per-frame path. The cheat's actual effect. */
#define ADDR_FLAME_TICK        0x00417810u  /* void(void) */
#define ADDR_FIELD51_MEETS_MIN 0x0040A490u  /* int32_t(const void *p) */
#define ADDR_OBJ_KIND538_10_17 0x0040D860u  /* int32_t(const void *obj) */
#define ADDR_FILTER_MATCHES    0x0041EF20u  /* int32_t(wantA,wantB,haveA,haveB,maskA,maskB) */
#define ADDR_CONSUME_PENDING   0x00408520u  /* void(src, dst, cfg) */
#define ADDR_FACING_DELTA_08   0x0045C870u  /* int32_t(const void*, int32_t) */
#define ADDR_FACING_DELTA_14   0x0043D550u  /* int32(step, turn) */
#define ADDR_MAP_CODE_18_28    0x00406A40u  /* int32_t(int32_t code) */
#define ADDR_OBJ_CODE_UNMAPPED 0x00449EF0u  /* int32_t(const void *obj) */
#define ADDR_MEETS_ALL_THREE   0x00409650u  /* int32_t(const void *p) */
/* 0x0042A240, 400 bytes and three callers. Every object in a rectangle: clip
 * the rectangle to the map (0x00514DD0 and 0x00514DD4 are its extents), turn
 * it into tile coordinates with an arithmetic shift of eight, walk the cells
 * and keep whatever the predicate accepts. The name is ours, from the body.
 *
 * The result is a list threaded through OBJ_OFF_QUERY_NEXT of each object,
 * the same scratch chain ADDR_OBJECTS_HIT_BY_POINT and ADDR_WALK_CELL_AT_POINT
 * answer with -- this is the fourth member of that family, and the only one
 * that walks a BLOCK of cells rather than one. Walking a block means an
 * object whose hit rect spans several cells would be answered once per cell,
 * so it carries a de-duplication rule the other three have no need of: an
 * object is taken only from the cell holding its own top-left, or, if that
 * cell is outside the query, from the first row and column scanned. See the
 * reconstruction in win32/mapdraw.cpp -- it lives there rather than beside
 * its siblings in item.cpp because it clips with IntersectRect. */
#define ADDR_OBJECTS_IN_RECT   0x0042A240u  /* obj *(const AM2_Rect *, void *, pred) */
/* 0x0042A3D0, 384 bytes and three callers -- ADDR_STEP_TYPE6, 0x0043D330 and
 * ADDR_SEQ_STEP7, all three passing ADDR_OBJ_MAP_DESC. The same query with no
 * predicate, and two differences that are easy to miss because the bodies are
 * otherwise instruction for instruction the same: its entry clip demands the
 * WHOLE rectangle be on the map where ADDR_OBJECTS_IN_RECT's accepts any
 * overlap, and its home-cell rule has one arm fewer. Both written up at the
 * definition in win32/mapdraw.cpp. */
#define ADDR_ALL_OBJECTS_IN_RECT 0x0042A3D0u /* obj *(const AM2_Rect *, void *) */
/* 0x00409680. Is there an enemy within five hundred units of a point? Used by
 * DoAirSupport to decide whether paratroopers are dropping into a fight, which
 * is the difference between its kind 2 and its kind 3. */
#define ADDR_FIND_ENEMY_NEAR   0x00409680u  /* uint32_t(uint32_t where, uint32_t from) */
/* 0x00409710, "DoAirSupport paratroopers where: %d, from %d, army %d,
 * count: %d" -- its own name, on its own line. Three callers. */
#define ADDR_DO_AIR_SUPPORT    0x00409710u  /* int32_t(int32, uint32, uint32) */
/* THE FOG OF WAR. What follows replaces three commits of a puzzle, and the
 * puzzle only existed because two names were inverted from the start.
 *
 * 0x0041A1B0 walks the whole registry and, for every enemy object of type 2,
 * 3 or 8 that is not already destroyed, calls one of these two -- chosen by
 * ADDR_FOG_OF_WAR, which it INVERTS on entry. Its two callers are arms of the
 * cheat table at 0x00417B80, and they name the pair outright:
 *
 *   "I see everything!"                 stores 0 -> becomes 1 -> ObjReveal
 *   "I bury my head 'neath the sand."   stores 1 -> becomes 0 -> ObjConceal
 *
 * THE FLAG IS WRITTEN TWICE ON THE WAY THROUGH and which write you look at
 * decides which polarity you conclude. The cheat arm stores what it wants;
 * 0x0041A1B0 opens with `sete al` on the old value and stores the COMPLEMENT,
 * and the sweep below it reads that. This paragraph used to say the cheat
 * "clears the flag -> ADDR_OBJ_REVEAL", which is true of the cheat's own store
 * and the opposite of what the sweep then sees -- and it sat two hundred lines
 * from the ADDR_FOG_OF_WAR comment saying the other thing. One inversion, two
 * comments, three commits of puzzle.
 *
 * So 0x0200 is CONCEALED and 0x0800 is REVEALED, established by the game's own
 * strings rather than by inference. The two functions are exact inverses --
 * each guarded on 0x0200, each setting it the other way, each writing row bit
 * 1 the other way -- which is why every reading of them in isolation came out
 * self-consistent and contradictory with its neighbour.
 *
 * RevealNearby skips an object already carrying 0x0800 because it is already
 * revealed, and stamps OBJ_OFF_REVEALED_UNTIL so it stays that way.
 *
 * THE SWEEP READS THAT STAMP THE OTHER WAY ROUND from what this used to say.
 * It conceals when the stamp is 0 or when the clock has NOT yet reached it,
 * and skips when it HAS -- `jae` on `cmp clock, stamp`, jumping to the loop
 * tail. So the cheat overrides a live reveal window rather than respecting it,
 * and the objects it leaves alone are the ones whose window has expired and
 * which the ordinary sweep will conceal anyway. Read off the branch rather
 * than from the shape.
 *
 * The names this replaces were OBJ_FLAG_ON_MAP, OBJ_FLAG_OFF_MAP, TakeOffMap
 * and TakeNearbyOffMap, and every one of them meant the opposite of what it
 * said. They came from reading 0x004296E0's row loop as unregistering, which
 * is what it does -- to the CONCEALED half of a two-state pair, so the object
 * disappears from the map when the flag goes DOWN. */
#define ADDR_OBJ_REVEAL        0x004296E0u  /* void(obj *) */
/* The counterpart, 144 bytes, thirteen callers. Takes a second argument that
 * ADDR_OBJ_REVEAL has no equivalent of: it declines when fog is off unless
 * that argument is non-zero, so a caller can force a conceal past the cheat. */
#define ADDR_OBJ_CONCEAL       0x00429650u  /* void(obj *, int32_t force) */
/* 0x0041A1B0, the sweep. FLIPS ADDR_FOG_OF_WAR on entry, then applies the
 * matching one of the pair to every enemy object. Not "recompute visibility";
 * it is the cheat's own toggle and has no other caller. */
#define ADDR_TOGGLE_FOG_OF_WAR 0x0041A1B0u  /* void(void) */
#define OBJ_FLAG_REVEALED      0x0800u
#define OBJ_FLAG_CONCEALED     0x0200u
/* Bit 4 of OBJ_OFF_FLAGS, and the field beside it that ObjConceal tests with
 * it. Both structural: the three-deep condition they form is what decides
 * whether OBJ_FLAG_REVEALED is cleared, and nothing read so far says why. */
#define OBJ_FLAG_BIT4            0x10u
/* The two 256-byte-record pointers, and reaching them from the object rather
 * than through the sub-record is what confirms ADDR_SCRIPT_SET_OBJ_TABLE's
 * note: that function writes "+0x4C0 and +0x4C8 of the sub-record at obj+0x6C",
 * and obj + 0x6C + 0x4C0 is exactly +0x52C. Two routes to one pair of fields.
 *
 * SetObjTablePair writes both, and NOT the same way: +0x52C is indexed by the
 * KIND it was given and +0x534 by the object's army's comm SLOT. */
#define OBJ_OFF_TABLE_REC_KIND   0x52Cu
#define OBJ_OFF_TABLE_REC_SLOT   0x534u
/* 0x0044BA70, one caller. Set an object's kind, both table-record pointers,
 * propagate one of them, and refresh its rows. */
#define ADDR_SET_OBJ_TABLE_PAIR  0x0044BA70u  /* void(uint32_t uid, int32_t) */
#define OBJ_OFF_FIELD_530        0x530u  /* int32_t; ObjConceal compares to 5 */
/* 0x0043CD40, two callers. Move an object to a new OBJ_OFF_FIELD_530 and put
 * the first row on the animation frame that goes with it -- gated on the
 * current animation having finished, unless the states involved allow the
 * interruption. Named for the field, as the field is named for its offset. */
#define ADDR_SET_OBJ_FIELD_530   0x0043CD40u  /* void(void *obj, int32_t) */
/* Seven int32 frame ids, indexed by the NEW field-530 value and nothing
 * bounds the index: 81, 81, 80, 85, 6, 35, 84. Past the seventh is string
 * data. */
#define ADDR_FIELD_530_FRAMES    0x00487BF8u  /* int32_t[7] */
#define AM2_FIELD_530_FRAMES     7
#define AM2_FIELD_530_DELAY_MS   0xC8         /* 200, stamped for state 4 */
/* 0x00449200, one caller. Put an object into state 5 and give up its
 * ALTERNATE table record: SAVED_OFF_TABLE_REC3 moves into
 * SAVED_OFF_TABLE_REC2, the alternate is cleared, and the value goes through
 * ADDR_SET_FIELD_IN_ALL. Refuses if the object is already in state 5.
 *
 * That state is what ObjsAreAllied tests: it chooses REC3 only when its third
 * argument is set AND this field is not 5, so once this has run the alternate
 * is never chosen again -- which is consistent, since it has been cleared.
 * ObjConceal tests the same 5.
 *
 * Its one caller reaches it when BOTH the object's health fields are at or
 * below zero and it is a type 2, so "the unit is finished" is the occasion.
 * The name is the mechanism rather than the occasion, because one call site is
 * thin ground for the second. Reconstructed. */
#define ADDR_OBJ_DROP_ALT_RECORD 0x00449200u  /* void(void *obj) */
#define AM2_OBJ_STATE_5          5
#define OBJ_OFF_FLAGS          0x08u
/* The object's own sub-list: a count and an array of 0x60-byte rows, each of
 * which registers itself with the map. */
/* The SUB-PIXEL remainder a moving object carries, two floats, added to the
 * step before it is truncated so that a fractional speed accumulates instead
 * of being lost every frame. ROW_OFF_FRAME and ROW_OFF_HEADING are at these
 * two offsets on a ROW, which is a different structure -- the same overlap
 * CLAUDE.md records for the ROW_/OBJ_ pair at 0x1C, and the reason both
 * prefixes exist. */
/* The vertical pair, named by exact analogy with the X/Y one below and
 * confirmed by the arithmetic that reads them: OBJ_OFF_VEL_Z is scaled by
 * ADDR_FRAME_DELTA_SEC into OBJ_OFF_ROW0_Y_ADJUST with the remainder kept,
 * then ADDR_GRAVITY x the same scale is subtracted from it.
 *
 * ZERO MEANS NOT FALLING. ObjMoveAlongFacing compares OBJ_OFF_VEL_Z against a
 * float zero and skips the whole vertical block when it matches -- which is
 * what AM2_VEL_Z_MIN is for: subtracting gravity can land exactly on zero, and
 * that would mark a falling object as landed. Both constants were read out of
 * the image rather than reasoned about, and the first reading of them as
 * sentinels distinct from zero was wrong. */
#define OBJ_OFF_VEL_Z          0x48u   /* float */
#define OBJ_OFF_SUBPIXEL_Z     0x54u   /* float */
#define ADDR_GRAVITY           0x004852ECu  /* float 440.0, units/s/s */
#define AM2_VEL_Z_MIN          0xBC23D70Au  /* -0.01f, the anti-zero nudge */
#define OBJ_OFF_SUBPIXEL_X     0x4Cu   /* float */
#define OBJ_OFF_SUBPIXEL_Y     0x50u   /* float */
#define OBJ_OFF_ROW_COUNT      0x70u
#define OBJ_OFF_ROWS           0x74u
/* The POSE a unit is in and the one queued behind it. Both int32, both written
 * only by ADDR_SET_UNIT_POSE, and the pending one is consumed there too -- so a
 * queued pose waits for the current animation to finish and for nothing else.
 *
 * "Pose" rather than "state" because of the table: ADDR_SET_UNIT_POSE indexes
 * ADDR_WEAPON_POSE_FRAMES with this field, and that table was already named
 * from the other side, where ADDR_WEAPON_POSE_INDEX computes an index into it
 * from (object, weapon). Same table, same index, two callers -- and the three
 * OBJ_OFF_HEIGHT_ADJ values it selects, 8, 0x10 and 0x18, are three heights,
 * which is what stand/kneel/prone look like. The alias ratchet is what found
 * this: naming the table a second time failed the build. */
#define OBJ_OFF_POSE               0x538u  /* VEHICLE_OFF_PTR_LIST on a vehicle */
#define OBJ_OFF_POSE_PENDING       0x53Cu  /* which makes this its seat COUNT */
/* THE SAME THREE DWORDS ON A ROACH, and none of them is a pose or a table
 * slot. RoachAliveStepB uses +0x534 as a game-clock stamp, +0x538 as the base
 * compass direction its search fans out from, and +0x53C as the fan counter --
 * even values step clockwise from the base, odd ones anticlockwise, both
 * masked to eight. Overloading by type, as at 0x52C and 0x538 already, and
 * named per type rather than aliased. */
#define ROACH_OFF_STAMP            0x534u  /* uint32_t, game-clock ms */
#define ROACH_OFF_BASE_DIR         0x538u  /* int32_t, 0..7 */
#define ROACH_OFF_FAN              0x53Cu  /* int32_t, the search counter */
#define AM2_ROACH_BLOCKED_MS       0x1F4   /* 500; under this it keeps fanning */
#define AM2_ROACH_FAN_LIMIT        7       /* directions tried before giving up */
/* 0x0040D930, nine callers. Put a unit into a pose: refuse if it is already
 * there, queue instead of switching for some poses, wait for the current
 * animation's last cell for others, then set OBJ_OFF_HEIGHT_ADJ from the frame
 * and call SetAnimFrame. Reconstructed, and heavily exercised: 26,057 calls on
 * a driven Boot Camp mission against SetAnimFrame's 12,399, so more than half
 * stop at the early-out or the wait. */
#define ADDR_SET_UNIT_POSE         0x0040D930u  /* void(void *obj, int32 pose) */
#define AM2_OBJ_ROW_STRIDE     0x60u
/* 0x0041D480, 37 callers. It was ADDR_ROW_UNREGISTER, "take one row out of the
 * map descriptor's cell lists", and that is only one of its two outcomes.
 *
 * Read: it removes -- by calling ADDR_ROW_UNREGISTER_ALL, which is the function that
 * really does that -- when the row's bit 0 is clear, or when bit 1 is set, and
 * otherwise it RE-LINKS the row at cell coordinates it recomputes from the
 * row's own position fields. So the caller CHOOSES which by setting bit 1
 * first, and the second argument forces the work even when nothing moved.
 *
 * That resolves what looked like a contradiction in 0x00429650, which sets bit
 * 1 on every row before calling this and therefore removes, while RevealObj
 * clears bit 1 and therefore does not. RevealObj's own comment in air.cpp
 * says "unregister" for the clear-bit-1 path and should be re-read against
 * this; flagged rather than rewritten, because that path also depends on the
 * row's bit 0 and I have not read ADDR_ROW_UNREGISTER_ALL. */
#define ADDR_ROW_UPDATE        0x0041D480u /* void(row *, int32 force, desc) */
/* Bit 1 of a ROW's own flags word, which is what ADDR_ROW_UPDATE branches on:
 * set means take the row out of the map's cell lists, clear means put it back.
 * It shares its value with OBJ_FLAG_OVERDUE and nothing else -- that one lives
 * in an OBJECT's flags at +0x08 and is a different field in a different
 * struct. air.cpp spelled the row's bit with the object's name for as long as
 * both were 0x02 and it read as though the two were related. */
#define ROW_FLAG_REMOVED         0x02u
/* 0x0041D3A0: the same row's TEARDOWN -- unregister it and free the buffer it
 * owns at +0x38, but only when its +0x34 flag says there is one.
 *
 * The unregister it calls is 0x0041DB20 and NOT ADDR_ROW_UNREGISTER above,
 * which this comment used to imply. The two are different functions; this one
 * takes the row and the descriptor and no index. */
#define ADDR_ROW_RELEASE       0x0041D3A0u /* void(row *, void *desc) */
#define ADDR_ROW_UNREGISTER_ALL 0x0041DB20u /* void(row *, void *desc) */
/* 0x0041D3D0, three callers. Put a new sprite on a row, rebuilding its cell
 * buffer only when the new sprite needs more cells than the old one did.
 *
 * The cell count is the same arithmetic RowAlloc uses -- and NOT the same
 * types. RowAlloc multiplies two int8 and stores a byte; this multiplies two
 * int32 and compares the result against that byte. A sprite big enough to
 * overflow the byte therefore looks bigger here than the row can ever record,
 * so it rebuilds every time. Both are the original's; see maprow.cpp.
 *
 * A NULL sprite returns without touching anything, which is what makes the
 * three callers' unguarded use of it safe. */
/* The sprite's bounds, as a rect at +0x14; this family reads the right and
 * bottom out of it and hands them to RowAlloc as a width and a height. Spelled
 * as an offset because maprow.cpp is flat and cannot name AM2_Sprite. */
#define SPRITE_OFF_BOUNDS       0x14u
/* The row's sprite slot and the copy of it ADDR_ROW_UPDATE last registered.
 * That function needs +0x04 non-null to do anything and compares it against
 * +0x08 alongside the current and previous rectangles, which is the shape of a
 * "nothing has changed, skip the work" test and is what identifies the pair. */
#define ROW_OFF_SPRITE         0x04u
#define ROW_OFF_PREV_SPRITE    0x08u
/* Where the row is now and where it was when it was last registered. The
 * "was" pair is written as ONE dword copied from the "now" pair, which is what
 * fixes their layout: 0x1C/0x1E and 0x22/0x24, two int16 each. */
#define ROW_OFF_X              0x1Cu  /* int16_t */
#define ROW_OFF_Y              0x1Eu  /* int16_t */
#define ROW_OFF_Y_ADJUST       0x20u  /* int16_t, taken off Y as well as hotY */
/* A row's byte at +0xB0, written by MakePlacedUnit from the vehicle's
 * OBJ_OFF_FIELD_530 and only when the vehicle has more than one row. Named
 * structurally: what it selects is not established, and OBJ_OFF_SCRIPT_ID
 * shares the number on a different structure. */
#define ROW_OFF_FIELD_B0       0xB0u  /* uint8_t */
/* Two more the seq adders write and nothing here reads. 0x26 takes 0x3E8 from
 * kind 5 and 1 from kind 7, so it is a scale or a count rather than a flag;
 * 0x2C is only ever zeroed. Named for their offsets, which is all that is
 * established. */
#define ROW_OFF_FIELD_26       0x26u  /* int16_t */
/* IT IS THE ROW'S REMAP TABLE, and the evidence is that MAPOBJ_OFF_LUT is
 * already 0x2C "-> the sprite's +0x34" -- which is AM2_Sprite::lut, the
 * 256-entry remap. SeqAddKind4 writes ADDR_REMAP_SHADES[0] here where the
 * other three adders write 0, so a kind 4 is a SHADED sprite and the rest are
 * not.
 *
 * Which also says MAPOBJ_* and ROW_OFF_* are two name families for ONE
 * structure: MAPOBJ_OFF_FLAGS is 0x00 and "bit 0 clear means do not draw",
 * and the row's first dword is the bit RowUpdate tests to choose between
 * unlinking and re-linking. Recorded rather than merged -- collapsing two
 * families is a change of its own and wants doing deliberately. */
#define ROW_OFF_FIELD_2C       0x2Cu  /* int32_t, a uint8_t *remap or NULL */
#define ROW_OFF_PREV_X         0x22u  /* int16_t */
#define ROW_OFF_PREV_Y         0x24u  /* int16_t */
/* One entry of the row's buffer: which map cell it is linked into, or -1. */
#define ROW_ENTRY_BYTES        0x10u
#define ROW_ENTRY_OFF_CELL     0x0Cu  /* int32_t */
/* Bit 1 asks ADDR_ROW_UPDATE to remove rather than re-link; bit 2 is its own
 * record that it has done so. */
#define ROW_FLAG_REMOVED_DONE  0x04u
#define ROW_OFF_OWNS           0x34u  /* uint8_t: there is a buffer at +0x38 */
#define ROW_OFF_BUFFER         0x38u
/* The sub-list header inside an object: {list, count, rows, capacity} at
 * OBJ_OFF_SUBRECORD, so the count the object reads at OBJ_OFF_ROW_COUNT and
 * the header's own +4 are the same dword seen two ways.
 *
 * THE FIRST FIELD WAS `?` UNTIL ApplyObjFrame WAS READ. It is the
 * ADDR_RECORD_LISTS header the rows were built from, and that header's own
 * LISTHDR_OFF_OWNER is the packed sprite key -- so the whole of that
 * function's "is this object already showing this frame" test is
 * `sub->list->owner == PackKey(set, index, frame)`, two dereferences deep.
 * Named from a reader; MakeRecordList is the writer that puts the key there,
 * and the two agree. */
#define SUBREC_OFF_LIST        0x00u
#define SUBREC_OFF_COUNT       0x04u
#define SUBREC_OFF_ROWS        0x08u
#define SUBREC_OFF_CAPACITY    0x0Cu
/* 0x00434E60, three callers. Clear bit 0 on every row the sub-list holds, so
 * none of them draws. Two of the three call it right after testing
 * OBJ_FLAG_DESTROYED, so this is what taking a destroyed object off the screen
 * looks like from the sub-list's side. Name ours. */
#define ADDR_SUBREC_HIDE_ROWS  0x00434E60u  /* void(void *subrec) */
/* The object's REGISTRATION table -- a byte count and an array of 0x10-byte
 * entries, each holding the index of the cell list it is linked into. -1 is
 * "not linked", which is what the teardown writes back. */
/* 0x00429B60, one caller -- ADDR_APPLY_OBJ_FRAME, which calls it only when
 * the box it is about to pass differs from the one the object already holds.
 * maprow.cpp's RowAlloc for objects: unlink, write the box twice (offsets at
 * OBJ_OFF_BOX_OFFSETS, absolute at OBJ_OFF_HIT_RECT), size and grow the cell
 * entry array, re-initialise every entry, relink. Reconstructed. */
#define ADDR_ITEM_SET_BOX      0x00429B60u  /* void(obj, l, t, r, b) */
/* 0x00429F40, two callers. The link half: one entry per cell the object's
 * OBJ_OFF_HIT_RECT covers, each pushed onto that cell's head. It is the reason
 * ADDR_OBJECTS_IN_RECT needs a de-duplication rule at all -- an object really
 * is in several cells. Reconstructed in item.cpp beside ADDR_ITEM_PRE_DESTROY,
 * which is the unlink half. */
#define ADDR_ITEM_LINK_CELLS   0x00429F40u  /* void(obj, cells) */
#define OBJ_OFF_CELL_COUNT     0x8Cu   /* uint8_t */
#define OBJ_OFF_CELL_ENTRIES   0x90u
#define AM2_CELL_ENTRY_STRIDE  0x10u
#define CELL_ENTRY_OFF_INDEX   0x0Cu   /* int32_t, or -1 */
/* The argument's array of list heads, indexed by that. */
#define CELLS_OFF_HEADS        0x0Cu
#define OBJ_OFF_REVEALED_UNTIL      0x5Cu   /* game-clock ms, set by the below */
/* 0x004097D0, 112 bytes, two callers. Everything of type 2, 3 or 8 within a
 * radius of a point goes off the map and comes back later. */
/* 0x00403AF0, 80 bytes, three callers. An object's position with its sprite's
 * second anchor pair taken off it -- see SPR_OFF_OVX. The name is ours.
 *
 * It picks ROW 1 when the object has more than one row and row 0 when it has
 * exactly one, which is not the same as "the first row" and is reproduced
 * rather than tidied. A null object answers ADDR_ZERO_POINT; every other
 * failure along the way answers the unadjusted position. */
#define ADDR_OBJ_ANCHOR_POINT  0x00403AF0u  /* uint32_t(const void *obj) */
/* 0x00404400, two callers. Writes the follower's formation position for
 * `slot` into `out`. Stays original; see OBJ_OFF_FORMATION_SLOT. */
/* The twelve formation slots, {uint8 facing, pad, int16 distance, pad}. */
#define ADDR_FORMATION_SLOTS   0x00473EA0u
#define AM2_FORMATION_SLOTS        12
#define AM2_FORMATION_SLOT_STRIDE  6u
/* Slot 12 and above, which the table does not cover. Stays original. */
#define ADDR_FORMATION_POINT_FAR 0x004042A0u /* void(follower, leader,
                                              *      AM2_Point *, int32) */
/* 0x00439F40, five callers, all of them `TileOfPoint(pt)` and then this with
 * the same point. The name is ours and describes the effect.
 *
 * What the dispatch does IS established now: this is 0x0043A0A0's twin, the
 * same square spiral, run under ADDR_POINT_RULE_DEFAULT because it installs
 * the rule for a null object. It differs from the sibling in writing nothing
 * through the point when the starting tile is already accepted, and in taking
 * two parameters rather than three. Reconstructed in region.cpp beside it. */
#define ADDR_SETTLE_POINT_IN_REGION 0x00439F40u /* int32_t(int32 tile,
                                                 *         AM2_Point *) */
/* 0x00437E00, four callers -- and one of them is 0x00439F40 itself, which
 * installs the rule and then dispatches through it in the same breath. Choose
 * which of three rules a point gets settled under, from the object doing the
 * asking, and record that object's army beside it.
 *
 * The three arms are established by the tests rather than by reading the
 * handlers: ObjIsType3 with VEHICLE_OFF_KIND 5 -- which the unit-type
 * table calls `ptboat` -- takes one; any other vehicle and any roach take
 * another; a null object or anything else takes the third. So the boat has a
 * rule of its own, which is what a unit that moves on water needs.
 *
 * All three handlers read ADDR_POINT_RULE_ARMY, so the rules are army-aware.
 * A NULL object leaves it holding the previous object's army, because the
 * null path jumps past the store; reproduced. */
#define ADDR_SET_POINT_RULE      0x00437E00u  /* void(void *obj) */
#define ADDR_POINT_RULE_ARMY     0x00523DD8u  /* int32_t, from OBJ_OFF_ARMY */
#define ADDR_POINT_RULE          0x00523DDCu  /* the installed handler */
/* The tile buffer ADDR_TRACE_TILE_LINE fills for ADDR_BEGIN_MOVE_TO -- one
 * uint16 per tile the line crosses, with the count in a local. */
#define ADDR_TILE_LINE_BUF       0x00523DE0u  /* uint16_t[] */
/* 0x00439E90, four callers. Can this object reach that point in a straight
 * line -- and if so, record the move on it.
 *
 * It installs the object's point rule, resolves the target's tile, traces the
 * tiles between the object and the point, and asks the rule about each. Any
 * refusal answers 0 and writes nothing; otherwise the from and to points go
 * into the object at +0x120 and +0x124 and three small fields are seeded.
 * Reconstructed. */
#define ADDR_BEGIN_MOVE_TO       0x00439E90u  /* int32_t(void *obj, uint32 *to) */
/* These five are ONE STRUCTURE and PlanPathTo is what says so. +0x120 is a
 * WAYPOINT LIST -- {int16 x, int16 y} pairs, stride 4, terminated by a zero
 * word -- with +0x520 the index of the one being walked to and +0x522 how many
 * there are. BeginMoveTo is the two-waypoint case: from at +0x120, to at
 * +0x124, the terminator at +0x128, index 1 of 2. That is why its three small
 * fields were "seeded 0, 1 and 2" and why nothing said what they were.
 * PlanPathTo writes as many as the route needs and sets the same three. */
#define OBJ_OFF_MOVE_FROM        0x120u   /* packed point, and waypoint 0 */
#define OBJ_OFF_MOVE_TO          0x124u   /* packed point, and waypoint 1 */
#define OBJ_OFF_MOVE_END         0x128u   /* the terminator when there are 2 */
#define OBJ_OFF_MOVE_AT          0x520u   /* uint16_t, the waypoint in hand */
#define OBJ_OFF_MOVE_COUNT       0x522u   /* uint16_t */
#define AM2_MOVE_STEP_BYTES      4u
/* When the move expires. PlanPathTo sets it to the clock plus
 * AM2_MOVE_VALID_MS on success and plus ADDR_PATH_RETRY_MS on failure, so a
 * route that could not be found is not attempted again for half a second. */
#define OBJ_OFF_MOVE_UNTIL       0x11Cu   /* game-clock ms */
#define AM2_MOVE_VALID_MS        0xBB8    /* 3000 */
#define ADDR_PATH_RETRY_MS       0x00487894u  /* int32_t, 500 */
/* 0x004395B0, 1,808 bytes, one caller. SURVEYED AND NOT RECONSTRUCTED.
 * PlanPathTo hands it the two tiles, ADDR_TILE_LINE_BUF, a length out and its
 * own third argument, and treats a zero answer as "no route".
 *
 * IT IS RegionFindPath AGAIN, ONE GRANULARITY DOWN, and that is the finding
 * the rest of this block rests on. Its node carries the SAME EIGHT FIELDS IN
 * THE SAME ORDER as REGION_OFF_G..NEXT -- cost, heuristic, depth, stamp,
 * parent, prev, next, state -- packed into sixteen bytes of uint16 where the
 * region version spreads int32s over 0x10..0x28, with STATE moved from fifth
 * to last. The heuristic is the same expression too, ApproxDistXY scaled by
 * AM2_CONST_1_5, which REGION_OFF_H already records. So the image contains one
 * weighted A* written twice: once over regions, once over tiles.
 *
 * AND IT HAS THE SAME DEFECT. Unlinking a node that is the open list HEAD sets
 * the head to ZERO rather than to the node's successor -- `xor edx,edx; mov
 * [esp+0x44],edx` at 0x00439945, against `edx = [esp+0x44]` on the other arm --
 * so the rest of the list is dropped. tools/pathcheck.py already reproduces
 * that for RegionFindPath rather than fixing it; the same choice applies here,
 * and a model that "corrects" it will disagree with the original.
 *
 * THE RESUME ARM IS DEAD CODE, by the scan this file prescribes rather than by
 * reading. 0x00487828 gates it, ships as -1, and its ONLY other reference
 * writes -1 back; the two globals that arm reads -- 0x00554B80 for the goal and
 * 0x00523DC8 for the open-list head -- have exactly ONE reference each in the
 * whole image and it is this read. Three globals with no writer, so a
 * continuation can never be resumed. Reproduce it; do not explain it.
 *
 * THE NODE ARRAY'S BASE IS CONFIRMED BY TILING. 0x00554BD8 + 0x10000 tiles *
 * 16 bytes lands exactly on 0x00654BD8, and the next thing anything touches is
 * 0x00654C30, 88 bytes later -- the same 88-byte gap that sits between
 * 0x00554B80 and the array. 0x10000 is also the bound the entry checks its two
 * arguments against. A layout that tiles at both ends is the check.
 *
 * THE BUDGET ADAPTS, which is the part no reading of the loop would suggest.
 * Both exits measure Ticks() across the search and, when it took more than
 * 3 ms, move ADDR_PATH_MAX_NODES toward what the machine actually managed:
 * `(MAX_SEARCHES * considered / elapsed + budget / 2) / 2 * 2`, rounded down
 * to even and floored at 0xFA0. So a slow machine searches less. The two
 * epilogues are instruction-for-instruction the same bar a register choice --
 * diffed, not assumed.
 *
 * FOUR CALLEES AND NOTHING ELSE: ApproxDistXY (ours, pure), _ftol, Ticks, and
 * the installed ADDR_POINT_RULE through a function pointer. That is what makes
 * it a tools/pathcheck.py target rather than an unverifiable one -- Ticks is
 * the only nondeterminism and a Unicorn hook pins it.
 *
 * Its exits: 1 with the path written backwards from the goal through PARENT
 * and `*n` set to depth+1, or 0. The give-up path is not a failure -- past
 * MAX_NODES it compares the best node's heuristic against the START's and
 * returns the PARTIAL path when it made progress. */
#define ADDR_FIND_PATH           0x004395B0u  /* int32(from, to, uint16 *,
                                               *       int32 *n, int32) */
/* The tile-level search's node array, one per tile index. */
#define ADDR_PATH_NODES          0x00554BD8u  /* [0x10000][16] */
#define AM2_PATH_TILES           0x10000
#define AM2_PATH_NODE_BYTES      16
#define PATHNODE_OFF_G           0x00u  /* uint16, cost from the start */
#define PATHNODE_OFF_H           0x02u  /* uint16, ApproxDistXY * 1.5 */
#define PATHNODE_OFF_DEPTH       0x04u  /* uint16, hops from the start */
#define PATHNODE_OFF_STAMP       0x06u  /* uint16, the search generation */
#define PATHNODE_OFF_PARENT      0x08u  /* uint16 tile, the came-from */
#define PATHNODE_OFF_PREV        0x0Au  /* open-list links, sorted by g+h */
#define PATHNODE_OFF_NEXT        0x0Cu
#define PATHNODE_OFF_STATE       0x0Eu  /* uint8: 1 open, 2 closed */
/* Bumped once per search so a stale STAMP means "never visited". A uint16, and
 * nothing resets it -- it wraps, which is the original's behaviour. */
#define ADDR_PATH_GENERATION     0x00654C30u  /* uint16_t */
/* Milliseconds of searching charged to the current frame, and the frame the
 * charge belongs to: when ADDR_GAME_CLOCK_MS moves, the total resets. Over
 * ADDR_PATH_MAX_SEARCHES the entry refuses outright, so this is a per-frame
 * TIME budget rather than a count of searches despite the name it earns from
 * being compared against one. */
#define ADDR_PATH_FRAME_MS       0x00654C34u  /* int32_t */
#define ADDR_PATH_FRAME_STAMP    0x00654C38u  /* int32_t, ADDR_GAME_CLOCK_MS */
#define ADDR_PATH_MAX_SEARCHES   0x00487890u  /* int32_t, ships 10 */
#define ADDR_PATH_MAX_NODES      0x0048788Cu  /* int32_t, ships 10000, adapts */
#define AM2_PATH_MIN_NODES       0xFA0        /* the adaptive floor, 4000 */
#define AM2_PATH_MIN_ELAPSED     3            /* ms below which it does not adapt */
/* The resume arm's three globals. NONE OF THEM HAS A WRITER -- see above. */
#define ADDR_PATH_RESUME         0x00487828u  /* int32_t, ships -1 */
#define ADDR_PATH_RESUME_GOAL    0x00554B80u  /* int32_t, reads 0 forever */
#define ADDR_PATH_RESUME_HEAD    0x00523DC8u  /* int32_t, reads 0 forever */
/* 0x00439D60, three callers -- both AI families' common steps and 0x00408210.
 * Reconstructed. */
#define ADDR_PLAN_PATH_TO        0x00439D60u  /* int32(obj, uint32 *, int32) */
/* THE THREE RULES ANSWER "IS THIS TILE REFUSED", not "is it allowed", and the
 * consumer is what says so: SettlePointInRegion returns the tile it was given
 * when `!rule(tile)`. All three end by answering 0 for a tile they have found
 * nothing against.
 *
 * They share a tail and differ in their first two tests. Every one of them
 * refuses a tile the asking army's reveal grid has something on, and reads
 * ADDR_POINT_RULE_ARMY to know which grid -- so the rules are army-aware, and
 * a null object leaves that holding the previous object's army.
 *
 * The BOAT's two tests are the interesting pair: it refuses a tile whose
 * AM2_TILE_OPEN bit is clear, and one whose ADDR_TILE_COVER is BELOW
 * AM2_BOAT_COVER_MIN -- a floor rather than the ceiling the other two apply.
 * That threshold is 0x15 against a table the footprint pair moves in fifteens,
 * so it is not a count of footprints and nothing read so far says what it is.
 * Transcribed. */
#define ADDR_POINT_RULE_BOAT     0x00437D60u  /* vehicle kind 5 */
#define ADDR_POINT_RULE_VEHICLE  0x00437D10u  /* other vehicles, and roaches */
#define ADDR_POINT_RULE_DEFAULT  0x00437DB0u  /* everything else, and null */
#define AM2_BOAT_COVER_MIN       0x15
/* The kind is VEHICLE_OFF_KIND, further down and already named. */
#define AM2_VEHICLE_KIND_BOAT    5
/* 0x0042C440, 3,920 bytes. THE MAP LOADER, and this file has called it that in
 * two places for a long time without ever giving the address a name. It opens
 * "%s.atl" for the tileset and "%s.amm" for the map itself, and parses a
 * "camera" record out of the latter.
 *
 * Identified by SWEEPING ITS PUSHED STRING LITERALS -- the minute's work this
 * file recommends and which named MakeBitmap and RestoreTileSet. Run over all
 * five remaining unnamed functions it produced exactly one identification, and
 * confirmed two names that were already right: 0x00417B80 pushes forty-odd
 * cheat phrases and their replies, and 0x00430530 pushes twenty multiplayer
 * panel bitmaps. 0x0044A420 and 0x00420410 push no literals at all, so for
 * those the sweep answers nothing and says so. */
#define ADDR_LOAD_MAP            0x0042C440u  /* int32(const char *base,
                                                       const char *folder) */
/* TWO ARGUMENTS, NOT ONE. This carried `int32(const char *name)` for as long
 * as it has been named, and its only caller settles it outright:
 * `push ADDR_MAP_FOLDER; push ADDR_MAP_NAME; call` with no cleanup between.
 * tools/espmap.py agrees, putting reads at both +0x13C and +0x140.
 *
 * The first is the base name and feeds BOTH "%s.atl" and "%s.amm"; the second
 * goes to SetDataDir and is kept in ADDR_MAP_BLOCK so the directory can be
 * restored on the way out. Written down because a missing argument is the one
 * prototype error a caller cannot expose by compiling -- the extra push is
 * simply ignored, and every field access inside stays correct.
 *
 * AND IT SETTLES TWO ALIAS PAIRS THIS FILE ALREADY HAD. 0x00511A88 is both
 * ADDR_MAP_NAME and ADDR_MAP_NAME; 0x00511AC8 is both ADDR_MAP_FOLDER
 * and ADDR_MAP_FOLDER. Since the first feeds the .amm as well as the .atl,
 * "tileset" is too narrow in both -- the same shape as ADDR_MISSILE_DEFS
 * being named for one of the things it holds. Not collapsed here: a rename is
 * a change to every use at once and belongs in its own commit. */
/* SURVEYED, and it is the friendliest of the three left. 3,920 bytes, FOUR
 * exits, THIRTY-FOUR distinct callees of which only three have no name, and
 * NO indirect dispatch at all -- no jump table to dump, which is what the
 * last two functions' surveys each turned on.
 *
 * It names its own job in the strings it pushes: "%s.atl" is the tileset
 * RestoreTileSet already loads and "%s.amm" is the map itself, with "camera"
 * a token it looks for inside. So this is the map load, and orig.h's
 * int32(const char *name) is consistent with both.
 *
 * The three unnamed are all small and none is a blocker:
 *   0x0042BEA0  opens a file through the CRT with a mode string at 0x00474170
 *               and reserves a 0x450 buffer -- and this is the address
 *               CLAUDE.md's declined list carried as "1200 B, 2 COM calls"
 *               before tools/merges.py split it; the COM half was
 *               RestoreTileSet and this is a different function in the same
 *               entry.
 *   0x004600F0  now ADDR_RESPAWN_KIND_ALLOWED.
 *   0x00460120  now ADDR_BUILD_RESPAWN_POOL. Both read properly and named at
 *               their own definitions; neither is CRT.
 *
 * AND docs/crt.md CALLS BOTH OF THOSE CRT, WHICH THEY ARE NOT. They are
 * listed there with the reason "position" -- meaning they sit above the
 * evidenced frontier and nothing identified them -- and reading them refutes
 * it outright: a CRT routine does not read ADDR_MP_SESSION,
 * ADDR_GAME_OVER_FLAGS or ADDR_RESPAWN_KINDS, all three of which are named
 * game globals in this file.
 *
 * That does not break the CRT exclusion, but it does qualify how it is
 * stated. CLAUDE.md's argument is that 58 functions up there are identified
 * from their own bodies and "the remaining 171 are CRT by sitting above the
 * evidenced frontier" -- and at least two of those 171 are game code, found
 * because a function below the line called them. Reading a global's name is
 * evidence the position rule cannot see.
 *
 * What the survey does NOT settle is which side of the split it belongs on.
 * It reaches files, and this project's answer to "where is the file I/O" is
 * that the game never opens one itself -- it goes through the statically
 * linked CRT, which libc replaces wholesale. If that holds here it is flat.
 * Check before filing it, the way 0x0044A420's `call ebp` turned out to be
 * GetTickCount rather than a callback. */
#define AM2_MAP_TILESET_FMT      0x00486328u  /* "%s.atl" */
#define AM2_MAP_FILE_FMT         0x00486330u  /* "%s.amm" */
/* The map's bounds in pixels, four int32 read as one 16-byte block out of the
 * map file by ADDR_LOAD_MAP and written nowhere else. Distinct from
 * ADDR_MAP_EXTENT_X/Y, which are a different pair at 0x00514DD0. */
#define ADDR_MAP_BOUNDS_LEFT     0x00514DF8u
#define ADDR_MAP_BOUNDS_TOP      0x00514DFCu
#define ADDR_MAP_BOUNDS_RIGHT    0x00514E00u
#define ADDR_MAP_BOUNDS_BOTTOM   0x00514E04u
#define ADDR_RESOLVE_FORMATION_POINT 0x00404580u /* void(follower, leader,
                                                  *      AM2_Point *out) */
/* 0x00404ED0, two callers, and a sibling of the two formation-point helpers
 * below. Pick a point `dist` away from an object on a heading within +/-32 of
 * the way it is facing. ITS FIRST ARGUMENT IS NEVER READ -- both call sites
 * push four, and the body uses only the last three. */
/* 0x00404E50, and the same function as ADDR_RANDOM_POINT_AHEAD with the
 * heading taken from ANOTHER OBJECT rather than from the way this one faces:
 * AngleBetween the two positions, then the same +/-32 spread. */
#define ADDR_RANDOM_POINT_TOWARD 0x00404E50u /* void(target, obj, int32,
                                              *      AM2_Point *) */
#define ADDR_RANDOM_POINT_AHEAD 0x00404ED0u /* void(void *, obj, int32,
                                             *      AM2_Point *) */
#define ADDR_FORMATION_POINT   0x00404400u  /* void(follower, leader,
                                             *      AM2_Point *out, int32 slot) */
#define ADDR_REVEAL_NEARBY 0x004097D0u /* void(AM2_Point, int32, int32) */
/* The sprite LIST -- the array ADDR_FREE_SPRITE_LIST releases and frees, and
 * ADDR_FREE_SPRITE_LIST_ALIAS jumps to. It sits just past the air save block
 * and is not part of it. Capacity grows a HUNDRED entries at a time, the same
 * shape as the script tokeniser's ten.
 *
 * The three global names are ours; the two function names were already here,
 * from the WinMain teardown that calls the alias. */
#define ADDR_SPRITE_LIST_CAP     0x004F96C0u  /* int32_t */
#define ADDR_SPRITE_LIST_COUNT   0x004F96C4u  /* int32_t */
#define ADDR_SPRITE_LIST         0x004F96C8u  /* AM2_Sprite ** */
#define AM2_SPRITE_LIST_GROW     100
#define ADDR_GROW_SPRITE_LIST    0x00409930u  /* void(void) */
/* 0x00409960. Remap a run-length-encoded sprite image in place. The header is
 * {uint16 width, uint16 height} followed by one uint16 per row, so the pixel
 * data starts at 4 + height*2. Each row is pairs of {skip, run} bytes, and the
 * row ends when skip+run has covered the width.
 *
 * Only indices at or above the threshold are remapped, which is the same
 * reserved-block convention NearestPalIndex's `from` argument follows. */
#define ADDR_REMAP_SPRITE_RUNS   0x00409960u  /* void(img, int32, table, int32) */
/* 0x004099F0. Read a whole sprite set from an open file into the sprite list:
 * a count, then that many sprites, each six uint16 fields and two sized
 * blocks. Its one caller is 0x00409FD9.
 *
 * Two fields land in what sprite.h calls pad28, so 0x0028 and 0x002A are a
 * pair of int16 read straight out of the file, not padding. */
#define ADDR_LOAD_SPRITE_SET     0x004099F0u  /* void(FILE *, table, int32, uint32) */
/* 0x00409F50, six callers. Open a sprite file, build a 256-entry remap table
 * from its palette, load the set through it and hand the file to
 * LoadAnimTable. Returns 1, or nothing at all when the open fails --
 * see the note in sprite.h. */
#define ADDR_LOAD_SPRITE_FILE    0x00409F50u  /* int32_t(path, a, b, from, flags) */
/* 0x00409BE0, 768 bytes, LoadSpriteFile's tail and its only caller. The
 * animation table that follows the sprites in a `.ani` file -- see
 * src/game/anim.h. The name is ours: the function's one string is "Error!
 * %d\n", which names nothing. */
#define ADDR_LOAD_ANIM_TABLE     0x00409BE0u  /* void(FILE*, table, base, fb) */
/* 0x0042DFE0, five callers. The bit index of a power of two in 1..0x8000; 0
 * for anything else. A jump table for 1..0x80 and a compare chain above it. */
#define ADDR_LOG2_MASK           0x0042DFE0u  /* uint8_t(int32_t mask) */
/* The animation tables, all of them {int32_t count, AM2_AnimEntry *entries}.
 * Every name here is ours; what fixes each one is the `.ani` path its loader
 * passes. The arrays are indexed by unit kind, and their lengths come from the
 * teardown loops rather than from a guess -- 0x0045A990 walks 0..0x30 by 8, so
 * six each. */
#define ADDR_EXPLOSION_ANIMS     0x00510228u  /* AM2_AnimTable  -- explosions.ani */
#define ADDR_MISSILE_ANIMS       0x00654C90u  /* AM2_AnimTable  -- missile.ani */
#define ADDR_ROACH_ANIMS         0x00654C98u  /* AM2_AnimTable  -- roach.ani */
#define ADDR_SOLDIER_ANIMS       0x00659F00u  /* AM2_AnimTable[9], rifleman first */
#define ADDR_TURRET_ANIMS        0x0065A2A8u  /* AM2_AnimTable[6] */
#define ADDR_VEHICLE_ANIMS       0x00661DF0u  /* AM2_AnimTable[6] */
/* The five `.ani` loaders, one per group and every one `void(void)`. Each
 * chdirs into `data\\ani` and loads what its group needs, skipping any table
 * that already has a count. See src/game/anim.h. */
#define ADDR_LOAD_EXPLOSION_ANIMS 0x00422820u
#define ADDR_LOAD_MISSILE_ANIMS   0x0043C6F0u
#define ADDR_LOAD_ROACH_ANIMS     0x0043CCF0u
#define ADDR_LOAD_SOLDIER_ANIMS   0x00446F50u
#define ADDR_LOAD_VEHICLE_ANIMS   0x0045A8C0u
#define AM2_ANIM_ID_LINK_BREAK    0x46  /* whose `next` the soldier loader
                                         * rewrites to -1 after loading */
/* 0x00409EE0, 14 callers. Free one table: every animation it owns, then the
 * entry array, then zero both fields. A borrowed entry is left alone, which is
 * the whole reason LoadAnimTable records the flag. */
#define ADDR_FREE_ANIM_TABLE     0x00409EE0u  /* void(AM2_AnimTable *) */
#define ADDR_FREE_EXPLOSION_ANIMS 0x00422850u /* void(void) */
#define ADDR_FREE_MISSILE_ANIMS  0x0043C720u  /* void(void) */
#define ADDR_FREE_ROACH_ANIMS    0x0043CD30u  /* void(void) */
#define ADDR_FREE_SOLDIER_ANIMS  0x004470D0u  /* void(void), all nine */
#define ADDR_FREE_VEHICLE_ANIMS  0x0045A990u  /* void(void), both arrays */
/* The three lookups: find the animation with a fixed id, turn an 8-bit heading
 * into one of its directions with RoundTo8, and return the sprite for frame 0.
 * The id is 1 for soldiers and 0x51 for vehicles and turrets, which the
 * shipped `.ani` files bear out -- rifleman.ani has id 1 and every vehicle and
 * turret file has 81. */
/* 0x0043C730, tail-jumped from the roach loader at 0x0043CCF0. The roach's
 * collision mask, one record per direction: a 16-pixel grid over the
 * sprite, a block kept when 16 of its 64 two-pixel samples are opaque.
 *
 * The record is {int32_t count; AM2_Point pts[40]} -- 0xA4 bytes, and the
 * stride is what says 40. The builder's own pointer starts at the POINTS and
 * writes the count through `[ebp-4]`, so the array really begins at
 * 0x00654CA8; taking 0x00654CAC as the base put the whole table one dword
 * early, over the global at 0x00654CA4, and no A/B could see it. */
#define ADDR_BUILD_ROACH_MASK 0x0043C730u  /* void(void) */
#define ADDR_ROACH_MASK_DIRECTIONS 0x00654CA0u /* int32_t */
#define ADDR_ROACH_MASK       0x00654CACu  /* the first record's POINTS */
/* The same table addressed from its real base -- the first record's COUNT, one
 * dword below the points. The original indexes both with the same
 * `dir * AM2_MASK_STRIDE`, so having both spellings is how the code reads
 * rather than an alias: two arrays, one stride. Getting THIS one wrong is the
 * mistake ADDR_BUILD_ROACH_MASK's comment above records. */
#define ADDR_ROACH_MASK_COUNT 0x00654CA8u  /* int32_t, stride AM2_MASK_STRIDE */
#define AM2_MASK_STRIDE        0xA4
#define AM2_MASK_POINTS        40
#define AM2_MASK_STEP          16   /* the grid */
#define AM2_MASK_SAMPLE        2    /* within a block */
#define AM2_ROACH_MASK_MIN_SOLID   16   /* of the 64 samples */
/* 0x0045A450, called from the vehicle loader at 0x0045A8C0 once per kind. The
 * same builder for vehicles, with three differences: it takes the kind, it
 * keeps a block on 12 of the 64 samples rather than 16, and it logs
 * "vehicle mask direction: %d" under -traceVEH, which is where this whole
 * family's vocabulary comes from.
 *
 * Its three tables tile exactly, which is the check that the bases are right:
 * ADDR_TURRET_ANIMS is six 8-byte tables ending at 0x0065A2D8, the direction
 * counts are six dwords ending at 0x0065A2F0, and 6 * 32 records of 0xA4 from
 * there end at 0x00661DF0, which is ADDR_VEHICLE_ANIMS. */
#define ADDR_BUILD_VEHICLE_MASK    0x0045A450u  /* void(int32_t kind) */
#define ADDR_VEHICLE_MASK_DIRECTIONS 0x0065A2D8u /* int32_t[6] */
#define ADDR_VEHICLE_MASK          0x0065A2F4u  /* record [kind*32 + dir]'s
                                                 * POINTS; its count is the
                                                 * dword below */
/* That dword, named for the same reason ADDR_ROACH_MASK_COUNT is: the two
 * spellings are how the code reads, one indexed by `slot` for the count and
 * one for the points four bytes on. */
#define ADDR_VEHICLE_MASK_COUNT    0x0065A2F0u  /* int32_t, stride AM2_MASK_STRIDE */
/* The general footprint pair's own scratch, the twin of ADDR_ROACH_MARK and
 * ADDR_ROACH_MARK_STAMP and at a different address -- the two families do not
 * share it. A stamp bumped once per call, and one uint16 per cell of the 16x16
 * window, so a cell two of the mask's points land on is only counted once. */
#define ADDR_OBJ_MARK              0x00661E20u  /* uint16_t[16 * 16] */
#define ADDR_OBJ_MARK_STAMP        0x00662020u  /* uint16_t */
#define AM2_VEHICLE_MASK_KINDS     6
#define AM2_VEHICLE_MASK_DIRS      32  /* the stride of the kind index */
#define AM2_VEHICLE_MASK_MIN_SOLID 12  /* of the 64 samples -- NOT the roach's 16 */
#define AM2_VEHICLE_MASK_STRIDE    0xA4u  /* one record, count then points */
/* 0x0045BBB0, five callers. Sum the block weight over a vehicle mask's points:
 * round the heading to one of AM2_VEHICLE_MASK_DIRS, take that kind's record,
 * and add up ADDR_BLOCK_WEIGHT_CHAIN or ADDR_BLOCK_WEIGHT_TROOPS over every
 * point offset from a base. KIND 5 IS THE ONE THAT TAKES THE CHAIN VARIANT --
 * that is the "value being 5" ADDR_BLOCK_WEIGHT_CHAIN's comment records
 * without knowing what it was. Reconstructed, and measured at 0: all five call
 * sites are in 0x0043A860, which no drive here reaches. */
#define ADDR_MASK_BLOCK_WEIGHT     0x0045BBB0u /* int32(kind, heading, uint32) */
#define AM2_MASK_CHAIN_KIND        5
/* 0x00446290, two callers. Is this sprite opaque at this point: the run-length
 * mask for a software format, the bounding box for anything else. */
#define ADDR_SPRITE_SOLID_AT     0x00446290u  /* int32_t(AM2_Sprite *, AM2_Point) */
#define ADDR_SOLDIER_ANIM_SPRITE 0x0044BB30u  /* AM2_Sprite *(kind, heading) */
#define ADDR_VEHICLE_ANIM_SPRITE 0x0045D9B0u
#define ADDR_TURRET_ANIM_SPRITE  0x0045DA20u
#define AM2_ANIM_ID_STAND        1
#define AM2_ANIM_ID_VEHICLE      0x51
#define AM2_SPRITE_PALETTE_SIZE  256
#define AM2_AIR_ENEMY_RADIUS   0x1F4        /* 500 */
#define OBJ_OFF_HEALTH         0x62u        /* int16_t */
/* What a damage handler records about the hit before applying it. The third
 * argument of the damage family is a DIRECTION, not a second amount:
 * ADDR_RECV_DAMAGE computes it from the message position and the object's own
 * with 0x0042DEB0 and masks it to a byte, and the message's own trace line
 * calls it "dir". It is clamped up to 1 on the way in, so 0 is never stored.
 *
 * CAVEAT, added after reading the SENDER. Every sender of that message passes
 * the VICTIM's own position, not the attacker's, so the angle the receiver
 * computes is between two views of one object and is near zero when the two
 * sides agree. The field is angle-SHAPED and the game's own trace line calls
 * it "dir"; what it means is less settled than this comment first claimed. 
 *
 * The time is the game clock, so a reader can age the hit. */
#define OBJ_OFF_HIT_DIR        0x104u       /* uint8_t, >= 1 */
#define OBJ_OFF_HIT_TIME       0x108u       /* uint32_t, ADDR_GAME_CLOCK_MS */
/* Written 5 or 6 by ADDR_DAMAGE_ROACH when health reaches zero, chosen on the
 * damage KIND: 1 and 3 give 5, everything else 6. */
#define OBJ_OFF_DEATH_STATE    0x554u       /* int32_t */
/* Bit 0 of OBJ_OFF_FLAGS. Cleared after ADDR_ITEM_PRE_DESTROY_ALIAS runs, and
 * named structurally because nothing yet read says what it means. */
#define OBJ_FLAG_BIT0          0x01u
/* The armour subtraction every roach hit goes through: damage is
 * max(0, amount - this), and the image ships 2. */
#define ADDR_ROACH_ARMOUR      0x00487BB0u  /* int32_t */
/* Wave index 0x32, played at the roach's own position when it dies. */
#define AM2_ROACH_DEATH_SOUND  0x32
#define OBJ_OFF_QUERY_NEXT     0x68u        /* obj *, the query result's thread */
#define ADDR_OBJ_TYPE2_FIELD548 0x00457450u /* uint32_t(const AM2_Object *) */
#define ADDR_POINTS_EQUAL      0x0042E140u  /* int32_t(AM2_Point, AM2_Point) */
#define ADDR_POINTS_DIFFER     0x0042E110u
#define ADDR_OBJ_IS_TYPE238 0x00457420u  /* types 2, 3, 8 */
/* 0x00437400, four callers. Does an object match a selector? Either a NAMED
 * one -- an index into the script name table, which must be a REF whose value
 * is this object's uid -- or a bitmask of nine independent tests. */
#define ADDR_OBJ_MATCHES_SEL   0x00437400u /* int32(int32 byName,int32,void*) */
#define AM2_SEL_TYPE_1OR4        0x0002u  /* ObjType2Field548 */
#define AM2_SEL_TYPE_238         0x0004u
#define AM2_SEL_TYPE_2           0x0008u
#define AM2_SEL_VEHICLE_KIND_1   0x0010u
#define AM2_SEL_TYPE_3           0x0020u
#define AM2_SEL_ARMY_LOCAL       0x0040u
#define AM2_SEL_ARMY_1           0x0080u
#define AM2_SEL_ARMY_2           0x0100u
#define AM2_SEL_ARMY_3           0x0200u
#define ADDR_APPROX_DIST    0x0042DDE0u  /* int32_t(const AM2_Point*, const AM2_Point*) */
/* 0x004064E0, four callers -- and the four are what name it. Each is a
 * one-line wrapper passing an army 0..3 and one of "gflagbase", "tflagbase",
 * "bflagbase" and "grflagbase", so this is the capture-the-flag proximity
 * test: is `who` standing at the named army's flag base.
 *
 * Two conditions, both required. `owner`'s army has to map through
 * CommArmyOfSlot to the army asked for, and `who` has to be within
 * AM2_FLAG_BASE_RANGE of the object the script name resolves to.
 *
 * IT ANSWERS 0 FOR YES. 0x80 is the failure code and it is shared with the
 * three neighbours above it, which return 0x1E, 0x60 and 0x80 -- a set of
 * codes rather than a boolean, so a reconstruction that answered 1 and 0
 * would be wrong in a way no compare against zero would show. */
#define ADDR_AT_FLAG_BASE        0x004064E0u  /* int32(who, owner, army, name) */
/* 0x00406550, two callers. Turn a thing's own code -- the dword its
 * OBJ_OFF_FIELD_C0 pointer points at -- into one of about a dozen result
 * codes, through a 39-entry index table at 0x00406688 and a 17-arm jump table
 * at 0x00406644.
 *
 * Thirteen of the arms are a constant. Four are the flag-base wrappers, one
 * per army colour, and one compares `owner`'s health against half its maximum
 * and answers 8 or 0x20 -- so most of the answer is a property of the THING
 * and a few arms are a property of the OWNER.
 *
 * TWO ARMS ANSWER ZERO BY DIFFERENT ROUTES. Arm 12 is an explicit
 * `xor eax, eax` and the out-of-range default falls out of the entry `xor`
 * seventy bytes earlier. Same answer, and worth reproducing as two arms
 * rather than one because the table really does distinguish them. */
#define ADDR_THING_CODE          0x00406550u  /* int32(who, owner) */
#define AM2_FLAG_BASE_RANGE      100
#define AM2_NOT_AT_FLAG_BASE     0x80
#define ADDR_STR_FLAGBASE_GREEN  0x00473F0Cu  /* "gflagbase"  */
#define ADDR_STR_FLAGBASE_TAN    0x00473F00u  /* "tflagbase"  */
#define ADDR_STR_FLAGBASE_BLUE   0x00473EF4u  /* "bflagbase"  */
#define ADDR_STR_FLAGBASE_GREY   0x00473EE8u  /* "grflagbase" */
#define ADDR_APPROX_DIST_XY 0x0042DE20u  /* int32_t(dx, dy) -- the same maths */
#define ADDR_ANGLE_DELTA    0x0042DD90u  /* int32_t(from, to), 8-bit headings */
#define ADDR_ROUND_TO_8     0x0042DFB0u  /* int32_t(value, bits) */
/* 0x00428E40, six call sites in five functions -- the movers. Where an object
 * gets to after one frame at a given heading and speed, without moving it.
 * Reconstructed. */
/* 0x0040DA70, three callers, all in 0x0044AFB0. MoveStepPoint with the speed
 * taken from the object's CURRENT ANIMATION rather than passed in.
 * Reconstructed. */
#define ADDR_ANIM_STEP_POINT 0x0040DA70u /* int32(obj, head, pose,
                                          *       AM2_Point *, fast) */
#define AM2_ANIM_FAST_STEP   8.0f  /* units this frame when `fast` is set */
#define ADDR_MOVE_STEP_POINT 0x00428E40u /* int32(obj,head,turn,speed,unused,
                                          *       flip, AM2_Point *) */
#define AM2_MOVE_MIN_STEP   2.0f  /* the step is clamped AWAY from zero */
/* 0x00449F40, three callers. Wobble a facing by rand() % 5 - 2, and keep the
 * wobble only if it rounds into the same direction bucket as the original. */
#define ADDR_JITTER_FACING  0x00449F40u  /* uint8_t(void *obj, uint8_t) */
#define AM2_JITTER_SPREAD   5            /* rand() % this, then minus 2 */
#define AM2_JITTER_BIAS     2
#define AM2_JITTER_BITS_3   3            /* the bucket width for kind 3 */
#define AM2_JITTER_BITS_OTHER 0x20       /* and for everything else -- see
                                          * JitterFacing on what that does */
#define ADDR_MAKE_POINT     0x0042E1A0u  /* uint32_t(x, y) -> packed AM2_Point */
#define ADDR_FIND_SLOT      0x004277A0u  /* int32_t(uint32_t uid, int32_t *insert_at) */
#define ADDR_LOOKUP_BY_UID  0x00427820u  /* void *(uint32_t uid) */

/* The global object registry: an array of 12-byte {uid, obj, serial} records
 * kept sorted by uid. See src/game/objtable.c. */
#define ADDR_OBJ_TABLE      0x00514F0Cu  /* AM2_ObjEntry * */
#define ADDR_OBJ_COUNT      0x00514F04u  /* int32_t */
#define ADDR_OBJ_CAPACITY   0x00514F00u  /* int32_t */

#define ADDR_ADD_TO_ITEM_LIST 0x00429740u /* uint32_t(AM2_Object*, uint32_t) */
/* item.cpp accessors. UID_ARMY is `uid >> 29`, the owner half of a uid -- the
 * same field AM2_UID_OWNER_SHIFT names, and what 0x0042A930 logs as "army".
 * UID_ON_WIRE returns its argument and is applied to uids crossing a comm
 * message; see src/game/item.h for why it is kept. */
/* item.cpp's savegame section. The saver brackets a FirstItem/NextItem walk
 * with tags; the loader checks the opening tag and then reads item markers
 * until one is not AM2_SAVE_ITEM_MARK. docs/savetags.tsv already had the
 * loader's site -- item.cpp line 1192, tag 0x06660007 -- from the string it
 * passes CheckSaveTag. */
#define ADDR_STR_ITEM_CPP    0x00485C58u  /* "C:\\ArmyMen2\\source\\item.cpp" */
#define ADDR_SAVE_ITEMS     0x00428950u  /* int32_t(FILE *) */
#define ADDR_LOAD_ITEMS     0x00428BB0u  /* int32_t(FILE *) */
/* The nine functions ADDR_SAVE_ONE_ITEM dispatches to -- a header and one per
 * object type -- named by the arm that reaches them, none naming itself.
 *
 * THE LOAD SIDE CORROBORATES THE MAPPING, and structurally rather than by
 * agreement of two readings: every loader ADDR_LOAD_ONE_ITEM calls sits
 * IMMEDIATELY AFTER its saver in the image.
 *
 *   type 1  save 0x00433D20   load 0x00433D60
 *   type 2  save 0x00447130   load 0x004471D0
 *   type 3  save 0x0045A070   load 0x0045A120
 *   type 4  save 0x0045EF00   load 0x0045EF50
 *   type 5  save 0x0043B800   load 0x0043B870
 *   type 6  save 0x00422750   load 0x00422780
 *   type 7  save 0x004354F0   load 0x00435500
 *   type 8  save 0x0043CB30   load 0x0043CB60
 *
 * Eight pairs, each adjacent, in the same order in both dispatch tables. The
 * source wrote them together and the linker kept them together.
 *
 * TYPE 7 SAVES NOTHING. Its arm calls ADDR_RETURN_ONE -- a shared `return 1`
 * -- rather than being special-cased out of the switch, which is why there
 * are eight arms and not seven. */
/* How many bytes of an object the header is, and it is a GLOBAL rather than a
 * constant: 0x00427640 sets it to 0x68 and nothing else writes it. Every saver
 * and loader in the family below reads it, which is what makes the two sides
 * agree by construction rather than by both spelling the same literal.
 *
 * The type-specific record starts at OBJ_OFF_FIELD_94, so 0x68..0x93 is saved
 * by neither half. Stated rather than smoothed over; what lives there is not
 * established. */
#define ADDR_ITEM_HEADER_SIZE 0x0051307Cu  /* int32_t, 0x68 */
#define ADDR_SET_ITEM_HEADER_SIZE 0x00427640u /* void(void) */
/* The per-type records, all at OBJ_OFF_FIELD_94 and each saved whole. The
 * sizes are literals in their own savers -- there is no table -- so this is
 * where they are collected. */
#define AM2_TYPE1_RECORD_SIZE 0x2Cu
#define AM2_TYPE6_RECORD_SIZE 0x28u
#define AM2_TYPE8_RECORD_SIZE 0x4CCu
#define AM2_TYPE2_RECORD_SIZE 0x53Cu
#define AM2_TYPE3_RECORD_SIZE 0x548u
/* Three fields of the type 6 record that LoadType6 hands to ADDR_SPAWN_AT
 * before it copies the record in -- so the object is MADE from them and then
 * overwritten by them. Named for the argument each becomes. */
/* THE RECORD IS FULLY ACCOUNTED FOR NOW, and CreateExplosion is what accounts
 * for it: the six fields below are the six the constructor writes, and each is
 * OBJ_OFF_FIELD_94 plus the BLAST_OFF_ offset StepType6 reads it back at.
 * 0x00 + 0x04 + 0x10 of rect + 0x04 + 0x04 + 0x04 + 0x04 is 0x28, which is
 * AM2_TYPE6_RECORD_SIZE exactly. */
#define TYPE6_REC_OFF_KIND    0x00u   /* -> OBJ_OFF_FIELD_94 */
#define TYPE6_REC_OFF_EXTRA   0x04u   /* -> BLAST_OFF_DAMAGE */
#define TYPE6_REC_OFF_RECT    0x08u   /* -> BLAST_OFF_RECT */
#define TYPE6_REC_OFF_DUE_MS  0x18u   /* -> BLAST_OFF_DUE_MS */
#define TYPE6_REC_OFF_SOUND   0x1Cu   /* -> BLAST_OFF_SOUND_PENDING */
#define TYPE6_REC_OFF_MODE    0x20u   /* -> BLAST_OFF_MODE */
#define TYPE6_REC_OFF_UID     0x24u   /* -> BLAST_OFF_SOURCE_UID */
#define AM2_ITEM_HEADER_TAG   0x6660000u
#define ADDR_SAVE_ITEM_HEADER 0x00428730u  /* int32_t(FILE *, obj) */
#define ADDR_SAVE_TYPE1       0x00433D20u
#define ADDR_SAVE_TYPE2       0x00447130u
#define ADDR_SAVE_TYPE3       0x0045A070u
#define ADDR_SAVE_TYPE4       0x0045EF00u  /* SaveType1 plus three tags */
#define ADDR_SAVE_TYPE5       0x0043B800u
#define ADDR_SAVE_TYPE6       0x00422750u
#define ADDR_SAVE_TYPE8       0x0043CB30u
#define ADDR_SAVE_ONE_ITEM  0x00428870u  /* int32_t(FILE *, void *obj) */
/* TWO OF THE SAVERS NORMALISE A POINTER BEFORE WRITING IT. ADDR_SAVE_TYPE2
 * turns SAVED_OFF_TABLE_REC2 from a pointer into
 * `(p - ADDR_OBJ_TABLE_RECORDS) >> 8` -- an index into the four 256-byte
 * records there -- and ADDR_SAVE_TYPE3 does the same with SAVED_OFF_TABLE_REC3.
 *
 * AND BOTH PUT THE POINTER BACK. This said "neither converts back, so a saved
 * object is left holding an index", which was written from the heads of the
 * two functions; each restores the saved original as its last act before
 * returning. Reading half a function and writing down what it does is how that
 * happens, and the file is the same either way -- the correction is about the
 * OBJECT, not the format.
 *
 * The format half stands: some types resolve their pointers and only 1, 4 and
 * 5 write a raw one.
 *
 * SAVE_TYPE3 ALSO TAKES ITS FOOTPRINT OUT OF THE MAP AND PUTS IT BACK, around
 * the same write -- ADDR_OBJ_CLEAR_FOOTPRINT first and ADDR_OBJ_SET_FOOTPRINT
 * last. That is what a save function was doing calling a footprint clear, a
 * question left open when SaveType4 landed: the saved record is the object
 * with its footprint lifted.
 *
 * The load side's own nine, adjacent to their savers -- see the table above.
 * ADDR_LOAD_ITEM_HEADER fills a 0x94-byte object header on the caller's stack
 * and each loader turns that into a real object, RETURNING it. */
#define ADDR_LOAD_ITEM_HEADER 0x004289B0u  /* int32_t(FILE *, void *hdr) */
#define ADDR_LOAD_TYPE1       0x00433D60u
/* READ FAR ENOUGH TO BE WORTH WRITING DOWN AND NOT RECONSTRUCTED. It is the
 * hardest of the nine loaders and the notes below are so the next attempt
 * starts where this one stopped rather than at the disassembly again.
 *
 * WHAT IS SETTLED:
 *
 *   The signature is (FILE *, hdr, int32) -- ARG1 at [esp+0x600] and ARG3 at
 *   [esp+0x608] with a frame depth of 1532, which is 12 bytes of SEH record
 *   plus 0x5E0 of locals plus four saved registers.
 *
 *   It freads a 0x53C-byte record and then a 4-byte tag, and the record is
 *   the whole of the trooper's saved state: memcpy'd to obj+0x94 as 0x14F
 *   dwords, which is 0x53C exactly.
 *
 *   ARG3 SELECTS WHICH CreateTrooper CALL, and the two differ in ONE
 *   argument: with ARG3 set the uid goes in as 0 and the pair
 *   UidRemapAdd(hdr->uid, made->uid) / hdr->uid = made->uid follows, which is
 *   the remap LoadType1 does not need; with it clear the header's own uid is
 *   passed straight through. Ten arguments either way.
 *
 *   THE HEALTH IS RESCALED THROUGH THE RANK TABLE, in x87: the fraction
 *   health/maxHealth is taken BEFORE SetMaxHealth is given
 *   ADDR_RANK_RECORDS[rank] + RANK_REC_OFF_MAX_HEALTH -- the base arithmetic
 *   is `[rank*28 + 0x473DD4]`, which is that field exactly -- and the result
 *   is multiplied back, _ftol'd and Clamped to 1..new. Same shape as
 *   LoadType8's roach rescale, different table.
 *
 *   THE C++ LOCAL WITH A CONSTRUCTOR IS INSIDE THE RECORD. InitPtrList runs
 *   on rec+0x10 before the fread, which then overwrites it, and the matching
 *   destructor runs on the same address AFTER the three fields have been
 *   explicitly zeroed -- so the constructor is dead and the destructor frees
 *   nothing. That is what the MSVC SEH prologue is there for, and CLAUDE.md's
 *   standing decision not to reproduce such a frame applies.
 *
 *   Its {capacity, count, items} at rec+0x10 supplies the COUNT for a loop
 *   that freads one uid at a time into a stack slot and PtrListPushes each
 *   into the OBJECT's own list at +0xA4 -- but only when ARG3 is clear.
 *
 * WHAT IS NOT: the exact identity of four small stack locals around
 * SoldierKindForWeapon and the Type2Action arms, where a first pass produced
 * two readings four bytes apart. That is the class of mistake this file has
 * caught twice today by reading siblings, and guessing it at the end of a
 * long session is how the third one would get written instead of found. */
#define ADDR_LOAD_TYPE2       0x004471D0u  /* int32(fp, hdr, renumber);
                                            * reconstructed */
/* THE THIRD ARGUMENT IS "RENUMBER", and the two arms are what say so. With it
 * clear the trooper is rebuilt with the uid the file gave it; with it set the
 * uid argument is 0, so CreateTrooper allocates a fresh one, the pair goes to
 * ADDR_UID_REMAP_ADD, the header takes the new value, OBJ_FLAG_NEEDS_REMAP
 * goes on, and five record fields that named the old uid are zeroed. That
 * global's own note says "both call sites build a replacement object and pass
 * (old->uid, new->uid)" -- this is one of the two, so the reading is confirmed
 * from the other end.
 *
 * ITS FOUR SMALL STACK LOCALS ARE SETTLED, which is what the deferral was
 * about. One is a four-byte read AFTER the record -- the weapon code, handed
 * to SoldierKindForWeapon in the default arm of the kind switch. One is the
 * per-uid buffer the inventory loop freads into. The last two are a health
 * PAIR saved across ADDR_TYPE2_ACTION_A and put back, and they looked like
 * three because an `add esp, 4` between the writes and the reads moves the
 * displacement by two words. */
#define TYPE2_REC_OFF_LIST      0x010u  /* {capacity, count, items} */
#define TYPE2_REC_OFF_COUNT     0x014u  /* how many uids follow the record */
#define TYPE2_REC_OFF_SCRIPT    0x020u  /* -> OBJ_OFF_SCRIPT_STATE */
#define TYPE2_REC_OFF_CLEAR_30  0x030u  /* zeroed when renumbering */
#define TYPE2_REC_OFF_CLEAR_38  0x038u
#define TYPE2_REC_OFF_SLOT      0x498u  /* a comm slot, as in types 3 and 8 */
#define TYPE2_REC_OFF_POSE      0x4A4u  /* -> SetUnitPose */
#define TYPE2_REC_OFF_CLEAR_4D0 0x4D0u
#define TYPE2_REC_OFF_CLEAR_4D8 0x4D8u
#define TYPE2_REC_OFF_CLEAR_4DC 0x4DCu
/* The three soldier kinds LoadType2 gives their own arm, and everything else
 * takes the weapon code. What each kind IS is not established; the numbers are
 * OBJ_OFF_SOLDIER_KIND's and the arms are named for the callee they reach. */
#define AM2_SOLDIER_KIND_ACTION_C 6
#define AM2_SOLDIER_KIND_ACTION_A 7
#define AM2_SOLDIER_KIND_ACTION_B 8
/* The health above which the kind-7 arm saves and restores the pair around its
 * call, rather than letting the callee keep what it produced. */
#define AM2_TYPE2_KEEP_HEALTH_OVER 100
#define ADDR_LOAD_TYPE3       0x0045A120u  /* reconstructed */
/* The type-3 record's fields LoadType3 reads BEFORE the memcpy puts the whole
 * thing at OBJ_OFF_FIELD_94. Each is that offset plus 0x94, and every one of
 * those lands on a field this file already names -- +0x10 on OBJ_OFF_PTR_LIST,
 * +0x498 on VEHICLE_OFF_KIND, +0x4A0 on OBJ_OFF_TABLE_REC_SLOT and +0x4A4 on
 * VEHICLE_OFF_PTR_LIST. So the record is the object's own tail written out,
 * and the loader's job is to turn its two SAVED COUNTS and one SAVED SLOT back
 * into pointers, which is the same index-for-a-pointer trade LoadType5 makes
 * with the missile defs. */
#define TYPE3_REC_OFF_LIST         0x10u   /* {capacity, count, items} */
#define TYPE3_REC_OFF_LIST_COUNT   0x14u   /* how many uids follow the record */
#define TYPE3_REC_OFF_ARG5         0x494u  /* CreateVehicle's fifth argument */
#define TYPE3_REC_OFF_KIND         0x498u  /* -> VEHICLE_OFF_KIND */
#define TYPE3_REC_OFF_SLOT         0x4A0u  /* a comm slot, not a pointer */
#define TYPE3_REC_OFF_PTR_LIST     0x4A4u  /* -> VEHICLE_OFF_PTR_LIST */
#define TYPE3_REC_OFF_PTR_COUNT    0x4A8u
/* What LoadType3 forces into OBJ_OFF_RANK after copying the record over it, so
 * the file's own value is discarded. Nothing read so far says what a rank of 5
 * means to a vehicle. */
#define AM2_TYPE3_LOAD_RANK        5
/* LoadType3's last write is `flags &= ~0x200000`, which went in as
 * OBJ_FLAG_BIT21 with "what it gates is unestablished" -- and
 * tools/checkoffsets.py refused it as a second name on OBJ_FLAG_FOOTPRINT_ON.
 * It is the same clear LoadType8 makes for the same reason: the loaded object
 * lays its footprint down again rather than trusting a flag the file supplied.
 * The sibling predicted it and the ratchet is what proved it. */
/* 0x0045EBF0, and it is ADDR_MISSILE_DEF_FIND's twin: a bsearch of the vehicle
 * definition table with ADDR_COMPARE_DWORD, so the id is the record's first
 * dword. The pointer and the count are two adjacent globals the parser fills.
 * Returns null for a type with no entry, which is the case 0x0045B090 logs as
 * "Vehicle aai entry not found for type %d". */
#define ADDR_VEHICLE_DEF_FIND      0x0045EBF0u /* rec *(int32 kind) */
#define ADDR_VEHICLE_DEFS          0x00662024u /* rec * */
#define ADDR_VEHICLE_DEF_COUNT     0x00662028u /* int32_t */
#define AM2_VEHICLE_DEF_BYTES      0x24
/* The rest of the record, named by the object field each lands in --
 * CreateVehicle copies eight of them straight across and that is the only
 * thing in the image that reads any of them. */
#define VEHDEF_OFF_SEATS           0x04u  /* -> VEHICLE_OFF_SEATS */
#define VEHDEF_OFF_FIELD_08        0x08u  /* -> obj +0x558 */
#define VEHDEF_OFF_FIELD_0C        0x0Cu  /* -> obj +0x55C */
#define VEHDEF_OFF_FIELD_10        0x10u  /* -> obj +0x560 */
#define VEHDEF_OFF_FIELD_14        0x14u  /* -> obj +0x564 */
#define VEHDEF_OFF_HEALTH          0x18u  /* what SetMaxHealth is given */
#define VEHDEF_OFF_FIELD_1C        0x1Cu  /* -> OBJ_OFF_FIELD_568 */
#define VEHDEF_OFF_ARMOUR          0x20u  /* -> VEHICLE_OFF_ARMOUR */
/* The three per-KIND tables CreateVehicle indexes, all six entries long. The
 * height is an int32 read as a BYTE -- 24, 32, 24, 24, 24, 33 -- and the two
 * animation tables are .bss, filled when the .ani files load. A vehicle whose
 * TURRET table entry is non-empty gets a SECOND row; that is the whole of what
 * decides a vehicle's row count. */
#define ADDR_VEHICLE_HEIGHT_BY_KIND 0x0048BDF8u /* int32[6], read as a byte */
#define ADDR_VEHICLE_BOX            0x0048BE10u /* AM2_Rect (-48,-48,48,48) */
#define ADDR_VEHICLE_ROW_SPEC       0x0048BE20u /* one AM2_ROW_SPEC_BYTES spec */
#define AM2_VEHICLE_KIND_COUNT      6
/* What weapon kind each vehicle kind is given, from the six-arm jump table at
 * 0x0045B450: kinds 3 and 4 get none at all and share the arm that skips the
 * whole CreateWeapon block. */
#define AM2_VEHICLE_WEAPON_KIND0    7
#define AM2_VEHICLE_WEAPON_KIND1    6
#define AM2_VEHICLE_WEAPON_KIND2    8
#define AM2_VEHICLE_WEAPON_KIND5    0x1D
#define ADDR_LOAD_TYPE4       0x0045EF50u
#define ADDR_LOAD_TYPE5       0x0043B870u
#define ADDR_LOAD_TYPE6       0x00422780u
#define ADDR_LOAD_TYPE7       0x00435500u
#define ADDR_LOAD_TYPE8       0x0043CB60u  /* reconstructed */
/* 0x0043CDD0. What LoadType8 builds its roach with. Eight arguments, the same
 * shape ADDR_CREATE_ITEM has for LoadType1. Name from that one use. */
#define ADDR_CREATE_ROACH     0x0043CDD0u  /* reconstructed */
/* The allocation, and the two literals in its body. The stagger is added to
 * ADDR_GAME_CLOCK_MS, which is milliseconds, so two roaches made in one frame
 * come due up to half a second apart. AM2_KIND7_FUSE_MS is 0x3E8 as well and
 * is a different thing in a different struct -- that one is a deadline in an
 * object, this one is a plain field in a ROW. */
#define AM2_ROACH_BYTES         0x560u
#define AM2_ROACH_STAGGER_MS    0x1F4   /* rand() % this, added to the clock */
/* What a constructor writes into ROW_OFF_FIELD_26. It was
 * AM2_ROACH_ROW_FIELD26 until LoadType5 wrote the same value into the same
 * field of a MISSILE's row -- one constant in one struct, so one name. */
#define AM2_ROW_FIELD26_INIT    0x3E8   /* int16, into ROW_OFF_FIELD_26 */
/* Bit 8 of a ROW's flags word, which CreateRoach sets with `or dh, 1`. Only
 * the write is identified: the sixteen byte-wide readers of bit 8 in the
 * image test whatever happens to be in `ah`, and pinning which of them hold a
 * row's flags is not done. Named for the bit, not for a meaning it has not
 * been shown to have -- the same standing as OBJ_FLAG_BIT8 beside it. */
#define ROW_FLAG_BIT8            0x100u
/* THE EIGHT ROACH CONSTANTS ARE NAMED BY THE GAME'S OWN DATA. DefGameParse
 * stores the ROACH_* keywords into the eight consecutive dwords at
 * ADDR_GAME_CONSTANTS, and aai/game.aai lists them in that order with the
 * image's own compiled-in defaults -- HEIGHT 32, HEALTH 60, ARMOR 2,
 * DAMAGE 16, FORVEL 130, REVVEL 80, FORACC 100, REVACC 80, eight for eight.
 * So the block needs no guessing, and the two entries anything here reads
 * are named. The other six get no name until something reads them: an
 * unused name is a second name waiting to happen, which is what the two the
 * alias ratchet rejected turned out to be.
 *
 * 0x00487BAC went in as ADDR_ROACH_HEALTH_SCALE, from CreateRoach handing it
 * to SetMaxHealth beside a comment about RANK_REC_OFF_SCALE. It is not a
 * scale: the file calls it ROACH_HEALTH and the value is 60, which is the
 * health itself. The same dword is read as a word straight into the
 * max-health field two instructions earlier, which is the second thing
 * saying so. ADDR_ROACH_ARMOUR at 0x00487BB0 was already right, read off the
 * subtraction every roach hit goes through -- the data spells it ARMOR and
 * the existing spelling stays, because renaming a correct name to match a
 * data file is churn.
 *
 * ADDR_GAME_CONSTANTS is index 0 of the block AND the ROACH_HEIGHT dword, so
 * CreateRoach reads its height through the base rather than through a second
 * name for the same address. */
#define ADDR_ROACH_HEALTH       0x00487BACu  /* int32_t; block index 1 */
/* Index 3, and it gets a name now because RoachBite reads it -- which is the
 * rule above working rather than an exception to it. Five of the eight are
 * still nameless. */
#define ADDR_ROACH_DAMAGE       0x00487BB4u  /* int32_t; block index 3 */
/* Four more of the eight, and they get names now because RoachStepAllowed
 * reads them -- the rule above working rather than an exception to it. It uses
 * them in two symmetric arms: FORVEL and REVVEL cap the speed, FORACC and
 * REVACC are what a frame adds to or takes off it. The reverse cap is applied
 * NEGATED, which is what says the two are one signed speed and not two. Only
 * ARMOR is still nameless of the eight. */
#define ADDR_ROACH_FORVEL       0x00487BB8u  /* int32_t; block index 4 */
#define ADDR_ROACH_REVVEL       0x00487BBCu  /* int32_t; block index 5 */
#define ADDR_ROACH_FORACC       0x00487BC0u  /* int32_t; block index 6 */
#define ADDR_ROACH_REVACC       0x00487BC4u  /* int32_t; block index 7 */
/* 0x0043D0F0, four callers. May the roach step the way this control record
 * says? Work out the speed, the turn and the direction it would end up facing,
 * and answer whether the mask weight there is at least what it is here.
 * Reconstructed. */
#define ADDR_ROACH_STEP_ALLOWED 0x0043D0F0u  /* int32(obj, ctrl, int32 *turn) */
/* THE SECOND ARGUMENT IS NOT A RECORD OF ITS OWN AND THESE NAMES CLAIMED IT
 * WAS. StepType8 passes `obj + OBJ_OFF_FIELD_540`, so the "control record" is
 * a window into the object and every offset here is an object field twelve
 * hundred bytes in: +0x00 is OBJ_OFF_FIELD_540, +0x14 is OBJ_OFF_DEATH_STATE
 * and +0x18 is the dword after it. Named ROACHCTL_OFF_* one batch ago, which
 * is a second name for fields that already had one -- my own mistake, of
 * exactly the kind this file catalogues.
 *
 * ROACHCTL_OFF_STOP was the worst of the three. It said "1 means do not move
 * at all", which is a guess off one branch; 1 is what StepType8 writes while
 * the roach is ALIVE and what DamageRoach replaces with 5 or 6 on death, so
 * the field is a state and 1 is the living value. What the branch does with it
 * is skip the speed arms; WHY is not established and the name no longer
 * pretends otherwise.
 *
 * The pointer form is kept because the original takes two pointers and four
 * callers pass them; only the meaning-claims are withdrawn. */
#define ROACHSTEP_OFF_FACING    0x00u  /* = OBJ_OFF_FIELD_540 */
#define ROACHSTEP_OFF_STATE     0x14u  /* = OBJ_OFF_DEATH_STATE; 1 is alive */
#define ROACHSTEP_OFF_FLAG18    0x18u  /* the dword after it; picks the arm */
/* 0x0043D750's two stuck fields, and the second is a UNION rather than a new
 * field. A roach that asks to move into a cell no better than the one it is in
 * is refused: its speed is zeroed, STUCK_COUNT rises by one and STUCK_SINCE is
 * stamped once with the clock. A later frame MULTIPLIES the speed by the count
 * before clamping, so the longer it is stuck the harder it shoves; a move that
 * succeeds clears both.
 *
 * STUCK_SINCE IS OBJ_OFF_TABLE_REC_SLOT'S OFFSET, which is a record pointer on
 * the types that have one. Overloading by type, as at 0x52C and 0x538 already
 * -- a roach never carries the table pair, so the slot is free for this. Named
 * in its own right rather than reusing the pointer's name, since a timestamp
 * read through a name that says "record pointer" is how a wrong dereference
 * gets written. */
#define OBJ_OFF_STUCK_COUNT     0xD8u   /* int32_t, refused moves in a row */
#define OBJ_OFF_STUCK_SINCE     0x534u  /* uint32_t ms; = OBJ_OFF_TABLE_REC_SLOT */
#define AM2_ROACH_TURN_HOLD_MS  0x64   /* a turn within this of the last is
                                        * refused */
#define AM2_ROACH_WEIGHT_FLOOR  0x1E   /* the here-weight is clamped up to it */
#define ADDR_ROACH_BOX          0x00487BC8u  /* AM2_Rect */
/* The rect 16 bytes later, which CreateRoach does not touch and RoachBite
 * does: (-24, -24, 24, 24), a 48x48 box centred on the point the roach steps
 * to. Named once something read it, which is why it was left alone before. */
#define ADDR_ROACH_BITE_BOX     0x00487BD8u  /* AM2_Rect */
/* 0x0043D330, one caller -- the roach's per-frame step, in its state 4. Step
 * AM2_ROACH_REACH along the facing, play a sound there, and damage every
 * object in ADDR_ROACH_BITE_BOX around that point which is not on the roach's
 * side. The name is DESCRIPTIVE: nothing in the image names this function. */
#define ADDR_ROACH_BITE         0x0043D330u  /* void(void *roach) */
#define ADDR_ROACH_REACH        0x0046FAA8u  /* float, 24.0 */
#define AM2_ROACH_BITE_SOUND    0x31
#define AM2_ROACH_DAMAGE_KIND   5
/* One ADDR_BUILD_ROW_SET spec -- x, y, w, h = -48, -48, 96, 96 -- and the
 * frame SetAnimFrame starts the roach on. The dword at 0x00487BF8 is 81 as
 * well and nothing here reads it, so it stays unnamed. */
#define ADDR_ROACH_ROW_SPEC     0x00487BE8u  /* int32_t[4] */
#define ADDR_ROACH_START_FRAME  0x00487BFCu  /* int16_t */
/* The object's OWN pointer list, three dwords, which LoadType8 clears so a
 * loaded roach starts with an empty one. */
#define OBJ_OFF_PTR_LIST      0xA4u
/* 0x004294C0, FIFTEEN callers -- the most-called thing still original in its
 * band. Recompute an object's tile from its position and, if anything moved,
 * put it back on the map and re-apply its height.
 *
 * The early exit needs all three to be true: the position equal to
 * OBJ_OFF_PREV_POS, the tile equal to what it was, and `force` zero. So the
 * third argument overrides both tests, which is what a caller that has
 * changed something else about the object wants.
 *
 * ADDR_OBJ_TILE_CHANGED_HOOK runs whenever OBJ_OFF_FLAGS bit 3 is CLEAR, and
 * it runs before the early exit -- so it is not part of the "something moved"
 * path however much its position in the body suggests it. */
#define ADDR_OBJ_TILE_CHANGED    0x004294C0u /* void(obj, int32 h, int32 force) */
#define ADDR_OBJ_TILE_HOOK       0x00437860u /* void(obj), when bit 3 is clear */
#define ADDR_OBJ_REMAP           0x00429D00u /* void(obj, desc, int32 force) */
/* READ IN FULL, not yet written. 544 bytes, three callers -- both deploys and
 * the stepper -- and every callee is already ours: ListUnlink, ListPushFront,
 * ItemPreDestroy, PointsEqual. Two `ret`s.
 *
 *   if (!force && PointsEqual(pos, OBJ_OFF_PREV_POS)) return;
 *   rebuild OBJ_OFF_HIT_RECT from the position plus the four
 *     OBJ_OFF_BOX_OFFSETS -- left, top, right, bottom, in that order,
 *     confirmed by the stores to +0x30/+0x34/+0x38/+0x3C;
 *   if (!(flags & 1)) return;
 *   shift each corner right by 8 -- the 256-unit tile -- clamp against the
 *     descriptor's grid at [desc+0] and [desc+4] with the shift at [desc+8],
 *     and bail if the box falls outside;
 *   walk the covered cells, and for each OBJ_OFF_CELL_ENTRIES entry (0x10
 *     bytes, the cell index at +0xC, -1 meaning unlinked): unlink from the old
 *     list when the index differs, store the new one, ListPushFront onto
 *     [desc+0xC] + index*4;
 *   then release every remaining entry up to OBJ_OFF_CELL_COUNT, unlinking
 *     each and writing -1 back.
 *
 * Two things found while reading it, both worth having on record.
 *
 * OBJ_OFF_DEPTH_LAYER (0x26) and OBJ_OFF_DEPTH_SLOPE (0x28) sit INSIDE
 * OBJ_OFF_BOX_OFFSETS' sixteen bytes, and both readings are right: the depth
 * pair is used only on ROW records (maprow.cpp's comparator, item.cpp writing
 * `rows + ...`) and the box only on OBJECTS. Same offsets, different structs --
 * so the depth pair is MISFILED under an OBJ_OFF_ prefix. That is the exact
 * trap that cost a defect today, where OBJ_OFF_OWNER's prefix was trusted and
 * the field belonged to another struct.
 *
 * And the offset macros DO have a ratchet -- tools/checkoffsets.py, baseline
 * 14 -- which is worth saying because I wrote the opposite here and it failed
 * the build within the minute. It refused OBJ_OFF_ENTRY_COUNT because 0x8C is
 * already OBJ_OFF_CELL_COUNT, which is the very field I was naming; and its
 * backlog already lists 0x8C with three names and 0x90 with two
 * (OBJ_OFF_CELL_ENTRIES beside OBJ_OFF_ALLOC_PTR). Use the existing ones. */

#define OBJ_FLAG_NO_TILE_HOOK    0x08u
/* 0x004278E0, four callers. Give an object a height and push it into the
 * depth sort. A ZERO height means "take the tile's own", read through
 * ADDR_TILE_ATTRS -- so 0 is not a height here, it is a request.
 *
 * Four arms over `type - 1` and only three distinct: types 1 and 4 share the
 * table slot for 0x00433C20 and do not touch the row at all, type 2 writes the
 * byte and the first row's depth layer, type 3 writes the SECOND row's as well
 * when there is one, and everything else checks the row count first.
 *
 * TYPE 2 IS THE ONE THAT DOES NOT CHECK. The default arm tests
 * OBJ_OFF_ROW_COUNT before touching a row and the type-2 arm jumps straight
 * into the same tail, so an object of type 2 with no rows writes through a
 * null. Reproduced; every type 2 in a mission has one. */
#define ADDR_APPLY_OBJ_HEIGHT 0x004278E0u  /* void(obj, int32_t height) */
#define ADDR_APPLY_HEIGHT_1_4 0x00433C20u  /* void(obj, int32_t), 3 callers */
#define AM2_ITEM_HEADER_BYTES 0x94u
/* It RETURNS THE OBJECT, not a flag -- this said void. */
#define ADDR_LOAD_ONE_ITEM  0x004289E0u  /* void *(FILE *, int32_t) */
#define ADDR_ITEMS_RESET    0x00429450u  /* void(void) */

#define AM2_SAVE_TAG_ITEMS  0x06660007u  /* opens the section */
#define AM2_SAVE_TAG_END    0x06660001u  /* closes a list section */

#define ADDR_UID_ARMY        0x0042A7A0u  /* uint32_t(uint32_t uid) */
#define ADDR_UID_ON_WIRE     0x0042A7B0u  /* uint32_t(uint32_t uid) */
/* 0x0044C370, ten callers, and it names itself: "Send TrooperSetWeapon
 * message, trooper: %08x, weapon:%08x". */
#define ADDR_SEND_TROOPER_WEAPON 0x0044C370u  /* void(obj, uid, int32) */
/* 0x0042AA10, two callers, and the SHORTEST message in the family: eight
 * bytes, the length and the kind and one uid, no payload at all.
 *
 * What kind 0x10 means comes from both callers rather than from a receiver.
 * 0x00429320 is the shared tail of every per-type destroy handler -- it
 * returns immediately if `flags & 4` is already set and sets it on the way
 * out, which is an idempotent "this is now gone" -- and 0x00428DA0 dispatches
 * on the object TYPE to one of those handlers and then sends this. So the
 * message is "that object was destroyed"; there is no receiver read yet and
 * the name should be revisited if one turns up saying otherwise. */
#define ADDR_SEND_OBJ_DESTROYED  0x0042AA10u  /* void(const void *obj) */
/* 0x0042A9A0, and it names itself: "itemGoneMessageSend uid %x item_type %d".
 * The same eight-byte shape as the one above with a different kind, and one
 * extra gate -- the object's TYPE must be 1..4, so the message is only sent
 * for the four kinds that are items. */
#define ADDR_ITEM_GONE_SEND      0x0042A9A0u  /* void(const void *obj) */
/* The six message codes ADDR_ARMY_MSG_FILTER handles, and the six receivers
 * it hands them to. FIVE OF THE SIX NAME THEMSELVES in their own trace lines,
 * which is what identified codes 0x0F, 0x11 and 0x12 -- they were unnamed
 * until the handlers were read:
 *
 *   0x0E  "itemGoneMessageReceive %x"
 *   0x0F  "itemDeployMessageReceive: uid=%x, pos=(%d,%d), facing=%d"
 *   0x10  -- no string; the code was already AM2_MSG_OBJ_DESTROYED
 *   0x11  "Recieved Damage: item->uid %x, amount: %d, army %d, dir: %d"
 *   0x12  "==> Receive Item Create [uid:%08x, item type: %d, subtype: %d"
 *   0x23  "Received Death Message: item->uid %x, army %d"
 *
 * The misspelling in 0x11's line is the game's own. Codes 0x13..0x22 fall to
 * the default and the filter answers 0 for them -- including
 * AM2_MSG_TROOPER_WEAPON at 0x22, which is handled somewhere else entirely. */
#define AM2_MSG_ITEM_GONE        0x0Eu
#define AM2_MSG_ITEM_DEPLOY      0x0Fu
#define AM2_MSG_DAMAGE           0x11u
#define AM2_MSG_ITEM_CREATE      0x12u
#define ADDR_RECV_ITEM_GONE      0x0042AEB0u  /* int32_t(msg) */
#define ADDR_RECV_ITEM_DEPLOY    0x0042AF30u
/* The SENDER for the same code, and it names itself the same way:
 * "itemDeployMessageSend: uid=%x, pos=(%d,%d), facing=%d". Sixteen bytes. */
#define AM2_MSG_ITEM_DEPLOY_LEN  0x10u
#define ADDR_STR_SEND_ITEM_DEPLOY 0x00485E3Cu
#define ADDR_RECV_OBJ_DESTROYED  0x0042AF00u
#define ADDR_RECV_DAMAGE         0x0042ADA0u
#define ADDR_RECV_ITEM_CREATE    0x0042AFA0u
#define ADDR_RECV_DEATH          0x0042AE50u
/* 0x00428450, and the receivers above are what named it: it calls FindSlot,
 * ADDR_TRIGGER_ITEM_DESTROYED and ADDR_OBJ_DEATH_CLEANUP, which between them
 * are what happens when an object dies rather than merely being removed. */
#define ADDR_OBJ_DIE             0x00428450u  /* void(obj, int32 kind, uint32 by) */
/* The two per-type death handlers ADDR_OBJ_DIE dispatches to. Named by the
 * type that reaches them -- 2 and 3, which RecvItemCreate's four arms
 * establish as trooper and vehicle -- because neither names itself and
 * neither carries a string. Types other than those two get no handler at all;
 * the common tail runs for every type. */
/* 0x00447E50, one caller. A trooper has died: take it off the map, drop
 * whatever OBJ_OFF_FIELD_5A4 gates, and hand the rest to the shared tail.
 *
 * THE THING IT SPAWNS BELONGS TO THE KILLER. `by` is a uid, and the army the
 * spawn gets is that object's OBJ_OFF_ARMY -- or 4, the neutral army, when
 * the uid no longer resolves. So a kill by someone who has since died is
 * credited to nobody rather than to the victim.
 *
 * Its middle argument is passed straight through to the tail at 0x00447EE0
 * and read nowhere here. */
#define ADDR_TROOPER_DIED        0x00447E50u  /* void(obj, int32, uint32 by) */
/* Three entries, {0x21, 0x22, 0x23}, indexed by ADDR_CLASSIFY_CODE74's answer
 * of 0, 1 or 2. Consecutive, so `0x21 + code` would do -- the original indexes
 * a table and so does the reconstruction. */
/* TrooperDiedTail's two helpers, both ABOVE the CRT line and so outside the
 * 1,239 -- they stay original behind seams. THE NAMES ARE OURS AND THEY COME
 * FROM THE ONE CALL SITE, which this file warns is how ADDR_SPRITE_DROP_NAMED
 * and AttachPalette went wrong; neither body is read. What is evidenced is
 * only that the first is handed the dying object and the second its position,
 * facing, OBJ_OFF_TABLE_REC_KIND and OBJ_OFF_HEIGHT_SET, and that the second
 * runs only for a small soldier whose death animation is 0x21. Read them
 * before relying on the names. */
#define ADDR_DIED_EFFECT_A       0x00461B20u  /* void(void *obj) */
#define ADDR_DIED_EFFECT_B       0x00461C30u  /* void(pt*, facing, kind, h) */
#define ADDR_DEATH_ANIM_BY_CODE  0x0047518Cu  /* int32_t[3] */
#define AM2_DEATH_ANIMS          3
/* Its ARGUMENT is the one TrooperDied passes straight through and never reads:
 * a death KIND of 1..5, dispatched through a five-entry table at 0x004480C8.
 * READ THAT TABLE AS DATA -- it orders the arms 1 -> 0x447FAF, 2 -> 0x447F4E,
 * 3 -> 0x447F12, 4 -> 0x447FEA, 5 -> 0x44800A, so KINDS 1 AND 3 ARE SWAPPED
 * against the layout and numbering the bodies top to bottom gets two of five
 * wrong. Kind 3's arm is also fallen into from the OBJ_OFF_FIELD_5A4 test
 * above the dispatch -- but that path CLEARS the field first and the table
 * path does not, so the two are not one condition. Reconstructed. */
#define ADDR_TROOPER_DIED_TAIL   0x00447EE0u  /* void(obj, int32 kind) */
#define AM2_SPAWN_KIND_95        0x95     /* what a dead trooper leaves */
#define AM2_ARMY_NEUTRAL         4
/* Read at three sites in the trooper band and passed straight through as an
 * argument, never tested. Same shape and same standing as
 * ADDR_SPAWN_EXTRA_6622BC. */
#define ADDR_SPAWN_EXTRA_6628D4  0x006628D4u /* int32_t */
/* 0x0045B630, one caller. ITS SECOND ARGUMENT IS UNUSED -- the body never
 * reads [esp+0xc] -- and the name kept it because the trooper twin beside it
 * does use one. Reproduced with the parameter present and ignored, since
 * dropping it would change the calling convention the one caller uses. */
#define ADDR_VEHICLE_DIED        0x0045B630u  /* void(obj, uint32 by) */
/* The armour DamageVehicle subtracts, and the reason a weak hit usually does
 * nothing at all. UNIT_OFF_INVENTORY and TROOPER_OFF_WEAPON_UID are at this
 * offset on the other two types -- overloading, as at 0x52C and 0x538. */
#define VEHICLE_OFF_ARMOUR       0x54Cu  /* int32_t */
#define VEHICLE_OFF_DEATH_STATE  0x580u  /* int32_t, set to 5 */
#define VEHICLE_OFF_DEAD         0x59Cu  /* int32_t, set to 1 */
/* The two it calls are ADDR_OBJ_CLEAR_FOOTPRINT and
 * ADDR_ITEM_PRE_DESTROY_ALIAS, both already named -- I gave each a second name
 * and the alias ratchet refused both in one run. The first is also SaveType3's
 * opening call, which is worth knowing: a SAVE function clearing a footprint
 * is not what the name suggests either function is for. */
#define ADDR_STR_RECV_ITEM_GONE  0x00485F20u
#define ADDR_STR_RECV_DEATH      0x00485EF0u
#define ADDR_STR_RECV_ITEM_DEPLOY 0x00485F3Cu
/* The deploy message's own layout, confirmed from BOTH ends: SendItemDeploy
 * packs these offsets and RecvItemDeploy reads the same ones. The byte at
 * 0x0D reaches DeployItem's `resurrect` parameter, which is what finally
 * names the second argument the sender was given. */
#define MSG_DEPLOY_OFF_UID       4u
#define MSG_DEPLOY_OFF_POS       8u    /* packed point; x at +8, y at +0x0A */
#define MSG_DEPLOY_OFF_FACING    0x0Cu /* uint8_t */
#define MSG_DEPLOY_OFF_RESURRECT 0x0Du /* uint8_t */
#define ADDR_STR_RECV_DAMAGE     0x00485EB0u
/* The damage message. It carries no direction: the receiver computes one with
 * ADDR_ANGLE_BETWEEN from the position at MSG_DAMAGE_OFF_POS to the victim's
 * own, masked to a byte.
 *
 * WHOSE POSITION THAT IS, CORRECTED. This said "the attacker's", which was an
 * inference from the receiver's use and is wrong. All FOUR senders --
 * 0x0042A880's callers at 0x00406219, 0x00428215, 0x004282BC and 0x0045AF4B
 * -- pass `victim + OBJ_OFF_POS`. It is the VICTIM's position as the SENDER
 * saw it.
 *
 * Which makes the angle one between two views of the same object, near zero
 * whenever the two sides agree. What it is FOR is therefore not established;
 * see OBJ_OFF_HIT_DIR, where the name rests on this call and now carries the
 * same caveat. */
#define MSG_DAMAGE_OFF_UID       4u
#define MSG_DAMAGE_OFF_ATTACKER  8u
#define MSG_DAMAGE_OFF_POS       0x0Cu /* two int16, the ATTACKER's position */
#define MSG_DAMAGE_OFF_AMOUNT    0x10u /* int16_t */
#define MSG_DAMAGE_OFF_KIND      0x12u /* uint8_t */
#define AM2_MSG_DAMAGE_LEN       0x14u
#define ADDR_STR_SEND_DAMAGE     0x00485DB0u
#define ADDR_STR_RECV_ITEM_CREATE 0x00485F78u
/* The item-create message and the four creators it dispatches to. The switch
 * is on MSG_CREATE_OFF_TYPE and the four arms map onto the object types this
 * project already knows: 1 item, 2 trooper, 3 vehicle, 4 weapon.
 *
 * Two of the four creators name themselves -- 0x0045F0C0 logs "CreateWeapon"
 * outright, and 0x0045B090 logs "Vehicle aai entry not found for type %d" --
 * which is what fixes the mapping and, with it, the other two. */
#define ADDR_CREATE_ITEM         0x00433980u  /* type 1 */
/* What CreateItem allocates and zeroes for one type-1 object. NOT
 * AM2_MISSILE_BYTES, which is 0xB8 -- the two were checked against each other
 * before this name went in. */
#define AM2_ITEM_BYTES           0xC0u
/* The "%s-%d" CreateItem names a composite's children with, and the 64-byte
 * stack buffer it formats into. The suffix is a SEPARATE string from
 * AM2_STR_UNIQUE_SUFFIX at the top of this file -- that one is "%s_%d", an
 * underscore, and it is ObjInitCommon's de-duplicator. Grepped before naming,
 * and the buffer likewise: AM2_SCRIPT_UNIQUE_BUF is also 0x40, and is also a
 * stack buffer a name is built in, but it is a different buffer in a different
 * function. Two names for two buffers, not two names for one number. */
#define AM2_STR_CHILD_SUFFIX     0x00487400u  /* "%s-%d" */
#define AM2_CHILD_NAME_BUF       0x40u
/* READ AND NOT RECONSTRUCTED, for the same reason as ADDR_LOAD_TYPE2 and
 * recorded the same way. Ten arguments, settled by the annotator against two
 * hard anchors and cross-checked against LoadType2's call site:
 *
 *   1 name   2 x   3 y   4 comm slot   5 army   6 flags
 *   7 remote (the broadcast gate, exactly CreateItem's)   8 uid
 *   9 "settle the point" -- non-zero routes x,y through NearestClearPoint
 *   10 facing, which goes to OBJ_OFF_FACING and +0x528
 *
 * The body is CreateItem's shape with a trooper's tail: malloc 0x5D0 and
 * zero it, pack x and y into ARGUMENT 2'S OWN SLOT, ObjInitCommon with
 * ADDR_TROOPER_BOX, max health from ADDR_RANK_RECORDS rank 0, an AI mode of 1
 * or 6 by whether the army is ADDR_DEFAULT_OWNER, a stagger deadline of
 * rand() %% 0x1F4 past the clock, TakeSoldierName when no name was given,
 * BuildRowSet, the army object list, ADDR_SOLDIER_ANIMS on row zero, and
 * SetUnitPose.
 *
 * WHAT STOPPED IT IS ONE ARGUMENT. BuildRowSet's `dy` is loaded from ARG2's
 * slot -- which by then holds the PACKED point, because the packing above
 * overwrote it -- while `dx` comes from a register holding the ORIGINAL ARG2.
 * Every other caller of BuildRowSet passes x and y as separate ints, and
 * RowInit's MakePoint truncates, so this reading makes every trooper's row y
 * equal its x. That is not what the game does.
 *
 * The depth was re-derived three times from two independent anchors --
 * ARG4 at [esp+0x48] after the `add esp, 0x34`, and ARG1 at [esp+0x38] at the
 * top -- and came out the same each time. So the error is an ASSUMPTION, not
 * arithmetic, and I could not find which one. Written down rather than
 * written out: a reconstruction that can be shown to be wrong is worse than
 * none, and the two role-swap defects found today were both of exactly this
 * shape. */
#define ADDR_CREATE_TROOPER      0x00447620u  /* type 2; reconstructed */
#define AM2_TROOPER_BYTES        0x5D0u
/* Three trooper fields CreateTrooper writes, all overloading offsets other
 * types own. +0x528 takes the FACING beside TROOPER_OFF_FACING -- one argument
 * into two fields -- and +0x524 takes the packed spawn point. +0x00B0 takes
 * the same point read one instruction too EARLY, before ObjInitCommon has put
 * it at OBJ_OFF_POS, so what lands there is the memset's zero; see the note in
 * item.cpp. Named for what the constructor puts in them and nothing more. */
#define TROOPER_OFF_FACING2      0x528u  /* uint8_t, the facing again */
#define TROOPER_OFF_SPAWN_POS    0x524u  /* packed point */
#define TROOPER_OFF_ZERO_B0      0x0B0u  /* always zero -- see item.cpp */
#define AM2_TROOPER_HEIGHT_ADJ   0x18    /* into OBJ_OFF_HEIGHT_ADJ */
/* The AI mode a fresh trooper gets: 1 when the army is ours and 6 when it is
 * not. The two values are the constructor's own; what each mode DOES is the
 * AI band's business. */
#define AM2_TROOPER_AI_MINE      1
#define AM2_TROOPER_AI_THEIRS    6
#define ADDR_TROOPER_BOX         0x00489850u  /* AM2_Rect {-16,-32,16,16} */
#define ADDR_TROOPER_ROW_SPEC    0x00489860u  /* one AM2_ROW_SPEC_BYTES spec */
#define ADDR_CREATE_VEHICLE      0x0045B090u  /* type 3 */
/* READ AND NOT RECONSTRUCTED, and recorded the way LoadType2 and CreateTrooper
 * were before they were taken -- both of which came back and went in once the
 * neighbouring arithmetic had been done twice.
 *
 * WHAT IS SETTLED, and the macros above and below carry it: the six-arm jump
 * table at 0x0045B450 mapping vehicle kind to weapon kind (7, 6, 8, none,
 * none, 0x1D), the three per-kind tables, the eight definition fields it
 * copies straight across, and the two refusals -- the network gate
 * CreateTrooper also has, and the definition lookup that logs "Vehicle aai
 * entry not found for type %d". A vehicle gets a SECOND row exactly when its
 * ADDR_TURRET_ANIMS entry is non-empty, which is the whole of what decides a
 * vehicle's row count. The packed point is built in ARGUMENT 1'S SLOT, the
 * third function today to reuse a slot that way. The ten-argument signature
 * needs no re-derivation at all: armymsg.cpp's typedef, LoadType3 and
 * MakePlacedUnit all agree, and it is in item.h.
 *
 * WHAT IS NOT: which argument reaches CommSlotHasPlayer, which reaches the
 * army-object list and OBJ_OFF_TABLE_REC_SLOT, ObjInitCommon's last two
 * arguments here, and the sense of the `test ah, 2` before the conceal. Those
 * are five esp-relative reads at four different depths, and a first pass
 * produced a version with three of them guessed. Written down rather than
 * written out: this file's rule is that a reconstruction which can be shown
 * wrong is worse than none, and today's two revived deferrals are the argument
 * for recording the gap precisely instead. */
#define ADDR_CREATE_WEAPON       0x0045F0C0u  /* type 4; names itself */
/* 0x00448280, 256 bytes, EIGHT callers -- the multiplayer weapon RESPAWN, read
 * but not reconstructed. Its four gates are all guards and each one is worth
 * knowing before trying to exercise it: the object must be ObjIsType4, there
 * must be an ADDR_MP_SESSION, ADDR_GAME_OVER_FLAGS bit 20 must be set, and
 * bit 15 of the object's own flags must be clear -- which it then SETS, so the
 * whole thing fires at most once per weapon.
 *
 * It looks a key up with the three-argument 0x004346E0 keyed on 0x2D, hands
 * the answer to ADDR_CREATE_WEAPON as one of eight arguments, and raises a
 * delayed EventNotify on the new weapon's uid with a delay of 0x000493E0 --
 * 300,000 ms, five minutes.
 *
 * THE ARGUMENT SHUFFLE IS THE ONLY HARD PART AND IS WRITTEN DOWN HERE SO IT
 * NEED NOT BE RE-DERIVED. Eight dwords are pushed, the key lookup consumes the
 * top three and `add esp, 0xc` removes exactly those, and the five that remain
 * become CreateWeapon's last five arguments under three fresh pushes. MSVC
 * builds one call's arguments across another call; a reading that pairs each
 * push with the nearest call gets both wrong.
 *
 * Not reconstructed because it cannot be exercised: no DirectPlay session can
 * be opened here, so ADDR_MP_SESSION is 0 on every drive and the first gate
 * always refuses. */
#define ADDR_WEAPON_RESPAWN      0x00448280u  /* void(void *weapon) */
/* 0x004601D0, and the respawn's other half: pick a random kind out of the list
 * at ADDR_RESPAWN_KINDS, write that kind's ADDR_MISSILE_DEFS field +0x30 into
 * the caller's dword, and answer the kind. Still original -- it is its own
 * entry in docs/functions.tsv.
 *
 * ITS STRIDE IS AM2_MISSILE_DEF_BYTES AND ITS BASE IS ADDR_MISSILE_DEFS + 0x30,
 * which is the second reader that table has had. LoadType5 named it for the
 * missile whose def index it stores; a weapon respawn indexing the same
 * records with the same stride suggests the table is WEAPONS generally and the
 * name is too narrow. Left alone -- one more reader would settle it, and a
 * rename on two is a guess. */
#define ADDR_RANDOM_RESPAWN_KIND 0x004601D0u  /* int32_t(int32_t *out) */
/* 0x00460120, which BUILDS that list, and 0x004600F0, the per-kind gate it
 * asks. The builder seeds the game RNG with its argument, frees any previous
 * list, then walks all 44 weapon-def records twice: once to total the weights
 * of the kinds the gate allows, and once to fill a malloc'd array with each
 * kind repeated its own weight. So a later draw is WEIGHTED, which is the
 * whole point of the pool and is not visible in the drawer.
 *
 * The gate answers 1 -- allowed -- when there is no multiplayer session at
 * all, when the kind's mask is zero, or when the mask meets
 * ADDR_GAME_OVER_FLAGS; only a set mask that misses refuses.
 *
 * THE WALK'S START IS A FIELD, NOT A TABLE. It runs esi from 0x0066205C to
 * 0x0066294C in steps of 0x34, which reads exactly like a table of its own --
 * and 0x0066205C is ADDR_MISSILE_DEFS + 0x2C, AM2_MISSILE_DEF_BYTES is 0x34,
 * and AM2_MISSILE_DEF_COUNT is 44. So the weight is FIELD +0x2C of the
 * existing records and the loop is a walk over all of them. Caught by
 * grepping the address before writing a #define, which is the whole of that
 * rule and cost one command; a new base here would have been the sixth
 * instance of this project's commonest mistake.
 *
 * The bound confirms it rather than merely fitting: 0x0066205C + 44*0x34 is
 * 0x00662920, which IS ADDR_RESPAWN_KINDS -- the loop stops where the next
 * global starts, the same argument that settled the registration table's
 * nine buckets.
 *
 * AND THIS IS THE THIRD READER THAT TABLE HAS HAD, which the note above
 * ADDR_RANDOM_RESPAWN_KIND asked for by name: it says one more reader would
 * settle whether ADDR_MISSILE_DEFS is too narrow a name, and here is a WEAPON
 * respawn pool weighting all 44 records. Three readers, two of them weapons.
 * Still not renamed here -- that is a change to every use at once and belongs
 * in its own commit -- but the evidence the note asked for now exists. */
/* 0x0042BEA0, the third of 0x0042C440's unnamed callees and the one that
 * gates it: LoadMap returns immediately if this answers 0. It fopen's the
 * "%s.atl" path in "rb", checks AM2_IFF_FORM and then AM2_IFF_TILE, and reads
 * the tileset from there -- so the .atl is an IFF file and this is its
 * reader, distinct from RestoreTileSet at 0x0042C0E0, which reloads the same
 * tileset onto a surface after DirectDraw takes it back.
 *
 * Both were inside the single docs/functions.tsv entry that CLAUDE.md's
 * declined list carried for months as "0x0042BEA0, 1200 B, 2 COM calls" --
 * one entry, four functions, and the two COM calls belonged to the other one.
 * tools/merges.py split it; this is what the rest of that entry turned out to
 * be. */
/* THE .amm IS IFF AND ITS CHUNKS ARE THESE, dumped rather than read off the
 * compare chain. LoadMap's first dispatch covers thirteen tags -- MHDR, BPAD,
 * NPAD, MOVE, OWNR, TRIG, REGN, SCEN, ELEV, ELOW, OLAY, NUMB, SCRI, INDX --
 * and most arms are the same shape: malloc(w*h), fread it, and store the
 * pointer in one global. MHDR is the odd one and comes first in the file: it
 * carries w and h, and from them the function derives ADDR_MAP_ROW_SHIFT via
 * Log2Mask, the two extents, three rectangles and the listener position.
 *
 * THE FIRST CHAIN'S ARMS, extracted from the disassembly rather than read off
 * and retyped -- each is malloc(w*h), fread, store, except where noted:
 *
 *     MHDR  w and h, and everything derived from them
 *     BPAD  ADDR_MAP_PADBIT_LAYER     NPAD  ADDR_MAP_PAD_LAYER
 *     MOVE  ADDR_CELL_WEIGHTS         OWNR  ADDR_TILE_KIND
 *     TRIG  ADDR_TILE_FLAGS           REGN  ADDR_REGION_OF_CELL
 *     ELEV  ADDR_TILE_ATTRS           TLAY  ADDR_MAP_TILES (w*h*2 + 0x14)
 *     SCEN  read to a temp and handed to ADDR_PARSE_SCENARIOS, then freed
 *     OLAY  a realloc'd object list rather than a plane
 *     default  fseek past it
 *
 * Two of those are worth not mistaking for the plane arms. TLAY reads a
 * 0x14-byte header first and its chunk is w*h*2 PLUS that header, so the size
 * check is `size == w*h*2 + 0x14` and not `size == w*h`. SCEN owns nothing:
 * the bytes go to a scratch buffer, ParseScenarios takes them, and the buffer
 * is freed immediately.
 *
 * ADDR_MAP_FIELD_DESCS (0x00485FB8) IS FILLED FROM THE FILE, WHICH MAKES THE
 * FORMAT SELF-DESCRIBING. An earlier version of this note called it a second
 * table in the image and read its contents out of .data. Those bytes are only
 * what the image ships as defaults: inside OLAY, LoadMap freads a COUNT and
 * then that many {tag, size} pairs straight into 0x00485FB8, eight bytes a
 * step. So the map declares its own per-record field layout and the loader
 * obeys it.
 *
 * The defaults it ships are INDX 4, then MOVE, ELOW, TRIG, RESV, ELEV and
 * OWNR at one byte each -- useful as a picture of the usual shape, and NOT a
 * fixed layout to write into the C.
 *
 * That is what makes RESV explicable rather than odd. The second compare
 * chain has arms for OWNR, TRIG, NUMB, MOVE and ELEV and none for RESV, so a
 * field the FILE declares is read for its stated width and thrown away. A
 * self-describing format needs exactly that: the loader must skip a field it
 * does not know by the width the file gives, which is the same discipline as
 * the chunk switch's default fseek one level down.
 *
 * THE WHOLE SHAPE, now that all 1,189 instructions are read: SetDataDir and
 * keep the name in ADDR_MAP_BLOCK; load "%s.atl" through ADDR_LOAD_ATL_FILE
 * and answer 0 if it fails; fopen "%s.amm"; check FORM/MAP; run the chunk
 * loop while the consumed total is under FORM's size; ALLOCATE ANY LAYER THE
 * FILE DID NOT SUPPLY; build the objects; free the temporaries; fclose;
 * restore the data directory; answer 1.
 *
 * AND THE DEFAULTING IS PARTLY A LOOP OVER A CONTIGUOUS RUN OF GLOBALS, not
 * nine separate blocks. 0x0042D01E is `cmp esi, ADDR_TILE_COVER; jl` with esi
 * stepping 4 -- so a range of adjacent layer pointers is walked and each null
 * one filled, and only some layers get an explicit block of their own. A scan
 * for `mov eax, [ADDR_x]; test eax, eax` finds the explicit ones and MISSES
 * every slot the loop covers, which is how the list below came to be nine.
 *
 * That matters because a layer left null is not a crash: SettlePointInRegion
 * spirals over the region map looking for a passable tile and simply never
 * finds one. The reconstruction hangs there, on a valid tile, with every
 * input printed and correct.
 *
 * NINE LAYERS GET AN EXPLICIT BLOCK, in this order --
 * ADDR_CELL_WEIGHTS, ADDR_TILE_ATTRS, ADDR_REGION_OF_CELL,
 * ADDR_MAP_PADBIT_LAYER, ADDR_MAP_PAD_LAYER, ADDR_TILE_KIND,
 * ADDR_TILE_FLAGS, ADDR_TILE_COVER and ADDR_LOAD_PENDING. So a chunk missing
 * from a map is not an error at all: every consumer downstream can assume its
 * layer exists, which is worth knowing before treating an absent chunk as a
 * failure. ADDR_TILE_COVER and ADDR_LOAD_PENDING have no chunk arm at all and
 * are allocated here and nowhere else.
 *
 * FIRST BUG FOUND AND IT WAS AN esp DISPLACEMENT, not a misreading. TLAY's
 * 0x14-byte header carries w and h at hdr[1] and hdr[2]. The original's
 * `lea` for the buffer happens two pushes into the fread call and the two
 * reads happen two pushes further still, so the displacements it uses are
 * four dwords beyond the fields they name -- taken at face value they give
 * hdr[3] and hdr[4], which are ZERO.
 *
 * The failure that produced was silent and total: bytes = w*h*2 came out 0,
 * the arm took its "nothing to read" exit, the tile plane was never loaded,
 * and the very next chunk header was read from the middle of the tile data.
 * A probe on the chunk walk shows it plainly -- CSUM, VERS, DESC ... MHDR,
 * TNAM, ONAM, TLAY, and then a tag of garbage with a size of 23 million.
 *
 * With hdr[1]/hdr[2] the walk runs to the last chunk of the file: OLAY at
 * 131266 with 1,587 objects and 8 declared fields, then MOVE, ELEV, OWNR,
 * NORM, TRIG, REGN, BPAD, NPAD and SCEN, ending at 682,774 of 682,766.
 *
 * WRITTEN ONCE AND REVERTED: the whole function was transcribed, compiled
 * clean, passed every static check and FAILED the A/B outright -- 293,671
 * pixels of 786,432 and a log that stops inside the map load, before the
 * original's "freeing temporary map load data...". So the draft dies in the
 * chunk loop or the object loop, and the map never finishes loading.
 *
 * That is the check earning its keep on the one function this session where
 * an A/B could not possibly be a no-op: LoadMap builds every object on the
 * map, so bootcamp's 1,610-line object dump and its pixels both depend on it
 * completely. The three functions before this were cold or nearly so and
 * their clean A/Bs proved much less.
 *
 * WHERE IT HAS GOT TO: THE FUNCTION COMPLETES AND RETURNS 1. Driven through
 * tools/ab.sh, which is reliable where a hand-rolled click is not, the probes
 * run end to end -- entry with base=bootcamp and folder=data\bootcamp, the
 * .atl loaded, the header accepted, every chunk walked to 682,774 of 682,766,
 * 1,587 objects built, the frees, the fclose and the final SetGameDir.
 *
 * The earlier reading that it died in fclose was wrong twice over: once from
 * the double-close artefact, and once from two runs that never left the title
 * screen. It does not die at all.
 *
 * SO THE BUG IS A GLOBAL IT FAILS TO SET, and the failure is in the CALLER:
 * the original goes on to print ReadScript's "lines: 101 tokens: 372 names:
 * 43 compounds: 16" and "calculating region data...", and ours prints
 * neither, so the LEVEL SCRIPT LOAD is what breaks.
 *
 * NORM is ruled out -- it is plane-sized and looks like a layer this misses,
 * and the original has no arm for it either: every cmp and sub in the switch
 * is accounted for and 'NORM' is not among them, so both sides fseek past it.
 *
 * BISECTED, TWO ARMS RULED OUT: skipping BuildMapObjects entirely changes
 * nothing, and skipping the final SetGameDir changes nothing. So the object
 * loop and the directory restore are both innocent, and what is left is the
 * chunk arms, DeriveMapGeometry and the layer defaulting -- everything that
 * writes a GLOBAL rather than doing work.
 *
 * THE ROOT CAUSE, AND IT IS AN OMISSION RATHER THAN A MISREADING. Between
 * the layer defaulting and the object loop the original does four things
 * this reconstruction did none of:
 *
 *     call ADDR_SEAL_MAP_EDGES
 *     call ADDR_REBUILD_TILE_COVER
 *     call ADDR_BUILD_AAI_BUILTINS
 *     if (!ADDR_LOAD_PENDING) create an object from ADDR_AAI_KEY_980000
 *                             at ADDR_ZERO_POINT
 *
 * AND ADDR_LOAD_PENDING IS A FLAG BEING TESTED, NOT A LAYER. It appeared in
 * my scan for `mov eax, [ADDR_x]; test eax, eax` -- which is the shape of an
 * allocate-if-null block AND the shape of an ordinary flag test, and the scan
 * cannot tell them apart. So it went into the defaulting list, where the
 * reconstruction malloc'd w*h bytes and stored the pointer into an int32_t
 * flag. That makes the flag non-zero, which SKIPS the creation above, which
 * is where at least some of the three missing objects come from.
 *
 * Two errors from one scan: it missed a loop it could not see (the four
 * reveal grids) and it swallowed a flag it should not have. A pattern match
 * for a code shape finds every site with that shape and no others, and
 * "allocate if null" and "test a flag" are the same shape.
 *
 * THE CORRECT LIST, extracted by the WRITER rather than the test -- every
 * `mov [ADDR_x], eax` that follows a malloc between 0x0042CE05 and
 * 0x0042D080: ADDR_CELL_WEIGHTS, ADDR_TILE_ATTRS, ADDR_REGION_OF_CELL,
 * ADDR_MAP_PADBIT_LAYER, ADDR_MAP_PAD_LAYER, ADDR_TILE_KIND,
 * ADDR_TILE_FLAGS and ADDR_TILE_COVER -- eight, plus the four reveal grids
 * the loop fills through a register. Twelve allocations, and
 * ADDR_LOAD_PENDING is not among them.
 *
 * Extracting by the writer would have got this right the first time and by
 * the test did not, which is the same lesson this file records for naming a
 * global: prefer the writer.
 *
 * AND THE MAP FILE ANSWERS QUESTIONS THE GAME CANNOT BE ASKED CHEAPLY.
 * bootcamp.AMM's OLAY chunk declares 1,587 objects, 8 fields and 92
 * field-described records -- and its longest SCRI string is ZERO BYTES. So
 * the script-name path never runs on this map, which kills the standing
 * suspicion that an unbounded read into a fixed buffer was smashing the
 * stack. One offline parse, no game, no A/B; the same class of instrument as
 * tools/ammcheck.py and it should have been the first thing tried rather than
 * the fifth.
 *
 * THE PEEK COMMAND IS WHAT LED HERE. `drive.sh ctl "peek
 * ADDR N"` dumps N dwords and answers under AM2_NOPATCH=1, so the original's
 * map block can be read after a real load with no probe code in it. Over the
 * whole 416-byte block the two sides agree on everything except the OBJECT
 * COUNT at 0x00514F04: the original ends with 1,590 and ours with 1,587,
 * which is exactly our OLAY record count. So the original creates THREE
 * objects this reconstruction does not, and every rectangle, extent, shift
 * and descriptor around it matches.
 *
 * The candidates are the three `continue` exits in the weapon arm -- kind
 * exactly 0x64, a kind the gate refuses, and a negative AAI index -- one of
 * which is presumably not an exit in the original at all.
 *
 * PARKED BEFORE THAT, and the next step was a TOOL rather than another bisect. Each
 * of these runs is a full ab.sh and answers one yes/no question; comparing
 * the map globals directly would answer all of them at once. The control
 * socket is harness and answers under AM2_NOPATCH=1 as well, so a peek
 * command -- dump N bytes at an address -- would let both sides be compared
 * after a load with no probe code in either. That is the same argument that
 * produced `ctl widgets` and `objdump.py --table`, and this is the third
 * subsystem to want it.
 *
 * Ruled out by probing: the record indices are 6..697 against an array of
 * 1,587, so nothing is written out of bounds; every record's script pointer
 * is null unless SCRI set it; skipping the string frees changes nothing;
 * skipping the array free changes nothing; and all eight field descriptors
 * this map declares have sizes of 4 or 1, so no read overruns its
 * destination. The map's own field list is INDX, MOVE, ELEV, OWNR, TRIG,
 * NUMB, GRUP and SCRI -- GRUP being a second tag with no arm, read and
 * dropped like RESV.
 *
 * HOW TO PICK THIS UP CHEAPLY, because the hand-driven probe loop is what
 * made it expensive. `tools/drive.sh start` plus a `point.py` click on BOOT
 * CAMP reaches the mission only sometimes -- two runs in a row sat on the
 * title screen with the click reporting success, and a title-screen run logs
 * the same "Object AAI record not found" lines a real load does, so the two
 * are indistinguishable from the log tail alone. Use `tools/ab.sh bootcamp`,
 * which drives reliably, and read $WORK/bootcamp-recon.log; or check
 * ADDR_GAME_CLOCK_MS is ticking before believing any probe, which this file
 * already recommends for exactly this reason.
 *
 * The transcription is kept out of the tree rather than installed while
 * broken. The chunk loop and the layer defaulting now both complete;
 * what still fails is the OBJECT loop, somewhere between its 200th and 400th
 * record of 1,587. Records 0 through 3 are type 17 kind 940, and three of
 * type 23 -- all plausible, so it is not the first thing it touches.
 *
 * THE OBJECT-BUILDING LOOP IS EFFECTIVELY A SECOND FUNCTION and is the last
 * part of this to be written. It is not the "CreateWeapon per record" the
 * tail looked like from a distance: per record it packs a sprite key, and if
 * the record's kind is 0x64 or more it either SUBSTITUTES a random respawn
 * kind -- when ADDR_GAME_OVER_FLAGS bit 1 is set, through
 * ADDR_RANDOM_RESPAWN_KIND and the pool ADDR_BUILD_RESPAWN_POOL weighted --
 * or subtracts 0x64 and packs a key for the remainder. Then
 * ADDR_RESPAWN_KIND_ALLOWED gates it, ADDR_ENSURE_SPRITE_AAI_REC finds the
 * record, ADDR_PRELOAD_SPRITE_KEY loads the sprite and its +0x24/+0x26 offset
 * is ADDED to the record's position, and the result is settled with
 * ADDR_TILE_OF_POINT and ADDR_SETTLE_POINT_IN_REGION before CreateWeapon.
 *
 * So the two respawn helpers named a few entries above are not a multiplayer
 * curiosity: they are on the map-load path for every object whose kind is
 * 0x64 or more.
 *
 * AND ITS RECORD POINTER IS OFFSET FROM THE RECORD START. The loop indexes
 * with `[esi-0x13]`, `[esi-0xf]`, `[esi-0xb]`, `[esi-7]`, `[esi-1]` and
 * `[esi+2]`, so esi is not the record base -- it points into the middle and
 * the fields run both ways from it. Reading any of those as a positive offset
 * from the record would put every field in the wrong place, and the stride is
 * still 0x1C either way, which is exactly what makes the mistake survivable
 * long enough to ship.
 *
 * AND THE OBJECT LIST IS A TEMPORARY, which is the last thing that reads
 * wrong from the middle. OLAY's realloc'd array lives in a LOCAL, not a
 * global: the tail walks it calling ADDR_CREATE_WEAPON per record and then
 * frees every record's script string and the array itself before returning.
 * So the records are a parse buffer and the objects are the output.
 *
 * THE PER-RECORD LOOP, and its one trap. For each object the file declares,
 * LoadMap zeroes a set of locals, walks the field table it just read, freads
 * each field by its declared width and dispatches: MOVE, NUMB, TRIG, OWNR,
 * ELEV and ELOW each keep one SIGNED byte, SCRI freads a string of the
 * declared length, INDX gives the record's index, and anything else is
 * dropped. A record whose INDX never arrived is logged by number and skipped
 * -- edi starts at -1 for exactly that.
 *
 * ELOW IS A PACKED PAIR AND IT OVERRIDES THE OTHER TWO FIELDS. When it is
 * non-zero the record takes its elevation from ELOW's HIGH nibble and its
 * owner from the LOW one; only when ELOW is zero do the separate ELEV and
 * OWNR fields get used. So a map can carry all three and the two singles are
 * dead, which is invisible unless the arms are read together -- three fields,
 * two destinations, and a precedence between them.
 *
 * The script string is copied rather than kept: strlen, malloc, strcpy into
 * the record's +0x18, with an empty string skipped. Two strlen passes in the
 * original, one to test for empty and one to size the copy.
 *
 * OLAY IS ALSO NOT A PLANE. It freads a count, reallocs an array of 0x1C-byte
 * records, zeroes the new tail, and then reads 0x10 bytes into each -- so the
 * on-disk record is 0x10 and the in-memory one is 0x1C, and the difference is
 * why a straight fread of the whole array would be wrong. */
#define MAPREC_OFF_TYPE    0x00u
#define MAPREC_OFF_KIND    0x04u
#define MAPREC_OFF_X       0x08u
#define MAPREC_OFF_Y       0x0Cu
#define MAPREC_OFF_TRIG    0x10u
#define MAPREC_OFF_ELEV    0x12u
#define MAPREC_OFF_OWNER   0x13u
#define MAPREC_OFF_MOVE    0x14u
#define MAPREC_OFF_NUMB    0x15u
#define MAPREC_OFF_SCRIPT  0x18u
#define AM2_MAPREC_BYTES   0x1Cu

#define ADDR_MAP_FIELD_DESCS      0x00485FB8u  /* {uint32 tag, uint32 size}[7] */
#define AM2_IFF_RESV              0x56534552u  /* 'RESV', read and discarded */
#define AM2_IFF_MHDR               0x5244484Du  /* 'MHDR' */
#define AM2_IFF_BPAD               0x44415042u  /* 'BPAD' */
#define AM2_IFF_NPAD               0x4441504Eu  /* 'NPAD' */
#define AM2_IFF_MOVE               0x45564F4Du  /* 'MOVE' */
#define AM2_IFF_OWNR               0x524E574Fu  /* 'OWNR' */
#define AM2_IFF_TRIG               0x47495254u  /* 'TRIG' */
#define AM2_IFF_REGN               0x4E474552u  /* 'REGN' */
#define AM2_IFF_SCEN               0x4E454353u  /* 'SCEN' */
#define AM2_IFF_ELEV               0x56454C45u  /* 'ELEV' */
#define AM2_IFF_ELOW               0x574F4C45u  /* 'ELOW' */
#define AM2_IFF_OLAY               0x59414C4Fu  /* 'OLAY' */
#define AM2_IFF_TLAY               0x59414C54u  /* 'TLAY' */
#define AM2_IFF_NUMB               0x424D554Eu  /* 'NUMB' */
#define AM2_IFF_SCRI               0x49524353u  /* 'SCRI' */
#define AM2_IFF_INDX               0x58444E49u  /* 'INDX' */
#define ADDR_LOAD_ATL_FILE        0x0042BEA0u  /* int32_t(const char *path) */
#define ADDR_RESPAWN_KIND_ALLOWED 0x004600F0u  /* int32_t(int32_t kind) */
#define ADDR_BUILD_RESPAWN_POOL   0x00460120u  /* void(int32_t seed) */
#define ADDR_RESPAWN_KIND_MASK    0x0048C530u  /* uint32_t[44], 0 = always */
#define MISSILE_DEF_OFF_WEIGHT    0x2Cu        /* int32_t, respawn weight */
#define ADDR_RESPAWN_KINDS       0x00662920u  /* int32_t *, the eligible kinds */
#define ADDR_RESPAWN_KIND_COUNT  0x00662924u  /* int32_t */
/* CreateMissile reads this as the vertical speed when it is positive; when it
 * is not, the missile derives one from the height difference instead. Named
 * field-numbered, as its neighbour is, since one reader is not a meaning. */
/* The heal percentage MedkitHeal reads out of the MEDKIT definition. The
 * record is not missile-specific and neither is the bsearch that finds it --
 * ADDR_MISSILE_DEF_FIND is a plain by-id lookup over the definition table, and
 * both names come from the first use anyone read. Noted rather than renamed on
 * the strength of one more site. */
#define MISSILEDEF_OFF_HEAL_PCT  0x20u
#define MISSILEDEF_OFF_FIELD_0C  0x0Cu
/* StepType5 reads three more of this record. +0x0C, already here, chooses
 * between the arced arm and the flat one; +0x08 is the divisor its life is
 * scaled by and the gate on flying at all; +0x10 is the range, compared
 * against MISSILE_OFF_FLOWN.
 *
 * AND +0x20 IS HANDED TO CreateExplosion AS ITS `damage`, which is a second
 * reading of MISSILEDEF_OFF_HEAL_PCT rather than a contradiction of it -- a
 * healing missile is a negative damage, and this file already records that
 * CreateExplosion's sixth argument is what the blast applies. The name stays;
 * this is what uses it. */
#define MISSILEDEF_OFF_LIFE      0x08u
#define MISSILEDEF_OFF_RANGE     0x10u
#define MISSILEDEF_OFF_FIELD_30  0x30u
/* A weapon's own script-name index, used to look the name up in
 * ADDR_SCRIPT_NAMES and hand it to CreateWeapon. Zero or negative means no
 * name and CreateWeapon gets a null. OBJ_OFF_BOUNDS is the same offset on
 * another kind of object -- overloading, as at 0x52C and 0x538. */
#define ITEM_OFF_NAME_INDEX      0x0Cu   /* int32_t, into ADDR_SCRIPT_NAMES */
#define AM2_NAME_TABLE_STRIDE    16
/* WeaponRespawn's own flag is OBJ_FLAG_8000, which already existed -- and the
 * name stays structural rather than becoming OBJ_FLAG_RESPAWNED, because its
 * two other users are a death event and a kind-7 object and neither is a
 * respawn. One bit, three subsystems, all of them using it as "this has been
 * handled once". Caught by tools/checkoffsets.py, which sees it precisely
 * because the new name used the family's own prefix. */
#define OBJ_FLAG_RESPAWN_RANDOM  0x10000u   /* pick a kind instead of reusing */
#define AM2_WEAPON_RESPAWN_MS    0x000493E0  /* 300,000 -- five minutes */
/* Was AM2_WEAPON_RESPAWN_KEY here, a second spelling of AM2_WEAPON_KEY_KIND
 * -- one concept, two names, which is the kind worth collapsing. Withdrawn;
 * the surviving name is the one that says what the argument IS. */
/* Reconstructed, and its SIGNATURE WAS WRONG HERE: `void(obj, int32)` said
 * the first argument is an object, and the function opens with `cmp eax, 4;
 * jge` and returns. It is (army, packed point). armymsg.cpp's RecvItemCreate
 * passed the created object into the first slot for as long as that typedef
 * stood, so every call returned at the first instruction. */
#define ADDR_ITEM_POST_CREATE    0x0043A210u  /* void(int32 army, uint32 pt) */
/* Four byte grids, one per army, one entry per map tile: PostCreate walks a
 * 5x5 block of tiles around a new object and INCREMENTS the entry in every
 * allied army's grid. A count of what reveals each tile, so a reference count
 * rather than a flag. */
#define ADDR_TILE_REVEAL_GRIDS   0x00514ED8u  /* uint8_t *[4] */
/* A per-tile COVER COUNT, and what settles it is that the only two functions
 * writing it are an exact +1/-1 pair over the same twenty neighbours --
 * TileCoverAdd (0x004384A0) and TileCoverSub (0x00438520). ObjClearFootprint
 * calls the second immediately after taking fifteen off ADDR_CELL_WEIGHTS, so
 * the two tables move together and count different things: weight in
 * fifteens on the cell itself, and a plain count out to a radius. */
#define ADDR_TILE_COVER          0x00514EE8u  /* uint8_t *, one per tile index */
/* Twenty tile-index deltas, in .bss and filled at map load, giving the
 * neighbourhood all three cover functions walk. The bound is the ADDRESS the
 * loop stops at, which is what fixes the count at twenty. */
#define ADDR_TILE_NEIGHBOURS     0x00654BD8u  /* int32_t[20] */
#define AM2_TILE_NEIGHBOUR_COUNT 20     /* 0x00654BD8..0x00654C28 */
/* THE BUILDER, and it fills four tables from one number. 0x00437B60 takes
 * ADDR_MAP_TILES_W and writes ADDR_TILE_NEIGHBOURS, ADDR_TILE_RING8,
 * ADDR_TILE_STEP8 and ADDR_TILE_RING4 in one run of straight-line stores
 * with no loop at all -- which is why three of those four tables had been
 * described as "built at map load" with no name for what builds them.
 *
 * The twenty neighbours are a 5x5 DIAMOND: three cells on the row two above,
 * five on the row above, four on its own row, five below and three two below.
 * The eight in ADDR_TILE_RING8 and ADDR_TILE_STEP8 are the same eight values
 * in the same order, and ADDR_TILE_RING4 is the four orthogonals. */
#define ADDR_BUILD_TILE_DELTAS   0x00437B60u  /* void(void) */
/* The four orthogonal deltas -- north, east, south, west, in that order. This
 * builder is the ONLY reference to any of the four dwords in the whole image:
 * nothing reads them, and no indexed access uses the base either. Written and
 * unused, and named for its shape beside the two rings rather than for a
 * consumer, because there is none to name it from. */
#define ADDR_TILE_RING4          0x00554B70u  /* int32_t[4] */
#define AM2_TILE_RING4_COUNT     4
#define AM2_TILE_RING8_STEPS     8      /* the ring itself */
#define AM2_TILE_RING8_SLOTS     17     /* doubled, plus the odd seventeenth */
/* The +1/-1 pair, and the reader that turns their two tables into tile flags.
 * All three share one bounds test: 2 <= x < width-2 and 2 <= y < height-2,
 * which is what keeps `tile + delta` inside the map without a per-neighbour
 * check. */
#define ADDR_TILE_COVER_ADD      0x004384A0u  /* void(uint16_t tile) */
#define ADDR_TILE_COVER_SUB      0x00438520u  /* void(uint16_t tile) */
#define ADDR_MARK_OPEN_TILE      0x0043A4F0u  /* int32_t(uint16_t tile) */
/* 0x0042BE10, one caller. Clear the cover grid and rebuild it from the cell
 * weights: every interior tile carrying a full weight gets TileCoverAdd. */
#define ADDR_REBUILD_TILE_COVER  0x0042BE10u  /* void(void) */
#define AM2_REVEAL_RADIUS        2
/* 0x0043A330, one caller. Decrement the reveal count over a five-by-five tile
 * block, in the grid of every army allied to this one. The counterpart of
 * whatever increments them; this is the half that takes visibility AWAY. */
#define ADDR_UNREVEAL_AREA       0x0043A330u  /* void(int32 army, uint32 at) */
#define AM2_REVEAL_ARMIES        4
/* A TYPE ID, and its neighbour 0x00516164 is the same kind of thing -- that
 * one holds 0xE80609 and is matched against the very same field. Both are
 * compared with the +8 dword of an object's OBJ_OFF_FIELD_94 record, which
 * orig.h already records as "a type id being matched, not a count". */
#define ADDR_CREATE_WATCHED_KIND 0x00516160u  /* int32_t */
/* 0x0045EED0, EIGHT callers, 48 bytes: is this object an ITEM of that type?
 * Null answers null, a non-item answers 0, and an item answers the comparison.
 * Reconstructed. */
#define ADDR_OBJ_IS_WATCHED_KIND 0x0045EED0u  /* int32_t(const void *obj) */
#define AM2_OBJ_TYPE_ITEM        1
/* Type 2, the one ObjIsType2 answers for and the one LoadDefaultCof counts
 * into `numgreen`. "Trooper" is the vocabulary orig.h already uses for it --
 * ADDR_CREATE_TROOPER is the type-2 arm of the item-create message. */
#define AM2_OBJ_TYPE_TROOPER     2
/* Type 3, from FreeItem's own "DestroyVehicle" arm; see the type table. */
#define AM2_OBJ_TYPE_VEHICLE     3
/* Type 8, from the type-8 destroy handler clearing ADDR_ROACH_MASK; the type
 * table at ADDR_OBJ_TABLE_RECORDS records the evidence. */
#define AM2_OBJ_TYPE_ROACH       8
/* TYPE 5 IS A MISSILE, and the evidence is exactly the shape that settled the
 * roach and the vehicle: LoadType5 calls ObjInitCommon with 5 and then puts
 * ADDR_MISSILE_ANIMS -- missile.ani -- into the row it builds. CLAUDE.md lists
 * 5, 6 and 7 as unread; this is one of the three.
 *
 * Its box is six units square and its whole record is 0xB8 bytes, against a
 * roach's 0x560, which is the other thing that fits. */
#define AM2_OBJ_TYPE_MISSILE     5
/* TYPE 6 IS AN EXPLOSION, identified from ADDR_STEP_TYPE6's body: it holds a
 * deadline against ADDR_GAME_CLOCK_MS, plays sound 0x27 at its own position
 * ONCE, damages every object AllObjectsInRect finds in a rectangle it carries,
 * shakes the screen through ADDR_SHAKE_AT, and can spawn. Sitting immediately
 * after MISSILE is the corroboration rather than the argument -- an exploding
 * missile is what becomes one. CLAUDE.md lists types 5, 6 and 7 as unread;
 * this settles 6.
 *
 * ITS FIELDS ARE OVERLOADED BY TYPE, which is the collision that cost a defect
 * today when one type's OBJ_OFF_ name was used on another's pointer. The same
 * offsets are OBJ_OFF_CHAIN_NEXT_UID, OBJ_OFF_SCRIPT_ID, OBJ_OFF_SCRIPT_STATE
 * and OBJ_OFF_SCRIPT_FRAME on the types that own those names, and none of
 * those meanings applies here -- so the explosion gets its own spelling, the
 * way orig.h already does where it notes "the same offset is OBJ_OFF_POSE on a
 * TROOPER". */
#define AM2_OBJ_TYPE_EXPLOSION   6
#define BLAST_OFF_DAMAGE         0x98u  /* halved for one trooper class */
#define BLAST_OFF_RECT           0x9Cu  /* AM2_Rect, the blast area */
#define BLAST_OFF_DUE_MS         0xACu  /* fires when the clock passes it */
#define BLAST_OFF_SOUND_PENDING  0xB0u  /* cleared after the one sound */
#define BLAST_OFF_MODE           0xB4u  /* >= 5 takes the spawn path */
#define BLAST_OFF_SOURCE_UID     0xB8u  /* handed to DamageObject as attacker */
#define AM2_BLAST_SOUND          0x27
/* 0x00461950, 160 bytes, one caller -- StepType6. Named from its BODY, not
 * from that call site: it tests the tile and all EIGHT neighbours against
 * ADDR_TILE_FLAGS bit 0, and only when every one is clear does it make a row,
 * give it one of AM2_DECAL_VARIANTS sprites chosen by the caller's rand, put
 * it at the point and call RowUpdate. A scorch mark under the explosion, and
 * one that will not be laid over anything already flagged. */
#define ADDR_PLACE_GROUND_DECAL  0x00461950u  /* void(int32 x, int32 y, int32) */
#define ADDR_DECAL_SPRITES       0x0048CBA0u  /* void *[6] */
/* THE EIGHT NEIGHBOUR STEPS, one copy: `{-1-w, -w, 1-w, -1, 1, w+1, w, w-1}`,
 * built by BuildTileDeltas beside ADDR_TILE_RING8, which is the same eight
 * values written TWICE so a walk starting anywhere in 0..7 needs no wrap test.
 *
 * IT WAS ADDR_DECAL_RING8 AND THAT NAME HAS EXPIRED. It was "named for its one
 * consumer, which is what distinguishes it from the other two rings rather
 * than anything in the data" -- an honest name at the time and wrong now: the
 * decal placer at 0x00461984 is one of THREE touchers, the others being
 * BuildTileDeltas, which writes it, and FindPath, which walks it as its A*
 * neighbour set. A name taken from one consumer has to be re-read when a
 * second arrives, and this is what that looks like.
 *
 * FindPath also confirms the BOUND independently, which the old comment could
 * not: its loop runs `p = 0x00523DA0; p += 4; while (p < 0x00523DC0)`, so the
 * table really is eight and ends where 0x00523DC8 begins. */
#define ADDR_TILE_STEP8         0x00523DA0u  /* int32[8], raster order */
#define AM2_TILE_STEP8_COUNT    8
#define AM2_DECAL_VARIANTS       6
/* The missile's own constants. The def record is 52 bytes and the file gives
 * LoadType5 an INDEX into the table rather than a pointer, which is the same
 * tag-for-pointer trade LoadType1 makes with its save tag. */
#define ADDR_MISSILE_BOX         0x00487B78u  /* AM2_Rect, (-3,-3,3,3) */
#define ADDR_MISSILE_ROW_SPEC    0x00487B88u  /* int32_t[4], (0,0,16,16) */
#define ADDR_MISSILE_DEFS        0x00662030u
#define AM2_MISSILE_BYTES        0xB8u
#define AM2_MISSILE_DEF_BYTES    52
/* One reader -- 0x00413E70, which does `[edx*4 + 0x00662054]` with edx =
 * 13*kind, i.e. field +0x24 of def `kind`. That displacement is NOT a table
 * base and naming it as one would have put the whole thing 0x24 bytes late;
 * this is the fourth time that shape has come up and the first where the
 * address I derived instead -- 0x00662030 -- turned out to be a table the tree
 * had named and documented all along. The alias ratchet caught it, not the
 * grep, because the grep was for the displacement.
 *
 * Named structurally, the way OBJ_OFF_FIELD_C0 is, since one reader cannot
 * carry a name. What that reader does with it: zero means the pointer only
 * acts on the frame the mouse button CHANGED, non-zero lets it keep acting
 * while the button is held. definfo.cpp writes it from the parsed record's
 * +0x1C -- one of five fields that table PERMUTES on the way in, which is the
 * reason an ITEMTYPE_ offset does not mean the same thing here. */
#define MISSILE_DEF_OFF_FIELD_24 0x24u
/* The two frames LoadType5 chooses between on the def's first dword: 2 and 5
 * take one and everything else the other. What the dword IS has not been
 * read -- only which two values it treats alike. */
#define AM2_MISSILE_FRAME_A      0x5A
#define AM2_MISSILE_FRAME_B      0x5B
/* 0x0043B9B0, 448 bytes... nine callers. The RUNTIME half of the missile pair,
 * beside LoadType5's savegame half. Everything structural is LoadType5's
 * vocabulary; what is new is the def-3 chain and the fields below.
 *
 * THE MISSILE'S OWN NAMES FOR THREE OVERLOADED OFFSETS. All three carry other
 * types' meanings -- 0xA8 is OBJ_OFF_CHAIN_UID on an item, 0xB4 is
 * OBJ_OFF_SCRIPT_STATE and 0xD0 is OBJ_OFF_DEADLINE_D0 -- and none of those
 * applies here, so the missile gets its own spelling the way the explosion got
 * BLAST_OFF_*. FIELD_A8 keeps a field-numbered name and no claim: this
 * function is its ONLY toucher, since LoadType5 restores RANK, REPAIR_FRAME,
 * PTR_LIST and CHAIN_NEXT_UID and steps straight over it. What is evidenced is
 * that the same argument also lands in OBJ_OFF_ROW0_Y_ADJUST. */
/* THE MISSILE'S FLIGHT STATE, named from StepType5 (0x0043C110) which is the
 * only thing that moves one. Its record is 0xB8 bytes and every offset here is
 * overloaded -- OBJ_OFF_ROW0_Y_ADJUST, SIGHTC_ and BLAST_ names all sit at
 * these numbers for other types -- so the MISSILE_ prefix is doing real work.
 *
 * THREE SUB-UNIT REMAINDERS AGAINST THREE INTEGER COORDINATES, which is what
 * settles the whole group. Each step computes `Cos8(facing) * speed` and adds
 * it to +0x4C, `Sin8(facing) * speed` into +0x50, and the vertical speed times
 * the frame delta into +0x54; then `_ftol`s each, adds the integer part to
 * OBJ_OFF_POS's x, to its y and to +0x42, and SUBTRACTS what it took back out
 * of the remainder. So +0x42 is a third coordinate and +0x4C..+0x54 are its
 * three fractional parts. A float triple tiling against an integer triple is
 * better evidence than any one of the six alone.
 *
 * +0x48 IS THE VERTICAL SPEED and it is the only one with an accelerating
 * term: the arced arm subtracts a gravity constant times the frame delta from
 * it every step and clamps the height at zero on the way down. The flat arm
 * compares it against zero once and leaves it alone. */
#define MISSILE_OFF_HEIGHT       0x42u  /* int16, clamped at 0 */
#define MISSILE_OFF_VZ           0x48u  /* float */
#define MISSILE_OFF_FRAC_X       0x4Cu  /* float, sub-unit remainder */
#define MISSILE_OFF_FRAC_Y       0x50u
#define MISSILE_OFF_FRAC_H       0x54u
#define MISSILE_OFF_SPEED_SCALE  0xA4u  /* float, multiplies the step */
#define MISSILE_OFF_DEF          0x94u  /* the def record StepType5 reads */
#define MISSILE_OFF_SOURCE       0x98u  /* uid, into CreateExplosion's src */
#define MISSILE_OFF_SCALED       0x9Cu  /* gates MISSILE_OFF_SPEED_SCALE */
#define MISSILE_OFF_FLOWN        0xACu  /* units travelled, against the range */
/* The flight is sub-stepped so a fast missile cannot tunnel: three units at a
 * time, with a ShotStrike test after each. AM2_MISSILE_TRAIL_SPACING is the
 * 18.0 that spaces a def-3 trail's segments. */
#define AM2_MISSILE_SUBSTEP      3.0f
#define AM2_MISSILE_TRAIL_SPACING 18.0f
#define AM2_MISSILE_TRAIL_MS     0x42   /* between def-3 segments */
#define AM2_MISSILE_SMOKE_MS     0x1E   /* between def-4/6 smoke puffs */
/* THE GROUND UNDER THE MISSILE, and it was MISSILE_OFF_GROUND -- a name
 * taken from the offset matching OBJ_OFF_CHAIN_UID's, which says nothing about
 * what a missile keeps there. StepType5 is what uses it: the row's
 * ROW_OFF_Y_ADJUST is `MISSILE_OFF_HEIGHT - this` every step, so the sprite
 * rides that far above the terrain, and when the flight crosses onto ground
 * that is higher the correction is added HERE rather than to the height -- the
 * trajectory is untouched and only the reference moves. */
#define MISSILE_OFF_GROUND       0xA8u
#define MISSILE_OFF_NEXT_UID     0xB4u  /* the next segment of a def-3 trail */
#define MISSILE_OFF_LAST_UID     0xD0u  /* on the WEAPON: last missile it made */
/* NOT AM2_ROW_FIELD26_INIT, which is 0x3E8. LoadType5 stores that constant
 * flat; this one stores ScaleBy32Blocks(y) plus 0x3E9. Reaching for the
 * existing name because the sibling uses it would have been off by one. */
#define AM2_MISSILE_ROW26_BIAS   0x3E9
#define ADDR_CREATE_MISSILE      0x0043B9B0u
/* 0x00461F90, two callers, ABOVE the CRT line and so outside the 1,239 -- it
 * stays original and is reached through a seam. Advance a time-based
 * directional animation: index a table by RoundTo8(dir,3) and the elapsed time
 * since ROW_OFF_STAMP_54, clamped to the table height, write that frame into
 * ROW_OFF_SPRITE, and answer whether the animation has NOT yet reached its
 * last frame. CreateMissile discards the answer; naming it from that call site
 * alone would have missed half of what it does. */
#define ADDR_TIMED_DIR_FRAME     0x00461F90u  /* int32(void *rows, int32 dir) */
/* MSVC's stack probe, 48 bytes with thirteen callers, above the CRT line. It
 * walks down a page at a time comparing against 0x1000, which is what makes it
 * recognisable; a function opening `mov eax, <size>; call 0x00465130` simply
 * has a local bigger than a page. Nothing to reproduce -- it is a compiler
 * artifact, not game code -- but it was UNNAMED, and an unnamed 48-byte helper
 * with thirteen callers is exactly what this file records being misread as
 * game code once already at 0x0045CAA0. */
#define ADDR_CRT_CHKSTK          0x00465130u  /* __chkstk */
/* 0x0043CF70, one caller. The FOURTH member of the block-weight family, and
 * the only one with a SIDE EFFECT: it damages what it walks past. See
 * item.cpp beside BlockWeightChain, which it is otherwise a twin of. */
#define ADDR_BLOCK_WEIGHT_DAMAGING 0x0043CF70u /* int32_t(from,at,chain,ref,x) */
/* 0x0043D050, four callers in two functions. Sum ADDR_BLOCK_WEIGHT_DAMAGING
 * over every point of a roach's mask for one direction, offset from a point.
 * Reconstructed. */
#define ADDR_ROACH_MASK_WEIGHT 0x0043D050u /* int32(from, dir, at, unused) */
#define AM2_BLOCK_WEAR_AMOUNT      1
#define AM2_BLOCK_WEAR_KIND        4
#define MSG_CREATE_OFF_UID       4u
#define MSG_CREATE_OFF_NAME      8u    /* char[]; empty means none */
#define MSG_CREATE_OFF_TYPE      0x48u /* int16_t, 1..4 */
#define MSG_CREATE_OFF_A         0x4Au /* WORD to types 2 and 3, DWORD to 1 and 4 */
#define MSG_CREATE_OFF_B         0x4Cu /* int16_t, types 2 and 3 only */
#define MSG_CREATE_OFF_C         0x60u
#define MSG_CREATE_OFF_SUBTYPE   0x64u /* int16_t */
#define MSG_CREATE_OFF_D         0x68u
#define MSG_CREATE_OFF_E         0x6Cu /* uint8_t */
/* Sixteen bytes copied wholesale out of the object at OBJ_OFF_BOX_OFFSETS,
 * which the receiver hands straight back. This used to say "nothing either end
 * does says what they are", which was true of both ends and not of the object:
 * ItemSetBox writes them, and they are the object's hit box RELATIVE to its
 * position -- left, top, right, bottom, the same four the absolute
 * OBJ_OFF_HIT_RECT is built from by adding the position. So an item create
 * message carries the sender's box shape, which is what lets the receiver
 * build the same box without knowing the sprite. */
#define MSG_CREATE_OFF_BLOCK     0x50u
#define AM2_MSG_ITEM_CREATE_LEN  0x70u
/* 0x0042AB50, FOUR callers -- the four creators, one per object type, which is
 * the same fourfold split RecvItemCreate dispatches on. Reconstructed. */
#define ADDR_SEND_ITEM_CREATE    0x0042AB50u  /* void(void *obj) */
#define ADDR_STR_SEND_ITEM_CREATE 0x00485E74u
#define AM2_TROOPER_SARGE_SUBTYPE 0x0A
#define AM2_TROOPER_SUBTYPE_LEADS 0xA  /* then SetLeadsAndAct runs */
#define AM2_WEAPON_KEY_KIND      0x2D  /* KeyLookupTriple's first argument */
#define ADDR_STR_ITEM_GONE_SEND  0x00485E10u
#define AM2_MSG_OBJ_DESTROYED    0x10u
#define AM2_MSG_OBJ_DESTROYED_LEN 8u
#define ADDR_STR_SEND_TROOPER_WEAPON 0x0048AA60u
#define AM2_MSG_TROOPER_WEAPON   0x22u
#define AM2_MSG_TROOPER_WEAPON_LEN 0x1Cu
/* The message ADDR_VEHICLE_DROP_OCCUPANT sends -- twelve bytes, with the
 * header's uid the VEHICLE and the dword after it the occupant. */
#define AM2_MSG_VEHICLE_EXIT     0x25u
#define AM2_MSG_VEHICLE_EXIT_LEN 0x0Cu
#define ADDR_STR_VEH_EXIT_SEND   0x0048C27Cu
#define ADDR_STR_VEH_EXIT_SENT   0x0048C24Cu
/* A 3-bit field at bit 18 of the object's word at +8, get and set. Named for
 * its position rather than its meaning; src/game/item.h records what points at
 * an army index and what argues against it. */
#define ADDR_OBJ_FIELD_A     0x0042A810u  /* uint32_t(const void *obj) */
#define ADDR_OBJ_SET_FIELD_A 0x0042A7F0u  /* void(void *obj, uint32_t) */
#define ADDR_OBJ_FIELD_B     0x00429560u  /* int32_t(const void *obj), +0x64 */
#define ADDR_REMOVE_FROM_ITEM_LIST 0x00428590u /* int32_t(AM2_Object*) */

/* 0x004285F0, "FreeItem %0x" -- its own name. A destructor dispatched on the
 * item kind at +0, with a jump table of eight arms. Four of them share one
 * callee and the rest have their own, each in a different translation unit,
 * which is what the kind really selects: whose object this is.
 *
 * Role names below, tagged with the kinds that reach them. Nothing here says
 * what those subsystems are called, and CLAUDE.md still lists object types 2,
 * 3 and 8 as unidentified -- this narrows where to look rather than answering
 * it. */
#define ADDR_FREE_ITEM             0x004285F0u  /* int32_t(void *, int32_t) */
/* The object's ARMY, and this one is not a guess from a single site: SIX
 * independent callers of ADDR_COMM_MUST_BROADCAST reach it the same way --
 * `movsx ax, byte ptr [obj+0x10]` -- and that function's parameter is
 * documented as the army from its own three answers. A signed byte. */
#define OBJ_OFF_ARMY               0x10u       /* int8_t */
/* 0x00428DA0, 22 callers: destroy an object, choosing the teardown by TYPE and
 * then telling the other players if this army's actions are their business.
 * The three typed arms and the default all end in 0x00429320, which is the
 * shared tail that sets `flags & 4`. Types 2, 3 and 8 are still unidentified,
 * so the arms are named structurally, exactly as ObjIsType2/3/8 are.
 *
 * This was ADDR_OBJ_ACTION, which event.cpp's reset path took from its own
 * call site and which says nothing about what the function does. Renamed
 * rather than aliased -- the checkpatches ratchet refused the second name,
 * which is the rule this file states and which I had just broken. */
/* 0x00428C40, one caller -- 0x00425EE0 -- and it runs EVERY FRAME of a live
 * mission, 69 times in a driven Boot Camp window. Read the caller's BODY, not
 * its summary: 0x00425EE0 consumes a pending menu request and RETURNS on that
 * branch, so the teardown is the short arm and everything after it, this sweep
 * included, is the ordinary per-frame path. Filed here because the opposite
 * was written down first, from the caller's name alone.
 *
 * It walks the whole item registry and frees everything past its deadline,
 * telling the other players first. Bit 27 exempts an object from the sweep;
 * what it means is not established, so it is named structurally as
 * OBJ_FLAG8_BIT40 already is. */
#define ADDR_FREE_OVERDUE_ITEMS    0x00428C40u  /* void(void) */
#define OBJ_FLAG_NO_SWEEP          0x08000000u  /* bit 27, meaning unestablished */
#define ADDR_DESTROY_BY_TYPE       0x00428DA0u  /* void(void *obj) */
/* Zeroed by every per-type destroy handler, in a pair of 16-bit stores, right
 * beside the script id. It sits immediately after the four script dwords at
 * 0xB0..0xBC, which is suggestive and not evidence, so it is named the way
 * OBJ_OFF_FIELD_540 already is -- structurally, until something reads it.
 *
 * Something reads it now. SelectInventorySlot does `mov ecx,[obj+0xC0]; mov
 * ecx,[ecx]` and uses that first dword to index ADDR_WEAPON_HANDLERS, so this
 * is a POINTER to a type record, not a scalar -- which also explains a
 * destroy handler clearing it. The name is left alone until the record itself
 * is read; what has changed is that it is no longer unread. */
#define OBJ_OFF_FIELD_C0           0xC0u
#define ADDR_DESTROY_TYPE2         0x00449460u  /* void(void *obj) */
/* 0x0044A3C0, one caller, which ADDS the answer to a y coordinate. The
 * negated SPR_OFF_OVY of the sprite the object's FIRST row is showing. */
#define ADDR_OBJ_OVERLAY_Y         0x0044A3C0u  /* int32_t(const void *obj) */
/* 0x0045A770, 336 bytes, seven callers -- READ, and it identifies its own
 * table. It takes an object's footprint back OUT of the map's cell weights:
 * gated on flag 0x00200000, which it clears on the way out, it walks the
 * points of ADDR_VEHICLE_MASK record [obj->[0x52C] * 32 + dir] -- the same
 * count-below-points layout that block documents -- and for each point
 * subtracts 15 from the cell byte and calls 0x00438520.
 *
 * The direction comes from RoundTo8 of two facings summed, and a per-call
 * STAMP at 0x00662020 with a mark array at 0x00661E20 stops a cell being
 * decremented twice when two points land in it -- the same idea FirstItem's
 * stamp uses on the object table.
 *
 * Which says something the project lists as unknown. CLAUDE.md records object
 * types 2, 3 and 8 as unidentified; this is called by the TYPE 3 destroy
 * handler and indexes the VEHICLE mask with obj->[0x52C] as the kind, and that
 * table is named from the builder that logs "vehicle mask direction: %d". So
 * type 3 is a vehicle, or at any rate carries a vehicle kind and a vehicle
 * footprint. Evidence, not proof: the function has seven callers and only one
 * of them is that handler. */
#define ADDR_OBJ_CLEAR_FOOTPRINT   0x0045A770u  /* void(void *obj) */
/* 0x0045A620, six callers -- the counterpart, and gated on the same flag the
 * other way round: it does nothing when OBJ_FLAG_FOOTPRINT_ON is already set,
 * bumps the same stamp at 0x00662020, and adds where the other subtracts.
 * SaveType3 brackets its record write with the pair. */
#define ADDR_OBJ_SET_FOOTPRINT     0x0045A620u  /* void(void *obj) */
/* What the two big savers write beside their records. The prefix is the thing
 * that reads them rather than a type, because 0xA8 and 0xAC already carry
 * OBJ_OFF_ and TROOPER_OFF_ names for other readings of the same bytes. */
#define SAVED_OFF_LIST_COUNT       0x0A8u  /* int32_t, dwords in the list */
#define SAVED_OFF_LIST             0x0ACu  /* int32_t * */
#define SAVED_OFF_LIST2_COUNT      0x53Cu  /* type 3 only */
#define SAVED_OFF_LIST2            0x540u
#define SAVED_OFF_TABLE_REC2       0x52Cu  /* type 2's normalised pointer */
#define SAVED_OFF_TABLE_REC3       0x534u  /* type 3's */
#define OBJ_FLAG_FOOTPRINT_ON      0x00200000u
#define ADDR_DESTROY_TYPE3         0x0045A9C0u  /* void(void *obj) */
/* 0x0043CA00, 304 bytes, three callers -- the ROACH twin of
 * ADDR_OBJ_CLEAR_FOOTPRINT. Instruction for instruction the same function with
 * one table swapped: ADDR_ROACH_MASK instead of ADDR_VEHICLE_MASK, its own
 * stamp and mark array, and no kind multiplier on the index because a roach
 * has one kind where a vehicle has six. Same 0xA4 record stride, same gate on
 * OBJ_FLAG_FOOTPRINT_ON, same subtract-15-per-cell.
 *
 * And it says the same kind of thing about TYPE 8 that its twin said about
 * type 3: the type-8 destroy handler calls this, and this indexes the roach
 * mask. So type 8 is a roach, on the same evidence and with the same caveat --
 * three callers, one of them that handler. Types 2, 3 and 8 were all three
 * listed as unidentified; two now have a reading. */
#define ADDR_OBJ_CLEAR_ROACH_FOOTPRINT 0x0043CA00u  /* void(void *obj) */
/* The visit MARK the clearer uses so a cell is decremented once however many
 * mask points land in it: a 16 x 16 window of uint16 around the object, and a
 * stamp bumped once per call and written into every cell it touches. The two
 * are adjacent and the array's size is exactly the gap -- 0x00656328 minus
 * 0x00656128 is 0x200, which is 256 uint16 -- so the window's extent is not a
 * guess. The vehicle twin has its own pair. */
#define ADDR_ROACH_MARK          0x00656128u  /* uint16_t[16 * 16] */
#define ADDR_ROACH_MARK_STAMP    0x00656328u  /* uint16_t */
#define AM2_MASK_WINDOW          16   /* cells across, X major */
#define AM2_MASK_WINDOW_HALF     8
#define AM2_MASK_CELL_SHIFT      4    /* AM2_MASK_STEP as a shift */
#define AM2_TILE_COVER_STEP      15   /* what a footprint takes off a cell */
#define ADDR_DESTROY_TYPE8         0x0043CF30u  /* void(void *obj) */
#define ADDR_DESTROY_OBJ_COMMON    0x00429320u  /* void(void *obj) */
/* DestroyObjCommon iterates the same rows RevealObj does -- see
 * OBJ_OFF_ROW_COUNT, OBJ_OFF_ROWS and AM2_OBJ_ROW_STRIDE further up, which
 * already existed. I defined a second copy of all three here and the compiler
 * said nothing, because an identical redefinition is legal; no ratchet watches
 * offset macros the way checkpatches watches ADDR_ names. Removed. */
/* The chain of attached objects DestroyObjCommon walks after marking, and
 * these two names describe the ITEM reading only -- types 1 and 4, which is
 * what that function has already checked before it reads them. Both hold a
 * UID rather than a pointer: each is handed to FindSlot, whose parameter is a
 * uid, and the object comes back out of the table.
 *
 * The same two dwords mean something else for types 2, 3 and 8. 0x00458070
 * reads +0xA8 as a signed COUNT -- it is the bound of a loop -- and +0xAC as a
 * POINTER to an array of uids indexed four bytes at a time. So the pair is
 * type-dependent, and a reader who takes these names as universal will
 * misread the other half of the object model.
 *
 * Found by reading 0x00458070 BEFORE writing the three handlers that call it,
 * which is the order STATUS.md argued for; the names had already gone in two
 * commits earlier without the qualification. */
/* AND THE TRIPLE'S THIRD MEMBER, written by CreateItem three lines from the
 * other two: the CHILD's +0xA4 takes the PARENT's uid, the parent's +0xA8
 * takes the first child's, and the previous child's +0xAC takes this one's.
 * So the item vocabulary covers 0xA4..0xAC and not just the pair above.
 *
 * IT IS THE SAME THREE DWORDS AS OBJ_OFF_PTR_LIST, and army.cpp settled the
 * other half already: for types 2, 3 and 8 the block is a sub-list header, so
 * 0xA4 + SUBREC_OFF_COUNT is 0xA8 and + SUBREC_OFF_ROWS is 0xAC. Two
 * vocabularies over one union arm each, which is what that note asked for --
 * the type-dependence is DISSOLVED by spelling each type its own way rather
 * than worked around.
 *
 * This is the alias checkoffsets' baseline went from 15 to 16 for, and it is
 * bought with a writer that touches all three: the 0xA0 case one screen down
 * was DECLINED on the same trade because nothing had read it whole. */
#define OBJ_OFF_CHAIN_PARENT_UID   0xA4u   /* uint32_t, item: its parent */
#define OBJ_OFF_CHAIN_UID          0xA8u   /* uint32_t, item: head of the chain */
#define OBJ_OFF_CHAIN_NEXT_UID     0xACu   /* uint32_t, item: the next link */
/* 656 bytes, six callers, still original -- the item-only half of the common
 * teardown, and the only callee of DestroyObjCommon without a name. */
#define ADDR_ITEM_TEARDOWN         0x00439320u  /* void(void *obj) */
/* 0x00458070, 640 bytes, 20 callers -- RECONSTRUCTED, and the name
 * is from the whole body rather than its head. 191 instructions, five returns.
 *
 * ObjAttachTo(subject, target). It DETACHES the subject from whatever it was
 * attached to and then, if a target is given, attaches it to that instead:
 *
 *   - refuses subject == target, a null subject, and any subject whose type is
 *     not 2, 3 or 8;
 *   - takes the old holder's uid from +0xC4, looks it up, and walks its
 *     membership list -- the COUNT at +0xA8 and the ARRAY at +0xAC, four bytes
 *     a slot -- removing the subject's own uid through 0x0042A750;
 *   - with a NULL target it stops there, clearing +0xA0 and +0xC4. That is how
 *     the three per-type destroy handlers call it, so for them it is purely a
 *     detach;
 *   - otherwise it decides a stance code into +0xE4 -- 0, 3 or 6, NOT "0, 3, 6
 *     or 7" as this note said until the body was written: only three
 *     instructions store the field, and the 7 is `mov esi,7`, the value
 *     soldier kind is COMPARED against. It comes from the two armies,
 *     ADDR_DEFAULT_OWNER and three comm queries at 0x0040F190, 0x0040F230 and
 *     0x0040F250, then stores the target's uid in +0xC4 and appends itself to
 *     the target's list through 0x0042A6E0.
 *
 * THE REASON IT WAS DEFERRED EXPIRED WITHOUT ANYBODY NOTICING. This note used
 * to say it "would need names for nine fields (+0xA0, +0xA4, +0xA8, +0xAC,
 * +0xC4, +0xCC, +0xE4, +0x52C, +0x544), three comm methods and four 0x100-byte
 * blocks". Every one of those was named by some other unit in the months
 * after, and re-testing the list against the tree took one command: it needed
 * no new names at all. Re-test a decline against the tree rather than against
 * the reason it was written. Reconstructed in army.cpp, beside ObjsAreAllied,
 * which is the same inlined alliance block and is what caught a wrong global
 * here that no A/B could have. */
/* 0x00429C80, "DestroyItemObject, %x" -- its own name. Five callers. Frees the
 * allocation at +0x90 and clears the live byte at +0x8C; does nothing at all
 * if that byte is already clear, which makes it idempotent. */
#define ADDR_DESTROY_ITEM_OBJECT   0x00429C80u  /* void(obj, int32, int32) */
/* Four bytes past ADDR_OBJ_TABLE, and passed BY ADDRESS to a great many
 * functions -- DestroyItemObject's second parameter among them, where it is
 * simply forwarded to ADDR_ITEM_PRE_DESTROY. What it holds is not established;
 * what is, is that it is one of the two things every object call carries.
 * Named for position, as AM2_COMM_CONNECTED is. */
/* The OTHER map descriptor. 0x0042C8C0 builds both from the map's world
 * extent in the same breath, this one first, and 0x0042D540 frees both -- so
 * ADDR_OBJ_MAP_DESC was a name for what it is passed AS rather than what it
 * is. Renamed rather than aliased. */
#define ADDR_OBJ_MAP_DESC          0x00514F10u
/* 0x00434EC0. Free a subrecord's row array: unregister each 0x60-byte row from
 * the map descriptor, then give the array back. The name is ours. */
#define ADDR_FREE_SUBRECORD_ROWS   0x00434EC0u  /* void(void *subrecord) */
#define TROOPER_OFF_WEAPON_UID     0x54Cu
#define VEHICLE_OFF_WEAPON_UID     0x550u
/* 0x0045AAC0, one caller. Put a unit aboard a vehicle: the unit's
 * OBJ_OFF_RIDING takes the vehicle's uid, and the unit's uid is pushed onto
 * the vehicle's VEHICLE_OFF_PTR_LIST. Two halves of one relationship, written
 * in one function, which is what makes the two field names agree.
 * Reconstructed. */
#define ADDR_BOARD_VEHICLE         0x0045AAC0u  /* void(uint32 uid, void *veh) */
/* The vehicle's SEAT LIST, and it is a sub-list header of the ordinary shape:
 * SUBREC_OFF_COUNT at +4 is how many are aboard and SUBREC_OFF_ROWS at +8 the
 * array of their uids. ADDR_LIST_REMOVE_AT is handed this address directly.
 * ExitAllFromVehicle spelled the count as `+ VEHICLE_OFF_PTR_LIST + 4` before
 * the shape was recognised.
 *
 * The same offset is OBJ_OFF_POSE on a TROOPER -- overloading by type, as
 * VEHICLE_OFF_KIND is beside OBJ_OFF_TABLE_REC_KIND at 0x52C. A THIRD name
 * here was written and deleted: grepping the offset found this one, which is
 * the rule working rather than the ratchet, since a new VEHICLE_ name would
 * have clashed and a new prefix would not have. */
#define VEHICLE_OFF_PTR_LIST       0x538u
/* How many the vehicle holds -- EnterVehicle refuses once the occupant list
 * has reached it. The same offset carries OBJ_OFF_DEATH_STATE on a roach,
 * which is a different record; recorded as its own name in the VEHICLE_
 * family rather than aliased onto that one, the way OBJ_OFF_FIELD_540 is
 * kept apart from its second use. */
#define VEHICLE_OFF_SEATS          0x554u
/* The four beside it, all written by CreateVehicle from the def record and
 * read nowhere that has been looked at. Spelled VEHICLE_ because the same
 * offsets carry UNIT_OFF_INVENTORY_LAST and UNIT_OFF_LAST_DROPPED on a
 * trooper -- one union arm each, which is what this file already does for
 * 0xA4 and 0x94 rather than aliasing. */
#define VEHICLE_OFF_FIELD_558      0x558u
#define VEHICLE_OFF_FIELD_55C      0x55Cu
#define VEHICLE_OFF_FIELD_560      0x560u
#define VEHICLE_OFF_FIELD_564      0x564u
/* A uid, and that is all that is evidenced. 0x00404730 resolves it through
 * the uid lookup and stores the object it gets; EnterVehicle clears it. Two
 * readers, neither of which says what it is FOR, so the name says only what
 * it holds. */
#define OBJ_OFF_UID_56C            0x56Cu
/* 0x0045AA00, three callers. Put a unit into a vehicle, all the way: the seat
 * check, the two fields BoardVehicle writes, Sarge's claim on seat zero, the
 * selection moving from the unit to the vehicle, the broadcast, and then the
 * unit's own destruction. */
/* THE ORDER HERE WAS A GUESS AND IT WAS WRONG, and the reconstruction picked
 * it up: the UNIT is the first argument and the VEHICLE the second. The
 * original's `mov edi, [esp+0xC]` takes the second and reads VEHICLE_OFF_SEATS
 * and VEHICLE_OFF_PTR_LIST off it; `mov esi, [esp+0xC]` one push later takes
 * the first and writes OBJ_OFF_RIDING and OBJ_OFF_SARGE. See item.cpp. */
#define ADDR_ENTER_VEHICLE         0x0045AA00u  /* void(unit, vehicle) */
/* 0x0045E300, and it names itself: "<--Vehicle Enter Send: Vehicle: %x,
 * item: %x". The counterpart of SendVehicleExit, one kind lower. */
#define ADDR_SEND_VEHICLE_ENTER    0x0045E300u  /* void(vehicle, unit) */
#define ADDR_STR_VEHICLE_ENTER_SEND 0x0048C21Cu
#define AM2_MSG_VEHICLE_ENTER      0x24u
#define AM2_MSG_VEHICLE_ENTER_LEN  0x0Cu
/* 0x0045AE30, one caller, and it names itself twice over: "ExitAllFromVehicle:
 * I was killed in a vehicle, damage owner is me" and "... not me". Empty a
 * vehicle from the last seat down.
 *
 * Its three still-original callees are named FROM THIS ONE CALL SITE, which is
 * the naming this file warns about; read their bodies before relying on the
 * names. What is evidenced is only what ExitAllFromVehicle does with them:
 * the first decides whether a seat is emptied at all, the second is run on an
 * occupant that WAS taken out, and the third goes out only on the path whose
 * message says the damage was not ours. */
#define ADDR_EXIT_ALL_FROM_VEHICLE 0x0045AE30u  /* void(vehicle, uint32 uid) */
/* NOT "seat blocked", which was named from ExitAllFromVehicle's call site and
 * which that function's own comment warned to check. It EMPTIES one seat: look
 * the occupant up, choose a spot beside the vehicle, unlink the seat, put the
 * occupant on the ground and move the selection if the vehicle is now empty.
 * Answers 1 when it took somebody out and 0 otherwise. Reconstructed. */
#define ADDR_EXIT_ONE_FROM_VEHICLE 0x0045AC90u  /* int32(int32 seat, vehicle) */
/* 0x0045AAF0, one caller. Where does a LIVING BOAT put somebody getting out?
 * Answers 0 when there is nowhere, which is the only case that plays a sound
 * and refuses. Nothing in it says what it is; the name is from that use. */
#define ADDR_BOAT_EXIT_POINT       0x0045AAF0u  /* int32(vehicle, uint32 *out) */
/* Reconstructed. It is a NEAREST-FREE-TILE search: a box of AM2_BOAT_EXIT_RANGE
 * either side of the vehicle, clipped to ADDR_MAP_BOUNDS, scanned tile by tile,
 * keeping the nearest whose ADDR_CELL_WEIGHTS entry is under AM2_BLOCK_CLEAR.
 * AM2_BOAT_EXIT_MAX is both the initial best and the reject threshold, so a
 * tile is only taken if it beats it -- which is what makes "nowhere to go"
 * and "nothing within 90" the same answer. */
#define AM2_BOAT_EXIT_RANGE        0x48   /* 72, half the search box */
#define AM2_BOAT_EXIT_MAX          0x5A   /* 90, and no tile may equal it */
#define AM2_VEHICLE_EXIT_OFFSET    0x30    /* up and left of the vehicle */
/* Reconstructed as SendVehicleExit in armymsg.cpp. The name here is the one
 * the call site suggested and it is half the story: it does not drop anybody,
 * it TELLS THE OTHER PLAYERS that somebody was dropped. Its own two log lines
 * say so -- "<--Vehicle Exit Send" and "-->Vehicle Exit Sent". Kept rather
 * than renamed, because the address already had it and a second name is what
 * the ratchet exists to refuse; the comment carries the correction. */
#define ADDR_VEHICLE_DROP_OCCUPANT 0x0045E3C0u  /* void(vehicle, occupant) */
#define ADDR_DAMAGE_BROADCAST      0x0042A880u  /* void(obj,uid,int,int,pt,int) */
#define VEHICLE_OFF_KIND           0x52Cu  /* 2 and 3 skip the damage entirely */
/* CreateVehicle's own constants. The size is what its malloc is given -- a
 * `rep stosd` of 0x177 dwords zeroes exactly that, which is the check that
 * neither number is guessed. The rank is a plain literal into OBJ_OFF_RANK and
 * the weapon flags a plain 4 into CreateWeapon's fifth argument. */
#define AM2_VEHICLE_BYTES          0x5DCu
#define AM2_VEHICLE_RANK           6
#define AM2_VEHICLE_WEAPON_FLAGS   4
#define ADDR_STR_VEHICLE_NO_AAI    0x0048BFB0u /* "Vehicle aai entry not found
                                                * for type %d" */
/* Two more bits of a ROW's flags word, named for the bit as ROW_FLAG_BIT8 is
 * and for the same reason: CreateVehicle sets 6 and 8 together on the hull row
 * and 5 alone on the turret, and nothing read so far says what any of them
 * mean. */
#define ROW_FLAG_BIT5              0x20u
#define ROW_FLAG_BIT6              0x40u

/* FORMATION. 0x00404400 places a follower relative to whatever it is
 * following, and the table it indexes is what identifies the whole cluster:
 * twelve 6-byte entries at 0x00473EA0, {uint8 facing, pad, int16 distance},
 * and the guard above it is `slot < 12`. Decoded, they are squad positions --
 * every facing a multiple of 45 degrees and every distance 48, 64, 96 or 128:
 *
 *    0 behind 64     3 behind 96     6 right 64      9 behind-left 96
 *    1 b-left 48     4 f-right 96    7 left 64      10 behind-right 96
 *    2 b-right 48    5 f-left 96     8 FRONT 128    11 behind 128
 *
 * The slot's facing is ADDED to the leader's, so the formation turns with it,
 * and the distance is doubled for a type 3 -- vehicles get twice the spacing.
 * The result is clamped to the map bounds and then put through the tile
 * resolver, so a follower is never placed off the map or inside terrain.
 *
 * A slot of 12 or more goes to ADDR_FORMATION_POINT_FAR instead, which is
 * reconstructed as FormationPointFar and called by name. */
/* An 8-bit heading. Read for types 2, 3 and 8 and ADDED to a formation slot's
 * own facing before going to ADDR_COS8/ADDR_SIN8, which mask to 8 bits. */
#define OBJ_OFF_FACING             0x40u   /* uint8_t */
/* 0x0043BBE0, one caller. Apply a shot's damage to what it hit, and make the
 * shooter turn on the target. Its OBJ_OFF_FIELD_94 record supplies both the
 * damage and the code that decides how it is scaled. */
#define ADDR_APPLY_SHOT_DAMAGE     0x0043BBE0u /* void(target,shot,a,b,dbl) */
#define TYPEREC_OFF_CODE           0x00u  /* 1..30, the switch's subject */
#define TYPEREC_OFF_DAMAGE         0x1Cu  /* int32_t, the base amount */
#define TYPEREC_OFF_FIELD_08       0x08u  /* ShotStrike subtracts it from 20 */
#define TYPEREC_OFF_FIELD_3C       0x3Cu  /* uint8_t; a non-zero one on an ITEM
                                           * makes ShotStrike leave it alone */
/* 0x0043C000, one caller -- ADDR_STEP_TYPE5. Reconstructed. */
#define ADDR_SHOT_STRIKE           0x0043C000u /* int32(shot, packed, height) */
/* 0x0043BCD0, one caller. Does this shot hit that object? Six arguments and
 * an out parameter; nothing in it says what it is, so the name is from what
 * ShotStrike does with the answer. */
#define ADDR_SHOT_HITS_OBJ         0x0043BCD0u /* int32(target, code, army,
                                                * height, shot, int32 *out) */
#define AM2_SHOT_STRUCK_NOTHING    0
#define AM2_SHOT_STRUCK_HARD       5   /* a flagged tile of weight >= 15 */
#define AM2_SHOT_STRUCK_GROUND     6
#define AM2_SHOT_CODE_RANDOM       3      /* 1..damage+1, and kind 1 not 2 */
#define AM2_SHOT_CODE_ANTI_TROOP   30     /* doubles again against a type 2 */
#define AM2_SHOT_DAMAGE_KIND       2
#define AM2_SHOT_DAMAGE_KIND_RAND  1
#define AM2_SHOT_DIR_BIAS          0x80   /* added to OBJ_OFF_FACING */
/* ShotHitsObj's own vocabulary, all of it read off the body.
 *
 * THE HEIGHT BANDS ARE THE WHOLE SHAPE. A shot at height H hits an object
 * whose ObjHeight is under H only while that object reaches within
 * AM2_SHOT_OVER_UNDER of it, and one at or above H only while the shot
 * reaches within AM2_SHOT_UNDER_OVER. Two constants, eight apart, and the
 * asymmetry is the original's.
 *
 * THE EIGHT CODES ARE A SET, NOT A RANGE, and the image spells it as a
 * two-level jump table -- a byte index per code and two targets -- built
 * TWICE with identical index bytes and different targets. Written here as a
 * membership test, which is what those thirty bytes are.
 *
 * THE PIERCE LIMITS ARE COMPARED UNSIGNED against the SHOT's own +0xB0. What
 * that byte is called is OBJ_OFF_SCRIPT_ID, which is another type-dependent
 * field: on a type 5 it is how much the shot can get through. Recorded, not
 * aliased, exactly as 0x98 and 0xA0 already are. Same for +0xA0, which this
 * function uses on a shot as "has already struck something" -- a FIFTH
 * reading of OBJ_OFF_FORMATION_SLOT's offset. */
#define OBJ_FLAG_SHOT_PROOF        0x2000000u /* same-army shots pass through */
#define AM2_SHOT_OVER_UNDER        0x10
#define AM2_SHOT_UNDER_OVER        8
#define AM2_SHOT_FACING_ARC        0x30   /* AngleDelta under this sets *out */
#define AM2_SHOT_PIERCE_CLASS1     0x5C
#define AM2_SHOT_PIERCE_CLASS2     0xA6
#define AM2_SARGE_SOLDIER_KIND     7
/* 0x00457DA0, one caller. What the shooter does once it has hit something it
 * is not allied with: award it experience. Reconstructed; see item.cpp. */
#define ADDR_SHOOTER_REACT         0x00457DA0u /* void(shooter, target) */
/* What it PAYS, which is the whole function: 1 for anything, the target's
 * rank + 1 for a live type 2, 3 or 8, three times that if the shot killed it,
 * and a flat 100 for a killed Sarge. Sarge is worth more than a rank-7
 * anything -- 100 against 24 -- and the Sarge test is on the dead branch
 * only. */
#define AM2_KILL_POINT_SCALE       3
#define AM2_SARGE_KILL_POINTS      100
#define OBJ_OFF_FORMATION_SLOT     0xA0u   /* int32_t, index into the above */
/* FOURTH READING, and it is the one that makes the type-1 answer plain rather
 * than likely: CreateItem's LEAF arm ends by storing the sprite FRAME it
 * unpacked out of the key here -- `key & 0x7F`, KeyFieldC, the third field of
 * the sprite triple. So on a type 1 this offset holds a frame index, written
 * by the creator and replayed by LoadType1, and the twelve-entry formation
 * table below belongs to some other arm of the union. Three readings called
 * that likely; a WRITER settles it. */
/* THIRD READING, and it strengthens the type-dependence note below rather
 * than settling it: LoadType1 replays this field through ChangeObjectFrame
 * with flag 1, exactly as it replays OBJ_OFF_REPAIR_FRAME with flag 0. So on
 * a TYPE 1 the pair are two frame indices for two layers, and a formation
 * slot is what the same offset means somewhere else. */
/* AND 0xA0 IS PROBABLY TYPE-DEPENDENT, which is a fact about the STRUCT
 * rather than about this name. Two readings, both from live code:
 *
 *   air.cpp passes it to ADDR_FORMATION_POINT as a slot, and that function
 *   indexes ADDR_FORMATION_SLOTS, which has TWELVE entries.
 *
 *   ADDR_STEP_TYPE1_4 reads it, adds one, skips the value 1, wraps after
 *   SIXTEEN, and hands the result to ADDR_CHANGE_OBJECT_FRAME. That is an
 *   animation cycle, and it would run off the end of a twelve-entry table.
 *
 * The two cannot both be true of one field -- but they are reached from
 * different object TYPES, air.cpp's follower being a 2/3/8 and the stepper's
 * being a type 1 or 4. So the likeliest answer is that the object is a union
 * past its header and 0xA0 means different things in different arms.
 *
 * Recorded rather than aliased: a second name at this offset would push
 * checkoffsets' family-alias baseline from 13 to 14, and buying a name with a
 * ratchet is the wrong trade for something not yet established. */
/* The uid of what this object is following. 0x00404730 resolves it and drops
 * it -- writing 0 back -- when the leader is concealed, out of health, already
 * destroyed, or the wrong type. */
#define OBJ_OFF_FOLLOW_UID         0xC4u   /* uint32_t */
/* +0xC4 on an ITEM is ITEM_OFF_LAST_USE, already named further up and used by
 * UnitWeaponInfo for exactly what that name says. It was very nearly given a
 * second name here -- WEAPON_OFF_LAST_FIRED -- and a NEW PREFIX is the one
 * shape tools/checkoffsets.py cannot see, so nothing would have refused it.
 * The rule is to grep the OFFSET, and grepping the prefix is not the same
 * thing: the duplicate that was caught in the same edit reused an existing
 * prefix, and this one would not have. */
#define OBJ_OFF_RIDING             0x570u  /* cleared as an occupant gets out */
#define AM2_VEHICLE_DEATH_DAMAGE   0x2710
#define AM2_VEHICLE_DEATH_KIND     4
/* 0x0042A680, thiscall. Empty a three-field {a, b, ptr} record: clear the two
 * fields, free the pointer if there is one, clear that too. */
#define ADDR_CLEAR_PTR_LIST        0x0042A680u  /* thiscall void(void *) */
/* 0x0042A660, thiscall, the other end of the same record: zero all three
 * fields without freeing anything, which is what a constructor does. And
 * 0x0042A670 is one `jmp` to the teardown above -- an alias, the same shape as
 * FreeSpriteListAlias. */
#define ADDR_INIT_PTR_LIST         0x0042A660u  /* thiscall void(void *) */
/* 0x0042A6E0, 13 callers: append to that record, growing it through 0x0042A6B0
 * when the count has caught the capacity. {capacity, count, items} is the
 * layout this fixes -- the grow test is [+4] against [+0] and the store is
 * into [+8] at index [+4]. */
#define ADDR_PTR_LIST_PUSH         0x0042A6E0u  /* thiscall void(void *, void *) */
#define ADDR_PTR_LIST_GROW         0x0042A6B0u  /* thiscall void(void *) */
#define ADDR_PTR_LIST_SHRINK       0x0042A710u  /* thiscall void(void *) */
#define ADDR_CLEAR_PTR_LIST_ALIAS  0x0042A670u  /* thiscall void(void *) */
/* 0x00434C80, one caller: free a pointer unless it is null, and nothing else.
 * The CRT's own free does the same test; this one is the game's. */
#define ADDR_FREE_IF_NOT_NULL      0x00434C80u  /* void(void *) */
#define TROOPER_OFF_ALLOC          0xACu
#define WEAPON_OFF_FLAGS           0x08u
#define WEAPON_FLAG_DEAD           0x02u
#define ADDR_ITEM_PRE_DESTROY      0x0042A0A0u  /* void(obj, int32_t) */
#define OBJ_OFF_ALLOC_LIVE         0x8Cu   /* uint8_t */
#define OBJ_OFF_ALLOC_PTR          0x90u
/* Kinds 1, 5, 6 and 8 share one arm, and it is the BARE version of the family:
 * the tail all five have and nothing else. 48 bytes. */
#define ADDR_FREE_ITEM_COMMON      0x0043BBB0u
/* Kind 2 is the TROOPER, and this arm names itself: "DestroyTrooper %x". The
 * family name is kept because it is what FreeItem's switch reads by. */
#define ADDR_FREE_ITEM_KIND2       0x004478C0u
/* Kind 3 is the VEHICLE -- ReceiveArmyMsg's switch sends kind 3 to the vehicle
 * message handler -- and this arm carries no string of its own, so the C++
 * name is ours by that mapping rather than from the image. */
#define ADDR_FREE_ITEM_KIND3       0x0045B470u
/* Kind 4 is the WEAPON, and this one names itself: "DestroyWeapon, %x". It is
 * "the one that logs" because its log is NOT gated on the comm object's
 * verbosity, where the trooper's is -- both reach ADDR_LOG, which this build
 * stubs to a single `ret`, so neither prints. */
#define ADDR_FREE_ITEM_KIND4       0x0045F290u
/* Kind 7's arm is the bare one plus a population decrement. 0x0051616C counts
 * live kind-7 objects: 0x00435550 refuses to make a thirty-third and this
 * clamps the count at zero on the way down, so the pair is bounded at both
 * ends. What a kind-7 object IS is not established; it is 0x94 bytes and there
 * are at most 32 of them. */
#define ADDR_FREE_ITEM_KIND7       0x004355F0u
/* 0x00435550, five callers -- the one that refuses a thirty-third. The
 * argument shape comes from LoadType7, which passes a saved header's fields
 * straight in: position, 1, army, OBJ_OFF_FACING as a BYTE, 1, and the
 * header's +4. */
/* 0x00435550, five callers -- the one that refuses a thirty-third. The
 * argument shape comes from LoadType7, which passes a saved header's fields
 * straight in, and MakeKind7's own body confirms it: the fourth argument ends
 * up at OBJ_OFF_FACING, which is exactly the field LoadType7 reads for it.
 *
 * THE COUNT IS INCREMENTED BEFORE THE CHECK AND NOT PUT BACK. A refused
 * attempt leaves ADDR_KIND7_COUNT one higher than the number alive, so once
 * the limit is reached every further attempt pushes it further out and the
 * only way back is ADDR_FREE_ITEM_KIND7's decrements, which clamp at zero.
 * The pair is bounded at both ends and is NOT symmetric in between.
 *
 * Its second argument is unused. */
#define ADDR_MAKE_KIND7            0x00435550u  /* void *(pt,int32,army,facing,
                                                 *        int32,int32) */
/* The AAI record's fields, every one named from the OBJECT FIELD it lands in
 * rather than from a guess -- InitObjFromAai copies each straight across.
 * +0x2C goes to both OBJ_OFF_MAX_HEALTH and OBJ_OFF_HEALTH, so it is the
 * starting health and the maximum at once; +0x2E to OBJ_OFF_HEIGHT_ADJ; +0x2F
 * to OBJ_OFF_RANK; +0x28 is OR'd into the object's flags; +0x10 indexes
 * ADDR_RECORD_LISTS for the row def; +0x14 is handed to ObjInitCommon; and
 * +0x34 is written into every row's own +0x28. */
#define AAI_OFF_DEF_INDEX          0x10u
/* Named for its USE -- "the block handed to ObjInitCommon" -- until that
 * argument was read. It is the box offsets, one AM2_Rect. */
#define AAI_OFF_BOX                0x14u  /* int32_t[4] */
#define AAI_OFF_OR_FLAGS           0x28u
#define AAI_OFF_HEALTH             0x2Cu  /* int16, to max AND current */
#define AAI_OFF_HEIGHT_ADJ         0x2Eu  /* int8 */
#define AAI_OFF_RANK               0x2Fu  /* int8 */
#define AAI_OFF_ROW_FIELD28        0x34u
#define ROW_OFF_FIELD_28           0x28u
/* 0x00433880, two callers. Build an object out of an AAI record: flags, army,
 * the common init, health, the row set, and a post-move. NINE arguments and
 * the ninth is never read. */
#define ADDR_INIT_OBJ_FROM_AAI     0x00433880u
/* TWO OF THIS PROTOTYPE'S SEVEN PARAMETERS WERE NAMED WRONG, AND THE WRONG
 * NAMES HAD BEEN COPIED INTO TWO PRIVATE TYPEDEFS THAT DISAGREED WITH EACH
 * OTHER. It read (obj, dir, type, pt, name, int32, int32); item.cpp declared
 * arg2 `void *dir` and arg5 `const char *name`, objtype.cpp declared arg2
 * `int32_t a` and arg5 `const void *blk`. The body settles both:
 *
 *   arg2 is the NAME. It is tested for null, tested for empty, and passed to
 *   _strlwr at 0x0046D7D6 -- none of which a direction survives.
 *   arg5 is the BOX OFFSETS, one AM2_Rect. Its four int32 go verbatim into
 *   OBJ_OFF_BOX_OFFSETS, and the same four translated by the position go into
 *   the hit box at +0x30, which is the pair tools/objdump.py already prints.
 *
 * arg3 is the object type, written to the object's first dword; CreateRoach
 * passes 8. Read a shared callee's parameters out of its own body -- a name
 * taken from one call site propagates into every private typedef that copies
 * it, and a second private typedef is a second place to be wrong. The single
 * corrected typedef is in src/game/objtype.h. 8 callers. */
#define ADDR_OBJ_INIT_COMMON       0x00429940u  /* void(obj,name,type,pt,box,
                                                 *      int32,int32) */
/* NOT THE EMPTY STRING, WHICH IS WHAT IT WAS CALLED FOR AS LONG AS ONLY THE
 * WRONG PROTOTYPE READ IT. MakeKind7 hands it to ObjInitCommon's fifth
 * argument, and that argument is the box offsets -- so the four int32 here,
 * (0, 0, 1, 1), are a 1x1 box at the origin and the leading zero byte that
 * made it look like "" is the box's own first coordinate.
 *
 * Third name settled by one function this batch, and all three the same
 * mistake: a global named from what its bytes resemble, or from one call
 * site, rather than from the code that reads it. */
#define ADDR_KIND7_BOX             0x00487420u  /* int32_t[4] */
#define AM2_KIND7_FUSE_MS          0x3E8   /* clock + this into OBJ_OFF_DEADLINE_58 */
#define AM2_KIND7_MAX              0x20
/* The 0x94 MakeKind7 allocates is AM2_ITEM_HEADER_BYTES, further down and
 * already named -- the object header and nothing else, which is what a kind 7
 * is. */
#define ADDR_KIND7_COUNT           0x0051616Cu  /* int32_t, at most 32 */
#define ADDR_FIRST_ITEM     0x00427850u  /* void *(void) */
#define ADDR_NEXT_ITEM      0x00427880u  /* void *(void) */

/* Iteration state. The cursor is an index into the table; the stamp is bumped
 * once per pass so a walk can tell which entries it has already visited even
 * though inserts shift indices underneath it. */
#define ADDR_ITER_CURSOR    0x00514F08u  /* int32_t */
#define ADDR_ITER_STAMP     0x0051308Cu
/* 0x00428700, one caller -- the per-frame object sweep, from
 * ADDR_TAKE_MENU_REQUEST's ordinary path. Bumps ADDR_ITER_STAMP, steps every
 * registered object, and in a session tail-jumps to the comm check. */
#define ADDR_OBJ_FRAME_SWEEP  0x00428700u  /* void(void) */
#define ADDR_OBJ_FRAME_STEP   0x004284D0u  /* void(obj *), one caller */
#define ADDR_COMM_SYNC_CHECK  0x00411FB0u  /* void(void); name ours */
/* 0x00424FE0, one caller. Advances a deadline one second at a time -- and
 * NOTHING reads that deadline. 0x005122F8 is written by this function and
 * seeded by 0x00424E80, and there is no other reference to it below the CRT
 * line. So the whole thing is bookkeeping with no consumer. Reconstructed
 * anyway: it is on the per-frame path and its absence would be a difference,
 * even if its presence is not. */
#define ADDR_ADVANCE_SECOND   0x00424FE0u  /* void(void) */
#define ADDR_SECOND_DEADLINE  0x005122F8u  /* uint32_t, game-clock ms */
/* ITS 1000 IS ONLY THE STATIC INITIALISER. ResetLevelState overwrites it at
 * every level start with AM2_TICK_BASE_MS plus AM2_TICK_PER_STEP per
 * difficulty -- 3000, 5000, 7000 -- or AM2_TICK_NET_MS flat in a network game,
 * and seeds ADDR_SECOND_DEADLINE to the same number. So the value in the image
 * is never the value in play, and the comment said otherwise for as long as
 * only the reader had been looked at. */
#define ADDR_TICK_INTERVAL_MS 0x00485104u  /* uint32_t; see ResetLevelState */
#define AM2_TICK_BASE_MS      0xBB8   /* 3000 */
#define AM2_TICK_PER_STEP     2000    /* times ADDR_DIFFICULTY */
#define AM2_TICK_NET_MS       0x1B58  /* 7000, flat, in a session */
/* 0x00424E80, one caller -- the level start. Clear two dozen globals, seed the
 * tick interval from the difficulty, fill ADDR_ALLY_MATRIX with the identity
 * and then ally any two comm players sharing a team. Reconstructed. */
#define ADDR_LEVEL_STATE_RESET 0x00424E80u  /* void(void) */
/* Two globals it clears that nothing else here explains. The first is written
 * twice below the CRT line and read nowhere, so it is write-only as far as
 * this image goes. The second is a game-clock deadline -- 0x0044A94E sets it
 * to the clock plus 300 and 0x0044AB91 refuses while the clock is at or under
 * it -- so it throttles something at three a second; WHAT it throttles is not
 * established, only that this clears it. */
#define ADDR_LEVEL_FLAG_E30    0x00511E30u  /* int32_t, written never read */
#define ADDR_THROTTLE_DEADLINE 0x00659EF8u  /* uint32_t, game-clock ms */
/* The turn keys' repeat delay. Charged 0x32 each time a turn is taken and
 * drained by ADDR_FRAME_DELTA_MS once per frame, so holding left or right
 * steps the facing at a fixed rate instead of once per frame. Named from the
 * pair of writers rather than from either alone: both turn arms charge it and
 * one block drains it, which is a writer/reader pair and not a free. */
#define ADDR_TURN_REPEAT_MS      0x00659F48u  /* int32_t */

/* A UID is (owner << 29) | counter, so eight owners each with a 29-bit
 * counter. These are the per-owner counters, indexed 0..7. */
#define ADDR_UID_COUNTERS   0x00511DE0u  /* uint32_t[8] */
/* 0x00429420, one caller on the level-load path. ItemsReset, then seed the
 * first FIVE uid counters to 1000 -- owners 0..4, which is the four armies
 * plus the neutral one, leaving 5..7 alone. Reconstructed. */
#define ADDR_RESET_ITEMS_AND_UIDS 0x00429420u  /* void(void) */
#define AM2_UID_COUNTER_START     0x3E8        /* 1000 */
#define AM2_UID_ARMY_OWNERS       5
/* Owner used for the object types that do not carry their own. */
#define ADDR_DEFAULT_OWNER  0x004F9FDCu  /* uint32_t */
/* Non-zero enables the AddToItemList commentary. */
#define ADDR_DEBUG_ITEMLIST 0x004FD73Cu  /* int32_t */

/* More of the statically linked MSVC 6 CRT. */
#define ADDR_REALLOC        0x004646D8u  /* void *(void*, size_t) */
#define ADDR_MEMMOVE        0x00465710u  /* void *(void*, const void*, size_t) */

/* Statically linked MSVC 6 CRT */
#define ADDR_FREAD          0x004645C1u  /* size_t(void*,size_t,size_t,FILE*) */
#define ADDR_FOPEN          0x004648E2u  /* FILE *(const char*, const char*) */
#define ADDR_FCLOSE         0x0046486Cu  /* int32_t(FILE*) */
#define ADDR_FSEEK          0x00464F18u  /* int32_t(FILE*, int32_t, int32_t) */
#define ADDR_FWRITE         0x004644B7u  /* size_t(const void*,size_t,size_t,FILE*) */
#define ADDR_MODE_RB        0x00474170u  /* "rb" */

/* ---- typed accessors -------------------------------------------------- */

/* The game's FILE is an MSVC 6 _iobuf. We never dereference it, so keep it
 * opaque rather than pretending it matches ours. */
typedef struct am2_FILE am2_FILE;

typedef size_t (__cdecl *am2_fread_fn)(void *buf, size_t size, size_t count,
                                       am2_FILE *fp);
typedef size_t (__cdecl *am2_fwrite_fn)(const void *buf, size_t size,
                                        size_t count, am2_FILE *fp);
typedef am2_FILE *(__cdecl *am2_fopen_fn)(const char *path, const char *mode);
typedef int32_t (__cdecl *am2_fclose_fn)(am2_FILE *fp);
typedef int32_t (__cdecl *am2_fseek_fn)(am2_FILE *fp, int32_t off, int32_t whence);
typedef void   (__cdecl *am2_log_fn)(const char *fmt, ...);

typedef void *(__cdecl *am2_malloc_fn)(size_t n);
typedef void  (__cdecl *am2_free_fn)(void *p);
typedef void *(__cdecl *am2_realloc_fn)(void *p, size_t n);
typedef void *(__cdecl *am2_memmove_fn)(void *dst, const void *src, size_t n);

/* See ADDR_BLIT_BITMAP_IN. `inout` is genuinely in/out; take its address from
 * a variable you re-read afterwards rather than from a temporary. */
typedef int32_t (__cdecl *am2_blit_bitmap_in_fn)(void *dest, int32_t pitch,
                                                 const void *src,
                                                 int32_t width, int32_t height,
                                                 const uint8_t *remap,
                                                 uint32_t *inout);

#define orig_fread   (*(am2_fread_fn)ADDR_FREAD)
#define orig_fwrite  (*(am2_fwrite_fn)ADDR_FWRITE)
/* The game's own stdio, for the same reason as its malloc: a FILE opened by
 * the game's CRT cannot be read or closed by ours. */
#define orig_fopen   (*(am2_fopen_fn)ADDR_FOPEN)
#define orig_fclose  (*(am2_fclose_fn)ADDR_FCLOSE)
#define orig_fseek   (*(am2_fseek_fn)ADDR_FSEEK)
typedef int32_t (__cdecl *am2_ftell_fn)(am2_FILE *fp);
#define orig_ftell   (*(am2_ftell_fn)ADDR_FTELL)
#define orig_log     (*(am2_log_fn)ADDR_LOG)
/* The game's heap, not ours -- msvcrt has a different one entirely, so anything
 * the game allocated must be freed here and vice versa. */
#define orig_malloc  (*(am2_malloc_fn)ADDR_GAME_MALLOC)
#define orig_free    (*(am2_free_fn)ADDR_GAME_FREE)
/* ChangeObjectFrame, 0x004351C0, still original and now wanted from two
 * modules -- objscript.cpp's runner and event.cpp's ScriptSetObjBitmap -- so
 * the seam lives here rather than being declared twice. */
typedef int32_t (__cdecl *am2_change_object_frame_fn)(void *obj, int32_t frame,
                                                      int32_t flag);
/* orig_blit_bitmap_in is gone: BlitBitmapIn is blit.cpp's now and both its
 * callers are in the win32 half, which can include the flat header directly. */
/* The object table was allocated by the game's CRT, so it must be grown by the
 * game's CRT -- our msvcrt has a different heap entirely. */
#define orig_realloc (*(am2_realloc_fn)ADDR_REALLOC)
#define orig_memmove (*(am2_memmove_fn)ADDR_MEMMOVE)

#endif /* AM2_ORIG_H */
