"""Disassemble a VA range of LETHALDREAMS.EXE (fileoff = VA - 0x400000).

Файл: tools/exe/disex.py. Каноническая команда:
`python3 tools/exe/disex.py <startVA> <len>` (из корня репозитория).
Без аргументов скрипт падает с IndexError (sys.argv[1]/sys.argv[2]) — это известное
поведение CLI, оно сохранено. EXE берётся из paths.game_path("LETHALDREAMS.EXE").

Цепь EXE (проверено построчно, md5):
  pristine (game.pristine_archive, entry LethalDreams.exe, md5 7943036fbd1233a0dbd43228f129cfc7)
    + exepatch3.py (6 правок) = deed9cf640de1fe31ecec6deacad21bd
    + 2 байта 6A 00 -> 6A 01 на fileoff 632177 = baseline 45e93d46aad6ceb3dfc17e86d04e4f82
      (= backup/LETHALDREAMS.EXE.pre4, строит tools/exe/apply_baseline.py)
    + exepatch12.py = 171319bbc170cf6e57c8d36a8ae5f50d = установленный игровой EXE.

Usage: python3 tools/exe/disex.py <startVA> <len>
"""
import subprocess, sys, tempfile, os

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

VA_BASE = 0x400000
EXE = paths.game_path("LETHALDREAMS.EXE")

start = int(sys.argv[1], 0)
ln = int(sys.argv[2], 0)
off = start - VA_BASE

data = open(EXE, "rb").read()
blob = data[off:off + ln]
tmp = tempfile.mktemp(suffix=".bin")
open(tmp, "wb").write(blob)
out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386",
                      "--adjust-vma=0x%x" % start, tmp], capture_output=True, text=True)
print(out.stdout)
print(out.stderr, file=sys.stderr)
os.unlink(tmp)
