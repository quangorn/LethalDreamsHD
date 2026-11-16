"""exepatch10: 16:9-шрифт на 4x-листах (сайдкары *_169) + коэффициент на advance.

Отличие от exepatch9:
  * в пещере перед масштабом метрик `k` домножается на SHEET=0.25 (листы апскейлены
    в 4x), поэтому метрики на экране те же, а текстура вчетверо плотнее;
  * advance масштабируется как `v = (a_sheet - CORR) * k_eff` (с carry):
    CORR = 4*A, где A = ошибка межбуквенного интервала в канвасных единицах.
    Для RowS=-3 при k=0.75 A = 0.3346, т.е. CORR = 1.3384 - этим снимается
    систематическое "+0.33 единицы на символ" (строки были шире, чем в 4:3).
    CORR = 0 -> только 4x-лист, без компенсации ширины (шаг 1 итерации).
  * при 4:3 (k >= 1) пещера по-прежнему identity: правка побитово ничего не меняет,
    листы родные (*_43).

Сайты те же, что у exepatch9: 0x41ce31 (хвост FUN_0041cc90) -> cave1,
0x40b494 (сеттер шрифта из SetFont) -> cave2.

  python3 exepatch10.py            # применить
  python3 exepatch10.py restore    # вернуть baseline
"""
import hashlib, shutil, os, struct, sys


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
P = paths.game_path('LETHALDREAMS.EXE')
BAK = os.path.join(_ROOT, 'backup', 'LETHALDREAMS.EXE.pre4')
CAVE_BIN = os.path.join(paths.cache_dir('cave'), 'fontscale4x.bin')
MD5_BASE = "45e93d46aad6ceb3dfc17e86d04e4f82"

CAVE_VA = 0x4ebf12

SITE1_VA = 0x41ce31
SITE1_SIG = bytes.fromhex("c641580159")
SITE2_VA = 0x40b494
SITE2_SIG = bytes.fromhex("898194020000")   # mov [ecx+0x294],eax
CAVE2_SIG = bytes.fromhex("89819402")


def va2off(va):
    return va - 0x400000


def relocate(cave):
    """Все модификаторы памяти с absolute disp32 (x87 m32/m64) -> CAVE_VA+disp."""
    pat = 0
    i = 0
    while i < len(cave) - 5:
        if cave[i] in (0xD8, 0xDC) and cave[i + 1] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            disp = struct.unpack_from("<I", cave, i + 2)[0]
            if disp < len(cave):
                struct.pack_into("<I", cave, i + 2, CAVE_VA + disp)
                pat += 1
                i += 6
                continue
        i += 1
    return pat


def main():
    shutil.copy2(BAK, P)
    d = bytearray(open(P, "rb").read())
    assert hashlib.md5(d).hexdigest() == MD5_BASE, "backup is not baseline"

    if len(sys.argv) > 1 and sys.argv[1] == "restore":
        print("restored baseline", hashlib.md5(d).hexdigest())
        return

    cave = bytearray(open(CAVE_BIN, "rb").read())
    assert cave[0:4] == bytes.fromhex("c6415801"), cave[0:4].hex()
    cave2_off = cave.find(CAVE2_SIG, 0x40)
    assert cave2_off > 0, "cave2 not found"
    n = relocate(cave)
    assert n == 4, n
    print("blob %d bytes, cave2 at +0x%x, relocs %d" % (len(cave), cave2_off, n))

    o1 = va2off(SITE1_VA)
    assert d[o1:o1 + 5] == SITE1_SIG, d[o1:o1 + 5].hex()
    o2 = va2off(SITE2_VA)
    assert d[o2:o2 + 6] == SITE2_SIG, d[o2:o2 + 6].hex()

    cave_off = va2off(CAVE_VA)
    assert set(d[cave_off:cave_off + len(cave)]) == {0}, "cave zone is not zero-run"
    assert cave_off + len(cave) <= va2off(0x4EC000), "cave does not fit"
    d[cave_off:cave_off + len(cave)] = cave

    rel1 = CAVE_VA - (SITE1_VA + 5)
    d[o1:o1 + 5] = b"\xe9" + rel1.to_bytes(4, "little", signed=True)
    cave2_va = CAVE_VA + cave2_off
    rel2 = cave2_va - (SITE2_VA + 5)
    d[o2:o2 + 6] = b"\xe9" + rel2.to_bytes(4, "little", signed=True) + b"\x90"

    assert len(d) == os.path.getsize(BAK)
    open(P, "wb").write(bytes(d))
    print("patched: site1 0x%x -> 0x%x, site2 0x%x -> 0x%x, md5=%s"
          % (SITE1_VA, CAVE_VA, SITE2_VA, cave2_va, hashlib.md5(d).hexdigest()))


main()
