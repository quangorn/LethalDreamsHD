# PIPELINE.md — пайплайны инструментов (канонические)

## pyc-пайплайн: gate → parity → build → install

- **База**: `decompiled_fixed/<module>.py` — декомпилированные и исправленные
  источники игровых модулей (производные от bytecode, локальные, в git не входят;
  восстановление из игрового .pyc: `tools/pyc/decompile_lit.py` + правки).
- **gate** (`tools/pyc/hdpatch.py`): прогоняет патч-функции по базе, перезаписывает
  `build/<module>.py` (mtime меняется, контент стабилен = идемпотентность).
- **parity** (`tools/pyc/parity_file.py`): CRC-сверка bytecode базы vs `.py`
  (после gate/worker'а паритет остаётся валидным: источники кэша read-only).
- **build** (`tools/pyc/pycbuild.py`, worker `tools/pyc/py22_worker.py`):
  компиляция `.py` → `.pyc` интерпретатором **2.2.3** (`py22.interpreter`),
  magic `ed2d0d0a`; worker'овский header mtime = build-time (рецепт .pyc и
  его следствие — `docs/PATCHES.md`). PYTHONPATH worker'а =
  `py223.dir + cache/lib` (игровой `Lib` не входит; кэш наполняет
  `python3 tools/extract_pristine.py lib`).
- **install** (`tools/pyc/install_patch.py <модуль> --install`): перед записью
  сверяет md5 целевого файла игры, переписывает header `.pyc` на
  `pyc_source_mtime` манифеста (рецепт — `docs/PATCHES.md`, «Рецепт .pyc»),
  печатает сверку md5 с манифестом, пишет `.pyc` в игру, идемпотентно.

Каноническая форма всех команд: `python3 tools/<dir>/<script>.py` из корня
репозитория. Исторический список команд сессий `cmds.json` утрачен: покрытия нет ни в git,
ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z,
python_libs.7z; тот же мандат applies к истории сессий.

## cave-пайплайн (шрифты exe)

1. Исходники пещеры: `tools/exe/caves/{fixall,fontscale,fontscale4x}.s`
   (asm, gcc-i386; кодировки сверены с disex).
2. `sh tools/exe/build_cave.sh [fixall|fontscale|fontscale4x]` — сборка
   `.bin` в `paths.cache_dir('cave')` (артефакты, в git не входят; md5 зафиксированы
   в `docs/STATE.md`).
3. `python3 tools/exe/exepatch12.py` — наложение блоков на exe: cave-блок 236 B
 (VA 0x4ebf12, fileoff 966418 = VA − base), три sites вызова. Без флага пишет в
   `paths.scratch_dir('exe')/LETHALDREAMS.EXE.patched`; `--install` пишет в EXE
   установки; `restore` возвращает baseline (в тот же выходной файл).
 4. `python3 tools/exe/exepatch_frame1.py` — **эксперимент, откатён (16.11.2026)**:
    единственный байт fileoff 149932 (`push 0x80000000` → `push 0x80800000`,
    добавление `WS_BORDER`). `--install` / `--restore`; после отката EXE = эталон
    `171319bbc170…`. Итог — `docs/PITFALLS.md`, «Рамка `WS_BORDER` вместо `h+1».
 5. `python3 tools/exe/exepatch_video43.py` — **эксперимент, откатён (16.11.2026)**: сайт-jmp (VA `0x49a322`, вызов `IVideoWindow::SetWindowPosition`) + пещера `tools/exe/caves/video43.s` в zero-run `.rdata` (VA `0x50ec70`) + флаг исполняемости `.rdata`. Игра падает (`0xc0000005`, адрес вне образа) — путь закрыт, см. `docs/PITFALLS.md`, «Пещера в `.rdata` для 4:3 роликов». `--install`/`--restore`; после отката EXE = эталон `171319bbc170…`.


## ldhook-пайплайн (.pyd-хуки в памяти)

1. `python3 tools/ldhook/fetch_headers.py` — официальные заголовки Python 2.2.3
   в `vendor/py223/include/` (untracked; `*.LIB` уже в `vendor/py223/DLLs/`).
2. `python3 tools/ldhook/build.py` — MSVC x86 (`/fp:precise`) →
   `build/ldhook/ldhook.pyd` (gitignored). Смоук Python-API без игры:
   `LDHOOK_LOG=2 vendor/py223/python.exe tools/ldhook/smoke_api.py
   build/ldhook/ldhook.pyd`.
3. `python3 tools/ldhook/spike.py --baseline` — полная копия установки в
   `paths.scratch_dir('ldhook')/tree`, `ldhook.pyd` в корень, `ui/ui.pyc` =
   `patches/ui/ui.py` + блок `imp.load_dynamic`, EXE заменён на baseline
   `45e93d46…` (в установленном cave-EXE сигнатуры сайтов уже `E9`, скан их не
   найдёт). Без `--baseline` — дерево с установленным cave-EXE (эталон
   `exepatch12` для A/B; хуки не ставятся).
4. Прогон: `tools/probes/probe_exitcode.ps1` (16:9 → оверленд, site2/site3;
   4:3 → identity). Лог — `<scratch>/ldhook/tree/hdlog.txt` (общий с
   Python-патчами; зонд копирует его в `<Tag>_hdlog.txt`), уровень — env
   `LDHOOK_LOG` (`0` off, `1` info по умолчанию, `2` debug-трассировка).
5. Гейт 16:9: `site3_impl` пишет `matrix[0] = 0x3F7987FA` (эталон exepatch12);
   гейт 4:3: `guard 4h==3w -> identity` и `k==1.0 -> identity`, записей нет;
   гейт Фазы 3 — офлайн replay (п. 7, решение 3.7), живой A/B санитарный.
   EXE на диске не модифицируется (только `VirtualProtect` в памяти).
6. Аудит Фазы 4 (без игры, после любой правки EXE):
   `python3 tools/ldhook/check_sites.py` — уникальность трёх сигнатур в `.text`;
   `installable` (baseline) / `patched` (cave exepatch12) / `UNEXPECTED` (rc 1).
   Решение: установка остаётся на cave, `ldhook.pyd` — путь следующих правок.
7. Офлайн replay `e0a0` (capture env `LDHOOK_BATTLE_CAPTURE=<jsonl>`, прогон в scratch-дереве):
   - `python3 tools/ldhook/build.py`
   - `python3 tools/probes/battle_replay_build.py`
   - `build/ldhook/battle_replay.exe [--chained] [--leaf-replay|--real-path] <capture.jsonl> <res_port.jsonl>`
   - `python3 tools/probes/battle_replay_diff.py <capture.jsonl> <res_port.jsonl> [--fields=leaf]`
   - `python3 tools/probes/leaf_stream_diff.py <capture.jsonl> <res_port.jsonl>`
   - `python3 tools/probes/trace_diff.py <native.log> <port.log> --align=ordinal`
   - инварианты боя (9.d1): `python3 tools/probes/battle_invariants.py <hdlog.txt>
     [--baseline tools/probes/battle_invariants_baseline.json]` — rc 0 только без
     НОВЫХ провалов относительно нативного эталона; натив сам даёт rc 1
     (`runaway`/`stall` — измеренные дефекты).
   - разбор дампа краха без отладчика: `python3 tools/probes/minidump_stack.py <dump.dmp>`
     (WER `LocalDumps` → модули, исключение, контекст, скан стека «модуль+смещение»).

8. Захват v12 `bdd0_native` (не-листовые RECORD-нативы тела `44bdd0`):
   - `python3 tools/probes/gen_native_sites.py [--check|--report]` — таблица сайтов из EXE
     (`battle/native_sites_table.inc`) / список RECORD-NOOP_OK; `python3 tools/probes/verify_native_sites.py` — сверка байтов сайтов.
   - `python3 tools/probes/bdd0_harness_build.py`; `build/ldhook/bdd0_harness.exe --impl port <capture.jsonl>`.
   - self-test эмиттера стубов: `python3 tools/probes/leaf_sites_selftest_build.py`.
   - корпус: `python3 tools/probes/bdd0_corpus_run.py` (positive + dropped/trap/order/args/pointer, ожидаемые rc 0/1/1/1/1/0-с-SKIP).
   - исход: `--check res_match` (al/out/u170), `--check state_match` (m0..m2), `--check native_args` (ecx/args порта против записи).
   - диагностика пути (не проверка): `--path-diff [--path-diff-json out.jsonl] [--from-seq N --limit 1]` —
     side-by-side recorded/consumed order, первый DIVERGE и gate-контекст.
    - покрытие тела каталогом (гейт веток, по id): `python3 tools/probes/bdd0_coverage.py <capture.jsonl> [...]`
      — union меток/условий+направление/выходов/лист-сайтов/RECORD-сайтов и пробелы каталога.


## btf-пайплайн (курсоры/иконки)

`unpack_btf.py` (распаковка blob→PNG, canon top-down) → апскейл/перерисовка
(эскизы, приватные) → `pack_btf.py` (по шаблону родного .btf; массовые прогоны —
исторические скрипты сессий) → список установки в `release/manifest.json`
(assets_tier) → установка (`--install`). Чёрные сайдкары
(`black_btf.py`) — эксперимент, в установку не входят (флагов `.flag` в игре нет;
sidecar-черные — только в snapshot-архиве).
Чёрная точка: `qc_blackpoint.py fix --in <каталог .btf> --out <каталог>` вычитает bandmin
краевой полосы 2 px из аддитивных `.btf` (`BBlend_Add`), пишет `qc_report.txt`.

## шрифты-пайплайн

`gen_font4x.py`: родные пары `.fdf+.btf` (из архивов) → PNG 4x (Lanczos,
remacri) → `.btf` по шаблону + `.fdf` c ×4 (`x,y,adv,pitch`) → `--install`.
Регрессия k=1 (4:3) identity; коэффициенты и замеры:
`docs/OBSERVATIONS_FONT.md`, `docs/archive/TODO_FONT_WIDTH.md`.

## exe-цепочка

pristine `7943036fbd…` → exepatch3 → +2 байта (7int, fileoff 632177) =
baseline `45e93d46…` (`backup/LETHALDREAMS.EXE.pre4`) → exepatch12 (cave) =
установленный `171319bb…`. Проверка: `apply_baseline.py`.

## Тестовый контур

- Writes тестируются на scratch-дереве: полная копия установки
  (`LD_CONFIG` c другим `game.root`), порча файлов, checker-ожидания,
  снапшот md5+mtime установки до/после любой команды (diff обязан быть нулевым).
- checker read-only: он ничего не пишет; `--json` пишет только в
  cache/scratch.


## Recovery-проход `tools/pyc/decompile_lit.py` (литералы по значению, гейты A/B)

- `[P]` Скрипт `tools/pyc/decompile_lit.py` (новый, результаты только в `decompiled_fixed/`):
  recovery поверх декомпилятов `decompiled_snap/`. Литералы восстанавливаются **по
  значению**, индекс `co_consts` никогда не подставляется в текст. Цикл:
  `normalize` -> воркер `tools/pyc/py22_worker.py litfix2 <orig.pyc> <файл>` -> выравнивание
  кодовых объектов **по именам** -> замен-проходы -> гейт по финальному файлу.
- `[P]` Стратегии замен (применяются по очереди, проход принимается только если
  гейт строго уменьшился — иначе осцилляция позиционного выравнивания):
  1) позиционные пары токенов: B=целое K, A=V, `consts_orig[K]==V` -> K на
     `py2_literal(V)`; B=целое K, A=int V -> K на `repr(V)`; B=строка S, A=V ->
     строковый литерал со значением S на `py2_literal(V)`;
  2) unops для неспаренных токенов (правило индекса + порядное k-е сопоставление);
  3) multiset-delta: мультимножества LOAD_CONST кодового объекта — лишние значения B
     спариваются с недостающими (сначала одноимённые типы, затем кросс-тайповые),
     литерал заменяется по значению; регион поиска — строки statements объекта;
  4) greedy-polish: по одной замене за раз, принимаем только улучшающие гейт.
- `[P]` Гейт A = мультимножества значений LOAD_CONST по выровненной паре объектов
  (хвостовой `LOAD_CONST None` от RETURN_VALUE снимается с обеих сторон; терпимость
  к peephole-разнице `-x` vs `x`+`UNARY_NEGATIVE` — альфа-константное свёртывание
  против 2.2.3). Итог по скану `приватный архив сессий Project/1.7z, entry LethalDreams/.tmp/decompile_all_scan3.log` (лог скана; в git не входит):
  **434 модулей, суммарный gate=0, статусных проблем=0**, отчёт
  `decompiled_fixed/report.txt` (A=B по всем 433).
- `[P]` Структурно неполные декомпиляты (единственный пропуск):
  `dictionary_rus` — в `decompiled_snap/` только заголовок uncompyle6 (237 байт,
  тело не декомпиллируется), восстановлен 0 модулей; в отчёте помечен skip.
- `[P]` Пилот `ui/battlemenu`: A=124 B=124 (потерянных функций нет), gate=0,
  Gate B: `grep -c ld_uncial12` = 9, ровно 9 списков
  `['', 'ld_uncial12', FONT_H, 1, 0, 0]`, 9 байткодных окон
  `LOAD_CONST ''` / `'ld_uncial12'` / `LOAD_GLOBAL FONT_H` / `1` / `0` / `0` /
  `BUILD_LIST 6` / `BUILD_LIST 8`. Проверка: `python3 tools/pyc/decompile_lit.py --check-battlemenu`
  (сам собирает `.pyc` в `decompiled_fixed/`). Строковые константы A=303,
  строковых литералов B=303.
- `[P]` Исправленные баги декомпиллятов по пути: `parse_literal` не пропускал
  префиксы `r/u/b/f` (docstring `os.py` с CR), сборка текста однострочного литерала
  удваивала хвост строки, окно поиска было фиксировано в 40 строк.
- `[P]` Итог по порче из предыдущей записи: `ui/battlemenu.py` в `decompiled_fixed/`
  содержит **восстановленные** литералы BHUD по значению, например
  `ui.BActionElement([30, 100, 300, 300, ui.GUI_TNORMAL, '', 0, ['', 'ld_uncial12',
  FONT_H, 1, 0, 0]], [], [])` (строка 930) — вместо индексов `[7, 8, 3, 2, 2]` из
  испорченного декомпилята. Base пригоден для патча. Игровая копия не менялась:
  `config[game.root]/data/scripts/ui/battlemenu.pyc` md5 = оригиналу.


## База пайплайна: `decompiled_fixed/`



- `[P]` `pycbuild.SNAP` переставлен на `decompiled_fixed/` (строка 30; docstring и
  комментарии по build/gate обновлены). `tools/pyc/decompile_lit.py` (SNAP = pycbuild.SNAP) и
  `tools/pyc/make_cfg.py` (SRC/GS_SRC) переведены на `decompiled_fixed/`.
- `[P]` Гейт на новой базе: `python3 tools/pyc/pycbuild.py gate` -> **434 компилируются, 0 падают**.
  Паритет по новой базе показывает дельту my-build против УСТАНОВЛЕННОГО `.pyc`
  (файл установки) (cmd_parity
  сравнивает с game_pyc = установленным), т.е. числа вроде ui/ui 0.2968 = мои HD-правки,
  не дефект базы.
- `[P]` Удалены папки `decompiled/` и `decompiled_snap/` (остался `decompiled_manifest.json` (историческая ссылка; рабочая директория, в git не входит)
  — нужен `tools/archive/decompile/dd.py`). `tools/archive/decompile/pin_decompiled.py`, `tools/archive/decompile/decompile_all.py`, `tools/archive/decompile/decompile_fast.py` помечены
  предупреждением "не запускать" (они воссоздали бы базу).


## tex-пайплайн (почва карт: `.tex` из `.map` → PNG)

1. **Экстрактор**: `python3 tools/tex/scan_tex.py` — скан 30 `.map`
   (`LDPACKER`+zlib payload), записи `<map>_<N>.tex`, гейт (44 записи, имена уникальны,
   blob'ы не перекрываются); `--dump` — raw-blob'ы и `MANIFEST.txt` в
   `scratch_dir('ld')/tex_raw`.
2. **Раскод**: `python3 tools/tex/unpack_tex.py --batch data/maps <outdir>` — канон
   raw interleaved RGB (bpp 3) / RGBA (bpp 4), строки top-down (тот же канон, что
   `.btf`); `--probe` — все варианты (layout/flip) для отбора; `-o … --index N` — запись.
3. **QC**: `python3 tools/tex/qc_tex.py` — round-trip blob→PNG→blob побитово
   (**44/44 lossless**) + статистика uniq/gray по всем записям; `--tiles` — сверка
   палитры генератора (тайлы `data/wrender/figures/textures/*.bmp` × цвета `GROUPS`).
4. **Тестовый контур отображения** (scratch-дерево, полная копия установки):
   `tools/tex/patch_map_pattern.py --tree <дерево> --band top|bottom|…` кладёт тестовый
   паттерн (`tools/tex/test_pattern.py`) в blob'ы `.tex`; снимок оверленда
   `tools/archive/probes/probe_loadshot.ps1 … -Mode new` читает ориентацию
   (полоса у верхнего края записи → дальние/левые рёбра тайлов). Откат
   `--restore`. Хук-материалы (`tools/tex/hook_ground.py`) проверены как зонд:
   модули грузятся, подмена `.btf`-ресурса движком не проходит.
5. **Ярусы после всех прогонов**: `check_state.py --quick` rc 0, `--untouched` rc 0;
   снапшот md5+mtime установки — diff нулевой (запуски игры дают известный дрейф
   `Lib/*.pyc`, `hd.cfg`, `hdlog.txt`, font-сайкары, `mm_bkg01.btf`).
6. **Апскейл (грузится)**: `tools/tex/upscale_tex.py` (Lanczos-3, stdlib, `--factor N` —
   произвольный целый множитель; для 4K: 1024²-записи x2, записи <=512 x4) кладёт PNG в
   `scratch_dir('ld')/tex_up2x`; `patch_map_pattern.py --up2x` пересобирает записи с
   удвоенными w/h/blob_len (plen/clen правятся). Записи пересобираются БЕЗ лишнего
   `name_len`-префикса (`name_len` u32 живёт в gap'е перед именем), а дельта blob'а
   (+/−) прибавляется ко ВСЕМ TLV-размерам, накрывающим запись: внешний заголовок
   записи и цепочка заголовков payload'а (`chain_headers`, `fix_chain`). Иначе
   `FUN_0049bf70` (ADV) сбивает координаты потока, FACT (`FUN_00495720`) даёт NULL и
   движок падает (`0xC0000005` на `0x467f54`). Механизм и гейты —
   `docs/archive/ISSUE_tex_upscale.md` («РЕШЕНИЕ»), факты — `docs/KNOWLEDGE.md`.
   Произвольные стороны: `--rescale self --num N --den D --record <stem>` (целочисленные
   w,h = родные·num/den; `--self` — источником служит blob самой записи).
   Сигнатура отказа фиксируется `tools/probes/probe_exitcode.ps1` (ExitCode +
  fault offset из журнала Application/WER). Съёмка оверленда —
   `tools/archive/probes/probe_loadshot.ps1 … -Mode new`, съёмка боя —
   `tools/probes/probe_battleshot.ps1`.
7. **Потайловый контур (блоки 32x32)**: `unpack_tex.py --batch <maps> x --tiles <dir>
   [--skip-empty]` — блоки `<stem>__tx<col>_ty<row>.png` (имя = позиция в атласе,
   `tools/tex/tex_tiles.py`); ресемпл любого множителя — `upscale_tex.py --in <dir>
   --out <dir> --factor N` (имена сохраняются); сборка обратно —
   `patch_map_pattern.py --tree <дерево> --tiles <dir>` (сторона блока 32*f → запись
   f*w x f*h, `fix_chain`, round-trip) или PNG-атласы без дерева игры —
   `tex_tiles.py glue <dir> --maps <maps> -o <out>`. Гейты: f=1 собирает родные
   атласы побитово (44/44) и даёт payload, идентичный установочному; f=2 (блок 64)
   даёт 2048²-записи с цепочкой TLV до EOF. Пустые блоки атласа можно не писать
   (`--skip-empty`): при сборке они восстанавливаются как пустые слоты, а
   непустой потерянный блок — ошибка (`--allow-missing`). `--maxside N` пропускает
   записи, где f*w > N (блоки 4x для 1024² дают 4096² и 50 MB blob'а при лимите
   32 MiB) — такие записи остаются родными.
8. **Почва в HD-паке**: в поставку вошёл 3x-комплект High Fidelity (блоки 96x96,
  1024→3072 … 32→96, max blob 27 MB) — 30 `.map` лежат в `config[hd_pack]`
  (`hd_pack.7z`, 1379 записей) и в установке; `check_state --full`/`--untouched`
  rc 0, `hd_pack` сверяется побитово. Родные карты — в `original.7z` (pristine);
  пересборка/накат — `patch_map_pattern.py --tiles <blocks> [--install]`.

Формат записи, поля шапки, статистика, наблюдения отображения — `docs/OBSERVATIONS_TEXTURE.md`
(«Почвенные текстуры карт (`.tex` внутри `.map`)»), факты — `docs/KNOWLEDGE.md`
(«Почва карт: `.tex` в `.map`»), команды — `docs/TOOLS.md` («tex-пайплайн»).
