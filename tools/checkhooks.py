"""Check that the reconstruction cannot bypass the harness's own hooks.

src/inject/dinput_hook.c drives the game by patching an entry in the GAME's
import table and wrapping the interfaces that come back. That works only while
every caller reaches the symbol through that patched entry.

A reconstructed function that imported the same symbol into am2hook.dll would
resolve it through OUR import table instead, walk straight past the hook, and
take the harness's ability to drive the game with it. Nothing would crash. The
game would run, look completely healthy, and silently ignore every scripted
click and keypress -- and the A/B would still pass, because both sides would be
equally undriven.

src/game/device.cpp avoids it by calling the game's own one-instruction import
thunks rather than importing DirectInput. That is a convention, and conventions
are the sort of thing that survives until someone writes the obvious code
instead. This turns it into a check.

The hooked slot is read out of dinput_hook.c and resolved against the game's
import directory, so if the hook ever moves to a different symbol the check
follows it rather than going quietly out of date.

    tools/checkhooks.py        # exits non-zero if a hook can be bypassed
"""

import os
import re
import sys

import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOK_SRC = os.path.join(REPO, "src", "inject", "dinput_hook.c")
HOOK_DLL = os.path.join(REPO, "build", "am2hook.dll")


def hooked_slots():
    """IAT addresses dinput_hook.c patches, from its own #defines."""
    out = []
    pat = re.compile(r"#define\s+IAT_\w+\s+0x([0-9A-Fa-f]+)u?")
    with open(HOOK_SRC) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                out.append(int(m.group(1), 16))
    return out


def symbol_at(pe, addr):
    """The imported symbol whose IAT entry lives at `addr`."""
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            if imp.address == addr:
                name = imp.name.decode() if imp.name else f"#{imp.ordinal}"
                return entry.dll.decode(), name
    return None, None


def main():
    slots = hooked_slots()
    if not slots:
        print("checkhooks: no IAT_ defines in dinput_hook.c -- did the hook move?")
        return 1

    game = pefile.PE(am2.EXE)
    hooked = {}
    for slot in slots:
        dll, name = symbol_at(game, slot)
        if name is None:
            print(f"checkhooks: {slot:#010x} is not an import of the game -- "
                  "the hook is patching something that is not there")
            return 1
        hooked[name.lower()] = (slot, dll, name)

    if not os.path.exists(HOOK_DLL):
        print(f"checkhooks: no {os.path.relpath(HOOK_DLL, REPO)} -- run make first")
        return 1

    ours = pefile.PE(HOOK_DLL)
    clashes = []
    for entry in ours.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            if not imp.name:
                continue
            name = imp.name.decode()
            if name.lower() in hooked:
                clashes.append((entry.dll.decode(), name))

    for name, (slot, dll, sym) in sorted(hooked.items()):
        print(f"  hooked: {dll}!{sym} at {slot:#010x}")

    if clashes:
        print("\nam2hook.dll IMPORTS A HOOKED SYMBOL, so the hook is bypassed:")
        for dll, name in clashes:
            print(f"    {dll}!{name}")
        print("\nCall the game's own import thunk instead -- see the note at the")
        print("top of src/game/device.cpp. Injected input is silently dead")
        print("until this is fixed, and no A/B will notice.")
        return 1

    print(f"\nam2hook.dll imports none of them, so every caller still reaches "
          f"the patched slot.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
