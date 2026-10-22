#!/usr/bin/env python3
"""c2walk.py SRC FN [--edits EDITS.py] [--lines LO HI] [--raw]

Log the tuple walk that hands out C2.DLL (build 8168)'s scratch registers:
every tuple the temp-assignment pass visits and every operand entry it tests,
with the three conditions that decide whether the cursor moves.

tools/c2rot.py prints only the EVENTS (the AssignTemp calls at 0x10734e4b).
This prints the walk around them, which is what you need when the image has a
rotation event with no instruction of its own and you must find which existing
tuple it hides in. Found by following the only caller of AssignTemp,
0x10733ddf, which for a tuple T walks the operand entries at [T+0x1c] and
calls AssignTemp(T, E) only when

    E.kind ([E+8]) == 1          a value operand
    A = [E+0x14]; A.op ([A+4]) == 3  an expression temporary (not a web)
    [A+0x14] != 0                 ...that needs a register (0 = folded operand)
    B = [E+0x18]; B.op != 1       and has none yet (B.op == 1: already in a
                                  register, [[B+8]+0x1c] says which; 66 = flags)

so a load shared by two uses, which c2 turns into a web (A.op 4 at its
definition, 5 at its use), can never move the cursor, and a one-use load always
does. Each line of the summary is one tuple: `op<n> L<line>[<entries>]`, an
entry written A<kind>, `*` when [A+0x14] is set, `r<n>` for a register already
held, `w` for a web id, and ASSIGN<cursor> where the cursor moved. Tuple ops seen
so far: 1 mov, 13 push, 14 call, 15 jcc, 16 jmp, 18 lea, 45 add, 47 cmp/test,
158 ret, 193 imul. --raw prints the gdb log instead. Needs gdb, build/vc6host
and VC6_ROOT, as c2rot.py does; about a minute for a large translation unit.
"""
import os, re, runpy, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("C2ROT_LAB", os.path.dirname(HERE))
import c2rot  # noqa: E402

c2rot.GDB_PY = r'''
import gdb, struct, os
log=open(os.environ['C2W_OUT'],'w'); FN=os.environ['C2W_FN']
def rd(a,n): return gdb.selected_inferior().read_memory(a,n).tobytes()
def u32(a): return struct.unpack('<I',rd(a,4))[0]
def reg(n): return int(gdb.parse_and_eval('$'+n))&0xffffffff
def w(m): log.write(m+'\n'); log.flush()
active=[False]; base=[0]
class Drv(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10720610')
    def stop(s):
        try:
            ecx=reg('ecx'); nm=rd(u32(u32(ecx)+0x18),80).split(b'\0')[0].decode('latin1')
            active[0]=(nm==FN[1:]) if FN.startswith('=') else (FN in nm)
        except Exception: active[0]=False
        return False
class Blk(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x107335c3',internal=True)
    def stop(s):
        if active[0]: base[0]=u32(reg('ecx')+0x24); w('BLOCK base=%d'%base[0])
        return False
class Tup(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10733ddf',internal=True)
    def stop(s):
        if active[0]:
            t=reg('ecx'); w('T op=%d line=%d t=%x'%(u32(t+4)&0xff, base[0]+(u32(t+0x10)&0xffff), t))
        return False
class Ent(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10733ded',internal=True)
    def stop(s):
        if active[0]:
            try:
                e=reg('esi'); kind=u32(e+8)&0xff; A=u32(e+0x14); B=u32(e+0x18)
                s_='  E kind=%d'%kind
                if kind==1 and A:
                    s_+=' Aop=%d A14=%x'%(u32(A+4)&0xff,u32(A+0x14))
                    if (u32(A+4)&0xff)==3 and u32(A+0x14) and B:
                        bop=u32(B+4)&0xff; s_+=' Bop=%d'%bop
                        if bop==1: s_+=' reg=%d'%u32(u32(B+8)+0x1c)
                        else: s_+=' web=%d hint=%x'%(u32(B+0x1c),u32(B+0x2c))
                w(s_)
            except Exception as ex: w('  E err %s'%ex)
        return False
class Asg(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10733e36',internal=True)
    def stop(s):
        if active[0]: w('  ASSIGN cursor=%d'%((u32(0x1079d710)-0x107adff4)//4))
        return False
class Occ(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10733e15',internal=True)
    def stop(s):
        if active[0]: w('  OCC reg=%d'%reg('eax'))
        return False
Drv();Blk();Tup();Ent();Asg();Occ()
gdb.execute('continue')
'''


def summarize(log):
    out, cur = [], None
    for l in log.split("\n"):
        if l.startswith("T "):
            m = re.match(r"T op=(\d+) line=(\d+)", l)
            cur = [int(m.group(1)), int(m.group(2)), []]
            out.append(cur)
        elif cur is not None and l.startswith("  E kind=1"):
            m = re.search(r"Aop=(\d+) A14=([0-9a-f]+)(?: Bop=(\d+)(?: reg=(\d+)| web=(\d+))?)?", l)
            if m:
                cur[2].append("A%s%s%s" % (m.group(1), "" if m.group(2) == "0" else "*",
                                           (" r" + m.group(4)) if m.group(4) else (" w" if m.group(5) else "")))
        elif cur is not None and l.startswith("  ASSIGN"):
            cur[2].append("ASSIGN" + l.split("cursor=")[1])
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 2 or "--help" in a or "-h" in a:
        sys.exit(__doc__)
    raw = "--raw" in a
    a = [x for x in a if x != "--raw"]
    edits = None
    if "--edits" in a:
        i = a.index("--edits"); edits = a[i + 1]; del a[i:i + 2]
    lo, hi = 0, 10 ** 9
    if "--lines" in a:
        i = a.index("--lines"); lo, hi = int(a[i + 1]), int(a[i + 2]); del a[i:i + 3]
    src, fn = a[0], a[1]
    path = os.path.join(c2rot.LAB, src)
    orig = open(path).read()
    if edits:
        text = orig
        for old, new in runpy.run_path(edits)["E"]:
            if old not in text:
                sys.exit("edit does not apply: %r" % old[:60])
            text = text.replace(old, new)
        open(path, "w").write(text)
    try:
        _, log = c2rot.compile_obj(src, fn, -1, 0, None)
    finally:
        if edits:
            open(path, "w").write(orig)
    if raw:
        sys.stdout.write(log)
        return
    k = 0
    for op, ln, es in summarize(log):
        if lo <= ln <= hi:
            tag = ""
            for e in es:
                if e.startswith("ASSIGN"):
                    tag = "  <- event %d" % k; k += 1
            print("op%-3d L%-5d [%s]%s" % (op, ln, ", ".join(es), tag))
        else:
            k += sum(1 for e in es if e.startswith("ASSIGN"))


if __name__ == "__main__":
    main()
