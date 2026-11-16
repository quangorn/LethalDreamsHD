# tb.py (tools/measure/) - строки текста в канвасе по полосовому профилю.
#   python3 tools/measure/tb.py <png> x0 y0 w h thr minc sx dx
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
f = sys.argv[1]
x0,y0,w,h = [int(v) for v in sys.argv[2:6]]
thr = int(sys.argv[6]); minc = int(sys.argv[7])
sx = float(sys.argv[8]); dx = float(sys.argv[9])
W,H,rows = decode_png(f)
x1=min(W,x0+w); y1=min(H,y0+h)
prof=[]
for y in range(y0,y1):
    r=rows[y]; n=0
    for x in range(x0,x1):
        p=x*3
        if max(r[p],r[p+1],r[p+2])>=thr: n+=1
    prof.append((y,n))
bands=[]; run=None
for y,n in prof:
    if n>=minc:
        if run is None: run=[y,y]
        else: run[1]=y
    else:
        if run: bands.append(tuple(run)); run=None
if run: bands.append(tuple(run))
prev=None
for a,b in bands:
    mnx,mxx=10**9,-1
    for y in range(a,b+1):
        r=rows[y]
        for x in range(x0,x1):
            p=x*3
            if max(r[p],r[p+1],r[p+2])>=thr:
                if x<mnx: mnx=x
                if x>mxx: mxx=x
    print('y %6.1f..%6.1f h=%5.1f | x %6.1f..%6.1f w=%5.1f cx=%6.1f | step %s' % (
        a/sx, b/sx, (b-a+1)/sx, (mnx-dx)/sx, (mxx-dx)/sx, (mxx-mnx+1)/sx, ((mnx+mxx)/2-dx)/sx,
        '-' if prev is None else '%.1f'%((a-prev)/sx)))
    prev=a
