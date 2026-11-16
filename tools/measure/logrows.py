# logrows.py (tools/measure/) - сводка HDINITBB-строк игрового лога hdlog.txt.
#   python3 tools/measure/logrows.py
import os, re, collections, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402

txt = open(paths.game_path('hdlog.txt'), 'rb').read().decode('latin-1')
rows = re.findall(r"HDINITBB \('MainBB', (-?\d+), (-?\d+), (-?\d+), (-?\d+)\) size=(-?\d+) "
                  r"pitch=(-?\d+) RowS=(-?\d+) Lead=(-?\d+) rs=(-?\d+) txt='(.*)'", txt)
c = collections.Counter()
samp = {}
for r in rows:
    key = (int(r[5]), int(r[6]))
    c[key] += 1
    s = samp.setdefault(key, [])
    if len(s) < 3 and r[9]:
        s.append(r[9])
print('всего HDINITBB:', len(rows))
for k, v in c.most_common(20):
    print('pitch=%-4d RowS=%-4d n=%-5d %s' % (k[0], k[1], v, samp[k]))
