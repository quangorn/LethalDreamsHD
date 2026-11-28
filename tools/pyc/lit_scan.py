#!/usr/bin/env python3
"""lit_scan.py — скан декомпилята на порчу литералов: индекс `co_consts` вместо значения.

Паритетный гейт (`pycbuild.py`, tolerant) такую порчу не замечает: индекс int совпадает
с int-константой того же кодового объекта (`sig=0`). Здесь компилят нормализованного
`decompiled_fixed/<module>.py` сверяется с оригинальным `.pyc` воркером `vdiff`, который
разрешает аргумент `LOAD_CONST` к значению, и сравниваются канонизованные токены
(`vdiff_canon.real_diffs`). Кодовые объекты выравниваются по именам (`co_name`), так что
потерянные декомпилатором функции не сдвигают сравнение.

Запуск из корня репозитория:
  python3 tools/pyc/lit_scan.py <orig_scripts_dir>                все модули decompiled_fixed/
  python3 tools/pyc/lit_scan.py <orig_scripts_dir> ui/battlemenu один модуль

Переменная окружения `LD_LIB_DIR` задаёт кэш `Lib` (по умолчанию `paths.lib_dir()`).
Пишет только `build/<module>.py`; установка игры не читается и не изменяется.
"""
import sys, os, difflib

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

LIB = os.environ.get('LD_LIB_DIR')
if LIB:
    paths.lib_dir = lambda: LIB
import pycbuild
from vdiff_mod import parse_vdiff, show_tok
from vdiff_canon import canon_list, real_diffs

SNAP = pycbuild.SNAP
BUILD = pycbuild.BUILD


def align_names(a_names, b_names):
    """Выравнивание списков co_name -> pairs (iA,iB), missing (A), extra (B)."""
    sm = difflib.SequenceMatcher(None, a_names, b_names, autojunk=False)
    pairs, missing, extra = [], [], []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            pairs += [(i1 + k, j1 + k) for k in range(i2 - i1)]
        elif tag == 'delete':
            missing += a_names[i1:i2]
        elif tag == 'insert':
            extra += b_names[j1:j2]
        else:
            missing += a_names[i1:i2]
            extra += b_names[j1:j2]
            for k in range(min(i2 - i1, j2 - j1)):
                pairs.append((i1 + k, j1 + k))
    return pairs, missing, extra


FAST_OPS = ('LOAD_FAST', 'STORE_FAST', 'DELETE_FAST')


def strip_fast(toks):
    """Аргумент FAST-опкодов воркер разрешает по co_names, а локалы лежат в
    co_varnames -> имя печатается чужое. Оставляем только опкод."""
    out = []
    for t in toks:
        head = t.split(None, 1)[0]
        if head in FAST_OPS:
            out.append(head)
        elif head == 'LOAD_LOCAL' and t.endswith('MAKE_FUNCTION'):
            out.append('MAKE_FUNCTION')
        else:
            out.append(t)
    return out


MODTOK = ('LOAD_GLOBAL __name__', 'STORE_NAME __module__', 'LOAD_CONST <code>',
          'LOAD_NAME __name__', 'STORE_NAME __qualname__')


def diff_pairs(ta_raw, tb_raw, ca, cb):
    """Канонизованные токены (vdiff_canon.canon_list), FAST-аргументы сняты,
    cosmetic-допуски как в real_diffs. -> [(tag, xa, xb)]."""
    import difflib
    ta = strip_fast(canon_list(ta_raw, ca))
    tb = strip_fast(canon_list(tb_raw, cb))
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    res = []
    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        xa, xb = ta[a1:a2], tb[b1:b2]
        if all(t in MODTOK for t in xa + xb):
            continue
        if (len(xa) == 1 and xa[0].startswith('CONST -') and len(xb) == 2
                and xb[1].startswith('UNARY_NEGATIVE')
                and xb[0] == 'CONST ' + xa[0][len('CONST -'):]):
            continue
        if len(xa) == len(xb):
            rel = lambda t: int(t.rsplit('REL', 1)[1]) if 'REL' in t else None
            deltas = set()
            for u, v in zip(xa, xb):
                ru, rv = rel(u), rel(v)
                if ru is None or rv is None:
                    deltas = None
                    break
                deltas.add(rv - ru)
            if deltas and len(deltas) == 1:
                continue
        res.append((tag, xa, xb))
    return res


def is_lit_corruption(ta, tb):
    """Пара канонизованных токенов: A грузит не-int значение, B грузит int ->
    индекс co_consts вместо литерала."""
    if not ta.startswith('CONST ') or not tb.startswith('CONST '):
        return False
    va, vb = ta[len('CONST '):], tb[len('CONST '):]
    try:
        int(vb, 0)
    except ValueError:
        return False
    try:
        int(va, 0)
        return False
    except ValueError:
        return True


MOD_NOISE = ('__name__', '__module__', '__qualname__')


def strip_mod_noise(ops):
    """Снимает косметические токены `LOAD_GLOBAL __name__` + `STORE_NAME __module__`
    (альфа 2.2a4 эмитит их в теле класса, официальная 2.2.3 — нет). Без снятия они
    сдвигают позиционное сравнение на 2 токена и порождают ложные расхождения."""
    out = []
    for op, v in ops:
        if op in ('LOAD_GLOBAL', 'STORE_NAME') and v in MOD_NOISE:
            continue
        out.append((op, v))
    return out


def positional_diffs(ta, tb):
    """Позиционное сравнение нормализованных операций (pyc_norm.norm_ops).
    Аргументы FAST-опкодов (lokals из co_varnames) не значимы; прыжковые дельта
    зависят от длины кода -> сравниваем только константы и имена.
    -> [(index, op, orig_value, mine_value)], lengths_report."""
    ta = strip_mod_noise(ta)
    tb = strip_mod_noise(tb)
    NOISE_OPS = ('LOAD_FAST', 'STORE_FAST', 'DELETE_FAST')
    JUMP_OPS = ('JUMP_ABSOLUTE', 'JUMP_FORWARD', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE',
                'FOR_ITER', 'SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY',
                'CONTINUE_LOOP', 'BREAK_LOOP')
    diffs = []
    for j in range(min(len(ta), len(tb))):
        op, va = ta[j]
        op2, vb = tb[j]
        if op != op2:
            diffs.append((j, 'op', op, op2))
            continue
        if op in NOISE_OPS or op in JUMP_OPS:
            continue
        if va != vb:
            diffs.append((j, op, va, vb))
    return diffs, (len(ta), len(tb))


def scan_module(module, orig_dir, verbose=False):
    """Сверка компилята decompiled_fixed/<module>.py с оригинальным .pyc.
    -> {'status', 'const_pairs', 'lit_corrupt', 'other_diffs', 'missing', 'extra', 'samples'}."""
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    pyc = os.path.join(orig_dir, module.replace('/', os.sep) + '.pyc')
    if not os.path.exists(src):
        return {'status': 'no decompilate'}
    if not os.path.exists(pyc):
        return {'status': 'no orig pyc'}
    py = pycbuild.build_py(module)
    outa, rc1 = pycbuild.run_worker('dis-pyc', pyc.replace(os.sep, '/'))
    outb, rc2 = pycbuild.run_worker('dis-py', py.replace(os.sep, '/'))
    if 'SYNTAX' in outb:
        return {'status': 'SYNTAX', 'syntax': outb.split('\n')[0]}
    if 'MAGIC-MISMATCH' in outa:
        return {'status': 'magic'}
    a = pycbuild.parse_blocks(outa)
    b = pycbuild.parse_blocks(outb)
    pairs, missing, extra = align_names([c[0] for c in a], [c[0] for c in b])
    lit = []
    other = []
    for (i, j) in pairs:
        na, sig_a, ops_a = a[i]
        nb, sig_b, ops_b = b[j]
        if not ops_a or not ops_b:
            continue
        pd_list, _lens = positional_diffs(ops_a, ops_b)
        for k, op, va, vb in pd_list:
            if op not in ('LOAD_CONST', 'LOAD_NAME', 'LOAD_GLOBAL'):
                continue
            # int-индекс co_consts вместо литерала: A не int, B int
            def isint(x):
                try:
                    int(x, 0)
                    return True
                except (ValueError, TypeError):
                    return False
            if not isint(va) and isint(vb):
                lit.append((na, k, va, vb))
            elif va != vb:
                other.append((na, k, op, va, vb))
    res = {'status': 'ok', 'lit': len(lit), 'other': len(other),
           'missing': missing, 'extra': extra,
           'samples': lit[:6] + other[:4]}
    if verbose and (lit or other):
        for n, k, va, vb in lit[:12]:
            print('   LIT %s[%s]: orig=%s mine=%s' % (n, k, str(va)[:70], str(vb)[:20]))
        for n, k, op, va, vb in other[:12]:
            print('   DIFF %s[%s] %s: orig=%s mine=%s' % (n, k, op, str(va)[:50], str(vb)[:50]))
    return res


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    orig_dir = sys.argv[1].replace('\\', '/')
    if len(sys.argv) > 2:
        mods = [sys.argv[2]]
    else:
        # список декомпилятов = файлы decompiled_fixed/ (путь модуля через '/')
        mods = []
        for root, dirs, files in os.walk(SNAP):
            for f in files:
                if f.endswith('.py'):
                    rel = os.path.relpath(os.path.join(root, f), SNAP).replace(os.sep, '/')[:-3]
                    mods.append(rel)
        mods = sorted(mods)
    rows = []
    for m in mods:
        r = scan_module(m, orig_dir, verbose=(len(sys.argv) > 2))
        rows.append((m, r))
    bad = [(m, r) for m, r in rows if r['status'] != 'ok']
    print('просканировано %d модулей; статусов «не ok» %d' % (len(rows), len(bad)))
    for m, r in bad:
        print('  BAD %s %s' % (m, r.get('status')), r.get('syntax', ''))
    worst = sorted(((m, r) for m, r in rows if r['status'] == 'ok'
                    and (r['lit'] or r['other'] or r['missing'])),
                   key=lambda x: (-x[1]['lit'], -x[1]['other']))
    print('модулей с расхождениями: %d' % len(worst))
    for m, r in worst:
        print('  %s: литералов-индексов=%d проч.расхождений=%d потерянных=%d' %
              (m, r['lit'], r['other'], len(r['missing'])))


main()
