# tools/archive — архив одноразовых RE-зондов

Здесь собраны скрипты законченных и закрытых итераций реверс-инженерии
Lethal Dreams. Это **архив**: они не входят в рабочую Pipeline, не поддерживаются
и не запускаются как часть процесса. Запуск этих скриптов может подключаться
Frida к живому процессу игры и писать в установку игры `config[game.install_dir]`,
поэтому без явной необходимости их не трогаем. Личные пути — на базе `assets.base`
внутри скриптов — исторические: они отражают конфигурацию машины на момент
исследования и не обязательно действующие.

Канонический текущий инструментарий:
- `tools/extract_pristine.py` — извлечение оригиналов из архивов (lib-кэш, .pyc,
  ресурсы) в `cache/`;
- `tools/measure/` — PNG-мерители и кроперы снимков (18 Python + 6 PowerShell):
  декодер `pngdiff.py`, диффы `pdiff.py`/`pngregdiff.py`/`region_diff.py`,
  канвасные мерители `measure.py`/`measure_cmp.py`/`band_bbox.py`/`pts.py`/
  `color_scan.py`/`edge_scan.py`, строки текста `lines.py`/`textlines.py`/
  `tb.py`/`tb2.py`/`linebb.py`/`gyr.py`/`gyr2.py`, сводка игрового лога
  `logrows.py`; PowerShell `norm_canvas.ps1` (канвасная нормализация),
  `downscale.ps1`, `stack_cmp.ps1`, `crop_center.ps1`, `crop_rect.ps1`,
  `crop2160.ps1`. Каноничная форма — `python3 tools/measure/<script>.py ...`
  из любого cwd (бутстрап `_HERE`/`_ROOT` разрешает `pngdiff` и `paths`);
- `tools/pyc/` — пайплайн декомпилята и патча: `decompile_lit.py`, `hdpatch.py`,
  `install_patch.py`, `make_cfg.py`, `parity_file.py`, `parity_vs.py`, `probe1.py`,
  `py22_worker.py`, `pyc_norm.py`, `pyc_scan.py`, `pycbuild.py`, `uninstall_hd.py`,
  `vdiff_canon.py`, `vdiff_mod.py` и таблица опкодов `opcode22.txt`
  (14 скриптов + opcode22.txt);
- `tools/exe/` — EXE-домен: `apply_baseline.py` (собирает baseline EXE из
  pristine-архива в `backup/LETHALDREAMS.EXE.pre4`; флаги `--output <file>` и
  `--install`), `exepatch3.py` (borderless-патчи), `exepatch12.py` (шрифт 4x в 16:9
  + правка проекции мира; режим `restore`), `disex.py` (дизасемблирование VA-диапазона)
  и `build_cave.sh` — сборка блоба пещеры из исходника: `sh tools/exe/build_cave.sh fixall`
  (имена: `fixall`, `fontscale`, `fontscale4x`; исходники `caves/fixall.s`,
  `caves/fontscale.s`, `caves/fontscale4x.s`). Пути — только из `paths.py`
  (`LD_CONFIG` перенаправляет install_dir и cache, `LD_BAK` перенаправляет BAK —
  нужно для тестов на фейковой игровой цепи). Цепь EXE (md5):
  pristine entry `LethalDreams.exe` md5 `7943036fbd1233a0dbd43228f129cfc7`
  → exepatch3 (6 правок) = `deed9cf640de1fe31ecec6deacad21bd`
  → 2 байта `6A 00` -> `6A 01` на fileoff 632177..632178 = baseline
  `45e93d46aad6ceb3dfc17e86d04e4f82` (= `backup/LETHALDREAMS.EXE.pre4`)
  → exepatch12 = `171319bbc170cf6e57c8d36a8ae5f50d` = установленный игровой EXE.
  Блобы пещеры: `fixall.bin` 236 B md5 `bf3fc99b796c9d42629db62e37829aba`,
  `fontscale.bin` 192 B md5 `546fd954343f87623cbd37a264622db8`,
  `fontscale4x.bin` 224 B md5 `f320fcb6e1a39e9fe0c729d716c923f4`.
- точки входа пайплайна: `pycbuild` (гейт компиляции, паритет, сборка .pyc,
  установка), `install_patch`, `uninstall_hd`, `make_cfg`, `extract_pristine`.

Каноничная форма команды — `python3 tools/pyc/<script>.py` из корня репозитория.
Установка игры `config[game.install_dir]` для команд чтения только: read-команды —
`python3 tools/pyc/pycbuild.py gate`, `python3 tools/pyc/pycbuild.py fnames-all <out>`,
`python3 tools/pyc/pycbuild.py vdiff <module>`, `python3 tools/extract_pristine.py lib`
(они читают игру и пишут только в `build/` и `cache/`; игровая установка не изменяется).
Запись в игру делают только явные install-команды (`install`, `install-min`,
`install-hd`, `install-all`, `restore-all`).

Семейства (по подкаталогам):

- **frida_probe/** — `ld_*.py` и Frida-джойники `*.js`: одноразовые пробы
  масштабирования, инъекций, счётчиков текста, фаз отрисовки (закрытая итерация
  шрифтового масштабирования).
- **frida_ext/** — `x_*.py` / `x_*.js`: расширенные Frida-зоны бэктрейсов,
  crash-фильтра, GL/рендера, мат-риц и проекций. `x_ld_matread.js` лежит здесь,
  а не в frida_probe (он входит в семейство x_* — правило дубликата).
- **cave_gen/** — генераторы и фильтры пещерных мешей: `caveext.py`, `cavefilt.py`,
  `cavegate.py`, `cavegate417.py`, `cavegatew.py`, `cavegen.py`, `cavesrc.py`,
  `cavew.py`, `test_cave.py`; ассемблерные энкодеры `enc3.s`, `enc4.s` лежат
  в этом каталоге (не в `.tmp`). Сюда же переехал из `.tmp` сборщик блоба
  `build_cave4x.py` (содержимое не менялось; его пути `.tmp/fontscale4x.*` —
  исторические: канонический сборщик блоба — `sh tools/exe/build_cave.sh <name>`,
  см. EXE-домен выше).
- **disasm/** — диффы дизасемблирования pyc-байткода: `disdiff.py`, `disdiff2.py`,
  `disdiff3.py`, `disdiff4.py`.
- **font_gen/** — итерации генератора шрифта: `rescale_font.py`, `rescale_font2.py`,
  `rescale_font3.py`, `rescale_font4.py`, `rescale_font5.py`. Семейство выросло на
  4 файла из `.tmp`: `fontqc.py` (QC 4x-апскейла RGBA-листов), `maskqc.py` (QC
  апскейла альфа-масок), `fontsrc_export.py` (выгрузка родных листов в PNG + сетка
  ячеек), `fontmask_export.py` (выгрузка масок покрытия в grayscale PNG). contents
  всех девяти файлов не менялось; во всех девяти (9/9) починен только путь импорта:
  `unpack_btf`/`pack_btf` разрешается в `tools/btf/` репозитория бутстрапом
  `_HERE`/`_ROOT` (`fontqc.py`, `maskqc.py`, `fontsrc_export.py`,
  `fontmask_export.py` — в C2; `rescale_font.py` (строка 20), `rescale_font2.py`
  (22), `rescale_font3.py` (24), `rescale_font4.py` (26), `rescale_font5.py` (12) —
  в C3, где цикл `for _tp in ('<scratch-каталог>','/d/tmp')` заменён на тот же бутстрап).
  Запускать их не нужно: их входные
  каталоги личные — `BAK = paths.btf_backup_dir()` (на этой машине есть, канонический
  источник — `cache/btf_backup`, `python3 tools/extract_pristine.py templates`) и
  каталоги вариантов апскейла `paths.assets_dir('LethalDreams_fonts_*')` (их нет);
  `-o` по умолчанию тоже указывает на каталоги под `assets.base`.
- **decompile/** — быстрые/временные декомпиляторы pyc (`decompile_all.py`,
  `decompile_fast.py`, `decompile_time.py`, `dd.py`, `inv_diff.py`,
  `pin_decompiled.py`). **Не запускать**: баннеры «не запускать» внутри
  `decompile_all.py`, `decompile_fast.py`, `pin_decompiled.py` сохранены.
- **exe_patch/** — опытный патчер exe (`exepatch10.py`).
- **probes/** — PowerShell-зоны окон, DPI, мер и кропов снимков экрана
  (`crop_*.ps1`, `measure_*.ps1`, `probe_*.ps1`, `shot_*.ps1`) и Python-зонд
  границ квадрата `qscan.py` (он импортирует `pngdiff` из `tools/measure/`,
  куда `pngdiff.py` переезжает в C3). Все эти скрипты **активируют окно игры и
  снимают экран** (Frida/`SetWindowPos`/снимок рабочего экрана): они никогда не
  запускаются как часть Pipeline. `set_layer.ps1` пишет значение `HIGHDPIAWARE`
  в реестре для EXE игры (путь к EXE там — исторический `game.install_dir`).
  Из корня репозитория в это семейство переезжают: `borderless.ps1`,
  `set_layer.ps1`, `restore_res.ps1`, `shot_activate.ps1`, `probe1_run.ps1`,
  `probe_clickshot.ps1`, `probe_dlgshot.ps1`, `probe_f1.ps1`, `probe_keyshot.ps1`,
  `probe_loadshot.ps1`, `probe_measure.ps1`, `probe_newgame.ps1`,
  `probe_ref1280.ps1`, `probe_shotwin.ps1` (содержимое не редактировано), а также
  исторические мерители снимков `measure_bgrows.ps1`, `measure_caption.ps1`,
  `measure_disc.ps1`, `measure_inkbox.ps1`, `measure_lines.ps1`,
  `measure_ring3.ps1`, `measure_ring4.ps1`, `measure_textrows.ps1`,
  `stack_crops.ps1` (после C3 в корне `.ps1` не осталось). Ранее лежавшие здесь:
  `crop_band.ps1`, `crop2.ps1`, `crop3.ps1`, `measure_cols.ps1`,
  `measure_ring.ps1`, `measure_ring2.ps1`, `measure_span.ps1`, `measure_tor.ps1`,
  `measure_torus.ps1`, `probe_dialog.ps1`, `probe_displaychange.ps1`,
  `probe_displaychange_exit.ps1`, `probe_dpi.ps1`, `probe_flicker.ps1`,
  `probe_mem.ps1`, `probe_mode.ps1`, `probe_resize.ps1`, `probe_shot.ps1`,
  `probe_windows.ps1`, `shot_info.ps1`, `shot_small.ps1`, `shot_win.ps1`.
- **texture_qc/** — семейство экспорта/QC текстур (19 скриптов закрытой итерации
  апскейла `.btf` через Upscayl; в C3 переезжает из `.tmp`, логика не менялась):
  `export_textures.py` (выгрузка родных .btf в PNG + MANIFEST),
  `export_all.py` (выгрузка всего оригинального бэкапа .btf),
  `export_bmp.py` (конвертация plain .bmp из `data/wrender/figures` в PNG),
  `reexport.py` (повторная выгрузка текстур с фальшивыми квадратными dims),
  `fix_scan.py` (поиск экспортированных с неверными dims),
  `state_scan.py` (классификация установленного .btf vs канонические dims),
  `all_check.py` (сверка всей игры после правки dims),
  `final_check.py` (строгая сверка второй установочной партии),
  `verify_staged.py` (строгая сверка staged .btf с source-PNG),
  `repack_fixed.py` (упаковка и установка 5 исправленных текстур),
  `replace_textures.py` (замена игровых .btf на 2x-апскейлы),
  `prune_src.py` (чистка `LethalDreams_textures_src` от уже установленных),
  `classify_pending.py` (installed/pending по dst-PNG),
  `game_census.py` (перепись `data/textures` vs оригинальные размеры),
  `hdr_probe.py` (распределение w,h/a/b в заголовках оригиналов),
  `layout_probe.py` (построчная и планарная структура текстур),
  `layout_variants.py` (PNG-превью кандаидатных раскладок пикселей),
  `lanczos_qc.py` (QC Lanczos-2x там, где Upscayl не справился),
  `lc_preview.py` (nearest-увеличенное side-by-side превью orig vs upscale).
  Импорт `unpack_btf`/`pack_btf` разрешается в `tools/btf/` репозитория
  (бутстрап `_HERE`/`_ROOT`, правка только sys.path). Их личные пути
  (`paths.assets_dir('LethalDreams_textures_*')`, `paths.scratch_dir()/*.txt`,
  `MANIFEST.txt`) — исторические: рабочие каталоги Upscayl на этой машине
  отсутствуют; канонический источник оригиналов — `cache/btf_backup`
  (`python3 tools/extract_pristine.py templates`) и `assets.hd_pack`.
- **tmp_scratch/** — черновики из `.tmp` (`secparse.py`, `zerorun.py`,
  `fixints_debug.py`, `bm_patch_backup.py`). Сюда же переехал из `.tmp` дампер
  заголовка `.btf` `tlv_dump.py` (содержимое не менялось; его путь
  `config[game.install_dir]/data/textures/` — исторический, читает установку
  игры). После этих переездов в каталоге `.tmp` не осталось ни одного файла.

**8 архивных скриптов не парсятся Python 3** (py2-only `print` без скобок, незакрытые
`***`-строки баннеров): `disasm/disdiff.py`, `disasm/disdiff2.py`, `disasm/disdiff3.py`,
`disasm/disdiff4.py`, `decompile/decompile_all.py`, `decompile/decompile_fast.py`,
`decompile/pin_decompiled.py`, `tmp_scratch/bm_patch_backup.py`. Остальные 97 архивных
`.py` парсятся Python 3 (проверено AST-разбором).

Авторитетное знание по проекту — документация в `docs/` (корень репозитория):
`docs/NOTES.md` (удалён из репозитория; содержание — в `docs/archive/NOTES_OLD.md` и
в сводных `docs/STATE.md`/`docs/PATCHES.md`), `docs/archive/NOTES_OLD.md`, `docs/archive/PATCH_PLAN_FONT.md`,
`docs/archive/PATCH_R_E_HELP.md`, `docs/archive/PLAN_borderless.md`, `docs/archive/RESEARCH_PY_DECOMPILE.md`,
`docs/archive/RESEARCH_TEXT_SCALE.md`, `docs/archive/SCALING_HANDOFF.md`, `docs/archive/TASK_btf_unpacker.md`,
`docs/archive/TASK_decompiler.md`, `docs/archive/TODO_FONT_WIDTH.md`, `docs/archive/VERIFY_HANDOFF.md`
(мигрированы из корня), и сводные документы `docs/STATE.md`, `docs/VERIFY.md`,
`docs/PATCHES.md`, `docs/PIPELINE.md`, `docs/TOOLS.md`, `docs/KNOWLEDGE.md`,
`docs/CONTRADICTIONS.md`; исследовательский указатель — `docs/research/README.md`.
Ссылки архивных скриптов на корневые `.md` исторические.

Архивные скрипты содержали личные абсолютные пути — исторические. Перед публикацией они нейтрализованы: игровая установка → `config[game.install_dir]` / `paths.game_path(...)`, scratch-каталоги → `paths.scratch_dir(...)`, бэкап родных листов → `paths.btf_backup_dir()`, рабочие каталоги апскейла → `paths.assets_dir(...)`, пути репозитория → `_ROOT`-относительные. Факты, числа, md5, смещения и логика скриптов при этом не менялись. По путям импорта починены:
`tools/pyc/parity_file.py`, `tools/pyc/parity_vs.py` (перенесены в `tools/pyc/`
и переведены на бутстрап через `paths.py`), `tools/archive/tmp_scratch/fixints_debug.py`
(цигвинские `/cygdrive/c/...` inserts вместо `sys.path.insert(0, '.')`) и
`tools/archive/decompile/dd.py` (вызов `tools/pyc/pycbuild.py`, cwd — корень
репозитория); в C3 — 15 скриптов `tools/archive/texture_qc/` (insert
`paths.scratch_dir()` → бутстрап в `tools/btf/`), 5 `tools/archive/font_gen/rescale_font*.py`
(цикл `for _tp in ('<scratch-каталог>','/d/tmp')` → тот же бутстрап) и
`tools/archive/probes/qscan.py` (insert корня репозитория → `tools/measure/`).
Остальное содержимое архива не трогали: нейтрализация путей — единственная правка архива.
Формулировки «содержимое не менялось» выше относятся к переездам в репозиторий; личные
абсолютные пути во всех файлах архива нейтрализованы (см. абзац выше).

Ничего из этого каталога не удалять; содержимое скриптов не редактируется (единственная правка — нейтрализация личных абсолютных путей перед публикацией).
