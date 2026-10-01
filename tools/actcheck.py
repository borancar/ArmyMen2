"""Check the reconstructed action parser against the original, under Unicorn.

    ./.venv/bin/python src/vc6/relink.py -nointro -dbg --json /tmp/rl.json
    ./.venv/bin/python tools/actcheck.py /tmp/rl.json [FN] [LINE... | +all LINE...]

The relink places OUR compiled ScriptParseActionRecon at the original's VA with
every call resolved to the image, so the two can run side by side over the same
input with every callee shared: one emulator holds the original, the other our
bytes (never one emulator swapping code -- Unicorn caches translated blocks and
would run the stale ones). Each shipped script line is tokenised by the
original NextToken, and every action keyword in it is parsed by both; the
return, the cursor, the 0x48-byte record, the name table and the trigger
counter are compared.

With LINE arguments only those lines are run, which is how the error shapes no
shipped script contains are reached; `+all` runs the shipped lines too.
"""
import sys, struct, json
sys.path.insert(0, 'tools')
import scriptcheck as sc
from vectors import Emu, SCRATCH, SCRATCH_PATTERN, SCRATCH_SZ

RJ = sys.argv[1]
FN = sys.argv[2] if len(sys.argv) > 2 else 'ScriptParseActionRecon'
d = json.load(open(RJ)); e = next(x for x in d if x['fn'] == FN)
VA = e['va']; OURS = bytes.fromhex(e['ours'])
CTX, AT, ACT, LINE = 0x40, 0x60, 0x100, 0x1000
TRIG = 0x511df4

class Side:
    def __init__(self, code):
        self.emu = Emu(); self.heap = sc.Heap(self.emu); self.uc = self.emu.uc
        if code is not None:
            self.uc.mem_write(VA, code)
    def names(self):
        uc = self.uc
        _c, n, arr = struct.unpack("<3I", bytes(uc.mem_read(sc.ADDR_NAME_CAP, 12)))
        out = []
        for i in range(n):
            p, ty, val, live = struct.unpack("<Iiii", bytes(uc.mem_read(arr + i*16, 16)))
            nm = bytearray()
            while uc.mem_read(p + len(nm), 1)[0] and len(nm) < 200:
                nm.append(uc.mem_read(p + len(nm), 1)[0])
            out.append((bytes(nm), ty, val, live))
        return tuple(out)
    def tokenize(self, line):
        uc = self.uc
        self.heap.reset()
        uc.mem_write(SCRATCH, SCRATCH_PATTERN[:SCRATCH_SZ])
        uc.mem_write(sc.ADDR_NAME_CAP, struct.pack("<3I", 0, 0, 0))
        uc.mem_write(TRIG, b"\0\0\0\0")
        uc.mem_write(SCRATCH + CTX, struct.pack("<3I", 0, 0, 0))
        uc.mem_write(SCRATCH + LINE, line + b"\0")
        if sc.raw_call(self.emu, sc.ADDR_NEXT_TOKEN, [SCRATCH + LINE, SCRATCH + CTX, 5]) is None:
            return None
        cnt, toks = struct.unpack("<2I", bytes(uc.mem_read(SCRATCH + CTX + 4, 8)))
        self.snap = (bytes(uc.mem_read(SCRATCH, SCRATCH_SZ)), self.heap.next,
                     dict(self.heap.sizes),
                     bytes(uc.mem_read(sc.HEAP, self.heap.next - sc.HEAP)))
        return cnt, toks
    def run(self, pos):
        uc = self.uc; s = self.snap
        uc.mem_write(SCRATCH, s[0]); self.heap.next = s[1]; self.heap.sizes = dict(s[2])
        uc.mem_write(sc.HEAP, s[3])
        uc.mem_write(sc.ADDR_NAME_CAP, struct.pack("<3I", 0, 0, 0))
        uc.mem_write(TRIG, b"\0\0\0\0")
        uc.mem_write(SCRATCH + AT, struct.pack("<i", pos))
        uc.mem_write(SCRATCH + ACT, b"\xcc" * 0x48)
        rc = sc.raw_call(self.emu, VA, [SCRATCH + CTX, SCRATCH + AT, SCRATCH + ACT])
        if rc is None:
            return 'fault'
        at = struct.unpack("<i", bytes(uc.mem_read(SCRATCH + AT, 4)))[0]
        return (rc, at, bytes(uc.mem_read(SCRATCH + ACT, 0x48)), self.names(),
                bytes(uc.mem_read(TRIG, 4)))

A = Side(None); B = Side(OURS)
extra = [l.encode() for l in sys.argv[3:]]
total = bad = 0
LINES = [(x, 0) for x in extra] if extra and extra[0] != b"+all" else sc.script_lines() + [(x, 0) for x in extra[1:]]
for line, _n in LINES:
    ta = A.tokenize(line); tb = B.tokenize(line)
    if ta is None or tb is None:
        continue
    cnt, toks = ta
    for pos in range(cnt):
        k, _ln, v = struct.unpack("<iiI", bytes(A.uc.mem_read(toks + pos*12, 12)))
        if k != 2 or not (64 <= v <= 186):
            continue
        a = A.run(pos); b = B.run(pos)
        total += 1
        if a != b:
            bad += 1
            if bad <= 25:
                print('DIFF', line[:100], 'pos', pos, flush=True)
                for tag, r in (('orig', a), ('ours', b)):
                    print('  ', tag, r if isinstance(r, str) else (r[0], r[1], r[2].hex(), r[3][:4]), flush=True)
print('cases', total, 'diffs', bad)
