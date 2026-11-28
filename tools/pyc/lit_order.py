#!/usr/bin/env python3
"""lit_order.py — позиционное восстановление литералов декомпилята.

Гейт `decompile_lit.py` построен на мультимножествах констант кодового объекта,
поэтому перестановку значений внутри одного объекта он не видит: мультимножества
совпадают, `gate=0`, замен 0. Пример `ui/battlemenu`: родной `ShowMainmenu(1,
[0, 0, 1, 1, 1, 3])`, в базе декомпилята `[1, 1, 1, 0, 0, 3]` — мультимножество
одинаковое, гейт слепой.

Здесь кодовые объекты выравниваются по ИМЕНАМ, внутри пары — по СКЕЛЕТУ
не-константных опкодов (прыжковые аргументы и аргументы FAST-опкодов не значимы),
а константы между якорями спариваются ПО ПОРЯДКУ. Пары (B=K, A=V), K != V,
передаются в `decompile_lit.apply_pass` (механика построчной замены целого),
цикл до нулевых позиционных расхождений; результат — `decompiled_fixed/<module>.py`.

Запуск из корня репозитория:
  python3 tools/pyc/lit_order.py ui/battlemenu
  python3 tools/pyc/lit_order.py ui/battlemenu --check      только число расхождений
  python3 tools/pyc/lit_order.py --all                       все модули decompiled_fixed/

Переменная `LD_LIB_DIR` задаёт кэш `Lib` (по умолчанию `paths.lib_dir()`).
"""
import sys, os, re, collections

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
import decompile_lit as dl
from decompile_lit import decode_hex, align_names, parse_litfix2

SNAP = pycbuild.SNAP
BACKUP = pycbuild.BACKUP
FIXED = os.path.join(pycbuild.REPO_ROOT, 'decompiled_fixed')

FAST_OPS = ('LOAD_FAST', 'STORE_FAST', 'DELETE_FAST')
JUMP_OPS = ('JUMP_ABSOLUTE', 'JUMP_FORWARD', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE',
            'FOR_ITER', 'SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY',
            'CONTINUE_LOOP', 'BREAK_LOOP')


def is_const(t):
    return t.startswith('LOAD_CONST')


def skel(t):
    """Токен -> элемент скелета: константы = 'C', прыжки и FAST-аргументы сняты."""
    if is_const(t):
        return 'C'
    parts = t.split(None, 1)
    op = parts[0]
    if op in JUMP_OPS:
        return op
    if op in FAST_OPS:
        return op
    return t


def val_of(t):
    if t.startswith('LOAD_CONST HEX '):
        return decode_hex(t[len('LOAD_CONST HEX '):])
    if t == 'LOAD_CONST <code>':
        return '<code>'
    return None


def skeleton_align(sides, i, j):
    """Выравнивание ПО ТОКУ НЕ-КОНСТАНТНЫХ якорей -> dict ai -> bj.
    Константные токены в выравнивании не участвуют: их число между якорями может
    отличаться, и SequenceMatcher по скелету со «C»-элементами сдвигает якорь."""
    import difflib
    ta = sides['A'][i]['toks']
    tb = sides['B'][j]['toks']
    ia = [k for k, t in enumerate(ta) if not is_const(t)]
    ib = [k for k, t in enumerate(tb) if not is_const(t)]
    sa = [skel(ta[k]) for k in ia]
    sb = [skel(tb[k]) for k in ib]
    sm = difflib.SequenceMatcher(None, sa, sb, autojunk=False)
    aligned = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            aligned += [(ia[i1 + k], ib[j1 + k]) for k in range(i2 - i1)]
    return dict(aligned), aligned


def positional_pairs(sides, i, j):
    """Выравнивание по скелету -> пары индексов (ai, bj), только LOAD_CONST-пары.
    Между якорями константы спариваются по порядку."""
    _, aligned = skeleton_align(sides, i, j)
    ta = sides['A'][i]['toks']
    tb = sides['B'][j]['toks']
    # константные сегменты между соседними якорями
    anchors = [p for p in aligned if not is_const(ta[p[0]])]
    out = []
    prev_a, prev_b = -1, -1
    for (ai, bj) in anchors:
        ca = [p for p in range(prev_a + 1, ai) if is_const(ta[p])]
        cb = [p for p in range(prev_b + 1, bj) if is_const(tb[p])]
        for k in range(min(len(ca), len(cb))):
            out.append((ca[k], cb[k]))
        prev_a, prev_b = ai, bj
    ca = [p for p in range(prev_a + 1, len(ta)) if is_const(ta[p])]
    cb = [p for p in range(prev_b + 1, len(tb)) if is_const(tb[p])]
    for k in range(min(len(ca), len(cb))):
        out.append((ca[k], cb[k]))
    return out


def is_peephole(va, vb):
    """Альфа 2.2a4 сворачивает `-x` в константу, официальная 2.2.3 эмитит `x` +
    `UNARY_NEGATIVE` -> позиция совпадает по скелету, значение знаковое.
    Такие пары не порча."""
    if va == vb:
        return True
    if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
        return va == -vb
    return False


def diff_count(sides, npairs):
    """Число позиционных LOAD_CONST-пар с разными значениями (снята альфа-свёртка)."""
    n = 0
    for (i, j) in npairs:
        for (ai, bj) in positional_pairs(sides, i, j):
            va = val_of(sides['A'][i]['toks'][ai])
            vb = val_of(sides['B'][j]['toks'][bj])
            if not is_peephole(va, vb):
                n += 1
    return n


def measure(text, module, dst, pyc):
    """Записать текст, получить стороны воркера litfix2."""
    open(dst, 'w', encoding='utf-8', newline='\n').write(text.replace('\r\n', '\n'))
    out, rc = dl.run_worker('litfix2', pyc, dst.replace(os.sep, '/'))
    if 'SYNTAX' in out:
        return None
    sides = parse_litfix2(out)
    if not sides['B']:
        return None
    npairs, missing, extra = align_names([c['name'] for c in sides['A']],
                                          [c['name'] for c in sides['B']])
    return {'sides': sides, 'npairs': npairs, 'diffs': diff_count(sides, npairs)}


def bracket_spans(line):
    """Все сбалансированные скобочные группы `[..]` и `(..)` строки (включая
    вложенные) -> (start, end) 0-based. Вложенные нужны, потому что список-литерал
    стоит внутри круглых скобок вызова."""
    out = []
    stack = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c in '[(':
            stack.append((c, i))
        elif c in '])':
            if stack and ((stack[-1][0] == '[' and c == ']') or (stack[-1][0] == '(' and c == ')')):
                open_ch, start = stack.pop()
                out.append((start, i + 1))
        i += 1
    out.sort()
    return out


def consumed_spans(line, consumed, li):
    """Заменённые литералы на строке li (1-based) -> (start, end) 0-based."""
    return [(sc, ec) for (sl, sc, el, ec) in consumed if sl == li and el == li]


def parse_pure(out):
    """Строки PURE <codeidx> <tokidx> <hex> -> {codeidx: [(tokidx, value)]}."""
    res = collections.defaultdict(list)
    for line in out.split('\n'):
        p = line.split()
        if len(p) == 4 and p[0] == 'PURE':
            res[int(p[1])].append((int(p[2]), decode_hex(p[3])))
    for k in res:
        res[k].sort()
    return res


def b_purelits(toks):
    """Чистые литеральные списки/кортежи компилята по потоку токенов litfix2."""
    out = []
    stack = []
    for j, t in enumerate(toks):
        op = t.split(None, 1)[0]
        if op == 'LOAD_CONST':
            v = val_of(t)
            if v == '<code>' or v is None:
                stack = []
            else:
                stack.append(v)
        elif op == 'BUILD_LIST':
            n = int(t.split()[1])
            items = stack[-n:] if len(stack) >= n else []
            stack = stack[:-n]
            if n > 0 and len(items) == n and all(isinstance(x, (int, float, str, complex)) for x in items):
                out.append((j, list(items)))
                stack.append(list(items))
            else:
                stack.append(None)
        elif op == 'BUILD_TUPLE':
            n = int(t.split()[1])
            items = stack[-n:] if len(stack) >= n else []
            stack = stack[:-n]
            if n > 0 and len(items) == n and all(isinstance(x, (int, float, str, complex)) for x in items):
                out.append((j, tuple(items)))
                stack.append(tuple(items))
            else:
                stack.append(None)
        else:
            stack = []
    return out


def apply_lit_replacements(text, sides, npairs, pureA):
    """Замена литералов списков/кортежей целиком. Внутри кодового объекта спариваем
    чистые литералы A и B по МУЛЬТИМНОЖЕСТВУ значений (перестановка элементов —
    единственный тип порчи, который виден как «те же значения, другой порядок»);
    литерал в исходнике со значением B заменяется на py2_literal(A).
    Возвращает (text, cnt, log, consumed) — consumed передаётся целочисленному
    проходу как защищённые позиции."""
    import ast
    lines = text.split('\n')
    consumed = []
    cnt = 0
    log = []
    for (i, j) in npairs:
        pa = pureA.get(i, [])
        pb = b_purelits(sides['B'][j]['toks'])
        used_a = set()
        pairs = []
        for (bj, vb) in pb:
            key = tuple(sorted([repr(x) for x in vb]))
            for k in range(len(pa)):
                if k in used_a:
                    continue
                va = pa[k][1]
                if tuple(sorted([repr(x) for x in va])) != key:
                    continue
                used_a.add(k)
                pairs.append((va, vb))
                break
        if not pairs:
            continue
        lns = [ln for ln in sides['B'][j]['line_of'] if ln is not None]
        ln0 = min(lns) if lns else 1
        ln1 = max(lns) if lns else len(lines)
        for (va, vb) in pairs:
            if va == vb:
                continue
            hit = None
            for li in range(max(1, ln0), min(ln1 + 1, len(lines) + 1)):
                for (s, e) in bracket_spans(lines[li - 1]):
                    txt = lines[li - 1][s:e]
                    if any(a <= s < b for (a, b) in consumed_spans(lines[li - 1], consumed, li)):
                        continue
                    try:
                        val = ast.literal_eval(txt)
                    except Exception:
                        continue
                    if not isinstance(val, (list, tuple)):
                        continue
                    if list(val) != list(vb):
                        continue
                    hit = (li, s, li, e)
                    break
                if hit:
                    break
            if not hit:
                continue
            sl, sc, el, ec = hit
            new = pycbuild.py2_literal(va)
            tail = lines[el - 1][ec:]
            lines[sl - 1] = lines[sl - 1][:sc] + new + tail
            del lines[sl:el]
            consumed.append((sl, sc, sl, sc + len(new)))
            cnt += 1
            log.append((sides['A'][i]['name'], vb, va))
    return '\n'.join(lines), cnt, log, consumed


def scan_module(module, work_dir, verbose=False):
    """Тriage без записи в decompiled_fixed: позиционные расхождения компилята
    `decompiled_fixed/<module>.py` с оригинальным `.pyc`. Компилят собирается в
    `work_dir`. -> {'status', 'diffs', 'litpairs'}."""
    src = os.path.join(FIXED, module.replace('/', os.sep) + '.py')
    pyc = os.path.join(BACKUP, module.replace('/', os.sep) + '.pyc').replace(os.sep, '/')
    if not os.path.exists(src):
        return {'status': 'no decompilate'}
    if not os.path.exists(pyc):
        return {'status': 'no orig pyc'}
    dst = os.path.join(work_dir, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    text = dl.normalize(open(src, encoding='utf-8').read())
    m = measure(text, module, dst, pyc)
    if m is None:
        return {'status': 'SYNTAX'}
    lit = []
    for (i, j) in m['npairs']:
        ta = m['sides']['A'][i]['toks']
        tb = m['sides']['B'][j]['toks']
        for (ai, bj) in positional_pairs(m['sides'], i, j):
            va = val_of(ta[ai])
            vb = val_of(tb[bj])
            if not is_peephole(va, vb):
                lit.append((m['sides']['A'][i]['name'], va, vb))
    return {'status': 'ok', 'diffs': m['diffs'], 'litpairs': lit}


def recover(module, max_passes=12, verbose=True):
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    pyc = os.path.join(BACKUP, module.replace('/', os.sep) + '.pyc').replace(os.sep, '/')
    if not os.path.exists(src) or not os.path.exists(pyc):
        print('%s: нет базы или оригинального .pyc' % module)
        return None
    dst = os.path.join(FIXED, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    text = dl.normalize(open(src, encoding='utf-8').read())
    m = measure(text, module, dst, pyc)
    if m is None:
        print('%s: компилят не собирается/пустой вывод воркера' % module)
        return None
    start = m['diffs']
    total = 0
    log = []
    pure_out, rc = dl.run_worker('purelit', pyc)
    pureA = parse_pure(pure_out)
    for _ in range(max_passes):
        if m['diffs'] == 0:
            break
        cand = text
        cnt = 0
        lg = []
        # 1) точные литералы списков/кортежей из оригинала
        cand, c1, l1, consumed = apply_lit_replacements(cand, m['sides'], m['npairs'], pureA)
        cnt += c1
        lg += l1
        step = m
        if c1:
            nm = measure(cand, module, dst, pyc)
            if nm is not None and nm['diffs'] < m['diffs']:
                step = nm
        # 2) позиционные целые (индекс co_consts -> значение) на СВЕЖИХ сторонах
        sides = step['sides']
        tok_pairs = []
        for (i, j) in step['npairs']:
            line_of = sides['B'][j]['line_of']
            for (ai, bj) in positional_pairs(sides, i, j):
                tok_pairs.append((i, j, ai, bj, line_of[bj]))
        cand2, c2, l2 = dl.apply_pass(cand, sides, tok_pairs, [])
        cnt += c2
        lg += l2
        if c2:
            nm2 = measure(cand2, module, dst, pyc)
            if nm2 is not None and nm2['diffs'] < step['diffs']:
                cand, m, cnt_best = cand2, nm2, cnt
            else:
                cand, m, cnt_best = cand, step, c1
        else:
            m = step
            cnt_best = c1
        if cnt_best == 0:
            break
        text = cand
        total += cnt_best
        log += lg
    out, rc = dl.run_worker('compile', dst.replace(os.sep, '/'))
    ok = 'COMPILE-OK' in out
    if verbose:
        print('%s: расхождений было %d, замен %d, осталось %d, compile=%s' %
              (module, start, total, m['diffs'], ok))
        for l in log[:20]:
            print('   %s: %s -> %s' % (l[0], l[1], str(l[2])[:40]))
    return {'module': module, 'start': start, 'rep': total, 'left': m['diffs'],
            'compile_ok': ok, 'log': log}


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == '--scan':
        work = (args[1] if len(args) > 1 else 'E:/LocalTemp/opencode/ldprobe/litcheck').replace('\\', '/')
        mods = []
        for root, dirs, files in os.walk(FIXED):
            for f in files:
                if f.endswith('.py'):
                    mods.append(os.path.relpath(os.path.join(root, f), FIXED).replace(os.sep, '/')[:-3])
        rows = []
        for m in sorted(mods):
            r = scan_module(m, work)
            rows.append((m, r))
        bad = [(m, r) for m, r in rows if r['status'] != 'ok']
        pos = [(m, r) for m, r in rows if r['status'] == 'ok' and r['diffs']]
        print('просканировано %d; статусных проблем %d; модулей с позиционными расхождениями %d' %
              (len(rows), len(bad), len(pos)))
        for m, r in sorted(pos, key=lambda x: -x[1]['diffs']):
            print('  %s: diffs=%d  первые: %s' % (m, r['diffs'], r['litpairs'][:4]))
        for m, r in bad:
            print('  BAD %s %s' % (m, r['status']))
    elif args[0] == '--all':
        mods = []
        for root, dirs, files in os.walk(SNAP):
            for f in files:
                if f.endswith('.py'):
                    mods.append(os.path.relpath(os.path.join(root, f), SNAP).replace(os.sep, '/')[:-3])
        rows = [recover(m) for m in sorted(mods)]
        bad = [r for r in rows if r and (r['left'] or not r['compile_ok'])]
        print('модулей с остаточными расхождениями: %d из %d' % (len(bad), len(rows)))
        for r in bad:
            print('  %s left=%d compile=%s' % (r['module'], r['left'], r['compile_ok']))
    else:
        recover(args[0])


main()
