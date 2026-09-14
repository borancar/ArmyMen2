# Reconstruction lessons (case studies)

## THE OFFSET-NAMING MISTAKE HAS FOUR SHAPES, and only one is in this file

This file already warns at length about naming a field pointer as a table
base. One session produced four distinct forms of the same underlying error,
and only the first is the one described:

| shape | instance |
|---|---|
| an offset that indexes correctly from the WRONG BASE | `ADDR_SPRITE_GROUPS` -- and BOTH its walkers were wrong, at +4 and +8, with the arithmetic tiling to ten records from either |
| a base correction APPLIED WHERE NONE WAS DUE | sprite pair 3's loader points at its record base; carrying pair 1's fix over would have put that table four bytes early |
| a name belonging to ANOTHER RECORD at the same displacement | five invented `TURNPLAN_OFF_*` over the existing `SIGHTCOUT_OFF_*`, which `checkoffsets` cannot see because a new prefix has nothing to compare against |
| one offset carrying names for SEVERAL TYPES | `0x52C` has three (`VEHICLE_OFF_KIND`, `OBJ_OFF_TABLE_REC_KIND`, `SAVED_OFF_TABLE_REC2`); `0x548` has two, one of them a record SIZE rather than a field |

All four compile. All four are invisible to `checkoffsets`. None is caught by
an A/B, because each indexes *something* correctly.

**The question that catches all four is "which record is this, HERE" -- asked
before the name is written, not after.** Its answers are: what is BEFORE the
field (a negative displacement in this body, or its absence); which base this
caller passes; what other prefixes hold that offset; and what type the object
actually is at that instruction.

Where it cannot be answered, a RAW OFFSET with the reason recorded beats
either candidate name. `orig.h` already keeps a dozen `FIELD_` placeholders on
that principle; this extends it to offsets that have too MANY names rather
than none.

## ARITY AND IDENTITY ARE DIFFERENT QUESTIONS, and answering one feels like both

`Step3TurnBlocked`'s arguments went in reversed after a reading that was
correct at every step. `tools/espmap.py` gave the frame slots; tracing which
register takes which slot was right; reading which offsets each register
touches was right, and they separated cleanly into object fields and record
fields. **None of that says which CALLER VALUE lands in which slot.**

I had read the call site -- to count the pushes, because the `add esp` was
lying about arity as usual. Having done that, checking it again for argument
IDENTITY felt redundant. It is not: the pushes answer "how many", and only
knowing what the caller's registers HOLD answers "which".

The result is the ADDR_ENTER_VEHICLE shape exactly -- internally consistent,
every field access on the other record, nothing wrong from inside, and it
compiles. Here it also could not be caught by running: both configurations
passed with it installed, because the function is cold.

So the rule below is two rules. Read the call site for the COUNT, and read
what the caller put in each slot for the ORDER, and record which instruction
settled each.

## Take every signature in a family from its CALL SITES, before writing any of it

Fifteen functions implementing five roles three times -- two hardcoded row
pools and one over a SEQ context -- and reading the five generic BODIES gave
two wrong signatures out of five. Both would have compiled.

- `SeqCtxInit` takes **five** arguments, not the three its first stores
  suggest. The caller pushes `0x20, 0x20, 0x28, 0xC8, ctx`, and the two extra
  are RowAlloc's width and height, which the hardcoded versions inline.
- The generic release takes the **context first**, not the record. Read off
  the body it looks the other way round, because the prologue's own pushes
  shift every `[esp+N]` and I had not counted them.

**A body shows the arguments a function USES; only the caller shows how many
there are and in what order.** And `ret N` does not help here -- all of these
are `__cdecl`, so the epilogue carries no size at all. CLAUDE.md's rule about
comparing `ret N` is about STDCALL and is silent about these; I cited it
confidently one message before checking, which is its own lesson.

`tools/espmap.py` is the tool for the second failure: it normalises every
`[esp+N]` to a frame slot, so an argument read at two different depths is one
slot rather than two. It settled a question three readings had not -- the loop
bound and `capacity` are the same slot -- in one command.

**AND THE SAME SUBSYSTEM CAN CARRY ITS SLACK IN TWO PLACES.** The hardcoded
pools have `capacity + budget` slots and test `count > capacity`; the generic
context has exactly `capacity` records and tests `count > capacity - margin`.
Same protection, opposite implementation, and writing either from the other's
outline runs the array off its end. Where a family is a REWRITE rather than a
re-emission -- 0.222 similarity here against 1.000 between the twins -- assume
nothing carries over but the shape.

## Five things the last function taught, all of them cheap

**AN OFFSET WITH NO PREFIX IS INVISIBLE TO `checkoffsets`, and that is the
hole this file only ever described from the other side.** It already says a
NEW prefix has nothing to compare against. The same is true of a displacement
read straight out of a disassembly -- `[edi + 0xc]`, `[ebx + 0x94]` -- which
arrives attached to no family at all. Two fields were written up as "no reader
before now" when both had been named twelve lines apart in `orig.h` for
months. Grep the BARE NUMBER; the checker cannot.

**DIFF BEFORE DECLINING TO MERGE, not only before merging.** The rule here is
"where the original repeats itself, diff the BYTES before believing the
repetition", and it exists to stop a merge that flattens a real difference. It
is worth exactly as much in reverse: two copies of one loop were declared "not
identical" from reading them, and `difflib` over normalised disassembly showed
48 instructions against 50 with every difference an `edi`/`esi` swap bar the
log line. The refusal to share a helper was as unfounded as a merge would have
been. Thirty seconds settles it either way.

**PRINT THE WHOLE LITERAL POOL AT ONCE.** Four separate errors in one function
came from reading strings out of fixed-size disassembly windows: a string
truncated by the window and reasoned about as a complete phrase for three
commits (`"PULSE Adding to "` is `"PULSE Adding to freelist from sendque
Buffer seq %d  elelment %x"`); a second log line just past the end of a dump,
silently dropped; and two strings that differ only in `%d` against `%x`, which
is why one helper had to take the format string as a parameter. One command
dumps every `push imm32` that points at a C string. **A literal that ends
without punctuation mid-phrase is the tell.**

The house style is what actually caught the truncation: while a function was
still being reversed, this tree took format strings from the image through
named `ADDR_STR_` constants rather than inlining them, so writing the call
FORCED the real text to be fetched. A convention that makes a class of mistake
impossible to write down beats one that documents it.

**THAT CONVENTION IS FOR REVERSING, AND ONCE A FUNCTION IS DONE THE STRING
FOLDS BACK INLINE.** The `ADDR_STR_`/`ADDR_FMT_`/`ADDR_MSG_`/`ADDR_NAME_`
indirection was a crutch that kept the real text one fetch from the image while
the code around it was still being read; a finished reconstruction should carry
its string literals inline, the way the MSVC source did. 462 sites across
src/game were folded that way (2026-09-11): every `(const char *)AM2_IMAGE(
ADDR_STR_X)` and `(const char *)(uintptr_t)ADDR_STR_X` became the literal, so
the native/standalone builds no longer read those strings out of the carried
blob. The fold is byte-safe because the literal was produced FROM the image and
asserted to unescape back to the exact bytes -- the same "fetch the real text"
discipline, applied once and frozen into the source. What did NOT fold is what
is not a string: `ADDR_MSG_*` window-message codes, the `MSG_LIST_*`/
`NAME_TABLE_*` pool and state pointers, and `ADDR_STR_AVI_DIR`, which is a
pointer-to-pointer. Those stay macros.

**WHICH MODULE A FUNCTION GOES IN IS DECIDED BY THE LINK, NOT ONLY BY
`checksplit`.** That tool asks whether a file NAMES a Win32 or COM type. A
function can name none and still belong elsewhere: four new functions went
into `air.cpp` because that is the original's translation unit, and `air.cpp`
is in `SELFTEST_SRC`, so calling `MsgListCopyByKey`, `CommSend` and
`DumpMsgList` -- all in `win32/dplay.cpp` -- broke a link that has no DirectX.
`make` was clean; only `make check`'s link guard failed. `commmsg.cpp` was the
right home and said so itself, carrying a forward declaration of `CommSend`
with a comment explaining the very constraint I had re-created one file over.

**ANCHOR A SCRIPTED INSTALL EDIT ON THE LAST `patch_replace`, NOT ON THE
CLOSING BRACE.** An install function ends `return 0; }`, so an edit anchored
on the brace lands AFTER the return -- the dead-patch defect this file records
from `dist_install` and `savetag_install`, where four reconstructions were
never installed and every tool read them as done. `checkpatches.py` was
written after that incident and this was the first time it caught the thing it
was written for. The lesson recorded then was to anchor on something every
install function has; a closing brace qualifies and was still not enough,
because what matters is what comes immediately BEFORE it.


## A field pointer named as a table base (the commonest mistake)

**A FIELD POINTER NAMED AS A TABLE BASE IS THE COMMONEST MISTAKE IN THIS
PROJECT, AND FOUR MORE LANDED IN ONE SESSION.** `ADDR_RANK_RECORDS` was four
bytes late, `COMM_OFF_PLAYERS` twelve, the roach step's "control record" was a
window into an object, and `ADDR_ARMY_INK` was byte 1 of
`ADDR_OBJ_TABLE_RECORDS`. Only the first two predate the session; the last two
were introduced BY the person writing this warning, one of them within a single
batch of correcting another.

The shape is always the same and it is always invisible at the time: a consumer
that touches ONE field indexes correctly from any base that puts that field
where it expects. Nothing can see the error until a second consumer wants an
earlier field.

What actually catches it, in order of how often it worked here:

- **Grep the ADDRESS before naming anything**, and grep it as a bare hex
  number, not as a name. Three of the four would have been caught by one
  `grep 0x004F9AC` before the `#define` was written.
- **Ask what is BEFORE the field you are naming.** A record whose "first" field
  has things written twelve bytes below it is not starting where you think.
- **A stride you have to define is a stride that probably already exists.**
  `AM2_ARMY_INK_STRIDE 0x100` was a second spelling of `AM2_OBJ_TABLE_REC_SIZE`
  and that alone should have stopped the edit.


## Control-flow and indirection pitfalls (from 'after writing a function')

**ONE PREDICATE ASKED TWICE CAN BE USED IN OPPOSITE SENSES, and writing both
the same way is a defect no pixel can see.** `CreateTrooper` calls
`ObjIsFriendly` at its head and again at its tail: the first is `je` past an
OR, so `OBJ_FLAG_REVEALED` goes on when the answer is NON-zero, and the second
is `jne` past a call, so `ObjConceal` runs when it is ZERO. Both were written
as the negation and half of that was wrong. `bootcamp`'s object-state dump
caught it -- eight lines differing by exactly 0x800, with the log identical and
the pixels at their usual 22 -- which is the fifth time that artifact has found
something the verdict line would have called clean. Two calls to one predicate
are two branches to read, not one.

**Grep for how the tree already reaches a GLOBAL before writing an expression
for it.** The naming rule -- grep the address before inventing a name -- has an
exact analogue for ACCESS, and ignoring it cost three defects in one function.
`EnsureSpriteAaiRecord` indexed `ADDR_AAI_RECORDS` and `ADDR_RECORD_LISTS` as
though the global were the array; both hold a POINTER to it, the original loads
`mov eax,[0x51614c]` and only then indexes, and `objtype.cpp` already spelled it
`(*(void ***)(uintptr_t)ADDR_RECORD_LISTS)[n]` in three places. A fourth
spelling went in without the grep. The one global in that batch reached through
an existing macro -- `kScriptNames` -- was right first time.

The payoff is not tidiness, it is that DIVERGENCE BECOMES VISIBLE. `ObjAttachTo`
gates two arms on `ADDR_MP_SESSION` and I had written the comm object; what
exposed it was that `ObjsAreAllied`, four hundred lines up in the same file, is
the SAME inlined block and gates them on `mp`. Had the surrounding lines been
written in a private idiom there would have been nothing to line up against --
and no A/B could have caught it, because `ADDR_MP_SESSION` is 0 on every drive
this project has, so both arms are unreachable and the run comes back clean.

**An exit NOTED is not an exit reproduced.** `ObjInitCommon`'s header comment
said "both exits set eax deliberately" and the body was then written as two
independent `if`s with a shared tail. The original's no-cells case is an EARLY
RETURN that also skips `ItemLinkCells`, so every object without a cell list was
linked into the map twice; both configurations truncated at the same point in
map load, 37% of pixels differing. Write the exits first, from the epilogues,
and count them -- 4 `ret` instructions can be 5 return PATHS sharing one, which
is what `ObjAttachTo` does.

**Verifying an ADDRESSING MODE is not verifying INDIRECTION DEPTH.** The same
function's record loop was "verified against raw bytes" -- `8b 46 0c` / `8b 00`,
no SIB, no index -- and that reading was correct and beside the point. It
established there was no index; it said nothing about the chain being
`array -> records[0] -> sprite -> key`, three loads where I had written two. A
byte-level check of one instruction cannot answer a question about a sequence of
them. CLAUDE.md already warns that `obj -> table -> slot` needs two
dereferences; this is the same trap one level along.

**Three defects in one session, and NOT ONE was a wrong offset.** Every one was
control flow or indirection depth, and `tools/checkoffsetuse.py` reported
byte-identically before and after all three fixes -- correctly, since it compares
which offsets are NAMED. Do not read a clean offset check as a clean function,
and do not stack a second unverified unit on an unverified one: both bugs
produced the identical failure signature, and telling them apart depended on
only one being in flight.
