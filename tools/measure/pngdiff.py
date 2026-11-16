# pngdiff.py (tools/measure/) - построчный diff двух PNG (RGB): максимум абсолютной разницы и счётчик пикселей выше допуска.
# Декодер PNG: 8-bit, глубина цвета/альфа, фильтры 0..4 (None/Sub/Up/Avg/Paeth).
#   python3 tools/measure/pngdiff.py <a.png> <b.png> [tol]
import os, sys, zlib, struct

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402


def decode_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'не PNG'
    pos = 8
    w = h = bd = ct = inter = 0
    plte = None
    idat = b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b'IHDR':
            w, h, bd, ct, comp, filt, inter = struct.unpack('>IIBBBBB', data)
            assert bd == 8 and inter == 0, 'поддержка только 8-bit non-interlaced'
        elif typ == b'PLTE':
            plte = data
        elif typ == b'IDAT':
            idat += data
        elif typ == b'IEND':
            break
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = w * ch
    out = [bytearray(stride) for _ in range(h)]
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        f = raw[i]
        i += 1
        cur = bytearray(raw[i:i + stride])
        i += stride
        for x in range(ch, stride):
            a = cur[x - ch]
            b = prev[x]
            c = prev[x - ch]
            if f == 0:
                pass
            elif f == 1:
                cur[x] = (cur[x] + a) & 0xFF
            elif f == 2:
                cur[x] = (cur[x] + b) & 0xFF
            elif f == 3:
                cur[x] = (cur[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[x] = (cur[x] + pr) & 0xFF
        out[y] = cur
        prev = cur
    if ct == 3:  # indexed -> RGB
        rgb = []
        for y in range(h):
            row = out[y]
            rgb.append(bytearray())
            for x in range(w):
                idx = row[x]
                rgb[y] += plte[idx * 3:idx * 3 + 3]
        return w, h, rgb
    # grayscale/alpha -> развёртка в RGB
    rgb = []
    for y in range(h):
        row = out[y]
        line = bytearray()
        for x in range(w):
            if ct == 0:
                g = row[x]
                line += bytes([g, g, g])
            elif ct == 2:
                line += bytes(row[x * 3:x * 3 + 3])
            elif ct == 4:
                g = row[x * 2]
                line += bytes([g, g, g])
            else:
                line += bytes(row[x * 4:x * 4 + 3])
        rgb.append(line)
    return w, h, rgb


def main():
    a, b = sys.argv[1], sys.argv[2]
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    wa, ha, ra = decode_png(a)
    wb, hb, rb = decode_png(b)
    if (wa, ha) != (wb, hb):
        print('размеры разные: %dx%d vs %dx%d' % (wa, ha, wb, hb))
        return
    mx = 0
    over = 0
    sumd = 0
    for y in range(ha):
        A = ra[y]; B = rb[y]
        for x in range(wa * 3):
            dd = abs(A[x] - B[x])
            if dd > mx:
                mx = dd
            sumd += dd
            if dd > tol:
                over += 1
    print('%dx%d  maxDelta=%d  pixels_over_tol(%d)=%d  meanDelta=%.4f' %
          (wa, ha, mx, tol, over, sumd / float(wa * ha * 3)))


if __name__ == '__main__':
    main()
