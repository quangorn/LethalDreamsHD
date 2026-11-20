#!/usr/bin/env python3
"""trace_diff.py - A/B diff of the battle tick trace (PLAN_BATTLE_AI_CPP.md, 1.7).

Input: two hdlog.txt-style logs containing `BATTLE tick=...` lines.
Fields are compared in the fixed order below; `us` and `ai` are excluded (not
deterministic). Alignment is positional (line i <-> line i): one line is one
call of FUN_0044bdd0 (one tick of one unit).

Output: the first divergence (line number, field, native and port values) plus
+-2 lines of context from both files.

rc: 0 - no divergence and both files have >= --prefix lines;
    1 - divergence found;
    2 - a file cannot be read or has no BATTLE lines;
    3 - no divergence, but fewer lines than --prefix / --tail required.
"""
import sys

COMPARE = [
    "tick", "unit", "code", "u170", "al", "out",
    "c1", "x1", "y1", "i1", "fx", "fy", "lx", "ly",
    "c2", "x2", "y2", "i2",
    "g", "h10k", "cost", "trk", "bd2", "s1", "s2", "vis",
    "m0", "m1", "m2",
]

USAGE = ("usage: trace_diff.py <native.log> <port.log> "
         "[--prefix=N|--tail=N] [--fields=a,b,c]")


def parse(path):
    rows = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                i = line.find(" BATTLE ")
                if i < 0:
                    continue
                rec = {}
                for tok in line[i + len(" BATTLE "):].split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        rec[k] = v
                rows.append((line.rstrip("\n"), rec))
    except OSError:
        raise SystemExit(2)
    return rows


def parse_args(argv):
    files = []
    prefix = 0
    tail = 0
    fields = COMPARE
    for a in argv:
        if a.startswith("--prefix="):
            prefix = int(a.split("=", 1)[1])
        elif a.startswith("--tail="):
            tail = int(a.split("=", 1)[1])
        elif a.startswith("--fields="):
            fields = [x for x in a.split("=", 1)[1].split(",") if x]
        elif a.startswith("--"):
            raise SystemExit(2)
        else:
            files.append(a)
    return files, prefix, tail, fields


def main(argv):
    files, prefix, tail, fields = parse_args(argv)
    if len(files) != 2:
        print(USAGE)
        return 2
    nat = parse(files[0])
    port = parse(files[1])
    if not nat:
        print("native: no BATTLE lines")
        return 2
    if not port:
        print("port: no BATTLE lines")
        return 2
    if tail > 0:
        if len(nat) < tail or len(port) < tail:
            print("tail=%d not met (native=%d port=%d)"
                  % (tail, len(nat), len(port)))
            return 3
        nat = nat[-tail:]
        port = port[-tail:]
    n = min(len(nat), len(port))
    for i in range(n):
        a, b = nat[i][1], port[i][1]
        for fld in fields:
            if a.get(fld) != b.get(fld):
                print("DIVERGE line=%d field=%s native=%s port=%s"
                      % (i + 1, fld, a.get(fld), b.get(fld)))
                print("native ctx:")
                for j in range(max(0, i - 2), min(len(nat), i + 3)):
                    print("   %s" % nat[j][0])
                print("port ctx:")
                for j in range(max(0, i - 2), min(len(port), i + 3)):
                    print("   %s" % port[j][0])
                return 1
    if len(nat) < prefix or len(port) < prefix:
        print("equal for %d lines, prefix=%d not met" % (n, prefix))
        return 3
    print("equal: %d lines, fields=%d" % (n, len(fields)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
