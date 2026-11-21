# STATE.md — состояние проекта (актуальное)

## Установленная игра (эталон проверки)

- `game.root` (из config.toml): 5095 файлов (было 5097; `lethaldreams.dll` и `LD-setup.bat`
  удалены из установки 2026-11-18 по решению пользователя; `LD-setup.bat` есть только в snapshot
  `6.7z`, в `original.7z` его нет; см. `docs/KNOWLEDGE.md`).
- EXE md5 `171319bbc170cf6e57c8d36a8ae5f50d`, размер 1191936 — базовая пропатченная версия
 (pristine-EXE того же размера, md5 `7943036fbd12…`). Маппинг: fileoff = VA − 0x400000
 (cave VA 0x4ebf12 = fileoff 966418; 491308 — fileoff первой правки exepatch3).
- `ui/cursor.pyc` = published base-печать `60754a253515…` (drift v6 закрыт 19.11.2026
  установкой `release/patches/ui_cursor.patch`; копия v6 `4a8eb4bab413…` — в
  `scratch_dir('battle_baseline')/cursor_installed_v6.pyc`). `check_state --quick` rc 0.
  Разбор и команда применения: `docs/VERIFY.md`.
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
  «Почвенные текстуры карт») и pristine-ассеты (2967) — неприкосновенны.
- Пользовательский бэкап (729+12 allowlist) — неприкосновенен.

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

Замер 18.11.2026 (`check_state --untouched`, установка 5095 файлов; `lethaldreams.dll` и
`LD-setup.bat` удалены из установки по решению пользователя):
PRISTINE 2967, HD_PACK 1379, ALLOWED 728, CODE 17, EXCLUDED 4, UNCOVERED 0, rc 0.
Покрытие allowlist'а (`release/allowlist.json`: `lib/*` 713, корневые `.pyc` 11,
корневые бинары 5 — `LETHALDREAMS.EXE`, `python22.dll`, `msvcr70.dll`, `msvcp70.dll`,
`msvcr70d.dll`, `data/scripts/*.pyc` 10, `data/textures/mm_bkg01.btf` 1);
`hd.cfg` классифицируется как REPO. — артефакт пайплайна, в git не входит

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

## ldhook `.pyd` (18–21.11.2026)

- `[P]` **EXE установки не трогаем**: установка остаётся на cave `exepatch12` (`171319bb…`), `ldhook.pyd` в поставку
  не входит; `.pyd` (хуки site1/site2/site3, site5/site6 + логгер `hdlog.txt`) — проверенный путь следующих правок движка,
  dev-прогоны на baseline scratch. Критерии перехода — `docs/PLAN_proxy_dll.md`, состояние и вход — `docs/HANDOFF_ldhook.md`.
- `[P]` Правило: после любой правки EXE — `python3 tools/ldhook/check_sites.py` (скан `.text` по `VirtualSize`).
  Грабли расширений 2.2.3 (`Py_InitModule(name, NULL)` падает) и привязка AppCompat-флага к пути EXE — `docs/PITFALLS.md`.
- `[P]` Site4 (cap итераций) и cap-API (`set_cap`/`cap_stats`/`diag_stats`, env `LDHOOK_CAP`) удалены
  (п. 3.4 плана); site5 — носитель точек H4/H5/H6 (`battle/trace.cpp`). Разбор капа — `docs/PERF_BATTLE.md`.
- `[P]` Эталон `.pyd` = артефакт `python3 tools/ldhook/build.py` (`build/ldhook/ldhook.pyd`, gitignored);
  эталон харнесса = `build/ldhook/battle_replay.exe` от `tools/probes/battle_replay_build.py`. md5 не фиксируется.
- `[P]` Контрольный прогон боя — обёртки `tools/probes/battle_169.ps1` (16:9) и `battle_43.ps1` (4:3) над раннером
  `perform_action_in_game.ps1` (20.11.2026): `MenuSec=6`, `-Waves 3`, снимок `shot:summons`; Enter×2 при загрузке
  сейва, Enter на «Атака?», один `key:32` сразу после входа в бой (снимает стартовую паузу боя). Грабля — `docs/PITFALLS.md`.
- `[P]` Порт боя: Фазы 0–4 закрыты (19–21.11.2026); гейт Фазы 3 — офлайн per-call replay
  (`battle_replay_diff.py` на capture v8), Фазы 4 — capture v9 + replay `eee0` (решение 4.8),
  живой A/B — санитарный, решения 3.7/4.8 — `docs/PLAN_BATTLE_AI_CPP.md`.
- `[P]` Остаток Фазы 3 (решение 3.8): на части вызовов порт уходит в runaway цикла `e0a0`; листья
  расходятся с первого такого вызова; открытый дефект, перенесён в остаток Фазы 4 (решение 4.8).
- `[P]` Фаза 4 закрыта: `45eee0` = `s6_unit_ai_tick` (тик юнит-AI, не «движение»), детур
  `install_tick_hook`, capture v9 + офлайн-replay `eee0`; trace — фиксированные 29 полей 1.7
  (`code`/`out`, `c1/x1/y1`, `c2/x2/y2`, `trk/bd2`) — `battle/move.cpp`, `battle/trace.cpp`.
- `[U]` Цель порта боя (19.11.2026): читаемая C++-реализация боевой логики и поиска пути
  (слои `44bdd0` → `45eee0` → `e0a0` → `dd70`), нативные вызовы для GUI/плумбинга; fidelity-first,
  CPU = baseline. Фазы, границы и гейты — `docs/PLAN_BATTLE_AI_CPP.md`.

## Прочие принятые правки сессий

- `[P]` Тултипы у курсора в 16:9: обёртка `ui/ui.CalcPopupPos` в `patches/hd_scale.py` (кламп к курсору,
  identity при `HD_K==1`); установлено `data/scripts/hd_scale.pyc` md5 `12d38f093d2c…` — `docs/PATCHES.md`, «Тултипы у курсора в 16:9».
- `[P]` Альфа-плоскость `.tex` задаёт отдельный альфа-проход движка: гладкая решётка эллипсов в дальней зоне создаёт
  она, opaque-проход даёт четырёхугольники тайлов; семантика по 12 bpp4-записям — `docs/OBSERVATIONS_TEXTURE.md`.
