#!/usr/bin/env python3
"""Count duplicate STRUCT-OFFSET macros, which no other check can see.

`checkpatches.py` ratchets `ADDR_` aliases and `checkglobals.py` ratchets the
`g_` macros.  Nothing watched the plain constants -- `OBJ_OFF_*`, `COMM_OFF_*`
and their kin -- and that is exactly where a duplicate went unnoticed:
`OBJ_OFF_ROW_COUNT` and `OBJ_OFF_ROWS` were each defined TWICE with the same
value, and `AM2_ROW_STRIDE` invented beside `AM2_OBJ_ROW_STRIDE`.  Everything
compiled, because an identical redefinition is legal C, and every check passed,
because none of them was looking.

Two rules, and they are not the same question.

A REDEFINITION is one macro name defined more than once.  That is always wrong
-- either the two values differ, in which case the second silently wins, or
they agree, in which case one is dead.  It fails outright; there is no baseline
to grow into.

A FAMILY ALIAS is two names in one `*_OFF_*` family holding the same value,
e.g. two `OBJ_OFF_` names both meaning 8.  Sometimes that is a real duplicate
and sometimes one structure genuinely has two things at one offset in a union,
so this is a ratchet with a baseline rather than a hard failure.  Lower the
baseline when it drops; never raise it.

Values are compared as NUMBERS, not as text, so 0x8u and 8u collide the way
they should.

The family baseline went in at 13, which is what was already there -- a
backlog, not a clean bill of health, in the same sense checkglobals says of
its own.  Some of those thirteen are real unions: `OBJ_OFF_CHAIN_UID` is a uid
for an item and a count for a vehicle, so one offset honestly has two
readings.  Others are plain duplicates -- `COMM_OFF_0x418` carries THREE names
(DEBUG, EVENT_DEBUG, VERBOSE) and `OBJ_OFF_FLAGS`/`OBJ_OFF_FLAGS8` are one
field spelled twice.  Settling which is which means reading, so the number is
recorded rather than forced down.

The REDEFINITION rule found eleven on its first run, all pre-existing and all
exact.  One was not cosmetic: `ADDR_POINTS_EQUAL` was documented once as
taking two pointers and once as taking two values, and CLAUDE.md records that
that exact confusion caused a live defect in PlaySoundAt.  A duplicate is not
harmless just because the values agree -- the COMMENTS can disagree, and one
of them can be the wrong one somebody follows.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "src", "inject", "orig.h")

# Two names on one offset inside a family.  May only go down.
#
# It went 13 -> 14 when `_FLAG_` families joined `_OFF_` ones, and the extra
# one is NOT the duplicate that prompted the change -- that was fixed in the
# same commit.  It is OBJ_FLAG_OVERDUE and OBJ_FLAG_REPLACED, both 0x2: two
# readings of one bit, sitting there unremarked for as long as nothing looked
# at flags.  Left as backlog rather than guessed at, which is what a ratchet
# baseline is for.
FAMILY_ALIAS_BASELINE = 14

DEFINE = re.compile(r"^#define\s+([A-Z][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+u?|\d+u?)\s*(?:/\*|$)")
# `_OFF_` was the whole of this for as long as offsets were the thing that got
# duplicated.  Then OBJ_FLAG_REMAP_DONE went on 0x400 beside OBJ_FLAG_SELECTED
# and nothing said a word: a flag is exactly the same failure -- one value,
# two names, two readings -- and it was simply not being watched.  The group
# is captured so `OBJ_OFF_` and `OBJ_FLAG_` stay separate families, since an
# offset and a bitmask sharing a number means nothing.
FAMILY = re.compile(r"^([A-Z][A-Z0-9]*(?:_[A-Z0-9]+)?_(?:OFF|FLAG))_")


def value_of(text):
    text = text.rstrip("uU")
    return int(text, 16) if text.lower().startswith("0x") else int(text)


def main():
    seen = {}          # name -> [(line, value)]
    families = {}      # family -> {value: [names]}

    with open(HEADER, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            m = DEFINE.match(line)
            if not m:
                continue
            name, raw = m.group(1), m.group(2)
            try:
                value = value_of(raw)
            except ValueError:
                continue
            seen.setdefault(name, []).append((lineno, value))
            fam = FAMILY.match(name)
            if fam:
                families.setdefault(fam.group(1), {}).setdefault(value, []).append(name)

    redefined = {n: v for n, v in seen.items() if len(v) > 1}
    alias_pairs = []
    for fam, by_value in sorted(families.items()):
        for value, names in sorted(by_value.items()):
            if len(names) > 1:
                alias_pairs.append((fam, value, names))

    surplus = sum(len(names) - 1 for _, _, names in alias_pairs)

    if redefined:
        for name, places in sorted(redefined.items()):
            where = ", ".join("line %d = 0x%X" % (ln, v) for ln, v in places)
            print("  REDEFINED %s -- %s" % (name, where))
        print("\n  FAILED   %d macro(s) defined more than once. An identical"
              % len(redefined))
        print("           redefinition is legal C and says nothing; delete the"
              " copy.")
        return 1

    if os.environ.get("AM2_SHOW_OFFSET_ALIASES"):
        for fam, value, names in alias_pairs:
            print("  ALIAS  %s_ 0x%X: %s" % (fam, value, ", ".join(sorted(names))))

    if surplus > FAMILY_ALIAS_BASELINE:
        print("\n  FAILED   %d surplus family names (baseline %d) -- a second"
              % (surplus, FAMILY_ALIAS_BASELINE))
        print("           name went on an offset that already had one."
              " AM2_SHOW_OFFSET_ALIASES=1 lists them.")
        return 1

    if surplus < FAMILY_ALIAS_BASELINE:
        print("\n  STALE    %d surplus family names (baseline %d)."
              % (surplus, FAMILY_ALIAS_BASELINE))
        print("           Lower FAMILY_ALIAS_BASELINE; the backlog shrank.")
        return 1

    print("  ok       %d offset macros, no redefinitions, %d surplus family"
          " names (at baseline)" % (len(seen), surplus))
    return 0


if __name__ == "__main__":
    sys.exit(main())
