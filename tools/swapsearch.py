"""swapsearch.py FN [FN ...] : greedy search over swaps of ADJACENT single-line
statements inside each function, scored by the one-unit relink distance.

Run from the repo root; edits the unit in place for each trial and restores it.
For each function: find its source (file from the baseline json), list adjacent
pairs of simple one-line statements, try each swap (compile the unit, read the
function's distance and every other function's bytes), keep the best improving
swap that makes no other function worse, repeat up to ROUNDS times. Prints the
improving swaps as line pairs and writes a unified diff to build/swapsearch/<FN>.diff. The
source file is always restored. Candidates are only CANDIDATES: the lead checks
that the two statements are independent before applying.
"""
import os, sys, re, json, tempfile, difflib, time

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(LAB, "tools"))
import relinkscreen as rs   # REPO resolves to the lab

ROUNDS = int(os.environ.get("ROUNDS", "4"))
MAXMOVE = int(os.environ.get("MAXMOVE", "1"))   # move a statement down over up to this many simple ones
base = rs.load(rs.BASE)
OUT = os.path.join(LAB, "build", "swapsearch")
os.makedirs(OUT, exist_ok=True)

KW = re.compile(r"^\s*(return|goto|break|continue|case|default|else|if|for|while|switch|do|\}|\{|#|/\*|\*|//)")
DECL = re.compile(r"^\s*(const\s+)?(u?int\d+_t|int|uint|char|void|AM2_\w+|struct\s+\w+|static)\b[\w\s\*]*\s+\**\w+(\[.*\])?\s*(=.*)?;\s*$")


def simple(line):
    s = line.rstrip()
    if not s.endswith(";"):
        return False
    if KW.match(s):
        return False
    if s.count("(") != s.count(")") or s.count("{") != s.count("}"):
        return False
    if DECL.match(s) and "=" not in s:
        return False        # bare declarations stay put
    return True


def find_function(text, fn):
    name = fn.lstrip("?")
    name = re.sub(r"@@.*", "", name)
    if name.startswith("0"):                 # ctor: ??0Class@@...
        name = name[1:]
    lines = text.split("\n")
    pat = re.compile(r"(^|[\s\*:])%s\s*\(" % re.escape(name))
    for i, l in enumerate(lines):
        if pat.search(l) and not l.lstrip().startswith(("//", "*", "/*")) and re.match(r"^[A-Za-z_]", l):
            # definition: the opening brace appears within the next few lines
            for j in range(i, min(i + 6, len(lines))):
                if lines[j].strip() == "{" or lines[j].rstrip().endswith("{"):
                    depth, k = 0, j
                    while k < len(lines):
                        depth += lines[k].count("{") - lines[k].count("}")
                        if depth == 0 and k > j - 1 and "{" in "".join(lines[j:k + 1]):
                            return i, k
                        k += 1
    return None


def compile_unit(src_rel, text):
    path = os.path.join(rs.REPO, src_rel)
    orig = open(path).read()
    out = tempfile.mktemp(suffix=".json")
    try:
        open(path, "w").write(text)
        try:
            rs.run_relink(out, {"RELINK_ONLY": src_rel, "RELINK_LOAD": rs.SYMS})
        except SystemExit:
            return None
        return rs.load(out)
    finally:
        open(path, "w").write(orig)
        if os.path.exists(out):
            os.remove(out)


def score(d, fn):
    if fn not in d:
        return None, None
    worse = [f for f, e in d.items()
             if f != fn and f in base and e["ours"] != base[f]["ours"]
             and rs.distance_of(e) > rs.distance_of(base[f])]
    return rs.distance_of(d[fn]), worse


for fn in sys.argv[1:]:
    if fn not in base:
        print(fn, "not in baseline"); continue
    src_rel = base[fn]["file"]
    path = os.path.join(rs.REPO, src_rel)
    text0 = open(path).read()
    loc = find_function(text0, fn)
    if not loc:
        print(fn, "definition not found in", src_rel); continue
    a, b = loc
    start = rs.distance_of(base[fn])
    cur_text = text0
    cur_d = start
    t0 = time.time()
    applied = []
    for rnd in range(ROUNDS):
        lines = cur_text.split("\n")
        cands = []
        for i in range(a, b):
            for k in range(1, MAXMOVE + 1):
                seg = lines[i:i + k + 1]
                if len(seg) < k + 1 or not all(simple(x) for x in seg) or i + k > b:
                    break
                if lines[i].strip() == lines[i + k].strip():
                    continue
                cands.append((i, k))
        best = None
        for i, k in cands:
            l2 = lines[:]
            moved = l2.pop(i)
            l2.insert(i + k, moved)
            d = compile_unit(src_rel, "\n".join(l2))
            if d is None:
                continue
            sc, worse = score(d, fn)
            if sc is None or worse:
                continue
            if sc < cur_d and (best is None or sc < best[0]):
                best = (sc, (i, k), "\n".join(l2))
        print("%s round %d: %d candidates, best %s  (%.0fs)" % (
            fn, rnd, len(cands), best[0] if best else "none", time.time() - t0), flush=True)
        if not best:
            break
        cur_d, (i, k), cur_text = best
        applied.append((i, lines[i].strip(), "(moved down %d)" % k))
    if applied:
        diff = "".join(difflib.unified_diff(text0.splitlines(True), cur_text.splitlines(True),
                                            "a/" + src_rel, "b/" + src_rel))
        open(os.path.join(OUT, fn.replace("?", "Q") + ".diff"), "w").write(diff)
        print("== %s: %d -> %d via %d swaps" % (fn, start, cur_d, len(applied)))
        for i, x, y in applied:
            print("   swapped: %s   <->   %s" % (x, y))
    else:
        print("== %s: %d, no improving swap" % (fn, start))
    sys.stdout.flush()
