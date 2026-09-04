#!/usr/bin/env python3
"""Check RestoreTileSet against the original: the file walk and the remap.

RestoreTileSet (0x0042C0E0) re-reads a map's `.atl` tileset into surfaces
DirectDraw has taken back.  It is the LAST function CLAUDE.md's unexercised
list had standing on reading alone, and it cannot be driven here: nothing
under Xvfb loses a surface, because there is no alt-tab and no mode change.

CHECKS = ("RestoreTileSet",)

ITS SIBLING DOES NOT COVER IT, which was worth measuring before building
this.  LoadAtlFile reads the same format and runs on every map load, so the
obvious argument is that the parse is verified by it.  Normalised disassembly
says otherwise: 177 instructions against 206, similarity 0.245, and NOT ONE
shared run of six.  The two are a rewrite of each other rather than a
re-emission -- the SeqCtx shape, not the AiStepTrack one -- so what the
sibling's live coverage establishes is the FILE FORMAT and nothing about
these instructions.  Diffing before relying on a resemblance, which this tree
already does before merging one.

Twelve callees are stubbed, in THREE calling conventions, and getting that
wrong is not cosmetic -- see tools/vehexitcheck.py, where a cdecl-only stub
shifted every frame below it and reported the arm sequence correct with every
argument wrong.  The two COM slots are stdcall, so they pop their own
arguments; the CRT and the game's own helpers here are cdecl.

WHAT IS COMPARED IS THE TRACE, because this function returns void.  In it:
which of the three error messages was logged, how the file cursor moved, the
REMAP TABLE handed to BlitBitmapIn, and whether the surface was unlocked.
The remap is the part with real arithmetic in it -- the reserved run, the
`from` floor passed to every lookup, and the identity fallback with no active
palette.

THE UNLOCK IS THE ORIGINAL'S OWN DEFECT AND IS REPRODUCED: the arm that
logs "Error on Lock in RestoreTileSet()" is the arm that has the surface
LOCKED, and it does not unlock it.  The corpus reaches it, so a
reconstruction that tidied it would fail here.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu

CHECKS = ("RestoreTileSet",)

RESTORE_TILESET = 0x0042C0E0
SET_GAME_DIR = 0x00422DE0
SPRINTF = 0x00464CE2
FOPEN = 0x004648E2
FREAD = 0x004645C1
FSEEK = 0x00464F18
FCLOSE = 0x0046486C
FREE = 0x004646A9
LOG = 0x0045CAA0
READ_DIB_CHUNK = 0x00422FF0
SWAP_COLOUR_BYTES = 0x0041AE90
NEAREST_PAL_INDEX = 0x0041B7C0
BLIT_BITMAP_IN = 0x0041BA90

ADDR_MAP_SURFACE = 0x00514E90
ADDR_ACTIVE_PALETTE = 0x00477A58
ADDR_TILESET_RESERVE = 0x00511CC8
MSG_OPEN = 0x00486310
MSG_LOCK = 0x004862EC

MAPSPR_OFF_SURFACE = 0x10
MAPSPR_OFF_WIDTH = 0x1C
MAPSPR_OFF_HEIGHT = 0x20

DESC_PITCH = 0x10                       # DDSURFACEDESC.lPitch
DESC_SURFACE = 0x24                     # DDSURFACEDESC.lpSurface
DD_OK = 0

FOURCC_FORM = 0x4D524F46
FOURCC_TILE = 0x454C4954
FOURCC_DIB = 0x20424944
FOURCC_OTHER = 0x52444448               # 'HDR'

DATA = 0x78000000
DATA_SZ = 0x40000
STUBS = DATA + 0x00000                  # the two COM entry points
VTABLE = DATA + 0x01000
SURFOBJ = DATA + 0x02000
MAPSPR = DATA + 0x03000
SURFPIX = DATA + 0x04000
DIBPIX = DATA + 0x10000
PALETTE = DATA + 0x20000
assert PALETTE + 0x400 < DATA + DATA_SZ

FILE_TOKEN = 0x11220000
PITCH = 0x140
WIDTH, HEIGHT = 32, 24
LOCK_STUB = STUBS + 0
UNLOCK_STUB = STUBS + 8


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)
        from unicorn import UC_HOOK_CODE

        # (stack args, mode, handler).  "this" is not a stack argument.
        stubs = {
            SET_GAME_DIR: (1, "cdecl", self._chdir),
            SPRINTF: (2, "cdecl", self._sprintf),
            FOPEN: (2, "cdecl", self._fopen),
            FREAD: (4, "cdecl", self._fread),
            FSEEK: (3, "cdecl", self._fseek),
            FCLOSE: (1, "cdecl", self._fclose),
            FREE: (1, "cdecl", self._free),
            LOG: (1, "cdecl", self._log),
            READ_DIB_CHUNK: (2, "cdecl", self._readdib),
            SWAP_COLOUR_BYTES: (2, "cdecl", self._swap),
            NEAREST_PAL_INDEX: (3, "cdecl", self._nearest),
            BLIT_BITMAP_IN: (7, "cdecl", self._blit),
            LOCK_STUB: (5, "stdcall", self._lock),
            UNLOCK_STUB: (2, "stdcall", self._unlock),
        }
        for a, (n, mode, fn) in stubs.items():
            uc.mem_write(a, b"\xc3")
            uc.hook_add(UC_HOOK_CODE, self._wrap(n, mode, fn), begin=a, end=a)

        uc.mem_write(VTABLE + 0x64, struct.pack("<I", LOCK_STUB))
        uc.mem_write(VTABLE + 0x80, struct.pack("<I", UNLOCK_STUB))
        uc.mem_write(SURFOBJ, struct.pack("<I", VTABLE))
        self.uc = uc

    def _wrap(self, nargs, mode, fn):
        def hook(uc, addr, size, user):
            from unicorn import x86_const
            esp = uc.reg_read(x86_const.UC_X86_REG_ESP)
            vals = struct.unpack("<%dI" % (nargs + 1),
                                 uc.mem_read(esp, 4 * (nargs + 1)))
            args = list(vals[1:])
            if mode == "thiscall":
                args.insert(0, uc.reg_read(x86_const.UC_X86_REG_ECX))
            rv = fn(tuple(args))
            uc.reg_write(x86_const.UC_X86_REG_EAX, (rv or 0) & 0xFFFFFFFF)
            # cdecl leaves its arguments to the caller; the other two do not
            pop = 4 + (4 * nargs if mode in ("thiscall", "stdcall") else 0)
            uc.reg_write(x86_const.UC_X86_REG_ESP, esp + pop)
            uc.reg_write(x86_const.UC_X86_REG_EIP, vals[0])
        return hook

    # -- the stubs ---------------------------------------------------------
    def _chdir(self, a):
        self.trace.append(("chdir",))
        return 1

    def _sprintf(self, a):
        self.trace.append(("sprintf",))
        self.uc.mem_write(a[0], b"tiles.atl\0")
        return 9

    def _fopen(self, a):
        self.trace.append(("fopen",))
        return FILE_TOKEN if self.can_open else 0

    def _fread(self, a):
        dst, size, count = a[0], a[1], a[2]
        n = size * count
        chunk = self.blob[self.cursor:self.cursor + n]
        chunk += b"\0" * (n - len(chunk))
        self.uc.mem_write(dst, chunk)
        self.cursor += n
        self.trace.append(("fread", n))
        return count

    def _fseek(self, a):
        self.cursor += a[1] if a[2] == 1 else 0
        self.trace.append(("fseek", a[1], a[2]))
        return 0

    def _fclose(self, a):
        self.trace.append(("fclose",))
        return 0

    def _free(self, a):
        self.trace.append(("free", a[0] == DIBPIX))
        return 0

    def _log(self, a):
        self.trace.append(("log", a[0]))
        return 0

    def _readdib(self, a):
        """Fill the caller's header with a palette and hand back pixels."""
        self.trace.append(("readdib",))
        self.cursor += self.dib_payload
        if not self.dib_ok:
            return 0
        pal = b"".join(struct.pack("<I", (i * 0x00010203) & 0xFFFFFFFF)
                       for i in range(256))
        self.uc.mem_write(a[1] + 10 * 4, pal)
        return DIBPIX

    def _swap(self, a):
        return (a[0] ^ 0xA5A5A5A5) & 0xFFFFFFFF

    def _nearest(self, a):
        return ((a[1] >> 8) + a[2]) & 0xFF

    def _blit(self, a):
        remap = bytes(self.uc.mem_read(a[5], 256))
        self.trace.append(("blit", a[1], a[3], a[4],
                           sum(remap) & 0xFFFF, remap[:4].hex()))
        return self.blit_ok

    def _lock(self, a):
        self.trace.append(("lock",))
        if not self.lock_ok:
            return 1
        self.uc.mem_write(a[2] + DESC_PITCH, struct.pack("<i", PITCH))
        self.uc.mem_write(a[2] + DESC_SURFACE, struct.pack("<I", SURFPIX))
        return DD_OK

    def _unlock(self, a):
        self.trace.append(("unlock", a[1] == SURFPIX))
        return DD_OK

    # -- driving -----------------------------------------------------------
    def run(self, c):
        uc = self.uc
        self.trace = []
        self.can_open = c["can_open"]
        self.dib_ok = c["dib_ok"]
        self.lock_ok = c["lock_ok"]
        self.blit_ok = c["blit_ok"]
        self.blob, self.dib_payload = build_atl(c)
        self.cursor = 0

        uc.mem_write(MAPSPR, b"\0" * 0x40)
        uc.mem_write(MAPSPR + MAPSPR_OFF_SURFACE, struct.pack("<I", SURFOBJ))
        uc.mem_write(MAPSPR + MAPSPR_OFF_WIDTH, struct.pack("<i", WIDTH))
        uc.mem_write(MAPSPR + MAPSPR_OFF_HEIGHT, struct.pack("<i", HEIGHT))
        uc.mem_write(ADDR_MAP_SURFACE, struct.pack("<I", MAPSPR))
        uc.mem_write(ADDR_ACTIVE_PALETTE,
                     struct.pack("<I", PALETTE if c["pal"] else 0))
        uc.mem_write(ADDR_TILESET_RESERVE, struct.pack("<i", c["reserve"]))
        uc.mem_write(PALETTE, b"\x77" * 0x400)

        self.emu.call(RESTORE_TILESET, [], b"\0" * 64, count=2000000)
        return tuple(self.trace)


CHUNK_BYTES = 0x20


def build_atl(c):
    """A synthetic `.atl`: FORM/TILE then the case's chunk sequence."""
    body = b""
    for kind in c["chunks"]:
        cc = FOURCC_DIB if kind == "dib" else FOURCC_OTHER
        body += struct.pack("<II", cc, CHUNK_BYTES) + b"\xEE" * CHUNK_BYTES
    magic1 = FOURCC_FORM if c["form_ok"] else 0x21212121
    magic2 = FOURCC_TILE if c["tile_ok"] else 0x22222222
    # `form_trim` shortens the declared size below the bytes actually there,
    # so the loop STOPS on its own arithmetic rather than on running out of
    # chunks.  Without it the offset accumulation is unobservable: a model
    # that iterates the chunk list agrees with the original whether or not it
    # adds the DIB payload, and that mutation passed all 84 cases.
    form_size = 12 + len(body) - c.get("form_trim", 0)
    return (struct.pack("<III", magic1, form_size, magic2) + body,
            CHUNK_BYTES)


def model(c):
    """src/game/win32/mapdraw.cpp's RestoreTileSet, in the same terms."""
    t = [("chdir",), ("sprintf",), ("fopen",)]
    if not c["can_open"]:
        t.append(("log", MSG_OPEN))
        return tuple(t)

    blob, payload = build_atl(c)
    form_size = struct.unpack_from("<I", blob, 4)[0]

    t.append(("fread", 4))
    if not c["form_ok"]:
        return tuple(t + [("log", MSG_LOAD), ("fclose",)])
    t.append(("fread", 4))
    t.append(("fread", 4))
    if not c["tile_ok"]:
        return tuple(t + [("log", MSG_LOAD), ("fclose",)])

    offset = 12
    for kind in c["chunks"]:
        t.append(("fread", 4))
        t.append(("fread", 4))
        offset += 8
        if kind != "dib":
            t.append(("fseek", CHUNK_BYTES, 1))
            offset += CHUNK_BYTES
            if offset >= form_size:
                break
            continue
        offset += CHUNK_BYTES

        t.append(("readdib",))
        if not c["dib_ok"]:
            return tuple(t + [("log", MSG_LOAD), ("fclose",)])

        remap = bytearray(256)
        if c["pal"]:
            frm = 0
            if c["reserve"]:
                for i in range(10):
                    remap[i] = i
                frm = 10
            for i in range(frm, 256):
                colour = ((i * 0x00010203) & 0xFFFFFFFF) ^ 0xA5A5A5A5
                remap[i] = ((colour >> 8) + frm) & 0xFF
        else:
            for i in range(256):
                remap[i] = i

        t.append(("lock",))
        if c["lock_ok"]:
            t.append(("blit", PITCH, WIDTH, HEIGHT,
                      sum(remap) & 0xFFFF, bytes(remap[:4]).hex()))
            if c["blit_ok"]:
                t.append(("unlock", True))
            else:
                t.append(("log", MSG_LOCK))       # locked, and NOT unlocked
        t.append(("free", True))
        if offset >= form_size:
            break

    t.append(("fclose",))
    return tuple(t)


MSG_LOAD = None                        # filled in from the image below


def _load_msg():
    """ADDR_MSG_TILESET_LOAD, read from orig.h so nothing is transcribed."""
    import re
    here = os.path.dirname(os.path.abspath(__file__))
    src = open(os.path.join(here, "..", "src", "inject", "orig.h")).read()
    m = re.search(r"#define ADDR_MSG_TILESET_LOAD\s+(0x[0-9A-Fa-f]+)", src)
    return int(m.group(1), 16)


def cases():
    base = dict(can_open=1, form_ok=1, tile_ok=1, dib_ok=1, lock_ok=1,
                blit_ok=1, pal=0, reserve=0, chunks=("dib",), form_trim=0)

    def var(**kw):
        c = dict(base)
        c.update(kw)
        return c

    yield var(can_open=0)
    yield var(form_ok=0)
    yield var(tile_ok=0)
    yield var(dib_ok=0)
    for chunks in (("dib",), ("other", "dib"), ("dib", "dib"),
                   ("other",), ("dib", "other", "dib")):
        for pal in (0, 1):
            for reserve in (0, 1):
                for lock_ok in (0, 1):
                    for blit_ok in (0, 1):
                        yield var(chunks=chunks, pal=pal, reserve=reserve,
                                  lock_ok=lock_ok, blit_ok=blit_ok)
    for c in _trim_cases(var):
        yield c


def _trim_cases(var):
    """The declared size ends the walk before the chunks do."""
    for chunks in (("dib", "dib"), ("dib", "other"), ("other", "dib"),
                   ("dib", "dib", "dib")):
        for trim in (8 + CHUNK_BYTES, 2 * (8 + CHUNK_BYTES)):
            if trim >= len(chunks) * (8 + CHUNK_BYTES):
                continue
            for pal in (0, 1):
                yield var(chunks=chunks, form_trim=trim, pal=pal)


def main():
    global MSG_LOAD
    MSG_LOAD = _load_msg()
    h = Harness()
    n = bad = 0
    for c in cases():
        got, want = h.run(c), model(c)
        n += 1
        if got != want:
            bad += 1
            if bad <= 3:
                print("  %s" % {k: v for k, v in c.items() if k != "chunks"})
                print("   chunks=%s" % (c["chunks"],))
                print("     got  %s" % (got,))
                print("     want %s" % (want,))
    print("tilesetcheck: %d cases, %d differ" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
