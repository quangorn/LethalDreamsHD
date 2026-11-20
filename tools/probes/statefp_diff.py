#!/usr/bin/env python3
"""statefp_diff.py — first differing state fingerprint at e0a0 entry (plan 3.6).

Input: two hdlog.txt-style logs with `STATEFP tick=... unit=... call=...`
lines (emitted by battle/trace.cpp when LDHOOK_BATTLE_TRACE=1). Records are
aligned by (tick, unit, call). Fields are compared in this order: h1 (map1
FNV-1a), h2 (map2-key FNV-1a), vis, s1, s2.

rc: 0 - equal on all common keys; 1 - divergence found; 2 - bad input.
"""
import sys

FIELDS = ("h1", "h2", "vis", "s1", "s2")
USAGE = "usage: statefp_diff.py <native.log> <port.log> [--limit=N]"


def parse(path):
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            i = line.find(" STATEFP ")
            if i < 0:
                continue
            rec = {}
            for tok in line[i + len(" STATEFP "):].split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    rec[k] = v
            rows.append((line.rstrip("\n"), rec))
    return rows


def main(argv):
    limit = 1
    files = []
    for a in argv:
        if a.startswith("--limit="):
            limit = int(a.split("=", 1)[1])
        elif a.startswith("--"):
            print(USAGE)
            return 2
        else:
            files.append(a)
    if len(files) != 2 or limit < 1:
        print(USAGE)
        return 2
    try:
        nat, port = parse(files[0]), parse(files[1])
    except OSError as e:
        print("ERROR: %s" % e)
        return 2
    if not nat or not port:
        print("no STATEFP lines (native=%d port=%d)" % (len(nat), len(port)))
        return 2

    def keyed(rows):
        out = {}
        for raw, rec in rows:
            k = (rec.get("tick"), rec.get("unit"), rec.get("call"))
            if k not in out:
                out[k] = (raw, rec)
        return out

    nk, pk = keyed(nat), keyed(port)
    common = sorted(set(nk) & set(pk), key=lambda k: tuple(
        (0, int(v)) if v is not None and v.lstrip("-").isdigit() else (1, str(v))
        for v in k))
    shown = 0
    for k in common:
        n = nk[k][1]
        p = pk[k][1]
        for fld in FIELDS:
            if n.get(fld) != p.get(fld):
                print("DIVERGE tick=%s unit=%s call=%s field=%s native=%s port=%s"
                      % (k[0], k[1], k[2], fld, n.get(fld), p.get(fld)))
                print("  native: %s" % nk[k][0])
                print("  port:   %s" % pk[k][0])
                shown += 1
                if shown >= limit:
                    return 1
    print("equal: %d common STATEFP keys; native=%d port=%d" %
          (len(common), len(nat), len(port)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
