"""CLI for the Lethal Dreams archives (pristine install, full snapshot, hd_pack).

Commands:
  list [PATTERN]            pristine archive entries matching a glob pattern
  extract PATTERN [--dest]  extract matching entries into cache/pristine/
  snapshot                  build cache/game_manifest.json (pyc/dll/exe/fdf/btf)
  lib [--refresh]           populate cache/lib from game.lib_archive
  templates                 font templates (*_orig.btf/.fdf) from pristine_archive

Pattern notes: the pattern is matched against archive entry names with fnmatch
(`*` also matches `/`), and the matched entries' exact archive names are then
passed to 7-Zip as include patterns. 7-Zip patterns are anchored at the start
of the path and `*` does not cross `/`, so a raw pattern handed to 7-Zip is
root-anchored: `*` matches only root-level entries and `**` is the full-tree
pattern. Driving 7-Zip by exact names keeps extraction, the disk verification
and the index match in agreement for any depth.

Stdlib only; config and archive access go through paths.py.
"""

import argparse
import collections
import fnmatch
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import paths  # noqa: E402

# Windows CreateProcess command-line limit is 32768 wide chars; each extra name
# adds its length plus quoting and a space. Reserve room for the switches.
MAX_ARGV_CHARS = 30000


def md5_file(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def crc32_md5_file(path):
    """(crc32 as 8 lowercase hex digits, md5) of one file, read once."""
    h = hashlib.md5()
    crc = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
            crc = zlib.crc32(chunk, crc)
    return "%08x" % (crc & 0xFFFFFFFF), h.hexdigest()


def cmd_list(pattern, entries):
    sel = [e for e in entries if not pattern or fnmatch.fnmatch(e["name"].lower(), pattern.lower())]
    for e in sel:
        print("%s  %10d  %s" % (e["name"], e["size"], e["crc"] or "--------"))
    print("matched %d entries" % len(sel))


def cmd_extract(pattern, dest):
    archive = paths.pristine_archive()
    out_dir = Path(paths.cache_dir()) / "pristine"
    if dest:
        out_dir = Path(paths.norm(dest))
    out_dir.mkdir(parents=True, exist_ok=True)

    matched = [e for e in paths.archive_list()
               if fnmatch.fnmatch(e["name"].lower(), pattern.lower())]
    for e in matched:
        print("archive match: %s  size=%d  crc=%s" % (e["name"], e["size"], e["crc"]))
    if not matched:
        print("no archive entries match %s" % pattern)
        return 1

    # archive path contains spaces -> single argv element, never a shell string
    proc = subprocess_run_extract(archive, [e["name"] for e in matched], out_dir)
    if proc.returncode != 0:
        raise RuntimeError(
            "7z extract failed for %s\n%s" % (archive, proc.stdout + proc.stderr)
        )

    # 7z prints per-file progress lines for every file in the solid block it
    # decodes, so verify every matched entry on disk against the archive CRC.
    bad = 0
    for e in matched:
        f = out_dir / e["name"]
        if not f.is_file():
            print("MISSING  %s  expected crc=%s" % (e["name"], e["crc"]))
            bad += 1
            continue
        disk_crc, md5 = crc32_md5_file(f)
        if e["crc"] and disk_crc != e["crc"]:
            print("CRC MISMATCH  %s  archive=%s  disk=%s" % (e["name"], e["crc"], disk_crc))
            bad += 1
        elif e["size"] != f.stat().st_size:
            print("CRC MISMATCH  %s  size archive=%d disk=%d (crc=%s)" % (
                e["name"], e["size"], f.stat().st_size, disk_crc))
            bad += 1
        else:
            print("OK  %s  crc=%s  md5=%s" % (e["name"], disk_crc, md5))

    print("extracted %d files into %s" % (len(matched) - bad, paths.norm(out_dir)))
    if bad:
        print("FAILED: %d of %d entries are missing or do not match the archive" % (bad, len(matched)))
        return 1
    return 0


def subprocess_run_extract(archive, names, out_dir):
    """Run 7z x once, driven by the exact archive names of the matched entries.

    Names are batched into a single argv list. When the batched command line
    would exceed the Windows CreateProcess limit, the names go into a
    UTF-8 @listfile instead (7-Zip reads patterns in the list file with
    -scsUTF-8). Relative structure is kept by `x`, output dir via `-o`.
    """
    prefix = ["x", "-y", "-scsUTF-8", "-o" + str(out_dir)]
    if sum(len(n) + 3 for n in names) + len(archive) + len(str(out_dir)) + 40 > MAX_ARGV_CHARS:
        listfile = Path(paths.scratch_dir()) / ("extract_pristine_names_%d.txt" % os.getpid())
        tmp = Path(str(listfile) + ".tmp")
        with open(tmp, "w", encoding="utf-8") as fh:
            fh.write("\n".join(names) + "\n")
        os.replace(tmp, listfile)
        try:
            return paths.run7z(prefix, archive, ["@" + paths.norm(listfile)])
        finally:
            os.remove(listfile)
    return paths.run7z(prefix, archive, names)


def lib_entries(archive):
    """(entries, roots) for every file under a `Lib` directory of an install
    archive. Each entry is (archive name, path relative to Lib, index entry);
    roots is the set of archive root directories that contain a Lib, so the
    cache layout can be checked for ambiguity before extracting. A `Lib` at the
    archive root (`Lib/aifc.py`) counts as its own root."""
    out = []
    roots = set()
    for e in paths.archive_list_of(archive):
        parts = e["name"].split("/")
        for i in range(0, len(parts) - 1):
            if parts[i].lower() == "lib":
                roots.add(parts[0])
                out.append((e["name"], "/".join(parts[i + 1:]), e))
                break
    return out, roots


def verify_lib(entries, lib):
    """Every cache/lib file present and matching the archive CRC/size. Returns
    the list of problem strings (empty means the cache is good)."""
    bad = []
    for name, rel, e in entries:
        f = lib / rel
        if not f.is_file():
            bad.append("MISSING %s  expected crc=%s" % (name, e["crc"]))
            continue
        disk_crc, md5 = crc32_md5_file(f)
        if e["crc"] and disk_crc != e["crc"]:
            bad.append("CRC MISMATCH %s  archive=%s  disk=%s" % (name, e["crc"], disk_crc))
        elif e["size"] != f.stat().st_size:
            bad.append("SIZE MISMATCH %s  archive=%d  disk=%d" % (
                name, e["size"], f.stat().st_size))
    return bad


def cmd_lib(refresh=False):
    """Populate cache/lib from game.lib_archive, verify every file against
    the archive CRC, then write the marker that `paths.lib_dir()` checks. An
    existing good cache is left untouched and reported; a cache that does not
    match the archive is reported and needs `--refresh`."""
    archive = paths.lib_archive()
    lib = Path(paths.cache_lib_dir())
    marker = paths.lib_marker_path()
    entries, roots = lib_entries(archive)
    if not entries:
        print("no Lib files found in %s" % archive)
        return 1
    if len(roots) != 1:
        print("Lib files sit under several archive roots %s; cache/lib assumes one"
              % sorted(roots))
        return 1

    stamp = paths.archive_stamp(archive)
    if marker.is_file() and not refresh:
        got = paths.read_lib_marker(marker)
        if got == stamp:
            bad = verify_lib(entries, lib)
            if not bad:
                print("cache lib already populated: %d files, every CRC matches the archive"
                      % len(entries))
                print("cache lib: %s" % paths.norm(lib))
                return 0
            print("cache lib on disk does not match the archive: %d of %d entries bad"
                  % (len(bad), len(entries)))
            for b in bad[:10]:
                print("   ", b)
            print("re-populate: python3 tools/extract_pristine.py lib --refresh")
            return 1

    # solid archive: extracting decodes the whole block, so extract to a scratch
    # dir, verify CRCs there, and move only verified files into the cache.
    tmp = Path(paths.scratch_dir()) / ("extract_pristine_lib_%d" % os.getpid())
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    proc = subprocess_run_extract(archive, [e[2]["name"] for e in entries], tmp)
    if proc.returncode != 0:
        shutil.rmtree(tmp)
        raise RuntimeError("7z extract failed for %s\n%s" % (archive, proc.stdout + proc.stderr))

    bad = []
    counts = collections.Counter()
    for name, rel, e in entries:
        src = tmp / name
        if not src.is_file():
            bad.append("MISSING %s  expected crc=%s" % (name, e["crc"]))
            continue
        disk_crc, md5 = crc32_md5_file(src)
        if e["crc"] and disk_crc != e["crc"]:
            bad.append("CRC MISMATCH %s  archive=%s  disk=%s" % (name, e["crc"], disk_crc))
            continue
        dst = lib / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            os.chmod(dst, 0o666)  # a read-only cache file must be replaceable on --refresh
        shutil.move(str(src), str(dst))
        counts[rel.rsplit(".", 1)[-1].lower()] += 1
    shutil.rmtree(tmp)

    if bad:
        print("cache lib NOT populated: %d of %d entries failed (missing/CRC)" % (len(bad), len(entries)))
        for b in bad[:10]:
            print("   ", b)
        print("the cache was not marked; paths.lib_dir() refuses to use it")
        return 1

    # Sources and .pyc are set read-only: the py223 worker cannot write new .pyc
    # next to them, so the cache stays byte-identical to the archive and the
    # CRC verification in cmd_lib stays valid after any number of worker runs.
    ro = 0
    for r, d, fs in os.walk(lib):
        for f in fs:
            p = os.path.join(r, f)
            if f.lower().endswith((".py", ".pyc")):
                os.chmod(p, 0o444)
                ro += 1

    text = ("LD_LIBDIR=cache/lib\n"
            "LD_LIB_ARCHIVE=%s\n"
            "LD_LIB_STAMP=%d|%d\n"
            "LD_LIB_COUNT=%d\n" % (archive, stamp[1], stamp[2], len(entries)))
    tmp_marker = Path(str(marker) + ".tmp")
    with open(tmp_marker, "w", encoding="utf-8") as fh:
        fh.write(text)
    os.replace(tmp_marker, marker)

    print("cache lib populated: %d files from %s" % (len(entries), archive))
    for ext in sorted(counts):
        print("  %s: %d" % (ext, counts[ext]))
    print("cache lib: %s" % paths.norm(lib))
    print("marker: %s" % paths.norm(marker))
    return 0


FONT_TEMPLATE_NAMES = ("ld_uncial20", "ld_uncial12", "ld_tahoma11", "ld_numbers15")


def cmd_templates():
    """Extract the four font sheet/table templates from the pristine archive into
    cache/btf_backup as `<name>_orig.btf` / `<name>_orig.fdf`, verified against the
    archive CRC. The project stores no copies: this cache is rebuildable, and it is
    what gen_font4x.py / restore_fonts.py use as the native template base."""
    archive = paths.pristine_archive()
    entries = paths.archive_list()
    out = Path(paths.btf_backup_dir())
    targets = []
    for nm in FONT_TEMPLATE_NAMES:
        targets.append(("data/textures/%s.btf" % nm, "%s_orig.btf" % nm))
        targets.append(("data/fonts/%s.fdf" % nm, "%s_orig.fdf" % nm))
    by_name = {e["name"].lower(): e for e in entries}
    missing = [n for n, o in targets if n.lower() not in by_name]
    if missing:
        for n in missing:
            print("MISSING in archive: %s" % n)
        return 1
    sel = [by_name[n.lower()] for n, o in targets]

    # solid archive: extract to scratch, verify CRCs there, copy verified files in
    tmp = Path(paths.scratch_dir()) / ("extract_pristine_templates_%d" % os.getpid())
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    proc = subprocess_run_extract(archive, [e["name"] for e in sel], tmp)
    if proc.returncode != 0:
        shutil.rmtree(tmp)
        raise RuntimeError("7z extract failed for %s\n%s" % (archive, proc.stdout + proc.stderr))

    bad = 0
    for (name, outname), e in zip(targets, sel):
        src = tmp / e["name"]
        if not src.is_file():
            print("MISSING after extract: %s" % name)
            bad += 1
            continue
        disk_crc, md5 = crc32_md5_file(src)
        if e["crc"] and disk_crc != e["crc"]:
            print("CRC MISMATCH %s  archive=%s  disk=%s" % (name, e["crc"], disk_crc))
            bad += 1
            continue
        dst = out / outname
        shutil.copyfile(src, dst)
        print("OK %s -> %s  crc=%s  md5=%s" % (name, paths.norm(dst), disk_crc, md5))
    shutil.rmtree(tmp)
    if bad:
        print("FAILED: %d of %d template files" % (bad, len(targets)))
        return 1
    print("templates: %d files in %s" % (len(targets), paths.norm(out)))
    return 0


def cmd_snapshot(entries):
    exts = (".pyc", ".dll", ".exe", ".fdf", ".btf")
    manifest = []
    counts = {}
    for e in entries:
        low = e["name"].lower()
        if low.endswith(exts):
            ext = low.rsplit(".", 1)[1]
            counts[ext] = counts.get(ext, 0) + 1
            manifest.append({
                "name": low,
                "size": e["size"],
                "crc": e["crc"],
                "mtime": e["mtime"],
            })
    out = Path(paths.cache_dir()) / "game_manifest.json"
    tmp = Path(str(out) + ".tmp")  # write aside, then move into place atomically
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1)
    os.replace(tmp, out)
    print("manifest: %s" % paths.norm(out))
    print("total entries: %d" % len(manifest))
    for ext in sorted(counts):
        print("  %s: %d" % (ext, counts[ext]))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="extract_pristine.py")
    ap.add_argument("command", choices=["list", "extract", "snapshot", "lib", "templates"])
    ap.add_argument("pattern", nargs="?", default=None)
    ap.add_argument("--dest", default=None, help="output dir, extract only")
    ap.add_argument("--refresh", action="store_true",
                    help="rebuild cache/lib from the snapshot archive (lib only)")
    args = ap.parse_args(argv)

    # fail fast on config problems; `lib` is the command that populates
    # cache/lib, so it does not require the cache to exist yet
    paths.validate_required(require_lib=args.command != "lib")

    if args.dest is not None and args.command != "extract":
        ap.error("--dest is only valid for extract, not for %s" % args.command)
    if args.refresh and args.command != "lib":
        ap.error("--refresh is only valid for lib, not for %s" % args.command)
    if args.command == "snapshot" and args.pattern:
        ap.error("snapshot takes no pattern (it indexes the whole archive)")
    if args.command == "lib" and args.pattern:
        ap.error("lib takes no pattern (it extracts Lib from game.lib_archive)")
    if args.command == "templates" and args.pattern:
        ap.error("templates takes no pattern (the four font templates are fixed)")
    if args.command == "extract" and not args.pattern:
        ap.error("extract needs a pattern")

    if args.command == "lib":
        return cmd_lib(refresh=args.refresh)
    if args.command == "templates":
        return cmd_templates()
    if args.command == "snapshot":
        cmd_snapshot(paths.archive_list())
        return 0
    if args.command == "extract":
        return cmd_extract(args.pattern, args.dest)
    cmd_list(args.pattern, paths.archive_list())
    return 0


if __name__ == "__main__":
    sys.exit(main())
