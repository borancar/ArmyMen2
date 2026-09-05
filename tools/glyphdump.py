#!/usr/bin/env python3
"""Read the game's built fonts out of a running ORIGINAL and keep them as
the reference for the platform layer's text.

The game does not ship fonts. BuildFont draws each of the 224 printable
characters of a face with GDI's TextOutA into a scratch surface and
run-length encodes whatever is not the background -- so a font in memory
IS GDI's rendering of that face, one bit per pixel, and nothing else the
game draws as text comes from anywhere else. Three fonts exist
(ADDR_FONT_DESCS): ArialNarrow 12 and 14, ArialBlack 18. Slots 1 and 2 are
built at the title screen, slot 0 when a mission starts.

The platform renders those faces with stb_truetype, and stb_truetype does
not rasterise like Wine's FreeType: the same 640x480 dialog differs from
the Wine frame by about 1,400 pixels and every one of them is text. So the
reference is taken from the original under Wine -- the process the A/B
suite already runs -- and src/platform/gdi32.cpp replays it: a CreateFontA
naming one of the three faces gets the recorded bitmaps back from
TextOutA and their sizes from GetTextExtentPoint32A, glyph for glyph.

    tools/glyphdump.py --port 31436 --merge tests/glyphs-reference.txt \\
                       --out tests/glyphs-reference.txt
    tools/glyphdump.py --emit src/platform/glyphs.inc

The reference file is text, one font header and one line per glyph, so a
change in Wine's rendering shows as a diff. --emit turns it into the C
tables gdi32.cpp includes, deterministically, so the .inc can be checked
for drift like the other generated files.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2ctl

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FONT_RECORD = 0x006598D0        # {u32 size; u16 offsets[256]; u8 *base} x 3
FONT_STRIDE = 524
FONT_DESCS = 0x004897E8         # {const char *face; i32 height; u16 style}
FONT_COUNT = 3
CHUNK = 96


def read(ctl, addr, size):
    out = bytearray()
    while len(out) < size:
        n = min(CHUNK, size - len(out))
        reply = ctl.send("dump %x %d" % (addr + len(out), n))
        if not reply.startswith("ok "):
            raise SystemExit("dump failed at %#x: %s" % (addr + len(out), reply))
        out += bytes.fromhex(reply.split()[2])
    return bytes(out[:size])


def read_cstr(ctl, addr):
    out = b""
    while True:
        chunk = read(ctl, addr + len(out), 32)
        if b"\0" in chunk:
            return (out + chunk[:chunk.index(b"\0")]).decode("latin-1")
        out += chunk


def decode_glyph(blob):
    """An AM2_Rle16 coverage glyph into (width, height, rows of 0/1)."""
    width, height = struct.unpack_from("<HH", blob, 0)
    rows = []
    for r in range(height):
        off, = struct.unpack_from("<H", blob, 4 + 2 * r)
        row = []
        p = off
        while len(row) < width:
            bg, fg = blob[p], blob[p + 1]
            p += 2
            row += [0] * bg + [1] * fg
            if bg == 0 and fg == 0:
                break
        rows.append(row[:width])
    return width, height, rows


def dump_fonts(port):
    ctl = am2ctl.Control(port=port)
    fonts = {}
    for f in range(FONT_COUNT):
        rec = FONT_RECORD + f * FONT_STRIDE
        size, = struct.unpack("<I", read(ctl, rec, 4))
        if size == 0:
            continue
        offsets = struct.unpack("<256H", read(ctl, rec + 4, 512))
        base, = struct.unpack("<I", read(ctl, rec + 4 + 512, 4))
        face_ptr, height, style = struct.unpack("<IiH", read(ctl, FONT_DESCS + f * 12, 10))
        face = read_cstr(ctl, face_ptr)
        data = read(ctl, base, size)
        glyphs = {}
        for ch in range(0x20, 0x100):
            start = offsets[ch]
            end = offsets[ch + 1] if ch < 0xFF else size
            glyphs[ch] = decode_glyph(data[start:end])
        fonts[(face, height, style)] = glyphs
        print("font %d: %s %d style %#x, %d bytes, %d glyphs" % (f, face, height, style, size, len(glyphs)))
    ctl.close()
    return fonts


def load(path):
    fonts = {}
    cur = None
    if not os.path.exists(path):
        return fonts
    for line in open(path):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "font":
            cur = (parts[1], int(parts[2]), int(parts[3], 0))
            fonts[cur] = {}
        elif parts[0] == "glyph":
            ch, w, h = int(parts[1], 16), int(parts[2]), int(parts[3])
            rows = [[1 if c == "#" else 0 for c in r] for r in parts[4:4 + h]] if h else []
            fonts[cur][ch] = (w, h, rows)
    return fonts


def save(path, fonts):
    with open(path, "w") as fh:
        fh.write("# GDI's rendering of the game's three fonts, read out of the original\n"
                 "# under Wine by tools/glyphdump.py. One line per glyph: code, width,\n"
                 "# height, then one word per row with # for ink.\n")
        for key in sorted(fonts):
            face, height, style = key
            fh.write("font %s %d %#x\n" % (face, height, style))
            for ch in sorted(fonts[key]):
                w, h, rows = fonts[key][ch]
                words = ["".join("#" if b else "." for b in r) or "." for r in rows]
                fh.write("glyph %02x %d %d %s\n" % (ch, w, h, " ".join(words)))


def emit(path, fonts):
    lines = ["/* Generated by tools/glyphdump.py from tests/glyphs-reference.txt --",
             " * do not edit. See the tool's docstring. */",
             ""]
    recs = []
    bits = []
    font_lines = []
    for key in sorted(fonts):
        face, height, style = key
        first = len(recs)
        for ch in sorted(fonts[key]):
            w, h, rows = fonts[key][ch]
            stride = (w + 7) // 8
            recs.append((ch, w, h, len(bits)))
            for r in rows:
                row = r + [0] * (stride * 8 - len(r))
                for b in range(stride):
                    byte = 0
                    for i in range(8):
                        byte |= row[b * 8 + i] << (7 - i)
                    bits.append(byte)
        font_lines.append('    { "%s", %d, %d, %d, %d },' % (face, height, style, first, len(recs) - first))
    lines.append("static const AM2_GlyphFont am2_glyph_fonts[] = {")
    lines += font_lines
    lines.append("};")
    lines.append("static const AM2_Glyph am2_glyphs[] = {")
    for ch, w, h, off in recs:
        lines.append("    { 0x%02x, %d, %d, %d }," % (ch, w, h, off))
    lines.append("};")
    lines.append("static const uint8_t am2_glyph_bits[] = {")
    for i in range(0, len(bits), 24):
        lines.append("    " + " ".join("0x%02x," % b for b in bits[i:i + 24]))
    if not bits:
        lines.append("    0,")
    lines.append("};")
    text = "\n".join(lines) + "\n"
    if os.path.exists(path) and open(path).read() == text:
        return
    open(path, "w").write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=None, help="dump the fonts a running original has built")
    ap.add_argument("--merge", default=os.path.join(REPO, "tests", "glyphs-reference.txt"))
    ap.add_argument("--out", default=None)
    ap.add_argument("--emit", default=None, help="write the C tables here")
    args = ap.parse_args()

    # No arguments: regenerate the C tables from the reference, which is what
    # `make check` runs to catch drift between the two.
    if not args.port and not args.emit:
        args.emit = os.path.join(REPO, "src", "platform", "glyphs.inc")

    fonts = load(args.merge)
    if args.port:
        new = dump_fonts(args.port)
        for key, glyphs in new.items():
            if key in fonts and fonts[key] != glyphs:
                print("font %s %d differs from the reference already held; replacing" % key[:2])
            fonts[key] = glyphs
        save(args.out or args.merge, fonts)
        print("%d fonts in %s" % (len(fonts), args.out or args.merge))
    if args.emit:
        emit(args.emit, fonts)
        print("emitted %s: %d fonts, %d glyphs" % (args.emit, len(fonts), sum(len(g) for g in fonts.values())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
