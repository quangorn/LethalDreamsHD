# VERIFY.md — модель проверки состояния

Инструмент: `tools/check_state.py` (read-only; ничего не исправляет; печатает
готовую команду исправления из манифеста). Ярусы: `--quick`, `--full`,
`--untouched`; вывод `--json` пишется только в cache/scratch.

## Ярус code (`--quick`, `--full`)

Проверяется по `release/manifest.json`:

- EXE, 3 DLL, `hd.cfg` — строгий md5;
- реестр: App Compat Flag `~ HIGHDPIAWARE` для `LETHALDREAMS.EXE`
  (`reg query`, argv-вызов, cp866);
- 10 игровых .pyc: md5 установленный vs md5 base-печати манифеста; при
  расхождении — разбор header (magic, source mtime) и тела: печатается
  KNOWN-drift с объяснением и fix_command;
- строгий ярус `--quick`: расхождение cursor → rc 1 (CI); на 19.11.2026 дрейфов нет,
  rc 0 (cursor приведён к base-печати). `--full` печатает KNOWN и считает rc 0.

Пояснение md5-разбора для mirror-дерева: `co_filename` компилируется из пути,
из которого компилировали; тело совпадает только при компиляции из канонического
пути — checker печатает поясняющую строку.

Дрейфов манифеста нет (cursor закрыт 19.11.2026, `MATCH`); механизм `DRIFT_KNOWN`
сохраняется для аналогичных исторических сборок (`docs/PATCHES.md`).

## Ярус assets (входит в `--full`)

- .btf: сверка по правилу `assets_tier` манифеста (hd_pack побитово по путям,
  наборы `data/fonts` равны, CRC32 каждого installed .btf ∈ {pristine} ∪ {hd_pack});
  MISSING/DRIFT — печатается, `fix_command` на `tools/btf/pack_btf.py`;
  измеренное число устанавливаемых .btf = **1342** (планspec писал 1268 —
  факт измерен, список обязывает).
- каталоги сканов по манифесту; stdlib-обёртки: измерено **11** pyc
  (`types.pyc` + 10; spec писал 12 — honest diff).

## Ярус untouched (`--untouched`)

Всё, что патч не трогает, объясняется тремя архивами
(`archive.pristine`, `archive.hd_pack`, `archive.snapshot`). Алгоритм классификации
(порядок фиксирован): пара путь-в-архиве → PRISTINE; пара путь → HD_PACK;
пара путь → byte-identical репозиторий → REPO; пара путь в snapshot (strip
префикса `LethalDreams/`) → SNAPSHOT; basename+(size,crc) fallback; basename
(без контента) → REPO_BASENAME; UNCOVERED — репорт.

Замер на установке (5096 файлов, без `__pycache__`, 22.11.2026; `lethaldreams.dll` и
`LD-setup.bat` удалены из установки по решению пользователя):

- PRISTINE **2966**, HD_PACK **1379** (включая 30 `data/maps/*.map` с 3x-почвой),
  ALLOWED **728**, CODE **18** (включая `globalscript.pyc` = `PRISTINE_IDENTICAL`), EXCLUDED **4**,
  ARCHIVE_BASENAME **1** (`lethaldreams.dll`), REPO_BASENAME **1** (`console.py`, 0 B),
  UNCOVERED **0**; `hd.cfg` — REPO (артефакт пайплайна, в git не входит).
- allowlist: `data/video/*.wmv` (glob) + `.log`-класс; отсутствие
  `boolat.wmv`, `russobit_r.wmv` — разрешено (присутствуют в archive, отсутствуют
  в установках; run без exclude это доказывает);
- `absent_ok`: `lethaldreams.dll` (удалён из установки как неиспользуемый StarForce-стаб,
  см. `docs/KNOWLEDGE.md`) — не репортится как MISSING.
- `LD-setup.bat` удалён из установки по решению пользователя (2026-11-18): snapshot-only
  запись (в `original.7z` файла нет), MISSING не репортится; строка снята из allowlist'а.

black-эксперимент: чёрные .btf в установку не входят (флагов нет, sidecar-черные
только в snapshot) — ярус untouched их не требует.

## Контур тестов (пройдены)

1. real install `--quick`: на 19.11.2026 — rc 0, нарушений 0 (cursor приведён к base);
   исторически (до 19.11) — 1 DRIFT (cursor) + fix_command, rc 1; снапшот diff 0;
2. real install `--full`: rc 0, KNOWN-drift, diff 0;
3. `--untouched`: rc 0 (PRISTINE 2967 / HD_PACK 1379 / ALLOWED 728 / CODE 17 /
   EXCLUDED 4 — без code-файлов), diff 0;
4. fake game (полная копия, sabotaged: gamescript.pyc modified, white.btf deleted,
   zzz_inject.pyc (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z) added): quick rc1 (DRIFT pyc + DRIFT registry), full rc1 — исторический прогон сессии, артефакты в git не входят
   (MISSING white.btf), untouched rc1 (EXTRA zzz, MISSING white.btf); реал diff 0;
5. `LD_CONFIG` (scratch-конфиг) — tree-redirect работает;
6. отсутствующий/неполный конфиг → rc2, понятные сообщения; `--json` вне
   scratch/cache → rc2;
7. exclude-off: MISSING boolat.wmv+russobit_r.wmv (доказательство glob-правил);
   types.pyc/hd_res.pyc — ALLOWED.

Перед и после каждой команды — снапшот дерева установки (md5+mtime); во всех
тестах diff = 0, кроме runtime-эффектов (`hdlog.txt`; движок при запуске перекомпилирует
часть `Lib/*.pyc`); EXE md5 не менялся, cursor приведён к base 19.11.2026.

- `[P]` Известный mtime-дрейф: ровно 9 файлов установки меняют **mtime без изменения md5** —
  `data/fonts/ld_numbers15.fdf`, `data/fonts/ld_tahoma11.fdf`, `data/fonts/ld_uncial12.fdf`,
  `data/fonts/ld_uncial20.fdf`, `data/textures/ld_numbers15.btf`, `data/textures/ld_tahoma11.btf`,
  `data/textures/ld_uncial12.btf`, `data/textures/ld_uncial20.btf`, `data/textures/mm_bkg01.btf`.
  Причина: копирующая активность установленной `hd_res.Prepare()` во время игрового прогона
  (файлы побитово равны своим `*_169`-сайдкарам: md5 пары совпадают). Строгий md5-diff = 0;
  mtime-diff после прогона не нулевой. Обход: снимать снапшот до прогона и после него,
  сравнивать md5 (checker сверяет size+crc32, mtime не требует).


## Контрольные точки
- `[P/U]` **4:3-регрессия пройдена (2026-11-14, пользователь подтвердил).** Прогон
  1920x1440 окно с установленными `gamescript.pyc` (hook) + `hd_scale.pyc`: — компиляты `.pyc` в установке, в git не входит
  лог даёт `k=1.0 dx=0.0`, пост-фиксы ни разу не сработали (identity-гарантура
  обёрток), меню целое, курсор работает. Снимок `scratch_dir('ld')/c43_win.png` (снимок прогона) = база.
- `[U]` **16:9 fullscreen (3840x2160, hd7): все пункты ок** (фон без полос, глифы,
  диалог, курсор); остаток — лёгкая вертикальная вытянутость некоторых элементов
  («разберёмся потом»). `[P]` Того прогона семантика: движок Y мыши = P/s uniform
  -> `MY = m/k`; X = `(m-dx)/k`; размеры `SC = w*k` (без dx), `EY = h`.
- `[P]` dxwrapper: `ConfigureDpiAwareness=1` => процесс **HighDpiAware** (DPI не при чём);
  `FullscreenWindowMode=1` превращает окно в engine-fullscreen (borderless) — все
  прогоны hd4..hd9 фактически были engine-fullscreen; сейчас в ini `FullscreenWindowMode=0`.
- `[P]` **True windowed (окно с рамкой, FullscreenWindowMode=0)**: движок отдаёт Y мыши
  per-axis `P/sy` (sy=h/768) => canvas Y = m (`MY=m` при F=0). Замер hd11:
  OS y=324 -> движок m=229 = 324/1.40625; при `MY=m/k` точкаcenter ушла +107 px.
  Итог: **mode-aware MY**: F=0 -> `m`, F=1 -> `m/k`; при k=1 оба identity.
- `[U]` **Borderless достигнут (2026-11-14, пользователь подтвердил).** Состав: игра в
  true windowed (`hd.cfg = 3840 2160 32 0`) + `tools/archive/probes/borderless.ps1` (снимает рамку стиля,
  ставит popup topmost 0,0 3840x2160) + dxwrapper отключён (stub `dsound_stub.bak`).
  Мигания при переключении окон нет (modeset не трогается). Применение после запуска:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/archive/probes/borderless.ps1` (в probe1_run
  есть флаг `-Borderless`).
- `[P]` Окно-без-рамки: `hd_scale` флаг `HD_BORDERLESS` выбирает семантику Y мыши;
  текущая сборка = 1 (`MY = m/k`); живой тест центрирования точки в центре экрана
  подтверждает/опровергает (если точка ниже указателя — ставить 0).
- `[P]` Архитектура правок: `patches/hd_scale.py` (собственный модуль, обёртки над
  оригинальными `ui.BActionElement` и `cursor.BCursor`) + хук в `gamescript.pyc`.
  `ui.pyc`/`cursor.pyc` — оригиналы. При k==1 обёртки вызывают оригинал с оригинальными
  аргументами (гарантия 4:3). Фон (billboard w>=1000, h>=700) передаётся движку без
  масштаба -> фон/мир на весь экран, UI-бокс 1024k x 768k по центру.
- `[P]` Замер 16:9 (прогон hd5, 3840x2160 fullscreen): все UI-зоны переданы движку по
  `e = k*c + dx` (k=0.75, dx=128), полос 480 px; курсор-иконка следует за мышью с
  точностью до пикселя по 5 OSPOS-точкам. Снимок `scratch_dir('ld')/hd5_win.png` (снимок прогона).
- `[P]` Наблюдение: после fullscreen-прогонов окно 1920x1440 измерилось как
  1920x1440 физическое (без DWM-виртуализации), до — 1440x1080; снимать DPI-aware.


## Финальная верификация (2026-11-16)

- `[P]` rc-модель (2026-11-16): `--quick` rc 1 (тогда — два известных дрейфа: `ui/cursor.pyc` `4a8eb4bab413…`
  против base-печати `60754a253515…`, и историческая сборка `ui/olmenu.pyc`), `--full` rc 0, `--untouched` rc 0;
  EXE установки `171319bbc170cf6e57c8d36a8ae5f50d` (1191936 B), md5-diff установок = 0.
- `[P]` (2026-11-19) Оба дрейфа закрыты: `--quick` rc 0, нарушений 0; `hd.cfg` = `3840 2160 32 1`.
- `[P]` (2026-11-18) Из установки удалены `lethaldreams.dll` (неиспользуемый
  StarForce/SFORCE-стаб; копия в `backup_orig/` и в `original.7z`) и `LD-setup.bat`
  (копия только в snapshot `6.7z`). Установка 5095 файлов; `lethaldreams.dll` в
  `absent_ok`, строка `ld-setup.bat` из allowlist'а снята. Ярусы — новых нарушений нет.
- `[P]` Цепь EXE воспроизведена: pristine `7943036fbd1233a0dbd43228f129cfc7` → exepatch3
  `deed9cf640de1fe31ecec6deacad21bd` → +2 байта `6A 00`→`6A 01` на fileoff 632177..632178
  = baseline `45e93d46aad6ceb3dfc17e86d04e4f82` (`backup/LETHALDREAMS.EXE.pre4`, побайтно)
  → `exepatch12.py` без флага (запись только в `scratch_dir('exe')/LETHALDREAMS.EXE.patched`)
  = `171319bbc170…` = установленный EXE. Блобы пещеры (`paths.cache_dir('cave')`,
  сборка `sh tools/exe/build_cave.sh <name>`): `fixall.bin` 236 B, `fontscale.bin` 192 B,
  `fontscale4x.bin` 224 B.
- `[P]` `tools/fonts/restore_fonts.py` гейтирован: без `--install` печатает только
  CHECK-строки (md5 шаблона + план записи/удаления сайдкаров), установка не трогается
  (проверено: md5-diff установки = 0 после check-прогона); запись — только с `--install`
  (список гейтинга — `AGENTS.md`, описание — `docs/TOOLS.md`).
- `[P]` Импортные таблицы PE измерены: `LETHALDREAMS.EXE` — 210 импортов
  (`KERNEL32` 102, `python22.dll` 53, `USER32` 48, `ole32` 3, `GDI32` 2, `WINMM` 1,
  `ADVAPI32` 1, `DSOUND`); все 53 символа, импортируемые из `python22.dll`, есть среди
  753 экспортов DLL. Импорты `MSVCRT` (162), `KERNEL32` (39), `ADVAPI32` (6), `USER32` (1),
  `SHELL32` (1) принадлежат `python22.dll`, не EXE. PE TimeDateStamp `python22.dll` =
  1054332733 = 2003-05-30 22:12 UTC; строка `2.2.3` в DLL есть, строки `30.05.2003` нет.
- `[P]` Нейтрализация личных путей архива (104 файла): скан личных путей по отслеживаемым файлам = 0;
  py2-only — `tools/pyc/py22_worker.py` (core) и 8 архивных (список `tools/archive/README.md`);
  состав SNAPSHOT_ONLY в `docs/STATE.md`: `data/scripts/*.pyc` = 10 (5 в корне + 5 в `ui/`).
