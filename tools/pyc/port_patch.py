#!/usr/bin/env python3
"""port_patch.py — перенести наши патчи на новую базу декомпилята.

Зачем: при смене декомпилятора текст базы меняется целиком (скобки, переносы,
стиль), и хунки `release/patches/*.patch` перестают находить контекст. Скрипт
перекладывает правки СЕМАНТИЧЕСКИ на новую базу:

  1. старая база (`decompiled_fixed/<m>.py`) выравнивается с новой по
     нормализованным строкам (пробелы и скобки снимаются, переносы внутри
     скобок схлопываются) — получается отображение «строка старой → строка новой»;
  2. каждый хунк старого патча переводится в координаты новой базы и
     применяется там: привязка по контекстным строкам хунка;
  3. результат компилируется `vendor/py223`, и «ядро» опкодов сравнивается с
     установленной сборкой `patches/<m>.pyc` — это и есть гейт верности.

Дальше (канонический порядок, `docs/PATCHES.md`):

    python3 tools/release/update_entry.py <module>     # .patch + запись манифеста
    python3 tools/apply_patch.py <module>              # гейт .py/.pyc против манифеста
    python3 tools/pyc/install_patch.py <module> --install

Команды (из корня репозитория):
  python3 tools/pyc/port_patch.py --all --base build/decompyle_base/py --dry
  python3 tools/pyc/port_patch.py --all --base build/decompyle_base/py
  python3 tools/pyc/port_patch.py ui/olmenu --base build/decompyle_base/py --dry
"""
import argparse
import difflib
import json
import os
import re
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
import pyc_bytes as PB  # noqa: E402
from decompyle_base import canon  # noqa: E402
from pyc_text import unwrap_literals as unwrap  # noqa: E402

MANIFEST = os.path.join(_ROOT, 'release', 'manifest.json')
BACKUP_PATCHES = os.path.join(_ROOT, 'release', 'patches')
PY22 = paths.win_path(os.path.join(paths.py223_dir(), 'python.exe'))
WORKER = paths.win_path(os.path.join(_HERE, 'py22_worker.py'))


def norm(line):
    return re.sub(r'[\s()]', '', line)


def line_map(old_text, new_text):
    old = [norm(l) for l in old_text.split('\n')]
    new = [norm(l) for l in new_text.split('\n')]
    sm = difflib.SequenceMatcher(None, old, new, autojunk=False)
    m = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                m[i1 + k] = j1 + k
    return m


def parse_hunks(patch_text):
    """[(old_start, old_lines, new_lines, is_context)] — old_start 0-based."""
    hunks, cur = [], None
    for line in patch_text.split('\n'):
        if line.startswith('@@'):
            m = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', line)
            cur = {'start': int(m.group(1)) - 1, 'old': [], 'new': [], 'ctx': []}
            hunks.append(cur)
            continue
        if cur is None or line[:1] not in (' ', '-', '+'):
            continue
        if line[:1] in (' ', '-'):
            cur['old'].append(line[1:])
            cur['ctx'].append(line[:1] == ' ')
        if line[:1] in (' ', '+'):
            cur['new'].append(line[1:])
    return [(h['start'], h['old'], h['new'], h['ctx']) for h in hunks]


def port(old_base, patch_text, new_base):
    m = line_map(old_base, new_base)
    lines = new_base.split('\n')
    edits = []
    for old_start, old_lines, new_lines, ctx in parse_hunks(patch_text):
        idxs = [m.get(old_start + k) for k in range(len(old_lines))]
        anchor = [i for i, c in zip(idxs, ctx) if c and i is not None]
        mapped = anchor or [i for i in idxs if i is not None]
        if not mapped:
            raise ValueError('хунк без привязки: строка %d' % (old_start + 1))
        edits.append((min(mapped), max(mapped) + 1, len(edits), new_lines))
    for nstart, nend, _i, new_lines in sorted(edits, key=lambda e: (e[0], e[2]), reverse=True):
        lines[nstart:nend] = new_lines
    return '\n'.join(lines)


def trim_dead_tails(seq):
    """Хвостовые мёртвые пары `LOAD_CONST None; RETURN_VALUE` (следы дублей `return`)."""
    out = list(seq)
    while len(out) >= 4 and out[-1] == ('RETURN_VALUE',) and out[-2] == ('CONST', 'None') \
            and out[-3] == ('RETURN_VALUE',) and out[-4] == ('CONST', 'None'):
        del out[-2:]
    return out


def parity_vs(ref_pyc, mine_pyc):
    A, B = PB.tree(ref_pyc), PB.tree(mine_pyc)
    if len(A) != len(B):
        return 999, ['объектов %d vs %d' % (len(A), len(B))]
    nops = 0
    det = []
    for a, b in zip(A, B):
        ca, cb = trim_dead_tails(canon(a)), trim_dead_tails(canon(b))
        for k in range(min(len(ca), len(cb))):
            if ca[k] != cb[k]:
                nops += 1
                if len(det) < 3:
                    det.append('%s %s/%s' % (a['name'].decode('latin-1'), ca[k], cb[k]))
        nops += abs(len(ca) - len(cb))
    return nops, det


def main(argv):
    ap = argparse.ArgumentParser(description='перенос патчей на новую базу')
    ap.add_argument('modules', nargs='*')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--base', required=True, help='каталог новой базы (.py)')
    ap.add_argument('--old-base', default=os.path.join(_ROOT, 'decompiled_fixed'))
    ap.add_argument('--patch-dir', default=BACKUP_PATCHES, help='каталог .patch (по умолчанию release/patches)')
    ap.add_argument('--dry', action='store_true')
    args = ap.parse_args(argv)

    man = json.load(open(MANIFEST, encoding='utf-8'))
    entries = [e for e in man['code_tier']['entries'] if 'base' in e]
    if not args.all and args.modules:
        entries = [e for e in entries if e['module'] in args.modules]
    if not entries:
        print('нечего делать: укажите модули или --all')
        return 2

    env = dict(os.environ, PYTHONPATH=paths.py223_dir() + ';' + paths.lib_dir(),
               PYTHONDONTWRITEBYTECODE='1')
    out_dir = os.path.join(_ROOT, 'build', 'port')
    os.makedirs(out_dir, exist_ok=True)

    rc = 0
    for e in entries:
        mod = e['module']
        old = os.path.join(args.old_base, mod + '.py')
        new = os.path.join(args.base, mod + '.py')
        patch = os.path.join(args.patch_dir, os.path.basename(e['patch_file']))
        pub_pyc = os.path.join(_ROOT, e['repo_pyc'])
        py = os.path.join(out_dir, mod.replace('/', '__') + '.py')
        missing = [n for n, p in (('база', old), ('новая база', new), ('патч', patch), ('.pyc', pub_pyc))
                   if not os.path.isfile(p)]
        if missing:
            print('%-28s SKIP (нет: %s)' % (mod, ', '.join(missing)))
            continue
        try:
            text = port(open(old, encoding='latin-1').read(),
                        open(patch, encoding='utf-8').read(),
                        unwrap(open(new, encoding='latin-1').read()))
        except Exception as exc:  # noqa: BLE001 — хунк без привязки
            print('%-28s PORT-FAIL %s' % (mod, exc))
            rc = 1
            continue
        with open(py, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(text)
        pyc = py[:-3] + '.pyc'
        proc = subprocess.run([PY22, WORKER, 'build', paths.win_path(py), paths.win_path(pyc)],
                              env=env, cwd=_ROOT, capture_output=True)
        out = proc.stdout.decode('utf-8', 'replace')
        if 'BUILD-OK' not in out:
            print('%-28s BUILD-FAIL %s' % (mod, out.strip()[-80:]))
            rc = 1
            continue
        nops, det = parity_vs(pub_pyc, pyc)
        status = 'ok' if nops == 0 else 'PARITY(%d)' % nops
        print('%-28s %-12s %s' % (mod, status, det[0] if det else ''))
        if nops:
            rc = 1
            continue
        if not args.dry:
            with open(os.path.join(_ROOT, e['repo_py']), 'w',
                      encoding='utf-8', newline='\n') as fh:
                fh.write(text)
    if args.dry:
        print('--dry: исходники репозитория не изменены')
    else:
        print('дальше: python3 tools/release/update_entry.py <module> && '
              'python3 tools/apply_patch.py <module> && '
              'python3 tools/pyc/install_patch.py <module> --install')
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
