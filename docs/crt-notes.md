# CRT reconstruction notes

**THE CRT IS RECONSTRUCTED LIKE THE GAME, under `src/platform/crt/`, and
its names carry a `crt_` prefix.** The image's MSVC 6 runtime is 231
functions the game reaches 44 of, and glibc behind those names is a second
runtime with its own tie order, heap and float formatting -- each a frame
the hybrid and the native build can disagree on. So it is derived from
the disassembly the way `src/game` is, one function at a time, each
declaration opening with its address; `crt_` because the host's libc owns
the plain names; the game reaches them through the `ADDR_CRT_*` seams in
`standalone.h`, so nothing in `src/game` changes; and its state is the
image's, `ADDR_RAND_SEED` and the rest, so original and reconstructed code
share it. Verification is by enumerating oracle where the function is pure
over memory a stub can supply -- `tools/qsortcheck.py` runs the original's
sort under Unicorn with a comparator stub in the scratch page and replays
the recording through the C in `tests/selftest.cpp` -- and by lockstep for
the rest. A mutation that cannot fail is stated as a theorem in the tool,
not left as a gap: which side of a partition `qsort` pushes first changes
nothing but the stack depth.

**THE HYBRID IS THE CRT'S ORACLE, AND IT NEEDS NEITHER WINE NOR AN
EMULATOR.** `build/armymen2-hybrid-dev` runs the ORIGINAL CRT over
`src/platform`, so its `fopen` reaches our `CreateFileA` through the IAT
and its `fread` our `ReadFile`; link `src/platform/crt` beside it and the
reconstruction reaches the same two by name. `AM2_CRTCHECK=<dir>` takes
over at `WinMain`, after the original's startup has built the heap and the
handle tables, and `src/hybrid/crtcheck.cpp` runs one scripted sequence
of stdio calls through each stack on its own copy of a generated file,
comparing every return value, errno, the FILE's fields, a hash of the
bytes and the file left behind. Where Unicorn needed a hooked `ReadFile`
and a Python model of a file, this needs nothing: the same platform is
under both, so only the CRT is being compared. `tools/crtcheck.py` is it
in `make check`, in seconds. Reach for this shape before an emulator for
anything the CRT does through kernel32.

Two things it settled on its first run that reading had not. A field can
be STALE rather than wrong: neither `_getstream` nor `_openfile` writes
`bufsiz`, so a fresh FILE carries whatever the slot's last user left, and
the comparison reads it only while a buffer exists -- an exact oracle over
shared tables has to know which fields the API can see. And the original
has undefined behaviour the reconstruction reproduces exactly: a read on
a stream whose last operation was a write leaves `cnt` at -1 through
`_filbuf`'s error arm, after which the next `fwrite` copies
`min(remaining, 0xFFFFFFFF)` bytes into the buffer. Both stacks took the
process down on that until the scripts stopped asking; a mutation that
makes writes fail reaches it again through a failed flush. Say what a
corpus must NOT do as well as what it reaches.

**A CRT FUNCTION THAT READS A TABLE STARTUP FILLS IS RIGHT AND EMPTY IN
THE NATIVE BUILD, and the two have to be told apart when it is compared.**
`getenv` walks `_environ`, `_mbctoupper` reads `_mbctype`, `__tzset`
consults both; the original's startup fills all three and nothing in the
native build does, so there they answer as if the table were empty --
which is faithful and is not what the original answers. The hybrid is
where the comparison is exact, because its startup has run. The other
half is state the two stacks SHARE in that process: `__tzset` runs once
and `time()` caches its minute, so whichever stack runs first does the
work and the second inherits it. Reset that state before each stack, or
the second stack's computation is never compared at all -- it was not,
until the timezone globals were snapshotted after each run.

**AN ORACLE OVER SHARED STATE IS COUNTERFACTUAL OR IT IS NOTHING, and a
snapshot makes it exact.** The heap check's first version asked "free a
block with one stack, then does the other hand the same block back?" --
and failed on a correct reconstruction, because after a free the block
has merged with its neighbours and the next allocation comes from the
head of whatever list serves it; the original promises nothing of the
kind either. What the two stacks CAN be asked is the same question from
the same state: snapshot every committed page, every region's bitmaps
and the five globals, run the operation through one stack, digest, put
the snapshot back, run it through the other, digest, compare. 24,279
operations that way, and every one of thirteen mutations fails. The one
thing a snapshot cannot undo is an unmapped region, so a step that
releases one is counted and skipped, not compared. Three of the
thirteen survived the first corpus and every survival was the corpus:
a "permutation" whose product overflowed before its modulus and so left
363 blocks unfreed, and no group ever empty; a growth probe that
alternated stacks, so the original repaired the table the mutation
left short; and no free entry in an earlier region while the scan
pointer sat on a later one. A mutation that passes names the input the
corpus lacks; build that input before calling the gap a theorem.

**A TABLE THE RECONSTRUCTION WALKS NEEDS ITS SLOTS REWRITTEN, NOT ONLY
ITS TARGETS RESOLVED.** `mkglobals` had resolved all 21 C++ static
initializers by name for years and generated a function that called them,
and the initializer table's own slots in the carried `.rdata` still held
the original's jmp thunks -- which the scan could not match, since a thunk
is neither a patched address nor a seam. The moment `_cinit` was
reconstructed and walked the table as the original does, the native build
jumped to `0x00408AB0` and died. Each entry is now rewritten by position
with the name already resolved for it. The general form: a fixup pass
proves only that what it could MATCH is rewritten; anything reached by
walking a table has to be checked at the table.

**AND A BLOCK'S OWNER DECIDES, NOT ITS SIZE.** A block reallocated above
the threshold and back down stays HeapAlloc's -- the original keeps it
there -- and the check, classifying by size, freed it through both
stacks: a double free of the host's block that MALLOC_CHECK_ reported
sixty operations later as corruption in the reconstruction. Ask
`__sbh_find_block` whose it is, which is what free itself does.

**A BYTE LOOP IS NOT THE ORIGINAL'S DWORD LOOP WHERE THE INPUT IS
UNDEFINED, AND THE VECTOR SET ASKS FOR UNDEFINED INPUTS.** `strcpy` and
`strcat` were written as byte loops, correct for every well-formed call;
the original walks the source a dword at a time and writes the tail from
the dword it has already read. On `strcat(p, p)` with a one-byte string --
which the generator produces, since two pointer arguments are made equal
on purpose -- the original writes two bytes and returns, and the byte loop
reads the byte it has just stored and fills memory until the selftest
dies at `0x19191919`, the pattern byte it was copying. The loop is the
original's now, for strcpy, strcat and every idiom that shares it.

**AND THE COMMIT THAT CARRIED THAT CRASH WENT IN BECAUSE `exit 2` WAS
READ AS PROSE.** The selftest's log ended in `exit 2` and the grep for its
summary line found nothing, which is exactly what a crashed run leaves,
and the commit command ran anyway. A run's exit status is the verdict;
read it before anything else, and never commit on a summary line that
did not print.

**A MUTATION PASS THAT RESTORES WITH `git checkout` DESTROYS AN UNTRACKED
FILE'S WORK AND RESETS A TRACKED ONE'S.** Nine mutations over two new
modules, restored with `git checkout -- FILE` after each: the untracked
one was left carrying two mutations at once, and the tracked one -- whose
committed state was a stub -- was silently replaced by that stub, so the
third mutation failed to build and the fourth's anchor was gone. Restore
from a copy taken before the edit, and assert the file equals it after.

**THE CRT'S FLOAT FORMATTING IS INTEGER ARITHMETIC, AND ITS CONSTANTS ARE
THE IMAGE'S.** `%6.2f` goes through a twelve-byte long double, a five-word
schoolbook multiply that rounds half to even at a guard word, a table of
powers of ten and a digit loop that multiplies by ten and reads the top
byte -- not one x87 instruction decides a digit. So it reproduces exactly
in C, and the powers of ten, `"e+000"` and the decimal point are read out
of `.data` and `.rdata` through `AM2_IMAGE` rather than transcribed; only
the 0.1 that `$I10_OUTPUT` builds on its own stack is a literal. One
idiom cost three of 1,793 cases: `mov al,[esi]; inc esi; test al; jne`
leaves the pointer ONE PAST the terminator, so a `[esi-2]` after it is
the last character, not the one before it. Write the walk as
`while (*p++)`, not `while (*p) p++`.

**THE ORIGINAL'S CRT IMPORTS ARE THE ORIGINAL'S CRT'S.** All 56 kernel32
entries the platform did not have -- heaps, virtual memory, the
environment, std handles, file I/O, find-file, code pages, string
classification, time, exit -- are called only from above `0x00465000`, the
statically linked MSVC runtime, and never from the game. `kernel32crt.cpp`
emulates Microsoft's runtime, not 3DO's, which is why a Win98 `GetVersion`
and a Latin-1 `GetStringTypeA` are enough.
