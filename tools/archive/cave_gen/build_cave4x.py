# build_cave4x.py - собрать блоб пещеры 4x-шрифта и отрезать padding секции.
#   gcc -m32 -c -> .tmp/fontscale4x.o ; objcopy -> .tmp/fontscale4x.bin ;
#   обрезка по концу последнего символа данных (иначе .align раздела даёт лишние
#   nop-байты, а пещера должна влезть в zero-run 238 байт).
import subprocess, os

os.chdir(os.path.dirname(os.path.abspath(__file__)) + '/..')
subprocess.check_call(['gcc', '-m32', '-c', '-O1', '-o', '.tmp/fontscale4x.o', '.tmp/fontscale4x.s'])
subprocess.check_call(['objcopy', '-O', 'binary', '-j', '.text', '.tmp/fontscale4x.o', '.tmp/fontscale4x.bin'])

out = subprocess.check_output(['nm', '--print-size', '-n', '.tmp/fontscale4x.o']).decode()
rows = [l for l in out.splitlines() if l.strip()]
a, s = [int(x, 16) for x in rows[-1].split()[:3:2]]
end = a + s
d = open('.tmp/fontscale4x.bin', 'rb').read()
open('.tmp/fontscale4x.bin', 'wb').write(d[:end])
print('blob: %d bytes (was %d, trimmed %d)' % (end, len(d), len(d) - end))
print('last symbol:', rows[-1])
