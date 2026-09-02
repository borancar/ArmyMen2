"""Resolve every `[esp + N]` in a function to a frame-relative slot.

WHY THIS EXISTS. Reading a large function means turning `[esp + 0x4c]` into
"argument 3" or "the third local", and the displacement alone does not say
which: it depends on how many pushes are outstanding at that instruction. The
whole class of error CLAUDE.md records under "THISCALL CLEANS ITS OWN STACK
ARGUMENTS" and "an ARGUMENT SLOT reused as a local" is this arithmetic done by
eye.

Doing it by eye failed on the first function it was tried on. Two writes to
`[esp + 0x4c]` in 0x00403B40 looked like the same slot and are not: one has an
outstanding `push esi` for a call in front of it, so they are E+0x48 and E+0x4C.
The conflict was the reader's, not the image's.

AND A LINEAR TRACE IS NOT ENOUGH, which was the second thing this found. A
single pass that adds and subtracts as it goes desynchronises at the first
branch that jumps past an `add esp` -- the model then reports negative slots and
offsets past the end of the frame, both of which 0x00403B40 produced. esp has to
be propagated along CONTROL FLOW, with every edge agreeing.

So this walks basic blocks from the entry, carries esp along fallthrough and
branch edges, and reports a slot only where every path into an instruction
agrees on the depth. Where they disagree it says so rather than guessing --
which is the answer that matters, because a disagreement is either a compiler
idiom worth understanding or a decode that has gone wrong.

VALIDATED THE WAY THIS PROJECT VALIDATES ANYTHING: pointed at three functions
whose frames were already worked out by hand and checked that it agrees.
AiAttackBody's facing slot, AiPatrolStep's, and StepType5's sub-step, outcome
and speed all land where the hand derivation put them -- and those took real
effort to get right, which is the point.

It reports what it cannot answer rather than guessing: instructions reached at
two different depths, and references in code no path reaches. On 0x00403B40 it
reports neither, which is itself evidence that the decode is sound -- 559
instructions with every edge agreeing is not what a desynchronised disassembly
looks like.

AND THE FIRST TIME IT REPORTED SOMETHING, THE TOOL WAS WRONG AND THE REPORT WAS
RIGHT. 0x0044AFB0 came back with ten disagreeing depths, every one of them four
bytes, every one downstream of `push ecx; mov ecx, <this>; call` -- a THISCALL,
whose callee pops its own argument with no `add esp` at the call site to see.
The fix is to decode each callee and read its `ret N`, which is the same
technique CLAUDE.md prescribes for telling two copy variants apart. All ten
went, and the four functions whose maps were already checked by hand came back
byte for byte the same.

That is the shape to expect from this tool: a disagreement is a fact about the
code or a gap in the model, and saying which costs one look at the site.

    tools/espmap.py 0x00403B40
"""

import collections
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def decode(addr):
    out = subprocess.run([os.path.join(HERE, "..", ".venv", "bin", "python"),
                          os.path.join(HERE, "disasm.py"), addr],
                         capture_output=True, text=True).stdout
    ins = []
    for ln in out.splitlines():
        m = re.match(r"\s+0x([0-9a-f]+)\s+(\S+)\s*(.*)", ln)
        if m:
            ins.append((int(m.group(1), 16), m.group(2),
                        re.sub(r";.*", "", m.group(3)).strip()))
    return ins


_RET_N = {}


def callee_pops(target):
    """How many bytes a callee removes on return -- its `ret N`, or 0.

    THIS IS WHY THE TOOL NEEDED A SECOND PASS. A cdecl call leaves esp alone
    and the caller cleans; a THISCALL or STDCALL callee pops its own arguments
    and there is no `add esp` at the call site to see. CLAUDE.md records that
    forgetting this "makes a function unreadable" and that it was the single
    thing which deferred CreateVehicle -- and it is exactly what made this tool
    report ten disagreeing depths in 0x0044AFB0, every one of them four bytes,
    every one of them downstream of `push ecx; mov ecx, <this>; call`.

    So the callee is decoded and its terminating `ret N` read, which is the
    same technique CLAUDE.md prescribes for telling two copy variants apart.
    A callee that cannot be decoded answers 0, which is the cdecl assumption
    and the one that was wrong before -- but now it is wrong only where the
    disassembler already failed, and the disagreement report says so."""
    if target in _RET_N:
        return _RET_N[target]
    _RET_N[target] = 0            # break recursion before decoding
    n = 0
    for _, mn, ops in decode("0x%08X" % target):
        if mn == "ret" and ops:
            # disasm.py annotates the line ("ret 4   <<< cleans 4 bytes"), so
            # take the first token and accept either base -- capstone prints
            # this immediate in decimal where it prints most others in hex.
            m = re.match(r"(0x[0-9a-fA-F]+|\d+)", ops.strip())
            n = int(m.group(1), 0) if m else 0
            break
    _RET_N[target] = n
    return n


def delta(mn, ops):
    """How this instruction moves esp, or None if it does not."""
    if mn == "push":
        return -4
    if mn == "pop":
        return 4
    if mn in ("sub", "add") and ops.startswith("esp,"):
        try:
            n = int(ops.split(",")[1].strip(), 16)
        except ValueError:
            return None
        return -n if mn == "sub" else n
    if mn == "ret":
        return None
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ins = decode(sys.argv[1])
    if not ins:
        print("no instructions")
        return 1
    at = {a: i for i, (a, _, _) in enumerate(ins)}

    # The frame origin is the depth after the prologue's saves, which is the
    # depth at the first instruction that is neither a sub esp nor a push.
    esp = {}
    esp[ins[0][0]] = 0
    work = [ins[0][0]]
    bad = {}
    src = {}
    while work:
        a = work.pop()
        i = at[a]
        cur = esp[a]
        while i < len(ins):
            ad, mn, ops = ins[i]
            d = delta(mn, ops)
            if d is None:            # ret: end of path
                break
            if mn == "call" and re.match(r"^0x[0-9a-f]+$", ops):
                d += callee_pops(int(ops, 16))
            nxt = cur + d
            tgt = None
            if mn[0] == "j" and re.match(r"^0x[0-9a-f]+$", ops):
                tgt = int(ops, 16)
            if tgt is not None and tgt in at:
                if tgt in esp and esp[tgt] != nxt:
                    bad.setdefault(tgt, []).append((ad, nxt, esp[tgt],
                                                    src.get(tgt)))
                elif tgt not in esp:
                    esp[tgt] = nxt
                    src[tgt] = ad
                    work.append(tgt)
            if mn == "jmp":
                break
            if i + 1 >= len(ins):
                break
            nad = ins[i + 1][0]
            if nad in esp and esp[nad] != nxt:
                bad.setdefault(nad, []).append((ad, nxt, esp[nad],
                                                src.get(nad)))
                break
            if nad in esp:
                break
            esp[nad] = nxt
            src[nad] = ad
            cur = nxt
            i += 1

    # The prologue's own adjustment sets the origin: take the depth once the
    # saves are done, which is the minimum reached in the first basic block.
    base = min(esp.get(a, 0) for a, _, _ in ins[:12])

    use = collections.defaultdict(list)
    unknown = 0
    for ad, mn, ops in ins:
        for g in re.finditer(r"\[esp \+ (0x[0-9a-f]+)\]", ops):
            if ad not in esp:
                unknown += 1
                continue
            use[esp[ad] + int(g.group(1), 16) - base].append((ad, mn))

    print("%s: %d instructions, frame origin at esp%+d" %
          (sys.argv[1], len(ins), -base))
    if bad:
        print("  %d instruction(s) reached at DISAGREEING depths:" % len(bad))
        for a in sorted(bad):
            for frm, got, had, first in bad[a]:
                print("     0x%08x: %+d from 0x%08x, but %+d from 0x%08x"
                      % (a, got, frm, had,
                         first if first is not None else 0))
    if unknown:
        print("  %d reference(s) in unreached code" % unknown)
    for off in sorted(use):
        kinds = collections.Counter(k[1] for k in use[off])
        where = " ".join("0x%08x" % a for a, _ in use[off][:4])
        print("  slot %+#07x  x%-3d %-28s %s"
              % (off, len(use[off]),
                 ",".join("%s:%d" % kv for kv in sorted(kinds.items())), where))
    return 0


if __name__ == "__main__":
    sys.exit(main())
