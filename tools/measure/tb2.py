import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
# tb2.py (tools/measure/) png sx dx sy thr minc r  -> строки текста в канвасе внутри круга (513,383)
f=sys.argv[1]; sx=float(sys.argv[2]); dx=float(sys.argv[3]); sy=float(sys.argv[4])
thr=int(sys.argv[5]); minc=int(sys.argv[6]); R=float(sys.argv[7])
W,H,rows=decode_png(f)
rows_={}
for py in range(H):
    cy=py/sy
    if abs(cy-383)>R: continue
    r=rows[py]; xs=[]
    dy=383-cy
    half=(R*R-dy*dy)**0.5
    x_lo=int((513-half)*sx+dx); x_hi=int((513+half)*sx+dx)
    n=0; mn=10**9; mx=-1
    for px in range(max(0,x_lo),min(W,x_hi)):
        p=px*3
        if max(r[p],r[p+1],r[p+2])>=thr:
            n+=1
            if px<mn: mn=px
            if px>mx: mx=px
    rows_[py]=(n,mn,mx)
bands=[];run=None
for py in sorted(rows_):
    n,_,_=rows_[py]
    if n>=minc:
        if run is None: run=[py,py]
        else: run[1]=py
    else:
        if run: bands.append(tuple(run)); run=None
if run: bands.append(tuple(run))
prev=None
for a,b in bands:
    mn=min(rows_[y][1] for y in range(a,b+1) if rows_[y][1]<10**9)
    mx=max(rows_[y][2] for y in range(a,b+1))
    print('y %6.1f..%6.1f h=%5.1f | x %6.1f..%6.1f w=%5.1f cx=%6.1f | step %s' % (
        a/sy,b/sy,(b-a+1)/sy,(mn-dx)/sx,(mx-dx)/sx,(mx-mn+1)/sx,((mn+mx)/2-dx)/sx,
        '-' if prev is None else '%.1f'%((a-prev)/sy)))
    prev=a
