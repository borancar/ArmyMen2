"""Generate differential-test vectors for pure functions, from the original.

The project's verification has always been whole-game A/B: drive the original
and the reconstruction through the same script and compare logs and pixels.
That works for the boundary, because the boundary is observable -- get
InitDirectDraw wrong and the screen is wrong. It does not scale to the
simulation, where a subtly wrong distance or clipping function produces a
different but entirely plausible game, and where two live missions already
differ by ~22% of the frame for reasons that are nobody's fault.

A large part of what is left does not need the game at all. 315 of the 433
unreconstructed leaf functions read no global data: they are pure functions of
their arguments. For those the original binary IS the specification, and it can
be executed directly -- no Wine, no display, no mission.

This emulates the original function with Unicorn over the mapped PE image and
records (inputs -> output) vectors. tests/selftest.cpp then replays them against
the reconstruction. A mismatch is a defect, located at one function, with the
exact arguments that expose it.

Arguments are classified before they are generated, because passing a random
integer where the function expects a pointer only ever produces a fault:

  SCALAR   the slot is read and used as a value
  POINTER  the slot is read and then used as a memory base, so it gets a
           pointer into a scratch page seeded with random bytes. Whatever the
           function writes back there is part of the output and is compared.

    tools/vectors.py --validate     # against the 17 already-reconstructed ones
    tools/vectors.py ADDR_CLAMP     # one function, by name or hex address
    tools/vectors.py --all          # every pure leaf, into tests/vectors/
"""

import csv
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import capstone
import pefile
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EIP,
                               UC_X86_REG_ESP)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x00400000
STACK, STACK_SZ = 0x10000000, 0x20000
# Deliberately not a round number: 0x20000000 is the kind of value ordinary
# arithmetic lands on, and a computed result that collides with the scratch
# base gets mistaken for a pointer into it.
SCRATCH, SCRATCH_SZ = 0x51ED0000, 0x8000
# How much of the scratch a vector describes: the pointer arguments are spaced
# PTR_STRIDE apart and each gets PTR_SYMBOLIC symbolic bytes, so this has to
# cover the last one. tests/selftest.cpp sizes its buffer from the same number.
PTR_STRIDE = 0x800
SCRATCH_USED = PTR_STRIDE * (6 + 1)
RET_MAGIC = 0x5EADBEE0
# Deterministic, and reproduced byte for byte by tests/selftest.cpp, so the
# vectors carry only offsets and never the buffer itself.
# Salted per PTR_STRIDE region, and the salt is the whole point. (i*7+13)&0xFF
# has period 256, PTR_STRIDE is 0x800, and 0x800 is a multiple of 256 -- so
# every pointer argument's region held a BYTE-IDENTICAL sequence, and a
# function that copies src to dst changed nothing observable. RemapBytes'
# `count & 3` mutated to `count & 7` copies four extra bytes and passed at 100%
# instruction coverage for exactly that reason. `i >> 11` differs per region,
# so the regions no longer match. tests/selftest.cpp fills its own buffer and
# the two expressions have to stay identical.
SCRATCH_PATTERN = bytes(((((i * 7 + 13) ^ (i >> 11))) & 0xFF)
                        for i in range(0x8000))
MAX_ARGS = 6
CRT_FRONTIER = 0x00464420      # measured by tools/crt.py, not the old constant
DATA_LO, DATA_HI = 0x00473000, 0x00667000
RDATA_LO, RDATA_HI = 0x0046F000, 0x00473000
# Struct fields live at real offsets: 0x0040D860 reads +0x538, ObjType2Field548
# reads +0x548. With a 16-byte window angr could not make those symbolic, so
# the branches depending on them were unreachable however many paths it found
# -- 0x0040D860 sat at 80% with its `return 1` arm never taken. The window has
# to be the size of a struct, not the size of a header.
PTR_SYMBOLIC = 0x600       # bytes made symbolic behind each pointer argument
NVECTORS = 96
# Coverage alone is not enough to call a function checked. 0x00429F20 measured
# 100% on ONE surviving vector, because it walks a linked list and every
# generated pointer graph but one faulted. One vector cannot distinguish a
# reconstruction from a coincidence, so a function needs both.
MIN_VECTORS = 8
REG = r"e(?:ax|bx|cx|dx|si|di)"


def addr_names():
    out = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(os.path.join(REPO, "src", "inject", "orig.h")) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                out[m.group(1)] = int(m.group(2), 16)
    return out


# Argument kinds the classifier gets wrong, with the reason. It follows a slot
# through `mov` copies, which covers most functions, but not through
# ARITHMETIC: ReverseBlocks computes its source as `add edx, edi` and then
# dereferences edx, so slot 1 never looks like a pointer and the vectors feed
# it an integer. Rather than build dataflow analysis for one case, say so here.
ARG_KIND_OVERRIDE = {
    0x004231A0: {0: "ptr", 1: "ptr"},   # ReverseBlocks(dst, src, total, count)
    # BitmapBitSet(base, x, y, height, stride). The address it reads is built
    # from TWO arguments -- `base + (height - y - 1) * stride` indexed by
    # `x >> 3` -- and the classifier picked the one that is finally used as the
    # index, so the base was left a scalar and every vector faulted.
    0x004232C0: {0: "ptr", 1: "scalar"},
    # RemapBytes(dst, src, table, count). Two pushes before the first argument
    # read put the analyser two slots out, so it reports six arguments and
    # calls the count a pointer -- and a pointer-sized count goes into
    # `rep movsd` when the table is NULL. The last two are not arguments at
    # all; a cdecl callee simply ignores them.
    0x0041BB60: {3: "scalar"},
}

# Per-argument value sets, for functions whose scalars are a geometry rather
# than a number.
#
# `hints` above is a POOL: one set of interesting values offered to every
# scalar argument. That is right for a switch code and useless for a width and
# a stride, where the arguments are not interchangeable and the interesting
# thing about them is that they are SMALL. BitmapBitSet multiplies three of
# them together into an offset, so a random 32-bit stride puts the read
# megabytes outside the scratch page and the vector is discarded as a fault --
# 0 vectors, and a reconstruction that could not be checked at all.
#
# Keep every combination inside +/-PTR_STRIDE of the argument's pointer, since
# that is what the scratch page can absorb. The negative x values are not
# decoration: the sign-correcting `dec` / `or` / `inc` after `and 0x80000007`
# is 6 instructions that nothing else reaches.
#
# {function: {arg index: [values]}}
ARG_VALUES = {
    # RemapBytes(dst, src, table, count). The count is a byte at both call
    # sites (`and edi, 0xFF`) and goes into `rep movsd` unsigned when the
    # table is NULL, so a random 32-bit one copies 4GB and faults.
    # Thirteen values, not eleven, and every low-bit pattern present. The
    # table-NULL path is reached only on the k where argument 2 is nulled, and
    # with an 11-long list those k picked out indices {0,1,2,3,8,9,10} and
    # never 4..7 -- so the copy path only ever saw counts 0,1,2,3,16,32,64,
    # not one of which has bit 2 set. `count & 3` and `count & 7` are the same
    # function for all of those, and mutating one into the other passed at
    # 100% instruction coverage. A length coprime with that stride reaches all
    # thirteen.
    0x0041BB60: {3: [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 32, 64]},
    0x004232C0: {
        1: [0, 1, 2, 3, 7, 8, 15, 63, -1, -8, -9],   # x
        2: [0, 1, 2, 3, 7],                          # y
        3: [1, 2, 4, 8, 16],                         # height
        4: [1, 2, 4, 8],                             # stride
    },
}

# What a record has to contain before a function can be run at all. The
# generator fills scratch with a fixed pattern, which is fine for a leaf field
# and useless for two kinds of field it cannot guess:
#
#   "ptr"  a pointer to follow. A flat buffer satisfies one hop; a function
#          walking obj->[a]->[b] reads pattern bytes at the second, faults, and
#          yields no vectors at all.
#   "u32v" a field a branch turns on, varied around the given value so both
#          sides of the test are reached. A fixed seed makes the function
#          runnable and still leaves half of it unvisited: 0x0040D860 asks
#          whether a field is between 10 and 17, and the fill pattern never
#          put it there.
#   "u32"  a count or a length. 0x00402700 takes its loop count from the record
#          -- a pattern dword means tens of millions of iterations, the run hits
#          the instruction cap, and every vector is discarded.
#
# {function: [(arg, offset, kind, value), ...]}. `arg` is which pointer
# argument's region the offset is in, counting from 0; -1 means an absolute
# scratch offset, for seeding a record something else points AT. For "ptr" the
# value is the scratch offset to point to.
SEED = {
    0x0040D7E0: [(0, 0x74, "ptr", 0x900)],
    0x00402700: [(0, 0x04, "u32", 0x40)],
    0x004010B0: [(0, 0x04, "ptr", 0x900)],
    0x0040A490: [(0, 0x44, "ptr", 0x900), (-1, 0x906, "u32", 4)],
    0x00434E90: [(0, 0x04, "u32", 8), (0, 0x08, "ptr", 0x900)],
    0x0040D860: [(0, 0x538, "u32v", 13)],
    0x00408520: [(2, 0x2C, "u32v", 0x40), (2, 0x10, "u32v", 0)],
    0x0045C870: [(0, 0x08, "u32v", 1), (0, 0x0C, "u32v", 0)],
    0x00402E50: [(0, 0x39C, "u32v", 28)],
    0x0041DAD0: [(0, 0x04, "ptr", 0x900), (0, 0x08, "ptr", 0xA00)],
    0x00409650: [(0, 0x00, "u32v", 1), (0, 0x08, "u32v", 0),
                 (0, 0x94, "ptr", 0x900), (-1, 0x900, "u32v", 0x1F)],
    0x0043D550: [(0, 0x14, "u32v", 1), (0, 0x18, "u32v", 0)],
    0x00429F20: [(1, 0x00, "ptr", 0x900)],
}


def analyse(img, md, addr, size, reachable=None):
    """(argument count, {index: 'ptr'|'scalar'}) read out of the body.

    Both conventions put argument i at [esp + 4 + 4i] on entry, and at
    [ebp + 8 + 4i] once a frame is set up. The esp form is only usable if the
    stack pointer is tracked -- ApproxDist reads both its arguments and THEN
    pushes esi, so a later [esp + 4] is not argument 0 any more.

    An argument is a pointer as soon as a register holding it is used as a
    memory base. Capstone prints small displacements in decimal and large ones
    in hex, which is worth knowing: a regex written for 0x... alone silently
    matches nothing and every function comes out with no arguments at all.
    """
    slots = {}
    kinds = {}
    highest = -1
    delta = 0          # how far esp has moved since entry
    has_frame = False

    for ins in md.disasm(img.read(addr, size), addr):
        # Skip anything the CFG says is not code. A jump table sits inside the
        # function and decodes into plausible-looking instructions -- one of
        # 0x00406920's tables disassembles as `cmp ebp, [ecx + 0x40]`, which
        # made its switch selector look like a pointer being dereferenced.
        if reachable is not None and ins.address not in reachable:
            continue
        op = ins.op_str
        m = ins.mnemonic

        if m == "push":
            delta += 4
        elif m == "pop":
            delta -= 4
        elif m in ("sub", "add") and op.startswith("esp,"):
            try:
                n = int(op.split(",")[1].strip(), 0)
                delta += n if m == "sub" else -n
            except ValueError:
                pass
        if m == "mov" and op.replace(" ", "") == "ebp,esp":
            has_frame = True

        for mm in re.finditer(r"\[(esp|ebp) \+ (0x[0-9a-f]+|\d+)\]", op):
            base, off = mm.group(1), int(mm.group(2), 0)
            if base == "ebp":
                if not has_frame or off < 8:
                    continue
                idx = (off - 8) // 4
            else:
                rel = off - delta
                if rel < 4:
                    continue
                idx = (rel - 4) // 4
            if idx < 0 or idx >= MAX_ARGS or (off % 4):
                continue
            highest = max(highest, idx)
            kinds.setdefault(idx, "scalar")
            dst = op.split(",")[0].strip()
            if m in ("mov", "movsx", "movzx") and re.fullmatch(REG, dst):
                slots[dst] = idx

        # A copy carries the slot with it. RectSet loads its rectangle into eax
        # and immediately does `mov ebx, eax`, then writes through ebx -- miss
        # that and argument 0 looks like a scalar, every generated call writes
        # to a nonsense address, and the function yields no vectors at all.
        if m == "mov" and "," in op and "[" not in op:
            dst, src = (x.strip() for x in op.split(",", 1))
            if re.fullmatch(REG, dst):
                if src in slots:
                    slots[dst] = slots[src]
                else:
                    slots.pop(dst, None)

        # lea COMPUTES an address, it does not dereference one. Counting it
        # made ScaleBy32Blocks look like it took a pointer, because multiplying
        # by 1000 is three `lea eax,[eax+eax*4]` chains -- so the vectors
        # passed a scratch address where the function wanted a number, and the
        # reconstruction failed a test that was wrong.
        # The register must be the BASE. In `jmp [eax*4 + 0x43D524]` eax is a
        # scale-index into a jump table and the value is a switch selector, not
        # a pointer -- and classifying it as one concretises it, so angr cannot
        # explore the arms and every one of them stays unreached. Same family
        # as the lea case below it: an address being COMPUTED from a value does
        # not make that value a pointer.
        if m != "lea":
            for mm in re.finditer(r"\[(%s)(?!\s*\*)\b([^\]]*)\]" % REG, op):
                reg, rest = mm.group(1), mm.group(2)
                # A displacement that is itself an address in the image means
                # the register indexes a static table -- 0x00406920 dispatches
                # through `mov dl, [ecx + 0x406988]`, where ecx is a switch
                # selector and 0x406988 is the table. A pointer argument has a
                # small struct offset instead.
                disp = re.search(r"\+ (0x[0-9a-f]+)", rest)
                if disp and int(disp.group(1), 16) >= 0x00400000:
                    continue
                if reg in slots:
                    kinds[slots[reg]] = "ptr"

    kinds.update(ARG_KIND_OVERRIDE.get(addr, {}))
    return highest + 1, kinds


class Emu:
    def __init__(self):
        pe = pefile.PE(am2.EXE, fast_load=True)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        self.uc.mem_map(IMAGE_BASE, size)
        for s in pe.sections:
            self.uc.mem_write(IMAGE_BASE + s.VirtualAddress, s.get_data())
        self.uc.mem_map(STACK, STACK_SZ)
        self.uc.mem_map(SCRATCH, SCRATCH_SZ)
        # Which instructions a vector set actually reaches. "100% coverage" is
        # a claim that has to be measured; without this the tool can only say
        # how many vectors it made, which is not the same thing at all.
        self.seen = set()
        self.uc.hook_add(UC_HOOK_CODE, lambda uc, a, sz, _u: self.seen.add(a))

    def call(self, addr, args, scratch_bytes, count=100000):
        """One emulated call. `count` caps instructions -- see below.

        The cap is a runaway-loop guard, not a budget, and the default suits
        the pure leaves it was written for. Something that legitimately does
        real work needs more: the script tokeniser walks all 185 keywords for
        every word and re-runs strlen per character inside ParseNumber, so a
        200-character line of prose reaches 100,000 instructions honestly and
        is then discarded as a failure. Raise it rather than assume a fault.
        """
        self.uc.mem_write(SCRATCH, scratch_bytes)
        sp = STACK + STACK_SZ - 0x1000
        for a in reversed(args):
            sp -= 4
            self.uc.mem_write(sp, struct.pack("<I", a & 0xFFFFFFFF))
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", RET_MAGIC))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_EBP, sp)
        try:
            self.uc.emu_start(addr, RET_MAGIC, timeout=500000, count=count)
        except UcError:
            return None, None

        # emu_start's instruction cap and timeout STOP execution without
        # raising, so a run that never reached the return address still comes
        # back here with a plausible-looking eax. Recording that as the
        # expected answer is how ReverseBlocks acquired twelve vectors
        # demanding 0 from a function whose every exit returns 1: a count of
        # 0x7FFFFFFF makes total/count zero, and the loop spins two billion
        # times copying nothing. Only a run that actually returned counts.
        if self.uc.reg_read(UC_X86_REG_EIP) != RET_MAGIC:
            return None, None

        return (self.uc.reg_read(UC_X86_REG_EAX),
                bytes(self.uc.mem_read(SCRATCH, SCRATCH_SZ)))


_PROJECT = None


def _project(angr):
    """One Project for the whole run.

    It was being built inside angr_inputs, which runs once per function, so a
    sweep re-loaded and re-analysed the same 2.5 MB image for every one. The
    image does not change between functions and nothing about the Project is
    per-function.

    Measured rather than assumed: on the 19-function validation set this makes
    no visible difference, because those functions are tiny and angr finishes in
    milliseconds. It matters for a sweep, where the fixed cost is paid once
    instead of ninety-seven times.
    """
    global _PROJECT
    if _PROJECT is None:
        _PROJECT = angr.Project(am2.EXE, auto_load_libs=False,
                                main_opts={"base_addr": IMAGE_BASE})
    return _PROJECT


def angr_inputs(addr, nargs, kinds, limit=24, timeout=20):
    """Argument sets that cover distinct paths, found by symbolic execution.

    Random inputs are weak at branch coverage: swapping min for max in
    ApproxDist -- a real defect -- was caught by only 13 of 512 random vectors,
    because it only shows when |dx| and |dy| straddle. angr solves for one
    input per path instead, which is what a branch actually needs.

    The budget is per function and deliberately small. Most of these are a few
    dozen instructions and angr finishes in well under a second; the ones that
    exhaust it will not yield to four times as long, and the validation set is
    now sixty functions -- at ninety seconds each a full pass cannot finish.
    Raise it for a single function when that looks worth doing.

    angr supplies INPUTS ONLY. The expected output always comes from the
    Unicorn run, so there is one source of truth for what the original does and
    angr never gets a vote on it.
    """
    try:
        import logging
        for n in ("angr", "cle", "pyvex", "claripy"):
            logging.getLogger(n).setLevel("ERROR")
        import angr
        import claripy
    except Exception:
        return []

    RET = 0xDEADBEEF
    try:
        proj = _project(angr)
        syms = [claripy.BVS("a%d" % i, 32) for i in range(nargs)]
        conc = []
        mem = {}                     # scratch offset -> symbolic byte vector
        for i in range(nargs):
            if kinds.get(i) == "ptr":
                off = PTR_STRIDE * (i + 1)
                conc.append(SCRATCH + off)
                mem[off] = claripy.BVS("m%d" % i, PTR_SYMBOLIC * 8)
            else:
                conc.append(syms[i])
        st = proj.factory.call_state(addr, *conc, ret_addr=RET)

        # A pointer argument's interesting variation is in the memory it points
        # AT, not in the pointer. Leaving that concrete is why the first version
        # of this found 8 "paths" through ApproxDist that all had identical
        # inputs -- both its arguments are pointers, so there was nothing
        # symbolic left to solve for and every path agreed.
        for off, bv in mem.items():
            st.memory.store(SCRATCH + off, bv)

        # BOUNDED. The budget below used to be a parameter that the body
        # never read, so explore() ran unbounded and a single function with a
        # loop hung the whole sweep -- which is what a full pass over 97
        # functions did, silently, for twenty minutes. Symbolic execution needs
        # a deadline or it does not terminate; that is the normal case, not the
        # exception.
        import time as _time
        deadline = _time.time() + timeout
        sm = proj.factory.simulation_manager(st)
        # `sm.found` RAISES until the stash exists, which is only after the
        # first explore(). Reading it as the loop condition threw on iteration
        # zero, the outer handler swallowed it, and this returned [] every
        # time -- angr contributed nothing at all while appearing to. Always go
        # through the stashes dict.
        while sm.active and len(sm.stashes.get("found", [])) < limit:
            if _time.time() > deadline:
                break
            sm.explore(find=lambda s: s.addr == RET, num_find=limit, n=16)
        found = sm.stashes.get("found", [])
        out = []
        for f in found:
            row, writes = [], []
            for i in range(nargs):
                if kinds.get(i) == "ptr":
                    off = PTR_STRIDE * (i + 1)
                    row.append(SCRATCH + off)
                    raw = f.solver.eval(mem[off], cast_to=bytes)
                    writes += [(off + j, raw[j]) for j in range(len(raw))]
                else:
                    row.append(f.solver.eval(syms[i], cast_to=int))
            out.append((row, writes))
        return out
    except Exception:
        return []


def body_addrs(img, md, addr, size):
    """Instruction addresses in the function -- the coverage denominator.

    angr's CFG first, linear decode as a fallback. The difference is not
    cosmetic: a jump-table dispatch keeps its index table and its target table
    INSIDE the function, and linear decode turns that data into instructions
    nothing can ever reach. 0x00406920 decodes to 59 "instructions" of which
    27 are two tables, so its coverage could never pass 54% however good the
    inputs were. The CFG sees 32.

    Trailing nop/int3 padding is dropped either way: that is the linker
    aligning the next function, not code.
    """
    try:
        import angr
        proj = _project(angr)
        cfg = proj.analyses.CFGFast(regions=[(addr, addr + size)], normalize=True)

        # Blocks REACHABLE FROM THE ENTRY, not every block angr finds in the
        # range. Taking them all puts the alignment padding back in: the
        # logger at 0x0045CAA0 is one `ret` followed by fifteen nops, and
        # CFGFast happily makes blocks out of the nops.
        start = cfg.model.get_any_node(addr)
        if start is None:
            raise ValueError("no entry node")
        out, seen, stack = set(), set(), [start]
        while stack:
            node = stack.pop()
            if node.addr in seen or not (addr <= node.addr < addr + size):
                continue
            seen.add(node.addr)
            try:
                out.update(proj.factory.block(node.addr,
                                              size=node.size).instruction_addrs)
            except Exception:
                pass
            stack.extend(cfg.model.get_successors(node))
        if out:
            return out
    except Exception:
        pass

    ins = list(md.disasm(img.read(addr, size), addr))
    while ins and ins[-1].mnemonic in ("nop", "int3"):
        ins.pop()
    return {i.address for i in ins}



def split_writes(before, after):
    """(byte writes, pointer writes) -- a written POINTER cannot be compared
    byte for byte.

    ListPushFront stores node addresses into the list it is splicing, and those
    are addresses in the emulator's scratch. The replay has a buffer of its own,
    so every such write differed and the vectors failed with identical-looking
    values printed either side. A dword landing inside the scratch range is
    recorded as (where, what-it-points-at) and compared after rebasing.
    """
    changed = [o for o in range(0, SCRATCH_USED) if after[o] != before[o]]
    ptr, taken = [], set()
    for o in changed:
        base = o - (o % 4)
        if base in taken or base + 4 > SCRATCH_USED:
            continue
        v = struct.unpack_from("<I", after, base)[0]
        if SCRATCH <= v < SCRATCH + SCRATCH_SZ:
            ptr.append((base, v - SCRATCH))
            taken.update(range(base, base + 4))
    byte = [(o, after[o]) for o in changed if o not in taken]
    return byte, ptr

def range_hints(img, md, addr, size, reachable=None):
    """Input values a biased range check makes interesting.

    A switch over a run of codes compiles to `add reg, -N` (or `sub reg, N`)
    then `cmp reg, M` then `ja default`, so the arms are only reachable for
    inputs N..N+M. Generic values almost never land there -- 0x00406A40
    dispatches on 0x18..0x28 and reached a third of itself, and every table
    function in this binary has the same shape.

    Returns the endpoints and a spread through each range found, plus one value
    either side so the default arm is exercised too.
    """
    out = set()
    pend = None
    for ins in md.disasm(img.read(addr, size), addr):
        if reachable is not None and ins.address not in reachable:
            continue
        op = ins.op_str
        # `dec reg` is `sub reg, 1` and the compiler prefers it, so a pattern
        # written for add/sub alone misses every switch biased by one --
        # MapCode's `dec ecx / cmp ecx, 0x1D` among them.
        if ins.mnemonic in ("dec", "inc") and re.fullmatch(REG, op):
            pend = (op, 1 if ins.mnemonic == "dec" else -1)
            continue

        m = re.match(r"(e[a-d]x|e[sd]i), (-?0x[0-9a-f]+|-?\d+)$", op)
        if m and ins.mnemonic in ("add", "sub"):
            n = int(m.group(2), 0)
            pend = (m.group(1), -n if ins.mnemonic == "add" else n)
        elif m and ins.mnemonic == "cmp" and pend and m.group(1) == pend[0]:
            lo, span = pend[1], int(m.group(2), 0)
            if 0 < span < 0x400:
                out.add(lo - 1)
                out.add(lo + span + 1)
                # Every value for a small range. A spread misses arms: MapCode
                # dispatches 30 codes over 8 arms, and sampling every other one
                # left a third of them unvisited.
                step = 1 if span <= 96 else max(1, (span + 1) // 24)
                for j in range(0, span + 1, step):
                    out.add(lo + j)
                out.add(lo + span)
            pend = None
        elif ins.mnemonic not in ("xor", "movzx", "mov"):
            pend = None
    return sorted(out)


def vectors_for(emu, addr, nargs, kinds, seed=1234, n=NVECTORS, extra=(),
                hints=()):
    """[(args, eax, scratch-writes)]. Interesting values first, then random."""
    rnd = random.Random(seed)
    EDGE = [0, 1, -1, 2, -2, 7, 255, 256, 0x7FFFFFFF, -0x80000000, 100, -100]
    argvals = ARG_VALUES.get(addr, {})
    out = []

    for args, pre in extra:                 # path-covering, from angr
        before = bytearray(SCRATCH_PATTERN)
        for o, b in pre:
            before[o] = b
        before = bytes(before)
        eax, after = emu.call(addr, args, before)
        if eax is None:
            continue
        writes, wptr = split_writes(before, after)
        out.append((list(args), eax, writes, pre, (), tuple(wptr)))

    for k in range(n):
        args = []
        for i in range(nargs):
            if kinds.get(i) == "ptr":
                # NULL belongs in the candidate set. Almost every accessor here
                # opens with `test eax,eax; jne; ret`, and passing only valid
                # pointers leaves that early return unreached -- which is
                # exactly the instruction a reconstruction is most likely to
                # forget.
                # Per-ARGUMENT NULL decision, for the same reason the seeds
                # below take one per seed. `k % 7 == 3` is one decision shared
                # by every pointer argument, so they were all null together and
                # never null one at a time -- which meant a function comparing
                # two of them saw them EQUAL every time it saw a null at all.
                # RemapBytes falls back to a plain copy when its table is null
                # and returns early when dst == src, and with both nulled on
                # the same k the whole copy path was unreachable: 11
                # instructions, including both `rep movs`.
                args.append(0 if (k // (i + 1)) % 7 == 3
                            else SCRATCH + PTR_STRIDE * (i + 1))
            elif i in argvals:
                vals = argvals[i]
                args.append(vals[(k + i) % len(vals)])
            elif hints and k % 4 != 3:
                # Not `k % 2 == 0`: that is disjoint from the correlation cases
                # below, which fire on odd k, so a correlated vector could never
                # be built out of in-range values. TypesCompatible needs its two
                # arguments EQUAL and both inside 8..29, and never saw it.
                args.append(hints[(k // 2 + i) % len(hints)])
            elif k < len(EDGE):
                args.append(EDGE[(k + i) % len(EDGE)])
            else:
                args.append(rnd.randint(-0x40000, 0x40000))
        scratch = SCRATCH_PATTERN
        chain = SEED.get(addr)
        if chain:
            b = bytearray(scratch)
            for seed_i, (arg, at, kind, val) in enumerate(chain):
                base = PTR_STRIDE * (arg + 1) if arg >= 0 else 0
                at = base + at
                if kind == "ptr":
                    # NULL some of the time. A seeded pointer that is always
                    # valid leaves the "and if it is null" arm unreached, which
                    # is the arm a reconstruction most easily forgets --
                    # 0x004010B0 sat at 85.7% for exactly that one instruction.
                    # Per-seed NULL decision. Nulling every seeded pointer on
                    # the same k means a list node always had both its links
                    # present or both absent, so the unlink at 0x0041DAD0 never
                    # saw a head or a tail.
                    null_now = (k // (seed_i + 1)) % 5 == 2
                    struct.pack_into("<I", b, at,
                                     0 if null_now else SCRATCH + val)
                elif kind == "u32v":
                    # Each seed varies on its OWN period. Stepping them all by
                    # k made them move in lockstep, so a function gated on two
                    # of them only ever saw the combinations that k happened to
                    # line up -- 0x0045C870 has three arms selected by two
                    # fields and reached two thirds of itself.
                    # Co-prime periods. Stepping by k//(i+1) modulo 16 gave
                    # seeds periods of 16, 32, 48 ... which share factors, so
                    # two fields could stay out of phase forever: 0x00409650
                    # wanted one field to be 1 while a bit in another was
                    # clear, and in 96 vectors that never once happened.
                    # Half the vectors put EVERY seed on its nominal value at
                    # once. The nominal value is the one chosen to satisfy the
                    # check, so a function gated on three fields needs all
                    # three there simultaneously -- and with each varying on
                    # its own period that coincidence is rare enough that
                    # 0x00409650 never once reached its success return.
                    # The other half varies, on co-prime periods, to reach the
                    # failure arms.
                    if k % 2 == 0:
                        struct.pack_into("<I", b, at, val & 0xFFFFFFFF)
                    else:
                        period = (11, 13, 17, 19, 23, 29)[seed_i % 6]
                        step = (k // (seed_i + 1)) % period
                        struct.pack_into("<I", b, at,
                                         (val + step - period // 2) & 0xFFFFFFFF)
                else:
                    struct.pack_into("<I", b, at, val)
            # Vary the field a chain leads to, so the vectors exercise it
            # rather than reading one constant every time.
            struct.pack_into("<h", b, 0x900 + 0x4C, (k * 7) % 90 - 5)
            scratch = bytes(b)
        # Independent values almost never satisfy a relation BETWEEN arguments,
        # and a lot of these functions test exactly that -- FilterMatches asks
        # whether one argument equals another, or is a subset of it, and sat at
        # 52.9% because no two generated arguments were ever equal. Correlate
        # some of them: copy one slot to another, and make one a superset.
        if nargs >= 2:
            args = list(args)
            # Only ever copy between slots of the SAME kind. Copying a
            # pointer into a scalar slot put a scratch address where RectSet
            # wanted a coordinate, it got stored into the rectangle, and the
            # write comparison then read it back as a pointer and failed with
            # identical-looking values on both sides.
            same = lambda i, j: kinds.get(i, "scalar") == kinds.get(j, "scalar")
            if k % 4 == 1:
                for i in range(nargs // 2, nargs):
                    if same(i, i - nargs // 2):
                        args[i] = args[i - nargs // 2]
            elif k % 4 == 2 and nargs >= 3:
                if kinds.get(nargs - 1, "scalar") != "ptr":
                    args[nargs - 1] = -1
                if same(nargs - 2, 0):
                    args[nargs - 2] = args[0]
            elif k % 8 == 3 and nargs >= 3:
                # All bits set is the natural superset, which is what a subset
                # test needs on its other side.
                if kinds.get(nargs - 1, "scalar") != "ptr":
                    args[nargs - 1] = -1
                if kinds.get(nargs - 2, "scalar") != "ptr":
                    args[nargs - 2] = -1

            # Neutralise the FIRST argument, INDEPENDENTLY of the correlations
            # above. A function that checks its arguments in order early-outs
            # on the first, so everything after it is unreachable until that one
            # passes -- the second half of FilterMatches was unvisited for
            # exactly this reason. -1 and 0 are the two wildcards this binary
            # uses. (Written as its own `if`: as an `elif` it never fired,
            # because every k it selects is already claimed above.)
            if kinds.get(0, "scalar") != "ptr":
                if k % 8 == 5:
                    args[0] = 0xFFFFFFFF
                elif k % 8 == 7:
                    args[0] = 0

        eax, after = emu.call(addr, args, scratch)
        if eax is None:
            continue
        writes, wptr = split_writes(scratch, after)
        pre_in = ()
        fx = ()
        if chain:
            # The leaf field travels as bytes; the POINTER cannot, because its
            # value is an address in the emulator's scratch and the replay has
            # a buffer of its own. It travels as (where, what-it-points-at) and
            # is rebased on the other side.
            pre_in = tuple((0x900 + 0x4C + i, scratch[0x900 + 0x4C + i])
                           for i in range(2))
            def _at(arg, off):
                return (PTR_STRIDE * (arg + 1) if arg >= 0 else 0) + off
            pre_in += tuple((_at(arg, at) + i, scratch[_at(arg, at) + i])
                            for arg, at, kind, _v in chain if kind != "ptr"
                            for i in range(4))
            fx = tuple((_at(arg, at), val)
                       for si, (arg, at, kind, val) in enumerate(chain)
                       if kind == "ptr" and (k // (si + 1)) % 5 != 2)
            # A NULLed seed travels as plain bytes, since there is no address
            # to rebase.
            pre_in += tuple((_at(arg, at) + i, 0)
                            for si, (arg, at, kind, _v) in enumerate(chain)
                            if kind == "ptr" and (k // (si + 1)) % 5 == 2
                            for i in range(4))
        out.append((args, eax, writes, pre_in, fx, tuple(wptr)))
    return out


def pure_leaves(img, md, sizes):
    """Unreconstructed functions that call nothing and read no global.

    The global test must cover the WHOLE data range. A first version matched
    only addresses beginning 0x4, which silently skipped everything at
    0x5xxxxx and 0x6xxxxx -- and .data runs to 0x667000. That reported 315 pure
    functions where there are 161, because NextItem reading [0x514F08] looked
    like a pure function of its arguments.
    """
    import merges
    merged = merges.real_functions(img)
    done = merges.reconstructed()
    real = {}
    for a, s in sizes.items():
        if a in merged:
            st, m = merged[a]
            for i, x in enumerate(st):
                real[x] = (st[i + 1] if i + 1 < len(st) else a + m) - x
        else:
            real[a] = s
    game = {a: s for a, s in real.items() if a < CRT_FRONTIER}

    def isdone(a, s):
        return any(a <= d < a + max(s, 1) for d in done)

    def owner(t):
        return next((f for f in game if f <= t < f + game[f]), None)

    pat = re.compile(r"0x[0-9a-f]{6,8}")
    out = []
    for a, s in sorted(game.items()):
        if isdone(a, s):
            continue
        body = list(md.disasm(img.read(a, s), a))
        if not body:
            continue

        # ANY call disqualifies, not merely a call to something unreconstructed.
        # 0x00434C80 is "free it if non-null" and would really free memory;
        # 0x00427420 is PollInput, `call PollMouse; jmp PollKeyboard`, which is
        # nothing but side effects and looked like a leaf because both callees
        # are already ours.
        if any(i.mnemonic == "call" for i in body):
            continue
        if any(i.mnemonic == "jmp" and i.op_str.startswith("0x")
               and 0x401000 <= int(i.op_str, 16) < CRT_FRONTIER
               and not (a <= int(i.op_str, 16) < a + s) for i in body):
            continue

        # ecx used as a base without ever being loaded is the implicit `this`
        # of a thiscall method -- 0x00432D40 writes a byte to [ecx+0x70]. There
        # is no way to supply that from the stack, so it is not testable here.
        loaded_ecx = False
        thiscall = False
        for i in body:
            op = i.op_str
            # ecx ANYWHERE inside a memory operand, not just at its start.
            # 0x0040F600 indexes `[edx + ecx + 0x20C]`, so a pattern anchored
            # to "[ecx" missed it and four thiscall methods were classified
            # pure. The emulator then ran them with a garbage ecx, nearly every
            # vector faulted, and the one or two survivors still measured 100%
            # coverage -- a reconstruction could have passed on two vectors.
            if not loaded_ecx and re.search(r"\[[^\]]*\becx\b[^\]]*\]", op):
                thiscall = True
            # ecx read as a VALUE before it is written is the implicit `this`
            # just as much as ecx used as a base. 0x00453910 opens with
            # `mov eax, ecx` and writes through eax, so nothing ever indexes
            # ecx and the pattern above saw a pure function.
            if not loaded_ecx and re.search(r",\s*ecx\b", op):
                thiscall = True
            if re.match(r"ecx\s*,", op) and i.mnemonic in ("mov", "lea", "xor",
                                                           "pop", "movsx", "movzx"):
                loaded_ecx = True
        if thiscall:
            continue

        glob = any(DATA_LO <= int(t, 16) < DATA_HI or
                   RDATA_LO <= int(t, 16) < RDATA_HI
                   for i in body for t in pat.findall(i.op_str))
        if not glob:
            out.append((a, s))
    return out


def main():
    img = am2.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")), delimiter="\t")}
    names = addr_names()
    emu = Emu()

    VALIDATE = ["ADDR_CLAMP", "ADDR_APPROX_DIST", "ADDR_POINT_IN_RECT",
                "ADDR_RECT_SET", "ADDR_PACK_KEY", "ADDR_KEY_FIELD_A",
                "ADDR_KEY_FIELD_B", "ADDR_KEY_FIELD_C", "ADDR_OBJ_IS_ITEM",
                "ADDR_OBJ_IS_TYPE2", "ADDR_OBJ_IS_TYPE3", "ADDR_OBJ_IS_TYPE238",
                "ADDR_CLIP_RECT", "ADDR_FIND_SLOT", "ADDR_FIRST_ITEM",
                "ADDR_NEXT_ITEM", "ADDR_SET_DRAW_TARGET",
                "ADDR_UID_ARMY", "ADDR_UID_ON_WIRE",
                "ADDR_OBJ_FIELD_A", "ADDR_OBJ_SET_FIELD_A", "ADDR_OBJ_FIELD_B",
                "ADDR_APPROX_DIST_XY", "ADDR_ANGLE_DELTA", "ADDR_ROUND_TO_8",
                "ADDR_MAKE_POINT",
                "ADDR_MSGSLOT_A0", "ADDR_MSGSLOT_A1", "ADDR_MSGSLOT_A2",
                "ADDR_MSGSLOT_B0", "ADDR_MSGSLOT_B1", "ADDR_MSGSLOT_B2",
                "ADDR_MSG_FIELD_12", "ADDR_COMM_MEAN_32",
                "ADDR_OBJ_FLAG_SET0", "ADDR_OBJ_FLAG_CLEAR0",
                "ADDR_OBJ_FLAG_BIT0", "ADDR_OBJ_FLAG_BIT1",
                "ADDR_OBJ_IS_TYPE8", "ADDR_OBJ_IS_TYPE4",
                "ADDR_FIELD_53C", "ADDR_ADD_BYTE_SAT", "ADDR_COMPARE_DWORD",
                "ADDR_COPY_BYTE_IF_SET", "ADDR_SCALE_32_BLOCKS",
                "ADDR_TITLE_CASE", "ADDR_RESET_PAIR_MASK", "ADDR_IS_KIND_7",
                "ADDR_IS_BLANK", "ADDR_IS_SCRIPT_DELIM",
                "ADDR_SWAP_COLOUR_BYTES", "ADDR_RETURN_ZERO", "ADDR_RETURN_ONE",
                "ADDR_REVERSE_BLOCKS", "ADDR_SCRIPT_COMPARE",
                "ADDR_OBJ_IS_TYPE8", "ADDR_OBJ_IS_TYPE4",
                "ADDR_COMPARE_PAIR", "ADDR_MAP_CODE",
                "ADDR_POINTS_EQUAL", "ADDR_POINTS_DIFFER",
                "ADDR_COMPARE_TRIPLE", "ADDR_TYPES_COMPATIBLE",
                "ADDR_SET_FACING_14", "ADDR_SET_FACING_08",
                "ADDR_IS_KIND_10_17", "ADDR_IS_KIND_14_22",
                "ADDR_OBJ_TYPE2_FIELD548", "ADDR_CLASSIFY_CODE74",
                "ADDR_KIND_IN_SET_A", "ADDR_KIND_IN_SET_B",
                "ADDR_MASK_PIXEL_SOLID", "ADDR_XOR_CHECKSUM",
                "ADDR_CHAIN_FIELD_14", "ADDR_LIST_PUSH_FRONT",
                "ADDR_SET_FIELD_IN_ALL", "ADDR_FIELD51_MEETS_MIN",
                "ADDR_OBJ_KIND538_10_17", "ADDR_FILTER_MATCHES",
                "ADDR_CONSUME_PENDING", "ADDR_FACING_DELTA_08",
                "ADDR_FACING_DELTA_14", "ADDR_MAP_CODE_18_28",
                "ADDR_MEETS_ALL_THREE",
                "ADDR_BITMAP_BIT_SET", "ADDR_RING_PUSH_32",
                "ADDR_LIST_UNLINK", "ADDR_REMAP_BYTES"]

    want = sys.argv[1:] or ["--validate"]
    emit = "--emit" in want
    use_angr = "--angr" in want
    want = [w for w in want if w not in ("--emit", "--angr")]
    if want and want[0] == "--all":
        leaves = pure_leaves(img, md, sizes)
        print("  %d pure unreconstructed leaves\n" % len(leaves))
        ok = nover = full = 0
        short = []
        for a, size in leaves:
            body = body_addrs(img, md, a, size)
            nargs, kinds = analyse(img, md, a, size, body)
            paths = angr_inputs(a, nargs, kinds) if use_angr else []
            emu.seen = set()
            vs = vectors_for(emu, a, nargs, kinds, extra=paths,
                             hints=range_hints(img, md, a, size, body))
            if not vs:
                nover += 1
                continue
            ok += 1
            hit = len(body & emu.seen)
            if body and hit == len(body) and len(vs) >= MIN_VECTORS:
                full += 1
            elif body and hit == len(body):
                short.append((a, size, 100.0, len(vs)))
            elif body:
                short.append((a, size, 100.0 * hit / len(body), len(body) - hit))
        print("  vectors generated for %d, none for %d" % (ok, nover))
        print("  of those with vectors, %d reach 100%% instruction coverage,"
              " %d fall short" % (full, len(short)))
        if short:
            print("\n  short of full coverage -- these need better inputs before"
                  "\n  a reconstruction of them can be called checked:\n")
            for a, size, pct, miss in sorted(short, key=lambda r: r[2])[:24]:
                what = ("only %d vectors" % miss) if pct >= 99.99 \
                       else ("%d unreached" % miss)
                print("    0x%08x %5dB  %5.1f%%  %s" % (a, size, pct, what))
        return 0

    # The list is edited by hand as functions are ported, so it acquires
    # duplicates; two of the same name emit two identically-named vector
    # arrays and the test binary fails to link.
    todo = VALIDATE if (not want or want[0] == "--validate") else want
    todo = list(dict.fromkeys(todo))

    # Only the truly pure ones can be replayed outside the game: a function
    # that reads a global needs that global mapped, and the point of this is to
    # test without the game running. PURE maps each to its C++ name.
    PURE = {
        "ADDR_CLAMP": "Clamp", "ADDR_APPROX_DIST": "ApproxDist",
        "ADDR_POINT_IN_RECT": "PointInRect", "ADDR_RECT_SET": "RectSet",
        "ADDR_PACK_KEY": "PackKey", "ADDR_KEY_FIELD_A": "KeyFieldA",
        "ADDR_KEY_FIELD_B": "KeyFieldB", "ADDR_KEY_FIELD_C": "KeyFieldC",
        "ADDR_UID_ARMY": "UidArmy", "ADDR_UID_ON_WIRE": "UidOnWire",
        "ADDR_OBJ_FIELD_A": "ObjFieldA", "ADDR_OBJ_SET_FIELD_A": "ObjSetFieldA",
        "ADDR_OBJ_FIELD_B": "ObjFieldB",
        "ADDR_APPROX_DIST_XY": "ApproxDistXY", "ADDR_ANGLE_DELTA": "AngleDelta",
        "ADDR_ROUND_TO_8": "RoundTo8", "ADDR_MAKE_POINT": "MakePoint",
        "ADDR_MSGSLOT_A0": "MsgSlotA0", "ADDR_MSGSLOT_A1": "MsgSlotA1",
        "ADDR_MSGSLOT_A2": "MsgSlotA2", "ADDR_MSGSLOT_B0": "MsgSlotB0",
        "ADDR_MSGSLOT_B1": "MsgSlotB1", "ADDR_MSGSLOT_B2": "MsgSlotB2",
        "ADDR_MSG_FIELD_12": "MsgField12", "ADDR_COMM_MEAN_32": "CommMean32",
        "ADDR_OBJ_FLAG_SET0": "ObjFlagSet0",
        "ADDR_OBJ_FLAG_CLEAR0": "ObjFlagClear0",
        "ADDR_OBJ_FLAG_BIT0": "ObjFlagBit0", "ADDR_OBJ_FLAG_BIT1": "ObjFlagBit1",
        "ADDR_OBJ_IS_TYPE8": "ObjIsType8", "ADDR_OBJ_IS_TYPE4": "ObjIsType4",
        "ADDR_FIELD_53C": "Field53C", "ADDR_ADD_BYTE_SAT": "AddByteSat",
        "ADDR_COMPARE_DWORD": "CompareDword",
        "ADDR_COPY_BYTE_IF_SET": "CopyByteIfSet",
        "ADDR_SCALE_32_BLOCKS": "ScaleBy32Blocks",
        "ADDR_TITLE_CASE": "TitleCaseName",
        "ADDR_RESET_PAIR_MASK": "ResetPairMask", "ADDR_IS_KIND_7": "IsKind7",
        "ADDR_IS_BLANK": "IsBlank", "ADDR_IS_SCRIPT_DELIM": "IsScriptDelim",
        "ADDR_SWAP_COLOUR_BYTES": "SwapColourBytes",
        "ADDR_RETURN_ZERO": "ReturnZero", "ADDR_RETURN_ONE": "ReturnOne",
        "ADDR_REVERSE_BLOCKS": "ReverseBlocks",
        "ADDR_SCRIPT_COMPARE": "ScriptCompare",
        "ADDR_COMPARE_PAIR": "ComparePair", "ADDR_MAP_CODE": "MapCode",
        "ADDR_POINTS_EQUAL": "PointsEqual", "ADDR_POINTS_DIFFER": "PointsDiffer",
        "ADDR_COMPARE_TRIPLE": "CompareTriple",
        "ADDR_TYPES_COMPATIBLE": "TypesCompatible",
        "ADDR_SET_FACING_14": "SetFacing14", "ADDR_SET_FACING_08": "SetFacing08",
        "ADDR_IS_KIND_10_17": "IsKind10To17", "ADDR_IS_KIND_14_22": "IsKind14Or22",
        "ADDR_OBJ_TYPE2_FIELD548": "ObjType2Field548",
        "ADDR_CLASSIFY_CODE74": "ClassifyByCode74",
        "ADDR_KIND_IN_SET_A": "KindInSetA", "ADDR_KIND_IN_SET_B": "KindInSetB",
        "ADDR_MASK_PIXEL_SOLID": "MaskPixelSolid",
        "ADDR_XOR_CHECKSUM": "XorChecksum", "ADDR_CHAIN_FIELD_14": "ChainField14",
        "ADDR_LIST_PUSH_FRONT": "ListPushFront",
        "ADDR_SET_FIELD_IN_ALL": "SetFieldInAll",
        "ADDR_FIELD51_MEETS_MIN": "Field51MeetsMin",
        "ADDR_OBJ_KIND538_10_17": "ObjKind538In10To17",
        "ADDR_FILTER_MATCHES": "FilterMatches",
        "ADDR_CONSUME_PENDING": "ConsumePendingByte",
        "ADDR_FACING_DELTA_08": "FacingFromDelta08",
        "ADDR_FACING_DELTA_14": "FacingFromDelta14",
        "ADDR_MAP_CODE_18_28": "MapCode18To28",
        "ADDR_MEETS_ALL_THREE": "MeetsAllThree",
        "ADDR_BITMAP_BIT_SET": "BitmapBitSet",
        "ADDR_RING_PUSH_32": "RingPush32",
        "ADDR_LIST_UNLINK": "ListUnlink",
        "ADDR_REMAP_BYTES": "RemapBytes",
    }
    # Functions whose C prototype is void. The original still leaves something
    # in eax -- ObjSetFieldA's last instruction is `mov [eax+8],ecx`, so the
    # object pointer is still there -- and the emulator faithfully records it.
    # Comparing that would test the calling convention rather than the
    # function. The one caller ignores eax, so void is the right prototype and
    # the harness has to be told, since it cannot see a C declaration.
    VOID = {"ObjSetFieldA", "MsgSlotA0", "MsgSlotA1", "MsgSlotA2",
            "MsgSlotB0", "MsgSlotB1", "MsgSlotB2",
            "ObjFlagSet0", "ObjFlagClear0", "CopyByteIfSet",
            "TitleCaseName", "ResetPairMask", "SetFacing14", "SetFacing08",
            "ListPushFront", "ConsumePendingByte",
            # RingPush32 loads the object into eax at entry and never writes
            # eax again, so the pointer is still there at `ret`. Both call
            # sites -- 0x0040168B and 0x00401C53 -- overwrite eax with their
            # next load immediately after the call, so nothing reads it.
            "RingPush32",
            # ListUnlink keeps the node in eax from entry to `ret` too.
            "ListUnlink",
            # RemapBytes leaves dst, or dst+count, in eax; both callers
            # discard it and advance their own pointer instead.
            "RemapBytes"}
    out = []

    print("  %-24s %-12s %4s %-14s %5s %5s %6s"
          % ("name", "addr", "args", "kinds", "vecs", "paths", "cov"))
    for nm in todo:
        addr = names.get(nm) or int(nm, 16)
        size = sizes.get(addr, 0)
        # functions.tsv merges neighbours, so its size can cover several real
        # functions. ScriptCompare's entry says 176 bytes where the function is
        # 80, which put the NEXT function's instructions in the coverage
        # denominator and its dereferences in the argument classifier. Always
        # ask merges.py, not only when there is no entry at all.
        import merges as _m
        _merged = _m.real_functions(img)
        for _e, (_starts, _sz) in _merged.items():
            if addr in _starts:
                _fn, size = _m.owner(_starts, _sz, addr)
                break
        if not size:
            print("  %-24s not in functions.tsv" % nm)
            continue
        body = body_addrs(img, md, addr, size)
        nargs, kinds = analyse(img, md, addr, size, body)
        paths = angr_inputs(addr, nargs, kinds) if use_angr else []
        emu.seen = set()
        vs = vectors_for(emu, addr, nargs, kinds, extra=paths,
                         hints=range_hints(img, md, addr, size, body))
        hit = len(body & emu.seen)
        cov = 100.0 * hit / len(body) if body else 0.0
        ks = ",".join(kinds.get(i, "-")[0] for i in range(nargs))
        note = ""
        if cov < 99.99:
            note = "<-- %d unreached" % (len(body) - hit)
        elif len(vs) < MIN_VECTORS:
            note = "<-- only %d vectors: too thin to check against" % len(vs)
        print("  %-24s 0x%08x %4d %-14s %5d %5d  %5.1f%% %s"
              % (nm, addr, nargs, ks, len(vs), len(paths), cov, note))
        if emit and nm in PURE:
            out.append((PURE[nm], nargs, kinds, vs))

    if emit:
        path = os.path.join(REPO, "tests", "vectors.h")
        with open(path, "w") as fh:
            fh.write("/* GENERATED by tools/vectors.py -- do not edit.\n"
                     " *\n"
                     " * Recorded by emulating the ORIGINAL function over the\n"
                     " * mapped PE image, so the binary is the specification and\n"
                     " * no part of the game has to run to check a\n"
                     " * reconstruction against it. */\n")
            fh.write("#include <stdint.h>\n\n")
            # The replay's buffer has to be as big as the emulator's mapped
            # scratch, not merely as big as the region a vector describes. A
            # record can hold offsets that reach anywhere: MaskPixelSolid's row
            # table sent it to +0x5637, past the described region, where the
            # emulator read pattern bytes and the replay read off the end of
            # its array. Same fill, same size, or the two sides are not running
            # the same test.
            fh.write("#define AM2_SCRATCH_LEN %d   /* whole mapped scratch */\n"
                     % SCRATCH_SZ)
            fh.write("#define AM2_SCRATCH_CMP %d   /* the part vectors describe */\n"
                     % SCRATCH_USED)
            fh.write("typedef struct {\n"
                     "    const char *name;\n"
                     "    void       *fn;\n"
                     "    int32_t     nargs;\n"
                     "    uint8_t     isptr[6];\n"
                     "    uint32_t    arg[6];\n"
                     "    uint32_t    eax;\n"
                     "    uint8_t     eax_is_ptr;  /* eax is scratch+eax, not a literal */\n"
                     "    uint8_t     void_ret;     /* prototype is void: do not compare eax */\n"
                     "    int32_t     nwrites;\n"
                     "    const uint32_t *writes;   /* offset, byte pairs */\n"
                     "    int32_t     ninputs;\n"
                     "    const uint32_t *inputs;   /* offset, byte pairs, written first */\n"
                     "    int32_t     nfixups;\n"
                     "    const uint32_t *fixups;   /* offset, scratch-offset: written first */\n"
                     "    int32_t     nwptr;\n"
                     "    const uint32_t *wptr;     /* offset, scratch-offset: expected write */\n"
                     "} AM2_Vector;\n\n")
            for cname, nargs, kinds, vs in out:
                for k, (args, eax, writes, pre, fx, wp) in enumerate(vs):
                    if writes:
                        fh.write("static const uint32_t w_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, b) for o, b in writes)))
                    if pre:
                        fh.write("static const uint32_t i_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, b) for o, b in pre)))
                    if fx:
                        fh.write("static const uint32_t f_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, t) for o, t in fx)))
                    if wp:
                        fh.write("static const uint32_t p_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, t) for o, t in wp)))
            fh.write("\nstatic const AM2_Vector kVectors[] = {\n")
            for cname, nargs, kinds, vs in out:
                for k, (args, eax, writes, pre, fx, wp) in enumerate(vs):
                    # A NULL pointer argument is a literal, not an offset. It
                    # was being emitted as 0 - SCRATCH, i.e. 0xE0000000, which
                    # the replay then rebased onto its own buffer and wrote to.
                    # The fault only appears for functions that TOLERATE null:
                    # everything before these dereferenced unconditionally, so
                    # the emulator faulted and those vectors were dropped
                    # before they could be emitted.
                    a, p = [], []
                    for i, x in enumerate(args):
                        isptr = kinds.get(i) == "ptr" and x != 0
                        a.append((x - SCRATCH) if isptr else x)
                        p.append(1 if isptr else 0)
                    a += [0] * (6 - len(a))
                    p += [0] * (6 - len(p))
                    # A function that returns one of its pointer arguments --
                    # RectSet hands the rectangle back -- records an address
                    # from the emulator's scratch page, which the replay's own
                    # buffer will never match. Store the offset and a flag.
                    # Only a function that was GIVEN a scratch pointer can
                    # return one. ApproxDistXY(0xA0000000, 0x80000000) computes
                    # 0x20000000, which happened to equal SCRATCH, so it was
                    # recorded as "returns scratch + 0" and the replay compared
                    # against its own buffer address. Requiring a pointer
                    # argument makes the coincidence harmless.
                    has_ptr = any(kinds.get(i) == "ptr" for i in range(nargs))
                    eaxp = 1 if (has_ptr and SCRATCH <= eax < SCRATCH + SCRATCH_SZ) else 0
                    eaxv = (eax - SCRATCH) if eaxp else eax
                    fh.write('    {"%s", (void *)%s, %d, {%s}, {%s}, 0x%08xu, '
                             '%d, %d, %d, %s, %d, %s, %d, %s, %d, %s},\n'
                             % (cname, cname, nargs,
                                ",".join(str(x) for x in p),
                                ",".join("0x%08xu" % (x & 0xFFFFFFFF) for x in a),
                                eaxv & 0xFFFFFFFF, eaxp,
                                1 if cname in VOID else 0, len(writes),
                                ("w_%s_%d" % (cname, k)) if writes else "0",
                                len(pre),
                                ("i_%s_%d" % (cname, k)) if pre else "0",
                                len(fx),
                                ("f_%s_%d" % (cname, k)) if fx else "0",
                                len(wp),
                                ("p_%s_%d" % (cname, k)) if wp else "0"))
            fh.write("};\n")
        print("\n-> tests/vectors.h  (%d functions, %d vectors)"
              % (len(out), sum(len(v[3]) for v in out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
