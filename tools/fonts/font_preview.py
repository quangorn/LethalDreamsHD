# font_preview.py - офлайн-превью шрифтового листа: строку текста собираем из глифов
# листа по .fdf (кодировка текста = cp1251, как в игре), масштабируем xN, рисуем на чёрном.
# Позволяет глазами сравнить родной лист и сайдкар _169 БЕЗ запуска игры.
#
#   python3 tools/fonts/font_preview.py -fdf1 A.fdf -btf1 A.btf -fdf2 B.fdf -btf2 B.btf -text "Настройки" -o out.png [-n 3]
import argparse
import binascii
import os
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u  # noqa: E402


def load(p):
    d = u.parse_btf(open(p, 'rb').read())
    w, h, bpp, _ = d['dims'][0]
    return w, h, u.decode32(d['blob'], w, h, 'RGBA', False)


def fdf(p):
    raw = open(p, 'rb').read().decode('latin-1')
    L = raw.replace('\r\n', '\n').split('\n')
    pitch = int(L[0].split()[1])
    m = {}
    for l in L[1:]:
        if not l.strip():
            continue
        t = l.split()
        tok = t[0]
        code = int(tok[1:]) if tok.startswith('%') else ord(tok)
        m[code] = (int(t[1]), int(t[2]), int(t[3]))
    return pitch, m


def strip(rows, pw, pitch, m, codes, n, bg):
    """Строка глифов: адванс из .fdf, ink из листа. Возвращает список RGB-строк."""
    cells = [m[c] for c in codes if c in m]
    W = sum(a for _, _, a in cells) * n
    out = []
    for yy in range(pitch):
        line = bytearray()
        for x, y, adv in cells:
            src = rows[y + yy]
            for xx in range(adv):
                i = x + xx
                a = src[i * 4 + 3] if i < pw else 0
                if a:
                    v = int(((src[i * 4] + src[i * 4 + 1] + src[i * 4 + 2]) / 3.0) * a / 255.0)
                else:
                    v = 0
                px = bytes([max(bg, v), max(bg, v), max(bg, v)])
                for q in range(n):
                    line += px
            # пустая часть адванса
        out.append(bytes(line))
    return out


def png_write_rgb(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(prog='font_preview.py')
    ap.add_argument('-fdf1', required=True)
    ap.add_argument('-btf1', required=True)
    ap.add_argument('-fdf2', required=True)
    ap.add_argument('-btf2', required=True)
    ap.add_argument('-text', required=True)
    ap.add_argument('-o', required=True)
    ap.add_argument('-n', type=int, default=3)
    a = ap.parse_args()
    codes = list(bytearray(a.text.encode('cp1251')))
    w1, h1, r1 = load(a.btf1); p1, m1 = fdf(a.fdf1)
    w2, h2, r2 = load(a.btf2); p2, m2 = fdf(a.fdf2)
    A = strip(r1, w1, p1, m1, codes, a.n, 30)
    B = strip(r2, w2, p2, m2, codes, a.n, 30)
    sep = bytes([130, 130, 130]) * a.n
    out = []
    for i in range(max(len(A), len(B))):
        a_ = A[i] if i < len(A) else bytes([0, 0, 0]) * (len(A[0]) // 3)
        b_ = B[i] if i < len(B) else bytes([0, 0, 0]) * (len(B[0]) // 3)
        out.append(a_ + sep + b_)
    png_write_rgb(a.o, len(out[0]) // 3, len(out), out)
    print('%s  %dx%d  left=native right=new  text=%r codes=%s' %
          (a.o, len(out[0]) // 3, len(out), a.text, [hex(c) for c in codes]))


if __name__ == '__main__':
    main()
