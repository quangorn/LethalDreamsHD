#!/usr/bin/env python3
"""check_patches.py — гейт «наши патчи ложатся на эту базу и дают тот же bytecode».

Для каждого модуля манифеста с `base` (game-derived):
  1. применить `release/patches/<…>.patch` к базе тем же аплаером
     (`tools/apply_patch.py:apply_patch`, в память, без записи в репозиторий);
  2. скомпилировать результат воркером 2.2.3;
  3. сравнить «ядро» опкодов с опубликованной сборкой `patches/<module>.pyc`
     (это ровно то, что стоит в игре). Ноль расхождений = патч лёг на базу
     семантически так же, как на прежнюю, даже если текст базы изменился.

Статусы: `ok` — применился и паритет; `CONFLICT` — контекст не найден (патч надо
править); `BUILD-FAIL` — результат не компилируется 2.2.3; `PARITY` — применился,
но bytecode разошёлся (смотреть первые расхождения); `NO-BASE` — базы нет.

Команды (из корня репозитория):
  python3 tools/pyc/check_patches.py                       база decompiled_fixed/
  python3 tools/pyc/check_patches.py --base build/decompyle_base/py
  python3 tools/pyc/check_patches.py --only ui/battlemenu
"""
import argparse
import json
import os
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)
import paths  # noqa: E402
import apply_patch as AP  # noqa: E402
from decompyle_base import parity  # noqa: E402

MANIFEST = os.path.join(_ROOT, 'release', 'manifest.json')
OUT = os.path.join(_ROOT, 'build', 'patchcheck')
PY22 = paths.win_path(os.path.join(paths.py223_dir(), 'python.exe'))
WORKER = paths.win_path(os.path.join(_HERE, 'py22_worker.py'))


def main(argv):
    ap = argparse.ArgumentParser(description='гейт патчей против базы-декомпилята')
    ap.add_argument('--base', default=os.path.join(_ROOT, 'decompiled_fixed'),
                    help='каталог базы (по умолчанию decompiled_fixed/)')
    ap.add_argument('--only', help='один модуль (подстрока пути)')
    args = ap.parse_args(argv)

    man = json.load(open(MANIFEST, encoding='utf-8'))
    entries = [e for e in man['code_tier']['entries'] if 'base' in e]
    if args.only:
        entries = [e for e in entries if args.only in e['module']]
    if not entries:
        print('нет модулей манифеста с базой')
        return 2

    env = dict(os.environ, PYTHONPATH=paths.py223_dir() + ';' + paths.lib_dir(),
               PYTHONDONTWRITEBYTECODE='1')
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for e in entries:
        mod = e['module']
        base = os.path.join(args.base, mod + '.py')
        if not os.path.isfile(base):
            rows.append((mod, 'NO-BASE', '', os.path.relpath(base, _ROOT)))
            continue
        report = []
        text, err = AP.apply_patch(open(base, encoding='utf-8').read(),
                                   open(os.path.join(_ROOT, e['patch_file']),
                                        encoding='utf-8').read(), report)
        if text is None:
            rows.append((mod, 'CONFLICT', '', err.split('\n')[0]))
            continue
        py = os.path.join(OUT, mod.replace('/', '__') + '.py')
        pyc = py[:-3] + '.pyc'
        with open(py, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(text)
        proc = subprocess.run([PY22, WORKER, 'build', paths.win_path(py), paths.win_path(pyc)],
                              env=env, cwd=_ROOT, capture_output=True)
        out = proc.stdout.decode('utf-8', 'replace').strip()
        if 'BUILD-OK' not in out:
            rows.append((mod, 'BUILD-FAIL', '', out.split('\n')[-1]))
            continue
        ref = os.path.join(_ROOT, e['repo_pyc'])
        if not os.path.isfile(ref):
            rows.append((mod, 'NO-REF', '', e['repo_pyc']))
            continue
        nobj, nops, detail = parity(ref, pyc)
        cur = os.path.join(_ROOT, e['repo_py'])
        same = os.path.isfile(cur) and open(cur, encoding='utf-8').read() == text
        status = 'ok' if nobj == 0 else 'PARITY'
        rows.append((mod, status, 'text-%s' % ('same' if same else 'differs'),
                     'объектов %d, опкодов %d%s' % (nobj, nops,
                                                   (' — ' + detail[0]) if detail else '')))

    w = max(len(r[0]) for r in rows)
    for mod, status, text_state, extra in rows:
        print('%-*s  %-11s %-12s %s' % (w, mod, status, text_state, extra))
    bad = [r for r in rows if r[1] not in ('ok',)]
    print('%d модулей, ок: %d, требуют правки: %d'
          % (len(rows), len(rows) - len(bad), len(bad)))
    return 0 if not bad else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
