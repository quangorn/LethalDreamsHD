#!/usr/bin/env python3
"""Reverse converter PNG -> Lethal Dreams .btf.

Strategy: the original .btf is the template (TLV header fields are copied verbatim),
the trailing pixel blob is replaced with pixel data read from a PNG.
Blob layout per class (confirmed from unpacking):
  bpp class 3 (blob == w*h*3): rows top-down, interleaved R,G,B
  bpp class 4 (blob == w*h*4): rows top-down, interleaved R,G,B,A (stored order)
The PNG must match the template's w,h. Mirrored textures: pass a mirrored PNG.

Usage: pack_btf.py in.png template.btf -o out.btf
No PIL, stdlib only.
"""
# Заголовок проекта (2026-11). Реальный упакoщик .btf; логика не менялась.
# Функции и их потребители в репозитории:
#   png_decode(path)          -> (w,h,RGBA-строки) из PNG (colortype 0/2/4/6).
#   build_blob(rows,w,h,bpp)   -> пиксельный blob 3 или 4 bpp из RGBA-строк.
#   find_offsets(...)          -> смещения полей w,h и blob_len, когда канонного
#                                 заголовка (canon) нет; зовёт pack_one.
#   pack_one(png,template,out) -> главный вход: tools/fonts/gen_font4x.py строит
#                                 4x-сайдкары листов шрифтов из апскейленных PNG;
#                                 CLI pack_btf.py in.png template.btf -o out.btf.
import sys, os, zlib, struct, binascii, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unpack_btf import parse_btf, u32

def png_decode(path):
    d = open(path,'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos = 8
    w = h = ct = None
    idat = b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        data = d[pos+8:pos+8+ln]
        pos += 12+ln
        if typ == b'IHDR':
            w, h = struct.unpack('>II', data[:8])
            ct = data[9]
        elif typ == b'IDAT':
            idat += data
    bppch = {0:1, 2:3, 3:1, 4:2, 6:4}[ct]
    stride = w*bppch
    raw = zlib.decompress(idat)
    prev = bytearray(w*bppch)
    rows = []
    i = 0
    for y in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if f == 1:
            for x in range(bppch, stride):
                line[x] = (line[x] + line[x-bppch]) & 0xff
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xff
        elif f == 3:
            for x in range(stride):
                a = line[x-bppch] if x >= bppch else 0
                line[x] = (line[x] + (a + prev[x])//2) & 0xff
        elif f == 4:
            for x in range(stride):
                a = line[x-bppch] if x >= bppch else 0
                b = prev[x]
                c = prev[x-bppch] if x >= bppch else 0
                p = a+b-c
                q = abs(p-a); r2 = abs(p-b); s2 = abs(p-c)
                pr = a if (q<=r2 and q<=s2) else (b if r2<=s2 else c)
                line[x] = (line[x] + pr) & 0xff
        rows.append(line)
        prev = line
    # normalize to RGBA rows
    out = []
    for line in rows:
        if ct == 6:
            out.append(bytes(line))
        elif ct == 2:
            r = bytearray(w*4)
            r[0::4] = line[0::3]; r[1::4] = line[1::3]; r[2::4] = line[2::3]; r[3::4] = b'\xff'*w
            out.append(bytes(r))
        elif ct == 0:
            r = bytearray(w*4)
            g = line[0::1]
            r[0::4] = g; r[1::4] = g; r[2::4] = g; r[3::4] = b'\xff'*w
            out.append(bytes(r))
        elif ct == 4:
            r = bytearray(w*4)
            r[0::4] = line[0::2]; r[1::4] = line[0::2]; r[2::4] = line[0::2]; r[3::4] = line[1::2]
            out.append(bytes(r))
        else:  # colortype 3 palette is not handled here
            raise ValueError('PNG colortype %d not supported' % ct)
    return w, h, out

def build_blob(rows, w, h, bpp):
    b = bytearray()
    for r in rows:
        if bpp == 4:
            b += r
        else:
            rgb = bytearray(w*3)
            rgb[0::3] = r[0::4]; rgb[1::3] = r[1::4]; rgb[2::3] = r[2::4]
            b += rgb
    return bytes(b)

def find_offsets(payload, w0, h0, blob_len):
    """Locate byte offsets to patch: w,h pair and blob_len field."""
    plen = len(payload)
    # w,h pair: first j with (u32(j), u32(j+4)) == (w0,h0)
    w_off = h_off = None
    for j in range(0, plen-7):
        if u32(payload, j) == w0 and u32(payload, j+4) == h0:
            w_off, h_off = j, j+4
            break
    # blob_len field: last i with i+4 == plen - u32(i)
    b_off = None
    for i in range(min(plen, 4096)-4, 3, -1):
        v = u32(payload, i)
        if v > 0 and i + 4 == plen - v:
            b_off = i
            break
    return w_off, h_off, b_off

def pack_one(png_path, template_path, outpath):
    d = parse_btf(open(template_path, 'rb').read())
    cd = d.get('canon')
    w0, h0, bpp, P = d['dims'][0]
    blob_len = d['blob_len']
    payload = d['payload']
    pw, ph, rows = png_decode(png_path)
    if (pw, ph) == (w0, h0):
        w, h = w0, h0
    else:
        w, h = pw, ph  # upscaled: take size from PNG
    new_blob = build_blob(rows, w, h, bpp)
    # engine texture limit seen in header fields: 33554432 bytes per texture
    if len(new_blob) > 33554432:
        print('WARNING: blob %d bytes exceeds engine limit 33554432' % len(new_blob))
    if cd is not None:
        # canonical header fields: w,h at the first two u32 after the name field,
        # blob_len at the last header u32 (hdr_end - 4)
        w_off, h_off, b_off = cd[4], cd[4] + 4, cd[5] - 4
        hdr = bytearray(payload[:cd[5]])
    else:
        w_off, h_off, b_off = find_offsets(payload, w0, h0, blob_len)
        hdr = bytearray(payload[:len(payload)-blob_len])
    if w_off is None:
        raise ValueError('cannot locate w,h fields in template header')
    struct.pack_into('<I', hdr, w_off, w)
    struct.pack_into('<I', hdr, h_off, h)
    if b_off is not None:
        struct.pack_into('<I', hdr, b_off, len(new_blob))
    # 't' field: u32 == plen - 16 (total payload size minus leading fields)
    old_plen = len(payload)
    new_plen = len(hdr) + len(new_blob)
    for i in range(0, min(len(hdr), 512)-3):
        if u32(hdr, i) == old_plen - 16:
            struct.pack_into('<I', hdr, i, new_plen - 16)
    new_payload = bytes(hdr) + new_blob
    # rebuild container from original template bytes
    orig = open(template_path, 'rb').read()
    if orig.startswith(b'LDPACKER'):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = orig.find(m, 8)
            if i >= 0:
                zi = i
                break
        comp = zlib.compress(new_payload, 9)
        head = bytearray(orig[:zi])
        old_clen = len(orig) - zi
        for i in range(8, zi-3):
            v = u32(head, i)
            if v == old_plen:
                struct.pack_into('<I', head, i, new_plen)
            elif v == old_clen:
                struct.pack_into('<I', head, i, len(comp))
        out = bytes(head) + comp
    else:
        out = new_payload
    open(outpath, 'wb').write(out)
    print('packed %s (template %dx%d -> output %dx%d bpp=%d blob=%d) from %s -> %s' % (
        os.path.basename(template_path), w0, h0, w, h, bpp, len(new_blob),
        os.path.basename(png_path), outpath))

def main():
    ap = argparse.ArgumentParser(description='PNG -> Lethal Dreams .btf packer')
    ap.add_argument('png', help='input PNG (same w,h as template)')
    ap.add_argument('template', help='original .btf providing header fields')
    ap.add_argument('-o', dest='out', required=True, help='output .btf')
    a = ap.parse_args()
    pack_one(a.png, a.template, a.out)

if __name__ == '__main__':
    main()
