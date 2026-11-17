# TOOLS.md — инструменты репозитория и канонические команды

## Легенда путей

Все пути берутся из `paths.py` (читает `config.toml`; шаблон —
`config.example.toml`):

- `paths.game_path(*parts)` — файл установленной игры (`install_dir` + части; часть
  с двоеточием драйва, ведущим `/` или `..` отвергается с ValueError);
  `paths.install_dir()` — корень установки;
- `paths.assets_dir(rel)` — корень HD-ассетов (`assets.base`);
- `paths.cache_dir(subdir)`, `paths.cache_lib_dir(subdir)`,
  `paths.btf_backup_dir(subdir)`, `paths.scratch_dir(subdir)` — рабочие директории
 (в git не входят); каталога `paths.build_dir()` нет (`build/` — путь репозитория,
  константа `pycbuild.BUILD`);
- `paths.win_path(p)` — native-форма пути для windows-python;
- archive-ключи: `game.pristine_archive` (original.7z — без `Lib`),
  `assets.hd_pack` (hd_pack.7z), `game.snapshot_archive` (6.7z — полный снап
  установки с `Lib`); `paths.archive_list_of(archive)` — листинг записей;
- `py223.dir` / `paths.py223_exe()` — Python 2.2.3 (компил-воркер игровых модулей);
  `python3` — сам интерпретатор инструментов;
- `check.exclude` — glob-ы неприкосновенного яруса (файлы, которых может не быть).

`scratch_dir('<имя>')` и `cache_dir('<имя>')` в документах — рабочие директории из
config (`scratch.dir`, `cache.dir` + подсказанная поддиректория). Снимки и логи
прогонов (`scratch_dir('ld')/<tag>_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), `scratch_dir('ld')/<tag>_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), `scratch_dir('ld')/REPLACE_LOG.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит) и т. п.) — артефакты
прогона: они пересоздаются скриптами, в git не входят; ссылка вида
`scratch_dir('ld')/<имя>` говорит, где их искать после прогона. Собираемые блобы
(`fixall.bin`, `fontscale.bin`, `fontscale4x.bin`, `.o`) — в `cache_dir('cave')` после
`sh tools/exe/build_cave.sh <name>`. Родные бэкапы текстур (`paths.btf_backup_dir()/*_orig.btf (родной бэкап оригинала (восстанавлируется из архивов: `python3 tools/extract_pristine.py templates`), в git не входит)/.fdf`) —
в `paths.btf_backup_dir()`, восстанавливаются из архивов (`tools/extract_pristine.py templates`).

## Проверка и применение

```
python3 tools/check_state.py --quick | --full | --untouched [--json]   # только чтение
python3 tools/apply_patch.py <module>            # diff -> patches/<module>.py + компиляция
python3 tools/apply_patch.py <module> --install  # запись .pyc в игру (делегирование)
python3 tools/pyc/install_patch.py <module>      # сборка, игру НЕ трогает
python3 tools/pyc/install_patch.py <module> --install
python3 tools/pyc/uninstall_hd.py                # по умолчанию --check (сверка, игра не трогается)
python3 tools/pyc/uninstall_hd.py --install            # откат к оригиналам (запись в игру)
python3 tools/pyc/make_cfg.py <w> <h> [--fs]      # генерация в cfg/ (игру не трогает)
python3 tools/pyc/make_cfg.py <w> <h> [--fs] --install
```

Модули apply_patch: `gamescript, ui/ui, ui/cursor, ui/olmenu, ui/battlemenu, ui/btutorial`.
Запись в игровую установку — только под явным `--install` (правило AGENTS.md).
Команды pycbuild, явно называющие установку (`install`, `install-min`, `install-hd`,
`install-all`, `install-list`, `uninstall`, `restore-all`), пишут в игру по имени команды.

## pyc-пайплайн (tools/pyc)

```
python3 tools/pyc/decompile_lit.py <module>        # декомпилят+литералы в decompiled_fixed/<module>.py
python3 tools/pyc/decompile_lit.py --all            # все 434 модуля (+ report.txt)
python3 tools/pyc/decompile_lit.py --check-battlemenu
python3 tools/pyc/pyc_scan.py [--code]            # каталог .pyc игры по magic (парсер marshal 2.2)
python3 tools/pyc/pycbuild.py <cmd> [args]        # normalize/gate/build/parity/vdiff/fixlits/... (см. ниже)
python3 tools/pyc/py22_worker.py <mode> <args>     # воркер Python 2.2.3; py2-скрипт (py3 AST падает)
python3 tools/pyc/parity_file.py <pyc> <py>       # паритет байткода пары файлов
python3 tools/pyc/parity_vs.py <module> <orig.pyc> # паритет компилята .py против оригинала
python3 tools/pyc/hdpatch.py                      # адъюстер HD-масштаба к build/*.py
python3 tools/pyc/probe1.py                       # зондовые сборки (после 2026-11 снимки не нужны)
```

`vdiff_canon.py`/`vdiff_mod.py` — библиотеки сравнения canon vs мод (вызываются
pycbuild, не CLI). Список всех команд pycbuild: `python3 tools/pyc/pycbuild.py` без
аргументов. Ключевое: `tools/pyc/README.md`.

Полный пайплайн (gate/parity/build/install, наполнение кэша):
`docs/PIPELINE.md`.

## btf-пайплайн (tools/btf)

```
python3 tools/btf/unpack_btf.py <btf> <out.png>      # распаковка texture blob
python3 tools/btf/pack_btf.py <in.png> <template.btf> -o <out.btf>  # запаковка по шаблону
python3 tools/btf/black_btf.py                      # чёрные сайдкары (эксперимент, sidecar)
```

(массовые pack-прогоны делались историческими скриптами сессий; итоговый
список устанавливаемых .btf зафиксирован в `release/manifest.json` assets_tier)

## Шрифты (tools/fonts)

```
python3 tools/fonts/gen_font4x.py [--install]        # листы 4x (.btf+.fdf) из родных
python3 tools/fonts/restore_fonts.py                 # проверка отката (запись — только с --install)
python3 tools/fonts/restore_fonts.py --install         # откат листов в установку
python3 tools/fonts/font_stats.py                    # статистика глифов листов
python3 tools/fonts/font_preview.py                  # превью листов
session-имена `pack_fonts.py` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z) / `unpack_fonts.py` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z) — утрачены (проверено: покрытия нет
ни в git, ни в приватных архивах сессий); пары `.fdf+.btf` умеют canon-скрипты
`tools/btf/pack_btf.py`, `tools/btf/unpack_btf.py`
```

## Измерения (tools/measure)

`measure.py`, `measure_cmp.py`, `region_diff.py`, `pngdiff.py`, `pngregdiff.py`,
`edge_scan.py`, `band_bbox.py`, `color_scan.py`, `linebb.py`, `lines.py`,
`logrows.py`, `norm_canvas.ps1`, `crop_rect.ps1`, `crop_center.ps1`,
`crop2160.ps1`, `downscale.ps1`, `stack_cmp.ps1` — замеры скриншотов, глифов,
text-lines (история измерений: `docs/OBSERVATIONS_FONT.md`, `docs/PITFALLS.md`, `docs/archive/TODO_FONT_WIDTH.md`).

## exe (tools/exe)

```
sh tools/exe/build_cave.sh <name>   # name=fixall|fontscale|fontscale4x; блобы в cache_dir('cave')
python3 tools/exe/exepatch12.py            # baseline -> scratch_dir('exe')/LETHALDREAMS.EXE.patched
python3 tools/exe/exepatch12.py --install  # тот же результат — запись в EXE установки
python3 tools/exe/exepatch12.py restore    # вернуть baseline (в выходной файл)
python3 tools/exe/exepatch3.py            # читает EXE установки, пишет в scratch; --install — в игру
python3 tools/exe/apply_baseline.py        # baseline -> backup/LETHALDREAMS.EXE.pre4
python3 tools/exe/apply_baseline.py --output <файл>        # baseline -> файл (тесты)
python3 tools/exe/apply_baseline.py --install          # копия baseline в install_dir
python3 tools/exe/disex.py                # disassembly-загрузчик (py3) — базовая загрузка EXE
```

## Архивы

```
python3 tools/extract_pristine.py lib        # наполнение cache/lib из pristine
python3 tools/extract_pristine.py templates  # btf-шаблоны/родные пары в cache
```

Измерение ярусов (замеры для `docs/VERIFY.md`) делал локальный сверочный скрипт
(приватный, в репозиторий не входит); его результаты зафиксированы в VERIFY.

Архив Python-слоя (`python_libs.7z`, приватный — путь задаётся приватным ключом config,
в репозиторий не входит): 725 файлов = `python22.dll` (2.2.3) + `Lib/` (713) + 11 корневых
`.pyc`; crc32-сверка: все побитово равны текущей установке. Это источник восстановления
слоя (замеры и обязательность — `docs/OBSERVATIONS_ENGINE.md`, раздел «Python-слой»).

Исторические скрипты сессий: `tools/archive/README.md`.


## Тулчейн
- `tools/pyc/pycbuild.py` — normalize/gate/build/parity/gate-all/sim-all/fixlits/fix-all/vdiff/
  fixints/build-all/hd-all/install-min/install-hd/install-all/restore-all/install-list/
  install-fns/install-probe-all/fnames/fnames-all/vdiff-all/install/uninstall (команды
  печатаются запуском без аргументов).
  Рабочий интерпретатор: `config[py223.dir]` (Python 2.2.3, магия `.pyc` `2ded0d0a`/0xED2D —
  читается движком), требует `PYTHONPATH = py223-интерпретатор + cache/lib (paths.cache_lib_dir())`
 (игровой `Lib` не входит).
  - `[P]` **Дерево интерпретатора** (2026-11-17): `config[py223.dir]` указывает на `vendor/py223/`
  в корне репозитория — untracked (`vendor/py223/**` в `.gitignore`, в git не входит),
  собирается из архивов python.org. Состав: корень `python.exe` (20526 B, md5
  `463004315f9915a4c7fb49b811d30dbc`), `pythonw.exe`, `python22.dll` (847920 B, md5
  `4ee79da1533f8ac2957fd46c6ba1cb2d` — побитово тот же, что в игровом бинарном ярусе,
  `docs/STATE.md`), `w9xpopen.exe`; `Lib/` — каноническое stdlib-дерево из `Python-2.2.3.tgz`
  (регистр имён источника); `DLLs/` — extension-модули установщика (`_sre.pyd`, `_symtable.pyd`,
  `parser.pyd`, `zlib.pyd`, `pyexpat.pyd`, …). Проверено в этом дереве: startup-`import site`
  проходит, `import sys, os, string, codecs, re, marshal, struct, imp, time, dis, types, UserDict`
  — OK, `imp.get_magic()` = `[45, 237, 13, 10]`. Источники: `Python-2.2.3.exe` (7334106 B, md5
  `d76e774a4169794ae0d7a8598478e69e`, `https://www.python.org/ftp/python/2.2.3/Python-2.2.3.exe`)
  даёт `PYTHON.EXE` (20526 B), `PYTHONW.EXE`, `PYTHON22.DLL` (847920 B) и DLLs;
  `Python-2.2.3.tgz` (6709556 B, md5 `169f89f318e252dac0c54dd1b165d229`,
  `https://www.python.org/ftp/python/2.2.3/Python-2.2.3.tgz`) даёт канонический
  `Lib/` (`plat-win` в tgz нет; путь `lib/plat-win` отсутствующий — безвреден). Плоский дамп
  установщика (имена в ВЕРХНЕМ регистре) **не импортируется** — грабли: `docs/PITFALLS.md`,
  раздел «Импорт 2.2.3 чувствителен к регистру имени модуля (плоский дамп установщика не
  импортируется)».
  - `[P]` Замеры состава (2026-11-17, повторная сборка дерева): `Lib/` = 832 файла / 30 каталогов
    (из tgz), `DLLs/` = 43 файла, корень = 4 файла. Рецепт восстановления: `Lib/` —
    `robocopy <распакованный tgz>/Lib vendor/py223/Lib /E` (регистр имён tgz; PowerShell
    `Copy-Item -Recurse` с wildcard-источником сплющил дерево в 180 файлов без каталогов —
    зафиксировано, не использовать); корень и `DLLs/` — бинары дампа установщика с именами
    в нижнем регистре (`python.exe`, `pythonw.exe`, `python22.dll`, `w9xpopen.exe`,
    `*.pyd`/`*.dll`/`*.lib`). Все файлы создаются нативными инструментами Windows
    (`robocopy`/`Copy-Item`), не Cygwin-копированием.
- `tools/pyc/py22_worker.py` — **только Python 2.2.3** (print-Statement; py3 AST
  падает на строке 84). Он вызывается через `paths.py223_exe()`, никогда python3.
- `tools/pyc/probe1.py` — генератор зондовых сборок: чистый build из snapshot + правки →
  `patches/<module>.py`, компиляция 2.2.3 → `patches/<module>.pyc` → установка только
  перечисленных модулей в игру. Правки применяются к чистому build; `build/` и
  `decompiled_snap/` не мутируются.
- `tools/archive/probes/probe1_run.ps1` — прогон: kill старых процессов, запуск, меню (5 с), movers
  OS-курсорa по 6 известным window-relative точкам (пишет `OSPOS ...` в
  `config[game.root]/hdlog.txt`), topmost-подъём окна перед съёмкой, курсор ОС за
  пределы окна, снимок **только окна** в `scratch_dir('ld')/<tag>_win.png` (артефакт
  прогона, пересоздаётся скриптом, в git не входит).
- Зонды пишут в `config[game.root]/hdlog.txt` (append): `SCREEN`, `INITBB`,
  `SETPOS`, `SET2`, `CHECK`, `MM/LBD/LBUP`, `OSPOS`.
- `tools/archive/probes/restore_res.ps1` — вернуть desktop 3840x2160@120 после fullscreen-прогонов.
- Логи прогонов и снимки: `scratch_dir('ld')/<имя>` (пересоздаются прогонами, в git не входят).
- `[P]` **Чёрный фон для измерений** (`tools/btf/pack_btf.py` + флаг): `tools/btf/pack_btf.py` собирает
  all-black `.btf` тем же контейнером (`LDPACKER`+флаги(4)+u32 clen+zlib; w,h по
  смещениям 52/56, pixels 3bpp). Sidecar'ы были в установке: `config[game.root]/data/textures/mm_bkg01_black_43.btf`
 (1024x1024) и `mm_bkg01_black_169.btf` (2048x2048) — в текущей установке их нет
 (black-эксперимент свёрнут; покрытия в git нет, в hd_pack.7z есть 169-пара);
  `hd_res.Prepare()` выбирает их, если в корне игры есть файл
  `config[game.root]/blackbg.flag` (вкл/выкл без пересборки; в текущей установке нет).
  Фон арта при
  этом не меняется.
- `[P]` Измерители центра кольца: `tools/archive/probes/measure_ring4.ps1` (tight-маска тора: белый>235 /
  красный R>120,G<60,B<60 / чёрный дуга — bbox+ratio), `tools/archive/probes/measure_ring3.ps1` (центроид
  сияния, 2 итерации), `tools/measure/crop_center.ps1` (кроп квадрата вокруг центра кадра). На
  цветном фоне центроид-метрики засорены спиралью — trustworthy только чёрный фон.
- `[P]` `tools/archive/probes/measure_bgrows.ps1 <png>` — постредный профиль яркости (среднее по строке,
  шаг выборки 32 px): first/last non-black row, top/bottom black, профиль каждые 64
  строки. Для проверки фонового заполнения кадра (фон-арт, не чёрный фон).
