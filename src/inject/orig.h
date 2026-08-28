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
#define ADDR_LOG            0x0045CAA0u  /* void(const char*,...) -- stubbed to `ret` */
#define ADDR_RECT_SET       0x0042E1C0u  /* void(AM2_Rect*,int32,int32,int32,int32) */
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
/* GameMenu -- 03_123/124/125 load, return, save and abort */
#define ADDR_DLG_GAMEMENU_DELETE          0x00452E20u
#define ADDR_DLG_GAMEMENU_DESTRUCT        0x00452E40u
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
#define ADDR_HUD_WIDGET_C  0x004FCF4Cu  /* may be null */
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
#define ADDR_HUD_WIDGET_TABLE  0x004FCF04u  /* AM2_Widget *[] */
#define HUDWIDGET_OFF_FLAG70   0x70u        /* uint8_t, cleared before paint */
/* 0x004143A0, two callers. Paint all three through vtable slot 1. */
#define ADDR_HUD_PAINT     0x004143A0u  /* void(void) */
/* 0x00414370, one caller -- the per-frame path. The same three widgets through
 * vtable slot 2, then two further steps. */
#define ADDR_HUD_UPDATE    0x00414370u  /* void(void) */
/* The two steps ADDR_HUD_UPDATE runs after the widgets, each with exactly one
 * caller -- this one -- so these names cannot be wrong about anything else.
 * They are still roles rather than recovered names: neither says what it is,
 * and neither pushes a string.
 *
 * The second is the tail JUMP, and a little is established about it: it walks
 * records of 0x64 bytes at 0x004FC8E0, clearing those whose deadline at +0x30
 * has passed and stamping ADDR_CURSOR_X/Y into +0x10 and +0x12 of the live
 * ones. So it ages a table of cursor-anchored, time-limited entries. What they
 * are FOR is not established. */
#define ADDR_HUD_POST_UPDATE 0x00413E70u  /* void(void), 1280 bytes */
#define ADDR_HUD_MARKER_AGE  0x00412190u  /* void(void), the tail jump */
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
#define ADDR_FONT_DESCS     0x004897E8u  /* {const char *face; int32 h; uint16 style}[] */
#define ADDR_BUILD_FONT     0x004466E0u  /* int32_t(int32_t fontIndex) */
/* 0x00446840 and 0x00446880: give one font's glyph bytes back and clear its
 * two table entries, and do that for all three. The second is entry 7 of
 * ShutdownSubsystems' ordered teardown, so naming it takes another of those
 * out of the "a name per entry would be a guess per entry" bucket. */
#define ADDR_FREE_FONT      0x00446840u  /* void(int32_t fontIndex) */
#define ADDR_FREE_ALL_FONTS 0x00446880u  /* void(void), three of them */
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
#define ADDR_BLIT_BITMAP_IN  0x0041BA90u
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
 * 0x00409070 is the AIR layer -- it logs "Air frame %d, pt %d,%d" and reaches
 * ADDR_AIR_POP and ADDR_REVEAL_NEARBY, so it steps and draws air units rather
 * than only drawing them.
 *
 * 0x004123D0 reserves 0x3144 bytes of stack, reads ADDR_GAME_RAND and the
 * framebuffer directly, and draws from a table at 0x004FC8C8. Randomised
 * full-screen drawing on a timer; what the effect IS is not established.
 *
 * 0x00462120 reads ADDR_SELECTED_COUNT, the view origins and four HUD
 * colours, and prunes a list with ADDR_LIST_REMOVE_AT while drawing. */
#define ADDR_AIR_FRAME_DRAW      0x00409070u  /* void(void) */
#define ADDR_DRAW_EFFECT_LAYER   0x004123D0u  /* void(void) */
#define ADDR_DRAW_SELECTION      0x00462120u  /* void(void) */
#define ADDR_REFRESH_DRAW   0x00424BF0u  /* void(void), stays original */
#define ADDR_MAP_CACHE_SURFACE 0x00514E94u /* IDirectDrawSurface *, the painted map */
#define ADDR_PAINT_MAP_TILES   0x0042D580u /* void(const AM2_Rect *tiles) */
#define ADDR_MAP_TILES         0x00514EB8u /* uint16 *, one index per tile */
/* The byte beside it, indexed by a TILE INDEX rather than by a map square: 27
 * sites read it and nothing here writes it. The name is ours. */
#define ADDR_TILE_ATTRS        0x00514EBCu /* uint8_t *, one per tile index */
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
#define OBJ_OFF_HIT_RECT       0x30u  /* AM2_Rect, in world units */
#define OBJ_OFF_HIT_MASK       0x78u  /* non-null means test the bitmask too */
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
#define ADDR_MAP_ROW_SHIFT     0x00514DE4u /* int32, log2 of the map's width */
/* The camera doubles as the top-left of the visible-tile rectangle: the four
 * dwords from ADDR_CAMERA_X are used as a RECT to clip against. */
#define ADDR_VISIBLE_TILES     0x00514EA8u /* AM2_Rect, in tiles */
#define MAP_TILE_SIZE          16
#define MAP_SHEET_COLUMNS      0x1F   /* mask; the sheet is 32 tiles wide */
#define ADDR_FREE_MAP_SURFACES 0x0042D390u /* void(void) */
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
#define ADDR_MENU_SPRITE_MODE    0x004FCDB0u  /* int32_t, non-zero -> mode 1 */
#define ADDR_MENU_OVERLAY_A_FLD  0x004FCDB4u
#define ADDR_MENU_OVERLAY_B_FLD  0x004FCDB8u
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
#define ADDR_TILESET_NAME    0x00511A88u /* char[] */
#define ADDR_TILESET_PATH    0x00511AC8u /* char[] */
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
#define ADDR_SEQ_RUN_BOTH        0x00461930u  /* void(void) */
#define ADDR_SEQ_CTX_A           0x00664580u
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
#define ADDR_MSG_LIST_A          0x0048D8E8u
#define ADDR_MSG_LIST_B          0x004F48C8u
#define ADDR_MSG_LIST_C          0x0048D8D8u
#define ADDR_MSG_LIST_D          0x004F8780u
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
#define ADDR_COMM_REGISTER_SELF  0x004027F0u  /* void(DPID), stays original */
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
#define ADDR_REMOVE_PLAYER       0x004029B0u  /* void(uint32 id), 7 callers */
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
#define ADDR_PACKET_THREAD_PROC  0x00401F00u  /* the thread, stays original */
#define ADDR_PACKET_THREAD_ID    0x004F8B90u  /* DWORD */
#define ADDR_STR_THREAD_FAILED   0x0047384Cu  /* "Error launching packet thread" */
#define ADDR_GAME_SRAND          0x00464416u  /* void(uint32_t) */
#define ADDR_GAME_RAND           0x00464420u  /* int32_t(void) */
#define ADDR_COMM_INIT_DEFAULTS  0x0040FD40u  /* void(void); fills a global table */
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
#define ADDR_COMM_SYSTEM_MSG     0x00410090u  /* void(int32, int32, int32,
                                               *      int32) */
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
 * refuses at thirty) and drained from the head. The layout closes the block
 * exactly: 0x0244 is the last dword of 584. */
#define AIR_OFF_ACTIVE           0x000u  /* int32_t, 1 while one is running */
#define AIR_OFF_PENDING          0x004u  /* int32_t, cleared as one retires */
#define AIR_OFF_COUNT            0x008u  /* int32_t, at most 30 */
#define AIR_OFF_WHERE            0x00Cu  /* packed point[30] */
#define AIR_OFF_KIND             0x084u  /* int32_t[30], 2 or 3 */
#define AIR_OFF_FROM             0x0FCu  /* uint32_t[30], the uid asking */
#define AIR_OFF_EXTRA            0x174u  /* int32_t[30], what 0x00409680 found */
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
#define ADDR_MAKE_PLACED_UNIT    0x0043ACF0u  /* void(where,type,slot,&pts,facing,
                                               * group,name) */
#define AM2_COMM_ARMY_COUNT      4
#define ADDR_COMM_FIND_PLAYER    0x0040F330u  /* thiscall int32(this,id), -1 if absent */
#define ADDR_COMM_REMOVE_PLAYER  0x0040F640u  /* thiscall int32(this,id) --
                                               * "Remove Player numPlayers now = %d" */
#define ADDR_COMM_PLAYER_LEFT    0x0040F790u  /* thiscall int32(this,id), 272 bytes */
/* void(void) -- "Sending EndSetupMessage". The end-of-setup scan, and the same
 * block is INLINED at the end of both ready handlers, so the image holds three
 * identical copies of it. That is what an inline member function looks like
 * once MSVC has declined to inline it at one site out of three. */
#define ADDR_COMM_END_SETUP      0x00410CE0u
#define ADDR_COMM_SEND_PLAYERS   0x00411270u  /* void(int32) -- "SendPlayerMsg for %d" */
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
#define ADDR_LOBBY_RESET         0x00413480u  /* void(void), 320 bytes */
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
#define ADDR_MENU_MSG_LIST       0x0051612Cu  /* the message log, a string list */
#define AM2_MENU_MSG_MAX         0x64         /* trimmed above this many lines */
#define ADDR_CHATBOX_REFLOW      0x00455D60u  /* thiscall(this) on [widget+0x7C] */
#define AM2_BLINK_PERIOD         0x64
#define AM2_BLINK_FLASHES        0x14
#define MP_PANEL_OFF_CHATBOX     0x21Cu
#define MP_PANEL_OFF_BLINKER_0   0x250u
#define MP_PANEL_OFF_BLINKER_1   0x254u
#define CHATBOX_OFF_INNER        0x7Cu
#define AM2_MENU_MODE_NO_CHAT    8
/* Was ADDR_CHAT_APPEND, which is what Announce's second call LOOKS like from
 * where it sits and not what the body does: it stamps a static message record
 * and hands it to SendGameMsg. Appending the line locally is what the FIRST
 * call does. Renamed, not aliased. */
#define ADDR_SEND_CHAT_MSG       0x00411E90u  /* void(char *, int32), 128 bytes */
/* NOT a sprite anything. 0x00457820 walks every object an army owns and calls
 * the SECOND argument on each -- `call ebp`, where ebp is that argument. It
 * went in as ADDR_SPRITE_DROP_NAMED from the one call site, which passes
 * 0x0045A030; that is a function too, not a sprite. See src/game/army.h. */
#define ADDR_FOR_EACH_ARMY_OBJECT 0x00457820u /* void(army, void(*)(void*)) */

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
#define ADDR_STR_ALLRIGHT_WAV    0x00474194u  /* "AllRight.wav" */
#define ADDR_STR_HOST_NOW        0x00474178u  /* "Player %s is now the host." */
#define ADDR_STR_LEFT_AI         0x004741ECu  /* "Player %s has left the game - now AI" */
#define ADDR_STR_LEFT_GAME       0x004741CCu  /* "Player %s has left the game." */
#define ADDR_STR_SET_SESSION_FAIL 0x004741A4u /* "Set Session Failed to reopen Session" */
#define ADDR_STR_DESTROYPLAYER   0x00474220u  /* "DESTROYPLAYER Win Message ..." */

/* The player records live at COMM_OFF_PLAYERS, 112 bytes apart, and the name is
 * the first field -- which is how both "Player %s ..." messages are built. */
#define COMM_OFF_PLAYERS         0x218u
#define COMM_PLAYER_STRIDE       112u
#define COMM_OFF_VERBOSE         0x418u   /* non-zero: log every DESTROYPLAYER */
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
 * when the owner is ours. The groups are 20-byte records at 0x00474444 -- a
 * count and up to four wave names -- and the names say what they are:
 * Aerosol.wav, AirStrike.wav, AutoRifle.wav, Bazooka.wav, Disguise.wav. It
 * goes out on slot 0x10, which orig.h already records as a voice slot. */
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
/* The two text fields of the ENTER BATTLE NAME dialog, inside the paint
 * object: the session's name and the hosting player's. */
#define DLG_OFF_BATTLE_NAME      0x064u
#define DLG_OFF_PLAYER_NAME      0x084u
/* Where HostBattle keeps the two names after the session is up. */
#define ADDR_SAVED_PLAYER_NAME   0x00516094u  /* char[] */
#define ADDR_SAVED_BATTLE_NAME   0x005160D5u  /* char[] */
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
/* The cheat runner. It matches the typed line against a table of 40 words at
 * ADDR_CHEAT_WORDS and dispatches through a 39-entry jump table; anything it
 * does not recognise falls through to ADDR_SCRIPT_RUN_LINE, which is why that
 * function is reachable at all. Entry 0, "when all else fails...", is the
 * master switch and is compared separately before the table is walked.
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
/* 0x0044CFA0: rewrite Options.cfg. Left original -- it is CRT file I/O, and
 * this port replaces the CRT wholesale rather than function by function. */
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
#define ARROWBAR_OFF_SHIFT       0x70u /* int32_t, where the thumb sits */
#define ARROWBAR_OFF_SPAN        0x74u
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
#define AM2_LEVEL_RECORD_SIZE     0x30Cu
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
#define LEVEL_OFF_MAP_NAME        0x004u  /* -> ADDR_MAP_NAME; NOT
                                           * LEVEL_OFF_NAME, which is the
                                           * DISPLAY name at +0x44 */
#define LEVEL_OFF_FOLDER          0x084u  /* -> ADDR_MAP_FOLDER */
#define LEVEL_OFF_STR_1C4         0x1C4u  /* -> ADDR_LEVEL_STR_A */
#define LEVEL_OFF_STR_204         0x204u  /* -> ADDR_LEVEL_STR_B */
#define LEVEL_OFF_RESERVE10       0x244u  /* -> ADDR_TILESET_RESERVE */
#define LEVEL_OFF_STR_248         0x248u  /* -> ADDR_LEVEL_STR_C */
#define LEVEL_OFF_SOUND_NAME      0x288u  /* -> ADDR_LEVEL_SOUND_NAME */
#define LEVEL_OFF_STR_2C8         0x2C8u  /* -> ADDR_LEVEL_STR_D */
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
#define ADDR_OPEN_CD_PROMPT      0x0042F440u  /* void(void) */
#define ADDR_CD_PROMPT_CTOR      0x0042EED0u  /* thiscall obj *(obj, bmp) */
#define AM2_CD_PROMPT_SIZE       0x64u
#define ADDR_OPEN_BATTLE_NAME    0x0042FF60u  /* void(void) */
#define ADDR_BATTLE_NAME_CTOR    0x0042FB00u  /* thiscall obj *(obj, bmp) */
#define AM2_BATTLE_NAME_SIZE     0xA4u
#define ADDR_OPEN_BATTLE_JOIN    0x0042F880u  /* void(void) */
#define ADDR_BATTLE_JOIN_CTOR    0x0042F4C0u  /* thiscall obj *(obj, bmp) */
#define AM2_BATTLE_JOIN_SIZE     0x88u
#define ADDR_OPEN_MOVIES         0x0044E6A0u  /* void(void) */
#define ADDR_MOVIES_CTOR         0x0044DFA0u  /* thiscall obj *(obj, bmp) */
#define VTABLE_MOVIES            0x0046FACCu
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
#define ADDR_SCRIPT_FIND_FILE    0x00421890u  /* probes <map><n>.txt via _findfirst */

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
#define ADDR_LOAD_SHADOW_BMP      0x00423300u  /* void(const char *, spr) */
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
#define ADDR_DEPLOY_VEHICLE      0x0045B9F0u  /* type 3 */
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
#define ADDR_DAMAGE_ITEM         0x004356C0u  /* type 1 */
#define ADDR_DAMAGE_TROOPER      0x00447A40u  /* type 2 -- and this one
                                               * is NOT a guess: it logs
                                               * "DamageTrooper: droping armor
                                               * uid:%x" */
#define ADDR_DAMAGE_VEHICLE      0x0045B4D0u  /* type 3 */
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
#define ADDR_DESELECT_UNIT       0x00427C80u  /* void(obj) */
/* The count word of ADDR_SELECTED_UIDS, which is {capacity, count, items}. */
#define ADDR_SELECTED_COUNT      0x0051230Cu  /* int32_t */
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
#define ADDR_UNIT_ACTION         0x00449660u  /* void(obj, action) -- 44 arms */
/* What SetSoldierKind reaches. The frame setter compares against the row's
 * current frame and returns early unless forced; the pose table is int32 and
 * ships {1, 1, 5, 3, ...}. */
#define ADDR_SET_ANIM_FRAME      0x0040A1A0u  /* void(row, int16 frame, int32) */
#define ADDR_WEAPON_POSE_INDEX   0x004494A0u  /* int32(obj, weapon) */
#define ADDR_WEAPON_POSE_FRAMES  0x00474FE0u  /* int32[] */
/* Kind 7's two extras, both read out of the image: a 64-entry table of names
 * filled at runtime, and a health multiplier that is exactly 1.5. */
#define ADDR_KIND7_NAMES         0x0050712Cu  /* char *[] */
#define AM2_KIND7_HEALTH_SCALE   0x0046FA98u  /* double, 1.5 */
#define AM2_KIND7_NAME_COUNT     64
/* Fields of the object's FIRST ROW that carry its animation. */
#define ROW_OFF_ANIM_CUR         0x40u   /* AM2_AnimTable * */
#define ROW_OFF_ANIM_NEXT        0x48u   /* AM2_AnimTable *, taken up next */
#define ROW_OFF_FRAME            0x4Cu   /* int16_t */
/* Two more, and SetAnimFrame is what establishes both: it searches the table
 * at ROW_OFF_ANIM_CUR for an entry whose id is ROW_OFF_FRAME, stores that
 * entry's AM2_Anim here, and resets the cell index to 0. So +0x44 is the
 * animation actually PLAYING, chosen out of the table four bytes below it,
 * and +0x51 is how far into its cells the row has got.
 *
 * RowAnimFinished reads both back and agrees: it compares +0x51 against
 * anim->frames - 1 and indexes anim->cells with it. */
#define ROW_OFF_ANIM_PLAYING     0x44u   /* AM2_Anim * */
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
#define OBJ_OFF_FIELD_44         0x44u   /* int32_t, from the row above */
/* 0x0042B210, five callers. ADDR_POINT_OF_TILE's two-pointer twin: the same
 * arithmetic, written back as a pair of int32 rather than packed into one
 * dword. Both add 8, which is the centre of a 16-pixel tile. */
#define ADDR_TILE_TO_XY          0x0042B210u  /* void(int32 tile,int32*,int32*) */
/* Four fields SetSoldierKind writes and nothing read so far explains. 0x578 is
 * cleared for EVERY kind; the other three belong to kind 7. */
#define OBJ_OFF_FIELD_578        0x578u  /* int32_t, cleared unconditionally */
#define OBJ_OFF_FIELD_5A8        0x5A8u  /* int32_t, the random name index */
#define AM2_ANIM_TABLE_BYTES     8u      /* ADDR_SOLDIER_ANIMS' stride */
#define AM2_MP_ROLE_SEVEN        7
/* Both read only by ADDR_OBJ_DEATH_CLEANUP and neither established further:
 * the field gates the first delayed event, the flag suppresses the second. */
#define OBJ_OFF_FIELD_94         0x94u
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
#define ADDR_BY_REF_ACTION_A     0x00462000u  /* void(int32_t *, int32, int32) */
#define ADDR_BY_REF_ACTION_B     0x00462080u  /* void(int32_t *,0,0,0,int32) */
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
#define ADDR_OBJ_AFTER_MOVE      0x00439000u  /* void(obj, int32, int32) */
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
#define OBJ_OFF_FIELD_E4         0xE4u
#define OBJ_OFF_FIELD_E8         0xE8u  /* the previous value of 0xE4 */
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
/* Raised beside it and consumed by the level teardown, which bumps
 * ADDR_ATTEMPT_COUNT and logs "Attempt# %d". */
#define ADDR_MISSION_RETRY       0x0051232Cu  /* int32_t */
#define ADDR_ATTEMPT_COUNT       0x00512330u  /* int32_t */
#define ADDR_SAVE_GAME           0x00425790u
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
 * loader at 0x0042C440 zeroes 416 bytes here and then strcpy's in the map
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
#define ADDR_PERFRAME_COUNT_A      0x004F93B8u /* int32_t, gates 0x00403B40 */
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
#define FLOW_OFF_SEQUENCE          0x94u   /* read, never bumped here */
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
/* The outgoing packet. 0x004FAA68 is its base and 0x004FAA6C is base+4 -- the
 * packet's own length field, which doubles as the write cursor. Flush resets
 * it to 0x14, so the packet header is twenty bytes and the first message lands
 * at base+0x14. */
#define ADDR_ARMY_PACKET           0x004FAA68u
#define ADDR_ARMY_PACKET_LEN       0x004FAA6Cu
#define AM2_ARMY_PACKET_HDR        0x14u
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
 * The name is structural. Kind 0x18 has no receiver reconstructed yet and
 * nothing read so far says what the pair means -- the callers are all in the
 * trooper band and pass an object, another object, a byte and a field. */
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
 * logger is stubbed to `ret` in this build, so it is inert either way. */
#define COMM_OFF_EVENT_DEBUG       0x418u
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
#define ADDR_EVAL_OPERAND        0x00421590u  /* int32_t(a, b, c) -- a triple */
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
#define ADDR_RANK_TABLE          0x00473DD4u  /* 28-byte records */
#define RANK_REC_BYTES           28u
#define RANK_REC_OFF_SCALE       0u    /* handed to ADDR_RANK_APPLY */
#define RANK_REC_OFF_XP          4u    /* experience needed for this rank */
#define ADDR_RANK_PROMOTE        0x00457BC0u  /* void(obj) */
/* 0x98 IS TYPE-DEPENDENT, the same way 0x94 and 0xA0 are. For a trooper it is
 * the rank, an int32 in 0..7. For an ITEM -- types 1 and 4 -- HeightAtPoint
 * reads its low byte and uses only the SIGN, as "this thing raises the ground
 * you stand on"; a rank could never make that byte negative. One name, both
 * readings, which is what OBJ_OFF_FIELD_94 already does rather than putting a
 * second name on the offset. */
#define OBJ_OFF_RANK             0x98u  /* int32_t 0..7, or an item's flag byte */
#define AM2_RANK_MAX             7
#define ADDR_TYPE238_ACTION       0x00457CD0u  /* void(void *obj, int32_t) */
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
#define ADDR_BITMAP_AREA_H        0x0048531Cu  /* int32_t, 480 */
/* The pause bits that mean "the map is still loading", which is what puts the
 * wait bitmap up. Four bits, 17 through 20, tested as a group. */
#define AM2_PAUSE_MAP_WAIT        0x1E0000u
/* 0x00462600, 1088 bytes. Whatever the paused mission frame drives before it
 * considers the wait bitmap; stays original and unnamed, since nothing here
 * says what it is. */
#define ADDR_PAUSED_FRAME_STEP    0x00462600u  /* void(void) */

#define ADDR_FREE_BITMAP          0x00446410u  /* void(void **slot) */
#define ADDR_LOAD_BITMAP          0x004462F0u  /* void *(const char *, int32) */
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
/* Positive means the item has a repair frame; see ADDR_HEAL_OBJECT, which is
 * the only reader this reconstruction has read. */
#define OBJ_OFF_REPAIR_FRAME     0x9Cu  /* int32_t */
/* Runs one parsed action against an owner. 4096 bytes in event.cpp with three
 * callers, and it names itself nowhere -- so this is a ROLE, not a recovered
 * source name, and it stays that way until the body says otherwise. */
#define ADDR_RUN_SCRIPT_ACTION     0x00420410u  /* void(action *, void *owner) */

/* The four object fields the object-script runner uses, all read out of
 * UpdateObjectScript's body rather than guessed at a call site. */
#define OBJ_OFF_SCRIPT_ID        0xB0u   /* 1-based index into the table; 0 = none */
#define OBJ_OFF_SCRIPT_STATE     0xB4u
/* UNRESOLVED, and recorded rather than renamed. Two functions WRITE a POINT
 * into this field -- Type2ActionB puts ADDR_ZERO_POINT there and PointActionA
 * puts the point ADDR_RESOLVE_POINT_FOR_TILE hands back -- while two READ it
 * as an int32 and compare it against a script value (objscript.cpp's state
 * compare and event.cpp's testvar). Both cannot be describing the same thing.
 *
 * Two independent writers agreeing it is a position is the stronger half, but
 * the readers are not obviously wrong either, and nothing here settles which.
 * Left alone until something does; the byte pattern is identical either way,
 * so no code depends on the answer yet. */
/* Named structurally: nothing read so far says what any of the three is. The
 * word at 0xB2 is cleared beside the script id, 0xEC is set to whether 0xF4 is
 * positive, and 0xE4 is the AI mode OBJ_OFF_FIELD_E4 already names. */
#define OBJ_OFF_FIELD_B2         0xB2u   /* uint16_t */
#define OBJ_OFF_FIELD_EC         0xECu   /* int32_t, 0 or 1 */
#define OBJ_OFF_FIELD_F4         0xF4u   /* int32_t; only its sign is read */
/* 0x0043A0A0, six callers. Takes an object, a TILE and a point by ADDRESS,
 * consults ADDR_POINT_OF_TILE and writes a point back through that pointer.
 * Named for what it does with its arguments; what it is FOR is not
 * established, and the object argument's role least of all. */
#define ADDR_RESOLVE_POINT_FOR_TILE 0x0043A0A0u /* void(obj, tile, AM2_Point*) */
#define OBJ_OFF_SCRIPT_FRAME     0xB8u
#define OBJ_OFF_SCRIPT_NEXT      0xBCu   /* deadline, compared against 0x00511E04 */
/* A second deadline on the same clock, at +0x58, and 0x004355D0 is the only
 * thing that reads it: past it, bit 1 of the flags goes on. Both names ours. */
#define OBJ_OFF_DEADLINE_58      0x58u
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
#define ADDR_SPAWN_AT              0x00422860u /* void(x, y, kind, army, uid, ...) */
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
#define ADDR_STEP_TYPE2          0x0044B7D0u  /* void(obj) */
#define ADDR_STEP_TYPE3          0x0045D660u  /* void(obj) */
#define ADDR_STEP_TYPE5          0x0043C110u  /* void(obj) */
#define ADDR_STEP_TYPE6          0x00422B90u  /* void(obj) */
#define ADDR_OBJ_MARK_IF_OVERDUE 0x004355D0u /* void(void *obj) -- type 7 */
#define ADDR_STEP_TYPE8          0x0043D980u  /* void(obj) */
#define OBJ_OFF_OWNER            0x04u   /* what a frame's actions are run against */
#define ADDR_SET_OBJ_SCRIPT_STATE  0x004372A0u
#define ADDR_DEF_PARSE_INFO_FILE   0x0041A5F0u
/* NOT DefGameParse -- 0x00424590 is docs/functions.tsv's merged 784-byte
 * entry, and the handler with the "DefGameParse:" string starts at 0x00424780.
 * Same mistake, same cause, as ADDR_DEF_LINK_PARSE two commits ago. */
#define ADDR_DEF_GAME_ENTRY        0x00424590u
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
#define DEF_OBJ_REC_OFF_LINKS      0x0Cu        /* where the count is stored */
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
#define COMM_ARMY_OFF_READY        0x274u   /* m_ArmyReady, its own name */
#define COMM_ARMY_RECORD_SIZE      112u
#define COMM_ARMY_OFF_READY_TO_LOAD 0x270u
#define ADDR_REMOVE_INVENTORY_ITEM 0x00447990u /* void(AM2_Object *, int32_t) */
#define ADDR_SELECT_INVENTORY_SLOT 0x00449860u /* void(AM2_Object *, int32_t) */
/* A unit's weapon inventory: six uids, the one in hand, and a spare field the
 * removal always clears. */
#define UNIT_OFF_INVENTORY        0x54Cu  /* int32_t[6], uids */
#define UNIT_OFF_INVENTORY_LAST   0x560u  /* the sixth entry */
#define UNIT_OFF_INVENTORY_SEL    0x568u  /* int32_t, which slot is in hand */
/* The weapon HANDLER table and the four globals SelectInventorySlot installs
 * out of it. Each record is 16 bytes and every field is a FUNCTION POINTER --
 * established by the readers, which do `mov eax,[global]; test eax,eax;
 * call eax`, not by the shape of the table. The index is the first dword of
 * the weapon's OBJ_OFF_FIELD_C0, so that field is a pointer to a type record
 * rather than the scalar its structural name suggests.
 *
 * THE MAPPING IS NOT SEQUENTIAL, which is the one thing worth getting right
 * here: slot 2 goes to 0x005122F0 and slot 3 to 0x005122DC. The globals are
 * not contiguous -- 0x005122E0..EC sit between them, and at least 0x005122E0
 * is another handler this function does not write -- so reading the four
 * stores as "in order" swaps the last two. Named by SLOT so the swap is
 * visible at the use site. */
#define ADDR_WEAPON_HANDLERS     0x00489880u  /* 16-byte records, 4 fns each */
#define ADDR_WEAPON_FN_SLOT0     0x005122D4u
#define ADDR_WEAPON_FN_SLOT1     0x005122D8u
#define ADDR_WEAPON_FN_SLOT2     0x005122F0u
#define ADDR_WEAPON_FN_SLOT3     0x005122DCu
/* Who selected, and which slot. Written here and by 0x00424F20, read by the
 * HUD and the two 0x458xxx sites. */
#define ADDR_WEAPON_OWNER_ID     0x00511E58u  /* uint32_t, the unit's uid */
#define ADDR_WEAPON_SLOT         0x00511E5Cu  /* int32_t */
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
#define orig_amm_checksum \
            ((uint32_t (__cdecl *)(const char *, const char *)) \
             (uintptr_t)ADDR_AMM_CHECKSUM)
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
#define ADDR_STR_PATH_SEP        0x00478984u  /* "\\" */
#define ADDR_MAP_FOLDER          0x00511AC8u  /* the map's own directory */
#define ADDR_RULES_DIR_STR       0x00485110u  /* a `char *` to "rules"    */
#define ADDR_SCORE_LIMIT         0x00515FF0u  /* seeds `gamescorelimit`   */
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
#define AM2_MSGLIST_SANE_MAX       0x190   /* 400 */
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
#define ADDR_REGIONS               0x00514EF0u
#define AM2_REGION_SIZE            44
#define REGION_OFF_NLINKS          8u   /* uint8_t */
#define REGION_OFF_LINKS           0x0Cu
#define ADDR_ADD_REGION_LINK       0x0042B860u  /* void(int32_t, int32_t) */
/* THE REGION ROUTING TABLES. Two square byte matrices of the same stride, and
 * 0x00406460 is what makes them legible: it indexes both as
 * `m[from * stride + to]`, tests the first against a sentinel byte, and walks
 * the second one hop at a time until it arrives.
 *
 * So ADDR_REGION_COST records whether a pair has been solved -- the sentinel
 * means "not yet" -- and ADDR_REGION_NEXT is a next-hop table: the region to
 * step to when you are in `from` and want `to`. Classic all-pairs routing,
 * stored as bytes because a map has fewer than 256 regions. */
#define ADDR_REGION_STRIDE         0x00514EECu  /* int16_t, both matrices' */
#define ADDR_REGION_NEXT           0x00514EF4u  /* uint8_t *, the next hop */
#define ADDR_REGION_UNSET          0x00514EF8u  /* uint8_t, "not solved yet" */
#define ADDR_REGION_COST           0x00514EFCu  /* uint8_t * */
#define ADDR_REGION_SOLVE_PAIR     0x00438300u  /* void(from, to) */
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
#define ADDR_OPT_MUSIC           0x005125C4u  /* -bm sets, -sm clears */
#define ADDR_OPT_NM              0x0051259Cu  /* -nm */
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

/* Two masks out of that record, each named by its own error message. */
#define ADDR_GET_PLAYER_MASK     0x00402BD0u  /* uint32_t(uint32_t id) -- +0x14 */
#define ADDR_GET_RESEND_MASK     0x00402C00u  /* uint32_t(uint32_t id) -- +0x18 */

/* The comm layer's outgoing message hub, named by its own
 * "SendGameMsg, first message to %x, hehas set to %d". 928 bytes and 14
 * callers, so it is a hub rather than a helper; two of those callers are
 * reconstructed below and reach it through here. Among its other messages is
 * "Error Send can't find Flow for Player %x", which is the same player/FlowQ
 * synonym ADDR_FIND_PLAYER_BY_ID records. */
#define ADDR_SEND_GAME_MSG       0x004022D0u  /* int32_t(void *msg, int32, int32) */

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
#define ADDR_VEHICLE_MSG_RECV    0x0045E590u  /* void(msg *, int32_t army) */
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
#define ADDR_ON_MP_NAME        0x00432D50u
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
#define FLOWQ_OFF_A              0xB0u
#define FLOWQ_OFF_B              0xB4u
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
#define COMM_OFF_OUR_PLAYER_ID   0x3CCu
#define COMM_OFF_PLAYER_COUNT    0x3D0u
#define COMM_OFF_LOCAL           0x400u   /* set when the game is offline */
#define COMM_SLOT_OFF_NAME       0x00Cu   /* 0x40-byte string; CommConstruct
                                           * clears it, StartSelectedGame writes
                                           * "Computer%d" into it */
#define COMM_SLOT_OFF_ID         0x008u
#define COMM_SLOT_OFF_UNACKED    0x058u
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
#define COMM_OFF_DEBUG           0x418u
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
#define ADDR_MOUSE_ACTIVITY      0x004854B0u  /* set from ADDR_GAME_CLOCK_MS on
                                               * any movement or button change */
#define ADDR_MOUSE_B0_EXTRA      0x004854B4u  /* zeroed when button 0 goes down;
                                               * nothing here reads it */
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
#define ADDR_PERF_WORD_A         0x00512568u  /* three int16 cleared with it */
#define ADDR_PERF_WORD_B         0x0051256Au
#define ADDR_PERF_WORD_C         0x0051256Cu
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
#define AM2_MAX_SELECTED         0x40
#define OBJ_FLAG_SELECTED        0x400u
#define ADDR_SELECT_UNIT         0x00427CE0u  /* void(void *obj) -- NOT
                                               * SelectObject: wingdi.h has that
                                               * name and dllmain.c sees it */
#define ADDR_ON_SELECTION_CHANGED 0x00427990u /* void(uint32_t packedPoint) */
#define orig_on_selection_changed \
            ((void (__cdecl *)(uint32_t))(uintptr_t)ADDR_ON_SELECTION_CHANGED)
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
#define ADDR_COMM_FRAME_POST_B   0x00402F50u
#define ADDR_COMM_FRAME_POST_C   0x00403050u
#define AM2_COMM_MIN_BUFFERS     10           /* below this, COMM ERROR: NO BUFFERS */
#define AM2_COMM_OFF_ACTIVE      0x3DCu       /* gates all the comm frame work */
#define ADDR_STATE_LEAVE_COMMON  0x00426640u  /* states 0 and 3 tail-jump here */
#define ADDR_STATE_FRAME_COMMON  0x00426650u  /* and then here */
#define ADDR_STATE0_ENTER        0x004265F0u
#define ADDR_STATE3_ENTER        0x004266F0u
#define ADDR_STATE1_LEAVE        0x004263E0u
#define ADDR_STATE1_ENTER        0x004262E0u
#define ADDR_STATE1_MENU         0x00426400u
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
#define ADDR_KEY_LOOKUP          0x00434290u  /* int32_t(uint32_t key) */
#define ADDR_KEY_LOOKUP_TRIPLE   0x004346E0u  /* int32_t(a, b, c) */
#define ADDR_KEY_TABLE_COUNT     0x00516148u  /* int32_t */
#define ADDR_KEY_TABLE           0x00516150u  /* {uint32 key, int32 value}[] */
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
#define AM2_UNIT_TYPE_STRIDE    0x28u
#define AM2_UNIT_TYPE_COUNT     18
#define UNIT_TYPE_OFF_TROOPER   0x00u
#define UNIT_TYPE_OFF_VEHICLE   0x04u
#define UNIT_TYPE_OFF_KIND      0x08u
#define UNIT_TYPE_OFF_NAME      0x0Cu
#define UNIT_TYPE_OFF_COST      0x20u
#define UNIT_TYPE_OFF_GAME_MASK 0x24u
#define ADDR_UNIT_TYPE_COST 0x0043A5E0u  /* uint32_t(int32_t type) */
#define ADDR_PACK_KEY       0x00433810u
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
#define ADDR_SET_FACING_14     0x0043D450u  /* void(facing, src, out) */
#define ADDR_SET_FACING_08     0x0045C5E0u
#define ADDR_IS_KIND_10_17     0x0044BBF0u  /* int32_t(int32_t) */
#define ADDR_IS_KIND_14_22     0x00433500u  /* int32_t(int32_t) */
#define ADDR_CLASSIFY_CODE74   0x0040D7E0u  /* int32_t(const void *obj) */
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
#define ADDR_REMAP_BYTES       0x0041BB60u  /* void(dst, src, table, count) */
#define ADDR_SET_FIELD_IN_ALL  0x00434E90u  /* int32_t(void *record, void *v) */
/* The "Flame On!" cheat's three globals, named from the two cheat arms that
 * write them -- 0x00417E20 sets the flag and clears the clock, 0x00417EF0
 * clears the flag. The record is what SetFieldInAll points the leader's
 * weapon field at; "Flame Off!" restores what the object had saved at +0x52C,
 * which is what says the two are the same kind of thing. */
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
#define ADDR_FACING_DELTA_14   0x0043D550u
#define ADDR_MAP_CODE_18_28    0x00406A40u  /* int32_t(int32_t code) */
#define ADDR_OBJ_CODE_UNMAPPED 0x00449EF0u  /* int32_t(const void *obj) */
#define ADDR_MEETS_ALL_THREE   0x00409650u  /* int32_t(const void *p) */
/* 0x0042A240, 400 bytes and three callers. Every object in a rectangle: clip
 * the rectangle to the map (0x00514DD0 and 0x00514DD4 are its extents), turn
 * it into tile coordinates with an arithmetic shift of eight, walk the cells
 * and keep whatever the predicate accepts. The name is ours, from the body.
 *
 * The result is a list threaded through 0x0068 of each object. */
#define ADDR_OBJECTS_IN_RECT   0x0042A240u  /* obj *(const AM2_Rect *, void *, pred) */
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
#define OBJ_OFF_FIELD_530        0x530u  /* int32_t; ObjConceal compares to 5 */
#define OBJ_OFF_FLAGS          0x08u
/* The object's own sub-list: a count and an array of 0x60-byte rows, each of
 * which registers itself with the map. */
#define OBJ_OFF_ROW_COUNT      0x70u
#define OBJ_OFF_ROWS           0x74u
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
/* The sub-list header inside an object: {?, count, rows, capacity} at
 * OBJ_OFF_SUBRECORD, so the count the object reads at OBJ_OFF_ROW_COUNT and
 * the header's own +4 are the same dword seen two ways. */
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
 * the same point. It reads ADDR_REGION_OF_CELL at that tile and dispatches
 * through the function pointer at 0x00523DDC, rewriting the point; every
 * caller discards the return. The name is ours and describes the effect --
 * what the dispatch actually does is not established here. Stays original. */
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
#define ADDR_POINT_RULE_BOAT     0x00437D60u  /* vehicle kind 5 */
#define ADDR_POINT_RULE_VEHICLE  0x00437D10u  /* other vehicles, and roaches */
#define ADDR_POINT_RULE_DEFAULT  0x00437DB0u  /* everything else, and null */
/* The kind is VEHICLE_OFF_KIND, further down and already named. */
#define AM2_VEHICLE_KIND_BOAT    5
/* The map's bounds in pixels, four int32 read as one 16-byte block out of the
 * map file by 0x0042C440 and written nowhere else. Distinct from
 * ADDR_MAP_EXTENT_X/Y, which are a different pair at 0x00514DD0. */
#define ADDR_MAP_BOUNDS_LEFT     0x00514DF8u
#define ADDR_MAP_BOUNDS_TOP      0x00514DFCu
#define ADDR_MAP_BOUNDS_RIGHT    0x00514E00u
#define ADDR_MAP_BOUNDS_BOTTOM   0x00514E04u
#define ADDR_RESOLVE_FORMATION_POINT 0x00404580u /* void(follower, leader,
                                                  *      AM2_Point *out) */
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
#define ADDR_ROACH_MASK       0x00654CACu  /* the first record's POINTS;
                                                  * its count is the dword
                                                  * below, at 0x00654CA8 */
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
#define AM2_VEHICLE_MASK_KINDS     6
#define AM2_VEHICLE_MASK_DIRS      32  /* the stride of the kind index */
#define AM2_VEHICLE_MASK_MIN_SOLID 12  /* of the 64 samples -- NOT the roach's 16 */
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
#define TYPE6_REC_OFF_KIND    0x00u
#define TYPE6_REC_OFF_EXTRA   0x04u
#define TYPE6_REC_OFF_UID     0x24u
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
#define ADDR_LOAD_TYPE2       0x004471D0u  /* takes a THIRD argument */
#define ADDR_LOAD_TYPE3       0x0045A120u
#define ADDR_LOAD_TYPE4       0x0045EF50u
#define ADDR_LOAD_TYPE5       0x0043B870u
#define ADDR_LOAD_TYPE6       0x00422780u
#define ADDR_LOAD_TYPE7       0x00435500u
#define ADDR_LOAD_TYPE8       0x0043CB60u
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
#define ADDR_TROOPER_DIED_TAIL   0x00447EE0u  /* void(obj, int32), 2 callers */
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
#define ADDR_CREATE_TROOPER      0x00447620u  /* type 2 */
#define ADDR_CREATE_VEHICLE      0x0045B090u  /* type 3 */
#define ADDR_CREATE_WEAPON       0x0045F0C0u  /* type 4; names itself */
#define ADDR_ITEM_POST_CREATE    0x0043A210u  /* void(obj, int32) */
#define ADDR_CREATE_WATCHED_KIND 0x00516160u  /* int32_t */
#define MSG_CREATE_OFF_UID       4u
#define MSG_CREATE_OFF_NAME      8u    /* char[]; empty means none */
#define MSG_CREATE_OFF_TYPE      0x48u /* int16_t, 1..4 */
#define MSG_CREATE_OFF_A         0x4Au /* WORD to types 2 and 3, DWORD to 1 and 4 */
#define MSG_CREATE_OFF_B         0x4Cu /* int16_t, types 2 and 3 only */
#define MSG_CREATE_OFF_C         0x60u
#define MSG_CREATE_OFF_SUBTYPE   0x64u /* int16_t */
#define MSG_CREATE_OFF_D         0x68u
#define MSG_CREATE_OFF_E         0x6Cu /* uint8_t */
#define AM2_TROOPER_SUBTYPE_LEADS 0xA  /* then SetLeadsAndAct runs */
#define AM2_WEAPON_KEY_KIND      0x2D  /* KeyLookupTriple's first argument */
#define ADDR_STR_ITEM_GONE_SEND  0x00485E10u
#define AM2_MSG_OBJ_DESTROYED    0x10u
#define AM2_MSG_OBJ_DESTROYED_LEN 8u
#define ADDR_STR_SEND_TROOPER_WEAPON 0x0048AA60u
#define AM2_MSG_TROOPER_WEAPON   0x22u
#define AM2_MSG_TROOPER_WEAPON_LEN 0x1Cu
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
#define OBJ_OFF_CHAIN_UID          0xA8u   /* uint32_t, item: head of the chain */
#define OBJ_OFF_CHAIN_NEXT_UID     0xACu   /* uint32_t, item: the next link */
/* 656 bytes, six callers, still original -- the item-only half of the common
 * teardown, and the only callee of DestroyObjCommon without a name. */
#define ADDR_ITEM_TEARDOWN         0x00439320u  /* void(void *obj) */
/* 0x00458070, 640 bytes, 20 callers -- READ but not yet written, and the name
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
 *   - otherwise it decides a stance code into +0xE4 -- 0, 3, 6 or 7 -- from the
 *     two armies, ADDR_DEFAULT_OWNER and three comm queries at 0x0040F190,
 *     0x0040F230 and 0x0040F250, then stores the target's uid in +0xC4 and
 *     appends itself to the target's list through 0x0042A6E0.
 *
 * NOT written yet on purpose: it would need names for nine fields (+0xA0,
 * +0xA4, +0xA8, +0xAC, +0xC4, +0xCC, +0xE4, +0x52C, +0x544), three comm
 * methods and four 0x100-byte blocks at 0x004F9ACC..0x004F9DCC. The three
 * 64-byte handlers that call it need only this name and can go first. */
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
#define VEHICLE_OFF_PTR_LIST       0x538u
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
#define ADDR_VEHICLE_SEAT_BLOCKED  0x0045AC90u  /* int32(int32 seat, vehicle) */
#define ADDR_VEHICLE_DROP_OCCUPANT 0x0045E3C0u  /* void(vehicle, occupant) */
#define ADDR_DAMAGE_BROADCAST      0x0042A880u  /* void(obj,uid,int,int,pt,int) */
#define VEHICLE_OFF_KIND           0x52Cu  /* 2 and 3 skip the damage entirely */
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
 * A slot of 12 or more goes to 0x004042A0 instead, which is not reconstructed
 * and is reached by address. */
/* An 8-bit heading. Read for types 2, 3 and 8 and ADDED to a formation slot's
 * own facing before going to ADDR_COS8/ADDR_SIN8, which mask to 8 bits. */
#define OBJ_OFF_FACING             0x40u   /* uint8_t */
#define OBJ_OFF_FORMATION_SLOT     0xA0u   /* int32_t, index into the above */
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
#define ADDR_OBJ_INIT_COMMON       0x00429940u  /* void(obj,dir,type,pt,name,
                                                 *      int32,int32), 8 callers */
#define ADDR_STR_EMPTY             0x00487420u  /* "" */
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
#define ADDR_TICK_INTERVAL_MS 0x00485104u  /* int32_t, 1000 */  /* uint32_t */

/* A UID is (owner << 29) | counter, so eight owners each with a 29-bit
 * counter. These are the per-owner counters, indexed 0..7. */
#define ADDR_UID_COUNTERS   0x00511DE0u  /* uint32_t[8] */
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
#define orig_blit_bitmap_in (*(am2_blit_bitmap_in_fn)ADDR_BLIT_BITMAP_IN)
/* The object table was allocated by the game's CRT, so it must be grown by the
 * game's CRT -- our msvcrt has a different heap entirely. */
#define orig_realloc (*(am2_realloc_fn)ADDR_REALLOC)
#define orig_memmove (*(am2_memmove_fn)ADDR_MEMMOVE)

#endif /* AM2_ORIG_H */
