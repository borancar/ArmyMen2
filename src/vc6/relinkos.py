#!/usr/bin/env python3
"""Generate src/vc6/include/am2_relink_os.h: the relink build's view of the
.bss working state as one symbol per global, not one struct.

The standalone build reaches every .bss global the game uses through a member
of am2_origstate, the single struct spanning the original's working region
(standalone.h, `#define ADDR_X ((uintptr_t)(const void *)&am2_origstate.m)`).
That is right for layout and wrong for codegen: VC6 knows a struct member's
neighbours are readable, so a byte global read into an int compiles to
`mov eax, [m]; and eax, 0xFF`, where the original -- a lone `unsigned char`
-- is `xor eax, eax; mov al, [m]`. Only an object whose size the compiler
does not know gives the original's form, so under AM2_RELINK each redirect is
re-pointed at `am2_os_<member>`, an incomplete `uint8_t[]`, and relink.py maps
that symbol to the member's VA (origstate.cpp pins every offset with a
static_assert, which is where the VAs come from).

Only VC6 compiles include src/vc6/include, and only AM2_RELINK ones include
this header, so no build that runs is affected.

    src/vc6/relinkos.py            # rewrite the header if it changed
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "src", "vc6", "include", "am2_relink_os.h")
ORIGSTATE_BASE = 0x0048E000

# Members that were ONE object in the original, by name prefix. None now:
# the aim markers' tables are a range below, and the other aim_ globals (the
# aim point, the lifetimes, the damage and spawn argument, far from the
# tables) are lone globals.
AGGREGATES = ()

# The same, by address: [lo, hi) ranges that were one object. The live game
# settings 0x00515FD8..0x00516078 (game-over flags, the setting at +0x22C,
# the army points, score limit, both live names) are one struct in the
# original: ApplyGameSettings' three setting stores stay after its strcpy
# into the live names, which VC6 does only when the copy's destination may
# alias them, while the loads from the host masks -- a separate object --
# are scheduled into the copy. Split into separate symbols, VC6 hoists the
# stores too.
# The level block 0x00511BC8..0x00511DA0 likewise: MissionEnded's two
# reload arms re-read ADDR_LEVEL_INDEX after the strcpy into
# ADDR_SCRIPT_RELOAD_PATH rather than hoisting the load into the copy.
# The keyboard state 0x005127C8..0x00512FD0 (both buffer pointers, the
# repeat deadlines, the pressed flags): PollKeyboard re-reads the current
# buffer pointer every key because its stores may alias it, and walks the
# two per-key arrays with one pointer.
# The aim markers' tables 0x004FC8C8..0x004FC994, both sets (sprites, live
# flags, points, stamps, deadlines): FreeAimSprites runs a pointer from the
# sprites up to the live flags and AimInit likewise, so VC6 must be able to
# share one base between them. The aim_ globals outside it were swept in by
# a name prefix once; as lone globals AimStartB keeps its element pointer and
# PlacementScreenClick matches.
# The sprite list 0x004F96C0..0x004F96CC (capacity, count, the array
# pointer): LoadSpriteSet's growth path matches only as one object.
# The menu cursor's animation state 0x004FCAA8..0x004FCDF4 (the hovered row,
# the per-row sprite table, the cursor and overlay sprites, inks, offsets,
# the frame clock and its stamp): OverlayPrepare loads the row's first sprite
# BEFORE storing the frame index, the source order, which VC6 keeps only
# when the indexed table load may alias the store.
AGGREGATE_RANGES = ((0x004F96C0, 0x004F96CC), (0x004FC8C8, 0x004FC994), (0x00515FD8, 0x00516078), (0x00511BC8, 0x00511DA0), (0x005127C8, 0x00512FD0),
                    (0x004FCAA8, 0x004FCDF4))


def _aggregated(va):
    return any(lo <= va < hi for lo, hi in AGGREGATE_RANGES)


# Aggregated ranges whose members stay DECLARED (see render()). Retuned
# whenever a shared struct grows: CheatLine's and PlacementScreenClick's
# operand order is a tie VC6 breaks by state the whole TU builds up, so
# adding struct members or externs can tip it. Empty matched once the
# comm and player-message structs were typed; the trooper fields on
# AM2_Object tipped it back; keeping the sprite list's and the level
# block's declarations settled both, until the map row, row set and sprite
# record were typed and empty matched again; the font record tipped it once
# more, and the sprite list's alone settles it; the object-script fields
# tipped it again, and the aim sprites' and the menu row's declarations
# beside the sprite list's settled it; with the object table typed the aim
# sprites' alone did, and with the map descriptor typed the sprite list's
# rejoins it. Search the subsets (empty first) when it flips.
KEEP_DECLS = ((0x004F96C0, 0x004F96CC), (0x004FC8C8, 0x004FC994))


def _kept(va):
    return any(lo <= va < hi for lo, hi in KEEP_DECLS)

REDIRECT = re.compile(r"^#define (ADDR_\w+) \(\(uintptr_t\)\(const void \*\)&?am2_origstate\.(\w+)\)$", re.M)
PIN = re.compile(r"offsetof\(struct AM2_OrigState, (\w+)\) == 0x([0-9A-Fa-f]+)u? - 0x0048E000u")


MEMBER = re.compile(r"^\s+(\w+)\s+(\w+)((?:\[[^\]]*\])*);", re.M)


def typed_members():
    """member -> (type, is_array) for the members origstate.h declares with
    a type from globaltypes.h. Those get a typed extern here, so the game
    code's typed global names (globals.h) have something to bind to; every
    other member stays the incomplete uint8_t[] the codegen note above wants."""
    types = set(re.findall(r"\}\s*(AM2_\w+);",
                           open(os.path.join(REPO, "src", "game", "globaltypes.h")).read()))
    src = open(os.path.join(REPO, "src", "game", "origstate.h")).read()
    body = src[src.index("struct AM2_OrigState {"):]
    return {m.group(2): (m.group(1), bool(m.group(3)))
            for m in MEMBER.finditer(body) if m.group(1) in types}


def member_vas():
    """member -> VA, from origstate.cpp's static_asserts."""
    src = open(os.path.join(REPO, "src", "game", "origstate.cpp")).read()
    return {m.group(1): int(m.group(2), 16) for m in PIN.finditer(src)}


def redirects():
    """[(ADDR_X, member)] in standalone.h order."""
    return [(a, m) for a, m in
            REDIRECT.findall(open(os.path.join(REPO, "src", "inject", "standalone.h")).read())
            if not m.startswith(AGGREGATES)]


def symbols():
    """COFF symbol -> VA for every member a redirect names."""
    vas = member_vas()
    return {"_am2_os_" + m: vas[m] for _, m in redirects()
            if m in vas and not _aggregated(vas[m])}


def render():
    vas = member_vas()
    reds = [(a, m) for a, m in redirects()
            if m in vas and not _aggregated(vas[m])]
    # The SET of externs declared shifts VC6's register tie-breaks in
    # functions that never name them: dropping the keyboard block's four
    # turned CheatLine's cursor sum around. Ranges in KEEP_DECLS lose their
    # redirects but keep their declarations. Which set is right is empirical
    # and moves with every other change to the set: with the aim globals
    # split and the sprite list aggregated, keeping ALL five ranges'
    # declarations is what matches both CheatLine and PlacementScreenClick
    # (2026-10-04); keeping the keyboard block's alone once did, and keeping
    # the settings and level blocks once cost CheatLine.
    members = sorted({m for _, m in redirects() if m in vas
                      and (not _aggregated(vas[m]) or _kept(vas[m]))},
                     key=lambda m: vas[m])
    out = ["/* GENERATED by src/vc6/relinkos.py -- do not edit.",
           " *",
           " * AM2_RELINK only: each .bss global as its own incomplete array, so VC6",
           " * reads it as the original's lone global rather than as a struct member.",
           " * See relinkos.py. */",
           "#ifndef AM2_RELINK_OS_H",
           "#define AM2_RELINK_OS_H",
           "",
           "",
           "#ifdef __cplusplus",
           'extern "C" {',
           "#endif",
           ]
    typed = typed_members()
    for m in members:
        if m in typed:
            t, arr = typed[m]
            out.append("extern %s am2_os_%s%s;  /* 0x%08X */" % (t, m, "[]" if arr else "", vas[m]))
        else:
            out.append("extern uint8_t am2_os_%s[];  /* 0x%08X */" % (m, vas[m]))
    out += ["#ifdef __cplusplus", "}", "#endif", ""]
    for a, m in reds:
        scalar = m in typed and not typed[m][1]
        out += ["#undef %s" % a, "#define %s ((uintptr_t)(const void *)%sam2_os_%s)"
                % (a, "&" if scalar else "", m)]
    out += ["", "#endif /* AM2_RELINK_OS_H */", ""]
    return "\n".join(out)


def generate():
    text = render()
    try:
        old = open(OUT).read()
    except OSError:
        old = None
    if old != text:
        open(OUT, "w").write(text)
    return OUT


if __name__ == "__main__":
    print(generate())
    missing = [m for _, m in redirects() if m not in member_vas()]
    if missing:
        sys.stderr.write("unpinned members (left on am2_origstate): %s\n" % ", ".join(sorted(set(missing))))
