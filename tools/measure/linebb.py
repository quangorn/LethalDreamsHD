import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
# linebb.py (tools/measure/) png sx dx sy y0 y1 thr R   (y0,y1 в пикселях снимка)
#   python3 tools/measure/linebb.py <png> sx dx sy y0 y1 thr R
f=sys.argv[1]; sx=float(sys.argv[2]); dx=float(sys.argv[3]); sy=float(sys.argv[4])
y0=int(sys.argv[5]); y1=int(sys.argv[6]); thr=int(sys.argv[7]); R=float(sys.argv[8])
W,H,rows=decode_png(f)
mn=10**9; mx=-1; t=10**9; b=-1
for py in range(y0,min(y1,H)):
    cy=py/sy; dy=383.0-cy
    if abs(dy)>R: continue
    half=(R*R-dy*dy)**0.5
    lo=int((513-half)*sx+dx); hi=int((513+half)*sx+dx)
    r=rows[py]
    for px in range(max(0,lo),min(W,hi)):
        p=px*3
        if max(r[p],r[p+1],r[p+2])>=thr:
            if px<mn: mn=px
            if px>mx: mx=px
            if py<t: t=py
            if py>b: b=py
print('x %7.2f..%7.2f w=%6.2f cx=%7.2f | y %7.2f..%7.2f h=%5.2f' % (
    (mn-dx)/sx,(mx-dx)/sx,(mx-mn+1)/sx,((mn+mx)/2-dx)/sx,t/sy,b/sy,(b-t+1)/sy))
