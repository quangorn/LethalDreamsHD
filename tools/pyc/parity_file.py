#!/usr/bin/env python3
"""Паритет байткода для готовой пары файлов: .pyc против .py (без нормализации декомпилята).

Файл лежит в tools/pyc/ (рядом с pycbuild.py и pyc_norm.py). Запуск из корня репозитория:
  python3 tools/pyc/parity_file.py <pyc> <py>
Ничего не пишет: воркер 2.2.3 только читает файлы и печатает dis. PYTHONPATH берётся из
pycbuild.ENV (py223 + cache/lib), игровой Lib в путь не входит - импорт воркера не пишет
.pyc в установку игры.
"""
import sys, os, subprocess
# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pycbuild, pyc_norm

ENV = pycbuild.ENV


def worker(*args):
    # cwd=корень репозитория: относительные пути из argv (backup_orig/..., build/...) воркер
    # разрешает от него; ENV без игрового Lib - импорт воркера не пишет .pyc в установку игры.
    p = subprocess.run([pycbuild.PY22, pycbuild.WORKER] + list(args), capture_output=True,
                       env=ENV, cwd=pycbuild.REPO_ROOT)
    out = p.stdout.decode('utf-8', 'replace')
    return [l for l in out.split('\n') if l.strip() and 'import site' not in l]


pyc = sys.argv[1]
py = sys.argv[2]
orig = worker('dis-pyc', pyc.replace('\\', '/'))
mine = worker('dis-py', py.replace('\\', '/'))
a = pycbuild.parse_blocks('\n'.join(orig))
b = pycbuild.parse_blocks('\n'.join(mine))
n = max(len(a), len(b))
bad_sig = []
bad_ops = []
total = 0
matched = 0
for i in range(n):
    if i >= len(a) or i >= len(b):
        bad_ops.append(('count', i, len(a), len(b)))
        continue
    if a[i][0] != b[i][0]:
        bad_ops.append(('name', i, a[i][0], b[i][0]))
        continue
    if a[i][1] != b[i][1]:
        bad_sig.append((i, a[i][0]))
    ta, tb = pyc_norm.tolerant_ops(a[i][2]), pyc_norm.tolerant_ops(b[i][2])
    total += max(len(ta), len(tb))
    matched += sum(1 for x, y in zip(ta, tb) if x == y)
    s = pyc_norm.similarity(ta, tb)
    if s < 0.999:
        k = 0
        while k < min(len(ta), len(tb)) and ta[k] == tb[k]:
            k += 1
        bad_ops.append(('ops', i, a[i][0], round(s, 4), k, ta[k:k + 4], tb[k:k + 4]))
sim = matched / total if total else 1.0
print('code objects orig=%d mine=%d; близость %.4f; sig=%d ops=%d' % (len(a), len(b), sim, len(bad_sig), len(bad_ops)))
for d in bad_sig[:8]:
    print('  SIG', d)
for d in bad_ops[:10]:
    print('  OPS', d)
