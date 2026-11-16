#!/usr/bin/env python3
"""
Парсер .pyc (Python 2.2, marshal v1) для скриптов Lethal Dreams.
Структура pyc: magic(4) + mtime(4) + marshal(top code object).

Layout code object по Python 2.2 marshal.c:
  'c', short argcount, short nlocals, short stacksize, short flags,
  code(str), consts, names, varnames, freevars, cellvars,
  filename(str), name(str), short firstlineno, lnotab(str)
Типы marshal (Python 2.2): '0' null, 'N' None, 'S' StopIter, '.' Ellipsis,
'i' int, 'I' int64, 'l' long, 'f' float, 'x' complex, 's' str, '(' tuple,
'[' list, '{' dict, 'c' code, 'u' unicode.

Файл лежит в tools/pyc/ (рядом с pycbuild.py); путь игры в выводе берётся из
install_dir (config.toml).
"""
import sys, struct, os, time

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

def r_short(d, i):
    x = d[i] | (d[i+1] << 8)
    x |= -(x & 0x8000)
    return x, i + 2

def r_long(d, i):
    x = int.from_bytes(d[i:i+4], 'little')
    x |= -(x & 0x80000000)
    return x, i + 4

def read(d, i):
    t = d[i]; i += 1
    if t == 0x30: return None, i           # '0'
    if t == 0x4e: return None, i           # 'N'
    if t == 0x53: return 'StopIteration', i
    if t == 0x2e: return 'Ellipsis', i
    if t == 0x69:                          # 'i' int
        v, i = r_long(d, i); return v, i
    if t == 0x49:                          # 'I' int64
        lo, i = r_long(d, i); hi, i = r_long(d, i)
        return (hi << 32) | (lo & 0xFFFFFFFF), i
    if t == 0x6c:                          # 'l' long
        n, i = r_long(d, i)
        size = abs(n); digits = []
        for _ in range(size):
            v, i = r_short(d, i); digits.append(v)
        val = 0
        for dgt in reversed(digits): val = val * 65536 + dgt
        return (-val if n < 0 else val), i
    if t == 0x66:                          # 'f' float (repr string)
        n = d[i]; i += 1
        s = d[i:i+n]; i += n
        return float(s.decode('ascii', 'replace')), i
    if t == 0x78:                          # 'x' complex
        n = d[i]; i += 1; re_ = d[i:i+n].decode(); i += n
        n = d[i]; i += 1; im = d[i:i+n].decode(); i += n
        return complex(float(re_), float(im)), i
    if t in (0x73, 0x75):                  # 's' str / 'u' unicode
        n, i = r_long(d, i)
        raw = d[i:i+n]; i += n
        return raw.decode('utf-8', 'replace'), i
    if t == 0x28:                          # '(' tuple
        n, i = r_long(d, i)
        out = []
        for _ in range(n):
            v, i = read(d, i); out.append(v)
        return tuple(out), i
    if t == 0x5b:                          # '[' list
        n, i = r_long(d, i)
        out = []
        for _ in range(n):
            v, i = read(d, i); out.append(v)
        return out, i
    if t == 0x7b:                          # '{' dict
        out = {}
        while True:
            k, i = read(d, i)
            if k is None: break
            v, i = read(d, i)
            out[k] = v
        return out, i
    if t == 0x63:                          # 'c' code
        argcount, i = r_short(d, i)
        nlocals, i = r_short(d, i)
        stacksize, i = r_short(d, i)
        flags, i = r_short(d, i)
        code, i = read(d, i)
        consts, i = read(d, i)
        names, i = read(d, i)
        varnames, i = read(d, i)
        freevars, i = read(d, i)
        cellvars, i = read(d, i)
        filename, i = read(d, i)
        name, i = read(d, i)
        firstlineno, i = r_short(d, i)
        lnotab, i = read(d, i)
        return {'argcount': argcount, 'nlocals': nlocals, 'stacksize': stacksize,
                'flags': flags, 'code': code, 'consts': consts, 'names': names,
                'varnames': varnames, 'freevars': freevars, 'cellvars': cellvars,
                'filename': filename, 'name': name, 'firstlineno': firstlineno,
                'lnotab': lnotab}, i
    raise ValueError('unknown marshal type 0x%02x at %d' % (t, i - 1))

def walk(code, depth=0, out=None):
    out = out if out is not None else []
    pad = '  ' * depth
    consts = [c for c in code['consts'] if not isinstance(c, dict)]
    out.append('%sdef %s(%d args) [%s]  # %s line %d' % (
        pad, code['name'], code['argcount'], ', '.join(code['varnames']),
        code['filename'], code['firstlineno']))
    out.append('%s  names : %s' % (pad, list(code['names'])))
    out.append('%s  consts: %s' % (pad, consts))
    for c in code['consts']:
        if isinstance(c, dict):
            walk(c, depth + 1, out)
    return out

def scan(path, show_code=False):
    d = open(path, 'rb').read()
    magic = struct.unpack('<H', d[:2])[0]
    mtime = struct.unpack('<I', d[4:8])[0]
    top, i = read(d, 8)
    prefix = paths.install_dir() + '/'  # путь игры: вырезаем из вывода
    print('== %s  magic=0x%04X  mtime=%s  bytes=%d' % (
        path.replace(prefix, ''), magic,
        time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(mtime)), len(d)))
    print('\n'.join(walk(top)))
    if show_code:
        print('  code bytes (first 120):', list(top['code'][:120]))

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--code']
    for p in args:
        scan(p, show_code='--code' in sys.argv)
