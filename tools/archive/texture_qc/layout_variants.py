#!/usr/bin/env python3
"""Candidate pixel layouts for the reported textures.

Given w,h from the header and blob_len/(w*h) = bytes per pixel, write PNG previews for:
  interleaved: RGBA, BGRA, ARGB, ABGR (top-down and bottom-up)
  planar: planes R,G,B,A in orders RGBA / BGRA / ARGB, each plane w*h bytes

import os
import sys
sys.path.insert(0, _ROOT)
import paths
Output: paths.scratch_dir()/var_<name>_<tag>.png
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
OUT = paths.scratch_dir()

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
    return w, h, a, b, blob_len, pl[plen-blob_len:]

def rows_from_planes(planes, w, h, order):
    """order: string like 'RGBA' giving which plane is R,G,B,A in that order."""
    idx = {c: i for i, c in enumerate(order)}
    R, G, B = planes[idx['R']], planes[idx['G']], planes[idx['B']]
    A = planes[idx['A']] if 'A' in idx else None
    rows = []
    for y in range(h):
        s, e = y*w, (y+1)*w
        out = bytearray(w*4)
        out[0::4] = R[s:e]
        out[1::4] = G[s:e]
        out[2::4] = B[s:e]
        out[3::4] = A[s:e] if A is not None else b'\xff'*w
        rows.append(bytes(out))
    return rows

def interleaved(blob, w, h, order, bottomup, stride=4):
    """order of bytes in file, e.g. 'RGBA' / 'BGRA' / 'ARGB'; stride = bytes per pixel."""
    idx = {c: i for i, c in enumerate(order)}
    px = w*h
    R = blob[idx['R']::stride]
    G = blob[idx['G']::stride]
    B = blob[idx['B']::stride]
    A = blob[idx['A']::stride] if 'A' in idx else b'\xff'*px
    rows = []
    for y in range(h):
        s, e = y*w, (y+1)*w
        out = bytearray(w*4)
        out[0::4] = R[s:e]; out[1::4] = G[s:e]; out[2::4] = B[s:e]; out[3::4] = A[s:e]
        rows.append(bytes(out))
    if bottomup:
        rows = rows[::-1]
    return rows

def make(n):
    pl = payload_of(ORIG + '/' + n + '.btf')
    w, h, a, b, blob_len, blob = hdr(pl)
    bpp = blob_len // (w*h)
    made = []
    if bpp == 4:
        variants = []
        for order in ('RGBA', 'BGRA', 'ARGB', 'ABGR'):
            variants.append(('IL_' + order + '_TD', interleaved(blob, w, h, order, False)))
            variants.append(('IL_' + order + '_BU', interleaved(blob, w, h, order, True)))
        planes = [blob[k*w*h:(k+1)*w*h] for k in range(4)]
        for order in ('RGBA', 'BGRA', 'ARGB', 'ABGR'):
            variants.append(('PL_' + order, rows_from_planes(planes, w, h, order)))
            variants.append(('PL_' + order + '_BU', rows_from_planes(planes, w, h, order)[::-1]))
    elif bpp == 3:
        variants = []
        for order in ('RGB', 'BGR'):
            variants.append(('IL_' + order + '_TD', interleaved(blob, w, h, order + 'A', False)))
            variants.append(('IL_' + order + '_BU', interleaved(blob, w, h, order + 'A', False)[::-1]))
        planes = [blob[k*w*h:(k+1)*w*h] for k in range(3)]
        planes.append(b'\xff'*(w*h))
        for order in ('RGB', 'BGR'):
            variants.append(('PL_' + order, rows_from_planes(planes, w, h, order + 'A')))
            variants.append(('PL_' + order + '_BU', rows_from_planes(planes, w, h, order + 'A')[::-1]))
    for tag, rows in variants:
        p = OUT + '/var_%s_%s.png' % (n, tag)
        U.png_write(p, w, h, rows)
        made.append(tag)
    print('%s %dx%d bpp=%d blob=%d a=%d b=%d -> %d variants: %s' % (n, w, h, bpp, blob_len, a, b, len(made), ' '.join(made)))

for n in sys.argv[1:]:
    make(n)
