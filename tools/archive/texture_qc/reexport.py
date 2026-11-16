#!/usr/bin/env python3
"""Re-export the textures whose export used spurious square dims.

Canonical dims come from the header (first two u32 after the file-name field;
bpp = blob_len/(w*h)).  Decode top-down, interleaved R,G,B,A (bpp=4) / R,G,B (bpp=3),
write PNGs into LethalDreams_textures_src (overwrite the wrong ones) and print the manifest
rows for the fixed names.  Source: the original .btf backup (read-only).
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
SRC = paths.assets_dir('LethalDreams_textures_src')

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

def canon(path):
    pl = payload_of(path)
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
    if not w or not h or blob_len is None or blob_len % (w*h):
        raise ValueError('header unparsable')
    return w, h, blob_len // (w*h), blob_len, pos, hdr_end(plen, blob_len)

def hdr_end(plen, blob_len):
    return plen - blob_len

def export(n):
    p = ORIG + '/' + n + '.btf'
    pl = payload_of(p)
    w, h, bpp, blob_len, pos, he = canon(p)
    blob = pl[len(pl)-blob_len:]
    if bpp == 4:
        rows = U.decode32(blob, w, h, 'RGBA', False)
    elif bpp == 3:
        rows = U.decode24(blob, w, h, 'RGB', False)
    elif bpp == 1:
        rows = U.decode_indexed(blob, w, h, 0, False)
    else:
        raise ValueError('bpp %d not handled' % bpp)
    out = SRC + '/' + n + '.png'
    U.png_write(out, w, h, rows)
    print('re-exported %s: %dx%d bpp=%d blob=%d -> %s' % (n, w, h, bpp, blob_len, out))

names = [l.strip() for l in open(os.path.join(paths.scratch_dir(), 'broken.txt')) if l.strip()]
for n in names:
    export(n)
