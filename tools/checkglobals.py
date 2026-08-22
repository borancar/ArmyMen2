#!/usr/bin/env python3
"""Ratchet the `g_` macros the way checkpatches.py ratchets ADDR_ names.

`src/game` reaches the original's globals through macros of the shape

    #define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)

and nothing was checking them. That let one global end up with FOUR
definitions across three types -- uint32_t in objtable.h, const uint32_t in
audio.cpp, int32_t in dplay.cpp, and int32_t again under the second name
g_defaultOwnerSlot. GCC objected to exactly one of those, because it is the
only pair that ever met in one translation unit; the alias never met anything
and nothing said a word.

Two rules, and they are the same two the project already applies one level up:

  ALIAS      one address reached through two different g_ names. This is what
             the ADDR_ ratchet in checkpatches.py forbids, and a g_ alias is
             worse, because the ADDR_ name underneath is identical so a grep
             for the address finds both and looks consistent.

  DRIFT      one g_ name defined with two different expansions. Same name,
             different type or different address, in two modules -- which is a
             signature being wrong in private, the failure that hid
             PlaySoundAt's two-pointer compare.

Repeating a definition VERBATIM in two modules is allowed and is not the
interesting case: it cannot drift silently, because changing one and not the
other turns it into DRIFT and this reports it.

This is a RATCHET, not a clean bill of health. The first run found 38 surplus names
and 17 surplus spellings already in the tree, which is far too many to fix in the commit
that adds the tool and several of which are naming questions rather than
tidy-ups -- ADDR_FONT_SURFACE is reached through five names including
g_backBuffer and g_fontSurface, and deciding which of those is right means
reading what the surface is used for, not renaming the loser. So the baselines
below are what was there, the check fails if either count goes UP, and they get
lowered as the backlog is worked off. Exactly the shape checkpatches.py uses
for ADDR_ aliases.
"""

import collections
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# What was in the tree when this check was written. Lower them, never raise.
ALIAS_BASELINE = 34
DRIFT_BASELINE = 17

DEFINE = re.compile(r"^\s*#\s*define\s+(g_[A-Za-z0-9_]*)\s+(.+?)\s*$")
ADDR = re.compile(r"\bADDR_[A-Za-z0-9_]+")


def sources():
    """Every game source and header, both halves of the split."""
    out = list(am2.game_sources())
    for root, _dirs, files in os.walk(os.path.join(REPO, "src", "game")):
        for f in files:
            if f.endswith(".h"):
                out.append(os.path.join(root, f))
    return sorted(set(out))


def main():
    # name -> {expansion -> [where]},  addr macro -> {name -> [where]}
    by_name = collections.defaultdict(lambda: collections.defaultdict(list))
    by_addr = collections.defaultdict(lambda: collections.defaultdict(list))

    for path in sources():
        rel = os.path.relpath(path, REPO)
        for n, line in enumerate(io.open(path, encoding="utf-8",
                                         errors="replace"), 1):
            m = DEFINE.match(line)
            if not m:
                continue
            name, body = m.group(1), m.group(2)
            where = "%s:%d" % (rel, n)
            by_name[name][body].append(where)
            found = ADDR.search(body)
            if found:
                by_addr[found.group(0)][name].append(where)

    bad = 0
    for addr, names in sorted(by_addr.items()):
        if len(names) > 1:
            bad += 1
            print("  ALIAS  %s is reached through %d names:" %
                  (addr, len(names)))
            for name, wheres in sorted(names.items()):
                print("           %-24s %s" % (name, ", ".join(wheres)))

    for name, bodies in sorted(by_name.items()):
        if len(bodies) > 1:
            bad += 1
            print("  DRIFT  %s is defined %d different ways:" %
                  (name, len(bodies)))
            for body, wheres in sorted(bodies.items()):
                print("           %-40s %s" % (body, ", ".join(wheres)))

    total = sum(len(b) for b in by_name.values())
    # Count SURPLUS names and SURPLUS spellings, not the addresses and names
    # that have any. Counting the latter lets a fifth name land on an address
    # that already has four without moving the number -- which is exactly
    # where a new alias is most likely to appear, and it passed when it was
    # tried.
    aliases = sum(len(names) - 1 for names in by_addr.values())
    drifts = sum(len(bodies) - 1 for bodies in by_name.values())

    if aliases > ALIAS_BASELINE or drifts > DRIFT_BASELINE:
        print("\n  FAILED   aliases %d (baseline %d), drifts %d (baseline %d)"
              % (aliases, ALIAS_BASELINE, drifts, DRIFT_BASELINE))
        print("           A new one of either. Reuse the name that is already "
              "on the\n           address, or fix the one that is wrong -- "
              "do not add a third.")
        return 1
    if aliases < ALIAS_BASELINE or drifts < DRIFT_BASELINE:
        print("\n  STALE    aliases %d (baseline %d), drifts %d (baseline %d)"
              % (aliases, ALIAS_BASELINE, drifts, DRIFT_BASELINE))
        print("           Lower the baselines in this file; the backlog "
              "shrank.")
        return 1
    print("  ok       %d g_ definitions, %d names, %d aliases and %d drifts "
          "(at baseline)" % (total, len(by_name), aliases, drifts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
