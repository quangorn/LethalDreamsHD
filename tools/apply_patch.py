#!/usr/bin/env python3
"""apply_patch.py — применение unified diff из release/patches/ к базе-декомпиляту.

Расположение (решение по стилю проекта, задокументировано в docs/PATCHES.md):
файл лежит в tools/ рядом с check_state.py и extract_pristine.py, потому что
это точка входа пользователя над репозиторием (её печатает fix_command манифеста),
а вся py223-машинерия остаётся в tools/pyc/.

Каноническая форма (из корня репозитория):
    python3 tools/apply_patch.py <module>            gamescript, ui/ui, ui/cursor, ...
    python3 tools/apply_patch.py <module> --install  установить .pyc в ИГРУ
                                                   (--install: явный флаг, пишет
                                                   только install_patch.py)

Шаги (рецепт воспроизведения release-.pyc из spec_ef.md):
  1. база decompiled_fixed/<module>.py; если её нет — печатает каноническую
     команду `python3 tools/pyc/decompile_lit.py <module>` и выходит 1;
  2. pure-python апикатор (бинарника patch(1) на машине нет) применяет
     release/patches/<file>.patch; при несовпадении контекста/удаления
     применение останавливается ДО любой записи, печатается номер базовой
     строки и ожидаемый/фактический текст;
  3. результат пишется в канонический путь репозитория patches/<module>.py
     (LF) — co_filename байткода = путь репозитория;
  4. mtime .py выставляется ровно на манифестное pyc_source_mtime;
  5. компиляция воркером 2.2.3: PYTHONPATH = py223.dir + cache/lib (игровой Lib
     не входит), cwd = корень репозитория; заголовок .pyc (magic 2ded0d0a +
     mtime исходника) приводится к манифестному mtime — тело [8:] побитово
     равен компиляту канонического исходника, и md5 .pyc == эталон манифеста;
  6. идемпотентно: повторный прогон даёт побитово те же .py/.pyc.

Запись в игровую установку — только под явным --install, делегирует
tools/pyc/install_patch.py. Чекер (check_state.py) ничего не чинит; этот скрипт
единственный, кто легально создаёт patches/<module>.py из публикованных данных.
"""
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import zlib

# tools -> корень репозитория (бутстрап проекта)
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

MANIFEST = os.path.join(_ROOT, "release", "manifest.json")
TOOL_DIR = paths.win_path(_HERE)
ROOT_WIN = paths.win_path(_ROOT)


def md5_file(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def apply_patch(base_text, patch_text, report):
    """Pure-python unified-diff applier (starting point: proven feasy.py).

    Возвращает (text, None) при успехе. При несовпадении контекста/удаления —
    (None, сообщение) с номером базовой строки и ожидаемым/фактическим текстом;
    вызывающий обязан остановиться ДО записи. Сообщения о ходе — в list report.
    """
    bl = base_text.split("\n")
    out = []
    lines = patch_text.split("\n")
    pos = 0        # абсолютный индекс bl следующего непрочитанного базового файла
    idx = 0
    hunks = 0
    adds = rems = 0
    while idx < len(lines):
        if lines[idx].startswith("@@"):
            m = re.match(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", lines[idx])
            a, n = int(m.group(1)), int(m.group(2) or 1)
            start = a - 1
            if start < pos:
                return None, "hunk overlaps consumed region (line %d)" % (pos + 1)
            out += bl[pos:start]
            ci = start
            hp = idx + 1
            while ci < start + n and hp < len(lines):
                l = lines[hp]
                if l.startswith(" "):
                    if bl[ci] != l[1:]:
                        return None, ("context mismatch at base line %d\n"
                                      "  expected: %r\n"
                                      "  actual  : %r" % (ci + 1, l[1:], bl[ci]))
                    out.append(l[1:]); ci += 1
                elif l.startswith("-"):
                    if bl[ci] != l[1:]:
                        return None, ("delete mismatch at base line %d\n"
                                      "  expected: %r\n"
                                      "  actual  : %r" % (ci + 1, l[1:], bl[ci]))
                    ci += 1
                elif l.startswith("+"):
                    out.append(l[1:]); adds += 1
                else:
                    break
                hp += 1
            pos = ci
            idx = hp
            hunks += 1
        else:
            idx += 1
    out += bl[pos:]
    report.append("hunks: %d, added: %d" % (hunks, adds))
    return "\n".join(out), None


def manifest_entry(module):
    man = json.load(open(MANIFEST, encoding="utf-8"))
    for e in man["code_tier"]["entries"]:
        if e.get("module") == module:
            return e
    return None


def compile_worker(py_win, pyc_win):
    """Компиляция воркером 2.2.3 (cwd = корень репозитория, PYTHONPATH =
    py223 + cache/lib; игровой Lib не входит в путь)."""
    env = dict(os.environ, PYTHONPATH=paths.py223_dir() + ";" + paths.lib_dir())
    worker = os.path.join(paths.win_path(TOOL_DIR), "pyc", "py22_worker.py")
    proc = subprocess.run([paths.py223_exe(), worker, "build", py_win, pyc_win],
                          env=env, cwd=ROOT_WIN, capture_output=True)
    out = proc.stdout.decode("utf-8", "replace")
    return out.strip(), proc.returncode


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    install = "--install" in args
    module = args[0]

    paths.validate_required()
    e = manifest_entry(module)
    if e is None:
        print("no manifest entry for module %s" % module)
        sys.exit(2)

    if "base" not in e:
        # Авторский модуль (hd_scale/hd_tex): патча нет, дерево-базы нет —
        # каноническая команда другая (fix_command манифеста).
        print("module %s: не game-derived (в манифесте нет base) —\n  %s"
              % (module, e.get("fix_command",
                               "python3 tools/pyc/install_patch.py " + module)))
        sys.exit(2)

    base = os.path.join(_ROOT, e["base"])
    if not os.path.exists(base):
        print("base missing: %s\nrecreate the decompilate from your own game copy:\n  python3 tools/pyc/decompile_lit.py %s"
              % (e["base"], module))
        sys.exit(1)

    patch_path = os.path.join(_ROOT, e["patch_file"])
    if not os.path.exists(patch_path):
        print("patch file missing: %s" % e["patch_file"])
        sys.exit(1)

    base_text = open(base, encoding="utf-8").read()
    patch_text = open(patch_path, encoding="utf-8").read()
    print("module %s" % module)
    print("  base: %s (md5 %s, %d B)" % (e["base"], md5_file(base), os.stat(base).st_size))
    print("  patch: %s (%d B)" % (e["patch_file"], os.stat(patch_path).st_size))

    report = []
    applied, err = apply_patch(base_text, patch_text, report)
    if applied is None:
        print("APPLY STOPPED, nothing written: %s" % err)
        sys.exit(1)
    for r in report:
        print("  %s" % r)

    target = os.path.join(_ROOT, e["repo_py"])
    text = applied.replace("\r\n", "\n")
    if md5_text(text) != e["py_md5"]:
        print("applied content md5 %s != manifest py_md5 %s — base does not match"
              " the patch's revision; nothing written" % (md5_text(text), e["py_md5"]))
        sys.exit(1)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(target, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("  wrote %s (LF, md5 %s == manifest)" % (e["repo_py"], e["py_md5"]))

    src_mtime = e["pyc_source_mtime"]
    os.utime(target, (src_mtime, src_mtime))
    print("  mtime set to pyc_source_mtime %d" % src_mtime)

    pyc = os.path.join(_ROOT, e["repo_pyc"])
    out, rc = compile_worker(paths.win_path(target), paths.win_path(pyc))
    print("  worker: %s" % out)
    if rc != 0 or "BUILD-OK" not in out:
        print("compile failed")
        sys.exit(1)
    # заголовок .pyc: magic (уже 2ded0d0a) + mtime исходника из манифеста
    d = bytearray(open(pyc, "rb").read())
    d[4:8] = struct.pack("<i", src_mtime)
    with open(pyc, "wb") as fh:
        fh.write(bytes(d))
    got = md5_file(pyc)
    print("  .pyc header: magic %s, mtime %d" % (d[:4].hex(), struct.unpack("<i", d[4:8])[0]))
    if got == e["md5"]:
        print("  md5 %s == manifest %s: OK (канонический путь репозитория)" % (got, e["md5"]))
    else:
        # тест на mirror-дереве: co_filename в байткоде = путь mirror, тело
        # legitimately отличается от эталона; побитовая проверка — md5 .py
        print("  md5 %s != manifest %s: компиляция НЕ из канонического пути"
              " репозитория (co_filename в теле), md5 .py проверен выше" % (got, e["md5"]))

    if install:
        sys.path.insert(0, os.path.join(_ROOT, "tools", "pyc"))
        import install_patch
        # идемпотентно: install_patch пересобирает .py воркером и кладёт .pyc
        # в install_dir (запись в игру — только под явным --install)
        install_patch.build_and_install(module)
        print("installed %s" % e["target"])

    sys.exit(0)


def md5_text(text):
    return hashlib.md5(text.encode("utf-8")).hexdigest()


if __name__ == "__main__":
    main()
