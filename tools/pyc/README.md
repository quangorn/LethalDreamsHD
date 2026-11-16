# tools/pyc — работа с `.pyc` игры (Python 2.2.3)

Каноническая форма всех команд: `python3 tools/pyc/<script>.py` из корня
репозитория. Все пути берутся из `paths.py` (`config.toml`).

## Слои

- `py22_worker.py` — **только Python 2.2.3** (`print` как statement; парсер
  Python 3 падает на строке 84). Запускается исключительно через
  `paths.py223_exe()` (ключ `py223.dir`), никогда через `python3`. Режимы:
  `compile`, `build`, `dis`, `vdiff`, `litfix2`, `fnames` и др.
- Остальные скрипты каталога — Python 3; они вызывают воркер подпроцессом.

## Скрипты

| файл | роль |
|---|---|
| `pyc_scan.py` | парсер marshal Python 2.2, каталог `.pyc` игры по magic (`--code` — layout code object) |
| `decompile_lit.py` | декомпилят + восстановление литералов: `<module>`, `--all`, `--check-battlemenu`; результат в `decompiled_fixed/<module>.py` (локальный, в git не входит) |
| `pycbuild.py` | пайплайн: normalize/gate/build/parity/gate-all/sim-all/fixlits/fix-all/vdiff/fixints/build-all/hd-all/install-min/install-hd/install-all/restore-all/install-list/install-fns/install-probe-all/fnames/fnames-all/vdiff-all/install/uninstall; список команд печатается запуском без аргументов. Установка/откат в игру — команды, явно называющие установку |
| `py22_worker.py` | воркер 2.2.3 (см. выше); `PYTHONPATH = py223.dir + paths.cache_lib_dir()` (игровой `Lib` не входит) |
| `install_patch.py` | сборка `patches/<module>.py` → `.pyc` компилятором 2.2.3; запись в игру только с `--install` |
| `uninstall_hd.py` | полный откат к оригиналам (`--check` — сверка без записи) |
| `make_cfg.py` | генератор `cfg_default.py`/`globalscript.py`; запись в установку только с `--install` |
| `parity_file.py` | паритет байткода готовой пары: `<pyc> <py>`; ничего не пишет |
| `parity_vs.py` | паритет компилята нашего `.py` против оригинала: `<module> <orig.pyc>` (оригинал — из `backup_orig/`, локального, в git не входящего) |
| `hdpatch.py` | адъюстер HD-масштаба к `build/*.py` (идемпотентен) |
| `probe1.py` | зондовые сборки (исторический прогон съёмки; снимки прогонов — артефакты в `scratch_dir('ld')`) |
| `pyc_norm.py`, `vdiff_canon.py`, `vdiff_mod.py` | библиотеки нормализации/сравнения байткода (вызываются из pycbuild, не CLI) |

## Рецепт `.pyc`

`.pyc` = magic `2ded0d0a` + `pyc_source_mtime` (4 байта, little-endian) + тело
маршалла. Тело побитово равно компиляту канонического исходника; заголовок
ставится равным манифестному `pyc_source_mtime`. Эталонная пара
`.py`/`.pyc` для публикуемых модулей — `release/manifest.json` (`code_tier`).

## База декомпилята

`decompiled_fixed/**` — производные от игрового bytecode, локальные, в git не
входят; создаются из вашей копии игры:
`python3 tools/pyc/decompile_lit.py <module>` (`--all` — все 434 модуля).
Полный порядок шагов: `docs/PIPELINE.md`.
