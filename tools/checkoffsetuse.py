#!/usr/bin/env python3
"""Do a reconstruction's structure offsets match the ones the original reads?

Every other ratchet here guards NAMES -- duplicates, drift, stale seams,
missing installs. None guards that a name's VALUE is the offset the original
actually touches, and that gap let a real defect ship: DeployVehicle used
OBJ_OFF_OWNER (0x04) where the code reads +0x10 (OBJ_OFF_ARMY), at three
sites, past a clean A/B.

It survived because the branch it guarded sets only AI_MODE = 6, which is a
dead store in the original -- two defects cancelling, which no whole-program
comparison can see.

The check: for each reconstructed function, collect the displacement set the
ORIGINAL's instruction stream references off a register, collect the set the
C's *_OFF_* macros expand to, and report offsets used by one and not the
other. KNOWN BLIND SPOTS, measured on real functions rather than guessed:
  - the original may compute a field address with  instead of a
    memory operand, which this never sees (FormationSlotPoint's 0x12);
  - the C may reach a field through a typed struct member where the asm uses a
    raw displacement, so no *_OFF_* macro exists (that function's 0x2);
  - array indexing and absolute addresses are excluded, the latter by the
    0x400000 test above.
  - a field reached as `[reg*scale + absolute]` folds the TABLE BASE and the
    field offset into one displacement, so the offset never appears alone --
    ObjTileHook reads PAD_OFF_DAMAGE as [ecx*8 + 0x5161CC], which is
    ADDR_PADS + 0x34;
  - a field at offset zero is `[reg]` with no displacement at all.
  - an ARGUMENT of a frameless function is [esp + N] and is skipped, but only
    esp is: a function with no `mov ebp, esp` has its [ebp + N] read as
    fields, which is right far more often than not (see below);
  - a SCALED index hides the displacement: `[ebx + ebp*4 + 0x54C]` is an array
    of records at a named offset, and the operand regex below matches only
    `[reg + reg + disp]` with no scale, so the C is reported as naming an
    offset the original "does not read". TrooperPickupItem's 0x54C is the
    example. Fixable by widening the regex; left alone because doing so
    changes the answer for every function and would need re-validating
    against the whole set.
  - a `lea reg, [reg + N]` on a SCALAR is arithmetic, not a field --
    UnitKindMatches' +1, +2 and +11 all report as unnamed offsets. Excluding
    `lea` would be worse: a `lea` on a struct pointer IS a field reference.
  - a function with a TRAILING JUMP TABLE has that table decoded as
    instructions, and its address bytes read as displacements --
    OnSelectionChanged's table at 0x00427B7C produces a phantom 0x7B, which is
    a byte of the address 0x00427B51. Data in .text desynchronising a linear
    decode is the same hazard CLAUDE.md records for am2.Image().disasm(); here
    it surfaces as a field that does not exist.
Validated by running it on a function known good: ObjMoveAlongFacing reports
"sets agree", 19 for 19. It is a HEURISTIC, not a proof -- the original indexes arrays and uses
literal displacements the C may legitimately spell differently -- so it is
meant to run as a report over a named function, not as a wall.

Regression test: revert those three sites to OBJ_OFF_OWNER; this must flag
0x04 as used-by-C-not-original and 0x10 as the reverse.
"""

import os, re, subprocess, sys

REPO = os.environ.get('AM2_REPO',
                      os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def _function_end(addr):
    """Where the function at `addr` really ends.

    disasm.py prints a whole docs/functions.tsv ENTRY, and at least 128 of
    those below the CRT line are several functions run together. On a merged
    entry every field of the NEIGHBOUR is reported as one the C failed to
    name -- ShooterReact came back with six phantoms, all of them belonging to
    the 549-byte function sharing its entry. tools/merges.py already knows
    where the splits are, so ask it.
    """
    sys.path.insert(0, os.path.join(REPO, 'tools'))
    import am2, merges
    for entry, (splits, size) in merges.real_functions(am2.Image()).items():
        if entry <= addr < entry + size:
            later = [p for p in splits if p > addr]
            return min(later) if later else entry + size
    return None

def _trim_to_function(out, addr):
    end = _function_end(addr)
    if end is None:
        return out
    keep = []
    for line in out.splitlines():
        m = re.match(r'\s+0x([0-9a-f]+)\s', line)
        if m and not (addr <= int(m.group(1), 16) < end):
            continue
        keep.append(line)
    return '\n'.join(keep)

def orig_offsets(addr):
    """Displacements the ORIGINAL reads or writes off a register."""
    out = subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'disasm.py'),
                          hex(addr)], capture_output=True, text=True).stdout
    out = _trim_to_function(out, addr)
    seen = set()
    # esp displacements are stack frame slots, never structure fields.
    #
    # ebp DEPENDS, and this used to say there was no cheap way to tell. There
    # is: a frame pointer is ESTABLISHED, by `mov ebp, esp`. A function that
    # never writes esp into ebp is using it as a general register, and its
    # [ebp + N] are fields. CanPickUpWeapon does `push ebp; mov ebp, [esp+8]`
    # and ObjInitCommon `push ebp; mov ebp, [esp+0x4c]` -- neither has the
    # prologue, and skipping ebp reported ObjInitCommon as touching THREE
    # displacements where it touches twelve, which is not a false positive so
    # much as no check at all.
    frame_ptr = re.search(r'\bmov\s+ebp, esp\b', out) is not None
    skip = ('esp', 'ebp') if frame_ptr else ('esp',)
    for m in re.finditer(r'\[(e[a-z][a-z])(?: \+ e[a-z][a-z])? \+ (0x[0-9a-f]+|\d+)\]', out):
        if m.group(1) in skip:
            continue
        if int(m.group(2), 0) >= 0x400000:
            continue        # an absolute image address, not a field
        seen.add(int(m.group(2), 0))
    return seen

def c_offsets(path, func):
    """Values the *_OFF_* macros in one C function expand to."""
    src = open(path if os.path.isabs(path) else os.path.join(REPO, path),
               encoding='utf-8',
               errors='surrogateescape').read()
    # THISCALL COUNTS TOO, and matching only __cdecl meant every widget
    # constructor and every vtable slot in the tree -- the whole menu layer --
    # answered "no definition of" and was never checked by this tool at all.
    # Found by pointing it at WarMenuConstruct; DifficultyDialogConstruct,
    # written long before, had the same non-answer.
    m = re.search(r'^\w[\w \*]*(?:__cdecl|__attribute__\(\(thiscall\)\))\s+'
                  + func + r'\(', src, re.M)
    if not m:
        raise SystemExit('no definition of ' + func)
    i = m.start()
    body = src[i:src.index('\n}\n', i)]
    # Offsets live in orig.h AND in the module headers -- CHECK_OFF_TICKED is
    # in win32/widget.h, and looking only at orig.h reported it unnamed.
    hdr = ''
    for root, _, files in os.walk(os.path.join(REPO, 'src')):
        for f in files:
            if f.endswith('.h'):
                hdr += open(os.path.join(root, f), encoding='utf-8',
                            errors='surrogateescape').read()
    vals = set()
    for name in set(re.findall(r'\b[A-Z][A-Z0-9_]*_OFF_[A-Z_0-9]+\b', body)):
        m = re.search(r'#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % name, hdr)
        if m:
            vals.add(int(m.group(1), 0))
    return vals

if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit('usage: checkoffsetuse.py <0xADDR> <src/path.cpp> <FuncName>')
    a = int(sys.argv[1], 16)
    o, c = orig_offsets(a), c_offsets(sys.argv[2], sys.argv[3])
    only_c = sorted(c - o)
    only_o = sorted(o - c)
    print(f"{sys.argv[3]}: original touches {len(o)} displacements, "
          f"C names {len(c)}")
    if only_c:
        print("  in the C but NOT read by the original: "
              + ', '.join(hex(v) for v in only_c))
    if only_o:
        print("  read by the original but not named in the C: "
              + ', '.join(hex(v) for v in only_o))
    if not only_c and not only_o:
        print("  sets agree")
