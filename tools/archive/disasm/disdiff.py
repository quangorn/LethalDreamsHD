#!/usr/bin/env python
# Python 2.2.3 dis-diff: original engine .pyc vs decompiled .py compiled by 2.2.3.
# Usage: PYTHON.EXE -E -S disdiff.py ORIG_PYC DECOMPILED_PY [max_diff_lines]
import sys, marshal, dis, types, StringIO, difflib

def load_pyc(path):
    f = open(path, 'rb')
    d = f.read()
    f.close()
    return marshal.loads(d[8:])

def collect(code, out, prefix=''):
    key = prefix + code.co_name
    out.append((key, code))
    for k in code.co_consts:
        if isinstance(k, types.CodeType):
            collect(k, out, key + '.')

def dump(code):
    s = StringIO.StringIO()
    old = sys.stdout
    sys.stdout = s
    try:
        dis.dis(code)
    finally:
        sys.stdout = old
    return s.getvalue()

def main():
    orig = load_pyc(sys.argv[1])
    srcpath = sys.argv[2]
    f = open(srcpath, 'rb')
    src = f.read()
    f.close()
    comp = compile(src, srcpath, 'exec')
    maxlines = 40
    if len(sys.argv) > 3:
        maxlines = int(sys.argv[3])
    a = []
    b = []
    collect(orig, a)
    collect(comp, b)
    da = {}
    db = {}
    for k, c in a:
        da[k] = dump(c)
    for k, c in b:
        db[k] = dump(c)
    ka = list(da.keys())
    ka.sort()
    kb = list(db.keys())
    kb.sort()
    missing = []
    for k in ka:
        if k not in db:
            missing.append(k)
    extra = []
    for k in kb:
        if k not in da:
            extra.append(k)
    same = 0
    diffn = 0
    for k in ka:
        if k in db:
            if da[k] == db[k]:
                same += 1
            else:
                diffn += 1
    print 'functions orig=%d compiled=%d same=%d differ=%d missing=%d extra=%d' % (
        len(ka), len(kb), same, diffn, len(missing), len(extra))
    for k in missing:
        print 'MISSING FUNCTION: %s' % k
    for k in extra:
        print 'EXTRA FUNCTION: %s' % k
    budget = maxlines
    for k in ka:
        if k in db and da[k] != db[k]:
            print '=== DIFF %s ===' % k
            d = list(difflib.ndiff(da[k].split('\n'), db[k].split('\n')))
            keep = []
            for l in d:
                if l[:1] != ' ':
                    keep.append(l)
            for l in keep[:budget]:
                print l
            budget = budget - len(keep)
            if budget <= 0:
                print '... (truncated)'
                break

if __name__ == '__main__':
    main()
