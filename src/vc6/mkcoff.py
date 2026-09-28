#!/usr/bin/env python3
"""Minimal i386 COFF object emitter for the VC6 standalone link.

LINK.EXE cannot pin a section to a fixed VA (no --section-start), so the VC6
standalone reproduces the original's placement by NATURAL sequential layout:
the reconstruction is the sole code class (0x00401000, padded to 0x0046F000),
and the carried data follows at its real VAs.  This module writes the carried
data as COFF objects LINK.EXE can consume: a raw-bytes section, a .bss-style
reservation, or a code-class filler, each optionally exporting symbols.

There is no assembler in this VC6 install (no ML.EXE), and CL cannot emit an
arbitrary named section of raw bytes from C, so the blob objects are written
here by hand.  i386 COFF is small and fixed; bytematch.parse_coff reads the
same layout back.
"""
import struct

# Section characteristics (winnt.h IMAGE_SCN_*).
CNT_CODE = 0x00000020
CNT_INITIALIZED_DATA = 0x00000040
CNT_UNINITIALIZED_DATA = 0x00000080
MEM_EXECUTE = 0x20000000
MEM_READ = 0x40000000
MEM_WRITE = 0x80000000

CODE = CNT_CODE | MEM_EXECUTE | MEM_READ
RDATA = CNT_INITIALIZED_DATA | MEM_READ
DATA = CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
BSS = CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE


def emit(path, sections):
    """sections: list of {name, chars, data(bytes) OR bsssize(int),
    syms:[(name,offset)]}.  Symbols are external (storage class 2) defined in
    the section so LINK keeps it and other objects can reference it."""
    strtab = bytearray(b"\x00\x00\x00\x00")

    def add_str(s):
        off = len(strtab)
        strtab.extend(s.encode("latin1") + b"\x00")
        return off

    nsec = len(sections)
    raw_off = 20 + 40 * nsec
    cur = raw_off
    for s in sections:
        if "bsssize" in s:
            s["_dataptr"] = 0
            s["_rawsize"] = s["bsssize"]
        else:
            s["_dataptr"] = cur
            s["_rawsize"] = len(s["data"])
            cur += len(s["data"])
    symptr = cur

    symrecs = []
    for i, s in enumerate(sections):
        for (nm, off) in s.get("syms", []):
            symrecs.append((nm, off, i + 1))

    out = bytearray()
    out += struct.pack("<HHIIIHH", 0x14c, nsec, 0, symptr, len(symrecs), 0, 0)

    for s in sections:
        name = s["name"]
        if len(name) <= 8:
            namefield = name.encode("latin1").ljust(8, b"\x00")
        else:
            namefield = ("/" + str(add_str(name))).encode("latin1").ljust(8, b"\x00")
        out += namefield
        out += struct.pack("<IIIIIIHHI", 0, 0, s["_rawsize"], s["_dataptr"],
                           0, 0, 0, 0, s["chars"])

    for s in sections:
        if "bsssize" not in s:
            out += s["data"]

    assert len(out) == symptr, (len(out), symptr)
    for (nm, off, secnum) in symrecs:
        if len(nm) <= 8:
            namefield = nm.encode("latin1").ljust(8, b"\x00")
        else:
            namefield = struct.pack("<II", 0, add_str(nm))
        out += namefield
        out += struct.pack("<IhHBB", off, secnum, 0, 2, 0)

    struct.pack_into("<I", strtab, 0, len(strtab))
    out += strtab
    open(path, "wb").write(out)
    return path
