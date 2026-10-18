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
- **Difference tests**, `a - b` for `a != b` (and `x - y >= 0`), which change which scratch
  register a compare uses.
- **Frame shaping**: local structs/unions in the image's slot order, an array for an extra
  frame slot, a named `bias` local defined inside a loop plus a redundant guard.
- **Unused labels and flattened tails** that keep VC6 from merging identical return blocks.

None of these should change what the program does. Before removing one, run
`tools/relinkscreen.py` to see what it costs. Each comes with the distance it
bought in the commit that added it (`git log -S"number win"`).
