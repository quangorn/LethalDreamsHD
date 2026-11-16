"""exepatch12 = exepatch10 (шрифт 4x в 16:9) + правка проекции мира (geometry fix).

Файл: tools/exe/exepatch12.py. Каноническая команда: `python3 tools/exe/exepatch12.py`
(из корня репозитория; режим `restore` — `python3 tools/exe/exepatch12.py restore`).

Цепь EXE (проверено построчно, md5):
  pristine (game.pristine_archive, entry LethalDreams.exe, md5 7943036fbd1233a0dbd43228f129cfc7)
    + exepatch3.py (6 правок) = deed9cf640de1fe31ecec6deacad21bd
    + 2 байта 6A 00 -> 6A 01 на fileoff 632177..632178 = baseline 45e93d46aad6ceb3dfc17e86d04e4f82
      (= backup/LETHALDREAMS.EXE.pre4, строит tools/exe/apply_baseline.py)
    + этот скрипт (exepatch12) = 171319bbc170cf6e57c8d36a8ae5f50d = установленный игровой EXE.

Сайты:
  site1 0x41ce31 (хвост FUN_0041cc90)              -> cave1 (масштаб листов/advance)
  site2 0x40b494 (сеттер шрифта из SetFont)        -> cave2
  site3 0x47dd80 (RenderWorld FUN_0047dd30: `call FUN_0046f970(ctx,2,matrix)`)
        -> cave3: перед сеттером matrix[0] := matrix[5] * h/w
           (w,h = [DAT_0051cc7c]+0x18/+0x1c). Live-подтверждено x_ld_proj.py:
           ratio кольца домена 1.4035 -> 1.0575 (4:3 = 1.06).
При 4:3 (h/w = 0.75 точно) движок уже хранит matrix[0] = 0.75*matrix[5],
запись даёт побитово то же число -> identity, 4:3 регрессия не затронута.
cave1 null-проверки вырезаны; blob 236 байта (`fixall.bin`, tools/exe/caves/fixall.s)
в zero-run VA 0x4ebf12 (лимит зоны 238; exepatch12 проверяет len(cave) <= 238).

  python3 tools/exe/exepatch12.py            # baseline -> scratch (игру не трогает)
  python3 tools/exe/exepatch12.py --install  # записать патченный EXE в установку
  python3 tools/exe/exepatch12.py restore    # вернуть baseline (в выходной файл)

Выходной файл: с --install — paths.game_path("LETHALDREAMS.EXE"); без флага —
scratch_dir('exe')/LETHALDREAMS.EXE.patched. Запись в установку только по флагу
(правило AGENTS.md). BAK = backup/LETHALDREAMS.EXE.pre4 (читается); CAVE_BIN =
paths.cache_dir('cave')/fixall.bin (собирается `sh tools/exe/build_cave.sh fixall`).
LD_CONFIG перенаправляет install_dir и cache; LD_BAK перенаправляет BAK (нужно
для тестов на фейковой игровой цепи).
"""
import hashlib, shutil, os, struct, sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

BAK = os.environ.get("LD_BAK") or (paths.norm(_ROOT) + "/backup/LETHALDREAMS.EXE.pre4")
MD5_BASE = "45e93d46aad6ceb3dfc17e86d04e4f82"

CAVE_BIN = paths.cache_dir("cave") + "/fixall.bin"
CAVE_VA = 0x4ebf12
# offsets символов из nm (fixall.o): cave2 +0x7f, cave3 +0xa7, c3call +0xca, c3jmp +0xcf
CAVE2_OFF = 0x7F
CAVE3_OFF = 0xA7
C3CALL_OFF = 0xCA
C3JMP_OFF = 0xCF

SITE1_VA = 0x41ce31
SITE1_SIG = bytes.fromhex("c641580159")          # mov byte [ecx+0x58],1; pop ecx
SITE2_VA = 0x40b494
SITE2_SIG = bytes.fromhex("898194020000")        # mov [ecx+0x294],eax
SITE3_VA = 0x47dd80
SITE3_SIG = bytes.fromhex("e8eb1bffff")          # call FUN_0046f970 (5 байт)

# целевые адреса внутри блоба после перемещения в CAVE_VA
C3_CALL_TARGET = 0x46f970
C3_JMP_TARGET = 0x47dd85   # продолжение RenderWorld после оригинального call


def va2off(va):
    return va - 0x400000


def main():
    args = sys.argv[1:]
    install = "--install" in args
    out = paths.game_path("LETHALDREAMS.EXE") if install \
        else paths.scratch_dir("exe") + "/LETHALDREAMS.EXE.patched"
    shutil.copy2(BAK, out)
    d = bytearray(open(out, "rb").read())
    assert hashlib.md5(d).hexdigest() == MD5_BASE, "backup is not baseline"

    if "restore" in args:
        open(out, "wb").write(bytes(d))
        print("restored baseline", hashlib.md5(d).hexdigest(), "->", out)
        return

    cave = bytearray(open(CAVE_BIN, "rb").read())
    assert len(cave) <= 238, len(cave)
    assert cave[0:4] == bytes.fromhex("c6415801"), cave[0:4].hex()

    # relocation 1: x87 absolute disp32 (fmul/fsub m32/m64) -> CAVE_VA + disp
    n = 0
    i = 0
    while i < len(cave) - 5:
        if cave[i] in (0xD8, 0xDC) and cave[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            disp = struct.unpack_from("<I", cave, i + 2)[0]
            if disp < len(cave):
                struct.pack_into("<I", cave, i + 2, CAVE_VA + disp)
                n += 1
                i += 6
                continue
        i += 1
    assert n == 4, n

    # relocation 2: rel32 поля call/jmp в cave3 -> точные цели при размещении в CAVE_VA
    struct.pack_into("<i", cave, C3CALL_OFF + 1, C3_CALL_TARGET - (CAVE_VA + C3CALL_OFF + 5))
    struct.pack_into("<i", cave, C3JMP_OFF + 1, C3_JMP_TARGET - (CAVE_VA + C3JMP_OFF + 5))

    o1 = va2off(SITE1_VA)
    assert d[o1:o1 + 5] == SITE1_SIG, d[o1:o1 + 5].hex()
    o2 = va2off(SITE2_VA)
    assert d[o2:o2 + 6] == SITE2_SIG, d[o2:o2 + 6].hex()
    o3 = va2off(SITE3_VA)
    assert d[o3:o3 + 5] == SITE3_SIG, d[o3:o3 + 5].hex()

    cave_off = va2off(CAVE_VA)
    assert set(d[cave_off:cave_off + len(cave)]) == {0}, "cave zone is not zero-run"
    assert cave_off + len(cave) <= va2off(0x4EC000), "cave does not fit"
    d[cave_off:cave_off + len(cave)] = cave

    d[o1:o1 + 5] = b"\xe9" + (CAVE_VA - (SITE1_VA + 5)).to_bytes(4, "little", signed=True)
    cave2_va = CAVE_VA + CAVE2_OFF
    d[o2:o2 + 6] = b"\xe9" + (cave2_va - (SITE2_VA + 5)).to_bytes(4, "little", signed=True) + b"\x90"
    cave3_va = CAVE_VA + CAVE3_OFF
    d[o3:o3 + 5] = b"\xe9" + (cave3_va - (SITE3_VA + 5)).to_bytes(4, "little", signed=True)

    assert len(d) == os.path.getsize(BAK)
    open(out, "wb").write(bytes(d))
    print("patched: blob %d bytes at 0x%x; site1 -> 0x%x, site2 -> 0x%x, site3 -> 0x%x; md5=%s -> %s"
          % (len(cave), CAVE_VA, CAVE_VA, cave2_va, cave3_va, hashlib.md5(d).hexdigest(), out))


main()
