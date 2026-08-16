"""Recover the savegame chunk tags and the file/line anchors that come with them.

ArmyMen2.exe embeds ten `C:\\ArmyMen2\\source\\*.cpp` strings. They are not
assert() sites -- they are arguments to a savegame chunk-tag verifier at
0x4235d0:

    BOOL CheckSaveTag(FILE *fp, uint32_t expected, const char *file, int32_t line)
    {
        uint32_t got;                       // fread reads over the fp slot
        fread(&got, 4, 1, fp);
        if (got == expected) return TRUE;
        LogError("Error reading save file, source file: %s  line: %d\\n", file, line);
        return FALSE;
    }

Each call site therefore yields three things at once: the tag constant for one
section of the save format, and a (file, line) pair pinned to a known address --
which is what lets us bound translation units in .text.

Writes docs/savetags.tsv.
"""

import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "savetags.tsv")

CHECK_SAVE_TAG = 0x4235D0

SOURCES = [
    "air.cpp", "audio.cpp", "event.cpp", "gameproc.cpp", "item.cpp",
    "map.cpp", "objscript.cpp", "pad.cpp", "script.cpp", "unit.cpp",
]

OP_IMM = capstone.x86.X86_OP_IMM


def imm_of(insn):
    if insn.mnemonic != "push" or len(insn.operands) != 1:
        return None
    op = insn.operands[0]
    return op.imm if op.type == OP_IMM else None


def main():
    img = am2.Image()
    insns = am2.Image.disasm(img, ".text")
    by_addr = {i.address: n for n, i in enumerate(insns)}

    # VA of each source-path string -> basename.
    file_of_va = {}
    for name in SOURCES:
        for va in am2.find_string_vas(img, "C:\\ArmyMen2\\source\\" + name):
            file_of_va[va] = name

    rows = []
    for insn in insns:
        if insn.mnemonic != "call":
            continue
        op = insn.operands[0]
        if op.type != OP_IMM or op.imm != CHECK_SAVE_TAG:
            continue
        # Walk back over the four pushes: fp, tag, file, line.
        idx = by_addr[insn.address]
        pushes = []
        for k in range(idx - 1, max(idx - 8, -1), -1):
            if insns[k].mnemonic != "push":
                break
            pushes.append(insns[k])
        # pushes[] is innermost-first: fp, tag, file, line
        tag = file_va = line = None
        for p in pushes:
            v = imm_of(p)
            if v is None:
                continue
            if v in file_of_va:
                file_va = v
            elif file_va is None:
                tag = v          # pushed after file in reverse order == before it
            else:
                line = v
        if file_va is None:
            rows.append((insn.address, "?", None, tag))
            continue
        rows.append((insn.address, file_of_va[file_va], line, tag))

    rows.sort()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("addr\tfile\tline\ttag\n")
        for addr, name, line, tag in rows:
            fh.write(f"0x{addr:08x}\t{name}\t{line if line is not None else ''}\t"
                     f"{('0x%08x' % tag) if tag is not None else ''}\n")

    print(f"{len(rows)} CheckSaveTag call sites -> {os.path.relpath(OUT, REPO)}\n")
    print(f"  {'addr':<12}{'file':<15}{'line':>6}  tag")
    print("  " + "-" * 48)
    for addr, name, line, tag in rows:
        print(f"  0x{addr:08x}  {name:<15}{line if line is not None else '?':>6}  "
              f"{('0x%08x' % tag) if tag is not None else '?'}")


if __name__ == "__main__":
    main()
