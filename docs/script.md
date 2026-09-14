# The script interpreter

## The script interpreter

The game ships its missions as readable text -- `data/<map>/<map>N.txt` and
`rules/*.txt`, 109 files under the prefix -- so this is the one subsystem whose
names come from the program's own vocabulary rather than from us. The chain is
`WinMain -> RunFrame -> ADDR_STATE2_FRAME -> LoadLevelScript (0x00425060) ->
ReadScript (0x00444CD0) -> NextToken (0x0043F450)`.

**`0x00444C40` was never in that chain**, and it sat in this file for several
commits as though it were, under a name -- `ParseLine` -- that is as invented as
`ParseScriptFile` was. `ReadScript` tokenises with `NextToken` directly and
never calls it. A call chain is worth checking with a cross-reference rather
than assuming the obvious middle step exists.

What it actually does comes from its only caller. `0x00417B80` carries
`Cheat!!!`, `I am the Juggernaut!`, `I can fly!` and `Aye aye Captain!`, so the
typed line is a **cheat code** and `0x00444C40` is what runs one. A function
that names itself nowhere can still be identified from the one that calls it.

**AND THAT LAST STEP WAS EXACTLY ONE STEP TOO FAR, which decoding the caller
settled.** `0x00417B80` reaches `0x00444C40` on its NO-MATCH path: the typed
line is compared against all 39 cheat phrases, and what is handed on is
whatever was *not* a cheat. Two arms call it as well, with `trigger greenwins`
and `trigger tanwins` -- which are script statements, not cheats. So it is a
SCRIPT LINE runner, and `orig.h` has had it right as `ADDR_SCRIPT_RUN_LINE`
for some time while this paragraph went on calling it the cheat runner.

The reasoning that produced the error is worth keeping, because it is the
rule's own failure mode. Identifying a function from its caller gave the right
NEIGHBOURHOOD -- a typed line, a console -- and then one inference too many
assigned it the caller's subject. A callee reached from a caller's default arm
is characteristically the GENERAL case, not the special one the caller is
named for. `0x00417B80` is also not its only caller's only call to it: it
calls it in three places.

**And the name `ParseScriptFile` was mine, not the program's.** There is no such
string anywhere in the image; `0x00444CD0` calls itself `ReadScript`, in
"ReadScript: Could not open %s for reading.". The macro said `PARSE_FILE` while
the function said `ReadScript` for the whole of this work, which is exactly the
drift the naming rule exists to stop. Nine more real names were sitting in the
strings unclaimed and are now in `orig.h`: `ScriptResurrectItem`,
`ScriptSetObjBitmap`, `UpdateObjectScript`, `ChangeObjectFrame`,
`SetObjScriptState`, `DefParseInfoFile`, `DefGameParse`, `DefObjParse`,
`DefLinkParse`. Two source filenames come with them -- `script.cpp` and
`objscript.cpp`.

**The whole script parser is reconstructed** -- `ParseScriptFile` and every
function under it, 42 in all, from the file down to the last action operand.
What is still original below it is engine: DirectDraw, comm, bitmap loading,
event dispatch, reached *through* the handlers rather than being parser code.

**Parse the game's own data in the game, not in an emulator.** I was about to
build a Unicorn harness with hooked file I/O to cover the action parser's 59
keywords, of which bootcamp and the campaign reach 24. `AM2_PARSE_ALL=1` makes
our `ReadScript`, after the game's first script load, parse every other script
the game ships and dump each 0x48-byte action record; `AM2_DUMP_ACTIONS=1`
prints them. 104 files, 9,934 records, 48 distinct action codes -- which is
exactly how many action keywords appear in any shipped script, so the sweep
reaches everything reachable. Byte-identical across runs, so it is an exact
oracle. `tests/actions-reference.txt` is the recording and `tools/actdiff.py`
maps a differing record back to its file, line and keyword.

Three things it took: the game chdirs into the map directory before loading, so
the file list must be absolute (`AM2_SCRIPTS`); feeding `EULA.txt` to the
statement dispatcher takes the process down, so the sweep covers `data/` and
`rules/` only; and state accumulates until the fixed tables overflow after
about seventy files, so two runs with the list in opposite orders cover all 104
-- clearing between files is worse, because the arrays and their capacities
have to be cleared together and the names the loaded mission still holds must
not be freed.

**That oracle found nine defects an A/B cannot see.** A mis-parsed action still
produces an action, so the log and the pixels agree either way. Among them:
`playsound` initialises two fields to zero and I had transcribed a scaled token
index, having read a dump with the `xor eax, eax` filtered out; `order`'s
`follow` and `goto` were swapped; `dropitem`, `setobjstate` and `fireweapon`
each put their two names in the opposite fields from how the statement reads;
and the AI modes are attack 6, defend 7, ignore 2, evade 5 -- neither
sequential nor in keyword order. Reading alone got all of them wrong.
