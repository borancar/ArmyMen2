#!/usr/bin/env python3
"""vc6asm.py FILE.cpp [NAME] -- compile a C++ snippet with the hosted VC6
(/O2 /GX, build/vc6host) and print each function's code, nops dropped.

The harness behind docs/vc6-regalloc.md: put several variants of one
function in a file and compare what VC6 does with each. Scratch files go in a
temporary directory, never under src/."""
import sys, os, struct, subprocess, tempfile, shutil
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'src', 'vc6')); import bytematch as BM
import capstone
def compile_src(src, flags=('/O2','/GX')):
    work=tempfile.mkdtemp(prefix='ra-')
    open(work+'/t.cpp','w').write(src)
    env=dict(os.environ); env['INCLUDE']=BM.SHIM+';'+os.path.join(BM.VC6_ROOT,'VC98','INCLUDE')
    r=subprocess.run([BM.HOST,BM.CL]+list(flags)+['/c','/Fot.obj','t.cpp'],cwd=work,env=env,capture_output=True,text=True)
    if not os.path.exists(work+'/t.obj'):
        shutil.rmtree(work); raise SystemExit('compile failed:\n'+r.stdout[-2000:])
    b=open(work+'/t.obj','rb').read(); shutil.rmtree(work)
    nsec=struct.unpack_from('<H',b,2)[0]; symptr,nsym=struct.unpack_from('<II',b,8)
    strtab=symptr+nsym*18
    secs=[]
    for k in range(nsec):
        h=20+k*40; name=b[h:h+8].rstrip(b'\0'); size,ptr=struct.unpack_from('<II',b,h+16)
        secs.append((name,b[ptr:ptr+size]))
    funcs=[]; i=0
    while i<nsym:
        e=symptr+i*18
        if b[e:e+4]==b'\0\0\0\0':
            off=struct.unpack_from('<I',b,e+4)[0]; nm=b[strtab+off:b.index(b'\0',strtab+off)].decode()
        else: nm=b[e:e+8].rstrip(b'\0').decode()
        val,sec,typ,scl,aux=struct.unpack_from('<IhHBB',b,e+8)
        if typ==0x20 and sec>0 and secs[sec-1][0]==b'.text': funcs.append((nm,sec,val))
        i+=1+aux
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    out={}
    for nm,sec,val in funcs:
        code=secs[sec-1][1]
        ends=sorted(v for n,s,v in funcs if s==sec and v>val)
        end=ends[0] if ends else len(code)
        out[nm]=[ (x.mnemonic+' '+x.op_str).strip() for x in md.disasm(code[val:end],0) if x.mnemonic not in ('nop','int3')]
    return out
if __name__=='__main__':
    r=compile_src(open(sys.argv[1]).read())
    for nm,ins in r.items():
        if len(sys.argv)>2 and sys.argv[2] not in nm: continue
        print('==',nm); print('\n'.join('   '+x for x in ins))
