"""exepatch3.py - borderless-патчи LETHALDREAMS.EXE (первый шаг цепи EXE).

Файл: tools/exe/exepatch3.py. Каноническая команда (из корня репозитория):
  python3 tools/exe/exepatch3.py            # читает EXE установки, пишет в scratch
  python3 tools/exe/exepatch3.py --install  # правки вписываются в EXE установки
(правок в игру без флага нет; скрипту нужен config.toml для paths.game_path)

Цепь EXE (проверено построчно, md5):
  pristine (game.pristine_archive, entry LethalDreams.exe, md5 7943036fbd1233a0dbd43228f129cfc7)
    + этот скрипт (6 правок) = deed9cf640de1fe31ecec6deacad21bd
    + 2 байта 6A 00 -> 6A 01 на fileoff 632177 = baseline 45e93d46aad6ceb3dfc17e86d04e4f82
      (= backup/LETHALDREAMS.EXE.pre4, строит tools/exe/apply_baseline.py)
    + exepatch12.py = 171319bbc170cf6e57c8d36a8ae5f50d = установленный игровой EXE.

Правки:
  1) ChangeDisplaySettingsA(&devmode, 4=CDS_FULLSCREEN) -> CDS_TEST
     fileoff 491308 (rva 0x477f2b+1): 6A 04 -> 6A 02
  2) стиль окна 0x86000000 (WS_POPUP|WS_THICKFRAME|WS_VSCROLL) -> 0x80000000 (WS_POPUP)
     fileoff 149933: 86 -> 80  (client area = ровно WxH)
  3) hbrBackground GetStockObject(4=BLACK_BRUSH) -> (5=NULL_BRUSH)
     fileoff 149798: 6A 04 -> 6A 05  (нет чёрной заливки на WM_ERASEBKGND)
  4) SetDisplayMode(f=1): вызов ChangeDisplaySettingsA заменить на xor eax,eax
     fileoff 491357 (rva 0x477f5d): FF 15 D8 C1 4E 00 -> 33 C0 90 90 90 90
     -> никакого обращения к драйверу дисплея и зависимости от поддержки режима;
        F=1 = popup+SetWindowPos (borderless) при любых w/h из hd.cfg
  5) restore при выходе/смене режима: ChangeDisplaySettingsA(NULL,0) -> nop
     fileoff 490588 (rva 0x477c5c, деструктор FUN_00477af0)  -> 90 * 6
     fileoff 455947 (rva 0x46f50b, FUN_0046f500)              -> 90 * 6
     (режим никогда не менялся -> restore не нужен и только дёргает DP-линк)
"""
import os, sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

EXE = paths.game_path("LETHALDREAMS.EXE")
# выход: без --install — scratch (игру не трогаем); с --install — запись в установку
install = "--install" in sys.argv[1:]
OUT = EXE if install else paths.scratch_dir("exe") + "/LETHALDREAMS.EXE.exepatch3"

PATCHES = [
    (491308, b'\x04', b'\x02', 'CDS_FULLSCREEN -> CDS_TEST (unused after patch 4)'),
    (149933, b'\x86', b'\x80', 'style 0x86000000 -> 0x80000000'),
    (149798, b'\x04', b'\x05', 'hbrBackground BLACK_BRUSH -> NULL_BRUSH'),
    (491357, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x33\xc0\x90', 'skip ChangeDisplaySettings in SetDisplayMode(f=1) + stdcall cleanup'),
    (490588, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x90\x90\x90', 'no restore on destroy + stdcall cleanup'),
    (455947, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x90\x90\x90', 'no restore in FUN_0046f500 + stdcall cleanup'),
]

d = open(EXE, 'rb').read()
for off, old, new, tag in PATCHES:
    cur = d[off:off + len(old)]
    if cur == new:
        print('already patched: %s @ %d' % (tag, off))
        continue
    broken = {
        491357: b'\x33\xc0\x90\x90\x90\x90',
        490588: b'\x90\x90\x90\x90\x90\x90',
        455947: b'\x90\x90\x90\x90\x90\x90',
    }.get(off)
    if cur != old and cur != broken:
        raise SystemExit('unexpected bytes at %d: %s (expected %s) - %s'
                         % (off, cur.hex(), old.hex(), tag))
    was_broken = (cur == broken)
    d = d[:off] + new + d[off + len(new):]
    if was_broken:
        print('fixed broken nop: %s @ %d' % (tag, off))
    else:
        print('patched: %s @ %d' % (tag, off))
open(OUT, 'wb').write(d)
print('done ->', OUT)
if not install:
    print('игровая установка НЕ тронута (запись только с --install)')
