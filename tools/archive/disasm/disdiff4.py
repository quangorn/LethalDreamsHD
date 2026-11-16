#!/usr/bin/env python
# Python 2.2.3 normalized dis-diff v4: engine .pyc vs decompiled .py compiled by 2.2.3.
# Key = opcode + annotation (consts/names resolved); SET_LINENO and numeric indices stripped;
# jump args mapped to target position index.
# Usage: PYTHON.EXE -S disdiff4.py ORIG_PYC DECOMPILED_PY [max_report]
import sys, marshal, dis, types, StringIO, difflib

JUMPS = ('SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY', 'JUMP_ABSOLUTE',
         'JUMP_FORWARD', 'CONTINUE_LOOP', 'SETUP_CHECK', 'BREAK_LOOP')

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

def norm(rest):
    # code object constants print as <code object NAME at ADDR, file "...", line N>
    if rest.find('<code object ') >= 0:
        parts = rest.split('<code object ')[1]
        name = parts.split(' at ')[0]
        return '<code %s>' % name
    return rest

def annot(rest):
    # 'N (annotation)' -> annotation; plain 'N' -> None
    i = rest.find('(')
    if i >= 0:
        return rest[i:]
    return None

def stream(code):
    raw = capture(code)
    lines = []
    offs = []
    for ln in raw.split('\n'):
        toks = ln.split()
        if not toks:
            continue
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
    idx = {}
    pos = 0
    for i in range(len(lines)):
        (op, off, rest) = lines[i]
        idx[off] = pos
        pos += 1
    out = []
    for i in range(len(lines)):
        (op, off, rest) = lines[i]
        a = annot(rest)
        if op in JUMPS:
            # jump numeric arg maps to stream index
            first = rest.split('(')[0].split()[0] if rest else '0'
            try:
                target = int(first.split()[0])
            except (ValueError, IndexError):
                target = None
            if target is not None:
                if idx.has_key(target):
                    a = '->%d' % idx[target]
                else:
                    a = '->END(%d)' % target
        if a is None:
            key = op
        else:
            key = op + ' ' + a
        out.append(key)
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
                difflist.append((k, i, oa[i], ob[i]))
    for k in kb:
        if k not in da:
            print 'EXTRA %s' % k
    total = len(ka)
    print 'functions orig=%d compiled=%d stream-same=%d stream-diff=%d' % (len(ka), len(kb), same, len(difflist))
    limit = 40
    if len(sys.argv) > 3:
        limit = int(sys.argv[3])
    budget = limit
    for k, i, oa, ob in difflist:
        if budget <= 0:
            break
        print '=== %s (variant %d) ===' % (k, i)
        la = oa
        lb = ob
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
