#!/usr/bin/env python3
"""Снапшот дерева установки (md5 + mtime) и diff к предыдущему.

  python3 tools/release/tree_snapshot.py <install_dir> <out.json> [--diff <prev.json>]
"""
import hashlib
import json
import os
import sys


def snap(root):
    d = {}
    for dp, dns, fns in os.walk(root):
        if '__pycache__' in dp:
            continue
        for f in fns:
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            st = os.stat(p)
            with open(p, 'rb') as fh:
                h = hashlib.md5(fh.read()).hexdigest()
            d[rel] = [h, int(st.st_mtime)]
    return d


def main():
    root, out = sys.argv[1], sys.argv[2]
    s = snap(root)
    with open(out, 'w', encoding='utf-8') as fh:
        json.dump(s, fh, indent=0, sort_keys=True)
    print('files: %d -> %s' % (len(s), out))
    if '--diff' in sys.argv:
        prev = json.load(open(sys.argv[sys.argv.index('--diff') + 1], encoding='utf-8'))
        changed = [k for k in s if k in prev and s[k][0] != prev[k][0]]
        added = [k for k in s if k not in prev]
        removed = [k for k in prev if k not in s]
        for k in sorted(changed):
            print('CHANGED %s  %s -> %s' % (k, prev[k][0][:8], s[k][0][:8]))
        for k in sorted(added):
            print('ADDED   %s' % k)
        for k in sorted(removed):
            print('REMOVED %s' % k)
        print('diff: changed=%d added=%d removed=%d' % (len(changed), len(added), len(removed)))
    return 0


if __name__ == '__main__':
    sys.exit(main())