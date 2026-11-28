#!/usr/bin/env python3
"""pyc_bytes.py — сверка двух .pyc на уровне маршалла (сырые байты co_code).

Воркер `dis-pyc` печатает токены через свою нормализацию и разрешает аргументы по
`co_names`, поэтому локальные (`co_varnames`) имена печатаются чужие, а хвостовые
`LOAD_CONST None`/`RETURN_VALUE` и `__module__`-пары альфы видны только как разность
длин. Здесь: дерево кодовых объектов по порядку обхода, сравнение `co_code` побайтно,
и по первому расхождению печатаются соседние инструкции с расшифровкой аргумента
(`co_consts`/`co_names`), чтобы видеть настоящую разницу компилята и родного .pyc.

Запуск из корня репозитория:
  python3 tools/pyc/pyc_bytes.py <orig.pyc> <mine.pyc>            первые расхождения
  python3 tools/pyc/pyc_bytes.py <orig.pyc> <mine.pyc> --all      все объекты
"""
import sys, os

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import pyc_scan

pyc_scan.RAW_STR = True

OPS = {}
with open(os.path.join(_HERE, 'opcode22.txt')) as f:
    for line in f:
        p = line.split()
        if len(p) == 2:
            OPS[int(p[1])] = p[0]

HAVE_ARG = 90


def tree(path):
    d = open(path, 'rb').read()
    top, _ = pyc_scan.read(d, 8)
    acc = []

    def walk(c):
        acc.append(c)
        for x in c['consts']:
            if isinstance(x, dict) and 'code' in x:
                walk(x)

    walk(top)
    return acc


def ops(bc, strip_lineno=True):
    out = []
    i = 0
    while i < len(bc):
        op = bc[i]
        if op >= HAVE_ARG:
            arg = bc[i + 1] | (bc[i + 2] << 8)
            i += 3
        else:
            arg = None
            i += 1
        if strip_lineno and OPS.get(op) == 'SET_LINENO':
            continue
        out.append((op, arg))
    return out


def parse_offs(c):
    """Опкоды с байтовыми смещениями: [(op, arg, offset)]."""
    bc = c['code']
    out = []
    i = 0
    while i < len(bc):
        op = bc[i]
        if op >= HAVE_ARG:
            out.append((op, bc[i + 1] | (bc[i + 2] << 8), i))
            i += 3
        else:
            out.append((op, None, i))
            i += 1
    return out


def core(c):
    """«Ядро» потока опкодов: без косметики компилятора, прыжки — индексами опкодов.
    Снимает пару `LOAD_GLOBAL __name__` + `STORE_NAME __module__` (есть только в
    официальном 2.3-поведении классов) и схлопывает повторный хвост
    `LOAD_CONST None` + `RETURN_VALUE` (2.2 и альфа эмитят разное число)."""
    items = [x for x in parse_offs(c) if OPS.get(x[0]) != 'SET_LINENO']
    keep = []
    for k, (op, arg, off) in enumerate(items):
        if OPS.get(op) == 'LOAD_GLOBAL' and arg is not None and c['names'][arg:arg + 1] == [b'__name__']:
            nxt = items[k + 1] if k + 1 < len(items) else None
            if nxt and OPS.get(nxt[0]) == 'STORE_NAME' and nxt[1] is not None \
                    and c['names'][nxt[1]:nxt[1] + 1] == [b'__module__']:
                continue
        keep.append((op, arg, off))

    def is_null(op, arg):
        if OPS.get(op) != 'LOAD_CONST' or arg is None:
            return False
        return arg < len(c['consts']) and c['consts'][arg] is None

    while len(keep) >= 4:
        o1, a1, _ = keep[-4]
        o2, a2, _ = keep[-3]
        o3, a3, _ = keep[-2]
        o4, a4, _ = keep[-1]
        if OPS.get(o1) == 'LOAD_CONST' and is_null(o2, a2) and OPS.get(o3) == 'RETURN_VALUE' \
                and OPS.get(o4) == 'RETURN_VALUE' and is_null(o1, a1):
            keep = keep[:-2]
            continue
        break

    idx = {off: j for j, (op, arg, off) in enumerate(items)}
    out = []
    for op, arg, off in keep:
        name = OPS.get(op)
        if name in ('JUMP_ABSOLUTE', 'CONTINUE_LOOP') and arg is not None:
            out.append((name, idx.get(arg, -1)))
        elif name in ('JUMP_FORWARD', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE', 'FOR_ITER',
                      'SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY') and arg is not None:
            out.append((name, idx.get(off + arg, -1)))
        else:
            out.append((op, arg))
    return out


def arg_name(c, op, arg):
    name = OPS.get(op, str(op))
    if arg is None:
        return name
    if name == 'LOAD_CONST':
        v = c['consts'][arg] if arg < len(c['consts']) else None
        if isinstance(v, dict) and 'code' in v:
            return 'LOAD_CONST <code:%s>' % v['name']
        return 'LOAD_CONST %r' % (v,)
    if name.endswith('_NAME') or name.endswith('_GLOBAL') or name.endswith('_DEREF'):
        v = c['names'][arg] if arg < len(c['names']) else None
        return '%s %r' % (name, v)
    if name.endswith('_FAST'):
        v = c['varnames'][arg] if arg < len(c['varnames']) else None
        return '%s %r' % (name, v)
    if name in ('JUMP_ABSOLUTE', 'JUMP_FORWARD', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE',
               'FOR_ITER', 'SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY', 'CONTINUE_LOOP'):
        return '%s +%d' % (name, arg)
    return '%s %d' % (name, arg)


def show(c, seq):
    return ' '.join(arg_name(c, op, a) for op, a in seq)


FIELDS = ('argcount', 'nlocals', 'stacksize', 'flags', 'name', 'firstlineno')


def field_diffs(a, b):
    """Расхождения полей кодового объекта (кроме lnotab и кода)."""
    out = []
    for k in FIELDS:
        if a.get(k) != b.get(k):
            out.append((k, a.get(k), b.get(k)))
    for k in ('names', 'varnames', 'consts'):
        la = [x if not (isinstance(x, dict) and 'code' in x) else '<code>' for x in a[k]]
        lb = [x if not (isinstance(x, dict) and 'code' in x) else '<code>' for x in b[k]]
        if la != lb:
            out.append((k, la[:12], lb[:12]))
    return out


def main():
    a_path = sys.argv[1].replace('\\', '/')
    b_path = sys.argv[2].replace('\\', '/')
    all_mode = '--all' in sys.argv
    fields_mode = '--fields' in sys.argv
    A = tree(a_path)
    B = tree(b_path)
    print('объектов: A=%d B=%d' % (len(A), len(B)))
    if '--core' in sys.argv:
        FAST = ('LOAD_FAST', 'STORE_FAST', 'DELETE_FAST', 'LOAD_LOCAL')
        nc = 0
        for i in range(min(len(A), len(B))):
            a, b = A[i], B[i]
            ca, cb = core(a), core(b)
            n = 0
            for k in range(min(len(ca), len(cb))):
                o1, v1 = ca[k]
                o2, v2 = cb[k]
                if OPS.get(o1) != OPS.get(o2):
                    n += 1
                    print('### %d %s op %d: A=%s B=%s' % (i, a['name'], k, OPS.get(o1), OPS.get(o2)))
                    continue
                if OPS.get(o1) in FAST:
                    continue
                if v1 != v2:
                    n += 1
                    av = arg_name(a, o1, v1)
                    bv = arg_name(b, o2, v2)
                    if av == bv:
                        continue
                    print('### %d %s op %d: A=%s B=%s' % (i, a['name'], k, av, bv))
            if len(ca) != len(cb):
                nc += 1
                print('### %d %s: core-длина A=%d B=%d, расхождений %d' % (i, a['name'], len(ca), len(cb), n))
            elif n:
                nc += 1
        print('объектов с расхождениями ядра: %d' % nc)
        return
    if fields_mode:
        nf = 0
        for i in range(min(len(A), len(B))):
            a, b = A[i], B[i]
            fd = field_diffs(a, b)
            fd = [f for f in fd if f[0] in ('argcount', 'nlocals', 'stacksize', 'flags')]
            if fd:
                nf += 1
                print('### %d %s %s' % (i, a['name'], fd))
        print('объектов с расхождениями числовых полей: %d' % nf)
        return
    if '--names' in sys.argv:
        nf = 0
        for i in range(min(len(A), len(B))):
            a, b = A[i], B[i]
            fd = [f for f in field_diffs(a, b) if f[0] in ('names', 'varnames', 'consts')]
            if fd:
                nf += 1
                print('### %d %s' % (i, a['name']))
                for f in fd:
                    print('   ', f[0], 'A=', f[1], 'B=', f[2])
        print('объектов с расхождениями names/varnames/consts: %d' % nf)
        return
    ndiff = 0
    for i in range(min(len(A), len(B))):
        a, b = A[i], B[i]
        ca = a['code'].encode('latin1') if isinstance(a['code'], str) else a['code']
        cb = b['code'].encode('latin1') if isinstance(b['code'], str) else b['code']
        sa = ops(ca)
        sb = ops(cb)
        if sa == sb:
            continue
        ndiff += 1
        k = 0
        while k < min(len(sa), len(sb)) and sa[k] == sb[k]:
            k += 1
        print('### %d %s: ops A=%d B=%d, первое расхождение на op %d' % (i, a['name'], len(sa), len(sb), k))
        lo = max(0, k - 6)
        print('   A:', show(a, sa[lo:k]), '>>', show(a, sa[k:k + 6]))
        print('   B:', show(b, sb[lo:k]), '>>', show(b, sb[k:k + 6]))
        if not all_mode:
            break
    print('объектов с differing co_code: %d' % ndiff)


if __name__ == '__main__':
    main()
