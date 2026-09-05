# Movement and the input path

Two defects a month apart, both of which made the player unable to move, and
neither of which any A/B could see -- both sides are driven with the same input
and agree about ignoring it.

The first was `StepType2`'s player arm, gated on `cmp [0x5122c8], esi; jne` at
0x0044B9B0 -- is the FOLLOWED OBJECT THIS ONE -- transcribed as a test for null.
The second was `UpdateTrooperAction`'s route test, which sent a CLEAR step to
the stop arms; see `STATUS.md` for that one, which is the jerkstep Boran
reported.

`tools/movecheck.sh` is the configuration that catches this class, and it is the
only one in the project that drives a real device: `xdotool` sends X events that
go through Wine's own DirectInput, where the control socket's `cursor` and `key`
write the game's globals directly and exercise neither poller.

Its assertion is DISPLACEMENT rather than equality, deliberately -- two
unsynchronised runs cannot leave Sarge on the same tile, so what is comparable
is whether he moved at all.

**`tools/movecheck.sh` IS THAT CONFIGURATION, and it is the only one in the
project that drives a real device.** Everything else writes the game's
globals over the control socket -- `cursor` and `key` never touch
DirectInput -- so the whole device-to-gameplay path was unexercised. This
drives X events through `xdotool`, which reach the game the way a player's
keyboard does, and asks the one question that matters: did the unit's
position change?

Its assertion is DISPLACEMENT, not equality, and that is deliberate. Two
unsynchronised runs cannot leave Sarge on the same tile -- they see different
frame deltas, so he walks for different lengths of time. What is comparable
is whether he moved at all, and the failure being guarded is zero against
hundreds, which needs no budget tuned to see it. Measured: 241 units against
the original's 237 with the fix, 0 against 237 with the bug put back.

It carries a VOID arm for the same reason `samission.sh` does: if the
ORIGINAL walked less than the floor, the drive never reached live play and
the run compares nothing. A reconstruction that cannot move looks exactly
like a drive that never started.

**THE PLAYER COULD NOT MOVE, FOR A MONTH, AND NOTHING IN THE SUITE COULD SEE
IT.** `StepType2`'s player arm gates the input call on
`cmp [0x5122c8], esi; jne` at 0x0044B9B0 -- is the FOLLOWED OBJECT THIS ONE.
It was transcribed as `!ADDR_OBJ_CTX_OBJ_A`, a test for null, which is the
same condition only when nothing is followed. The camera follows Sarge for
the whole of a mission, so the original takes that arm EVERY frame and we
took it NEVER: no key and no click moved anything, in either build.

Every artifact was silent, and each for a reason this file already states.
The A/B drives both sides with the same input and they agreed about ignoring
it -- the same argument the `ActionKeyReleased` header makes for why a
pressed/released mix-up is invisible. `bootcamp`'s object dump is taken AT THE
BRIEFING, before anything moves. `mission`'s pixel check is off by
construction. And the counters cannot help: `Type2PlayerInput`, `StepType2`
and `MoveStepPoint` are all BLIND, every caller being ours, so their zeros
mean nothing.

**What found it was driving REAL INPUT and asking whether the game moved.**
`xdotool` sends X events that go through Wine's own DirectInput, which is the
one path the control socket bypasses entirely -- `cursor` and `key` write the
game's globals directly. Held a key, dumped Sarge's position, and compared
against `AM2_NOPATCH=1`: the original walked and we stood still. That is a
configuration this project did not have and should keep.

The localisation is worth recording because three cheap measurements each
removed a whole class:

- `ctl keys` showed scancode 0x11 down while W was held, in BOTH builds, so
  DirectInput and the key buffers were fine and the defect was above them.
- the binding table at 0x004854BC came back byte-identical to the original's,
  so the actions resolved.
- `ADDR_CHAR_HANDLER` and `ADDR_INPUT_SUPPRESS` both read 0, and poking the
  cursor to a screen edge MOVED `ADDR_VIEW_TARGET` -- so `MissionInput` was
  running and the per-frame input step was alive. That left one gate.

**AND THE GLOBAL WAS NOT THE CLUE IT LOOKED LIKE.** `ADDR_OBJ_CTX_OBJ_A` is
non-zero in a live mission on BOTH sides -- it holds the followed object --
so reading it proved nothing until the instruction was decoded. A gate whose
operand is a REGISTER cannot be checked by looking at the global alone; the
comparison is the fact, and `checkoffsetuse` cannot see it either, because
every offset involved is correct.
