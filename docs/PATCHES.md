# PATCHES.md — модель патчей

> EXE-патчи и cave: см. раздел «Окно, мигание и патч EXE» ниже и цепочку в
> `docs/STATE.md`. Ограничение (`.text` 238 байт, свободно 2) и план перехода на
> прокси-DLL — `docs/PLAN_proxy_dll.md`.

## Модель

- База каждого модуля — `decompiled_fixed/<module>.py` (локальная, производная от
  игрового bytecode).
- Патч — минимальный diff-файл `release/patches/<module>.patch` (контекст
  `-u0`-семантики: строки до/после, удалённые/добавленные), авторский.
- Аппикатор `tools/apply_patch.py` — чистый Python (бинарника `patch(1)` нет):
  dry-run по умолчанию; запись только `--install`; контексты сверяются
  (md5 базы + строковые контексты); при конфликте не пишет ничего и печатает
  ожидаемую/фактическую строки; идемпотентен (повторный прогон = byte-identical
  результат).
- Смена базы-декомпилята: `tools/pyc/port_patch.py --all --base <dir>` переносит
  правки на новую базу (выравнивание строк + гейт байткодом), затем
  `tools/release/update_entry.py <м>` пересобирает `.patch` и запись манифеста.
- Гейт «патч ↔ база»: `python3 tools/pyc/check_patches.py [--base DIR]` — rc≠0
  значит хунки не ложатся или патч перестал давать установленный bytecode.
- `ui/ui` — исключение из шага установки: в этой установке он ставится
  `tools/ldhook/patch_ui.py --install` (сборка base-печати + загрузчик
  `imp.load_dynamic(ldhook)`), иначе хуки боевого AI не грузятся. `install_patch.py
  ui/ui --install` загрузчик стирает. `state` в манифесте — описательное поле
  (`update_entry.py` всегда пишет `MATCH`), признак «чистой» сборки — только md5.
- Расположение: `tools/apply_patch.py` рядом с `tools/check_state.py` — это место
  зафиксировано в `fix_command` манифеста
  (`python3 tools/apply_patch.py <м> && python3 tools/pyc/install_patch.py <м>`),
  чтобы checker печатал готовую команду. Патч-файлы ищутся в `release/patches/`.

## Рецепт .pyc (важное)

Игровые модули компилируются интерпретатором **2.2.3** (magic `ed2d0d0a`).
Запись header'а pyc: magic + mtime исходника (4 байта little-endian).
Скрипт-сборщик (`py22_worker.py build`) пишет mtime = время сборки; чтобы
получить **base-печать** (md5 из манифеста), апикатор/установщик обязан
переписать `bytes[4:8]` на `pyc_source_mtime` манифеста. С 17.11.2026 это делает
сам установщик `tools/pyc/install_patch.py` (`fix_header()` перед записью в игру;
печатает header и сверку md5 с манифестом). Замер 17.11.2026: установка без
rewrite даёт header build-time (`2026-11-17 10:42`) и md5, не равный base-печати;
после rewrite md5 == манифест, `check_state --quick` — MATCH.

Следствие: `co_filename` компиляции = путь `.py`, из которого компилировали.
Тело bytecode (bytes[8:]) совпадает с base-печатью только при компиляции из
канонического репозитного пути `patches/<module>.py` (путь корня репозитория);
в зеркале/scratch-дереве md5 легитимно расходится по `co_filename` — checker
печатает пояснение, это не поломка. Base-печати манифеста проверены компиляцией
из опубликованного дерева (`tools/pyc/py22_worker.py build patches/<module>.py ...`
+ header-rewrite на `pyc_source_mtime`).

## Известные дрейфы

- `ui/cursor.pyc` — дрейф закрыт 19.11.2026: установлена published base-печать
  `60754a253515…` (`release/patches/ui_cursor.patch`); прежняя сборка v6
  `4a8eb4bab413…` сохранена в `scratch_dir('battle_baseline')/cursor_installed_v6.pyc`.
- `ui/olmenu.pyc` — с 18.11.2026 каноническая сборка (`MATCH`, md5 `af176d4859a1…`);
  прежний `DRIFT_KNOWN` (приватное дерево `3d96162df727…`, base-печать
  `5cb5cc88ddc8…`) закрыт пересборкой из опубликованного дерева вместе с фиксом
  Esc×2 (см. `docs/STATE.md`, сессия 18.11).

## Модули и константы (release/manifest.json)

| модуль | патч (размер) | pyc_source_mtime |
|---|---|---|
| data/scripts/gamescript | gamescript.patch (554 B) | 1794778202 |
| ui/ui | ui_ui.patch (4764 B) | 1794778202 |
| ui/cursor | ui_cursor.patch (4196 B) | 1794778645 |
| ui/olmenu | ui_olmenu.patch (2758 B) | 1794778203 |
| ui/battlemenu | ui_battlemenu.patch (6237 B) | 1794778202 |
| ui/btutorial | ui_btutorial.patch (864 B) | 1794778203 |
| ui/olcamera | ui_olcamera.patch (1943 B) | 1794778203 |
| battlecamera | battlecamera.patch (12473 B) | 1794778203 |
| mentality/std_brains | mentality_std_brains.patch (5655 B) | 1047745868 |
| mentality/std_bstates | mentality_std_bstates.patch (11577 B) | 1052809502 |
| mentality/battlecharacters | mentality_battlecharacters.patch (996 B) | 1052156450 |

- 11 патчей (патч `bdomain` снят 27.11.2026, модуль родной); новые модули без патча:
  `data/scripts/types.pyc`, `data/scripts/hd_res.pyc`,
  `data/scripts/hd_scale.pyc`, `data/scripts/hd_tex.pyc`.
- base-печать == установленный md5 для всех модулей, кроме загрузчика `ui/ui.pyc`
  (KNOWN-drift, `docs/VERIFY.md`); дрейф `ui/cursor.pyc` закрыт 19.11.2026.
- Цепочка exe: `docs/STATE.md`.

## Применение (полная команда)

```
python3 tools/apply_patch.py <м> && python3 tools/pyc/install_patch.py <м>
```

Порядок: патч `.py` (локальная база) → компиляция с манифестным mtime →
установка в игру (`install_patch.py --install`). Для `ui/ui` третий шаг —
`python3 tools/ldhook/patch_ui.py --install` (он добавляет загрузчик).

## Тесты (пройдены)

1. все 12 модулей: `.py` после apply → md5 == манифест (gate сверяет py_md5);
2. canonical-компиляция репозитного `.py` → scratch `.pyc`: 12 md5 == манифест;
3. идемпотентность: прогоны 1/2 byte-identical;
4. sabotaged patch: «APPLY STOPPED, nothing written: context mismatch at base
   line 958», печат ожидаемой/фактической строк; исходники репозитория и
   декомпиляты после теста — diff 0.

### Панели оверленда: патч `HD_PanelFix` (2026-11-15, ночь)
- `[P]` Сдвиг панелей, не растягивание: `SC(w)`/`SY(h)` не меняются (аспект арта остаётся 4:3), при `k >= 1` (4:3) — возврат без правки. Реализовано: функция `HD_PanelFix(templ)` в `patches/ui/olmenu.py`, вызов из ветки `BCCT_TEMPLATE` в `BOLMenu.ModifyCallBack` (rect правится один раз до создания контролов). Раскладка панелей и общий паттерн сдвига — `docs/OBSERVATIONS_UI.md`.
- `[P]` Установка: `python3 tools/pyc/install_patch.py ui/olmenu`; эталон установленной сборки `ui/olmenu.pyc` md5 `af176d4859a1…` (`docs/STATE.md`). — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Панели оверленда: патч `HD_PanelFix` (2026-11-15, ночь)»)

## Окно, мигание и патч EXE (проверено реверсом + живым тестом пользователя)
- `[P]` Окно создаёт EXE (`FUN_00424910`; exstyle `0x40000` задаётся ровно один раз — `push 0x40000` по rva `0x4249b8`, fileoff 149944); видео-окно класса `lethaldreamsvideo`; `SetDisplayMode` = `FUN_00477de0(this,w,h,bpp,f)`, при `f=1` — `ChangeDisplaySettingsA(&devmode,4)` = **CDS_FULLSCREEN** (реальный modeset), а не `CDS_TEST`; `SetWindowPos(...,0xfffffffe,...)` = HWND_NOTOPMOST.
- `[P]` Мигание при alt-tab снято однобайтовым патчем fileoff 632177 (`6A 00`→`6A 01`, SW_HIDE→SW_SHOWNORMAL), скрипт `scratch_dir('ld')/exepatch2.py`: оригинал `7943036fbd1233a0dbd43228f129cfc7`, патченный `ce79027f48d5092f31abea1421cf7305`; бэкап `backup_orig/LETHALDREAMS.EXE.orig`.
- `[P]` `lethaldreams.dll` в финальной сборке не используется (StarForce, секция `.sforce`, кода в файле нет); удалена из установки (бэкап `backup_orig/lethaldreams.dll`).
- грабля — `docs/PITFALLS.md` (ловушка `WS_EX_COMPOSITED`) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Окно, мигание и патч EXE (проверено реверсом + живым тестом пользователя)»)

## Borderless fullscreen (F=1) — реализовано 2026-11-14
- `[P]` Полный набор правок — `tools/exe/exepatch3.py`; EXE после exepatch3 md5 `45e93d46aad6ceb3dfc17e86d04e4f82` (цепочка бэкапов `backup_orig/`: оригинал `7943036f…`, `.swhide` `ce79027f…`, `.cds` `6396d19d…`); установленный EXE — продолжение цепочки, `exepatch12` (`171319bb…`, `docs/STATE.md`).
- `[P]` Правки: fileoff 491308 `6A 04→6A 02` (`CDS_FULLSCREEN`→`CDS_TEST`); все обращения к драйверу дисплея убраны через `add esp,8`+nop (fileoff 491357/490588/455947 — просто nop нельзя: stdcall-стек едет, msvcrt «Buffer overrun detected!»); стиль окна `0x86000000→0x80000000` (клиент = ровно WxH); фон класса `6A 04→6A 05` (`NULL_BRUSH`).
- `[P]` OSD монитора снимает `h+1` по высоте окна (`pycbuild.MIN_NEW`, накат `python3 tools/pyc/pycbuild.py install-min`); видео-окно его не заменяет. AppCompat-слой `~ HIGHDPIAWARE` (`tools/archive/probes/set_layer.ps1`) обязателен. Фон 16:9 — `_bg_fix` = pass-through (канвас 1024x1024, aspect един). Замеры и природа масштаба текста — `docs/OBSERVATIONS_FONT.md`.
- грабли — `docs/PITFALLS.md` (ловушка `WS_EX_COMPOSITED`; рамка `WS_BORDER` вместо `h+1` непригодна) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Borderless fullscreen (F=1) — реализовано 2026-11-14»)

## Шрифты 16:9 — рабочее решение (2026-11-14)
- `[P]` Строка `.fdf` = `char x y adv` (4-е поле — АДВАНС, не высота); сжатие ячейки (ink вместе с адвансом) фактором `k = h*1024/(w*768)` даёт нужную пропорцию.
- `[P]` Генератор `tools/archive/font_gen/rescale_font3.py` (база — только родные бэкапы `paths.btf_backup_dir()/*_orig.*`), раскладка сайдкаров `--install`; пары `*_169` (сжатые) / `*_43` (родные), выбор в `hd_res.Prepare()` по `|w/h - 16/9| <= 0.06`; откат `tools/fonts/restore_fonts.py`.
- `[P]` `HD_FNT_F` в `hd_scale` = 1.0: адвансы уже сжаты в листе, двойная компенсация не нужна. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Шрифты 16:9 — рабочее решение (2026-11-14)»)

## Центрирование и межстрочный шаг 16:9 (2026-11-14, ночь)
- `[P]` Ширину текстовой раскладки движок фиксирует в `InitBillboard` (`Set()` её не пересчитывает), поэтому `hd_scale.InitBillboard` передаёт движку масштабированную копию `InitParams`, а в `Params` возвращает канвас.
- `[P]` Межстрочный шаг = `GetFontSize() + RowS`; компенсация `RowS_new = k*(pitch + RowS) - pitch` (действует до exepatch8; после него — `RowS_new = k*RowS`, флаг `HD_FNT_PATCHED`).
- `[P]` 4:3 побитово прежнее. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Центрирование и межстрочный шаг 16:9 (2026-11-14, ночь)»)

## Дополнительное сжатие шрифта 16:9 (2026-11-14, 5% по X + 15% по Y)
- `[P]` Независимые факторы `-kx` (адвансы и расстановка ячеек), `-fx` (ширина ink), `-fy` (высота ink) в `tools/archive/font_gen/rescale_font4.py`; адвансы не меняются → межглифные просветы растут сами, ширина строк и раскладка прежние.
- `[P]` Якорь вертикального сжатия — нижняя строка ink той же полосы; pitch из `.fdf` не трогается (сегментация полос и индексы глифов целые, компенсация `RowS_new` остаётся валидной). База — только родные бэкапы.
- `[P]` Итог установки: `python3 tools/archive/font_gen/rescale_font4.py -kx 0.75 -fx 0.7125 -fy 0.85 --install`. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Дополнительное сжатие шрифта 16:9 (2026-11-14, 5% по X + 15% по Y)»)

## Патч EXE: глифы UI (сессия 2026-11-15)
- `[P]` Живая линия отрисовки глифов — сайт `0x470932` (`glDrawElements`); запись вершины: stride `0x20`, `x = float@+0xc`, `y = float@+0x10`. Live-мутация ×0.75 вокруг центра квада на этом сайте доказана (рычаг работает).
- `[P]` Ранние cave-encoding'и x87 были семантически неверны; точная таблица encoding'ей — из `gcc -m32 -masm=intel` (метки objdump сдвинуты — доверять только gcc).
- `[P]` Гипотеза «SProtect-проверка целостности `.text`» опровергнута: статические патчи `.text` работают. Состояние на тот момент — EXE чистый `backup/LETHALDREAMS.EXE.pre4` md5 `45e93d46aad6ceb3dfc17e86d04e4f82`; рабочий патч — exepatch8 ниже.
- грабли — `docs/PITFALLS.md` (пещера на сайте draw = тождество; снимки только при активном окне) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Патч EXE: глифы UI (сессия 2026-11-15)»)

## 2026-11-15 (день, вторая половина): статический EXE-патч масштаба шрифта РАБОТАЕТ
- `[P]` Точка масштаба — `FUN_0041cc90` (ленивая нормировка UV): умножить `slot4` и `pitch` на `k` сразу после неё — квад, перо, межстрочный шаг и центровка уменьшаются согласованно; один сайт, `.data` не трогается.
- `[P]` `exepatch8` (приватный архив сессий Project/1.7z, entry LethalDreams/exepatch8.py — исторический скрипт, не в git): сайт `0x41ce31` (5 байт `c6 41 58 01 59`) → `jmp 0x4ebf12`; пещера 120 байт (`tools/exe/caves/fontscale.s`, сборка `tools/exe/build_cave.sh`): `k = h*1024/(w*768)` из `[0x51cc7c]+0x18/+0x1c`, зажим `k>1` в 1.0 (4:3 → прежнее), умножение `pitch` и всех 256 `slot4`.
- `[P]` Состояние на тот момент: EXE = патч `exepatch8` (md5 `cce4537307ba83525f8ca0554c3584eb`), шрифты родные (`tools/fonts/restore_fonts.py`); откат `exepatch8.py restore` → `backup/LETHALDREAMS.EXE.pre4` (текущий установленный EXE — `exepatch12`). — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («2026-11-15 (день, вторая половина): статический EXE-патч масштаба шрифта РАБОТАЕТ»)

## 2026-11-15 (вечер): exepatch9 — вертикаль и переносы в 16:9 исправлены
- `[P]` Вертикаль: после exepatch8 движок отдаёт `pitch_engine = k*pitch`, поэтому нужен `RowS_new = k*RowS` (флаг `HD_FNT_PATCHED`). Переносы: сайт 2 `0x40b494` (`mov [ecx+0x294],eax`, сеттер шрифта из `SetFont`) → cave2 зовёт `FUN_0041cc90` сразу при назначении шрифта, до раскладки.
- `[P]` Ширина: advance округляются с накоплением ошибки (carry) `v = a*k + e; n = round(v); e = v - n` — сумма по строке точна; остаток ~+3% на длинной строке вынесен в `docs/archive/TODO_FONT_WIDTH.md`. Замеры и коэффициент A — `docs/OBSERVATIONS_FONT.md`.
- `[P]` `exepatch9` (приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py — исторический скрипт, не в git): два сайта + блоб 192 Б в `0x4ebf12` (cave1 `0x00..0x7a`, cave2 `0x80..0xad`, data `0xb0/0xb8`); md5 патча `49976478943f0e2871872a2c6fb55f28`, откат `exepatch9.py restore`. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («2026-11-15 (вечер): exepatch9 — вертикаль и переносы в 16:9 исправлены»)

## 4x-листы шрифтов + коэффициент A (2026-11-15, вечер)
- `[P]` Масштаб: 4x-лист даёт 0.70 экранных px на тексель; генератор `tools/fonts/gen_font4x.py` (PNG 4x → `.btf` шаблоном родного + `.fdf` с ×4 у `x,y,adv,pitch`; родные пары в `*_43`), установка `python tools/fonts/gen_font4x.py --install`.
- `[P]` `exepatch10` (`tools/archive/exe_patch/exepatch10.py`, пещера 224 Б `tools/exe/caves/fontscale4x.s` + `tools/archive/cave_gen/build_cave4x.py`): те же сайты `0x41ce31`/`0x40b494`; `k_eff = k*0.25`, при `k == 1` — identity; `adv = (a_sheet - CORR)*k_eff + carry`, `CORR = 4*A = 1.3384`; EXE md5 `51f69974e1dada6e7b191961e853a1ef`.
- `[P]` Решение Фазы 4 (18.11.2026): установка остаётся на `exepatch12` (md5 `171319bb…`), EXE больше не трогаем; `ldhook.pyd` (те же сайты через скан сигнатур, `tools/ldhook/`) — проверенный dev-путь, в поставку не входит; чек-лист перехода — `docs/PLAN_proxy_dll.md` §4. Base-требования к декомпиляту — `docs/PIPELINE.md`; запись о дефекте старой базы — `docs/PITFALLS.md`. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («4x-листы шрифтов + коэффициент A (2026-11-15, вечер)»)

## Боевой HUD поверх recovered-базы (2026-11-15, вечер)
- `[P]` База правок — `decompiled_fixed/` (`tools/pyc/decompyle_base.py`); правка `patches/ui/battlemenu.py`: `XLeft = 0 - sh` (`sh = hd_scale.HD_Shift()`) — весь левый BHUD-блок едет; ветка `BCCT_TEMPLATE` в `ModifyCallBackSel` → `hd_scale.HD_EdgeFix(param, левый, правый)` до создания контролов (левый: `mc_pullbtn`, `mc_pullbtn_bkg`, `btn_mainmenu`, `btn_disband`, `btn_retreat`, `btn_pause`, `btn_defatt`, `btn_move`; правый: `mini_map`, `frame`, `spells_border_zone`; `console` не входит).
- `[P]` Установка: `python3 tools/pyc/install_patch.py hd_scale ui/olmenu ui/battlemenu`; регрессия 4:3 — регионы `left_bhud`/`left_btns`/`console_strip`/`center_battle` побитово идентичны.
- грабля — `docs/PITFALLS.md` (декомпилят печатает ИНДЕКС констант вместо литерала: BHUD исчезает целиком) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Боевой HUD поверх recovered-базы (2026-11-15, вечер)»)

## Esc из боя: главное меню (27.11.2026)
- `[P]` Вектор пунктов `BackToMainMenu` (`patches/ui/battlemenu.py`) = `[1, 0, 1, 1, 1, 3]`: Quit и Options
  активны (оригинал `[0, 0, 1, 1, 1, 3]`), Load оставлен включённым (загрузка сейва из боя), Save — выключен как в оригинале.
- `[P]` Установка: `python3 tools/apply_patch.py ui/battlemenu --install`; эталон `data/scripts/ui/battlemenu.pyc` md5 `cece97624608…`.
- грабля — `docs/PITFALLS.md` (декомпилят переставляет элементы литерала `ShowMainmenu`)

## Консоль боя (левая полоса) — исправлена (2026-11-15, финал итерации)
- `[P]` `console` добавлен в левый список `HD_EdgeFix` (`patches/ui/battlemenu.py::ModifyCallBackSel`): rect `[-434,90,450,428]` при сдвиге `-sh` даёт правый край движка `k*(16-170)+dx = 12.3 >= 0` — полоска видима и прижата к левому краю; при 4:3 сдвиг=0 → identity. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Консоль боя (левая полоса) — исправлена (2026-11-15, финал итерации)»)

## Журнал боя (консоль) Tab в 16:9 — исправлено (2026-11-15, финал)
- `[P]` Границы журнала в `Update` (`patches/ui/battlemenu.py`) сдвинуты на `-sh = -HD_Shift()`: OPEN stop `rect['left'] >= -sh`, CLOSE stop `rect['right'] = MinConcoleWidth(16) - sh`; скорость `ConcoleStep=60` канвас/кадр не менялась; при `sh=0` — побитово оригинал.
- `[P]` `hd_scale._log` лимит строк поднят 4000→12000 (в 16:9 прогон упирался в лимит). — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Журнал боя (консоль) Tab в 16:9 — исправлено (2026-11-15, финал)»)

## R/E/F1: финальное решение — обычная k-схема для квадратных диалогов (16.11.2026)
- `[P]` Единственная установленная правка — `patches/hd_scale.py`: `DIALOG_TEX = ['re_bkg.tga', 'encyclopedy_scr.tga', 'player_dialog.tga']` + префикс `qrs_`; такой фон при `HD_K<1` не идёт в `_is_bg` pass-through, а получает обычные `EC/EY/SC/SY` (метка `self.HDDBG`); экран = канвас 1024x768 по центру, дети — той же k-схемой. При `HD_K==1` identity, 4:3 не затронут; `bspellbook.py`/`enmenu.py`/`help.py` остаются ОРИГИНАЛЬНЫМИ.
- `[P]` Q-режим (Q-листы шрифтов + q-трансформ) отменён как построенный на неверной модели масштаба; код и листы удалены; хроника и план — `docs/archive/CANCELLED_Q_MODE.md`.
- грабля — `docs/PITFALLS.md` («1024x1024 → экранный квадрат со стороной h» — модель неверна) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («R/E/F1: финальное решение — обычная k-схема для квадратных диалогов (16.11.2026)»)

## Краевой скролл оверленда: патч `ui/olcamera` (17.11.2026)
- `[P]` Порог низа = движковая координата нижнего края канваса: метод `HDEdgeY()` = `hd_scale.SY(screen_size[1] - 1)` и условие `if self.ms_lasty > self.HDEdgeY():`; при `k=1` порог побитово родный 765, остальные края не тронуты.
- `[P]` Патч `release/patches/ui_olcamera.patch` (2 ханка) поверх базы `decompiled_fixed/ui/olcamera.py`; каноническая команда: `python3 tools/apply_patch.py ui/olcamera && python3 tools/pyc/install_patch.py ui/olcamera --install`.
- `[P]` Эталон установленной сборки: `data/scripts/ui/olcamera.pyc` md5 base-печати `f67ae9904032…` == установленный (`MATCH`), `.py` md5 `4bf8df324ee6…`, `pyc_source_mtime` 1794778203; pristine-копия установки до патча `ba346c67b543…`.
- грабля — `docs/PITFALLS.md` (сырое событие `WMO_MOUSEMOVE` vs канвасные пороги краёв) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Краевой скролл оверленда: патч `ui/olcamera` (17.11.2026)»)

## Боевая камера: тот же краевой скролл (17.11.2026)
- `[P]` Тот же механизм во втором модуле (`BattleCamera.B_Init`, `screen_size=[1022,766]`, пороги в `hB_Update`): тот же `HDEdgeY()` = `hd_scale.SY(screen_size[1]-1)` и условие `ms_lasty > self.HDEdgeY()`.
- `[P]` Патч `release/patches/battlecamera.patch` (2 ханка) поверх базы `decompiled_fixed/battlecamera.py`, паритет `PARITY-EQUAL[TOLERANT]`; каноническая команда: `python3 tools/apply_patch.py battlecamera && python3 tools/pyc/install_patch.py battlecamera --install`.
- `[P]` Эталон установленной сборки: `data/scripts/battlecamera.pyc` md5 base-печати `ddb3e6fa629e…` == установленный (`MATCH`), `.py` md5 `63990fd1eeb4…`, mtime 1794778203; pristine-копия `9efdb4d1dae9…`. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Боевая камера: тот же краевой скролл (17.11.2026)»)

### Установка обоих патчей (17.11.2026, после подтверждения пользователя)

- `[U]` Оверленд: «На глобальной карте (сразу после загрузки сохранения) скролл
  работает»; бой (патченная сборка в scratch-дереве, новая игра + 15 Enter):
  «всё работает. ставь оба патча».
- `[P]` Установка: `python3 tools/apply_patch.py <м>` (base-печать == манифест) →
  `python3 tools/pyc/install_patch.py <м> --install`. Записаны
  `data/scripts/ui/olcamera.pyc` md5 `f67ae9904032…` и
  `data/scripts/battlecamera.pyc` md5 `ddb3e6fa629e…` (обе == base-печать); остальная установка не тронута.

## Тултипы у курсора в 16:9: патч-обёртка `CalcPopupPos` (17.11.2026)
- `[P]` Обёртка `CalcPopupPos` в авторском модуле `patches/hd_scale.py` (`ui.CalcPopupPos = CalcPopupPos` в `install()`): 4 кандидата popup проверяются против **видимого** канвас-бокса 16:9 `x = -HD_DX/HD_K .. (1024-HD_DX)/HD_K`, `y = 0..768`; ни один не влез → тултип ставится у курсора и клампится в бокс; при `HD_K == 1` — вызов оригинала, 4:3 побитово. Ограничение: у правого края нижней панели текст клампится к левому краю бокса — принято пользователем («рядом с курсором, точность не нужна»).
- `[P]` Эталон установленной сборки: `data/scripts/hd_scale.pyc` md5 `12d38f093d2cacd8b2892832e13649d5` (== base-печати манифеста, mtime `1794819372`), `.py` md5 `dc8314f90af0…`. — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Тултипы у курсора в 16:9: патч-обёртка `CalcPopupPos` (17.11.2026)»)

## Боевая камера: MMB-обзор и зум колесом (18.11.2026)
- `[P]` Только Python-слой (`patches/battlecamera.py`, база `decompiled_fixed/battlecamera.py`, 5 ханков, `release/patches/battlecamera.patch`); EXE/DLL не тронуты. MMB-обзор: якорь `mmb_anchor` в точке нажатия, отклонение от якоря в движковых координатах с насыщением `±mmb_omax = 260.0` и мёртвой зоной `±mmb_dz = 6.0`; рыскание `RotateAroundY(-mmb_sens*(|ox|-dz)*time)` с `mmb_sens = 0.012` (рад/ед.), наклон клампится `cur_pitch ∈ [-60,-45]` (`mmb_sens_y = 0.18`); краевой скролл подавляется на время MMB.
- `[P]` Зум колесом = высота камеры `zoom_up` (fov из Python недоступен, `SetScale` на рендер боевой камеры не влияет), шаг `zoom_step` (было `3.0`, ослаблён до `1.0` — см. следующий раздел), диапазон `[min_up, 33.5]`; `zoom_active` включается первым щелчком без прыжка, до первого щелчка высота — родное поведение.
- `[P]` Эталон установленной сборки (18.11.2026): `data/scripts/battlecamera.pyc` md5 `91be96e74662448a5672b3c6edcace28`, `.py` md5 `5f757d07164f1fb54bd568c47cd9ea02`; каноническая сборка — `tools/apply_patch.py battlecamera` + `tools/release/update_entry.py battlecamera`.
- грабли — `docs/PITFALLS.md` (`SetScale` ≠ зум, `RotateAroundY` в радианах, уроки «Игровая камера и панели» не снимаются Enter'ом) — подробный разбор: `docs/archive/NARRATIVE_2026-11.md` («Боевая камера: MMB-обзор и зум колесом (18.11.2026)»)

## Ослабление зума колесом (18.11.2026) — итог `zoom_step = 1.0`

- `[P]` Шаг зума колесом ослаблен до `zoom_step = 1.0` ед. высоты на щелчок (было `3.0`):
  константа `self.zoom_step` в `h_MouseWheel` (`patches/battlecamera.py`); диапазон `[min_up=6.5, 33.5]`,
  terrain following и MMB-обзор не менялись. Откат — вернуть значение константы и прогнать тот же пайплайн.
- `[P]` Эталон установленной сборки: `data/scripts/battlecamera.pyc` md5 `c7d3150409af…` (гейт
  `apply_patch.py battlecamera` даёт `.py` md5 `8e64086fcdde…`), state `MATCH`;
  каноническая сборка — `docs/PIPELINE.md` (`update_entry.py` → `apply_patch.py` → `install_patch.py --install`).

## Полоса каста (жёлтый сектор `bhud_spgauge`) в 16:9 — исправлено (27.11.2026)

- `[P]` Обёртка `RotateBillBoardAroundPt` в авторском модуле `patches/hd_scale.py`
  (`BE.RotateBillBoardAroundPt = RotateBillBoardAroundPt` в `install()`): pivot поворота передаётся
  движку как `SC(point[0]), SY(point[1])`, оригинал при `HD_K<1` не вызывается; при `k==1` — вызов
  оригинала, 4:3 побитово. Полоса стоит внутри нижнего сектора рамы, как в 4:3.
- `[P]` Эталон установленной сборки: `data/scripts/hd_scale.pyc` md5 `f96c168a0c24…` (27010 B,
  crc32 `0f466682`, mtime `1794819372`) == base-печати манифеста, `.py` md5 `fadd5cfcce5d…`;
  каноническая сборка — `python3 tools/pyc/install_patch.py hd_scale --install`.
- грабля — `docs/PITFALLS.md` (pivot поворота без масштаба — полоса вне рамы)

## HP-индикатор выбранного юнита: патч `mentality/battlecharacters` (27.11.2026)

- `[P]` Выделенный юнит — HP-индикатор ×1.5 по ширине и ×2 по толщине; мелкие индикаторы невыбранных
  юнитов — прежний размер (значения заданы литералами) и темнее (`HPIND_SMALL_COLORMULT = 0.5`).
  Реализовано: константы `HPIND_BIG_OUTER`/`HPIND_BIG_INNER`/`HPIND_SMALL_OUTER`/`HPIND_SMALL_INNER`/`HPIND_SMALL_COLORMULT`,
  патч `release/patches/mentality_battlecharacters.patch` (790 B, 1 ханк).
- `[P]` Эталон установленной сборки: `data/scripts/mentality/battlecharacters.pyc` md5 `83ea2b28fc1f…`
  (== base-печати, mtime `1052156450`), `.py` md5 `81984b4ee9e6…`; pristine-копия `fb1c48e47dde…` =
  `installed_recorded_md5`; новый кодовый модуль манифеста, каноническая команда —
  `python3 tools/apply_patch.py mentality/battlecharacters && python3 tools/pyc/install_patch.py mentality/battlecharacters`.
