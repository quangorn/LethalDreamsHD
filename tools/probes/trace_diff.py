#!/usr/bin/env python3
"""trace_diff.py - A/B diff of the battle tick trace (PLAN_BATTLE_AI_CPP.md, 1.7).

Input: two hdlog.txt-style logs containing `BATTLE tick=...` lines.
By default lines are aligned by (tick, unit): only keys present in both logs
are compared, sorted by tick. With --align=pos alignment is positional
(line i <-> line i, as before); positional is also the automatic fallback
when a BATTLE line lacks `tick` or `unit`.
Fields are compared in the order given by --fields; `us` and `ai` are
excluded by default (not deterministic).

Output: up to --limit divergences (default 1), each with native/port values
and +-2 lines of context, plus the compared range (tick A..B, N common keys).

rc: 0 - no divergence and both logs have >= --prefix compared entries;
     1 - divergence found;
     2 - a file cannot be read, has no BATTLE lines, or bad arguments;
     3 - no divergence, but the compared range is empty or shorter than
         --prefix / --tail.
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
         "[--prefix=N|--tail=N] [--fields=a,b,c] "
         "[--align=tick|pos] [--limit=N]")


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


def keyed(rows):
    """Map (tick, unit) -> (index, raw line, rec); None if a key is missing.

    A repeated key keeps its first occurrence.
    """
    out = {}
    for idx, (raw, rec) in enumerate(rows):
        t, u = rec.get("tick"), rec.get("unit")
        if t is None or u is None:
            return None
        if (t, u) not in out:
            out[(t, u)] = (idx, raw, rec)
    return out


def key_order(key):
    def order(v):
        try:
            return (0, int(v), "")
        except (TypeError, ValueError):
            return (1, 0, str(v))
    return (order(key[0]), order(key[1]))


def parse_args(argv):
    files = []
    prefix = 0
    tail = 0
    fields = COMPARE
    align = "tick"
    limit = 1
    for a in argv:
        if a.startswith("--prefix="):
            prefix = int(a.split("=", 1)[1])
        elif a.startswith("--tail="):
            tail = int(a.split("=", 1)[1])
        elif a.startswith("--fields="):
            fields = [x for x in a.split("=", 1)[1].split(",") if x]
        elif a.startswith("--align="):
            align = a.split("=", 1)[1]
        elif a.startswith("--limit="):
            limit = int(a.split("=", 1)[1])
        elif a.startswith("--"):
            raise SystemExit(2)
        else:
            files.append(a)
    if align not in ("tick", "pos") or limit < 1 or prefix < 0 or tail < 0:
        raise SystemExit(2)
    return files, prefix, tail, fields, align, limit


def context(rows, idx, tag):
    print("%s ctx:" % tag)
    for j in range(max(0, idx - 2), min(len(rows), idx + 3)):
        print("   %s" % rows[j][0])


def compare_pairs(pairs, fields, limit):
    """pairs: list of (label, a, b, a_rows, a_idx, b_rows, b_idx)."""
    shown = 0
    for label, a, b, a_rows, a_idx, b_rows, b_idx in pairs:
        for fld in fields:
            if a.get(fld) != b.get(fld):
                print("DIVERGE %s field=%s native=%s port=%s"
                      % (label, fld, a.get(fld), b.get(fld)))
                context(a_rows, a_idx, "native")
                context(b_rows, b_idx, "port")
                shown += 1
                if shown >= limit:
                    return True
    return False


def main(argv):
    files, prefix, tail, fields, align, limit = parse_args(argv)
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

    nat_key = keyed(nat) if align == "tick" else None
    port_key = keyed(port) if align == "tick" else None
    if align == "tick" and (nat_key is None or port_key is None):
        print("tick/unit missing; falling back to positional alignment")
        align = "pos"

    if align == "tick":
        common = sorted((k for k in nat_key if k in port_key), key=key_order)
        if not common:
            print("common range: empty (native=%d port=%d keys)"
                  % (len(nat_key), len(port_key)))
            return 3
        if tail and len(common) < tail:
            print("common range: %d keys, tail=%d not met" % (len(common), tail))
            return 3
        if prefix and len(common) < prefix:
            print("common range: %d keys, prefix=%d not met" % (len(common), prefix))
            return 3
        if tail:
            common = common[-tail:]
        if prefix:
            common = common[:prefix]
        print("compared range: tick %s..%s, %d common keys (align=tick)"
              % (common[0][0], common[-1][0], len(common)))
        pairs = []
        for k in common:
            ni, nraw, nrec = nat_key[k]
            pi, praw, prec = port_key[k]
            pairs.append(("tick=%s unit=%s native_line=%d port_line=%d"
                          % (k[0], k[1], ni + 1, pi + 1),
                          nrec, prec, nat, ni, port, pi))
        if compare_pairs(pairs, fields, limit):
            return 1
        print("equal: %d common keys, fields=%d" % (len(common), len(fields)))
        return 0

    if tail > 0:
        if len(nat) < tail or len(port) < tail:
            print("tail=%d not met (native=%d port=%d)"
                  % (tail, len(nat), len(port)))
            return 3
        nat = nat[-tail:]
        port = port[-tail:]
    n = min(len(nat), len(port))
    pairs = [("line=%d" % (i + 1), nat[i][1], port[i][1],
              nat, i, port, i) for i in range(n)]
    if compare_pairs(pairs, fields, limit):
        return 1
    if len(nat) < prefix or len(port) < prefix:
        print("equal for %d lines, prefix=%d not met" % (n, prefix))
        return 3
    print("equal: %d lines, fields=%d" % (n, len(fields)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
