"""Check DefParseBoolean against the original, over the game's own tokens.

The function is twelve inlined strcmps against TRUE/true/True/T/t/1 and
FALSE/false/False/F/f/0, with a complaint for anything else. Its input space is
strings, so vectors are useless -- a random 32-bit argument is a wild pointer,
and tools/vectors.py could give none at all. What it does have is a CORPUS: the
.aai files its three callers parse, which ship with the game.

So the corpus is every distinct whitespace/`;`/`,`-delimited token in every
.aai file under the prefix, plus the twelve words themselves and a set of
near-misses chosen to reach the arms the data does not -- a trailing space, a
prefix, the wrong case of a word that only exists in one case, and the empty
string. `tools/scriptcheck.py`'s rule applies: take the whole corpus, including
the parts that are not input.

It matters because the A/B cannot see this. A mis-parsed boolean in an .aai
record changes a unit's behaviour rather than a pixel or a log line, and eleven
of the twelve words appear nowhere in the shipped data at all.

    tools/boolcheck.py
"""

import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH

ADDR = 0x0041A2D0

TEXT = SCRATCH + 0x100
OUT  = SCRATCH + 0x10

TRUE_WORDS  = ("TRUE", "true", "True", "T", "t", "1")
FALSE_WORDS = ("FALSE", "false", "False", "F", "f", "0")

# THE CORPUS'S COPY OF THE TWELVE, AND IT IS DELIBERATELY A SECOND LIST. The
# first version fed the corpus from TRUE_WORDS/FALSE_WORDS, which are also what
# `expected` answers from -- so deleting a word from the model deleted it from
# the corpus too and the mutation passed. Found by trying it: dropping "T" gave
# "1292 distinct tokens, all identical" instead of one failure. A corpus that
# is derived from the model under test cannot fail against it.
#
# Only FOUR of the twelve appear in the shipped data at all -- `0` 11,428
# times, `1` 6,117, `False` 102 and `True` 74 -- so the other eight are covered
# by this list or by nothing.
VOCABULARY = ("TRUE", "true", "True", "T", "t", "1",
              "FALSE", "false", "False", "F", "f", "0")

# The arms the shipped .aai files do not reach, and the refusals either side of
# them. Case matters -- the function is not case-folding, it is listing.
NEAR_MISSES = (
    "", " ", "TRUE ", " TRUE", "TRU", "TRUEX", "tRUE", "TrUe", "FALS",
    "FALSEX", "fALSE", "yes", "no", "on", "off", "2", "-1", "01", "10",
    "TT", "ff", "\t", "0.0", "1.0", "True\n",
)


def expected(tok):
    """What definfo.cpp's DefParseBoolean answers: (ret, out) or (0, None)."""
    if tok in TRUE_WORDS:
        return 1, 1
    if tok in FALSE_WORDS:
        return 1, 0
    return 0, 0          # complains, and stores 0


def corpus():
    seen, out = set(), []
    for word in VOCABULARY + NEAR_MISSES:
        if word not in seen:
            seen.add(word)
            out.append(word)

    prefix = os.path.join(os.path.dirname(am2.EXE), "**", "*.aai")
    for path in sorted(glob.glob(prefix, recursive=True)):
        with open(path, "rb") as fh:
            text = fh.read().decode("latin-1")
        for tok in re.split(r"[ \t\r\n;,]+", text):
            if tok and tok not in seen:
                seen.add(tok)
                out.append(tok)
    return out


def main():
    emu  = Emu()
    toks = corpus()
    bad  = 0

    for tok in toks:
        raw = tok.encode("latin-1", "replace")[:0x80] + b"\0"
        emu.uc.mem_write(TEXT, raw)
        emu.uc.mem_write(OUT, struct.pack("<i", -1))

        ret, _ = emu.call(ADDR, [OUT, TEXT], b"")
        if ret is None:
            print("faulted on %r" % tok)
            bad += 1
            continue

        got_out = struct.unpack("<i", emu.uc.mem_read(OUT, 4))[0]
        want_ret, want_out = expected(tok)

        if (ret & 0xFFFFFFFF) != want_ret or got_out != want_out:
            bad += 1
            print("%-24r original ret=%d out=%d   ours ret=%d out=%d"
                  % (tok, ret & 0xFFFFFFFF, got_out, want_ret, want_out))

    # The null token is the one case that cannot go through the corpus: it is a
    # null POINTER, not an empty string, and it must leave *out untouched.
    emu.uc.mem_write(OUT, struct.pack("<i", 0x5A5A5A5A))
    ret, _ = emu.call(ADDR, [OUT, 0], b"")
    got_out = struct.unpack("<i", emu.uc.mem_read(OUT, 4))[0]
    if ret is None or (ret & 0xFFFFFFFF) != 0 or got_out != 0x5A5A5A5A:
        bad += 1
        print("null token: original ret=%s out=%#x -- expected 0 and untouched"
              % (ret, got_out))

    if bad:
        print("boolcheck: %d token(s) differ" % bad)
        return 1
    print("boolcheck: %d distinct tokens plus the null one, all identical"
          % len(toks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
