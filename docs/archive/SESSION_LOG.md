# archive/SESSION_LOG.md — хроника итераций сессий

Хроника в датах, newest-first. Одна дата = принятое решение (≤3 строки): что принято и где
реализовано. Актуальное состояние — `docs/STATE.md`, пайплайны — `docs/PIPELINE.md`,
отменённые планы — `docs/archive/CANCELLED_Q_MODE.md`, грабли — `docs/PITFALLS.md`.

## Сессия 2026-11-27: полоса каста в 16:9 + случайный спавн отряда

- `[P]` Pivot поворота gauge — движковые единицы `SC/SY` (обёртка `RotateBillBoardAroundPt`,
  `patches/hd_scale.py`); эталон `data/scripts/hd_scale.pyc` md5 `336867c67316…`.
- `[P]` Съёмка полосы каста: DSL-шаги `rdown`/`rup` (`tools/probes/perform_action_in_game.ps1`), снимок `castbar`.
- `[P]` Спавн отряда в бою — случайный вход карты: патч `bdomain` снят, `data/scripts/bdomain.pyc` = родной (crc32 `5e282f17`).

- `[P]` Вектор `BackToMainMenu` = `[1, 0, 1, 1, 1, 3]` (`release/patches/ui_battlemenu.patch`):
  Quit и Options активны, Load из боя сохранён — `docs/PATCHES.md`, «Esc из боя: главное меню».

## Сессия 2026-11-21: порт боя — Фазы 3 и 4 закрыты офлайн-replay

- `[P]` Гейт Фазы 3 — офлайн per-call replay (`battle_replay_diff.py` на capture v8); живой A/B санитарный
  — решение 3.7, `docs/PLAN_BATTLE_AI_CPP.md`.
- `[P]` Фаза 4 закрыта (решение 4.8): `s6_unit_ai_tick` + детур, capture v9 + replay `eee0` (решение 28/28),
  живой прогон без крашей; остаток `map1`/`occ`/`s30` — класс 3.8.
- `[P]` Site4/cap-API удалены (п. 3.4), site5 — носитель H4/H5/H6; эталон `.pyd` и `battle_replay.exe` — артефакты
  `tools/ldhook/build.py` и `tools/probes/battle_replay_build.py`.

## Сессия 2026-11-20: порт `e0a0`/`dd70` до Фазы 3 (trace + офлайн-replay)

- `[P]` Фазы 2 и 3 закрыты по коду; решения 2.0/2.8 (гейт Фазы 2 живой `s1/s2/vis`
  перенесён в Фазу 3) — `docs/PLAN_BATTLE_AI_CPP.md`.
- `[P]` Трассировка: `BATTLE` 29 полей + `STATEFP` (`battle/trace.cpp`), хуки H1/H2/H3/H4/H5/H6/H7,
  пиннинг `LDHOOK_SEED`; сравнение `tools/probes/trace_diff.py --align=ordinal`.
- `[P]` Офлайн-харнесс: `LDHOOK_BATTLE_CAPTURE` (v3) + `tools/probes/battle_replay.cpp` +
  `battle_replay_diff.py`; на нативном входе rc 0. Открыто: порт держит +3 слота map1
  с 38-го вызова юнита 3 (`register_occupied_slot`/ветка 1 `dd70`), гейт Фазы 3 не закрыт.
- `[U]` Модели сабагентов: максимум работы на strata; другие — только средние/высокие по
  сложности (сторонняя модель — сложные, DS 4.1 — резерв после провала). Первое ревью — всегда strata.
  Reasoning: `strata/q3.8-fn#medium` (исполнители), `strata/q3.8-fn#high` (ревью),
  `@preset/ds-flash-41-cheapest#low` (набор пересмотрен 21.11.2026).

## Сессия 2026-11-20: точка спавна отряда в бою фиксирована (индекс 5)

- `[P]` Спавн отряда и стартовая камера в бою — вход `EntrancePoints[5]` на всех боевых картах
  (`FIXED_ENTRANCE_INDEX = 5`, `InitializeData`, `patches/bdomain.py`); случайного выбора нет.
- `[P]` Эталон: `data/scripts/bdomain.pyc` md5 `957bc9d86ccc…`, state `MATCH`; патч — `docs/PATCHES.md`
  («Точка входа отряда в бой»), снятый вариант спавна на точке врагов — `docs/PITFALLS.md`.

## Сессия 2026-11-19: тормоза боя — кап проверен живьём, выбран структурный путь

- `[P]` Кап 32 (site4 в `ldhook.pyd`) убирает лаги полностью, но ломает агро (31% поисков → «цель не найдена»);
  установка откачена в чистое состояние, решения по капу нет — `docs/STATE.md`, `docs/PERF_BATTLE.md`.
- `[P]` Причина по коду: `FUN_0045ce80` — полный проход списка, `FUN_0045e0a0` зовёт его до ~4400 раз;
  выбор цели — фильтр `[obj+8]` + минимум `[obj+0x24]`, расстояние только в предикате `c880`.
- `[P]` Канонический установщик загрузчика `tools/ldhook/patch_ui.py` (`--build/--install/--restore`);
  зонды `perf_frida9_frames.js` (кадры), `perf_frida10.js` (поиск цели), `perf_frames_summary.py`.

## Сессия 2026-11-18 (ночь): зум колесом ослаблён (итог `zoom_step = 1.0`)

- `[P]` Одна константа `self.zoom_step` в `h_MouseWheel` (`patches/battlecamera.py`): `3.0` → **`1.0`** ед. высоты
  на щелчок; диапазон `6.5…33.5`, terrain following и MMB-обзор не тронуты. Полный ход зума = 27 щелчков.
- `[P]` Эталон установленной сборки: `data/scripts/battlecamera.pyc` md5 `c7d3150409af…`, state `MATCH`;
  разбор и откат — `docs/PATCHES.md`, «Ослабление зума колесом». Проверку в игре делает пользователь.

## Сессия 2026-11-18 (ночь): зависание ввода после Esc×2 из оверленда

- `[P]` Фикс: шестой элемент `ShowMainmenu` = `2` (ResumeGame) в `patches/ui/olmenu.py`; причина — пункт
  ResumeBattle (`3`) при `BattlemenuScene is None` давал `KeyError` в `cursor.Set` и мёртвый ввод.
- `[P]` Манифест пересобран канонически, установлено `install_patch.py ui/olmenu --install` (md5 `af176d4859a1…`,
  state `MATCH`); прежний дрейф `DRIFT_KNOWN` olmenu закрыт. Живая проверка Esc×2 пройдена.

## Сессия 2026-11-18 (день): боевая камера — MMB-обзор и зум колесом

- `[P]` MMB-обзор: якорь в точке нажатия, мёртвая зона 6 ед., насыщение 260 ед., `mmb_sens` 0.012 рад/ед.,
  `mmb_sens_y` 0.18 град/ед., краевой скролл подавляется на время удержания; средняя кнопка — флаг `MK_MBUTTON` (16)
  в `WMO_MOUSEMOVE`.
- `[P]` Зум сделан высотой камеры (`zoom_up`, terrain following, кламп 33.5); `SetScale` на рендер не влияет,
  `RotateAroundY` — радианы (клавиша turn_left 1.75 рад/с = 90°) — `docs/PITFALLS.md`.
- `[P]` Установлено в игру: `battlecamera.pyc` md5 `91be96e74662…` (позже заменён сборкой шага 1.0).
  Новые инструменты: `tools/probes/probe_camera.ps1`, `tools/release/update_entry.py`, `tools/release/tree_snapshot.py`.

## Сессия 2026-11-18 (вечер): ldhook Фаза 4 — решение по EXE

- `[P]` **EXE установки не трогаем**: установка остаётся на cave `exepatch12` (`171319bb…`), `ldhook.pyd` в поставку
  не входит; `.pyd` — dev-путь следующих правок (критерии §5 `docs/PLAN_proxy_dll.md` не выполнены).
- `[P]` Правило: после любой правки EXE — `tools/ldhook/check_sites.py` (уникальность трёх сигнатур в `.text` по
  `VirtualSize`; установка `patched`, baseline `45e93d46…` `installable`).
- `[P]` Матрица стабильности: 4:3 (`1920x1440`, `1440x1080`, `2880x2160`) — identity, записей нет; 16:9
  `3840x2160` — биты `0x3FA646F1 → 0x3F7987FA`; все четыре рабочих режима покрыты, крашей нет.

## Сессия 2026-11-18 (вечер): ldhook Фаза 3 — логгер

- `[P]` Лог `.pyd` — общий `hdlog.txt` в каталоге EXE (`ldhook_log.txt` упразднён), уровни env `LDHOOK_LOG`
  (`0` off / `1` info / `2` debug) и `ldhook.set_level(n)`, `ldhook.log()`, `ldhook.stats()`.
- `[P]` `.text` берётся из PE-заголовков (VA + VirtualSize), не из констант — сдвиг `.text` правками EXE не ломает хуки.
- `[P]` Гейты: 16:9 — три сайта found, site3 биты эталона `exepatch12`, site2/site1 FIRED; 4:3 — identity guard'ы;
  `probe_phase1.ps1` читает `hdlog.txt`. Установка не тронута.

## Сессия 2026-11-18 (вечер): ldhook Фазы 0-бис–2 — `.pyd`-путь и три хука

- `[P]` `.pyd`-путь доставки кода подтверждён (`docs/PLAN_proxy_dll.md`): `ldhook.pyd` грузится из игрового скрипта
  `imp.load_dynamic`; прокси `python22.dll` — резерв. Грабля `Py_InitModule(name, NULL)` — `docs/PITFALLS.md`.
- `[P]` Три хука в памяти (`tools/ldhook/ldhook.c`): site1 `0x41ce31` (масштаб шрифта, порт cave1), site2 `0x40b494`
  (setFont), site3 `0x47dd80` (матрица проекции); EXE на диске не меняется.
- `[P]` Гейты: 16:9 — все сайты found, биты матрицы равны эталону `exepatch12`; A/B с cave-EXE — сигнатуры
  не находятся, игра работает на `exepatch12` (fallback и эталон). Установка не тронута.

## Сессия 2026-11-18 (позже): профилирование тормозов боя

- `[P]` Бой CPU-bound: ~100% одного ядра (GPU 0–22%); тяжёлый вызов `FUN_0045e0a0` — патологический цикл
  до ~4400 итераций (норма 2), цепочка `FUN_0044bdd0 → FUN_0045eee0 → FUN_0045e0a0` — ~85% времени боя.
- `[P]` Инструментарий `tools/perf/` (в git): PS-пробы, C# x86-семплеры, frida-хуки; состав и замеры —
  `tools/perf/README.md`, `docs/PERF_BATTLE.md`.
- `[P]` Троттлинг `FUN_0044bdd0` не помог (frame-rate-адаптивна); план — cap итераций `FUN_0045e0a0` (32),
  не реализован — `docs/PERF_BATTLE.md`.

## Сессия 2026-11-18: блоки 32x32 почвенных `.tex`

- `[P]` Атлас = сетка блоков 32×32; blob уходит в `glTexImage2D` без перестановок, UV ссылаются только на непустые
  блоки; мозаика — упакованные патчи земли (`BTexturer.dll`, `TexturizeIt`), переставлять блоки не нужно.
- `[P]` Инструменты: `tools/tex/tex_tiles.py` (блок `<stem>__tx<col>_ty<row>.png`, CLI `glue`),
  `unpack_tex.py --tiles [--skip-empty]`, `patch_map_pattern.py --tiles DIR [--maxside N]`; f=1 сборка == установка побитово.
- `[P]` Итог — **High Fidelity 3x** в установке и `hd_pack.7z` (1379 записей, 30 `.map`): max blob 28 311 552 B,
  подтверждённый максимум стороны 2048 → **3072** (blob ≤ 32 MiB, NPOT допустим) — `docs/OBSERVATIONS_TEXTURE.md`.

## Сессия 2026-11-17 (ночь): краш при изменённом w/h `.tex` разобран

- `[P]` Сигнатура: ExitCode `0xC0000005`, fault offset `0x67f54` (`FUN_00467eb0`), NULL-возврат FACT
  `FUN_00495720`; payload `.map` ходит ADV `FUN_0049bf70` по цепочке TLV `(type,size)`, содержимое `pos+8..pos+8+size` —
  `docs/KNOWLEDGE.md`, `docs/OBSERVATIONS_TEXTURE.md`.
- `[P]` Две причины, обе данные: дублирующий `name_len` u32-префикс (сдвиг полей +4) и неисправленные TLV-размеры.
  Исправлено в `tools/tex/patch_map_pattern.py`: `rec_bin = имя+шапка+blob`, `chain_headers()`, `fix_chain()` — `docs/PITFALLS.md`.
- `[P]` Гейты грузятся (32²→64², полный 2x, даунскейл, не-POT 24²) с `probe_exitcode.ps1`; дерево восстановлено, установка не тронута.

## Сессия 2026-11-17 (вечер): Lanczos-2x и отказ движка на изменённые w/h

- `[P]` `tools/tex/upscale_tex.py --factor N` (Lanczos-3); `patch_map_pattern.py --up2x [--maxsrc N]` пересобирает
  записи с удвоенными w/h (round-trip побитовый) — регистрация в `docs/TOOLS.md`, `docs/PIPELINE.md`.
- `[P]` Отказ: игра закрывается, если хоть одна запись имеет изменённый размер (полный 2x, `--maxsrc 512`, и одна
  запись 32²→64²); same-size патчи грузятся — протокол `docs/archive/ISSUE_tex_upscale.md`.
- `[P]` Новый штатный проб съёмки боя: `tools/probes/probe_battleshot.ps1` (новая игра, скип уроков Enter'ом).

## Сессия 2026-11-17 (вечер): тултипы у курсора в 16:9

- `[P]` Причина — `ui/ui.CalcPopupPos` (канвасный бокс при сдвинутых EdgeFix-панелях); правка — обёртка
  в `patches/hd_scale.py` (кламп к курсору, identity при `HD_K==1`).
- `[P]` Установлено: `data/scripts/hd_scale.pyc` md5 `12d38f093d2c…` — `docs/PATCHES.md`, «Тултипы у курсора в 16:9».
  Новый зонд `tools/probes/probe_hovershot.ps1`.

## Сессия 2026-11-17 (после коммита): круг на земле и альфа-плоскости `.tex`

- `[P]` Поля шапки всех 44 записей различаются только `w,h,alpha,blob_len` — единый raw-раскод; семантика
  альфа-плоскости по 12 bpp4-записям (5 полностью прозрачных, 5 с общим набором альфа 137..216) — `docs/OBSERVATIONS_TEXTURE.md`.
- `[P]` Альфа-проход движка создаёт решётку эллипсов в дальней зоне, opaque-проход — четырёхугольники тайлов;
  режимы `test_pattern.py --circle … --frac`, `patch_map_pattern.py --alpha keep|opaque|zero`.

## Сессия 2026-11-17 (день): почвенные текстуры `.tex` → PNG

- `[P]` Формат `.map`: `LDPACKER` + zlib payload; записи `name_len | <map>_<N>.tex | шапка 40 B | blob`,
  `blob_len = w*h*(3+alpha)`; 30 карт, 44 записи; канон раскода = raw top-down RGB/RGBA (как `.btf`) — `docs/KNOWLEDGE.md`.
- `[P]` `scan_tex.py` → `unpack_tex.py` → `qc_tex.py`: round-trip blob→PNG→blob побитово **44/44**
  (`scratch_dir('ld')/tex_src`, `MANIFEST.txt`).
- `[P]` Known-answer: подмена blob'ов паттерном даёт решётку тайлов на земле; `.btf`-ресурс резолвится из
  источника — подмена `.btf` не видна. Стартовая карта новой игры — `portal_domain`.

## Сессия 2026-11-17 (день): краевой скролл оверленда и боя (`ui/olcamera`, `battlecamera`)

- `[P]` Причина: пороги краёв сравниваются с движковыми (канвасными) координатами, а движковый Y-диапазон =
  `768*k` = 576 при `3840x2161` — нижний порог 765 недостижим; `h+1` не причастен.
- `[P]` Патчи: `release/patches/ui_olcamera.patch`, `release/patches/battlecamera.patch`; метод `HDEdgeY() =
  hd_scale.SY(screen_size[1]-1)`, при `k=1` identity (регрессия 4:3 побитово). A/B-замеры в scratch-дереве, base-печать
  `f67ae9904032…` и `ddb3e6fa629e…`.
- `[P]` Грабля: AppCompat-флаг привязан к пути EXE — scratch-копия запускается DPI-unaware; обход
  `set_layer.ps1 -GameRoot <scratch>` — `docs/PITFALLS.md`.

## Сессия 2026-11-17: интерпретатор 2.2.3 в каталог проекта

- `[P]` Архивы игровых поставок интерпретатор не содержат (exe/dll 0 во всех листингах); дерево собрано из
  официальных архивов python.org в `vendor/py223/` (untracked): `python22.dll` побитово = игровой (`4ee79da1533f…`),
  `imp.get_magic()` = `[45, 237, 13, 10]` — состав и рецепт в `docs/TOOLS.md`.
- `[P]` Грабля регистра имени модуля (`case_ok()` в `Python/import.c`, `PYTHONCASEOK`) — `docs/PITFALLS.md`.

## Сессия 2026-11-16 (вечер): публикация git-репозитория

- `[P]` **Опубликовано**: `https://github.com/quangorn/LethalDreamsHD` (public), ветка `master`,
  publish-цепочка initial commit `85ec7b9d…` → `45ccb09`; 272 файла, игровых бинаров 0, личных путей 0,
  `config.toml` не отслеживается; локальная история удалена по просьбе пользователя.
- `[P]` Шесть дефектов верификации исправлены (коммит `039baa0`): `restore_fonts.py` гейтирован `--install`;
  SNAPSHOT_ONLY = 713+11+6+10+1; C-API — EXE импортирует из `python22.dll` **53** символа из **753** экспортов.
- `[P]` Личные пути нейтрализованы в 104 файлах `tools/archive/**` (зоны PowerShell получили
  `$ScratchRoot`/`$GameRoot`/`$RepoRoot`, env `LD_SCRATCH`/`LD_GAME`/`LD_REPO`).

## Сессия 2026-11-16 (вечер): база пайплайна переставлена на `decompiled_fixed/`

- `[P]` `decompiled/` и `decompiled_snap/` байт-идентичны (434 файла); база правок — `decompiled_fixed/`
  (434 модуля, `dictionary_rus.py` перенесён из `decompiled/`), устаревший `decompiled_fixed/ui/battlemenu.pyc` удалён.
- `[P]` `build/gamescript.py` перетёрт оригиналом (`h = h + 1` + `hd_res.Prepare()`, строки 79/81); контроль
  установленных `.pyc`: gamescript/ui/cursor/battlemenu/btutorial/olmenu/hd_scale — md5 game == staged.

## Сессия 2026-11-15: установка 2x-апскейлов текстур в игру

- `[P]` Установлено **599 `.btf`** в `data/textures/` (211 МБ → 500 МБ, число файлов 1270 без изменений);
  пакер `tools/btf/pack_btf.py <in.png> <template.btf>` (шаблон = игровой `.btf`, патчатся w,h,blob_len, контейнер).
  Откат — из личного бекапа пользователя.
- `[P]` Грабля round-trip на неквадратных текстурах (`unpack_btf.parse_btf` сортирует квадратную пару первой):
  надёжная проверка читает w,h по байтовым офсетам пакера — `docs/PITFALLS.md`.

## Сессия 2026-11-15 (вечер): выгрузка оригиналов текстур в PNG

- `[P]` **1266 PNG** + `MANIFEST.txt`: dims канонические (первые два u32 после имени), layout top-down
  интерлейс, round-trip побайтовый; не парсятся 4 (blob меньше `w*h`).
- `[P]` Классы bpp: 4 → 714, 3 → 552. Итоговое решение по кадрам — `docs/PATCHES.md`, «R/E/F1: финальное решение».

## Сессия 2026-11-15 (ночь): цель съёмки R/E/F1 (промежуточная, отменена)

- `[P]` Цель «квадрат h x h по центру экрана» привела к Q-режиму, **отменённому 16.11.2026**; итоговое решение —
  обычная k-схема, 4:3-колонка по центру (`docs/PATCHES.md`, `docs/archive/CANCELLED_Q_MODE.md`).
- `[P]` Разбор механизма фона: `_is_bg` (`w>=1000 and h>=700`) — pass-through, движок растягивает единым `s = w/1024`,
  видны канвасные строки `0..h/s` — `docs/OBSERVATIONS_UI.md`.

## Сессия 2026-11-14: база Python-слоя и раскладка 16:9

- `[P]` База: родные `.pyc` + `python22.dll` 2.2.3 + `Lib/` — старт стабилен; `gamescript.pyc` = install-min
  (читает `hd.cfg` и зовёт `SetDisplayMode`), `hd.cfg` = строка `W H BPP F`.
- `[P]` Текстура меню выбирается игрой при старте: `hd_res.Prepare()` копирует sidecar
  `mm_bkg01_43.btf`/`mm_bkg01_169.btf` в `mm_bkg01.btf` по aspect ratio; ручная подмена не нужна.
- `[P]` Principled-пересчёт 4:3→16:9 (`patches/hd_scale.py`): `EC(x) = k*x + DX`, `DX = 512*(1-k)`,
  `k = h*1024/(w*768)`, центрирующий Y-сдвиг тождественно 0; итерация kx-при-parse (exepatch4) снята —
  даёт рост ширины глифов и требует рестарта, рабочий маршрут — cave при назначении шрифта (`docs/PATCHES.md`).

## Сессия 2026-11-27: снятие корневых stdlib-`.pyc`

- `[P]` Принято: корневые `.pyc` не нужны — на scratch-копии с полным `Lib/` игра проходит меню → персонаж
  → оверленд; снято 11 файлов (`tools/pyc/root_pyc_layer.py --install`), слой и allowlist пересчитаны
  (`--untouched`/`--full` rc 0, установка 5087 файлов).
