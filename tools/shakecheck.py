"""Check ShakeAt's falloff against the original, exhaustively over its inputs.

ShakeAt (0x0042B360) decides how hard an explosion shakes the screen: it takes
the midpoint of the view rectangle, measures ApproxDist to the blast, and turns
the result into one of the four ADDR_SHAKE_PRESETS records. Everything it does
is decided by four view globals, a point and a strength, so the space can be
ENUMERATED rather than sampled -- the same argument as tools/moviecheck.py,
tools/posecheck.py and tools/formationcheck.py.

IT EXISTS BECAUSE NO DRIVE REACHES IT. A probe on a live MAP 01 mission, past
both dialogs and thirty seconds into play, reads ShakeAt=0 and StartShake=0:
nothing exploded near enough to the view to shake it. So this tool is the
verification or there is none, and `ab.sh` cannot be quoted for this function.

It calls the ORIGINAL through the emulator and lets it call the original
StartShake underneath, then reads the four shake globals back. Those are zeroed
before each case, and StartShake takes the maximum of each field against what
is already there, so from zero every field lands and the end state is exactly
the preset that was chosen. A case that chose nothing leaves all four at zero
and is distinguished from one that chose a zero preset by whether 0x0042B2E0
was executed at all -- Emu already records every address it runs.

What it covers that reading cannot: the FLAT TOP of the falloff, where the two
arms are alternatives rather than factors and the far arm throws away the 1.0
the near arm loaded; and the preset index running PAST the four-entry table,
which the clamp to ten allows and which the tool reads out of the image rather
than modelling.

IT FOUND A REAL DEFECT ON ITS FIRST RUN WITH A NON-ZERO SEED, and not in
ShakeAt. The reconstructed StartShake had its four maxima written as NESTED
early returns, under a confident comment arguing that reading them as four
separate maxima "would be wrong for every case but the strongest". Each `jle`
in the original jumps only past its OWN store. From an all-zero state the two
readings agree exactly -- every preset field is positive, so nothing is ever
refused -- so the first version of this tool passed with the bug in, and no
drive could have caught it either: StartShake's counter is 0 everywhere.

TWO OF ITS CONSTANTS ARE ONLY WEAKLY OBSERVED, and one of those is provable
rather than a gap. AM2_SHAKE_FALLOFF is 1/512 and AM2_SHAKE_FAR - AM2_SHAKE_NEAR
is 832 - 320 = 512, so the ramp meets the flat top at exactly 1.0 and the
piecewise function is CONTINUOUS. Moving the near radius by one, or swapping
the boundary's `>` for `>=`, therefore cannot be detected by anything -- the
two arms compute the same number there. Moving it to 200 does fail, on 192
cases, and only because OFFSETS covers the 250..300 band where the difference
survives the truncation.

    tools/shakecheck.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
from vectors import Emu, SCRATCH

ADDR_SHAKE_AT    = 0x0042B360
ADDR_START_SHAKE = 0x0042B2E0

VIEW_ORIGIN_X = 0x00514E14
VIEW_ORIGIN_Y = 0x00514E18
VIEW_FAR_X    = 0x00514E1C
VIEW_FAR_Y    = 0x00514E20

SHAKE_TIME      = 0x00514E64
SHAKE_PHASE_X   = 0x00514E68
SHAKE_STEP_X    = 0x00514E6C
SHAKE_PHASE_Y   = 0x00514E70
SHAKE_STEP_Y    = 0x00514E74
SHAKE_AMPLITUDE = 0x00514E78

SHAKE_PRESETS   = 0x00486170
SHAKE_NEAR      = 0x140
SHAKE_FAR       = 0x340
STRENGTH_MAX    = 10

# Three view rectangles, one at the origin and two well into a map, so the
# midpoint arithmetic is exercised with and without a large offset.
RECTS = (
    (0, 0, 640, 480),
    (1000, 2000, 1640, 2480),
    (100, 100, 900, 700),
)

# Offsets from the centre, chosen to straddle both radii exactly: 320 is the
# last value inside the flat top and 321 the first outside it; 831 is the last
# that shakes at all and 832 the first that does not.
OFFSETS = (0, 1, 100, 150, 200, 250, 280, 300, 319, 320, 321, 400, 500, 600, 700,
           800, 830, 831, 832, 833, 900, 2000)

STRENGTHS = (-5, -1, 0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 100)

# The shake state a case STARTS from. StartShake takes the maximum of each
# field against what is already running, and the tests are NESTED -- a field
# that is not greater stops everything after it -- so from an all-zero state
# every preset lands whole and the nesting is unobservable. Measured: with
# only the zero seed, flattening the nesting in the model below passes all
# 2,268 cases. The other two seeds are a weak shake and a strong one, which is
# what makes those branches reachable at all.
#
# THE FOURTH SEED HAS NEGATIVE STEPS AND IT IS NOT DECORATION. Every preset
# step is positive and so are the other seeds, so comparing the steps signed
# instead of by absolute value passes all 5,103 cases without it. A step goes
# negative in play -- ADDR_SHAKE_STEP_X's sign flips at a limit as ScrollDecay
# runs -- so that state is reachable and the comparison really is on magnitude.
SEEDS = (
    (0, 0, 0, 0),
    (250, 25, 12, 2),      # preset 1
    (750, 45, 17, 2),      # preset 3, the strongest
    (400, -30, -20, 1),    # a shake whose steps have gone negative
)


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


class Harness:
    def __init__(self):
        self.emu = Emu()
        self.img = am2.Image()

    def _poke(self, addr, value):
        self.emu.uc.mem_write(addr, struct.pack("<i", value))

    def _peek(self, addr):
        return struct.unpack("<i", self.emu.uc.mem_read(addr, 4))[0]

    def preset(self, index):
        """Four int32 at the index, read from the IMAGE -- past the table too."""
        at = SHAKE_PRESETS + index * 16
        return struct.unpack("<4i", self.img.read(at, 16))

    def original(self, rect, px, py, strength, seed):
        """(called, time, stepX, stepY, amplitude) as the original leaves them."""
        for a, v in ((VIEW_ORIGIN_X, rect[0]), (VIEW_ORIGIN_Y, rect[1]),
                     (VIEW_FAR_X, rect[2]), (VIEW_FAR_Y, rect[3])):
            self._poke(a, v)
        self._poke(SHAKE_PHASE_X, 0)
        self._poke(SHAKE_PHASE_Y, 0)
        for a, v in ((SHAKE_TIME, seed[0]), (SHAKE_STEP_X, seed[1]),
                     (SHAKE_STEP_Y, seed[2]), (SHAKE_AMPLITUDE, seed[3])):
            self._poke(a, v)

        scratch = struct.pack("<hh", s16(px), s16(py)) + b"\0" * 60
        self.emu.seen.clear()
        eax, _ = self.emu.call(ADDR_SHAKE_AT, [SCRATCH, strength], scratch)
        if eax is None:
            return None
        return (ADDR_START_SHAKE in self.emu.seen,
                self._peek(SHAKE_TIME), self._peek(SHAKE_STEP_X),
                self._peek(SHAKE_STEP_Y), self._peek(SHAKE_AMPLITUDE))

    def approx_dist(self, ax, ay, bx, by):
        """The image's own ApproxDist, so the model has one source of truth."""
        scratch = struct.pack("<hhhh", s16(ax), s16(ay), s16(bx), s16(by)) \
                + b"\0" * 56
        eax, _ = self.emu.call(0x0042DDE0, [SCRATCH, SCRATCH + 4], scratch)
        return None if eax is None else eax & 0xFFFFFFFF


def rule(h, rect, px, py, strength, seed):
    """What win32/mapdraw.cpp's ShakeAt implements, restated."""
    if strength <= 0:
        return (False,) + seed
    if strength > STRENGTH_MAX:
        strength = STRENGTH_MAX

    cx = rect[0] + (rect[2] - rect[0]) // 2
    cy = rect[1] + (rect[3] - rect[1]) // 2

    dist = h.approx_dist(px, py, cx, cy)
    if dist is None:
        return None
    if dist >= SHAKE_FAR:
        return (False,) + seed

    scale = ((SHAKE_FAR - dist) * (1.0 / 512.0)) if dist > SHAKE_NEAR else 1.0
    index = int(strength * scale)          # _ftol truncates toward zero
    if index < 1:
        return (False,) + seed

    ms, sx, sy, amp = h.preset(index)
    # StartShake: four INDEPENDENT maxima, each `jle` jumping only past its own
    # store. The two STEPS compare by absolute value -- a step is signed and
    # "stronger" means further from zero -- while the time and the amplitude
    # are plain magnitudes.
    t, cx, cy, ca = seed
    return (True,
            ms if ms > t else t,
            sx if abs(sx) > abs(cx) else cx,
            sy if abs(sy) > abs(cy) else cy,
            amp if amp > ca else ca)


def main():
    h = Harness()
    n = bad = shaken = 0

    for rect in RECTS:
        cx = rect[0] + (rect[2] - rect[0]) // 2
        cy = rect[1] + (rect[3] - rect[1]) // 2
        for off in OFFSETS:
            for px, py in ((cx + off, cy), (cx, cy + off), (cx - off, cy - off)):
                for strength in STRENGTHS:
                    for seed in SEEDS:
                        got  = h.original(rect, px, py, strength, seed)
                        want = rule(h, rect, px, py, strength, seed)
                        n += 1
                        if got is not None and got[0]:
                            shaken += 1
                        if got is None or want is None:
                            bad += 1
                            print("  rect=%s at=(%d,%d) s=%d seed=%s -> FAULTED"
                                  % (rect, px, py, strength, seed))
                        elif got != want:
                            bad += 1
                            if bad <= 8:
                                print("  rect=%s at=(%d,%d) s=%d seed=%s"
                                      " -> %s, expected %s"
                                      % (rect, px, py, strength, seed,
                                         got, want))

    print("%d case(s), %d shook, %d disagree" % (n, shaken, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
