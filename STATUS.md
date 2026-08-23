# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-23**, at `edccf4b`+1. Working tree clean.

## In flight

Nothing uncommitted.

- **An i386 MSVC constructor returns `this` in eax, and a reconstruction that
  drops it had been killing the multiplayer path for four days.** `RecordCtor`
  (`0x00453910`) sets three fields and its body was byte-for-byte right; what
  it did not do was return. Declared `void`, it left the `value` argument in
  eax, and the caller at `0x00451473` stores that straight into the dialog's
  `0x0064` -- so the SELECT PLAYER and COMM CHANNEL lists became the pointer
  `1`, and the next `ListAdd` took the process down.

  Nothing static could see it. `make check` was green throughout, the body
  passes any reading, and `tools/checkdetour.py` says the patch site is sound.
  The only witness was `tools/ab.sh multi`, which had gone from 8 widget nodes
  to none and 291,000 differing pixels at `19282a4` and stayed that way through
  eleven commits, because nothing re-ran it. **Run the configuration that
  covers what you just touched, not the one that is quickest.**

  Found by bisecting `19282a4..HEAD` on one question -- does clicking
  MULTI-PLAYER leave the process alive -- eight builds, and then by disabling
  the commit's eight patches two at a time. The trace's last line was
  `ListAdd#1("Internet TCP/IP Connection For DirectPlay", ...)`, which named
  the list but not the pointer.

  The tell is exact and cheap: MSVC opens such a function with `mov eax, ecx`
  purely so the value survives to the `ret`. `tools/checkthis.py` resolves
  every patched address, reads the original's first two bytes, and fails if a
  function that opens `8B C1` is reconstructed as `void`. It is in `make
  check`, and tested in the failing direction by putting `void` back.

  It found a second one immediately. `InitPtrList` (`0x0042A660`) has the same
  shape and the same omission, and its caller at `0x0040A628` stores the result
  too -- `eb 02 / 33 c0 / 89 06`, the identical `jmp` past an `xor eax,eax`.
  Nothing has reached that caller yet, so this one was found before it cost
  anything.

- **The whole MULTIPLAYER OPTIONS screen is reconstructed, and it is
  DECLARED rather than built.** `OptionsDefaults` (`0x00432710`),
  `OptionsApply` (`0x004327A0`, which names itself "Options changed by host."),
  `OptionsRequest` (`0x00432830`), `OptionsSyncGroup` (`0x00432870`) and the
  two aliases in front of them, `MpDialogDestruct` (`0x004326F0`) and
  `OptionsUpdate` (`0x00432700`).

  A 43-record table at `0x004865B8`, 36 bytes each, is the entire screen: per
  checkbox an x, a y, a label, the bit it owns, which of two masks that bit is
  in, and -- for the five group headers -- the range of boxes it commands. The
  columns and the mask choice agree exactly: records 0..21 are the left column
  and `ADDR_GAME_OVER_FLAGS`, 22..42 the right and `ADDR_GAME_SETTING_22C`.

  **Its end is not the literal the original compares against, and taking it as
  one froze the game.** The loop walks with a cursor 0x18 bytes INTO each
  record, so the bound in the image, `0x00486BDC`, is 0x18 past the last
  record's base. Read as a record bound it runs one record too far and lands in
  the label strings -- `"Heavy MG Pillbox"` decoded as a widget index, which is
  a wild pointer. The first click of DEFAULT stopped the frame loop dead;
  `OptionsUpdate` froze at a fixed count and nothing else logged. The table is
  43 records, `0x004865B8..0x00486BC4`.

- **`tools/ab.sh mpoptions` is the configuration that compares it**, and it
  needed a new harness command to exist at all. The screen is reached through a
  DirectPlay session that will not open on this machine, so `poke` -- a
  one-dword write, symmetric with the `dump` that was already there -- writes
  the menu-request pair that the game's own ESCAPE handler writes. Those are
  the GAME's globals, so the same three commands drive both sides and
  `AM2_NOPATCH=1` takes them unchanged; that is what makes it an A/B rather
  than a demonstration.

  One more poke earns its keep: `comm+0x3D8` is the host flag, and without it
  the panel is read-only with CANCEL alone. Set, OK and DEFAULT appear and the
  three interesting functions become clickable. The run ends on the lobby with
  "Options changed by host." in the comms panel, which is the apply's own last
  line and about 360 pixels of menu text -- comfortably over the budget of 200,
  so an apply that stopped short would be caught.

  Counters from one traced run: `OptionsUpdate` 26,355, `OptionsApply` 1,
  `OptionsDefaults` 1, `OptionsSyncGroup` 1, `OptionsRequest` 1,
  `MpDialogDestruct` 2. `Announce` reads 0, which is the usual blind spot --
  `OptionsApply` calls it directly.

- **`mpoptions` failed on its first run and found a real defect, and it was
  ours after all -- just not from this work.** `MakeBitmap` reserved the first
  ten palette entries when `BMP_FLAG_RESERVE10` was CLEAR. The original
  reserves them when it is SET.

  The symptom was 918 pixels in the lobby's map preview, x 338..523 /
  y 272..457 and nothing else on the frame. Four colour pairs, 888 of them the
  original's `(0,0,128)` against our `(0,128,128)`, the rest `(128,0,128)` and
  `(192,192,192)` -- VGA entries 4, 6, 5 and 7, all inside the ten Windows
  reserves. Both sides were choosing from that block; ours was allowed to and
  the original's was not.

  How the sense got lost is worth keeping. The original holds it in ONE
  register and reads it both ways round: `ebp` is `(flags & 0x80) == 0`, the
  branch at `0x0041BEDE` jumps PAST the identity fill when `ebp` is non-zero,
  and the `or al, 0x10` at `0x0041BFBD` tests the same register the other way.
  Our transcription got the second right and the first backwards, so the two
  halves of one flag disagreed. It is now written from the FLAG rather than
  from the register, with the `?:` arms swapped so that half stays identical.
  **When the original reuses a register for a predicate, decide what the flag
  means once and write every use from that.**

  Measured, not argued. Taking the lobby frame directly on both sides went
  from 918 to **50**, which is the cursor; the same screen's own animation
  measures at most 54 over four seconds. Then the full configuration: 918 to
  **0**, on all three of its frames, with 42 widget nodes and 35 log messages
  identical. `bootcamp` stays at 22 and `windowed` stays pixel-perfect, which
  is what says the inversion cost nothing elsewhere -- every sprite in the game
  goes through `MakeBitmap`.

  The sibling site is right, which is the contrast worth keeping: the tileset
  loader in `mapdraw.cpp` reserves when the global at `0x00511CC8` is non-zero,
  and `0x0042C215` really does skip the fill on zero. Two remaps of the same
  shape, two different conditions, and only one of them inverted.

  It survived this long because nothing had ever reached that screen. Boot Camp
  is clean either way, which says the flag is clear for everything it loads;
  the preview is the first bitmap in the project that sets it. Two things
  follow. `NearestPalIndex`'s `from` guard, which CLAUDE.md records as never
  having been discriminated by any configuration, IS discriminated here -- it
  was doing its job and being handed the wrong threshold. And a whole-frame A/B
  that never visits a screen says nothing about it: this defect sat behind
  every green run in the suite.

- **The first three menu screen factories are reconstructed**: `OpenMpHost`
  (`0x004317C0`), `OpenMpJoin` (`0x00433480`) and `OpenMpOptions`
  (`0x00432910`). One shape -- close the current screen, allocate, construct,
  store the constructor's return -- and the store is the RecordCtor lesson
  again, which is why this family was worth taking next.

  The host and join panels are ONE class: both allocate 0x278 and call the
  same constructor at `0x00430530`, differing only in backdrop and in what
  they write to `ADDR_MP_SESSION` -- 1 and 2. Driving menu request 9 puts up
  "MULTIPLAYER JOIN PANEL" where 7 puts up "MULTIPLAYER HOST PANEL", from the
  same vtable. That confirms by running what `orig.h`'s comment on that global
  had worked out by reading; what stays open is the title screen also writing
  2, which host-versus-client still does not explain.

  They went into `widget.cpp` rather than a `screens.cpp` of their own, and
  the ratchet is why. A separate module names no Win32 or COM type at all, so
  `tools/checksplit.py` refused it -- correctly. The flat-half alternative
  would have meant writing the vtable call against `void **` instead of
  `AM2_WidgetDeleteFn`, which is exactly the private signature that hid the
  `PlaySoundAt` defect. The rule pointed at the right answer both times.

  Counters from one driven run: `OpenMpHost` 1, `OpenMpOptions` 1,
  `OpenMpJoin` 1, each with the right screen on the frame.

- **`RefreshScreen` is what a menu screen opened DURING a mission calls, and
  that answers a standing open item.** CLAUDE.md lists it as unexercised with
  seven callers and says "whatever forces an out-of-band repaint is somewhere
  further in". It is the screen factories: four of them -- AUDIO, DELETE GAME,
  LOAD GAME and the two-branch pair beside them -- open with
  `cmp [ADDR_GAME_STATE], 2`, and the state-2 arm calls `RefreshScreen` before
  it allocates.

  So the two arms are the same screen in two contexts. In a mission it gets
  its own backdrop and a flag of 0, and the frame under it has to be repainted
  because a mission is not a menu; on the title screen it gets the shared
  backdrop and a flag of 1 and no repaint is needed. That is also why nothing
  in the suite has ever run it: every configuration that opens a menu screen
  does so from the title.

  **The obvious probe does not work, and that is worth knowing before anyone
  else spends an hour on it.** Driving Boot Camp to live play, confirming
  `ADDR_GAME_STATE` reads 2, and then poking the menu-request pair leaves
  everything untouched: `RefreshScreen` 0, `OpenAudioOptions` 0, no dialog,
  and the state still 2. Nothing in ordinary state-2 play consumes that pair
  -- the consumer is `0x00425EE0`, which is the in-mission ESCAPE arm, and
  that is sub-state 34 where ordinary play sits in 33.

  So the state-2 arms of those four factories are verified by reading, and
  what is missing is not a way to poke but the CONTEXT in which the menu
  dispatcher runs with the game state at 2. Find that and `RefreshScreen`
  runs for the first time in this project.

- **The `args` column in `docs/screens.md` earned itself immediately.** These
  constructors are thiscall, so the CALLEE pops: reconstructing a two-argument
  one with a single argument corrupts the stack rather than merely painting
  the wrong screen. Sixteen of the twenty-one take one argument and are
  reconstructed; the remaining five take two, and they are exactly the five
  that are not. That correspondence was not designed -- the batch was chosen
  by "has no branch and no extra call" and the argument counts agreed
  afterwards.

- **`tools/checkseams.py` had two blind spots and the second one hid eight
  live seams.** It knew two spellings of "reach our own code through the
  image": an `orig_` macro on one line, and `callN(ADDR_X)` at a call site. It
  did not know the third -- a cast around `AM2_IMAGE(ADDR_X)` written inline at
  the point of use, which is the same function pointer without a name on it.
  Nor did it know a two-LINE `#define orig_x \` ... `AM2_IMAGE(ADDR_Y)`,
  because its regex wanted both on one line.

  Found because a seventh instance appeared and only that one was reported --
  it happened to have been given a name. Teaching the check the third spelling
  found **eight more, none of them from this work**: `ListRemoveAt` in
  `army.cpp` and `event.cpp`, `ArmyMessageSend` in `event.cpp`, and five
  comparator pointers handed to `bsearch` in `defparse.cpp`. Every one was a
  call into our own reconstruction routed through a detour. All closed.

  A ratchet only guards the spellings it knows. When one fires, ask what else
  would have looked the same and not fired.

- **The CHECKBOX constructor is reconstructed, and writing it found a defect
  in code that had already passed an A/B twice.** `0x00454640`, 255 B.

  **Its left-click action is the constructor's, not the caller's.**
  `ADDR_CHECKBOX_TOGGLE` goes into `BUTTON_OFF_ON_LEFT` unconditionally and
  the caller's handler goes to a separate change slot -- which is why clicking
  a plain box only ticks it while a group header also disables its group.
  Both run the same `OptionsSyncGroup`; what differs is the RECORD INDEX the
  constructor stored.

  And that index is the ninth argument, which `MpOptionsConstruct` was passing
  as a literal **0** -- I had read the original's `push ebp` as a constant
  when ebp is the loop counter. Every checkbox got group 0.

  **`mpoptions` passed anyway, twice, at 42 nodes and 0 pixels**, because it
  clicks POWER-UPS -- and POWER-UPS *is* record 0, so the wrong index synced
  the right group by accident. The configuration now clicks MISCELLANEOUS,
  record 17, where only the right index reaches the right group. A test that
  can only pass is worth as little as one that cannot fail, and this one could
  only pass for one specific input.

- **The LIST BOX constructor is reconstructed** (`0x00454F90`, 209 B), and it
  is where the row height finally appears. **A row is FOURTEEN pixels tall**,
  and that number is written nowhere in the image: it comes out of a
  magic-number division, `LIST_OFF_VISIBLE = (height - 4) / 14`, spelled
  `imul 0x92492493` then `sar 3`.

  **The constant alone does not say the divisor.** `0x92492493` serves 7, 14
  and 28; what picks between them is the SHIFT. I read the constant, wrote 7,
  and every list drew twice as many rows as it had room for -- seven map names
  on a lobby that shows four. Caught by `mpoptions` on the very next run, and
  only because that run also changed which header it clicks. Recognising a
  magic number is half of reading one.

  Its hot row starts at **-1** -- nothing under the pointer -- and becomes 0
  only if the rows it was handed are non-empty, testing the pointer and then
  the count. The selected row is 0 either way. So an empty connection list
  opens with nothing hot and a populated one opens with the first row hot,
  which is what DIFFICULTY's green bar and the player list's highlight are.

- **The EDIT BOX constructor is reconstructed** (`0x00454C10`, 137 B) and it
  has **thirteen stack arguments**, `ret 0x34` -- the longest list in the
  widget hierarchy: the buffer, the maximum, four of rectangle, a font, three
  colours, the RETURN handler and two more that every call site passes as
  zero. Its seventh argument is the FONT, as the key row's is, and not a flag.

  **The two character sets are a default and an override, not two tables.**
  The constructor installs the permissive one from `0x00485304` -- with
  `` ` ~ ! @ # $ % ^ & `` in it -- and ENTER BATTLE NAME then overwrites
  `EDIT_OFF_CHARSET` with the letters-and-digits set at `0x00485308`. Reading
  the constructor is what turned two unexplained pointers into one mechanism.

- **The MULTI-SPRITE constructor is reconstructed, and the contradiction it
  seemed to raise was mine.** `0x00456BC0` writes the first bitmap's sprite to
  0x0060 and the second to 0x0064, which looked like it put the array one slot
  earlier than `widget.h`'s note says.

  **The PAINTER settles it, and the painter is A/B-verified**: it reads
  `MULTISPR_OFF_SPRITES + index * 4`, base 0x0064. The note was right. The
  constructor simply has one slot in FRONT of the array, holding the first
  bitmap, which the painter never reads.

  Which makes the widget's behaviour legible for the first time: the SECOND
  bitmap goes into sprites[0] and sprites[1] is left null, so an index of 0
  shows a dot and an index of 1 shows nothing. **The null is the off half of
  the blink**, not an unfilled slot. Reading the consumer beats reasoning from
  the producer, and stopping a turn early to say so cost one turn and no
  wrong code.

- **The SCREEN BASE and the BUTTON are reconstructed** -- `0x00454B00`
  (106 B) and `0x004540F0` (203 B), the two most-executed constructors in the
  menu layer. Every screen starts at the first and nearly every one uses the
  second, so every configuration in the suite runs both.

  The screen base is just a PANEL over the whole display with the dialog
  vtable stamped on top, and its rectangle is `(0, 0, ADDR_SCREEN_W,
  ADDR_SCREEN_H)` -- 640 by 480 READ FROM THE IMAGE rather than written down,
  which is why a backdrop covers exactly the display and not a constant
  somebody chose.

  The button is where the reading needed care. **Only the FIRST of its three
  bitmaps is tested for null**, and a null there also sets 0x0048, which
  `WidgetRepaint` reads as "defer to an ancestor" -- so a button with no
  sprite of its own is drawn by whatever contains it. The other two go through
  unconditionally, so a null there would be stored as a null sprite rather
  than caught. And the normal sprite is copied to the base's own 0x0038 as
  well, the same doubling the panel does.

  With these two closed, the whole chain from a menu request down to a
  rectangle is ours: factory, screen constructor, panel, button, and the
  key row.

- **The key-capture ROW is reconstructed** (`0x00450C50`, 106 B), and **it
  passes an UNINITIALISED byte to the label constructor.** `mov al, byte ptr
  [esi + 0x64]` at `0x00450C5D` reads the object's own 0x0064 before anything
  has written it -- the memory is straight out of `operator new` -- and hands
  it over as the label's ink.

  It is harmless, and knowing WHY took looking rather than assuming, which is
  why this one waited a turn. The label's ink is at 0x0060, not 0x0064, so the
  garbage lands there; the focus label overrides it with its own pair at
  0x0064 and 0x0065, which this function then writes from its arguments.
  Nothing on this class reads 0x0060. Reading it back is faithful and, through
  a `uint8_t *`, is not the undefined behaviour it would be through a wider
  type -- which is the distinction that made it worth checking first rather
  than either reproducing blind or quietly "fixing".

  Two more readings fell out of the argument map: the seventh argument is the
  FONT, and the tenth is used twice -- as the label's paper AND as the colour
  at 0x0066.

- **The PANEL constructor is reconstructed** (`0x00454980`, 136 B) -- the
  container eight of the reconstructed screens hang everything off, and so the
  most-used constructor in the family and the cheapest to check: every
  configuration that opens a dialog draws one.

  **It keeps the sprite twice**, at 0x0038 and 0x0058. 0x0038 is the base
  class's own field -- what `WidgetPaint` draws and what `WidgetRepaint` walks
  the parent chain looking for -- and 0x0058 is the panel's. Reproduced rather
  than collapsed: something reads one of them and nothing here establishes
  which.

  It also named `0x00445CF0`, which is not `PreloadSprite` but a wrapper: it
  splits a bitmap NAME into set, index and frame through `0x0042E310` and then
  calls `PreloadSprite` with the three numbers. That is how a screen can name
  `03_017_00_check.bmp` where the sprite layer wants integers.

- **SELECT PLAYER is reconstructed** (`0x00451400`, 1,247 B) -- the one screen
  whose rows come off the FILESYSTEM rather than from a table or the comm
  object. It chdirs to `save` and walks it with the CRT's `_findfirst` /
  `_findnext`, taking every entry that is a DIRECTORY and whose name does not
  begin with a dot, which is how "." and ".." are skipped without comparing
  whole names.

  Then, once the list exists, **the first row's name is copied into the
  current-player string** at `ADDR_GAMEPROC_BLOCK` -- so opening this screen
  selects a player whether or not anyone clicks. That the campaign still
  reaches MAP 01 is what says the copy works; the widget tree alone would not.

  Reconstructed the turn after the dump that made it comparable, which is the
  order that costs least.

- **`ab.sh campaign` compares the SELECT PLAYER tree now**, ten nodes, and
  until this it compared that screen not at all -- it drove straight through
  on its way to the map and compared only the log and a live-play frame. The
  three defects this session that only `ctl widgets` could name were all on
  screens that HAD a tree dump; this closes the largest screen that did not.

  No screenshot goes with it, deliberately: `campaign`'s pixel budget is -1
  because it ends in live play, and a frame compared against a check that
  cannot fail is not a check.

- **COMM. CHANNEL SELECT is reconstructed** (`0x0042E9C0`, 760 B). Its rows
  come from our own `CommEnumConnections` into a record built by our own
  `RecordCtor` -- with a flag of 1 where DIFFICULTY passes 0.

  **Its list box takes `ADDR_LOG` as a callback, and that is not a mistake in
  either direction.** `orig.h` already records that the linker folded an empty
  virtual and the stubbed varargs logger onto one address, because both are a
  single `ret` byte. Passing it here means "no callback" -- and it is passed
  as the literal address rather than as a null, which is what the original
  does and what we reproduce. A reconstruction that "cleaned it up" to 0 would
  be a behavioural change nobody could see until something read the slot.

  The bar is the arrow-ended one, `ret 0x24`, and the list and bar point at
  each other afterwards -- the list at 0x007C and the bar at 0x0058.

- **ENTER BATTLE NAME is reconstructed** (`0x0042FB00`, 1,082 B), and it
  answers a question the multiplayer code raised: **the two fields edit the
  DIALOG's own buffers in place.** The constructor copies
  `ADDR_SAVED_BATTLE_NAME` into its 0x0064 and `ADDR_SAVED_PLAYER_NAME` into
  its 0x0084 before any widget exists, and hands the edit boxes those
  addresses. That is why `HostBattle` can read the names back out of globals
  afterwards without the dialog passing them anywhere.

  `ret 0x34` on the edit constructor is 52 bytes: buffer, a maximum of 0x18
  characters, sixteen of rectangle, a flag, three colours, a handler and two
  zeroes. **The handler is `ADDR_HOST_BATTLE`, the same function the OK button
  gets** -- so RETURN in either field starts the battle.

  **Two defects, both named by `ctl widgets` and neither visible in a pixel
  count.** The dialog's `focusedChild` is the PANEL, set at `0x0042FC04` right
  after the panel is added -- I had left it unset, which the tree reported as
  `foc=2` against `foc=-1` in its first line. And the focus SLOT is called on
  the first field only, at `0x0042FC9C`, with no such call in the second
  block; calling it for both left the wrong field marked dirty. The frame was
  180 pixels either way, inside the budget.

  That is now three defects in this session that the tree named and the pixels
  could not, against one the pixels named and the tree could not.

  Two smaller findings. `0x00485308` points at
  `" abcdefghijklmnopqrstuvwxyzABC...0123456789!..."`, which every edit box
  takes as `EDIT_OFF_CHARSET`: the field is a WHITELIST, not a length limit.
  And this dialog's buttons are 0x4E wide where every other screen's are 0x51,
  which is why it does not share `MakeButton`.

- **The AUDIO dialog is reconstructed.** `0x0044F370`, 1,208 B, `ret 8` -- two stack arguments, the backdrop
  and the flag, which is what its factory's two-argument call already implied.

  It branches on `ADDR_GAME_STATE` like its factory does, and the branch is
  structural rather than cosmetic: **in a mission there is no panel at all**.
  The dialog itself becomes the parent and the three bars carry an offset of
  (0x89, 0x79); with a panel the panel sits at that position and the bars are
  placed relative to it, so the offset is zeroed. Two stack slots hold that
  offset and are reused for other things afterwards, which is what makes the
  listing hard to follow.

  Each bar is `new(0x80)` then `ScrollBarCtor(bar, rect, parent, 0x92)` --
  `ret 0x18`, so rectangle by value, parent, and a maximum. The rows are at
  +0x38, +0x7D and +0xC2 from the offset, all at x +0x25, 0xBA by 0x15. The
  three are stored on the dialog at 0x0064, 0x0068 and 0x006C, their
  on-change handlers are `0x0044F2A0`, `0x0044F2E0` and `0x0044F320`, and the
  dialog saves the current volumes at 0x0070 and up so CANCEL can put them
  back.

  The position arithmetic is the part to be careful with. `bar[0x74]` comes
  from the volume by a MAGIC-NUMBER DIVISION -- `imul 0x51EB851F` then
  `sar 5`, which is `(volume + 2000) / 100` -- and is clamped at zero. The
  thumb at `bar[0x6C]` is then x87: `fild bar[0x74]`, `fidiv bar[0x78]`,
  `fmulp` against `bar[0x70] - bar[0x64][0x1C]`, and `_ftol` (`0x00464490`).
  `long double` reproduces the 80-bit intermediate, as it does for
  `SetMaxHealth` and `Ticks`.

  The three volume globals were already named: `ADDR_VOLUME_AT_ZERO`
  (effects), `ADDR_STREAM_VOLUME` (music), `ADDR_VOLUME_VOICE`.

  It is the best-checked of the screen constructors, because `audiovol` does
  not only open the dialog -- it nudges the SOUND EFFECTS thumb four times.
  The thumb position IS the x87 arithmetic, and STATUS already records that
  dropping the thumb offset from `ScrollBarPaint` moves 336 pixels on that
  frame. So the division-then-multiply order is compared and not merely
  reasoned about. 13 nodes identical, all three frames at cursor noise.

- **MULTIPLAYER OPTIONS is reconstructed** (`0x00432320`, 968 B): 43
  checkboxes built from the declarative table, then three buttons -- or one.

  Three things depend on being the host, and they are exactly what the two
  panels differ by. A non-host gets `unknown4C` on every box, so none can be
  focused; a non-host gets CANCEL alone, at the OK position; and the pass that
  disables a group whose header is unticked runs for the host only.

  The original walks the table with a cursor four bytes IN, so every field
  offset in the disassembly reads four low and the loop bound is `0x00486BC8`
  where the table ends at `0x00486BC4`. Written here from the record base,
  which is why the numbers do not match the listing on sight -- the same
  cursor offset that made the 43rd record look like a 42-record table when
  `OptionsApply` went in.

- **The CONTROLS dialog is reconstructed** (`0x00450E10`, 689 B) -- the only
  screen in the game whose children come out of TABLES rather than being
  written out one at a time. Three walk together: `ADDR_KEY_BINDINGS` for the
  scancode each row is bound to (a `uint8_t[][2]` walked one byte at a time
  with a stride of two, which is why the loop bound is on the pointer and not
  a counter), `ADDR_KEYROW_POSITIONS` for the row's x and y as int16s, and the
  caption out of `ADDR_KEY_NAME_TABLE`'s second field, selected by our own
  `KeyNameIndexOf`.

  **`ret 0x2C` on the row constructor is 44 bytes** -- index, caption, sixteen
  of rectangle, a flag and four colours. Three of the colours are pushed as
  whole dwords from BYTE loads, so their top three bytes are stale stack, the
  same matched-argument shape `MakeBitmap` has. Safe for the same reason and
  checked rather than assumed: `0x00450C8E` reads all three back as
  `mov al, byte ptr`, so a zero-extended byte is faithful.

  **The defect it shipped with is one the widget tree could not see.** The
  three buttons' flag is the `push 1` in the middle of the block, not the
  `push 0` at the top -- that one is the TRAILING argument. Reading it as the
  flag left the buttons one palette step off: 547 pixels on the dialog frame,
  with the tree **identical at all 25 nodes**. The exact reverse of the
  OPTIONS menu's focus bug an hour earlier, where the tree named it and the
  pixels only said "something". The suite needs both and this pair of defects
  is why.

- **The DIFFICULTY dialog is reconstructed** (`0x0044E730`, 786 B): the
  confirm-dialog shape with a LIST BOX where they have a message.

  It builds its rows with `RecordCtor` and `ListAdd`, both ours -- so the
  record whose missing return took the multiplayer path down for four days is
  now constructed by our code on a screen the suite drives every run.

  Two of the list's fields are seeded from `ADDR_DIFFICULTY` and which two is
  the interesting part: `LIST_OFF_SELECTED` **and** `LIST_OFF_HOT`. The dialog
  opens with the current setting both selected and highlighted rather than
  merely selected, which is the green bar on Medium in a default install. The
  list is also kept on the DIALOG at 0x0064 -- the constructor reaches it
  again twice -- and the blinking dot is stored on the LIST at 0x0094, not on
  the panel that owns it.

- **The three CONFIRM dialogs are one body three times over** -- CONFIRM GAME
  EXIT (`0x0044EB50`), the replay prompt (`0x0044EED0`) and DELETE PLAYER
  (`0x00450730`), 685 bytes each. They differ in five things and nothing else:
  the vtable, the panel's bitmap, the OK handler, the message, and -- for
  DELETE PLAYER alone -- the CANCEL handler. Written once with those five as
  arguments.

  The shape is the thing to know before reading any of them: **the dialog gets
  ONE child, a panel, and everything visible is a child of the PANEL** -- both
  buttons, the typewriter message and the blinking red dot beside it. The
  panel is also what carries the focus, not the dialog.

  Three more widget constructors came with them, and every `ret N` was checked
  rather than inferred from the pushes: the panel's `0x00454980` is `ret 0x18`
  (bitmap, flag, sixteen bytes of rectangle), the typewriter's `0x004566F0` is
  `ret 0x14` (rectangle then message), the two-sprite dot's `0x00456BC0` is
  `ret 0x1C` (two bitmaps, a flag, a rectangle). The rectangle is by value in
  all three, as it is for the button.

  **Three stores are unguarded in the original and are reproduced that way.**
  `panel->flag44`, `panel->focusedChild` and the message's blinker field are
  all written after the allocation was tested and found null on the failure
  path, so a genuine out-of-memory faults there. VC6's `operator new` answers
  null rather than throwing and this game checks it everywhere else, which is
  what makes these an oversight rather than a convention -- but reproducing
  them costs nothing and diverging would be a silent behavioural change.

- **The alias ratchet caught its author again.** All three constructors were
  already named in `orig.h`, from the batch that reconstructed their
  factories, and I gave them second names. I *did* grep the addresses first --
  and grepped the four new callees and the vtables and the string, and left
  the three constructor addresses out of the list. Checking a rule and
  checking every case of it are not the same thing.

- **The first SCREEN CONSTRUCTOR is reconstructed**, `OptionsMenuConstruct`
  (`0x0044FAB0`): a backdrop and four buttons, AUDIO / CONTROLS / DIFFICULTY /
  BACK. The factories were the easy half of this layer; the constructors are
  where the widgets actually come from, and they run 580 to 1,400 bytes each.

  **The rectangle goes to the button constructor BY VALUE, in the middle of
  the argument list**, and the original builds it in place: it pushes the four
  numbers as placeholders, hands RectSet a pointer to them, and overwrites the
  same four slots with what RectSet returns. What settles the reading is not
  the shape of the pushes but `ret 0x28` -- 40 bytes, being three bitmaps, a
  flag, sixteen bytes of rectangle, a handler and a trailing zero.

  And the four numbers are (left, top, WIDTH, HEIGHT), not the four edges the
  type says. `ctl widgets` puts the buttons at 231,160,383,185 and three rows
  below, which is 0xE7 and 0xE7 + 0x98, 0xA0 and 0xA0 + 0x19 -- so the button
  constructor is what turns them into edges. Measured: RectSet stores what it
  is given and cannot tell the difference.

  Written as a table where the original unrolls four copies, because they
  differ in exactly three things and four copies of eleven lines would hide
  that -- **and that is exactly how the one real defect got in.** The FIRST
  button is stored as the dialog's `focusedChild` and only the first, one
  instruction sitting inside block one; a loop over what the four blocks have
  in common drops precisely the line they do not share.

  What NAMED it is `ctl widgets`: `foc=2` on the original against `foc=-1` on
  ours, in the first line of the tree. The pixels said something was wrong --
  294 on the menu itself, AUDIO coming up plain instead of highlighted, over
  the budget of 200 but not by much -- and 305,895 on the dialog frame,
  because with no focused child the click had nowhere to land and CONTROLS
  never opened at all. Neither number says WHICH field; the tree does.

  Compressing repetition is right. Check what the repetition is hiding first.

- **The arm index IS the menu request, confirmed three times by the buttons
  themselves.** The OPTIONS menu's handlers raise 15, 16 and 19 for CONTROLS,
  DIFFICULTY and AUDIO -- which are those screens' arm numbers in
  `docs/screens.md`. The table confirms its own indexing, and the poke that
  reached the host panel with request 7 was not a lucky guess.

- **Two ratchets fired on the commit that landed the last four, and both were
  right.** `checkseams.py`: `RefreshScreen` is already reconstructed, so
  reaching it through an `orig_` macro was a lie about where control goes --
  it would have made a closed seam read as open. `checkglobals.py`: a `const`
  on `g_gameState` that `gameproc.cpp` and `winproc.cpp` do not have.

  Both fixed in the commit after. Worth saying plainly: `make check` was run
  before the change and after the build, but not between the last edit and
  `git commit`, and that is the gap. The ratchets caught it within the minute;
  the discipline they exist to enforce did not.

- **Twenty of the twenty-one screen factories are reconstructed.** The last
  four were the two-argument ones: COMM CHANNEL SELECT (`0x0042EE40`), AUDIO
  CONTROLS (`0x0044F9E0`), DELETE GAME (`0x00450250`) and LOAD GAME
  (`0x00452680`).

  **Where the repaint goes is not the same in all of them, and it was worth
  not tidying.** AUDIO and DELETE GAME call `RefreshScreen` BEFORE they
  allocate; LOAD GAME calls it AFTER constructing and publishes the screen
  only then. Reproduced as written. Whether the ordering matters is not
  something reading settles, and all three screens are reachable, so it can be
  measured rather than argued about.

  COMM CHANNEL SELECT is the one factory that does work before allocating
  rather than around the branch: `CommCreateDirectPlay(comm, 0)`. That literal
  zero is the one `orig.h` already records as making `CommOnConnected`
  unreachable in this build, and it now comes from our code.

  **The twenty-first is not a factory.** `0x0044D730` is the TITLE SCREEN and
  it is 1,108 bytes: it chdirs to `shared`, tears the comm object down, clears
  three globals, and then builds every button on the screen. It belongs with
  the dialog constructors, not with this family.

- **The thirteen plain factories went in as one batch**, generated from the same
  measurements `tools/screens.py` reports rather than transcribed: SELECT MAP,
  SELECT PLAYER, ENTER NAME, the CD prompt, ENTER BATTLE NAME, CHOOSE A
  BATTLE, MOVIES, the OPTIONS menu, CONTROLS, DIFFICULTY, CONFIRM GAME EXIT,
  the replay prompt and DELETE PLAYER.

  Three of them named themselves through their captions rather than a bitmap
  -- "Are you sure you want to quit?", "Do you wish to reattempt your failed
  mission?", "Caution: All saved games for this player will also be deleted!"
  -- and one through both: `0x0042F440` pushes "Copy Protection" and "The
  ARMYMEN2 CD must be in the drive to play Army Men II.", so the menu table
  has a CD prompt in it that is a screen rather than one of the five patched
  `MessageBoxA` sites.

- **`docs/screens.md` is the table, generated.** `tools/screens.py` reads all
  21 arms out of the jump table and reports each factory's allocation size,
  constructor and screen. Nineteen name themselves; the other two are
  identified by their BUTTONS rather than given a name borrowed from whichever
  function sits next in the image.

  Three things it had to learn, each a mistake this repository has recorded
  before. **Find the constructor from the STORE**, not from the first
  `operator new` -- two arms allocate something else first, and one of those
  constructs a helper that has nothing to do with the screen. **Decode
  forward**: walking back byte-wise from the call for a `68`/`6A` finds other
  instructions' operands and reported sizes like `0x8800511A`, which is the
  aligned-dword cross-reference mistake in another costume. And **a fixed
  window cannot bound a function** -- 0xC0 bytes was too short for two arms
  and reading past the `ret` named four screens after their neighbours, giving
  DELETE GAME twice.

- **`tools/ab.sh` has a fourth comparison slot.** It compared the final frame
  plus `mid` and `dlg`; `alt` is the fourth, and `mpoptions` now uses all of
  them -- the host panel, the options dialog before and after DEFAULT, and the
  join panel are four different screens on one run. Without it `OpenMpJoin`
  would have been run rather than compared.

- **`tools/ab.sh` took only its first argument.** `cfgs="${1:-bootcamp}"`, so
  `ab.sh bootcamp controls` ran bootcamp alone and printed "A/B clean" -- which
  reads as both configurations passing. It is `"${*:-bootcamp}"` now. Same
  family as the two missing files that once diffed as identical: a check that
  can report success on work it did not do.

- **The menu is a 21-entry table of screen factories, and none of them is
  named.** `RunFrame`'s menu-request arm dispatches through a jump table at
  `0x00426518`; every arm is seven bytes -- `call <factory>; jmp end` -- and
  every factory is the same shape: destroy whatever dialog is the repaint
  object, allocate, construct on it, store the CONSTRUCTOR'S RETURN into
  `0x0065A058`. That last is the RecordCtor lesson again, twenty-one times.

  Seven name themselves from the bitmap the FACTORY passes -- LOAD GAME
  (`0x00452680`), MP HOST (`0x004317C0`), MP HOST OPTIONS (`0x00432910`),
  MP JOIN (`0x00433480`), CONTROLS (`0x00451210`), AUDIO (`0x0044F9E0`) and
  DELETE GAME (`0x00450250`). The other fourteen open on the shared
  `01_000_00_screen.bmp`, and reading the CONSTRUCTOR's bitmaps instead names
  nine more: SELECT MAP `0x0044DF20`, SELECT PLAYER `0x00451910`, ENTER NAME
  `0x00451E10`, CHANNEL HOST `0x0042F440`, ENTER BATTLE NAME `0x0042FF60`,
  JOIN `0x0042F880`, MOVIES `0x0044E6A0`, the OPTIONS menu itself `0x0044FDD0`
  and DIFFICULTY `0x0044EAD0`.

  Three more -- `0x0044EE50`, `0x0044F220`, `0x00450B70` -- carry only OK and
  CANCEL, so they need their captions read rather than their bitmaps. Two,
  `0x0044D730` and `0x0042EE40`, push no bitmap at all and are probably not
  dialogs; check what they are before assuming the table is homogeneous.

  A good next batch: it is one shape repeated, it gives twenty-one screens
  real names from the program's own vocabulary, and `mpoptions`, `controls`,
  `audiovol` and `difficulty` between them already drive four of the arms.

- **The poke/key asymmetry stands and is not an oversight.** `poke` writes a
  global the game ACCUMULATES, so the write survives and becomes the next
  starting point. There is still no way to set the key buffer, because
  `PollKeyboard` replaces it wholesale every poll. Same reasoning as `cursor`.

- **`COMM_OFF_READY` was `COMM_OFF_IS_HOST` under an invented name.** Both
  named `0x3D8`; the first came from a call site, the second from `DPCAPS_ISHOST`
  and has provenance. `startgame.cpp` sets it to 1 when hosting and 0 when not,
  which settles it. Gone, along with a `const` drift on `g_gameOverFlags`.

- **Four reconstructions had never been installed.** `dist_install` opened
  with `return patch_replace(...)` and had three calls under it;
  `savetag_install` had one. Every tool that reads the sources counted them
  done; the game's own log printed one `patch:` line where there should have
  been four. So `ApproxDistXY`, `AngleDelta`, `RoundTo8` and `WriteSaveTag`
  had never run once, and every A/B that "covered" them compared the original
  against itself. Fixed, and `tools/checkpatches.py` now fails on the shape.
  The three arithmetic ones went live immediately: `ApproxDistXY` 58,
  `AngleDelta` 1,252, `RoundTo8` 4,876 in one Boot Camp mission, `bootcamp`
  clean. `WriteSaveTag` still reads 0 because nothing saves in that run.

- **A whole setter family is reconstructed and never runs.** A counts probe on
  the campaign reads `EvtSetField540=0`, `EvtSetByte530=0` and
  `LookupType3ByUID=0`; of the family only `EvtSetByte40` fires, 4 times. They
  are arms of the action executor at `0x00420410`, so which ones run is decided
  by which actions the shipped scripts use, and `kitchen1.txt` uses almost none
  of them. **The clean campaign A/B says nothing about any of them** -- it
  establishes only that nothing else broke. Verified by reading.
  `blindspots.py` confirms these counters can move, so the zeroes are real
  rather than the usual blind spot.
- **The pure-leaf pool is nearly empty**: 14 pure unreconstructed leaves left,
  from the 161 this project started that count at. Three of the 14 are false
  positives -- see Leads.
- **Six holes closed in the vector harness, all found by mutation.** The
  newest is a lockstep: a seeded field and an argument set of the SAME list
  length move together, so `CommRemoveKeyed` never met the record that
  exercises its clamp. Co-prime lengths, the lesson `vectors.py` already
  carried for SEED periods. The
  newest: a `u32v` seed varies on a period fixed by its position in the chain,
  so a table with answers at BOTH ends could not be aimed at -- `ObjCodeUnmapped`
  hit 100% coverage with two of its five zero answers never produced. `u32s`
  takes an explicit value set, as `ARG_VALUES` already does for arguments. The
  correlation heuristics -- copy one argument to another, set one to -1 -- were
  overwriting explicit `ARG_VALUES` sets, so `MaskPixelSolid32` reached only 4
  of 55 (x, y) combinations and could not tell `acc >= x` from `acc > x`. Every
  pointer argument took its NULL from one shared decision, so they were null
  together and never one at a time -- which made `RemapBytes`' whole copy path
  unreachable. And the scratch fill had period 256 while `PTR_STRIDE` is
  `0x800`, so **every pointer argument's region held identical bytes** and a
  copy from src to dst changed nothing observable; every copy-like function in
  the set had been checked against indistinguishable buffers. Both fixed at
  `bbefc4e`, and no existing reconstruction was relying on either.
- **The vectors now check for writes the original did NOT make.** They only
  ever asked whether the expected writes happened, so a reconstruction that
  scribbled elsewhere passed. Found by a `ListUnlink` mutation that should have
  failed and did not. Closing it immediately found a real defect:
  `SetFacing14` and `SetFacing08` set a dword on facing 1 that the original
  sets only on facing 3, and had done since they were written. Fixed at
  `1b6d541`.

## Where the work is

The **Win32/DirectX boundary phase is finished** -- `docs/boundary.md` reports
0 outstanding on every channel it can see: named imports, imports by ordinal,
COM vtables, runtime resolution, delay imports. The three `MessageBoxA` sites
left are a decision, not an omission: all three sit behind CD checks this build
has patched to jump past them.

The front has moved into **game logic**, and the current front is savegame
serialisation. `SaveGame` writes eleven sections and **all eleven savers are
now ours, and so are all eleven loaders** -- the serialiser is complete. The two
halves mirror each other, which is what confirmed each struct's layout from
both ends. The newest is `SaveObjScriptSection`, the deepest of them -- four
nested levels, with each action's string length-prefixed in place of the
pointer field it occupies in memory.

## Measured

| | | how |
|---|---:|---|
| `patch_replace` sites | 682 | `grep -rho patch_replace src/game \| wc -l` |
| distinct addresses reconstructed | 681 | 671 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 123,232 / 372,816 B (**33.1%**) | `tools/reconstructed.py`, split at referenced starts |
| the same, crediting whole entries | 140,144 / 372,816 B (37.6%) | what every earlier session quoted, and an over-count |
| modules | 30 flat + 16 `win32/` | `tools/checkclaims.py` |
| pure unreconstructed leaves | **0** (2 listed, both false positives) |
| self-naming unreconstructed functions | 109 at the sweep, 10 taken since | `tools/vectors.py --all` |
| boundary functions reconstructed | 68, 179 import sites | `docs/boundary.md` |
| COM dispatch outstanding | 0 of 79 functions | `docs/boundary.md` |

Read the percentage as what still crosses an original boundary, not as how
much of the game runs on our code -- the count-of-0 blind spot cuts the other
way, and `tools/blindspots.py` says which counters can move at all.

## Verification state

| check | when | result |
|---|---|---|
| `make` | current | builds clean |
| `make check` (16 static checks) | current | all pass, generated files regenerate identically |
| `make selftest` | current | **7,282** vectors, 15,228 words, 13,956 lines, 9,062 spine, 198 variable -- 0 fail |
| `tools/ab.sh campaign` | current | clean, three times: log identical at 14 messages, 2,571/786,432 pixels every time |
| savegame oracle, per section | current | `map` `pad` `script` `eventblock` `event` `air` `audio` **0**; `objscript` 376, all inside pointer fields; `conds` 372, a uniform -196 uid shift; `item` 16 heap pointers; `gameproc` 2 volatile |
| `tools/objdump.py --leader` | current | max health 140, current 140 -- identical to `AM2_NOPATCH=1` |
| `AM2_SELFCHECK=1` | current | 6,144 calls across 48 functions, 0 disagree -- and the pointer arguments finally differ from one another |
| `tools/maskdump.py` | current | roach 32 records/237 points, vehicle 192/3,081, 36,768 bytes, sha256 `532e52a0...` -- byte-identical to `AM2_NOPATCH=1` |
| `tools/anicheck.py` | current | 20 `.ani` files parsed to their last byte, 21 tables in the game, 0 mismatched, 121 borrowed entries all resolved right |
| `tools/ab.sh bootcamp\|windowed\|intro\|audio\|mission\|quit` | not since this run began | the rest of `ab.sh all` is still owed |

A clean A/B is not evidence about a function the run never calls. Check with a
counts probe before reading one as coverage -- that is what turned the
`EvtSetByte530` result from "verified" into "verified by reading", above.

## Next

1. **The menu widget layer is the current front and the best-verified part of
   the tree.** All five vtable slots have a reconstructed base, plus the
   placement helper, the constructors, the destructors, the focus walkers, the
   shared painter and the forwarding thunks. Three whole subclasses are done:
   the plain label, the focus-highlighting label, and the button (paint and
   mouse update). The edit box's lifecycle is done -- it is the owner of
   `g_charHandler`, and typing into it is checked end to end.

   Two configurations cover it: `tools/ab.sh controls` compares three frames,
   and `tools/ab.sh multi` is the only one that reaches the edit box.

   What is left, smallest first from the ranking in the Leads: `0x00454B70`
   (~43 B), `0x00456D00` (~61), `0x00455110` (~99), `0x00454A10` (~118),
   `0x004561C0` (~122), `0x00456C80` (~124), `0x00455070` (~138). Then the 33
   per-class constructors, which is where the subclass tails get their
   meaning, and the edit box's own painter at `0x00454D20` (242 B, the one
   with the text buffer and the caret).

   **Before claiming any of them, run the address past `orig.h` AND the
   harness patch list.** `0x0045CAA0` looked like an empty virtual and was the
   game's logger; see CLAUDE.md.

2. **Drive a LOAD -- a genuine puzzle, with the ruled-out branches named.**
   `LoadGame` (`0x00425A10`) is reconstructed, patched and traced, and it still
   never runs. Measured, from a temporary `hooklog` probe plus the trace log:

   - The GAME SELECT PANEL's LOAD arm (`0x00452060`) fires: `0x00511B88`
     holds `"map1_mission1.sav"`, `0x00511A68` holds `"sarge"`.
   - Mission start (`0x00425300`) takes the LOAD branch, so `0x00511DD8` was
     set when it read it at `0x00425360`.
   - `0x00425950` **succeeds** -- `SetGameDir("save\sarge")`,
     `CheckSaveTag(fp, 0x06660666, gameproc.cpp, 0x528)`,
     `LoadGameProcSection` returning 1. So the flag is NOT cleared at
     `0x00425373`, which was one of the two candidates.
   - `LoadGame` is patched and never traced; `LoadLevelScript` is, so
     `0x004255CB` read the flag as 0.
   - There is **no write** to `0x00511DD8` between `0x00425385` and
     `0x004255CB`.

   The most likely remaining reading is that mission start is ENTERED TWICE
   and the second entry clears the flag before reaching the test, which a
   probe on the two writes would settle in one run.

3. **Fold the pointer-aware savefile comparison into a tool.** Done by hand
   for objscript -- walk the section, collect the offsets holding heap
   pointers, compare everything else -- and it turned "188 differing bytes"
   into a clean result with a sharp pass criterion. `tools/actdiff.py` already
   renumbers pointers by first-seen index; the savefile deserves the same, and
   then `tools/ab.sh` could carry it as a standing check.

4. **Work off the `checkglobals` backlog**, currently 28 surplus names and 15
   surplus spellings. The three worst were the back buffer, the draw target
   and the primary surface, and all three are done. `ADDR_HWND` through three
   names is next, one of which is `g_enumContext`.

5. Keep taking self-naming functions from `docs/logs.tsv`, recomputed against
   the current patch list rather than quoted from an old sweep. The air.cpp
   message list is the notable remainder -- `RemHead` (`0x004010C0`, 144 B,
   10 callers) and `RemMsg` (`0x00401410`, 176 B, 3) -- and CLAUDE.md warns
   it is mutex-guarded and multi-threaded, so a mistake there is a race
   rather than a crash.

6. `tools/ab.sh all` is clean on all eight configurations; see Leads.

## Leads

- **`| head` in a build command hides the exit status, and an A/B ran against a
  stale binary because of it.** `make -s 2>&1 | head -6 && make check && ab.sh`
  continues past a failed compile, because the pipeline's status is `head`'s.
  The compile had failed on a missing include; `make check` then passed on the
  PREVIOUS object files and the A/B measured a binary without the change in it.

  Caught by reading the task output rather than the summary line. Put the build
  in its own command, or check `${PIPESTATUS[0]}` -- never `&&` after a pipe.

- **`ab.sh mission`'s frame guard fired for real**, reporting 0/0 markers and
  refusing to compare "the two ways of not getting there". The re-run gave
  7807/6713 and was clean. That guard was added after a run compared 24,914
  lines against 21,741; this is the first time it has caught a drive that
  reached nothing at all.

- **Six self-naming functions are left in the whole image**, and the sweep now
  covers any size: `ExitAllFromVehicle` (368 B, taken), "Options changed by
  host." (544), `UpdateTrooperAction` (2,080), "Player %s has left the game -
  now AI controlled." (2,256), "Avoid the agony..." (2,304) and "Starting Slave
  Session" (3,040). After that, naming has to come from somewhere else again.

- **Three of `ExitAllFromVehicle`'s callees are named from that one call
  site**, which is the naming this project keeps getting bitten by. Said so in
  `orig.h` beside them: what is evidenced is only what the caller does with
  each answer, and the bodies want reading before the names are trusted.

- **The self-naming pool is nearly dry below 320 bytes.** A sweep for
  unpatched sub-CRT functions that push a string looking like their own name
  turns up exactly ONE: `0x0044C250`, which logs "Trooper Fire Send, trooper:
  %d,  face:%d, pos (%d,%d,%d), loctarg %x, globTarg %x, weap %d, seq:%d". That
  one line named the function and nine of its fields.

  It is a 28-byte army message of kind 0x17 and it cannot run here: no
  DirectPlay session, so it returns at its first test. Verified by reading, and
  the counter exists -- so that 0 is real rather than a missing patch, which is
  a distinction this tree could not draw two days ago.

- **The sequence number is READ and not bumped.** `TrooperFireSend` takes it
  off the flow record at +0x94 and stores it on the trooper at +0x5CC; whatever
  advances it is somewhere else.

- **A function that does not appear in `counts` at ALL has no patch**, and
  that is a usable check only since the trace table stopped overflowing.
  `ProgressBar` was written, compiled, and never installed -- an edit that
  targeted `    patch_replace(...` where the file says `    rc |= patch_replace(...`
  simply did not apply, and nothing static could know a patch had been
  intended. `counts ProgressBar` answering "(nothing traced)" is what caught
  it, three commits after "(nothing traced)" stopped being ambiguous.

- **And the 76 was noise.** That same run put `bootcamp` at 76 differing
  pixels where it has read 22 all session, which looked like the new drawing
  code. Two re-runs on the fixed build: 22 and 22. `ab.sh` has said to re-run a
  difference before believing it since long before this session.

- **`TextExtent`'s height does not depend on the string.** It is the second
  uint16 of the SPACE glyph, read through a fixed entry of the font's offset
  table, so an empty string still answers a line height. The width skips '^' as
  an escape and anything below 0x1F as a control -- and the test is SIGNED, so
  0x80 and up are skipped too.

- **The heading arithmetic exists three times in the image**, which is how it
  was recognised: `AngleBetween` from two points, `AngleOfDelta` from the
  deltas already subtracted, and `DistAndAngle`, which answers the distance and
  the heading together through two out-pointers -- and whose distance half is
  `ApproxDist`'s formula to the instruction. One copy here, called three times.

  `AngleOfDelta` reads **58** in a Boot Camp mission where `AngleBetween` reads
  0, so the shared body IS exercised, just not through the entry that looked
  like the main one. It is also the one of the three the selfcheck can drive
  directly: its arguments are scalars, so `pick` varies them without going
  through the scratch at all.

- **`ADDR_REFRESH_GATE`'s "stays original" meant "not yet".** No reason was
  written beside it and the body is two stores to globals that were already
  named. Reconstructed, and surface.cpp's seam closed with it. A decline with
  no reason recorded is not a decision -- that is what the note beside it is
  for.

- **The voice lines are a table with the answers in it.** `SpeakLine`
  (0x0040BFF0, 35 callers) picks one of a group's wave names at random and
  only when the owner is ours; the groups are 20-byte records at 0x00474444 --
  a count and up to four names -- and the names say what they are:
  Aerosol.wav, AirStrike.wav, AutoRifle.wav, Bazooka.wav, Disguise.wav,
  Explosives.wav. It goes out on slot 0x10, which orig.h already records as a
  voice slot.

  **It reads 0, and so does PlayDynamicSound underneath it**, even with the
  ALSA null device attached and both Boot Camp dialogs cleared. So the whole
  dynamic-sound path is unreached by any configuration here -- which is a
  pre-existing gap for PlayDynamicSound, now measured rather than assumed.

- **`ListAdd` reallocs per entry.** Every append grows the array to exactly
  count+1 rows of 0x104, so filling a list of n costs n reallocs and n copies
  of everything before it, and the name is copied with no bound at all -- a
  name of 0x100 runs into the value beside it. Both are the original's.

- **`AM2_SELFCHECK=1` was comparing every pointer-argument function against
  ONE input, and the two pointers at the same bytes.** Two defects in one
  line. `fill_scratch` did not vary with the iteration, so all 128 calls saw
  the same memory; and `(i * 7 + 13) & 0xFF` has period 256 while the pointer
  arguments are 0x100 apart, so every pointer pointed at IDENTICAL bytes.
  ApproxDist, PointInRect, PointsEqual and every Obj* predicate were being
  handed two copies of one value, 128 times.

  It is the offline harness's own bug, which `vectors.py` hit with a stride of
  0x800 and fixed with a salt; the in-process one had it with 0x100 and nobody
  had looked. Found by mutating `AngleBetween`'s table index by one and
  watching the run pass -- twice, because the first fix (vary with `k`) was not
  enough.

  Fixed and re-run: **0 disagree**, so nothing was relying on the accident. The
  same mutation now fails 127 of 128 with the arguments printed.

- **`AngleBetween` needed the trig tables seeded to be checkable at all.** It
  reads the two reverse tables, which are .bss zeros when the selfcheck runs --
  every index would answer 0 and the indexing, which is the whole function,
  would go unchecked. `fill_atan_tables()` puts a position-dependent byte in
  each of the 2,050 entries first; the game overwrites both at startup.

  Worth the trouble because nothing else reaches it: twenty callers in the
  image, none reconstructed, and the counter reads 0 through a live Boot Camp
  mission with both dialogs cleared and the view scrolled.

- **The selfcheck learned `byte_ret` too**, for the same reason the vectors
  did: `AngleBetween` returns in AL and the two sides leave different rubbish
  above it -- the division's quotient in the original, a sign-extended table
  byte in ours. `-> ffffff91, original 00000091` was the first thing the fixed
  harness said.

- **The packed key addresses SPRITES.** `PreloadSpriteByKey` (0x00445AD0)
  splits a key into PackKey's three fields and passes them as PreloadSprite's
  first three arguments -- the shifts and masks are `KeyFieldA`, `KeyFieldB`
  and `KeyFieldC` written out, and they agree exactly. Together with
  `KeyLookup`'s table two commits ago, `packkey.cpp` is no longer a set of
  accessors with nothing that uses them. It runs 3,073 times a mission.

- **The state machine's two halves are ours**: `RequestState` raises the
  pending flag and records what is wanted, `CommitState` takes it and moves it
  into `ADDR_GAME_STATE`. CommitState runs whether anything was pending or
  not -- called with nothing wanted it would put -1 into the game state, and
  its one caller checks first.

- **The Boot Camp map is 256 x 256 tiles and 4096 x 4096 pixels**, read out of
  the running game -- `0x00514DD0`/`0x00514DD4` are 4096 and
  `0x00514DDC`/`0x00514DE0` are 256, which also fixes the tile at 16 pixels
  and matches `TileOfPoint`'s shift of four exactly.

  **That is why the map-dimension contradiction has survived: the two globals
  are EQUAL.** CLAUDE.md records `0x00514DDC` as both `ADDR_MAP_TILES_W` and
  `ADDR_MAP_HEIGHT` and says both pairs cannot be right. `TileOfPoint` uses
  `0x00514DDC` as its row stride and `ScriptPad` uses `0x00514DE0` as its row
  stride, which is two pieces of CODE disagreeing rather than two names -- and
  swapping `TileOfPoint`'s stride to the other global leaves `bootcamp`
  identical at 22 pixels, because on a square map they are interchangeable.

  To settle it, read both globals on a NON-SQUARE map. The campaign drive is
  the obvious place and it died twice under the hand-driven sequence; `ab.sh
  campaign` is the reliable route and would need a dump added to it.

- **`TileOfPoint` runs 24,884 times a mission**, which is the most of anything
  taken this session apart from the keyboard pair.

- **The trace table had been silently full, and a full table reads exactly
  like a missing patch.** 104 functions could not be wrapped -- everything
  patched late in `install()`, so the palette, sprite, surface, device and
  winmain modules -- and `counts NearestPal` answered "(nothing traced)",
  which is indistinguishable from never installed. Nothing was WRONG:
  `trace_wrap` falls back to the unwrapped function and the patch goes in
  either way. Only the measurement was gone, and only under `TRACE=1`.

  Third overflow, and `trace.c`'s own comment had already said what to do
  ("the limits want raising, not the message suppressing"). Raised to 2,048
  with the arena sized from it, and the overflow is COUNTED now: `counts`
  appends `[N function(s) NOT WRAPPED: trace table full]`, so the quiet
  version of this failure cannot happen again. `NearestPalIndex` reads 3,072
  and `NearestPalIndexRGB` 50 -- both invisible an hour ago.

- **The unit types name themselves, and the table is 12 records of 40 bytes.**
  `0x004878B8` holds {value, bit, isTrooper, isVehicle, index, char name[20]}
  and the names are IN it: bazookaman, mortarman, grenadier, flamerman, tank,
  jeep, halftrack, truck, ptboat, riflepill, bazookapill, mgpill. That identity
  is certain.

  What the first field means is NOT. It runs 100 to 500, `0x0043A5E0` is its
  only reader, and the three callers are the mission-start screens -- which is
  what suggests a cost and is not proof of one. Named `UnitTypeCost` with that
  said in the header rather than left to look established.

- **`ResetDrawCounts` is blind by construction and stays verified by reading.**
  Its only caller is `ComposeFrame`, which is ours, so closing the seam took
  its counter to 0. It is three stores of zero; the proportionate check is
  reading them, and that is what it has.

- **The keyboard four are all ours now**: `IsKeyDown` and `KeyChanged` off the
  two poll buffers, `KeyPressed` off the edge-and-auto-repeat array at
  0x00512BD0, and `LatchKeyState`, which COPIES the current buffer over the
  previous one so every edge test that follows sees no change. Note it copies
  where `PollKeyboard` swaps -- two different operations on the same pair.

- **`FreeAllFonts` is teardown entry 7**, so six of the thirteen are named now.
  It reads 1 on the quit path and `FreeFont` reads 0, which is the ordinary
  blind spot: the sweep calls it directly.

- **`ListFirstField548` runs 348 times a mission** and `SetLeadsAndAct` once.
  Between them they are the only writer and one of the readers of the dword at
  +0x548, whose meaning is still not established -- `LookupOwnerObj` picks an
  army's object by it and this puts 1 in it. Worth knowing that the whole set
  of things that touch it is now small and reconstructed.

- **The packed key has a TABLE, and it is what the region pass consults.**
  `0x00434290` binary-searches a sorted {key, value} array at `0x00516150` and
  `0x004346E0` packs its three arguments with exactly `PackKey`'s arithmetic
  before handing them over -- which is what ties `packkey.cpp`'s field
  accessors to something that uses them.

  Checked by mutation rather than by a clean run: `KeyLookup` runs 1,588 times
  in a Boot Camp mission, and making the search never match puts the frame
  293,671 pixels wrong -- 37% of it -- and drops the game's own "calculating
  region data..." line. That log line is also the only evidence so far of what
  the table is FOR.

- **`ab.sh controls`' pixel figure is NOT deterministic, and this file said it
  was.** Four runs of one build gave 0/0/0, 45/45/45, 54/45/50 and 0/0/0 --
  the key-capture boxes blink, so two screenshots agree only when they land in
  the same phase. The widget tree (25 nodes) and the log (5 messages) are
  identical every time, and those are the evidence. A 0 there is luck, not a
  guarantee; the earlier "0 pixels on all three frames" was one sample read as
  a property.

- **`orig_` was one spelling of the seam and not the only one.**
  `frame.cpp` reached the movie step as `call0(ADDR_MOVIE_FRAME_STEP)`, which
  was fine until `0x00445630` was reconstructed and then went through the
  detour into our own code -- exactly what `checkseams.py` exists to stop,
  written differently. It checks `callN(ADDR_X)` at a call site now as well as
  `#define orig_x ... ADDR_X`, and is tested in the failing direction by
  putting the call back.

- **And that made `blindspots.py` wrong in the other direction.** It reported
  `MovieStepCurrent` blind because both its callers are reconstructed, while
  the counter read **746,792** -- because those callers were reaching it by
  ADDRESS. Closing the seam took the counter to 0 with no behaviour change at
  all: `MoviePoll` still reads 712,509 on the same run. A counter falling to
  zero for that reason is the blind spot happening on purpose, and it is worth
  having seen it once.

- **`IsKeyDown` and `KeyChanged` are the two most-called reconstructions in
  the tree**: 2,669,477 and 2,667,117 in one Boot Camp mission. Both mask the
  scancode themselves and read through `ADDR_KEYS_NOW_PTR`, because
  `PollKeyboard` swaps the two buffers each poll. `IsKeyDown` returns 0x80
  rather than 1 -- an `and eax, 0x80` with no normalisation -- so every caller
  is testing it against zero.

  Neither can go in `AM2_SELFCHECK=1`, and for the reason `LookupOwnerObj`
  taught last commit: the buffer pointers are NULL before `install()` runs.

- **`Cos8` and `Sin8` return a FLOAT**, in st(0), which puts them outside both
  differential harnesses -- the vectors and the selfcheck each compare eax.
  What checks them is one layer down: they are the only readers of the two
  forward trig tables, and `tools/trigdump.py` compares those byte for byte.

- **The nine smallest functions left were worth more than their bytes.** 192
  bytes across five modules, and eight of the nine already had a name in
  `orig.h` waiting for them -- `ADDR_MENU_ROW`, `ADDR_CLEAR_PTR_LIST`,
  `ADDR_BUILD_FONT`, `ADDR_OBJ_BY_UID`, `ADDR_MOVIE_CURRENT` and the movie
  block. Grepping the address first turned what would have been nine guesses
  into one: `0x00412DD0` is `GetMenuRow`, `0x0044BA60` and `0x00446830` are
  plain cdecl wrappers, `0x0042A660` is the constructor for the record
  `ADDR_CLEAR_PTR_LIST` empties, and `0x0042A670` is a one-instruction alias
  for that teardown.

- **The movie vtable dispatch works, and its counter proves the chain.**
  `MovieStepCurrent` reaches `MoviePoll` through `object -> table -> slot 0`,
  and on the intro path `MovieStepCurrent` reads 746,792 while `MoviePoll`
  reads 746,794 -- so the two dereferences are right. Writing that as one
  dereference calls the vtable pointer as a function and the game exits
  instantly; CLAUDE.md says it cost an iteration once.

- **`ab.sh` can SEE a defect and still report clean, and here is a measured
  case.** Forcing `SetMaxHealth`'s difficulty index to 0 doubles the player's
  health, and `bootcamp` goes from its usual 22 differing pixels to 96 -- and
  passes, because 96 is well inside the budget of 500. The budget is what
  makes the check survive a moving scene; it is also what makes a small real
  difference invisible. Read the number, not the verdict.

  `tools/objdump.py` is the answer for this class: it reads a registered
  object's fields out of the running game by uid, binary-searching the sorted
  table. The leader's max health is 140 on a correct build and 280 on the
  mutated one -- 4.0 against 2.0, exactly. That turns "verified by reading"
  into a comparison for every function that writes an object field and returns
  nothing, which neither the vector harness nor `AM2_SELFCHECK=1` can check.

- **The game gives up on you gently.** `SetMaxHealth` takes five health off
  every enemy for each retry of the level, divided by 2*difficulty + 2 --
  faster on easy than on normal, and not at all on hard, where the whole enemy
  branch returns without writing. `0x00512330` is the retry counter, and it
  says so: the level loader logs `"Attempt# %d"` and `0x00421890` clears it
  when the campaign moves on.

- **And that rubber band is unobserved in every run here.** Boot Camp reads
  difficulty 1 and attempt 0, so the enemy arm computes
  `max(amount * 0.33, amount - 0)`, which is `amount`. The 0.33 constant, the
  division and the floor are all verified by reading; only the player's x2 and
  the 400 cap are actually exercised.

- **Two names in `orig.h` were wrong about what they named, and both were
  named from a single call site.** `ADDR_SPRITE_DROP_NAMED` (`0x00457820`)
  walks every object an army owns and CALLS its second argument -- `call ebp`
  -- so nothing about it is a sprite; the one call site passes `0x0045A030`,
  which is a function that hands a unit to the AI, matching the "left, AI takes
  over" message beside it. And `EvtMarkSet`/`EvtMarkClear` write into the 4x4
  ALLIANCE matrix: `0x00424E80` fills that table with the identity and then
  allies any two comm players on the same team. They are `ForEachArmyObject`,
  `ADDR_OBJ_TO_AI`, `EvtSetAllied` and `EvtClearAllied` now.

- **And the alias ratchet caught the author again**, one commit after the
  lesson was last written down. Five addresses went into `orig.h` and two
  already had names; the group was grepped for its globals and not for its
  functions. Grep EVERY address, not the ones that feel new.

- **A function can be safe for `AM2_SELFCHECK=1` and still not survive it.**
  `LookupOwnerObj` range-checks its army perfectly and then indexes
  `0x004F9ECC`, which is NULL that early -- the selfcheck runs before
  `install()`, which is before the game has loaded anything. It took the
  process down with 47 functions announced and no summary, the same symptom
  `XorChecksum` produced. The question is not only "does it survive a random
  argument" but "does it survive the empty world this runs in".

- **`ObjIsFriendly` passes the selfcheck and one mutation of it also passes.**
  Inverting the matrix lookup fails all 128 calls, so that half is genuinely
  compared. Changing the `owner == 4` shortcut to `owner == 5` changes
  nothing: the scratch byte at that offset is fixed, so the argument never
  varies. The shortcut and the multiplayer block -- `g_mpSession` is 0 before
  anything loads -- stay verified by reading.

- **The `.ani` subsystem is closed: five loaders, six frees, one table
  reader, three lookups, two mask builders.** Nothing between a `.ani` file on
  disk and a sprite index is original any more.

- **A `chdir` is a side effect, so which side of a test it falls on is
  behaviour.** `LoadExplosionAnims` and `LoadMissileAnims` chdir into
  `data\ani` and THEN test whether the table is already loaded;
  `LoadRoachAnims` tests first and returns without chdiring. Reproduced rather
  than tidied.

- **Neither oracle sees the soldier loader's last line**, which rewrites the
  `next` of rifleman's animation 0x46 to -1. `anicheck.py` reads the table
  inside LoadAnimTable, before the fixup, and `maskdump.py` never looks at the
  soldier tables. Checked by reading the entry over the control socket in both
  builds instead: id 70, `next` -1, on ours and under `AM2_NOPATCH=1`.

- **The boat is given the jeep's turret** and vehicle kind 4 has no paths at
  all -- two of the twelve slots in `LoadVehicleAnims`' table point at the
  image's shared empty string at `0x004F96B8`, which 67 sites use and nothing
  writes. Both reproduced as they stand.

- **The game calls these masks, and it said so itself.** `BuildVehicleMask`
  logs `"vehicle mask direction: %d"` under `-traceVEH`, which named the whole
  family: the tables are MASKS and their index is a DIRECTION. What went in as
  `footprints`/`facings` is renamed throughout -- `AM2_Anim::directions` and
  `directionBits` with it, since that message's counter runs over exactly that
  field. `tools/footprints.py` is `tools/maskdump.py`.

- **The vehicle bases are confirmed by tiling, which is the check that matters
  after the roach's was wrong.** Six turret animation tables end exactly at
  `0x0065A2D8`, six direction counts end exactly at `0x0065A2F0`, and
  6 x 32 records of 0xA4 end exactly at `0x00661DF0`, which is
  `ADDR_VEHICLE_ANIMS`. Nothing left over anywhere.

- **The roach wants 16 of its 64 samples solid and the vehicle 12.** Same
  builder otherwise; not unified, because the two constants are the only thing
  separating them and a shared helper would hide that.

- **`BuildVehicleMask=5`, not 6.** The loader skips a kind whose path is empty,
  so its direction count stays 0 -- `dirs 32,32,32,32,0,32` -- and the mask
  builder is never called for it. The `directions <= 0` early return is
  therefore NOT what that zero comes from; nothing reaches it.

- **The mis-centred trig table happened again, and only a table dump caught
  it.** `BuildRoachFootprints` writes each record's count through `[ebp-4]`
  with `ebp` starting at the POINTS, so the array begins at `0x00654CA8`.
  Taking `0x00654CAC` as the base put the whole table one dword early, over the
  global at `0x00654CA4` -- every point correct and every one in the wrong
  place. `tools/maskdump.py` found it on its first run by hashing the raw
  region rather than the decoded records.

- **And that A/B cannot fail on this at all.** With the sample step doubled
  from 2 to 4, all 32 records change and the point total drops from 237 to 25;
  `ab.sh bootcamp` is still clean at the usual 22 pixels with an identical log.
  So the table is verified by `footprints.py` or by nothing.

- **`0x0043C720` was 12 bytes, not 432.** It is `FreeMissileAnims`, the fifth
  anim sweep, and `docs/functions.tsv` had run it together with the roach
  footprint builder next door. The teardown table's comment said it "does more
  than free" on that basis; a merged entry, exactly as `merges.py` warns.

- **The three animation lookups are what prove anim.h's field names.**
  `0x0044BB30`, `0x0045D9B0` and `0x0045DA20` each find the entry with a fixed
  id -- 1 for soldiers, 0x51 for vehicles and turrets, and the shipped files
  bear both out -- then hand `facingBits` to `RoundTo8` as its BIT COUNT and
  index the cell grid at `frames * facing`. So `facingBits` really is the log
  of the facing count and `frames` really is the inner stride. Nothing in the
  loader could have settled either; only a reader could.

  All three are installed and read 0 in every configuration. The gate is
  `0x004FCF84`, four frames up the chain at `0x00413BC0`, which also rotates a
  ghost with keys 2 and 3 and places it at the cursor -- a developer placement
  overlay, not gameplay. Verified by reading plus the structural agreement.
  Finding the switch that sets `0x004FCF84` would make all three drivable.

- **The turret lookup returns NULL where the other two fall back to entry 0**,
  and it opens with a null test on `lea esi, [eax*8 + 0x65A2A8]` -- the address
  of a global plus an index, which cannot be zero. Not reproduced, same as
  `UpdateMouseState`'s unreachable `je`; the NULL return IS reproduced, since a
  turret with no animation is a different thing from one drawn as its first.

- **Four of ShutdownSubsystems' thirteen teardown entries are no longer
  guesses.** They are the anim sweeps -- explosions, roach, vehicles+turrets,
  soldiers -- each a `push <table>; call FreeAnimTable`, and the table names
  the `.ani`. Measured at shutdown: all four run once and `FreeAnimTable`
  itself reads 1, called with `00654C90`, the missile table, from the one
  caller still original. `ab.sh quit` clean at 8 messages and 0 pixels.

- **`counts` truncates, and the filter argument is the answer.** The three new
  lookups were absent from an unfiltered `counts` reply, which reads exactly
  like "never installed" -- the failure `control.c`'s own comment predicts.
  `drive.sh ctl "counts Anim"` lists them. Use the filter whenever a name you
  expect is missing.

- **The `.ani` format is confirmed by arithmetic.** `LoadAnimTable` --
  `LoadSpriteFile`'s tail, and the last unread part of a sprite file -- reads a
  count and that many 16-byte entries, each either an animation of its own or a
  borrow. Parsing all twenty shipped files with that layout ends every one on
  its last byte, 349 entries in all; `rifleman.ani` is 1,103,262 of 1,103,262.
  A mis-sized field could not do that.

  `tools/anicheck.py` then compares what the game built against that parse.
  Sprite indices are compared as deltas, because the absolute value depends on
  how many sprites earlier files put in the list.

- **Everyone borrows from the rifleman.** Eight soldier files pass rifleman's
  table as their fallback, and an entry with no animation of its own takes the
  one with the same id out of it -- `grenadier.ani` gets 43 of its 49 that way.
  `explosions.ani` passes no fallback at all, so its three borrowers fall to
  the loader's final fixup and take its own entry 0. All 121 resolve to the
  predicted pointer.

  Two paths the shipped data cannot reach: no borrowed id is missing from
  rifleman, so the `entries[0]` last resort never fires, and rifleman's 52 ids
  are distinct, so first-match and last-match are indistinguishable.

- **`facings` is always a power of two, which is what fixed the field names.**
  The loader hands it to `0x0042DFE0`, a jump table that turns a single-bit
  value in 1..0x8000 into its bit index, and stores the answer in a byte. The
  shipped files use 1, 2, 8, 16 and 32 and nothing else. So the pair
  multiplying to the cell count is frames x facings and not two anonymous
  dimensions; the cell data settles the order, stepping consecutively within a
  facing and jumping between them.

- **`LoadSpriteFile` runs 21 times in Boot Camp**, once per `LoadSpriteSet`, so
  both halves of the sprite loader are measured rather than assumed.

- **A byte-returning function needed the vector harness to learn a new
  thing.** `Log2Mask` (`0x0042DFE0`) writes `al` and leaves the rest of `eax`
  holding its own argument -- the recorded answer for `Log2Mask(0)` is
  `0xFFFFFF00`, which is `dec eax` on 0 followed by `xor al, al`. The harness
  compared all 32 bits, so 90-odd vectors failed on register contents the i386
  ABI says nobody may read. Measured, not assumed: turning the new `byte_ret`
  flag off reproduces exactly those failures with the low byte agreeing every
  time. `VOID` was already the same problem one step further on.

- **A 16-way switch is the case for an explicit argument set.** 96 random
  32-bit arguments reached 50.8% of `Log2Mask`, because a random integer is
  almost never an exact power of two. Twenty-five values -- every power, the
  ends of the compare chain, and near-misses for the default arm -- put it at
  100%. Mutating one arm fails 3 vectors and prints the argument.

- **`pad28` was not padding.** `sprite.h` had eight bytes at 0x0028 named as
  filler; `LoadSpriteSet` reads two int16 straight out of the file into 0x0028
  and 0x002A, beside hotX and hotY and in exactly the same shape. They are
  `fileA` and `fileB` now -- what they MEAN is still unknown, but "padding" was
  a claim and it was wrong.

  A field is only padding when something has looked for a writer. Nothing had.

- **A sprite's format comes from the CALLER, not the file.** `LoadSpriteSet`
  sets it to 3 for flag bit 0x10, 2 for bit 0x08, and otherwise leaves the zero
  the memset put there -- which `sprite.h` reads as "image is a DirectDraw
  surface". A set loaded with neither bit would claim to hold surfaces while
  holding file bytes. No caller does it; the zero is left alone.

- **`LoadSpriteSet` runs 21 times in a Boot Camp mission** and feeds the 5,798
  `RemapSpriteRuns` calls, so this A/B is real evidence rather than "nothing
  else broke". Probed rather than assumed, which is the habit the last commit
  argued for.

- **`RemapSpriteRuns`' unused second argument is the image byte count.** The
  caller has it and passes it; the RLE walker does not need it, because the
  header already says how far to go.

- **A counts probe on today's work, and it splits three ways.** Every commit
  since the comm family said "bootcamp and mission clean". One probe says what
  that was worth:

  | | calls | what the zero means |
  |---|---:|---|
  | `RemapSpriteRuns` | **5,798** | genuinely exercised; the A/B is real evidence |
  | `GrowSpriteList` | **58** | the same |
  | `DoAirSupport`, `AirSupportPop` | 0 | REAL zeros -- `blindspots.py` says these counters can move, so the air-support path simply never runs in Boot Camp. Verified by reading |
  | the five `FreeItem` arms, `AirSupportBegin`, `AirSupportClear` | 0 | BLIND -- every caller is ours, so the zero says nothing either way |

  So of eleven functions landed since the comm work, two are exercised, two are
  known not to run, and seven are unmeasurable from the outside. The A/Bs were
  still worth having -- they establish that nothing else broke, which is not
  nothing -- but "clean" was doing more work in those messages than it earned.

- **The `quit` configuration is the one that reaches the free family**, through
  `ReportLeaks`' "Unreleased memory (0) blocks:", and that is why it is worth
  running for anything that frees.

- **The duplicate-PATCH check earned its keep a third time.** I wrote a thunk
  for 0x00409920 that `winmain.cpp` had already reconstructed as
  `FreeSpriteListAlias` -- one of the twelve WinMain-chain functions CLAUDE.md
  names -- and `checkpatches` refused the build. The two before it were
  `TakeUid`/`AllocUid` and `ScriptCompare`.

  What the alias JUMPS to had a name too, `ADDR_FREE_SPRITE_LIST`, reached
  through an `orig_` seam. So this was never new frontier: it was the seam
  under a function that was already ours, and closing it is what the commit
  actually did.

  The lesson is narrower than "grep the address". I DID grep -- for
  0x004098B0, and found nothing, because the constant is spelled
  `0x004098B0u` in one place and the thunk is a different address entirely.
  Grep the address of every function in the group, not just the one you
  started from.

- **Naming follows the code that is already there.** The globals became
  `ADDR_SPRITE_LIST*` rather than my `SPRITE_POOL*` because `orig.h` had
  already committed to "sprite list" in the two function names. A new name
  beside an established vocabulary is a second vocabulary.

- **`GrowSpriteList` does not look at the count.** It adds a hundred to the
  capacity and reallocs, so it is "make room", called by whoever is about to
  need it, rather than "grow if full". And nothing checks the realloc.

- **Three ratchets fired on one 112-byte function, and all three were right.**
  `checkseams` caught a fresh `orig_` on 0x00457420, which `objtype.cpp` has
  had as `ObjIsTypeIn238` for a long time; `checkglobals` caught the game clock
  spelled `(const int32_t *)(uintptr_t)` where `event.cpp` spells it
  `(const uint32_t *)AM2_IMAGE(...)`; and the COMPILER caught `OBJ_OFF_FLAGS`,
  which I put in `orig.h` while `item.cpp` had a local copy of the same offset.

  The third is the one with no tool behind it -- a duplicate `#define` is only
  a warning, and only because both were in scope at once. The offset now lives
  in `orig.h` alone.

- **The "radius" is ApproxDist's, so it is a diamond and not a circle.**
  `TakeNearbyOffMap` measures with `ApproxDist`, which is the game's cheap
  approximation, and that is what decides who gets caught by an air strike.
  Reproduced rather than tidied into a true distance.

- **Three tests in the original's order, all three needed.** Type 2, 3 or 8;
  not ALREADY off the map, which is the 0x0800 flag the taking-off sets; and
  within the radius. The second is what stops a second strike re-scheduling
  something already gone.

- **The air-support family is complete** -- request, enemy check, queue head,
  start and reset. Five functions, and every one of them is inside or beside
  the 584-byte block `air.cpp` was already saving, so the savegame section and
  the live subsystem turned out to be the same thing.

- **The caller's `kind` is a floor, not a decision.** `DoAirSupport` takes kind
  2 as given, but anything else asks `FindEnemyNear` and becomes kind 3 if
  there is one. So a caller cannot ask for the quiet drop when the drop zone is
  contested -- except by asking for kind 2, which skips the check entirely.

- **It calls `AirSupportBegin` with the entry written and the count still
  zero**, so Begin reads a slot the count says is not there. Harmless, because
  Begin only ever looks at slot 0, and it is the original's order.

- **The same field is written as a dword and copied as two words.**
  `DoAirSupport` stores `where` with one 32-bit move; `AirSupportPop` moves its
  two halves separately. Both reproduced, because the packing is only visible
  in the second.

- **CORRECTION: `obj + 0x0010` is the OWNER, not a kind byte.** Two commits ago
  I wrote that `TrooperDropItem` "sets its kind byte to 4, so a dropped item
  becomes a weapon object on the ground". It does not. `objtable.h` has had
  that field named `owner` since long before today, and army 4 is the neutral
  one -- so what dropping an item does is give it to NOBODY, which is what a
  thing lying on the ground should belong to.

  Two independent uses settle it: `TrooperDropItem` passes that byte to
  `CommMustBroadcast`, which takes an army, and `FindEnemyNear` compares it
  against `UidArmy`. I guessed a field the repo had already named -- the same
  failure as naming a function from a call site, one level down.

- **And `OBJ_OFF_OWNER` in `orig.h` is a DIFFERENT structure's field**, at
  0x0004. The object's owner is at 0x0010 and lives in `objtable.h`'s
  `AM2_Object`. Two right names, one collision, and the wrong one is the one
  that greps first.

- **`FindEnemyNear` calls `UidArmy` once per CANDIDATE**, not once before the
  loop, so a query returning forty objects calls it forty times. Reproduced.

- **`0x0042A240` is "every object in a rectangle"**: clip to the map extents,
  shift down by eight into tile coordinates, walk the cells, keep what the
  predicate accepts, and thread the result through 0x0068 of each object.
  Named from the body; three callers.

- **"EndMission" is a log PREFIX, not a function name**, and the self-naming
  sweep would pair it with whichever function it reached first. 0x00408FF0
  prints "EndMission  AirSupport.count decreasing to: %d" and DoAirSupport
  prints "EndMission  AirSupport.count increasing to: %d" from a different
  function entirely -- and DoAirSupport names ITSELF on the line above its own.
  Second time the sweep has been shown to attribute a string wrongly, after
  "TIMING OUT PLAYER"; the first was a merged entry, this one is a shared
  prefix. Read the body.

- **The air-support queue IS the block `air.cpp` saves.** All nine fields are
  offsets into ADDR_AIR_SAVE_BLOCK rather than addresses of their own, and the
  layout closes it exactly: the last flag sits at 0x0244 of 584 bytes. That is
  independent confirmation of both the field map and the block size, from two
  facts that were established years apart in this project.

  It also settled a naming collision the honest way -- `checkpatches` refused a
  second name on 0x004F945C, which is the block's start AND the active flag,
  and expressing the fields as offsets removes the question rather than
  answering it.

- **Two of the three tail into each other.** `AirSupportPop` really does
  `jmp` to Begin or Clear rather than calling them, which is why they are three
  functions and not one with arms. And Begin's two shapes disagree about the
  active flag: only the sound-playing one raises it.

- **"Must I tell the other players?" is one function, and nine callers ask it.**
  `0x0040F560` answers NO when there is no multiplayer session at all -- which
  is what settles the name, because under "is this army mine" a single-player
  game would have to answer yes to everything. Army 4 answers "am I the host".
  Everything else answers "is that slot NOT remote", inheriting
  `CommSlotRemote`'s three-valued oddity: a slot answering -1 is truthy there
  and becomes 0 here.

- **`g_hostChanged` was the multiplayer-session flag under a name from one
  writer.** `OnHostChanged` puts 1 in it, and that is not a second meaning --
  it is a machine that has just become the host asserting there is a session.
  The `orig.h` macro said `ADDR_MP_SESSION` all along and a comment beside the
  local name explained the discrepancy rather than fixing it. `checkglobals`
  refused to let the honest name exist alongside it, which is the ratchet
  working exactly as intended: it does not care which name is right, only that
  there is one.

- **`TrooperDropItem` is read and NOT written**, because it needs five more
  names first: 0x0042B290, 0x00439F40 (352 B), 0x0044C150 (256 B, the send
  side of a trooper message) and 0x00427F60. Reading it did establish that a
  trooper's inventory is six uids at 0x054C and that slot 0 is the wielded
  weapon -- the same field `DestroyTrooper` reads as `TROOPER_OFF_WEAPON_UID`.
  It also sets the dropped item's kind byte to 4, so a dropped item becomes a
  weapon object on the ground.

- **`FreeItem`'s whole switch is closed** -- all five arms, eight kinds, no
  `orig_` left in it. The bare arm serves kinds 1, 5, 6 and 8 and is exactly
  the tail the other four share; every other arm is that plus something.

- **The kind-7 counter is bounded at BOTH ends, which is why the clamp is not
  tidying.** 0x00435550 refuses to make a thirty-third kind-7 object and the
  free clamps the count at zero coming down. Reproducing only one half would
  have looked like a defensive check worth dropping.

- **A comment beside a seam goes stale exactly when the seam closes.**
  `item.cpp` carried "the five per-kind destructors stay original and are
  reached by address" while they stopped being original one arm at a time over
  three commits. Nothing checks prose; `checkseams` only sees `orig_` macros,
  and the last of those went in this commit, taking the sentence's last
  reader with it. Corrected, and the correction says what happened.

- **`FreeItem`'s arms are one function written four times, and the differences
  are the interesting part.** Three are now ours -- trooper, vehicle, weapon --
  and every one ends the same way: free the subrecord rows, hand the object to
  `DestroyItemObject`, free the object.

  What differs: the vehicle keeps its weapon uid at 0x0550 and the trooper at
  0x054C; the vehicle also empties a pointer list at 0x0538 that neither other
  arm has; the trooper's log is behind the verbosity flag, the weapon's is in
  front of it, and the vehicle has none at all. And the same weapon flag is set
  with an 8-bit OR in one arm and a 32-bit OR in another -- the compiler's
  difference, from one piece of source written twice.

- **The COMMON arm at 0x0043BBB0 is the bare version**, 48 bytes, serving kinds
  1, 5, 6 and 8: the shared tail and nothing else. Read, and the obvious next
  step -- with `ADDR_FREE_ITEM_KIND7` after it, that closes the whole switch.

- **Both of these landed as seam closures.** `FreeItem` was already ours and
  was calling into the image for kinds 3 and 4; it now calls functions. That is
  a different kind of progress from a new frontier and the percentage barely
  moves for it -- 96 bytes -- while three `orig_` seams disappear.

- **"Grep the address first" failed again, and this time BOTH ratchets caught
  it.** `0x004478C0` names itself "DestroyTrooper", and I added
  `ADDR_DESTROY_TROOPER` for it -- while `orig.h` already called it
  `ADDR_FREE_ITEM_KIND2` and `item.cpp` already reached it through
  `orig_free_kind2`. `checkpatches` reported the 32nd alias and `checkseams`
  reported the seam in the same run.

  The old name was not wrong either: it is the kind-2 arm of `FreeItem`'s
  switch, and kind 2 is the trooper -- `ReceiveArmyMsg`'s switch says so
  independently. So the address keeps the family name that `FreeItem` reads by,
  and the C++ function takes the name the function gives itself. When two names
  are both right, keep the one the surrounding code is organised around.

- **This landed as a SEAM CLOSURE, not a new frontier.** `FreeItem` was already
  ours and was calling into the image for kind 2; now it calls a function.
  Three of the five arms are still original -- common, kind 3 and kind 4 -- and
  each is a self-contained target of the same shape.

- **A trooper is freed twice over, in an order that matters.**
  `DestroyItemObject` frees the 0x0090 allocation and clears the live byte, and
  only then is the object itself freed. Reproduced in that order.

- **The weapon step is skipped in silence for anything that is not a weapon.**
  `WeaponByUid` complains and answers null for any kind but 4, and a zero uid
  never asks -- both land on the same path, and the trooper still comes down.

- **BOTH dispatchers are ours now.** `CommDispatchMessage` handles the
  packet-level messages and `ReceiveArmyMsg` handles one message out of a
  packet, and the two switch on different things: the first on a message type,
  the second on the object KIND behind the message's uid. 2 is a trooper, 3 a
  vehicle, 4 the game itself, and 1 and 5 are accepted and ignored in silence.

- **A message about uid 0 is attributed to whoever sent it.** Every other
  message is attributed to the uid's owning army; uid 0 belongs to nobody, so
  the packet's sender slot is used instead.

- **Two calls in it are to ADDR_LOG, which this build has stubbed to a single
  `ret`, and one passes the MESSAGE BUFFER where a format string goes.**
  Reproduced through a typed pointer so the compiler has no opinion, because
  what the image does is call a function that ignores everything.

- **GAME_WON is recorded as GAME_LOST unless 0x00512304 is set.** With winning
  enabled the winner is 1 if it was us and the army otherwise; with it clear,
  the win takes the loss arm exactly. Both arms then write the menu request and
  the state-pending flag BY HAND, without going through `RequestState` -- the
  same pair CLAUDE.md records as the route to the level teardown, reached here
  from a network message rather than from ESCAPE.

- **`0x025C` is "this army is in play".** `ArmyIsInPlay` answers yes for army 4
  unconditionally -- the one every colour lookup treats as neutral -- and reads
  that field for anything else. It is the same field `CommSlotRemote` falls
  back to for an empty slot, which now makes sense of that fallback.

- **The comm receive path is complete.** `CommDispatchMessage` and every
  handler it names are reconstructed; what is left below it is
  `0x0040FBB0`, the SECOND dispatcher, which handles one message out of a
  packet and is 736 bytes of item and unit traffic.

- **`ReceivePlayerMsg`'s loop bound is the address of the next global.** It
  fills `ADDR_ARMY_SETTING` at 0x00515FE0 and stops when the cursor reaches
  `ADDR_SCORE_LIMIT` at 0x00515FF0 -- exactly four slots, whatever count the
  message carries. Second instance of that shape in this image.

- **And the bound check sits in the MIDDLE of the loop body.** A fifth record
  still gets its remote flag set and can still overwrite `ADDR_OUR_SLOT` before
  the loop gives up, because both happen before the check. A reconstruction
  that hoisted the test to the top would be tidier and wrong.

- **A record that is not ours has its remote flag set TWICE**, once before the
  bound check and once after -- and only the second is guarded by the player
  count. Both reproduced.

- **The version-mismatch message names OUR player, not the sender's.** "%s has
  a different version of the game" is filled from `ADDR_DEFAULT_OWNER`, so a
  client that disagrees with the host announces itself.

- **`checkglobals` caught a spelling, which is what it is for.**
  `g_defaultOwner` is `uint32_t` in `objtable.h` and I wrote `int32_t`; that is
  one definition of one address becoming two, and the ratchet refused it before
  it could become a habit.

- **`ADDR_HOST_SLOT` was our slot, not the host's**, and the way it went wrong
  is one this file has recorded three times for FUNCTIONS and not once for a
  global: it was named from a call site. `CommOpenSession` reads it, and the
  machine opening a session IS the host, so the wrong reading and the right one
  agree there and nothing looked amiss.

  `0x0040E117` settles it. The player-created handler writes it only when the
  new player's id equals the comm object's own id -- that is us, whoever is
  hosting. `CommRemovePlayer` confirms it by decrementing this and
  `ADDR_DEFAULT_OWNER` together when a lower slot leaves.

  The host-migration message is the one that looks like counter-evidence and is
  not: it fills "Player %s is now the host" from this slot, naming US, which is
  right because that handler runs on the machine that has just taken over.

  Renamed to `ADDR_OUR_SLOT`, and the two modules reading it renamed with it.
  **Before trusting a global's name, find the site that WRITES it**; a reader
  can agree with a wrong name for a reason peculiar to that reader.

- **The two globals track the same value and can still diverge.**
  `ADDR_DEFAULT_OWNER` is read about 130 times across items, units and weapons
  and is written in one place `ADDR_OUR_SLOT` is not (`0x0040EB57`). They are
  not aliases, and neither is redundant.

- **A three-valued query that only sometimes answers the question it is named
  for.** `CommSlotRemote` reads the 0x020C flag only for an OCCUPIED slot; an
  empty slot that once held someone answers "am I NOT the host" from 0x025C
  instead, and anything else answers -1. A caller treating the result as a
  boolean gets a truthy answer from that -1 as well. Three different questions
  behind one return value.

- **It takes its slot as a SIGNED WORD**, alone in the family -- every other
  per-slot accessor takes a full `int32_t`. A negative would index backwards.

- **These three are PURE and could have vectors.** They reach the comm object
  only through `this`, never through the global, so `tools/vectors.py` could
  record them. That is why they went into `msgslot.cpp` rather than
  `commmsg.cpp`: the module split made a fortnight of an hour ago is exactly
  the line "can this be checked offline", and these fall on the offline side.

- **Next up is `ReceivePlayerMsg` (0x004114E0, 928 B), read but not written**,
  and it is worth knowing what it will cost. Its loop bound is taken from the
  ADDRESS OF THE NEXT GLOBAL -- it walks a table at 0x00515FE0 and stops on
  reaching `ADDR_SCORE_LIMIT` at 0x00515FF0, so there are exactly four entries.
  That is the same shape as the registration table walking up to
  `ADDR_SCRIPT_CONDITIONS`.

  It also puts pressure on two existing names. `ADDR_HOST_SLOT` (0x004FA904)
  is set here to the slot whose id is OURS, which is not what "host slot"
  means, and `ADDR_DEFAULT_OWNER` (0x004F9FDC) is set to the same value in the
  same breath. One of the three readings is wrong. Check both before writing
  that function.

- **`ReceivePacket`'s loop bound is UNSIGNED and that is not a detail.** The
  bogus-length test compares each part against the length the packet arrived
  with, not against what is LEFT, so a part longer than the remainder is
  accepted and drives the length field negative -- and read unsigned, a
  negative length is enormous, so the walk carries on off the end of the packet
  instead of stopping. A signed compare here quietly repairs that, which is not
  what this port is for. Written the first time with `int32_t` and corrected
  before it was committed.

- **A bad checksum does not stop the walk.** It logs, bumps that player's error
  count, and then parses a packet it has just been told is corrupt.

- **And the part length is re-read AFTER the handler has had the bytes**, so a
  handler that rewrote those two bytes would move the cursor somewhere else.
  Three original behaviours in one 256-byte function, none of which any A/B in
  this project can reach.

- **`0x0040FBB0` is a SECOND dispatcher**, for one message out of a packet --
  "Unknown Army Msg Item Type %d, msgtype:%d, item uid: %x; msgsize: %d" -- and
  it takes the slot where `CommDispatchMessage` takes the id. Two layers of
  dispatch, and the names had to distinguish them.

- **The self-naming sweep credited a string to the wrong function, and this is
  the first time that has been caught.** It listed `0x00411BD0` as carrying
  "TIMING OUT PLAYER %d %s". That string belongs to `0x00411C20`; the two share
  one `functions.tsv` entry, and the sweep attributes by ENTRY. So the sweep's
  output is "a name somewhere in this entry", not "this function's name" --
  read the body before believing the pairing, exactly as with `merges.py` and
  the COM ranking.

  `0x00411BD0` carries no string at all. From its body it is the host telling
  us how to send: the value becomes the comm object's SEND FLAGS -- the third
  argument `ArmyMessageFlush` hands `SendGameMsg` for every outgoing packet --
  and two more fields go into our own flow record.

- **It is the one message in the family that ignores its dpid**, looking up the
  flow record by OUR id instead. And if we have no flow record yet the two
  fields are dropped while the send flags are kept anyway.

- **This commit is a clean demonstration of the two coverage figures.** The
  honest one moved 80 bytes; the entry-crediting one moved 704, because
  `0x00411BD0` and `0x00411C20` share an entry and patching one credits both.
  That is the whole argument for `tools/reconstructed.py` in a single step.

- **The pause mask is one bit per player per REASON, and `RemoteGamePause` is
  where a peer's bit moves.** Two independent blocks, not a switch: bit 0x0008
  of the message's flags drives the `0x10 << slot` family and bit 0x10000 the
  `0x20000 << slot` family, and a message carrying both runs both. That is what
  the 767,153 `GetPauseFlags` reads in a Boot Camp run are testing against.

- **A slot above 3 is not clamped, it is not handled.** Each block is four
  explicit compares rather than a shift, so a fifth player would fall out with
  no call made and the mask left at zero -- which then suppresses the log,
  since it is guarded on the mask as well as on verbosity. Reproduced.

- **`GetPauseFlags()` takes no arguments and the call site pushes two.** They
  are the last two varargs of the log line that follows, pushed first because
  cdecl evaluates right to left; the call simply steps over them. Reading that
  as a two-argument function -- which is what it looks like -- would have
  invented a signature. `frame.h` already had it right.

- **`SendMapMsg` was wrong in two ways and its own caller is what said so.**
  Reconstructing `ReceiveStartGameMsg` a commit later showed the call site
  pushing TWO arguments and cleaning eight, where the body reads one -- and all
  three call sites do. So it has two parameters and reads only the first.
  Behaviourally identical under cdecl, but a signature that is wrong in the
  header is the thing CLAUDE.md already warns about under private typedefs.

  Worse, my comment said it tells the other players "which map is chosen". It
  does not: the value is a RESULT CODE. This function sends 7, 0x00411830 sends
  5, and `ReceivedMapMsg` switches on 0..8 calling 4 nominal. I named the
  argument from the function's name instead of from a caller. Both corrected.

- **The seed arrives at 0x0190 of the start record**, straight into the global
  `SendGameStartMsg` chose it in -- so both halves of that story are now
  reconstructed. And "Seed is %d" prints a literal 0 in the RECEIVE half too,
  so the seed is never in the log at either end.

- **One failing player stops the game for everyone.** The failure flag is
  checked once, after the whole loop, so a single `DPLAY ERROR SENDING TO` or
  `FlowQ creation Failure` reaches "Error in start" and nothing starts. A zero
  player count skips both the loop and the check.

- **0x0469 is a seventh window message and nothing handles it.**
  `ReceiveStartGameMsg` is its only sender in the image and `WndProc` has no
  case for it, so `DefWindowProc` eats it. The six in `winproc.cpp`'s table
  were found by decoding forward from each `push` to its `PostMessageA`; this
  one was missed, so that sweep was not complete.

- **The receive side has a dispatcher and it is ours now.** `0x0040FEA0`, an
  eighteen-arm jump table on the message's first dword, reached from
  `0x004026D5` and gated on `0x0404` of the comm object -- which is NOT the
  `0x0400` that `COMM_OFF_STARTED` and `COMM_OFF_LOCAL` both already name.

- **Type 2 returns in silence; types 4, 12, 13 and 16 are LOGGED as unknown.**
  So the original distinguishes a message it knows and ignores from one it does
  not know, and a reconstruction that folded them together would lose a log
  line that only a live session could show.

- **Three different host tests sit side by side in one function.** Types 9, 17
  and 18 return unless this machine is the host -- on top of the identical test
  inside the handler each would have called. Type 8 tests the host only to LOG
  that it should not have received the message, and calls the handler either
  way. Everything else does not test at all.

- **Type 10 pushes two arguments at a handler that takes none.** cdecl, so the
  arguments are simply dropped; `ReceiveEndSetupMsg` really is `void(void)` and
  the call site really does push the message and the sender.

- **`MsgSlotB0`'s first argument is a PLAYER record, not the comm object.** The
  type 11 arm passes what `FindPlayerById` returned. `msgslot.h` describes that
  family as fields of the comm object, which is where the six writers were
  first read; one call site says otherwise and both may be true if the player
  records live inside it, but the header should not be trusted on that point
  until it is checked.

- **`COMM_OFF_STARTED` and `COMM_OFF_LOCAL` are two names for `0x400`**, and
  `checkglobals` cannot see it -- that ratchet tracks `ADDR_` macros, not
  `COMM_OFF_` ones. There is a second family of names with no ratchet on it.

- **The selftest link drew a module boundary, and it drew the right one.**
  `msgslot.cpp` is in `SELFTEST_SRC` because its slot writers, its latency ring
  and `CommRemoveKeyed` are pure functions with recorded vectors. The comm
  MESSAGE family had been growing in the same file, and it can never have a
  vector -- every one of those functions reads the comm object, logs, repaints
  a dialog or plays a sound. The moment `ReceivedMapMsg` needed `PlaySoundAt`,
  which lives in `win32/audio.cpp` where the selftest deliberately does not
  reach, `selftest.exe` stopped linking.

  This is the second time `selftest-link` has caught a real structural
  question, and the answer was different from last time: `SendGameStartMsg`
  moved to the module of the functions it drives, while here the module itself
  had to split. `src/game/commmsg.cpp` is the message half; `msgslot.cpp` keeps
  what can be checked offline. The header stays whole, because it documents a
  field and both functions that touch it, and that is not what the linker was
  asking about.

- **The three lobby settings and their receive halves are done.** `SendMapMsg`
  returns 1 WITHOUT sending when this machine is the host -- the host has
  nobody to tell -- and its log is mislabelled: it prints the ARGUMENT under
  "Error = %d" while the send result it just took is never printed at all. The
  same author's "Seed is %d" in `SendGameStartMsg` pushes a literal 0.

- **`ReceivedMapMsg`'s arms do not line up with its own log text.** The message
  says "Result = %d (4 is nominal)", and 4 takes the same arm as the failures:
  clear the slot's flag and play sound 3. Only 0 sets it. 5 and 7 do nothing,
  as does anything above 8. Taken from the table at 0x00411998, which as always
  in this image is not the order the arms are laid out in.

- **The colour and team receivers differ in the middle and nowhere else.** The
  colour one goes through `CommSetArmyColour`, which SWAPS the colour with
  whoever already had it and can refuse with -1; the team one writes the field
  with no check of any kind. Both then repaint the current dialog and send the
  player list.

- **`make -s check` was committed against while FAILING, and the reason is
  worth knowing.** Its last line reads "all static checks pass" only on
  success, but `tail -1` of a failing run shows whatever the last tool printed
  -- here "generated ok" -- which reads exactly like a pass. The exit code was
  1. `checkclaims` had caught the Lock/Unlock bracket count going from 10 to 11
  the moment `TyperPaint` was written, which is precisely what that tool is
  for. Read the exit status, not the last line.

- **`ab.sh quit` compared the title screen and nothing else.** The only frame
  it took was before the QUIT click, so CONFIRM GAME EXIT -- the dialog the
  configuration exists to reach, and the one that runs the typewriter label and
  the QUIT GAME destructor -- was never in the pixels at all. It takes a `dlg`
  frame now.

  The wait before it is the part that needed measuring. That dialog's body
  reveals one character every 100 ms, so a shot taken too early catches the two
  sides at different characters and is unsynchronised by construction. "Are you
  sure you want to quit?" settles in about three seconds; shots at 4 s and 14 s
  are identical, so six is the margin.

- **The default pixel budget of 500 is too loose for a MENU configuration, and
  that has now been found twice in one session.** Dropping the trailing line
  from `TyperPaint` deletes the whole message and moves **361** pixels; the
  default passed it, exactly as it passed the 336 of the scroll-bar mutation.
  A line of menu text is about 360 pixels, so a budget that cannot see 361
  cannot see a missing line. `quit` is 200 now, with `controls`, `difficulty`
  and `audiovol`.

  The gameplay configurations are a different case and 500 is right there: the
  scene moves between runs and that is the noise floor. The number was never
  wrong, it was being applied to screens it was not measured on.

- **Both typewriter counters are blind**, so the frame is the only evidence.
  `blindspots.py` lists `TyperPaint` and `TyperUpdate` as unable to move --
  every caller is ours, through the vtable. That is why the shot mattered more
  than usual here.

- **The percentage in the table above was an over-count, and had been for the
  whole project.** It asked "does a patched address fall inside this
  `functions.tsv` entry", which credits the WHOLE entry to whichever function
  in it was patched -- the same defect `tools/coverage.py` was fixed for, where
  reconstructing `AudioTimerProc` marked `OpenAudioStream`'s COM calls covered
  a commit before they were.

  It bites hardest exactly where the work is easiest. The fifteen dialog
  destructors are two instructions each, about 400 bytes all told, and they
  moved the naive figure 3.8 points -- because each one sits inside an entry
  holding a whole dialog's implementation. A jump like that is the tell.

  `tools/reconstructed.py` now prints both, splitting merged entries at their
  referenced starts the way `merges.py` already does for the boundary count.
  The honest figure is **25.4%**, the old one 32.1%. It is a tool rather than a
  line of shell because it had been recomputed by an ad-hoc script every
  session, which is how it drifted in the first place.

- **The dialog hierarchy is three deep and the middle level had no name.**
  0x0046FC84 sits under the ICON, whose destructor it jumps to, and fifteen
  full-screen dialogs sit under it -- SELECT MAP, DIFFICULTY, QUIT GAME,
  REPLAY, AUDIO, OPTIONS, DELETE GAME, the overwrite confirm, DELETE PLAYER,
  CONTROLS, SELECT PLAYER, the recruit name box, LOAD GAME, the plain message
  box and the in-game ESCAPE menu. Each is named from the bitmap its
  constructor loads, which is the only thing that tells them apart.

- **Fifteen identical destructors are still fifteen functions in the image.**
  Unlike CommEndSetup, there is nothing here for the linker to fold: each
  stamps a DIFFERENT vtable constant. So the macro in widget.cpp is a way of
  writing them, not a claim that the original had one function -- and the
  reason for it is that fifteen chances to mistype a vtable address are not
  fifteen pieces of evidence.

- **`ab.sh quit` is the check for a destructor, and it is a real one.**
  `ReportLeaks` prints "Unreleased memory (%d) blocks:" on DLL_PROCESS_DETACH
  and that line is inside the compared log, reading (0) on both sides. A
  destructor that forgot a free would move a number that is in the diff.

- **The dialog dispatcher's jump table is another case of layout order lying.**
  0x00426400 has 21 arms selected by 0x00511DBC, and reading the call sites
  top to bottom numbers them wrong: arm 7 is the tenth call laid out. Take the
  order from the table at 0x00426518. AUDIO is id 19.

- **Next in this layer, both read and not yet written.** The class at
  0x0046FD24 is a TYPEWRITER message label: its constructor word-wraps the
  text into a `|`-separated buffer at 0x0058, its update reveals one more
  character every 100 ms and plays a click, and its painter draws the revealed
  prefix line by line. Six confirm dialogs build it, including CONFIRM GAME
  EXIT -- so `ab.sh quit` reaches it. And 0x0046FC5C is a four-sprite checkbox
  with four ink bytes chosen on (focused, checked) and a caption drawn after
  the sprite.

- **The AUDIO CONTROLS dialog is the only screen with a scroll bar**, and
  `tools/ab.sh audiovol` is new for it: OPTIONS -> AUDIO reaches three of them
  -- the SOUND EFFECTS, MUSIC and VOICE sliders -- each with an ltarrow and an
  rtarrow child. `ctl widgets` says class 0x0046FCFC appears nowhere else.

- **And the widget oracle CANNOT see what that configuration is for.** A scroll
  bar's own sprite lives at 0x0064, not at the base's 0x0038, so every bar
  prints `spr=-1` and a wrong bar would dump identically. The pixels are the
  evidence here, which is the reverse of `controls`, where the pixels are blunt
  and the tree is sharp. Ask which of the two can see a given field before
  claiming a screen is covered.

- **The first version of `audiovol` could not fail, and the mutation is what
  said so.** Dropping the thumb offset from `ScrollBarPaint` moved 336 pixels
  against the DEFAULT budget of 500, and the run reported A/B clean. Worse, the
  four arrow clicks it makes to move a thumb were photographed by nothing: the
  `dlg` frame is taken before them and the final frame after CANCEL has closed
  the dialog. Both fixed -- a `mid` frame after the clicks, and a budget of 200
  measured in both directions (clean 45/45/50, mutation 336).

- **The scroll bar's two axes are not symmetric.** x is centred on the widget's
  width less a span field and then shifted by an offset field, which is what
  moves the thumb; y is centred on the SPRITE's height with nothing added. Only
  one axis can scroll, which for a horizontal bar is the point. And both halves
  halve AFTER the subtraction where `WidgetPaint` halves each side BEFORE it --
  a different rounding on odd values, kept as each has it.

- **The arrow children have no constructor.** The scroll bar builds each one by
  calling the BUTTON constructor and stamping vtable 0x0046FCD4 over it, and
  the arrow's destructor is a single `jmp` to the button's -- so it stamps
  VTABLE_BUTTON and never its own.

- **Fifteen dialog destructors are the same two instructions**: stamp my own
  vtable, jump to the dialog base at 0x00454B90. The base is one level under
  the ICON, whose destructor it jumps to in turn. That is the next batch, and
  `ab.sh quit` is its check -- `ReportLeaks` prints "Unreleased memory (0)
  blocks:" into the compared log, so a destructor that forgets a free moves a
  number that is in the diff.

- **Three identical copies of a block is an INLINE FUNCTION, not three
  transcriptions.** The end-of-setup scan appears at the tail of
  `SendGameReadyMsg`, at the tail of `ReceiveGameReadyMsg`, and once on its own
  at `0x00410CE0` with a caller of its own -- instruction for instruction the
  same, modulo register allocation. That is what MSVC does with an inline
  member function it declines at one site out of three. Written ONCE here and
  called from all three, which is both less to be wrong about and closer to
  what the original source said.

- **And the standalone copy was already named.** `ADDR_COMM_END_SETUP` had been
  in `orig.h` since the comm survey; I added `ADDR_SEND_END_SETUP_IF_READY`
  beside it without grepping the address first -- the exact mistake CLAUDE.md
  warns about, within one function of writing a comment about it.
  `checkseams` is what caught it, not `checkpatches`: the alias ratchet counts
  the surplus, and one more alias sat under its baseline. **A ratchet with a
  baseline cannot fail on the first offence.**

- **The handshake pair is not symmetric, and that is the original's design.**
  The ready-to-load pair splits host and client with an early return each. The
  READY pair does not: `SendGameReadyMsg` runs the host scan for whoever calls
  it, because a host marking ITSELF ready may be the last one the scan was
  waiting on.

- **All six handshake functions are done.** The next thing on that path is
  `0x00418F90` -- 24 bytes, a widget activate handler registered by
  `push 0x418f90` at `0x004192C1`: play sound 0, then `SendGameReadyMsg(1)`.
  That is the READY button itself.

- **Closing a seam can move a function to another FILE, and this one did.**
  `SendGameStartMsg` was written in `msgslot.cpp`, and its three comm callees
  turned out to be reconstructed -- so `checkseams` demanded direct calls, and
  direct calls turned reaches-by-address into LINK dependencies on
  `win32/dplay.cpp`. `msgslot.cpp` is in the selftest link, which deliberately
  does not pull in DirectX, so `selftest.exe` stopped linking.

  The fix was not a stub or an exclusion: the function belongs in `dplay.cpp`,
  beside the three methods it drives. `selftest-link` is the check that caught
  it, and its own message predicted the cause exactly.

- **The host picks the shared RANDOM SEED here**, reads it from the clock, keeps
  it in `0x00512314` and sends a copy -- so every machine's RNG starts from the
  same number. That is the only place found that sets it.

- **Two of the original's oddities in one function.** The opening log is NOT
  gated on the comm verbosity field, unlike every other function in this group.
  And the second log says "Seed is %d" while a literal 0 is pushed for it, so
  the seed it reports is always zero and the value actually used is never
  printed. Both kept.

- **Five of the six handshake functions are done.** Only `SendGameReadyMsg`
  (352 B) remains.

- **The original disagrees with itself about a null check.**
  `SendGameReadyToLoadMsg` and `ReceiveGameReadyToLoadMsg` end with the SAME
  two-call lobby repaint, and the receive half tests the dialog pointer while
  the send half does not. One of the two is wrong. Both are reproduced as
  written -- a crash on a null dialog is the original's behaviour, and adding
  the guard to the send half would hide a real difference between the two
  paths.

  Worth noting for the port: if the native build ever wants that guard, it is a
  deliberate divergence and should be marked as one, not slipped in.

- **Four of the six handshake functions are done.** `SendGameReadyMsg` (352 B)
  and `SendGameStartMsg` (256 B) remain.

- **The member-name sweep found exactly three**, and they were worth having:
  `m_ArmyReady` (`0x0274`), `m_ArmyReadyToLoad` (`0x0270`) and `m_pLobby`.
  Two of them are adjacent fields of the 112-byte per-army record, and having
  the name before reading the function made `ReceiveGameReadyMsg` legible on
  the first pass rather than the second.

  Three is a small return, but they are the game's OWN identifiers. Worth
  re-running whenever the log corpus grows.

- **"Occupied" means `player id != -1` only.** `ReceiveGameReadyMsg` decides
  setup is over when every occupied slot is ready, and skips only `-1` --
  while `AM2_PLAYER_ID`'s own note says "0 or -1 is none". So a slot holding 0
  must be ready for the game to start. Left as the original has it, and
  flagged rather than smoothed.

- **Three of the six handshake functions are done.** `SendGameReadyMsg`
  (352 B), `SendGameReadyToLoadMsg` (256 B) and `SendGameStartMsg` (256 B)
  remain -- all send-side, all read-verified only.

- **A log string handed over an original MEMBER NAME.** "Setting
  m_ArmyReadyToLoad[%d] to %s" places that array at `0x0270` of the 112-byte
  per-army record -- the same stride `0x0040F5A0` indexes -- and it is the
  original's own identifier, `m_` prefix and all, not a name of ours. Worth
  sweeping the log corpus for others: a format string that prints a field
  usually names it.

- **The comm side drives the MENU, which nothing had shown before.**
  `ReceiveGameReadyToLoadMsg` repaints the lobby through the current dialog's
  slot 2 then slot 1 -- the same update-then-paint pair the widget layer uses
  everywhere. So the handshake and the widget work meet here, and the widget
  slots being reconstructed is what made this function legible at a glance.

- **`0x0065A058` had two names and one is mine.** `control.c`'s `WD_ROOT` was a
  duplicate of `ADDR_PAINT_OBJECT`, introduced when the widget dump was
  written. Fixed by reuse. The `ctl widgets` root and WndProc's repaint target
  are the same object, which is worth knowing: the thing WndProc repaints IS
  the current dialog.

- **`0x3D8` still has two names**, `COMM_OFF_IS_HOST` and `COMM_OFF_READY`.
  The first is evidenced -- "from DPCAPS_ISHOST" -- and this function's
  host-only gate agrees with it. Not collapsed yet.

- **The six private window messages live in `orig.h` now.** They were defined
  in `winproc.cpp`, which HANDLES them, and the comm side POSTS them -- so the
  first comm function to need one would have duplicated the constant. One
  constant in two files is one too many, and this session has spent several
  commits undoing exactly that for `g_` macros and `ADDR_` names.

- **`ReceiveEndSetupMsg` is done; five handshake functions remain.**
  `SendGameReadyMsg` (352 B), `ReceiveGameReadyMsg` (304 B),
  `SendGameReadyToLoadMsg` (256 B), `ReceiveGameReadyToLoadMsg` (224 B),
  `SendGameStartMsg` (256 B). All self-naming, all in the same band, and none
  exercisable without a second player -- so the whole group is read-verified
  and the A/B can only confirm it does not break single player.

- **The self-naming sweep missed SEVEN more, because it required a colon.**
  `RemoveInventoryItem` logs exactly `"RemoveInventoryItem\n"` -- no colon, no
  arguments -- and the regex wanted `Name:`. Widening it to "the whole message
  is an identifier" finds six comm handshake functions as well:
  `SendGameReadyMsg` (`0x00410A10`), `ReceiveEndSetupMsg` (`0x00410B70`),
  `ReceiveGameReadyMsg` (`0x00410BB0`), `SendGameReadyToLoadMsg`
  (`0x00410D90`), `ReceiveGameReadyToLoadMsg` (`0x00410E90`),
  `SendGameStartMsg` (`0x00411000`).

  So the list is 14 + 7 = 21, and the six comm ones are the handshake CLAUDE.md
  says is verified by reading because it needs a second player.

- **A unit's inventory is six weapon uids at `0x054C`**, with the one in hand
  at `0x0568`. `RemoveInventoryItem` shifts, clears the sixth, and fixes the
  selection -- and the three selection cases are not symmetric: equal resets to
  0 and re-selects, above slides down, below is untouched. Its counter reads 0
  on Boot Camp; nothing there loses a weapon.

- **`UseInventoryItem` (`0x00449760`, 256 B) is READ; four callees need names
  first.** The body is plain enough:

  - In a multiplayer session (`ADDR_MP_SESSION`) it returns immediately unless
    `0x0040F560` approves the unit's army; in single player that test is
    skipped entirely.
  - `comm[0x418]` gates two log lines, so it is a verbosity flag.
  - The item is `unit[0x54C + slot*4]` -- an inventory of uids -- looked up
    through `0x0045EE80`, with `0x0045EE20` (`KindInSetA`, already ours)
    vetting its kind.
  - `item[0xCC]` is a USE COUNT. It is decremented, and only when it reaches
    zero does the item leave: `0x00447990(unit, slot)` clears the slot and
    `0x0044C150(unit, item, slot, 0, unit[0x12])` is the drop, whose log line
    is "droping item:%x".
  - `item[8] |= 2` happens on BOTH exits of that last branch, taken or not.

  **One of the four is now named, and it changes what the function means.**
  `0x0045EE80` looks a uid up through `ADDR_OBJ_BY_UID` and requires kind 4,
  complaining **"uid wasn't a weapon!"** when it is not. So it is
  `WeaponByUid`, kind 4 is a WEAPON, and the "inventory" at `unit[0x54C]` is a
  weapon inventory -- which makes `item[0xCC]` an AMMO count rather than a
  generic use count, and the drop at zero the gun being thrown away when it is
  empty. Named from its own message, not from this call site.

  Three still need reading: `0x0040F560`, `0x00447990` and `0x0044C150`.
  `0x0040F560` and `0x0040F5A0` are a thiscall pair on the comm object -- the
  second indexes **112-byte per-army records** at `comm + army*112` and tests
  fields at `0x020C`, `0x0214` and `0x025C` against `comm[0x3D8]`. That record
  size is worth having on its own; the pair's meaning is not established.

- **`LoadDibFlipped` cannot run in this installation, and that is measurable
  rather than inferred.** Its only caller globs
  `%02d_%03d_%02d_*.msk` out of a `masks` directory, and the GOG build ships
  **no `masks` directory and zero `.msk` files anywhere in the prefix**. So the
  counter's 0 is neither a blind spot -- `blindspots.py` agrees the caller is
  original -- nor a path not driven. The content simply is not there.

  Worth doing anyway: it is 192 bytes, it is on the self-naming list, and the
  reading corrected a swap before it shipped. But it is verified by reading and
  no amount of driving will change that.

- **Two header fields nearly went in swapped.** `hdr[0x14]` is the LISTED SIZE
  -- checked for zero, passed to `malloc`, and returned through the out
  parameter -- while `hdr[0x08]` is the block count only `ReverseBlocks` sees.
  The first draft had them the other way round, which would have allocated the
  block count and reported it as the size. The compiler caught it only because
  an unrelated type error forced a re-read of the same lines.

- **`CreateTimer` is in, and it is what settled the clock.** 1,000 records of
  `{start, period, count, id}` at `0x0050C370`, a slot free when its id is
  zero, and a live count at `0x0050C36C`. Two refusals with DIFFERENT
  thresholds: a low-priority request is dropped past 900 live timers, a request
  with a period over fifteen seconds past 950. So a slow timer outranks a
  low-priority one and both outrank nothing -- an ordinary request is refused
  only when the table is genuinely full.

  A schedule that has already begun is CAUGHT UP rather than fired late: the
  elapsed repeats are counted off `count` and added to `start`.

- **Its one caller passes a period of ZERO**, and that is safe only by
  ordering. With `count == 1` the already-elapsed test answers before the
  catch-up divides by the period. Two fires at period zero would divide by
  zero and nothing in the function stops it. Reproduced with the reasoning
  beside it, because a reader tidying that branch could easily "fix" it into a
  crash.

- **`MsgListRemHead` is in and reads 0**, which is exactly what the earlier
  mutation predicted: single player FILLS the message-buffer pool at startup
  and never draws from it. `MsgListAdd`'s 400 appends were that fill, and
  breaking the list's forward link was invisible for the same reason. The pair
  is now reconstructed and both halves are verified by reading.

- **"Empty List!" is gated on one specific list**, `ADDR_MSG_LIST_POOL`,
  because an empty POOL means the game has run out of message buffers while an
  empty ordinary queue is simply idle. That is the kind of thing a generic
  linked-list reconstruction would smooth away.

- **14 self-naming functions are still original, not 21** -- six of the
  entries were never names at all. A message beginning `ERROR:`, `Error:`,
  `Warning:` or `List:` says nothing about which function printed it, and one
  is actively misleading: `0x004372A0` prints "ERROR: SetObjScriptState was
  called with %s", which names a DIFFERENT function. Taking a name from that
  table without reading the body is how `0x00423200` nearly became "ERROR"
  instead of a DIB loader.

  The real fourteen, and the item and unit ones run during a mission so they
  can be A/B'd:

  `RemMsg` (`0x00401410`), `Resend` (`0x004014C0`), `DestroyFlow`
  (`0x004029B0`), `ArmyMessageFlush` (`0x00410420`), `DefGameParse`
  (`0x00424590`), `itemDeployMessageSend` (`0x0042AA50`),
  `itemDeployMessageReceive` (`0x0042AF30`), `DamageTrooper` (`0x00447A40`),
  `DeployTrooper` (`0x00449250`), `UseInventoryItem` (`0x00449760`),
  `UpdateTrooperAction` (`0x0044AFB0`), `troopMessageReceive` (`0x0044C590`),
  `ExitAllFromVehicle` (`0x0045AE30`), `CreateWeapon` (`0x0045F0C0`).

- **Two ownership conventions sit side by side in one hierarchy.** The list
  box's destructor tests BOTH an ownership flag at `0x0064` and the pointer
  itself before freeing its row array; the icon's and the blinker's release
  their sprites with no null test at all. Neither is wrong -- `ReleaseSprite`
  copes with a null and `free` would too -- but a reader who generalises from
  one to the other will write a guard the original does not have, or drop one
  it does.

- **The vtable survey says the widget layer is done for every menu screen that
  can be driven.** Five screens checked -- CONTROLS, the multiplayer battle
  dialog, DIFFICULTY, the film archive and SINGLE PLAYER -- and what is left on
  any of them is per-dialog DESTRUCTORS in the menu band (`0x0044E4F0`,
  `0x004510D0`, `0x004518E0`, `0x0042FF40`, `0x00455B80`) plus two list-box
  updates. The shared bases cover everything else those screens instantiate.

  So the widget layer proper is finished, and what remains under these dialogs
  is dialog logic rather than widget logic.

- **`BlinkerStart` runs, and finding out how confirmed the unwritten function's
  reading.** It had read 0 on every run and was verified by reading only.
  Driving the DIFFICULTY dialog and moving the pointer across the list rows
  takes it 0 -> 2 -> **10**: two from clicking a row, eight more from moving
  over them. So the analysis of `0x00455340` below is right about the part
  that matters -- a change of hovered row starts a blink -- established by its
  effect rather than by more reading.

  **It needs `mouse move`, not `cursor`.** `cursor` writes the position
  globals and nothing else, so `g_mouseMoved` stays clear and every
  hover-gated path is skipped; the first attempt poked the cursor across all
  three rows and moved the counter not at all. Anything gated on movement has
  to go through the relative path.

- **`0x00455340`, the list box's update, is READ but not written -- 2 KB and
  branchy, and the reading is the hard part.** What it establishes:

  - It opens with an optional per-frame callback at `0x006C`, called with the
    widget, before anything else.
  - Hover to focus like the edit box, then it computes the row under the
    POINTER from the cursor's y: `(cursorY - rect.top - 4) / 14 + topRow`,
    clamped to `count - 1`. The division is the compiler's magic-number
    sequence -- `imul 0x92492493; sar edx, 3` with the sign add-back -- which
    is division by **14**, independently confirming the row height that
    `ListTakeFocus`'s arithmetic gave.
  - When the row under the pointer CHANGES and `0x0094` holds a widget, it
    calls `BlinkerStart(that, 0x46, 1)` -- so moving over a list flashes the
    associated indicator once for 70 ms. That is what the blinker is FOR, and
    it is the first thing found that starts one.
  - The rest, about 1.5 KB, is the mouse-button and keyboard handling.

  Two independent routes to the 14-pixel row height, and a use for the blinker,
  are worth having even before the function is written.

- **`checkglobals` was keyed on the ADDR_ NAME, which made it blind to the
  case it exists for.** Two `g_` names sitting on two `ADDR_` aliases of one
  byte looked like two unrelated globals. It surfaced only because collapsing
  those `ADDR_` aliases made the surplus go UP: `ADDR_SEQ_BAR_BG` and
  `ADDR_PIXEL_FORMAT_BYTE` became one name, and three `g_` names suddenly
  landed on one key.

  Keyed on the ADDRESS it reports **39** where it reported 28. A number that
  rises after a tooling fix is the tool getting more honest, and this is the
  second time this project has seen that -- `merges.py` did the same thing to
  the boundary count.

- **`0x00502AD9` is the palette index this game fills with**, and it had two
  names and three uses: the sequence bar's unfilled colour, the fill
  `AttachPalette` is handed, and now the list box's background. All three were
  local descriptions of one thing. `ADDR_BACKGROUND_COLOUR` replaces both, and
  the three `g_` names with them -- `ADDR_` aliases 31 -> 30, `g_` surplus
  39 -> 37. "Pixel format byte" was the actively misleading one; it is not a
  pixel format.

- **The blinker derives from a one-sprite ICON**, established by its
  destructor chaining to that class's rather than to the base. So the
  multiplayer dialog's "send" dot is an icon that can flash: one sprite from
  the parent at `0x0058`, two more of its own at `0x0060` and `0x0064`, and the
  blink swaps between the latter pair.

  Both destructors release their sprites with NO null test. Worth knowing
  before adding a guard the original does not have -- `ReleaseSprite` is
  trusted to cope, and it does.

- **`tools/ab.sh difficulty` is a third menu configuration**, OPTIONS ->
  DIFFICULTY, six nodes, identical trees and 0 pixels. It exists because that
  dialog is the ONLY place the list box at `0x0046FCC0` is instantiated, so
  its painter (`0x00455180`) and its update (`0x00455340`) are checkable there
  or nowhere. Found by dumping the tree and crossing the vtables against the
  patch list, which is now the standard way to pick a target.

- **`ListDraw` is done, and it is one of the few things in this layer with a
  real defect signal.** Dropping the highlight fill on the selected row -- the
  green bar in SELECT DIFFICULTY -- is **3,299 pixels**, well over budget. Most
  of this layer sits between 0 and 249; a filled row is big enough to see.

- **The list's row records are 260 bytes.** `0x00455180` computes the offset as
  `((idx << 6) + idx) << 2`, which is `idx * 65 * 4`, into an array at `0x0060`
  whose first dword is the row count. It draws rows `0x0074` through
  `0x0074 + 0x0078` and picks an ink from four different fields -- `0x0080`
  normally, `0x0084` and `0x0088` for the selected row depending on whether the
  left mouse button is down, and `0x008C` for another case still. Not yet
  reconstructed; the colour selection needs the rest of the body read.

- **What is left on the two drivable dialogs is now two functions.**
  `0x004510D0` on CONTROLS (2,063 B) and `0x0042FF40` on the multiplayer one.
  Everything else either screen instantiates is reconstructed.

- **The MSVC SEH prologue on a destructor is not reproduced, deliberately.**
  Nothing in this program throws -- VC6's `operator new` answers NULL and the
  game tests it at `0x00451251` -- so the registered frame is never consulted.
  The cost if that were ever wrong is a skipped base destructor during an
  unwind: a leak, not a crash. CLAUDE.md carries the reasoning and the failure
  mode. That unblocks six destructors across the two drivable dialogs.

- **A leaked sprite has no signature in any test here.** `ButtonDestruct`
  releases its three sprites when `0x0075` says it owns them, and suppressing
  that entirely leaves `controls`, `multi` AND `quit` clean -- the last was
  worth trying, because its log carries an "Unreleased memory (N) blocks" line,
  but sprites do not reach that counter. So the release is verified by reading.

  `ButtonDelete` reads 13 on a run that opens and cancels the CONTROLS dialog,
  so the path itself is thoroughly exercised; it is only the RELEASE that
  nothing can see.

- **The two-state indicator is a BLINKER, and it is the toggle's other half.**
  `0x00456D40` flips `0x006C` on a timer, counts flashes down and stops -- and
  `0x006C` is the same field `TogglePaint` reads to choose its sprite, so the
  blink IS the sprite swap. A blink always ends in the OFF sprite whatever
  count it was given, because reaching zero clears the state as well as the
  active flag. On the multiplayer dialog these are the "send" dots.

  Suppressing the flip is **106 pixels** -- under `multi`'s budget of 500, so
  the pixels pass it -- and the WIDGET TREE catches it. Second time the oracle
  has caught something the budget could not.

- **Both drivable dialogs are now nearly all ours.** Crossing the classes each
  screen instantiates against the patch list leaves, on CONTROLS: two SEH
  destructors. On the multiplayer dialog: three SEH destructors and
  `0x0042FF40`. Everything else on both screens is reconstructed.

  So the next real work is elsewhere -- either the SEH destructors, which need
  a decision about reproducing MSVC exception frames, or screens neither
  configuration reaches.

- **`ctl widgets` prints the vtable ADDRESS now, and it turned target selection
  into a lookup.** The CONTROLS dialog uses exactly three of the thirty-three
  classes -- `0x0046FB80` x21, `0x0046FB94` x1, `0x0046FC34` x3 -- so crossing
  those against the patch list said, in one command, that only three functions
  on that whole screen were still original. Two are SEH destructors; the third
  was `0x00450D50`, and it was the key-capture row.

  **Ask which classes are on the screen before choosing what to reconstruct.**
  Ranking by size picks functions that may never run -- `MultiSpritePaint` is
  9,081 calls that never draw. Ranking by what the drivable screens actually
  instantiate picks functions that can be checked.

- **The key rebinding is the best-verified thing in this session.** Click
  CUSTOMIZE CONTROLS, click FORWARD, `key 0x24 tap`: the row reads **`j`**.
  Click BACKWARD and press it again: BACKWARD reads `j` and FORWARD reads
  **`None`**. That is the 95-entry key table, the index, the name pointer, the
  repaint AND the twenty-one-row duplicate-clearing loop, all confirmed by
  looking at the screen. No budget, no second run.

  The 21 rows are the same 21 the widget tree counts on that dialog, which is
  two independent routes to the same number.

- **`MultiSpritePaint` runs 9,081 times and never draws.** Its sprite is null
  on every call on the multiplayer path. Shifting the drawn position five
  pixels changes nothing; returning outright before the blit changes nothing.
  So the placement and the array index are covered and the centring, the two
  intersects and the blit are verified by reading only.

  The probe that settled it is worth reusing: a mutation that DROPS a term
  proves nothing when the term is zero, but one that ADDS a constant
  distinguishes "the field is zero" from "this code never runs". The first
  attempt dropped the y bias, got 0 pixels, and would have been recorded as
  "the bias is zero here" -- which was the wrong conclusion about a function
  that was not drawing at all.

- **Where the second sprite of that class comes from is unknown.** The array at
  `0x0064` holds at most two, because `0x0064 + index * 4` reaches the index
  field itself at 2. Nothing driven so far populates either.

- **A first-seen pointer index cannot see a SUBSTITUTION, and that nearly cost
  the oracle its point.** The widget dump renumbers pointers so it survives the
  heap moving -- the same trick `tools/actdiff.py` uses. But forcing
  `TogglePaint` to the wrong sprite left the tree **identical**: the substituted
  sprite is simply first-seen at the same position and takes the same index.
  `spr=10` on both sides, 212 pixels apart on screen.

  Printing the sprite's own `id` alongside fixes it -- `sid=1576448` against
  `sid=1576449` on exactly the two "send" indicators. **Renumbering makes a
  dump reproducible and blind in the same stroke; carry one real datum beside
  every renumbered pointer.**

- **`ab.sh multi` captures the tree too**, 8 nodes, and it is what moved the
  toggle row of the table from "not caught" to "caught".

- **`drive.sh ctl widgets` is an EXACT oracle for the menu layer, and it is in
  `ab.sh controls`.** The CONTROLS dialog is 25 nodes and they come back byte
  for byte identical from the original and from the reconstruction, so the
  comparison is a `diff` and not a budget. Setting the base constructor's
  `0x0050` to 0 -- which every pixel frame reads as 0 -- changes all 25 lines.

  That converts most of the "not caught" column of the table above into
  something catchable. The remaining pixel-only defects are the ones that live
  outside the tree: `g_charHandler` (a global, 72 pixels) and the list row
  strip (a transient, 0 pixels).

- **Field `0x0040` had to come OUT of the dump.** It is the one the constructor
  never writes, so for any widget whose update has not run it is allocator
  junk -- 25, 1 and 27,346,604 across runs that were otherwise identical. Two
  hand-compared runs happened to agree, which is how it got into the first
  version and why `ab.sh` failed on its first real run. An uninitialised field
  cannot be part of an exact oracle.

- **The defect-signal table, kept current, because it is the honest measure of
  what a clean run here is worth:**

  | defect | pixels | caught? |
  |---|---:|---|
  | button never fires (`ButtonUpdate`) | 306,126 | yes |
  | width from height (`WidgetScreenRect`) | 305,939 | yes |
  | label cleared with ink (`LabelDraw`) | 17,110 | yes |
  | focus highlight never shown (`ButtonPaint`) | 249 | yes, by 43 |
  | toggle always ON (`TogglePaint`) | 212 | **yes**, by the widget tree |
  | handler never installed (`EditTakeFocus`) | **72** | **no** |
  | wrong caret glyph (`EditDraw`) | **34** | **no** -- and not in the tree either |
  | row strip not repainted (`ListTakeFocus`) | **0** | **no** -- unobservable |
  | two constructed flags, focus quirk, repaint deferral | 0 | no |

  `multi` has now measured 0 four times running, so a budget of 150 would catch
  the toggle. It has NOT been tightened: four samples is the same evidence that
  said `controls` was exact before a fifth run gave 45, and repeating that
  mistake the same day would be worse than missing a 212-pixel defect.

- **The list box's rows are 14 pixels tall, and the arithmetic says so.**
  `ListTakeFocus` computes `top + 14 * (0x58 - 0x74) + 4` and clips a strip 14
  tall, which is what establishes the row height, the top margin, and that
  `0x0058` is the row being singled out while `0x0074` is the first row on
  screen. Calling those two the selected row and the scroll origin is the
  obvious reading and nothing here evidences it further.

- **Its distinctive half is unobserved.** Skipping the row repaint entirely
  leaves `multi` at 0 pixels. It is an optimisation -- repaint one strip now
  rather than wait for the next full repaint -- and a settled frame cannot show
  the difference. `ListTakeFocus` runs once on that path, when the TCP/IP row
  is clicked, so what IS checked is the base take-focus underneath it.

- **`ButtonUpdate` is the widget layer's sharpest A/B, by a wide margin.**
  Suppressing the left handler on release -- so no menu button does anything --
  is **30,096 / 29,964 / 306,126** pixels on the three `controls` frames. Two
  orders of magnitude above the focus-highlight and caret defects in the same
  layer, because a button that does not fire means the next screen never
  appears at all.

  Ranked by defect signal, this layer now reads: button not firing 306,126;
  wrong widget geometry 305,939; label colour 17,110; button focus highlight
  249; typed text 72. The budgets only reach the top three.

- **The four classes that use `0x00454310` are the auto-repeat buttons**, and
  the left/right asymmetry in it is reproduced rather than reconciled: with
  repeat enabled the LEFT handler does not fire on first press, only arming the
  250 ms deadline, while the RIGHT handler does. Both then repeat every 150 ms.
  Nothing driven so far reaches the repeat path -- it needs a button held down
  past 250 ms -- so that half stays verified by reading.

- **`controls`' budget of 200 is only just tight enough.** Making `ButtonPaint`
  always show the unfocused sprite -- so no button ever lights -- moves **249,
  249 and 243** pixels on the three frames. It is caught, by 43 pixels. A
  button's focus highlight is simply not many pixels, and the same is true of
  most menu state.

  Together with the edit box's 72, that is the shape of this whole layer:
  correct-looking output and small defect signals. The budgets are near the
  limit of what a whole-frame comparison can do here, and the sharper checks
  are the ones that DRIVE -- press the key, look at where the game went.

- **The edit box types, and that is end-to-end evidence.** `EditTakeFocus` is
  what installs `g_charHandler`, so a field can only receive a character if our
  reconstruction ran. Driving `AM2_MULTIPLAYER=1` to ENTER BATTLE NAME gives
  `EditUpdate` 12,552 and `EditTakeFocus` 1, and `ctl "type Zulu"` puts
  **`Zulu_`** in the field -- text and caret both.

  The caret was predicted from reading the painter, which appends a literal
  `'_'` when `0x0044` is set, and then seen. Prediction first, observation
  second, which is worth more than either alone.

- **`tools/ab.sh multi` is in, and its pixel check is weak on purpose.** It
  drives MULTI-PLAYER -> TCP/IP -> SELECT -> START A WAR to ENTER BATTLE NAME
  under `AM2_MULTIPLAYER=1` and types into the field: 7 identical messages and
  **0 pixels**, three runs.

  It does NOT catch the defect it was built for. Making `EditTakeFocus` skip
  installing `g_charHandler`, so nothing typed ever appears, moves only **72**
  pixels -- "Zulu" in a menu font is small -- and no budget can sit below that
  and still survive a blinking caret. So the configuration covers the PATH,
  12,552 `EditUpdate` calls and a dialog nothing else reaches, and the handler
  itself stays checked by driving and looking.

  **Measure the defect signal, not just the noise floor.** Three clean runs
  said the noise was 0 and that was true and useless; what settles whether a
  budget is worth anything is how big a real error is.

- **`controls` does NOT reach the edit box.** Its key-capture boxes look like
  text fields and are a different class: all five `Edit*` counters read 0 on
  that configuration. The classes that use it are ENTER BATTLE NAME behind
  `AM2_MULTIPLAYER=1` and the campaign's RECRUIT dialog, which CLAUDE.md warns
  against driving. So the edit box is verified on the multiplayer path or not
  at all -- worth an `ab.sh` configuration of its own, since the whole path is
  drivable and CLAUDE.md already records the coordinates.

- **The `dlg` frame earned its keep, and it is the only one that did.** Making
  `FocusLabelDraw` always use the focused colour pair leaves the final frame at
  **0** and the mid frame at **0**, and puts the DIALOG frame **635 pixels**
  over its budget. So the extra shots are not redundant: each covers a screen
  the others do not, and this defect is invisible on two of the three.

  That also answers the note from earlier today about the mid frame "not doing
  what it was added for". The principle was right and the first test of it was
  simply the wrong mutation.

- **A whole subclass in six small functions.** The focus-highlighting label --
  vtable `0x0046FB80`, what the CONTROLS panel builds its captions from -- adds
  nothing to the plain label but a second colour pair, picked on `0x0044` and
  copied into the label's own ink and paper before delegating. That is why
  those two bytes are rewritten every frame instead of being set once by a
  constructor. `FocusLabelDraw` runs 132,192 times opening the dialog.

- **A silenced log looks exactly like a clean run, and that cost five
  configurations.** Patching `0x0045CAA0` -- which is `ADDR_LOG`, folded with an
  empty virtual because both are a single `c3` -- replaced the game's logger
  with a no-op. Boot Camp still loaded, the HUD still drew, the pixel figure
  stayed at its usual 22. Only the LOG changed, from thirteen game messages to
  zero, and the log is the half of the A/B that reports it.

  Two wrong diagnoses on the way, both worth remembering. "The recon side is
  crashing" -- it was not, it rendered a perfect mission. And "my manual run
  works, so it must be ab.sh" -- it did not; I was comparing an unfiltered
  703-line log against ab.sh's FILTERED count, and filtered, my run gave the
  same single line. **Compare like with like before concluding the harness is
  at fault.**

- **`tools/ab.sh all` is CLEAN, all eight configurations**, at `74701d8` --
  the first full-suite pass of this session and the first since the widget
  layer began.

  | config | log | pixels |
  |---|---|---|
  | `bootcamp` | 13 identical | 22 |
  | `windowed` | 5 identical | **0** |
  | `intro` | 4 identical | **0** |
  | `audio` | 13 identical | 22 |
  | `mission` | 13 identical | 172,775 -- live scroll, budget disabled |
  | `campaign` | 14 identical | 2,571 -- budget disabled |
  | `controls` | 5 identical | **0 / 0 / 0** on all three frames |
  | `quit` | 8 identical | **0** |

  `audio`, `mission`, `campaign`, `controls` and `quit` are the five that ran
  blind against a silenced log before the `ADDR_LOG` fix; all five match now.
  `mission` and `campaign` have their pixel checks disabled by design -- two
  unsynchronised runs of live play -- so their evidence is the log, and both
  logs agree exactly.

- **The subclass tails are laid out INDEPENDENTLY, and three classes now prove
  it at the same offsets.** `0x005C` is the font in a label, and in the class
  at `0x00454310` it is the auto-repeat enable. `0x0060` is the label's ink
  colour, the cancel handler in `WidgetUpdateCancel`'s class, and the
  auto-repeat DEADLINE in `0x00454310`'s. So `AM2_Widget` must stop at the base
  -- anything past `0x0054` belongs to whichever subclass is looking, and a
  single struct covering the tail would be wrong three ways at once.

- **`0x00454310` is the mouse update, and it is a button with auto-repeat.**
  Slot 2 for 4 classes, about 800 bytes, and fully mapped now:

  - place, then bail to the base update if there is no parent or `0x004C` is
    set;
  - `0x0040 = PointInRect(rect, g_cursorPoint)` -- the hover flag, and the one
    field `WidgetConstruct` deliberately never writes, because this computes
    it before anything reads it;
  - if the mouse moved, take focus through slot 3;
  - `0x0054` is the LEFT/activate handler and `0x0064` the right one. `0x0054`
    is the same field `WidgetUpdate` fires on SPACE and RETURN, which is a
    clean cross-check between the keyboard and mouse paths;
  - and when `0x005C` is set, holding a button repeats: `GetTickCount` through
    the IAT at `0x0046F084`, **250 ms** before the first repeat and **150 ms**
    between them, with the deadline in `0x0060`.

  Every arm ends the same way -- repaint self with its own rectangle through
  slot 1, then call the base update -- which is what makes 800 bytes out of a
  small amount of logic.

- **The edit box filters what can be typed.** `0x0068` holds a pointer to an
  allowed-character string and the constructor defaults it to `0x004853A8`,
  which is nearly all printable ASCII plus tab. The other one in the image,
  `0x00485360`, is ` a-zA-Z0-9!'&+-_` -- no path-hostile characters, so it is
  the player-name and battle-name filter. Relevant to CLAUDE.md's note about
  typing names: what a field accepts is data, not code.

- **The caret is an underscore, and `flag44` is what shows it.** The edit box
  painter copies its text into a stack buffer and, when `0x0044` is set,
  appends `'_'` and re-terminates before drawing; it also picks ink `0x0064`
  rather than `0x0065`. `0x0044` is set by `WidgetTakeFocus` and cleared by
  `WidgetRepaint`, so it toggles -- which is independently what CLAUDE.md
  recorded as "the blinking caret" in the multiplayer A/B, arrived at from the
  other end.

- **Field `0x0038` is the widget's SPRITE, and that improves an older
  comment.** `WidgetPaint` draws it and reads its bounds to centre it, so it
  is an `AM2_Sprite *`. `WidgetRepaint`'s walk up the parent chain -- written
  a few commits ago as "the first ancestor with `0x0038` set" -- is really
  "the nearest ancestor that has a backdrop to repaint over", which is a
  reason rather than a field test.

- **`flag3C` centres the sprite**, and that is why mutating it in
  `WidgetConstruct` showed nothing: the base constructor's 1 is not what the
  buttons on screen are using. Whether a subclass overwrites it is not yet
  established -- worth a probe rather than another guess.

- **The widget layer's remaining pieces.** All five vtable slots have a
  reconstructed base now, and the two forwarding thunks with them, so every
  class in the array reaches OUR code through every slot it does not override.
  What is left is per-class: the 33 constructors, which is where the subclass
  tails get their meaning, the per-class painters that are not the shared one,
  and the edit box at `0x00454C10`, which owns `g_charHandler` and is the one
  with a text buffer to get wrong.

- **The two focus walkers disagree about what "eligible" means.** Forwards
  (`0x00453DB0`) requires `0x0050` set AND `0x004C` clear. Backwards
  (`0x00453E20`) looks only at `0x0050` and never reads `0x004C` at all. So a
  widget with `0x004C` set is skipped going down and landed on going up.
  Reproduced rather than reconciled: nothing in the shipped menus has been seen
  to set `0x004C`, so which of the two is the bug is not established -- only
  that they differ. Worth a probe that reads `0x004C` across a live dialog.

- **UP is now driven too, so all five of `WidgetUpdate`'s branches are.**
  DOWN, DOWN, UP on the OPTIONS menu leaves the highlight on CONTROLS -- net
  one down from AUDIO -- and `WidgetTakeFocus` climbs to 5, which is the
  cross-check that the walkers actually dispatched slot 3 rather than merely
  running. The walkers' own counters read 0, because `WidgetUpdate` is ours and
  calls them directly.

- **Four of `WidgetUpdate`'s five key branches are confirmed by DRIVING, not
  by pixels.** With the OPTIONS menu up and 408,272 calls on the clock:
  DOWN moves the highlight AUDIO -> CONTROLS -> DIFFICULTY (721 pixels, twice,
  the same signature each time), TAB moves it identically (721 pixels, same
  bounding box), SPACE opens the CONTROLS dialog (305,916 pixels), and RETURN
  on DIFFICULTY opens SELECT DIFFICULTY. UP is the only one not driven, and it
  is the only branch with a callee the others do not share.

  This is the strongest evidence available in this project and it costs one
  run. A reconstruction that CAUSES a transition is checked by driving the
  input and seeing where the game ends up -- no second run, no budget, and it
  discriminates a wrong scancode constant, which no pixel comparison against
  an identically-driven original ever could.

- **Three more fields fell out of the two focus walkers.** `0x002C` is the
  PREVIOUS sibling, so the child list is doubly linked; `0x0050` clear and
  `0x004C` set each disqualify a widget from taking focus; and `0x0054` is a
  cdecl activate handler fired by SPACE or RETURN on release. `0x0050` is the
  flag whose mutation `controls` could not see -- it is read by the focus
  walkers, which that configuration never reaches.

- **ESCAPE closes the CONTROLS dialog, through our code, and that is the best
  evidence in this layer so far.** `WidgetUpdateCancel` runs 73,393 times with
  the dialog up -- once per widget per frame -- and tapping ESCAPE takes it to
  77,370 and returns the screen to the OPTIONS menu. So the branch that was
  reconstructed from reading `!IsKeyDown(1) && KeyChanged(1)` as "the key was
  RELEASED" is the branch that produced a visible state change. Not a pixel
  comparison and not a counter: the game did the thing.

  Worth remembering as a technique. A reconstruction that CAUSES a state
  transition can be checked by driving the input and looking at where the game
  ends up, which is stronger than any budget and needs no second run.

- **`tools/ab.sh controls` still closes with the CANCEL button, not ESCAPE.**
  Both reach the same handler; the click also exercises the button widget's
  own path, so it is the better of the two to automate. ESCAPE is the probe.

- **`LabelConstruct` 21, `LabelDestruct` 21, and that is an invariant.** Every
  label the CONTROLS dialog builds is destroyed when CANCEL closes it, exactly.
  It is the registry invariant's shape -- two counters that must agree or
  something leaked -- and it is the first one this project has in the menu
  layer.

- **The labels are MEMBERS, not heap children, and the counters say so.**
  `LabelDelete` and `WidgetDelete` -- the two MSVC scalar deleting destructors,
  vtable slot 0 -- read **0** while `LabelDestruct` reads 21. A heap child
  destroyed through `WidgetDestruct`'s walk would go through slot 0 and move
  both. Twenty-one calls arriving at the destructor ADDRESS while the deleting
  wrapper is never entered means original code is calling the destructor
  directly with no free, which is what MSVC emits for a member object whose
  container is going away.

  So the child-walk in `WidgetDestruct` is not what takes these down, and
  slot 0 stays verified by reading.

- **`tools/ab.sh controls` closes the dialog now** and compares three frames:
  the OPTIONS menu mid-sequence, the open dialog, and the screen after CANCEL.
  The dialog frame is exact. Closing it is the only thing in the suite that
  destroys a widget at all.

- **`controls` samples two frames now, and it did NOT fix what it was built
  to fix.** `ab.sh` takes a shot between the two clicks as well as after them,
  and the comparer checks every frame a configuration leaves behind. The mid
  frame is genuinely discriminating -- a width-from-height slip in
  `WidgetScreenRect` is 93,347 pixels there, independently of the 305,939 on
  the final frame -- so a defect that only touched the OPTIONS menu would now
  be caught where before it could not be.

  But it does not catch the three mutations that motivated it. Focusing the
  obvious widget instead of the parent's first child still passes both frames.
  So the reason those pass is NOT that the sample was too late; the focus and
  flag state simply does not change what either screen looks like. They stay
  verified by reading, and the next idea has to be a different one -- a probe
  that reads the widget tree over the control socket would settle it directly,
  where more screenshots will not.

  **Extending a test is not the same as extending its reach**, and the second
  frame had to be mutation-checked on its own before it counted for anything.

- **`WidgetTakeFocus` teaches the type of field 0x0034.** It stores `this` and
  `firstChild` there and dispatches a vtable slot on what it reads back, so it
  is a widget pointer -- `focusedChild` -- and not the `int32_t` the struct
  had. Reconstructing a function is how the struct gets learned; the fields
  were named from the constructor, which only ever writes zeroes and cannot
  say what a zero is.

- **The widget vtable has five slots and they are all named now.** Reading all
  33 vtables at once is what did it -- slot 3 is the same function in 30 of
  them and slot 4 in 29, so those are the base's and the rest are overrides.
  0 destructor (all distinct), 1 paint (`0x00454BA0` in 18), 2 click
  (`0x00454BD0` in 17), 3 focus (`0x00454070` in 30), 4 repaint
  (`0x00453FF0` in 29).

  Slot 2 was recorded as "click" for one commit and is the per-frame UPDATE.
  Its three input queries are `IsKeyDown`, `KeyChanged` and a consume, the
  scancode is 1 -- ESCAPE -- and `!down && changed` is a RELEASE, so
  `0x00454BD0` is the base update with a cancel key in front of it. For the
  LABEL, `0x0060` is the ink colour byte rather than the handler, and the
  label's vtable has the base update in slot 2 rather than this one, which is
  what makes that safe and is the clearest evidence the subclasses lay their
  own tails out independently.

- **`WidgetRepaint` runs 3 times on the CONTROLS dialog and its interesting
  branch runs 0 times.** Making it never defer to an ancestor leaves
  `controls` at 0 pixels, so the walk up the parent chain is not taken on this
  path at all; it stays verified by reading. Clicking CUSTOMIZE CONTROLS
  lights the button and leaves the counter at 3, so whatever repaints a
  hovered button is not slot 4.

  That is the next thing to find, and it matters for the whole layer: 1.5
  million `WidgetScreenRect` calls and 78,174 paints are reaching the screen
  through a route that is not the repaint virtual.

- **`ADDR_FONT_SURFACE` and `ADDR_BACK_SURFACE` are gone, and they were both
  wrong.** `0x004FE08C` is the back buffer -- `InitDirectDraw` takes it off the
  primary with `DDSCAPS_BACKBUFFER`, the lock target starts pointing at it and
  `PresentFrame` blits it to the primary -- and it was named for the one thing
  font.cpp does with it. `0x00503100` is the offscreen map surface and was
  called BACK_SURFACE. Both comments in `orig.h` already said the name was
  wrong and that it had been kept anyway because it was spread about.

  **A comment saying a name is wrong is not a correction; it is a note that
  the correction was declined.** Renaming both cost one `sed` and took the
  five `g_` names on the back buffer -- `g_back`, `g_backBuffer`,
  `g_backBuffer2`, `g_backBufferSurf`, `g_fontSurface` -- down to one. The
  `checkglobals` alias backlog went 38 to 34 and `checkpatches`'s `ADDR_`
  count stayed at 31, which is what renaming rather than aliasing looks like.

- **The `checkglobals` backlog is 28 + 15, down from 38 + 17.** Three of the
  four worst entries are done: the back buffer (five names), the draw target
  (four) and the primary surface (four) are one name each. What is left is
  smaller and mostly const-vs-non-const. `ADDR_HWND` through three is the next
  one with an actual claim in it -- one of the three is `g_enumContext` in
  `dplay.cpp`, which says something quite different from "the window".

- **`tools/checkglobals.py` is in, and the backlog started at 38 + 17.** Nothing had
  ever checked the `g_` macros. The first run found 38 surplus names for
  addresses that already had one and 17 names carrying more than one spelling.
  It is a ratchet at those figures -- lower them, never raise them.

  Worth working off in order of how much a name is claiming:

  1. ~~`ADDR_FONT_SURFACE` through five~~ -- done, and the name was wrong too.
  2. ~~`ADDR_LOCKED_SURFACE` through four~~ -- done. Both `SetDrawTarget` and
     `LockSurface` write it, so "currently locked" was half a name; it is
     `ADDR_DRAW_TARGET`.
  3. `ADDR_HWND` through three, one of which is `g_enumContext` in
     `dplay.cpp` -- a name that says something quite different from "the
     window". ~~`ADDR_PRIMARY_SURFACE` through four~~ is done.
  4. The remaining drifts are mostly const-vs-non-const and cosmetic. One is
     not: `g_screenClip` is the ADDRESS in one module and the OBJECT in
     another. A single spelling across the split is impossible for a COM
     pointer -- a flat module may not name `LPDIRECTDRAWSURFACE` -- so a
     couple of these drifts are structural rather than sloppy, and the ratchet
     should not be expected to reach zero.

- **Two of the base widget's constructed flags are not observed at all.**
  `WidgetConstruct` writes 1 into `0x003C` and 1 into `0x0050`; setting EITHER
  to 0 leaves `controls` at 0 pixels. So the exact A/B that catches a
  one-colour error in `LabelDraw` and a width-from-height error in
  `WidgetScreenRect` says nothing whatever about those two fields, and they
  stay verified by reading. Executing is not covering, on a screen where
  almost everything else is.

  Worth chasing later with a probe rather than more mutations: find where
  either field is READ. `0x0044` is read by `0x00453E80` and written by
  `0x00454070`, so that one at least has a known consumer.

- **`g_defaultOwner` was defined four times over three types; it is one now.**
  `objtable.h` had it as `uint32_t`, `audio.cpp` redefined it as
  `const uint32_t`, `dplay.cpp` as `int32_t` -- and again as
  `g_defaultOwnerSlot`, a second name for the same address. Same hazard as the
  local typedef that hid `PlaySoundAt`'s two-pointer compare, and an alias of
  exactly the kind the `ADDR_` ratchet stops one level up, where something is
  watching. The build now compiles with no warnings at all.

  Nothing checks this. `checkpatches.py` ratchets `ADDR_` aliases and
  `checkseams.py` ratchets `orig_` lies, but a `g_` macro can be redefined in
  as many modules and as many types as anyone likes, and only GCC objects --
  and only when two of them meet in one translation unit. `g_defaultOwnerSlot`
  never did meet the others, so nothing said a word about it.

- **`WidgetScreenRect` is the busiest thing in the tree: 1,510,864 calls.**
  Eighty bytes, thirty-three callers, and the shared base helper the whole
  menu hierarchy places itself with. It is also thoroughly CHECKED and not
  merely run -- taking the width from the height field puts `controls` at
  **305,939 pixels** of 786,432, 39% of the frame.

  Ratio worth keeping: 1,510,864 against `LabelDraw`'s 135,490, so roughly
  eleven placements per label draw. The menu repaints far more widgets than it
  paints text on.

- **The rest of the label class is the obvious next unit**, and all of it is
  covered by `controls`: the constructor at `0x00454E70`, the destructor at
  `0x00454EF0` and the scalar deleting destructor at `0x00454ED0`. After that
  the shared base virtuals, which pay for themselves across many classes at
  once -- `0x00454BA0` is slot 1 for about fifteen of them and is a 48-byte
  forwarder, `0x00454070` is 128 bytes, and `0x00453E80` is 496 with 21
  callers and two vtable dispatches in it.

- **The menu widget layer is 33 classes and one of them is now ours.** The
  image lays out thirty-three FIVE-slot vtables end to end from `0x0046FAB8`
  to `0x0046FD38`, each with exactly one constructor and one destructor storing
  it. `LabelDraw` (`0x00454F00`) is slot 1 of the twenty-sixth. That is a whole
  subsystem sized before any of it was read, and `src/game/win32/widget.cpp` is
  where it goes -- the four other virtuals per class, the containers, and the
  edit box at `0x00454C10` that owns `g_charHandler`.

- **`tools/ab.sh controls` is in; its budget is 200 and was briefly 0.** Two
  clicks from the title screen, no typing and no mission, 78,174 `LabelDraw`
  calls. The dialog is exact. The CURSOR is not: about one run in five it
  differs by ~45 pixels inside a 10x13 box wherever the last click left the
  pointer, and three consecutive runs at 0 were enough to convince me it was
  deterministic and not enough to be true. 200 covers the box; the errors this
  screen exists to catch are thousands of pixels.

  It discriminates: clearing the label background with the ink colour instead
  of the paper colour puts it **17,110 pixels** over. Nothing else in the suite
  compares the menu widget layer at all -- `bootcamp` and `campaign` pass
  through the menus on their way somewhere, and the game composes no frames
  while a dialog is up.

- **`am2.Image.refs_to` cannot see a call.** It scans for the address as a
  dword, which finds vtable slots and `push imm32` and nothing else; `call
  rel32` stores a displacement. It answers **0** for `LockSurface`, which has
  38 call sites. Believing it produced a survey saying not one of the 33 menu
  widget classes is ever instantiated -- nonsense that is one screenshot away
  from being disproved, and it was two minutes from being committed as a
  "dead code" finding. `am2.Image.xrefs` decodes and is the one to use.

- **The 400 appends were one loop, and the counter proves it.** Converting
  `dplay.cpp`'s `orig_msg_add` seam to a direct call -- which `checkseams.py`
  demanded the moment `0x00401050` was patched -- took `MsgListAdd` from 400 to
  **0** in one step, with the code behaving identically either way. So the whole
  400 was the pool fill in `CommCreateDirectPlay`, not traffic. This is the
  count-of-0 blind spot manufactured on purpose rather than stumbled into, and
  it is the cheapest way there is to find out which caller a count belongs to.

- **400 appends and the linkage is still unobserved.** `MsgListAdd` runs 400
  times on a campaign drive, which by call count is the busiest thing taken in
  a while -- and breaking the list's forward link entirely leaves `campaign`
  clean. Single player appends to the comm message list and never WALKS it, so
  the structure the function maintains is never read. Another case where a high
  call count says nothing about coverage.

- **Three more import sites are ours, reached through the game's own IAT.**
  `WaitForSingleObject`, `ReleaseMutex` and `PostMessageA` are called through
  their slots rather than by importing the symbols into `am2hook.dll`, which
  keeps `msgslot.cpp` on the flat side of the split -- the handle is an opaque
  pointer and no Win32 type is named. `docs/boundary.md` moves 56 -> 58
  functions and 160 -> 163 sites; "still boundary" stays at 3 and 6, the
  unreachable CD dialogs.

- **The .aai files contain no floating-point numbers either.**
  `DefParseNumber` runs 553 times and `DefParseFloat` -- its strtod twin, with
  the identical shape and complaint -- runs 0. Taken with the earlier finding
  that forcing strtol to base 10 changes nothing, the shipped `.aai` corpus
  uses plain decimal integers throughout and exercises neither the alternate
  bases nor the float path.

- **What is left in event.cpp is the dense half, and the shim run is over.**
  Ten functions remain: the 4096-byte action executor at `0x00420410`, a
  448-byte helper at `0x00421590`, and eight in the 160..192 byte range that
  are NOT more shims -- `0x00420260` and `0x00420300` each open with a
  two-step lookup through `0x00459FB0`/`0x00459FE0`, build a struct on the
  stack, and reach five or six unnamed callees apiece. Read one before
  budgeting for it; the pattern that made the last dozen quick does not hold
  here.

- **"Clear field 0x540 first, but only for type 2" is a recurring step.** Three
  sightings now: `EvtAtPointA`, `EvtObjPair`, and `EvtSetField540` which exists
  to write that field directly. Several actions reset it before giving an
  object something new to do, so whatever it holds is per-order state rather
  than per-object identity. Expect the step in anything that redirects a type-2
  object.

- **Pause reason 8 is "a full-screen bitmap is up", and that names a bit of the
  pause mask.** `EvtShowBitmap` calls `SendGamePause(1, AM2_EVENT_FLAG_8)`
  where frame.cpp calls `SendGamePause(0, AM2_EVENT_FLAG_8)` -- set and clear
  of the same reason. CLAUDE.md records the event flags AS the pause mask
  without naming any bit; this is one named.

- **`showbitmap` selects sub-state 22 and marks the overlay dirty, which
  confirms CLAUDE.md's reading of that table from a caller.** 0x16 is 22, which
  is `AM2_SUBSTATE_BASE` -- the first of the thirteen arms, and one of the nine
  described as "repaint if the overlay is dirty, then DrawMenuOverlay". The
  companion write is to `ADDR_OVERLAY_DIRTY`, already named, which is exactly
  that dirty bit. The two writes are one gesture; the alias ratchet is what
  made them legible, by refusing a second name for the flag.

- **A pair that looks symmetric can be crossed, and drawing the table is what
  shows it.** `EvtFlag40Clear` and `EvtFlag40Set` each have an ordinary arm and
  an ID15 arm, and across those four cells the bit is SET twice and CLEARED
  once, with the fourth cell touching no object at all. Reading them as
  "one sets, one clears" -- the obvious shape for a pair sharing a bit -- would
  be wrong in three cells of four. Writing the two-by-two out before coding is
  cheap and settled it.

- **"Unreachable" was true of the id and false of the name.** `ADDR_SVAR_ID15`
  carried the comment "unreachable", which is correct about the RESOLVER's jump
  table -- no keyword produces id 15. Two functions, `0x0041F570` and
  `0x0041F5C0`, compare a name index against that global directly and take a
  special path when it matches, so the global is live. The comment is corrected
  rather than removed: both halves are worth knowing, and a bare "unreachable"
  invites skipping the functions that use it.

- **A redefinition warning caught a name that was a guess AND a clash.**
  `AM2_OBJ_STATE_REC_SIZE` already meant `AM2_ObjState`'s sixteen bytes; I
  reused it for a 256-byte record in an unrelated table, on the strength of
  calling those records "state" without evidence. GCC said `redefined` --
  audible only because that warning stopped being filtered several commits ago
  -- and the fix was both to rename and to stop claiming what the records hold.
  Two mistakes, one warning.

- **Object kinds 2 and 3 get identical treatment differing by one field.**
  `ScriptSetObjTable` writes `+0x4C0` for kind 2 and `+0x4C8` for kind 3 inside
  the sub-record at `obj+0x6C`, then propagates the same value through
  SetFieldInAll for both. Two of the three kinds CLAUDE.md lists as
  unidentified, and that they are this close together is a fact about them
  worth keeping.

- **Name a function from the body at the OTHER end of its calls too.** The
  alias ratchet refused `ADDR_COND_LOCAL` on `0x00421890`, which already held
  `ADDR_SCRIPT_FIND_FILE`. Reading the caller alone -- a two-armed branch on
  ADDR_MP_SESSION -- suggested "the single-player way of handling a condition".
  The callee's own "%s%d.txt" says it is the mission-script loader, so the
  caller is the end-of-mission router and is named `AdvanceMission` instead.
  The project rule has been "read the body, not the call site"; this is the
  same rule applied downward, and the ratchet is what forced the check.

- **"A zero x means use the object's own position" is a convention of this
  codebase, not a coincidence.** Three functions now use it -- `EvtDeployItem`,
  `ScriptResurrectItem` and `ActionPoint` -- and in every one the test is on
  the LOW WORD alone, so a point with x == 0 and any y counts as absent. Worth
  expecting in anything that takes a packed point.

- **Executing a function is not covering its branches, and the count says so.**
  `ActionPoint` runs 6 times and has three ways to produce a point; removing
  the object-position fallback entirely leaves `campaign` clean, so all six
  calls take the literal path. The variable path cannot even be measured from
  the counters, since `GetVarValue` is ours and reached directly.

- **The event system's authority rule and its off switch are one function.**
  `EventNotify` refuses twice before dispatching: in a multiplayer session only
  the HOST raises anything, and nothing is raised at all while the in-mission
  sub-state is 34 -- the ESCAPE arm CLAUDE.md records ordinary play as never
  being in. So entering that menu stops the event system rather than merely
  pausing the frame, which is a stronger statement than the pause mask makes.

- **A delayed event carries less than an immediate one.** `EventTriggerDelayed`
  takes no masks and no second num/uid pair, so `EventNotify` DROPS four of its
  ten arguments on that path. `delay 0` and `delay 1` are not the same event
  arriving at different times. The `delay > 0` boundary is exact and observed:
  making it `>= 0` puts campaign at 294,304 differing pixels.

- **A log string that made no sense alone is explained by two functions
  together.** `DeployItem`'s own line reads "DeployItem(resurrection)", which
  looked like a mis-copied message when only `EvtDeployItem` had been read --
  that one passes 0 for the third argument and has nothing to do with
  resurrecting. `ScriptResurrectItem` passes 1 for the same argument. So the
  flag is the resurrect flag and the string is accurate; neither caller
  explains it, and both together do.

- **`0x0041F4A0` has 26 callers and is the densest thing left in the module.**
  128 bytes, gated on two globals and a state compare against 0x22, then one of
  two six-argument calls into `0x0041F410` depending on whether an argument is
  positive. Worth doing next on call-count alone.

- **`EvtArmyAtPoint` ships an accumulating offset, and it is reproduced.** With
  `relative` set it copies the point into two registers before the loop and
  never reloads them, so the second matching object receives
  point + first.pos + second.pos and the third gets all three summed. The loop's
  back edge landing after the initialisation is what settles it -- reading the
  body top to bottom suggests a fresh copy each time. Nothing in this port may
  quietly fix a bug the game ships; a mission that happens to rely on the first
  object's offset would change behaviour if it were.

- **A `ret 4` is what makes surrounding stack arithmetic legible.**
  `CommSlotForArmy` is thiscall -- comm object in ecx, one stacked argument
  cleaned by the callee -- and until that was checked, the `mov [esp+8], eax`
  after the call appeared to overwrite the return address. Check the callee's
  epilogue before concluding a caller is doing something impossible.

- **`0x0041F8B0` is not another shim and should not be taken as one.** It is
  the third "At" address, so it looked like a peer of `EvtAtPointA` and
  `EvtAtPointC` -- but it resolves an ARMY through `CommSlotForArmy`, indexes a
  per-slot list at `0x004F9ECC`, and walks every object in it. 192 bytes of
  iteration rather than a guard and a call. Worth reading before assuming the
  pattern holds, which is the same trap the "At" halves already sprang once.

- **`AM2_ScriptAction.relative` is honoured two levels below the parser, and
  finding that corrected a naming guess.** The "At" halves were described last
  commit as plain point-takers, on the strength of the "On" wrappers that call
  them. They are not: each takes its own uid and a `relative` flag, and ADDS
  the object's position to the point when it is set. That flag is the leading
  `+` a script may write on coordinates, which script.h has recorded since the
  parser was done -- this is the far end of it. The lesson is the usual one in
  reverse: a caller can mislead about a callee just as a call site can mislead
  about a function.

- **Hoisting a lookup above its guard is an easy way to change behaviour while
  the code still reads right.** Three of these shims went in with
  `LookupByUID` called before the `uid >= 1000` test, because writing the
  pointer as an initialiser is the natural C shape. The original checks the
  threshold FIRST and only then looks up -- so the hoisted version calls into
  the object table for uids the game never would, moving a counter and running
  code the original does not reach. Caught by re-reading the disassembly beside
  the C rather than by any check; nothing in `make check` or the A/B would have
  shown it.

- **The Evt* shims differ in which check they make, and it is worth recording
  rather than smoothing.** Three patterns now appear in the family: check the
  UID against 1000 (`EvtSetOwner`), check the POINTER LookupByUID returned
  (`EvtSetByte40`, `EvtObjAction`, `EvtDeployItem`), and check BOTH
  (`EvtType2ActionA`/`B`, which test the uid and then hand the possibly-null
  result to ObjIsType2, safe only because that function opens with a null
  test), and check NEITHER, passing a possibly-null object straight on
  (`EvtObjSet`, the unsafe one). Writing them all the same way would lose a
  real distinction, so they are written as found.

- **10 functions are left in the event.cpp band, and most are tiny.** Four
  are 32 bytes, eight are 48, and nearly all have a single caller -- they are
  the `Evt*` shim family this module already holds ten of: check a uid or a
  pointer, look the object up, poke one field or call one thing. They are cheap
  to take a few at a time, and the naming convention is settled. The one that
  is not tiny is the 4096-byte action executor at `0x00420410`, which is the
  last thing under the condition layer.

- **A mutation resolves the count-of-0 blind spot as well as a probe does, and
  verifies the function while it is at it.** `CondRunAction` reads 0 -- its
  only live caller is our own `RunCondActions` -- and CLAUDE.md's standing
  advice is to settle that with a temporary probe. Making it always run action
  0 instead of the i'th puts `campaign` at 294,304 differing pixels, 37.4% of
  the frame. That proves it runs AND that this drive checks what it does, in
  one run, with nothing to add or remove from the harness. Prefer it to a probe
  when the function is small enough to mutate meaningfully.

- **The alias ratchet has now caught four call-site names this run, and each
  time the body's name was better.** `ADDR_ARMY_MESSAGE_FLUSH` vs
  `ADDR_COMM_FRAME_POST_A`, `ADDR_DEF_LINK_PARSE` and `ADDR_DEF_GAME_PARSE` on
  merged-entry addresses, and now `ADDR_SEND_GAME_PAUSE` vs
  `ADDR_EVENT_FLAG_8_SEND`. That last one repays the rename immediately:
  frame.cpp calls it `(0, AM2_EVENT_FLAG_8)`, which under the old name read as
  a flag poke and under the new one is "tell the other players the game has
  un-paused, reason 8" -- consistent with CLAUDE.md's finding that the event
  flags ARE the pause mask.

- **CORRECTION: the "two-word poke" claim was wrong, and the probe that tested
  it says so.** The previous commit asserted that setting `ADDR_VIEW_RECT_ON`
  and a rectangle would push DrawViewRect's whole trio through on the next
  frame. A temporary `poke` command was added to the control socket and it did
  not happen. What the probe DID establish, by sampling counters at every step
  of the drive:

  | point | DrawViewRect | flag |
  |---|---:|---|
  | title, SINGLE PLAYER, player row, SELECT, loading | 0 | 0 |
  | mission live | 621 | 0 |

  So it runs only once the mission is live, roughly per frame, and the flag is
  0 for the whole of a normal drive -- setting it at the title never takes,
  because nothing has called the function yet. Poking during the mission left
  DrawViewRect's own counter unmoved over the following four seconds, which
  means that sampling window was not a rendering one; why is not isolated.
  DrawRect stayed 0 throughout.

  The claim to carry forward is the narrow one: DrawViewRect is per-frame in a
  live mission and gated off, so the trio is reachable in principle. Getting
  the flag set at a moment the function is actually running is the unsolved
  part, and it is more than a two-word poke.

- **The Lock/Unlock pairing is per FEATURE, not per function.** DrawVLine and
  DrawHLine each Lock and never Unlock; DrawViewRect Locks once, draws the
  whole outline through DrawRect, and Unlocks once. So a census of "functions
  calling the bracket" necessarily finds halves, and 29 is not 29 pairs.
  CLAUDE.md now says so.

- **The line trio is MENU drawing, and that makes it drivable -- just not by
  this drive.** `DrawRect`'s only caller is `0x00413610`, which is itself the
  first entry on CLAUDE.md's bracket shortlist, and chasing it upward reaches
  `0x00425EE0` (the menu-request consumer, under `ADDR_STATE2_FRAME`) and a
  widget helper at `0x0044D6D0` with seven callers spread through the
  `0x0045xxxx` menu code. So a rectangle outline is a widget border, and the
  reason all three read 0 is that the campaign drive visits SINGLE PLAYER and
  nothing else. A drive through OPTIONS or the in-mission menu would very
  likely light them up -- the cheapest observability win currently on the
  table, and it needs a drive rather than a reconstruction.

- **A rasteriser is not automatically observable.** I switched to CLAUDE.md's
  Lock/Unlock bracket list precisely because those draw PIXELS and the A/B
  compares pixels -- and `DrawVLine` still reads 0. Its two call sites are both
  inside one 96-byte function, `0x0041CDC0`, which has a single caller of its
  own; the whole trio (`0x0041CBA0` vertical, `0x0041CC40` horizontal,
  `0x0041CDC0` the rectangle that uses both) never fires on a campaign drive.
  Subsystem is not the right unit for guessing observability; reachability is.

- **`0x0041CC40` is `DrawVLine`'s horizontal twin** -- same null-stub branch,
  clips the other pair of edges, and replicates the colour byte into a dword
  for a wide fill rather than stepping a byte at a time. It is the obvious next
  one off the bracket list, and `0x0041CDC0` after it completes the trio.

- **Pathfinding is a whole subsystem the drive cannot see.** `AddRegionLink`
  runs 2,230 times building the region graph at map load, and building NO graph
  at all leaves `campaign` with an identical log and 2,571 pixels. Not the
  dedup rule, not the edges -- nothing. The reason is the drive: it clears two
  dialogs and scrolls, and no unit ever needs to path anywhere in that window.
  So every function in `region.cpp` will be reading-verified until there is a
  drive that makes something walk somewhere.

- **A global that holds a POINTER to a table looks exactly like the table.**
  `0x00514EF0` is `mov edx, dword ptr [0x514ef0]` followed by indexing off
  `edx` -- one indirection, same as the cell map beside it. Writing it as the
  array base took the game down instantly on the first run, with 14
  AddRegionLink log lines and then nothing. CLAUDE.md records the same shape
  for `obj -> table -> slot`; this is the one-level version and it is just as
  easy to miss.

- **Comparing two code arms BYTE for byte is the wrong test.** `FreeItem`'s
  kinds 1, 5, 6 and 8 all call the same destructor, and a `memcmp` of their
  arms says they differ -- because `call rel32` encodes a RELATIVE
  displacement, so identical code at four addresses has four encodings.
  Disassembled, kinds 1, 5 and 6 are instruction-for-instruction identical and
  kind 8 differs only in whether `pop edi` precedes `mov eax, 1`. Reading the
  byte difference as a real one would have produced four spurious cases.

- **A drive that never kills anything never frees an item.** `FreeItem` and
  `RemoveFromItemList` both read 0 on a campaign run: 325 items are added
  during load and none is destroyed in the ~25 s observed. That is what
  CLAUDE.md's long-standing "RemoveFromItemList unexercised" note has been
  recording without saying why, and it is now written down there -- the gap is
  the DRIVE, not a missing code path. A mission driven long enough for
  something to die would exercise both.

- **The `.aai` files are checksummed for peer agreement, which is why none of
  it runs here.** `Checksum` (`0x0042DBB0`) XORs a file's dwords; the wrapper
  at `0x004303B0` chdirs into `aai` and XORs the checksums of `game.aai`,
  `object.aai`, `troop.aai`, `vehicle.aai` and `weapon.aai` into one number.
  That is a data-integrity handshake, so it needs a second player and reads 0
  in every configuration this project can drive -- despite seven call sites.
  Those are the same files defparse.cpp and definfo.cpp parse, so the whole
  `.aai` subsystem now has both halves: what the files MEAN and how two
  machines agree they are the same files.

- **Fifty-three callers, fifteen executions, and the drive still cannot see
  what it returns.** `ResolveUid` is the one place a script name becomes a uid.
  Dropping its type test is invisible; so is returning 0 for EVERY call, which
  was meant to be the control. Both leave `campaign` with an identical log and
  2,571 pixels. So the whole function is verified by READING despite being
  genuinely executed -- the fifteen resolutions this mission performs feed
  nothing the drive watches. Call-site count is not coverage, and neither is a
  counter.

  Note this makes it the second kind of control failure worth naming: not
  "the mutation did not apply" (checked -- the marker was there) but "the
  control itself passes", which means the observation channel, not the test,
  is what is missing.

- **`AM2_ScriptCond`'s `unused28` is the round-robin cursor.** The parser never
  writes it, which is why it went in as unused; `RunCondActions` mode 2 reads
  it as a SIGNED byte, runs that action, and stores `(cursor + 1) % nactions`
  back. A field the writer leaves alone is not evidence that nobody uses it --
  the reader is where to look.

- **`AM2_ScriptAction.extra` really is `onobjstate`'s name, in the third of the
  three roles script.h lists for it.** Mode 3 treats it as a name-table index,
  considers only type-2 entries, and runs the first action whose value equals
  the object's current state. So all three readings of that field now have a
  use site rather than a guess.

- **`0x0041F520` has 53 callers and no name.** 80 bytes, in event.cpp, turning
  a name into a uid. It is the densest unnamed thing left in that module and
  worth doing on its own merits rather than as somebody's helper.

- **The event message's two pass-through fields are FilterMatches' masks.**
  They went into `AM2_EventMsg` as `aux1`/`aux2` two weeks of commits ago,
  positional names, because neither EventMessageSend nor EventMessageReceive
  logs or inspects them. `EventTriggerImmediate` hands them straight to
  `FilterMatches` as `maskA`/`maskB` -- the sets the event belongs to, against
  which an entry's NEGATIVE key is a subset test rather than an equality. They
  are renamed throughout. Neither uid takes part in matching at all.

- **A mutation script that fails to parse produces a clean A/B, and it looks
  exactly like a pass.** A quoting error in the python that was meant to gut
  EventTriggerImmediate's handler loop left the source untouched; the build
  succeeded, the drive ran, and the result read "A/B clean". CLAUDE.md already
  says a test that cannot fail has not passed -- the practical form of that is
  to `grep -c` for the mutation marker between editing and building, which the
  re-run did.

- **The event propagation model, from three functions agreeing.** A locally
  raised event broadcasts once through EventMessageSend and only when `remote`
  is 0, so an event arriving from the wire does not echo; the broadcast happens
  on the first MATCHING entry, before its handlers run. `type` is the bucket
  index straight into the nine-entry table, so the buckets are event types
  rather than a hash.

- **Twelve .aai game constants are parsed and thrown away.** `DefGameParse`'s
  jump table has twenty arms and twelve of them share one target that is
  literally `xor eax,eax; ret` -- vehicle_danger, vehicle_standoff,
  trooper_turn_rate, trooper_pose_rate, trooper_slide_rate, defense_radius,
  attack_radius, attack_hunt, follow_radius, follow_engaged_radius, gravity
  and scroll_speed. Only the eight `roach_*` values reach a global. The
  keywords still parse, so the shipped files remain valid; the values simply do
  nothing in this build. Read the table, not the arm layout -- twelve arms
  pointing at one address is not visible any other way.

- **Not filtering `redefined` caught the same mistake a second time, before it
  shipped.** `ADDR_DEF_GAME_PARSE` already existed pointing at `0x00424590`,
  the merged entry, exactly as `ADDR_DEF_LINK_PARSE` had. Two commits ago that
  cost a wasted run and a wrong patch; this time GCC said so and the build was
  fixed before it was ever driven. The stale name is now `ADDR_DEF_GAME_ENTRY`.

- **`0x00511E04` is static during play but not a constant.** It read 500 across
  every earlier sample and 501 on this run. So "it does not tick" still holds
  -- it did not move across twelve seconds -- but it is not fixed either, and
  the name remains unestablished.

- **CORRECTION to the previous commit: the handler is passed the entry's
  VALUE, not its index.** The dispatcher reads `[eax + 0x476FE4]`, the `+4`
  field, and hands that to the handler. Value and index coincide for entries
  77..96 -- which is every keyword with a handler I had looked at -- so
  "the index IS the command id" survived the off-by-one mutation that shifts
  BOTH. It is wrong as a rule: entry 1 is "trooperlevel1" with value 45. And it
  is observably wrong, not just pedantically: passing the index instead moves
  `campaign` from 2,571 to 2,616 differing pixels, because the entries where
  they disagree have a handler of their own at `0x0044CDA0`.

- **The .aai vocabulary can be read by name, which ends the guessing.**
  Entries 79..94 of the table are rocks, bush, trees, ground, fence, wall,
  bridge, barrel, building, pillbox, aagun, tent, garage, radar,
  miscellaneous, powerups -- exactly DefObjParse's sixteen tokens, in order --
  and entry 95 is literally "link". `AM2_DEF_CMD_LINK = 0x5F` began as an
  inference from a bare `cmp`; it is now the name in the table.

- **A counter can survive its caller being reconstructed if the call goes
  through a POINTER in the image.** `DefFindKeyword` fell 395 -> 0 when
  DefDispatchFile became ours, the ordinary blind spot. `DefObjLine` stayed at
  183 through the same change, because the dispatcher reaches it through the
  vocabulary table's function pointer -- the original address, so still the
  patched entry. Same commit, same caller, opposite outcomes.

- **The .aai handlers are reached through a table, not a call, which is why
  `tools/callsites.py` reports them as having no callers at all.** Neither
  `DefObjLine` nor `DefLinkParse` has a single `call rel32`. Scanning for
  ALIGNED dwords found them in `.data`: sixteen slots at `0x0047739C..0x00477450`
  and one at `0x0047745C`. Those are the `+8` field of entries 79..95 of the
  vocabulary table at `0x00476FE0` -- 12 bytes an entry, `{name, value,
  handler}` -- so the entry INDEX is the command id, 0x4F..0x5E reaching
  DefObjLine and 0x5F reaching DefLinkParse. That is where `AM2_DEF_CMD_LINK`
  came from, now confirmed from the data rather than inferred from a compare.

- **"Index is the command id" is not a reading, it is measured.** An off-by-one
  in `DefFindKeyword` puts `campaign` at 297,845 differing pixels -- 37.9% of
  the frame -- and the save at 317 items instead of 310.

- **No shipped .aai file uses a hex or octal literal.** `DefParseNumber` calls
  strtol with base 0; forcing base 10 changes nothing at all. So the base-0
  behaviour, and the fact that "12abc" is accepted as 12 because only
  `end != tok` is tested, both stay verified by reading.

- **`defparse.cpp` is complete: ten functions, both .aai tables end to end.**
  DefObjParse, DefObjLine, DefAddObjRec, DefFindObjRec for the object records;
  DefLinkParse, DefAddLink, DefFindLink, DefCountLinks, DefCheckLinks for the
  links; DefFreeTables for both. Every one arrived in this run of work, and
  each table is now confirmed from at least three directions -- who packs the
  key, who unpacks it, and what the comparator orders on.

  What is still original below it: the .aai FILE reader that dispatches OBJ and
  LINK lines, `DefGameParse` (`0x00424590`) and `DefParseInfoFile`, plus the
  two shared helpers `0x0041A250` (parse a number, 48 callers) and
  `0x0041A640` (name -> index). Those two sit in the audio.cpp..event.cpp band
  rather than this one, which is why they are not here.

- **Mutate a CONTROL before concluding a field is unobserved.** Two field-level
  mutations of `DefObjLine` passed clean -- keeping `rec[3]` instead of zeroing
  it, and swapping `rec[1]` with `rec[2]`. On its own that reads as "the drive
  cannot see this function". It can: making `DefObjLine` return immediately
  puts `campaign` at 31,494 differing pixels and the save at **25** items
  instead of 310. So the function is observed as hard as anything in this
  project, and the honest statement is narrower and more useful -- those three
  particular slots are not discriminated, while `rec[0]` is (the DefObjParse
  33->34 mutation is caught in the log). Without the control the first result
  would have been written up as the wrong claim.

- **The self-naming sweep attributes a string to whatever `functions.tsv` says
  contains it, and merged entries make that a guess.** `DefObjParse`'s own
  default arm is `or eax,-1; ret` and logs NOTHING. The string "DefObjParse:
  Bad object Constant Type" is at `0x00435C4C`, inside a DIFFERENT function --
  the OBJ-line parser at `0x00435C20`, which `functions.tsv` merges into the
  same 768-byte entry. The sweep got the right name only because a caller
  happens to name the callee it is complaining about. Treat "function X names
  itself" as "something inside X's ENTRY names X" until the entry is known not
  to be merged; that is now three merged entries found in this one band.

- **The .aai chain is verified end to end by one legible mutation.** Changing
  `DefObjParse`'s token `0x5B` from 33 to 34 turns the startup complaint from
  "link 33-4" into "link 34-4". So the keyword maps to 33 in DefObjParse,
  DefLinkParse packs it with PackKey, DefCheckLinks unpacks it with KeyFieldA
  and prints it -- four reconstructed functions and the game's own message
  agreeing on one number.

- **Passing a comparator by ADDRESS rather than as our own symbol keeps its
  counter honest.** `DefFindObjRec` hands bsearch `AM2_IMAGE(ADDR_COMPARE_TRIPLE)`
  because that is what the original pushes; the call therefore still crosses
  the patched entry and `CompareTriple` stayed at 22,535 across the change.
  Handing it `&CompareTriple` would have been one instruction shorter, silently
  correct, and would have zeroed a counter that is currently the best evidence
  the sort is ours. The same choice was made for `ComparePair` in
  `DefCheckLinks` -- where it is also why `ComparePair` fell rather than
  vanished when `DefAddLink` landed.

- **The def-object fallback is load-bearing and heavily observed.** Removing
  the two less-specific bsearches from `DefFindObjRec` puts `campaign` at
  22,125 differing pixels against a budget of 500, and the save drops from 310
  items to 303. That is the strongest mutation signal seen in this run of
  work; contrast the event and message functions, where mutations were
  invisible.

- **A counter going DOWN can be the evidence you wanted.** `ComparePair` read
  3,022 before `DefAddLink` was reconstructed and 1,846 after. Nothing changed
  about the sort: the difference is `DefAddLink`'s duplicate scan, which used
  to cross the patched entry from original code and now calls our ComparePair
  directly. So the drop measures exactly the calls that moved inside the
  reconstruction, and it is a cheap confirmation that the new patch is the one
  doing the work. Worth reading counter deltas rather than only absolute
  values.

- **`siblings` is an INDEX, and three separate uses say so.** DefLinkParse
  fills it with the count of links already sharing the parent; ComparePair
  orders on (parent, siblings); DefAddLink refuses a duplicate on that pair;
  DefFindLink bsearches it. So the field is this link's ordinal among its
  parent's, the pair is unique per link, and the sort DefCheckLinks runs is
  what makes the search well defined. The name in defparse.h says count
  because that is how it is COMPUTED; the comment says what it means.

- **The link table is now confirmed from three sides.** `DefLinkParse` PACKS
  `(type, number)` into the parent key with `PackKey`; `DefCheckLinks` UNPACKS
  the same key with `KeyFieldA`/`KeyFieldB` to print "link 33-1"; and the
  qsort it runs first is given stride `0x14` and `ComparePair`, which is the
  record size arrived at independently from the table's search wrapper. Three
  readings, one layout, none of them taken from the others.

- **The original makes a bare `Log()` call with no format pushed.**
  `DefCheckLinks` does it right after qsort: the callee reads whatever sits
  above the popped arguments. Every observed run has that slot at 0 --
  `trace Log#10(00000000)` -- so the reconstruction passes a literal 0 rather
  than reading its own stack garbage, which would be mechanism-faithful and
  less reproducible. Documented at the call site; the logger is a `ret` here,
  so nothing but the trace line can see it.

- **A name collision in orig.h, not an address collision -- and the build said
  so while a filter ate it.** `ADDR_DEF_LINK_PARSE` already existed, pointing
  at `0x00436080`, the merged wrapper; adding a second `#define` with
  `0x004360C0` meant the OLD one won, so the patch landed on the wrong function
  and the first run logged "'LINK' command not found" with a pointer where the
  command should be. GCC warned `redefined` and my own `grep -v` for the
  pre-existing `g_defaultOwner` warning hid it. Two rules, then: grep orig.h
  for the NAME as well as the address, and never filter `redefined` out of a
  build you are about to trust. The stale name is now
  `ADDR_DEF_LINK_SEARCH`, which is what `0x00436080` actually is.

- **`DefLinkParse` is the first reconstruction in several with a
  DISCRIMINATING A/B.** It runs 49 times at load, and swapping the parent and
  child keys diverges the log ("Saved 310 items") and moves the pixels from
  2,571 to 2,575. Contrast the preceding four, where the campaign A/B was
  clean for mutations too. Worth remembering which subsystems the drive
  actually observes: load-time parsing yes, event records and outgoing
  messages no.

- **A function can be live code, correctly wired, and still unreachable from
  anything the game ships.** `ScriptSetObjBitmap` is one arm of the 4096-byte
  action executor at `0x00420410`, so it is reached the way every other action
  handler is -- but no `.txt` under the prefix names a keyword that gets there,
  and the 185-entry token table has `showbitmap` and `showbitmapnopause` and
  nothing else bitmap-shaped. Its counter reads 0 and always will on shipped
  content. CLAUDE.md already records that 48 of the 59 action keywords appear
  in scripts; this is what one of the other eleven looks like from the inside.

- **`DefLinkParse` is a merged entry, and the tool that would say so is
  silent.** `docs/functions.tsv` files it at `0x00436080` with 512 bytes, but
  that address is a 52-byte table-search wrapper and the function with the
  three "DefLinkParse:" strings starts at `0x004360C0`. `tools/merges.py`
  does not split it -- the same lower-bound caveat its docstring carries.
  Worth knowing before ranking it: it parses `LINK <parent> <n> <child> <m>`
  with seven numbered failure exits, and it is the obvious place to look for
  the unexplained `object.aai` complaint about `link 33-1..4`.

- **A counter of 12 can mean the first line ran twelve times.**
  `ArmyMessageSend` reads 12 on a campaign mission -- matching
  `EventMessageSend` exactly, since the event system is single player's only
  sender -- and every one of those calls returns at the FIRST gate. Probed
  live: the comm object at `0x004FA480` has `dplay` 0, `joined` 0 and
  `playerCount` 1, and the packet length at `0x004FAA6C` still holds its
  initial `0x14`, which is independent proof nothing was ever appended. So the
  whole body -- the size complaints, the copy, the flush lookahead -- is
  verified by READING. The blind spot this project already documents is a
  counter that cannot move; this is the opposite one, a counter that moves
  without the function doing anything.

- **Single player sends event messages that nobody receives, so the campaign
  A/B cannot check what is IN one.** `EventMessageSend` runs 12 times a
  mission -- it is genuinely executed -- and packing `num2`/`uid2` into the
  `num1`/`uid1` slots still leaves `campaign` with an identical log and 2,571
  pixels. `EventMessageReceive` never runs at all here; it needs an inbound
  message. What DOES confirm the 40-byte layout is structural rather than
  behavioural: the sender writes eleven offsets and the receiver reads the same
  eleven back, and a native `offsetof` check reproduces all of them plus the
  0x28 size. The behavioural check needs `AM2_MULTIPLAYER=1` with a second
  player, which is the configuration this project has never had.

- **The self-naming sweep undercounts, and by a known amount.** The 29 figure
  comes from matching `Name:` at the start of a log message. `ArmyMessageSend`
  -- 304 bytes, 20 callers, the transport the whole game sends through -- names
  itself three times without ever using a colon ("ArmyMessageSend Zero length
  message"). Anything relying on that list should treat it as a floor.

- **`EventTriggerDelayed` runs seven times a mission and the A/B does not check
  what it puts in the record.** Swapping `uid` and `removeevent` in the
  16 bytes it allocates leaves `campaign` completely clean -- identical log,
  2,571 pixels. So the field ORDER is verified by reading only, even though the
  function is genuinely executed. The likely reason is that the handler fires
  outside the ~25 s the drive observes. Anything that wants to check it needs
  to reach `ADDR_EVT_RECORD_HANDLER` firing, not just the registration.
  Contrast `UpdateObjectScript`, where a one-field mutation is caught
  immediately -- executing a function often is not the same as observing it.

- **`0x00511E04` is not a clock, and the reading that said so lasted one
  probe.** `UpdateObjectScript` skips an object while `obj[0xBC] >= this` and
  on advancing sets `obj[0xBC] = frame->a + this`. That is a deadline against a
  rising tick in every particular, and it is what went into `orig.h` first.
  Then `dump 511E04` three times over twelve seconds of Boot Camp: 500, 500,
  500, while `ComposeFrame` climbed and `UpdateObjectScript` ran 177,370 times.
  It does not tick. `orig.h` still calls it `ADDR_INPUT_CONTEXT` and still says
  the meaning is unestablished -- now with one candidate positively excluded,
  which is worth more than the plausible name would have been.

- **A clean pixel figure can hide what the log catches.** Passing an object
  frame's `a` where the original passes `b` leaves `campaign` at exactly 2,571
  differing pixels -- the usual number, inside budget -- while the log grows
  `ChangeObjectFrame failed in UpdateObjectScript` lines that are not in the
  original. The mutation is caught, but only by the half of `ab.sh` that
  compares text. Worth remembering when judging a reconstruction whose effect
  is a sprite choice.

- **The audio section is the last in the file, which turns its LENGTH into a
  check.** 68 bytes -- a tag and sixteen zero lengths -- ending exactly at EOF.
  The saver's loop bound is exclusive (`jl`) where `FreeDynamicSounds` walks
  the same table with `jle`, so one covers 16 slots and the other 17. Writing
  the seventeenth would put the file at 176,854 bytes instead of 176,850. The
  two functions genuinely disagree about that table; neither was made to agree
  with the other.

- **All sixteen slots are empty at the autosave, so the populated path is
  verified by READING only.** A dynamic sound has to be looping and active to
  be written, and none is at mission start. What the oracle checked is the tag,
  the slot count and the empty-slot arm; the length-prefixed name and the four
  dwords behind it -- looping, position, priority, owner, which are exactly
  PlayDynamicSound's arguments -- have never been executed. Driving a mission
  long enough to start an ambient loop before the save would close that.

- **A structural parse that lands exactly on the next tag proves every record
  boundary, and the objscript section is the strongest case of it yet.** Four
  nested levels -- 11 scripts, 73 states, 137 frames, 104 actions, one embedded
  string -- walked from the count alone, ending precisely on `0x06660002`. Get
  any record size or any count field wrong and the walk lands somewhere else.
  The sizes the original pushes (0x14, 0x10, 0x14, 0x48) are a second
  derivation of the layouts in `objscript.h` and `script.h`, neither of which
  was written from this function.

- **Do not read "0 differing bytes" in a pointer-bearing section as a stronger
  result than it is.** The objscript section stores three levels of raw heap
  pointer -- 221 dwords -- and this run pair matched on all of them, where an
  earlier pair of the same section differed on 188 bytes, every one inside a
  pointer field. What changed is heap layout between the two runs, not the
  serialiser. So the honest pass criterion is the one applied here: walk the
  section, set the pointer offsets aside, and require everything else to match.
  A future run differing in those 221 dwords means nothing on its own.

- **The savegame oracle needs a control, and the campaign drive is not
  deterministic to the uid.** Two runs of one tree gave 766 differing bytes and
  then 391 -- every difference a UNIFORM shift of every stored uid, by -125 in
  one run and -71 in the other, in exactly the two sections that store uids.
  The two sides reach the autosave having allocated different numbers. So a
  clean comparison (nothing but heap pointers) is strong evidence, and a
  difference in a uid-bearing section is not evidence of anything without a
  control -- the same relationship `ab.sh`'s pixel figure has with its budget.
  It was read as a regression twice before it was measured.
- **A savegame is an exact oracle the log-and-pixel A/B cannot be.** A
  mis-serialised save shows in neither the log nor the screen. Comparing the
  `.sav` two runs produce catches what `ab.sh` structurally cannot -- and it
  took two wrong readings to interpret the first result: "written by original
  code, so noise" (the control disproved it: two ORIGINAL runs differ by one
  byte) and then "a real divergence in object state" (the bytes are heap
  pointers). Run the control, then look at what the differing bytes ARE.
- **The savegame SAVE half is verified by execution; the LOAD half is not.**
  `SaveItems=1` and `SaveScriptCond=91` on the campaign path -- the game
  autosaves at mission start. Every loader reads 0 and none is blind.
- **A zero on one half of a pair says nothing about the other half.** I read
  `CheckSaveTag=0` as "the savegame layer is undriven" and wrote that down. It
  only meant nothing was LOADED: saving goes through `WriteSaveTag`, which had
  no counter because it was not reconstructed yet. The prefix settles it --
  `save/sarge/map1_mission1.sav` is rewritten on every campaign run. Check that
  the counter you are reading is on the path you are asking about.
- **The self-naming sweep replaced the pure-leaf pool as the source of
  targets.** 109 unreconstructed functions below the CRT line push their own
  name in a string. It is a better basis than naming from a call site, which is
  how three wrong names got into `orig.h` before -- every function taken since
  the sweep is named by its own message.

Things believed but not established. Written down so they are not re-derived,
and not promoted to fact without evidence.

- **Type 3 may be vehicles.** `0x0045A9C0`, the type-3 arm of the per-type
  teardown at `0x00428DA0`, sits inside a band that is entirely vehicle code --
  `vehicle mask direction`, `ExitAllFromVehicle`, `Vehicle aai entry not found`.
  That is adjacency, not proof, and the check that would settle it came back
  empty: none of `ObjIsType3`'s 25 caller functions carries a log string at all.
  Types 2, 3 and 8 stay unidentified.
- **Three of `vectors.py`'s 14 pure leaves are not targets.** `0x00427974` is
  the jump table `0x004278E0` dispatches through, not a function -- the split
  list is a lower bound and says so. `0x0045CAA0` is the stubbed logger, which
  IS reconstructed, by `src/inject/gamelog.c`; the scan reads the reconstructed
  set from `patch_replace` calls in `src/game` only and cannot see a harness
  patch. Worth fixing in `merges.reconstructed()`, which is where the same
  lesson is already written down.
- **Some things cannot be checked offline at all, and `MaskPixelSolid32` is
  the clean example.** The only difference between it and `MaskPixelSolid` is
  that its row table holds dword offsets rather than word ones -- and a row
  offset must land inside a 0x8000-byte scratch to be followed, so its high
  word is always zero and the two reads are indistinguishable. Mutating the
  dword read back to a word passes every vector. Raising `SCRATCH_SZ` past
  64K would fix it and would re-cut every vector in the set; not done, and
  recorded here rather than left as a silent gap.
- **A seed that is "valid" can still be untestable.** `ObjMaskBitAt` computes
  `mask.origin - obj.pos + point`, and the first seed put the object at (0, 0)
  -- where subtracting and adding are the same expression. The sign mutation
  passed all 6982 vectors at 100% coverage. Ask of every seeded field whether
  its value makes the operation on it observable, not merely legal.
- **The two mask decoders are one format with a parameter, not a duplicated
  pair.** `MaskPixelSolid` and `MaskPixelSolid32` went in as near-twins
  differing only in a word row table against a dword one, guessed to be one
  source function with the width chosen at compile time. `RemapRleRuns` takes
  that width as an ARGUMENT and walks the same rows, so it is a property of the
  format. A third function is what settled a question two others could only
  raise.
- **A mutation that passes is only evidence when the mutation is a change.**
  Two of `CollapseEqualDeltas`' five mutations left every vector green and
  neither was a gap: both rewrites are provably the same function, confirmed by
  modelling them over 20,000 random arrays. From the test output that is
  indistinguishable from a real hole, so check that a passing mutation actually
  alters behaviour before widening the inputs to chase it.
- **Two tools caught the author in one commit, and both were right.**
  `BuildRgb332Palette` went in as a second name on `0x0041ADE0`, which already
  had `ADDR_FILL_PALETTE` -- the alias ratchet failed the build at 32. And
  `winmain.cpp` reached the same address through `orig_fill_palette`, which
  became a lie the moment it was reconstructed; `checkseams` caught that. Both
  rules are written in CLAUDE.md and both were broken anyway, which is the
  argument for having them as tools rather than as prose.
- **`+0x538` looks like an animation state and `+0x74` like its sequence.**
  `ObjNextKind538` refuses a change to `+0x538` while an unsigned byte at
  `obj->[0x74]->[0x51]` has not reached `[0x44][0]-1` -- a position, a length,
  and a refusal until the end. Its first dispatch arm is exactly the set of
  codes allowed to interrupt, and a current value of 1 never holds anything up.
  Suggestive, not established: nothing yet says what a code means.
- **The comm object's flow control is coming into focus.** `0x004014C0` is the
  ack handler -- `"??? PULSE seq %d latency %d acks for %d msgs %d thru %d"`,
  `"Flow Ack for Message not in sendqueue sequence %d"` -- and it is the only
  caller of both `RingPush32` and `CommRemoveKeyed`. So the 32-dword ring at
  `+0x3A0` that `CommMean32` averages is a **latency** average, which `msgslot.h`
  had guessed from its shape before the writer was found; and the 12-byte
  records at `+0xBC` are very probably the send queue, keyed by sequence number.
  The second reading is the caller's vocabulary, not the function's, so it is
  recorded and not in a name.
- **Object codes 0x18..0x28 are a real vocabulary, and two tables agree on
  which five matter.** `MapCode18To28` maps `{0x18,0x19,0x1A,0x27,0x28}` to
  `{8,2,1,4,6}` and everything between to 0; `ObjCodeUnmapped` answers 0 for
  exactly those five. Two independently transcribed tables in different parts
  of the image singling out the same subset is worth more than either alone,
  and it is a thread to pull on for what the codes mean.
- **100% instruction coverage is not verification, and there are now two
  worked examples.** `RemapBytes` reached every instruction while `count & 3` and
  `count & 7` were indistinguishable, because no count that reached the copy
  path had bit 2 set. Coverage says which lines ran, never whether the values
  that ran them could tell two behaviours apart. Mutation is what says that.
- **Coverage percentages in `--validate` are not being read.** `SetFacing14`
  and `SetFacing08` have sat at **39.3%** for as long as they have existed and
  the line printing it went past every time. The defect found at `1b6d541` was
  in an arm those vectors do reach, so coverage was not the thing that hid it
  -- but a third of two functions is still unvisited and nothing is tracking
  which functions are short.
