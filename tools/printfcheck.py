#!/usr/bin/env python3
"""Check crt_sprintf -- _output and the float conversions under it -- against
the original's, over a corpus of formats and values.

sprintf at 0x00464CE2 is run under Unicorn with its format and its
arguments in the scratch page and its output buffer beside them, and what
it wrote is recorded; tests/selftest.cpp replays every row through the C.
The corpus is the conversions, flags, widths and precisions _output's
state machine distinguishes, and for %f, %e and %g the values that decide
rounding: halves at every precision, nines that carry, denormals, the
extremes, an infinity and a NaN -- the digits of "%6.2f" are how the
script layer turns a float into text, so 2.675 and 1.005 are in it.

    tools/printfcheck.py --emit tests/printfvec.h

CHECKS names the engine and the float conversion under it; neither has a
counter, since nothing patches the CRT.

MUTATION-CHECKED. Rounding the digit string only above '5' fails 86 rows,
moving the decimal-exponent estimate by one fails 615, giving %p a
precision of 6 fails 10, and printing "0x" for a zero fails 4. Two do NOT
fail: taking a power-of-ten entry one below at a guard word of exactly
0x8000 rather than above it is a theorem, since no entry in either table
has that guard word; and rounding the multiply up at an exact half with
the bit above clear is a GAP -- no value here lands its product on that
tie. Say which is which.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, SCRATCH_SZ

CHECKS = ("crt_output", "crt_cfltcvt")

SPRINTF = 0x00464CE2
OUT_AT = SCRATCH + 0x100          # the buffer sprintf writes
FMT_AT = SCRATCH + 0x1000         # the format
STR_AT = SCRATCH + 0x1100         # a string argument

INTS = [0, 1, -1, 7, 42, -42, 255, 256, 1000, -1000, 65535, 65536, 0x7FFFFFFF,
        -0x80000000, 0xDEADBEEF - (1 << 32), 12345678, -12345678]
STRS = ["", "a", "hello", "Army Men II", "green", "a longer string than the width"]
FLOATS = [0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, 0.25, 0.125, 0.1, 0.2, 0.3, 4.6, 2.4,
          2.675, 1.005, 0.005, 0.015, 0.025, 0.045, 0.995, 9.995, 99.995, 999.995,
          123.456, 0.000123456, 999999.5, 9999999.0, 1e-5, 1e-7, 1e20, 1e21, 1e-20,
          3.14159265358979, 2.718281828459045, 1e15, 1.5e15, 123456789.0,
          0.1 + 0.2, 1.0 / 3.0, 2.0 / 3.0, 100.0, 1e100, 1e-100, 1.7976931348623157e308,
          2.2250738585072014e-308, 4.9e-324, -2.5, -0.005, -999.995, 12.345, 0.0004999999999]
INT_FORMATS = ["%d", "%i", "%u", "%x", "%X", "%o", "%5d", "%-5d|", "%05d", "%+d", "% d",
               "%#x", "%#X", "%#o", "%.3d", "%8.3d", "%-8.3d|", "%08x", "%hd", "%ld", "%lu",
               "%c", "%3c", "%-3c|", "%p", "%.0d", "%10u", "%I64d"]
STR_FORMATS = ["%s", "%10s", "%-10s|", "%.3s", "%8.2s", "%.0s|", "%-.4s|"]
FLOAT_FORMATS = ["%f", "%.0f", "%.1f", "%.2f", "%6.2f", "%.3f", "%10.4f", "%-10.2f|",
                 "%010.3f", "%+.2f", "%#.0f", "%e", "%.2e", "%E", "%.0e", "%12.3e",
                 "%g", "%.1g", "%.3g", "%.10g", "%G", "%#g", "%.17g", "%.0g"]
MISC = [("%%", ()), ("%d%%", (7,)), ("[%5s|%-5d|%05.1f]", ("ab", 3, 2.5)),
        ("%s is %d and %.2f", ("x", 1, 0.5)), ("%.*f", (3, 2.5)), ("%*d", (6, 42)),
        ("%-*d|", (6, 42)), ("%z", ()), ("%q", (1,)), ("abc", ()), ("", ())]


def run(emu, fmt, args):
    buf = bytearray(SCRATCH_SZ)
    fb = fmt.encode("latin-1") + b"\0"
    buf[FMT_AT - SCRATCH:FMT_AT - SCRATCH + len(fb)] = fb
    call = [OUT_AT, FMT_AT]
    sat = STR_AT
    for a in args:
        if isinstance(a, float):
            lo, hi = struct.unpack("<II", struct.pack("<d", a))
            call += [lo, hi]
        elif isinstance(a, str):
            sb = a.encode("latin-1") + b"\0"
            buf[sat - SCRATCH:sat - SCRATCH + len(sb)] = sb
            call.append(sat)
            sat += 0x40
        else:
            call.append(a & 0xFFFFFFFF)
    eax, after = emu.call(SPRINTF, call, bytes(buf), count=2000000)
    if after is None:
        return None, None
    out = after[OUT_AT - SCRATCH:OUT_AT - SCRATCH + 0x200]
    return eax, out[:out.index(b"\0")].decode("latin-1")


def corpus():
    rows = []
    for f in INT_FORMATS:
        for v in INTS:
            rows.append((f, (v,)))
    for f in STR_FORMATS:
        for v in STRS:
            rows.append((f, (v,)))
    for f in FLOAT_FORMATS:
        for v in FLOATS:
            rows.append((f, (v,)))
    inf = float("inf")
    nan = float("nan")
    for f in ["%f", "%e", "%g", "%.2f"]:
        for v in (inf, -inf, nan, -nan):
            rows.append((f, (v,)))
    rows += MISC
    return rows


def c_string(s):
    out = ""
    for ch in s:
        if ch == '"' or ch == "\\":
            out += "\\" + ch
        elif 0x20 <= ord(ch) < 0x7F:
            out += ch
        else:
            out += "\\%03o" % ord(ch)
    return '"' + out + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit")
    args = ap.parse_args()
    emu = Emu()
    # The float conversion table is filled at startup by _cfltcvt_init at
    # 0x00464456; the file's image still points every entry at _fptrap, so
    # run the initialiser once before anything formats a double.
    emu.call(0x00464456, [], bytes(SCRATCH_SZ))
    rows = []
    faults = 0
    for fmt, argv in corpus():
        rc, text = run(emu, fmt, argv)
        if text is None:
            faults += 1
            print("  %r %r: the original FAULTED" % (fmt, argv))
            continue
        rows.append((fmt, argv, rc, text))
    print("printfcheck: %d rows, %d faults" % (len(rows), faults))
    if args.emit:
        lines = ["/* Generated by tools/printfcheck.py -- do not edit.",
                 " *",
                 " * What the ORIGINAL sprintf at 0x00464CE2 wrote for each format and",
                 " * argument list, and what it answered. Arguments travel as up to",
                 " * four dwords: an int as itself, a string as an index into",
                 " * am2_printf_strings, a double as its two halves, kind 2. */",
                 "typedef struct { const char *fmt; int32_t nargs; int32_t kind[4]; uint32_t a[4];",
                 "                 int32_t rc; const char *want; } AM2_PrintfCase;"]
        strs = []
        def sidx(s):
            if s not in strs:
                strs.append(s)
            return strs.index(s)
        cases = []
        for fmt, argv, rc, text in rows:
            kinds, vals = [], []
            for a in argv:
                if isinstance(a, float):
                    lo, hi = struct.unpack("<II", struct.pack("<d", a))
                    kinds += [2, 3]
                    vals += [lo, hi]
                elif isinstance(a, str):
                    kinds.append(1)
                    vals.append(sidx(a))
                else:
                    kinds.append(0)
                    vals.append(a & 0xFFFFFFFF)
            while len(kinds) < 4:
                kinds.append(0)
                vals.append(0)
            cases.append("    { %s, %d, { %s }, { %s }, %d, %s }," % (
                c_string(fmt), len(argv) + sum(1 for a in argv if isinstance(a, float)),
                ", ".join(str(k) for k in kinds), ", ".join("0x%08xu" % v for v in vals),
                rc, c_string(text)))
        lines.append("static const char *const am2_printf_strings[] = {")
        lines += ["    %s," % c_string(s) for s in strs] or ['    "",']
        lines.append("};")
        lines.append("static const AM2_PrintfCase am2_printf_cases[] = {")
        lines += cases
        lines.append("};")
        text = "\n".join(lines) + "\n"
        if not (os.path.exists(args.emit) and open(args.emit).read() == text):
            open(args.emit, "w").write(text)
        print("emitted %s" % args.emit)
    return 1 if faults else 0


if __name__ == "__main__":
    sys.exit(main())
