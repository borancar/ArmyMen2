"""Check that src/game/win32 holds the platform code and the flat half does not.

The split is the answer to "what still talks to the outside world" in directory
form, and it only means that while it is true. Two ways it can rot, and this
refuses both:

  a flat module that names a Win32 or COM type    -- the platform half has
                                                     leaked downward
  a win32/ module that names none                 -- something is filed as
                                                     platform code that is not

Comments are stripped before scanning, which is not fussiness. script.cpp
carries a comment explaining that it forward-declares PreloadSprite rather than
including win32/sprite.h precisely BECAUSE AM2_Sprite has an
LPDIRECTDRAWSURFACE in it -- a scan that reads comments fails the very file
that documents the rule.

Includes are checked as well as identifiers: a flat module that pulls
inject/win32.h, windows.h or ddraw.h has the types whether it names one or not.
That is followed transitively through the project's own headers, since a header
can be the thing that leaks.

    tools/checksplit.py
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(REPO, "src", "game")
WIN32 = os.path.join(GAME, "win32")

# Naming one of these is what makes a module platform code.
PLATFORM = re.compile(
    r"\b(?:IDirect[A-Za-z0-9]+_[A-Za-z0-9]+|LPDIRECT[A-Z0-9]+|HWND|HRESULT|HDC"
    r"|HINSTANCE|HANDLE|WINAPI|CALLBACK|WPARAM|LPARAM|LRESULT|DDSURFACEDESC"
    r"|WAVEFORMATEX|HMMIO|MMRESULT|timeSetEvent|MessageBoxA|CoCreateInstance"
    r"|PostMessageA|GetTickCount|CreateThread|WaitForSingleObject|RegOpenKeyExA"
    r"|RegCreateKeyExA|GetDC|TextOutA|CreateFontA)\b")

WIN32_HEADERS = ("inject/win32.h", "windows.h", "ddraw.h", "dsound.h",
                 "dinput.h", "dplay.h", "mmsystem.h")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def includes(path, seen=None):
    """Every header a file reaches, following the project's own includes."""
    if seen is None:
        seen = set()
    if path in seen or not os.path.exists(path):
        return seen
    seen.add(path)
    here = os.path.dirname(path)
    with open(path, errors="replace") as fh:
        for m in re.finditer(r'#include\s+[<"]([^">]+)[">]', fh.read()):
            name = m.group(1)
            if any(name.endswith(h) for h in WIN32_HEADERS):
                seen.add(name)
                continue
            nxt = os.path.normpath(os.path.join(here, name))
            if nxt.startswith(REPO):
                includes(nxt, seen)
    return seen


def main():
    bad = []

    flat = sorted(f for f in os.listdir(GAME) if f.endswith((".cpp", ".h")))
    for name in flat:
        path = os.path.join(GAME, name)
        body = strip_comments(open(path, errors="replace").read())
        hit = PLATFORM.search(body)
        if hit:
            bad.append("%s names %s -- move it to win32/ or forward declare"
                       % (name, hit.group(0)))
        reached = includes(path)
        for h in WIN32_HEADERS:
            if any(str(r).endswith(h) for r in reached):
                bad.append("%s reaches %s" % (name, h))
                break

    win = sorted(f for f in os.listdir(WIN32) if f.endswith(".cpp"))
    for name in win:
        path = os.path.join(WIN32, name)
        body = strip_comments(open(path, errors="replace").read())
        if not PLATFORM.search(body):
            bad.append("win32/%s names no platform type -- does it belong here?"
                       % name)

    print("  flat modules  %d" % len(flat))
    print("  win32 modules %d" % len(win))
    if bad:
        print("\n  SPLIT BROKEN:")
        for b in bad:
            print("    " + b)
        return 1
    print("\n  every win32/ module names a platform type; no flat module names\n"
          "  one or reaches a Win32 header, even transitively.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
