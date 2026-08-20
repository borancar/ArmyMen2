#!/usr/bin/env python3
"""Find `orig_` macros that point at an address we have already reconstructed.

An `orig_` name means "this stays in the original image". When the address it
resolves to is in the patch list, the call goes through the detour and lands in
our OWN code -- correct behaviour under a name that says the opposite.

Harmless most of the time, and not always: AM2_PROBE_NOACTION was a switch that
selected the original, and once its address was patched it selected us instead
and quietly re-recorded the oracle from the code it existed to check.

Four of these had to be found by grep before this existed -- InitInput,
DirectDraw and ReportError in winmain.cpp; SetGameDir under `orig_path_exists`
in three modules; the pause pair in dplay.cpp; CreateOffscreenSurface and
ClearSurface in device.cpp. Each time the comment beside it had gone stale too,
which is the real cost: `orig_create_offscreen` sat under "on the list to
reconstruct next" for however long after it was done.

Deliberate ones are listed in ALLOWED, with the reason.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# `orig_` macros that MUST keep pointing at a patched address, and why.
ALLOWED = {
    # The probe that re-records tests/actions-reference.txt. It only reaches
    # the original because script_install skips the patch under the same flag.
    "orig_parse_action",
}


def macros():
    out = {}
    text = open(os.path.join(ROOT, "src/inject/orig.h")).read()
    for m in re.finditer(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u", text):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def main():
    addr = macros()
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import coverage

    patched = set()
    for f in glob.glob(os.path.join(ROOT, "src/game/**/*.cpp"), recursive=True):
        for m in re.finditer(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)", open(f).read()):
            if m.group(1) in addr:
                patched.add(addr[m.group(1)])
    for n in coverage.REGISTERED:
        if n in addr:
            patched.add(addr[n])

    bad = []
    for f in sorted(glob.glob(os.path.join(ROOT, "src/**/*.c*"), recursive=True)
                    + glob.glob(os.path.join(ROOT, "src/**/*.h"), recursive=True)):
        for line in open(f):
            m = re.match(r"\s*#define\s+(orig_\w+)\s+.*?(ADDR_[A-Z0-9_]+)", line)
            if not m:
                continue
            name, sym = m.group(1), m.group(2)
            if name in ALLOWED or sym not in addr:
                continue
            if addr[sym] in patched:
                bad.append((os.path.relpath(f, ROOT), name, sym, addr[sym]))

    for f, name, sym, a in bad:
        print("  %s: %s -> %s (0x%08X) is reconstructed; call it directly"
              % (f, name, sym, a))
    if bad:
        print("  %d orig_ macro(s) name our own code. Add to ALLOWED only with"
              " a reason." % len(bad))
        return 1
    print("no orig_ macro points at a reconstructed address")
    return 0


if __name__ == "__main__":
    sys.exit(main())
