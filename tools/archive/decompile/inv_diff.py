#!/usr/bin/env python3
# inv_diff.py - сравнить backup_orig (оригинальные .pyc) с файлами игры.
import os, hashlib
BO = 'backup_orig'

import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
G = paths.game_path('data')
SUBS = ('dialogs', 'goscripts', 'mentality', 'spells')

def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()

diff = []
for root, dirs, files in os.walk(BO):
    rel = os.path.relpath(root, BO).replace('\\', '/')
    for f in files:
        if not f.endswith('.pyc'):
            continue
        src = os.path.join(root, f)
        if rel == '.':
            dst = os.path.join(G, 'scripts', f)
        elif rel == 'ui':
            dst = os.path.join(G, 'scripts', 'ui', f)
        elif rel in SUBS:
            dst = os.path.join(G, 'scripts', rel, f)
        else:
            dst = None
        if dst is None:
            continue
        if not os.path.exists(dst):
            diff.append(('MISSING_IN_GAME', rel, f))
            continue
        a = md5(src)
        b = md5(dst)
        if a != b:
            diff.append(('DIFF', rel, f, a[:8], b[:8]))
for d in diff:
    print(d)
