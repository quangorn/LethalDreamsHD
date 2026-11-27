# STATE.md — состояние проекта (актуальное)

## Установленная игра (эталон проверки)

- `game.root` (из config.toml): **5096 файлов** (`lethaldreams.dll` и `LD-setup.bat` удалены из
  установки по решению пользователя; `LD-setup.bat` только в snapshot `6.7z`, в `original.7z`
  его нет — `docs/KNOWLEDGE.md`).
- EXE md5 `171319bbc170cf6e57c8d36a8ae5f50d`, размер 1191936 — базовая пропатченная версия
 (pristine-EXE того же размера, md5 `7943036fbd12…`). Маппинг: fileoff = VA − 0x400000
 (cave VA 0x4ebf12 = fileoff 966418; 491308 — fileoff первой правки exepatch3).
- `ui/cursor.pyc` = published base-печать `60754a253515…` (drift v6 закрыт 19.11.2026
  установкой `release/patches/ui_cursor.patch`; копия v6 `4a8eb4bab413…` — в
  `scratch_dir('battle_baseline')/cursor_installed_v6.pyc`). `check_state --quick` rc 0.
  Разбор и команда применения: `docs/VERIFY.md`.
- `ldhook.pyd` в установке + `ui/ui.pyc` с блоком загрузчика (`31dd9064…` против эталона
  `ae59f601…`) — dev-состояние для живых прогонов боевого AI; ставится
  `python3 tools/ldhook/patch_ui.py --install`, откат `--restore` (после отката rc 0).
  Грабли: `docs/PITFALLS.md`.
- Реестр:
  `HKCU\SOFTWARE\Microsoft\Windows NT\CurrentVersion\App Compat Flags\LETHALDREAMS.EXE` =
  строковый `~ HIGHDPIAWARE` (флаг совместимости, отключает системное масштабирование exe).

## Цепочка бинарных патчей exe

1. pristine `LETHALDREAMS.EXE` md5 `7943036fbd1233a0dbd43228f129cfc7` (5.0,
   из приватного архива).
2. `tools/exe/exepatch3.py` — правки под high-dpi (в т.ч. правки кода
   PE-загрузчика, entry-указателей) — зафиксирован в истории сессий.
3. +2 байта (патч 7int: offset 632177 `6A 00`→`6A 01`) → **baseline**
   `45e93d46aad6ceb3dfc17e86d04e4f82`.
4. `tools/exe/exepatch12.py` — cave-патч шрифтов (236 байт кода в пещере):
   cave md5 `bf3fc99b796c9d42629db62e37829aba` 236 B; три sites вызова (VA, fileoff = VA −
   0x400000): 0x41ce31 (патч 5 байт, сигнатура 5), 0x40b494 (патч 6 байт = jmp+nop, сигнатура 6),
   0x47dd80 (патч 5 байт, сигнатура 5); cave-код `paths.cache_dir('cave')/fixall.bin`
 (символьная ре-матрица шрифта), `paths.cache_dir('cave')/fontscale.bin` (собираемый артефакт) 192 B md5
   `546fd954343f87623cbd37a264622db8` (масштаб 1,25×),
   `paths.cache_dir('cave')/fontscale4x.bin` (собираемый артефакт) 224 B md5 `f320fcb6e1a39e9fe0c729d716c923f4` (масштаб 4×).
   `.bin/.o` — собираемые артефакты, в git не входят. Исходники: `tools/exe/caves/*.s`,
   сборка `sh tools/exe/build_cave.sh`.
5. результат = установленный exe `171319bbc170…`.
6. `python3 tools/exe/apply_baseline.py` — идемпотентная проверка: baseline
   `45e93d46…` + cave-блоки; при `--install` пишет baseline в игру
   (`backup/LETHALDREAMS.EXE.pre4`), `restore` возвращает его.

## Кодовый ярус (12 pyc)

Манифест `release/manifest.json`. Для каждого модуля: md5 base-печати
(header=build-time, source mtime — указанный), md5 установленной копии,
`pyc_source_mtime`, путь `.py`, патч. Ключевые константы:

| модуль | pyc_source_mtime |
|---|---|
| data/scripts/gamescript | 1794778202 |
| ui/ui | 1794778202 |
| ui/cursor | 1794778645 |
| ui/olmenu | 1794778203 |
| ui/battlemenu | 1794778202 |
| ui/btutorial | 1794778203 |
| ui/olcamera | 1794778203 |
| battlecamera | 1794778203 |
| bdomain | 1794778203 |

- Новые модули лежат в `config[game.root]/data/scripts/`: `types.pyc` (3225 B,
  md5 `414bd2aa0857…`; allowlist `named_legals` + `snapshot_only`) и `hd_res.pyc`
 (2497 B, md5 `fc524b6139c0…`; manifest `state` = `GENERATED`, собирается пайплайном
  из `hd.cfg`). В `data/scripts/ui/` их нет. Патч к ним не применяется.
- Патчи: `release/patches/*.patch` (`tools/apply_patch.py`).
- `[P]` Новый кодовый модуль (17.11.2026): `data/scripts/ui/olcamera.pyc` — base-печать md5
  `f67ae9904032…`, pristine-копия установки `ba346c67b543…` = `installed_recorded_md5`;
  патч (краевой скролл оверленда) `release/patches/ui_olcamera.patch` — `docs/PATCHES.md`; установлен, state `MATCH`.
- `[P]` Второй модуль той же правки: `data/scripts/battlecamera.pyc` — pristine-копия
  `9efdb4d1dae9…` = `installed_recorded_md5`; патч (краевой скролл боевой камеры)
  `release/patches/battlecamera.patch` — тот же раздел `docs/PATCHES.md`; установлен, state `MATCH`.
- `[P]` Текущая сборка `battlecamera` (18.11.2026): MMB-обзор + зум колесом шагом
  `zoom_step = 1.0` (`h_MouseWheel`, `patches/battlecamera.py`); установленный md5 = base-печать
  `c7d3150409af…`, state `MATCH`; разбор — `docs/PATCHES.md` («Боевая камера: MMB-обзор и зум колесом», «Ослабление зума колесом»).
- `[P]` Спавн отряда при входе в бой фиксирован (20.11.2026): индекс входа `FIXED_ENTRANCE_INDEX = 5`
  (`InitializeData`, `patches/bdomain.py`), случайного выбора нет; установленный md5 = base-печать
  `957bc9d86ccc…`, state `MATCH`; патч — `docs/PATCHES.md` («Точка входа отряда в бой»).
- Дрейфов кодового яруса нет: `ui/cursor.pyc` приведён к base `60754a25…` (19.11.2026);
  `ui/olmenu.pyc` (md5 `af176d4859a1…`, state `MATCH`) — каноническая сборка с фиксом Esc×2:
  шестой элемент `ShowMainmenu` = `2` (ResumeGame) в `patches/ui/olmenu.py`.

## Ярус ассетов

- 1342 .btf в установке (список зафиксирован в `release/manifest.json` assets_tier);
  из них 12 шрифтов HD Pack (побитово) и 1379 hd_pack-записей побитово;
  black-пары — только snapshot-архив, в install не входят.
- Шрифты: `data/fonts/` из `tools/fonts/gen_font4x.py` (изображения-эскизы
  приватны, не в git).
- HD Pack ассеты (1379, из них 30 `.map` с 3x-почвой — `docs/OBSERVATIONS_TEXTURE.md`,
  «Почвенные текстуры карт») и pristine-ассеты (2966) — неприкосновенны.
- Пользовательский бэкап (728 ALLOWED + allowlist-правила) — неприкосновенен.

## Python-слой (обязательная часть базы)

- `python22.dll` = финальный Python 2.2.3, 847920 B, md5 `4ee79da1533f8ac2957fd46c6ba1cb2d`
  (сборка 30.05.2003); оригинал — альфа 2.2a4+ (`backup_orig/python22_2.2a4.dll`).
- `Lib/` = stdlib 2.2.3: **713 файлов** (694 `.py` + 19 `.pyc`); в `archive.pristine` `Lib` нет.
- Корневые `.pyc` = **11** компилятов stdlib, собранных движком из корневых `.py`-копий
  (`.py` удалены). Состав: `__future__`, `copy`, `linecache`, `ntpath`, `os`, `site`, `stat`,
  `string`, `traceback`, `types`, `whrandom`.
- Эксперимент 16.11.2026 (пользователь): альфа DLL + пустой `Lib` + без корневых `.pyc` →
  игра не запускается; слой возвращён, заархивирован в `python_libs.7z` (725 файлов,
  crc32-сверка с установкой — 0 расхождений).
- Покрытие: SNAPSHOT_ONLY (`release/allowlist.json`: 713 `lib/*` + 11 корневых) → `--untouched` rc 0.
- Разбор происхождения и header-mtime — `docs/OBSERVATIONS_ENGINE.md` (раздел «Python-слой»),
  грабли — `docs/PITFALLS.md`.

## Замеры проверки (см. docs/VERIFY.md)

- `[P]` Замер 22.11.2026 (`--untouched`/`--full` rc 0, установка 5096 файлов): PRISTINE 2966,
  HD_PACK 1379, ALLOWED 728, CODE 18, EXCLUDED 4, ARCHIVE_BASENAME 1 (`lethaldreams.dll`), UNCOVERED 0.
- Покрытие allowlist'а (`release/allowlist.json`: `lib/*` 713, корневые `.pyc` 11,
  корневые бинары 5 — `LETHALDREAMS.EXE`, `python22.dll`, `msvcr70.dll`, `msvcp70.dll`,
  `msvcr70d.dll`, `data/scripts/*.pyc` 10, `data/textures/mm_bkg01.btf` 1);
  `hd.cfg` классифицируется как REPO — артефакт пайплайна, в git не входит.

---

Ниже — сжатые записи сессий по датам; актуальная цепочка exe и константы — разделы выше,
хроника — `docs/archive/SESSION_LOG.md`.

## Шрифт, геометрия UI и мира (состояние)

- `[P]` Шрифт в 16:9 уменьшается при отрисовке патчем EXE (`exepatch12`), листы шрифтов —
  4x-сайдкары `*_169` + родные `*_43` (`tools/fonts/gen_font4x.py`); межстрочный шаг в Python-слое:
  `HD_FNT_PATCHED = 1`, `RowS_new = round(k*RowS)` (`patches/hd_scale.py`). Остаток по ширине — коэффициент `A`, разбор `docs/archive/TODO_FONT_WIDTH.md`.
- `[P]` Правило сдвига к краю экрана обобщено: `hd_scale.HD_Shift`/`HD_EdgeFix`, вызывается из
  `patches/ui/olmenu.py` (панели оверленда) и `patches/ui/battlemenu.py` (левый BHUD-блок, колонка
  кнопок, консоль — влево; миникарта, `bhud_frame_r`, зона сброса заклинаний — вправо).
- `[P]` Геометрия мира: aspect зашит в матрицу проекции, правка `matrix[0] = matrix[5]*h/w` с
  guard'ом `4h==3w` (site3 `0x47dd80`) — `docs/OBSERVATIONS_ENGINE.md`.

## Публикация (2026-11-16)

- `[P]` Репозиторий опубликован: `https://github.com/quangorn/LethalDreamsHD` (public), ветка `master`;
  publish-цепочка `85ec7b9d…` → `45ccb09`; 272 файла, игровых бинаров 0, личных путей 0, `config.toml`
  не отслеживается; локальная история (`local-history`) удалена по просьбе пользователя.

## Интерпретатор 2.2.3 (2026-11-17)

- `[P]` Архивы игровых поставок интерпретатор **не содержат** (exe/dll 0 во всех листингах);
  дерево собрано из официальных архивов python.org в `vendor/py223/` (untracked, `.gitignore`):
  `python22.dll` побитово = игровой (`4ee79da1533f…`), `imp.get_magic()` = `[45, 237, 13, 10]`;
  состав и замеры импортов — `docs/TOOLS.md`, «Дерево интерпретатора».
- `[P]` Грабля плоского дампа установщика (регистр имени модуля, `case_ok`) —
  `docs/PITFALLS.md`, раздел «Импорт 2.2.3 чувствителен к регистру имени модуля».
- `[P]` Публикационное ядро: tracked-файлов 272, личных путей 0, игровых бинаров/`.pyc` в git 0,
  `vendor/**` не отслеживается; AST core `.py` = 51 (py2-only 1 — `tools/pyc/py22_worker.py`),
  AST `tools/archive/**` = 105 (py2-only 8, список — `tools/archive/README.md`).
## Зависание ввода после Esc×2 из оверленда (18.11.2026)

- `[P]` Фикс: шестой элемент `ShowMainmenu` = `2` (ResumeGame) в `patches/ui/olmenu.py`; каноническая
  сборка `ui/olmenu.pyc` md5 `af176d4859a1…` — `docs/PATCHES.md`. Причина: в оверленде `BattlemenuScene is None`,
  пункт ResumeBattle (`3`) вёл к `KeyError` в `cursor.Set` и мёртвому вводу; при новой игре не воспроизводится.

## Почва карт: HD-комплект (17–18.11.2026)

- `[P]` Почва в установке и в `config[hd_pack]` = **High Fidelity 3x**: 44 записи `.tex` ×3 (1024→3072 … 32→96),
  max blob 28 311 552 B; атлас = сетка блоков 32×32, блок = `<stem>__tx<col>_ty<row>.png`. Родная почва — из `original.7z` (pristine).
- `[P]` Ограничение движка: blob ≤ 32 MiB, NPOT допустим (3072 подтверждён грузящим); записи с изменённым w/h
  грузятся только при исправленных TLV-заголовках и `rec_bin` без `name_len`-префикса — `docs/KNOWLEDGE.md`,
  `docs/OBSERVATIONS_TEXTURE.md`, `docs/PITFALLS.md`; пайплайн и инструменты — `docs/PIPELINE.md`, `docs/TOOLS.md`.

## ldhook `.pyd` (18–22.11.2026)

- `[P]` **EXE установки не трогаем**: установка остаётся на cave `exepatch12` (`171319bb…`), `ldhook.pyd` в поставку
  не входит; `.pyd` (хуки site1/site2/site3, site5/site6 + логгер `hdlog.txt`) — проверенный путь следующих правок движка,
  dev-прогоны на baseline scratch. Критерии перехода — `docs/PLAN_proxy_dll.md`, состояние и вход — `docs/archive/HANDOFF_ldhook.md`.
- `[P]` Правило: после любой правки EXE — `python3 tools/ldhook/check_sites.py` (скан `.text` по `VirtualSize`).
  Грабли расширений 2.2.3 (`Py_InitModule(name, NULL)` падает) и привязка AppCompat-флага к пути EXE — `docs/PITFALLS.md`.
- `[P]` Site4 (cap итераций) и cap-API (`set_cap`/`cap_stats`/`diag_stats`, env `LDHOOK_CAP`) удалены
  (п. 3.4 плана); site5 — носитель точек H4/H5/H6 (`battle/trace.cpp`). Разбор капа — `docs/PERF_BATTLE.md`.
- `[P]` Эталон `.pyd` = артефакт `python3 tools/ldhook/build.py` (`build/ldhook/ldhook.pyd`, gitignored);
  эталоны харнесса = `build/ldhook/battle_replay.exe`, `build/ldhook/bdd0_harness.exe`,
  `build/ldhook/bdd0_synth.exe` (сборка — `tools/probes/*_build.py`). md5 не фиксируется.
- `[P]` Scratch-дерево для живых прогонов = копия установки + baseline EXE + свежий `.pyd` + загрузчик
  `data/scripts/ui/ui.pyc`; источник `spike.py` (`patches/ui/ui.py`) на диске нет, загрузчик берётся из
  сохранённого дерева (`ldhook_prev/tree/data/scripts/ui/ui.pyc`, md5 `cbd4ce2c…`).
- `[P]` Контрольный прогон боя — обёртки `tools/probes/battle_169.ps1` (16:9) и `battle_43.ps1` (4:3) над раннером
  `perform_action_in_game.ps1` (20.11.2026): `MenuSec=6`, `-Waves 3`, снимок `shot:summons`; Enter×2 при загрузке
  сейва, Enter на «Атака?», один `key:32` сразу после входа в бой (снимает стартовую паузу боя). Грабля — `docs/PITFALLS.md`.
  Принято (27.11.2026): `-HoldMs 6000` постоянный дефолт (каст 5 с из-за лага иногда даёт 1 саммон вместо 2;
  гейт ≥2 саммона за волну, снимок это подтверждает), `-AfterWavesWait SEC` (дефолт 1 = прежний гейт);
  стресс «много монстров» — `-Waves 8 -AfterWavesWait 20` (враг успевает призвать: в трассе 35 юнитов).
- `[P]` Порт боя: Фазы 0–5 закрыты (19–22.11.2026); гейты — офлайн per-call replay: Ф3 capture v8
  (`battle_replay_diff.py`), Ф4 capture v9 + replay `eee0` (решение 4.8), Ф5 capture v12 + `bdd0_harness`
  (решения 5.1c/5.1d); живой A/B — санитарный.
- `[P]` Остаток Фазы 3 (решение 3.8): на части вызовов порт уходит в runaway цикла `e0a0`; листья
  расходятся с первого такого вызова; открытый дефект, перенесён в остаток Фазы 4 (решение 4.8).
- `[P]` Фаза 4 закрыта: `45eee0` = `s6_unit_ai_tick` (тик юнит-AI, не «движение»), детур `install_tick_hook`
  (сиг `83 C4 34 C2 10 00`, продолжение `0x45eee5`), trace — 29 полей 1.7 — `battle/move.cpp`, `battle/trace.cpp`.
- `[P]` Фаза 5 закрыта (22.11.2026): тело `44bdd0` R1–R22 портировано (`battle/tick.cpp`, R23 нативен),
  детур на H1 (`0x44bdf5`) с возвратом в родной эпилог `0x44d074`; гейт — офлайн-replay на записанном
  состоянии (`bdd0_harness --impl port` rc 0), перепроверен на capture v13.
- `[P]` capture v13 (аддитивно, 23.11.2026): писатели через out-указатель (`42d9c0` → `ww`, ключи −1..−3),
  replay заполняет ими pos-тройку порта — `battle/capture.cpp`, `bdd0/native_replay.cpp`; гейт веток по id —
  `tools/probes/bdd0_coverage.py`.
- `[P]` capture v14 (24.11.2026): `band` (begin/end ленты, `unit+0x194/0x198`) в `bdd0_in`/`bdd0_res`,
  replay применяет его вместо реконструкции из `45af50` — `leaf_value` сравнивает заявки порта.
  Слой e0a0 отключается `LDHOOK_BATTLE_CAPTURE_E0=0`; запись буферизована 1 МиБ.
- `[P]` capture v16 (аддитивно, 25.11.2026): `bdd0_in "band_pos"` (ok/x/y + scale-триплет sa/sl/sm на элемент)
  и grid-запись `kind:"grid"` — `battle/capture.cpp`; загрузчик/арена (`bdd0/scenario.cpp`/`arena.cpp`) материализуют
  LDPos/GridLevel и биндят указатели; гейты `bdd0_band_geometry_modelled()`/`bdd0_grid_level_modelled()` data-driven, порты 45b150/45b3e0/458050 claim'ят.
- `[P]` capture v16 (25.11.2026): `bdd0_leaf_pre`/`bdd0_leaf_post` только для листа `0x45e1d0` (guard по va) —
  аддитивные ключи `pf`, `rec`/`rec_b64`, `sub`/`sub_b64`, `ctx`/`ctx120`/`ctx134`, `bandrec`/`bandrec_b64` (сбой blob → null, указатели
  всегда) и `maps`{`m1`,`m2`} (обе RB-карты PathFinder, головы +0x6c/+0x78, узлы `a`/`n`/`ps`, кап `CAP_BDD0_MAP_MAX`/карту) — `battle/capture.cpp` `cap_write_leaf_state`.
- `[P]` capture v17 (26.11.2026): 20 внутренних сайтов листа `0x45e1d0` (номера 35..54,
  `battle/leaf_sites.cpp`) в поток `bdd0_leaf` + аддитивный `kind:"bdd0_site_pf"` на сайте `0x45e320`
  (`cap_leaf_sink_exit`); ABI-гейт сайтов: caller-clean cdecl только `0x4cfa11` + байты `83 C4 04` — `battle/capture.cpp`.
- `[P]` capture v18 (аддитивно, 26.11.2026): вид листа `LS_K_INT_W2_ARG1` (пара float `456b20` по out-arg1
  в `w[0..1]`) на сайтах `0x45e297`/`0x45e2ab`/`0x45e49b`/`0x45e4b1` и `kind:"bdd0_cand"` (PathState 0x58
  на выходе `45ce80`) на сайтах `0x45e443`/`0x45e487` — `battle/leaf_sites.{h,cpp}`, `battle/capture.cpp`.
- `[P]` Фаза 5.2 (24.11.2026): портированы `45c370`/`45af50`/`45c6f0`(`b0_read_pos_from_ctx`)/`403e80`
  (`b0_pos3_equal`; в захватах всегда `ret=0`, истинная ветвь — только синтетика)/`456b60`/`457e20`/
  `458050` — `battle/grid.cpp`, сайты 33/34 (`0x458142`/`0x45811a`), `grid.cpp` в харнессе.
- `[P]` Индексы `458050` — от arg1 и arg3 (arg2 — только `min_metric − arg2`; `docs/CONTRADICTIONS.md` п. 20).
- `[P]` Включение порта `45e1d0` (26.11.2026): гейт `bdd0_path_state_modelled()` требует v18 (pre/post/site_pf +
  окно nested, `bdd0_cand` на каждом скане, `w[0..1]` на всех четырёх `456b20`); иначе — реплей записанного
  натива (`path_recompute_replayed`). Тело — `battle/recompute.cpp`, граница `path_boundary(site, va, ECX, args)`;
  сверки — `path_post_match` (pf/карты) и `path_boundary_args` (приёмник/аргументы против записи), `bdd0/checks_builtin.cpp`.
- `[P]` Фаза 6 закрыта (26.11.2026): `docs/OBSERVATIONS_BATTLE_AI.md` — автомат тика R1..R23 и выходы,
  поиск цели/скан, поля A* и маски, метрика и выбор цели, граф вызовов, бюджет на тик, формат trace, открытые семантики.
- `[P]` Живой гейт пройден на установке (26.11.2026): `ldhook.pyd` + блок загрузчика в `ui/ui.pyc`,
  `battle_169.ps1` → 587 тиков, 386 вызовов `45e1d0`, на снимке ≥4 саммона; запись `cap_live_gate.jsonl`
  реплеится офлайн без расхождений (`path_post_match`/`path_boundary_args` 386/386, `fail=0`).
- `[P]` Поставка (26.11.2026): `check_sites` installable, `smoke_api` OK, `probe_exitcode` — без событий
  Application/WER; два прогона A/B расходятся только адресами и временем (`docs/PITFALLS.md`, 22.11.2026).
  `patch_ui.py --install` (27.11.2026) проверяет и восстанавливает AppCompat-флаг `~ HIGHDPIAWARE`
  для EXE установки (значение дописывается к существующим флагам, имя — Windows-путь с `\`).
- `[P]` Фазы 8–9 (26.11.2026): 8.3 — 36 именованных слотов кадра `FR_*` в `battle/tick.cpp`; 9.d1/9.d2/9.m1 —
  `tools/probes/battle_invariants.py` (инварианты, `latency`/`spin`, `--profile`, `--baseline`) и
  `tools/probes/battle_ab_runs.py` (перемежаемые A/B по живому сценарию); `check_state --quick` rc 0.
- `[P]` Лаг (27.11.2026): порт и натив считают одинаково (`CONTRADICTIONS` №26), причина — квадратичный
  пере-скан `map1` в оригинале (`map1` 1→4900 за вызов). `E0COST`: скан 59.7 %, путь `45cb40` 31.7 %,
  `45dd70` 8 %, `b2=0`, живых ≈380; `e0a0` ≈94 % времени боя; потолок одной кучи 70→28–30 ms.
- `[P]` Гейты порта (26.11.2026): `LDHOOK_SITE6` (e0a0) и `LDHOOK_SITE6_EEE0` (тик `45eee0`) независимы,
  оба независимы; с 9.3 дефолты 1 и 0 (см. ниже); краш-логгер `LDHOOK_CRASHLOG` (VEH, стадии, глубина) — no-op без env.
- `[P]` 9.1a (27.11.2026): куча `(unsigned metric, LDPos*)` в `s6_target_search` (`LDHOOK_E0_HEAP=off|shadow|on`,
  по умолчанию off; членство — только в массиве кучи, поля `PathState` не пишутся). Тень: 193 377 сравнений,
  0 расхождений. Латентность не изменилась: цикл ограничен бюджетом `ctx[0x14]` (~70 ms), итераций за бюджет 4.2k→11k.
- `[P]` 9.1c (27.11.2026): `LDHOOK_E0_MAXITER` — потолок итераций `e0a0` (тот же `code=3`, до `45c5a0`).
  Принято 2000: max тик 11.1 ms, >15 ms — 0, `code=1` 93.0 %, `code=3` 2.6 %; без капа 60 тиков >20 ms.
- `[P]` 9.1b принято (27.11.2026, 9+9 прогонов): куча + потолок 2000 — поведение по умолчанию
  (`LDHOOK_E0_HEAP=off`, `LDHOOK_E0_MAXITER=0` — откат). Медиана µs/тик 1161 против 2916 натива,
  `latency`/1000 0.8 против 31.0, тиков >20 ms 5 против 279; `code=1` не ниже; guard 0/18.
  Остаток — первый тик (181 ms, `i1=0`), цель 9.2.
- `[P]` 9.1d принято (27.11.2026): `LDHOOK_E0_MAXUS=3000` — потолок по времени вызова (QPC каждые
  32 итерации, тот же `code=3`). Стресс «35 юнитов»: p90 тика 5705 → 3221 µs, max 13.6 ms, тиков
  >20 ms 0/3, `code=1` 79.9 % против 68.3 %; `MAXUS=0` — откат. Плюс к `MAXITER=2000`.
- `[P]` 9.3 поставка (27.11.2026): порт `e0a0` включён по умолчанию — `s6_port_enabled()`
  читает `LDHOOK_SITE6` с дефолтом 1 (`=0` — натив), `LDHOOK_SITE6_EEE0` дефолт 0 (тик `45eee0`
  нативный). До этого игра шла с `install_site6: disabled (LDHOOK_SITE6 not set)` — замеры не игрались.
- `[P]` 9.3 принято (27.11.2026, реальный бой 165 с с портом): тиков 202/с, AI 18.6 % календарного
  времени, из них `e0a0` 88.7 %; тиков >20 ms 1 (103 ms, вне пути), >50 ms 1; лага нет.
  `code=1` 35.7 %, `code=3` 19.6 %, `code=4` 44.5 %; `max_capped_us` 8028 при бюджете 3000.
- `[P]` 9.3 инструмент (27.11.2026): `BATTLE` несёт `e0n`/`e0us` (доля поиска в тике, без
  QPC-накладных), `LDHOOK_BATTLE_TRACE=2` — `TICKSEC` раз в секунду + медленные тики; разбор —
  `tools/probes/tick_stats.py`, запуск ручного боя — `tools/probes/play_profiled.ps1`.
- `[P]` 9.2a принято (27.11.2026): path-слой = 80–90 % µs `e0a0` (window-цикл+гейты 53 %, `map2`
  `lower_bound` 23 %, grid-lookup 10 %, tree-walk 13 %); повторы `(cell,target)` 2.7–9 %, все внутри
  одного вызова. Разложение — `E0PATH`/`E0PATHSUM`/`E0TICK` (`LDHOOK_E0_DIAG=1`, `battle/path.cpp`).
- `[P]` Офлайн-гейт `458050`: `bdd0_grid_level_modelled()` (сетки в v12..v15 нет) — сводка
  `grid_anchor_replayed`, включение — `docs/PITFALLS.md`. Out-writes сверяет `B0_LEAF_CLAIM_W` (`ported.h`).
- `[P]` Офлайн-гейт `45b150`/`45b3e0` (`battle/tick.cpp`): порты `s6_band_interp_gate`/`s6_band_target_moved`
  (сайты `0x44c4cb`/`0x44c5e0` и `0x44c554`/`0x44c666`), гейт `bdd0_band_geometry_modelled()`,
  сводки `band_interp_gate_replayed`/`band_target_replayed` — сравнение требует v16; FX/FY занулены
  только у `45b150` (`docs/PITFALLS.md`).
- `[P]` Для capture без `band` длина реконструируется из записи `45af50`, claim'ы таких тиков → `resync`
  → SKIP (тавтологический PASS невозможен) — `battle/ported.h` (mutators), `bdd0/native_replay.cpp`
  (`band_resync_from_stream`). `w` листа `45c6f0` несравним (тройка `ctx` не в захвате) — capture v15.
- `[P]` Scope Фазы 5 (решение 5.1c): порт — слой проводки; state/string/GUI site нативны намеренно,
  из 12 листьев нативны 3 — `44bbf0`, `45c730`, `45e1d0` (с v18 — условный порт, см. выше); таблица сайтов `bdd0_native` —
  `tools/probes/gen_native_sites.py` → `battle/native_sites_table.inc`, стубы — `battle/native_sites.cpp`.
- `[P]` Проверки v12 (`tools/probes/bdd0/checks_builtin.cpp`): `res_match` (al/out/u170), `state_match` (m0..m2),
  `native_args` (ecx/args против записанного порядка нативов), post-state `45eee0` (`ww` = BattleAI+0x00..0x38);
  корпус `tools/probes/bdd0/corpus/` + `bdd0_corpus_run.py`.
- `[P]` Пропуски проверок по классу (SKIP, не pass): uninit-аргументы (`0x44c71b`), noncomparable by-value
  аргументы `446a80` (`0x44bfa7/0x44c109/0x44c9d5/0x44cee1`), unbound-trap, оборванный хвост захвата
  (`incomplete_tail` — `bdd0/scenario.cpp`).
- `[P]` Равенство неразвязанных указателей измерено достоверно: сырые адреса игры привязываются к
  identity-cell (`arena.cpp`: `FX_IDCELL` для `gs+0x14`, пара `{0x44c3fa,0x45c730}` в `k_identity_rets`),
  база `gs_raw` — из потока `bdd0_native`, `NK_GS` в проверках раньше unit. Грабля — `docs/PITFALLS.md`.
- `[P]` Диагностика пути (не проверка): `--path-diff` — recorded/consumed order, первый DIVERGE, gate-контекст.
- `[P]` Гейт веток измеряется по id каталога (`tools/probes/bdd0_coverage.py`): метки, условия+направление,
  выходы, лист-сайты, RECORD-сайты; имена условий дублируются в каталоге, сверка по имени не годится.
- `[U]` Цель порта боя (19.11.2026): читаемая C++-реализация боевой логики и поиска пути
  (слои `44bdd0` → `45eee0` → `e0a0` → `dd70`), нативные вызовы для GUI/плумбинга; fidelity-first,
  CPU = baseline. Фазы, границы и гейты — `docs/PLAN_BATTLE_AI_CPP.md`.

## Прочие принятые правки сессий

- `[P]` Тултипы у курсора в 16:9: обёртка `ui/ui.CalcPopupPos` в `patches/hd_scale.py` (кламп к курсору,
  identity при `HD_K==1`); установлено `data/scripts/hd_scale.pyc` md5 `12d38f093d2c…` — `docs/PATCHES.md`, «Тултипы у курсора в 16:9».
- `[P]` Альфа-плоскость `.tex` задаёт отдельный альфа-проход движка: гладкая решётка эллипсов в дальней зоне создаёт
  она, opaque-проход даёт четырёхугольники тайлов; семантика по 12 bpp4-записям — `docs/OBSERVATIONS_TEXTURE.md`.
