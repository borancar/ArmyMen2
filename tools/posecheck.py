"""Check WeaponPoseIndex against the original, exhaustively.

The function answers from three things: the object's class (0, 1 or 2, from
ClassifyByCode74), one bit of OBJ_OFF_FIELD_578, and a 43-entry table indexed
by the weapon's code. That whole space is 3 x 2 x 46 cases including a null
weapon and codes either side of the range, so it can be ENUMERATED rather than
sampled -- the same argument as tools/moviecheck.py.

It matters here because no A/B can reach it. A Boot Camp mission issues a
handful of weapon codes, so the other forty entries of the arm table are
verified by this or by nothing. The table is transcribed byte for byte into
item.cpp and a single wrong byte is exactly what this catches.

    tools/posecheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH, SCRATCH_SZ

ADDR = 0x004494A0
POSE_BY_CLASS = 0x00475180

OBJ  = SCRATCH
ROW  = SCRATCH + 0x1000
WPN  = SCRATCH + 0x2000
CODE = SCRATCH + 0x2400

OFF_ROWS  = 0x74
OFF_578   = 0x578
OFF_C0    = 0xC0
ROW_KIND  = 0x4C

# Row kinds that make ClassifyByCode74 answer 1, 2 and 0 -- read off its own
# index table at 0x0040D820.
CLASS_OF_ROW_KIND = {7: 1, 8: 2, 9: 0}

ARM_OF_CODE = [
    0, 3, 3, 0, 3, 3, 0, 0, 0, 0, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 3, 3, 0, 0,
    3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 3, 3, 2,
]

POSE_CLASS2 = 6
POSE_STAND = 1
POSE_STAND_ARMED = 0x19
POSE_KNEEL = 4
POSE_KNEEL_ARMED_A = 0x1A
POSE_KNEEL_ARMED_B = 0x1F


def expect(default_table, cls, armed, code):
    """`code` is the raw weapon code; None means a null weapon."""
    if code is None:
        return default_table[cls]

    i = code - 1
    if not 0 <= i < len(ARM_OF_CODE):
        return default_table[cls]

    arm = ARM_OF_CODE[i]
    if arm == 0:
        if cls == 1:
            return POSE_KNEEL_ARMED_A if armed else POSE_KNEEL
        if cls == 2:
            return POSE_CLASS2
        return POSE_STAND_ARMED if armed else POSE_STAND
    if arm == 1:
        if cls == 1:
            return POSE_KNEEL_ARMED_B if armed else POSE_KNEEL
        if cls == 2:
            return POSE_CLASS2
        return POSE_STAND
    if arm == 2:
        return POSE_STAND
    return default_table[cls]


def main():
    emu = Emu()
    img = am2.Image()
    default_table = list(struct.unpack("<3i", img.read(POSE_BY_CLASS, 12)))

    bad = 0
    n = 0
    for row_kind, cls in sorted(CLASS_OF_ROW_KIND.items()):
        for armed in (0, 1):
            for code in [None] + list(range(0, 46)):
                buf = bytearray(SCRATCH_SZ)

                def put(off, val):
                    struct.pack_into("<I", buf, off, val & 0xFFFFFFFF)

                put(OBJ - SCRATCH + OFF_ROWS, ROW)
                put(OBJ - SCRATCH + OFF_578, armed)
                struct.pack_into("<h", buf, ROW - SCRATCH + ROW_KIND, row_kind)
                put(WPN - SCRATCH + OFF_C0, CODE)
                put(CODE - SCRATCH, code if code is not None else 0)

                got, _ = emu.call(ADDR, [OBJ, WPN if code is not None else 0],
                                  bytes(buf))
                want = expect(default_table, cls, armed, code)
                n += 1
                if got is None:
                    bad += 1
                    print("  cls=%d armed=%d code=%s -> FAULTED" %
                          (cls, armed, code))
                elif got != want:
                    bad += 1
                    print("  cls=%d armed=%d code=%s -> %d, expected %d" %
                          (cls, armed, code, got, want))

    print("%d case(s), %d disagree" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
