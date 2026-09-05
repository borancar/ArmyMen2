#!/usr/bin/env python3
"""Check every COM vtable src/platform declares against the SDK's slot order.

The reconstruction reaches DirectX through the IDirectDraw_* macros, which
name a method, so the order of a platform vtable was private to
src/platform -- and ddraw.h said so, and put SetDisplayMode last so one
macro could declare the prefix two interfaces share. The PE loader in
src/hybrid runs the ORIGINAL binary over the same layer, and the original
indexes by NUMBER: InitDirectDraw calls slot 21 of IDirectDraw2 with six
dwords, which in that private order was WaitForVerticalBlank taking three,
and the function returned twelve bytes off into its own HWND argument.

So the order is not private any more. This preprocesses each platform
header, reads the method names out of every *Vtbl struct, and compares
them slot by slot with the same interface in mingw's SDK headers, which
declare it with DECLARE_INTERFACE_ / STDMETHOD. A mismatch names the slot.
Exits 1 on any.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MINGW = "/usr/i686-w64-mingw32/sys-root/mingw/include"
HEADERS = {
    "ddraw.h":   "ddraw.h",
    "dinput.h":  "dinput.h",
    "dsound.h":  "dsound.h",
    "dplay.h":   "dplay.h",
    "dplobby.h": "dplobby.h",
}
# The platform's struct name -> the SDK's interface name.
RENAME = {"IDirectInputAVtbl": "IDirectInputA", "IDirectInputDeviceAVtbl": "IDirectInputDeviceA"}


def platform_vtables(header):
    src = subprocess.run(
        ["g++", "-m32", "-E", "-P", "-isystem", os.path.join(REPO, "src/platform/include"),
         "-include", "callconv.h", "-x", "c++", os.path.join(REPO, "src/platform/include", header)],
        capture_output=True, text=True, check=True).stdout
    out = {}
    for m in re.finditer(r"typedef struct (\w+Vtbl) \{(.*?)\} \1;", src, re.S):
        name = RENAME.get(m.group(1), m.group(1)[:-4])
        out[name] = re.findall(r"\*\s*(\w+)\s*\)\s*\(", m.group(2))
    return out


def sdk_vtables(header):
    text = open(os.path.join(MINGW, header), errors="replace").read()
    out = {}
    for m in re.finditer(r"DECLARE_INTERFACE_?\(\s*(\w+)\s*(?:,\s*\w+\s*)?\)\s*\{(.*?)\n\};", text, re.S):
        name = m.group(1)
        body = m.group(2)
        methods = []
        for line in body.splitlines():
            line = line.strip()
            mm = re.search(r"STDMETHOD_?\((?:[^,()]+,\s*)?(\w+)\)", line)
            if mm:
                methods.append(mm.group(1))
        if methods:
            out[name] = methods
    return out


def main():
    bad = 0
    total = 0
    for ours, theirs in HEADERS.items():
        pv = platform_vtables(ours)
        sv = sdk_vtables(theirs)
        for name, methods in pv.items():
            if name == "IUnknown" or name not in sv:
                continue
            total += 1
            sdk = sv[name]
            n = min(len(methods), len(sdk))
            for i in range(n):
                if methods[i] != sdk[i]:
                    print(f"{ours}: {name} slot {i}: platform has {methods[i]}, SDK has {sdk[i]}")
                    bad += 1
                    break
            else:
                if len(methods) < len(sdk):
                    # A shorter vtable is fine only if the original never calls past it.
                    print(f"{ours}: {name}: platform declares {len(methods)} of the SDK's "
                          f"{len(sdk)} slots (a call past slot {len(methods) - 1} lands off the end)")
                    bad += 1
    print(f"checkvtables: {total} interfaces compared, {bad} mismatched")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
