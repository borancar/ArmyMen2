#!/usr/bin/env python3
"""List every script the game ships, for the AM2_PARSE_ALL probe.

The probe reads this rather than enumerating directories itself: directory
enumeration is Win32, and src/game/ that is not under win32/ does not name a
Win32 type. A plain list read with fopen keeps it that way.

Paths are absolute in Windows form, because the game chdirs into the map
directory before loading a script -- ReadScript is handed a bare
"bootcamp1.txt" -- so nothing relative to the game root can be opened from
inside it.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "build/scripts.txt")


def main():
    rows = []
    for root, _dirs, names in os.walk(am2.GAME_DIR):
        for n in sorted(names):
            if not n.lower().endswith(".txt") or n == "scripts.txt":
                continue
            rel = os.path.relpath(os.path.join(root, n), am2.GAME_DIR)
            # Only what the interpreter is meant to see. The .txt files at the
            # top level are prose and lists -- EULA, readme, campaign, mpmaps
            # -- and feeding EULA.txt to the statement dispatcher takes the
            # process down, which is a fair answer to a nonsense question.
            if not (rel.startswith("data" + os.sep) or
                    rel.startswith("rules" + os.sep)):
                continue
            rows.append("C:\\GOG Games\\Army Men II\\" +
                        rel.replace(os.sep, "\\"))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        for r in sorted(rows):
            fh.write(r + "\n")
    print("-> %s  (%d scripts)" % (os.path.relpath(OUT), len(rows)))


if __name__ == "__main__":
    main()
