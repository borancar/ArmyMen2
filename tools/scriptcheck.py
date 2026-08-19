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
from vectors import (Emu, SCRATCH, SCRATCH_SZ, SCRATCH_PATTERN,
                     STACK, STACK_SZ, RET_MAGIC)
from unicorn import UC_HOOK_CODE, UC_PROT_ALL
from unicorn import UcError
from unicorn.x86_const import (UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_EAX, UC_X86_REG_EBP)

GAME_DIR = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), ".wine/drive_c/GOG Games/Army Men II")
ADDR_LOOKUP_TOKEN = 0x0043EEE0
ADDR_NEXT_TOKEN = 0x0043F450
ADDR_TOKEN_NAME = 0x0043EF40
ADDR_TOKEN_TEXT = 0x00444A90
ADDR_IS_STMT = 0x00444B80
TOK_AT, OUT_AT, STR_AT = 0x100, 0x200, 0x400

# The game's own CRT allocator. Under emulation these reach HeapAlloc, which
# is an import and does not exist, so they are serviced by a bump allocator in
# a region of their own -- see Heap. With that, AddToken and AddNameTableName
# run for real instead of being hooked away, and a statement handler can be
# emulated end to end.
ADDR_CRT_MALLOC = 0x004647F8
ADDR_CRT_REALLOC = 0x004646D8
ADDR_CRT_FREE = 0x004646A9
HEAP, HEAP_SZ = 0x60000000, 0x100000

ADDR_NAME_CAP, ADDR_NAME_COUNT, ADDR_NAMES = 0x00656460, 0x00656464, 0x00656468
ADDR_SCRIPT_VARIABLE = 0x00443F70
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


class Heap:
    """A bump allocator for the emulated CRT.

    Nothing is ever reclaimed. That is deliberate: `free` becoming a no-op
    cannot change what a correct caller observes, and a real allocator would
    add a second thing to be wrong about when the point is to test the caller.
    A megabyte outlasts any script line by a wide margin.
    """

    def __init__(self, emu):
        self.uc = emu.uc
        self.uc.mem_map(HEAP, HEAP_SZ, UC_PROT_ALL)
        self.reset()
        self.uc.hook_add(UC_HOOK_CODE, self._hook)

    def reset(self):
        self.next = HEAP + 0x10
        self.sizes = {}

    def _alloc(self, n):
        n = (n + 15) & ~15
        p = self.next
        self.next += n
        if self.next >= HEAP + HEAP_SZ:
            raise RuntimeError("emulated heap exhausted")
        self.sizes[p] = n
        return p

    def _hook(self, uc, addr, size, _user):
        if addr not in (ADDR_CRT_MALLOC, ADDR_CRT_REALLOC, ADDR_CRT_FREE):
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        args = struct.unpack("<3I", bytes(uc.mem_read(esp, 12)))
        ret = args[0]
        if addr == ADDR_CRT_MALLOC:
            eax = self._alloc(args[1])
        elif addr == ADDR_CRT_FREE:
            eax = 0
        else:
            old, n = args[1], args[2]
            eax = self._alloc(n)
            if old:
                keep = min(self.sizes.get(old, n), n)
                uc.mem_write(eax, bytes(uc.mem_read(old, keep)))
        uc.reg_write(UC_X86_REG_EAX, eax)
        uc.reg_write(UC_X86_REG_EIP, ret)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)


def raw_call(emu, addr, args, count=4000000):
    """Emulate a call WITHOUT resetting scratch.

    Emu.call rewrites the whole scratch region on every invocation, which is
    right for independent vectors and wrong here: a handler test builds a token
    list with one call and then runs the handler over it with another, so the
    memory has to survive in between.
    """
    uc = emu.uc
    sp = STACK + STACK_SZ - 0x2000
    for a in reversed(args):
        sp -= 4
        uc.mem_write(sp, struct.pack("<I", a & 0xFFFFFFFF))
    sp -= 4
    uc.mem_write(sp, struct.pack("<I", RET_MAGIC))
    uc.reg_write(UC_X86_REG_ESP, sp)
    uc.reg_write(UC_X86_REG_EBP, sp)
    try:
        uc.emu_start(addr, RET_MAGIC, timeout=5000000, count=count)
    except UcError:
        return None
    if uc.reg_read(UC_X86_REG_EIP) != RET_MAGIC:
        return None
    return uc.reg_read(UC_X86_REG_EAX)


def token_text(emu, kind, value, text):
    """What the ORIGINAL ScriptTokenText renders for one token.

    Kind 4 is not emulable: it formats through the MSVC CRT's "%6.2f", which
    needs more of the runtime than the mapped image provides, and returns a
    fault every time. Callers skip it -- that arm stays verified by reading.
    """
    buf = bytearray(SCRATCH_PATTERN[:SCRATCH_SZ])
    if text is not None:
        buf[STR_AT:STR_AT + len(text) + 1] = text + b"\0"
        value = SCRATCH + STR_AT
    struct.pack_into("<iiI", buf, TOK_AT, kind, 7, value & 0xFFFFFFFF)
    buf[OUT_AT:OUT_AT + 0x100] = b"\0" * 0x100
    eax, mem = emu.call(ADDR_TOKEN_TEXT, [SCRATCH + TOK_AT, SCRATCH + OUT_AT],
                        bytes(buf), count=2000000)
    if eax is None:
        return None
    out = mem[OUT_AT:OUT_AT + 0x100]
    return out[:out.index(b"\0")]


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


def variable_lines():
    """Every distinct `variable ...` line the scripts contain, plus the error
    shapes they never exercise.

    The malformed ones matter more than usual here: this handler has four exit
    paths and three of them are errors, and a shipped script naturally takes
    none of them.
    """
    out = []
    seen = set()
    for _line, _n in script_lines():
        t = _line.strip()
        if t.lower().startswith(b"variable") and t not in seen:
            seen.add(t)
            out.append((None, t))
    # `variable x 1.5` is deliberately absent. Its error path renders the
    # offending Float token through ScriptTokenText, whose "%6.2f" goes
    # through the MSVC CRT and does not emulate -- the same gap that keeps
    # kind 4 out of the text corpus. A Float where an Integer belongs is
    # therefore verified by reading, like the rest of that arm.
    for extra in (b"variable", b"variable 7 3", b"variable onlyname",
                  b"variable x y", b"variable \"q\" 4"):
        if extra not in seen:
            seen.add(extra)
            out.append((None, extra))

    # The duplicate-name check needs a name to already be there, and every
    # case above starts from an empty table -- so without these, deleting the
    # check outright passes all 196. Measured, by deleting it.
    out.append((b"variable dup 1", b"variable dup 2"))
    out.append((b"variable Dup 1", b"variable dup 2"))
    return out


def run_variable(emu, heap, line, pre=None):
    """Tokenise one line and run the ORIGINAL `variable` handler over it.

    The name table is zeroed first, so each case starts from an empty table and
    the recorded entry list is the whole of what the handler added.
    """
    uc = emu.uc
    CTX, AT, LINE = 0x40, 0x60, 0x1000
    heap.reset()
    uc.mem_write(SCRATCH, SCRATCH_PATTERN[:SCRATCH_SZ])
    uc.mem_write(ADDR_NAME_CAP, struct.pack("<3I", 0, 0, 0))

    # A `pre` line runs first against the same name table, which is the only
    # way to reach the duplicate-name path.
    if pre is not None:
        uc.mem_write(SCRATCH + CTX, struct.pack("<3I", 0, 0, 0))
        uc.mem_write(SCRATCH + LINE, pre + b"\0")
        if raw_call(emu, ADDR_NEXT_TOKEN,
                    [SCRATCH + LINE, SCRATCH + CTX, 5]) is None:
            return None
        uc.mem_write(SCRATCH + AT, struct.pack("<i", 0))
        if raw_call(emu, ADDR_SCRIPT_VARIABLE,
                    [SCRATCH + CTX, SCRATCH + AT]) is None:
            return None

    uc.mem_write(SCRATCH + CTX, struct.pack("<3I", 0, 0, 0))
    uc.mem_write(SCRATCH + LINE, line + b"\0")

    if raw_call(emu, ADDR_NEXT_TOKEN, [SCRATCH + LINE, SCRATCH + CTX, 5]) is None:
        return None
    _cap, count, toks = struct.unpack(
        "<3I", bytes(uc.mem_read(SCRATCH + CTX, 12)))

    uc.mem_write(SCRATCH + AT, struct.pack("<i", 0))
    rc = raw_call(emu, ADDR_SCRIPT_VARIABLE, [SCRATCH + CTX, SCRATCH + AT])
    if rc is None:
        return None
    at = struct.unpack("<i", bytes(uc.mem_read(SCRATCH + AT, 4)))[0]

    _ncap, ncount, arr = struct.unpack(
        "<3I", bytes(uc.mem_read(ADDR_NAME_CAP, 12)))
    names = []
    for i in range(ncount):
        p, ty, val, live = struct.unpack(
            "<Iiii", bytes(uc.mem_read(arr + i * 16, 16)))
        nm = bytearray()
        while uc.mem_read(p + len(nm), 1)[0]:
            nm.append(uc.mem_read(p + len(nm), 1)[0])
        names.append((bytes(nm), ty, val, live))

    kinds = []
    for i in range(count):
        k, _ln, v = struct.unpack("<iiI", bytes(uc.mem_read(toks + i * 12, 12)))
        kinds.append((k, v if k != 5 else 0))

    return dict(rc=rc, at=at, count=count, names=names, kinds=kinds)


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

    # ScriptTokenText and ScriptIsStatementStart, over every distinct token
    # the corpus produces plus the kinds it never produces.
    distinct = {}
    for kind, _ln, val in toks:
        if kind == 4:
            continue        # not emulable; see token_text
        key = (kind, val if not isinstance(val, bytes) else None,
               val if isinstance(val, bytes) else None)
        distinct.setdefault(key, True)
    for kind in (0, 6):
        distinct.setdefault((kind, 0, None), True)

    rendered = []
    for kind, val, text in sorted(distinct, key=lambda k: (k[0], k[2] or b"",
                                                           k[1] or 0)):
        got = token_text(emu, kind, val or 0, text)
        if got is None:
            sys.exit("token_text failed on kind %d %r" % (kind, text or val))
        rendered.append((kind, val or 0, text, got))

    names = []
    for i in range(-2, 200):
        eax, _m = emu.call(ADDR_TOKEN_NAME, [i & 0xFFFFFFFF],
                           SCRATCH_PATTERN[:SCRATCH_SZ])
        if eax is None:
            sys.exit("ScriptTokenName failed on %d" % i)
        names.append((i, am2.Image().cstring(eax) if eax else None))

    stmts = []
    for tid in range(0, 200):
        buf = bytearray(SCRATCH_PATTERN[:SCRATCH_SZ])
        struct.pack_into("<iiI", buf, TOK_AT, 2, 0, tid)      # kind 2
        struct.pack_into("<iii", buf, 0x80, 0, 1, SCRATCH + TOK_AT)  # ctx
        struct.pack_into("<i", buf, 0x90, 0)                  # at = 0
        eax, _m = emu.call(ADDR_IS_STMT, [SCRATCH + 0x80, SCRATCH + 0x90],
                           bytes(buf))
        if eax is None:
            sys.exit("ScriptIsStatementStart failed on %d" % tid)
        stmts.append((tid, eax))

    with open(OUT, "a") as fh:
        fh.write("\n/* ScriptTokenText over every distinct token the corpus\n"
                 " * produces. Kind 4 is absent: its \"%6.2f\" goes through the\n"
                 " * MSVC CRT and does not emulate. */\n")
        fh.write("typedef struct { int32_t kind; uint32_t value;\n"
                 "                 const char *text; const char *want; }\n"
                 "        AM2_ScriptTextVec;\n\n")
        fh.write("static const AM2_ScriptTextVec am2_script_text[] = {\n")
        for kind, val, text, got in rendered:
            fh.write('    { %d, 0x%08Xu, %s, "%s" },\n'
                     % (kind, val & 0xFFFFFFFF,
                        ('"%s"' % cstr(text)) if text is not None else "0",
                        cstr(got)))
        fh.write("};\n\n")
        fh.write("/* ScriptTokenName for every id, including two below zero\n"
                 " * and everything past the end of the table. */\n")
        fh.write("static const AM2_ScriptVec am2_script_names[] = {\n")
        for i, nm in names:
            fh.write('    { %s, %d },\n'
                     % (('"%s"' % cstr(nm.encode())) if nm else "0", i))
        fh.write("};\n\n")
        fh.write("/* ScriptIsStatementStart over every id. */\n")
        fh.write("static const AM2_ScriptVec am2_script_stmt[] = {\n")
        for tid, yes in stmts:
            fh.write("    { 0, %d },   /* %d */\n" % (yes, tid))
        fh.write("};\n")

    heap = Heap(emu)
    vcases = []
    for pre, line in variable_lines():
        got = run_variable(emu, heap, line, pre)
        if got is None:
            sys.exit("variable handler failed on %r" % line)
        vcases.append((pre, line, got))

    with open(OUT, "a") as fh:
        fh.write("\n/* The `variable` statement, run through the ORIGINAL\n"
                 " * handler over every such line the scripts contain plus the\n"
                 " * malformed shapes they never take. The CRT allocator is\n"
                 " * serviced by a bump allocator in the emulator, so AddToken\n"
                 " * and AddNameTableName run for real. */\n")
        fh.write("typedef struct { const char *pre; const char *line;\n"
                 "                 int32_t rc; int32_t at;\n"
                 "                 int32_t count; int32_t names;\n"
                 "                 const char *name0; int32_t type0;\n"
                 "                 int32_t value0; int32_t kind0; }\n"
                 "        AM2_ScriptVarVec;\n\n")
        fh.write("static const AM2_ScriptVarVec am2_script_vars[] = {\n")
        for pre, line, g in vcases:
            n0 = g["names"][0] if g["names"] else (b"", 0, 0, 0)
            k1 = g["kinds"][1][0] if len(g["kinds"]) > 1 else -1
            fh.write('    { %s, "%s", %d, %d, %d, %d, "%s", %d, %d, %d },\n'
                     % (('"%s"' % cstr(pre)) if pre else "0",
                        cstr(line), g["rc"], g["at"], g["count"],
                        len(g["names"]), cstr(n0[0]), n0[1], n0[2], k1))
        fh.write("};\n")

    print("-> %s  (%d words, %d keywords, %d files; %d lines, %d tokens; "
          "%d rendered, %d variable)"
          % (os.path.relpath(OUT), len(rows), named, len(script_files()),
             len(lines), len(toks), len(rendered), len(vcases)))


if __name__ == "__main__":
    main()
