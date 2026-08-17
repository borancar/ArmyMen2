"""Find the entries in docs/functions.tsv that are really several functions.

docs/functions.tsv is built from the symbol-free image, so where it cannot see a
boundary it runs two neighbours together. That was known -- tools/checkcom.py
already lists "docs/functions.tsv merges neighbours" as a reason a COM count can
disagree -- but it was treated as a nuisance in one tool rather than as
something that corrupts a number the project actually steers by.

It does corrupt it. CLAUDE.md says to pick the next reconstruction target by
BOUNDARY DENSITY, sites per byte, and a merged entry has the sites of one
function over the bytes of several. Both halves of the ratio are wrong and they
push it the same way, so merged functions systematically rank too low and get
declined.

That is not hypothetical. `0x0042BEA0` sat on the declined list as "1200 bytes,
2 COM calls" -- 600 bytes per call, well under the ~50 that CLAUDE.md says is
worth a look. The entry is four functions. The one holding both COM calls is
`RestoreTileSet` at `0x0042C0E0`, 624 bytes, and it is a file-to-surface loader
of exactly the kind this port exists to replace. It was reconstructed once the
real boundary was known.

HOW A BOUNDARY IS FOUND. MSVC 6 pads between functions with 0x90, so a `ret`
followed by NOPs followed by code is a candidate. That alone is not enough:
linear disassembly desynchronises on jump tables and other data in .text, and
then invents `ret`s that are not there. Two of the first five candidates checked
by hand were exactly that -- `0x00406644` disassembles as `jp` into the middle
of an instruction.

So a candidate is only reported when something REFERENCES it: a call, a jump, or
a stored pointer such as a vtable slot. A run of bytes that nothing in the image
points at is not a function anyone can enter, whatever it disassembles to. That
takes 260 candidates to 186, and the discarded 74 are a mix of genuinely
unreferenced code and disassembly noise, with no way to tell them apart cheaply
-- which is the point of leaving them out.

The numbers are therefore a LOWER BOUND on how much of functions.tsv is merged,
and are meant to be. Read it as "at least this much", never as a full census.

    tools/merges.py            # what is merged, worst first
    tools/merges.py --com      # only entries that contain a COM call site,
                               # with density recomputed on the real size
"""

import collections
import csv
import os
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRT_START = 0x0045C000


def referenced_addresses(img):
    """Everything the image points at: call/jmp rel32 targets and stored VAs."""
    out = set()
    for _name, start, _end, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] in (0xE8, 0xE9):
                out.add(start + j + 5 + struct.unpack_from("<i", data, j + 1)[0])
        for j in range((-start) % 4, len(data) - 4, 4):
            out.add(struct.unpack_from("<I", data, j)[0])
    return out


def split_points(img, addr, size, md):
    """Addresses inside [addr, addr+size) that begin after `ret` + NOP padding."""
    try:
        insns = list(md.disasm(img.read(addr, size), addr))
    except Exception:
        return []
    out, after_ret, pad = [], False, 0
    for insn in insns:
        if insn.mnemonic == "nop":
            pad += 1
            continue
        if after_ret and pad:
            out.append(insn.address)
        after_ret = insn.mnemonic.startswith("ret")
        pad = 0
    return out


def real_functions(img):
    """{listed address: [real start, ...]} for every merged entry."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    referenced = referenced_addresses(img)
    out = {}
    with open(os.path.join(REPO, "docs", "functions.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addr, size = int(row["addr"], 16), int(row["size"])
            if addr >= CRT_START or size < 16:
                continue
            starts = [a for a in split_points(img, addr, size, md)
                      if a in referenced]
            if starts:
                out[addr] = [addr] + starts, size
    return out


def com_sites():
    """{function address: [call site, ...]}, genuine COM dispatch only."""
    out = collections.defaultdict(list)
    with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            if row.get("abi") == "thiscall":
                continue
            func = int(row["func"], 16)
            if func and func < CRT_START:
                out[func].append(int(row["site"], 16))
    return out


def owner(starts, size, site):
    """Which of the split functions a call site actually falls in."""
    bounds = list(starts) + [starts[0] + size]
    for k in range(len(starts)):
        if bounds[k] <= site < bounds[k + 1]:
            return starts[k], bounds[k + 1] - bounds[k]
    return starts[0], size


def main():
    img = am2.Image()
    merged = real_functions(img)
    total = sum(size for _starts, size in merged.values())

    print(f"{len(merged)} entries in docs/functions.tsv are at least two "
          f"functions,\ncovering {total:,} bytes attributed to the wrong one. "
          "Confirmed by xref only,\nso this is a lower bound.\n")

    if "--com" not in sys.argv:
        rows = sorted(merged.items(), key=lambda kv: -len(kv[1][0]))
        print(f"{'listed':<12} {'listed size':>11} {'functions':>10}  real starts")
        for addr, (starts, size) in rows[:20]:
            rest = " ".join(f"{a:#x}" for a in starts[1:4])
            more = " ..." if len(starts) > 4 else ""
            print(f"{addr:#010x} {size:>11,} {len(starts):>10}  {rest}{more}")
        return 0

    sites = com_sites()
    print("COM-bearing entries, density recomputed on the real function:\n")
    print(f"{'listed':<12} {'real fn':<12} {'was':>10} {'now':>10}  sites")
    changed = []
    for addr, (starts, size) in merged.items():
        if addr not in sites:
            continue
        per = collections.defaultdict(list)
        for site in sites[addr]:
            real, real_size = owner(starts, size, site)
            per[(real, real_size)].append(site)
        for (real, real_size), got in sorted(per.items()):
            was = size / len(sites[addr])
            now = real_size / len(got)
            changed.append((now, addr, real, was, now, len(got)))
    for now, addr, real, was, _n, count in sorted(changed):
        flag = "  <-- was over 50 B/site, now under" if was > 50 >= now else ""
        print(f"{addr:#010x} {real:#010x} {was:>9.0f}B {now:>9.0f}B {count:>6}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
