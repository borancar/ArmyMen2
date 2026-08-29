"""Check MovieBuildName against the original, exhaustively.

The function turns a short movie name into a filename. Everything it can do is
decided by two globals and by whether the name is one of four literals, so the
whole input space is small enough to enumerate: every name the game can pass,
crossed with the four settings of ADDR_SLOW_MACHINE and ADDR_OPT_BIG_MOVIES.
That is an exact oracle rather than a sample, which is what this function
deserves and what tools/vectors.py cannot give it -- its arguments are strings
and it reads two globals the game writes.

Run the ORIGINAL under Unicorn and compare against the rule the reconstruction
implements. A disagreement names the input.

    tools/moviecheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH, SCRATCH_SZ

ADDR = 0x0042E770
SLOW_MACHINE = 0x005125C0
BIG_MOVIES = 0x005125C4

EXEMPT = ("3do", "credits", "act2", "portal")

# Every name a call site can supply. Three are literals in the image; the
# fourth call site passes ADDR_MOVIE_TO_PLAY, which a mission's script fills,
# so the rest are names a script could plausibly write -- including each
# exempt one, since only those exercise the match arms.
NAMES = ["3do", "act1", "act2", "credits", "portal", "sml", "",
         "a", "3d", "3don", "credit", "creditss", "portalx", "act22",
         "mission01", "x" * 20]


def expect(name, slow, big):
    out = name
    if name not in EXEMPT and slow and not big:
        out += "sml"
    return out + ".smk"


def main():
    emu = Emu()
    dst = SCRATCH
    src = SCRATCH + 0x400
    bad = 0
    n = 0

    for name in NAMES:
        for slow in (0, 1):
            for big in (0, 1):
                emu.uc.mem_write(SLOW_MACHINE, struct.pack("<I", slow))
                emu.uc.mem_write(BIG_MOVIES, struct.pack("<I", big))

                buf = bytearray(SCRATCH_SZ)
                buf[0x400:0x400 + len(name)] = name.encode()
                emu.call(ADDR, [dst, src], bytes(buf))

                got = bytes(emu.uc.mem_read(dst, 64)).split(b"\0")[0].decode()
                want = expect(name, slow, big)
                n += 1
                if got != want:
                    bad += 1
                    print("  %-12r slow=%d big=%d -> %r, expected %r"
                          % (name, slow, big, got, want))

    print("%d case(s), %d disagree" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
