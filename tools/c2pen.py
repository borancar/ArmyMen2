#!/usr/bin/env python3
"""c2pen.py SRC FUNCTION [--edits EDITS.py]   per-decision penalty inputs (c2lines.py plus the choice routine at 0x10716715)

For every register decision prints PEN-FOR id key cost allowed hints lines, then one `nb` line per UNCOLOURED neighbour
(allowed set, [(hint-reg-id, weight)], source lines).  Penalty = own hints + hints of uncoloured neighbours with cost>0 whose
hinted reg is in the neighbour's allowed set + 100*cost on single-allowed-register neighbours.  Run: c2pen.py src/game/x.cpp Func

Original c2lines docstring follows.

c2lines.py SRC FUNCTION [--edits EDITS.py] [--all-pops]   (c2allow.py plus source lines)

Adds, per web, the SOURCE LINES of its reference tuples (walk of the list at web+0x38; a tuple's line is
u32(tuple+0x10)&0xffff, relative to the function's declaration line: file line = decl line + rel),
so a web id can be read as a variable.  Original docstring follows.

c2webs.py SRC FUNCTION [--edits EDITS.py] [--all-pops]

Print the VC6 code generator's (C2.DLL build 8168) register-allocation table for
one function: every web (live range) in the order c2 colours it, with the key it
is sorted by, its reference count and weighted cost, the penalty vector in force
when it chose, and the register it got.  The point is to see WHY a register tie
goes the way it does, instead of guessing at spellings.

    SRC       a game source file relative to the repo (src/game/item.cpp)
    FUNCTION  substring of the decorated name (BlockWeightTroops)
    --edits   EDITS.py defining E=[(old,new),...], applied to a temp copy
              of SRC exactly as relinkscreen.py does (so you can compare tables)

How it works: it compiles SRC (a sibling temp copy, with the relink defines)
under gdb with detach-on-fork off, follows the c2 child, and breakpoints the
allocator at fixed addresses of C2.DLL 8168 (loaded at 0x10700000):

    0x10720610  per-function driver (name: [[ctx+0]+0x18])
    0x107208b7  pop of the next web from the colouring list
    0x1071689a  register chosen (penalty vector at 0x1079d868, 9 dwords)
    0x1071431d  cost/key computation (re-run after every split/spill round)

Web record fields used: +0x0c key, +0x14 next, +0x1c id, +0x24 refs, +0x3c cost,
+0x40 tie-break (k40), +0x10 chosen register descriptor.  See the model in
docs/vc6-regalloc.md ("What C2.DLL does when it colours").

Needs gdb, build/vc6host, VC6_ROOT as for relinkscreen.py.  Takes about a second.
"""
import os, re, runpy, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "src", "vc6"))
import bytematch as BM  # noqa: E402

GDB_PY = r'''
import gdb, struct, os
log = open(os.environ['C2W_OUT'], 'w')
FN = os.environ.get('C2W_FN', '')
ALL = os.environ.get('C2W_ALL') == '1'
def rd(a, n): return gdb.selected_inferior().read_memory(a, n).tobytes()
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
def s32(x): return x - 2**32 if x >= 2**31 else x
def w(m): log.write(m + '\n'); log.flush()
active = [False]
class Drv(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10720610')
    def stop(s):
        try:
            ecx = int(gdb.parse_and_eval('$ecx')) & 0xffffffff
            nm = rd(u32(u32(ecx) + 0x18), 80).split(b'\0')[0].decode('latin1')
            active[0] = FN in nm
            if active[0]: w('DRIVER ' + nm)
        except Exception: active[0] = False
        return False
def bits(addr):
    out = []
    try:
        ch = u32(addr); n = 0
        while ch and n < 20:
            base = u32(ch); b = u32(ch + 8)
            for k in range(32):
                if b >> k & 1: out.append(base + k)
            ch = u32(ch + 4); n += 1
    except Exception: pass
    return out
def wlines(web):
    out = []
    lim = u32(web + 0x24)
    try:
        node = u32(web + 0x38); n = 0
        seen = set()
        while node and n < 40 and node not in seen:
            seen.add(node)
            out.append(u32(node + 0x10) & 0xffff if os.environ.get('C2W_FULL')!='1' else '%d:op%d' % (u32(node + 0x10) & 0xffff, u32(node + 4) & 0xff))
            node = u32(node); n += 1
    except Exception: pass
    return out[:lim] if os.environ.get('C2W_FULL')=='1' else out[:1] + [min([x for x in out if x] or [0])]
class Pop(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x107208b7')
    def stop(s):
        if not (active[0] and ALL): return False
        esi = int(gdb.parse_and_eval('$esi')) & 0xffffffff
        w('POPLIST')
        p = esi; n = 0
        while p and n < 60:
            f = struct.unpack('<32I', rd(p, 0x80))
            w('  id%x key=%d refs=%d cost=%d allowed=%s lines=%s' % (f[7], s32(f[3]), f[9], s32(f[15]), bits(u32(p + 0x20)), wlines(p)))
            p = f[5]; n += 1
        return False
class Cst(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x1071431d')
    def stop(s):
        if active[0]: w('COSTS (new round)')
        return False
class Pick(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x1071689a')
    def stop(s):
        if not active[0]: return False
        ebx = int(gdb.parse_and_eval('$ebx')) & 0xffffffff
        edi = int(gdb.parse_and_eval('$edi')) & 0xffffffff
        f = struct.unpack('<18I', rd(ebx, 0x48))
        pen = struct.unpack('<9i', rd(0x1079d868, 36))
        w('PICK id%x key=%d k40=%x refs=%d cost=%d pen=%s chosen=%d lines=%s' % (f[7], f[3], f[0x10], f[9], s32(f[15]), list(pen), edi, wlines(ebx)))
        return False

def hints(web):
    out=[]
    try:
        node=u32(web+0x34); n=0
        while node and n<20:
            ob=u32(node+4); out.append((u32(ob+0x1c), s32(u32(node+8)))); node=u32(node); n+=1
    except Exception as e: out.append(str(e))
    return out
def byid(i):
    try:
        p = u32(0x1079d88c + 4*(i & 0x3ff))
        return p if p and u32(p+0x1c) == i else None
    except Exception: return None
class Ent(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10716715')
    def stop(s):
        if not active[0]: return False
        ecx = int(gdb.parse_and_eval('$ecx')) & 0xffffffff
        esp = int(gdb.parse_and_eval('$esp')) & 0xffffffff
        f = struct.unpack('<18I', rd(ecx, 0x48))
        nb = bits(u32(esp+4))
        w('PEN-FOR id%x key=%d cost=%d allowed=%s hints=%s lines=%s nbrs=%d' % (f[7], s32(f[3]), s32(f[15]), bits(u32(ecx+0x20)), hints(ecx), wlines(ecx), len(nb)))
        for i in nb:
            p = byid(i)
            if p is None: w('   nb id%x (not a web)' % i); continue
            g = struct.unpack('<32I', rd(p, 0x80))
            tag = 'colored' if g[4] else 'uncolored'
            w('   nb id%x %s key=%d cost=%d allowed=%s hints=%s lines=%s' % (i, tag, s32(g[3]), s32(g[15]), bits(u32(p+0x20)), hints(p), wlines(p)))
        return False
Drv(); Pop(); Cst(); Pick(); Ent()
gdb.execute('continue')
'''
GDB_CMDS = '''set pagination off
set debuginfod enabled off
set breakpoint pending on
set detach-on-fork off
set follow-fork-mode parent
set schedule-multiple on
break c2_override_pass
run
inferior 1
continue
source %s
'''
REG = {1: 'eax', 2: 'ecx', 3: 'edx', 4: 'ebx', 6: 'ebp', 7: 'esi', 8: 'edi'}

def main():
    args = sys.argv[1:]
    if len(args) < 2: sys.exit(__doc__)
    src, fn = args[0], args[1]
    edits = None; allpops = '--all-pops' in args
    if '--edits' in args: edits = runpy.run_path(args[args.index('--edits') + 1])['E']
    path = os.path.join(REPO, src); d = os.path.dirname(path)
    text = open(path).read()
    for o, n in (edits or []):
        if o not in text: sys.exit('edit does not apply: %r' % o[:60])
        text = text.replace(o, n)
    tmp = os.path.join(d, 'c2w_tmp.cpp'); open(tmp, 'w').write(text)
    work = tempfile.mkdtemp(prefix='c2w-')
    py = os.path.join(work, 't.py'); open(py, 'w').write(GDB_PY)
    gc = os.path.join(work, 'g.gdb'); open(gc, 'w').write(GDB_CMDS % py)
    out = os.path.join(work, 'out.txt')
    env = dict(os.environ, INCLUDE=BM.SHIM + ';' + os.path.join(BM.VC6_ROOT, 'VC98', 'INCLUDE'),
               VC6_IL_LOG='1', C2W_OUT=out, C2W_FN=fn, C2W_ALL='1' if allpops else '0')
    try:
        subprocess.run(['gdb', '-q', '-batch', '-x', gc, '--args', BM.HOST, BM.CL, '/O2', '/GX', '/c',
                        '/DAM2_STANDALONE', '/DAM2_RELINK', '/Foc2w_tmp.obj', 'c2w_tmp.cpp'],
                       cwd=d, env=env, capture_output=True, timeout=900)
    finally:
        for x in ('c2w_tmp.cpp', 'c2w_tmp.obj'):
            try: os.remove(os.path.join(d, x))
            except OSError: pass
    if not os.path.exists(out): sys.exit('no output (is the function name right? is gdb installed?)')
    for l in open(out):
        m = re.match(r'PICK id(\w+) key=(-?\d+) k40=(\w+) refs=(\d+) cost=(-?\d+) pen=(\[.*?\]) chosen=(\d+) lines=(.*)', l)
        if m:
            key = int(m.group(2)) & 0xffffffff
            key = key - 2**32 if key >= 2**31 else key
            print('web %-3s key %-6d refs %-3s cost %-4s -> %-4s pen %s lines %s' % (
                m.group(1), key, m.group(4), m.group(5), REG.get(int(m.group(7)), m.group(7)), m.group(6), m.group(8)))
        else:
            print(l.rstrip())

if __name__ == '__main__':
    main()
