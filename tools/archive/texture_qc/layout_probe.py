#!/usr/bin/env python3
"""Row/planar structure analysis of the reported textures.

Reads the ORIGINAL .btf from the backup, takes w,h,bpp strictly from the header
(w,h = first two u32 after the name field; bpp by blob_len/(w*h)), then reports:
  - per-row statistics (nonzero bytes, mean) for stride w*bpp
  - row-sum periodicity (period 1,2,3,4)
  - planar-plane check: split blob into bpp planes of w*h bytes
Writes a text report and preview PNGs for candidate layouts.
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')

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
    plen = len(pl)
    pos = 4
    while True:
        L = u32(pl, pos)
        if 3 <= L <= 60 and all(32 <= c < 127 for c in pl[pos+4:pos+4+L]):
            hp = pos + 4 + L
            break
        pos += 4
    vals = [u32(pl, hp + 4*k) for k in range(10)]
    w, h, a, b = vals[0], vals[1], vals[2], vals[3]
    blob_len = vals[9]
    # verify blob_len: bytes after the header must be exactly blob_len
    blob = pl[plen - blob_len:]
    return w, h, a, b, blob_len, blob, pl[hp + 40:plen]

def rows_of(blob, w, h, bpp):
    stride = w * bpp
    return [blob[y*stride:(y+1)*stride] for y in range(h)]

def analyze(n):
    pl = payload_of(ORIG + '/' + n + '.btf')
    w, h, a, b, blob_len, blob, tail = hdr(pl)
    bpp = blob_len // (w*h)
    print('== %s  w=%d h=%d a=%d b=%d blob=%d bpp=%d  tail_after_hdr %d bytes' % (n, w, h, a, b, blob_len, bpp, len(tail)))
    print('   tail first 32 bytes:', ' '.join('%02x' % c for c in tail[:32]))
    stride = w * bpp
    rsums = [sum(row) for row in rows_of(blob, w, h, bpp)]
    nz = [sum(1 for c in row if c) for row in rows_of(blob, w, h, bpp)]
    print('   first 24 row sums:', rsums[:24])
    print('   first 24 row nonzero:', nz[:24])
    # periodicity: is row content repeating with period p (rows equal)?
    for p in (2, 3, 4):
        eq = all(rows_of(blob, w, h, bpp)[y] == rows_of(blob, w, h, bpp)[y % p] for y in range(h))
        print('   period %d identical rows: %s' % (p, eq))
    # zero-row count
    z = sum(1 for r in rsums if r == 0)
    print('   zero rows %d of %d' % (z, h))
    # channel byte-position statistics for interleaved bpp=4
    if bpp == 4:
        for k in range(4):
            ch = blob[k::4]
            print('   byte-slot %d: mean %.2f  uniq %d  zeros %d' % (k, sum(ch)/len(ch), len(set(ch)), ch.count(0)))
    # planar split
    if bpp == 4:
        planes = [blob[k*w*h:(k+1)*w*h] for k in range(4)]
        for k, p in enumerate(planes):
            print('   plane %d: mean %.2f uniq %d zeros %d first8 %s' % (k, sum(p)/len(p), len(set(p)), p.count(0), ' '.join('%02x' % c for c in p[:8])))

for n in sys.argv[1:]:
    analyze(n)
