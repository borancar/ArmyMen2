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
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ESP)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x00400000
STACK, STACK_SZ = 0x10000000, 0x20000
SCRATCH, SCRATCH_SZ = 0x20000000, 0x4000
RET_MAGIC = 0x5EADBEE0
# Deterministic, and reproduced byte for byte by tests/selftest.cpp, so the
# vectors carry only offsets and never the buffer itself.
SCRATCH_PATTERN = bytes(((i * 7 + 13) & 0xFF) for i in range(0x4000))
MAX_ARGS = 6
CRT_FRONTIER = 0x00464420      # measured by tools/crt.py, not the old constant
DATA_LO, DATA_HI = 0x00473000, 0x00667000
RDATA_LO, RDATA_HI = 0x0046F000, 0x00473000
PTR_SYMBOLIC = 16          # bytes made symbolic behind each pointer argument
NVECTORS = 64
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


def analyse(img, md, addr, size):
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

        for mm in re.finditer(r"\[(%s)\b" % REG, op):
            reg = mm.group(1)
            if reg in slots:
                kinds[slots[reg]] = "ptr"

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

    def call(self, addr, args, scratch_bytes):
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
            self.uc.emu_start(addr, RET_MAGIC, timeout=500000, count=100000)
        except UcError:
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


def angr_inputs(addr, nargs, kinds, limit=24, timeout=90):
    """Argument sets that cover distinct paths, found by symbolic execution.

    Random inputs are weak at branch coverage: swapping min for max in
    ApproxDist -- a real defect -- was caught by only 13 of 512 random vectors,
    because it only shows when |dx| and |dy| straddle. angr solves for one
    input per path instead, which is what a branch actually needs.

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
                off = 0x40 * (i + 1)
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
        while sm.active and len(sm.found) < limit:
            if _time.time() > deadline:
                break
            sm.explore(find=lambda s: s.addr == RET, num_find=limit, n=16)
        out = []
        for f in sm.found:
            row, writes = [], []
            for i in range(nargs):
                if kinds.get(i) == "ptr":
                    off = 0x40 * (i + 1)
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
    """Instruction addresses in the function, minus trailing alignment padding.

    Linear decode: these are leaves with no data in the middle. int3 and nop
    runs at the end are the linker aligning the next function and are not
    reachable, so counting them would put 100% out of reach for every function.
    """
    ins = list(md.disasm(img.read(addr, size), addr))
    while ins and ins[-1].mnemonic in ("nop", "int3"):
        ins.pop()
    return {i.address for i in ins}


def vectors_for(emu, addr, nargs, kinds, seed=1234, n=NVECTORS, extra=()):
    """[(args, eax, scratch-writes)]. Interesting values first, then random."""
    rnd = random.Random(seed)
    EDGE = [0, 1, -1, 2, -2, 7, 255, 256, 0x7FFFFFFF, -0x80000000, 100, -100]
    out = []

    for args, pre in extra:                 # path-covering, from angr
        before = bytearray(SCRATCH_PATTERN)
        for o, b in pre:
            before[o] = b
        before = bytes(before)
        eax, after = emu.call(addr, args, before)
        if eax is None:
            continue
        writes = [(o, after[o]) for o in range(0, 0x900) if after[o] != before[o]]
        out.append((list(args), eax, writes, pre))

    for k in range(n):
        args = []
        for i in range(nargs):
            if kinds.get(i) == "ptr":
                # NULL belongs in the candidate set. Almost every accessor here
                # opens with `test eax,eax; jne; ret`, and passing only valid
                # pointers leaves that early return unreached -- which is
                # exactly the instruction a reconstruction is most likely to
                # forget.
                args.append(0 if k % 7 == 3
                            else SCRATCH + rnd.randrange(0, 0x400, 4))
            elif k < len(EDGE):
                args.append(EDGE[(k + i) % len(EDGE)])
            else:
                args.append(rnd.randint(-0x40000, 0x40000))
        scratch = SCRATCH_PATTERN
        eax, after = emu.call(addr, args, scratch)
        if eax is None:
            continue
        writes = [(off, after[off]) for off in range(0, 0x900)
                  if after[off] != scratch[off]]
        out.append((args, eax, writes, ()))
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
            if not loaded_ecx and re.search(r"\[ecx", op):
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
                "ADDR_COPY_BYTE_IF_SET"]

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
            nargs, kinds = analyse(img, md, a, size)
            paths = angr_inputs(a, nargs, kinds) if use_angr else []
            emu.seen = set()
            vs = vectors_for(emu, a, nargs, kinds, extra=paths)
            if not vs:
                nover += 1
                continue
            ok += 1
            body = body_addrs(img, md, a, size)
            hit = len(body & emu.seen)
            if body and hit == len(body):
                full += 1
            elif body:
                short.append((a, size, 100.0 * hit / len(body), len(body) - hit))
        print("  vectors generated for %d, none for %d" % (ok, nover))
        print("  of those with vectors, %d reach 100%% instruction coverage,"
              " %d fall short" % (full, len(short)))
        if short:
            print("\n  short of full coverage -- these need better inputs before"
                  "\n  a reconstruction of them can be called checked:\n")
            for a, size, pct, miss in sorted(short, key=lambda r: r[2])[:20]:
                print("    0x%08x %5dB  %5.1f%%  %d unreached" % (a, size, pct, miss))
        return 0

    todo = VALIDATE if (not want or want[0] == "--validate") else want

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
    }
    # Functions whose C prototype is void. The original still leaves something
    # in eax -- ObjSetFieldA's last instruction is `mov [eax+8],ecx`, so the
    # object pointer is still there -- and the emulator faithfully records it.
    # Comparing that would test the calling convention rather than the
    # function. The one caller ignores eax, so void is the right prototype and
    # the harness has to be told, since it cannot see a C declaration.
    VOID = {"ObjSetFieldA", "MsgSlotA0", "MsgSlotA1", "MsgSlotA2",
            "MsgSlotB0", "MsgSlotB1", "MsgSlotB2",
            "ObjFlagSet0", "ObjFlagClear0", "CopyByteIfSet"}
    out = []

    print("  %-24s %-12s %4s %-14s %5s %5s %6s"
          % ("name", "addr", "args", "kinds", "vecs", "paths", "cov"))
    for nm in todo:
        addr = names.get(nm) or int(nm, 16)
        size = sizes.get(addr, 0)
        if not size:
            # functions.tsv merges neighbours, so a real function can have no
            # entry of its own -- CompareDword at 0x0043E150 is one. merges.py
            # knows where the real boundaries are; ask it before giving up,
            # otherwise a reconstruction silently gets no vectors at all.
            import merges as _m
            _merged = _m.real_functions(img)
            for _e, (_starts, _sz) in _merged.items():
                if addr in _starts:
                    _fn, size = _m.owner(_starts, _sz, addr)
                    break
        if not size:
            print("  %-24s not in functions.tsv" % nm)
            continue
        nargs, kinds = analyse(img, md, addr, size)
        paths = angr_inputs(addr, nargs, kinds) if use_angr else []
        emu.seen = set()
        vs = vectors_for(emu, addr, nargs, kinds, extra=paths)
        body = body_addrs(img, md, addr, size)
        hit = len(body & emu.seen)
        cov = 100.0 * hit / len(body) if body else 0.0
        ks = ",".join(kinds.get(i, "-")[0] for i in range(nargs))
        print("  %-24s 0x%08x %4d %-14s %5d %5d  %5.1f%% %s"
              % (nm, addr, nargs, ks, len(vs), len(paths), cov,
                 "" if cov >= 99.99 else "<-- %d unreached" % (len(body) - hit)))
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
            fh.write("#define AM2_SCRATCH_LEN %d\n" % 0x900)
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
                     "} AM2_Vector;\n\n")
            for cname, nargs, kinds, vs in out:
                for k, (args, eax, writes, pre) in enumerate(vs):
                    if writes:
                        fh.write("static const uint32_t w_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, b) for o, b in writes)))
                    if pre:
                        fh.write("static const uint32_t i_%s_%d[] = {%s};\n"
                                 % (cname, k, ",".join("%d,%d" % (o, b) for o, b in pre)))
            fh.write("\nstatic const AM2_Vector kVectors[] = {\n")
            for cname, nargs, kinds, vs in out:
                for k, (args, eax, writes, pre) in enumerate(vs):
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
                    eaxp = 1 if SCRATCH <= eax < SCRATCH + SCRATCH_SZ else 0
                    eaxv = (eax - SCRATCH) if eaxp else eax
                    fh.write('    {"%s", (void *)%s, %d, {%s}, {%s}, 0x%08xu, '
                             '%d, %d, %d, %s, %d, %s},\n'
                             % (cname, cname, nargs,
                                ",".join(str(x) for x in p),
                                ",".join("0x%08xu" % (x & 0xFFFFFFFF) for x in a),
                                eaxv & 0xFFFFFFFF, eaxp,
                                1 if cname in VOID else 0, len(writes),
                                ("w_%s_%d" % (cname, k)) if writes else "0",
                                len(pre),
                                ("i_%s_%d" % (cname, k)) if pre else "0"))
            fh.write("};\n")
        print("\n-> tests/vectors.h  (%d functions, %d vectors)"
              % (len(out), sum(len(v[3]) for v in out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
