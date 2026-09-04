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

# AND THE GAME'S OWN HEADERS, which this tool did not read for as long as it
# existed. 68 `_OFF_` macros live in src/game/**.h, and every duplicate among
# them was invisible: LIST_OFF_ARG7C sat on LIST_OFF_ARROWBAR's 0x7C -- a
# placeholder beside the body-derived name, in the SAME family, which is the
# one thing this checker is for. It was found by a hand scan while chasing an
# unrelated bug, which is the argument for the tool reading them.
def _headers():
    out = [HEADER]
    for base, _dirs, files in os.walk(os.path.join(ROOT, "src", "game")):
        for f in sorted(files):
            if f.endswith(".h"):
                out.append(os.path.join(base, f))
    return out

# Two names on one offset inside a family.  May only go down.
#
# It went 13 -> 14 when `_FLAG_` families joined `_OFF_` ones, and the extra
# one is NOT the duplicate that prompted the change -- that was fixed in the
# same commit.  It is OBJ_FLAG_OVERDUE and OBJ_FLAG_REPLACED, both 0x2: two
# readings of one bit, sitting there unremarked for as long as nothing looked
# at flags.  Left as backlog rather than guessed at, which is what a ratchet
# baseline is for.
# 16 -> 15. Two of the three names on COMM_OFF_ 0x418 -- COMM_OFF_DEBUG and
# COMM_OFF_EVENT_DEBUG, which this docstring named as a plain duplicate -- are
# gone, and OBJ_OFF_UID went on 0x04 beside OBJ_OFF_OWNER in the same commit.
# Net one down, and the new pair is EVIDENCED rather than assumed: TrooperFire
# logs +0x04 as a uid and TrooperFireSend hands it to UidOnWire, while the
# "owner" reading of the same offset stands unresolved beside it.
# 15 -> 17, and the rise is COVERAGE rather than decay. This tool read only
# orig.h until now; teaching it the 68 `_OFF_` macros in src/game/**.h brought
# their aliases into the count for the first time. Four were collapsed in the
# same commit before this number was taken -- LIST_OFF_ARG7C onto
# LIST_OFF_ARROWBAR, and FOCUSLABEL_OFF_INK2/3/4 onto INK_FOCUS, PAPER and
# PAPER_FOCUS, placeholders every one of them sitting on a body-derived name --
# and a straight duplicate of VTABLE_EDIT was deleted, so without those the
# figure would have been higher still.
#
# A baseline that goes UP when a checker starts looking somewhere new is not
# the ratchet failing; it is the ratchet's population changing. Say which of
# the two it is whenever this number moves.
# 17 -> 16. OBJ_OFF_FLAGS8 was `8u` beside OBJ_OFF_FLAGS at 0x08 -- the same
# word, and its single OBJ_FLAG8_ bit collides with none of the 23 OBJ_FLAG_
# values, so there was never a second field for the second name to describe.
# 16 -> 15. OBJ_OFF_COUNT62 was named after its own offset and sat on
# OBJ_OFF_HEALTH's 0x62. All four uses were alive/dead tests, and the "suicide
# kings" cheat settles it beyond argument: it writes 1 and then calls
# DamageObject with 0x64, which only kills if the field is health.
# 15 -> 13. EDIT_OFF_SCROLL ("int32_t, constructed 0", one use) sat on
# EDIT_OFF_DOT's 0x70, which six sites store and follow a widget through -- the
# constructor's zero is that pointer being nulled. TYPER_OFF_ICON, a bare
# `AM2_Widget *`, sat on TYPER_OFF_BLINKER's 0x460, which says what the field
# DOES and has four uses to its one.
#
# The pattern in all six retired today: a placeholder describing the field's
# TYPE or its initial value, beside a name describing its JOB. Keep the job.
# 13 -> 10. Three names on COMM_OFF_ 0x3E8 and two on 0x3F0, and FOUR of the
# five surplus came from ONE teardown: CommClose frees both buffers, and a name
# taken from a free cannot tell what a buffer holds. SEND_BUF and RECV_BUF were
# both misnomers for it -- nothing sends or receives through either.
# 10 -> 9. COMM_OFF_STARTED, "non-zero once the game is running", sat on
# COMM_OFF_LOCAL's 0x400 with the OPPOSITE sense. StartSelectedGame writes 0
# when it joins an existing session and 1 for a local game, so the field is
# LOCAL: a writer PAIR against a single reader's guess, which is the ordering
# this project already keeps for naming.
# 9 -> 8. SOUND_REC_OFF_STATE only ever wrote NULL; SOUND_REC_OFF_OWNER_DS at
# the same 0x08 writes g_dsound, a typed IDirectSound. Placeholder against a
# typed writer.
#
# WHAT IS LEFT IS NOT ALL BACKLOG, and saying which is which is the useful part
# -- the count alone invites a future reader to "fix" a pair that is correct:
#
#   DLG_OFF_ 0x64   LEGITIMATE. BATTLE_NAME is a char buffer read by the battle
#                   dialog; LIST is a record pointer written by another. The
#                   DLG_ prefix spans several dialog classes.
#   OBJ_OFF_ 0x12   LEGITIMATE. POS is the AM2_Point and X its first member;
#                   both names are true of the same address.
#   OBJ_OFF_ 0x4    LEGITIMATE, and the tree already said so before this tool
#                   could see it: air.cpp's comment reads "OBJ_OFF_OWNER is
#                   NOT [objtable's owner] -- that constant is 0x0004 and
#                   belongs to a different structure entirely. Two right
#                   names, one collision." Do not collapse it.
#   OBJ_OFF_ 0xA4   CHAIN_PARENT_UID against PTR_LIST, and 0x534 STUCK_SINCE
#   OBJ_OFF_ 0x534  against TABLE_REC_SLOT. Still unread. The OBJ_ prefix
#                   covers eight object TYPES, so either may be the documented
#                   one-offset-several-types case; check the DESTRUCTOR first,
#                   which is what settled three other pairs.
#   OBJ_FLAG_ 0x2   OVERDUE against REPLACED, two readings of one bit, already
#                   recorded above as backlog.
# 8 -> 6. The 0x8C/0x90 pair flagged above as needing the writer is settled and
# it was the teardown again: DestroyItemObject reads 0x8C, frees the array at
# 0x90 and zeroes 0x8C -- the CELL LIST coming down. ALLOC_LIVE and ALLOC_PTR
# were that free's names for CELL_COUNT and CELL_ENTRIES, which twelve and
# fourteen other sites write a count into, iterate and grow.
#
# Third time today one function's frees produced the aliases: CommClose gave
# SEND_BUF and RECV_BUF, this gave two more. **When a family has aliases, look
# at the destructor first** -- it touches every field and names none of them.
FAMILY_ALIAS_BASELINE = 6

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

    for path in _headers():
      with open(path, encoding="utf-8") as fh:
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
