#!/usr/bin/env python3
"""Check the multiplayer row's ink and paper against the original, exhaustively.

MpNameInk (0x00432C50) and MpNamePaper (0x00432CE0) colour one player row of
the host/join panel.  CLAUDE.md records them among the functions that need "a
live DirectPlay session with a second player, which this machine cannot open"
-- and both counters are BLIND besides, every caller being reconstructed, so
`counts` says nothing either.  With no drive and no counter, an oracle is the
only thing that can speak for them.

Their input space is SMALL, so it is enumerated rather than sampled: paper is
decided by three flags, and ink by which of three latency bands the link is in,
whether the row is our own, whether the player record exists, and whether it
has gone silent.  That is the moviecheck/posecheck argument -- where the space
is small, stop sampling.

THREE STUBS, and each is the standard answer to a wall this project has hit
before.  GetTickCount is an IMPORT, so the IAT slot is pointed at a stub in a
mapped page -- collectcheck's finding, that the redirect target must be mapped
because a code hook fires before the instruction executes.  PlayerLatency and
FindPlayerById are stubbed so the latency and the presence of a record become
inputs rather than consequences of a comm object nobody can build here.

WHAT IT DOES NOT COVER: PlayerLatency's own body, which is FindPlayerById plus
CommMean32 over the ring, and everything about how a real record comes to hold
the values fed in here.  Those stay verified by reading.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vectors import Emu, SCRATCH

MP_NAME_INK = 0x00432C50
MP_NAME_PAPER = 0x00432CE0
PLAYER_LATENCY = 0x00402EC0
FIND_PLAYER_BY_ID = 0x00402990
IAT_GET_TICK_COUNT = 0x0046F084

COMM_OBJECT = 0x004751B0
OUR_PLAYER_ID = 0x3CC
IS_HOST = 0x3D8
PLAYER_STRIDE = 0x70
PLAYER_ID = 0x214
MAP_OK = 0x278
READY_TO_LOAD = 0x270
LAST_SEEN = 0x70

VIEW_RECT_COLOUR = 0x004FE089
COLOUR_LAG_MID = 0x004FE092
HUD_MESSAGE_COLOUR = 0x00507234
COLOUR_STALE = 0x004FE090
COLOUR_NO_MAP = 0x00502CE5
COLOUR_BELOW_BG = 0x00502AD8
BACKGROUND_COLOUR = 0x00502AD9

LATENCY_MID = 0x2EE
LATENCY_BAD = 0x3E8
SILENCE_BAD = 0x4E2

DATA, DATA_SZ = 0x61000000, 0x10000
COMM = DATA + 0x1000
PLAYER_REC = DATA + 0x4000
STUBS = DATA + 0x8000
# The stubs READ their answers from here rather than carrying them as
# immediates.  Rewriting an immediate inside a stub does not work: Unicorn
# caches translated blocks, so once 0x00402EC0 has executed the first value
# is what every later call returns.  An isolated call passes and a loop does
# not, which is a confusing way to find out.  Vary the DATA, not the CODE.
LATENCY_CELL = DATA + 0x8100
FOUND_CELL = DATA + 0x8104

INKS = {"view": 0x11, "lag": 0x22, "msg": 0x33, "stale": 0x44}
PAPERS = {"nomap": 0x55, "below": 0x66, "bg": 0x77}

NOW = 100000


class Harness:
    def __init__(self):
        self.emu = Emu()
        uc = self.emu.uc
        uc.mem_map(DATA, DATA_SZ)

        # GetTickCount is an import: point the IAT slot at a mapped stub.
        uc.mem_write(STUBS, b"\xb8" + struct.pack("<I", NOW) + b"\xc3")
        uc.mem_write(IAT_GET_TICK_COUNT, struct.pack("<I", STUBS))

        # `mov eax, [cell]; ret` -- written ONCE, before either address has
        # been executed, so the code never changes and only the cells do.
        uc.mem_write(PLAYER_LATENCY,
                     b"\xa1" + struct.pack("<I", LATENCY_CELL) + b"\xc3")
        uc.mem_write(FIND_PLAYER_BY_ID,
                     b"\xa1" + struct.pack("<I", FOUND_CELL) + b"\xc3")

        for addr, colour in ((VIEW_RECT_COLOUR, INKS["view"]),
                             (COLOUR_LAG_MID, INKS["lag"]),
                             (HUD_MESSAGE_COLOUR, INKS["msg"]),
                             (COLOUR_STALE, INKS["stale"]),
                             (COLOUR_NO_MAP, PAPERS["nomap"]),
                             (COLOUR_BELOW_BG, PAPERS["below"]),
                             (BACKGROUND_COLOUR, PAPERS["bg"])):
            uc.mem_write(addr, bytes([colour]))

    def _stub_latency(self, ms):
        self.emu.uc.mem_write(LATENCY_CELL, struct.pack("<i", ms))

    def _stub_find(self, found):
        self.emu.uc.mem_write(FOUND_CELL,
                              struct.pack("<I", PLAYER_REC if found else 0))

    def ink(self, row, ours, ms, found, silent):
        uc = self.emu.uc
        uc.mem_write(COMM, b"\0" * 0x400)
        uc.mem_write(COMM_OBJECT, struct.pack("<I", COMM))
        rid = 0x1234 if not ours else 0x4321
        uc.mem_write(COMM + OUR_PLAYER_ID, struct.pack("<I", 0x4321))
        uc.mem_write(COMM + row * PLAYER_STRIDE + PLAYER_ID,
                     struct.pack("<I", rid))
        uc.mem_write(PLAYER_REC, b"\0" * 0x100)
        seen = NOW - (SILENCE_BAD + 10 if silent else 1)
        uc.mem_write(PLAYER_REC + LAST_SEEN, struct.pack("<I", seen))
        self._stub_latency(ms)
        self._stub_find(found)
        eax, _ = self.emu.call(MP_NAME_INK, [row], b"\0" * 64)
        return None if eax is None else eax & 0xFF

    def paper(self, row, host, map_ok, ready):
        uc = self.emu.uc
        uc.mem_write(COMM, b"\0" * 0x400)
        uc.mem_write(COMM_OBJECT, struct.pack("<I", COMM))
        uc.mem_write(COMM + IS_HOST, struct.pack("<i", host))
        base = COMM + row * PLAYER_STRIDE
        uc.mem_write(base + MAP_OK, struct.pack("<i", map_ok))
        uc.mem_write(base + READY_TO_LOAD, struct.pack("<i", ready))
        eax, _ = self.emu.call(MP_NAME_PAPER, [row], b"\0" * 64)
        return None if eax is None else eax & 0xFF


def ink_model(ours, ms, found, silent):
    ink = INKS["view"]
    if ours:
        return ink
    if (ms & 0xFFFFFFFF) > LATENCY_MID:
        ink = INKS["lag"]
    if (ms & 0xFFFFFFFF) > LATENCY_BAD:
        ink = INKS["msg"]
    if not found:
        return ink
    if silent:
        ink = INKS["stale"]
    return ink


def paper_model(host, map_ok, ready):
    if host and not map_ok:
        return PAPERS["nomap"]
    return PAPERS["below"] if ready else PAPERS["bg"]


MS_VALUES = (0, 1, LATENCY_MID - 1, LATENCY_MID, LATENCY_MID + 1,
             LATENCY_BAD - 1, LATENCY_BAD, LATENCY_BAD + 1, 5000)


def main():
    h = Harness()
    cases = bad = 0

    for row in (0, 1, 3):
        for host in (0, 1):
            for map_ok in (0, 1):
                for ready in (0, 1):
                    got = h.paper(row, host, map_ok, ready)
                    want = paper_model(host, map_ok, ready)
                    cases += 1
                    if got != want:
                        bad += 1
                        if bad <= 5:
                            print("  paper row %d host %d map %d ready %d: "
                                  "%s vs %s" % (row, host, map_ok, ready,
                                                got, want))

    for row in (0, 1, 3):
        for ours in (0, 1):
            for ms in MS_VALUES:
                for found in (0, 1):
                    for silent in (0, 1):
                        got = h.ink(row, ours, ms, found, silent)
                        want = ink_model(ours, ms, found, silent)
                        cases += 1
                        if got != want:
                            bad += 1
                            if bad <= 5:
                                print("  ink row %d ours %d ms %d found %d "
                                      "silent %d: %s vs %s"
                                      % (row, ours, ms, found, silent,
                                         got, want))

    print("mprowcheck: %d cases, %d differ" % (cases, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
