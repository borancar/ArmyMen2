"""Shared helpers for poking at ArmyMen2.exe.

The binary is MSVC 6.0, i386, relocations stripped, so it is always mapped at
its preferred base of 0x400000 and every address in here is an absolute VA.
"""

import os

import capstone
import pefile

GAME_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    ".wine", "drive_c", "GOG Games", "Army Men II",
)
EXE = os.path.join(GAME_DIR, "ArmyMen2.exe")


class Image:
    """A loaded PE, flattened so we can address it by VA."""

    def __init__(self, path=EXE):
        self.pe = pefile.PE(path, fast_load=False)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode("latin-1")
            start = self.base + s.VirtualAddress
            self.sections.append((name, start, start + s.Misc_VirtualSize, s.get_data()))

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise KeyError(name)

    def read(self, va, n):
        """Read n bytes at a VA, or b'' if it is not backed by file data."""
        for _name, start, end, data in self.sections:
            if start <= va < end:
                off = va - start
                return data[off:off + n]
        return b""

    def valid(self, va):
        return any(start <= va < end for _n, start, end, _d in self.sections)

    def xrefs(self, va):
        """Every reference to `va`: calls, jumps, and stored addresses.

        Decodes the text section rather than scanning it for bytes, so a
        `call rel32` is resolved to its target. This is the question
        "what reaches this function"; `refs_to` answers a narrower one, and
        this repo has three times now mistaken the second for the first.
        """
        out = []
        for ins in self.disasm():
            if ins.op_str.startswith("0x"):
                try:
                    target = int(ins.op_str, 16)
                except ValueError:
                    continue
                if target == va and ins.mnemonic in ("call", "jmp", "push"):
                    out.append(ins.address)
        needle = va.to_bytes(4, "little")
        for _name, start, _end, data in self.sections:
            at = 0
            while True:
                at = data.find(needle, at)
                if at < 0:
                    break
                out.append(start + at)
                at += 1
        return sorted(set(out))

    def cstring(self, va, limit=512):
        """NUL-terminated ASCII string at a VA, or None if it does not look like one."""
        raw = self.read(va, limit)
        end = raw.find(b"\0")
        if end < 1:
            return None
        s = raw[:end]
        if not all(32 <= c < 127 or c in (9, 10, 13) for c in s):
            return None
        return s.decode("latin-1")

    def disasm(self, section=".text"):
        """Resilient linear sweep of a section.

        Cs.disasm() is a generator that simply stops at the first byte it
        cannot decode, and .text has jump tables and alignment padding mixed
        into it. So resume one byte past every stall instead of giving up.
        """
        _name, start, _end, data = self.section(section)
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        out, off = [], 0
        while off < len(data):
            got = False
            for insn in md.disasm(data[off:], start + off):
                out.append(insn)
                off = insn.address - start + insn.size
                got = True
            if not got:
                off += 1
        return out

    def refs_to(self, va, section=".text"):
        """Offsets in a section holding `va` as a little-endian dword.

        This finds ABSOLUTE references only -- vtable slots, dispatch tables,
        the operand of a `push imm32`. It does NOT find calls: `call rel32`
        and `jmp rel32` encode a displacement, so the target address is
        nowhere in the instruction stream. Asking this about a function and
        reading zero means "nothing stores its address", never "nothing calls
        it" -- LockSurface has 38 call sites and answers 0 here.

        Use `xrefs` for the question that is usually meant.
        """
        _name, start, _end, data = self.section(section)
        needle = va.to_bytes(4, "little")
        out, at = [], 0
        while True:
            at = data.find(needle, at)
            if at < 0:
                return out
            out.append(start + at)
            at += 1


def find_string_vas(img, needle, section=None):
    """Every VA whose C string equals `needle`.

    MSVC 6 without /GF pools string literals into .data rather than .rdata, so
    search every section unless the caller pins one down.
    """
    target = needle.encode("latin-1") + b"\0"
    out = []
    for name, start, _end, data in img.sections:
        if section is not None and name != section:
            continue
        at = 0
        while True:
            at = data.find(target, at)
            if at < 0:
                break
            out.append(start + at)
            at += 1
    return out


# src/game is split: win32/ holds every module that talks to Win32 or DirectX,
# and the flat part holds the pure computation -- rasterisers, rect maths,
# object tables, save tags -- which touches no API at all.
#
# Four separate tools derive "what is reconstructed" by scanning these sources
# for patch_replace calls, and all four used a NON-RECURSIVE listdir. Adding the
# subdirectory would have made every one of them silently miss fourteen modules
# and report the boundary as barely started, which docs/boundary.md would then
# have asserted in writing. One definition, used by all of them, for the same
# reason coverage.REGISTERED is imported rather than copied.
def game_sources(ext=".cpp"):
    """Every reconstruction source under src/game, at any depth."""
    root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "src", "game")
    out = []
    for base, _dirs, files in os.walk(root):
        out += [os.path.join(base, f) for f in files if f.endswith(ext)]
    return sorted(out)
