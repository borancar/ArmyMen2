#!/usr/bin/env python3
"""Every reconstruction whose header says which address it replaces must
actually be PATCHED there.

This exists because the same failure happened twice in one session and neither
the compiler nor any other check said a word.  A function is written, its
declaration goes in the header, the `patch_replace` line is meant to follow --
and does not, because the edit that adds it matched nothing.  The build is
clean, `make check` passes, and `tools/ab.sh` passes too, because the address
still holds the ORIGINAL's code and the A/B is comparing the original against
itself.  StartShake (0x0042B2E0) and TroopSubParse (0x0044BEA0) both shipped
that way for as long as it took to read the coverage count by hand.

CLAUDE.md already records the older form of this -- four functions behind a
`return` in an install function -- and `checkpatches.py` catches THAT one.  It
cannot catch this one: there is no wrong code to find, only absent code.

WHAT IT KEYS ON.  The convention in these headers is that a reconstruction's
declaration is preceded by a comment OPENING with the original's address:

    /* 0x0042B2E0, one caller. Start a screen shake ... */
    void __cdecl StartShake(...);

801 declarations follow it and zero exceptions were found when this was
written, which is what makes it usable as a gate rather than a hint.  A
comment that merely mentions an address partway through is NOT matched --
that was tried first and produced three false hits, each an address the
comment was discussing rather than replacing.

ALLOWED holds the reconstructions that are deliberately not patched: those
registered with the game some other way, where a detour would install a jump
nothing reaches.  `tools/coverage.py` calls the same set REGISTERED and this
imports it rather than keeping a second list, for the reason merges.py imports
it too -- two lists of "what is done" is how they come to disagree.
"""

import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import merges
import coverage

# The image's code runs to 0x00464420, where the CRT proper starts -- see
# tools/crt.py.  An address outside that is data and cannot be a patch target.
CODE_LO = 0x00400000
CODE_HI = 0x00464420

DECL = re.compile(
    r"/\*\s*(?:Original:\s*)?0x00(?P<addr>[0-9A-Fa-f]{6})\b"
    r"(?P<body>[^*]*(?:\*(?!/)[^*]*)*)\*/\s*"
    r"(?P<decl>[A-Za-z_][\w \*]*?"
    r"(?:__cdecl|__attribute__\(\(thiscall\)\)|__attribute__\(\(stdcall\)\))"
    r"\s+(?P<name>\w+)\s*\()",
    re.S,
)


def headers():
    return sorted(glob.glob("src/game/*.h") + glob.glob("src/game/win32/*.h"))


def main():
    allowed = set(getattr(coverage, "REGISTERED", ()))
    patched = set(merges.reconstructed())

    seen = 0
    missing = []

    for path in headers():
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        for m in DECL.finditer(text):
            addr = int(m.group("addr"), 16)
            if not (CODE_LO <= addr < CODE_HI):
                continue
            seen += 1
            if addr in patched or addr in allowed:
                continue
            line = text.count("\n", 0, m.start("decl")) + 1
            missing.append((path, line, addr, m.group("name")))

    if missing:
        for path, line, addr, name in missing:
            print("  %s:%d: %s says it replaces 0x%08X and nothing patches it"
                  % (path, line, name, addr))
        print("\n  FAILED   %d reconstruction(s) declared but never installed."
              % len(missing))
        print("           The address still holds the original, so the build"
              " is clean and")
        print("           tools/ab.sh compares the original against itself."
              " Add the")
        print("           patch_replace call, or add the address to"
              " coverage.REGISTERED")
        print("           if it is reached some way other than a detour.")
        return 1

    print("  ok       %d declared reconstruction(s), all installed" % seen)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
