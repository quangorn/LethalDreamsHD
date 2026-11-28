#!/usr/bin/env python3
"""Нормализация декомпилированных .py -> компилябельных Python 2.2, гейт компиляции,
паритет байткода с оригинальным .pyc, установка в игру.

Файл лежит в tools/pyc/ (рядом с py22_worker.py и opcode22.txt). Каталоги в корне
репозитория:
  decompiled_fixed/  нормализованный декомпилят (база правок, base для build_py)
  build/            нормализованные .py и собранные .pyc
  backup_orig/      оригинальные .pyc

Команды (из корня репозитория):
  python3 tools/pyc/pycbuild.py normalize <module>   build/<module>.py (module = ui/ui, cfg/cfg_default)
  python3 tools/pyc/pycbuild.py gate                 нормализовать всё + компиляция локальным 2.2.3
  python3 tools/pyc/pycbuild.py parity <module>      dis оригинала против dis компилята, дифф
  python3 tools/pyc/pycbuild.py build <module>       .py -> .pyc (магия 2.2.3) в build/
  python3 tools/pyc/pycbuild.py install <module> [--pyc|--py]   установка в data/scripts
  python3 tools/pyc/pycbuild.py uninstall <module>   вернуть оригинальный .pyc, убрать наши файлы
  python3 tools/pyc/pycbuild.py install-min          ТОЧЕЧНО: gamescript (hd_res+SetDisplayMode) + hd_res;
                                                     остальные .pyc остаются оригиналами (текущий метод)
  python3 tools/pyc/pycbuild.py install-hd           полная HD-версия: 5 модулей (масштаб UI) - меню кривое,
                                                     накатывать по частям
  python3 tools/pyc/pycbuild.py restore-all          вернуть ВСЕ оригиналы из backup_orig

Примечание: рабочий интерпретатор 2.2.3 берётся из config.toml (py223.dir);
PYTHONPATH = py223-каталог + cache/lib (кэш Lib из game.lib_archive,
`python3 tools/extract_pristine.py lib`). Игровой каталог в путь компиляции не
входит: импорт рабочего процесса не пишет .pyc в установку игры.
"""
import json, os, re, subprocess, sys, shutil

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pyc_norm

# TOOL_DIR/REPO_ROOT in Windows form: __file__ in Cygwin python is /cygdrive/c/...
# and the native py223 PYTHON.EXE cannot open that path.
TOOL_DIR = paths.win_path(_HERE)
REPO_ROOT = paths.win_path(_ROOT)
SNAP = os.path.join(REPO_ROOT, 'decompiled_fixed')
BUILD = os.path.join(REPO_ROOT, 'build')
BACKUP = os.path.join(REPO_ROOT, 'backup_orig')
MANIFEST = os.path.join(REPO_ROOT, 'decompiled_manifest.json')
GAME_SCRIPTS = paths.game_path('data', 'scripts')
GAME_ROOT = paths.install_dir()
PY22 = paths.py223_exe()
WORKER = os.path.join(TOOL_DIR, 'py22_worker.py')
# лог зондов - в корень игры: путь для строк генерируемого игрового кода
HDLOG = paths.game_path('hdlog.txt')
# PYTHONPATH: py223 (ради .pyd и источников stdlib) + кэш Lib из архива.
# Игровой Lib в путь НЕ входит: импорт рабочего процесса не пишет .pyc в игру.
ENV = dict(os.environ, PYTHONPATH=paths.py223_dir() + ';' + paths.lib_dir())
# декомпилляция не удалась (пустой .py) -> в игру ставим только оригинал
SKIP = {'dictionary_rus'}
HD_MODULES = ['ui/ui', 'gamescript', 'cfg/cfg_default', 'globalscript', 'hd_res']


def run_worker(*args):
    p = subprocess.run([PY22, WORKER] + list(args), capture_output=True, text=False, env=ENV,
                       cwd=TOOL_DIR)
    out = p.stdout.decode('utf-8', 'replace')
    err = p.stderr.decode('utf-8', 'replace')
    lines = [l for l in out.split('\n') if l.strip() and 'import site' not in l]
    if p.returncode != 0:
        bad = [l for l in err.split('\n') if 'import site' not in l and l.strip()]
        return '\n'.join(lines + bad), p.returncode
    return '\n'.join(lines), p.returncode


def normalize(text):
    """Вырезает заголовок uncompyle6, module-level return, py3-литералы bytes."""
    t = text
    # заголовок: все '#' до первой кодовой строки
    t = re.sub(r'\A(#[^\n]*\n)+', '', t)
    # module-level return (строка без отступа) - артефакт RETURN_VALUE декомпилятора
    t = re.sub(r'^return[ \t]*$\n', '', t, flags=re.M)
    # b'..' -> '..' (Python 2: строка и есть bytes; байты из эскейпов сохраняются)
    t = re.sub(r"(?<![A-Za-z0-9_])b(?=['\"])", '', t)
    return t


def inject_probe(text, tag):
    """Зонд внутри игрового скрипта: единственный подтверждённый канал наблюдения
    (хуки stdlib движок не исполняет). Пишем абсолютным путём из install_dir, латиницей."""
    head = "_HD = open('%s', 'a')\n_HD.write('%s start' + chr(10))\n_HD.close()\n" % (HDLOG, tag)
    tail = "_HD = open('%s', 'a')\n_HD.write('%s module-done' + chr(10))\n_HD.close()\n" % (HDLOG, tag)
    return head + text + tail


def instrument_functions(text, tag, limit=40, only=None):
    """Логируем вход в каждую функцию (def), имена берём из строки def.
    limit нужно, чтобы лог не разрастался в игровом цикле.
    only - регулярка: логировать только функции, чьё имя ей подходит."""
    head = ("_HDN = [0]\n"
            "def _hdlog(s):\n"
            "    _HDN[0] += 1\n"
            "    if _HDN[0] > %d:\n"
            "        return\n"
            "    f = open('%s', 'a')\n"
            "    f.write(str(s) + chr(10))\n"
            "    f.close()\n" % (limit, HDLOG))
    out = [head]
    pending_name = None
    for line in text.split('\n'):
        if pending_name is not None:
            out.append("    _hdlog('%s enter' + chr(10))" % pending_name)
            pending_name = None
        out.append(line)
        m = re.match(r'^\s*def ([A-Za-z_][A-Za-z_0-9]*)\(', line)
        if m:
            if only and not re.search(only, m.group(1)):
                continue
            indent = line[:line.index('def ')]
            pending_name = indent + m.group(1)
    return '\n'.join(out)


def inject_trace(text, tag, limit=200):
    """sys.settrace: лог каждой Python-функции, исполненной движком после импорта модуля.
    Единственный способ увидеть, где обрывается исполнение (PyErr_Print движка нам не виден)."""
    head = ("import sys\n"
            "_TRN = [0]\n"
            "def _tr(frame, event, arg):\n"
            "    if event == 'call':\n"
            "        _TRN[0] += 1\n"
            "        if _TRN[0] > %d:\n"
            "            return None\n"
            "        f = open('%s', 'a')\n"
            "        f.write('CALL ' + frame.co.co_name + ':' + str(frame.f_lineno) + chr(10))\n"
            "        f.close()\n"
            "    return _tr\n"
            "sys.settrace(_tr)\n" % (limit, HDLOG))
    return head + text


def build_py(module, probe=False, fns=False, trace=False, fixlits=False, fixints=False, fns_only=None):
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    if not os.path.exists(src):
        return None
    text = normalize(open(src, encoding='utf-8').read())
    dst = os.path.join(BUILD, module.replace('/', os.sep) + ('.probe.py' if probe else '.py'))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if fixints:
        pyc = game_pyc(module)
        if pyc:
            # записываем текст, воркер компилирует его и даёт байткод для сопоставления
            open(dst, 'w', encoding='utf-8', newline='\n').write(text.replace('\r\n', '\n'))
            text, n, log = fixints_text(module, text, dst)
    if fixlits:
        pyc = game_pyc(module)
        if pyc:
            text, _ = apply_lits(text, reconstruct_literals(pyc))
    if fns:
        text = instrument_functions(text, module, only=fns_only)
    if probe:
        text = inject_probe(text, module)
    if trace:
        text = inject_trace(text, module)
    # компилятор 2.2.3 не принимает CRLF в compile(); пишем LF
    open(dst, 'w', encoding='utf-8', newline='\n').write(text.replace('\r\n', '\n'))
    return dst


def all_modules():
    man = json.load(open(MANIFEST, encoding='utf-8'))
    return sorted(k[:-3] if k.endswith('.py') else k for k in man.keys())


def game_pyc(module):
    man = json.load(open(MANIFEST, encoding='utf-8'))
    return man[module.replace(os.sep, '/') + '.py']['pyc']


def cmd_normalize(module):
    dst = build_py(module)
    print('normalized', dst)


def cmd_gate():
    bad = []
    ok = 0
    for m in all_modules():
        dst = build_py(m)
        if dst is None:
            bad.append((m, 'no source'))
            continue
        out, rc = run_worker('compile', dst.replace(os.sep, '/'))
        if 'COMPILE-OK' in out:
            ok += 1
        else:
            line = [l for l in out.split('\n') if l.startswith('SYNTAX')]
            bad.append((m, line[0] if line else out.strip()))
    print('gate: %d компилируются, %d падают (всего %d)' % (ok, len(bad), len(all_modules())))
    for m, why in bad:
        print('  BAD', m, why)


def cmd_build(module):
    py = build_py(module)
    pyc = os.path.join(BUILD, module.replace('/', os.sep) + '.pyc')
    out, rc = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
    print(out)


def normalize_ops(ops):
    """Косметические шумы, не влияющие на семантику:
    - repr вложенного кодового объекта содержит адрес и путь файла -> оставляем имя;
    - декомпилятор пишет лишний return в конце функции (двойной LOAD_CONST None/RETURN_VALUE),
      что байткод-эквивалентно неявному возврату оригинала."""
    out = []
    for op, rep in ops:
        if rep is not None and rep.startswith('<code object '):
            m = re.match(r'<code object (\S+) at', rep)
            rep = 'code:' + (m.group(1) if m else '?')
        out.append((op, rep))
    pair = [('LOAD_CONST', 'None'), ('RETURN_VALUE', None)]
    while len(out) >= 4 and out[-2:] == pair and out[-4:-2] == pair:
        out = out[:-2]
    return out


def parse_blocks(text):
    """Разбор вывода воркера: блоки кодовых объектов (имя, нормализованная сигнатура,
    нормализованный список операций). Шум вырезается pyc_norm (см. pyc_norm.py)."""
    blocks = []
    name = None
    sig = None
    raw = []
    for line in text.split('\n'):
        if line.startswith('### FILE') or line.startswith('### PYC'):
            continue
        if line.startswith('### CODE '):
            if name is not None:
                blocks.append((name, sig, pyc_norm.norm_ops(raw)))
            name = line[len('### CODE '):].rstrip('\r')
            sig = None
            raw = []
        elif line.startswith('SIGNATURE '):
            sig = pyc_norm.norm_signature(line.rstrip('\r'))
        elif line.strip():
            raw.append(line)
    if name is not None:
        blocks.append((name, sig, pyc_norm.norm_ops(raw)))
    return blocks


def cmd_parity(module, verbose=False, strict=False):
    py = build_py(module)
    pyc = game_pyc(module)
    if pyc is None:
        print('parity %s: нет оригинального .pyc' % module)
        return
    orig, rc1 = run_worker('dis-pyc', pyc.replace(os.sep, '/'))
    mine, rc2 = run_worker('dis-py', py.replace(os.sep, '/'))
    if 'SYNTAX' in mine:
        print('parity %s: компилят не компилируется: %s' % (module, mine.strip()))
        return
    a = parse_blocks(orig)
    b = parse_blocks(mine)
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
        if strict:
            ta, tb = a[i][2], b[i][2]
        else:
            ta, tb = pyc_norm.tolerant_ops(a[i][2]), pyc_norm.tolerant_ops(b[i][2])
        s = pyc_norm.similarity(ta, tb)
        total += max(len(ta), len(tb))
        matched += sum(1 for x, y in zip(ta, tb) if x == y)
        if s < 0.995:
            k = 0
            while k < min(len(ta), len(tb)) and ta[k] == tb[k]:
                k += 1
            bad_ops.append(('ops', i, a[i][0], round(s, 4), k, ta[k:k + 5], tb[k:k + 5]))
    sim = matched / total if total else 1.0
    tag = 'STRICT' if strict else 'TOLERANT'
    if not bad_ops and not bad_sig:
        print('PARITY-EQUAL[%s] %s (%d code objects, операции совпадают)' % (tag, module, len(a)))
        return
    print('PARITY %s [%s]: близость %.4f; расхождений sig=%d, ops=%d' %
          (module, tag, sim, len(bad_sig), len(bad_ops)))
    for d in bad_ops[:12]:
        print('   OPS', d)
    for d in bad_sig[:6]:
        print('   SIG', d[0], d[1])
        print('      orig:', str(d[2])[:200])
        print('      mine:', str(d[3])[:200])




def cmd_gate_all():
    """Паритет по всем модулям: сводка равных/расходящихся."""
    equal = []
    diff = []
    missing = []
    for m in all_modules():
        pyc = game_pyc(m)
        if pyc is None:
            missing.append(m)
            continue
        py = build_py(m)
        orig, _ = run_worker('dis-pyc', pyc.replace(os.sep, '/'))
        mine, _ = run_worker('dis-py', py.replace(os.sep, '/'))
        if 'SYNTAX' in mine:
            diff.append((m, 'syntax'))
            continue
        a = parse_blocks(orig)
        b = parse_blocks(mine)
        if a == b:
            equal.append(m)
        else:
            diff.append((m, len(a), len(b)))
    print('паритет: равны %d, расходятся %d, без оригинала %d (всего %d)' %
          (len(equal), len(diff), len(missing), len(all_modules())))
    for d in diff:
        print('  DIFF', d)
    for m in missing:
        print('  NOPYC', m)


def module_similarity(module):
    """Близость операций (tolerant) между оригинальным .pyc и компилятом .py."""
    py = build_py(module)
    pyc = game_pyc(module)
    if pyc is None:
        return None
    orig, _ = run_worker('dis-pyc', pyc.replace(os.sep, '/'))
    mine, _ = run_worker('dis-py', py.replace(os.sep, '/'))
    if 'SYNTAX' in mine:
        return 0.0
    a = parse_blocks(orig)
    b = parse_blocks(mine)
    total = 0
    matched = 0
    for i in range(max(len(a), len(b))):
        if i >= len(a) or i >= len(b):
            total += 1
            continue
        ta = pyc_norm.tolerant_ops(a[i][2])
        tb = pyc_norm.tolerant_ops(b[i][2])
        total += max(len(ta), len(tb))
        matched += sum(1 for x, y in zip(ta, tb) if x == y)
    return matched / total if total else 1.0


def cmd_sim_all(out_path=None):
    """Рейтинг близости по всем модулям (низкие - кандидаты на семантический дрейф)."""
    rows = []
    for m in all_modules():
        s = module_similarity(m)
        rows.append((s, m))
    rows.sort()
    lines = []
    for s, m in rows:
        lines.append('%.4f %s' % (s, m))
    text = '\n'.join(lines)
    print('модулей: %d; полностью совпадают: %d; <0.95: %d; <0.80: %d' % (
        len(rows), sum(1 for s, m in rows if s >= 0.9999),
        sum(1 for s, m in rows if s < 0.95), sum(1 for s, m in rows if s < 0.80)))
    print('худшие 25:')
    for s, m in rows[:25]:
        print('  %.4f %s' % (s, m))
    if out_path:
        open(out_path, 'w', encoding='utf-8').write(text)
        print('полный список ->', out_path)


def load_opcodes():
    """Таблица опкодов Python 2.2 (дамp из 2.2.3, файл opcode22.txt в tools/pyc/)."""
    op = {}
    for line in open(os.path.join(TOOL_DIR, 'opcode22.txt'), encoding='utf-8'):
        parts = line.split()
        if len(parts) == 2:
            op[parts[0]] = int(parts[1])
    return op


MARK = 'MARK'


def has_mark(v):
    """Есть ли внутри значения (list/tuple/dict) недосимвольная заглушка."""
    if v == MARK:
        return True
    if isinstance(v, (list, tuple)):
        for x in v:
            if has_mark(x):
                return True
    return False


def reconstruct_literals(pyc_path):
    """Симуляция стека по байткоду кодовых объектов: чистые литеральные последовательности,
    завершаемые STORE_NAME/STORE_ATTR/STORE_GLOBAL/STORE_SUBSCR, дают точный литерал оригинала.

    Порядок стека в 2.2 (проверено dis'ом на 2.2.3):
      a['b'] = 5      -> LOAD_CONST 5; LOAD_NAME a; LOAD_CONST 'b'; STORE_SUBSCR
                         (стек: значение, контейнер, ключ)
      self.x = 1      -> LOAD_CONST 1; LOAD_FAST self; STORE_ATTR
      {k: v}          -> BUILD_MAP 0; для каждой пары DUP_TOP; LOAD_CONST v; ROT_TWO;
                         LOAD_CONST k; STORE_SUBSCR
    Возвращает список (имя_кодового_объекта, имя, repr_литерала) в порядке обхода дерева -
    этот порядок совпадает с порядком def в исходнике."""
    import pyc_scan
    op = load_opcodes()
    d = open(pyc_path, 'rb').read()
    top, i = pyc_scan.read(d, 8)
    out = []
    def walk(c):
        st = []
        code = c['code']
        consts = c['consts']
        names = c['names']
        pos = 0
        while pos < len(code):
            o = ord(code[pos])
            if o >= 90:  # HAVE_ARGUMENT у 2.2 = 90
                arg = ord(code[pos + 1]) | (ord(code[pos + 2]) << 8)
                pos += 3
            else:
                arg = None
                pos += 1
            if o == op['SET_LINENO']:
                continue
            if o == op['LOAD_CONST']:
                if arg >= len(consts):
                    st = []
                    continue
                v = consts[arg]
                st.append(MARK if isinstance(v, dict) else v)
            elif o == op['BUILD_LIST']:
                if len(st) < arg:
                    st = []
                    continue
                items = st[-arg:]
                st = st[:-arg]
                st.append(MARK if has_mark(items) else list(items))
            elif o == op['BUILD_TUPLE']:
                if len(st) < arg:
                    st = []
                    continue
                items = st[-arg:]
                st = st[:-arg]
                st.append(MARK if has_mark(items) else tuple(items))
            elif o == op['BUILD_MAP']:
                st.append({})
            elif o == op['POP_TOP']:
                if st:
                    st.pop()
            elif o == op['DUP_TOP']:
                if st:
                    st.append(st[-1])
            elif o == op['ROT_TWO']:
                if len(st) >= 2:
                    st[-2], st[-1] = st[-1], st[-2]
            elif o == op['ROT_THREE']:
                if len(st) >= 3:
                    st[-3], st[-2], st[-1] = st[-1], st[-3], st[-2]
            elif o == op['BINARY_SUBSCR']:
                # контейнер[ключ]: значение вычисляется, восстанавливать нельзя
                st = st[:-2] + [MARK] if len(st) >= 2 else []
            elif o in (op['LOAD_NAME'], op['LOAD_GLOBAL'], op['LOAD_FAST'], op['LOAD_ATTR']):
                st.append(MARK)
            elif o == op['STORE_SUBSCR']:
                # стек: значение, контейнер, ключ
                if len(st) < 3:
                    st = []
                    continue
                val = st[-3]
                key = st[-1]
                if isinstance(key, str) and not has_mark(val):
                    out.append((c['name'], key, repr(val)))
                st = st[:-3]
            elif o in (op['STORE_NAME'], op['STORE_ATTR'], op['STORE_GLOBAL']):
                if arg >= len(names):
                    st = []
                    continue
                if len(st) >= 2 and st[-1] == MARK:
                    v = st[-2]
                elif len(st) >= 1:
                    v = st[-1]
                else:
                    st = []
                    continue
                if not has_mark(v):
                    out.append((c['name'], names[arg], repr(v)))
                st = []
            else:
                st = []
        for k in consts:
            if isinstance(k, dict):
                walk(k)
    walk(top)
    return out


def split_assign(line):
    """Разбить строку присваивания на (левая часть, правая) на уровне вложенности 0.
    Возвращает None для строк без простого '=' (сравнение, +=, и т.п.)."""
    depth = 0
    for i, ch in enumerate(line):
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif ch == '=' and depth == 0:
            prev = line[i - 1] if i > 0 else ''
            nxt = line[i + 1] if i + 1 < len(line) else ''
            if nxt == '=' or prev in '=+-*/%<>!&|':
                continue
            return line[:i], line[i + 1:]
    return None


def target_matches(lhs, attr):
    """Левая часть присваивания - name или ...['name'] / ...["name"]."""
    lhs = lhs.strip()
    if lhs == attr:
        return True
    if lhs.endswith(']'):
        depth = 0
        for i in range(len(lhs) - 1, -1, -1):
            if lhs[i] == ']':
                depth += 1
            elif lhs[i] == '[':
                depth -= 1
                if depth == 0:
                    key = lhs[i + 1:-1].strip()
                    return key in ("'%s'" % attr, '"%s"' % attr)
        return False
    return False


def apply_lits(text, lits):
    """Подстановка восстановленных литералов в нормализованный декомпилят.
    Пары: k-й code object обхода <-> k-й def в исходнике (порядок совпадает).
    Заменяем только строки присваиваний, правая часть которых - чистый литерал
    (ast.literal_eval), и значение литерала отличается от восстановленного.
    Так правки не задевают вычисляемые выражения."""
    import ast
    lines = text.split('\n')
    defs = []
    for i, l in enumerate(lines):
        m = re.match(r'^\s*def ([A-Za-z_][A-Za-z_0-9]*)\(', l)
        if m:
            defs.append((i, m.group(1)))
    regions = []
    for idx, (line_no, name) in enumerate(defs):
        indent = len(lines[line_no]) - len(lines[line_no].lstrip())
        end = len(lines)
        for j in range(line_no + 1, len(lines)):
            if lines[j].strip() == '':
                continue
            cur = len(lines[j]) - len(lines[j].lstrip())
            if cur <= indent:
                end = j
                break
        regions.append((name, line_no, end))
    used = {}
    changed = []
    for cname, attr, val in lits:
        if len(val) > 20000:
            continue
        want = eval(val)  # val - repr из 2.2; eval в py3 даёт то же значение
        if cname == '?':
            rng = (0, len(lines))
        else:
            k = used.get(cname, 0)
            occ = [r for r in regions if r[0] == cname]
            if k >= len(occ):
                continue
            used[cname] = k + 1
            rng = (occ[k][1], occ[k][2])
        for j in range(rng[0], rng[1]):
            sp = split_assign(lines[j])
            if not sp:
                continue
            lhs, rhs = sp[0].strip(), sp[1].strip()
            if not target_matches(lhs, attr):
                continue
            try:
                got = ast.literal_eval(rhs)
            except Exception:
                continue
            if got != want:
                indent = lines[j][:len(lines[j]) - len(lines[j].lstrip())]
                lines[j] = indent + lhs + ' = ' + val
                changed.append((cname, attr, rhs[:40], val[:40]))
            break
    return '\n'.join(lines), changed


def cmd_fixlits(module):
    """Показать подстановки литералов (декомпилят vs оригинал) и записать build/<module>.py."""
    pyc = game_pyc(module)
    lits = reconstruct_literals(pyc)
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    text = normalize(open(src, encoding='utf-8').read())
    new, changed = apply_lits(text, lits)
    dst = os.path.join(BUILD, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8', newline='\n').write(new)
    print('%s: литералов из оригинала %d, подставлено %d' % (module, len(lits), len(changed)))
    for cname, attr, decomp, val in changed:
        print('  %s.%s' % (cname, attr))
        print('     декомпилят: %s' % decomp)
        print('     оригинал : %s' % val)


def cmd_install(module, mode='pyc', probe=False, fns=False, trace=False, fixlits=True, fixints=True, fns_only=None):
    """Установка одного файла в игру. .pyc - собранный офлайн 2.2.3; .py - нормализованный
    исходник (движок скомпилирует его сам). Рядом кладётся только один артефакт.
    probe=True: собирать .pyc из зондированного исходника; fns=True: лог входа в функции;
    trace=True: sys.settrace; fixlits=True: подставить литералы из оригинального .pyc."""
    if module in SKIP:
        print('скип установки', module, '(декомпиллят пуст - остаётся оригинальный .pyc)')
        return
    py = build_py(module, probe, fns, trace, fixlits, fixints, fns_only)
    pyc = os.path.join(BUILD, module.replace('/', os.sep) + ('.probe.pyc' if probe else '.pyc'))
    out = os.path.join(GAME_SCRIPTS, module.replace('/', os.sep))
    orig = game_pyc(module)
    # запасная копия оригинала (один раз)
    if orig:
        bak = os.path.join(BACKUP, module.replace('/', os.sep) + '.pyc')
        os.makedirs(os.path.dirname(bak), exist_ok=True)
        if not os.path.exists(bak):
            shutil.copyfile(orig, bak)
    if mode == 'pyc':
        out_worker, rc = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
        print(out_worker)
        shutil.copyfile(pyc, out + '.pyc')
        os.utime(out + '.pyc', None)
        if os.path.exists(out + '.py'):
            os.remove(out + '.py')
        print('installed .pyc:', out + '.pyc')
    else:
        shutil.copyfile(py, out + '.py')
        os.utime(out + '.py', None)
        print('installed .py:', out + '.py')


def cmd_uninstall(module):
    """Вернуть оригинальный .pyc из backup_orig и убрать наши артефакты из игры."""
    out = os.path.join(GAME_SCRIPTS, module.replace('/', os.sep))
    bak = os.path.join(BACKUP, module.replace('/', os.sep) + '.pyc')
    orig = game_pyc(module)
    for ext in ('.py', '.pyc'):
        if os.path.exists(out + ext):
            os.remove(out + ext)
    src = bak if os.path.exists(bak) else orig
    if src and os.path.exists(src):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(src, out + '.pyc')
        print('restored', out + '.pyc', 'from', src)
    else:
        print('НЕТ оригинального .pyc для', module)


def cmd_fix_all(out_path=None):
    """Скан по всем модулям: сколько литералов подставляется из оригинального .pyc."""
    rows = []
    for m in all_modules():
        pyc = game_pyc(m)
        if pyc is None:
            continue
        src = os.path.join(SNAP, m.replace('/', os.sep) + '.py')
        if not os.path.exists(src):
            continue
        text = normalize(open(src, encoding='utf-8').read())
        try:
            lits = reconstruct_literals(pyc)
            new, changed = apply_lits(text, lits)
        except Exception as e:
            rows.append((m, 'FAIL', str(e)[:40]))
            continue
        rows.append((m, len(changed), len(lits)))
    rows.sort(key=lambda r: -(r[1] if isinstance(r[1], int) else 0))
    total = sum(r[1] for r in rows if r[1] != 'FAIL')
    print('модулей %d, всего подстановок литералов %d' % (len(rows), total))
    print('модули с подстановками:')
    for m, n, l in rows:
        if n and n != 'FAIL':
            print('  %3d %s' % (n, m))
    if out_path:
        open(out_path, 'w', encoding='utf-8').write('\n'.join('%s %s %s' % r for r in rows))
        print('полный список ->', out_path)


MIN_OLD = "def Initialize():\n    import units_base, spells, ui\n"
MIN_NEW = ("def Initialize():\n"
           "    import units_base, spells, ui, hd_res\n"
           "    w, h, bpp, f = hd_res.Read()\n"
           "    sw, sh = gamemodule.GetScreenSize()\n"
           "    if sw == w and sh == h:\n"
           "        h = h + 1\n"
           "    gamemodule.SetDisplayMode(w, h, bpp // 8, f)\n"
           "    hd_res.Prepare()\n"
           "    import hd_scale\n"
           "    hd_scale.HD_Init(w, h, f)\n"
           "    hd_scale.install()\n")


def cmd_install_min():
    """Шаг 1 частичного наката: ТОЛЬКО чтение hd.cfg и применение разрешения.
    Правится одна строка gamescript.Initialize; hd_res генерируется из hd.cfg.
    ui/ui, globalscript, cfg/cfg_default - оригиналы."""
    import hdpatch
    rows = hdpatch.read_hd_cfg()
    open(os.path.join(BUILD, 'hd_res.py'), 'w', encoding='utf-8', newline='\n').write(hdpatch.hd_res_src(rows))
    py = build_py('gamescript', fixlits=True, fixints=True)
    r = hdpatch.apply_patch(py, MIN_OLD, MIN_NEW)
    print('gamescript min-patch:', r)
    if r.startswith('FAIL'):
        return
    n = 0
    for m in ('gamescript', 'hd_res'):
        pyp = os.path.join(BUILD, m + '.py')
        pyc = os.path.join(BUILD, m + '.pyc')
        out, rc = run_worker('build', pyp.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
        if 'BUILD-OK' not in out:
            print('BAD BUILD', m, out.strip()[:80])
            return
        dst = os.path.join(GAME_SCRIPTS, m)
        shutil.copyfile(pyc, dst + '.pyc')
        os.utime(dst + '.pyc', None)
        print('installed', dst + '.pyc')
        n += 1
    shutil.copyfile(os.path.join(REPO_ROOT, 'hd.cfg'), os.path.join(GAME_ROOT, 'hd.cfg'))
    print('установлено %d: gamescript + hd_res (остальные - оригиналы)' % n)


def cmd_install_hd():
    """Обновлённый механизм патча: меняем ТОЛЬКО патченые модули (HD_MODULES),
    остальные .pyc в игре остаются оригинальными. Порядок:
    1) нормализуем декомпилят модулей из snapshot в build/ (fixlits/fixints),
    2) hdpatch.main() кладёт правки в build/*.py + генерирует build/hd_res.py,
    3) собираем .pyc (2.2.3) и ставим в игру только эти файлы,
    4) hd.cfg -> корень игры."""
    import hdpatch
    for m in HD_MODULES:
        if m == 'hd_res':
            continue
        py = build_py(m, fixlits=True, fixints=True)
        if py is None:
            print('НЕТ источника для', m)
            return
    hdpatch.main()
    n = 0
    for m in HD_MODULES:
        py = os.path.join(BUILD, m.replace('/', os.sep) + '.py')
        pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.pyc')
        out, rc = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
        if 'BUILD-OK' not in out:
            print('BAD BUILD', m, out.strip()[:80])
            continue
        dst = os.path.join(GAME_SCRIPTS, m.replace('/', os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(pyc, dst + '.pyc')
        os.utime(dst + '.pyc', None)
        if os.path.exists(dst + '.py'):
            os.remove(dst + '.py')
        print('installed', dst + '.pyc')
        n += 1
    cfg = os.path.join(REPO_ROOT, 'hd.cfg')
    if os.path.exists(cfg):
        shutil.copyfile(cfg, os.path.join(GAME_ROOT, 'hd.cfg'))
    print('установлено %d патченых .pyc (остальные - оригиналы)' % n)


def cmd_build_all(fixlits=True):
    """Собрать .pyc офлайн (2.2.3) для всех модулей из decompiled_fixed/.
    Все файлы уже проходят гейт компиляции; fixlits подставляет литералы из оригинала."""
    ok = 0
    bad = []
    for m in all_modules():
        py = build_py(m, fixlits=fixlits, fixints=True)
        if py is None:
            bad.append((m, 'no source'))
            continue
        pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.pyc')
        out, rc = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
        if 'BUILD-OK' in out:
            ok += 1
        else:
            bad.append((m, out.strip()[:60]))
    print('собрано %d .pyc, неудач %d' % (ok, len(bad)))
    for m, why in bad:
        print('  BAD', m, why)


def cmd_install_all(fixlits=True):
    """Поставить собранные .pyc в игру ( originals бэкапятся в backup_orig/ ).
    SKIP не устанавливается: декомпиллят пуст, оригинал остаётся."""
    n = 0
    for m in all_modules():
        if m in SKIP:
            continue
        pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.pyc')
        if not os.path.exists(pyc):
            continue
        orig = game_pyc(m)
        out = os.path.join(GAME_SCRIPTS, m.replace('/', os.sep))
        if orig:
            bak = os.path.join(BACKUP, m.replace('/', os.sep) + '.pyc')
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            if not os.path.exists(bak):
                shutil.copyfile(orig, bak)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(pyc, out + '.pyc')
        os.utime(out + '.pyc', None)
        if os.path.exists(out + '.py'):
            os.remove(out + '.py')
        n += 1
    # модуль-конфиг hd_res: генерируется hdpatch.py, в манифесте декомпиллятов нет
    pyc = os.path.join(BUILD, 'hd_res.pyc')
    if os.path.exists(pyc):
        out = os.path.join(GAME_SCRIPTS, 'hd_res')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(pyc, out + '.pyc')
        os.utime(out + '.pyc', None)
        if os.path.exists(out + '.py'):
            os.remove(out + '.py')
        n += 1
    # hd.cfg кладём в корень игры: движковый open('hd.cfg') читает loose-файл в рантайме
    cfg = os.path.join(REPO_ROOT, 'hd.cfg')
    if os.path.exists(cfg):
        shutil.copyfile(cfg, os.path.join(GAME_ROOT, 'hd.cfg'))
    print('установлено %d .pyc' % n)


def cmd_restore_all():
    """Вернуть все оригинальные .pyc из backup_orig/ (полный откат)."""
    n = 0
    missing = []
    for m in all_modules():
        bak = os.path.join(BACKUP, m.replace('/', os.sep) + '.pyc')
        out = os.path.join(GAME_SCRIPTS, m.replace('/', os.sep))
        if os.path.exists(bak):
            for ext in ('.py', '.pyc'):
                if os.path.exists(out + ext):
                    os.remove(out + ext)
            shutil.copyfile(bak, out + '.pyc')
            n += 1
        else:
            missing.append(m)
    # hd_res генерируется патчем; в оригинальной игре его нет
    for ext in ('.py', '.pyc'):
        p = os.path.join(GAME_SCRIPTS, 'hd_res' + ext)
        if os.path.exists(p):
            os.remove(p)
    print('восстановлено %d, без бэкапа %d' % (n, len(missing)))
    for m in missing:
        print('  NO BACKUP', m)


def cmd_install_list(path, restore_first=True):
    """Установить .pyc из build/ только для модулей из списка (по одному на строку)."""
    mods = [l.strip() for l in open(path, encoding='utf-8') if l.strip()]
    if restore_first:
        cmd_restore_all()
    n = 0
    for m in mods:
        if m in SKIP:
            print('  скип', m)
            continue
        pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.pyc')
        if not os.path.exists(pyc):
            print('  пропущено', m)
            continue
        out = os.path.join(GAME_SCRIPTS, m.replace('/', os.sep))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(pyc, out + '.pyc')
        os.utime(out + '.pyc', None)
        if os.path.exists(out + '.py'):
            os.remove(out + '.py')
        n += 1
    print('установлено %d модулей из %d' % (n, len(mods)))


def tree_consts(pyc_path):
    """Список co_consts каждого кодового объекта в порядке обхода дерева (тот же порядок,
    что и у обхода воркера vdiff)."""
    import pyc_scan
    d = open(pyc_path, 'rb').read()
    top, i = pyc_scan.read(d, 8)
    acc = []
    def walk(c):
        acc.append(c['consts'])
        for k in c['consts']:
            if isinstance(k, dict):
                walk(k)
    walk(top)
    return acc


def decode_hex(h):
    """repr константы из 2.2 (байты) -> значение Python 3."""
    raw = bytes.fromhex(h)
    txt = raw.decode('cp1251', 'replace')
    return eval(txt, {'__builtins__': {}}, {})


import vdiff_mod
vdiff_mod.pb = __import__('pycbuild')
cmd_vdiff = vdiff_mod.cmd_vdiff
cmd_vdiff_all = vdiff_mod.cmd_vdiff_all


def py2_literal(v):
    """Значение py3 -> литерал в стиле Python 2.2 (байтовые строки с экранированием \\x)."""
    if isinstance(v, bool):
        return 'True' if v else 'False'
    if isinstance(v, str):
        b = v.encode('cp1251', 'replace')
        out = []
        for c in b:
            if c == 0x27:
                out.append("\\'")
            elif c == 0x5c:
                out.append('\\\\')
            elif c == 0x09:
                out.append('\\t')
            elif 32 <= c < 127:
                out.append(chr(c))
            else:
                out.append('\\x%02x' % c)
        return "'" + ''.join(out) + "'"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, list):
        return '[' + ', '.join(py2_literal(x) for x in v) + ']'
    if isinstance(v, tuple):
        if len(v) == 1:
            return '(' + py2_literal(v[0]) + ',)'
        return '(' + ', '.join(py2_literal(x) for x in v) + ')'
    if isinstance(v, dict):
        return '{' + ', '.join('%s: %s' % (py2_literal(k), py2_literal(x)) for k, x in v.items()) + '}'
    return repr(v)


def source_regions(text):
    """Области кодовых объектов: def/class по порядку появления (совпадают с порядком обхода дерева).
    Возвращает [(имя, начало, конец)], область модуля отдельно - (None, 0, len(lines))."""
    lines = text.split('\n')
    regions = []
    for i, l in enumerate(lines):
        m = re.match(r'^\s*(?:def|class)\s+([A-Za-z_][A-Za-z_0-9]*)\b', l)
        if m:
            indent = len(l) - len(l.lstrip())
            end = len(lines)
            for j in range(i + 1, len(lines)):
                if lines[j].strip() == '':
                    continue
                cur = len(lines[j]) - len(lines[j].lstrip())
                if cur <= indent:
                    end = j
                    break
            regions.append((m.group(1), i, end))
    return regions


def fixints_text(module, text, dst_path, max_passes=3):
    """Декомпилят местами пишет ИНДЕКС co_consts как ЦЕЛОЕ (LOAD_CONST 364 вместо
    './data/.../bug01.wav'). Сопоставляем байткод оригинала (A) с компилятом нашего .py (B):
    если A даёт значение V, а B - целое K, и consts_оригинала[K] == V, то в исходнике K
    заменяем на литерал V. Повторяем, пока замены не перестанут находиться.
    dst_path - файл, в который пишется текст для компиляции воркером."""
    pyc = game_pyc(module)
    consts_tree = tree_consts(pyc)
    total = 0
    log = []
    for _ in range(max_passes):
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        open(dst_path, 'w', encoding='utf-8', newline='\n').write(text)
        out, rc = run_worker('litfix', pyc.replace(os.sep, '/'), dst_path.replace(os.sep, '/'))
        trees = {'A': [], 'B': []}
        for line in out.split('\n'):
            p = line.split(None, 3)
            if len(p) < 4 or p[0] != 'TOKEN':
                continue
            side, idx = p[1], int(p[2])
            tok = re.sub(r'^\d+\s+', '', p[3].strip())
            while len(trees[side]) <= idx:
                trees[side].append([None, []])
            trees[side][idx][1].append(tok)
        lines = text.split('\n')
        cnt = 0
        import difflib
        n = min(len(trees['A']), len(trees['B']))
        for i in range(n):
            name = trees['A'][i][0]
            ta = trees['A'][i][1]
            tb = trees['B'][i][1]
            # B-токены без SLINE + номер строки исходника для каждого токена
            b2 = []
            line_of = []
            cur = None
            for t in tb:
                if t.startswith('SLINE '):
                    cur = int(t[len('SLINE '):])
                else:
                    b2.append(t)
                    line_of.append(cur)
            cs = consts_tree[i]
            sm = difflib.SequenceMatcher(None, ta, b2, autojunk=False)
            for tag, a1, a2, b1, b2i in sm.get_opcodes():
                if tag == 'equal':
                    continue
                for q in range(min(a2 - a1, b2i - b1)):
                    x = ta[a1 + q]
                    y = b2[b1 + q]
                    if not (x.startswith('LOAD_CONST HEX ') and y.startswith('LOAD_CONST HEX ')):
                        continue
                    val = decode_hex(x[len('LOAD_CONST HEX '):])
                    kv = decode_hex(y[len('LOAD_CONST HEX '):])
                    if not isinstance(kv, int):
                        continue
                    K = kv
                    if K >= len(cs) or val != cs[K]:
                        continue
                    # дефект "индекс вместо значения" только когда оригинал грузит
                    # строку/список/кортеж/словарь. Числовые литералы декомпиллят воспроизводит
                    # верно, и замена int на int даёт ложные срабатывания (ui/ui: return 0).
                    if not isinstance(val, (str, list, tuple, dict)):
                        continue
                    if K == val:
                        continue
                    ln = line_of[b1 + q]
                    if ln is None or ln < 1 or ln > len(lines):
                        continue
                    pat = re.compile(r'(?<![\w.])' + str(K) + r'(?![\w.])')
                    m = pat.search(lines[ln - 1])
                    if not m:
                        continue
                    # число сразу после арифметического оператора = настоящий int-литерал
                    # (например `270 + 0 * 43`), менять нельзя
                    pre = lines[ln - 1][:m.start()].rstrip()
                    if pre and pre[-1] in '+-*/%&|^<>=~':
                        continue
                    lines[ln - 1] = lines[ln - 1][:m.start()] + py2_literal(val) + lines[ln - 1][m.end():]
                    cnt += 1
                    log.append((name, K, str(val)[:40]))
        text = '\n'.join(lines)
        total += cnt
        if cnt == 0:
            break
    return text, total, log


def cmd_fixints(module):
    src = os.path.join(SNAP, module.replace('/', os.sep) + '.py')
    text = normalize(open(src, encoding='utf-8').read())
    dst = os.path.join(BUILD, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    text, total, log = fixints_text(module, text, dst)
    open(dst, 'w', encoding='utf-8', newline='\n').write(text)
    print('%s: целые-индексы заменены %d' % (module, total))
    for name, K, val in log[:12]:
        print('  %s: %s -> %s' % (name, K, val))


def cmd_install_fns_all(regex):
    """Установить .pyc по всему дереву с логом входа в функции, чьи имена подходят regex.
    Позволяет увидеть последний вызов перед отказом движка."""
    n = 0
    for m in all_modules():
        if m in SKIP:
            continue
        cmd_install(m, 'pyc', fns=True, fns_only=regex)
        n += 1
    print('установлено %d .pyc с зонадом функций regex=%s' % (n, regex))


def cmd_install_probe_all():
    """Установить .pyc с зондом 'MOD <module> start' / 'module-done' по всему дереву:
    последний 'start' без 'module-done' = модуль, импорт которого валится."""
    ok, plain = 0, 0
    for m in all_modules():
        if m in SKIP:
            continue
        py = build_py(m, probe=True, fixints=True)
        pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.probe.pyc')
        out, rc = run_worker('compile', py.replace(os.sep, '/'))
        if 'COMPILE-OK' in out:
            out2, rc2 = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
            if 'BUILD-OK' in out2:
                shutil.copyfile(pyc, os.path.join(GAME_SCRIPTS, m.replace('/', os.sep)) + '.pyc')
                ok += 1
                continue
        # зонд не компилируется -> ставим чистый .pyc
        cmd_install(m, 'pyc', fixints=True)
        plain += 1
    print('зонд установлен на %d модулях, чистых %d' % (ok, plain))


def fnames_diff(module):
    """Сравнить мультимножества имён кодовых объектов оригинала и нашего компилята."""
    import collections
    pyc = game_pyc(module)
    py = os.path.join(BUILD, module.replace('/', os.sep) + '.py')
    if not pyc or not os.path.exists(py):
        return None
    out, rc = run_worker('fnames', pyc.replace(os.sep, '/'), py.replace(os.sep, '/'))
    a, b = [], []
    for line in out.split('\n'):
        p = line.split(None, 2)
        if len(p) < 3 or p[0] != 'FNAMES':
            continue
        (a if p[1] == 'A' else b).append(p[2])
    ca = collections.Counter(a); cb = collections.Counter(b)
    missing = ca - cb
    extra = cb - ca
    if not missing and not extra:
        return []
    rows = []
    for name, n in missing.items():
        rows.append(('missing', name, n))
    for name, n in extra.items():
        rows.append(('extra', name, n))
    return rows


def cmd_fnames(module):
    rows = fnames_diff(module)
    if rows is None:
        print('нет оригинала или build-файла для', module)
        return
    if not rows:
        print('%s: имена кодовых объектов совпадают' % module)
        return
    print('%s: расхождения имён кодовых объектов' % module)
    for kind, name, n in rows:
        print('  %-8s %s x%d' % (kind, name, n))


def cmd_fnames_all(out_path=None):
    """Скан по всем модулям: где имена кодовых объектов оригинала и нашего компилята не совпадают."""
    bad = []
    for m in all_modules():
        rows = fnames_diff(m)
        if rows is None:
            continue
        if rows:
            bad.append((m, rows))
    print('модулей с расхождениями имён: %d' % len(bad))
    for m, rows in bad:
        print(' %s: %s' % (m, ', '.join('%s %s' % (k, n) for k, n, c in rows)))
    if out_path:
        open(out_path, 'w', encoding='utf-8').write(
            '\n'.join('%s %s' % (m, ';'.join('%s %s' % (k, n) for k, n, c in rows)) for m, rows in bad))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    args = sys.argv[2:]
    if cmd == 'normalize':
        cmd_normalize(args[0])
    elif cmd == 'gate':
        cmd_gate()
    elif cmd == 'build':
        cmd_build(args[0])
    elif cmd == 'parity':
        cmd_parity(args[0], verbose='--verbose' in args, strict='--strict' in args)
    elif cmd == 'gate-all':
        cmd_gate_all()
    elif cmd == 'sim-all':
        cmd_sim_all(args[0] if args else None)
    elif cmd == 'fixlits':
        cmd_fixlits(args[0])
    elif cmd == 'fix-all':
        cmd_fix_all(args[0] if args else None)
    elif cmd == 'vdiff':
        cmd_vdiff(args[0])
    elif cmd == 'fixints':
        cmd_fixints(args[0])
    elif cmd == 'build-all':
        cmd_build_all()
    elif cmd == 'hd-all':
        import hdpatch
        cmd_build_all()
        hdpatch.main()
        # компилируем уже пропатченные build/*.py (build_py regenerирует из snapshot,
        # поэтому компилируем файл напрямую воркером)
        for m in HD_MODULES:
            py = os.path.join(BUILD, m.replace('/', os.sep) + '.py')
            pyc = os.path.join(BUILD, m.replace('/', os.sep) + '.pyc')
            out, rc = run_worker('build', py.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
            print(out)
    elif cmd == 'install-min':
        cmd_install_min()
    elif cmd == 'install-hd':
        cmd_install_hd()
    elif cmd == 'install-all':
        cmd_install_all()
    elif cmd == 'restore-all':
        cmd_restore_all()
    elif cmd == 'install-list':
        cmd_install_list(args[0])
    elif cmd == 'install-fns':
        cmd_install_fns_all(args[0])
    elif cmd == 'install-probe-all':
        cmd_install_probe_all()
    elif cmd == 'fnames':
        cmd_fnames(args[0])
    elif cmd == 'fnames-all':
        cmd_fnames_all(args[0] if args else None)
    elif cmd == 'vdiff-all':
        cmd_vdiff_all(args[0] if args else None)
    elif cmd == 'install':
        mode = 'py' if '--py' in args else 'pyc'
        cmd_install(args[0], mode, probe='--probe' in args, fns='--fns' in args,
                    trace='--trace' in args, fixlits='--fixlits' in args)
    elif cmd == 'uninstall':
        cmd_uninstall(args[0])
