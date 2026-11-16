#!/usr/bin/env python3
"""Header probe over the original .btf backup.

After the name field the header has u32 values; take the first four after the name:
w, h, a, b.  Compute ratio = blob_len / (w*h) and report the distribution of
(ratio, a, b).  Shows which files have a non-integer / non-3-4 ratio (the "interlaced"
textures) and how w,h encode them.
"""
import os, sys, struct, zlib


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
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
        if zi < 0:
            raise ValueError('no zlib')
        return zlib.decompress(d[zi:])
    return d

def hdr_of(pl):
    plen = len(pl)
    blob_len = None
    hdr_end = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = u32(pl, i)
        if v > 0 and i + 4 == plen - v:
            blob_len = v
            hdr_end = i + 4
            break
    name = None
    pos = None
    for i in range(4, min(plen, 256)):
        L = u32(pl, i)
        if 3 <= L <= 60 and i + 4 + L <= plen:
            raw = pl[i+4:i+4+L]
            if all(32 <= c < 127 for c in raw):
                name = raw.decode('ascii')
                pos = i + 4 + L
                break
    return blob_len, hdr_end, name, pos

rows = []
dist = {}
bad = []
for f in sorted(os.listdir(ORIG)):
    if not f.endswith('.btf'):
        continue
    n = f[:-4]
    try:
        pl = payload_of(ORIG + '/' + f)
        blob_len, hdr_end, name, pos = hdr_of(pl)
        if blob_len is None:
            rows.append('%s\tNO-BLOBLEN' % n); continue
        vals = [u32(pl, pos + 4*k) for k in range(6) if pos + 4*k + 4 <= len(pl)]
        w, h, a, b = vals[0], vals[1], vals[2], vals[3]
        px = w * h
        r = blob_len / px if px else None
        key = (r, a, b)
        dist[key] = dist.get(key, 0) + 1
        rows.append('%s\t%dx%d\tblob %d\tratio %s\ta=%d b=%d' % (n, w, h, blob_len, r, a, b))
        if px == 0 or blob_len % px != 0:
            bad.append((n, w, h, blob_len, a, b))
    except Exception as e:
        rows.append('%s\tERR %s' % (n, e))

with open(os.path.join(paths.scratch_dir(), 'hdr_probe.txt'), 'w') as f:
    f.write('\n'.join(rows) + '\n')
print('files:', len(rows))
for (r, a, b), c in sorted(dist.items(), key=lambda kv: -kv[1]):
    print('  ratio %-8s a=%-12d b=%-12d  count %d' % (r, a, b, c))
print('non-integer ratio files:', len(bad))
for t in bad[:20]:
    print('   ', t)
