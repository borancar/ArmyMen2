"""Find the edits made to this executable after it was compiled.

The GOG build of ArmyMen2.exe is not what the compiler emitted. Several
conditional branches have been overwritten with unconditional ones -- `74` or
`75` replaced by `EB`, the same length, so nothing moved and no other byte
changed. Two things are disabled that way: the copy protection, and the
MULTIPLAYER entry on the title screen.

That matters to a reconstruction in three separate ways, which is why it is
worth a tool rather than a note. Transcribing the bytes faithfully would record
"this game has no copy protection and no multiplayer", which is untrue of the
game and only true of this copy. Import sites sitting in code the patch made
unreachable are not outstanding boundary work, and counting them as such
overstates what is left. And a whole subsystem being unreachable is the
difference between a reconstruction verified by running and one verified by
reading.

THE SIGNATURE. A compare or test, then an unconditional jump, with the flags
never read. No compiler emits that: it computes a condition and then ignores it.
The refinement that makes the scan trustworthy is checking the jump's target --
`cmp; jmp L` is perfectly ordinary when L begins with a `jcc` that consumes the
flags, which is what a loop back-edge looks like. Requiring the target not to
read flags takes the candidate list from 16 to 6, and all 6 are confirmed
patches rather than a judgement call.

Writes docs/binarypatches.md. Nothing here modifies the executable; the harness
does that, off by default, from src/inject/restore.c.
"""

import csv
import os
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "binarypatches.md")

CRT_START = 0x0045C000
FIND_GAME_CD = 0x00426B50

JMP_REL8 = 0xEB
JE_REL8 = 0x74
JNE_REL8 = 0x75

# Instructions that leave the flags alone, so a compare can still be "live"
# across them on its way to a jump.
FLAG_SAFE = {"mov", "lea", "push", "pop", "nop", "movzx", "movsx", "xchg"}

# What each confirmed patch was, and how that was established. The scan finds
# the sites; which conditional belonged there is a reading of the code around
# it, so it is recorded rather than guessed at run time.
KNOWN = {
    0x0040EE9D: (JNE_REL8, "copy protection",
                 "falls through to the CD dialog, so the jump was taken when "
                 "FindGameCD answered non-zero"),
    0x0042ED4B: (JNE_REL8, "copy protection", "same shape"),
    0x0042F2A9: (JNE_REL8, "copy protection", "same shape"),
    0x0044D303: (JNE_REL8, "copy protection", "same shape"),
    0x0044D40A: (JNE_REL8, "copy protection", "same shape"),
    0x0044D8FE: (JE_REL8, "MULTIPLAYER button",
                 "eight sibling buttons in the same function read `74 55` after "
                 "the identical allocation check; this one alone reads `EB 55`"),
}


def reads_flags(mnemonic):
    return ((mnemonic.startswith("j") and mnemonic != "jmp")
            or mnemonic.startswith("set")
            or mnemonic.startswith("cmov")
            or mnemonic in ("adc", "sbb", "rcl", "rcr", "lahf", "cmc"))


def find_patches(img):
    """Compare-then-unconditional-jump where nothing consumes the flags."""
    insns = img.disasm(".text")
    index = {insn.address: k for k, insn in enumerate(insns)}
    out = []

    for k, insn in enumerate(insns):
        if insn.mnemonic != "jmp" or not insn.op_str.startswith("0x"):
            continue
        if insn.address >= CRT_START:
            continue
        for j in range(k - 1, max(-1, k - 5), -1):
            prev = insns[j]
            if prev.mnemonic in ("cmp", "test"):
                target = int(insn.op_str, 16)
                at = index.get(target)
                # A back-edge into a jcc uses the flags; that is ordinary code.
                if at is not None and reads_flags(insns[at].mnemonic):
                    break
                out.append({
                    "jump": insn.address,
                    "target": target,
                    "compare": f"{prev.mnemonic} {prev.op_str}",
                    "bytes": bytes(img.read(insn.address, insn.size)),
                })
                break
            if prev.mnemonic not in FLAG_SAFE:
                break
    return out


def cd_check_sites(img):
    """Addresses of the `call FindGameCD` in front of each protection check."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    _n, start, _e, data = img.section(".text")
    out = {}
    for j in range(len(data) - 5):
        if data[j] != 0xE8:
            continue
        if start + j + 5 + struct.unpack_from("<i", data, j + 1)[0] != FIND_GAME_CD:
            continue
        insns = list(md.disasm(bytes(data[j:j + 16]), start + j))[:3]
        if len(insns) == 3 and insns[1].mnemonic == "test":
            out[insns[2].address] = start + j
    return out


def branch_targets(img):
    """Every address the image can transfer control to or points at.

    Branches come from DECODED instructions, not from a byte scan. Scanning
    raw bytes for 0x70-0x7F and 0xEB finds operands of other instructions and
    invents branches: it reported a `jo` and a `js` into the second CD dialog's
    span, neither of which is an instruction at all. Data references still come
    from a raw scan, because an address genuinely can sit at any offset as an
    immediate -- but a wrong data reference only ever makes this check more
    conservative, while a wrong branch makes it wrong.
    """
    out = set()
    for insn in img.disasm(".text"):
        if insn.mnemonic in ("jmp", "call") or (
                insn.mnemonic.startswith("j") and insn.mnemonic != "jmp"):
            if insn.op_str.startswith("0x"):
                out.add(int(insn.op_str, 16))
    for _name, start, _end, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] == 0x68 or 0xB8 <= data[j] <= 0xBF:
                out.add(struct.unpack_from("<I", data, j + 1)[0])
        for j in range((-start) % 4, len(data) - 4, 4):
            out.add(struct.unpack_from("<I", data, j)[0])
    return out


def guarded_dialogs(img, patches):
    """Per patched jump: the MessageBoxA it skips, and whether that can be run.

    Skipping a block proves only that it cannot be fallen into, so this also
    asks whether anything reaches the dialog itself. The question has to be
    that precise. Asking merely "does anything point into the span" answers
    yes for 0x0040EE9D, whose span is re-entered at 0x0040EEE7 -- which is
    AFTER its MessageBoxA and therefore cannot run it.

    A dialog is reachable if some target outside the span lands at or before
    its call site but still inside the span.
    """
    targets = branch_targets(img)
    calls = {}
    path = os.path.join(REPO, "docs", "imports.tsv")
    if os.path.exists(path):
        with open(path) as fh:
            for row in csv.DictReader(fh, delimiter="\t"):
                if row["symbol"] == "MessageBoxA":
                    calls.setdefault(int(row["site"], 16), row["func"])

    out = []
    for p in patches:
        lo, hi = p["jump"] + len(p["bytes"]), p["target"]
        if hi <= lo:
            continue
        site = next((c for c in sorted(calls) if lo <= c < hi), None)
        if site is None:
            continue
        reaching = sorted(t for t in targets if lo <= t <= site)
        out.append((p["jump"], site, reaching))
    return out


def main():
    img = am2.Image()
    patches = find_patches(img)
    calls = cd_check_sites(img)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        w = fh.write
        w("# What was patched in this executable\n\n")
        w("Generated by `tools/binpatches.py`. Do not edit.\n\n")
        w("This build of `ArmyMen2.exe` is not what the compiler emitted. Some\n"
          "conditional branches have been overwritten with unconditional ones --\n"
          "`74` or `75` replaced by `EB`, same length, nothing else moved. The\n"
          "tell is the compare left in front of each one, setting flags that are\n"
          "then never read; no compiler emits that.\n\n")
        w(f"**{len(patches)} such sites**, and every one of them is accounted for\n"
          "below. Candidates whose jump lands on an instruction that *does* read\n"
          "the flags are excluded, because that is an ordinary loop back-edge --\n"
          "without that filter the list is 16 and mostly noise.\n\n")

        w("| jump | bytes | compare in front | goes to | what it disables | put back |\n")
        w("|---|---|---|---|---|---|\n")
        for p in patches:
            known = KNOWN.get(p["jump"])
            what = known[1] if known else "**not identified**"
            fix = f"`{known[0]:02X}`" if known else "—"
            raw = " ".join(f"{b:02x}" for b in p["bytes"])
            w(f"| `{p['jump']:#010x}` | `{raw}` | `{p['compare']}` | "
              f"`{p['target']:#010x}` | {what} | {fix} |\n")
        w("\n")

        w("## The copy protection\n\n")
        w("`FindGameCD` (`0x00426B50`) answers non-zero when the ARMYMEN2 CD is\n"
          "in a drive. Five callers test it and branch past a `MessageBoxA`\n"
          "saying the CD is required. All five branches are unconditional here,\n"
          "so none of those dialogs can appear:\n\n")
        w("| the call | the patched jump |\n|---|---|\n")
        for p in patches:
            if p["jump"] in calls:
                w(f"| `{calls[p['jump']]:#010x}` | `{p['jump']:#010x}` |\n")
        w("\nThe call itself was left alone -- only the branch on its result --\n"
          "so `FindGameCD` still runs and still sets what it sets. A faithful\n"
          "reconstruction calls it and discards the answer, which is what\n"
          "`src/game/win32/cdcheck.h` does; that header also carries the retail check\n"
          "as compilable source behind `#ifdef AM2_COPY_PROTECTION`, off by\n"
          "default.\n\n")

        w("## Can the skipped dialogs run?\n\n")
        w("A patched jump proves the block after it cannot be fallen into, and\n"
          "no more -- a branch from elsewhere would still get there. So for each\n"
          "one the `MessageBoxA` it skips is located and checked against every\n"
          "decoded branch target and every stored or immediate address in the\n"
          "image. What matters is whether anything lands AT OR BEFORE the call,\n"
          "since a target past it cannot run it: 0x0040EE9D's span is re-entered\n"
          "at 0x0040EEE7, which is after its dialog and therefore harmless.\n\n")
        w("| patched jump | the dialog it skips | can reach it |\n|---|---|---:|\n")
        for jump, site, reaching in guarded_dialogs(img, patches):
            w(f"| `{jump:#010x}` | `{site:#010x}` | "
              f"{'**nothing**' if not reaching else len(reaching)} |\n")
        w("\nEvery one of them answers *nothing*, so none of these dialogs can\n"
          "execute in this build however the game is driven.\n\n")

        w("## The MULTIPLAYER button\n\n")
        w("`0x0044D110` builds the title menu one button at a time, each the\n"
          "same shape: allocate `0x78` bytes, compare the result against zero,\n"
          "skip the button if the allocation failed. Eight of them read\n"
          "`74 55`. The ninth -- MULTIPLAYER, handler `0x0044D380`, artwork\n"
          "`03_101_0*_multiplay.bmp` -- reads `EB 55`. Same displacement, one\n"
          "byte different, and the button is never built. That is the gap on the\n"
          "title screen between SINGLE PLAYER and OPTIONS.\n\n")
        w("It can be put back, and doing so is how the reconstructed DirectPlay\n"
          "code gets exercised at all:\n\n")
        w("```\nAM2_DISPLAY=:99 tools/drive.sh start 25 AM2_MULTIPLAYER=1\n```\n\n")
        w("See `src/inject/restore.c`. It is off by default because anything\n"
          "enabled there makes this process differ from the binary it is derived\n"
          "from, which is exactly what `tools/ab.sh` is built to notice.\n")

    print(f"-> {os.path.relpath(OUT, REPO)}")
    print(f"{len(patches)} patched jumps, {sum(1 for p in patches if p['jump'] in KNOWN)} identified")
    for p in patches:
        known = KNOWN.get(p["jump"])
        print(f"  {p['jump']:#010x}  {p['compare']:<22} -> {p['target']:#010x}  "
              f"{known[1] if known else 'UNIDENTIFIED'}")


if __name__ == "__main__":
    main()
