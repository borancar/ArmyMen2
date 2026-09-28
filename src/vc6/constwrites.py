#!/usr/bin/env python3
"""Find writes into read-only (const) data in the VC6 runnable build.

A transcribed table the game WRITES at run time must not be `const`: the
native build never noticed, because place.ld gathers every placed symbol --
.rodata included -- into one writable section, but the VC6 LINK.EXE image maps
.rdata read-only and the first such write faults.  This finds them statically
from the objects in build/vc6sa, instead of one crash per run:

  * an instruction whose memory operand is relocated to a symbol defined in a
    read-only section, and which WRITES that operand (capstone access flags);
  * a `push offset <const symbol>` that is the LAST push before a call to a
    function that writes through its first argument (memcpy, fread, strcpy..).

    src/vc6/constwrites.py        # report; exit 1 if any
"""
import glob
import os
import struct
import sys

import capstone
from capstone import x86

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OBJDIR = os.path.join(REPO, "build", "vc6sa")
# first-argument writers (cdecl: the first argument is pushed last)
DEST_WRITERS = {"_memcpy", "_memmove", "_memset", "_fread", "_strcpy", "_strcat",
                "_strncpy", "_sprintf", "_vsprintf", "_fgets", "_crt_fread",
                "_crt_fgets", "_crt_strncpy", "_crt_sprintf", "_crt_vsprintf",
                "_crt_memmove", "_read", "__read"}


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
        rels = [struct.unpack_from("<IIH", d, rptr + r * 10) for r in range(nrel)]
        secs.append(dict(name=name, data=d[ptr:ptr + sz] if ptr else b"",
                         chars=chars, rels=rels))
    syms = []
    i = 0
    while i < nsym:
        b = symptr + i * 18
        raw = d[b:b + 8]
        name = strn(struct.unpack_from("<I", d, b + 4)[0]) if raw[:4] == b"\0\0\0\0" else raw.split(b"\0")[0].decode("latin1")
        val, sec = struct.unpack_from("<Ih", d, b + 8)
        syms.append((name, val, sec))
        for _ in range(d[b + 17]):
            syms.append(None)
        i += 1 + d[b + 17]
    return secs, syms


def readonly(sec):
    return not (sec["chars"] & 0x80000000) and not (sec["chars"] & 0x20)


def main():
    objs = sorted(glob.glob(os.path.join(OBJDIR, "*.obj")))
    parsed = {o: parse(o) for o in objs}
    const_syms = set()
    for secs, syms in parsed.values():
        for s in syms:
            if s and 1 <= s[2] <= len(secs) and readonly(secs[s[2] - 1]) \
                    and not s[0].startswith(("??_C@", "$", ".", "__real@")):
                const_syms.add(s[0])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    hits = []
    for o, (secs, syms) in parsed.items():
        for s in secs:
            if not (s["chars"] & 0x20) or not s["data"]:
                continue
            relsym = {va: syms[idx][0] for va, idx, _t in s["rels"] if syms[idx]}
            starts = sorted(set(v for (n, v, sec) in [x for x in syms if x]
                                if 1 <= sec <= len(secs) and secs[sec - 1] is s
                                and not n.startswith(("$", "."))))
            if not starts or starts[0] != 0:
                starts = [0] + starts
            owner = {v: n for (n, v, sec) in [x for x in syms if x]
                     if 1 <= sec <= len(secs) and secs[sec - 1] is s}
            for i, st in enumerate(starts):
                end = starts[i + 1] if i + 1 < len(starts) else len(s["data"])
                insns = list(md.disasm(s["data"][st:end], st))
                fn = owner.get(st, "?")
                for k, ins in enumerate(insns):
                    rel = [relsym[a] for a in range(ins.address, ins.address + ins.size)
                           if a in relsym]
                    targets = [r for r in rel if r in const_syms]
                    if not targets:
                        continue
                    # the relocated field must BE the written operand's
                    # displacement -- `mov [esi], OFFSET vtable` stores the
                    # table's address, it does not write into the table
                    dsym = relsym.get(ins.address + ins.disp_offset) if ins.disp_offset else None
                    wrote = dsym in const_syms and any(
                        op.type == x86.X86_OP_MEM and (op.access & capstone.CS_AC_WRITE)
                        for op in ins.operands)
                    if wrote:
                        hits.append((os.path.basename(o), fn, ins.address, dsym,
                                     "%s %s" % (ins.mnemonic, ins.op_str)))
                        continue
                    if ins.mnemonic == "push":
                        for nxt in insns[k + 1:k + 8]:
                            if nxt.mnemonic == "push":
                                break
                            if nxt.mnemonic == "call":
                                callee = [relsym[a] for a in range(nxt.address, nxt.address + nxt.size)
                                          if a in relsym]
                                if callee and callee[0] in DEST_WRITERS:
                                    hits.append((os.path.basename(o), fn, ins.address, targets[0],
                                                 "dest of %s" % callee[0]))
                                break
    for h in hits:
        print("%-14s %-40s +0x%05x  writes %-32s %s" % h)
    print("TOTAL writes into read-only data: %d" % len(hits))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
