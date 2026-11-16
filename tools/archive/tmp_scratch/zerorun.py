
import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
d = open(os.path.join(_ROOT, 'backup', 'LETHALDREAMS.EXE.pre4'), "rb").read()
VA_BASE = 0x400000
runs = []
i = 0x4000
n = len(d)
while i < n:
    if d[i] == 0:
        j = i
        while j < n and d[j] == 0:
            j += 1
        if j - i >= 160:
            runs.append((i, j - i))
        i = j
    else:
        i += 1
out = ["zero runs (len>=160): %d" % len(runs)]
for off, ln in runs[:40]:
    out.append("fileoff 0x%x -> VA 0x%x len %d" % (off, VA_BASE + off, ln))
open(os.path.join(paths.scratch_dir('ld'), 'zeroruns.txt'), "w").write("\n".join(out))
print("\n".join(out[:42]))
