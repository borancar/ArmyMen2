#!/usr/bin/env python3
"""Verify every global structure transcribed out of the carried data blob.

The native/standalone builds carry the original's `.rdata`/`.data` at its own
VAs and read each global out of it; the migration (STATUS.md's MIGRATION note)
carves a table at a time into hand-written, typed C and redirects the `ADDR_*`
macro at the C symbol under AM2_STANDALONE. A transcribed table is right ONLY
if its bytes equal the image's -- and nothing checked that, so a mistyped value
or a wrong length would compile, pass every other check, and diverge only in
play. This is that check, and it is exact: it packs each C definition and
compares it to `am2.Image().read(addr)`.

It hand-keeps NO list. The pairing (macro -> C symbol) is read from the
AM2_STANDALONE redirects in `src/inject/standalone.h`; the macro's ADDRESS from
`src/inject/orig.h`; the VALUES and element TYPE from the C definition in
`src/`. So adding a transcription -- a redirect plus a definition -- extends
what this covers automatically, the way `checkpatches.py` grows with the patch
list.

What it does NOT cover: a redirect whose target is a SLICE of another array
(`&am2_weapon_pose_frames[53]`, the AI-move aliases) -- the parent array is
checked whole, and the slice is address arithmetic the compiler settles. Those
are reported as skipped by name, not silently dropped.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SA = os.path.join(ROOT, "src", "inject", "standalone.h")
ORIG = os.path.join(ROOT, "src", "inject", "orig.h")

# element size and struct code for each type the transcriptions use
TYPES = {
    "uint8_t":  ("<B", 1), "int8_t":  ("<b", 1),
    "uint16_t": ("<H", 2), "int16_t": ("<h", 2),
    "uint32_t": ("<I", 4), "int32_t": ("<i", 4),
    "float":    ("<f", 4), "double":  ("<d", 8),
}

# A redirect points ADDR_X at a transcribed symbol:
#   #define ADDR_X ((uintptr_t)(const void *)am2_sym)
#   #define ADDR_X ((uintptr_t)(const void *)&am2_sym)          (scalar)
#   #define ADDR_X ((uintptr_t)(const void *)&am2_sym[53])      (alias slice)
REDIRECT = re.compile(
    r"#define\s+(ADDR_\w+)\s+\(\(uintptr_t\)\(const void \*\)\s*&?"
    r"(am2_\w+)(\[(\d+)\])?\)")


def orig_addresses():
    out = {}
    for m in re.finditer(r"^#define\s+(ADDR_\w+)\s+0x([0-9A-Fa-f]+)u?",
                         open(ORIG).read(), re.M):
        out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def reconstruction_map(addrs):
    """{original .text address -> reconstructed function name}, from the
    patch_replace calls. Lets a function-pointer table be verified: the
    image's dword is the ORIGINAL function address, and the C entry names the
    reconstruction that replaces it."""
    import glob
    pat = re.compile(r"patch_replace\(\s*(ADDR_\w+)\s*,\s*"
                     r"\(const void \*\)\s*&?(\w+)")
    out = {}
    for p in (glob.glob(os.path.join(ROOT, "src/game/*.cpp"))
              + glob.glob(os.path.join(ROOT, "src/game/win32/*.cpp"))):
        for macro, fn in pat.findall(open(p).read()):
            a = addrs.get(macro)
            if a is not None:
                out[a] = fn
    return out


def _split_top_commas(s):
    """Split on commas that are NOT inside a double-quoted string, so a name
    like "NUM ," or the "," key is one field. A plain str.split(",") tore those
    in half and mis-sized the table."""
    out, buf, i = [], "", 0
    while i < len(s):
        c = s[i]
        if c == '"':
            buf += c
            i += 1
            while i < len(s):
                buf += s[i]
                if s[i] == '\\' and i + 1 < len(s):
                    buf += s[i + 1]
                    i += 2
                    continue
                if s[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == ',':
            out.append(buf)
            buf = ""
            i += 1
            continue
        buf += c
        i += 1
    out.append(buf)
    return out


def find_definition(symbol):
    """(type, values-as-python-numbers) for a transcribed C symbol, or None.

    Reads the `extern "C" const <type> <sym>[<n>] = { ... };` or
    `... const <type> <sym> = <literal>;` definition out of src/. The type is
    the declared element type; the values are parsed as numbers (ints from an
    array initializer, one float/double from a scalar).
    """
    for base, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for f in files:
            if not f.endswith((".cpp", ".c", ".inc")):
                continue
            text = open(os.path.join(base, f)).read()
            # char* array: const char *NAME[N] = { "a", "b", 0, ... };
            # its POINTER values legitimately differ from the image's, so it is
            # verified by dereference (see main), not by byte packing.
            m = re.search(r'const\s+char\s*\*\s*(?:const\s+)?' +
                          re.escape(symbol) +
                          r'\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\};', text, re.S)
            if m:
                body = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', m.group(1), flags=re.S)
                lits = []
                for tok in re.finditer(r'"((?:[^"\\]|\\.)*)"|\b(?:0|NULL)\b',
                                       body):
                    lits.append(tok.group(1) if tok.group(1) is not None
                                else None)
                return "char*", lits
            # array (scalar element type) OR a struct/fn-pointer table
            m = re.search(r'const\s+(\w+)\s+' + re.escape(symbol) +
                          r'\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\};', text, re.S)
            if m:
                # strip comments from the initializer first -- an address in a
                # /* 0x... */ note inside the braces is not a value
                body = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', m.group(2), flags=re.S)
                if m.group(1) in TYPES:
                    # hex or decimal; the (?<![\w.]) skips digits embedded in an
                    # identifier, e.g. the "32" of a (int32_t) cast.
                    toks = re.findall(
                        r'(?<![\w.])-?0[xX][0-9A-Fa-f]+|(?<![\w.])-?\d+', body)
                    return m.group(1), [int(t, 0) for t in toks]
                # a struct / function-pointer table: flatten the braces and
                # read one token per dword -- an integer, or a function name.
                flat = body.replace("{", " ").replace("}", " ")
                toks = []
                for el in _split_top_commas(flat):
                    el = el.strip()
                    if not el:
                        continue
                    s = re.search(r'"((?:[^"\\]|\\.)*)"', el)
                    num = re.fullmatch(r'-?0[xX][0-9A-Fa-f]+u?|-?\d+u?|NULL', el)
                    if s:
                        toks.append(("str", s.group(1)))   # string-pointer field
                    elif num:
                        toks.append(("int", 0 if el == "NULL"
                                     else int(el.rstrip("uU"), 0)))
                    else:
                        # the last identifier -- ignore any (cast) or & prefix
                        ids = re.findall(r'[A-Za-z_]\w*', el)
                        toks.append(("fn", ids[-1] if ids else el))
                return "mixed", toks
            # scalar
            m = re.search(r'const\s+(\w+)\s+' + re.escape(symbol) +
                          r'\s*=\s*([^;,]+?)[fF]?\s*;', text)
            if m:
                return m.group(1), [float(m.group(2))]
    return None


def pack(typ, values):
    code, size = TYPES[typ]
    if typ in ("float", "double"):
        return b"".join(struct.pack(code, float(v)) for v in values)
    # pack integers by their BYTES: mask to width and emit unsigned, so a
    # signed literal (-1), an unsigned one (0xFFFFFFFFu) and a cast
    # ((int32_t)0x80000000) all produce the right bytes.
    ucode = {1: "<B", 2: "<H", 4: "<I"}[size]
    mask = (1 << (size * 8)) - 1
    return b"".join(struct.pack(ucode, int(v) & mask) for v in values)


def _decode_c(lit):
    """A C string literal's byte content (the escapes escape() emits, plus \\0)."""
    out, i = bytearray(), 0
    while i < len(lit):
        if lit[i] != "\\":
            out.append(ord(lit[i])); i += 1; continue
        c = lit[i + 1]
        simple = {"n": 10, "t": 9, "r": 13, "\\": 92, '"': 34, "0": 0}
        if c in "01234567":
            j = i + 1
            while j < len(lit) and j < i + 4 and lit[j] in "01234567":
                j += 1
            out.append(int(lit[i + 1:j], 8)); i = j
        elif c in simple:
            out.append(simple[c]); i += 2
        else:
            out.append(ord(c)); i += 2
    return bytes(out)


def _image_cstr(img, ptr):
    b = img.read(ptr, 256)
    n = b.find(b"\0")
    return b[:n] if n >= 0 else b


def verify_mixed(img, addr, tokens, recon):
    """A struct / function-pointer table: each dword is either an int (byte-
    compared) or a function pointer. For a pointer, the image holds the
    ORIGINAL function address, so it must appear in the reconstruction map and
    name the same reconstruction the C entry does."""
    TEXT_LO, TEXT_HI = 0x00401000, 0x0046F000
    for i, (kind, val) in enumerate(tokens):
        dw = struct.unpack("<I", img.read(addr + 4 * i, 4))[0]
        if kind == "fn":
            if dw not in recon:
                return ("entry %d: C names %s but image 0x%08X is not a "
                        "reconstructed function" % (i, val, dw))
            if recon[dw] != val:
                return ("entry %d: C names %s but image 0x%08X is %s"
                        % (i, val, dw, recon[dw]))
        elif kind == "str":            # a string-pointer field: dereference
            if not (0x0046F000 <= dw < 0x00700000):
                return "entry %d: C is a string but image 0x%08X is not a ptr" \
                       % (i, dw)
            want, got = _decode_c(val), _image_cstr(img, dw)
            if want != got:
                return "entry %d: C %r != image string %r" % (i, want, got)
        else:
            if TEXT_LO <= dw < TEXT_HI:
                return ("entry %d: C is int %d but image 0x%08X is a "
                        "function pointer" % (i, val, dw))
            if (val & 0xFFFFFFFF) != dw:
                return ("entry %d: C int 0x%08X != image 0x%08X"
                        % (i, val & 0xFFFFFFFF, dw))
    return None


def verify_strptr(img, addr, lits):
    """A char* array is right when each entry points at the string the C
    literal spells. Compare by DEREFERENCE, not by pointer value."""
    for i, lit in enumerate(lits):
        ptr = struct.unpack("<I", img.read(addr + 4 * i, 4))[0]
        if lit is None:
            if ptr != 0:
                return "entry %d: C NULL but image 0x%08X" % (i, ptr)
            continue
        if not (GAP_LO <= ptr < 0x00700000):
            return "entry %d: image ptr 0x%08X not a string" % (i, ptr)
        want, got = _decode_c(lit), _image_cstr(img, ptr)
        if want != got:
            return "entry %d: C %r != image %r" % (i, want, got)
    return None


GAP_LO = 0x00401000


def check_origstate(addrs):
    """src/game/origstate.cpp lays the .bss working memory out as one C struct
    placed at 0x0048E000, each member sized to land at its original VA. The
    member offsets are PINNED by static_assert(offsetof(...) == VA - base) --
    the COMPILER enforces those, so a mis-sized (e.g. newly typed) member fails
    the build. Here we verify the asserts themselves are honest: there is one
    per .bss global naming its true VA, and the total-size assert is right.
    This stays valid however the members are typed."""
    SPLIT, HI = 0x0048E000, 0x00666000
    path = os.path.join(ROOT, "src", "game", "origstate.cpp")
    if not os.path.exists(path):
        return None
    text = open(path).read()
    # the .bss globals orig.h names, that the struct must pin
    want = {a for n, a in addrs.items() if SPLIT <= a < HI}
    # some ADDR_ names alias one address; the struct has one member per address
    asserted = set()
    for m in re.finditer(r'offsetof\(struct AM2_OrigState,\s*\w+\)\s*==\s*'
                         r'0x([0-9A-Fa-f]+)u?\s*-\s*0x0048E000u?', text):
        asserted.add(int(m.group(1), 16))
    errs = []
    missing = want - asserted
    if missing:
        errs.append("no offsetof pin for VA(s): "
                    + ", ".join("0x%08X" % a for a in sorted(missing)[:8]))
    if not re.search(r'sizeof\(struct AM2_OrigState\)\s*==\s*0x00666000u?\s*-\s*'
                     r'0x0048E000u?', text):
        errs.append("missing/!= total-size assert (0x00666000 - 0x0048E000)")
    return errs


def main():
    addrs = orig_addresses()
    recon = reconstruction_map(addrs)
    img = am2.Image()
    sa = open(SA).read()

    checked = skipped = 0
    fail = []
    for m in REDIRECT.finditer(sa):
        macro, symbol, _br, index = m.group(1), m.group(2), m.group(3), m.group(4)
        if index is not None:
            skipped += 1                      # a slice of another array
            print("  skip  %-26s -> &%s[%s]" % (macro, symbol, index))
            continue
        addr = addrs.get(macro)
        if addr is None:
            fail.append("%s: no address in orig.h" % macro)
            continue
        found = find_definition(symbol)
        if found is None:
            fail.append("%s: no C definition of %s found" % (macro, symbol))
            continue
        typ, values = found
        if typ == "mixed":
            bad = verify_mixed(img, addr, values, recon)
            if bad:
                fail.append("%s (fn-ptr table, 0x%08X): %s"
                            % (symbol, addr, bad))
            else:
                checked += 1
                print("  ok    %-26s fn-table[%d] @0x%08X"
                      % (macro, len(values), addr))
            continue
        if typ == "char*":
            bad = verify_strptr(img, addr, values)
            if bad:
                fail.append("%s (char*[], 0x%08X): %s" % (symbol, addr, bad))
            else:
                checked += 1
                print("  ok    %-26s char*[%d] @0x%08X (deref)"
                      % (macro, len(values), addr))
            continue
        if typ not in TYPES:
            fail.append("%s: unknown element type %s" % (symbol, typ))
            continue
        got = pack(typ, values)
        want = img.read(addr, len(got))
        if got != want:
            fail.append("%s (%s, 0x%08X): C %s != image %s"
                        % (symbol, typ, addr, got.hex(), want.hex()))
        else:
            checked += 1
            print("  ok    %-26s %s[%d] @0x%08X"
                  % (macro, typ, len(values), addr))

    os_errs = check_origstate(addrs)
    if os_errs:
        fail += ["origstate.cpp: " + e for e in os_errs]
    elif os_errs is not None:
        print("  ok    origstate.cpp .bss struct pins every member at its VA "
              "(offsetof asserts, compiler-enforced)")

    if fail:
        print("\ncheckimagedata: FAIL")
        for f in fail:
            print("  " + f)
        return 1
    print("\ncheckimagedata: %d transcribed globals match the image, "
          "%d slice-aliases skipped" % (checked, skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
