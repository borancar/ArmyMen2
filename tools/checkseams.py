#!/usr/bin/env python3
"""Find calls into our own code that go through the image.

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

`orig_` is one spelling of that mistake and not the only one. frame.cpp reaches
a dozen still-original functions as `call0(ADDR_X)`, which is fine until X is
reconstructed -- and `call0(ADDR_MOVIE_FRAME_STEP)` sat there for exactly as
long as it took to reconstruct 0x00445630. It works, because the address is
patched and the call lands in our code; what it costs is the same thing the
orig_ case costs. It also makes tools/blindspots.py wrong in the other
direction: it reported MovieStepCurrent blind because both callers are
reconstructed, while the counter read 746,792, because those callers were
reaching it by address.

So both spellings are checked: a `#define orig_x ... ADDR_Y`, and a
`callN(ADDR_Y)` written out at a call site.

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
        rel = os.path.relpath(f, ROOT)
        for n, line in enumerate(open(f), 1):
            m = re.match(r"\s*#define\s+(orig_\w+)\s+.*?(ADDR_[A-Z0-9_]+)", line)
            if m:
                name, sym = m.group(1), m.group(2)
                if name not in ALLOWED and sym in addr and addr[sym] in patched:
                    bad.append((rel, name, sym, addr[sym]))
                continue
            # The other spelling: a call site that names the address itself.
            # Only in a call, not in a patch_replace or a #define of the macro.
            if "patch_replace" in line or line.lstrip().startswith("#define"):
                continue
            for m in re.finditer(r"\bcall\d\(\s*(ADDR_[A-Z0-9_]+)", line):
                sym = m.group(1)
                if sym in addr and addr[sym] in patched:
                    bad.append(("%s:%d" % (rel, n), "call site", sym,
                                addr[sym]))
            # And the third spelling, which is how six of these hid. A cast
            # around AM2_IMAGE(ADDR_X) is a function pointer to the image
            # exactly as an orig_ macro is -- it just does not look like one,
            # because it is written inline at the point of use rather than
            # given a name. Found when a SEVENTH appeared and only that one
            # was reported, because it happened to have been given a name.
            for m in re.finditer(r"\)\s*AM2_IMAGE\(\s*(ADDR_[A-Z0-9_]+)\s*\)",
                                 line):
                sym = m.group(1)
                if sym in addr and addr[sym] in patched:
                    bad.append(("%s:%d" % (rel, n), "cast through the image",
                                sym, addr[sym]))

    for f, name, sym, a in bad:
        print("  %s: %s -> %s (0x%08X) is reconstructed; call it directly"
              % (f, name, sym, a))
    if bad:
        print("  %d site(s) reach our own code through the image. Add to"
              " ALLOWED only with a reason." % len(bad))
        return 1
    print("nothing reaches a reconstructed address through the image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
