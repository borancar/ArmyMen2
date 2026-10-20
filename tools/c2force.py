#!/usr/bin/env python3
"""c2force.py SRC FN IMGKEY [--fix ID:REG,ID:REG] [--min N]     (Round AO)

Greedy 'target colouring' finder.  For every colouring web of FN and every register, force that
web into that register (c2rot_m's multi-web force; the --fix list is forced in every run) and score
the relocation-resolved instruction diff against the image (tools/c2reloc.py: jump tables and
padding after the last ret are trimmed).  Prints every single forcing that lowers the diff, best
first.  Add the best to --fix and run again: a few rounds name the web->register assignments the
image's colouring has, before any source search.  Each compile is ~0.5-1.5 s; a forcing the web
cannot take may hang c2, so every compile has an 8 s timeout.  Needs a writable src/ and no other
tool compiling in the same lab (BM.manifest() globs src/game/win32/*.cpp)."""
import sys, os, re, importlib.util, subprocess as _sp
here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
fix = ''
if '--fix' in args:
    i = args.index('--fix'); fix = args[i + 1]; del args[i:i + 2]
src, fn, key = args[:3]
_sv = list(sys.argv); sys.argv = ['c2rot_m', '__none__']
spec = importlib.util.spec_from_file_location('c2rot_m', os.path.join(here, 'c2rot_m.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
sys.argv = _sv
sys.path.insert(0, here)
import c2reloc, capstone, vc6lab
m.GDB_PY = m.GDB_PY.replace("Drv();Blk();Atr();Set();Pick()", "Drv();Pick()")
_run = _sp.run
def _run_to(*a, **k):
    k['timeout'] = 8
    try: return _run(*a, **k)
    except _sp.TimeoutExpired: raise RuntimeError('timeout')
m.subprocess.run = _run_to
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
va, b = vc6lab.image_function(key); b = b.rstrip(b'\xcc')
img = [(0, (i.mnemonic + ' ' + i.op_str).strip()) for i in md.disasm(b, 0) if i.mnemonic not in ('nop', 'int3')]
def trim(L):
    last = max(i for i, x in enumerate(L) if x[1].split(' ')[0] == 'ret')
    return L[:last + 1]
img = trim(img)
def score(spec_):
    o, log = m.compile_obj(src, fn, -1, 0, None, spec_, 0)
    alt = trim(c2reloc.ins_of(o, fn))
    return sum(max(i2 - i1, j2 - j1) for t, i1, i2, j1, j2, a, b_ in c2reloc.diff(alt, img)), log
b0, log = score(fix)
print('base diff', b0, 'fix', fix or '-', flush=True)
ids = []
for r in log.split('\n'):
    mm = re.match(r'PICK id(\w+) key=(-?\d+) k40=\w+ refs=(\d+) cost=(-?\d+) pen=\[(.*?)\] chosen=(\d+)', r)
    if mm and int(mm.group(1), 16) not in [i for i, _, _ in ids]:
        ids.append((int(mm.group(1), 16), int(mm.group(6)), int(mm.group(2))))
res = []
for wid, cur, k_ in ids:
    for reg in (1, 2, 3, 4, 6, 7, 8):
        if reg == cur: continue
        try:
            d, _ = score((fix + ',' if fix else '') + '%x:%d' % (wid, reg))
        except Exception:
            continue
        if d < b0:
            res.append((d, '%x:%d' % (wid, reg), cur, k_)); print('web %x key %d %s->%s diff %d' % (wid, k_, cur, reg, d), flush=True)
res.sort()
print('BEST', res[:8])
