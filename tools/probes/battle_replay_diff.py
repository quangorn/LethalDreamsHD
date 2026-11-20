#!/usr/bin/env python3
"""battle_replay_diff.py — offline A/B diff of e0a0 (battle_ai_ab_method.md §3.2).

Input: capture.jsonl (native `cap`/`res` records) and res_port.jsonl (harness
`res_port` records). Records are aligned by `seq`. Compared fields, in order:
node, code, iters, iter_seq, map1_after{pos -> (filter,g,d2,h10k,cost)},
occ_after (sorted key set). Node identity is the position (x,y); pointers and
raw ordering of map1 are ignored (map1 key = 0 in native).

Output: up to --limit divergences (default 1) with field-level native/port
values and +-2 raw-record context, then a summary.

rc: 0 - equal and >= --prefix compared records;
    1 - divergence found;
    2 - file unreadable / malformed / no res records;
    3 - no divergence but fewer than --prefix compared records.
"""
import json
import sys

COMPARE = ["node", "code", "iters", "iter_seq", "map1_after", "occ_after", "leaf"]
USAGE = ("usage: battle_replay_diff.py <capture.jsonl> <res_port.jsonl> "
         "[--prefix=N] [--fields=a,b,c] [--limit=N]")


def parse_args(argv):
    files, prefix, limit, fields = [], 0, 1, COMPARE
    for a in argv:
        if a.startswith("--prefix="):
            prefix = int(a.split("=", 1)[1])
        elif a.startswith("--limit="):
            limit = int(a.split("=", 1)[1])
        elif a.startswith("--fields="):
            fields = [x for x in a.split("=", 1)[1].split(",") if x]
        elif a.startswith("--"):
            raise ValueError(a)
        else:
            files.append(a)
    if len(files) != 2 or prefix < 0 or limit < 1:
        raise ValueError(USAGE)
    if any(f not in COMPARE for f in fields):
        raise ValueError("unknown field in --fields")
    return files[0], files[1], prefix, fields, limit


def read_lines(path):
    """Строки файла; хвостовая недописанная запись (обрыв при гашении игры)
    отбрасывается, чтобы aborted capture не давал rc 2."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        data = fh.read()
    lines = data.split("\n")
    if lines and lines[-1] != "":
        lines.pop()
    return lines


def load_native(path):
    """seq -> (rec, raw_line, tick, unit). Native result lives in `res`."""
    out = {}
    for line in read_lines(path):
        if not line.strip():
            continue
        rec = json.loads(line)
        if rec.get("kind") != "res":
            continue
        seq = rec.get("seq")
        if not isinstance(seq, int) or seq in out:
            raise ValueError("bad/duplicate native res seq: %r" % seq)
        out[seq] = (rec, line)
    return out


def load_port(path):
    out = {}
    for line in read_lines(path):
        if not line.strip():
            continue
        rec = json.loads(line)
        if rec.get("kind") != "res_port":
            continue
        seq = rec.get("seq")
        if not isinstance(seq, int) or seq in out:
            raise ValueError("bad/duplicate port seq: %r" % seq)
        out[seq] = (rec, line)
    return out


def pos(v):
    if v is None:
        return None
    if not isinstance(v, list) or len(v) != 2:
        raise ValueError("bad pos: %r" % (v,))
    return (v[0], v[1])


def seq_of_positions(v):
    if not isinstance(v, list):
        raise ValueError("bad iter_seq: %r" % (v,))
    return tuple(pos(x) for x in v)


def map_by_pos(v):
    if not isinstance(v, list):
        raise ValueError("bad map1_after: %r" % (v,))
    out = {}
    for e in v:
        key = pos(e.get("pos"))
        out[key] = tuple(e.get(k) for k in ("filter", "g", "d2", "h10k", "cost"))
    return out


def occ_set(v):
    if not isinstance(v, list):
        raise ValueError("bad occ_after: %r" % (v,))
    return tuple(sorted(v))


def fields(rec):
    return {
        "node": pos(rec.get("node")),
        "code": rec.get("code"),
        "iters": rec.get("iters"),
        "iter_seq": seq_of_positions(rec.get("iter_seq", [])),
        "map1_after": map_by_pos(rec.get("map1_after", [])),
        "occ_after": occ_set(rec.get("occ_after", [])),
        "leaf": leaf_stream(rec.get("leaf", {})),
    }


def leaf_stream(v):
    """Нативный и портовый потоки листьев; ignored `ptr`/`timer` (identity)."""
    if not isinstance(v, dict):
        raise ValueError("bad leaf: %r" % (v,))
    return (tuple(tuple(x) if isinstance(x, list) else x
                  for x in v.get("path", [])),
            tuple(v.get("heur", [])),
            tuple(v.get("timeout", [])))


def fmt(v):
    s = repr(v)
    return s if len(s) <= 300 else s[:297] + "..."


def context(rows, idx, tag):
    print("%s ctx:" % tag)
    for j in range(max(0, idx - 2), min(len(rows), idx + 3)):
        print("   seq=%d %s" % (rows[j][0], rows[j][1]))


def main(argv):
    try:
        npath, ppath, prefix, selected, limit = parse_args(argv)
        nat = load_native(npath)
        port = load_port(ppath)
    except (ValueError, OSError, json.JSONDecodeError) as e:
        print("ERROR: %s" % e)
        print(USAGE)
        return 2

    if not nat:
        print("native: no res records in %s" % npath)
        return 2
    if not port:
        print("port: no res_port records in %s" % ppath)
        return 2
    common = sorted(set(nat) & set(port))
    if not common:
        print("common range: empty (native=%d port=%d seqs)"
              % (len(nat), len(port)))
        return 3
    if prefix and len(common) < prefix:
        print("common range: %d seqs, prefix=%d not met" % (len(common), prefix))
        return 3

    nat_rows = [(s, nat[s][1]) for s in sorted(nat)]
    port_rows = [(s, port[s][1]) for s in sorted(port)]
    ni = {s: i for i, (s, _) in enumerate(nat_rows)}
    pi = {s: i for i, (s, _) in enumerate(port_rows)}

    shown = 0
    for seq in common:
        a, b = fields(nat[seq][0]), fields(port[seq][0])
        for fld in COMPARE:
            if fld not in selected:
                continue
            if a[fld] != b[fld]:
                print("DIVERGE seq=%d field=%s native=%s port=%s"
                      % (seq, fld, fmt(a[fld]), fmt(b[fld])))
                context(nat_rows, ni[seq], "native")
                context(port_rows, pi[seq], "port")
                shown += 1
                if shown >= limit:
                    return 1
    print("equal: %d seqs, fields=%d" % (len(common), len(selected)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
