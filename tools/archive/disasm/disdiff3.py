#!/usr/bin/env python
# Python 2.2.3 normalized dis-diff: engine .pyc vs decompiled .py compiled by 2.2.3.
# Strips byte offsets and SET_LINENO; jump args mapped to target position index.
# Usage: PYTHON.EXE -S disdiff3.py ORIG_PYC DECOMPILED_PY [max_report]
import sys, marshal, dis, types, StringIO, difflib

JUMPS = ('SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY', 'JUMP_ABSOLUTE',
         'JUMP_FORWARD', 'CONTINUE_LOOP', 'SETUP_CHECK', 'BREAK_LOOP')

def norm(rest):
    # code object constants print as <code object NAME at ADDR, file "...", line N>
    if rest.find('<code object ') >= 0:
        parts = rest.split('<code object ')[1]
        name = parts.split(' at ')[0]
        return '<code %s>' % name
    return rest
def load_pyc(path):
    f = open(path, 'rb')
    d = f.read()
    f.close()
    return marshal.loads(d[8:])

def capture(code):
    s = StringIO.StringIO()
    old = sys.stdout
    sys.stdout = s
    try:
        dis.dis(code)
    finally:
        sys.stdout = old
    return s.getvalue()

def stream(code):
    raw = capture(code)
    lines = []
    offs = []
    for ln in raw.split('\n'):
        toks = ln.split()
        if not toks:
            continue
        # dis line: " 12 LOAD_CONST 0 (None)" ; label line ">>": starts with '>>'
        t0 = toks[0]
        op = None
        rest = ''
        off = None
        if t0 == '>>' and len(toks) > 1 and toks[1][0].isdigit():
            off = int(toks[1])
            offs.append(off)
            op = toks[2]
            rest = ' '.join(toks[3:])
        elif t0[0].isdigit():
            off = int(t0)
            offs.append(off)
            op = toks[1]
            rest = ' '.join(toks[2:])
        else:
            continue
        if op == 'SET_LINENO':
            continue
        lines.append((op, off, norm(rest)))
    # map offsets -> stream index (order of appearance; labels refer to last offset)
    idx = {}
    pos = 0
    for i in range(len(lines)):
        (op, off, rest) = lines[i]
        if op == 'LABEL':
            continue
        idx[off] = pos
        pos += 1
    out = []
    for (op, off, rest) in lines:
        if op == 'LABEL':
            continue
        real = rest
        if op in JUMPS and rest:
            try:
                a = int(rest.split()[0])
            except (ValueError, IndexError):
                a = None
            if a is not None:
                if idx.has_key(a):
                    real = '->%d' % idx[a]
                else:
                    real = '->END(%d)' % a
        out.append((op, real))
    return out

def collect(code, out, prefix=''):
    key = prefix + code.co_name
    out.append((key, code))
    for k in code.co_consts:
        if isinstance(k, types.CodeType):
            collect(k, out, key + '.')

def main():
    orig = load_pyc(sys.argv[1])
    srcpath = sys.argv[2]
    f = open(srcpath, 'rb')
    src = f.read()
    f.close()
    comp = compile(src, srcpath, 'exec')
    a = []
    b = []
    collect(orig, a)
    collect(comp, b)
    da = {}
    db = {}
    for k, c in a:
        da.setdefault(k, []).append(stream(c))
    for k, c in b:
        db.setdefault(k, []).append(stream(c))
    ka = list(da.keys())
    ka.sort()
    kb = list(db.keys())
    kb.sort()
    same = 0
    diffn = 0
    difflist = []
    for k in ka:
        if k not in db:
            print 'MISSING %s' % k
            continue
        oa = da[k]
        ob = db[k]
        if len(oa) != len(ob):
            print 'COUNT-MISMATCH %s orig=%d compiled=%d' % (k, len(oa), len(ob))
            difflist.append((k, 0, oa[0], ob[0]))
            continue
        for i in range(len(oa)):
            if oa[i] == ob[i]:
                same += 1
            else:
                diffn += 1
                difflist.append((k, i, oa[i], ob[i]))
    for k in kb:
        if k not in da:
            print 'EXTRA %s' % k
    print 'functions orig=%d compiled=%d stream-same=%d stream-diff=%d' % (len(ka), len(kb), same, diffn)
    limit = 12
    if len(sys.argv) > 3:
        limit = int(sys.argv[3])
    budget = limit
    for k, i, oa, ob in difflist:
        if budget <= 0:
            break
        print '=== %s (variant %d) ===' % (k, i)
        la = []
        for o, a in oa:
            la.append(o + ' ' + a)
        lb = []
        for o, b in ob:
            lb.append(o + ' ' + b)
        kept = 0
        for l in difflib.ndiff(la, lb):
            if l[:1] != ' ':
                print l
                kept += 1
                if kept > budget:
                    break
        budget -= kept
    print 'total diff functions:', len(difflist)

if __name__ == '__main__':
    main()
