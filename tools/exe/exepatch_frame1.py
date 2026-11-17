"""exepatch_frame1.py - ЭКСПЕРИМЕНТ: 1 px рамка окна (WS_BORDER) вместо `+1 px` в hd.cfg.

Файл: tools/exe/exepatch_frame1.py. Каноническая команда (из корня репозитория):
  python3 tools/exe/exepatch_frame1.py            # копия в scratch, игру не трогает
  python3 tools/exe/exepatch_frame1.py --install  # правка вписывается в EXE установки
  python3 tools/exe/exepatch_frame1.py --restore  # вернуть байт в установке (0x80800000 -> 0x80000000)

Суть: в `FUN_00424910` стиль окна — `push 0x80000000` (WS_POPUP, exepatch3 снял
рамку). Добавляем `WS_BORDER` (0x00800000) -> `0x80800000`. Окно по-прежнему
ставится SetWindowPos(0,0,w,h) на весь экран, но клиентская область (и WGL-
поверхность) становится (w-2)x(h-2); 1 px не-клиентской рамки рисует Windows.
Цель — проверить, уходит ли OSD монитора, когда GL-поверхность не совпадает
с экраном, при полном перекрытии кадра.

fileoff 149929: 68 00 00 00 80 (push 0x80000000) -> 68 00 00 80 80 (push 0x80800000)
единственный изменяемый байт — 149932: 00 -> 80.
"""
import hashlib
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

EXE = paths.game_path("LETHALDREAMS.EXE")
OFF = 149932
BASE = b"\x00"   # 0x80000000
NEW = b"\x80"    # 0x80800000

install = "--install" in sys.argv[1:]
restore = "--restore" in sys.argv[1:]
OUT = EXE if (install or restore) else paths.scratch_dir("exe") + "/LETHALDREAMS.EXE.frame1"

d = open(EXE, "rb").read()
if d[149929:149934] != b"\x68\x00\x00\x00\x80" and d[149929:149934] != b"\x68\x00\x00\x80\x80":
    raise SystemExit("unexpected style push at 149929: %s" % d[149929:149934].hex())

cur = d[OFF:OFF + 1]
if restore:
    if cur != NEW:
        raise SystemExit("restore: byte 149932 is %s, expected 80" % cur.hex())
    d = d[:OFF] + BASE + d[OFF + 1:]
    open(OUT, "wb").write(d)
    print("restored ->", OUT, "md5", hashlib.md5(d).hexdigest())
elif cur == NEW:
    print("already patched (0x80800000)")
    if install:
        open(OUT, "wb").write(d)
else:
    d = d[:OFF] + NEW + d[OFF + 1:]
    open(OUT, "wb").write(d)
    print("patched (0x80800000) ->", OUT, "md5", hashlib.md5(d).hexdigest())
if not (install or restore):
    print("игровая установка НЕ тронута (запись только с --install/--restore)")
