#!/usr/bin/env python3
"""c2rot.py -- watch and perturb C2.DLL (build 8168)'s scratch-register rotation.

The pass that gives short expression temporaries eax/ecx/edx is NOT the colouring
pass tools/c2webs.py prints (that one only sees webs: variables and common
subexpression temporaries).  It is a second pass, per function, that walks the
tuples of every block in layout order and calls one routine for every temporary
that has no register yet (C2.DLL 0x10734e4b; see "The rotation" in
docs/vc6-regalloc.md).  This tool breakpoints it under gdb, exactly as c2webs.py
does for the colouring (needs gdb, build/vc6host, VC6_ROOT, a writable src/).

    c2rot.py atr  SRC FN                  every rotation event of FN, in order: k, source line,
                                          tuple op, cursor (0=eax 1=ecx 2=edx) before it, register
                                          chosen, then OCC/CONF (registers busy / in conflict) and HINT
    c2rot.py map  SRC FN [KMAX]           which instruction (index in the function) changes when
                                          event K is forced into another register: maps events to code
    c2rot.py diag SRC FN IMGNAME [KMAX]   same, printing our instruction beside the image's;
                                          lines marked !! are where the register differs
    c2rot.py web  SRC FN ID:REG ...       force colouring web ID (hex, from c2webs.py) into REG
                                          (1 eax 2 ecx 3 edx 4 ebx 7 esi 8 edi) and show what changes
    c2rot.py oracle  SRC FN IMGNAME [K0 K1]   freeze the cursor after event K (the temporary becomes a
                                          non-consumer) or bump it one extra slot, for each K, and
                                          report the instruction diff against the image: tells you
                                          which event the image treats differently BEFORE you search
                                          for a source lever
    c2rot.py oracle2 SRC FN IMGNAME K0 K1   the same for every pair of such operations in the window

FN is a substring of the decorated name; '=_Name' matches exactly (needed when a
name is a prefix of another: ScriptParseEvent / ScriptParseEvents).  SRC is a
source file relative to the lab root.  Each compile takes about a second.
"""
import os,sys,subprocess,tempfile,struct,re,difflib
HERE=os.path.dirname(os.path.abspath(__file__))
LAB=os.environ.get('C2ROT_LAB') or (os.path.dirname(HERE) if os.path.basename(HERE)=='tools' else '/tmp/claude-1000/labs-am2/lab1')
sys.path.insert(0,LAB+'/src/vc6'); sys.path.insert(0,LAB+'/tools')
import bytematch as BM
import capstone
GDB_PY=r'''
import gdb, struct, os
log=open(os.environ['C2W_OUT'],'w')
FN=os.environ['C2W_FN']; K=int(os.environ.get('C2W_K','-1')); NEW=int(os.environ.get('C2W_REG','0'))
def rd(a,n): return gdb.selected_inferior().read_memory(a,n).tobytes()
def u32(a): return struct.unpack('<I',rd(a,4))[0]
active=[False]; cnt=[0]; pend=[False]; base=[0]; mode=[-1]
def reg(n): return int(gdb.parse_and_eval('$'+n))&0xffffffff
def w(m): log.write(m+'\n'); log.flush()
def bt(P,i):
    c=u32(P); key=i&~0x1f
    while c:
        k=u32(c)
        if k>=key: break
        c=u32(c+4)
    if not c or u32(c)!=key: return 0
    return (u32(c+8)>>(i&0x1f))&1
class Drv(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10720610')
    def stop(s):
        try:
            ecx=reg('ecx'); nm=rd(u32(u32(ecx)+0x18),80).split(b'\0')[0].decode('latin1')
            active[0]=(nm==FN[1:]) if FN.startswith('=') else (FN in nm); cnt[0]=0
        except Exception as e: active[0]=False
        return False
class Blk(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x107335c3',internal=True)
    def stop(s):
        if active[0]: base[0]=u32(reg('ecx')+0x24)
        return False
OPS=[x for x in os.environ.get('C2W_OPS','').split(',') if x]; FZ=set(int(x[1:]) for x in OPS if x[0]=='f'); BP=set(int(x[1:]) for x in OPS if x[0]=='b')
saved=[0]
class Atr(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10734e4b',internal=True)
    def stop(s):
        if active[0]:
            t=reg('edx'); o=reg('ecx'); R=u32(o+0x18)
            w('ATR %d line=%d op=%d web=%d cursor=%d base=%d'%(cnt[0],base[0]+(u32(t+0x10)&0xffff),u32(t+4)&0xff,u32(R+0x1c),(u32(0x1079d710)-0x107adff4)//4,base[0]))
            occ=[u32(0x1079d6ec+4*r) for r in range(1,9)]
            conf=[bt(u32(0x1079d6c8+4*r),u32(R+0x1c)) for r in range(1,9)]
            w('  OCC %s CONF %s HINT %x'%(''.join('1' if x else '.' for x in occ),''.join(str(x) for x in conf),u32(R+0x2c)))
            pend[0]=(cnt[0]==K)
            saved[0]=u32(0x1079d710); mode[0]=cnt[0]
            cnt[0]+=1
        return False
class Set(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x10735143',internal=True)
    def stop(s):
        if active[0]:
            r=reg('ecx')
            w('  SET %d'%r)
            if pend[0]:
                pend[0]=False
                gdb.execute('set $ecx=%d'%NEW)
            if mode[0] in FZ:
                gdb.execute('set {int}0x1079d710=%d'%saved[0]); mode[0]=-2
            elif mode[0] in BP:
                cur=u32(0x1079d710)
                nx=cur+4 if (u32(cur)!=0 and u32(cur)<4) else 0x107adff4
                if u32(nx)==0 or u32(nx)>=4: nx=0x107adff4
                gdb.execute('set {int}0x1079d710=%d'%nx); mode[0]=-2
        return False
WEB=int(os.environ.get('C2W_WEB','-1')); WREG=int(os.environ.get('C2W_WREG','0'))
class Pick(gdb.Breakpoint):
    def __init__(s): super().__init__('*0x1071689a',internal=True)
    def stop(s):
        if not active[0]: return False
        ebx=reg('ebx'); edi=reg('edi')
        f=struct.unpack('<18I',rd(ebx,0x48))
        pen=struct.unpack('<9i',rd(0x1079d868,36))
        w('PICK id%x key=%d k40=%x refs=%d cost=%d pen=%s chosen=%d'%(f[7],struct.unpack('<i',struct.pack('<I',f[3]))[0],f[0x10],f[9],struct.unpack('<i',struct.pack('<I',f[15]))[0],list(pen),edi))
        if f[7]==WEB:
            gdb.execute('set $edi=%d'%WREG)
        return False
Drv();Blk();Atr();Set();Pick()
gdb.execute('continue')
'''
GDB_CMDS='''set pagination off
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
def compile_obj(src,fn,K,NEW,out,web=-1,wreg=0,ops=''):
    path=os.path.join(LAB,src); d=os.path.dirname(path)
    tag='c2w_%d'%os.getpid(); tmp=os.path.join(d,tag+'.cpp'); open(tmp,'w').write(open(path).read())
    work=tempfile.mkdtemp(prefix='c2p-')
    py=work+'/t.py'; open(py,'w').write(GDB_PY)
    gc=work+'/g.gdb'; open(gc,'w').write(GDB_CMDS%py)
    env=dict(os.environ,INCLUDE=BM.SHIM+';'+os.path.join(BM.VC6_ROOT,'VC98','INCLUDE'),VC6_IL_LOG='1',C2W_OUT=work+'/log.txt',C2W_FN=fn,C2W_K=str(K),C2W_REG=str(NEW),C2W_WEB=str(web),C2W_WREG=str(wreg),C2W_OPS=ops)
    try:
        subprocess.run(['gdb','-q','-batch','-x',gc,'--args',BM.HOST,BM.CL,'/O2','/GX','/c','/DAM2_STANDALONE','/DAM2_RELINK','/Fo'+tag+'.obj',tag+'.cpp'],cwd=d,env=env,capture_output=True,timeout=1800)
        obj=open(os.path.join(d,tag+'.obj'),'rb').read()
    finally:
        for x in (tag+'.cpp',tag+'.obj'):
            try: os.remove(os.path.join(d,x))
            except OSError: pass
    return obj,open(work+'/log.txt').read()
def func_code(obj,fn):
    nsec=struct.unpack_from('<H',obj,2)[0]; symptr,nsym=struct.unpack_from('<II',obj,8)
    strtab=symptr+nsym*18
    secs=[]
    for k in range(nsec):
        h=20+k*40; name=obj[h:h+8].rstrip(b'\0'); size,ptr=struct.unpack_from('<II',obj,h+16); secs.append((name,size,ptr))
    funcs=[];i=0
    while i<nsym:
        e=symptr+i*18
        if obj[e:e+4]==b'\0\0\0\0': off=struct.unpack_from('<I',obj,e+4)[0]; nm=obj[strtab+off:obj.index(b'\0',strtab+off)].decode()
        else: nm=obj[e:e+8].rstrip(b'\0').decode()
        val,sec,typ,cls,na=struct.unpack_from('<IhHBB',obj,e+8)
        if typ==0x20 and sec>0: funcs.append((val,nm,sec))
        i+=1+na
    funcs.sort()
    for j,(val,nm,sec) in enumerate(funcs):
        if (nm==fn[1:]) if fn.startswith('=') else (fn in nm):
            nxt=[v for v,n,s in funcs[j+1:] if s==sec]
            name,size,ptr=secs[sec-1]
            end=nxt[0] if nxt else size
            return obj[ptr+val:ptr+end]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def ins(code):
    out=[]
    for i in md.disasm(code,0):
        t=i.mnemonic+' '+i.op_str
        if i.mnemonic in ('call','jmp') or i.mnemonic.startswith('j'): t=i.mnemonic+' X'
        out.append((i.address,t))
    return out
def webs(src,fn,ids=None,reg=1):
    obj,log=compile_obj(src,fn,-1,0,None)
    base=ins(func_code(obj,fn))
    rows=[l for l in log.split('\n') if l.startswith('PICK')]
    return rows,base
def image_ins(fn):
    import vc6lab
    key=fn[1:] if fn.startswith('=') else fn
    key=key.lstrip('_?').split('@')[0]
    va,b=vc6lab.image_function(key); b=b.rstrip(b'\xcc')
    while b.endswith(b'\x90'): b=b[:-1]
    out=[]
    for i in md.disasm(b,va):
        t=i.mnemonic+' '+i.op_str
        if i.mnemonic in ('call','jmp') or i.mnemonic.startswith('j'): t=i.mnemonic+' X'
        out.append((i.address,t))
    return out
def idiff(a,b):
    def norm(s):
        s=re.sub(r'\[(0x[0-9a-f]+|0)\]','[G]',s)
        s=re.sub(r'\[(e[a-z]{2}\*\d \+ )?(0x[0-9a-f]{5,})\]','[G]',s)
        s=re.sub(r'(?<![\w+*-])(0x[0-9a-f]{5,})\b','G',s)
        s=re.sub(r', (0x[0-9a-f]{5,})$',', G',s)
        s=re.sub(r'\b(call|jmp|j\w+) .*','\\1 X',s)
        return s
    sm=difflib.SequenceMatcher(None,[norm(x[1]) for x in a],[norm(x[1]) for x in b],autojunk=False)
    return sum(max(i2-i1,j2-j1) for t,i1,i2,j1,j2 in sm.get_opcodes() if t!='equal')
if __name__=='__main__' and sys.argv[1]=='oracle':
    # perturb.py oracle SRC FN IMGNAME [K0 K1]: for every ATR K in the window, freeze the cursor after it (non-consumer) and bump it (extra step); report instruction-diff count against the image
    src,fn,key=sys.argv[2],sys.argv[3],sys.argv[4]
    img=image_ins(key)
    obj,log=compile_obj(src,fn,-1,0,None)
    base=ins(func_code(obj,fn)); b0=idiff(base,img)
    n=len([l for l in log.split('\n') if l.startswith('ATR')])
    k0=int(sys.argv[5]) if len(sys.argv)>5 else 0; k1=int(sys.argv[6]) if len(sys.argv)>6 else n
    print('baseline instruction diff',b0,'ATRs',n)
    for K in range(k0,min(k1,n)):
        for nm,kw in (('freeze',dict(ops='f%d'%K)),('bump',dict(ops='b%d'%K))):
            o2,_=compile_obj(src,fn,-1,0,None,**kw)
            try: pc=func_code(o2,fn)
            except Exception: continue
            if pc is None: continue
            d=idiff(ins(pc),img)
            if d<b0: print('K=%-3d %-6s -> %d  (was %d)'%(K,nm,d,b0))
    sys.exit(0)
if __name__=='__main__' and sys.argv[1]=='oracle2':
    # perturb.py oracle2 SRC FN IMGNAME K0 K1 : all pairs of {freeze,bump} ops inside the window
    src,fn,key=sys.argv[2],sys.argv[3],sys.argv[4]
    img=image_ins(key)
    obj,log=compile_obj(src,fn,-1,0,None)
    base=ins(func_code(obj,fn)); b0=idiff(base,img)
    n=len([l for l in log.split('\n') if l.startswith('ATR')])
    k0=int(sys.argv[5]); k1=int(sys.argv[6])
    print('baseline',b0)
    best=b0
    import itertools
    cands=[(t,K) for K in range(k0,min(k1,n)) for t in 'fb']
    for (t1,k1_),(t2,k2_) in itertools.combinations(cands,2):
        if k1_==k2_: continue
        ops='%s%d,%s%d'%(t1,k1_,t2,k2_)
        o2,_=compile_obj(src,fn,-1,0,None,ops=ops)
        try: pc=func_code(o2,fn)
        except Exception: continue
        if pc is None: continue
        d=idiff(ins(pc),img)
        if d<=best: print(ops,d); best=min(best,d)
    sys.exit(0)
if __name__=='__main__' and sys.argv[1]=='diag':
    # perturb.py diag SRC FN [name]: rotation events with the image's register at the same instruction
    src,fn=sys.argv[2],sys.argv[3]
    key=sys.argv[4] if len(sys.argv)>4 else fn
    obj,log=compile_obj(src,fn,-1,0,None)
    base=ins(func_code(obj,fn))
    img=image_ins(key)
    norm=lambda s: re.sub(r'\b(e?[abcd]x|[abcd][lh]|e?[sd]i|e?bp|[sd]il)\b','R',s)
    sm=difflib.SequenceMatcher(None,[norm(x[1]) for x in base],[norm(x[1]) for x in img],autojunk=False)
    amap={}
    for t,i1,i2,j1,j2 in sm.get_opcodes():
        if t=='equal':
            for k in range(i2-i1): amap[i1+k]=j1+k
    lines=log.split('\n'); idxs=[i for i,l in enumerate(lines) if l.startswith('ATR')]
    kmax=int(sys.argv[5]) if len(sys.argv)>5 else len(idxs)
    for K in range(min(kmax,len(idxs))):
        cur=int(lines[idxs[K]+2].split()[1])
        res=None
        for new in [r for r in (1,2,3) if r!=cur]:
            o2,_=compile_obj(src,fn,K,new,None)
            try: pc=func_code(o2,fn)
            except Exception: continue
            if pc is None: continue
            alt=ins(pc)
            sm2=difflib.SequenceMatcher(None,[x[1] for x in base],[x[1] for x in alt],autojunk=False)
            ch=[i1 for t,i1,i2,j1,j2 in sm2.get_opcodes() if t!='equal' and i1<len(base) and 'X' not in base[i1][1]]
            if ch: res=ch[0]; break
        info=lines[idxs[K]][4:50]
        if res is None: print('K=%-3d %s  (no visible effect)'%(K,info)); continue
        ours=base[res][1]; im=img[amap[res]][1] if res in amap else '?'
        flag='  ' if ours==im else '!!'
        print('%s K=%-3d %-44s idx=%-4d ours: %-34s image: %s'%(flag,K,info,res,ours,im))
    sys.exit(0)

def atr_main():
    src,fn=sys.argv[2],sys.argv[3]
    obj,log=compile_obj(src,fn,-1,0,None)
    text=open(os.path.join(LAB,src)).read().split('\n')
    decl=None
    for i,l in enumerate(text):
        if re.match(r'^[A-Za-z_].*\b'+re.escape(fn.lstrip('=?_').split('@')[0])+r'\s*\(',l) and not l.rstrip().endswith(';'):
            decl=i+1
    lines=log.split('\n')
    atr=[(l,lines[i+1],lines[i+2]) for i,l in enumerate(lines) if l.startswith('ATR')]
    mb=re.search(r'base=(\d+)',log)
    off=(decl-int(mb.group(1))) if (mb and decl) else None
    for l,o,s in atr:
        m=re.match(r'ATR (\d+) line=(\d+) op=(\d+) web=(\d+) cursor=(\d+)',l)
        k,ln,op,web,cur=map(int,m.groups()); reg=int(s.split()[1])
        t=''
        if off is not None and 0<=ln+off-1<len(text): t=text[ln+off-1].strip()[:60]
        print('%3d L%-5d op%-3d web%-5d cur%d -> %s  %s  | %s'%(k,ln,op,web,cur,{1:'eax',2:'ecx',3:'edx',7:'esi',8:'edi',4:'ebx',6:'ebp'}.get(reg,reg),t,o.strip()))
if __name__=='__main__' and sys.argv[1]=='atr':
    atr_main(); sys.exit(0)
if __name__=='__main__' and sys.argv[1]=='map':
    # perturb.py map SRC FN [Kmax]: for each ATR K, which instruction (index in the function) changes when its register is changed
    src,fn=sys.argv[2],sys.argv[3]
    obj,log=compile_obj(src,fn,-1,0,None)
    base=ins(func_code(obj,fn))
    lines=log.split('\n'); idxs=[i for i,l in enumerate(lines) if l.startswith('ATR')]
    kmax=int(sys.argv[4]) if len(sys.argv)>4 else len(idxs)
    for K in range(min(kmax,len(idxs))):
        cur=int(lines[idxs[K]+2].split()[1])
        res=None
        for new in [r for r in (1,2,3) if r!=cur]:
            o2,_=compile_obj(src,fn,K,new,None)
            try: pc=func_code(o2,fn)
            except Exception: continue
            if pc is None: continue
            alt=ins(pc)
            sm=difflib.SequenceMatcher(None,[x[1] for x in base],[x[1] for x in alt],autojunk=False)
            ch=[(i1,base[i1][1],alt[j1][1] if j1<len(alt) else '') for t,i1,i2,j1,j2 in sm.get_opcodes() if t!='equal' and i1<len(base)]
            ch=[c for c in ch if not c[1].startswith('j') and 'X' not in c[1]]
            if ch: res=(new,ch[0]); break
        print('K=%d  %s  chose=%d  ->  %s'%(K,lines[idxs[K]][4:60],cur,res))
    sys.exit(0)
if __name__=='__main__' and sys.argv[1]=='web':
    src,fn=sys.argv[2],sys.argv[3]
    rows,base=webs(src,fn)
    import re
    for r in rows: print(r)
    for a in sys.argv[4:]:
        wid,new=a.split(':'); wid=int(wid,16); new=int(new)
        o2,_=compile_obj(src,fn,-1,0,None,wid,new)
        try: alt=ins(func_code(o2,fn))
        except Exception: print(a,'failed'); continue
        sm=difflib.SequenceMatcher(None,[x[1] for x in base],[x[1] for x in alt],autojunk=False)
        print('WEB %x -> reg %d'%(wid,new))
        for t,i1,i2,j1,j2 in sm.get_opcodes():
            if t=='equal': continue
            print('   ',[base[i][0] for i in range(i1,min(i2,i1+1))],'|'.join(base[i][1] for i in range(i1,i2)),'=>','|'.join(alt[j][1] for j in range(j1,j2)))
    sys.exit(0)
if __name__=='__main__':
    src,fn=sys.argv[1],sys.argv[2]; Ks=[int(x) for x in sys.argv[3:]]
    obj,log=compile_obj(src,fn,-1,0,None)
    print(log)
    base=ins(func_code(obj,fn))
    for K in Ks:
        # pick the replacement: any of 1,2,3 different from baseline choice
        m=[l for l in log.split('\n') if l.startswith('  SET')]
        atr=[l for l in log.split('\n') if l.startswith('ATR')]
        # baseline reg of K-th ATR: the SET following it
        lines=log.split('\n'); idx=[i for i,l in enumerate(lines) if l.startswith('ATR')][K]
        cur=int(lines[idx+2].split()[1])
        for new in [r for r in (1,2,3) if r!=cur][:1]:
            o2,_=compile_obj(src,fn,K,new,None)
            try: pc=func_code(o2,fn)
            except Exception: print('K=%d failed'%K); continue

            if pc is None: print(K,'no code'); continue
            alt=ins(pc)
            sm=difflib.SequenceMatcher(None,[x[1] for x in base],[x[1] for x in alt],autojunk=False)
            print('K=%d (%s) cur=%d new=%d'%(K,atr[K],cur,new))
            for t,i1,i2,j1,j2 in sm.get_opcodes():
                if t=='equal': continue
                print('   ',[base[i][0] for i in range(i1,min(i2,i1+1))],'|'.join(base[i][1] for i in range(i1,i2)),'=>','|'.join(alt[j][1] for j in range(j1,j2)))
