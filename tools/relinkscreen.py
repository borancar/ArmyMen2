#!/usr/bin/env python3
"""relinkscreen.py -- score a source edit against the original image's bytes.

The byte-match loop's measuring instrument (docs/bytematching.md). The metric
is the one `make vc6-relink` produces: for each function, the byte edit
distance between what the hosted VC6 emits for the whole relink and what is in
ArmyMen2.exe (difflib over the two byte strings; 0 means byte-identical).
Unlike the shape metric of the older tools it sees jump targets and register
numbers, which is where the last hundreds of bytes hide.

    relinkscreen.py --baseline
        One full relink of the committed tree. Saves build/relink-base.json
        (every function's ours/orig bytes) and build/relink-syms.pkl (the
        symbol tables later screens reuse). Run it first, and again after each
        commit that changes code (--rebase is a synonym).

    relinkscreen.py SRC EDITS.py FN [FN ...]
        The SCREEN, about a second. Applies the edit list in EDITS.py to SRC in
        place, compiles ONLY that translation unit with the saved tables, prints
        `FN OLD -> NEW` for each named function ('(identical)' when the bytes did
        not change), and restores SRC. Constructors and other C++ members are
        named by their mangled name as it appears in the json
        (`??0AM2_MpPanelScreen`).

    relinkscreen.py --confirm SRC EDITS.py
        The same edit through a FULL relink, printing every function whose
        distance changed (so a neighbour that moved shows up) and the
        match/differ counts. Slow (a minute or two). Confirm every win with it:
        the screen compiles one unit and can miss a cross-unit effect.

    relinkscreen.py --list
        The functions with a non-zero distance in the baseline, largest first.

EDITS.py defines `E = [(old_text, new_text), ...]`. Every occurrence of each
old_text in SRC is replaced, so anchor it with enough context that it does not
also match a sibling function (a sibling that shares the lines gets the edit
too, which the screen then reports as a change to the sibling).

Needs build/vc6host (`make vc6host`) and VC6_ROOT as for `make vc6-relink`.
"""
import difflib
import json
import os
import pickle
import runpy
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(REPO)
BASE = os.path.join(REPO, "build", "relink-base.json")
SYMS = os.path.join(REPO, "build", "relink-syms.pkl")
PY = os.path.join(REPO, ".venv", "bin", "python")
RELINK = os.path.join(REPO, "src", "vc6", "relink.py")


def dist(ours: bytes, orig: bytes) -> int:
    sm = difflib.SequenceMatcher(None, ours, orig, autojunk=False)
    return len(ours) + len(orig) - 2 * sum(m.size for m in sm.get_matching_blocks())


def run_relink(out_json, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    r = subprocess.run([PY, RELINK, "-nointro", "-dbg", "--json", out_json],
                       env=env, capture_output=True, text=True)
    if not os.path.exists(out_json):
        sys.stderr.write("relink produced no json (compile failure?):\n"
                         + r.stdout[-1500:] + r.stderr[-1500:])
        sys.exit(1)
    return r


def load(path):
    return {e["fn"]: e for e in json.load(open(path))}


def distance_of(e):
    return dist(bytes.fromhex(e["ours"]), bytes.fromhex(e["orig"]))


def apply_edits(src, edits_py):
    edits = runpy.run_path(edits_py)["E"]
    text = open(src).read()
    for old, new in edits:
        if old not in text:
            sys.exit("edit does not apply (old text not found): %r" % old[:80])
        text = text.replace(old, new)
    return text


def with_edits(src, edits_py, fn):
    """Run fn() with SRC edited in place, always restoring it."""
    original = open(src).read()
    try:
        edited = apply_edits(src, edits_py)  # before open(): "w" truncates
        open(src, "w").write(edited)
        return fn()
    finally:
        open(src, "w").write(original)


def baseline():
    os.makedirs(os.path.dirname(BASE), exist_ok=True)
    run_relink(BASE, {"RELINK_SAVE": SYMS})
    rows = load(BASE)
    bad = sum(1 for e in rows.values() if not e["ok"])
    print("baseline: %d functions, %d differ, total %d bytes"
          % (len(rows), bad, sum(distance_of(e) for e in rows.values() if not e["ok"])))


def need_baseline():
    if not (os.path.exists(BASE) and os.path.exists(SYMS)):
        sys.exit("no baseline: run `tools/relinkscreen.py --baseline` first")


def screen(src, edits_py, fns):
    need_baseline()
    base = load(BASE)
    src_rel = os.path.relpath(os.path.abspath(src), REPO)

    def go():
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as t:
            out = t.name
        os.remove(out)
        try:
            run_relink(out, {"RELINK_ONLY": src_rel, "RELINK_LOAD": SYMS})
            return load(out)
        finally:
            if os.path.exists(out):
                os.remove(out)

    new = with_edits(src, edits_py, go)
    for fn in fns:
        if fn not in new:
            print("%s MISSING (not in this unit's output)" % fn)
            continue
        b = base.get(fn)
        old = distance_of(b) if b else None
        same = b is not None and new[fn]["ours"] == b["ours"]
        print("%s %s -> %d%s" % (fn, old, distance_of(new[fn]), "  (identical)" if same else ""))


def confirm(src, edits_py):
    need_baseline()
    base = load(BASE)

    def go():
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as t:
            out = t.name
        os.remove(out)
        try:
            run_relink(out, {})
            return load(out)
        finally:
            if os.path.exists(out):
                os.remove(out)

    new = with_edits(src, edits_py, go)
    lost = [f for f, e in base.items() if e["ok"] and not new.get(f, {}).get("ok")]
    gained = [f for f, e in base.items() if not e["ok"] and new.get(f, {}).get("ok")]
    print("lost %s gained %s" % (lost, gained))
    for fn, e in new.items():
        if fn in base and e["ours"] != base[fn]["ours"]:
            print("%s %d -> %d" % (fn, distance_of(base[fn]), distance_of(e)))
    print("match %d differ %d" % (sum(1 for e in new.values() if e["ok"]),
                                  sum(1 for e in new.values() if not e["ok"])))


def listing():
    need_baseline()
    rows = sorted(((distance_of(e), fn) for fn, e in load(BASE).items() if not e["ok"]),
                  reverse=True)
    for d, fn in rows:
        print("%5d %s" % (d, fn))
    print("%d functions, %d bytes" % (len(rows), sum(d for d, _ in rows)))


def main(argv):
    if len(argv) >= 2 and argv[1] in ("--baseline", "--rebase"):
        return baseline()
    if len(argv) >= 2 and argv[1] == "--list":
        return listing()
    if len(argv) >= 4 and argv[1] == "--confirm":
        return confirm(argv[2], argv[3])
    if len(argv) >= 4 and not argv[1].startswith("-"):
        return screen(argv[1], argv[2], argv[3:])
    sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
