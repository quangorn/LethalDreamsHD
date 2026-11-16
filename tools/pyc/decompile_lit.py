#!/usr/bin/env python3
"""decompile_lit.py — восстановление литералов в декомпиляте (uncompyle6) по имени
кодовых объектов, с гейтом LOAD_CONST.

Декомпилят местами пишет ИНДЕКС co_consts как ЦЕЛОЕ (оригинал грузит '' (const 7),
в декомпиляте стоит 7). Этот скрипт:
  1. нормализует decompiled_fixed/<module>.py;
  2. воркером litfix2 получает потоки токенов A (оригинальный .pyc) и B (компилят .py),
     где аргумент LOAD_CONST разрешён к значению;
  3. выравнивает кодовые объекты по ИМЕНАМ (difflib по списку co_name) — декомпилят
     может терять функции;
  4. внутри пар выравнивает токены; пара (A = LOAD_CONST V, B = LOAD_CONST K), где K —
     целое и consts_orig[K] == V, K != V => в строке, указанной SLINE этого токена,
     заменяем K на py2_literal(V); int->int тоже разрешён (3->1, 2->0);
  5. цикл до сходимости, финальная компиляция воркером;
  6. Гейт A: число LOAD_CONST-пар, где значение компилята != значения оригинала (в
     выровненных позициях). Отчёт строковых литералов и потерянных функций.

Файл лежит в tools/pyc/. Результаты — в decompiled_fixed/<module>.py (LF, utf-8)
корня репозитория. База = сам decompiled_fixed/ (в корне репозитория).

Команды (из корня репозитория):
  python3 tools/pyc/decompile_lit.py <module>          один модуль (ui/battlemenu)
  python3 tools/pyc/decompile_lit.py --all             все 434 модуля -> decompiled_fixed/ + report.txt
  python3 tools/pyc/decompile_lit.py --check-battlemenu  гейт B для ui/battlemenu
"""
import sys, os, re, difflib, collections

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pycbuild
import pyc_scan

SNAP = pycbuild.SNAP
BACKUP = pycbuild.BACKUP
FIXED = os.path.join(pycbuild.REPO_ROOT, 'decompiled_fixed')
# собственный run_worker: путь компиляции = py223-каталог + cache/lib (кэш Lib из
# snapshot-архива, `python3 tools/extract_pristine.py lib`); py2.2 требует stdlib с
# строчными .py (в py223 все .PY верхним регистром и import os по ним не находится);
# игровой Lib в путь компиляции не входит; сам pycbuild.py не трогаем
ENV = dict(os.environ, PYTHONPATH=paths.py223_dir() + ';' + paths.lib_dir())


def run_worker(*args):
    import subprocess
    p = subprocess.run([pycbuild.PY22, pycbuild.WORKER] + list(args),
                       capture_output=True, text=False, env=ENV, cwd=pycbuild.TOOL_DIR)
    out = p.stdout.decode('utf-8', 'replace')
    err = p.stderr.decode('utf-8', 'replace')
    lines = [l for l in out.split('\n') if l.strip() and 'import site' not in l]
    if p.returncode != 0:
        bad = [l for l in err.split('\n') if 'import site' not in l and l.strip()]
        return '\n'.join(lines + bad), p.returncode
    return '\n'.join(lines), p.returncode


py2_literal = pycbuild.py2_literal
normalize = pycbuild.normalize
load_opcodes = pycbuild.load_opcodes


def decode_hex(h):
    """repr константы из 2.2 (байты) -> значение Python 3; py2 long 'L' трактуется int."""
    raw = bytes.fromhex(h)
    txt = raw.decode('cp1251', 'replace')
    try:
        return eval(txt, {'__builtins__': {}}, {})
    except Exception:
        t = txt.strip()
        if t.endswith('L'):
            try:
                return int(t[:-1])
            except ValueError:
                pass
        return ('RAW', txt)


def parse_litfix2(out):
    """Парсинг вывода воркера litfix2: sides[A|B] = список кодовых объектов
    {name, consts, toks, line_of}; line_of[j] - номер строки B-исходника для токена j."""
    sides = {'A': [], 'B': []}
    cur_line = {}
    for line in out.split('\n'):
        parts = line.split()
        if len(parts) < 3:
            continue
        tag, side = parts[0], parts[1]
        if tag == 'SLINE' and len(parts) >= 4 and parts[1] != 'B':
            # воркер печатает SLINE для B без side-маркера: 'SLINE idx j arg'
            idx2 = int(parts[1])
            cur_line[('B', idx2)] = int(parts[3])
            continue
        if side not in ('A', 'B'):
            continue
        try:
            idx = int(parts[2])
        except ValueError:
            continue
        lst = sides[side]
        while len(lst) <= idx:
            lst.append({'name': None, 'consts': [], 'toks': [], 'line_of': []})
        entry = lst[idx]
        if tag == 'CODE':
            entry['name'] = parts[3] if len(parts) > 3 else ''
        elif tag == 'CONST':
            v = parts[3] if len(parts) > 3 else ''
            if v == '<code>':
                entry['consts'].append(None)
            else:
                entry['consts'].append(decode_hex(v))
        elif tag == 'TOK':
            j = int(parts[3])
            tok = ' '.join(parts[4:])
            if j == len(entry['toks']):
                entry['toks'].append(tok)
                entry['line_of'].append(cur_line.get((side, idx)))
    return sides


def align_names(a_names, b_names):
    """Выравнивание списков co_name. Возвращает pairs (iA,iB), missing (A), extra (B)."""
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


def protected_ranges(line):
    """Интервалы внутри строковых литералов ('..'/"..") - позиции, где число менять нельзя."""
    ranges = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c in "'\"":
            q = c
            start = i + 1
            i += 1
            while i < n:
                if line[i] == '\\':
                    i += 2
                    continue
                if line[i] == q:
                    break
                i += 1
            ranges.append((start, min(i, n)))
            i += 1
        else:
            i += 1
    return ranges


def find_int_from(line, K, cursor, prot):
    """Первое standalone-вхождение целого K в строке с позиции cursor, вне строковых
    литералов и не сразу после арифметического оператора (это настоящий int-литерал)."""
    pat = re.compile(r'(?<![\w.])' + str(K) + r'(?![\w.])')
    pos = max(0, cursor)
    while True:
        m = pat.search(line, pos)
        if not m:
            return None
        if any(s <= m.start() < e for s, e in prot):
            pos = m.start() + 1
            continue
        pre = line[:m.start()].rstrip()
        if pre and pre[-1] in '+-*/%&|^<>=~':
            pos = m.start() + 1
            continue
        return m


def const_val(tok):
    """LOAD_CONST HEX h -> значение; LOAD_CONST <code> -> 'CODE'; иначе None."""
    if tok.startswith('LOAD_CONST HEX '):
        return decode_hex(tok[len('LOAD_CONST HEX '):])
    if tok == 'LOAD_CONST <code>':
        return 'CODE'
    return None


def align_tokens(toksA, toksB):
    """Выравнивание токенов кодового объекта: pairs [(ai, bj)], unmatchedA, unmatchedB."""
    sm = difflib.SequenceMatcher(None, toksA, toksB, autojunk=False)
    pairs, ua, ub = [], [], []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            pairs += [(i1 + k, j1 + k) for k in range(i2 - i1)]
        elif tag == 'delete':
            ua += list(range(i1, i2))
        elif tag == 'insert':
            ub += list(range(j1, j2))
        else:
            for k in range(min(i2 - i1, j2 - j1)):
                pairs.append((i1 + k, j1 + k))
            ua += list(range(i1 + min(i2 - i1, j2 - j1), i2))
            ub += list(range(j1 + min(i2 - i1, j2 - j1), j2))
    return pairs, ua, ub


def decompile_module(module, max_passes=14, verbose=True):
    """Полный цикл recovery: normalize -> passes litfix2 -> gate -> компиляция."""
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    if not os.path.exists(src):
        return {'module': module, 'status': 'no source'}
    pyc = os.path.join(BACKUP, module.replace('/', os.sep) + '.pyc').replace(os.sep, '/')
    if not os.path.exists(pyc):
        return {'module': module, 'status': 'no orig pyc'}
    text = normalize(open(src, encoding='utf-8').read())
    dst = os.path.join(FIXED, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    outp = dst.replace(os.sep, '/')
    total_rep = 0
    all_log = []
    syntax = None

    def measure(t):
        """Гейт+выравнивание по текущему тексту файла."""
        open(dst, 'w', encoding='utf-8', newline='\n').write(t.replace('\r\n', '\n'))
        out, rc = run_worker('litfix2', pyc, outp)
        if 'MAGIC-MISMATCH' in out:
            return None
        syn = [l for l in out.split('\n') if l.startswith('SYNTAX')]
        if syn:
            return {'syntax': syn[0]}
        s = parse_litfix2(out)
        if not s['A']:
            return {'syntax': 'empty worker output'}
        np, ms, ex = align_names([c['name'] for c in s['A']],
                                 [c['name'] for c in s['B']])
        tp, un = build_pairs(s, np)
        return {'sides': s, 'npairs': np, 'missing': ms, 'extra': ex,
                'tok_pairs': tp, 'unops': un, 'mu': multiset_unops(s, np),
                'gate': gate_count(s, np, tp)}

    m = measure(text)
    if m is None:
        return {'module': module, 'status': 'magic'}
    if 'syntax' in m:
        return {'module': module, 'status': 'syntax', 'syntax': m['syntax']}
    for _ in range(max_passes):
        if m['gate'] == 0:
            break
        sides = m['sides']
        improved = False
        # стратегии от самых агрессивных к строгим; проход принимается только
        # если гейт строго уменьшился (иначе осцилляция позиционного выравнивания)
        subsets = [
            (m['tok_pairs'], m['unops']),                       # полная пара+unops
            ([], m['unops']),                                   # только unops
            ([], m['mu']),                                      # multiset-delta
            ([], [u for u in m['unops']
                  if isinstance(u[3], int) and 0 <= u[3] < len(sides['A'][u[0]]['consts'])
                  and sides['A'][u[0]]['consts'][u[3]] == u[4]]),  # rule-1
        ]
        for tp, un in subsets:
            cand, cnt, log = apply_pass(text, sides, tp, un)
            if cnt == 0:
                continue
            nm = measure(cand)
            if nm is None or 'syntax' in nm:
                continue
            if nm['gate'] < m['gate']:
                text = cand
                total_rep += cnt
                all_log += log
                m = nm
                improved = True
                break
        if not improved:
            # greedy-polish: по одному кандидату за раз, принимаем только те
            # замены, которые строго уменьшают гейт
            round_prog = True
            budget = 250
            while round_prog and budget > 0 and m['gate'] > 0:
                round_prog = False
                sides0 = m['sides']
                cands = []
                for (i, j, ai, bj, ln) in m['tok_pairs']:
                    if sides0['A'][i]['toks'][ai] != sides0['B'][j]['toks'][bj]:
                        cands.append(('pair', (i, j, ai, bj, ln)))
                for u in m['mu']:
                    cands.append(('unop', u))
                for u in m['unops']:
                    cands.append(('unop', u))
                for kind, item in cands:
                    if budget <= 0:
                        break
                    budget -= 1
                    tp1 = [item] if kind == 'pair' else []
                    un1 = [item] if kind == 'unop' else []
                    cand, cnt, log = apply_pass(text, sides0, tp1, un1)
                    if cnt == 0:
                        continue
                    nm = measure(cand)
                    if nm is None or 'syntax' in nm:
                        continue
                    if nm['gate'] < m['gate']:
                        text = cand
                        total_rep += cnt
                        all_log += log
                        m = nm
                        round_prog = True
                        break
            if not round_prog:
                break
    sides = m['sides']
    npairs, missing, extra = m['npairs'], m['missing'], m['extra']
    gate = m['gate']
    ca, cb = str_metrics(sides)
    # финальная компиляция
    open(dst, 'w', encoding='utf-8', newline='\n').write(text.replace('\r\n', '\n'))
    out, rc = run_worker('compile', outp)
    compile_ok = 'COMPILE-OK' in out
    res = {'module': module, 'status': 'ok' if compile_ok else 'SYNTAX',
           'syntax': syntax, 'A': len(sides['A']), 'B': len(sides['B']),
           'gate': gate, 'rep': total_rep, 'missing': missing, 'extra': extra,
           'strA': ca, 'strB': cb, 'compile_ok': compile_ok}
    if verbose:
        print('%s: A=%d B=%d gate=%d замен=%d стр.констант A=%d стр.литералов B=%d compile=%s' % (
            module, res['A'], res['B'], gate, total_rep, ca, cb, compile_ok))
        for line in all_log[:12]:
            print('   %s: %s -> %s' % (line[0], line[1], line[2]))
        if missing:
            print('   потерянные функции:', ', '.join(missing))
        if extra:
            print('   лишние:', ', '.join(extra))
        if not compile_ok:
            print('   SYNTAX:', out.strip())
    res['log'] = all_log
    return res


def span_consumed(consumed, lit):
    """Пересечение интервала литерала (sl..el) с уже заменёнными литералами."""
    sl, sc, el, ec = lit
    for (a, ac, b, bc) in consumed:
        if sl <= b and a <= el:
            return True
    return False


def parse_literal(lines, ln, p):
    """Разбор строкового литерала, начинающегося в lines[ln-1][p].
    Префиксы r/u/b/f (комбинации) пропускаются. Возвращает
    (start_line, start_col, end_line, end_col) - 1-based строки."""
    s = lines[ln - 1]
    i = p
    while i < len(s) and s[i] in 'rRbBuUfF':
        i += 1
    if i >= len(s) or s[i] not in "'\"":
        return None
    # начало литерала включает префикс r/u/b/f слева от кавычки
    start = i
    while start > 0 and s[start - 1] in 'rRbBuUfF':
        start -= 1
    q = s[i:i + 3]
    if q in ("'''", '"""'):
        for e in range(ln - 1, len(lines)):
            t = lines[e]
            j = i + 3 if e == ln - 1 else 0
            while j < len(t):
                if t[j] == '\\':
                    j += 2
                    continue
                if t[j:j + 3] == q:
                    return (ln, start, e + 1, j + 3)
                j += 1
        return None
    q = s[i]
    j = i + 1
    while j < len(s):
        if s[j] == '\\':
            j += 2
            continue
        if s[j] == q:
            return (ln, start, ln, j + 1)
        j += 1
    return None


def lit_text(lines, lit):
    """Текст литерала по span (sl,sc,el,ec) 1-based: одна строка - срез,
    многострочный - строки целиком между началом и концом."""
    sl, sc, el, ec = lit
    if el == sl:
        return lines[sl - 1][sc:ec]
    return '\n'.join([lines[sl - 1][sc:]] + lines[sl:el - 1] + [lines[el - 1][:ec]])


def find_int_unused_region(lines, start_idx, K, used, window=40):
    """Ищет standalone-вхождение целого K в регионе statements, начиная со строки
    start_idx (SLINE = начало statement; литералы dict/list могут быть на следующих
    строках). Окно window строк. Возвращает (line_idx, match) или None."""
    pat = re.compile(r'(?<![\w.])' + str(K) + r'(?![\w.])')
    for li in range(start_idx, min(start_idx + window, len(lines))):
        line = lines[li]
        prot = protected_ranges(line)
        pos = 0
        while True:
            m = pat.search(line, pos)
            if not m:
                break
            pos = m.start() + 1
            if any(s <= m.start() < e for s, e in prot):
                continue
            if any(m.start() < e and m.end() > s for (s, e) in used.get(li, [])):
                continue
            pre = line[:m.start()].rstrip()
            if pre and pre[-1] in '+-*/%&|^<>~':
                continue
            return (li, m)
    return None


def multiset_unops(sides, npairs):
    """Delta-замены по мультимножеству LOAD_CONST: для каждого кодового объекта
    берём значения B, которых нет в A (extra), и значения A, отсутствующие в B
    (missing), и спариваем их по количеству. Каждая пара (e -> v): литерал e в
    регионе statements объекта заменяется на py2_literal(v). Гейт A = мультимножества,
    поэтому любое биективное спаривание приводит к gate=0; регион поиска -
    строки statements кодового объекта."""
    import collections

    def vals(toks):
        toks = list(toks)
        # хвостовой LOAD_CONST None (RETURN_VALUE) - артефакт, не печатается
        while toks and toks[-1] == 'LOAD_CONST HEX 4e6f6e65':
            toks.pop()
        out = []
        for t in toks:
            if t.startswith('LOAD_CONST HEX '):
                out.append(decode_hex(t[len('LOAD_CONST HEX '):]))
            elif t == 'LOAD_CONST <code>':
                out.append('<code>')
        return collections.Counter(out)

    res = []
    for (i, j) in npairs:
        ca = vals(sides['A'][i]['toks'])
        cb = vals(sides['B'][j]['toks'])
        d_extra = cb - ca   # лишние в B
        d_miss = ca - cb    # недостающие в B
        extra = [(v, n) for v, n in d_extra.items() if n > 0]
        miss = [(v, n) for v, n in d_miss.items() if n > 0]
        if not extra or not miss:
            continue
        # регион statements кодового объекта B
        lns = [ln for ln in sides['B'][j]['line_of'] if ln is not None]
        ln0 = min(lns) if lns else 1
        span = (max(lns) - min(lns) + 40) if lns else 40
        if ln0 < 1:
            ln0 = 1
        flat_extra = [v for v, n in extra for _ in range(n)]
        flat_miss = [v for v, n in miss for _ in range(n)]
        # спариваем значения: сначала внутри одного типа (int<->int, str<->str),
        # затем оставшиеся кросс-тайповые (в декомпилате строковый литерал там,
        # где в оригинале int - тип литерала восстанавливается заменой)
        def tname(v):
            return type(v).__name__
        used_extra = collections.Counter()
        used_miss = collections.Counter()
        pairs = []
        types = sorted(set(tname(v) for v in flat_extra) &
                       set(tname(v) for v in flat_miss))
        for tt in types:
            ex = sum([[v] * n for v, n in d_extra.items()
                      if n > 0 and tname(v) == tt], [])
            ms = sum([[v] * n for v, n in d_miss.items()
                      if n > 0 and tname(v) == tt], [])
            q = 0
            for e in ex:
                if q >= len(ms):
                    break
                pairs.append((e, ms[q]))
                used_extra[e] += 1
                used_miss[ms[q]] += 1
                q += 1
        rem_ex = [v for v in flat_extra
                  if used_extra[v] < flat_extra.count(v)]
        rem_ms = [v for v in flat_miss
                  if used_miss[v] < flat_miss.count(v)]
        q = 0
        for e in rem_ex:
            if q >= len(rem_ms):
                break
            pairs.append((e, rem_ms[q]))
            q += 1
        for (e, v) in pairs:
            if e == v:
                continue
            res.append((i, j, ln0, e, v, span))
    return res


def build_pairs(sides, npairs):
    """tok_pairs: выровненные позиции; unops: явные замены для НЕспаренных токенов.
    Неспаренный B-токен K (целое) сопоставляется с неспаренным A-значением V,
    если consts_orig[K] == V - это индекс, потерянный позиционным выравниванием."""
    import collections
    tok_pairs = []
    unops = []
    for (i, j) in npairs:
        tp, ua, ub = align_tokens(sides['A'][i]['toks'], sides['B'][j]['toks'])
        for (ai, bj) in tp:
            tok_pairs.append((i, j, ai, bj, sides['B'][j]['line_of'][bj]))
        consts = sides['A'][i]['consts']
        tb = sides['B'][j]['toks']
        cA = collections.Counter()
        for idx in ua:
            t = sides['A'][i]['toks'][idx]
            if t.startswith('LOAD_CONST HEX '):
                cA[repr(decode_hex(t[len('LOAD_CONST HEX '):]))] += 1
        # порядок обхода неспаренных A-значений (порядок B-потока в строке = порядок литералов)
        ua_vals = []
        for idx in sorted(ua):
            t = sides['A'][i]['toks'][idx]
            if t.startswith('LOAD_CONST HEX '):
                ua_vals.append(decode_hex(t[len('LOAD_CONST HEX '):]))
        p = 0
        for bj in sorted(ub):
            tB = tb[bj]
            if not tB.startswith('LOAD_CONST HEX '):
                continue
            K = decode_hex(tB[len('LOAD_CONST HEX '):])
            V = None
            # 1) правило индекса: consts_orig[K] == значение из мультимножества неспаренных A
            if isinstance(K, int) and K >= 0 and K < len(consts) and cA.get(repr(consts[K]), 0) > 0:
                V = consts[K]
            # 2) порядное сопоставление: k-й неспаренный B-токен <-> k-е неспаренное A-значение
            if V is None and p < len(ua_vals):
                V = ua_vals[p]
                p += 1
            if V is None:
                continue
            if V == K:
                continue  # no-op: значение уже совпадает, замена не нужна
            if repr(V) not in cA:
                continue
            cA[repr(V)] -= 1
            ln = sides['B'][j]['line_of'][bj]
            unops.append((i, j, ln, K, V, 40))
    return tok_pairs, unops


def apply_pass(text, sides, tok_pairs, unops):
    """Восстановление литералов построчными мультимножествами.
    Для каждой выровненной пары кодовых объектов группы токенов сортируются по
    строке B-исходника (SLINE). Равные токены (tokA==tokB) защищают позиции
    соответствующих литералов (мультинабор E). Неравные LOAD_CONST-пары:
      - B=целое K, A=V, consts_orig[K]==V  -> K заменяем на py2_literal(V);
      - B=целое K, A=int V                 -> K заменяем на repr(V);
      - B=строка S, A=V (str/int/...)      -> строковый литерал со значением S
        заменяем на py2_literal(V) (дрейф содержимого/docstring).
    Позиции ищутся с начала строки с пропуском занятых (used) - это переживает
    обращения value->key в BUILD_MAP иProtection дубликатов. Смещение строк после
    схлопования многострочных литералов учитывается offset."""
    import ast, collections
    lines = text.split('\n')
    used = {}
    consumed = []  # заменённые строковые литералы: (sl, sc, el, ec), 1-based
    cnt = 0
    log = []

    def decode_tok(t):
        if t.startswith('LOAD_CONST HEX '):
            return decode_hex(t[len('LOAD_CONST HEX '):])
        if t == 'LOAD_CONST <code>':
            return 'CODE'
        return None

    # группы по строке, порядок обхода - по B-потоку
    by_line = collections.OrderedDict()
    un_by_line = collections.OrderedDict()

    def clamp_ln(ln):
        if ln is None:
            return None
        if ln < 1:
            return 1  # первый SET_LINENO module-кода = 0; литерал - на строке 1
        if ln > len(lines):
            return None
        return ln

    for (i, j, ai, bj, ln) in tok_pairs:
        ln = clamp_ln(ln)
        if ln is None:
            continue
        by_line.setdefault((i, j, ln), []).append((ai, bj))
    for (i, j, ln, K, V, span) in unops:
        ln = clamp_ln(ln)
        if ln is None:
            continue
        un_by_line.setdefault((i, j, ln), []).append((K, V, span))

    keys = sorted(set(by_line.keys()) | set(un_by_line.keys()), key=lambda t: t[2])
    off = 0  # схлопнутые строки, сдвиг номеров строк
    for key in keys:
        (i, j, ln) = key
        idx = ln - 1 - off
        if idx < 0 or idx >= len(lines):
            continue
        ta = sides['A'][i]['toks']
        tb = sides['B'][j]['toks']
        consts = sides['A'][i]['consts']
        cur_line = lines[idx]
        prot = protected_ranges(cur_line)
        used_idx = used.setdefault(idx, [])
        pairs = []
        E = collections.Counter()
        for (ai, bj) in by_line.get(key, []):
            tA = ta[ai]
            tB = tb[bj]
            if tA == tB:
                vb = decode_tok(tB)
                if isinstance(vb, int) and vb >= 0:
                    E[vb] += 1
                continue
            va = decode_tok(tA)
            vb = decode_tok(tB)
            if va is None or vb is None:
                continue
            pairs.append((vb, va))
        # защита первых E[K] standalone-вхождений значения K
        pat_all = re.compile(r'(?<![\w.])\d+(?![\w.])')
        pos_list = []
        for m in pat_all.finditer(cur_line):
            if any(s <= m.start() < e for s, e in prot):
                continue
            pos_list.append((m.start(), m.end(), int(m.group())))
        for val, n in E.items():
            taken = 0
            for (s, e, v) in pos_list:
                if v == val and taken < n:
                    used_idx.append((s, e))
                    taken += 1
        for (K, V) in pairs:
            if not isinstance(K, int) or K < 0:
                continue
            if K == V:
                continue
            target = None
            if K < len(consts) and consts[K] == V:
                target = py2_literal(V)
            elif isinstance(V, int):
                target = repr(V)
            if target is None:
                continue
            r = find_int_unused_region(lines, idx, K, used)
            if not r:
                continue
            li, m = r
            lines[li] = lines[li][:m.start()] + target + lines[li][m.end():]
            used.setdefault(li, []).append((m.start(), m.start() + len(target)))
            cnt += 1
            log.append((sides['A'][i]['name'], K, str(V)[:40]))
        # строковый дрейф: литерал со значением S -> py2_literal(V), регион statement
        for (S, V) in pairs:
            if not isinstance(S, str):
                continue
            if V == S:
                continue
            hit = None
            for li in range(idx, min(idx + 40, len(lines))):
                for p in range(len(lines[li])):
                    if lines[li][p] not in "'\"":
                        continue
                    lit = parse_literal(lines, li + 1, p)
                    if not lit:
                        continue
                    sl, sc, el, ec = lit
                    if span_consumed(consumed, lit):
                        continue
                    txt = lit_text(lines, lit)
                    try:
                        val = ast.literal_eval(txt)
                    except Exception:
                        continue
                    if val != S:
                        continue
                    hit = lit
                    break
                if hit:
                    break
            if not hit:
                continue
            sl, sc, el, ec = hit
            lit_new = py2_literal(V)
            tail = lines[el - 1][ec:]
            lines[sl - 1] = lines[sl - 1][:sc] + lit_new + tail
            del lines[sl:el]
            consumed.append((sl, sc, sl, sc + len(lit_new)))
            off += el - sl
            idx = ln - 1 - off
            cnt += 1
            log.append((sides['A'][i]['name'], str(S)[:30], str(V)[:30]))
        # явные замены для неспаренных токенов (правило consts[K]==V), регион statement
        for (K, V, span) in un_by_line.get(key, []):
            if isinstance(K, int) and K >= 0:
                target = py2_literal(V)
                r = find_int_unused_region(lines, idx, K, used, span)
                if not r:
                    continue
                li, m = r
                lines[li] = lines[li][:m.start()] + target + lines[li][m.end():]
                used.setdefault(li, []).append((m.start(), m.start() + len(target)))
                cnt += 1
                log.append((sides['A'][i]['name'], K, str(V)[:40]))
            elif isinstance(K, str):
                # строковый литерал со значением K -> py2_literal(V)
                hit = None
                qpat = re.compile(r'[\'"]')
                for li in range(idx, min(idx + span, len(lines))):
                    for mm in qpat.finditer(lines[li]):
                        p = mm.start()
                        lit = parse_literal(lines, li + 1, p)
                        if not lit:
                            continue
                        sl, sc, el, ec = lit
                        if span_consumed(consumed, lit):
                            continue
                        txt = lit_text(lines, lit)
                        try:
                            val = ast.literal_eval(txt)
                        except Exception:
                            continue
                        if val != K:
                            continue
                        hit = lit
                        break
                    if hit:
                        break
                if not hit:
                    continue
                sl, sc, el, ec = hit
                lit_new = py2_literal(V)
                tail = lines[el - 1][ec:]
                lines[sl - 1] = lines[sl - 1][:sc] + lit_new + tail
                del lines[sl:el]
                consumed.append((sl, sc, sl, sc + len(lit_new)))
                off += el - sl
                idx = ln - 1 - off
                cnt += 1
                log.append((sides['A'][i]['name'], str(K)[:30], str(V)[:40]))
    return '\n'.join(lines), cnt, log


def gate_count(sides, npairs, tok_pairs):
    """Гейт A: для каждой выровненной пары кодовых объектов — мультимножества значений
    LOAD_CONST A и B; расхождение = |A-B| + |B-A| (потерянные/лишние значения).
    Реальный дрейф (63 вместо 69) даёт расхождение; шум позиционного выравнивания
    SequenceMatcher при равных мультимножествах — нет. Неспаренные кодовые объекты:
    все их LOAD_CONST считаются расхождением."""
    import collections
    mism = 0
    pairedA = set(i for (i, j) in npairs)
    pairedB = set(j for (i, j) in npairs)
    def vals(c, side):
        toks = list(c['toks'])
        # хвостовые LOAD_CONST None - артефакт неявного/двойного return (RETURN_VALUE=82
        # ниже HAVE_ARGUMENT и litfix2 его не печатает); снимаем с обеих сторон
        while toks and toks[-1] == 'LOAD_CONST HEX 4e6f6e65':
            toks.pop()
        out = []
        for t in toks:
            if t.startswith('LOAD_CONST HEX '):
                v = decode_hex(t[len('LOAD_CONST HEX '):])
                out.append(repr(v))
            elif t == 'LOAD_CONST <code>':
                out.append('<code>')
        return collections.Counter(out)
    for (i, j) in npairs:
        ca = vals(sides['A'][i], 'A')
        cb = vals(sides['B'][j], 'B')
        d1 = ca - cb
        d2 = cb - ca
        # терпимость к peephole-разнице альфа/2.2.3: константа -x vs (x, unary minus).
        # В B-компиляте x+UNARY_NEGATIVE даёт то же значение; пары (x, -x) снимаем.
        for ra in list(d1.keys()):
            for rb in list(d2.keys()):
                try:
                    va = eval(ra, {'__builtins__': {}}, {})
                    vb = eval(rb, {'__builtins__': {}}, {})
                except Exception:
                    continue
                if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and va == -vb and va != 0:
                    n = min(d1[ra], d2[rb])
                    d1[ra] -= n
                    d2[rb] -= n
                    if d1[ra] == 0:
                        del d1[ra]
                    if d2[rb] == 0:
                        del d2[rb]
        mism += sum(d1.values()) + sum(d2.values())
    for idx, c in enumerate(sides['A']):
        if idx not in pairedA:
            mism += sum(1 for t in c['toks'] if t.startswith('LOAD_CONST'))
    for idx, c in enumerate(sides['B']):
        if idx not in pairedB:
            mism += sum(1 for t in c['toks'] if t.startswith('LOAD_CONST'))
    return mism


def str_metrics(sides):
    """Число строковых констант оригинала (A) и строковых литералов компилята (B)."""
    ca = sum(1 for c in sides['A'] for v in c['consts'] if isinstance(v, str))
    cb = sum(1 for c in sides['B'] for v in c['consts'] if isinstance(v, str))
    return ca, cb


def cmd_all():
    man = pycbuild.all_modules()
    rows = []
    for m in man:
        if m in pycbuild.SKIP:
            rows.append('%s: skip (пустой декомпилят)' % m)
            print('skip', m)
            continue
        r = decompile_module(m, verbose=False)
        r['log'] = None
        rows.append(r)
        print(r['module'], 'A=%s B=%s gate=%s rep=%s' % (
            r.get('A', '?'), r.get('B', '?'), r.get('gate', '?'), r.get('rep', 0)))
    # report.txt
    lines = []
    tot_gate = 0
    bad = []
    for r in rows:
        if isinstance(r, str):
            lines.append(r)
            continue
        if r['status'] not in ('ok',):
            bad.append(r)
            lines.append('%s: STATUS=%s %s' % (r['module'], r['status'], r.get('syntax', '')))
            continue
        tot_gate += r['gate']
        mark = ''
        if r['A'] != r['B'] or r['missing'] or r['extra']:
            mark = ' LOST: %s | EXTRA: %s' % (','.join(r['missing']) or '-', ','.join(r['extra']) or '-')
        lines.append('%s: A=%d B=%d gate=%d strA=%d strB=%d rep=%d compile=%s%s' % (
            r['module'], r['A'], r['B'], r['gate'], r['strA'], r['strB'], r['rep'],
            r['compile_ok'], mark))
    out_path = os.path.join(FIXED, 'report.txt')
    open(out_path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('=== ИТОГО: модулей %d, суммарный gate=%d, статусных проблем=%d' % (
        len(rows), tot_gate, len(bad)))
    print('report ->', out_path)


def check_battlemenu():
    """Гейт B: восстановленные строковые литералы в ui/battlemenu."""
    py = os.path.join(FIXED, 'ui', 'battlemenu.py')
    text = open(py, encoding='utf-8').read()
    n_uncial = text.count('ld_uncial12')
    n_empty_inner = len(re.findall(r"\['',\s*'ld_uncial12',\s*FONT_H,\s*1,\s*0,\s*0\]", text))
    print('grep ld_uncial12: %d (ожидание > 0)' % n_uncial)
    print("списков ['', 'ld_uncial12', FONT_H, 1, 0, 0]: %d (ожидание 9)" % n_empty_inner)
    # компиляция и dis: LOAD_CONST ''(7), 'ld_uncial12'(8), FONT_H, 1(3), 0(2), 0(2), BUILD_LIST 6, BUILD_LIST 8
    pyc = os.path.join(FIXED, 'ui', 'battlemenu.pyc').replace(os.sep, '/')
    out, rc = run_worker('build', py.replace(os.sep, '/'), pyc)
    print(out.strip())
    d = open(pyc, 'rb').read()
    top, i = pyc_scan.read(d, 8)
    op = load_opcodes()
    acc = []
    def walk(c):
        acc.append(c)
        for k in c['consts']:
            if isinstance(k, dict):
                walk(k)
    walk(top)
    def c_at(c, idx):
        return c['consts'][idx] if idx < len(c['consts']) else None

    def n_at(c, idx):
        return c['names'][idx] if idx < len(c['names']) else None

    hits = 0
    for c in acc:
        code = c['code']
        toks = []
        pos = 0
        while pos < len(code):
            o = ord(code[pos])
            if o >= 90:
                arg = ord(code[pos + 1]) | (ord(code[pos + 2]) << 8)
                pos += 3
            else:
                arg = None
                pos += 1
            toks.append((o, arg))
        # окно LOAD_CONST '' LOAD_CONST 'ld_uncial12' LOAD_GLOBAL FONT_H LOAD_CONST 1
        #      LOAD_CONST 0 LOAD_CONST 0 BUILD_LIST 6 BUILD_LIST 8
        for k in range(len(toks) - 7):
            w = toks[k:k + 8]
            checks = [
                w[0][0] == op['LOAD_CONST'] and c_at(c, w[0][1]) == '',
                w[1][0] == op['LOAD_CONST'] and c_at(c, w[1][1]) == 'ld_uncial12',
                w[2][0] == op['LOAD_GLOBAL'] and n_at(c, w[2][1]) == 'FONT_H',
                w[3][0] == op['LOAD_CONST'] and c_at(c, w[3][1]) == 1,
                w[4][0] == op['LOAD_CONST'] and c_at(c, w[4][1]) == 0,
                w[5][0] == op['LOAD_CONST'] and c_at(c, w[5][1]) == 0,
                w[6][0] == op['BUILD_LIST'] and w[6][1] == 6,
                w[7][0] == op['BUILD_LIST'] and w[7][1] == 8,
            ]
            if all(checks):
                hits += 1
    print('окон LOAD_CONST \'\' ld_uncial12 FONT_H 1 0 0 BUILD_LIST 6 BUILD_LIST 8: %d (ожидание >= 9)' % hits)
    ok = n_uncial > 0 and hits >= 9
    print('GATE B: %s' % ('PASS' if ok else 'FAIL'))


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)
    if args[0] == '--all':
        cmd_all()
    elif args[0] == '--check-battlemenu':
        check_battlemenu()
    else:
        decompile_module(args[0])
