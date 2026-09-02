#!/usr/bin/env python3
"""Find calls into our own code that go through the image.

An `orig_` name means "this stays in the original image". When the address it
resolves to is in the patch list, the call goes through the detour and lands in
our OWN code -- correct behaviour under a name that says the opposite.

Harmless most of the time, and not always: AM2_PROBE_NOACTION was a switch that
selected the original, and once its address was patched it selected us instead
and quietly re-recorded the oracle from the code it existed to check.

Four of these had to be found by grep before this existed -- InitInput,
DirectDraw and ReportError in winmain.cpp; SetGameDir under `orig_path_exists`
in three modules; the pause pair in dplay.cpp; CreateOffscreenSurface and
ClearSurface in device.cpp. Each time the comment beside it had gone stale too,
which is the real cost: `orig_create_offscreen` sat under "on the list to
reconstruct next" for however long after it was done.

`orig_` is one spelling of that mistake and not the only one. frame.cpp reaches
a dozen still-original functions as `call0(ADDR_X)`, which is fine until X is
reconstructed -- and `call0(ADDR_MOVIE_FRAME_STEP)` sat there for exactly as
long as it took to reconstruct 0x00445630. It works, because the address is
patched and the call lands in our code; what it costs is the same thing the
orig_ case costs. It also makes tools/blindspots.py wrong in the other
direction: it reported MovieStepCurrent blind because both callers are
reconstructed, while the counter read 746,792, because those callers were
reaching it by address.

So both spellings are checked: a `#define orig_x ... ADDR_Y`, and a
`callN(ADDR_Y)` written out at a call site.

Deliberate ones are listed in ALLOWED, with the reason.

There is a FOURTH spelling and it IS a gate now: naming a reconstructed
address anywhere in src/game except its own patch_replace. That covers the
three above and everything they missed -- a menu handler installed by ADDRESS
(`MakeButton(..., ADDR_ON_MENU_BACK)`, with the helper applying AM2_IMAGE to
the parameter), an inline `((Fn)(uintptr_t)ADDR_X)(...)`, and a table of
plain integers that are function pointers.

It looked unpromotable at first, reporting about two hundred sites and a
caveat that "some are data, not calls". Both were wrong for the same reason:
comments were being scanned, and every ADDR_ name in this tree is discussed in
one. With comments stripped it was 21, all of them calls, and closing them was
an afternoon rather than a project.

Two bugs in this file came out of that. The single-line `#define` match missed
every macro continued with a backslash -- six real seams, unreported for as
long as they had existed. And the by-address rule needed comment stripping to
be worth reading at all. A check nobody can read is a check nobody acts on.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# `orig_` macros that MUST keep pointing at a patched address, and why.
ALLOWED = {
    # The probe that re-records tests/actions-reference.txt. It only reaches
    # the original because script_install skips the patch under the same flag.
    "orig_parse_action",
}


def join_continuations(text):
    """Fold `\\`-continued lines onto the first, leaving blanks behind.

    The first version folded exactly ONE continuation. It appended a blank
    after each fold, so the next line saw `out[-1] == ""` rather than the
    line it had just extended, and a macro continued TWICE kept its tail on
    a line of its own -- with the joined half still ending in a backslash and
    holding no ADDR_ name at all. The gate was therefore green over every
    three-line seam macro in the tree. `orig_comm_army_of_slot` was one, and
    it had been reaching our own reconstructed CommArmyOfSlot through the
    image for as long as both existed.

    Folding until the accumulated line no longer ends in a backslash is the
    fix; the blanks still hold the line numbering steady.
    """
    out = []
    target = None
    for line in text.split("\n"):
        if target is not None:
            out[target] = out[target][:-1] + " " + line.strip()
            out.append("")
            if not out[target].endswith("\\"):
                target = None
        else:
            out.append(line)
            if line.endswith("\\"):
                target = len(out) - 1
    return "\n".join(out)


def strip_comments(text):
    """Blank out /* */ and // comments, keeping every newline in place."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def macros():
    out = {}
    text = open(os.path.join(ROOT, "src/inject/orig.h")).read()
    for m in re.finditer(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u", text):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def main():
    addr = macros()
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import coverage

    patched = set()
    for f in glob.glob(os.path.join(ROOT, "src/game/**/*.cpp"), recursive=True):
        for m in re.finditer(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)", open(f).read()):
            if m.group(1) in addr:
                patched.add(addr[m.group(1)])
    for n in coverage.REGISTERED:
        if n in addr:
            patched.add(addr[n])

    bad = []
    for f in sorted(glob.glob(os.path.join(ROOT, "src/**/*.c*"), recursive=True)
                    + glob.glob(os.path.join(ROOT, "src/**/*.h"), recursive=True)):
        rel = os.path.relpath(f, ROOT)
        # Comments are blanked for the by-address rule and only for it. The
        # other three match code shapes that do not occur in prose; this one
        # matches a bare identifier, and every ADDR_ macro in this tree is
        # discussed in a comment somewhere. Newlines are kept so the line
        # numbers still name the right line.
        raw = open(f).read()
        # A `#define` continued with a backslash puts the macro name on one
        # line and the address on the next, and the first rule below matches
        # a single line -- so every multi-line orig_ macro was invisible to
        # it. Six of them were, all genuine seams. Joining continuations
        # first costs nothing and keeps the line numbers, because the joined
        # text replaces the FIRST line and the rest become blank.
        raw = join_continuations(raw)
        code = strip_comments(raw)
        for n, (line, bare) in enumerate(zip(raw.split("\n"),
                                             code.split("\n")), 1):
            m = re.match(r"\s*#define\s+(orig_\w+)\s+.*?(ADDR_[A-Z0-9_]+)", line)
            if m:
                name, sym = m.group(1), m.group(2)
                if name not in ALLOWED and sym in addr and addr[sym] in patched:
                    bad.append((rel, name, sym, addr[sym]))
                continue
            # The other spelling: a call site that names the address itself.
            # Only in a call, not in a patch_replace or a #define of the macro.
            # A #define is skipped ONLY when it is a patch_replace line.
            # It used to skip every #define that rule one had not already
            # matched, which left a hole exactly the width of a macro that
            # SHADOWS the reconstruction's own name: `#define SendVehicleFire
            # ((Fn)(uintptr_t)ADDR_SEND_VEHICLE_FIRE)` is a seam in every
            # sense, and is invisible to rule one because it is not spelled
            # `orig_`. region.cpp carried one for a commit; the sibling in
            # item.cpp WAS spelled orig_ and was reported, so the pair made
            # the gap visible. Falling through to the "named by address" rule
            # below costs nothing -- that rule only fires on a patched
            # address, so a #define naming unpatched data is still quiet.
            if "patch_replace" in line:
                continue
            for m in re.finditer(r"\bcall\d\(\s*(ADDR_[A-Z0-9_]+)", line):
                sym = m.group(1)
                if sym in addr and addr[sym] in patched:
                    bad.append(("%s:%d" % (rel, n), "call site", sym,
                                addr[sym]))
            # And the third spelling, which is how six of these hid. A cast
            # around AM2_IMAGE(ADDR_X) is a function pointer to the image
            # exactly as an orig_ macro is -- it just does not look like one,
            # because it is written inline at the point of use rather than
            # given a name. Found when a SEVENTH appeared and only that one
            # was reported, because it happened to have been given a name.
            for m in re.finditer(r"\)\s*AM2_IMAGE\(\s*(ADDR_[A-Z0-9_]+)\s*\)",
                                 line):
                sym = m.group(1)
                if sym in addr and addr[sym] in patched:
                    bad.append(("%s:%d" % (rel, n), "cast through the image",
                                sym, addr[sym]))
            # The FOURTH spelling: the address named at all. Comments are
            # stripped for this one and only this one -- the other three
            # match code shapes that do not occur in prose.
            if rel.startswith("src/game/"):
                for m in re.finditer(r"\b(ADDR_[A-Z0-9_]+)\b", bare):
                    sym = m.group(1)
                    if sym in addr and addr[sym] in patched:
                        bad.append(("%s:%d" % (rel, n), "named by address",
                                    sym, addr[sym]))
            # The FIFTH spelling, and the one the fourth's own docstring
            # promised was covered when it was not. "A table of plain
            # integers that are function pointers" is only caught while the
            # integers are written as ADDR_ names; a BARE HEX LITERAL is not
            # an ADDR_ name, so nothing looked at it.
            #
            # It had eight live instances when this rule was added, in the
            # two places a table of addresses exists: frame.cpp's nine
            # sub-state painters, five of which are ours, and winmain.cpp's
            # teardown table, whose comment said in so many words that the
            # remaining integers were "still the original's and the shape
            # says which is which". The shape did not say. Two of the four
            # were reconstructed, and so was the thiscall call above the
            # loop.
            #
            # Five hex digits is the floor because a four-digit constant is
            # ordinarily a mask or a message id; every address in this image
            # is at least 0x400000. Data addresses cannot collide, since a
            # patched address is by definition code.
            if rel.startswith("src/game/"):
                for m in re.finditer(r"\b0x0*([0-9A-Fa-f]{5,8})u?\b", bare):
                    val = int(m.group(1), 16)
                    if val in patched:
                        bad.append(("%s:%d" % (rel, n), "bare literal",
                                    "0x%08X" % val, val))

    for f, name, sym, a in bad:
        print("  %s: %s -> %s (0x%08X) is reconstructed; call it directly"
              % (f, name, sym, a))
    if bad:
        print("  %d site(s) reach our own code through the image. Add to"
              " ALLOWED only with a reason." % len(bad))
        return 1
    print("nothing reaches a reconstructed address through the image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
