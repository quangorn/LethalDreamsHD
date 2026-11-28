#!/usr/bin/env python3
"""check_state.py — read-only чекер состояния игровой установки Lethal Dreams.

Каноническая форма (из корня репозитория):
    python3 tools/check_state.py --quick            code tier
    python3 tools/check_state.py --full             code + assets tier
    python3 tools/check_state.py --untouched        сверка с pristine-архивом
    python3 tools/check_state.py --full --json отчёт.json   (файл обязателен
        в paths.scratch_dir()/paths.cache_dir(), отчёты в проект не пишутся)

Чекер НИЧЕГО не пишет в игровую установку, в patches/, в build/. Допустимые
записи — только индексы архивов и отчёты в cache (paths.archive_list_of и
paths.cache_dir('check_state')). Ничего не лечит: для каждого нарушения печатает
fix_command из манифеста.

Выходы: rc 0 — чисто; rc 1 — есть нарушения; rc 2 — конфигурация невалидна
(paths.validate_required) или --json вне scratch/cache.

Три tier'а (единственный легальный способ судить об установке):
  --quick    : записи code tier манифеста (EXE, 3 DLL, hd.cfg (сверка первой
              строки `W H BPP F`), 12 .pyc, реестр HIGHDPIAWARE) — сравнение
              md5/crc32 с эталонами.
  --full     : --quick + assets tier: каждая запись hd_pack присутствует побитово
              (CRC32+size); набор data/fonts == шрифтовой набор hd_pack; каждый
              игровой .btf имеет CRC32 из {pristine} U {hd_pack}.
  --untouched: каждый файл установки вне code/asset-классов обязан быть побитово
              pristine (путь, затем basename+пара); отклонения вне allowlist ->
              DRIFT; файл не найден ни в одном архиве и не в allowlist -> EXTRA;
              запись pristine-архива отсутствует в установке и не в exclude ->
              запись pristine-архива отсутствует в установке и не в exclude ->
              MISSING. Allowlist = SNAPSHOT_ONLY + REPO + REPO_BASENAME + NAMED_LEGALS +
              dev_artifacts + check.exclude (data/video/**) + побитовая пара пути из
              коммитного индекса снапшота `release/snapshot_index.json`
              (классифицируется после PRISTINE; пустая запись индекса —
              пара по размеру).
"""
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import zlib

# tools -> корень репозитория (бутстрап проекта)
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

MANIFEST = os.path.join(_ROOT, "release", "manifest.json")
ALLOWLIST = os.path.join(_ROOT, "release", "allowlist.json")
SNAPSHOT_INDEX = os.path.join(_ROOT, "release", "snapshot_index.json")

EXE_BASE = "LETHALDREAMS.EXE"


def md5_crc(path):
    """md5 + crc32 (8 hex) за один проход файла."""
    h = hashlib.md5()
    crc = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
            crc = zlib.crc32(chunk, crc)
    return h.hexdigest(), "%08x" % (crc & 0xFFFFFFFF)


def decode_console(data):
    """Вывод reg/7z в OEM-кодовой странице (cp866 на русской Windows)."""
    for enc in ("cp866", "cp1251", "utf-8"):
        try:
            return data.decode(enc)
        except (LookupError, UnicodeDecodeError):
            continue
    return data.decode("utf-8", errors="replace")


def glob_to_regex(pattern):
    """Glob из check.exclude -> регулярка. `**` = любой путь, `*` = без слэша.

    Пути для сравнения — lowercase, прямые слэши, относительные install_dir.
    """
    p = paths.norm(pattern).lower()
    out = []
    i = 0
    while i < len(p):
        if p[i] == "*":
            if p[i:i + 2] == "**":
                out.append(".*")
                i += 2
                if i < len(p) and p[i] == "/":
                    i += 1
                    out.append("(?:/|$)")  # data/video/** = каталог и всё внутри
                continue
            out.append("[^/]*")
            i += 1
        elif p[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(p[i]))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def walk_install(install):
    """Все файлы установки: lowercase-путь (относительный) -> абсолютный путь."""
    out = {}
    for r, d, fs in os.walk(install):
        d[:] = [x for x in d if x != "__pycache__"]
        for f in fs:
            p = os.path.join(r, f)
            rel = os.path.relpath(p, install).replace("\\", "/").lower()
            out[rel] = p
    return out


def snapshot_index():
    """Коммитный индекс снапшота установки: {имя_без_корня: [(size, crc)]}.
    Формат — release/snapshot_index.json (paths.snapshot_index_path())."""
    doc = json.load(open(SNAPSHOT_INDEX, encoding="utf-8"))
    idx = {}
    for e in doc["entries"]:
        idx.setdefault(e["path"].lower(), []).append((e["size"], e["crc"]))
    return idx


def archive_index(archive, strip_root="lethaldreams/"):
    """Нижнийрегистровый индекс {имя_без_корня: [(size, crc)]} из archive_list_of."""
    idx = {}
    for e in paths.archive_list_of(archive):
        n = e["name"].lower()
        if n.startswith(strip_root):
            n = n[len(strip_root):]
        idx.setdefault(n, []).append((e["size"], e["crc"]))
    return idx


def check_registry():
    """Реестр HIGHDPIAWARE для EXE установки (reg query, argv-список)."""
    man = json.load(open(MANIFEST, encoding="utf-8"))
    reg = [e for e in man["code_tier"]["entries"] if e["kind"] == "registry"][0]
    key = reg["key"]
    value_name = paths.install_dir().replace("/", "\\") + "\\LETHALDREAMS.EXE"
    proc = subprocess.run(["reg", "query", key, "/v", value_name],
                          capture_output=True)
    text = decode_console(proc.stdout)
    if proc.returncode == 0 and "~ HIGHDPIAWARE" in text:
        return True, value_name, reg
    return False, value_name, reg


def code_tier(install_files, strict):
    """Сверка записей code tier манифеста с установкой.

    strict=True (--quick): строгий гейт — любое отклонение от эталона есть
    нарушение, кроме объяснённых сборок `drift_known_md5` (они печатаются KNOWN
    и в strict). Исторический DRIFT_RELEASE_AHEAD (`installed_recorded_md5`) в strict — нарушение.
    strict=False (--full): известные отклонения из манифеста печатаются KNOWN и
    НЕ считаются нарушениями: установка объяснима целиком.
    """
    man = json.load(open(MANIFEST, encoding="utf-8"))
    lines = []
    viol = 0
    for e in man["code_tier"]["entries"]:
        if e["kind"] == "registry":
            ok, name, reg = check_registry()
            if ok:
                lines.append("OK registry %s ~ HIGHDPIAWARE" % name)
            else:
                viol += 1
                lines.append("DRIFT registry %s (HIGHDPIAWARE отсутствует)" % name)
                lines.append("   fix_command: %s" % reg["fix_command"])
            continue
        target = e["target"]
        ap = install_files.get(target.lower())
        if ap is None:
            viol += 1
            lines.append("MISSING %s (%s)" % (target, e["kind"]))
            lines.append("   fix_command: %s" % e["fix_command"])
            continue
        m, c = md5_crc(ap)
        if e["kind"] == "cfg":
            # семантика hd.cfg: первая строка `W H BPP F`; игра при каждом запуске
            # пересоздаёт файл и дописывает пустую строку -> сверяем непустое содержимое
            want = b"3840 2160 32 1"
            data = open(ap, "rb").read()
            if data.strip() == want:
                tail = len(data) - len(want)
                lines.append("OK cfg %s (%d B, первая строка побайтно%s)" %
                             (target, len(data), ", хвост %d B — писала игра" % tail if tail else ""))
            else:
                viol += 1
                lines.append("DRIFT cfg %s: содержимое != b'3840 2160 32 1\\r\\n'" % target)
                lines.append("   fix_command: %s" % e["fix_command"])
            continue
        if m == e["md5"]:
            state = e.get("state", "MATCH")
            lines.append("OK %s %s md5 %s (эталон, state %s)" % (e["kind"], target, m[:12], state))
        elif e.get("drift_known_md5") and m == e["drift_known_md5"]:
            # объяснённая сборка (drift_known_md5) — KNOWN в обоих ярусах: она
            # воспроизводится канонической командой, и strict-ярус её не флагит
            state = e.get("state", "DRIFT_KNOWN")
            tag = "KNOWN"
            lines.append("%s %s %s: установленный md5 %s != эталон %s (state %s)" %
                         (tag, e["kind"], target, m[:12], e["md5"][:12], state))
            lines.append("   note: %s" % e.get("drift_known_note", ""))
            lines.append("   fix_command: %s" % e["fix_command"])
        elif m == e.get("installed_recorded_md5"):
            state = e.get("state", "DRIFT")
            if strict:
                viol += 1
                tag = "DRIFT"
            else:
                tag = "KNOWN"
            lines.append("%s %s %s: установленный md5 %s != эталон %s (state %s)" %
                         (tag, e["kind"], target, m[:12], e["md5"][:12], state))
            lines.append("   fix_command: %s" % e["fix_command"])
        else:
            viol += 1
            lines.append("DRIFT %s %s: установленный md5 %s != эталон %s" %
                         (e["kind"], target, m[:12], e["md5"][:12]))
            lines.append("   fix_command: %s" % e["fix_command"])
    return lines, viol


def assets_tier(install_files):
    """hd_pack: 1349 записей побитово; наборы шрифтов; CRC-замыкание .btf."""
    lines = []
    viol = 0
    hl = paths.archive_list_of(paths.hd_pack())
    ok_n = 0
    for e in hl:
        ap = install_files.get(e["name"].lower())
        if ap is None:
            viol += 1
            lines.append("MISSING hd_pack %s (crc=%s)" % (e["name"], e["crc"]))
            lines.append("   fix_command: python3 tools/extract_pristine.py extract '%s' --dest <install_dir>"
                         % e["name"])
            continue
        st = os.stat(ap)
        if st.st_size != e["size"]:
            viol += 1
            lines.append("DRIFT hd_pack %s: size %d != %d" % (e["name"], st.st_size, e["size"]))
            continue
        crc = "%08x" % (zlib.crc32(open(ap, "rb").read()) & 0xFFFFFFFF)
        if crc == e["crc"]:
            ok_n += 1
        else:
            viol += 1
            lines.append("DRIFT hd_pack %s: crc32 %s != %s" % (e["name"], crc, e["crc"]))
    lines.append("OK hd_pack: %d/%d записей побитово установлены" % (ok_n, len(hl)))

    fonts_inst = sorted(k.rsplit("/", 1)[-1] for k in install_files if k.startswith("data/fonts/"))
    fonts_pack = sorted(e["name"].lower().rsplit("/", 1)[-1] for e in hl if e["name"].lower().startswith("data/fonts/"))
    if fonts_inst == fonts_pack:
        lines.append("OK fonts: набор data/fonts (%d) == шрифтовой набор hd_pack (%d)" % (len(fonts_inst), len(fonts_pack)))
    else:
        viol += 1
        extra = [x for x in fonts_inst if x not in fonts_pack]
        miss = [x for x in fonts_pack if x not in fonts_inst]
        lines.append("DRIFT fonts: установлены лишние %s; отсутствует %s" % (extra, miss))

    pr_crc = set(e["crc"] for e in paths.archive_list_of(paths.pristine_archive()))
    pk_crc = set(e["crc"] for e in hl)
    btf = [k for k in install_files if k.endswith(".btf")]
    miss_crc = [k for k in btf if not _crc_in(install_files[k], pr_crc | pk_crc)]
    if not miss_crc:
        lines.append("OK btf-closure: все %d игровых .btf имеют CRC32 из {pristine} U {hd_pack}" % len(btf))
    else:
        viol += len(miss_crc)
        for k in miss_crc:
            lines.append("DRIFT btf %s: CRC32 не принадлежит ни одному архиву" % k)
    return lines, viol


def _crc_in(path, crcset):
    """CRC32 файла (потоком) ∈ crcset?"""
    crc = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            crc = zlib.crc32(chunk, crc)
    return "%08x" % (crc & 0xFFFFFFFF) in crcset


def untouched_tier(install_files):
    """Сверка всего остального с pristine-архивом + allowlist + exclude."""
    al = json.load(open(ALLOWLIST, encoding="utf-8"))
    man = json.load(open(MANIFEST, encoding="utf-8"))
    code_targets = set(e["target"].lower() for e in man["code_tier"]["entries"]
                       if "target" in e)
    allow = set(al["snapshot_only"]) | set(x["target"] for x in al["repo"]) \
            | set(x["target"] for x in al["repo_basename"]) | set(al.get("dev_artifacts", [])) \
            | set(x["target"] for x in al.get("named_legals", []))
    excludes = [glob_to_regex(g) for g in paths.check_exclude()]

    p_idx = archive_index(paths.pristine_archive())
    s_idx = snapshot_index()
    h_idx = archive_index(paths.hd_pack())
    pr_crcset = set(c for lst in p_idx.values() for (_, c) in lst)

    counts = {"PRISTINE": 0, "HD_PACK": 0, "ALLOWED": 0, "CODE": 0, "EXCLUDED": 0,
              "ARCHIVE_BASENAME": 0}
    lines = []
    viol = 0
    base_crc_cache = {}

    def pair_in(idx, key, rec):
        # пустая запись архива: 7-Zip печатает CRC пустого файла пустым,
        # crc32 пустого файла = 0 -> сравнение по размеру (docs/PITFALLS.md)
        for size, crc in idx.get(key, []):
            if size == rec[0] and (crc == rec[1] or size == 0):
                return True
        return False

    for rel in sorted(install_files):
        p = install_files[rel]
        st = os.stat(p)
        if rel not in base_crc_cache:  # кэш crc32 (файл читается один раз)
            crc = 0
            with open(p, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 16), b""):
                    crc = zlib.crc32(chunk, crc)
            base_crc_cache[rel] = (st.st_size, "%08x" % (crc & 0xFFFFFFFF))
        rec = base_crc_cache[rel]
        base = rel.rsplit("/", 1)[-1]
        if any(rx.match(rel) for rx in excludes):
            counts["EXCLUDED"] += 1
            continue
        if rel in code_targets:
            counts["CODE"] += 1
            continue
        if pair_in(h_idx, rel, rec):
            counts["HD_PACK"] += 1
            continue
        if rel in allow:
            counts["ALLOWED"] += 1
            continue
        if pair_in(p_idx, rel, rec):
            counts["PRISTINE"] += 1
            continue
        if pair_in(s_idx, rel, rec):
            counts["ALLOWED"] += 1
            continue
        if any(pair_in(idx, base, rec) for idx in (p_idx, h_idx, s_idx)):
            counts["ARCHIVE_BASENAME"] += 1
            continue
        # файл не побитово равен ни одной парной записи архивов
        if any(rel in idx for idx in (p_idx, h_idx, s_idx)) or \
           any(base in idx for idx in (p_idx, h_idx, s_idx)):
            viol += 1
            lines.append("DRIFT %s: размер/CRC32 (%d, %s) != запись архива с этим путём" %
                         (rel, rec[0], rec[1]))
            lines.append("   fix_command: python3 tools/extract_pristine.py extract <pristine-запись> --dest <install_dir>")
        else:
            viol += 1
            lines.append("EXTRA %s: файла нет ни в одном архиве, он не в allowlist (%d B, crc %s)" %
                         (rel, rec[0], rec[1]))
            lines.append("   fix_command: удалить файл из установки (артефакт вне состояния проекта)")

    # MISSING: pristine-записи, отсутствующие в установке
    absent_ok = set(x["pristine_entry"].lower() for x in al.get("absent_ok", []))
    missing = []
    for name, lst in p_idx.items():
        for size, crc in lst:
            if name not in install_files and name not in absent_ok and \
               not any(rx.match(name) for rx in excludes):
                missing.append(name)
                break
    for name in sorted(set(missing)):
        viol += 1
        lines.append("MISSING %s (pristine-запись отсутствует в установке)" % name)
        lines.append("   fix_command: python3 tools/extract_pristine.py extract %s --dest <install_dir>" % name)

    lines.append("OK untouched-классификация: %s" % counts)
    return lines, viol


def main():
    args = sys.argv[1:]
    modes = [a for a in ("--quick", "--full", "--untouched") if a in args]
    if not modes:
        print(__doc__)
        sys.exit(2)
    json_path = None
    if "--json" in args:
        i = args.index("--json")
        if i + 1 >= len(args):
            print("--json требует путь файла")
            sys.exit(2)
        json_path = paths.norm(args[i + 1])

    # отчёт — только в scratch/cache, проект не трогаем
    if json_path:
        allowed_roots = (paths.scratch_dir(), paths.cache_dir(), paths.cache_dir("check_state"))
        if not any(json_path.startswith(root) for root in allowed_roots):
            print("отчёт должен лежать в paths.scratch_dir() или paths.cache_dir(), не в проекте:\n  %s" % json_path)
            sys.exit(2)

    try:
        paths.validate_required()
    except Exception as exc:
        print("конфигурация невалидна: %s" % exc)
        sys.exit(2)

    install = paths.install_dir()
    print("install_dir = %s" % install)
    t0 = time.time()
    install_files = walk_install(install)
    lines = []
    viol = 0
    if "--quick" in modes:
        cl, v = code_tier(install_files, strict=True)
        lines += cl
        viol += v
    if "--full" in modes:
        # --full = code tier (мягко: известные отклонения из манифеста KNOWN) + assets
        cl, v = code_tier(install_files, strict=False)
        lines += cl
        viol += v
        cl, v = assets_tier(install_files)
        lines += cl
        viol += v
    if "--untouched" in modes:
        cl, v = untouched_tier(install_files)
        lines += cl
        viol += v

    for l in lines:
        print(l)
    print("файлов установки: %d; нарушений: %d; режимы: %s; время %.1f s" %
          (len(install_files), viol, "+".join(m.lstrip("-") for m in modes), time.time() - t0))

    if json_path:
        rep = {
            "modes": modes,
            "install_dir": install,
            "config": paths.norm(paths._config_path()),
            "install_files": len(install_files),
            "violations": viol,
            "lines": lines,
        }
        d = os.path.dirname(json_path)
        if d:
            os.makedirs(d, exist_ok=True)
        tmp = json_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(rep, fh, indent=1, ensure_ascii=False)
            fh.write("\n")
        os.replace(tmp, json_path)
        print("json-отчёт: %s" % json_path)

    sys.exit(1 if viol else 0)


if __name__ == "__main__":
    main()
