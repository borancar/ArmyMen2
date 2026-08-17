#!/usr/bin/env python3
"""Check that the samples reaching DirectSound are the samples in the file.

    tools/checkwaves.py [logfile]

The audio reconstruction had no way to be verified for a long time. DirectSound
will not start without a device, this machine has none, and every other check
in the project -- the build, the fingerprints, the A/B on all three
configurations -- passes whether the audio code is right or wrong. It was
written by reading and nothing else, which is exactly the situation a bug
survives.

Two things together fix that, and neither needs the game to be audible.

ALSA's `null` plugin is built into libasound and needs no sound server: it
accepts a stream and throws it away. Point ALSA_CONFIG_PATH at
tools/alsa/asoundrc and Wine has a device, DirectSound starts, and the whole
sample path executes.

AM2_DUMP_SOUND=1 then makes each wave print its name, its length and an FNV-1a
hash of the samples it is about to hand over. This reads those lines back,
finds each .WAV in the install, parses out its `data` chunk and hashes it the
same way. Matching means the bytes that reached DirectSound are the bytes in
the file, which exercises WaveOpenFile, WaveReadFile, LoadWaveSound and
everything under them.

    export ALSA_CONFIG_PATH=$PWD/tools/alsa/asoundrc AM2_DUMP_SOUND=1
    AM2_DISPLAY=:99 tools/drive.sh start 25 "ARGS=-nointro -dbg"
    tools/checkwaves.py

It found a real defect the first time it ran: LoadWaveSound was leaving the
DSBUFFERDESC without a format or a length, because the wave reader writes both
straight into that structure and the reconstruction had been passing separate
locals. Every CreateSoundBuffer in the game was failing. Nothing else in the
project would have noticed.

What it does NOT cover is the Lock/copy/Unlock inside FillSoundBuffer, which
is the half that needs a buffer somebody can read back.
"""

import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINE = re.compile(r"sound: (\S+)\s+(\d+) bytes\s+fnv1a=([0-9a-f]{8})")


def fnv1a(data):
    h = 2166136261
    for c in data:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def wav_data(path):
    """The bytes of a RIFF/WAVE `data` chunk, or None."""
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        return None
    off = 12
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        size = struct.unpack_from("<I", d, off + 4)[0]
        if cid == b"data":
            return d[off + 8:off + 8 + size]
        off += 8 + size + (size & 1)     # chunks are word-aligned
    return None


def make_config():
    """GAMEDIR and LOGFILE, from the same place everything else gets them."""
    out = subprocess.run(["make", "-s", "config"], cwd=REPO,
                         capture_output=True, text=True).stdout
    cfg = {}
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip().strip("'\"")
    return cfg


def main():
    cfg = make_config()
    gamedir = cfg.get("GAMEDIR")
    if not gamedir or not os.path.isdir(gamedir):
        sys.exit("checkwaves: no GAMEDIR -- run with DISPLAY set")
    log = sys.argv[1] if len(sys.argv) > 1 else os.path.join(gamedir,
                                                             cfg.get("LOGFILE", ""))
    if not os.path.isfile(log):
        sys.exit(f"checkwaves: no log at {log}")

    # The install spells names inconsistently; match case-insensitively.
    waves = {}
    for root, _dirs, files in os.walk(gamedir):
        for f in files:
            if f.lower().endswith(".wav"):
                waves.setdefault(f.lower(), os.path.join(root, f))

    ok = bad = missing = 0
    with open(log, errors="replace") as fh:
        for line in fh:
            m = LINE.match(line)
            if not m:
                continue
            name, length, want = m.group(1), int(m.group(2)), int(m.group(3), 16)
            path = waves.get(name.lower())
            if not path:
                missing += 1
                print(f"  no such file        {name}")
                continue
            data = wav_data(path)
            if data is None:
                missing += 1
                print(f"  not a RIFF/WAVE     {name}")
                continue
            if len(data) == length and fnv1a(data[:length]) == want:
                ok += 1
            else:
                bad += 1
                print(f"  MISMATCH {name:<20} uploaded {length}/{want:08x}"
                      f"  file {len(data)}/{fnv1a(data):08x}")

    if not (ok or bad or missing):
        print("no `sound:` lines in the log -- was AM2_DUMP_SOUND=1 set, and did\n"
              "DirectSound start? Without ALSA_CONFIG_PATH it will not have.")
        return 1

    print(f"\n{ok} wave(s) match the .WAV data chunk exactly, "
          f"{bad} differ, {missing} not found")
    return 1 if (bad or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
