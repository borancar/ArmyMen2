"""The menu's 21 screen factories, read out of the jump table that reaches
them.

RunFrame's menu-request handler dispatches through a jump table at 0x00426518.
Every arm is seven bytes -- `call <factory>; jmp end` -- and every factory
opens one screen: destroy whatever dialog is currently the repaint object at
0x0065A058, allocate, construct on the allocation, and store the
CONSTRUCTOR'S RETURN back into that global.

That last step is why this table is worth having in front of you before
touching any of them. `mov [0x0065A058], eax` after the call is the same
load-bearing return that made RecordCtor's omission fatal -- see
tools/checkthis.py -- and it appears here twenty-one times.

Most screens open on the shared 01_000_00_screen.bmp, so the FACTORY's bitmap
names only seven of them. The constructor's own bitmaps name nine more, which
is why the report reaches into the constructor rather than stopping at the
push it can see. Two arms take a branch first and construct one of two
backdrops depending on 0x00511DA4, so they appear with both.

Sizes come from the `push N` in front of the call to operator new at
0x00464900, and N is a BYTE push for everything under 0x80 -- a scan that
looks only for `68 imm32` reports two thirds of the table as unknown, which is
how the first version of this tool read it.

Writes docs/screens.md.
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone

import am2
import coverage

TABLE = 0x00426518
ARMS = 21
OPERATOR_NEW = 0x00464900
CRT_START = 0x0045C000
STATE = 0x00511DA4
SHARED = "01_000_00_screen.bmp"


def bitmaps(img, addr, span=0x200):
    """Every .bmp string pushed in the first `span` bytes of a function."""
    body = img.read(addr, span)
    out = []
    for i in range(len(body) - 5):
        if body[i] != 0x68:
            continue
        v = struct.unpack_from("<I", body, i + 1)[0]
        if not 0x470000 <= v < 0x4A0000:
            continue
        try:
            t = img.read(v, 64).split(b"\0")[0].decode("latin-1")
        except Exception:
            continue
        if t.endswith(".bmp") and t not in out:
            out.append(t)
    return out


PAINT_OBJECT = 0x0065A058


def factory(img, addr, span=0x200):
    """(size, ctor, backdrop, argc) for one factory, found from the STORE.

    Not from the first `operator new` in the body: two of the twenty-one
    allocate something else first. The definitive tell is
    `mov [0x0065A058], eax` -- the store of the constructor's RETURN -- so the
    constructor is the last `call` before it and the size is the last `push`
    before the call to operator new.

    `argc` is how many stack arguments the constructor takes, which is the one
    number here that a reconstruction cannot get wrong quietly: these are
    thiscall, so the CALLEE pops, and calling a two-argument constructor with
    one argument corrupts the stack rather than merely painting the wrong
    screen. All 21 take exactly one today.

    Decoded forward rather than scanned. A byte-wise search for 0x68/0x6A
    walking backwards from the call finds other instructions' OPERANDS and
    reports sizes like 0x8800511A, which is the mistake docs/ already records
    against an aligned-dword cross-reference scan.

    And the push is not always the instruction in front of the call: arm 1
    sets three globals in between.
    """
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    code = img.read(addr, span)

    size = ctor = back = None
    last_push = last_call = None
    pushes = []
    args = 0
    for insn in md.disasm(code, addr):
        m = insn.mnemonic
        if m == "push":
            pushes.append(insn.op_str)
            op = insn.op_str
            if op.startswith("0x") or op.lstrip("-").isdigit():
                try:
                    last_push = int(op, 0)
                except ValueError:
                    last_push = None
                if last_push is not None and 0x470000 <= last_push < 0x4A0000:
                    try:
                        t = img.read(last_push, 64).split(b"\0")[0]
                        t = t.decode("latin-1")
                        if t.endswith(".bmp"):
                            back = t
                    except Exception:
                        pass
        elif m == "call":
            try:
                target = int(insn.op_str, 0)
            except ValueError:
                continue
            if target == OPERATOR_NEW:
                pushes = []
            if target == OPERATOR_NEW and size is None:
                # The FIRST allocation, not the last. Two arms construct one
                # of two backdrops behind a `cmp [0x00511DA4], 2`; both
                # branches allocate the same size, and a linear decode walks
                # through the not-taken one, where the last push before the
                # second call is a string.
                size = last_push
            elif 0x400000 < target < CRT_START:
                last_call = target
                args = len(pushes)
                pushes = []
        elif m == "mov" and insn.op_str.startswith("dword ptr [0x%x], eax"
                                                   % PAINT_OBJECT):
            ctor = last_call
            break
    return size, ctor, back, args


def main():
    img = am2.Image()
    done, _ = coverage.reconstructed(coverage.addr_table())

    rows = []
    table = struct.unpack("<%dI" % ARMS, img.read(TABLE, 4 * ARMS))
    for arm, stub in enumerate(table, start=1):
        b = img.read(stub, 5)
        if b[0] != 0xE8:
            rows.append((arm, stub, None, [], [], [], "not a call"))
            continue
        f = stub + 5 + struct.unpack_from("<i", b, 1)[0]
        size, ctor, back, argc = factory(img, f)
        # Name the screen: the factory's own backdrop if it is specific, else
        # the constructor's. Four of them have neither and are identified by
        # their BUTTONS instead, which is the honest answer rather than a name
        # borrowed from whatever sits next in the image.
        name = back if back and back != SHARED else ""
        if not name and ctor:
            own = [x for x in bitmaps(img, ctor, 0x200)
                   if x != SHARED and not x.startswith("03_")]
            name = own[0] if own else ""
        if not name and ctor:
            btns = [x for x in bitmaps(img, ctor, 0x200)
                    if x.startswith("03_")]
            seen, keep = set(), []
            for x in btns:
                k = x[:6]
                if k not in seen:
                    seen.add(k)
                    keep.append(x)
            name = ("buttons: " + ", ".join(keep[:3])) if keep else ""
        rows.append((arm, stub, f, [size] if size else [],
                     [ctor] if ctor else [], [back], name, argc))

    out = []
    out.append("# The menu screen factories")
    out.append("")
    out.append("Generated by `tools/screens.py` -- do not edit.")
    out.append("")
    out.append("The 21-entry jump table at `0x%08X`, one arm per menu request."
               % TABLE)
    out.append("Each arm is `call <factory>; jmp end`; each factory closes the")
    out.append("current screen, allocates, constructs, and stores the")
    out.append("constructor's RETURN into `0x0065A058`.")
    out.append("")
    out.append("`size` is the argument to `operator new`. Two arms construct")
    out.append("one of two backdrops depending on `0x%08X` and show both."
               % STATE)
    out.append("")
    out.append("`args` is how many stack arguments the constructor takes.")
    out.append("These are thiscall, so the CALLEE pops: reconstructing a")
    out.append("two-argument constructor with one argument corrupts the stack")
    out.append("rather than merely painting the wrong screen.")
    out.append("")
    out.append("| arm | stub | factory | size | ctor | args | screen | done |")
    out.append("|---:|---|---|---|---|---:|---|---|")
    for arm, stub, f, sizes, ctors, backs, name, argc in rows:
        if f is None:
            out.append("| %d | `0x%08X` | -- | | | | | |" % (arm, stub))
            continue
        out.append("| %d | `0x%08X` | `0x%08X` | %s | %s | %d | %s | %s |"
                   % (arm, stub, f,
                      ", ".join("0x%X" % s for s in sizes) or "?",
                      ", ".join("`0x%08X`" % c for c in ctors if c) or "?",
                      argc, name or "(shared backdrop)",
                      "yes" if f in done else ""))
    out.append("")

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "docs", "screens.md")
    text = "\n".join(out)
    with open(os.path.normpath(path), "w") as fp:
        fp.write(text)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
