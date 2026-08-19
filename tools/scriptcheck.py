#!/usr/bin/env python3
"""Differential-test the script tokeniser against the words the game ships.

The mission scripts are plain text on disk, so this subsystem can be checked
against real input rather than against synthetic vectors -- and real input is
the better test here: a random 32-bit integer says nothing about a function
whose argument is a keyword. Every distinct word in every shipped .txt goes
through the ORIGINAL under Unicorn, and the answers land in tests/scriptvec.h
for tests/selftest.cpp to replay against the reconstruction.

No part of the game runs. The token table lives in .data, which is mapped from
the file, so the original tokeniser is fully self-contained here.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH, SCRATCH_SZ, SCRATCH_PATTERN
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

GAME_DIR = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), ".wine/drive_c/GOG Games/Army Men II")
ADDR_LOOKUP_TOKEN = 0x0043EEE0
ADDR_NEXT_TOKEN = 0x0043F450
ADDR_ADD_TOKEN = 0x0043F370
LINE_AT = 0x1000          # where the line under test goes, inside SCRATCH
CTX_AT = 0x0800           # a dummy context; AddToken never runs, so it is
                          # never touched -- only its address is observed
OUT = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tests/scriptvec.h")

# The tokeniser splits on IsScriptDelim and blanks; this is the same split done
# crudely, which is all that is needed to harvest candidate words.
WORD = re.compile(rb"[^\s(),<>={}&+\"]+")


def cstr(b):
    """A C string literal for arbitrary bytes.

    Octal rather than hex, because \\x is greedy in C: "\\x41" followed by a
    digit is one very large escape, and the script text has digits after
    non-ASCII bytes in at least one file.
    """
    out = []
    for ch in b:
        if ch == 0x22 or ch == 0x5C:
            out.append("\\" + chr(ch))
        elif 0x20 <= ch < 0x7F:
            out.append(chr(ch))
        else:
            out.append("\\%03o" % ch)
    return "".join(out)


def script_files():
    out = []
    for root, _dirs, names in os.walk(GAME_DIR):
        for n in sorted(names):
            if n.lower().endswith(".txt"):
                out.append(os.path.join(root, n))
    return sorted(out)


def harvest():
    """Every distinct word the shipped scripts contain, plus the delimiters.

    Lower-cased, because the tokeniser lower-cases before looking up and the
    table is lower case throughout -- feeding it `Undeploy` would test the
    caller's _strlwr, which is CRT and not ours.
    """
    words = set()
    for path in script_files():
        with open(path, "rb") as fh:
            for line in fh:
                line = line.split(b"//")[0]
                for m in WORD.finditer(line):
                    w = m.group(0).lower()
                    if 0 < len(w) < 0x40:
                        words.add(w)
    # The one- and two-character operator tokens never survive the word split,
    # and they are ids 1..13 -- the half of the table that would otherwise go
    # untested. NextToken builds them a character at a time and looks them up
    # through this same function.
    for op in (b"(", b")", b",", b"<", b"<=", b"=", b">", b">=", b"<>",
               b"{", b"}", b"&", b"+"):
        words.add(op)
    # A word that is not a keyword must come back -1, and the scripts are full
    # of those already (object names, numbers). One deliberate near-miss per
    # keyword is cheap and tests the walk running off the end.
    words.add(b"")

    # Case variants, because without them the corpus cannot see the difference.
    # Every word above is lower-cased, and lower case is what the table holds,
    # so a reconstruction that compared case-INSENSITIVELY passed all 9,327 --
    # measured, by making exactly that change. The original compares raw bytes
    # and its caller lower-cases first, so `Undeploy` here must come back -1;
    # these are what proves it.
    for w in list(words):
        if w.isalpha():
            words.add(w.capitalize())
            words.add(w.upper())
    return sorted(words)


def token_stream(emu, line, lineno):
    """The tokens the ORIGINAL NextToken emits for one line.

    AddToken is not executed. It reaches the game's malloc, which reaches
    HeapAlloc, which is an import and does not exist here -- and running it
    would only rebuild a list this already has. Hooking its entry, reading the
    four cdecl arguments off the stack and returning gives exactly the stream,
    which is the thing being compared.
    """
    uc = emu.uc
    out = []

    def at_add_token(u, addr, size, _user):
        if addr != ADDR_ADD_TOKEN:
            return
        esp = u.reg_read(UC_X86_REG_ESP)
        ret, _ctx, kind, value, ln = struct.unpack(
            "<IIiIi", bytes(u.mem_read(esp, 20)))
        if kind == 5:
            text = bytearray()
            while True:
                b = u.mem_read(value + len(text), 1)[0]
                if b == 0 or len(text) > 0x100:
                    break
                text.append(b)
            out.append((kind, ln, bytes(text)))
        elif 1 <= kind <= 4:
            out.append((kind, ln,
                        struct.unpack("<I", bytes(u.mem_read(value, 4)))[0]))
        else:
            out.append((kind, ln, None))
        # cdecl: the caller cleans up, so returning is just popping the address.
        u.reg_write(UC_X86_REG_EIP, ret)
        u.reg_write(UC_X86_REG_ESP, esp + 4)

    h = uc.hook_add(UC_HOOK_CODE, at_add_token)
    try:
        buf = bytearray(SCRATCH_PATTERN[:SCRATCH_SZ])
        buf[LINE_AT:LINE_AT + len(line) + 1] = line + b"\0"
        eax, _mem = emu.call(ADDR_NEXT_TOKEN,
                             [SCRATCH + LINE_AT, SCRATCH + CTX_AT, lineno],
                             bytes(buf), count=4000000)
    finally:
        uc.hook_del(h)

    return None if eax is None else out


def script_lines():
    """Distinct lines from the shipped scripts, with a line number.

    Deduplicated: `undeploy greenflag1,` appears in dozens of rule files and
    emulating it dozens of times tests nothing further. The line number travels
    with the token, so one occurrence of each is kept with the number it first
    had.
    """
    seen = {}
    for path in script_files():
        with open(path, "rb") as fh:
            for n, line in enumerate(fh, 1):
                line = line.rstrip(b"\r\n")
                if len(line) < 0x200:
                    seen.setdefault(line, n)
    return sorted(seen.items())


def main():
    if not os.path.isdir(GAME_DIR):
        sys.exit("no game directory at %s" % GAME_DIR)

    emu = Emu()
    words = harvest()
    rows = []
    for w in words:
        buf = bytearray(SCRATCH_PATTERN[:SCRATCH_SZ])
        buf[0:len(w) + 1] = w + b"\0"
        eax, _mem = emu.call(ADDR_LOOKUP_TOKEN, [SCRATCH], bytes(buf))
        if eax is None:
            sys.exit("emulation failed on %r" % w)
        rows.append((w, struct.unpack("<i", struct.pack("<I", eax))[0]))

    named = sum(1 for _w, v in rows if v >= 0)
    with open(OUT, "w") as fh:
        fh.write("/* Generated by tools/scriptcheck.py -- do not edit.\n *\n"
                 " * Every distinct word in the %d shipped script files, run\n"
                 " * through the original ScriptLookupToken under Unicorn.\n"
                 " * %d of %d resolve to a keyword id.\n */\n"
                 % (len(script_files()), named, len(rows)))
        fh.write("typedef struct { const char *word; int32_t id; } "
                 "AM2_ScriptVec;\n\n")
        fh.write("static const AM2_ScriptVec am2_script_vectors[] = {\n")
        for w, v in rows:
            esc = w.decode("latin-1").replace("\\", "\\\\").replace('"', '\\"')
            fh.write('    { "%s", %d },\n' % (esc, v))
        fh.write("};\n")

    lines = script_lines()
    toks, index = [], []
    for line, lineno in lines:
        got = token_stream(emu, line, lineno)
        if got is None:
            sys.exit("emulation failed on %r" % line)
        index.append((line, lineno, len(toks), len(got)))
        toks.extend(got)

    with open(OUT, "a") as fh:
        fh.write("\n/* The token stream the original NextToken emits for every\n"
                 " * distinct line in those files. AddToken is hooked rather\n"
                 " * than executed -- it reaches HeapAlloc, which does not exist\n"
                 " * under emulation -- so what is recorded is exactly the\n"
                 " * sequence of (kind, line, value) it was asked to append.\n */\n")
        fh.write("typedef struct { int32_t kind; int32_t line; uint32_t value;\n"
                 "                 const char *text; } AM2_ScriptTokVec;\n")
        fh.write("typedef struct { const char *line; int32_t lineno;\n"
                 "                 int32_t at; int32_t count; }"
                 " AM2_ScriptLineVec;\n\n")
        fh.write("static const AM2_ScriptTokVec am2_script_toks[] = {\n")
        for kind, ln, val in toks:
            if isinstance(val, bytes):
                fh.write('    { %d, %d, 0, "%s" },\n' % (kind, ln, cstr(val)))
            else:
                fh.write("    { %d, %d, 0x%08Xu, 0 },\n"
                         % (kind, ln, val if val is not None else 0))
        fh.write("};\n\nstatic const AM2_ScriptLineVec am2_script_lines[] = {\n")
        for line, lineno, at, count in index:
            fh.write('    { "%s", %d, %d, %d },\n'
                     % (cstr(line), lineno, at, count))
        fh.write("};\n")

    print("-> %s  (%d words, %d keywords, %d files; %d lines, %d tokens)"
          % (os.path.relpath(OUT), len(rows), named, len(script_files()),
             len(lines), len(toks)))


if __name__ == "__main__":
    main()
