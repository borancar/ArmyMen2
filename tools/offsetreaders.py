#!/usr/bin/env python3
"""offsetreaders.py OFFSET [OFFSET ...] [--file SUBSTR ...]

List the functions of the shipped image (the ORIGINAL bytes build/relink-base.json
keeps for every function) that use a [reg+OFFSET] memory operand, optionally only
those defined in source files whose path contains SUBSTR.

This is the evidence behind the `unusedXX` members of docs/fieldnames.md: a member
that no function of the shipped binary reads (outside the code that zeroes or copies
whole records) has no role in the program to name. A hit is not proof the member is
read -- [esp+OFFSET] stack slots and other structs share the number -- so each hit
has to be looked at, as the notes in docs/fieldnames.md did for Flow +0x34 (a stack
slot in FlowRecvMessage) and FontRec +0x208 (a stack buffer in SaveGame).

Needs build/relink-base.json (tools/relinkscreen.py --baseline).
"""
import json, os, sys
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    args = sys.argv[1:]
    files = []
    while "--file" in args:
        i = args.index("--file")
        files.append(args[i + 1])
        del args[i:i + 2]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    want = {int(a, 0) for a in args}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    for e in json.load(open(os.path.join(REPO, "build", "relink-base.json"))):
        if files and not any(f in e["file"] for f in files):
            continue
        hits = {}
        for i in md.disasm(bytes.fromhex(e["orig"]), e["va"]):
            for op in i.operands:
                if (op.type == capstone.x86.X86_OP_MEM and op.mem.base != 0
                        and op.mem.index == 0
                        and (op.mem.disp & 0xFFFFFFFF) in want):
                    reg = i.reg_name(op.mem.base)
                    hits.setdefault(op.mem.disp & 0xFFFFFFFF, []).append(
                        "%s%s" % (reg, "(stack)" if reg in ("esp", "ebp") else ""))
        for off, regs in sorted(hits.items()):
            print("0x%03X  %-28s %-28s %s" % (off, e["fn"], e["file"],
                                             " ".join(sorted(set(regs)))))


if __name__ == "__main__":
    main()
