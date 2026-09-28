#!/usr/bin/env python3
"""Find baked absolute original-image addresses in the VC6 runnable build.

The runnable symbol-relative image (link.py --run) must not depend on the
original's addresses: every global a proper symbol, every pointer relocated.
A 4-byte field whose value falls in the original data range (0x0046F000 ..
0x00666000) and which is NOT the target of a COFF relocation is a raw address
the linker never saw -- an un-migrated `ADDR_` literal in code, or a pointer
transcribed into data as a number.  In the relocated image each one reads or
writes the wrong memory (the --run guard section makes it fault).

Scans every .obj in build/vc6sa: data sections at 4-byte alignment, code at
every offset (an immediate or displacement can sit anywhere in an insn), and
attributes each hit to the nearest preceding symbol.  Code hits can include
the odd false positive from instruction bytes that happen to spell a value in
range; data hits at alignment essentially cannot.

    src/vc6/rawaddr.py            # summary + every hit
    src/vc6/rawaddr.py --data     # data sections only
    src/vc6/rawaddr.py --text     # original CODE addresses instead: raw calls
                                  # or function pointers into original code
"""
import glob
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OBJDIR = os.path.join(REPO, "build", "vc6sa")
LO, HI = 0x0046F000, 0x00666000          # the original data range
TEXT_LO, TEXT_HI = 0x00401000, 0x0046F000  # the original code range (--text)
RANGE = [LO, HI]


def parse(path):
    d = open(path, "rb").read()
    nsec = struct.unpack_from("<H", d, 2)[0]
    symptr, nsym = struct.unpack_from("<II", d, 8)
    optlen = struct.unpack_from("<H", d, 16)[0]
    strtab = symptr + nsym * 18

    def strn(off):
        e = d.index(b"\0", strtab + off)
        return d[strtab + off:e].decode("latin1")

    secs = []
    for i in range(nsec):
        b = 20 + optlen + i * 40
        raw = d[b:b + 8]
        name = strn(int(raw[1:].split(b"\0")[0])) if raw[:1] == b"/" else raw.split(b"\0")[0].decode("latin1")
        sz, ptr, rptr = struct.unpack_from("<III", d, b + 16)
        nrel = struct.unpack_from("<H", d, b + 32)[0]
        chars = struct.unpack_from("<I", d, b + 36)[0]
        data = d[ptr:ptr + sz] if ptr else b""
        rel = set()
        for r in range(nrel):
            va = struct.unpack_from("<I", d, rptr + r * 10)[0]
            rel.update(range(va, va + 4))
        secs.append(dict(name=name, data=data, rel=rel, chars=chars, syms=[]))
    i = 0
    while i < nsym:
        b = symptr + i * 18
        raw = d[b:b + 8]
        name = strn(struct.unpack_from("<I", d, b + 4)[0]) if raw[:4] == b"\0\0\0\0" else raw.split(b"\0")[0].decode("latin1")
        val, sec = struct.unpack_from("<Ih", d, b + 8)
        if 1 <= sec <= nsec and not name.startswith(("$", ".")):
            secs[sec - 1]["syms"].append((val, name))
        i += 1 + d[b + 17]
    return secs


# Owners whose in-range values are NOT addresses (verified by type/decode), or
# code that never runs in a standalone image.  Reported only with --all.
NOT_ADDRESSES = {
    "_am2_aim_displace_map": "int16[] table",
    "_am2_air_frame_hotspots": "int16[] hotspots",
    "_am2_keyrow_positions": "int16[] positions",
    "_am2_hud_squad_slot_xy": "int16[12][2] x,y",
    "_am2_slot_recs": "uint8 bytes of packed int16 pairs (64,80)...",
    "_am2_formation_slots": "uint8 bytes of packed int16 pairs (-96,96)...",
    "_am2_bool_word_names": "ASCII \"OFF\\0\"",
    "_am2_df_keyboard": "hybrid-only; standalone uses the SDK's c_dfDIKeyboard",
    "_am2_df_mouse": "hybrid-only; standalone uses the SDK's c_dfDIMouse",
}


# Owners that mix pointers with non-address bytes: only the listed byte range
# (from the symbol, end exclusive, None = to the end) is exempt.
NOT_ADDRESS_RANGES = {
    # build_menu[] (0x004762D0 = base+0x128): packed record bytes incl. ASCII
    # labels; cmd[]'s function pointers before it stay checked.
    "_am2_hud_commands": (0x128, None),
}


def ignorable(owner):
    base, _, off = owner.partition("+")
    if base in NOT_ADDRESS_RANGES and off:
        lo, hi = NOT_ADDRESS_RANGES[base]
        o = int(off, 16)
        if o >= lo and (hi is None or o < hi):
            return True
    # *_install bodies are patch_replace call sites for the injected harness;
    # a drop-in image never calls them (vc6run's patch_replace is a no-op).
    return base in NOT_ADDRESSES or base.endswith("_install")


def _owner(syms, off):
    owner = "?"
    for so, sn in syms:
        if so <= off:
            owner = "%s+0x%x" % (sn, off - so)
        else:
            break
    return owner


def _code_hits(s):
    """Disassemble from each symbol to the next and keep only 4-byte fields
    that ARE an operand's immediate or displacement -- raw-byte scanning of
    code reports instruction encodings (`8b 4d ..`) as addresses."""
    import capstone
    from capstone import x86
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    data = s["data"]
    starts = sorted(set(o for o, _ in s["syms"] if o < len(data)))
    if not starts or starts[0] != 0:
        starts = [0] + starts
    hits = []
    for i, st in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(data)
        for ins in md.disasm(data[st:end], st):
            vals = set()
            for op in ins.operands:
                if op.type == x86.X86_OP_IMM:
                    vals.add(op.imm & 0xFFFFFFFF)
                elif op.type == x86.X86_OP_MEM:
                    vals.add(op.mem.disp & 0xFFFFFFFF)
            for k in range(ins.size - 3):
                off = ins.address + k
                if any(o in s["rel"] for o in range(off, off + 4)):
                    continue
                v = struct.unpack_from("<I", data, off)[0]
                if RANGE[0] <= v < RANGE[1] and v in vals:
                    hits.append(off)
    return hits


def scan(path, data_only=False):
    hits = []
    for s in parse(path):
        is_code = bool(s["chars"] & 0x20)
        if data_only and is_code:
            continue
        if not s["data"]:
            continue
        syms = sorted(s["syms"])
        if is_code:
            offs = _code_hits(s)
        else:
            offs = [off for off in range(0, len(s["data"]) - 3, 4)
                    if not any(o in s["rel"] for o in range(off, off + 4))
                    and RANGE[0] <= struct.unpack_from("<I", s["data"], off)[0] < RANGE[1]]
        for off in offs:
            v = struct.unpack_from("<I", s["data"], off)[0]
            owner = _owner(syms, off)
            if owner.startswith("??_C@"):      # string literal bytes, not data
                continue
            hits.append((s["name"], off, v, owner))
    return hits


def main():
    data_only = "--data" in sys.argv
    show_all = "--all" in sys.argv
    if "--text" in sys.argv:
        RANGE[:] = [TEXT_LO, TEXT_HI]
    total = ignored = 0
    for obj in sorted(glob.glob(os.path.join(OBJDIR, "*.obj"))):
        hits = scan(obj, data_only)
        keep = [h for h in hits if show_all or not ignorable(h[3])]
        ignored += len(hits) - len(keep)
        if not keep:
            continue
        total += len(keep)
        print("%s: %d" % (os.path.basename(obj), len(keep)))
        for sec, off, v, owner in keep:
            print("    %-8s +0x%05x  0x%08x  %s" % (sec, off, v, owner))
    print("TOTAL raw original addresses: %d  (%d in whitelisted non-address "
          "tables / dead install code; --all shows them)" % (total, ignored))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())


if __name__ == "__main__":
    main()
