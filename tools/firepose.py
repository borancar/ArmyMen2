"""Check SelectFirePose against the original, exhaustively.

The function answers from six things and every one of them is an argument or a
plain field: the item kind (43 of them through an EIGHT-arm table), the object
class ClassifyByCode74 gives from the first row, whether the current pose is
one of nine the function calls BRACED, whether the soldier kind is 7 or 8, the
speed, and the caller's `seen` and `ready`. That whole space enumerates, which
is the same argument tools/posecheck.py makes for its smaller sibling one
function over.

It matters here for the reason it matters there, and more so. This function is
COLD: its one caller is TrooperFire, which nothing this project can drive
reaches, so an A/B compares it not at all. Every one of the eight arms is
verified by this or by nothing.

AND ITS OUTPUT IS NOT THE RETURN VALUE. It writes SIGHTCOUT_OFF_STATE and
answers 1 on every path past its refusals, so a check that compared `eax` would
pass with the whole body deleted. The pose is seeded with a sentinel first, so
"wrote nothing" -- which two arms really do -- is distinguishable from "wrote
zero".

NOTHING IS HOOKED. Its two callees, ClassifyByCode74 and Type2Field5A4Set, are
pure reads of the object and run for real under the emulator; the only reason
tools/explcheck.py and tools/collectcheck.py hook anything is that theirs reach
the heap and the map.

    tools/firepose.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH, SCRATCH_SZ

ADDR = 0x00449AB0

OBJ   = SCRATCH
ROW   = SCRATCH + 0x1000
WPN   = SCRATCH + 0x2000
KIND  = SCRATCH + 0x2400
SIGHT = SCRATCH + 0x3000

OBJ_TYPE      = 0x00
OBJ_FLAGS     = 0x08
OBJ_SPEED     = 0x44
OBJ_ROWS      = 0x74
OBJ_POSE      = 0x538
OBJ_SOLDIER   = 0x544
OBJ_FIELD_5A4 = 0x5A4
WPN_TYPEREC   = 0xC0
ROW_FRAME     = 0x4C
SIGHT_STATE   = 0x08
SIGHT_SEEN    = 0x10

SENTINEL = 0x7BADF00D

# Row frame values that make ClassifyByCode74 answer 1, 2 and 0, read off its
# own index table at 0x0040D820 -- the same three posecheck.py uses.
CLASS_OF_ROW_FRAME = {7: 1, 8: 2, 9: 0}

# The nine poses SelectFirePose treats as braced, and one that is not.
BRACED = (0x19, 0x1C, 0x13, 0x1A, 0x1D, 0x14, 0x06, 0x1E, 0x15)
UNBRACED = 0x00

POSE_KNEEL          = 4
POSE_CARRY          = 5
POSE_FLAME_KNEEL    = 8
POSE_FLAME_CLASS2   = 9
POSE_GRENADE_STAND  = 0x13
POSE_GRENADE_KNEEL  = 0x14
POSE_RAISE_STAND    = 0x16
POSE_RAISE_KNEEL    = 0x17
POSE_STAND_ARMED    = 0x19
POSE_KNEEL_ARMED_A  = 0x1A
POSE_GUN_STAND      = 0x1C
POSE_GUN_KNEEL      = 0x1D
POSE_CLASS2_ARMED   = 0x1E
POSE_KNEEL_ARMED_B  = 0x1F

GUNS  = (1, 7, 8, 9, 10, 29, 30)
THROW = (5, 11, 12)
CALL  = (24, 25, 26, 39, 40)


def expect(kind, cls, braced, aimed, moving):
    """The pose the model says goes into SIGHTCOUT_OFF_STATE, or None for
    'writes nothing'."""
    if kind in GUNS:
        if moving:
            return None
        if cls == 2:
            return POSE_CLASS2_ARMED
        if cls == 1:
            return (POSE_GUN_KNEEL if aimed else POSE_KNEEL_ARMED_A) \
                if braced else POSE_RAISE_KNEEL
        return (POSE_GUN_STAND if aimed else POSE_STAND_ARMED) \
            if braced else POSE_RAISE_STAND

    if kind == 2:
        if cls == 2:
            return POSE_KNEEL
        if cls == 1:
            return (POSE_GRENADE_KNEEL if aimed else POSE_KNEEL_ARMED_A) \
                if braced else POSE_RAISE_KNEEL
        return (POSE_GRENADE_STAND if aimed else POSE_STAND_ARMED) \
            if braced else POSE_RAISE_STAND

    if kind == 3:
        if moving:
            return None
        if cls == 1:
            return POSE_FLAME_KNEEL
        if cls == 2:
            return POSE_FLAME_CLASS2
        return None

    if kind == 4:
        if cls == 2:
            return POSE_KNEEL
        if cls == 1:
            return (POSE_GUN_KNEEL if aimed else POSE_KNEEL_ARMED_A) \
                if braced else POSE_RAISE_KNEEL
        return (POSE_GUN_STAND if aimed else POSE_STAND_ARMED) \
            if braced else POSE_RAISE_STAND

    if kind in THROW:
        return POSE_KNEEL if 0 < cls <= 2 else POSE_CARRY
    if kind in CALL:
        return POSE_KNEEL_ARMED_B if 0 < cls <= 2 else POSE_CARRY
    if kind == 43:
        return POSE_GUN_STAND
    return None


def build(kind, row_frame, pose, soldier, speed, seen, obj_type=0, f5a4=0,
          flags=0):
    buf = bytearray(SCRATCH_SZ)

    def put(base, off, val, fmt="<I"):
        struct.pack_into(fmt, buf, base - SCRATCH + off, val & 0xFFFFFFFF)

    put(OBJ, OBJ_TYPE, obj_type)
    put(OBJ, OBJ_FLAGS, flags)
    put(OBJ, OBJ_SPEED, speed)
    put(OBJ, OBJ_ROWS, ROW)
    put(OBJ, OBJ_POSE, pose)
    put(OBJ, OBJ_SOLDIER, soldier)
    put(OBJ, OBJ_FIELD_5A4, f5a4)
    struct.pack_into("<h", buf, ROW - SCRATCH + ROW_FRAME, row_frame)
    put(WPN, WPN_TYPEREC, KIND)
    put(KIND, 0, kind)
    put(SIGHT, SIGHT_STATE, SENTINEL)
    put(SIGHT, SIGHT_SEEN, seen)
    return bytes(buf)


def run(emu, buf, ready, obj=OBJ):
    got, after = emu.call(ADDR, [obj, WPN, SIGHT, ready], buf)
    if got is None:
        return None, None
    state, = struct.unpack_from("<I", after, SIGHT - SCRATCH + SIGHT_STATE)
    return got, (None if state == SENTINEL else state)


def emit(rows, path):
    """Write the corpus for tests/selftest.cpp to replay against the C.

    The oracle above checks the READING against the original; this checks the
    C against the same recorded answers, which is the half a Python model
    cannot cover on its own. Only the six inputs and the two answers are
    stored -- the object is rebuilt from them, so the file cannot drift from
    what SelectFirePose is handed.
    """
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/firepose.py -- do not edit.\n"
                 " *\n"
                 " * One row per case, with what the ORIGINAL at 0x00449AB0\n"
                 " * answered and the pose it wrote into SIGHTCOUT_OFF_STATE.\n"
                 " * `wrote` is 0 where it wrote nothing, which two of its\n"
                 " * eight arms really do.\n"
                 " */\n"
                 "typedef struct { int32_t kind; int32_t frame; int32_t pose;\n"
                 "                 int32_t soldier; int32_t speed; int32_t seen;\n"
                 "                 int32_t ready; int32_t objtype; int32_t f5a4;\n"
                 "                 int32_t flags; int32_t nullobj;\n"
                 "                 int32_t rc; int32_t wrote;\n"
                 "                 int32_t state; } AM2_FirePoseVector;\n\n"
                 "static const AM2_FirePoseVector am2_firepose_vectors[] = {\n")
        for r in rows:
            fh.write("  { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
                     "%d, %d },\n" % r)
        fh.write("};\n")
    print("wrote %s, %d row(s)" % (path, len(rows)))


def main():
    emu = Emu()
    bad = 0
    n = 0
    rows = []

    for row_frame, cls in sorted(CLASS_OF_ROW_FRAME.items()):
        for braced in (0, 1):
            pose = BRACED[0] if braced else UNBRACED
            for soldier in (0, 7):
                for speed in (0, 1):
                    for seen in (0, 1):
                        for ready in (0, 1):
                            for kind in range(0, 46):
                                buf = build(kind, row_frame, pose, soldier,
                                            speed, seen)
                                rc, state = run(emu, buf, ready)
                                # Soldier kind 7 is braced whatever the pose.
                                want = expect(kind, cls,
                                              braced or soldier == 7,
                                              seen != 0 and ready != 0,
                                              speed > 0)
                                n += 1
                                if rc is not None:
                                    rows.append((kind, row_frame, pose,
                                                 soldier, speed, seen, ready,
                                                 0, 0, 0, 0,
                                                 rc, 0 if state is None else 1,
                                                 0 if state is None else state))
                                if rc is None:
                                    bad += 1
                                    print("  kind=%d cls=%d braced=%d sk=%d "
                                          "spd=%d seen=%d rdy=%d -> FAULTED"
                                          % (kind, cls, braced, soldier,
                                             speed, seen, ready))
                                elif rc != 1 or state != want:
                                    bad += 1
                                    print("  kind=%d cls=%d braced=%d sk=%d "
                                          "spd=%d seen=%d rdy=%d -> rc=%s "
                                          "pose=%s, expected rc=1 pose=%s"
                                          % (kind, cls, braced, soldier,
                                             speed, seen, ready, rc,
                                             state, want))

    # Which poses are braced, swept over a RANGE rather than over BRACED
    # itself. Driving this loop from the model's own tuple is the trap
    # tools/boolcheck.py records: deleting an entry then deletes the case that
    # would have caught it, and the symptom is a corpus three cases SMALLER
    # rather than a failure. Measured here, not reasoned -- with the sweep
    # written the other way, removing 0x06 from BRACED passed.
    for pose in range(0, 0x25):
        for row_frame, cls in sorted(CLASS_OF_ROW_FRAME.items()):
            buf = build(9, row_frame, pose, 0, 0, 1)
            rc, state = run(emu, buf, 1)
            want = expect(9, cls, pose in BRACED, True, False)
            n += 1
            if rc is not None:
                rows.append((9, row_frame, pose, 0, 0, 1, 1, 0, 0, 0, 0,
                             rc, 0 if state is None else 1,
                             0 if state is None else state))
            if rc != 1 or state != want:
                bad += 1
                print("  braced-set pose=0x%X cls=%d -> rc=%s pose=%s, "
                      "expected %s" % (pose, cls, rc, state, want))

    # The four refusals, each of which must answer 0 and write nothing.
    refusals = [
        ("null object", dict(obj=0)),
        ("destroyed", dict(flags=4)),
        ("type 2 with +0x5A4", dict(obj_type=2, f5a4=1)),
        ("soldier kind 8", dict(soldier=8)),
    ]
    for name, kw in refusals:
        kwsave = dict(kw)
        obj = kw.pop("obj", OBJ)
        buf = build(9, 9, UNBRACED, kw.pop("soldier", 0), 0, 1, **kw)
        rc, state = run(emu, buf, 1, obj=obj)
        n += 1
        if rc is not None:
            rows.append((9, 9, UNBRACED, kwsave.get("soldier", 0), 0, 1, 1,
                         kwsave.get("obj_type", 0), kwsave.get("f5a4", 0),
                         kwsave.get("flags", 0), 1 if obj == 0 else 0,
                         rc, 0 if state is None else 1,
                         0 if state is None else state))
        if rc != 0 or state is not None:
            bad += 1
            print("  refusal %-22s -> rc=%s pose=%s, expected rc=0 and no "
                  "write" % (name, rc, state))

    print("%d case(s), %d disagree" % (n, bad))
    if not bad and "--emit" in sys.argv:
        emit(rows, sys.argv[sys.argv.index("--emit") + 1])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
