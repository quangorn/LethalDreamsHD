#!/usr/bin/env python3
"""Фаза 4 (PLAN_proxy_dll.md §4): аудит трёх сайтов хуков на диске — без игры.

Сканирует `.text` EXE ровно так, как это делает `ldhook.pyd` (границы секции из
PE-заголовков, только `VirtualSize`; zero-run пещеры `0x4ebf12..0x4ec000` лежит
ЗА `VirtualSize` и в скан не попадает). Для каждой сигнатуры `exepatch12.py`
печатает число вхождений и VA.

Файлы по умолчанию: установленный EXE (`config[game.install_dir]`) и baseline
(`backup/LETHALDREAMS.EXE.pre4`, `LD_BAK`). Вердикты:

  installable  — все 3 сигнатуры по одному разу в `.text` → in-memory хуки
                 (`ldhook.pyd`) встанут (baseline `45e93d46…`);
  patched      — сигнатур нет, на сайтах статические `E9 rel32` → это cave-EXE
                 `exepatch12` (`171319bb…`); `ldhook.pyd` штатно ничего не
                 ставит (no-op), игра работает на cave;
  UNEXPECTED   — иначе: новая правка EXE сдвинула/унесла сайты (rc 1).

    python3 tools/ldhook/check_sites.py
    python3 tools/ldhook/check_sites.py --exe <путь к EXE> [--exe ...]
"""
import hashlib
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

BAK = os.environ.get("LD_BAK") or (paths.norm(_ROOT) + "/backup/LETHALDREAMS.EXE.pre4")

SITES = (
    # имя, сигнатура, ожидаемые VA в baseline, ожидаемое число вхождений
    ("site1", bytes.fromhex("c641580159"), (0x41CE31,), 1),
    ("site2", bytes.fromhex("898194020000"), (0x40B494,), 1),
    ("site3", bytes.fromhex("e8eb1bffff"), (0x47DD80,), 1),
    # trace-наблюдатели тика (PLAN_BATTLE_AI_CPP.md 1.7/2.0)
    ("h1", bytes.fromhex("8be9745fd98514010000"), (0x44BDF5,), 1),
    # ядро H2 (8B 8C 24 4C 01 00 00) встречается и на 0x420d90; сигнатура
    # расширена хвостом эпилога (pop edi/esi/ebp) до уникальной.
    ("h2", bytes.fromhex("8b8c244c0100005f5e5d"), (0x44D074,), 1),
    ("h7", bytes.fromhex("83ec60538b5c2468"), (0x45DD70,), 1),
    # H3 0x44cbc5: movl $0,0xc0(%esp) + call 45eee0 (16 Б, уникальна).
    ("h3", bytes.fromhex("c78424c0" + "00" * 7 + "e80b230100"),
     (0x44CBC5,), 1),
    # H5: эпилог e0a0 `add esp,0x34; ret 0x10`. Сигнатура НЕ уникальна для e0a0
    # (6 копий в .text; 0x45cdaf/0x45ce77 — другие функции), поэтому установка
    # site5 ограничена окном 0x45e0a0..0x45e1e0: хукаются 4 копии 0x45e16d/
    # 0x45e187/0x45e1a1/0x45e1bb.
    ("h5", bytes.fromhex("83c434c21000"),
     (0x45CDAF, 0x45CE77, 0x45E16D, 0x45E187, 0x45E1A1, 0x45E1BB), 6),
    # H6: хвост цикла e0a0 (mov esi,eax; cmp esi,ebp; jne) — тот же сайт, что
    # у site5/site6 (0x45e163).
    ("h6", bytes.fromhex("8bf03bf575ae"), (0x45E163,), 1),
    # R1..R3: пиннинг seed'а (LDHOOK_SEED); R2-сигнатура встречается дважды.
    ("r1", bytes.fromhex("ff157cc24e0050"), (0x49EBCF,), 1),
    ("r2", bytes.fromhex("ff15b0c04e0050"), (0x4A7677, 0x4AE87F), 2),
    # E0-diag (LDHOOK_E0_DIAG=1): 5 CALL-сайтов ветки 1 FUN_0045dd70
    # (leaf_sites diag=1) + пролог FUN_0045dd00 (пролог не уникален в .text).
    ("e0n_lb", bytes.fromhex("e8f06cfdff"), (0x45DECB,), 1),
    ("e0n_grid", bytes.fromhex("e8d68fffff"), (0x45DEF5,), 1),
    ("e0n_path", bytes.fromhex("e824ecffff"), (0x45DF17,), 1),
    ("e0n_pfs", bytes.fromhex("e8afe3ffff"), (0x45DF2C,), 1),
    ("e0n_reg", bytes.fromhex("e855fdffff"), (0x45DFA6, 0x464976, 0x4E1E77), 3),
    ("e0n_reg_prolog", bytes.fromhex("83ec18568b742420"), (0x4399D0, 0x45DD00), 2),
)

MD5_BASELINE = "45e93d46aad6ceb3dfc17e86d04e4f82"
MD5_INSTALL = "171319bbc170cf6e57c8d36a8ae5f50d"


def parse_text(data):
    """(ImageBase, .text VA, .text VirtualSize, .text raw offset, raw size)."""
    if data[:2] != b"MZ":
        raise SystemExit("не PE-файл (нет MZ)")
    e = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e:e + 4] != b"PE\0\0":
        raise SystemExit("не PE-файл (нет PE-подписи)")
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    optsz = struct.unpack_from("<H", data, e + 20)[0]
    base = struct.unpack_from("<I", data, e + 24 + 28)[0]
    off = e + 24 + optsz
    for _ in range(nsec):
        name = data[off:off + 8].rstrip(b"\0")
        if name == b".text":
            vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
            if vsize == 0:
                vsize = rsize
            return base, va, vsize, raw, rsize
        off += 40
    raise SystemExit("в PE нет секции .text")


def scan(data):
    base, va, vsize, raw, rsize = parse_text(data)
    text = data[raw:raw + rsize][:vsize]
    found = []
    for name, sig, want, expect in SITES:
        pos = []
        i = text.find(sig)
        while i >= 0:
            pos.append(base + va + i)
            i = text.find(sig, i + 1)
        found.append((name, sig, want, expect, pos))
    return base, va, vsize, raw, found


def bytes_at_va(data, base, text_va, raw, want, n):
    delta = want - (base + text_va)
    if delta < 0:
        return b""
    return data[raw + delta:raw + delta + n]


def audit(path):
    data = open(path, "rb").read()
    md5 = hashlib.md5(data).hexdigest()
    base, va, vsize, raw, found = scan(data)
    print("%s" % path)
    print("  md5=%s size=%d .text base=0x%08x VA=0x%08x VirtualSize=0x%x"
          % (md5, len(data), base, base + va, vsize))

    counts = [len(pos) for (_n, _s, _w, _e, pos) in found]
    for name, sig, want, expect, pos in found:
        occ = ", ".join("0x%08x" % p for p in pos) if pos else "-"
        note = ""
        if pos != list(want) or len(pos) != expect:
            note = " (ожидалось %s)" % (
                ", ".join("0x%08x" % p for p in want) if expect == 1
                else "%d шт.: %s" % (expect, ", ".join("0x%08x" % p for p in want)))
        print("  %-5s %-12s count=%d  at %s%s"
              % (name, sig.hex(), len(pos), occ, note))

    baseline_ok = all(len(pos) == expect and pos == list(want)
                      for (_n, _s, want, expect, pos) in found)

    # Вердикт `patched` относится к HD-cave (site1/2/3): установленный
    # exepatch12 заменяет их статическими E9, боевой регион остаётся как в
    # baseline (поэтому trace-сайты на месте и в установке).
    hd = found[:3]
    hd_zero = all(len(pos) == 0 for (_n, _s, _w, _e, pos) in hd)
    hd_e9 = all(bytes_at_va(data, base, va, raw, want[0], 1) == b"\xe9"
                for (_n, _s, want, _e, _p) in hd)

    if baseline_ok:
        verdict = "installable"
        print("  VERDICT: installable — in-memory хуки ldhook.pyd встанут")
    elif hd_zero and hd_e9:
        verdict = "patched"
        print("  VERDICT: patched — статические E9 на site1/2/3 "
              "(cave exepatch12); ldhook.pyd штатно no-op для HD, "
              "trace-сайты не тронуты")
    else:
        verdict = "UNEXPECTED"
        print("  VERDICT: UNEXPECTED — сигнатуры не совпали с ожиданием "
              "(site сдвинулся/пропал или сигнатура не уникальна)")
    print("")
    return md5, verdict


def main():
    argv = sys.argv[1:]
    exes = []
    i = 0
    while i < len(argv):
        if argv[i] == "--exe" and i + 1 < len(argv):
            exes.append(argv[i + 1])
            i += 2
        else:
            raise SystemExit("usage: check_sites.py [--exe PATH ...]\n"
                             "  --exe задаёт файлы; по умолчанию установка + baseline")
    if not exes:
        exes = [paths.game_path("LETHALDREAMS.EXE"), BAK]

    ok = True
    for path in exes:
        if not os.path.isfile(path):
            print("MISSING %s" % path)
            ok = False
            continue
        md5, verdict = audit(path)
        if md5 == MD5_BASELINE and verdict != "installable":
            print("  ! baseline обязан быть installable")
            ok = False
        if md5 == MD5_INSTALL and verdict != "patched":
            print("  ! установленный cave-EXE обязан быть patched")
            ok = False
        if verdict == "UNEXPECTED":
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
