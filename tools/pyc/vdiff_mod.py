import re, difflib
from vdiff_canon import canon_list, real_diffs

pb = None  # модуль pycbuild, задаётся извне


def parse_vdiff(out):
    """Строки воркера vdiff -> {'A': [(name, tokens, consts)], 'B': [...]}."""
    trees = {'A': [], 'B': []}
    for line in out.split('\n'):
        p = line.split(None, 3)
        if len(p) < 3:
            continue
        kind, side, idx = p[0], p[1], int(p[2])
        if kind not in ('TREE', 'TOKEN', 'CONSTS') or side not in ('A', 'B'):
            continue
        while len(trees[side]) <= idx:
            trees[side].append([None, [], []])
        if kind == 'TREE':
            trees[side][idx][0] = p[3] if len(p) > 3 else None
        elif kind == 'CONSTS':
            if len(p) < 4:
                continue
            v = p[3]
            if v == '<code>':
                trees[side][idx][2].append('<code>')
            else:
                try:
                    trees[side][idx][2].append(eval(bytes.fromhex(v).decode('cp1251', 'replace'),
                                                    {'__builtins__': {}}, {}))
                except Exception:
                    trees[side][idx][2].append(v)
        else:
            tok = re.sub(r'^\d+\s+', '', p[3].strip())
            trees[side][idx][1].append(tok)
    return trees


def show_tok(t):
    h = 'LOAD_CONST HEX '
    if t.startswith(h):
        try:
            return 'LOAD_CONST %s' % repr(eval(bytes.fromhex(t[len(h):]).decode('cp1251', 'replace'),
                                               {'__builtins__': {}}, {}))[:60]
        except Exception:
            return t
    return t


def build_py(module):
    """Нормализованный .py компилята (создаёт при отсутствии)."""
    py = pb.BUILD + '/' + module + '.py'
    if not pb.os.path.exists(py):
        src = pb.SNAP + '/' + module + '.py'
        text = pb.normalize(pb.open(src, encoding='utf-8').read())
        pb.os.makedirs(pb.os.path.dirname(py), exist_ok=True)
        pb.open(py, 'w', encoding='utf-8', newline='\n').write(text)
    return py


def cmd_vdiff(module):
    """Смысловой дифф: оригинал против компилята. Прыжки канонизированы относительно
    позиции опкода, None-имена = None-константа, хвостовой return вырезан."""
    pyc = pb.game_pyc(module)
    py = build_py(module)
    out, rc = pb.run_worker('vdiff', pyc.replace(pb.os.sep, '/'), py.replace(pb.os.sep, '/'))
    trees = parse_vdiff(out)
    n = min(len(trees['A']), len(trees['B']))
    if len(trees['A']) != len(trees['B']):
        print('код-объектов: оригинал %d, компилят %d' % (len(trees['A']), len(trees['B'])))
    total = 0
    for i in range(n):
        na, ta, ca = trees['A'][i]
        nb, tb, cb = trees['B'][i]
        for tag, xa, xb in real_diffs(ta, tb, ca, cb):
            total += 1
            print('код-объект %d (%s) %s' % (i, na, tag))
            for t in xa:
                print('   - A', show_tok(t))
            for t in xb:
                print('   + B', show_tok(t))
    print('%s: смысловых расхождений %d (код-объектов %d)' % (module, total, n))


def cmd_vdiff_all(out_path=None):
    """Скан по всем модулям; пишет в файл строку 'модуль количество'."""
    rows = []
    for m in pb.all_modules():
        pyc = pb.game_pyc(m)
        py = pb.BUILD + '/' + m + '.py'
        if not pyc or not pb.os.path.exists(py):
            continue
        out, rc = pb.run_worker('vdiff', pyc.replace(pb.os.sep, '/'), py.replace(pb.os.sep, '/'))
        trees = parse_vdiff(out)
        n = min(len(trees['A']), len(trees['B']))
        real = 0
        samples = []
        for i in range(n):
            na, ta, ca = trees['A'][i]
            nb, tb, cb = trees['B'][i]
            for tag, xa, xb in real_diffs(ta, tb, ca, cb):
                real += 1
                if len(samples) < 4:
                    samples.append((na, tag, xa[:2], xb[:2]))
        rows.append((m, real, samples))
    bad = [r for r in rows if r[1] > 0]
    print('модулей с реальными расхождениями: %d из %d' % (len(bad), len(rows)))
    for m, real, samples in bad:
        print(' %s: %d' % (m, real))
        for na, tag, xa, xb in samples:
            print('   %s %s A:%s B:%s' % (na, tag, [show_tok(t)[:36] for t in xa],
                                           [show_tok(t)[:36] for t in xb]))
    if out_path:
        open(out_path, 'w', encoding='utf-8').write(
            '\n'.join('%s %d' % (m, real) for m, real, s in rows))
