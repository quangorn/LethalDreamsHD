#!/usr/bin/env python3
"""Паритет байткода: компилят нашего .py против оригинального .pyc.

Файл лежит в tools/pyc/ (рядом с pycbuild.py и pyc_norm.py). Запуск из корня репозитория:
  python3 tools/pyc/parity_vs.py <module> <original.pyc>
Пишет только build/<module>.py (его создаёт pycbuild.build_py); установку игры читает,
не изменяет.
"""
import sys, os
# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pycbuild, pyc_norm

module = sys.argv[1]
orig_pyc = sys.argv[2]

py = pycbuild.build_py(module)
mine, rc = pycbuild.run_worker('dis-py', py.replace(os.sep, '/'))
orig, rc2 = pycbuild.run_worker('dis-pyc', orig_pyc.replace(os.sep, '/'))
a = pycbuild.parse_blocks(orig)
b = pycbuild.parse_blocks(mine)
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
        bad_sig.append((i, a[i][0], a[i][1], b[i][1]))
    ta, tb = pyc_norm.tolerant_ops(a[i][2]), pyc_norm.tolerant_ops(b[i][2])
    total += max(len(ta), len(tb))
    matched += sum(1 for x, y in zip(ta, tb) if x == y)
    if pyc_norm.similarity(ta, tb) < 0.995:
        k = 0
        while k < min(len(ta), len(tb)) and ta[k] == tb[k]:
            k += 1
        bad_ops.append(('ops', i, a[i][0], round(pyc_norm.similarity(ta, tb), 4), k, ta[k:k + 5], tb[k:k + 5]))
sim = matched / total if total else 1.0
if not bad_ops and not bad_sig:
    print('PARITY-EQUAL %s (%d code objects)' % (module, len(a)))
else:
    print('PARITY %s: близость %.4f; sig=%d ops=%d' % (module, sim, len(bad_sig), len(bad_ops)))
    for d in bad_ops[:6]:
        print('  OPS', d)
    for d in bad_sig[:6]:
        print('  SIG', d[0], d[1])
        print('    orig:', str(d[2])[:300])
        print('    mine:', str(d[3])[:300])
