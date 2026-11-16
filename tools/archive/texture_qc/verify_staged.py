#!/usr/bin/env python3
"""Rigorous verification of staged upscaled .btf against source PNGs.

The generic unpacker guesses w,h from blob size and picks a square factor pair when
several pairs multiply to blob/bpp (e.g. 2048x512 and 1024x1024) — that is why the
dry run reported 'staged dims' failures for all non-square textures. Here we check the
header fields at the exact byte offsets the packer patched, not a guessed pair.

Checks per file:
  u32(w_off)==png_w, u32(h_off)==png_h, u32(b_off)==blob_len,
  blob length == png_w*png_h*bpp, blob bytes == PNG pixels byte-for-byte.

Usage: python3 verify_staged.py [name ...]
"""
import os, sys, zlib, struct

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U
import pack_btf as P

DST = paths.assets_dir('LethalDreams_textures_dst')
GAME_DIR = paths.game_path('data', 'textures')
STAGE = paths.assets_dir('LethalDreams_textures_btf2x')
SRC = paths.assets_dir('LethalDreams_textures_src')

man = {}
for line in open(SRC + '/MANIFEST.txt').read().splitlines()[1:]:
    n, w, h, b, blob = line.split('\t')
    if '/' not in n:
        man[n] = (int(w), int(h), int(b))

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

def offsets(payload, w0, h0):
    plen = len(payload)
    w_off = h_off = None
    for j in range(0, plen - 7):
        if U.u32(payload, j) == w0 and U.u32(payload, j + 4) == h0:
            w_off, h_off = j, j + 4
            break
    b_off = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = U.u32(payload, i)
        if v > 0 and i + 4 == plen - v:
            b_off = i
            break
    return w_off, h_off, b_off

def verify(n):
    png = DST + '/' + n + '.png'
    tmpl = GAME_DIR + '/' + n + '.btf'
    staged = STAGE + '/' + n + '.btf'
    if not os.path.exists(staged):
        return (n, 'staged missing')
    pw, ph, rows = P.png_decode(png)
    tp = payload_of(tmpl)
    # original dims come from MANIFEST.txt (authoritative): the unpacker's dims guess
    # is ambiguous for non-square textures (square factor pairs match blob/bpp too)
    w0, h0, bpp = man[n]
    w_off, h_off, b_off = offsets(tp, w0, h0)
    if w_off is None:
        return (n, 'template w,h offsets not found')
    sp = payload_of(staged)
    hdr_len = len(tp) - w0 * h0 * bpp
    exp_blob = P.build_blob(rows, pw, ph, bpp)
    got_w, got_h = U.u32(sp, w_off), U.u32(sp, h_off)
    got_blen = U.u32(sp, b_off) if b_off is not None else None
    blob = sp[hdr_len:]
    if (got_w, got_h) != (pw, ph):
        return (n, 'header w,h = %d,%d expected %d,%d' % (got_w, got_h, pw, ph))
    if got_blen is not None and got_blen != len(exp_blob):
        return (n, 'blob_len field %d expected %d' % (got_blen, len(exp_blob)))
    if len(blob) != len(exp_blob):
        return (n, 'blob size %d expected %d' % (len(blob), len(exp_blob)))
    if blob != exp_blob:
        nd = sum(1 for a, b in zip(blob, exp_blob) if a != b)
        return (n, 'blob differs in %d bytes' % nd)
    return (n, 'OK %dx%d bpp=%d blob=%d' % (pw, ph, bpp, len(exp_blob)))

names = sys.argv[1:]
if not names:
    names = sorted(f[:-4] for f in os.listdir(STAGE) if f.endswith('.btf'))
ok, bad = 0, []
for n in names:
    r = verify(n)
    if r[1].startswith('OK'):
        ok += 1
    else:
        bad.append(r)
print('verified ok: %d, problems: %d' % (ok, len(bad)))
for n, e in bad:
    print('  PROBLEM %s: %s' % (n, e))
