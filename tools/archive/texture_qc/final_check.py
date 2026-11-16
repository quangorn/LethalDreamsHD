#!/usr/bin/env python3
"""Final check of the second installation batch (650 textures from dst).

Per name:
  - rigorous staged verification against the dst PNG: w,h read at the exact header
    offsets the packer patched, blob_len field checked, blob compared byte-by-byte
    (the generic unpacker's dims guess is ambiguous for non-square textures)
  - md5(game .btf) == md5(staged .btf)

import os
import sys
sys.path.insert(0, _ROOT)
import paths
Writes paths.scratch_dir()/final_check.txt, prints summary.
"""
import os, sys, struct, zlib, hashlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U
import pack_btf as P

DST = paths.assets_dir('LethalDreams_textures_dst')
GAME = paths.game_path('data', 'textures')
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

def check(n):
    pw, ph, rows = P.png_decode(DST + '/' + n + '.png')
    sp = payload_of(STAGE + '/' + n + '.btf')
    plen = len(sp)
    # the packer wrote w,h = PNG size; locate that pair in the staged header
    w_off = h_off = None
    for j in range(0, plen - 7):
        if U.u32(sp, j) == pw and U.u32(sp, j + 4) == ph:
            w_off, h_off = j, j + 4
            break
    if w_off is None:
        return 'no w,h pair (%d,%d) in staged header' % (pw, ph)
    b_off = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = U.u32(sp, i)
        if v > 0 and i + 4 == plen - v:
            b_off = i
            break
    exp = P.build_blob(rows, pw, ph, man[n][2])
    if b_off is not None and U.u32(sp, b_off) != len(exp):
        return 'blob_len field %d expected %d' % (U.u32(sp, b_off), len(exp))
    blob = sp[plen - len(exp):]
    if blob != exp:
        return 'blob differs in %d bytes' % sum(1 for a, b in zip(blob, exp) if a != b)
    g = hashlib.md5(open(GAME + '/' + n + '.btf', 'rb').read()).hexdigest()
    s = hashlib.md5(open(STAGE + '/' + n + '.btf', 'rb').read()).hexdigest()
    if g != s:
        return 'md5 game != staged'
    return 'OK %dx%d bpp=%d blob=%d' % (pw, ph, man[n][2], len(exp))

names = [l.strip() for l in open(os.path.join(paths.scratch_dir(), 'pending.txt')) if l.strip()]
rows, bad = [], []
for n in names:
    r = check(n)
    if r.startswith('OK'):
        rows.append('%s\t%s' % (n, r))
    else:
        rows.append('%s\tPROBLEM %s' % (n, r))
        bad.append((n, r))
with open(os.path.join(paths.scratch_dir(), 'final_check.txt'), 'w') as f:
    f.write('\n'.join(rows) + '\n')
print('checked %d, ok %d, problems %d' % (len(names), len(names) - len(bad), len(bad)))
for n, e in bad:
    print('  PROBLEM %s: %s' % (n, e))
