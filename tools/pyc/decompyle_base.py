#!/usr/bin/env python3
"""decompyle_base.py — база `decompiled_fixed/` из родных `.pyc` декомпилятором
decompyle 2.3.2 (vendor/decompyle, Python 1.5–2.3, ровно наша эпоха).

Почему не uncompyle6: его 2.2-грамматика печатает ИНДЕКС `co_consts` вместо
значения и склеивает общие хвосты ветвей (`docs/DECOMP_DEFECTS.md`, классы 1–3).
decompyle 2.3.2 этих дефектов не имеет; сверка ядра опкодов после пересборки:
роздано 428/433 модулей побитово, остаток — ±1–3 позиции цели условного прыжка.

Вход — РОДНЫЕ скрипты из `original.7z` (не установка: там уже наши патчи):

    python3 tools/extract_pristine.py extract "data/scripts/*"
    python3 tools/pyc/fetch_decompyle.py                 # vendor/decompyle
    python3 tools/pyc/decompyle_base.py --scan           # отчёт без записи
    python3 tools/pyc/decompyle_base.py --write          # записать decompiled_fixed/

Шаги: decompyle (py2-воркер под `vendor/py223`) → компиляция 2.2.3 → гейт
«ядро опкодов равно родному .pyc». Модули, которые decompyle не берёт (огромное
плоское тело: `dictionary_rus`), восстанавливает fallback `tools/pyc/litmod.py`
(та же стековая машина, тот же гейт). Модуль, не прошедший гейт, печатается как
PARITY и НЕ записывается при `--write` без `--force`.
"""
import argparse
import os
import shutil
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
import pyc_bytes as PB  # noqa: E402
import pyc_text  # noqa: E402
import litmod  # noqa: E402

VENDOR = os.path.join(_ROOT, 'vendor', 'decompyle')
WORKER = paths.win_path(os.path.join(_HERE, 'decompyle_worker.py'))
PY22 = paths.win_path(os.path.join(paths.py223_dir(), 'python.exe'))
DEFAULT_OUT = os.path.join(_ROOT, 'build', 'decompyle_base')

NAMEOPS = ('LOAD_NAME', 'STORE_NAME', 'DELETE_NAME', 'LOAD_GLOBAL', 'STORE_GLOBAL',
           'DELETE_GLOBAL', 'LOAD_ATTR', 'STORE_ATTR', 'DELETE_ATTR', 'IMPORT_NAME',
           'IMPORT_FROM', 'LOAD_DEREF', 'STORE_DEREF')


def env_worker():
    return dict(os.environ,
                PYTHONPATH=paths.py223_dir() + ';' + paths.lib_dir() + ';' + paths.win_path(VENDOR),
                PYTHONDONTWRITEBYTECODE='1')


def run_worker(mode, listfile, jobs=1):
    """Воркер(ы) 2.2.3: при jobs>1 список режется на куски и идёт параллельно."""
    if jobs <= 1:
        proc = subprocess.run([PY22, WORKER, mode, paths.win_path(listfile)],
                              capture_output=True, text=False, env=env_worker(), cwd=_ROOT)
        out = proc.stdout.decode('utf-8', 'replace')
        err = proc.stderr.decode('utf-8', 'replace')
        if proc.returncode != 0:
            print(out[-2000:])
            print(err[-2000:])
            raise SystemExit('воркер %s упал (rc %d)' % (mode, proc.returncode))
        return [l for l in out.split('\n') if l.strip()]

    with open(listfile) as fh:
        lines = [l for l in fh.read().split('\n') if l.strip()]
    chunks = [lines[i::jobs] for i in range(jobs)]
    procs = []
    for i, chunk in enumerate(chunks):
        part = '%s.%d' % (listfile, i)
        with open(part, 'w') as fh:
            fh.write('\n'.join(chunk) + '\n')
        procs.append(subprocess.Popen([PY22, WORKER, mode, paths.win_path(part)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      env=env_worker(), cwd=_ROOT))
    out_lines = []
    for i, proc in enumerate(procs):
        out, err = proc.communicate()
        if proc.returncode != 0:
            print(out.decode('utf-8', 'replace')[-1500:])
            print(err.decode('utf-8', 'replace')[-1500:])
            raise SystemExit('воркер %s (кусок %d) упал (rc %d)'
                             % (mode, i, proc.returncode))
        out_lines += [l for l in out.decode('utf-8', 'replace').split('\n') if l.strip()]
    return out_lines


def resolve(c, op, arg):
    """opcode+arg -> (имя, значение) в терминах значений, не индексов."""
    if isinstance(op, str):
        return (op, arg)
    name = PB.OPS.get(op)
    if arg is None:
        return (name,)
    if name == 'LOAD_CONST':
        v = c['consts'][arg] if arg < len(c['consts']) else None
        if isinstance(v, dict) and 'code' in v:
            return ('CONST', 'code:' + repr(v['name']))
        return ('CONST', repr(v))
    if name in NAMEOPS:
        v = c['names'][arg] if arg < len(c['names']) else None
        return (name.replace('_GLOBAL', '_NAME'), v)
    if name.endswith('_FAST'):
        v = c['varnames'][arg] if arg < len(c['varnames']) else None
        return ('FAST', v)
    return (name, arg)


def canon(c):
    seq = [resolve(c, o, a) for o, a in PB.core(c)]
    out = []
    for x in seq:
        # 2.2: None — глобальное имя; в source это литерал
        if x == ('LOAD_NAME', b'None'):
            x = ('CONST', 'None')
        # альфа-свёртка отрицательных литералов: CONST 0.956 + UNARY_NEGATIVE
        if x == ('UNARY_NEGATIVE',) and out and out[-1][0] == 'CONST':
            try:
                out[-1] = ('CONST', repr(-float(out[-1][1])))
                continue
            except (TypeError, ValueError):
                pass
        out.append(x)
    return out


def parity(orig_pyc, mine_pyc):
    """(число объектов с расхождениями, число расхождений опкодов, первые детали)."""
    A = PB.tree(orig_pyc)
    B = PB.tree(mine_pyc)
    if len(A) != len(B):
        return (999, 0, ['объектов %d vs %d' % (len(A), len(B))])
    nobj = nops = 0
    detail = []
    for a, b in zip(A, B):
        ca, cb = canon(a), canon(b)
        n = 0
        for k in range(min(len(ca), len(cb))):
            if ca[k] != cb[k]:
                n += 1
                if len(detail) < 3:
                    detail.append('%s: A=%s B=%s' % (a['name'].decode('latin1'), ca[k], cb[k]))
        if len(ca) != len(cb):
            n += abs(len(ca) - len(cb))
            if len(detail) < 3:
                detail.append('%s: длина %d vs %d' % (a['name'].decode('latin1'), len(ca), len(cb)))
        if n:
            nobj += 1
            nops += n
    return (nobj, nops, detail)


def modules_of(src_scripts):
    out = []
    for dp, _dn, fn in os.walk(src_scripts):
        for f in fn:
            if f.endswith('.pyc'):
                rel = os.path.relpath(os.path.join(dp, f), src_scripts)[:-4].replace(os.sep, '/')
                out.append(rel)
    return sorted(out)


def pri_list(path, pairs):
    """Строки `src<TAB>dst` в виндовой форме: воркер — Windows-python 2.2.3."""
    with open(path, 'w') as fh:
        for src, dst in pairs:
            fh.write('%s\t%s\n' % (paths.win_path(src), paths.win_path(dst)))


def main(argv):
    ap = argparse.ArgumentParser(description='декомпилятор базы: decompyle 2.3.2')
    ap.add_argument('--src', default=os.path.join(paths.cache_dir(), 'pristine', 'data', 'scripts'),
                    help='каталог РОДНЫХ .pyc (data/scripts из original.7z)')
    ap.add_argument('--out', default=DEFAULT_OUT, help='каталог сборки (_set/py, _set/pyc)')
    ap.add_argument('--only', help='подстрока пути модуля (один модуль/подкаталог)')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--scan', action='store_true', help='только отчёт (по умолчанию)')
    g.add_argument('--write', action='store_true', help='записать базу в decompiled_fixed/')
    ap.add_argument('--resume', action='store_true',
                    help='не чистить каталог сборки: дописать недостающие модули (после обрыва)')
    ap.add_argument('--jobs', type=int, default=1,
                    help='сколько воркеров python 2.2.3 запускать параллельно')
    ap.add_argument('--normalize-only', action='store_true',
                    help='декомпиляцию пропустить: только нормализация + компиляция + гейт')
    ap.add_argument('--force', action='store_true', help='писать и модули с расхождениями PARITY')
    args = ap.parse_args(argv)

    if not os.path.isdir(args.src):
        raise SystemExit('нет каталога родных скриптов %s\n'
                         'сначала: python3 tools/extract_pristine.py extract "data/scripts/*"'
                         % args.src)
    if not os.path.isfile(os.path.join(VENDOR, 'decompyle', '__init__.py')):
        raise SystemExit('нет vendor/decompyle -> python3 tools/pyc/fetch_decompyle.py')

    mods = modules_of(args.src)
    if args.only:
        mods = [m for m in mods if args.only in m]
    if not mods:
        raise SystemExit('модулей не найдено в %s' % args.src)
    print('модулей: %d (%s)' % (len(mods), paths.norm(args.src)))

    py_dir = os.path.join(args.out, 'py')
    pyc_dir = os.path.join(args.out, 'pyc')
    for d in (py_dir, pyc_dir):
        if os.path.isdir(d) and not (args.resume or args.normalize_only):
            shutil.rmtree(d)
        os.makedirs(d, exist_ok=True)
    if args.normalize_only:
        have = [m for m in mods if os.path.isfile(os.path.join(py_dir, m + '.py'))]
        print('--normalize-only: декомпиляция пропущена, .py на диске: %d' % len(have))
        mods = have
        if not mods:
            raise SystemExit('каталог %s пуст: нужен полный прогон' % py_dir)
    elif args.resume:
        todo = [m for m in mods if not os.path.isfile(os.path.join(py_dir, m + '.py'))]
        print('--resume: уже готово %d, к обработке %d' % (len(mods) - len(todo), len(todo)))
        mods = todo or mods

    pairs = []
    win2rel = {}
    for rel in mods:
        src = os.path.join(args.src, rel + '.pyc')
        pairs.append((src, os.path.join(py_dir, rel + '.py')))
        win2rel[paths.win_path(src)] = rel
    lst = os.path.join(args.out, 'decompyle.txt')
    pri_list(lst, pairs)

    lines, failed = [], {}
    if not args.normalize_only:
        print('decompyle 2.3.2 ...')
        lines = run_worker('batch', lst, args.jobs)
    for ln in lines:
        if ln.startswith('FAIL '):
            src, _, msg = ln[5:].partition(' :: ')
            failed[win2rel.get(src, src)] = msg
    print(lines[-1] if lines else '')

    # fallback: плоские литеральные модули (decompyle их не берёт по размеру)
    restored = []
    for rel in list(failed):
        code = PB.tree(os.path.join(args.src, rel + '.pyc'))[0]
        try:
            text = litmod.emit_module(code)
        except litmod.LitModError as exc:
            print('FALLBACK-FAIL %s :: %s' % (rel, exc))
            continue
        dst = os.path.join(py_dir, rel + '.py')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='latin-1', newline='\n') as fh:
            fh.write(text)
        restored.append(rel)
        print('FALLBACK %s (decompyle: %s)' % (rel, failed.pop(rel)))
    if restored:
        print('litmod восстановил: %s' % ', '.join(restored))

    merged = 0
    for dp, _dn, fn in os.walk(py_dir):
        for fname in fn:
            if not fname.endswith('.py'):
                continue
            f = os.path.join(dp, fname)
            text = open(f, encoding='latin-1').read()
            new = pyc_text.normalize(text)
            if new != text:
                with open(f, 'w', encoding='latin-1', newline='\n') as fh:
                    fh.write(new)
                merged += 1
    print('import-строки склеены в %d модулях' % merged)

    print('компиляция 2.2.3 ...')
    pairs = []
    win2rel = {}
    for rel in mods:
        src = os.path.join(py_dir, rel + '.py')
        if not os.path.isfile(src):
            continue
        pairs.append((src, os.path.join(pyc_dir, rel + '.pyc')))
        win2rel[paths.win_path(src)] = rel
    lst = os.path.join(args.out, 'compile.txt')
    pri_list(lst, pairs)
    clines = run_worker('compile', lst, args.jobs)
    syntax = {}
    for ln in clines:
        if ln.startswith('FAIL '):
            src, _, msg = ln[5:].partition(' :: ')
            syntax[win2rel.get(src, src)] = msg
    print(clines[-1] if clines else '')

    print('гейт: ядро опкодов против родного .pyc')
    clean, parity_bad, missing = [], [], []
    for rel in mods:
        mine = os.path.join(pyc_dir, rel + '.pyc')
        if not os.path.isfile(mine):
            missing.append((rel, syntax.get(rel, 'нет .pyc')))
            continue
        nobj, nops, detail = parity(os.path.join(args.src, rel + '.pyc'), mine)
        if nobj == 0:
            clean.append(rel)
        else:
            parity_bad.append((rel, nobj, nops, detail))

    print('  идентично: %d из %d' % (len(clean), len(mods)))
    if parity_bad:
        print('  PARITY (%d):' % len(parity_bad))
        for rel, nobj, nops, detail in parity_bad:
            print('    %-34s объектов %-3d опкодов %-5d %s' % (rel, nobj, nops,
                                                             detail[0] if detail else ''))
    if missing:
        print('  НЕТ ИСХОДНИКА (%d):' % len(missing))
        for rel, why in missing:
            print('    %-34s %s' % (rel, why))

    if not args.write:
        print('\n--scan: база не тронута. Записать: --write (в %s)'
              % os.path.relpath(os.path.join(_ROOT, 'decompiled_fixed'), _ROOT))
        return 0

    skip = {rel for rel, *_ in parity_bad} | {rel for rel, _ in missing}
    if skip and not args.force:
        print('\nНЕ записаны (нужен --force или разбор): %s' % ', '.join(sorted(skip)))
    written = 0
    base = os.path.join(_ROOT, 'decompiled_fixed')
    for rel in mods:
        if rel in skip and not args.force:
            continue
        src = os.path.join(py_dir, rel + '.py')
        if not os.path.isfile(src):
            continue
        dst = os.path.join(base, rel + '.py')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        written += 1
    print('записано в decompiled_fixed/: %d модулей' % written)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
