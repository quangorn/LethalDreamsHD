# gyr2.py (tools/measure/) - центроид/дисперсия чернил в канвасном окне x[lo..hi].
#   python3 tools/measure/gyr2.py <png> sx dx sy y0 y1 [lo hi]
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
f=sys.argv[1]; sx=float(sys.argv[2]); dx=float(sys.argv[3]); sy=float(sys.argv[4])
y0,y1=int(sys.argv[5]),int(sys.argv[6]); lo=float(sys.argv[7]) if len(sys.argv)>7 else 320; hi=float(sys.argv[8]) if len(sys.argv)>8 else 710
W,H,rows=decode_png(f)
S=SX=SXX=0.0
for py in range(y0,min(y1,H)):
    r=rows[py]
    for px in range(W):
        x=(px-dx)/sx
        if x<lo or x>hi: continue
        p=px*3; v=max(r[p],r[p+1],r[p+2])
        w=v-120 if v>120 else 0
        if not w: continue
        S+=w; SX+=w*x; SXX+=w*x*x
if S<=0: print('no ink'); raise SystemExit
m=SX/S; print('cx=%7.2f sd=%6.2f' % (m,(SXX/S-m*m)**0.5))
