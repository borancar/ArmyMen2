"""Find the COM / DirectX dispatch sites in ArmyMen2.exe.

tools/imports.py sees only the IAT, and DirectX is almost invisible there: the
game imports DirectDrawCreate, DirectSoundCreate and DirectInputCreateA once
each, and from then on every call goes through an interface vtable. So the real
DirectDraw/DirectSound/DirectInput/DirectPlay boundary is a set of indirect
calls that no import scan can see.

The shape being matched is the canonical MSVC 6 COM dispatch:

    mov  reg2, [reg1]           ; reg1 = the interface, reg2 = its vtable
    ...                         ; arguments pushed, reg2 preserved
    call [reg2 + disp]          ; disp/4 = the method slot

so a `call dword ptr [reg + disp]` counts when some earlier instruction in the
same basic block loaded that register from a pointer dereference. Scanning
backwards is bounded and stops at anything that redefines the register, at a
branch target, and at a call, since the register would not survive one.

This UNDERCOUNTS, in two ways rather than one, and the second was measured
rather than guessed at.

The interface often cannot be named, which the `this` column already says.

Less obviously, the vtable register itself sometimes cannot be found. The
backward scan gives up at a `call`, because a call genuinely clobbers the
scratch registers being chased, and at a `ret`, because the block is then a
branch target whose predecessor is not visible. Both are correct and neither
can be relaxed: crossing the `ret` was tried and recovers nothing, since a
`call` sits between the load and the use in every one of these cases.

The size of that miss is known. Sweeping every `call [reg+disp]` below the CRT
with a plausible vtable displacement and subtracting what is recorded here
leaves EIGHT sites. Five are real DirectX calls -- PresentFrame's first Flip,
ClearSurface's Blt, InitDirectDraw's SetCooperativeLevel, and a Release in each
of the two bitmap loaders -- and all five are in functions that are already
reconstructed. The other three are not COM at all: two `call [esp+N]` callbacks
and one function pointer in a member field.

So the per-function counts are a floor, by five across the whole image, and no
function is missing from this file. That is a much stronger statement than
"lower bound" and it is worth re-measuring if the scan ever changes.

The slot number is reported rather than a method name: which interface a
register holds is not decidable from one site, so naming is left to whoever
reads the result against the SDK vtable order. `this` is recovered when the
interface came from a global, which is the common case here and is what ties a
site to a known object.

Writes docs/comcalls.tsv.
"""

import bisect
import collections
import csv
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "comcalls.tsv")
FUNCS = os.path.join(REPO, "docs", "functions.tsv")

OP_MEM = capstone.x86.X86_OP_MEM
OP_REG = capstone.x86.X86_OP_REG

CRT_START = 0x0045C000
# How far back to look for the vtable load. MSVC 6 pushes arguments between the
# load and the call, so this has to clear a full argument list.
LOOKBACK = 24


def load_functions():
    out = []
    with open(FUNCS) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            out.append((int(row["addr"], 16), int(row["size"]), row["tu"]))
    return sorted(out)


def owner(addr, funcs, addrs):
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return None
    start, size, tu = funcs[i]
    return (start, tu) if addr < start + size else None


def defining_load(insns, i, reg):
    """Walk back from insns[i] for the `mov reg, <src>` that defined it.

    Returns (src_operand, index), or None if the register was not defined by a
    mov within the window -- which includes hitting a call, since the scratch
    registers would not survive one.
    """
    for j in range(i - 1, max(-1, i - LOOKBACK) - 1, -1):
        insn = insns[j]
        if insn.mnemonic in ("call", "ret", "jmp"):
            return None
        if insn.mnemonic != "mov" or len(insn.operands) != 2:
            continue
        dst, src = insn.operands
        if dst.type != OP_REG or insn.reg_name(dst.reg) != reg:
            continue
        return (src, j)
    return None


def find_vtable_load(insns, i, reg):
    """Identify `call [reg + disp]` as vtable dispatch and recover `this`.

    The register must have been loaded by dereferencing something -- that load
    is the vtable fetch. Returns (this_global, at).

    `this_global` is only non-zero when the interface pointer itself came from
    a global, which is the discriminator that matters here: DirectDraw,
    DirectSound and DirectInput interfaces are kept in globals, whereas the
    game's own C++ objects live on the heap. Both compile to exactly the same
    two instructions, so without chasing the pointer one level further this
    would report every virtual method call in the engine as DirectX.
    """
    found = defining_load(insns, i, reg)
    if found is None:
        return None
    src, at = found
    if src.type != OP_MEM:
        # Not a dereference, so an ordinary function-pointer call.
        return None

    # mov vt, [global] -- the vtable pointer was itself read straight out of a
    # global, so the global holds the object and `this` is that address.
    if src.mem.base == 0 and src.mem.index == 0:
        return (src.mem.disp & 0xFFFFFFFF, at, None, None)
    if src.mem.index != 0 or src.mem.disp != 0:
        # [reg + something] is a member fetch, not a plain vtable load.
        return (0, at, None, None)

    # mov vt, [obj] -- chase `obj` back one more level to see where it came
    # from: a global, which names it, or a struct field, which does not name it
    # but does group it. The second case is not a lesser answer. DirectPlay is
    # reached entirely through comm+0x3EC and the sprite surfaces through
    # sprite+0x10, so a whole subsystem can live behind one displacement and be
    # invisible to a survey that only looks for globals.
    obj_reg = insns[at].reg_name(src.mem.base)
    outer = defining_load(insns, at, obj_reg)
    if outer is None:
        return (0, at, obj_reg, None)
    osrc, _oat = outer
    if osrc.type == OP_MEM and osrc.mem.base == 0 and osrc.mem.index == 0:
        return (osrc.mem.disp & 0xFFFFFFFF, at, obj_reg, None)
    if (osrc.type == OP_MEM and osrc.mem.index == 0 and osrc.mem.disp != 0
            and insns[at].reg_name(osrc.mem.base) not in ("esp", "ebp")):
        # esp- and ebp-relative loads are arguments and locals, not fields.
        # Counting them would group unrelated functions under whatever stack
        # offset they happened to use -- FillSoundBuffer takes its buffer as
        # an argument at [esp+0x10] and would otherwise be filed alongside the
        # sprites, whose surfaces really are at +0x10 of a record.
        return (0, at, obj_reg, osrc.mem.disp & 0xFFFFFFFF)
    return (0, at, obj_reg, None)


def call_abi(insns, i, obj_reg):
    """Tell a COM dispatch from an ordinary C++ virtual call.

    Both compile to the same two instructions, but they do not pass `this` the
    same way, and that *is* decidable from one site:

      COM      is stdcall. Under CINTERFACE every method takes the interface as
               an explicit first argument, so `this` is PUSHED like any other.
      C++      is thiscall on i386 MSVC. `this` travels in ecx and is never
               pushed.

    Returns "stdcall", "thiscall", or "?" when neither pattern is visible.

    This matters more than it looks. Without it the survey reports every
    virtual method call in the engine as DirectX, and the densest-looking
    candidates -- tiny functions that are three vtable calls and nothing else --
    turn out to be C++ destructor chains rather than boundary code. The MSVC
    scalar deleting destructor is the clearest example: `push 1` then slot 0.
    COM's slot 0 is QueryInterface and takes three arguments, so a one-argument
    slot-0 call is never COM.
    """
    # A CDECL DISCRIMINATOR WAS TRIED HERE AND DOES NOT WORK. The idea was
    # sound -- a COM method is stdcall and cleans its own arguments, so an
    # `add esp` after the call means the callee did not and it is a callback --
    # and it correctly identifies 0x0041F060, which walks a linked list calling
    # each node's function pointer.
    #
    # It is still wrong, because these functions push cdecl arguments AROUND
    # their COM calls, so the `add esp` after a COM call is routinely the
    # enclosing call's cleanup. Even reading only the instruction immediately
    # after, it reclassified SetSurfaceColorKey's SetColorKey and ClearRegion's
    # Blt; with a few instructions of slack it also took PresentFrame's BltFast,
    # its Restore, and BlitMapBackdrop's BltFast. Five real DirectDraw calls
    # would have left the COM count to make a total read 75/75 instead of 77/78.
    #
    # Deciding this properly needs the stack depth tracked across the call, not
    # a peephole. Until then 0x0041F060 stays "?" -- one honest unknown is worth
    # more than a complete-looking number that is quietly missing five calls.
    if obj_reg is None:
        return "?"
    for j in range(i - 1, max(-1, i - LOOKBACK) - 1, -1):
        insn = insns[j]
        if insn.mnemonic in ("call", "ret", "jmp"):
            break
        if insn.mnemonic == "push" and insn.operands:
            op = insn.operands[0]
            if op.type == OP_REG and insn.reg_name(op.reg) == obj_reg:
                return "stdcall"
        if insn.mnemonic == "mov" and len(insn.operands) == 2:
            dst, src = insn.operands
            # `mov ecx, <object>` is thiscall being set up, and it settles the
            # question even when something was pushed as well -- those pushes
            # are the method's own arguments. Whichever of the two appears
            # CLOSEST to the call wins, because that is the one establishing the
            # convention: COM pushes `this` last, thiscall loads ecx last.
            #
            # Without this every virtual method that takes an argument lands in
            # "?", and "?" was 56 sites -- 45 of them in script.cpp..unit.cpp,
            # i.e. the engine's own object model. Anything ranking targets by
            # COM density then floods its top with game logic.
            if (dst.type == OP_REG and insn.reg_name(dst.reg) == "ecx"
                    and src.type == OP_REG and insn.reg_name(src.reg) == obj_reg):
                return "thiscall"
            # Anything that redefines the object register ends the window: what
            # is pushed beyond this point is not the same pointer.
            if dst.type == OP_REG and insn.reg_name(dst.reg) == obj_reg:
                break
    return "thiscall" if obj_reg == "ecx" else "?"


def main():
    img = am2.Image()
    funcs = load_functions()
    addrs = [f[0] for f in funcs]

    insns = img.disasm(".text")
    print(f"{len(insns)} instructions decoded in .text")

    index = {insn.address: k for k, insn in enumerate(insns)}
    _n, text_start, text_end, _d = img.section(".text")

    rows = []
    for i, insn in enumerate(insns):
        if insn.mnemonic != "call" or not insn.operands:
            continue
        op = insn.operands[0]
        if op.type != OP_MEM:
            continue
        # Register-relative, no index -- [reg + disp].
        if op.mem.base == 0 or op.mem.index != 0:
            continue
        disp = op.mem.disp
        if disp < 0 or disp % 4 or disp > 0x400:
            continue
        reg = insn.reg_name(op.mem.base)
        found = find_vtable_load(insns, i, reg)
        if found is None:
            continue
        this_global, _at, obj_reg, field = found
        abi = call_abi(insns, i, obj_reg)
        own = owner(insn.address, funcs, addrs)
        fn, tu = own if own else (0, "?")
        rows.append((insn.address, fn, tu, disp // 4, disp, this_global, abi,
                     field))

    rows.sort()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("site\tfunc\ttu\tslot\tdisp\tthis\tabi\tfield\n")
        for addr, fn, tu, slot, disp, this, abi, field in rows:
            fh.write(f"0x{addr:08x}\t0x{fn:08x}\t{tu}\t{slot}\t"
                     f"0x{disp:02x}\t0x{this:08x}\t{abi}\t"
                     f"{'' if field is None else f'0x{field:x}'}\n")

    game = [r for r in rows if r[1] and r[1] < CRT_START]
    fns = {r[1] for r in game}
    print(f"\n{len(rows)} COM-shaped dispatch sites -> {os.path.relpath(OUT, REPO)}")
    print(f"{len(game)} in game code, across {len(fns)} functions")

    fields = collections.Counter(r[7] for r in game
                                 if r[7] is not None and r[6] == "stdcall")
    if fields:
        print("\n  interfaces held in struct fields (a subsystem per displacement):")
        for off, n in fields.most_common(8):
            print(f"    +0x{off:<5x} {n:3d} sites")

    by_abi = collections.Counter(r[6] for r in game)
    com = [r for r in game if r[6] == "stdcall"]
    print(f"  by how `this` is passed: "
          + ", ".join(f"{k} {v}" for k, v in sorted(by_abi.items())))
    print(f"  {len(com)} sites across {len({r[1] for r in com})} functions pass it"
          f" pushed, so those are the real COM boundary; the thiscall ones are"
          f" the engine's own C++ virtuals.")

    print("\nmost-dispatched vtable slots:")
    per_slot = collections.Counter(r[3] for r in game)
    for slot, n in per_slot.most_common(20):
        print(f"  slot {slot:>3} (+0x{slot * 4:02x})  {n:>4} sites")

    print("\ninterfaces taken from a global (top 20):")
    per_this = collections.Counter(r[5] for r in game if r[5])
    for this, n in per_this.most_common(20):
        slots = sorted({r[3] for r in game if r[5] == this})
        print(f"  0x{this:08x}  {n:>4} sites  slots {slots[:12]}")

    print("\nfunctions with the most COM dispatch:")
    per_fn = collections.Counter(r[1] for r in game)
    size_of = {a: s for a, s, _t in funcs}
    for fn, n in per_fn.most_common(25):
        tu = next(r[2] for r in game if r[1] == fn)
        slots = sorted({r[3] for r in game if r[1] == fn})
        print(f"  0x{fn:08x}  {size_of.get(fn, 0):>5}B  {n:>3} sites  [{tu}]  slots {slots[:10]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
