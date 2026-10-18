# Number wins: source kept only because it lowers the relink distance

To review later (Boran, 2026-10-18). These are edits made to bring a function nearer
the original's bytes that do NOT recover the original's source. They are
behaviour-neutral, and each one is marked in the source with the words "a number win"
(or, for the impossible pairs, "impossible pair"). List them with:

    grep -rn "number win\|impossible pair" src

The kinds, and why each works (all measured; the mechanism notes are in
`docs/bytematching.md`):

- **Dead volatile reads**, `(void)*(volatile T *)&x;` of a value the function already
  reads. One load, and it supplies a register-rotation temporary or takes a local's address.
- **Impossible conditions**, `|| (v == 1000 && v == 1001)`. They fold away and raise `v`'s
  pre-CSE use count, which orders callee-saved registers. Identical pairs are merged, so
  the constants differ per pair.
- **Self-referential ternaries**, `pos = klass ? pos : &o->pos;` and
  `sp = o ? &rows->sprite : &rows->sprite;`, a second reaching definition so VC6 cannot
  propagate a pointer away.
- **Volatile stores and locals**, `*(volatile int8_t *)&r->numb = ...`,
  `d1 = ApproxDist(...)` through a volatile local, a volatile pointer store in a written-out
  DEFAULT_LAYER.
- **Negated tests**, `(-r->which) ? a : b` in OptionsDefaults, which is `x != 0` with a real `neg` the image does not have; revisit.
- **Difference tests**, `a - b` for `a != b` (and `x - y >= 0`), which change which scratch
  register a compare uses.
- **Frame shaping**: local structs/unions in the image's slot order, an array for an extra
  frame slot, a named `bias` local defined inside a loop plus a redundant guard.
- **Unused labels and flattened tails** that keep VC6 from merging identical return blocks.

None of these should change what the program does. Before removing one, run
`tools/relinkscreen.py` to see what it costs. Each comes with the distance it
bought in the commit that added it (`git log -S"number win"`).

## What they probably stand in for

Boran's reading (2026-10-18): the original source referred to conditions or reads that
the compiler optimized away. The measurements agree that the original's variables had
more uses, at the point where VC6 ranks registers, than our source gives them; they do
not say what those uses were, and the constants in the pairs are arbitrary. Candidates:
debug-only code compiled out of the release build (asserts, traces, consistency checks),
conditions the compiler proved constant from earlier code, inlined helpers whose result
was unused, macro expansions that re-tested a value, and leftovers from edits.

Where several pairs were needed (DrawMapObjects: four on `bottom`; BlockWeightTroops:
three on `o`), one stripped macro could account for all of them. Treat each stand-in as a
marker that the original touched that variable at that point in some way not yet found.

## When to revisit

Boran (2026-10-18): the stand-ins are fine for now; revisit them once the binary matches 100%.
That is the point where each can be traded against a real source shape without losing a
match: replace one, run `tools/relinkscreen.py`, keep whichever form keeps the function at 0.
