#!/usr/bin/env python
# Python 2.2.3 opcode-stream diff: engine .pyc vs decompiled .py compiled by 2.2.3.
# SET_LINENO is stripped; args resolved to consts/names/varnames.
# Usage: PYTHON.EXE -S disdiff2.py ORIG_PYC DECOMPILED_PY
import sys, marshal, dis, types

SET_LINENO = dis.opname.index('SET_LINENO')
opmap = {}
for i in range(256):
    opmap[dis.opname[i]] = i

def load_pyc(path):
    f = open(path, 'rb')
    d = f.read()
    f.close()
    return marshal.loads(d[8:])

def stream(code):
    co_code = code.co_code
    consts = code.co_consts
    names = code.co_names
    varnames = code.co_varnames
    out = []
    i = 0
    n = len(co_code)
    extend = 0
    while i < n:
        op = ord(co_code[i])
        name = dis.opname[op]
        if name == 'EXTEND_ARG':
            extend += 256
            i += 1
            continue
        if dis.HAS_ARG[op]:
            arg = code[i+1] + extend
            extend = 0
            i += 2
            real = None
            if dis.HASCONST[op]:
                if arg < len(consts):
                    real = consts[arg]
                else:
                    real = 'IDX%d' % arg
            elif dis.HASNAME[op]:
                if arg < len(names):
                    real = names[arg]
                else:
                    real = 'IDX%d' % arg
            elif dis.HASLOCAL[op]:
                if arg < len(varnames):
                    real = varnames[arg]
                else:
                    real = 'IDX%d' % arg
            elif op == SET_LINENO:
                continue
            out.append((name, real))
        else:
            i += 1
            out.append((name, None))
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
    limit = 20
    if len(sys.argv) > 3:
        limit = int(sys.argv[3])
    for k, i, oa, ob in difflist[:limit]:
        print '=== %s (variant %d) ===' % (k, i)
        import difflib
        la = ['%s %s' % (o, a) for o, a in oa]
        lb = ['%s %s' % (o, b) for o, b in ob]
        for l in difflib.ndiff(la, lb):
            if l[:1] != ' ':
                print l
    print 'total diff functions:', len(difflist)

if __name__ == '__main__':
    main()
