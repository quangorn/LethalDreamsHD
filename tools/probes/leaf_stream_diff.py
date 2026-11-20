#!/usr/bin/env python3
"""leaf_stream_diff.py — element-wise A/B of the cb40 leaf stream (path).

Input: capture.jsonl (native `res` records) and res_port.jsonl (port `res_port`
records), aligned by `seq`. Per seq compares `res.leaf.path` with
`res_port.leaf.path` element-wise and prints native count, port count, the
length of the equal common prefix, the first differing index with both values,
and an explicit EXTRA marker for the trailing entries of the longer side.

rc: 0 - every seq: common prefix equal AND equal lengths;
    1 - divergence;
    2 - file unreadable / malformed / no records.
"""
import json
import sys

USAGE = "usage: leaf_stream_diff.py <capture.jsonl> <res_port.jsonl> [--extra-limit=N]"


def parse_args(argv):
    files, extra_limit = [], 8
    for a in argv:
        if a.startswith("--extra-limit="):
            extra_limit = int(a.split("=", 1)[1])
        elif a.startswith("--"):
            raise ValueError(a)
        else:
            files.append(a)
    if len(files) != 2 or extra_limit < 1:
        raise ValueError(USAGE)
    return files[0], files[1], extra_limit


def read_lines(path):
    """Хвостовая недописанная запись (обрыв при гашении игры) отбрасывается."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        data = fh.read()
    lines = data.split("\n")
    if lines and lines[-1] != "":
        lines.pop()
    return lines


def load(path, kind):
    out = {}
    for line in read_lines(path):
        if not line.strip():
            continue
        rec = json.loads(line)
        if rec.get("kind") != kind:
            continue
        seq = rec.get("seq")
        if not isinstance(seq, int) or seq in out:
            raise ValueError("bad/duplicate %s seq: %r" % (kind, seq))
        out[seq] = rec
    return out


def norm_leaf(x):
    """Элемент потока: [w0..w4] или null."""
    if x is None:
        return None
    if not isinstance(x, list):
        raise ValueError("bad leaf entry: %r" % (x,))
    return tuple(x)


def path_of(rec):
    leaf = rec.get("leaf")
    if not isinstance(leaf, dict):
        raise ValueError("bad leaf block at seq=%r" % rec.get("seq"))
    p = leaf.get("path")
    if not isinstance(p, list):
        raise ValueError("bad leaf.path at seq=%r" % rec.get("seq"))
    return [norm_leaf(x) for x in p]


def fmt(v):
    s = "null" if v is None else repr(list(v))
    return s if len(s) <= 300 else s[:297] + "..."


def extras(side, start, lst, limit):
    shown = ["%d=%s" % (i, fmt(lst[i])) for i in range(start, min(len(lst), start + limit))]
    tail = len(lst) - start - len(shown)
    return "EXTRA %s[%d..%d] %s%s" % (side, start, len(lst) - 1,
                                      " ".join(shown),
                                      " ... +%d more" % tail if tail > 0 else "")


def main(argv):
    try:
        npath, ppath, extra_limit = parse_args(argv)
        nat = load(npath, "res")
        port = load(ppath, "res_port")
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
        print("common range: empty (native=%d port=%d seqs)" % (len(nat), len(port)))
        return 2

    n_ok, n_prefix_diff, n_len_diff = 0, 0, 0
    first = None
    for seq in common:
        a, b = path_of(nat[seq]), path_of(port[seq])
        m = min(len(a), len(b))
        idx = 0
        while idx < m and a[idx] == b[idx]:
            idx += 1
        prefix_equal = idx
        diff = (idx < m) or (len(a) != len(b))
        parts = ["seq=%d native=%d port=%d prefix_equal=%d" %
                 (seq, len(a), len(b), prefix_equal)]
        if idx < m:
            parts.append("first_diff=%d native=%s port=%s" %
                         (idx, fmt(a[idx]), fmt(b[idx])))
            n_prefix_diff += 1
            if first is None:
                first = (seq, parts[-1])
        elif len(a) != len(b):
            n_len_diff += 1
            if first is None:
                first = (seq, "prefix equal, port +%d" % (len(b) - len(a))
                         if len(b) > len(a) else "prefix equal, native +%d" %
                         (len(a) - len(b)))
        else:
            n_ok += 1
        side = "port" if len(b) > len(a) else "native"
        if len(a) != len(b):
            parts.append(extras(side, m, b if len(b) > len(a) else a, extra_limit))
        print("LEAF " + " ".join(parts) + (" DIVERGE" if diff else " OK"))

    print("SUMMARY: seqs=%d equal=%d prefix_diff=%d length_diff=%d rc=%d" %
          (len(common), n_ok, n_prefix_diff, n_len_diff,
           0 if (n_prefix_diff == 0 and n_len_diff == 0) else 1))
    if first is not None:
        print("FIRST_DIVERGE seq=%d %s" % (first[0], first[1]))
    return 0 if (n_prefix_diff == 0 and n_len_diff == 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
