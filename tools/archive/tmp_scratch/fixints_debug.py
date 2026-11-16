import sys, os, re, difflib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'pyc'))
import pycbuild

module = 'ui/battlemenu'
pyc = pycbuild.game_pyc(module)
py = pycbuild.build_py(module)
text = open(py, encoding='utf-8').read()
out, rc = pycbuild.run_worker('litfix', pyc.replace(os.sep, '/'), py.replace(os.sep, '/'))
trees = {'A': [], 'B': []}
for line in out.split('\n'):
    p = line.split(None, 3)
    if len(p) < 4 or p[0] != 'TOKEN':
        continue
    side, idx = p[1], int(p[2])
    tok = re.sub(r'^\d+\s+', '', p[3].strip())
    while len(trees[side]) <= idx:
        trees[side].append([None, []])
    trees[side][idx][1].append(tok)
cs = pycbuild.tree_consts(pyc)
print('code objects A=%d B=%d consts tree=%d' % (len(trees['A']), len(trees['B']), len(cs)))
n = min(len(trees['A']), len(trees['B']))
hits = 0
for i in range(n):
    ta = trees['A'][i][1]
    tb = [t for t in trees['B'][i][1] if not t.startswith('SLINE ')]
    csn = cs[i] if i < len(cs) else []
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    equal = 0
    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == 'equal':
            equal += a2 - a1
            continue
        for q in range(min(a2 - a1, b2 - b1)):
            x = ta[a1 + q]
            y = tb[b1 + q]
            if not (x.startswith('LOAD_CONST HEX ') and y.startswith('LOAD_CONST HEX ')):
                continue
            val = pycbuild.decode_hex(x[len('LOAD_CONST HEX '):])
            kv = pycbuild.decode_hex(y[len('LOAD_CONST HEX '):])
            if not isinstance(kv, int):
                continue
            K = kv
            ok = K < len(csn) and val == csn[K]
            hits += 1 if ok else 0
            if ok and hits < 12:
                print('HIT i=%d name=%s K=%d val=%r' % (i, trees['A'][i][0], K, val))
print('total candidate int-index hits:', hits)
