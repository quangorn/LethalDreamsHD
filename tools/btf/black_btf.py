#!/usr/bin/env python3
"""Пакер .btf: делает чёрную (all-zero pixels) текстуру тем же контейнером, что и
оригинальные текстуры игры (для чистых измерений UI-кольца на фоне без арта).

Контейнер: 'LDPACKER' + 4 байта флагов + u32 LE длина сжатого потока + zlib-поток.
Декомпрессированное: 60 байт заголовка (w,h - u32 LE по смещениям 52,56; тот же
заголовок, что у исходника, меняются только пиксели) + pixels (bpp из оригинала).

    python3 tools/btf/black_btf.py                                    -> чёрная пара
        в paths.scratch_dir('black_btf'): mm_bkg01_black_169.btf, mm_bkg01_black_43.btf
    python3 tools/btf/black_btf.py -o OUTDIR                          -> пара в OUTDIR
    python3 tools/btf/black_btf.py <in.btf> <out.btf>                -> чёрная версия
        in.btf (тот же w,h) в out.btf

Шаблоны берутся только из кэша архивов: 4:3 = cache/pristine/data/textures/
mm_bkg01.btf (CRC сверяется с индексом pristine_archive), 16:9 = кэш-экстракция
data/textures/mm_bkg01_169.btf из assets.hd_pack (CRC сверяется с индексом hd_pack).
Сам проект шаблонов не хранит.

Эксперимент с чёрными текстурами НЕ УСТАНОВЛЕН В ИГРЕ: флага data/blackbg.flag в
игровой установке нет (проверено чтением каталога data/), и сайдкаров
mm_bkg01_black_*.btf в игре тоже нет. Выбор чёрного фона делал hd_res.Prepare()
при наличии флага data/blackbg.flag, а созданные файлы клались в игру как
sidecar'ы mm_bkg01_black_*.btf. Сегодня скрипт лишь генерирует пару в scratch.
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402

NAME43 = "data/textures/mm_bkg01.btf"
NAME169 = "data/textures/mm_bkg01_169.btf"


def parse(d):
    """(flags4, decompressed) - контейнер .btf"""
    assert d[:8] == b'LDPACKER', 'не LDPACKER'
    flags = d[8:12]
    clen = struct.unpack('<I', d[12:16])[0]
    out = zlib.decompress(d[16:16 + clen])
    return flags, out


def pack(flags, out):
    c = zlib.compress(out, 6)
    return b'LDPACKER' + flags + struct.pack('<I', len(c)) + c


def black(src, dst):
    d = open(src, 'rb').read()
    flags, out = parse(d)
    w = struct.unpack('<I', out[52:56])[0]
    h = struct.unpack('<I', out[56:60])[0]
    n = len(out) - 60
    bpp = n // (w * h)
    px = bytes(n)  # all black
    new = pack(flags, out[:60] + px)
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    open(dst, 'wb').write(new)
    print('made', dst, w, 'x', h, 'bpp', bpp, 'bytes', len(new))


def _entry(archive, name):
    """Index entry of `name` in a 7-Zip archive (normalized lowercase match)."""
    for e in paths.archive_list_of(archive):
        if e["name"].lower() == name.lower():
            return e
    raise FileNotFoundError("%s has no entry %s" % (archive, name))


def _extract(archive, name, out_dir):
    """One entry out of a 7-Zip archive into out_dir (exact archive name, single
    argv element; the archive path may contain spaces)."""
    proc = subprocess.run(
        ["7z", "x", "-y", "-scsUTF-8", "-o" + str(out_dir), archive, name],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise RuntimeError("7z extract failed for %s\n%s" % (archive, proc.stdout + proc.stderr))


def _verified(archive, name, out_dir):
    """Extract `name` into out_dir if needed and check its CRC against the archive
    index; returns the file path. The cache is rebuilt from the archive, so a CRC
    mismatch is a hard error, not a silent overwrite."""
    f = Path(out_dir) / name
    entry = _entry(archive, name)
    if f.is_file():
        disk_crc = "%08x" % (zlib.crc32(open(f, "rb").read()) & 0xFFFFFFFF)
        if entry["size"] != f.stat().st_size or (entry["crc"] and disk_crc != entry["crc"]):
            raise RuntimeError(
                "cache %s does not match archive %s (archive size=%d crc=%s, disk size=%d crc=%s)\n"
                "delete it and re-run" % (paths.norm(f), paths.norm(archive),
                                           entry["size"], entry["crc"], f.stat().st_size, disk_crc))
        return f
    out_dir.mkdir(parents=True, exist_ok=True)
    _extract(archive, name, out_dir)
    disk_crc = "%08x" % (zlib.crc32(open(f, "rb").read()) & 0xFFFFFFFF)
    if entry["size"] != f.stat().st_size or (entry["crc"] and disk_crc != entry["crc"]):
        raise RuntimeError(
            "extracted %s does not match the archive (archive size=%d crc=%s, disk size=%d crc=%s)"
            % (paths.norm(f), entry["size"], entry["crc"], f.stat().st_size, disk_crc))
    print("template extracted and CRC-verified: %s  crc=%s" % (paths.norm(f), disk_crc))
    return f


def template_43():
    """The 4:3 template: cache/pristine/data/textures/mm_bkg01.btf, CRC-checked
    against the pristine archive index."""
    return _verified(paths.pristine_archive(), NAME43, Path(paths.cache_dir("pristine")))


def template_169():
    """The 16:9 template: cache extraction of assets.hd_pack's
    data/textures/mm_bkg01_169.btf, CRC-checked against the hd_pack index."""
    return _verified(paths.hd_pack(), NAME169, Path(paths.cache_dir("hd_pack")))


def main():
    ap = argparse.ArgumentParser(prog="black_btf.py")
    ap.add_argument("in_btf", nargs="?", help="source .btf (single-file mode)")
    ap.add_argument("out_btf", nargs="?", help="output .btf (single-file mode)")
    ap.add_argument("-o", "--output", default=None,
                    help="out dir for the black pair (default paths.scratch_dir('black_btf'))")
    a = ap.parse_args()

    if a.in_btf and a.out_btf:
        black(a.in_btf, a.out_btf)
        return 0
    out_dir = Path(paths.norm(a.output)) if a.output else Path(paths.scratch_dir("black_btf"))
    out_dir.mkdir(parents=True, exist_ok=True)
    black(str(template_169()), str(out_dir / "mm_bkg01_black_169.btf"))
    black(str(template_43()), str(out_dir / "mm_bkg01_black_43.btf"))
    print("black pair in", paths.norm(out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
