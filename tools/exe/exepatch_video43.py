"""exepatch_video43.py — ЭКСПЕРИМЕНТ: ролики DirectShow в 4:3 по центру экрана.

Файл: tools/exe/exepatch_video43.py. Каноническая команда (из корня репозитория):
  sh tools/exe/build_cave.sh video43                        # пещера из tools/exe/caves/video43.s
  python3 tools/exe/exepatch_video43.py                     # копия в scratch, игру не трогает
  python3 tools/exe/exepatch_video43.py --install           # правки в EXE установки
  python3 tools/exe/exepatch_video43.py --restore           # откат правок в EXE установки

Что делается (три правки):
  1) секция `.rdata` становится исполняемой: flags fileoff 604 `0x40000040` -> `0xe0000040`
     (добавлен IMAGE_SCN_MEM_EXECUTE). Пещера кладётся в zero-run внутри `.rdata`.
  2) пещера (48 B) в zero-run fileoff 1109104 (VA 0x50ec70, зона 916 B):
     считает width = 4*height/3 и left = (client.right - width)/2 и зовёт
     IVideoWindow::SetWindowPosition(this, left, 0, width, height) — видео-окно
     `lethaldreamsvideo` получает 4:3 по центру. При 4:3-окне identity.
  3) сайт fileoff 631586 (VA 0x49a322, FUN_0049a1a0): 22 байта
     `8b4c2414 a1f4d55100 8b10 51 8b4c2414 51 6a00 6a00 50` -> `e9 rel32` + 17 nop;
     возврат из пещеры в VA 0x49a33e (сразу после оригинального вызова).

Цепь EXE: baseline-цепочка (exepatch3 + 2 байта + exepatch12) = установленный EXE
`171319bbc170cf6e57c8d36a8ae5f50d`; этот скрипт добавляет три правки поверх него.
"""
import hashlib
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

EXE = paths.game_path("LETHALDREAMS.EXE")
CAVE_BIN = paths.cache_dir("cave") + "/video43.bin"
BASE_MD5 = "171319bbc170cf6e57c8d36a8ae5f50d"

VA_BASE = 0x400000
SITE_VA = 0x49a322
SITE_RET_VA = 0x49a33e
SITE_ORIG = bytes.fromhex("8b4c2414a1f4d551008b10518b4c2414516a006a0050")
CAVE_VA = 0x50ec70
CAVE_ZONE = 916
FLAGS_OFF = 604
FLAG_RO = 0x40000040
FLAG_RX = 0xE0000040

site_off = SITE_VA - VA_BASE
cave_off = CAVE_VA - VA_BASE

install = "--install" in sys.argv[1:]
restore = "--restore" in sys.argv[1:]
OUT = EXE if (install or restore) else paths.scratch_dir("exe") + "/LETHALDREAMS.EXE.video43"

d = bytearray(open(EXE, "rb").read())
flags = struct.unpack_from("<I", d, FLAGS_OFF)[0]

# --- состояние: что уже применено ---
site_patched = d[site_off:site_off + 22] != SITE_ORIG
cave_patched = any(b != 0 for b in d[cave_off:cave_off + CAVE_ZONE])
flag_patched = flags == FLAG_RX

if restore:
    if flags not in (FLAG_RO, FLAG_RX):
        raise SystemExit("unexpected .rdata flags at %d: %08x" % (FLAGS_OFF, flags))
    d[site_off:site_off + 22] = SITE_ORIG
    d[cave_off:cave_off + CAVE_ZONE] = bytes(CAVE_ZONE)
    struct.pack_into("<I", d, FLAGS_OFF, FLAG_RO)
    open(OUT, "wb").write(d)
    print("restored ->", OUT, "md5", hashlib.md5(d).hexdigest())
    if hashlib.md5(d).hexdigest() != BASE_MD5:
        print("ВНИМАНИЕ: md5 не равен baseline %s" % BASE_MD5)
    if not install:
        print("запись только с --install/--restore")
    raise SystemExit(0)

if site_patched and cave_patched and flag_patched:
    print("уже применено (сайт+пещера+флаг)")
    if install:
        open(OUT, "wb").write(d)
    raise SystemExit(0)

if site_patched or cave_patched:
    raise SystemExit("частично применённое состояние — сначала --restore")

# сайт: проверка оригинальных байтов
if d[site_off:site_off + 22] != SITE_ORIG:
    raise SystemExit("сайт %d: unexpected bytes %s" % (site_off, d[site_off:site_off + 22].hex()))
if flags != FLAG_RO:
    raise SystemExit(".rdata flags at %d = %08x, expected %08x" % (FLAGS_OFF, flags, FLAG_RO))
if set(d[cave_off:cave_off + CAVE_ZONE]) != {0}:
    raise SystemExit("cave zone at %d is not a zero-run" % cave_off)

# пещера: релокация финальной jmp (rel32 -> SITE_RET_VA)
cave = bytearray(open(CAVE_BIN, "rb").read())
if len(cave) > CAVE_ZONE:
    raise SystemExit("cave too big: %d > %d" % (len(cave), CAVE_ZONE))
jmp_at = None
for i in range(len(cave) - 5, -1, -1):
    if cave[i] == 0xE9:
        jmp_at = i
        break
if jmp_at is None:
    raise SystemExit("no trailing jmp in cave blob")
disp = SITE_RET_VA - (CAVE_VA + len(cave))
struct.pack_into("<i", cave, jmp_at + 1, disp)

# сайт: jmp на пещеру + nop до конца 22 байт
site_new = bytearray([0xE9]) + struct.pack("<i", CAVE_VA - (SITE_VA + 5)) + bytes([0x90] * (22 - 5))
d[site_off:site_off + 22] = site_new
d[cave_off:cave_off + len(cave)] = cave
struct.pack_into("<I", d, FLAGS_OFF, FLAG_RX)

open(OUT, "wb").write(d)
print("applied: cave %d B @ VA 0x%x, site jmp @ VA 0x%x, .rdata flags executable"
      % (len(cave), CAVE_VA, SITE_VA))
print("->", OUT, "md5", hashlib.md5(d).hexdigest())
if not (install or restore):
    print("игровая установка НЕ тронута (запись только с --install/--restore)")
