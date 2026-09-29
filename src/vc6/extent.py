"""extent.py RELINK_JSON [MIN]: for each unmatched function in a relink.py --json
dump, the image function's length by a linear sweep (the end is the first ret or
leaving jmp past which no earlier branch lands) against the length of ours.

The json's `orig` bytes are sliced to OUR length, so a function missing code
looks closer than it is there. A large positive gap here is code we omitted
(LoadOneItem's per-case null tests); a negative one is usually our layout.
Jump tables and indirect-jmp tails fool the sweep, so read before believing."""
import sys, json, capstone
sys.path.insert(0, 'src/vc6')
import bytematch as BM
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
img = BM.RetailImage()
def extent(va):
    b = img.read(va, 0x3000)
    far = va
    for x in md.disasm(b, va):
        m = x.mnemonic
        if (m.startswith('j') or m == 'loop') and x.op_str.startswith('0x'):
            t = int(x.op_str, 16)
            if va <= t < va + 0x3000: far = max(far, t)
        end = x.address + x.size
        if (m == 'ret' or m == 'jmp' and not x.op_str.startswith('0x')
                or m == 'jmp' and x.op_str.startswith('0x') and not (va <= int(x.op_str, 16) < end)) and end > far:
            return end - va
        if m == 'jmp' and x.op_str.startswith('0x') and end > far and int(x.op_str,16) < end:
            return end - va
    return None
D = json.load(open(sys.argv[1]))
rows = []
for d in D:
    if d['ok']: continue
    e = extent(d['va']); o = len(BM.trim(bytes.fromhex(d['ours'])))
    if e is not None: rows.append((e - o, e, o, d['fn'], d['file']))
for r in sorted(rows):
    if abs(r[0]) >= (int(sys.argv[2]) if len(sys.argv) > 2 else 24): print('%+5d img %5d ours %5d %-28s %s' % r)
