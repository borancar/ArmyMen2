#!/usr/bin/env python3
"""Zero the DEAD strings in the carried blob.

The string constants the reconstruction reads were folded to inline literals,
and the char* tables that named engine assets are being transcribed to C (whose
literals live in the native binary's own .rodata, not this blob). So in the
native/SA build a growing set of blob strings is dead weight: nothing in the
built binary can reach them. This zeroes those, so the "still-opaque bytes"
measurement counts only data that is actually still load-bearing.

The hazard this tool exists to avoid: a naive "printable NUL-terminated run"
sweep also matches coincidental ASCII *inside* binary structures -- e.g. the
DirectInput device GUIDs here carry Data4 = BF C7 44 45 53 54 00 00, so "DEST"
looks like a string at the GUID's +6. Those bytes are read as whole 16-byte
GUIDs via the GUID's START address; zeroing the fragment would corrupt live
data. The same shape appears in any record array of fixed name fields read by
base+i*stride: only record[0]'s field is pointed at.

So a run counts as a genuine STRING only if its exact start VA is referenced
somewhere in the ORIGINAL image -- by a code immediate (in any section's bytes)
or a data dword. A mid-GUID fragment or a record[1..N] name field is never
referenced at its own start, and is therefore never touched.

A genuine string is then DEAD -- and zeroed -- only when it is UNREACHABLE in
the native build:
  * no dword in the CURRENT blob points into it (no carried char* table, and no
    migrated table -- migrated char* tables point at C literals, not here), AND
  * neither its ADDR_ macro nor its raw hex address appears anywhere in src/.

Conservative by construction: anything still pointed at, still code-referenced,
or that was never a referenced string in the original, is kept. The hybrid build
is unaffected -- it loads the real ArmyMen2.exe, not this blob.

Runs in standalone-generate, after mkglobals.py writes the pristine blob and
before placement.py carves it. Idempotent: mkglobals rewrites the pristine blob
each generate. Emits build/standalone/deadstrings.txt (the zeroed ranges) for
tools/checkdeadstrings.py to re-verify.
"""
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "build", "standalone")
BLOB_LO = 0x0046F000
BLOB_HI = 0x00666000
MIN_LEN = 4


def src_text():
    """The reconstruction's source as CODE only: comments stripped, and the
    `#define ADDR_x 0x..` lines stripped. Otherwise the "coded" keep below fires
    on a macro's OWN definition in orig.h (and on a mention in a comment), so a
    string that is merely NAMED but never read -- the multiplayer debug/log
    format strings, say -- looks referenced and is kept though it is dead."""
    t = []
    for f in (glob.glob(os.path.join(REPO, "src", "**", "*.c*"), recursive=True) +
              glob.glob(os.path.join(REPO, "src", "**", "*.h"), recursive=True)):
        t.append(open(f, errors="replace").read())
    src = "\n".join(t)
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'//[^\n]*', ' ', src)
    src = re.sub(r'(?m)^\s*#\s*define\s+ADDR_\w+\b.*$', ' ', src)
    return src


# Pointer tables the ORIGINAL read but the reconstruction does not, because the
# reconstruction carries its own C copy. Their bytes are dead in the native
# build, and so are the strings they -- and only they -- point at. Each is
# (start_va, end_va, name); dead_tables() drops any whose address IS reached
# from code, so a future rewiring cannot silently zero a live table.
#   SCRIPT_TOKENS: ScriptLookupToken walks am2_script_tokens (scripttokens.h),
#   never 0x00487C90; the 185 blob entries and their keyword strings are dead.
# The five below are char* name tables the reconstruction never reads (no ADDR_
# macro, no bare hex in src -- confirmed by grep, comments and #defines aside):
# the original's input, animation and menu code walked them, and that code is
# 0xCC .origgap in the native build. Each is a run of pointer/NULL dwords ending
# exactly where its string pool begins (verified byte-by-byte, no placed symbol
# inside); declaring the pointer array dead carves it, so the private strings it
# alone pointed at fall out too, while any shared target (the ON/OFF bool words,
# the code-referenced colour names) is kept by the per-string pointed/coded gate.
#   SCANCODE_NAMES: a 255-slot scancode-indexed table (0x485510), nulls for the
#   undefined codes, then its own string pool through 0x485B28 (ESC, F1..F15, DEL,
#   TAB, the letter/digit/punctuation keys, ...) -- the range covers both, since
#   the pool is reached only through the (dead) pointer table.
#   POSE_NAMES: the animation-pose names (Null, Stand, Run, ... Last).
#   IMPORT_MACHINERY: the PE import section -- the IMAGE_IMPORT_DESCRIPTOR array
#   (0x004716C8, dir[1]), every DLL's import-lookup thunks, and the hint/name and
#   DLL-name string pool that follows. The native build is an ELF: there is no PE
#   image mapped at 0x400000 and no loader that walks these, so nothing reads the
#   region (0 refs anywhere in 0x004716C8..0x00472658, checked; imports resolve
#   through src/platform, and mkglobals binds them from the real image, not this
#   blob). Unlike the others this is a DATA range, not a pointer table: its own
#   descriptor/thunk dwords point at the names inside it, which is exactly why the
#   string sweep cannot see the names as dead (a name is referenced only at the
#   hint word two bytes before it) and why the whole region is declared here.
_DEAD_TABLE_DECL = [
    (0x00487C90, 0x00488258, "SCRIPT_TOKENS"),
    (0x00485510, 0x00485B28, "SCANCODE_NAMES"),
    (0x0048A5B4, 0x0048A668, "POSE_NAMES"),
    (0x00476FBC, 0x00476FD0, "BOOL_NAMES"),
    (0x004751B8, 0x004751C8, "FLAG_TEAM_NAMES"),
    (0x004852F0, 0x00485304, "LC_COLOR_NAMES"),
    (0x004716C8, 0x00472658, "IMPORT_MACHINERY"),
    # The DirectInput/DirectDraw/DirectSound interface data the image carries in
    # .rdata but the native platform never dereferences. The live GUIDs -- the
    # ones device.cpp/audio.cpp actually pass (IID_IDirectDraw2 0x46F338,
    # IID_IDirectSound3DListener 0x46F3E8, GUID_SysMouse 0x46F5A8, GUID_SysKeyboard
    # 0x46F5B8) -- are placed symbols and sit OUTSIDE these gaps. What is left is
    # the DIOBJECTDATAFORMAT axis/button GUIDs (GUID_XAxis/YAxis/ZAxis/Button/Key)
    # and the DI property/format blocks: reached only through the c_dfDIMouse /
    # c_dfDIKeyboard rgodf arrays, which live in .text (0xCC .origgap in native) and
    # feed SetDataFormat -- and src/platform/dinput.cpp's Dev_SetDataFormat only
    # null-checks the format, never walking it. So nothing in the ELF build reads
    # these (0 src refs, 0 blob dwords point in -- checked). Each gap sits between
    # placed live GUIDs, so the ranges are stable.
    (0x0046F300, 0x0046F338, "DX_GUID_GAP_1"),
    (0x0046F348, 0x0046F3E8, "DX_GUID_GAP_2"),
    (0x0046F3F8, 0x0046F5A8, "DI_OBJFMT_GUIDS"),
    (0x0046F5C8, 0x0046F6C8, "DX_GUID_GAP_3"),
    (0x0046F6E8, 0x0046F768, "DX_GUID_GAP_4"),
    (0x0046F788, 0x0046F888, "DX_GUID_GAP_5"),
    (0x0046F898, 0x0046F8A8, "DX_GUID_GAP_6"),
    # The MSVC C++ exception-handling data: 93 __ehfuncinfo tables (each starts
    # with the VC6 EH magic 0x19930520) and their unwind/tryblock/handler sub-maps,
    # one per function the original compiled with try/catch. They point at handler
    # code in .text (0xCC .origgap in native) and at each other. The native build
    # compiles -fno-exceptions -fno-rtti and links no MSVC EH runtime, so nothing
    # walks them: 0 src refs and 0 blob dwords point into the region from outside
    # (checked). The C++ error strings the CRT error path still uses (R6025 pure-
    # virtual, etc.) sit BELOW this range and are left to the head at 0x0046FECC.
    (0x004702B8, 0x004716C8, "CXX_EH_FUNCINFO"),
    # The multiplayer network debug/log format strings ("TrooperPickupItem %x",
    # "TrooperHostApprovedPickup...", "UpdateTrooperAction: ask...", etc.) and a
    # small dead offset table, in .data right after the placed am2_step_facing_sweep
    # (which ends at 0x00489E18) and before the placed am2_key_defaults (0x0048AE80).
    # The MP send/recv code that logged them is unreconstructed (0xCC .origgap in
    # native); the ONLY reconstructed macro anywhere in 0x00489E00..0x0048AE80 is
    # ADDR_STEP_FACING_SWEEP, which is that placed symbol OUTSIDE this range. 0 src
    # refs and 0 blob dwords point in (checked). Many of these strings begin with a
    # tab, which is why the string sweep -- it only starts a run on a printable
    # byte -- never saw them as dead on its own.
    (0x00489E18, 0x0048AE80, "MP_NET_DEBUG_STRINGS"),
    # Debug/error/log format strings from subsystems that are unreconstructed in
    # native (0xCC .origgap), so nothing in the ELF build reads them (0 src refs,
    # 0 blob dwords point in -- checked): the bitmap loader's error messages
    # ("ERROR: invalid file size in %s.", "ERROR: %s not in 1-bit mode"), and the
    # region-activation debug line plus the %s.atl/%s.amm file-name format strings
    # (their ADDR_FMT_ATL/ADDR_FMT_DOT_AMM macros appear only in orig.h -- the
    # original loader used them; the reconstruction does not).
    (0x00478950, 0x00478CDC, "BITMAP_LOADER_DEBUG"),
    (0x00486220, 0x00486444, "REGION_DEBUG_AND_FMT"),
    # The script.cpp module's string pool, between the (dead) SCRIPT_TOKENS table
    # and the placed am2_sprite_set_dirs (0x00489554): the script variable names
    # ("all", "me", "difficulty", "systemspeed", "numgreen", ...), a few short
    # keyword fragments ("hit"/"and"/"npc"/"pad", which the string sweep's MIN_LEN=4
    # skipped), the startup/mission-save filename formats, and the "script.cpp"
    # module name. The reconstructed script.cpp registers every variable with its
    # OWN C literal (AddNameTableName("all", ...), etc.), and none of the ADDR_STR_*
    # macros here are read in reconstructed src (they resolve the integer VALUE
    # slots via separate ADDR_SVAR_* addresses, not these strings) -- so nothing in
    # the ELF build reads this pool (0 src refs, 0 blob dwords point in -- checked;
    # verified with a mission-loading side-by-side, which exercises the script
    # name-table init).
    (0x0048825C, 0x00489554, "SCRIPT_STRING_POOL"),
    # HUD/gameplay display strings in the .data gap between the placed
    # am2_cheat_words (0x00476704) and am2_fog_of_war (0x00476FB0): the squad-info
    # stat labels ("HP:"/"MV:"/"WT:"/"HT:"), the weapon abbreviations ("BAZ", "MAG",
    # "M80", ...), and a scatter of format/misc strings ("%d", the stratmap/portal
    # names). The reconstructed HUD writes every one of these with its OWN C literal
    # (widget.cpp SQD_TEXT("HP:") etc., the "GREN"/"FLAM"/"BAZ" weapon-abbrev array,
    # item.cpp's item names) and reads none of the ADDR_STR_* macros here -- all are
    # orig.h-only, no placed symbol lies inside, and no blob dword points in (all
    # checked). Many are three chars, which the string sweep's MIN_LEN=4 skipped.
    (0x004767A8, 0x00476FB0, "HUD_LABELS_AND_ABBREVS"),
    # Two more C++ EH record blocks, the same shape as CXX_EH_FUNCINFO above but
    # sitting in the CRT-string head rather than the big 0x004702B8 block: a
    # `{-1, handler, handler, 0}` record at 0x0046FDA8 (for the FDIV self-test
    # function, whose .text handlers are 0xCC .origgap in native) and a run of them
    # at 0x0046FDF8..0x0046FE70 (for the CRT string/printf functions). The build is
    # -fno-exceptions with no MSVC EH runtime, so nothing walks them. The LIVE
    # neighbours are left intact and lie OUTSIDE these ranges: the FDIV constants
    # (placed am2_crt_fdiv at 0x0046FDB8), the folded "IsProcessorFeaturePresent" /
    # "KERNEL32" / "e+000" strings the CRT startup still reads (0x0046FDC8..0x0046FDF8),
    # and the placed printf classifier table (0x0046FE70). 0 src refs, 0 placed
    # symbols inside, 0 blob dwords point in (all checked).
    (0x0046FDA8, 0x0046FDB8, "CRT_EH_RECORD_FDIV"),
    (0x0046FDF8, 0x0046FE70, "CRT_EH_RECORDS_STDIO"),
]


def dead_tables():
    """[(va, size)] for the dead pointer tables above, minus any still reached
    from code (macro or bare hex, comments and #defines already stripped)."""
    src = src_text()
    oh = open(os.path.join(REPO, "src", "inject", "orig.h")).read()
    out = []
    for start, end, name in _DEAD_TABLE_DECL:
        macs = re.findall(r'#define\s+(ADDR_\w+)\s+0x0*%X' % start, oh)
        hexes = ("0x%08x" % start, "0x%x" % start)
        used = any(re.search(r'\b' + m + r'\b', src) for m in macs) \
            or any(h in src.lower() for h in hexes)
        if used:
            continue          # someone reads it now -- do not touch it
        out.append((start, end - start))
    return out


def referenced_in_original(va, img):
    """True if the 4-byte LE of `va` occurs in any section of the original image
    (a code immediate or a stored pointer -- i.e. the program points at `va`)."""
    needle = struct.pack("<I", va)
    for _name, _start, _end, data in img.sections:
        if data.find(needle) >= 0:
            return True
    return False


def dead_ranges(blob):
    """[(va, length)] of genuine strings that are unreachable in the native build."""
    oh = open(os.path.join(REPO, "src", "inject", "orig.h")).read()
    addr_macro = {}
    for m in re.finditer(r'#define\s+(ADDR_\w+)\s+0x([0-9A-Fa-f]+)u', oh):
        addr_macro.setdefault(int(m.group(2), 16), m.group(1))
    src = src_text()
    img = am2.Image()

    # Ranges of migrated POINTER tables (char*/mixed): placement carves these
    # out of the blob and replaces their pointers with C literals pointing at
    # the native binary's own .rodata, not here. So a blob dword sitting inside
    # one is NOT a live pointer in the native build -- skip it, so a string that
    # only a migrated name table reached becomes dead and is dropped too. A
    # pure-data migrated symbol keeps its bytes byte-identical, so its dwords
    # still point where they did and are NOT skipped.
    import placement
    placed = placement.placed_set(placement.manifest(), placement.split())
    carved = [(a, a + s) for (a, s, _sym, is_ptr) in placed if is_ptr]
    # EVERY placed symbol's bytes -- pure-data too -- are restored byte-exact by
    # placement, so the sweep must not zero a run inside one (a pure-data table
    # like UNIT_TYPES carries INLINE name strings; zeroing one would leave the
    # blob and the placed C bytes disagreeing).
    placed_ranges = [(a, a + s) for (a, s, _sym, _p) in placed]

    def in_placed(va, end):
        return any(lo < end and va < hi for lo, hi in placed_ranges)
    # Dead pointer tables count like carved ranges: their pointers do not
    # survive into the native build, so a string only they reached is dead.
    deadtabs = dead_tables()
    carved += [(a, a + s) for a, s in deadtabs]
    # Strings folded to C literals (tools/foldstrings.py): the reconstruction
    # now reads the literal, so the blob copy is dead even though the macro
    # still appears in code. Treat their VAs as not code-referenced.
    import foldstrings
    folded = foldstrings.folded_vas()

    def in_carved(va):
        return any(lo <= va < hi for lo, hi in carved)

    # dwords in the CURRENT blob that could point into the image data region
    targets = set()
    for off in range(0, len(blob) - 3, 4):
        if in_carved(BLOB_LO + off):
            continue
        v = struct.unpack("<I", blob[off:off + 4])[0]
        if BLOB_LO <= v < BLOB_HI:
            targets.add(v)

    # A string byte is printable ASCII, or one of the whitespace controls a C
    # string legitimately carries -- tab, CR, LF. Log/format strings are full of
    # \n, and stopping the run at the first \n left them undetected (and so
    # never dropped) even though they are dead: only .text push-immediates name
    # them, which the genuine-string gate below sees but the native build cannot
    # reach. A run must still START on a real printable byte.
    def is_str_byte(b):
        return 32 <= b < 127 or b in (9, 10, 13)

    out = []
    i, n = 0, len(blob)
    while i < n:
        if 32 <= blob[i] < 127:
            j = i
            while j < n and is_str_byte(blob[j]):
                j += 1
            if j < n and blob[j] == 0 and j - i >= MIN_LEN:
                va = BLOB_LO + i
                end = va + (j - i)
                # (1) genuine string, NOT inside a placed symbol, and NOT inside
                # a declared dead range (its bytes are already zeroed wholesale --
                # e.g. the DLL-name strings inside IMPORT_MACHINERY -- so counting
                # them again here would double-record the same VAs).
                if (not in_placed(va, end) and not in_carved(va)
                        and referenced_in_original(va, img)):
                    pointed = any(va <= t < end for t in targets)
                    mac = addr_macro.get(va)
                    coded = va not in folded and (
                        (mac is not None and re.search(r'\b' + mac + r'\b', src))
                        or ("0x%08x" % va) in src.lower()
                        or ("0x%x" % va) in src)
                    # (2) dead: nothing in the native build reaches it
                    if not pointed and not coded:
                        out.append((va, j - i + 1))   # include the NUL
            i = j + 1
        else:
            i += 1
    # And the dead tables themselves: their pointer/id dwords are dead too.
    for a, s in deadtabs:
        out.append((a, s))
    out.sort()
    return out


def main():
    path = os.path.join(OUT, "origdata.bin")
    blob = bytearray(open(path, "rb").read())
    ranges = dead_ranges(blob)
    total = 0
    for va, ln in ranges:
        off = va - BLOB_LO
        blob[off:off + ln] = b"\0" * ln
        total += ln
    open(path, "wb").write(blob)
    with open(os.path.join(OUT, "deadstrings.txt"), "w") as fh:
        for va, ln in ranges:
            fh.write("0x%08X %d\n" % (va, ln))
    print("deadstrings: zeroed %d dead ranges, %d bytes" % (len(ranges), total))


if __name__ == "__main__":
    main()
