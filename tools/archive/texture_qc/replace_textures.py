#!/usr/bin/env python3
"""Replace game textures with upscaled PNGs (2x) from LethalDreams_textures_dst.

Pipeline per texture (pack mode):
  1. template = current game .btf (read-only), PNG must be exactly FACTOR x original
  2. pack PNG -> .btf into staging dir (desktop LethalDreams_textures_btf2x)
  3. verify: unpack the staged .btf, blob must equal the PNG pixels byte-for-byte
  4. log the original md5, copy the staged .btf over the game file
     (no backup written — the user keeps his own copy of the originals)

Modes:
  --dry   pack + verify only, nothing is written into the game
  --copy  write the staged .btf (verified by a finished --dry run) into the game
  default = pack + verify + write

Run:  python3 .tmp/replace_textures.py [--dry | --copy]
"""
import os, sys, hashlib

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
FACTOR = 2.0

copy_mode = '--copy' in sys.argv
dry = '--dry' in sys.argv
names_file = None
if '--names' in sys.argv:
    names_file = sys.argv[sys.argv.index('--names') + 1]

all_names = sorted(f[:-4] for f in os.listdir(DST) if f.endswith('.png'))
if names_file:
    subset = set(l.strip() for l in open(names_file) if l.strip())
    names = [n for n in all_names if n in subset]
    missing = sorted(subset - set(all_names))
    if missing:
        print('names not in dst: %d %s' % (len(missing), missing[:10]))
else:
    names = all_names

os.makedirs(STAGE, exist_ok=True)
ok, failed, log = [], [], []

if copy_mode:
    for n in names:
        staged = STAGE + '/' + n + '.btf'
        src = GAME_DIR + '/' + n + '.btf'
        if not os.path.exists(staged):
            failed.append((n, 'staged .btf missing')); continue
        raw = open(src, 'rb').read()
        log.append('%s\tmd5_orig=%s' % (n, hashlib.md5(raw).hexdigest()))
        ok.append(n)
    replaced = 0
    if not dry:
        for n in ok:
            with open(GAME_DIR + '/' + n + '.btf', 'wb') as f:
                f.write(open(STAGE + '/' + n + '.btf', 'rb').read())
            replaced += 1
    with open(STAGE + '/REPLACE_LOG.txt', 'w') as f:
        f.write('\n'.join(log) + '\n')
    print('staged ok: %d, written to game: %d, failed: %d (dry=%s)' % (len(ok), replaced, len(failed), dry))
    for n, e in failed:
        print('  FAIL %s: %s' % (n, e))
    sys.exit(0)

for n in names:
    png = DST + '/' + n + '.png'
    tmpl = GAME_DIR + '/' + n + '.btf'
    if not os.path.exists(tmpl):
        failed.append((n, 'no template .btf in game')); continue
    try:
        pw, ph, rows = P.png_decode(png)
        d = U.parse_btf(open(tmpl, 'rb').read())
        w0, h0, bpp, Pl = d['dims'][0]
        if (pw / w0, ph / h0) != (FACTOR, FACTOR):
            failed.append((n, 'scale %s not %sx' % ((pw / w0, ph / h0), FACTOR))); continue
        new_blob = P.build_blob(rows, pw, ph, bpp)
        if len(new_blob) > 33554432:
            failed.append((n, 'blob %d > 32MiB engine limit' % len(new_blob))); continue
        staged = STAGE + '/' + n + '.btf'
        P.pack_one(png, tmpl, staged)
        v = U.parse_btf(open(staged, 'rb').read())
        w1, h1, bpp1, _ = v['dims'][0]
        if (w1, h1) != (pw, ph) or bpp1 != bpp:
            failed.append((n, 'staged dims %s bpp=%d expected %dx%d bpp=%d' % ((w1, h1), bpp1, pw, ph, bpp))); continue
        if v['blob'] != new_blob:
            nd = sum(1 for a, b in zip(v['blob'], new_blob) if a != b)
            failed.append((n, 'blob mismatch %d bytes' % nd)); continue
        ok.append(n)
        log.append('%s\t%dx%d->%dx%d\tbpp=%d\tblob=%d' % (n, w0, h0, pw, ph, bpp, len(new_blob)))
    except Exception as e:
        failed.append((n, repr(e)))

replaced = 0
if not dry:
    for n in ok:
        src = GAME_DIR + '/' + n + '.btf'
        raw = open(src, 'rb').read()
        log.append('%s\tmd5_orig=%s' % (n, hashlib.md5(raw).hexdigest()))
        with open(src, 'wb') as f:
            f.write(open(STAGE + '/' + n + '.btf', 'rb').read())
        replaced += 1

with open(STAGE + '/REPLACE_LOG.txt', 'w') as f:
    f.write('\n'.join(log) + '\n')

print('packed+verified: %d, replaced in game: %d, failed: %d (dry=%s)' % (len(ok), replaced, len(failed), dry))
for n, e in failed:
    print('  FAIL %s: %s' % (n, e))
