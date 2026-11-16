#!/usr/bin/env python3
"""Offline unpacker for Lethal Dreams .btf textures -> .png / .bmp.

Format (verified from header dumps):
  file = "LDPACKER" + small header + zlib stream, or raw 'mdf '
  payload = "mdf " + TLV fields + trailing pixel blob
  blob_len: u32 field at byte offset i with i+4 == plen - blob_len (last match)
  w = u32 at offset j, h = u32 at j+4, bpp_field = u32 at j+8
    bpp_field 0 -> 24-bit, blob = w*h*3
    bpp_field 4 -> 32-bit, blob = w*h*4
    bpp_field 1 -> indexed, blob = P*768 (P palettes, animation) + w*h
No PIL, stdlib only (zlib, struct, binascii).
"""
# Заголовок проекта (2026-11). Распаковщик .btf; логика не менялась.
#   parse_btf(data) -> dict(name, dims, canon, blob_len, blob, payload): читает
#       контейнер LDPACKER/mdf. Потребители: pack_btf.pack_one,
#       tools/fonts/font_stats.py, tools/fonts/font_preview.py и архивное
#       семейство tools/archive/font_gen/ (fontqc.py, fontsrc_export.py,
#       fontmask_export.py, maskqc.py, rescale_font*.py).
#   decode32(blob,w,h,layout,bottomup) -> RGBA-строки 32-битного листа: font_stats,
#       font_preview, и синтетические 4x PNG-входы для gen_font4x.py.
#   decode24 / decode_indexed / decode_indexed_gray -> 24-битные и индексируемые.
#   canon_dims(payload) -> канонические w,h,bpp,blob_len из заголовка.
#   png_write / png_write_gray / bmp_write -> запись растра.
#   probe_one / unpack_one -> CLI: --probe, --batch, одиночный файл.
import sys, os, zlib, struct, binascii, argparse

MAGIC = b'LDPACKER'

def u32(buf, i):
    return struct.unpack('<I', buf[i:i+4])[0]

def canon_dims(payload):
    """Authoritative dims: w,h = the two u32 fields right after the file-name field,
    bpp = blob_len/(w*h), blob_len = the last u32 field i with i+4 == plen - v.
    Returns (w, h, bpp, blob_len, w_off, hdr_end) or None."""
    plen = len(payload)
    pos = 4
    while pos + 8 <= plen:
        L = u32(payload, pos)
        if 3 <= L <= 60 and pos + 4 + L <= plen and all(32 <= c < 127 for c in payload[pos+4:pos+4+L]):
            pos += 4 + L
            break
        pos += 4
    w, h = u32(payload, pos), u32(payload, pos+4)
    blob_len = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = u32(payload, i)
        if v > 0 and i + 4 == plen - v:
            blob_len = v
            break
    if not w or not h or blob_len is None or blob_len % (w*h) or blob_len > plen:
        return None
    bpp = blob_len // (w*h)
    if bpp not in (1, 3, 4):
        return None
    return (w, h, bpp, blob_len, pos, plen - blob_len)

def parse_btf(data):
    """Return dict: name, dims [(w,h,bpp,P)], blob_len, blob, payload."""
    if data.startswith(MAGIC):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = data.find(m, 8)
            if i >= 0:
                zi = i
                break
        if zi < 0:
            raise ValueError('no zlib stream')
        payload = zlib.decompress(data[zi:])
    elif data.startswith(b'mdf '):
        payload = data
    else:
        raise ValueError('no LDPACKER magic and no mdf header')
    if payload[:4] != b'mdf ':
        raise ValueError('payload is not mdf')
    plen = len(payload)
    # name: u32 length then ascii bytes (fields after name are not 4-aligned)
    name = None
    for i in range(4, min(plen, 256)):
        L = u32(payload, i)
        if 3 <= L <= 40 and i+4+L <= plen:
            raw = payload[i+4:i+4+L]
            if all(32 <= c < 127 for c in raw):
                name = raw.decode('ascii')
                break
    # blob_len: u32 v at byte offset i with i+4 == plen - v; scan from the end,
    # the LAST match is the blob length field (earlier matches are total-size fields)
    blob_len = None
    hdr_end = None
    for i in range(min(plen, 4096)-4, 3, -1):
        v = u32(payload, i)
        if v > 0 and i + 4 == plen - v:
            blob_len = v
            hdr_end = i + 4
            break
    if blob_len is None:
        raise ValueError('blob_len not found')
    blob = payload[plen-blob_len:]
    header = payload[:hdr_end]
    # w,h: consecutive u32 fields (4 bytes apart); blob layout by size:
    #   blob == w*h*3 -> 24-bit interleaved RGB ; w*h*4 -> 32-bit BGRA ; w*h -> indexed
    dims = []
    for j in range(0, len(header)-7):
        a, b = u32(header, j), u32(header, j+4)
        if not (4 <= a <= 8192 and 4 <= b <= 8192):
            continue
        px = a*b
        if blob_len == px*3:
            dims.append((a, b, 3, 0))
        elif blob_len == px*4:
            dims.append((a, b, 4, 0))
        elif blob_len == px:
            dims.append((a, b, 1, 0))
    # priority: canonical header dims first (w,h right after the name field), then
    # size-matching pairs, 32-bit before 24-bit before indexed, square last
    cd = canon_dims(payload)
    if cd is not None:
        c = (cd[0], cd[1], cd[2], 0)
        dims = [c] + [d for d in dims if d != c]
    dims.sort(key=lambda t: ([4, 3, 1].index(t[2]), t[0] != t[1], -t[0]))
    if cd is not None:
        c = (cd[0], cd[1], cd[2], 0)
        dims = [c] + [d for d in dims if d != c]
    return {
        'name': name,
        'dims': dims,
        'canon': cd,
        'blob_len': blob_len,
        'blob': blob,
        'payload': payload,
    }

LAYOUTS = {
    'BGRA': (2, 1, 0, 3),
    'RGBA': (0, 1, 2, 3),
    'ABGR': (3, 2, 1, 0),
    'ARGB': (1, 2, 3, 0),
}

def png_write(path, w, h, rgba_rows):
    """rgba_rows: list of bytes length w*4 (top-down). Write RGBA PNG."""
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    raw = b''.join(b'\x00' + bytes(row) for row in rgba_rows)
    idat = zlib.compress(raw, 9)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', binascii.crc32(t+d) & 0xffffffff)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)

def png_write_gray(path, w, h, rows):
    """rows: list of bytes length w (top-down). Write 8-bit grayscale PNG."""
    ihdr = struct.pack('>IIBBBB', w, h, 8, 0, 0, 0)
    raw = b''.join(b'\x00' + bytes(row) for row in rows)
    idat = zlib.compress(raw, 9)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', binascii.crc32(t+d) & 0xffffffff)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)

def bmp_write(path, w, h, rgb_rows):
    """rgb_rows: top-down rows of bytes length w*3 -> bottom-up 24-bit BMP."""
    pad = (4 - (w*3) % 4) % 4
    rows = []
    for row in rgb_rows[::-1]:
        r = bytes(row)[::-1]
        rows.append(r + b'\x00'*pad)
    data = b''.join(rows)
    size = 54 + len(data)
    hdr = b'BM' + struct.pack('<I', size) + b'\x00\x00\x00\x00' + struct.pack('<I', 54)
    hdr += struct.pack('<I', 40) + struct.pack('<ii', w, h)
    hdr += struct.pack('<HH', 1, 24) + b'\x00'*8 + struct.pack('<ii', w, h)
    open(path, 'wb').write(hdr + data)

def decode32(blob, w, h, layout='BGRA', bottomup=False):
    rp, gp, bp, ap = LAYOUTS[layout]
    R = blob[rp::4]; G = blob[gp::4]; B = blob[bp::4]
    A = blob[ap::4] if ap is not None else b'\xff'*(w*h)
    rows = []
    for y in range(h):
        s = y*w; e = (y+1)*w
        out = bytearray(w*4)
        out[0::4] = R[s:e]
        out[1::4] = G[s:e]
        out[2::4] = B[s:e]
        out[3::4] = A[s:e]
        rows.append(bytes(out))
    if bottomup:
        rows = rows[::-1]
    return rows

def decode24(blob, w, h, rgb_order='RGB', bottomup=False):
    # interleaved 3 bytes per pixel
    if rgb_order == 'RGB':
        R = blob[0::3]; G = blob[1::3]; B = blob[2::3]
    else:
        B = blob[0::3]; G = blob[1::3]; R = blob[2::3]
    rows = []
    for y in range(h):
        s = y*w; e = (y+1)*w
        out = bytearray(w*4)
        out[0::4] = R[s:e]; out[1::4] = G[s:e]; out[2::4] = B[s:e]; out[3::4] = b'\xff'*w
        rows.append(bytes(out))
    if bottomup:
        rows = rows[::-1]
    return rows

def decode_indexed(blob, w, h, pal_index=0, bottomup=False):
    """blob = P*768 palettes + w*h indexed data. Render with pal_index."""
    data = blob[len(blob) - w*h:]
    pal = blob[w*h:w*h+768] if pal_index == -1 else blob[pal_index*768:pal_index*768+768]
    rows = []
    for y in range(h):
        s = y*w; e = (y+1)*w
        out = bytearray(w*4)
        for x in range(w):
            idx = data[s+x]
            o = x*4
            out[o] = pal[idx*3]; out[o+1] = pal[idx*3+1]; out[o+2] = pal[idx*3+2]; out[o+3] = 255
        rows.append(bytes(out))
    if bottomup:
        rows = rows[::-1]
    return rows

def decode_indexed_gray(blob, w, h, bottomup=False):
    data = blob[len(blob) - w*h:]
    rows = []
    for y in range(h):
        rows.append(bytes(data[y*w:(y+1)*w]))
    if bottomup:
        rows = rows[::-1]
    return rows

def probe_one(path, outdir):
    d = parse_btf(open(path, 'rb').read())
    w, h, bpp, P = d['dims'][0]
    os.makedirs(outdir, exist_ok=True)
    stem = os.path.basename(path).rsplit('.', 1)[0]
    made = []
    blob = d['blob']
    if bpp == 4:
        for layout in ('BGRA','RGBA','ABGR','ARGB'):
            for bottomup in (False, True):
                name = '%s__%s_%s.png' % (stem, layout, 'BU' if bottomup else 'TD')
                png_write(os.path.join(outdir, name), w, h, decode32(blob, w, h, layout, bottomup))
                made.append(name)
    elif bpp == 3:
        for order in ('BGR','RGB'):
            for bottomup in (False, True):
                name = '%s__%s_%s.png' % (stem, order, 'BU' if bottomup else 'TD')
                png_write(os.path.join(outdir, name), w, h, decode24(blob, w, h, order, bottomup))
                made.append(name)
    elif bpp == 1:
        for pi in range(min(P, 4)):
            for bottomup in (False, True):
                name = '%s__PAL%d_%s.png' % (stem, pi, 'BU' if bottomup else 'TD')
                png_write(os.path.join(outdir, name), w, h, decode_indexed(blob, w, h, pi, bottomup))
                made.append(name)
        for bottomup in (False, True):
            name = '%s__IDX_%s.png' % (stem, 'BU' if bottomup else 'TD')
            png_write_gray(os.path.join(outdir, name), w, h, decode_indexed_gray(blob, w, h, bottomup))
            made.append(name)
    print('FILE %s: w=%d h=%d blob=%d bpp=%d palettes=%d variants=%d' % (stem, w, h, d['blob_len'], bpp, P, len(made)))
    return d

def unpack_one(path, outpath, layout='RGBA', bottomup=False, as_bmp=False, pal_index=0, flip=False, vflip=False):
    d = parse_btf(open(path, 'rb').read())
    w, h, bpp, P = d['dims'][0]
    if bpp == 4:
        rows = decode32(d['blob'], w, h, layout, bottomup)
        pxpp = 4
    elif bpp == 3:
        rows = decode24(d['blob'], w, h, 'RGB', bottomup)
        pxpp = 4
    elif bpp == 1:
        rows = decode_indexed(d['blob'], w, h, pal_index, bottomup)
        pxpp = 4
    if vflip:
        rows = rows[::-1]
    if flip:
        rows = hflip_rows(rows, pxpp)
    if as_bmp:
        rgb_rows = []
        for rr in rows:
            seq = bytearray()
            for x in range(w):
                seq += rr[x*4:x*4+3]
            rgb_rows.append(bytes(seq))
        bmp_write(outpath, w, h, rgb_rows)
    else:
        png_write(outpath, w, h, rows)

def hflip_rows(rows, bpp):
    out = []
    for row in rows:
        pixels = [row[i:i+bpp] for i in range(0, len(row), bpp)]
        out.append(b''.join(reversed(pixels)))
    return out

def main():
    ap = argparse.ArgumentParser(description='Lethal Dreams .btf unpacker')
    ap.add_argument('file', nargs='?', help='single .btf file')
    ap.add_argument('-o', dest='out', help='output .png/.bmp path')
    ap.add_argument('--batch', nargs=2, metavar=('DIR', 'OUTDIR'), help='unpack every .btf in DIR into OUTDIR')
    ap.add_argument('--probe', action='store_true', help='write every decode variant for visual check')
    ap.add_argument('--outdir', default='.', help='probe/batch output directory')
    ap.add_argument('--layout', default='RGBA', choices=list(LAYOUTS.keys()))
    ap.add_argument('--bottomup', action='store_true')
    ap.add_argument('--flip', action='store_true', help='mirror horizontally (display orientation)')
    ap.add_argument('--vflip', action='store_true', help='mirror vertically (display orientation)')
    ap.add_argument('--pal', type=int, default=0, help='palette index for indexed textures')
    ap.add_argument('--bmp', action='store_true')
    a = ap.parse_args()
    if a.probe:
        probe_one(a.file, a.outdir)
    elif a.batch:
        d, out = a.batch
        os.makedirs(out, exist_ok=True)
        made = 0
        for fn in sorted(os.listdir(d)):
            if not fn.endswith('.btf'):
                continue
            outp = os.path.join(out, fn[:-4] + ('.bmp' if a.bmp else '.png'))
            try:
                unpack_one(os.path.join(d, fn), outp, a.layout, a.bottomup, a.bmp, a.pal)
                made += 1
            except Exception as e:
                print('skip %s: %s' % (fn, e))
        print('unpacked %d files -> %s' % (made, out))
    else:
        unpack_one(a.file, a.out, a.layout, a.bottomup, a.bmp, a.pal, a.flip, a.vflip)
        print('wrote', a.out)

if __name__ == '__main__':
    main()
