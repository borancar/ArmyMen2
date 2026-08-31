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

def orig_offsets(addr):
    """Displacements the ORIGINAL reads or writes off a register."""
    out = subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'disasm.py'),
                          hex(addr)], capture_output=True, text=True).stdout
    seen = set()
    # esp/ebp displacements are stack frame slots, not structure fields --
    # right for a standard prologue and WRONG whenever ebp is a general
    # register. CanPickUpWeapon does `push ebp; mov ebp, [esp+8]`, so its
    # [ebp + 0xC8] is OBJ_OFF_PICKUP_AFTER and this reports it missing. There
    # is no cheap way to tell the two apart; it stays a known false positive.
    for m in re.finditer(r'\[(e[a-z][a-z])(?: \+ e[a-z][a-z])? \+ (0x[0-9a-f]+|\d+)\]', out):
        if m.group(1) in ('esp', 'ebp'):
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
    m = re.search(r'^\w[\w \*]*__cdecl\s+' + func + r'\(', src, re.M)
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
