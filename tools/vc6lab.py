#!/usr/bin/env python3
"""vc6lab.py CLONE.cpp FUNCTION [-v] [--flags "/O2 /GX"] [--keep-il]

Compile a standalone clone of one function with the hosted VC6 (build/vc6host)
and diff its code against the shipped image's, with call and jump targets,
absolute globals and pushed string addresses masked. It takes about a second,
because no game translation unit is involved.

What it is for: register and scheduling ties in the relink that no edit to the
real source seems to move. The method that found EvtRuleA (2026-10-06):

  1. Write the function as a small .cpp: extern "C" declarations for its
     callees and globals, raw offsets or a minimal struct for its fields.
  2. Check that the clone reproduces OUR relink output (the same diff the
     relink shows). If it does, the tie is local to the function.
  3. Ablate: delete statements one at a time and watch which register
     choice flips. That isolates the statement that decides it.
  4. Try equivalent shapes of that one statement until the diff closes, then
     carry the shape back into the real source and re-check with the relink.

EvtRuleA's tie was `BlockWeightAt(0, field, field)`, which always compiles to
ecx/edx. The image's eax-plus-copy came from `BlockWeightAt(0, field,
localCopy)`, and nothing else in the function mattered.

The scratch files go in a temporary directory, never under src/:
checkimagedata and placement scan the tree, and a stray .cpp there once broke
`make check`. --keep-il keeps c1xx's intermediate files (see VC6_IL_OVERRIDE in
src/vc6/vc6host.cpp) and prints the directory.
"""
import argparse, bisect, difflib, os, re, shutil, struct, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "src", "vc6"))
import bytematch as BM  # noqa: E402
import capstone  # noqa: E402

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def norm(insns):
    """Instruction text with the parts a clone cannot reproduce masked."""
    out = []
    for x in insns:
        if x.mnemonic in ("nop", "int3"):
            break
        t = x.mnemonic + " " + x.op_str
        if x.mnemonic.startswith("j") or x.mnemonic == "call":
            t = x.mnemonic + " T"
        t = re.sub(r"dword ptr \[0x[0-9a-f]+\]|dword ptr \[0\]", "dword ptr [G]", t)
        t = re.sub(r"push 0x4[0-9a-f]{5}$", "push S", t)
        out.append(t)
    return out


def image_function(name):
    """The image's bytes for a reconstructed function, bounded by the next
    manifest entry, as tools/xsc-style scorers do."""
    man = BM.manifest()
    vas = sorted({va for _, _, va in man})
    want = [va for _, fn, va in man if fn == name]
    if not want:
        sys.exit("vc6lab: %s is not in the relink manifest" % name)
    va = want[0]
    nxt = vas[bisect.bisect_right(vas, va)]
    img = BM.RetailImage()
    return va, bytes(img.read(va, nxt - va))


def compile_clone(src_path, flags, keep_il):
    work = tempfile.mkdtemp(prefix="vc6lab-")
    shutil.copy(src_path, os.path.join(work, "clone.cpp"))
    env = dict(os.environ)
    env["INCLUDE"] = BM.SHIM + ";" + os.path.join(BM.VC6_ROOT, "VC98", "INCLUDE")
    if keep_il:
        env.update({"VC6_KEEPTMP": "1", "TMP": work, "TEMP": work})
    r = subprocess.run([BM.HOST, BM.CL] + flags + ["/c", "/Foclone.obj", "clone.cpp"],
                       cwd=work, env=env, capture_output=True, text=True)
    obj = os.path.join(work, "clone.obj")
    if not os.path.exists(obj):
        sys.exit("vc6lab: compile failed\n" + r.stdout + r.stderr)
    b = open(obj, "rb").read()
    code = b""
    for k in range(struct.unpack_from("<H", b, 2)[0]):
        h = 20 + k * 40
        if b[h:h + 5] == b".text":
            size, ptr = struct.unpack_from("<II", b, h + 16)
            code += b[ptr:ptr + size]
    if not keep_il:
        shutil.rmtree(work, ignore_errors=True)
    return code, (work if keep_il else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("clone")
    ap.add_argument("function")
    ap.add_argument("-v", action="store_true", help="print the differing lines")
    ap.add_argument("--flags", default="/O2 /GX")
    ap.add_argument("--keep-il", action="store_true")
    a = ap.parse_args()

    va, orig = image_function(a.function)
    code, work = compile_clone(a.clone, a.flags.split(), a.keep_il)
    ours = norm(MD.disasm(code, 0))
    theirs = norm(MD.disasm(orig, va))
    diff = [t for t in difflib.ndiff(ours, theirs) if t[0] in "+-"]
    if a.v:
        print("\n".join(diff))
    print("%s: %d differing lines (- clone, + image)" % (a.function, len(diff)))
    if work:
        print("IL kept in", work)


if __name__ == "__main__":
    main()
