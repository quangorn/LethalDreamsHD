"""Канонизация токенов vdiff: прыжковые аргументы приводятся к относительному смещению,
константы разрешаются к значениям, None-имена = None-константа, хвостовой return вырезается.
Возвращает нормализованные списки токенов для A и B, чтобы сравнение не зависело от
косметических сдвигов длин кода."""
import re

# опкоды, чей аргумент = адрес прыжка
JUMPS = ('SETUP_LOOP', 'SETUP_EXCEPT', 'SETUP_FINALLY', 'BREAK_LOOP', 'CONTINUE_LOOP',
         'JUMP_ABSOLUTE', 'JUMP_FORWARD', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE', 'FOR_ITER')

CONST_HEX = re.compile(r'^LOAD_CONST HEX (.+)$')


def canon_list(toks, consts):
    """Список токенов (уже без SET_LINENO) -> канонический список.
    consts - co_consts этого кодового объекта, для разрешения LOAD_CONST."""
    out = []
    n = len(toks)
    # сначала считаем позиции: у токена 'OP arg' размер 3, иначе 2 (в байткоде)
    pos = []
    p = 0
    for t in toks:
        pos.append(p)
        p += 3 if ' ' in t else 2
    for t, p in zip(toks, pos):
        m = CONST_HEX.match(t)
        if m:
            try:
                v = eval(bytes.fromhex(m.group(1)).decode('cp1251', 'replace'),
                         {'__builtins__': {}}, {})
                t = 'CONST %r' % (v,)
            except Exception:
                pass
        for pre in ('LOAD_NAME ', 'LOAD_GLOBAL ', 'STORE_NAME ', 'STORE_GLOBAL '):
            if t == pre + 'None':
                t = 'CONST None'
        parts = t.split(None, 1)
        op = parts[0]
        if op in JUMPS and len(parts) == 2:
            try:
                t = '%s REL %+d' % (op, int(parts[1]) - p)
            except ValueError:
                pass
        out.append(t)
    # вырезаем хвостовой 'CONST None' + 'RETURN_VALUE'
    while len(out) >= 2 and out[-2] == 'CONST None' and out[-1] == 'RETURN_VALUE':
        out = out[:-2]
    return out


def real_diffs(ta_raw, tb_raw, ca, cb):
    import difflib
    ta = canon_list(ta_raw, ca)
    tb = canon_list(tb_raw, cb)
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    res = []
    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        xa, xb = ta[a1:a2], tb[b1:b2]
        # __module__/__qualname__ в компилят - косметика
        MODTOK = ('LOAD_GLOBAL __name__', 'STORE_NAME __module__', 'LOAD_CONST <code>',
                  'LOAD_NAME __name__', 'STORE_NAME __qualname__')
        if all(t in MODTOK for t in xa + xb):
            continue
        # -x против x + UNARY_NEGATIVE - то же значение
        if (len(xa) == 1 and xa[0].startswith('CONST -') and len(xb) == 2
                and xb[1].startswith('UNARY_NEGATIVE')
                and xb[0] == 'CONST ' + xa[0][len('CONST -'):]):
            continue
        if len(xa) == len(xb):
            # блок прыжковых токенов с одинаковым delta = сдвиг из-за вставок, косметика
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
