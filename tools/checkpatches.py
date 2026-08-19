#!/usr/bin/env python3
"""Every address must be patched by exactly one reconstruction.

Two `patch_replace` calls on the same address is a real defect and a quiet one:
the second writes its jump over the first, so whichever install runs last wins
and the other reconstruction is dead code that still looks installed. It also
burns a trace counter, so the counts read as though both are running.

It happened: `ScriptCompare` in misc.cpp and `ScriptCompare3` in objscript.cpp
were two reconstructions of 0x004374F0 under two names, behind two ADDR_ macros
holding the same value, patched twice. Nothing failed -- the bodies agreed --
which is exactly why it needs a check rather than a reader.

Two ADDR_ macros holding the same value is how that one started, so those are
counted too -- but they do not fail the check. Most are honest: a global found
from two directions gets a name from each, and `ADDR_OBJ_BY_UID` alongside
`ADDR_LOOKUP_BY_UID` says the same true thing twice. AM2_SHOW_ALIASES=1 lists
them, and it is worth reading occasionally, because a few are not aliases but
disagreements -- 0x00514DDC is `ADDR_MAP_HEIGHT` and `ADDR_MAP_TILES_W` while
0x00514DE0 is `ADDR_MAP_WIDTH` and `ADDR_MAP_TILES_H`, and both pairs cannot be
right.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def macros():
    text = open(os.path.join(ROOT, "src/inject/orig.h")).read()
    out = {}
    for m in re.finditer(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u", text):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def main():
    addr = macros()
    rc = 0

    # Two names for one address.
    byval = {}
    for name, v in addr.items():
        byval.setdefault(v, []).append(name)
    aliases = [(v, n) for v, n in sorted(byval.items()) if len(n) > 1]
    if os.environ.get("AM2_SHOW_ALIASES"):
        for v, names in aliases:
            print("  alias 0x%08X: %s" % (v, ", ".join(sorted(names))))

    # Two patches for one address.
    sites = {}
    for root, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for f in sorted(files):
            if not f.endswith((".c", ".cpp")):
                continue
            p = os.path.join(root, f)
            for m in re.finditer(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)", open(p).read()):
                name = m.group(1)
                if name not in addr:
                    continue
                sites.setdefault(addr[name], []).append(
                    "%s:%s" % (os.path.relpath(p, ROOT), name))
    for v, where in sorted(sites.items()):
        if len(where) > 1:
            print("  0x%08X patched %d times: %s" % (v, len(where), ", ".join(where)))
            rc = 1

    if rc == 0:
        print("%d addresses patched, each exactly once; %d ADDR_ aliases "
              "(AM2_SHOW_ALIASES=1 to list)" % (len(sites), len(aliases)))
    return rc


if __name__ == "__main__":
    sys.exit(main())
