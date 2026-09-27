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


def resolve_member_first(names, fn):
    """Like BM.resolve_sym, but prefer the C++ member symbol ?fn@Class@@ over a
    cdecl `_fn`.  Under AM2_STANDALONE the vtables take the address of the inline
    thiscall forwarders, so `_fn` (the forwarder) gets emitted alongside the real
    method -- and the function installed at the manifest VA is the METHOD, not
    the 13-byte forwarder."""
    for s in names:
        if s.startswith("?" + fn + "@"):
            return s
    return BM.resolve_sym(names, fn)


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


def string_va(sym, secs, symlist, symidx, img, cache):
    """VA of an MSVC string constant (??_C@_0..) by finding its bytes in the
    shipped .rdata.  The reconstruction's literals are the SAME strings the
    original carried, at fixed .rdata VAs, so a content search resolves them."""
    if not sym.startswith("??_C@_0"):        # _0 = narrow char array
        return None
    if sym in cache:
        return cache[sym]
    _, secnum, value = symlist[symidx]
    if secnum <= 0:
        return None
    data = secs[secnum - 1]["data"]
    end = data.find(b"\0", value)
    s = bytes(data[value:end + 1]) if end >= 0 else bytes(data[value:])
    va = None
    if len(s) >= 2:
        hits = _find_all(img, s)
        if len(hits) == 1:
            va = hits[0]
    cache[sym] = va
    return va


def _find_all(img, needle):
    """All VAs where `needle` occurs in .rdata/.data."""
    out = []
    for name, start, _end, blob in img.sections:
        if name not in (".rdata", ".data"):
            continue
        i = blob.find(needle)
        while i >= 0:
            out.append(start + i)
            i = blob.find(needle, i + 1)
    return out


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
    # Compiler/CRT helpers the reconstruction calls directly (e8) with no source
    # change -- the compiler emits the call, the CRT is in the image at a fixed VA.
    HELPER_VA = {
        "__ftol": addr["ADDR_FTOL"],       # float->long cast helper
    }

    sym_va = dict(ORIGFN_VA)     # coff symbol name -> real VA
    sym_va.update(HELPER_VA)

    # Migrated-global symbols: standalone.h (now included under AM2_RELINK too)
    # redirects each ADDR_X to &am2_x, so the reconstruction's ((T*)ADDR_X)[i]
    # compiles to symbol-relative [idx*scale + am2_x] -- the original's form, not
    # the constant-base fold.  Map each am2_x symbol to the VA its ADDR_X names.
    gv = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+"
                    r"\(\(uintptr_t\)\(const (?:void|char) \*\)&?(am2_[A-Za-z0-9_]+)")
    ng = 0
    for line in open(os.path.join(BM.REPO, "src/inject/standalone.h")):
        m = gv.match(line.strip())
        if m and m.group(1) in addr:
            sym_va.setdefault("_" + m.group(2), addr[m.group(1)])
            ng += 1
    # am2_origstate is the single struct reserving the whole .bss working region
    # at ORIGBSS; a member redirect (ADDR_X -> &am2_origstate.member) compiles to
    # a reloc against am2_origstate whose ADDEND is the member offset, so the base
    # symbol resolves to ORIGBSS (not the member VA the gv loop guessed above).
    sym_va["_am2_origstate"] = 0x0048E000
    if "--unresolved" in sys.argv:
        sys.stderr.write("migrated-global symbols mapped: %d\n" % ng)
    imap = import_map()         # base import name -> IAT slot VA
    compiled = {}               # file -> (secs, symlist, names, base)
    with tempfile.TemporaryDirectory() as od, tempfile.TemporaryDirectory() as odb:
        for f in sorted(byfile):
            if os.path.basename(f) in BM.SKIP_FILE:
                continue
            obj = BM.compile_file(f, od, extra=["/DAM2_STANDALONE", "/DAM2_RELINK"])
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
                sym = resolve_member_first(names, fn)
                if sym:
                    sym_va[sym] = va
        # Fallback for installed functions whose defining TU is SKIP'd (trig.cpp,
        # crt.cpp -- Cos8/Sin8/... ) or otherwise not compiled here: a C-linkage
        # caller references `_fn`, so map that from the manifest VA.  setdefault
        # never overrides a symbol a compiled definer already placed.
        for f, fn, va in man:
            sym_va.setdefault("_" + fn, va)

        # ---- discovery: learn the VAs of static/un-installed reconstructed
        # helpers (CloseCurrentScreen, OpenScreen, ...) that have no ADDR.  An
        # installed caller's SHIPPED bytes already hold the e8 to the helper at
        # the call site, so read the target straight out of the image.  A wrong
        # guess cannot create a false match -- the final byte compare rejects it
        # -- so this is safe.  Iterate to a fixpoint (a discovered helper may
        # itself call another).  Guard on the e8 opcode actually being there.
        def discover():
            found = 0
            for f in sorted(byfile):
                if f not in compiled:
                    continue
                secs, symlist, names, _ = compiled[f]
                for fn, va in byfile[f]:
                    sym = resolve_member_first(names, fn)
                    if not sym:
                        continue
                    secnum, value = names[sym]
                    sec = secs[secnum - 1]
                    n = len(sec["data"])
                    for off, symidx, typ in sec["rels"]:
                        if typ != IMAGE_REL_I386_REL32:
                            continue
                        roff = off - value
                        if not (0 <= roff < n):
                            continue
                        tname = symlist[symidx][0]
                        if tname in sym_va:
                            continue
                        site = va + roff
                        if img.read(site - 1, 1) != b"\xe8":
                            continue
                        rel = struct.unpack("<i", img.read(site, 4))[0]
                        tgt = (site + 4 + rel) & 0xffffffff
                        if 0x00401000 <= tgt < 0x0048e000:   # .text range
                            sym_va[tname] = tgt
                            found += 1
            return found
        rounds = 0
        while discover() and rounds < 8:
            rounds += 1

        # ---- pass 2: resolve and compare, holding the .objs in memory.
        match = diff = 0
        unresolved_hist = collections.Counter()
        diffs = []
        newly = []              # relink matches that the piecewise harness misses
        regressed = []
        strcache = {}           # string constant symbol -> image VA (or None)
        for f in sorted(byfile):
            if f not in compiled:
                continue
            secs, symlist, names, base = compiled[f]
            for fn, va in byfile[f]:
                sym = resolve_member_first(names, fn)
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
                        tgt_va = string_va(tgt_name, secs, symlist, symidx,
                                           img, strcache)
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
                    if piece_ok:
                        regressed.append((fn, va, os.path.basename(f)))
                    diff += 1
                    nd = sum(1 for i in range(n)
                             if not wildcard[i] and cur[i] != orig[i])
                    diffs.append((nd, fn, va, os.path.basename(f)))

    print("=== relink byte-match (siblings + CRT + IAT + migrated globals resolved) ===")
    print("  match %d   diff %d   (relink closes %d the piecewise harness cannot)"
          % (match, diff, len(newly)))
    if regressed:
        print("  REGRESSED (matched piecewise, not under relink): %d" % len(regressed))
        for fn, va, f in sorted(regressed, key=lambda x: x[1])[:20]:
            print("  -  %-26s 0x%08x %s" % (fn, va, f))
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
