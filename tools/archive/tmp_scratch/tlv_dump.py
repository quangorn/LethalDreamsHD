#!/usr/bin/env python3
"""TLV dump of the .btf HEADER region only (first HDR bytes), correctly aligned.

Layout: 'mdf ' + fields; each field = (type u32, value u32), types t=116,u=117,v=118,
w=119,x=120; the file-name field has NO type: (len u32, ascii bytes). After the name,
plain u32 values follow (w,h are two consecutive u32 right after the name).
"""
import os, sys, struct, zlib

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

def u32(b, i):
    return struct.unpack('<I', b[i:i+4])[0]

HDR = int(os.environ.get('HDR', '160'))

def fields(pl, limit):
    pos = 4
    out = []
    while pos + 4 <= min(len(pl), limit):
        a = u32(pl, pos)
        if 3 <= a <= 60 and pos + 4 + a <= len(pl):
            raw = pl[pos+4:pos+4+a]
            if all(32 <= c < 127 for c in raw):
                out.append((pos, 'name', a, raw.decode('ascii')))
                pos += 4 + a
                continue
        b = u32(pl, pos+4) if pos + 8 <= len(pl) else None
        out.append((pos, 'pair', a, b))
        pos += 8
    return out


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
for n in sys.argv[1:]:
    p = paths.game_path('data', 'textures', '%s.btf') % n
    pl = payload_of(p)
    print('== %s plen %d' % (n, len(pl)))
    for pos, kind, a, b in fields(pl, HDR):
        print('   off %3d  %-4s  %12s  %s' % (pos, kind, a, b))
