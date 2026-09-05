#!/usr/bin/env python3
"""A reconstruction whose callers are ALL ours must be called by one of them.

THE DEFECT THIS CATCHES, which nothing else could.  `Step3Input` -- the whole
of a driven vehicle's keyboard and mouse handling, 1,424 bytes -- was written,
declared, installed with `patch_replace`, counted as reconstructed by every
tool, and CALLED BY NOBODY.  Its one caller in the image is `StepType3`, which
is also ours, and our `StepType3` had lost the arm that reaches it.  So the
detour at 0x0045C050 sat there and nothing ever jumped to it: no vehicle in
the game could be steered, by the player or by the AI, and the function's
counter read 0 for the ordinary blind-counter reason, which is exactly what a
healthy function reached only from our own code looks like.

The rule is narrow on purpose.  A reconstruction whose callers are still the
ORIGINAL's is reached through its detour and is fine.  It is when EVERY caller
has been reconstructed that our source becomes the only route in, and then a
missing call is dead code that no build error, no A/B and no counter reports.
`tools/blindspots.py` already computes which counters cannot move and says
nothing about whether the function still runs; this asks the other half.

WHAT COUNTS AS BEING CALLED.  The function's name appearing anywhere in
`src/game` outside its own definition, its declaration, and the
`patch_replace` line that installs it.  A table of function pointers counts --
`kSubStatePainter` and the widget vtables reach their entries that way -- and
so does a forward declaration used by another module.  The check is about
REACHABILITY, not about the shape of the call.

WHAT IT CANNOT SEE.  A function reached only through a table that lives in the
carried `.data` is reported as an orphan even though the game's own data still
points at its address; those are the ALLOWED entries below, and each one names
why.  And a function our source mentions but never actually reaches -- inside
an arm that is itself dead -- passes, because "mentioned" is all a static
scan can honestly claim.  This is a floor, not a proof.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import am2
import merges
import coverage

CRT_START = 0x00464420

# Reached by ADDRESS from data the original still owns, or registered with the
# system rather than called: our source names them once, at the registration,
# and that is the whole of their reachability.
ALLOWED = {
    "WndProc",              # the WNDCLASS field InitApplication fills
    "AudioTimerProc",       # handed to timeSetEvent by StartAudioStream
}

# WHAT THIS CHECK FOUND ON ITS FIRST RUN and nobody has fixed yet. Each is a
# reconstruction the game cannot reach, for the same reason Step3Input could
# not: its only caller is ours and ours does not call it. They are listed so
# the gate is green on what is known and RED on anything new -- the ratchet
# checkglobals.py uses, and for the same reason. This set may only go down.
#
# MsgSlotB1 and MsgSlotB2 are the tell that this is a missing dispatch arm
# rather than a dead function: MsgSlotB0 and MsgSlotA2 sit beside them, share
# the caller, and ARE called. Fixing them means reading 0x004014C0's arms
# against our reconstruction of it, which is comm code no drive here reaches.
KNOWN = {
    "MsgSlotB1",            # 0x00403350, caller 0x004014C0 (comm, unreachable here)
    "MsgSlotB2",            # 0x004033B0, caller 0x004014C0
    "Call4057D0",           # 0x00405D10, caller 0x004062B0
}


def patched_names():
    """{address: (name, source, line)} for every patch_replace in src/game."""
    addrs = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(os.path.join(am2.REPO if hasattr(am2, "REPO") else ".",
                           "src", "inject", "orig.h")) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                addrs[m.group(1)] = int(m.group(2), 16)

    call = re.compile(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)\s*,"
                      r"[^,]*?\)\s*(\w+)\s*,")
    out = {}
    for path in am2.game_sources():
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        for m in call.finditer(text):
            va = addrs.get(m.group(1))
            if va is None or va >= CRT_START:
                continue
            line = text.count("\n", 0, m.start()) + 1
            out[va] = (m.group(2), path, line)
    return out


def mentioned(name, own_path):
    """Is `name` used anywhere in src/game other than where it is defined,
    declared and installed?"""
    word = re.compile(r"\b%s\b" % re.escape(name))
    for path in am2.game_sources() + am2.game_sources(".h"):
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        # Comments discuss every name in this tree; strip them, as
        # checkseams.py and checkoffsetuse.py both had to learn to.
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        for m in word.finditer(text):
            line_start = text.rfind("\n", 0, m.start()) + 1
            line_end = text.find("\n", m.start())
            line = text[line_start:line_end if line_end > 0 else len(text)]
            if "patch_replace" in line:
                continue
            # Its own definition or declaration: the name followed by `(`
            # with a return type in front of it.
            before = line[:m.start() - line_start].strip()
            after = line[m.end() - line_start:].lstrip()
            is_decl = after.startswith("(") and (
                "__cdecl" in before or "thiscall" in before
                or "stdcall" in before)
            if is_decl:
                continue
            return True
    return False


def main():
    img = am2.Image()
    patched = patched_names()
    allowed_addrs = set(getattr(coverage, "REGISTERED", ()))
    merged = merges.real_functions(img)

    # Every function entry, split where merges.py can see a split, so a call
    # site can be attributed to the function it is really in.
    starts = []
    import csv
    with open("docs/functions.tsv") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addr, size = int(row["addr"], 16), int(row["size"])
            if addr >= CRT_START:
                continue
            if addr in merged:
                for s in merged[addr][0]:
                    starts.append(s)
            else:
                starts.append(addr)
    starts = sorted(set(starts))

    def owner_of(site):
        lo, hi = 0, len(starts) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if starts[mid] <= site:
                best = starts[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        return best

    # ONE PASS, not one per function. `am2.Image.xrefs` disassembles the whole
    # text section for every question asked, which is fine for one function and
    # is 1,500 full disassemblies here -- the first version of this check ran
    # for ten minutes and was killed. Collect every call/jmp/push target once,
    # and every dword in the data sections that looks like a code address.
    sites_by_target = {}
    for ins in img.disasm():
        if ins.mnemonic in ("call", "jmp", "push") and ins.op_str.startswith("0x"):
            try:
                tgt = int(ins.op_str, 16)
            except ValueError:
                continue
            sites_by_target.setdefault(tgt, []).append(ins.address)
    in_data = set()
    for name, start, end, data in img.sections:
        if name == ".text":
            continue
        for off in range(0, len(data) - 3, 4):
            v = int.from_bytes(data[off:off + 4], "little")
            if 0x00401000 <= v < CRT_START:
                in_data.add(v)

    orphans = []
    checked = 0
    for va, (name, path, line) in sorted(patched.items()):
        if name in ALLOWED or va in allowed_addrs:
            continue
        if va in in_data:
            continue                    # reached by address from carried data
        sites = sites_by_target.get(va, [])
        if not sites:
            continue                    # nothing reaches it in the image either
        callers = set()
        outside = False
        for site in sites:
            own = owner_of(site)
            if own is None or own >= CRT_START:
                outside = True          # past the CRT line
                break
            callers.add(own)
        if outside:
            continue
        if not callers or not callers.issubset(set(patched)):
            continue                    # some caller is still the original's
        checked += 1
        if not mentioned(name, path):
            orphans.append((name, va, path, line, sorted(callers)))

    known = sorted(o for o in orphans if o[0] in KNOWN)
    orphans = [o for o in orphans if o[0] not in KNOWN]
    stale = sorted(KNOWN - {o[0] for o in known})

    if stale:
        print("  %d KNOWN entr(ies) no longer orphaned: %s"
              % (len(stale), ", ".join(stale)))
        print("     Remove them from KNOWN; the set may only go down.")
        return 1

    if orphans:
        for name, va, path, line, callers in orphans:
            who = ", ".join("0x%08X" % c for c in callers[:4])
            print("  %s:%d: %s (0x%08X) is reachable only from OUR code"
                  % (path, line, name, va))
            print("      every caller in the image is reconstructed (%s)"
                  " and nothing in src/game calls it" % who)
        print("\n  FAILED   %d reconstruction(s) that nothing can reach."
              % len(orphans))
        print("           The detour is installed and no code jumps to it, so"
              " the function")
        print("           is dead in our build: no build error, no A/B"
              " difference, and a")
        print("           counter of 0 that reads like the ordinary blind"
              " spot. Call it from")
        print("           the reconstruction of its caller, or add it to"
              " ALLOWED with why.")
        return 1

    print("  ok       %d reconstruction(s) whose callers are all ours, "
          "%d reached, %d known-unreachable"
          % (checked, checked - len(known), len(known)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
