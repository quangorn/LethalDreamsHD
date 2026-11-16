#!/usr/bin/env python3
"""Repack + install the 5 textures that were packed with spurious square dims.

Templates come from the ORIGINAL backup (`LethalDreams_textures_original_btf2x`) — the game
files there have the true w,h and blob_len. The upscaled PNG (from LethalDreams_textures_dst)
must be exactly 2x the canonical dims. Verification per file:
  - staged header w,h (canonical offsets) == PNG dims, blob_len field == blob size
  - staged blob == bytes built from the PNG rows (bpp from canon)
  - staged header == template header byte-for-byte except the patched fields
    (w_off, h_off, blob_len field, and the payload-size field)
Modes: default = pack + verify into the staging folder; --copy writes to the game.

Usage: python3 repack_fixed.py [--copy]
"""
import os, sys, struct, zlib, hashlib

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U
import pack_btf as P

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
DST = paths.assets_dir('LethalDreams_textures_dst')
GAME = paths.game_path('data', 'textures')
STAGE = paths.assets_dir('LethalDreams_textures_btf2x')
names = [l.strip() for l in open(os.path.join(paths.scratch_dir(), 'broken.txt')) if l.strip()]
copy_mode = '--copy' in sys.argv

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

def check(n):
    tmpl = ORIG + '/' + n + '.btf'
    png = DST + '/' + n + '.png'
    staged = STAGE + '/' + n + '.btf'
    cd = U.parse_btf(open(tmpl, 'rb').read())['canon']
    if cd is None:
        return (n, 'template canon dims unparsable')
    w0, h0, bpp, blob0, w_off, hdr_end = cd
    pw, ph, rows = P.png_decode(png)
    if (pw, ph) != (w0*2, h0*2):
        return (n, 'png %dx%d is not 2x of canon %dx%d' % (pw, ph, w0, h0))
    exp_blob = P.build_blob(rows, pw, ph, bpp)
    sp = payload_of(staged)
    plen = len(sp)
    if U.u32(sp, w_off) != pw or U.u32(sp, w_off+4) != ph:
        return (n, 'staged w,h = %d,%d expected %d,%d' % (U.u32(sp, w_off), U.u32(sp, w_off+4), pw, ph))
    blob_off = plen - len(exp_blob)
    if blob_off < hdr_end:
        return (n, 'blob starts at %d, expected at/after header %d' % (blob_off, hdr_end))
    if U.u32(sp, blob_off-4) != len(exp_blob):
        return (n, 'blob_len field %d expected %d' % (U.u32(sp, blob_off-4), len(exp_blob)))
    if sp[blob_off:] != exp_blob:
        return (n, 'blob differs in %d bytes' % sum(1 for a, b in zip(sp[blob_off:], exp_blob) if a != b))
    # header equality: template header (before the blob_len field) vs staged header,
    # only the patched cells (w,h, payload-size marker) may differ
    tp = payload_of(tmpl)
    th = tp[:hdr_end-4]
    sh = sp[:blob_off-4]
    if len(th) != len(sh):
        return (n, 'header length %d vs staged %d' % (len(th), len(sh)))
    patched = {i for i in range(len(th)) if th[i] != sh[i]}
    allowed = {w_off, w_off+1, w_off+2, w_off+3, w_off+4, w_off+5, w_off+6, w_off+7}
    old_marker = len(tp) - 16
    for i in range(0, min(len(th), 512)-3):
        if U.u32(th, i) == old_marker:
            allowed |= {i, i+1, i+2, i+3}
    extra = patched - allowed
    if extra:
        return (n, 'header bytes differ outside patched fields at %s' % sorted(extra)[:8])
    gmd5 = smd5 = None
    if os.path.exists(GAME + '/' + n + '.btf'):
        gmd5 = hashlib.md5(open(GAME + '/' + n + '.btf', 'rb').read()).hexdigest()
    smd5 = hashlib.md5(open(staged, 'rb').read()).hexdigest()
    return (n, 'OK %dx%d bpp=%d blob=%d md5_stage=%s md5_game=%s' % (pw, ph, bpp, len(exp_blob), smd5[:8], gmd5[:8] if gmd5 else '-'))

os.makedirs(STAGE, exist_ok=True)
ok, bad = [], []
for n in names:
    if not copy_mode:
        try:
            P.pack_one(DST + '/' + n + '.png', ORIG + '/' + n + '.btf', STAGE + '/' + n + '.btf')
        except Exception as e:
            bad.append((n, 'pack %s' % e))
            continue
    r = check(n)
    if r[1].startswith('OK'):
        ok.append(r[0])
    else:
        bad.append((r[0], r[1]))
print('packed+verified: %d, problems: %d' % (len(ok), len(bad)))
for n, e in bad:
    print('  PROBLEM %s: %s' % (n, e))
if copy_mode and not bad:
    for n in names:
        s = STAGE + '/' + n + '.btf'
        g = GAME + '/' + n + '.btf'
        data = open(s, 'rb').read()
        open(g, 'wb').write(data)
        if hashlib.md5(open(g, 'rb').read()).hexdigest() != hashlib.md5(data).hexdigest():
            print('  WRITE MISMATCH %s' % n)
    print('written to game: %d' % len(names))
