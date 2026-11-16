#!/usr/bin/env python3
"""Export every original .btf to PNG for visual inspection.


import os
import sys
sys.path.insert(0, _ROOT)
import paths
Source: `paths.assets_dir('LethalDreams_textures_original_btf2x')` (read-only).
Dims are canonical: w,h = the first two u32 fields after the file-name field,
blob_len = the last u32 field i with i+4 == plen - v. Layout as stored (top-down rows,
R,G,B,A / R,G,B — the same rules the packer uses, so files round-trip). Extra cases:
24-bit blobs with a short zero padding tail, indexed blobs with embedded palettes,
and 1-byte indexed data rendered as grayscale.
Writes `LethalDreams_textures_src/<name>.png` + MANIFEST.txt (name, w, h, bpp, blob).
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
OUT = paths.assets_dir('LethalDreams_textures_src')

def u32(b, i):
    return struct.unpack('<I', b[i:i+4])[0]

def payload_of(path):
    d = open(path, 'rb').read()
    if d.startswith(b'LDPACKER'):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = d.find(m, 8)
            if i >= 0:
                zi = i
                break
        return zlib.decompress(d[zi:])
    return d

def hdr(pl):
    """(w, h, blob_len) from the header; None if unparsable."""
    plen = len(pl)
    pos = 4
    while pos + 8 <= plen:
        L = u32(pl, pos)
        if 3 <= L <= 60 and pos + 4 + L <= plen and all(32 <= c < 127 for c in pl[pos+4:pos+4+L]):
            pos += 4 + L
            break
        pos += 4
    w, h = u32(pl, pos), u32(pl, pos+4)
    blob_len = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = u32(pl, i)
        if v > 0 and i + 4 == plen - v:
            blob_len = v
            break
    if not w or not h or blob_len is None or blob_len > plen:
        return None
    return (w, h, blob_len)

def decode(pl, w, h, blob_len):
    data = pl[len(pl)-blob_len:]
    px = w*h
    if blob_len == px*4:
        return w, h, 4, U.decode32(data, w, h, 'RGBA', False)
    if blob_len == px*3:
        return w, h, 3, U.decode24(data, w, h, 'RGB', False)
    if blob_len > px*3 and blob_len - px*3 <= 256:
        return w, h, 3, U.decode24(data[:px*3], w, h, 'RGB', False)
    if blob_len == px:
        return w, h, 1, U.decode_indexed_gray(data, w, h, False)
    if blob_len > px and (blob_len - px) % 768 == 0:
        return w, h, 1, U.decode_indexed(data, w, h, 0, False)
    raise ValueError('blob %d does not fit %dx%d' % (blob_len, w, h))

files = sorted(f for f in os.listdir(ORIG) if f.endswith('.btf'))
os.makedirs(OUT, exist_ok=True)
made, failed, manifest = [], [], []
for f in files:
    n = f[:-4]
    try:
        pl = payload_of(ORIG + '/' + f)
        hd = hdr(pl)
        if hd is None:
            raise ValueError('header unparsable')
        w, h, blob_len = hd
        w2, h2, bpp, rows = decode(pl, w, h, blob_len)
        U.png_write(OUT + '/' + n + '.png', w2, h2, rows)
        made.append(n)
        manifest.append('%s\t%d\t%d\t%d\t%d' % (n, w2, h2, bpp, blob_len))
    except Exception as e:
        failed.append((n, repr(e)))

with open(OUT + '/MANIFEST.txt', 'w') as f:
    f.write('name\twidth\theight\tbpp\tblob_bytes\n')
    f.write('\n'.join(manifest) + '\n')
print('exported: %d of %d' % (len(made), len(files)))
print('failed: %d' % len(failed))
for n, e in failed:
    print('  FAIL %s: %s' % (n, e))
