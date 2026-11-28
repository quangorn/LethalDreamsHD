"""Path/config foundation for the Lethal Dreams HD patch RE project.

Stdlib only (tomllib, json, subprocess, pathlib). Every function that returns
a path returns a forward-slash string; spaces are never escaped. Callers pass
paths as single argv elements to subprocess (list form), never as shell strings.
Paths built from `__file__` are Cygwin mount paths (`/cygdrive/C/...`), so they
go through `win_path()` before being handed to native Windows binaries.

Keys: game.install_dir, game.pristine_archive (unpatched install, no `Lib`),
game.lib_archive (the game's copy of the Python 2.2.3 `Lib`, the compile-path
source), assets.hd_pack (all upscales), assets.base (optional, personal working
dirs), py223.dir, scratch.dir, cache.dir, check.exclude. The game install is
read by the checker and written only by explicit install commands; the py223
compile path is cache/lib (`lib_dir()`), never the game.
"""

import json
import os
import re
import subprocess
from pathlib import Path
from tomllib import load as _toml_load

ENV_VAR = "LD_CONFIG"

# Required keys: dotted key -> [section, key]
REQUIRED_KEYS = {
    "game.install_dir": ["game", "install_dir"],
    "game.pristine_archive": ["game", "pristine_archive"],
    "game.lib_archive": ["game", "lib_archive"],
    "assets.hd_pack": ["assets", "hd_pack"],
    "py223.dir": ["py223", "dir"],
    "scratch.dir": ["scratch", "dir"],
    "cache.dir": ["cache", "dir"],
}

# Optional keys. check.exclude is the exclusion list for checks; assets.base is
# the root of the personal working dirs (Upscayl sources and destinations).
# There is no built-in default for assets.base: it is a personal path, so the
# user must set it. assets_dir() raises a clear error naming the key when the
# key is absent; validate_required() only validates assets.base when it is set.
OPTIONAL_KEYS = {
    "check.exclude": ["check", "exclude"],
    "assets.base": ["assets", "base"],
}

DEFAULT_ASSETS_BASE = None  # no personal default: set assets.base in config.toml

# Marker written by `tools/extract_pristine.py lib` inside cache/lib.
LIB_MARKER_NAME = "libdir.txt"

CACHE = {}  # config-path string -> parsed dict


def norm(p):
    """Forward slashes, no escaping (Windows drive letters keep colon).

    UNC paths (`//server/share`) keep their leading double slash; a drive root
    keeps its trailing slash (`C:/`). Everything else is stripped of trailing
    slashes and extra slashes are collapsed.
    """
    s = str(p)
    s = s.replace("\\", "/")
    unc = s.startswith("//")
    while "//" in s:
        s = s.replace("//", "/")
    if unc:
        s = "/" + s  # restore the double leading slash of //server/share
    if s == "//":
        return s
    if s.endswith(":"):
        return s + "/"  # drive root "C:" -> "C:/"
    if len(s) == 3 and s[1] == ":" and s[2] == "/":
        return s  # drive root "C:/" keeps its trailing slash
    return s.rstrip("/")


# Backward alias for callers of the private name.
_norm = norm


def _config_path():
    env = os.environ.get(ENV_VAR)
    if env:
        return Path(env)
    return Path(__file__).resolve().parent / "config.toml"


def _get(data, dotted):
    node = data
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


def load():
    """Load config (cached per config path). Raises FileNotFoundError with a
    clear message if the file is absent, and KeyError listing the missing
    required keys (and the full required set) if the file is incomplete."""
    cfg_path = _config_path()
    key = norm(cfg_path)
    if key in CACHE:
        return CACHE[key]
    if not cfg_path.is_file():
        raise FileNotFoundError(
            "config file not found: %s\n"
            "create it (see config.example.toml) or set %s to its path."
            % (key, ENV_VAR)
        )
    with open(cfg_path, "rb") as fh:
        data = _toml_load(fh)
    missing = [d for d, parts in REQUIRED_KEYS.items() if _get(data, ".".join(parts)) is None]
    if missing:
        raise KeyError(
            "config %s is missing required keys: %s\n"
            "required keys: %s"
            % (key, ", ".join(sorted(missing)), ", ".join(sorted(REQUIRED_KEYS)))
        )
    CACHE[key] = data
    return data


def win_path(p):
    """Path in Windows form, for native Windows binaries (py223 PYTHON.EXE, 7z).
    A Cygwin mount path (`/cygdrive/C/x`) becomes `C:/x`; a forward-slash drive
    path (`C:/x`) is kept as it is. Anything else is normalized and returned."""
    s = norm(p)
    m = re.match(r"^/cygdrive/([A-Za-z])/(.*)$", s)
    if m:
        return m.group(1).upper() + ":/" + m.group(2)
    return s


def cygwin_path(p):
    """Path in Cygwin mount form (`/cygdrive/c/x`) for p7zip; a non-drive path is
    returned normalized. Windows 7-Zip does not read this form."""
    s = norm(p)
    m = re.match(r"^([A-Za-z]):/(.*)$", s)
    if m:
        return "/cygdrive/" + m.group(1).lower() + "/" + m.group(2)
    return s


def run7z(prefix, archive, suffix=()):
    """Run `7z` with the archive path in both forms: Windows 7-Zip reads `C:/x`,
    cygwin p7zip reads `/cygdrive/c/x` and fails on `C:/x`. Returns the first
    successful process; if both forms fail, the last process. `prefix` holds the
    switches, `suffix` the entry names."""
    proc = None
    for form in (norm(archive), cygwin_path(archive)):
        proc = subprocess.run(["7z"] + list(prefix) + [form] + list(suffix),
                              capture_output=True)
        if proc.returncode == 0:
            return proc
    return proc


def _writable(d):
    """Create d if missing, then prove it is writable. Returns a problem string
    or None. The probe file is our own file, created and removed here.

    A drive-relative path (`Z:/nope`) is created as a stray `Z:` directory in
    the current working directory when the drive is not mounted (Cygwin's
    `os.path.splitdrive` does not see Windows drive letters), so an unavailable
    drive is reported instead of attempted.
    """
    m = re.match(r"[A-Za-z]:", str(d))
    if m and not os.path.isdir(m.group(0) + "/"):
        return "drive %s is not available: %s" % (m.group(0), norm(d))
    try:
        d.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        return "cannot be created: %s (%s)" % (norm(d), exc)
    probe = d / ".write_test"
    try:
        with open(probe, "w") as fh:
            fh.write("ok")
        os.remove(probe)
    except OSError as exc:
        return "is not writable: %s (%s)" % (norm(d), exc)
    return None


def validate_required(require_lib=True):
    """Fail fast: load config, check required values are non-empty and that
    install_dir / pristine_archive / lib_archive / hd_pack exist on disk,
    py223.dir and py223_exe() exist, assets.base exists when set,
    scratch.dir / cache.dir are creatable and writable, and cache/lib is
    populated and fresh for the current lib archive (`require_lib=False`
    skips that check, used by the command that populates it). Every problem
    found is reported together in one message. Returns the config dict.
    """
    data = load()
    empty = [d for d in REQUIRED_KEYS if not _get(data, ".".join(REQUIRED_KEYS[d]))]
    if empty:
        raise KeyError("config has empty required values: %s" % ", ".join(sorted(empty)))

    problems = []
    inst = Path(install_dir())
    arch = Path(pristine_archive())
    libarch = Path(lib_archive())
    pack = Path(hd_pack())
    if not inst.exists():
        problems.append("install_dir does not exist: %s" % norm(inst))
    if not arch.is_file():
        problems.append("pristine_archive does not exist: %s" % norm(arch))
    if not libarch.is_file():
        problems.append("game.lib_archive does not exist: %s" % norm(libarch))
    if not pack.is_file():
        problems.append("assets.hd_pack does not exist: %s" % norm(pack))

    base = _get(data, "assets.base")
    if base and not Path(base).is_dir():
        problems.append("assets.base does not exist: %s" % norm(base))

    pydir = Path(py223_dir())
    if not pydir.is_dir():
        problems.append("py223.dir does not exist: %s" % norm(pydir))
    exe = Path(py223_exe())
    if not exe.is_file():
        problems.append("py223 PYTHON.EXE does not exist: %s" % norm(exe))

    for label, dotted in (("scratch.dir", "scratch.dir"), ("cache.dir", "cache.dir")):
        problem = _writable(Path(_get(data, dotted)))
        if problem:
            problems.append("%s %s" % (label, problem))

    # cache/lib: the compile path. Populated and verified by
    # tools/extract_pristine.py lib; staleness is a problem, not a silent rebuild.
    populate = "python3 tools/extract_pristine.py lib"
    if require_lib:
        marker = lib_marker_path()
        if not marker.is_file():
            problems.append("cache lib is not populated: %s (run: %s)"
                            % (norm(cache_lib_dir()), populate))
        else:
            got = read_lib_marker(marker)
            if got is None:
                problems.append("cache lib marker is corrupt: %s (run: %s --refresh)"
                                % (norm(marker), populate))
            elif got != archive_stamp(lib_archive()):
                problems.append("cache lib is stale for %s (run: %s --refresh)"
                                % (norm(libarch), populate))

    if problems:
        raise FileNotFoundError("\n".join(problems))
    return data


def install_dir():
    return norm(load()["game"]["install_dir"])


def pristine_archive():
    return norm(load()["game"]["pristine_archive"])


def py223_dir():
    return norm(load()["py223"]["dir"])


def py223_exe():
    return py223_dir() + "/PYTHON.EXE"


def lib_archive():
    """Archive of the game's copy of the Python 2.2.3 `Lib` tree (`.py` and
    `.pyc`, shipped with the game). It is the source that populates cache/lib,
    the py223 compile path; the pristine archive contains no `Lib` at all."""
    return norm(load()["game"]["lib_archive"])


def snapshot_index_path():
    """Committed pair index of the install snapshot (path + size + crc32 for
    every entry), the reference the checker classifies snapshot-covered files
    against. A project file, not a personal path."""
    return norm(Path(__file__).resolve().parent / "release" / "snapshot_index.json")


def hd_pack():
    """Archive of every upscaled asset (textures, cursors, font sidecars). The
    project stores no upscales: this archive is the only place they live."""
    return norm(load()["assets"]["hd_pack"])


def assets_dir(subdir=None):
    """Personal working dir (Upscayl sources/destinations) under assets.base.

    assets.base is a personal path with no built-in default; when the key is
    absent this raises FileNotFoundError naming the key, so the caller sets it
    in config.toml instead of silently writing into some hardcoded personal dir.
    """
    base = _get(load(), "assets.base")
    if not base:
        raise FileNotFoundError(
            "assets.base is not set in config.toml; set it to your personal "
            "working dirs root (Upscayl sources/destinations) before calling "
            "assets_dir()"
        )
    p = Path(base) / subdir if subdir else Path(base)
    p.mkdir(parents=True, exist_ok=True)
    return norm(p)


def cache_lib_dir(subdir=None):
    base = Path(cache_dir()) / "lib"
    p = base / subdir if subdir else base
    p.mkdir(parents=True, exist_ok=True)
    return norm(p)


def btf_backup_dir(subdir=None):
    """Font texture templates (`*_orig.btf`/`*.fdf`) extracted from the archives.
    Rebuildable from the archives, so not kept in the project."""
    base = Path(cache_dir()) / "btf_backup"
    p = base / subdir if subdir else base
    p.mkdir(parents=True, exist_ok=True)
    return norm(p)


def archive_stamp(archive):
    """(normalized archive path, size, mtime) — the cache stamp."""
    st = os.stat(archive)
    return (norm(archive), int(st.st_size), int(st.st_mtime))


def lib_marker_path():
    return Path(cache_lib_dir()) / LIB_MARKER_NAME


def read_lib_marker(marker):
    """Parse the marker into (archive, size, mtime); None if it is not readable
    or malformed."""
    try:
        with open(marker, "r", encoding="utf-8") as fh:
            lines = [l.strip() for l in fh.read().replace("\r\n", "\n").split("\n")]
        fields = dict(l.split("=", 1) for l in lines if "=" in l)
        archive = norm(fields["LD_LIB_ARCHIVE"])
        size, mtime = fields["LD_LIB_STAMP"].split("|")
        return (archive, int(size), int(mtime))
    except (OSError, ValueError, KeyError):
        return None


def lib_dir():
    """Cache copy of the game `Lib`, used as the py223 compile path so imports
    never write .pyc into the game install. Populated by
    `python3 tools/extract_pristine.py lib` from game.lib_archive. An
    unpopulated cache, a corrupt marker, or a cache built from a different
    archive stamp all raise FileNotFoundError naming the command to run."""
    d = cache_lib_dir()
    marker = Path(d) / LIB_MARKER_NAME
    populate = "python3 tools/extract_pristine.py lib"
    if not marker.is_file():
        raise FileNotFoundError(
            "cache lib is not populated: %s\nrun: %s" % (d, populate))
    got = read_lib_marker(marker)
    want = archive_stamp(lib_archive())
    if got is None:
        raise FileNotFoundError(
            "cache lib marker %s is corrupt\nrun: %s" % (norm(marker), populate))
    if got != want:
        raise FileNotFoundError(
            "cache lib was built from %s stamp %d|%d, archive %s is now stamp %d|%d\n"
            "re-populate: %s --refresh"
            % (got[0], got[1], got[2], want[0], want[1], want[2], populate))
    return d


def scratch_dir(session_subdir=None):
    base = Path(load()["scratch"]["dir"])
    p = base / session_subdir if session_subdir else base
    p.mkdir(parents=True, exist_ok=True)
    return norm(p)


def cache_dir(subdir=None):
    base = Path(load()["cache"]["dir"])
    p = base / subdir if subdir else base
    p.mkdir(parents=True, exist_ok=True)
    return norm(p)


def check_exclude():
    """List of glob patterns to skip in checks."""
    raw = _get(load(), "check.exclude")
    if raw is None:
        return []
    if isinstance(raw, str):
        raw = [raw]
    return [norm(g) for g in raw]


def game_path(*parts):
    """install_dir joined with parts, forward slashes. A part that carries a
    drive letter (`:`), starts with `/`, or is `..` would escape the install
    dir, so it is rejected with ValueError."""
    p = Path(install_dir())
    for x in parts:
        part = str(x)
        if ":" in part:
            raise ValueError(
                "game_path part %r carries a drive colon; parts must be relative "
                "to install_dir %s" % (part, install_dir())
            )
        if part.startswith("/"):
            raise ValueError(
                "game_path part %r starts with / (absolute); parts must be relative "
                "to install_dir %s" % (part, install_dir())
            )
        if part == "..":
            raise ValueError(
                "game_path part '..' escapes install_dir %s; parts must stay inside it"
                % install_dir()
            )
        p = p / part
    return norm(p)


def _decode_console(data):
    """Decode 7-Zip console output. 7-Zip writes it in the OEM codepage (cp866
    on a Russian Windows), which Python spells "oem"; cp1251 is the fallback,
    so entry names with non-ASCII characters do not turn into `?`."""
    for enc in ("oem", "cp866", "cp1251"):
        try:
            return data.decode(enc)
        except (LookupError, UnicodeDecodeError):
            continue
    return data.decode("utf-8", errors="replace")


def archive_list(refresh=False):
    """List of dicts {name, size, crc, mtime} for FILE entries of the pristine
    archive. See archive_list_of()."""
    return archive_list_of(pristine_archive(), refresh)


def archive_list_of(archive, refresh=False):
    """List of dicts {name, size, crc, mtime} for the FILE entries of any
    7-Zip archive. Names are normalized to forward slashes. Cached in
    cache_dir()/archive_index_<archive>.json keyed by archive name and
    mtime+size, so a cache dir shared by two archives cannot serve one
    archive's index for the other, and repeated calls are fast.

    Note: 7-Zip 24.08 has no `-lcrc` switch ("Command Line Error: Unknown
    switch"), so we use `7z l -slt`, which prints one "Key = Value" block per
    entry including Path, Size, Modified and CRC.
    """
    archive = norm(archive)
    st = os.stat(archive)
    stamp = {"mtime": st.st_mtime, "size": st.st_size}
    slug = re.sub(r"[^a-z0-9]", "_", norm(Path(archive).name).lower())
    index_path = Path(cache_dir()) / ("archive_index_%s.json" % slug)
    if not refresh and index_path.is_file():
        try:
            with open(index_path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
            if doc.get("stamp") == stamp and doc.get("archive") == archive:
                return doc["entries"]
        except (OSError, ValueError, KeyError):
            pass  # stale/corrupt cache -> rebuild

    proc = run7z(["l", "-slt"], archive)
    if proc.returncode != 0:
        raise RuntimeError(
            "7z listing failed for %s\n%s" % (archive, _decode_console(proc.stdout) + _decode_console(proc.stderr))
        )

    lines = _decode_console(proc.stdout).replace("\r\n", "\n").replace("\r", "\n").split("\n")
    start = None
    for i, line in enumerate(lines):
        if line.strip() == "----------":  # end of the archive header block
            start = i + 1
            break
    if start is None:
        raise RuntimeError("no entry table found in 7z listing of %s" % archive)

    entries = []
    block = {}
    for line in lines[start:] + [""]:
        if not line.strip():
            path = block.get("Path")
            if path and "D" not in block.get("Attributes", ""):
                entries.append({
                    "name": norm(path),
                    "size": int(block.get("Size", "0") or 0),
                    "crc": block.get("CRC", "").strip().lower(),
                    "mtime": block.get("Modified", "").strip(),
                })
            block = {}
            continue
        if " = " in line:
            k, v = line.split(" = ", 1)
            block[k] = v

    cache_dir()  # ensure dir
    tmp_path = Path(str(index_path) + ".tmp")
    with open(tmp_path, "w", encoding="utf-8") as fh:
        json.dump({"archive": archive, "stamp": stamp, "entries": entries}, fh)
    os.replace(tmp_path, index_path)
    return entries
