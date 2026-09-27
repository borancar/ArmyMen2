#!/usr/bin/env python3
"""Whole-image relink byte-match.

The per-function harness (bytematch.py) compiles one .obj at a time and
WILDCARDS every relocated 4-byte slot, because piecewise it cannot know the
final addresses.  That leaves two systematic diff classes it can never close
(see src/vc6/README.md): a call to an un-reconstructed sibling that the
reconstruction routes through a pointer (ff15) where the original linked a
direct e8, and an indexed global folded from a constant base.

This harness simulates the LINK instead of wildcarding: it lays every
reconstructed function at its real VA, builds one symbol->VA table spanning the
whole image, and RESOLVES each relocation to the address the linker would have
filled in, then compares the resolved bytes to the shipped image with NO
wildcarding.  A reconstructed-sibling call is thus verified to reach the right
target (a stronger check than bytematch's wildcard), and once the source calls
un-reconstructed code by symbol (relink.h, phased in) those e8 calls resolve too.

Phase 1 (this file): resolve relocations whose target is a reconstructed
function; leave the rest wildcarded.  Re-verifies the piecewise matches with
their call targets pinned, and reports how many diffs are now purely the
still-unresolved (un-reconstructed / global) references the later phases close.
"""
import os, sys, struct, tempfile, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bytematch as BM
sys.path.insert(0, os.path.join(BM.REPO, "tools"))
import am2

IMAGE_REL_I386_DIR32 = 0x06
IMAGE_REL_I386_REL32 = 0x14


def import_map():
    """{base import name -> IAT slot VA}.  A reconstruction that calls a Win32
    import compiles `call [__imp__Foo@n]` (ff15 through the IAT) -- the SAME form
    the original linked, so once __imp__Foo@n resolves to the IAT slot the bytes
    match with no source change."""
    import pefile
    pe = pefile.PE(am2.EXE, fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    m = {}
    for ent in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in ent.imports:
            if imp.name:
                m[imp.name.decode("latin1")] = imp.address
    return m


def import_va(sym, imap):
    """VA for a COFF __imp_ symbol, e.g. __imp__IntersectRect@12 -> IAT slot."""
    if not sym.startswith("__imp_"):
        return None
    core = sym[len("__imp_"):].lstrip("_").split("@")[0]
    return imap.get(core)


def parse_coff_full(path):
    """secs[i] = {data, rels:[(off, symidx, type)]}; symlist[idx]=(name,secnum,value)."""
    d = open(path, "rb").read()
    nsec = struct.unpack_from("<H", d, 2)[0]
    symptr, nsym = struct.unpack_from("<II", d, 8)
    optlen = struct.unpack_from("<H", d, 16)[0]
    secoff = 20 + optlen
    strtab = symptr + nsym * 18

    def strn(off):
        e = d.index(b"\0", strtab + off)
        return d[strtab + off:e].decode("latin1")

    secs = []
    for i in range(nsec):
        b = secoff + i * 40
        _, _, sz, dataptr, relptr, _, nrel, _, _ = \
            struct.unpack_from("<IIIIIIHHI", d, b + 8)
        data = bytearray(d[dataptr:dataptr + sz]) if dataptr else bytearray()
        rels = [struct.unpack_from("<IIH", d, relptr + r * 10)
                for r in range(nrel)]
        secs.append(dict(data=data, rels=rels))

    # symlist indexed by symbol number (aux records occupy slots too)
    symlist = [None] * nsym
    i = 0
    while i < nsym:
        b = symptr + i * 18
        raw = d[b:b + 8]
        name = strn(struct.unpack_from("<I", d, b + 4)[0]) if raw[:4] == b"\0\0\0\0" \
            else raw.split(b"\0")[0].decode("latin1")
        value, secnum = struct.unpack_from("<ih", d, b + 8)
        aux = d[b + 17]
        symlist[i] = (name, secnum, value)
        i += 1 + aux
    return secs, symlist


def sym_section_of(secs, symlist, symidx):
    """The section index (1-based) a symbol is defined in, or 0 if external."""
    _, secnum, _ = symlist[symidx]
    return secnum


def main():
    man = BM.manifest()
    byfile = collections.defaultdict(list)
    for f, fn, va in man:
        byfile[f].append((fn, va))
    img = am2.Image()

    # ---- pass 1: compile every TU, record each reconstructed function's
    # COFF symbol name and its real VA, to build the global symbol->VA table.
    # The AM2_ORIGFN symbols (crt.h): un-reconstructed CRT/engine functions the
    # reconstruction now calls directly under AM2_RELINK, each resolving to the
    # game-CRT VA it stands for.  These are the ff15->e8 closures.
    addr = {}
    import re
    for line in open(os.path.join(BM.REPO, "src/inject/orig.h")):
        m = re.match(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?", line.strip())
        if m:
            addr[m.group(1)] = int(m.group(2), 16)
    ORIGFN_VA = {
        "_am2_malloc":  addr["ADDR_GAME_MALLOC"],
        "_am2_realloc": addr["ADDR_REALLOC"],
        "_am2_free":    addr["ADDR_GAME_FREE"],
        "_am2_log":     addr["ADDR_LOG"],
        "_am2_strtol":  addr["ADDR_CRT_STRTOL"],
        "_am2_strtok":  addr["ADDR_CRT_STRTOK"],
        "_am2_sprintf": addr["ADDR_GAME_SPRINTF"],
        "_am2_chdir":   addr["ADDR_CRT_CHDIR"],
        "_am2_getcwd":  addr["ADDR_CRT_GETCWD"],
    }

    sym_va = dict(ORIGFN_VA)     # coff symbol name -> real VA
    imap = import_map()         # base import name -> IAT slot VA
    compiled = {}               # file -> (secs, symlist, names, base)
    with tempfile.TemporaryDirectory() as od, tempfile.TemporaryDirectory() as odb:
        for f in sorted(byfile):
            if os.path.basename(f) in BM.SKIP_FILE:
                continue
            obj = BM.compile_file(f, od, extra=["/DAM2_RELINK"])
            if obj is None:
                continue
            secs, symlist = parse_coff_full(obj)
            # name -> (secnum, value) for defined symbols, like bytematch.syms
            names = {}
            for s in symlist:
                if s and s[0] not in names:
                    names[s[0]] = (s[1], s[2])
            # the piecewise baseline: same TU WITHOUT the relink source form,
            # compiled into a separate dir so the .obj paths do not collide.
            base_obj = BM.compile_file(f, odb)
            base = BM.parse_coff(base_obj) if base_obj else None
            compiled[f] = (secs, symlist, names, base)
            for fn, va in byfile[f]:
                sym = BM.resolve_sym(names, fn)
                if sym:
                    sym_va[sym] = va

        # ---- pass 2: resolve and compare, holding the .objs in memory.
        match = diff = 0
        unresolved_hist = collections.Counter()
        diffs = []
        newly = []              # relink matches that the piecewise harness misses
        for f in sorted(byfile):
            if f not in compiled:
                continue
            secs, symlist, names, base = compiled[f]
            for fn, va in byfile[f]:
                sym = BM.resolve_sym(names, fn)
                if not sym:
                    continue
                secnum, value = names[sym]
                sec = secs[secnum - 1]
                data = sec["data"]
                # function extent: value .. next defined symbol in this section
                end = len(data)
                for nm, (sn, v) in names.items():
                    if sn == secnum and v > value:
                        end = min(end, v)
                n = len(BM.trim(data[value:end]))
                if n == 0:
                    continue
                cur = bytearray(data[value:value + n])
                wildcard = [False] * n
                # apply this function's relocations
                for off, symidx, typ in sec["rels"]:
                    roff = off - value
                    if not (0 <= roff < n):
                        continue
                    tgt_name = symlist[symidx][0]
                    tgt_va = sym_va.get(tgt_name)
                    if tgt_va is None:
                        tgt_va = import_va(tgt_name, imap)
                    if tgt_va is None:
                        unresolved_hist[tgt_name] += 1
                        for k in range(4):
                            if roff + k < n:
                                wildcard[roff + k] = True
                        continue
                    site_va = va + roff
                    addend = struct.unpack_from("<i", cur, roff)[0]
                    if typ == IMAGE_REL_I386_REL32:
                        val = tgt_va + addend - (site_va + 4)
                    else:                      # DIR32 (and treat others as abs)
                        val = tgt_va + addend
                    struct.pack_into("<I", cur, roff, val & 0xffffffff)
                orig = bytearray(img.read(va, n))
                ok = all(wildcard[i] or cur[i] == orig[i] for i in range(n))
                # the true piecewise baseline: bytematch.py on the NON-relink .obj
                piece_ok = False
                if base is not None:
                    bsecs, bsyms = base
                    bsym = BM.resolve_sym(bsyms, fn)
                    fb = BM.func_bytes(bsecs, bsyms, bsym) if bsym else None
                    if fb is not None:
                        bc, brel = fb
                        bm = BM.trim(bc)
                        bn = len(bm)
                        bo = bytearray(img.read(va, bn))
                        piece_ok = (bytes(BM.mask(bm, brel, bn)) ==
                                    bytes(BM.mask(bo, brel, bn)))
                if ok:
                    match += 1
                    if not piece_ok:
                        newly.append((fn, va, os.path.basename(f)))
                else:
                    diff += 1
                    nd = sum(1 for i in range(n)
                             if not wildcard[i] and cur[i] != orig[i])
                    diffs.append((nd, fn, va, os.path.basename(f)))

    print("=== relink byte-match (siblings + AM2_ORIGFN CRT + Win32 IAT resolved) ===")
    print("  match %d   diff %d   (relink closes %d the piecewise harness cannot)"
          % (match, diff, len(newly)))
    if "--newly" in sys.argv:
        for fn, va, f in sorted(newly, key=lambda x: x[1]):
            print("  +  %-26s 0x%08x %s" % (fn, va, f))
    if "--diffs" in sys.argv:
        for nd, fn, va, f in sorted(diffs)[:60]:
            print("  %3d  %-26s 0x%08x %s" % (nd, fn, va, f))
    if "--unresolved" in sys.argv:
        print("top unresolved reloc targets (what later phases must place):")
        for nm, c in unresolved_hist.most_common(40):
            print("  %5d  %s" % (c, nm))


if __name__ == "__main__":
    main()
