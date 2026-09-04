#!/usr/bin/env python3
"""Every seam src/game reads as DATA must be redefined for the standalone.

The standalone build does not carry the original's `.text`. That range is
filled with int3 -- see tools/mkglobals.py, which explains why 0xCC and not
zero -- so a reference into it is not a link error and not a crash at the
call site. It is whatever the filler happens to mean.

THAT IS NOT HYPOTHETICAL AND IT IS HOW THIS FILE CAME TO EXIST. `c_dfDIMouse`
is a DIDATAFORMAT whose `rgodf` array lives at 0x004643A0, inside the gap:
the struct came from the image's `.rdata`, which IS carried, and the array it
points at did not. SetDataFormat answered E_INVALIDARG and the game showed
"DDERROR 80070057: SetDataFormat (mouse)" -- a plausible DirectInput failure
with nothing to say it was a missing byte range. A missing `free` seam did
worse, faulting at an address that appeared nowhere in the binary.

So the invariant: any ADDR_ macro that src/game DEREFERENCES, or hands to
AM2_IMAGE, whose address lies in the uncarried gap, must be redefined under
AM2_STANDALONE in src/inject/standalone.h. All 37 today are the statically
linked MSVC CRT -- fopen, malloc, qsort, strlwr -- plus the game's stubbed
logger, which is exactly the set the standalone points at the real C library.

WHAT THIS DOES NOT SEE, said plainly. A `patch_replace` target is skipped,
because installing a patch in the standalone is a no-op and the address is
never reached. A seam reached through a VARIABLE is invisible, the same blind
spot tools/checkseams.py documents -- this resolves macros, not dataflow. And
a table read by the ORIGINAL's own code cannot be seen at all, since there is
no original code left to read it; those surface as a fault the vectored
handler in src/standalone/runtime.cpp logs by name.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkglobals import BLOB_LO, GAP_LO       # one definition of the gap

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A data-shaped use: cast through uintptr_t, or handed to AM2_IMAGE. A bare
# mention is not one -- `patch_replace(ADDR_X, ...)` names the address without
# ever reading it.
DATA_USE = re.compile(r"\(uintptr_t\)\s*(ADDR_\w+)"
                      r"|AM2_IMAGE\(\s*(ADDR_\w+)\s*\)")


def macros():
    """ADDR_ names to addresses, with backslash continuations joined.

    checkseams.py was green over six real seams for as long as it existed
    because a macro continued with a backslash never matched.
    """
    src = re.sub(r"\\\n", "", open(os.path.join(REPO, "src", "inject",
                                                "orig.h")).read())
    return {m.group(1): int(m.group(2), 16) for m in
            re.finditer(r"#define\s+(ADDR_\w+)\s+(0x[0-9A-Fa-f]+)u?\b", src)}


def strip(text):
    """Drop comments. Every ADDR_ name in this tree is discussed in one."""
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def gap_seams():
    """{name: [sites]} for gap-resident addresses src/game reads as data."""
    addr = macros()
    out = {}
    for path in sorted(glob.glob(os.path.join(REPO, "src", "game", "**",
                                              "*.cpp"), recursive=True)
                       + glob.glob(os.path.join(REPO, "src", "game", "**",
                                                "*.h"), recursive=True)):
        rel = os.path.relpath(path, REPO)
        for n, line in enumerate(strip(open(path).read()).split("\n"), 1):
            if "patch_replace" in line:
                continue
            for m in DATA_USE.finditer(line):
                name = m.group(1) or m.group(2)
                a = addr.get(name)
                if a is not None and GAP_LO <= a < BLOB_LO:
                    out.setdefault(name, []).append("%s:%d" % (rel, n))
    return out, addr


def main():
    seams, addr = gap_seams()
    sa = strip(open(os.path.join(REPO, "src", "inject", "standalone.h")).read())
    missing = [n for n in seams
               if not re.search(r"#\s*define\s+%s\b" % re.escape(n), sa)]

    for name in sorted(missing, key=lambda n: addr[n]):
        print("  0x%08X  %s is read as data and is NOT redefined for the\n"
              "              standalone -- it would read int3 filler\n"
              "              %s"
              % (addr[name], name, ", ".join(seams[name][:3])))

    if missing:
        print("\n%d seam(s) into the uncarried .text gap. Add a definition to "
              "src/inject/standalone.h,\nor stop reading the address."
              % len(missing))
        return 1
    print("gap: %d data seams into the original's .text, all %d redefined "
          "for the standalone" % (len(seams), len(seams)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
