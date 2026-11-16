import struct

import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
d = open(paths.game_path('LETHALDREAMS.EXE'), "rb").read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
assert d[pe:pe+4] == b'PE\0\0'
n = struct.unpack_from('<H', d, pe+6)[0]
sections = d[pe+0x18:pe+0x18+40*n]
out = ["sections = %d" % n]
for i in range(n):
    s = sections[i*40:(i+1)*40]
    name = s[:8].rstrip(b'\0').decode('ascii', 'replace')
    vsize, va, rawsz, raw = struct.unpack_from('<IIII', s, 8)
    flags = struct.unpack_from('<I', s, 36)[0]
    out.append(f"{name!r} VA={va:#08x} vsize={vsize:#08x} raw={raw:#08x} rawsz={rawsz:#08x} flags={flags:#010x}")

def sec_of(va):
    for i in range(n):
        s = sections[i*40:(i+1)*40]
        va0 = struct.unpack_from('<I', s, 12)[0]
        vsize = struct.unpack_from('<I', s, 8)[0]
        rawsz = struct.unpack_from('<I', s, 16)[0]
        if va0 <= va < va0 + max(vsize, rawsz):
            return (s[:8].rstrip(b'\0').decode('ascii','replace'), hex(va0))
    return None

for va in (0x41cf52, 0x41cedc, 0x477df5, 0x49a571, 0x477f5d, 0x41cc90, 0x41d020, 0x51c000):
    out.append(f"site {va:#x} -> {sec_of(va)}")
open(os.path.join(paths.scratch_dir('ld'), 'secparse.txt'), "w").write("\n".join(out))
print("\n".join(out))
