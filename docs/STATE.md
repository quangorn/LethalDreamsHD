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

## Кодовый ярус (10 pyc)

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

- Новые модули лежат в `config[game.root]/data/scripts/`: `types.pyc` (3225 B,
  md5 `414bd2aa0857…`; allowlist `named_legals` + `snapshot_only`) и `hd_res.pyc`
 (2497 B, md5 `fc524b6139c0…`; manifest `state` = `GENERATED`, собирается пайплайном
  из `hd.cfg`). В `data/scripts/ui/` их нет. Патч к ним не применяется.
- Патчи: `release/patches/*.patch` (`tools/apply_patch.py`).
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
