"""Verify the numeric claims in CLAUDE.md against the tools that produce them.

Three separate figures in CLAUDE.md were found stale by measuring rather than
reading: the Lock/Unlock item counted two functions that do not lock, the COM
function count was one high, and an "unknown sites" figure described history as
if it were current. Each had the same cause -- a tool changed, some prose was
updated, and the rest went on asserting the old number.

`make check` already fails when a GENERATED file drifts. Prose cannot be
regenerated, so the next best thing is to state where each number comes from
and check it. That is what this does: every entry below pairs a sentence in
CLAUDE.md with the computation that produces its number.

It is deliberately small. Most of CLAUDE.md is judgement and prose, which is
not checkable and should not pretend to be -- the argument for keeping numbers
OUT of prose and pointing at docs/boundary.md instead. What is here is the
handful that kept going stale.

    tools/checkclaims.py      # exits non-zero if CLAUDE.md and the tools differ
"""

import csv
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import merges
import checkinstalled

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRT_START = 0x0045C000


def com_rows():
    with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def import_rows():
    with open(os.path.join(REPO, "docs", "imports.tsv")) as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def lock_bracket():
    """(functions calling LockSurface/UnlockSurface, how many are ours)."""
    img = am2.Image()
    done = merges.reconstructed()
    merged = merges.real_functions(img)
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")),
                 delimiter="\t")}
    seen = set()
    for _n, start, _e, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] != 0xE8:
                continue
            t = start + j + 5 + struct.unpack_from("<i", data, j + 1)[0]
            if t not in (0x0041B9A0, 0x0041BA40):
                continue
            site = start + j
            fn = next((f for f, sz in sizes.items() if f <= site < f + sz), None)
            size = sizes.get(fn, 0)
            if fn in merged:
                starts, msz = merged[fn]
                fn, size = merges.owner(starts, msz, site)
            if fn:
                seen.add((fn, size))
    ours = sum(1 for f, sz in seen if any(f <= d < f + sz for d in done))
    return len(seen), ours


def module_split():
    """(flat modules, win32 modules) -- how src/game is divided.

    checksplit.py checks that each module is on the correct side; nothing
    checked how MANY were, and the prose describing the split was eleven
    modules out of date before anyone measured it.
    """
    srcs = am2.game_sources()
    win32 = [p for p in srcs
             if os.path.basename(os.path.dirname(p)) == "win32"]
    return len(srcs) - len(win32), len(win32)


def check_tool_count():
    """How many analysis tools `make check` actually runs.

    CLAUDE.md's own paragraph about this says it "said eight here for a long
    time after it stopped being eight, which is what a hand-maintained number
    in prose always comes to" -- and then said fifteen while the recipe ran
    seventeen. A warning about stale numbers is not a defence against one.
    """
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = open(os.path.join(root, "Makefile"), encoding="utf-8").read()
    m = re.search(r"^\tfor t in ([a-z0-9 ]+); do", text, re.M)
    if not m:
        raise SystemExit("checkclaims: cannot find the check recipe's tool list")
    return (len(m.group(1).split()),)


# The enumerating oracles -- the tools that generate a corpus and compare it
# against the original.  Deliberately NOT every tools/*check*.py: checkabi.py
# and checkcom.py are inventories that name every function in the image, so
# matching against them would report the whole list as checked.
ORACLES = ("moviecheck posecheck formationcheck shakecheck roachcheck rlecheck "
           "mprowcheck weaponcheck listcheck ringcheck boolcheck explcheck "
           "collectcheck firepose regioncheck pathcheck tilepathcheck "
           "placementcheck aicheck rectquerycheck scriptcheck").split()

DOCSTRING = re.compile(r'"""[\s\S]*?"""')
COMMENT = re.compile(r"#[^\n]*")


def _strip_python_prose(src):
    """Drop docstrings and comments, so a MENTION cannot count as coverage.

    Two of these tools discuss a function they do not test -- roachcheck.py
    names ItemSetBox in its header to say whose shape it borrows, and
    tilepathcheck.py names PlanPathTo to say whose callee it covers.  A plain
    substring search reports both as checked, which is the same mistake
    checkseams.py and checkoffsetuse.py each had to be taught out of.
    """
    return COMMENT.sub("", DOCSTRING.sub("", src))


PATCH = re.compile(r"patch_replace\(\s*(ADDR_\w+)\s*,\s*\(const void \*\)\s*(\w+)")
ADDRDEF = re.compile(r"^#define\s+(ADDR_\w+)\s+0x([0-9A-Fa-f]{8})u?", re.M)


def _name_addresses():
    """Every reconstruction's name -> the address it replaces.

    An oracle reaches the ORIGINAL by address, so the function's NAME often
    appears in its prose and nowhere else -- searching for names alone found
    one of thirty-two.  Searching for names anywhere at all found sixteen,
    two of them tools merely discussing a function they do not test.  The
    address is the key that is neither.

    Two routes, because neither is complete on its own: the declaration
    comment checkinstalled.py already parses, and the patch list.  Seven of
    the thirty-two have no matching declaration -- a static definition, or a
    signature the DECL regex does not take -- and the patch list has them.
    """
    out = {}
    for path in (glob.glob(os.path.join(REPO, "src", "game", "*.h"))
                 + glob.glob(os.path.join(REPO, "src", "game", "win32", "*.h"))):
        for m in checkinstalled.DECL.finditer(open(path).read()):
            out.setdefault(m.group("name"), "0x00" + m.group("addr").upper())

    addrs = dict(ADDRDEF.findall(open(os.path.join(REPO, "src", "inject",
                                                   "orig.h")).read()))
    for path in (glob.glob(os.path.join(REPO, "src", "game", "*.cpp"))
                 + glob.glob(os.path.join(REPO, "src", "game", "win32", "*.cpp"))):
        for macro, name in PATCH.findall(open(path).read()):
            if macro in addrs:
                out.setdefault(name, "0x" + addrs[macro].upper())
    return out


CHECKS_DECL = re.compile(r"^CHECKS\s*=\s*\(([^)]*)\)", re.M)


def _oracle_subjects():
    """Per oracle: an explicit CHECKS tuple, or None to fall back to search.

    A tool that stubs the functions around its subject holds their addresses
    in its code, where stripping prose cannot help -- tools/aicheck.py names
    six arms it replaces with stubs.  So a tool may declare what it verifies,
    and a declaration wins over the search outright.
    """
    out = {}
    for t in ORACLES:
        path = os.path.join(REPO, "tools", t + ".py")
        if not os.path.exists(path):
            continue
        src = open(path).read()
        m = CHECKS_DECL.search(src)
        out[t] = (re.findall(r'"(\w+)"', m.group(1)) if m else None, src)
    return out

def unexercised_split(text):
    """(names in CLAUDE.md's unexercised list an oracle checks, list length)."""
    m = re.search(r"- Unexercised by any drive: (.*?)\n\n", text, re.S)
    names = re.findall(r"`(\w+)`", m.group(1))
    addrs = _name_addresses()

    declared, bodies = set(), []
    for t, (names_, src) in _oracle_subjects().items():
        if names_ is None:
            bodies.append(_strip_python_prose(src))
        else:
            declared.update(names_)
    vectors = os.path.join(REPO, "tests", "vectors.h")
    if os.path.exists(vectors):
        bodies.append(open(vectors).read())

    checked = 0
    for n in names:
        if n in declared:
            checked += 1
            continue
        a = addrs.get(n)
        keys = [n] if a is None else [n, a, a.lower(), a.replace("0x00", "0x")]
        if any(re.search(r"\b%s\b" % re.escape(k), b)
               for b in bodies for k in keys):
            checked += 1
    return (checked, len(names))


def claims():
    com = com_rows()
    game = [r for r in com if int(r["func"], 16) < CRT_START and r["func"] != "0x00000000"]
    imp = [r for r in import_rows() if int(r["func"], 16) < CRT_START]
    total, ours = lock_bracket()
    flat, win32 = module_split()

    return [
        ("in-game COM-shaped sites that are C++",
         r"\*\*(\d+) of (\d+) in-game sites are C\+\+ rather than COM\*\*",
         (sum(1 for r in game if r["abi"] == "thiscall"), len(game))),

        ("functions below the CRT touching the import table",
         r"outward-in: (\d+)\n  functions below the CRT touch the import table",
         (len({r["func"] for r in imp}),)),

        ("functions containing genuine COM dispatch",
         r"import table \(`docs/imports\.tsv`\) and (\d+)\n  contain genuine COM dispatch",
         (len({r["func"] for r in game if r["abi"] == "stdcall"}),)),

        ("total COM-shaped dispatch sites",
         r"`cdecl` for all (\d+) with nothing unknown",
         (len(com),)),

        ("modules under src/game/win32",
         r"talks to Win32 or DirectX -- \*\*(\d+)\*\*",
         (win32,)),

        ("modules in the flat half of src/game",
         r"touches no\s+API at all, and there are \*\*(\d+)\*\*",
         (flat,)),

        ("functions calling the Lock/Unlock bracket, and how many are ours",
         r"Measured: \*\*(\d+) functions\*\* call the bracket and \*\*(\d+)\*\*",
         (total, ours)),

        ("unexercised functions an oracle checks, and the list length",
         r'\*\*"UNEXERCISED" IS NOT "UNVERIFIED", and (\d+) of the (\d+) below are\n  now checked',
         unexercised_split(open(os.path.join(REPO, "CLAUDE.md")).read())),

        ("analysis tools `make check` runs",
         r"does not need the game\.\*\* \*\*(\d+)\*\* analysis",
         check_tool_count()),
    ]


def main():
    text = open(os.path.join(REPO, "CLAUDE.md")).read()
    bad = 0
    for what, pattern, expected in claims():
        m = re.search(pattern, text)
        if not m:
            print(f"  MISSING  {what}\n           no sentence matches {pattern!r}")
            bad += 1
            continue
        found = tuple(int(g) for g in m.groups())
        if found != expected:
            print(f"  STALE    {what}\n"
                  f"           CLAUDE.md says {found}, the tools say {expected}")
            bad += 1
        else:
            print(f"  ok       {what}: {expected}")

    if bad:
        print(f"\n{bad} claim(s) no longer true. Fix the sentence, or find out "
              "why a tool\nchanged its mind -- both have happened.")
        return 1
    print("\nEvery checked claim matches. Most of CLAUDE.md is judgement and "
          "cannot be\nchecked this way, which is the argument for keeping "
          "numbers in docs/boundary.md\nrather than in prose.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
