# Analysis-tool notes and build/kill hazards

**RE-BASING A SHARED MACRO NEEDS AN AUDIT, NOT A GREP.** Moving
`COMM_OFF_PLAYERS` twelve bytes to the record's real start meant every use had
to gain a compensating field offset. One of twenty-six was missed --
`widget.cpp:7890`, wrapped across two lines so a `grep -n 'rec + COMM_OFF'`
never saw it -- and it silently wrote a computer player's name twelve bytes
early. `ab.sh mpoptions` caught it outright: the widget tree lost rows, the
chat record came back empty and the frame moved 253,227 pixels.

The fix is a script, not more grepping. Walk every use with its next two lines
and assert each one carries a field offset or a stride; that check is three
lines of Python and it found the one that eyes did not. **A macro whose VALUE
changes is not a rename** -- it is a change to every site at once, and the
sites are what have to be enumerated.

The control mattered here too, and nearly lied a second way: the first two
control runs failed with "produced no game log lines" because a stale
`ArmyMen2.exe` still held port 31436 from the failing run. Check `pgrep` and
`ss` before believing a control, which this file already says and which is easy
to skip when you are chasing something else.

**`tools/checkoffsets.py` has one blind spot and it is the PREFIX, which was
demonstrated rather than reasoned.** One edit created two duplicates of the
same two fields: `ITEMTYPE_OFF_COOLDOWN`, which already existed at the same
value under the same prefix and was refused outright, and
`WEAPON_OFF_LAST_FIRED` for `0xC4`, which the tree already had as
`ITEM_OFF_LAST_USE`. Only the first was caught. A NEW prefix is a new namespace
and the checker has nothing to compare it against, so the second would have
landed silently and been a second name for one field forever.

So "grep the OFFSET before naming it" means the offset, not the prefix -- and
the case where it matters most is exactly the case where the prefix is new,
because that is when the checker cannot help. `0xC4` alone would have found it
in one command.

**Sharing a VALUE is not the same as being a duplicate, and only one of the
two is worth collapsing.** `orig.h` holds three names for 4 --
`AM2_COMM_SLOTS`, `AM2_COMM_ARMY_COUNT`, `AM2_REVEAL_ARMIES` -- and four for
15, and none of those is a defect: comm slots, armies and reveal grids are
three concepts that happen to number the same, and a footprint's weight step
is not a passability threshold. What IS a defect is one concept under two
spellings, which is what `AM2_TILE_NEIGHBOURS` and `AM2_TILE_NEIGHBOUR_COUNT`
were -- the same table's bound, both in use in the same file. Collapsed. Any
tool for this class has to key on the concept, not the number, which is why
there is no ratchet and probably cannot be a cheap one.

**Offset macros have no ratchet at all, and that is where the fourth duplicate
of the session landed.** `checkpatches.py` counts `ADDR_` aliases and
`checkglobals.py` counts `g_` ones; nothing counts `OBJ_OFF_*`, `AM2_*` or the
other plain constants. So when `OBJ_OFF_ROW_COUNT` and `OBJ_OFF_ROWS` were
defined a second time with the same values, and `AM2_ROW_STRIDE` invented
beside the existing `AM2_OBJ_ROW_STRIDE`, everything compiled and every check
passed -- an identical redefinition is legal C, and the third name is simply a
different spelling of a number.

It was found by reading `TakeOffMap`, which had been using the originals all
along. Until something counts them, the only defence is the same one the
`ADDR_` rule states: grep for the OFFSET before defining a name for it, not
just for the name.

**THE NEXT PROSE RATCHET WAS TRIED AND IS NOT WORTH BUILDING, which is worth
saying so nobody builds it twice.** After checkprose.py the obvious extension
is to flag MACRO NAMES cited in the docs that no longer exist in `src/` --
this file names plenty, and several were renamed in one session
(ADDR_EVT_PAD_HANDLER_A, ADDR_BATTLE_JOIN_DRAW, ADDR_COMM_SYNC_CHECK,
COMM_OFF_SEND_COARSE). Scanned: fifteen hits, and not one is a defect.

Eleven are deliberate HISTORY -- ADDR_STARTUP_4249C0, ADDR_GAME_DIR_ALT and
the rest of the alias table exist in this file precisely because they were
deleted, and AM2_ROW_STRIDE and AICTX_OFF_OBJ_10 are named as duplicates that
were collapsed. Removing those mentions would delete the finding. The other
four are the scan's own fault: `ADDR_STR_` is a prefix rather than a macro,
and AM2_AB_PIXELS, AM2_AB_TRACE and AM2_MAKEVARS are environment variables,
all three real and all three defined in tools/ rather than src/.

So the class is dominated by correct usage, and a check over it would be a
noise generator with a caveat attached -- which this file already says is
worth suspecting before it is worth believing. checkprose works because
"this address is still original" is a claim that can only be true or false;
"this name is mentioned" is not a claim at all.

**`tools/checkglobals.py` ratchets the `g_` macros, and there is a large
backlog behind it.** `src/game` reaches the original's globals through macros
like `#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)`, and
nothing was checking them at all — so one global ended up with four definitions
across three types, one of them a second name. The `ADDR_` ratchet in
`checkpatches.py` would never have seen it: the `ADDR_` name underneath is the
same in every case, so grepping the address finds them all and they look
consistent.

Two rules, both already applied one level up. An **alias** is one address
reached through two `g_` names. A **drift** is one `g_` name defined with two
different expansions.

It found **38 surplus names and 17 surplus spellings** on its first run, so it
is a baseline that may only go down, not a clean bill of health. Some of the
backlog is harmless const-vs-non-const; some is not.

**The example this paragraph used went stale by being fixed, which is the good
way for prose to go wrong and still worth correcting.** It named
`ADDR_FONT_SURFACE`, reached through five names including `g_backBuffer` and
`g_fontSurface`, and said settling it meant reading what the surface is for
rather than renaming the loser. That was done: `InitDirectDraw` takes it off
the primary with `DDSCAPS_BACKBUFFER` when fullscreen and makes a plain
offscreen surface of the same size when windowed, `PresentFrame` blits it to
the primary, and it is `ADDR_BACK_BUFFER` now. Read the tool's own output for
what is outstanding, never this sentence.

What the backlog actually holds today is mostly SPELLING rather than
misreading, and that is a cheaper job than the one described above:
`g_originDX` beside `g_originDx`, `g_originDY` beside `g_originDy`, `g_curX`
beside `g_cursorX`, `g_clipRect` beside `g_screenClip`, `g_comm` beside
`g_commObject`. Those need no reading at all — they need one spelling. The ones
that do need reading are the DirectSound buffer slots, where `g_dsBufA` and
`g_dsPrimary` are on one address and `g_dsBufC`, `g_dsound` and
`g_movieDSound` on another, and a name there is a claim about which object it
is.

**Count the surplus, not the addresses that have any.** The first version
counted addresses with more than one name, and a fifth name landing on an
address that already had four did not move it — which is exactly where a new
alias is most likely to appear, and it passed when tried. Counting
`len(names) - 1` fails in both directions, tested both ways.

**`am2.Image.refs_to` cannot see a call, and reading it as though it could is
how three separate things in this file were nearly recorded as dead code.** It
scans a section for the target as a little-endian dword, which finds vtable
slots and `push imm32` operands and nothing else — `call rel32` and `jmp rel32`
encode a displacement, so the address is not in the byte stream at all. Asked
about `LockSurface`, which has 38 call sites, it answers **0**.

The failure is quiet and it is convincing: a survey of all 33 menu widget
classes came back "not one is ever instantiated", which is nonsense the moment
you look at a menu. `am2.Image.xrefs` decodes instead and is the method to
reach for; `refs_to` answers the narrower question and now says so in its
docstring. The check that either is working is to point it at a function whose
call count is already known.

**`tools/checkcallers.py` ASKS THE OTHER HALF OF THE BLIND-COUNTER
QUESTION: not "can this counter move" but "can this function be reached at
all".** A reconstruction whose callers in the image are ALL reconstructed is
reachable only from our own source, so if nothing in `src/game` names it, the
detour is installed and nothing jumps to it. That is `Step3Input` exactly --
1,424 bytes of a driven vehicle's whole input handling, installed, counted as
done by every tool, and called by nobody, because our `StepType3` had lost the
arm that reaches it. No vehicle in the game could be steered and every check
was green; its counter read 0, which is what a healthy function reached from
our own code looks like.

It found three more on its first run and they are in `KNOWN`, a baseline that
may only go down. The useful tell is that `MsgSlotB0` and `MsgSlotA2` pass
while `MsgSlotB1` and `MsgSlotB2` fail: same caller, same family, so it is a
missing dispatch arm rather than a dead function.

**`tools/espmap.py --site=0xADDR` answers what ONE instruction reads**, which
its per-slot listing cannot: that truncates to four addresses, which is fine
for a survey and useless when you are reading a call site's arguments. It
prints the frame slot, which argument it is, and how many bytes of outstanding
push shift the raw displacement, so a `[esp+0x24]` that is really ARG2 says
so. **Run it on every argument-slot read in a body that pushes.** Two defects
in one afternoon were exactly that shift, and each had a correct sibling four
lines away: `Type2PlayerInput` read the OBJECT's +0xC0 where the original
reads the WEAPON's, so shift-clicking to fire dereferenced NULL and killed the
process; `FireWeapon`'s lobbed arm passed the clamped RANGE where the original
passes the launch HEIGHT, so a grenade aimed further flew shorter.

**`tools/checksplit.py` keeps the split honest, in both directions.** It fails
if a flat module names a Win32 or COM type — or reaches a Win32 header
transitively, since a header can be the thing that leaks — and equally if a
`win32/` module names none, which would mean something is filed as platform
code that is not. Tested both ways round: a bare `HWND` in `rect.cpp` and a
`frame.cpp` with its three platform references renamed away both fail it.

Comments are stripped before scanning, and that is not fussiness. `script.cpp`
carries a comment saying it forward-declares `PreloadSprite` rather than
including `win32/sprite.h` precisely BECAUSE `AM2_Sprite` has an
`LPDIRECTDRAWSURFACE` in it — a scan that reads comments fails the very file
that documents the rule.

**Four tools derive "what is reconstructed" by scanning these sources**, and
all four used a non-recursive `listdir` before the split. Adding a
subdirectory would have made every one of them miss the whole `win32/` half
silently and report the boundary as barely started. They now share `am2.game_sources()`
— one definition, for the same reason `tools/merges.py` imports
`coverage.REGISTERED` rather than copying it.


## Build and install / process-kill incidents (from CLAUDE.md)

**`drive.sh stop` once took the whole login session down with it, and the
mechanism is a shell idiom rather than anything about this game.** The
process-tree walk built its next generation with

```
kids="$kids $(pgrep -P $(echo "$kids" | tr ' ' ',') ...)"
```

and `tr '\n' ' '` leaves a TRAILING SPACE, which becomes a TRAILING COMMA in
the PPID list. `pgrep` reads an empty list element as PPID **0**, so
`pgrep -P "1234,"` answers **1 and 2**. The first pass put init into the list,
the second asked for every child of init, and the `kill -KILL` that followed
reached `user@1000.service` -- `Main process exited, code=killed, status=9/KILL`
in the journal, and every terminal, browser and editor on the desktop with it.

It only fired when a `desktop=amii*` process was still alive at `stop` time,
which is why it was intermittent rather than constant.

Two defences now, because neither is obviously sufficient alone: the walk
carries a `frontier` that is never empty and never has stray whitespace, and
the kill list refuses pid 1 and 2 whatever the walk produced. Anything that
expands a PID list and then signals it wants the second check -- the cost of
being wrong is not a failed test, it is the user's session. A surviving game also
keeps holding `ArmyMenMutex`, which silently makes the next run in that prefix
exit; `tools/drive.sh stop` walks the process tree for this reason.

**`pkill` on a SCRIPT does not kill the game it started, and the leftovers
produce completely convincing false failures.** Three in one session: an
`ab.sh` whose reconstructed side "produced no game log lines"; a black screen
with an empty log while the control socket still answered; and an `mpoptions`
run that sat on the wrong screen because
`control: bind/listen on port 31436 failed (10013)` -- a stale instance still
held the port, so every drive command went nowhere. Each read exactly like a
broken reconstruction and none was.

The mutex is only half of it. The control PORT is the other half, and it fails
differently: with the mutex the new game exits, with the port it runs happily
and ignores everything you tell it. After killing anything, check BOTH before
believing the next run:

```
pgrep -c -f 'ArmyMen2[.]exe'      # want 0
ss -ltn | grep 31436              # want nothing
```

and grep the run's own log for `bind/listen`, which says so outright.

**AND THE CURE FOR THAT -- MATCHING ON SOMETHING BESIDES THE EXE NAME -- HAS
ITS OWN FAILURE, WHICH COST HALF A SESSION QUIETLY.** A `/proc` sweep that
excludes the caller's own ancestor chain is the right shape, and the one used
here then narrowed on `"wine" in cmdline or "explorer" in cmdline` to avoid
matching shells. The game's own processes do NOT say either:

    C:\GOG Games\Army Men II\launcher.exe C:\GOG Games\Army Men II\ArmyMen2.exe

so every "cleaned N" that sweep printed was counting the wrapper and leaving
the GAME running. Two instances survived a dozen kills, kept
`ArmyMenMutex` and port 31436, and the next suite failed with `bootcamp/orig
produced no game log lines` -- the exact symptom this file already documents,
diagnosed at once and caused by the tool that was supposed to prevent it.

**The EXE NAME is the signal; the ancestor chain is what excludes the caller.**
Do not add a second predicate about how the process was launched, because the
launcher's command line is the game's own path and says nothing about wine.
And check BOTH conditions afterwards -- `ss -ltn | grep 31436` as well as the
process count -- which this file already says and which a "killed 0" makes
very easy to skip.

**A SHELL WAITING FOR A COMMAND MATCHES ITSELF, AND THE BRACKET TRICK DOES
NOT SAVE IT.** This file already records `pkill -f 'ArmyMen2.exe'` matching
the killing shell, and the cure -- bracket the pattern. That cure does not
reach the case where the waiting shell's OWN command line CONTAINS the
command it is waiting for:

```
nohup tools/ab.sh bootcamp campaign > out 2>&1 &
while pgrep -f 'tools/ab[.]sh' >/dev/null; do sleep 25; done
```

The brackets stop the pattern matching itself as written, and the shell's
command line still holds the literal `tools/ab.sh` from the `nohup` clause --
so the loop waits for itself and never ends. Three of these were left
spinning across a session, and each one reported its background task as
FAILED long after the A/B it was waiting for had finished CLEAN. The runs
were fine; only the waiters hung.

The reading it produces is the dangerous part and it goes both ways. A
`pgrep -cf 'tools/ab[.]sh'` answered **3** with no suite and no game running,
which is exactly the answer that argues against running `make check` -- and
`make check` is a build, so a false "a suite is running" is as capable of
stopping real work as a false "nothing is running" is of voiding a run.

Wait on the PID (`wait $!`, or `kill -0`), never on a pattern that the
waiter's own arguments contain. And when the question is really "is it safe
to build", ask the two things that cannot match a shell: `pgrep -c -f
'ArmyMen2[.]exe'` and whether the control port is held.

**IT HAPPENED A FOURTH AND FIFTH TIME IN ONE HOUR, so the mechanism is a
tool now: `tools/pidfd.py wait PID [TIMEOUT]` and `tools/pidfd.py kill PID
[SIG]`.** A waiter on `pgrep -f 'timeout 900 make check'` spun after the
check had passed, and the `pkill -f` meant to end it killed the shell
issuing it -- one command before `git commit`, which therefore never ran. A
pidfd names one process for as long as it is open: it becomes readable when
that process exits and a signal through it cannot reach a reused pid.
Capture `$!` when a job starts and use nothing else to wait for it.

**AND `$!` IS NOT ALWAYS THE PROGRAM, so `kill` signals the descendants
too.** Two `strace` runs from a leak hunt were killed through the tool and
WAITED FOR, and the wait returned -- because the pid was a wrapper. The
tracers were reparented to init with their games and ran for three and a
half hours: 1.9 GB resident each from the very leak being hunted, and two
UNLINKED trace files of 9 GB and 2.8 GB held open on a tmpfs /tmp, which
the user found at 16 GB after resizing it. `du` could not see them, since
the names were gone; what did was walking `/proc/*/fd` for links ending
`(deleted)`. `pidfd.py kill` walks the pid's descendants deepest first
now and prints each one it signals, and takes `KILL` as well as
`SIGKILL`, which it did not. After any kill, read `df` and the deleted-fd
walk, not only the process list: a dead name can still be a live file.


## checkclaims / drift-check incidents (from CLAUDE.md)

**AND IT KEPT A SECOND LIST OF ITS OWN, which drifted from `make check`'s
within one commit.** `ORACLES` in `checkclaims.py` names the tools whose
subjects count toward the verified split, and `make check`'s recipe names the
tools it runs. Adding `pathplancheck.py` to the recipe alone left the split
reporting 28 when the tool it had just gained made it 29 -- so a check that
exists to stop numbers going stale had a hand-kept list going stale inside it.

The two are not the same set and cannot simply be merged: `scriptcheck` is in
one and not the other. What is checked instead is the direction that bites --
a tool declaring `CHECKS` and missing from `ORACLES` is an ORPHAN and fails
the run, because a declaration nothing reads is a check nobody is counting.
Tested by removing the entry again, which reports it by name.

**AND IT NOW READS `STATUS.md` TOO, which had drifted on every number in
it.** That file summarises where the work is, and nothing was checking it:
1,641 patches against 1,643, 41 analysis tools against 57, and a verification
split of 12 of 32 against 31. Its own header says it "can be stale between
updates" -- which is the warning this file already says is not a defence
against the thing it warns about.

Two of its three numbers are checked, and the third is deliberately left. The
patch count belongs to `checkpatches.py`, which exposes no function to ask,
so checking it here would mean a second copy of that scan -- the drift this
is meant to stop, one level down. Say which numbers a check covers and why
the rest are absent.

It catches a tool whose output changed without being regenerated — tested by
making `coverage.py` print a different heading, which fails the target. It does
NOT catch a hand-edit to a generated file, because the tools rewrite those
before git is consulted; the edit is healed silently rather than reported.
