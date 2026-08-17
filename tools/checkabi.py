"""Audit the calling convention of every reconstructed function.

Getting a convention wrong is not a compile error and usually is not an
immediate crash either -- it corrupts the caller's stack and faults somewhere
else entirely, which is how the BlitCopy16 bug presented. So the declared
convention is checked against what the original actually does.

The evidence in the machine code:

  ecx read before written, edx not   ->  thiscall (`this` in ecx) or one-arg
                                          fastcall; distinguished by `ret N`
  ecx and edx both read before written -> fastcall
  neither read                        -> cdecl or stdcall
  `ret N`                             -> callee cleans: stdcall/thiscall/fastcall
  plain `ret`                         -> caller cleans: cdecl

Run against the table below, which must be kept in step with the harness:

    ./.venv/bin/python tools/checkabi.py
"""

import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

# (address, name, declared convention) -- mirrors src/inject/orig.h.
FUNCS = [
    (0x004235D0, "CheckSaveTag",       "cdecl"),
    (0x0042E1C0, "RectSet",            "cdecl"),
    (0x0042E180, "Clamp",              "cdecl"),
    (0x0042E1F0, "PointInRect",        "cdecl"),
    (0x0042E220, "ClipRect",           "cdecl"),
    (0x0042DDE0, "ApproxDist",         "cdecl"),
    (0x004277A0, "FindSlot",           "cdecl"),
    (0x00427820, "LookupByUID",        "cdecl"),
    (0x00429740, "AddToItemList",      "cdecl"),
    (0x00428590, "RemoveFromItemList", "cdecl"),
    (0x00427850, "FirstItem",          "cdecl"),
    (0x00427880, "NextItem",           "cdecl"),
    (0x00433860, "ObjIsItem",          "cdecl"),
    (0x00457470, "ObjIsType2",         "cdecl"),
    (0x00457490, "ObjIsType3",         "cdecl"),
    (0x00457420, "ObjIsTypeIn238",     "cdecl"),
    (0x00433810, "PackKey",            "cdecl"),
    (0x00433830, "KeyFieldA",          "cdecl"),
    (0x00433840, "KeyFieldB",          "cdecl"),
    (0x00433850, "KeyFieldC",          "cdecl"),
    (0x00446930, "DrawText",           "cdecl"),
    (0x00445FF0, "DrawSprite",         "cdecl"),
    (0x00446070, "DrawSpriteClipped",  "cdecl"),
    (0x0041B9A0, "LockSurface",        "cdecl"),
    (0x0041BA40, "UnlockSurface",      "cdecl"),
    (0x004464C0, "EncodeGlyph",        "cdecl"),
    (0x004465E0, "RenderGlyph",        "cdecl"),
    (0x0041AC40, "SetDrawTarget",      "cdecl"),
    (0x0041CF90, "RedrawMapRegion",    "cdecl"),
    (0x0041AFC0, "CalibratePalette",   "cdecl"),
    (0x0040B360, "WinMain",            "stdcall"),
    (0x0040B600, "InitApplication",    "cdecl"),
    (0x0040B280, "PumpMessage",        "cdecl"),
    (0x0040B070, "PositionWindow",     "cdecl"),
    # Not patched -- registered over. We still call it, so its ABI matters.
    (0x0040A6B0, "WndProc",            "stdcall"),
    (0x0041AA10, "InitDirectDraw",     "cdecl"),
    (0x00426D30, "InitInput",          "cdecl"),
    (0x0041B850, "CreateOffscreenSurface", "cdecl"),
    (0x0041AD30, "ClearSurface",       "cdecl"),
    (0x0041AF00, "RealizeSystemPalette", "cdecl"),
    (0x00445170, "SnapshotSystemPalette", "cdecl"),
    (0x0041E7A0, "ReportError",       "cdecl"),
    (0x0041E750, "FatalError",        "cdecl"),
    (0x0040CA10, "WaveOpenFile",      "cdecl"),
    (0x0040CBB0, "WaveStartDataRead", "cdecl"),
    (0x00445120, "MovieStop",        "thiscall",
     "thiscall taking no stack arguments, so a plain `ret` is right and there "
     "is nothing to clean -- indistinguishable from the tool's `thiscall?`"),
    (0x00445280, "MovieSetVolume",   "thiscall"),
    (0x004453C0, "MovieDrawFrame",   "thiscall"),
    (0x00445600, "MovieFinished",    "cdecl"),
    (0x00426B50, "FindGameCD",       "cdecl"),
    (0x004466E0, "BuildFont",        "cdecl"),
    (0x0041AC60, "PresentFrame",     "cdecl"),
    (0x0040B2B0, "DetectCpuSpeed",   "cdecl"),
    (0x00446450, "CreateGameFont",   "cdecl"),
    (0x0040CBF0, "WaveReadFile",      "cdecl"),
    (0x0040CCE0, "WaveCloseReadFile", "cdecl"),
    (0x0040DD20, "CommCreateDirectPlay", "thiscall"),
    (0x0040DDD0, "CreateDirectPlayLobby", "stdcall",
     "opens `push ecx` to allocate one local, then overwrites the slot with 0 "
     "before anything reads it -- ecx on entry is dead, so stdcall"),
    (0x0041C710, "BlitGlyph",          "fastcall"),
    (0x0041C2B0, "BlitCopy16",         "fastcall"),
    (0x0041C1C0, "BlitCopy32",         "fastcall"),
    (0x0041C3A0, "BlitRemap16",        "fastcall"),
    (0x0041C480, "BlitOverlay",        "fastcall"),
]

SCAN = 40          # instructions to scan for entry register reads


def idiom_is_write_only(insn):
    """True for `xor r, r`, `sub r, r` and `or r, -1`."""
    ops = insn.operands
    if len(ops) != 2:
        return False
    if insn.mnemonic in ("xor", "sub"):
        return (ops[0].type == capstone.x86.X86_OP_REG and
                ops[1].type == capstone.x86.X86_OP_REG and
                ops[0].reg == ops[1].reg)
    if insn.mnemonic == "or":
        return (ops[1].type == capstone.x86.X86_OP_IMM and
                (ops[1].imm & 0xFFFFFFFF) == 0xFFFFFFFF)
    return False


def load_sizes():
    """{addr: size} from docs/functions.tsv, or {} if it is not there yet."""
    import csv

    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "docs", "functions.tsv")
    if not os.path.exists(path):
        return {}
    with open(path) as fh:
        return {int(r["addr"], 16): int(r["size"])
                for r in csv.DictReader(fh, delimiter="\t")}


def analyse(img, va, size=None):
    """Return (reads_ecx, reads_edx, ret_immediates).

    `size` bounds the scan to the function. Without it the disassembly runs off
    the end into whatever follows and collects that function's `ret N` as if it
    were this one's -- which reported SnapshotSystemPalette, 128 bytes and
    plainly cdecl, as possibly stdcall. The whole-binary survey was reading the
    same way, so the convention counts it prints were wrong too.

    Known false positive, left in deliberately: MSVC 6 allocates a single
    4-byte local with `push ecx`, which reads ecx and so reports as thiscall.
    0x0041B7C0 does exactly this and is plain cdecl. Suppressing it would mean
    treating an entry `push ecx` as a non-read, and a genuine thiscall spilling
    `this` looks identical at that instruction -- which would quietly deflate
    the whole-binary thiscall count the C++ decision rests on. Better to
    over-report and check the few by hand.
    """
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    reads = {"ecx": False, "edx": False}
    settled = {"ecx": False, "edx": False}
    rets = set()

    for n, insn in enumerate(md.disasm(img.read(va, size or 800), va)):
        if insn.mnemonic == "ret":
            rets.add(int(insn.op_str, 0) if insn.op_str else 0)
            if n > SCAN:
                break
            continue
        if n < SCAN:
            regs_read, regs_written = insn.regs_access()
            names_read = {insn.reg_name(r) for r in regs_read}
            names_written = {insn.reg_name(r) for r in regs_written}
            # Zeroing and set-to-all-ones idioms encode a read of the register
            # but the result does not depend on its old value, so they are
            # writes. Missing this made FindSlot, ObjIsType2/3 and DrawText all
            # look like thiscall because of `xor ecx, ecx` and `or ecx, -1`.
            if idiom_is_write_only(insn):
                names_read = set()
            for full, sub in (("ecx", {"ecx", "cx", "cl", "ch"}),
                              ("edx", {"edx", "dx", "dl", "dh"})):
                if settled[full]:
                    continue
                if names_read & sub:
                    reads[full] = True
                    settled[full] = True
                elif names_written & sub:
                    settled[full] = True
        if n > 400:
            break
    return reads["ecx"], reads["edx"], rets


def expected(reads_ecx, reads_edx, rets):
    callee_cleans = any(r > 0 for r in rets)
    if reads_ecx and reads_edx:
        return "fastcall"
    if reads_ecx and not reads_edx:
        return "thiscall" if callee_cleans else "thiscall?"
    return "stdcall" if callee_cleans else "cdecl"


def main():
    img = am2.Image()
    bad = 0

    print(f"{'function':<20}{'addr':<12}{'declared':<10}{'ecx':>5}{'edx':>5}"
          f"{'ret':>8}  verdict")
    print("  " + "-" * 68)
    sizes = load_sizes()
    notes = []
    for entry in FUNCS:
        va, name, declared = entry[0], entry[1], entry[2]
        # A fourth field is a hand-verified override: the machine-code
        # heuristic disagrees and has been checked by reading the function.
        # Recording why here keeps the audit green without it going quiet.
        waived = entry[3] if len(entry) > 3 else None
        rc, rd, rets = analyse(img, va, sizes.get(va))
        got = expected(rc, rd, rets)
        retstr = ",".join(hex(r) if r else "-" for r in sorted(rets)) or "?"
        ok = (got == declared)
        # cdecl and stdcall are indistinguishable from the entry registers
        # alone; only a mismatch on the ecx/edx axis is a real finding.
        if ok:
            flag = "ok"
        elif waived:
            flag = "ok (hand-checked)"
            notes.append((name, got, waived))
        elif "this" in got or "fast" in got or "fast" in declared:
            flag = "MISMATCH"
            bad += 1
        else:
            flag = f"note: {got}"
        print(f"{name:<20}{va:#010x}  {declared:<10}"
              f"{('yes' if rc else 'no'):>5}{('yes' if rd else 'no'):>5}"
              f"{retstr:>8}  {flag}"
              .replace("True ", " yes ").replace("False", "  no "))

    print()
    for name, got, why in notes:
        print(f"{name} reads as {got}; declared otherwise on purpose:")
        print(f"  {why}")
    if notes:
        print()
    if bad:
        print(f"{bad} function(s) may use a register convention we did not declare")
    else:
        print("no thiscall or fastcall mismatches: every function taking `this`-like")
        print("state in ecx is declared fastcall, and no cdecl-declared function")
        print("reads ecx or edx before writing it.")
    survey(img)
    return 1 if bad else 0


def survey(img):
    """How much thiscall is in the binary at all?

    The game is C++ -- the savegame anchors name air.cpp, unit.cpp and friends
    -- so member functions taking `this` in ecx should exist somewhere, even if
    none of the free functions reconstructed so far use it. A thiscall function
    reads ecx before writing it and cleans its own stack arguments.
    """
    import csv

    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "docs", "functions.tsv")
    if not os.path.exists(path):
        return

    kinds = {"cdecl": 0, "stdcall": 0, "fastcall": 0, "thiscall": 0,
             "ecx-in, caller-cleans": 0}
    examples = []
    with open(path) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            va = int(row["addr"], 16)
            if va >= 0x0045C000:          # MSVC CRT, not game code
                continue
            rc, rd, rets = analyse(img, va, int(row["size"]))
            kind = expected(rc, rd, rets)
            # A plain `ret` with ecx live on entry is NOT thiscall -- thiscall
            # is callee-cleaned. Keep them apart rather than collapsing them.
            if kind == "thiscall?":
                kind = "ecx-in, caller-cleans"
            kinds[kind] = kinds.get(kind, 0) + 1
            if kind == "thiscall" and len(examples) < 8:
                examples.append((va, row["callers"]))

    total = sum(kinds.values())
    print()
    print(f"whole-binary survey of {total} game functions below the CRT:")
    for k in ("cdecl", "stdcall", "fastcall", "thiscall",
              "ecx-in, caller-cleans"):
        print(f"    {k:<10}{kinds.get(k, 0):>6}")
    if examples:
        print("  thiscall examples (addr, callers):")
        for va, c in examples:
            print(f"    {va:#010x}  {c}")


if __name__ == "__main__":
    sys.exit(main())
