# STATE.md — состояние проекта (актуальное)

## Установленная игра (эталон проверки)

- `game.root` (из config.toml): 5097 файлов.
- EXE md5 `171319bbc170cf6e57c8d36a8ae5f50d`, размер 1191936 — базовая пропатченная версия
 (pristine-EXE того же размера, md5 `7943036fbd12…`). Маппинг: fileoff = VA − 0x400000
 (cave VA 0x4ebf12 = fileoff 966418; 491308 — fileoff первой правки exepatch3).
- Известный drift: `ui/cursor.pyc` в установке = патч курсоров версии 6
 (установленный md5 `4a8eb4bab413…`, размер 26396); published base-печать того же
  diff-патча — `60754a253515…` (`release/patches/ui_cursor.patch`).
  Число `644b54d8…`, встречавшееся в ранних заметках, ни в одном измерении
  не подтверждено (см.
  `docs/CONTRADICTIONS.md`, п. 5). Разбор и команда применения: `docs/VERIFY.md`.
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
 (символьная ре-матрица шрифта), `paths.cache_dir('cave')/fontscale.bin` (собираемый артефакт, в git не входит) 192 B md5
   `546fd954343f87623cbd37a264622db8` (масштаб 1,25×),
   `paths.cache_dir('cave')/fontscale4x.bin` (собираемый артефакт, в git не входит) 224 B md5 `f320fcb6e1a39e9fe0c729d716c923f4` (масштаб 4×).
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

- Новые модули лежат в `config[game.root]/data/scripts/`: `types.pyc` (3225 B,
  md5 `414bd2aa0857…`; allowlist `named_legals` + `snapshot_only`) и `hd_res.pyc`
 (2497 B, md5 `fc524b6139c0…`; manifest `state` = `GENERATED`, собирается пайплайном
  из `hd.cfg`). В `data/scripts/ui/` их нет. Патч к ним не применяется.
- Патчи: `release/patches/*.patch` (`tools/apply_patch.py`).
- `[P]` Новый кодовый модуль (17.11.2026): `data/scripts/ui/olcamera.pyc` —
  base-печать md5 `f67ae9904032…` (11910 B, crc32 `25112d1b`, `.py` md5
  `4bf8df324ee6…` 8671 B), pristine-копия установки md5 `ba346c67b543…` (10669 B)
  = manifest `installed_recorded_md5`; патч `release/patches/ui_olcamera.patch`
  (краевой скролл оверленда) — `docs/PATCHES.md`, «Краевой скролл оверленда».
  **Установлен в игру 17.11.2026** (`install_patch.py ui/olcamera --install` после
  подтверждения пользователя), installed md5 = base-печать `f67ae9904032…`, state `MATCH`.
- `[P]` Второй модуль той же правки (17.11.2026): `data/scripts/battlecamera.pyc` —
  base-печать md5 `ddb3e6fa629e…` (25320 B, crc32 `d3218ac8`, `.py` md5
  `63990fd1eeb4…` 19049 B), pristine-копия установки md5 `9efdb4d1dae9…` (23289 B)
  = manifest `installed_recorded_md5`; патч `release/patches/battlecamera.patch`
  (краевой скролл боевой камеры) — тот же раздел `docs/PATCHES.md`.
  **Установлен в игру 17.11.2026** (`install_patch.py battlecamera --install`),
  installed md5 = base-печать `ddb3e6fa629e…`, state `MATCH`. `[U]` Подтверждение
  в бою после установки патченной сборки в scratch-дереве: «всё работает».
- Известные дрейфы кодового яруса (manifest `state`):
  `ui/cursor.pyc` = `DRIFT_RELEASE_AHEAD` (в установке патч курсоров версии 6,
  md5 `4a8eb4bab413` vs base-печать `60754a253515`);
  `ui/olmenu.pyc` = `DRIFT_KNOWN` (установленный `3d96162df727` собран в
  приватном историческом дереве с личным co_filename-строковым путём;
  опубликованный зонд-лог персонализирован, base-печать опубликованного дерева =
  `5cb5cc88ddc8`). Installed-значения оставлены измеренными фактами.

## Ярус ассетов

- 1342 .btf в установке (список зафиксирован в `release/manifest.json` assets_tier);
  из них 12 шрифтов HD Pack (побитово) и 1349 hd_pack-записей побитово;
  black-пары — только snapshot-архив, в install не входят.
- Шрифты: `data/fonts/` из `tools/fonts/gen_font4x.py` (изображения-эскизы
  приватны, не в git).
- HD Pack 1.4.3 ассеты (1349) и pristine-ассеты (3005) — неприкосновенны.
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

PRISTINE 3005, HD_PACK 1349, SNAPSHOT_ONLY 741, REPO 1 (hd.cfg),
REPO_BASENAME 1 (console.py), UNCOVERED 0. Состав SNAPSHOT_ONLY (741): `lib/*` 713,
корневые `.pyc` 11, корневые бинары 6 (`LETHALDREAMS.EXE`, `python22.dll`,
`msvcr70.dll`, `msvcp70.dll`, `msvcr70d.dll`, `ld-setup.bat`), `data/scripts/*.pyc` 10
(5 в корне `data/scripts` + 5 в `data/scripts/ui/`) и `data/textures/mm_bkg01.btf` 1. — артефакт
пайплайна (`release/allowlist.json`), в git не входит

---

Срез зафиксирован дословно (даты сохранены). Он описывает состояние до exepatch12:
актуальная цепочка exe — раздел «Цепочка бинарных патчей exe» выше; патч-реализация — `docs/PATCHES.md`.


## ТЕКУЩЕЕ СОСТОЯНИЕ (2026-11-15, вечер) — читать первым

- `[P]` **Шрифт в 16:9 уменьшается при отрисовке патчем EXE; листы/таблицы шрифтов — РОДНЫЕ.**
  `config[game.root]/LETHALDREAMS.EXE` md5 `49976478943f0e2871872a2c6fb55f28` =
  baseline + `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py` (исторический скрипт сессии; в git не входит). Откат: `python3 приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py (исторический скрипт сессии; в git не входит) restore` (берёт
  `backup/LETHALDREAMS.EXE.pre4`, md5 `45e93d46aad6ceb3dfc17e86d04e4f82`).
  Построение пещеры: `sh tools/exe/build_cave.sh` (`tools/exe/caves/fontscale.s` (источник в git; собираемые блобы — в paths.cache_dir('cave')) → `paths.cache_dir('cave')/fontscale.bin` (собираемый артефакт, в git не входит)).
- `[P]` **«EXE нельзя патчить» — неверно, таких ограничений нет.** Статический патч `.text`
  (сайты `0x41ce31`, `0x40b494`, пещера в zero-run `0x4ebf12`) работает; проверок
  целостности/упаковки в EXE нет (см. раздел «SProtect опровергнута»). Краши ранних
  попыток (`exepatch6/7`) были ошибками самих пещер.
- `[P]` Python-слой: `patches/hd_scale.py` (ставится `python tools/pyc/install_patch.py hd_scale`),
  ключевое: `HD_FNT_PATCHED = 1` и `RowS_new = round(k*RowS)` — метрики шрифта уже
  k-сжаты движком, компенсация pitch не нужна.
- `[P]` **Листы шрифтов для 16:9 — 4x-сайдкары (2026-11-15, позже)**: `tools/archive/exe_patch/exepatch10.py`
  (md5 EXE `51f69974e1dada6e7b191961e853a1ef`) + `tools/fonts/gen_font4x.py`. Сборка пещеры:
  `tools/archive/cave_gen/build_cave4x.py` (источник в git; собираемые блобы — в paths.cache_dir('cave')) (`tools/exe/caves/fontscale4x.s` (источник в git; собираемые блобы — в paths.cache_dir('cave'))). Откат: `python tools/archive/exe_patch/exepatch10.py restore`.
  Ширина строки теперь совпадает с 4:3 (sd 108.53 против 108.22, было 110.32) —
  см. раздел «4x-листы шрифтов + коэффициент A».
- `[P]` Проверено: 4K — межстрочный шаг в круге персонажа = 16.0 канвасных (как 4:3),
  диалог настроек без лишних переносов; 4:3 — раскладка идентична эталону.
- `[P]` **Остаток по ширине закрыт**: в 16:9 ширина строк была больше 4:3 из-за
  целочисленного межбуквенного интервала (`RowS`); лечится коэффициентом `A` в пещере.
  Разбор: **`docs/archive/TODO_FONT_WIDTH.md`**.
- Состояние на диск: `hd.cfg` = `3840 2160 32 1`; шрифты = родные (`tools/fonts/restore_fonts.py`);
  игра остановлена. Снимки/логи: `scratch_dir('ld')/`.
- `[P]` **Панели оверленда придвинуты к краям экрана в 16:9 (2026-11-15, ночь)** — см. раздел
  «Панели оверленда: сдвиг к краям экрана». Установлен `patches/ui/olmenu.py`; снят первый
  контрольный прогон (загрузка сейва), 4:3 побитово прежняя. Визуальную проверку делает
  пользователь (4K fullscreen). Пользователь подтвердил: «всё ок».
- `[P]` **Live-подтверждён фикс геометрии мира в 16:9 (2026-11-15, вечер)** — см. раздел
  «Геометрия мира: aspect зашит в матрицу проекции». Формула live-патча: в матрице
  проекции `matrix[0] = matrix[5] * h/w` (w,h = ctx+0x18/+0x1c). Замер ratio кольца
  домена: baseline 1.4035 → patch5 **1.0575** (4:3 = 1.06); гексы ровные, мир заполняет
  окно, UI цел. Статический EXE-патч ещё НЕ сделан.
- `[P]` **Правило сдвига к краю экрана обобщено** (`hd_scale.HD_Shift`/`HD_EdgeFix`) и
  применено к боевому HUD (`patches/ui/battlemenu.py`: левый BHUD-блок + колонка кнопок
  + battle-консоль влево, миникарта/`bhud_frame_r`/зона сброса заклинаний вправо).
  Установлено: `hd_scale.pyc`, `ui/olmenu.pyc`, `ui/battlemenu.pyc`. Боевой HUD в 4K — компиляты `.pyc` в установке, в git не входит
  проверяет пользователь; baseline боя (до патча) ещё не снят.


Срез зафиксирован дословно; текущие константы установок — разделы выше. Последовательность
`exepatch9 → exepatch10 → exepatch12` зафиксирована в `docs/PATCHES.md`.


## Публикация (2026-11-16)

- `[P]` Репозиторий опубликован: `https://github.com/quangorn/LethalDreamsHD` (public), ветка
  `master`; чистый initial commit `85ec7b9d…` из дерева HEAD, поверх него publish-коммит
  `45ccb09` (видео-измерения и отказ от 4:3 video-window патча); 272 файла, игровых бинаров 0,
  личных путей 0, `config.toml` не отслеживается. Полная локальная история (39 коммитов, ветка-копия
  `local-history`) удалена по просьбе пользователя (2026-11-17): в репозитории только publish-цепочка.
  Хроника — `docs/archive/SESSION_LOG.md`.

## Интерпретатор 2.2.3 (2026-11-17)

- `[U]` Пользователь: temp-каталог для интерпретатора не годится, нужен untracked каталог
  внутри проекта; вопрос «из архивов игровых поставок его не взять?» закрыт замером.
- `[P]` Архивы игровых поставок интерпретатор **не содержат**: переписаны все листинги
  приватных архивов (`python_libs.7z` 727 записей, `original.7z` 4460, `hd_pack.7z` 1355) —
  exe/dll **0** (в `python_libs.7z` есть `python22.dll`, исполняемых файлов нет).
- `[P]` Интерпретатор взят из официального архива python.org и собран в
  `vendor/py223/` (untracked, `.gitignore`; состав и замеры импортов — `docs/TOOLS.md`,
  «Тулчейн», пункт «Дерево интерпретатора»): `python.exe` banner
  `Python 2.2.3 (#42, May 30 2003, 18:12:08) [MSC 32 bit (Intel)] on win32`,
  `python22.dll` побитово = игровой (`4ee79da1533f…`), startup-`import site` проходит,
  `imp.get_magic()` = `[45, 237, 13, 10]` (`2ded0d0a`).
- `[P]` После переноса `config[py223.dir]` в `vendor/py223/` ярусы чекера восстановлены
  дословно: `--quick` rc 1 (два известных дрейфа), `--full` rc 0, `--untouched` rc 0;
  установка 5097 файлов, md5-diff 0 после всех read-прогонов.
- `[P]` Компи-путь восстановлен: `python3 tools/pyc/pycbuild.py gate` → **434 компилируются,
  0 падают**; `python3 tools/pyc/install_patch.py hd_scale` (без `--install`) → `BUILD-OK`,
  magic `2ded0d0a`, size 23781 B, кодовая секция побитово равна установленному
  `data/scripts/hd_scale.pyc` (`dd4123fbc19c`); `hd_tex` → 1382 B, кодовая секция равна
  установленному (`b69dfa3799da`). Запись в игру не тронута (diff 0).
- `[P]` Грабля плоского дампа установщика (регистр имени модуля, `case_ok`) —
  `docs/PITFALLS.md`, раздел «Импорт 2.2.3 чувствителен к регистру имени модуля».
- `[P]` Повторные замеры тулчейна после переноса (2026-11-17): паритет
  `python3 tools/pyc/parity_file.py config[game.root]/data/scripts/hd_scale.pyc patches/hd_scale.py`
  (установка читается) → **code objects 43/43, близость 1.0000, sig=0 ops=0**;
  `python3 tools/exe/exepatch12.py` (без флага, scratch) из baseline
  `45e93d46…` → md5 **`171319bbc170…`** = установленный EXE; cave-блобы из
  `sh tools/exe/build_cave.sh` сверяются с `docs/STATE.md` (fixall 236 B
  `bf3fc99b796c…`, fontscale 192 B `546fd954343f…`, fontscale4x 224 B `f320fcb6e1a3…`);
  `sh tools/exe/build_cave.sh video43` даёт пещеру **49 B** (docstring
  `tools/exe/exepatch_video43.py` исправлен с 48 B на измеренное), прогон
  `python3 tools/exe/exepatch_video43.py` (scratch, игра не тронута) воспроизводит
  патченный EXE md5 `aab6454ca501…`; `python3 tools/exe/exepatch_frame1.py` (scratch)
  воспроизводит правку байта fileoff 149932 (`00 → 80`, md5 scratch
  `7c4036a07850…`). Установка после всех прогонов: 5097 файлов, md5-diff 0.
- `[P]` Воспроизведены сканы публикационного ядра: tracked-файлов 272, личных путей вне
  `tools/archive/**` **0**, игровых бинаров/`.pyc` в git **0**, `vendor/**` не отслеживается;
  AST core `.py` (вне архива) = 51, парсится Python 3 = 50, py2-only = 1
  (`tools/pyc/py22_worker.py`, задокументировано в `docs/TOOLS.md`); AST `tools/archive/**` `.py`
  = 105, парсится = 97, py2-only = 8 — список имён в `tools/archive/README.md`
  (абзац «8 архивных скриптов не парсятся Python 3»).
## Сессия 2026-11-17 (вечер): тултипы у курсора в 16:9

- `[U]` «Тултипы в 16:9 возникают в верху экрана, а не под курсором»;
  «достаточно сделать так, чтобы тултипы были рядом с курсором, попиксельная
  точность не нужна».
- `[P]` Причина — `ui/ui.CalcPopupPos` (канвасный бокс `(0,0,1024,768)` при
  сдвинутых EdgeFix-панелях); правка — обёртка в `patches/hd_scale.py` (видимый
  бокс `x = -HD_DX/HD_K .. (1024-HD_DX)/HD_K`, кламп к курсору; identity при
  `HD_K==1`). Замер и установка: `docs/PATCHES.md`, раздел «Тултипы у курсора
  в 16:9». Установлено: `data/scripts/hd_scale.pyc` md5 `12d38f093d2c…`
  (25618 B), манифест перечислен по новой сборке; снапшот до/после — diff
  ровно этот файл.
- `[P]` Ярусы после записи: `--quick` rc 1 (два известных дрейфа: cursor
  DRIFT_RELEASE_AHEAD, olmenu DRIFT_KNOWN), `--full` rc 0, `--untouched` rc 0.
- `[P]` Новый канонический зонд: `tools/probes/probe_hovershot.ps1`
  (наведение курсора + съёмка окна; см. `docs/TOOLS.md`). Реестр: значения
  HIGHDPIAWARE для scratch-деревьев тестов добавлялись на время прогонов
  (`scroll_fix` — осталось, `tt_fix` — снято после тестов); для установки —
  штатное значение. После финального прогона из установки `hdlog.txt` (его
  пересоздаёт игра при каждом запуске) убран из дерева — `--untouched` rc 0;
  `hd.cfg` = `3840 2160 32 1` (записан последним прогоном).


## Сессия 2026-11-17 (день): почвенные текстуры карт (`.tex` из `.map`)

- `[P]` План `docs/TASK_tex_unpacker.md`, фазы A–D: 44 записи `.tex` из 30 `.map`
  (гейт сканера OK, сумма blob'ов 41732096 B); канон раскода = raw interleaved
  RGB (bpp 3) / RGBA (bpp 4), строки top-down (тот же канон, что `.btf`);
  round-trip blob→PNG→blob побитово **44/44** (`tools/tex/qc_tex.py`).
  Раскод 44 PNG: `scratch_dir('ld')/tex_src/` + `MANIFEST.txt`.
- `[P]` Новые инструменты (регистрация — `docs/TOOLS.md`, «tex-пайплайн»; пайплайн —
  `docs/PIPELINE.md`, «tex-пайплайн»): `tools/tex/scan_tex.py`, `unpack_tex.py`,
  `qc_tex.py`, `tile_match.py`, `test_pattern.py`, `patch_map_pattern.py`,
  `hook_ground.py`, `realcopy_tree.py`; хук-материалы
  `patches/tex_hook_opaque.py`, `patches/tex_hook_alpha.py`.
- `[P]` Known-answer по отображению (scratch-дерево `D:/tmp/cygwin/opencode/ld/tex_hook_tree`,
  полная копия установки): подмена blob'ов `.tex` паттерном даёт решётку тайлов на
  земле (`scratch_dir('ld')/taxpat_overland.png`, `taxband_overland.png`,
  `taxbandb_overland.png` (снимки прогона съёмки; пересоздаются скриптами,
  в git не входят)); полоса у верхнего края записи = дальние/левые рёбра тайлов.
  Стартовая карта новой игры — `portal_domain`. Подмена `.btf`-ресурса (`mm_bkg01.btf`)
  движком не проходит (ресурс пересоздаётся при запуске, текстура в материале 2048²).
- `[P]` Дерево после тестов восстановлено (`patch_map_pattern.py --restore`,
  `hook_ground.py --restore`: 30 `.map` и 92 почвенных материала из backup).
  Реестр: `~ HIGHDPIAWARE` для `D:\tmp\cygwin\opencode\ld\tex_hook_tree\LETHALDREAMS.EXE`
  — осталось.
- `[P]` Установка не изменена инструментами: снапшот md5+mtime до/после — дрейф только
  игровых артефактов запуска (`Lib/*.pyc` 9, `hd.cfg`, `hdlog.txt`, font-сайкары
  `.fdf`/`ld_*.btf`, `mm_bkg01.btf`); `hdlog.txt` из дерева убран. Ярусы:
  `--quick` rc 0 (файлов 5097, 2 известных дрейфа), `--untouched` rc 0 (нарушений 0).


## Сессия 2026-11-17 (после коммита): круг на земле и альфа-плоскости `.tex`

- `[P]` Режим круга: `tools/tex/test_pattern.py --circle red --bg yellow --frac 0.5`
  (диаметр = доля стороны; превью 64x64 с кругом d=32 — `scratch_dir('ld')/tex_64_yellow_red.png`),
  `tools/tex/patch_map_pattern.py --circle … --frac 0.5 --alpha keep|opaque|zero`.
- `[P]` Гипотеза «не все текстуры кодируются одинаково» проверена: поля шапки всех 44
  записей различаются только `w,h,alpha,blob_len` (9 сигнатур), единый raw-раскод.
  Различается семантика альфа-плоскости 12 bpp4-записей (5 полностью прозрачных,
  5 с общим набором альфа 137..216, остальные индивидуальные) — см.
  `docs/OBSERVATIONS_TEXTURE.md`.
- `[P]` Отображение: при `--alpha keep` в дальней области земля = решётка красных
  эллипсов на жёлтом (`scratch_dir('ld')/taxcircle2_overland.png`, кроп `circle_far.png`),
  в ближней — красные четырёхугольники целых тайлов (`circle_near.png`). Контроль
  `--alpha zero` (альфа-пасс прозрачен): красные четырёхугольники остаются — они от
  opaque-прохода, крупный тайл = выборка в точке, ~25% тайлов красные; гладкая решётка
  эллипсов в дальней зоне создаётся альфа-проходом с родной альфа-плоскостью. Первый прогон
  (`--alpha` из паттерна = 255) дал «жёлтое с красными квадратами» — альфа-пасс стал
  непрозрачным overlay; это свойство движка, не кодировки записей.
- `[P]` После прогонов карты дерева восстановлены (`--restore`), установка: diff = 9
  известных дрейфов `Lib/*.pyc` запуска игры, `--quick` rc 0, игра закрыта.


## Сессия 2026-11-17 (вечер): Lanczos-2x и отказ движка на изменённые w/h

- `[P]` `tools/tex/upscale_tex.py` — Lanczos-3 апскейл 2x всех 44 PNG
  (`scratch_dir('ld')/tex_up2x`). `patch_map_pattern.py --up2x [--maxsrc N]` пересобирает
  записи с удвоенными w/h/blob_len (plen/clen правятся, round-trip побитовый).
  Режимы сглаживания (`--smooth`, `--frompng`) откатаны по указанию пользователя
  `[U]`, scratch-каталог `tex_smooth` удалён.
- `[P]` Краш: при загрузке стартовой карты игра закрывается, если хоть одна запись
  имеет изменённый размер — полная 2x, ограниченный (--maxsrc 512), и минимальный
  (одна запись 32²→64² в portal_domain.map). Same-size патчи грузятся. Протокол,
  проверки контейнера и план REA-разбора: `docs/ISSUE_tex_upscale.md`.
- `[P]` В payload карты парные копии w,h нет (пара только в шапке записи).
- `[P]` Новый штатный проб съёмки боя: `tools/probes/probe_battleshot.ps1`
  (новая игра, скип уроков Enter'ом, клики по канвасу, `SCENE battle` из hdlog).
- `[P]` После всех прогонов дерево восстановлено (`--restore`), игра закрыта;
  установка — дрейф только 9 известных `Lib/*.pyc`, `--quick` rc 0.
