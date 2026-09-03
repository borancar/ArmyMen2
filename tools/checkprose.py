#!/usr/bin/env python3
"""Fail when the prose says an address is still original and it is not.

CLAUDE.md and STATUS.md describe a moving target, and one way they go wrong is
mechanical: a sentence naming an address as untouched -- "still original",
"stays original", "left original", "reached by address" -- outlives the commit
that reconstructed it.  That happened twice in one session and had happened
before:

  * five parsers below a script statement were listed as reached by address
    long after all five were ours, and one of the five even carried a
    parenthetical "now reconstructed" while the clause around it said
    otherwise;
  * the sort at 0x0045EBC0 was DELIBERATELY left original, with the reason
    written out -- and when the reason expired the note stayed.

Neither is reachable by tools/checkclaims.py, which recomputes NUMBERS and
cannot see a sentence whose numbers are fine.  This checks the other half: for
every hex address mentioned within a few lines of such a phrase, is that
address in the patch list?

IT IS DELIBERATELY NARROW.  It reads only sentences that make a checkable
claim about a specific address, and says nothing about prose in general.  A
paragraph explaining why something WAS original, in the past tense, is not a
defect -- so the phrases matched are the present-tense ones, and an address
inside a fenced block or a table of historical figures is still matched,
because a stale claim reads the same wherever it sits.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

import merges

DOCS = ("CLAUDE.md", "STATUS.md")

# Present-tense claims that an address has NOT been reconstructed.  "was left
# original" and "used to be" are history and are not matched.
CLAIMS = re.compile(
    r"(?<!was )(?<!were )"
    r"(still original|stays original|is left original|are left original"
    r"|left original|still the image's|still theirs|remains original)",
    re.IGNORECASE)

ADDR = re.compile(r"0x00[0-9A-Fa-f]{6}")

# A negated claim is the opposite of a defect: "NOTHING below a statement is
# still original" is the correction, and matching it would fail the very
# sentence that fixed the problem.  Found immediately -- the first run flagged
# four addresses in a paragraph written to say they are all reconstructed.
NEGATED = re.compile(r"\b(nothing|not|no longer|never|none)\b", re.IGNORECASE)

# How many lines away an address still counts as the phrase's subject.  The
# window stops at a BLANK LINE: without that it reaches into the next
# paragraph and attributes its addresses to this sentence, which is how
# 0x00426400 came to be flagged by a claim two paragraphs above it.
WINDOW = 3


def paragraph(lines, i):
    lo = i
    while lo > 0 and lines[lo - 1].strip():
        lo -= 1
    hi = i
    while hi + 1 < len(lines) and lines[hi + 1].strip():
        hi += 1
    return max(lo, i - WINDOW), min(hi, i + WINDOW)


def main():
    done = set(merges.reconstructed())
    bad = []

    for name in DOCS:
        path = os.path.join(REPO, name)
        if not os.path.exists(path):
            continue
        lines = open(path).read().split("\n")
        for i, line in enumerate(lines):
            if not CLAIMS.search(line) or NEGATED.search(line):
                continue
            lo, hi = paragraph(lines, i)
            window = " ".join(lines[lo:hi + 1])
            for hit in ADDR.findall(window):
                addr = int(hit, 16)
                if addr in done:
                    bad.append((name, i + 1, hit, line.strip()[:70]))

    for name, ln, hit, text in bad:
        print("  %s:%d says %s is original; it is reconstructed" % (name, ln, hit))
        print("      %s" % text)

    if bad:
        print("\nFAILED   %d claim(s) that an address is still the image's, "
              "where\n         the patch list says otherwise. Rewrite the "
              "sentence, or say\n         in the past tense what used to be "
              "true." % len(bad))
        return 1

    print("prose: no address is described as original while being patched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
