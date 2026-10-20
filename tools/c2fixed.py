#!/usr/bin/env python3
"""c2fixed.py FN [FN ...]   (Round AO)

For each function: the instructions that pin a REGISTER (variable shift count in cl, cdq/idiv/mul/imul
one-operand/div, rep movs/stos/scas, 8-bit register ops (setcc, movzx from a byte register, mov
r8,...), in/out, xlat, cmpxchg ...) in the original image and in our relink bytes
(build/relink-base.json), side by side, with their index in the function.  The model (docs/vc6-regalloc.md,
'Allowed sets'): a web live across such an instruction loses the pinned register(s), so a different
count/position of these between image and ours explains an exclusion difference such as
'x must skip ecx'.  No compile; reads the saved relink and the image."""
import sys, os, json, re
HERE = os.path.dirname(os.path.abspath(__file__)); REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, 'src', 'vc6')); sys.path.insert(0, HERE)
import capstone, vc6lab
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
BYTE = re.compile(r'\b(al|bl|cl|dl|ah|bh|ch|dh)\b')
def cls(i):
    m, o = i.mnemonic, i.op_str
    if m in ('shl', 'shr', 'sar', 'rol', 'ror', 'sal') and o.endswith(', cl'): return 'SHIFT-CL'
    if m in ('cdq', 'cwde', 'cwd', 'idiv', 'div'): return m.upper()
    if m in ('mul', 'imul') and o and ',' not in o: return m.upper() + '1'
    if m.startswith('rep') or m.startswith('movs') or m.startswith('stos') or m.startswith('scas') or m.startswith('cmps') or m.startswith('lods'): return 'STR:' + m + ' ' + o
    if m.startswith('set'): return 'SETCC'
    if m in ('movzx', 'movsx') and BYTE.search(o.split(',')[1] if ',' in o else ''): return m.upper() + '-REG8'
    if BYTE.search(o) and m not in ('call', 'jmp') and not m.startswith('j'): return 'BYTEREG ' + m
    if m in ('in', 'out', 'xlat', 'cmpxchg', 'xchg', 'lahf', 'sahf', 'fnstsw', 'cpuid', 'rdtsc'): return m.upper()
    return None
def listing(code, base=0):
    out = []; n = 0
    for i in md.disasm(code, base):
        c = cls(i)
        if c: out.append((n, '%x' % i.address, c, i.mnemonic + ' ' + i.op_str))
        n += 1
    return out, n
def main():
    base = json.load(open(os.path.join(REPO, 'build', 'relink-base.json')))
    for fn in sys.argv[1:]:
        e = next((x for x in base if x['fn'] == fn or fn in x['fn']), None)
        if not e: print(fn, 'not in relink-base'); continue
        ours = bytes.fromhex(e['ours'])
        key = e['fn']
        va, img = vc6lab.image_function(key if key.startswith('??') else key); img = img.rstrip(b'\xcc')
        lo, no = listing(ours); li, ni = listing(img, va)
        print('==', e['fn'], 'ours %d instrs, image %d instrs' % (no, ni))
        from collections import Counter
        co = Counter(x[2].split()[0] for x in lo); ci = Counter(x[2].split()[0] for x in li)
        print('  counts ours', dict(co), ' image', dict(ci))
        print('  OURS :', ' | '.join('%d:%s' % (n, d) for n, a, c, d in lo))
        print('  IMAGE:', ' | '.join('%d:%s' % (n, d) for n, a, c, d in li))
main()
