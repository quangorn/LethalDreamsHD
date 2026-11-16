"""apply_baseline.py - собирает baseline EXE из pristine-архива и кладёт его в
backup/LETHALDREAMS.EXE.pre4 (файл, который exepatch12.py считает baseline).

Файл: tools/exe/apply_baseline.py. Каноническая команда:
  python3 tools/exe/apply_baseline.py                  # пишет backup/LETHALDREAMS.EXE.pre4
  python3 tools/exe/apply_baseline.py --output <file>  # писать в scratch-файл (тесты)
  python3 tools/exe/apply_baseline.py --install         # copy built baseline into install_dir/LETHALDREAMS.EXE

Цепь EXE (проверено построчно, md5):
  pristine (game.pristine_archive, entry LethalDreams.exe, md5 7943036fbd1233a0dbd43228f129cfc7)
    + exepatch3.py (6 правок) = deed9cf640de1fe31ecec6deacad21bd      (промежуточный)
    + 2 байта 6A 00 -> 6A 01 на fileoff 632177..632178 = baseline 45e93d46aad6ceb3dfc17e86d04e4f82
    + exepatch12.py = 171319bbc170cf6e57c8d36a8ae5f50d = установленный игровой EXE.

Скрипт идемпотентен: если целевой файл уже равен baseline, он только сообщает это.
Запись в игровую установку — только под явным флагом --install.
"""
import hashlib
import os
import subprocess
import sys
import tempfile
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

ENTRY = "LethalDreams.exe"          # entry в pristine-архиве
MD5_PRISTINE = "7943036fbd1233a0dbd43228f129cfc7"
MD5_STEP3 = "deed9cf640de1fe31ecec6deacad21bd"     # после 6 правок exepatch3
MD5_BASE = "45e93d46aad6ceb3dfc17e86d04e4f82"      # baseline = backup/LETHALDREAMS.EXE.pre4

# те же 6 правок, что и в tools/exe/exepatch3.py (порядок применения сохранён)
PATCHES = [
    (491308, b'\x04', b'\x02', 'CDS_FULLSCREEN -> CDS_TEST (unused after patch 4)'),
    (149933, b'\x86', b'\x80', 'style 0x86000000 -> 0x80000000'),
    (149798, b'\x04', b'\x05', 'hbrBackground BLACK_BRUSH -> NULL_BRUSH'),
    (491357, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x33\xc0\x90', 'skip ChangeDisplaySettings in SetDisplayMode(f=1) + stdcall cleanup'),
    (490588, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x90\x90\x90', 'no restore on destroy + stdcall cleanup'),
    (455947, b'\xff\x15\xd8\xc1\x4e\x00', b'\x83\xc4\x08\x90\x90\x90', 'no restore in FUN_0046f500 + stdcall cleanup'),
]
# 2-байтная правка pre4: x87-константа 0 -> 1 (k=4x режим листов), fileoff 632177..632178
TWO_BYTE = (632177, b'\x6a\x00', b'\x6a\x01', 'pre4 flag: 6A 00 -> 6A 01')


def md5_of(data):
    return hashlib.md5(data).hexdigest()


def extract_pristine_exe(work):
    """Достаёт entry LethalDreams.exe из game.pristine_archive в scratch-каталог
    и сверяет CRC32 с индексом архива (paths.archive_list)."""
    archive = paths.pristine_archive()
    entry = None
    for e in paths.archive_list_of(archive):
        if e["name"] == ENTRY:
            entry = e
            break
    if entry is None:
        raise SystemExit("entry %s not found in archive %s" % (ENTRY, archive))

    out_dir = paths.scratch_dir(work)
    proc = subprocess.run(
        ["7z", "x", "-y", "-o" + paths.win_path(out_dir), paths.win_path(archive), ENTRY],
        capture_output=True, text=True, stdin=subprocess.DEVNULL,
    )
    if proc.returncode != 0:
        raise SystemExit("7z extraction failed for %s\n%s" % (archive, proc.stdout + proc.stderr))
    p = paths.norm(os.path.join(out_dir, ENTRY))
    data = open(p, "rb").read()
    if len(data) != entry["size"]:
        raise SystemExit("extracted size %d != archive size %d" % (len(data), entry["size"]))
    crc = "%08x" % (zlib.crc32(data) & 0xFFFFFFFF)
    if crc != entry["crc"]:
        raise SystemExit("extracted CRC32 %s != archive CRC32 %s" % (crc, entry["crc"]))
    print("extracted: %s from %s" % (ENTRY, archive))
    print("  size %d, crc32 %s (archive %s) - match" % (len(data), crc, entry["crc"]))
    h = md5_of(data)
    print("  md5 %s" % h)
    if h != MD5_PRISTINE:
        raise SystemExit("pristine md5 %s != expected %s" % (h, MD5_PRISTINE))
    return data


def apply(data, off, old, new, tag):
    """Правка с проверкой ожидаемых байт перед записью."""
    cur = data[off:off + len(old)]
    if cur == new:
        print("  already: %s @ %d" % (tag, off))
        return data
    if cur != old:
        raise SystemExit("unexpected bytes at %d: %s (expected %s) - %s"
                         % (off, cur.hex(), old.hex(), tag))
    data = data[:off] + new + data[off + len(new):]
    print("  patched: %s @ %d" % (tag, off))
    return data


def main():
    args = sys.argv[1:]
    install = "--install" in args
    out = None
    if "--output" in args:
        out = args[args.index("--output") + 1]

    # целевой файл: --output (scratch-тесты), иначе backup/LETHALDREAMS.EXE.pre4
    target = out
    if target is None:
        bak_dir = paths.norm(_ROOT) + "/backup"
        if not os.path.isdir(bak_dir):
            raise SystemExit(
                "каталог backup/ отсутствует: %s\n"
                "каноническая команда: python3 tools/exe/apply_baseline.py "
                "[--output <file>] [--install]" % bak_dir)
        target = bak_dir + "/LETHALDREAMS.EXE.pre4"

    if os.path.isfile(target) and md5_of(open(target, "rb").read()) == MD5_BASE:
        print("already baseline: %s (md5 %s) - ничего не делаем" % (target, MD5_BASE))
        return

    data = extract_pristine_exe("apply_baseline")
    for off, old, new, tag in PATCHES:
        data = apply(data, off, old, new, tag)
    h = md5_of(data)
    print("step3 md5: %s" % h)
    if h != MD5_STEP3:
        raise SystemExit("md5 после 6 правок %s != expected %s" % (h, MD5_STEP3))

    off, old, new, tag = TWO_BYTE
    data = apply(data, off, old, new, tag)
    h = md5_of(data)
    print("baseline md5: %s" % h)
    if h != MD5_BASE:
        raise SystemExit("baseline md5 %s != expected %s" % (h, MD5_BASE))

    d = paths.norm(os.path.dirname(target))
    os.makedirs(d, exist_ok=True)
    open(target, "wb").write(data)
    print("written: %s" % target)
    print("verified: md5 %s == %s" % (md5_of(open(target, "rb").read()), MD5_BASE))

    if install:
        exe = paths.game_path("LETHALDREAMS.EXE")
        with open(target, "rb") as fh, open(exe, "wb") as gh:
            gh.write(fh.read())
        print("installed baseline: %s (md5 %s)" % (exe, md5_of(open(exe, "rb").read())))


main()
