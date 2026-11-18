# PATCHES.md — модель патчей

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

- `ui/cursor.pyc` (`DRIFT_RELEASE_AHEAD`): установленный `4a8eb4bab413…` = патч
  курсоров версии 6; base-печать `60754a253515…`.
- `ui/olmenu.pyc` — с 18.11.2026 каноническая сборка (`MATCH`, md5 `af176d4859a1…`);
  прежний `DRIFT_KNOWN` (приватное дерево `3d96162df727…`, base-печать
  `5cb5cc88ddc8…`) закрыт пересборкой из опубликованного дерева вместе с фиксом
  Esc×2 (см. `docs/STATE.md`, сессия 18.11).

## Модули и константы (release/manifest.json)

| модуль | патч (размер) | pyc_source_mtime |
|---|---|---|
| data/scripts/gamescript | gamescript.patch (554 B) | 1794778202 |
| ui/ui | ui_ui.patch (3574 B) | 1794778202 |
| ui/cursor | ui_cursor.patch (4120 B) | 1794778645 |
| ui/olmenu | ui_olmenu.patch (2237 B) | 1794778203 |
| ui/battlemenu | ui_battlemenu.patch (4813 B) | 1794778202 |
| ui/btutorial | ui_btutorial.patch (599 B) | 1794778203 |

- `ui/types.pyc`, `ui/hd_res.pyc` — новые модули, без патча.
- base-печать vs установленный md5 различаются для `ui/cursor.pyc`
  (drift, `docs/VERIFY.md`); остальное совпадает (header 2 байта).
- Цепочка exe: `docs/STATE.md`.

## Применение (полная команда)

```
python3 tools/apply_patch.py <м> && python3 tools/pyc/install_patch.py <м>
```

Порядок: патч `.py` (локальная база) → компиляция с манифестным mtime →
установка в игру (`install_patch.py --install`).

## Тесты (пройдены)

1. все 6 модулей: `.py` после apply → md5 == манифест (gate сверяет py_md5);
2. canonical-компиляция репозитного `.py` → scratch `.pyc`: 7 md5 == манифест
   (cursor `60754a253515…`); `ui/olmenu` — ожидаемый `DRIFT_KNOWN`
   (`5cb5cc88ddc8…` base-печать, installed `3d96162df727…`);
3. идемпотентность: прогоны 1/2 byte-identical;
4. sabotaged patch: «APPLY STOPPED, nothing written: context mismatch at base
   line 958», печат ожидаемой/фактической строк; исходники репозитория и
   декомпиляты после теста — diff 0.

### Панели оверленда: патч `HD_PanelFix` (2026-11-15, ночь)

Раскладка панелей и причина смещения — `docs/OBSERVATIONS_UI.md`; общий паттерн сдвига — `docs/OBSERVATIONS_UI.md`,
раздел «Боевой HUD: сдвиг к краям экрана и общий паттерн».


- `[P]` **Результат правки**: `patches/ui/olmenu.py`, функция `HD_PanelFix(templ)`, вызывается из
  ветки `BCCT_TEMPLATE` в `BOLMenu.ModifyCallBack` (`CreateFromTemplate` вызывает callback на
  шаблон ДО создания контролов, т.е. rect правится один раз). Сдвиг, НЕ растягивание:
  левая панель канвас x = `-dx/k` (170 при 4K) -> движковый rect `0..192` = экран **0..720**;
  правая канвас x += `dx/k` (512->682) -> движковый rect `640..1024` = экран **2400..3840**.
  `SC(w)`/`SY(h)` не меняются => аспект арта = 4:3, растяжения нет. Дети едут с родителем
  (левая колонка −480 px, правый блок хода/маны +480 px) — колонка сохраняется как целое.
  При `k >= 1` (4:3) функция сразу возвращает, правки нет.
- `[P]` Первая версия (растгивание ширины: левая `w += dx/k`, правая `w = (1024-dx)/k - x`)
  дала растяжение арта по X (фактор 1.667/1.333) — отвергнута пользователем.
- `[P]` Паритет декомпилята `ui/olmenu` = **0.9952** (`python3 tools/pyc/pycbuild.py parity ui/olmenu`,
  расхождения sig=0, только артефакт `RETURN_VALUE`), поэтому патч поверх нормализованного
  декомпилята (`build/ui/olmenu.py`) безопасен. Установка: `python3 tools/pyc/install_patch.py ui/olmenu`.
  Откат: `python3 tools/pyc/pycbuild.py uninstall ui/olmenu`.


## Окно, мигание и патч EXE (проверено реверсом + живым тестом пользователя)
- `[P]` Окно создаёт **EXE** (`FUN_00424910`): `CreateWindowExA(exstyle=0x40000
  (WS_EX_WINDOWEDGE), class="LethalDreamsWndClass", style=0x86000000
  (WS_POPUP|WS_THICKFRAME|WS_VSCROLL), 100x100)`, lpParam=object; window handle
  хранится в `object+0x7c`. Стиль exstyle задаётся ровно один раз (`push 0x40000`
  по rva 0x4249b8, fileoff 149944).
- `[P]` Видео-окно (`FUN_0049a5e0`): класс `lethaldreamsvideo`, стиль `0x82000000`
  (WS_POPUP|WS_VSCROLL), создаётся child'ом на весь client, хэндл в `DAT_0051d5e8`.
- `[P]` `SetDisplayMode` движка = `FUN_00477de0(this,w,h,bpp,f)`:
  - `f=0` (окно): если bpp тот же → `SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,w,h,0)`
    (rva 0x477e5b, hWndInsertAfter=0xfffffffe — это NOTOPMOST, не TOPMOST!) +
    resize-вызов `[0x51d1c0]`; modeset не трогается;
  - `f=1`: `ChangeDisplaySettingsA(&devmode, 4)` — **флаг 4 = `CDS_FULLSCREEN`, т.е.
    РЕАЛЬНАЯ смена режима дисплея** (ранее в этом файле ошибочно значилось `CDS_TEST`,
    это `2`). Именно отсюда «эксклюзивный fullscreen»: долгий alt-tab и мигание при
    каждом переключении. Далее тот же `SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,w,h,0)`;
  - при ошибке `ChangeDisplaySettingsA` (return != 0) функция возвращает ошибку и
    не доходит до создания окна/GL;
  - `ChangeDisplaySettingsA(NULL,0)` — restore в `FUN_0046f500` (вызывается только
    когда устройство уже создано и меняется режим; при нашем патче — no-op).
  **Вывод `[P]`:** fullscreen-ветка = реальный modeset + popup; для borderless режим
  дисплея менять нельзя — патч ниже.
- `[U]` **Фикс мигания применён и подтверждён (2026-11-14).** Патч один байт:
  rva 0x49a571 fileoff 632177: `6A 00` → `6A 01` (`ShowWindow(video, SW_SHOWNORMAL)`
  вместо SW_HIDE). Мигание при alt-tab ушло; при выходе из игры мигания нет.
  Единственный остаток: экран мигает 1 раз при запуске — «в целом не критично».
  md5: оригинал `7943036fbd1233a0dbd43228f129cfc7`, патченный
  `ce79027f48d5092f31abea1421cf7305`; бэкап `backup_orig/LETHALDREAMS.EXE.orig`.
  Скрипт патча: `scratch_dir('ld')/exepatch2.py`.
- `[P]` `lethaldreams.dll` — **не используется в финальной сборке** (StarForce/SFORCE-протектор,
  секция `.sforce`, кода в файле нет, FF15-ссылок на IAT ноль). Проверено 2026-11-18: EXE её
  ни статически не импортирует (в import-таблице только WINMM/DSOUND/KERNEL32/USER32/GDI32/
  ADVAPI32/ole32/python22.dll), ни загружает в рантайме (frida: в списке 81 модуля её нет),
  а игра с удалённой DLL доходит до меню. Удалена из установки (бэкап
  `backup_orig/lethaldreams.dll`, оригинал в `original.7z`); все оконные вызовы находятся в EXE.



## Borderless fullscreen (F=1) — реализовано 2026-11-14
- `[P]` Причина «эксклюзивного» поведения при `F=1`: `ChangeDisplaySettingsA(&devmode, 4)`
  в `FUN_00477de0` — флаг `4` = `CDS_FULLSCREEN` (реальная смена режима), а не `CDS_TEST`.
  Патч: fileoff 491308 (rva 0x477f2b+1) `6A 04 -> 6A 02` (`CDS_TEST`). Скрипт
  `tools/exe/exepatch3.py`.
- `[P]` Рамка окна: fileoff 149933 `push 0x86000000 -> 0x80000000`
  (WS_POPUP|WS_THICKFRAME|WS_VSCROLL -> WS_POPUP). Client area = ровно WxH;
  в F=0 окно тоже попp без рамки (рамка/ресайз сознательно сняты).
- `[P]` Мигание отрисовки: класс окна `LethalDreamsWndClass` (`FUN_00424910`,
  wndproc `FUN_004245f0`) регистрируется с `hbrBackground = GetStockObject(4)`
  = `BLACK_BRUSH`, а `WM_ERASEBKGND` (0x14) не обрабатывается -> `DefWindowProc`
  заливает клиент чёрным на каждом invalidate. Патч fileoff 149798
  `6A 04 -> 6A 05` (`NULL_BRUSH`) — фон класса больше не стирается.
- `[P]` `SetWindowPos(hwnd, 0xfffffffe, ...)` = **HWND_NOTOPMOST**, а не TOPMOST:
  окно не висит поверх всех, alt-tab обычный, в taskbar присутствует (WS_EX_APPWINDOW).
- `[P]` md5 патченного EXE `45e93d46aad6ceb3dfc17e86d04e4f82`. Полный набор патчей —
  `tools/exe/exepatch3.py`; бэкапы: `backup_orig/LETHALDREAMS.EXE.orig` (оригинал
  `7943036f...`), `.swhide` (`ce79027f...`), `.cds` (`6396d19d...`).
- `[P]` Дополнительно убраны ВСЕ обращения к драйверу дисплея:
  `ChangeDisplaySettingsA(&devmode, ...)` в `SetDisplayMode(f=1)` и два
  `ChangeDisplaySettingsA(NULL,0)` (деструктор `FUN_00477af0`, `FUN_0046f500`) заменены
  на `add esp,8` + nop (fileoff 491357 / 490588 / 455947). Важно: нельзя ставить просто
  nop — stdcall-вызов сам чистит 8 байт аргументов, иначе стек едет и вылезает
  «Buffer overrun detected!» из msvcrt.
  Побочный полезный эффект: `F=1` больше не зависит от того, поддерживается ли режим
  (раньше `CDS_TEST` для нестандартного w/h возвращал ошибку и движок не делал
  `SetWindowPos` — окно оставалось 100x100).
- `[P]` AppCompat-слой `HKCU\...\AppCompatFlags\Layers`,
  `config[game.root]/LETHALDREAMS.EXE = ~ HIGHDPIAWARE` (скрипт `tools/archive/probes/set_layer.ps1`).
  `HIGHDPIAWARE` обязателен: без него процесс DPI-unaware и окно 3840x2160 физически
  становится 2194x1234 (системный масштаб 175%). `DISABLEDXMAXIMIZEDWINDOWEDMODE`
  (отключение fullscreen-оптимизаций) пробовали — на OSD монитора не повлияло, снят.
- `[P]` **OSD монитора (DisplayPort/3840x2160) на запуске/alt-tab/выходе** — не смена
  режима (его в игре больше нет вообще), а реакция DWM/драйвера на окно, **ровно
  совпадающее с экраном**. Проверено: при окне 3840x2159 и 3840x2161 OSD не появляется
  ни в одном из трёх моментов (пользователь подтвердил), при 3840x2160 — появляется.
  Решение: если в `hd.cfg` задано ровно нативное разрешение экрана, хук сам добавляет
  1 px по высоте (`w,h` сравниваются с `gamemodule.GetScreenSize()` в
  `gamescript.Initialize`). Окно 3840x2161 в (0,0) закрывает кадр полностью (лишний
  пиксель уходит за нижнюю границу), панель задач при этом перекрыта и не рисуется
  поверх игры. Правка генератора хука — `pycbuild.MIN_NEW`, накат:
  `python3 tools/pyc/pycbuild.py install-min`.
  - `[P]` **Эксперимент 2026-11-16: видео-окно не заменяет `h+1`.** Видео-окно
    `lethaldreamsvideo` — отдельный top-level `WS_POPUP` (owner = главное окно), живёт
    только на время ролика; при отключённом `h+1` и 4:3 видео-окне OSD/мигание главного
    окна вернулись. Разбор — `docs/OBSERVATIONS_ENGINE.md`, раздел «Второе окно…».
- `[P]` Замер alt-tab: слушатель `WM_DISPLAYCHANGE` (`tools/archive/probes/probe_displaychange.ps1`) за
  переключение туда/обратно не получил **ни одного** события; `EnumDisplaySettings`
  до/во время/после = `3840x2160 bpp=32 hz=120`; переключение 3-75 мс. То есть режим
  дисплея не меняется; OSD монитора — не от modeset.
- `[P]` DPI: процесс DPI-aware через AppCompat-слой реестра
  `HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers`:
  `config[game.root]/LETHALDREAMS.EXE = ~ HIGHDPIAWARE`. Без слоя окно 3840x2160
  при системном масштабе 75% было бы физически 2880x1620 (borderless бы не получился).
- `[P]` dxwrapper вынесен из игры (`config[game.root]/dxwrapper.dll` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит), `config[game.root]/dxwrapper.ini` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит), `dsound_stub.bak`,
  `config[game.root]/dxwrapper-lethaldreams.log` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) -> `scratch_dir('removed')/`); звук — системный dsound.
- `[P]` Проверка `tools/archive/probes/probe_f1.ps1` (прогон f1b, `hd.cfg = 3840 2160 32 1`):
  `MODE before/during/after_switch_out/after_switch_back` = `3840x2160 bpp=32 hz=120`
  (режим не меняется); `WINDOW rect=3840x2160 at (0,0) style=0x94000000`
  (= WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS) `exstyle=0x00040000` (APPWINDOW);
  `ALT-TAB` в notepad 1 ms, назад 75 ms. Снимки `scratch_dir('ld')/f1b_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), `scratch_dir('ld')/f1b_desktop.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит).
- `[?]` За пользователем: визуально подтвердить отсутствие мигания при alt-tab/выходе и
  «точку строго под пальцем» в F=1 (`MY = m/k`).
- `[P]` Фон 16:9 (`config[game.root]/data/textures/mm_bkg01_169.btf` (файл установленной игры, в git не входит)) = 2048x2048, арт в строках 896..2047 (замер офлайн
  по payload: first-non-black=896, last=2044). **Исправлено 2026-11-14: `_bg_fix` =
  pass-through** (фон передаётся движку без пересчёта, канвас 1024x1024). В F=1 движок
  растягивает текстуру по rect единым `s=w/1024` и клиппит по окну, blit инвертирует
  строки (как `qrs_battle`): видимые канвас-строки 0..h/s = арт заполняет окно ровно,
  aspect един (3840/2160 = 2048/1152), чёрного прямоугольника снизу нет.
  Старое `sy2 = HD_SY^2` сжатие давало неединый Y-масштаб (x=1.875, y~1.05) и чёрную
  полосу ~945 px снизу. Замер снимка `scratch_dir('ld')/bgpt_5s_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (`tools/archive/probes/measure_bgrows.ps1`):
  top-black=6 (art-виньетка, совпадает с 3 чёрными строками texture*1.875), bottom-black=1
  (лишний px окна за экраном), арт во всех строках 6..2159; лог `HDBGINIT BG ('MainBB', 0, 0, 1024, 1024)`.
  В окне (F=0) и при k=1 — identity, 4:3-регрессия не затронута.

Замеры и природа масштаба текста — `docs/OBSERVATIONS_FONT.md`.


## Шрифты 16:9 — рабочее решение (2026-11-14)
- `[P]` **`.fdf` строка = `char x y adv`** (4-е поле — АДВАНС, не высота). Проверено
  по всем родным листам: ink глифа всегда внутри `[x, x+adv)`, ни одного выхода за
  адванс (552 глифа). Адванс движок берёт из таблицы -> его можно масштабировать.
- `[P]` Сжатие ячейки (ink ВМЕСТЕ с адвансом) фактором `k = sy/sx = h*1024/(w*768)`
  даёт нужную пропорцию. Провал прежних генераторов — неверная трактовка 4-го поля
  как высоты (`tools/archive/font_gen/rescale_font.py`) и alpha=255 по покрытию без усреднения
  (`tools/archive/font_gen/rescale_font2.py`) -> «фрагменты» и «чёрные плашки». Модель движка не мешает.
- Генератор `tools/archive/font_gen/rescale_font3.py` (база — только `paths.btf_backup_dir()/*_orig.*`),
  раскладка сайдкаров `--install`, откат `tools/fonts/restore_fonts.py`. Пары `*_169`
  (сжатые) / `*_43` (родные), выбор в `hd_res.Prepare()` по `|w/h - 16/9| <= 0.06`;
  4:3 получает побайтово родные листы.
- `[P]` `HD_FNT_F` в `hd_scale` = 1.0: адвансы сжаты в листе, метрика ширины уже
  k-кратная, двойная компенсация не нужна.
- `[P]` Замер заголовка «Настройки» (ink): 4:3 151x25 (аспект 6.04);
  4K родные 483x58 (8.33, = 1.38 x — растяжение `sx/sy`=1.333 подтверждено);
  4K `_169` 356x58 (6.14, +1.7%); 2K `_169` 237x~40. Высота полосы при 4K не
  изменилась (58 px) — вертикаль не тронута.
- `[U]` Прежний референс `scratch_dir('ld')/ref43_dialog_nocursor.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) снят в момент fade-in
  (текст тёмный) -> замер «доля 0.56» по нему неточен. Свежий эталон:
  `scratch_dir('ld')/r43new_dialog_nocursor.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (1280x960, родные листы).

## Центрирование и межстрочный шаг 16:9 (2026-11-14, ночь)
- `[P]` Движок фиксирует ширину текстовой раскладки в `InitBillboard` (SetText/SetFont);
  `Set()` её не пересчитывает. Поэтому `hd_scale.InitBillboard` передаёт движку СРАЗУ
  масштабированные `InitParams` (копию), а в `Params` возвращает канвас. Иначе
  центрированный текст уезжает вправо, а блок заворачивается по неверной ширине.
  Проверено: заголовок «Настройки» 4K 2303→1932 px (центр 1920), 2K 1290 (центр 1280),
  4:3 640 (центр 640).
- `[P]` `SetFont(size)` НЕ масштабирует глифы (эксперимент `HD_FM=2` — ноль изменений);
  глифы идут в нативном pitch, `GetFontSize()` = pitch шрифта (38/19).
- `[P]` Межстрочный шаг = `GetFontSize() + RowS`. Компенсация:
  `RowS_new = k*(pitch + RowS) - pitch`. Иначе шаг в канвасе в 1/k раз больше.
  `newgamemenu` 4K: шаг 24 → 16 канвас (=эталон 4:3). Первая строка 395→368 канвас
  (эталон 366) — текст вписан в свиток, «Начать» виден. 2K тоже 16.
- `[P]` 4:3 остаётся побитово прежним (`imgdiff maxDelta<=4`, шум дизеринга).
- Инструмент навигации: `tools/archive/probes/probe_newgame.ps1` (главное меню → «Новая игра» (канвас 200,400)
  → 2 клика скип интро → съёмка экрана выбора персонажа).

## Дополнительное сжатие шрифта 16:9 (2026-11-14, 5% по X + 15% по Y)
- Задача пользователя: основной шрифт при 16:9 ужать ещё на ~5% по горизонтали и на ~15%
  по вертикали, увеличив горизонтальный паддинг.
- `[P]` Реализовано независимыми факторами в **`tools/archive/font_gen/rescale_font4.py`**: `-kx` (адвансы и
  расстановка ячеек в полосе, 0.75 = текущий k), `-fx` (ширина ink, 0.7125 = 0.75*0.95),
  `-fy` (высота ink, 0.85). адвансы НЕ меняются -> межглифные просветы растут сами,
  ширина строк и раскладка остаются прежними.
- `[P]` Якорь вертикального сжатия — **нижняя строка ink на той же строке полосы**
  (базовая линия и нижняя часть полосы сохраняются), сжатие вверх. Pitch из `.fdf`
  не трогается -> сегментация полос и индекс глифов целые, компенсация `RowS_new`
  в `hd_scale` остаётся валидной, правок Python-слоя не потребовалось.
- `[P]` Генератор читает базу **только** из родных бэкапов
  `paths.btf_backup_dir()/*_orig.btf` + `*_orig.fdf` (родные бэкапы оригиналов; восстанавливаются из архивов: python3 tools/extract_pristine.py templates, в git не входит)
  (никаких прогонов по уже сжатым сайдкарам — меньше потерь). 2D-ресемплинг ink-бокса
  ячейки: RGB — среднее по альфе, alpha — среднее покрытия. Контроль в выводе:
  `bleed` (ink за правым краем ячейки), `bandviol` (ink вне полосы/якорь) = 0 для всех 4 листов.
- `[P]` Офлайн-метрики (`tools/fonts/font_stats.py`), old(`kx=0.75`) -> new: ink width 0.956/0.956/0.829/0.897,
  ink height 0.850/0.850/0.838/0.846, advance 1.0000 (не тронут) — цели 5% и 15% точно.
- `[P]` Офлайн-превью глифов: `tools/fonts/font_preview.py` (собирает строку из глифов листа по `.fdf`,
  коды = cp1251; Cyrillic в uncial-листах под кодами 0xC0..0xFF, в tahoma11 — %128..%255).
  Сравнение родной/новый лист: `scratch_dir('ld')/cmp_nast20.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), `scratch_dir('ld')/cmp_tahoma_body.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), `scratch_dir('ld')/cmp_numbers.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит).
- `[P]` Живые замеры (`tools/archive/probes/measure_caption.ps1` — полосы + ink bbox, `tools/archive/probes/stack_crops.ps1` — base/new):
  - 4K `newgamemenu`: межстрочный шаг 45 px (не изменился), высота текстовой строки
    28 -> 24 px (**0.857**), позиции строк те же; ширина строк ink не изменилась.
  - 4K диалог настроек: полоса заголовка на тёмном фоне 67 -> 47 px;
    ink x-бокс строки идентичен (ширина строки прежняя).
  - 2K диалог: левый заголовок 36 -> 30 px (**0.833**), ink x 625..809 (ширина та же).
  - 4:3 (`1280 960 32 0`): load-имена = родные `*_43`, diff со свежим эталоном
    `scratch_dir('ld')/r43new_dialog_nocursor.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит): meanDelta 0.35, шум дизеринга — побитово прежнее.
- Инструменты: `tools/archive/font_gen/rescale_font4.py` (генератор + `--install`), `tools/fonts/font_stats.py`,
  `tools/fonts/font_preview.py`, `tools/archive/probes/measure_inkbox.ps1`, `tools/archive/probes/measure_caption.ps1`, `tools/measure/crop_rect.ps1`,
  `tools/archive/probes/stack_crops.ps1`, `tools/measure/downscale.ps1`, `tools/measure/pngdiff.py`.
- Итог установки: `python3 tools/archive/font_gen/rescale_font4.py -kx 0.75 -fx 0.7125 -fy 0.85 --install`
  (выход в `scratch_dir('ld169b')`).



## Патч EXE: глифы UI (сессия 2026-11-15)
- `[P]` Живая линия отрисовки глифов найдена: сайт `0x470932` — `push edi; push 0x1403; push ecx; push edx; call [0x51cd90] (glDrawElements)`; возврат `0x470938`; функция заканчивается `pop edi; pop esi; ret 0x14`. Запись вершины: база `esi+0xc`, stride `0x20`, **x = float@+0xc, y = float@+0x10**; индексы — ushort по `edi`, count = `ecx`.
- `[P]` Live-мутация ×0.75 вокруг центра квада на этом сайте доказана: цифровая полоса интро 87→57 px (`scratch_dir('ld')/scale75_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)) — рычаг работает.
- `[P]` Ранние encoding'ы cave (из прошлой сессии) были **неверны семантически**:
  `d8 04 24` = FIADD (целочисленное сложение), `d9 1c 24` = FSTP m32int,
  `6b db 20` = `imul ebx,ebx`; movzx писал не в тот регистр, и fld/fstp грузили/писали 8-байтовые double поверх 4-байтовых float полей x/y. Cave исполнялся (8496 хитов), координаты глифов становились мусором →screen серый, глифы улетали за экран. Это объясняет «серый снимок».
- `[P]` Точная таблица encoding'ей получена из gcc -m32 -masm=intel: `fld/fstp dword [esp]=d9 04 24 / d9 5c 24 04`, `fadd/fsub dword = d8 04 24 / d8 24 24` (**objdump метки x87 сдвинуты — доверять только gcc**), `fild dword [esp]=db 04 24`, `fld/fstp dword [esi+ebx+d]=d9 44 1e d / d9 5c 1e d`, `fmul st(0),st(1)=d8 c9`, `fadd st(0),st(1)=d8 c1`, `fdivp st(1),st(0)=de f9`, `movzx ebx,[edi+eax*2]=0f b7 5c 47 00`, `imul ebx,ebx,0x20=6b db 20`.
- `[P]` `tools/archive/cave_gen/cavegen.py` — генератор cave (float-формы, guard `test ecx,ecx; jz restore`, accumulators в `sub esp,8`), пока **не проверен стендом**; `tools/archive/frida_probe/ld_inject.py` — runtime-инжекция site-jmp + cave через Frida `Memory.patchCode` (сайт `base+0x70932`, cave `base+0xf5201`).
- `[P]` Снимки под Frida: после долгого attach рендер сцены умирает (окно серый фон, съёмка только после `detach`). Frida закрывать до снимка.
- `[P]` **ОПРОВЕРГНУТО** (см. раздел «SProtect опровергнута», 2026-11-15). Здесь раньше
  стоял вывод: «статический патч (site jmp + cave в zero-run `.text` VA `0x4f5201`) → игра
  падает `0xC0000005` за ~1 с; ведущая гипотеза — SProtect-проверка целостности `.text` при
  загрузке». **Гипотеза неверна:** проверки целостности/упаковки в EXE нет, падения были
  багами самих пещер (`exepatch6/7`), а статические патчи `.text` (сайты `0x41ce31`,
  `0x40b494`, пещера `0x4ebf12`) работают — см. `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py` (исторический скрипт сессии; в git не входит).
- `[P]` Состояние на диск **на тот момент** (сейчас EXE патчен — см. «ТЕКУЩЕЕ СОСТОЯНИЕ»):
  `config[game.root]/LETHALDREAMS.EXE` = чистый `backup/LETHALDREAMS.EXE.pre4`
  (md5 `45e93d46aad6ceb3dfc17e86d04e4f82`). Игра остановлена.
- Следующий шаг: C-стенд `test_cave.c` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z; в git есть стенд той же
  схемы `tools/archive/cave_gen/test_cave.py`) исполняет байты cave на пуле вершин и сверяет
  числа → затем inject+снимок (полоса 221–307 h87 → ~65), затем статический патч по
  проверенным байтам.



## 2026-11-15 (день, вторая половина): статический EXE-патч масштаба шрифта РАБОТАЕТ
- `[P]` **«SProtect / проверка целостности .text» опровергнута.** «332 секции» — баг
  `tools/archive/tmp_scratch/secparse.py` (таблица секций читалась с `pe+0x18` вместо `pe+0x18+SizeOfOptionalHeader`).
  Реально: 4 секции, `.text` VirtualSize `0xeaf12` (кончается на `0x4ec000`), энтропия 6.56,
  10.7 % нулей, валидный x86 по всему диапазону, импорты обычные (+python22.dll), анти-отладки,
  ReadFile-на-себя, CRC-циклов нет. Краши `exepatch7` объясняются его же багами:
  cave_k откатывал `sub esp,0xd0` перед `jmp 0x477dfb` (кадр не выделен → порча стека/SEH →
  `0xC000041D`); `k` брался из угаданных `[esp+0xe4]/[esp+0xe0]` (SEH-пролог!) → мусорный
  множитель; во всех ветках бисекции безусловно писались double в `0x51c088..`.
- `[P]` **Правильная точка — `FUN_0041cc90`** (ленивая нормировка UV, зовётся один раз на
  объект шрифта из `FUN_0041d020` по флагу `obj+0x58`). Она считает
  `u2 = adv*invW + u1`, `v2 = pitch*invH + v1` из ОРИГИНАЛЬНЫХ `slot4`/`pitch`. Если сразу
  после неё умножить `slot4` и `pitch` на `k`, то UV остаются полной ячейкой, а квад
  (adv×pitch), перо, межстрочный шаг (`FUN_0041d560`: `y += pitch`) и ширина/центровка
  (`FUN_0041c1f0`) — все согласованно меньше в `k`. Один сайт, `.data` не трогается.
- `[P]` `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch8.py` (исторический скрипт сессии; в git не входит): сайт `0x41ce31` (5 байт `c6 41 58 01 59`) → `jmp 0x4ebf12`;
  пещера 120 байт (`tools/exe/caves/fontscale.s` (источник в git; собираемые блобы — в paths.cache_dir('cave')), сборка `tools/exe/build_cave.sh`) ставит флаг, считает
  `k = h*1024/(w*768)` из `[0x51cc7c]+0x18/+0x1c`, зажимает `k>1` в 1.0 (4:3 → прежнее),
  умножает `pitch` и все 256 `slot4`, затем `pop ecx; ret`. `python3 приватный архив сессий Project/1.7z, entry LethalDreams/exepatch8.py (исторический скрипт сессии; в git не входит) restore`
  возвращает `backup/LETHALDREAMS.EXE.pre4`. md5 патча `cce4537307ba83525f8ca0554c3584eb`.
- `[P]` Проверка: 4K + РОДНЫЕ листы (`tools/fonts/restore_fonts.py`) — `scratch_dir('ld')/base8_4k_dialog_nocursor.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)
  (заголовок 478 px) → `scratch_dir('ld')/p8_4k_dialog_nocursor.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (365 px, ×0.764), резко, без кропа;
  старый офлайн-лист `x7sheet_4k` даёт 339 px (×0.709, пережат). 4:3 1280x960:
  `p8_43` vs `base8_43` meanDelta=0.0055, maxDelta=3, over_tol=0 — идентично.
- `[?r]` Ширина 365 против ожидаемых 358 (478×0.75) — запас на округление advance до int
  в таблице; при желании лечится floor'ом вместо round (fistp → FISTP + коррекция).
- Состояние на диск: EXE = патч `exepatch8` (md5 `cce45373…`), шрифты = **родные**
  (`tools/fonts/restore_fonts.py`, сайдкары `*_169/*_43` удалены), `hd.cfg` = `3840 2160 32 1`.



## 2026-11-15 (вечер): exepatch9 — вертикаль и переносы в 16:9 исправлены
- `[P]` **Симптом пользователя**: на экране выбора персонажа (круг «Веритион») в 16:9 текст сжат
  по вертикали (шаг строк 9.2 канвасных единиц вместо 16.0) и местами шире, чем в 4:3.
  В диалоге настроек список клавиш начал переноситься на вторую строку (в 16:9).
- `[P]` **Причина №1 (вертикаль)** — двойная компенсация в `hd_scale.InitBillboard`:
  формула `RowS_new = k*(pitch+RowS) - pitch` выводилась для НЕпатченного шрифта. После
  exepatch8 движок уже отдаёт `pitch_engine = k*pitch`, поэтому нужен просто
  `RowS_new = k*RowS` (шаг = k*pitch + k*RowS = k*(pitch+RowS)). Флаг `HD_FNT_PATCHED`.
  Замер: шаг 9.24 -> 16.0 канвасных, позиции строк 13-строчного блока совпали с 4:3
  (первая 257.6, последняя 465.6, абзацный отступ 29.6/30.2).
- `[P]` **Причина №2 (переносы)** — порядок: `SetText` (который режет строку по ширине)
  вызывался ДО первой отрисовки шрифта, т.е. мерил ещё НЕсжатые advance'ы против уже
  k-сжатого бокса -> лишний перенос. Прежняя (ошибочная) формула RowS маскировала это,
  давая по 5 единиц «слака» на символ.
  Решение: **сайт 2** — 6 байт `0x40b494` (`mov [ecx+0x294],eax`, сеттер шрифта из
  `SetFont`) -> `jmp cave2`, где после сохранённых байт зовётся `FUN_0041cc90(шрифт)`.
  Шрифт нормируется/масштабируется сразу при назначении, до любой раскладки.
- `[P]` **Причина №3 (ширина)** — advance'ы округлялись `FISTP` по одному: сумма по строке
  систематически больше k-масштаба (до +2%). В пещере теперь **накопление ошибки (carry)**:
  `v = a*k + e; n = round(v); e = v - n`. Сумма по всей строке точна (±1 единица).
- `[P]` `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py` (исторический скрипт сессии; в git не входит) (два сайта + блоб 192 Б в 0x4ebf12: cave1 0x00..0x7a, cave2 0x80..0xad,
  data 0xb0/0xb8). md5 патча `49976478943f0e2871872a2c6fb55f28`, откат `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py (исторический скрипт сессии; в git не входит) restore`.
- `[P]` Проверено 4K: диалог настроек — все 14 строк клавиш однострочные, видны все 4
  разрешения (в exepatch8 3-4-я строки съезжали); круг персонажа — блок 13 строк на тех же
  канвасных позициях, что и в 4:3.
- `[?r]` **Остаток по ширине: ~+0.33 канвасной единицы на символ (~3% на длинной строке).**
  Причина: движок использует ОДНО целочисленное поле и как межстрочный шаг, и как
  межбуквенный интервал (`RowS`). Для 4:3 нужен ровно `-3`, а в движковых единицах это
  `-2.25` — целым не бывает; ближайшее `-2` даёт `-2.67` в канвасе. `Leading` тут не участвует
  (эксперимент: `SetTextLeading(0)` ширину не изменил).
  → **Разбор и варианты решения вынесены в `docs/archive/TODO_FONT_WIDTH.md`** (следующий агент
  продолжает оттуда; там адреса, замеры, что не надо повторять).
- 4:3 не изменился: сетка строк/ширины в диалоге совпадают до сотых с `base8_43`;
  пиксельный диф `meanDelta=1.37, maxDelta=15` — это дизеринг/атмосфера кадра, не раскладка.


Замеры и коэффициент A — `docs/OBSERVATIONS_FONT.md`.


## 4x-листы шрифтов + коэффициент A (2026-11-15, вечер)
- `[U]` Задача от пользователя: «сделать upscale sidecar-текстуры шрифтов для 16:9 (начать с 2x)
  и подобрать коэффициенты в патче EXE, чтобы шрифт укладывался по ширине ± как в 4:3».
  По расчёту выбран **4x** (не 2x): 1 тексель листа → `k*sx = 2.81` экранных px; 2x даёт 1.41x
  (всё ещё растяжение), 4x — **0.70x** (уменьшение, полная резкость). 4x = степень двойки →
  `152*(k/4) ≡ 38*k` побитово, округления pitch/advance те же. Все листы остаются POT.
- `[U]` Апскейл делает пользователь (Upscayl), варианты в
  `scratch_dir('fonts_dst')/`; выбран **remacri-4x**.
- `[P]` `tools/fonts/gen_font4x.py` (PNG 4x → `.btf` шаблоном родного + `.fdf` с ×4 у `x,y,adv,pitch`;
  родные пары в `*_43`), `python tools/fonts/gen_font4x.py --install`. Круг-трип `.btf` побайтовый (0 diff).
  Артефакты: `resources/fonts4x/`.
- `[P]` **`tools/archive/exe_patch/exepatch10.py`**: те же сайты (`0x41ce31`, `0x40b494`), новая пещера 224 Б
  (`tools/exe/caves/fontscale4x.s` (источник в git; собираемые блобы — в paths.cache_dir('cave')) + `tools/archive/cave_gen/build_cave4x.py` (источник в git; собираемые блобы — в paths.cache_dir('cave'))): `k_eff = k*0.25`, **при `k == 1` — identity**
  (это критично: иначе 4:3 с родными листами получил бы текст в 0.25x; первый прогон это поймал),
  и `adv = (a_sheet - CORR)*k_eff + carry`, `CORR = 4*A = 1.3384`. Полярность проверки:
  `fld1; fcomip; je isone`, зажим `k>1 -> 1`.
- `[P]` Статически: пещера 224 Б влезает в zero-run 238 Б; константы (`c1024`,`c768`,`csheet`,
  `ccorr`) — последними, чтобы не платить padding раздела; из `cave2` убран guard `pitch!=0`
  (нужен был 8 байт), сам вызов `FUN_0041cc90` остался.
- Инструменты: `tools/archive/font_gen/fontqc.py` (источник в git; собираемые блобы — в paths.cache_dir('cave')) (QC апскейла), `tools/archive/font_gen/fontsrc_export.py` (родные PNG + сетка
  ячеек), `tools/fonts/gen_font4x.py`, `tools/archive/exe_patch/exepatch10.py`, `tools/archive/cave_gen/build_cave4x.py` (источник в git; собираемые блобы — в paths.cache_dir('cave')), `tools/measure/logrows.py` (сводка
  `HDINITBB` по классам pitch/RowS).
- `[P]` Состояние на диск: EXE = `exepatch10` (md5 `51f69974e1dada6e7b191961e853a1ef`),
  сайдкары `*_169` (4x remacri) + `*_43` (родные) в `data/textures`+`data/fonts`, `hd.cfg` =
  `3840 2160 32 1`, игра остановлена.
Base-требования к декомпиляту — `docs/PIPELINE.md`; запись о дефекте старой базы — `docs/PITFALLS.md`.


## Боевой HUD поверх recovered-базы (2026-11-15, вечер)

Другой агент написал `tools/pyc/decompile_lit.py`; выход — `decompiled_fixed/` (все 434 модуля).
Для `ui/battlemenu`: `grep -c ld_uncial` = 9 (в декомпиляте 0), пример
`self.StatDisplay = ui.BActionElement([30, 100, 300, 300, ui.GUI_TNORMAL, '', 0,
['', 'ld_uncial12', FONT_H, 1, 0, 0]], [], [])`; паритет против
`backup_orig/ui/battlemenu.pyc`: code objects 124 = 124, близость **0.9979**,
`sig=0`, `ops=9` (только артефакты `RETURN_VALUE` и LOAD_GLOBAL/LOAD_NAME) ->
база пригодна для правок. Сравнение делает `tools/pyc/parity_file.py (источник в git; собираемые блобы — в paths.cache_dir('cave')) <orig.pyc> <file.py>`.

- `[P]` Правка на recovered-базе (файл `patches/ui/battlemenu.py` =
  `decompiled_fixed/ui/battlemenu.py` + два хуна):
  1) `XLeft = 0 - sh` (`sh = hd_scale.HD_Shift()` в `try/except`, лог `XLEFT sh=`);
     весь левый BHUD-блок (underlay/hgauge/mgauge/spgauge/frame_l) едет целиком.
  2) новая ветка `if type == BCCT_TEMPLATE:` в `ModifyCallBackSel` ->
     `hd_scale.HD_EdgeFix(param, левый, правый)` ДО создания контролов
     (`bgui.CreateFromTemplate` зовёт callback с deepcopy шаблона, проверено:
     `decompiled_fixed/ui/bgui.py:34 modify_call_back(BCCT_TEMPLATE, main_template, param)`).
     Левый список: `mc_pullbtn`, `mc_pullbtn_bkg`, `btn_mainmenu`, `btn_disband`,
     `btn_retreat`, `btn_pause`, `btn_defatt`, `btn_move`;
     правый: `mini_map`, `frame`, `spells_border_zone`.
     **`console` в список НЕ входит**: её canvas-rect `[-434,90,450,428]` при
     `-sh` уходит за левый край целиком (полоска исчезает) — проверено расчётом.
- `[P]` Установка: `python3 tools/pyc/install_patch.py hd_scale ui/olmenu ui/battlemenu`
  (после правки путей в `tools/pyc/pycbuild.py`: `GAME_SCRIPTS`/`GAME_ROOT` были
  `config[game.root] (установка 'LethalDreams2', историческая)/...`, а `ENV PYTHONPATH` — с `\` и тоже LethalDreams2;
  теперь `config[game.root]/...` и прямые слэши, иначе воркер падает
  `ImportError: No module named os`).
- `[P]` Результат 4K (`scratch_dir('ld')/p4kbt4_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), сравнение с baseline `scratch_dir('ld')/base4kbt3_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)):
  левый BHUD-блок прижат к левому и нижнему краю (кроп `scratch_dir('ld')/cmp4k_left_new.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)
  0..720 x 1440..2160: артка рамки начинается у x=0; в baseline блок начинается
  у x=340), миникарта прижата к правому/нижнему краю
  (кроп `scratch_dir('ld')/cmp4k_right_new.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) 3120..3840); пропорции арта и кнопок = 4:3, растяжения нет.
  Консольная чёрная полоска у левого края осталась (её не сдвигаем).
- `[P]` Регрессия 4:3 (`scratch_dir('ld')/p43bt5_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) vs baseline `scratch_dir('ld')/base43bt2_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит),
  `tools/measure/region_diff.py ... battle`): `left_bhud`, `left_btns`, `console_strip`,
  `center_battle` = `mean=0 max=0 over8=0` (побитово идентично),
  `minimap` mean 4.2 / over8 3034 — только анимация содержимого миникарты.
- `[U]` Путь съёмки 4:3: нужен `-EnterCount 3` (третий Enter закрывает диалог
  «Мы настоятельно рекомендуем Вам пройти обучение», иначе бой не начинается:
  снимок остаётся на экране выбора персонажа).
- `[P]` `patches/hd_scale.py`: `HD_LOG = 0` и лог в `hdlog.txt` (приватный архив сессий 5.7z, entry LethalDreams/hdlog.txt; в git не входит) по **относительному**
  пути (рядом с игрой) — в прогоне `SCENE`/`XLEFT`/`HDMOVE` строк нет, лог
  отключён для установки. Для диагностики включать `HD_LOG = 1`.



## Консоль боя (левая полоса) — исправлена (2026-11-15, финал итерации)

- `[U]` «Исправляй левую черную панель с боевыми логами».
- `[P]` Правка по образцу EdgeFix: `console` добавлен в левый список `HD_EdgeFix`
  (`patches/ui/battlemenu.py::ModifyCallBackSel`). Расчёт подтверждён снимком:
  rect `[-434,90,450,428]` при сдвиге `-sh` даёт правый край engine
  `k*(16-170)+dx = 12.3 >= 0` — видима ровно та же полоска канвас 0..16 (правый
  border) с border-крышками, прижатая к левому краю экрана (screen ~46 px), не исчезает.
  Кадр `scratch_dir('ld')/bt4kn7_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит): большой чёрный прямоугольник исчез. При 4:3 сдвиг=0 — identity.
  Текст логов консоли (canvas x -402..-14) не видим НИ в 4:3, НИ в 16:9 — так же, как в эталоне.
- Состояние: `hd_scale.pyc` (EdgeFix+hover+GTH_F), `ui/battlemenu.pyc` (EdgeFix списки +console), — компилят `.pyc` в установке, в git не входит
  `ui/btutorial.pyc` (зонд BTRECT), `ui/olmenu.pyc`. `hd.cfg` = `3840 2160 32 1`, игра закрыта.



## Журнал боя (консоль) Tab в 16:9 — исправлено (2026-11-15, финал)

- `[U]` «по Tab журнал выезжает, ещё Tab — убирается; в 16:9 после двух Tab встаёт не
  туда, при одинарном Tab слишком близко к центру; сравни с 4:3. Чтобы Tab заработал,
  после входа в бой надо 6-10 раз подряд нажать Enter (пропуск первых уроков)».
- `[P]` Механизм: `OnOpenInfo` (`battlemenu.py`:1413; декомпилят модуля — локальный,
  в git не входит) ставит `CurrentStep = ConcoleStep(60) * (1-2*(st-1))`
 (открытие +60, закрытие -60 канвас/кадр); `Update` (1497) двигает rect консоли
  и останавливает по границам `rect['left'] >= 0` (открыто) и
  `rect['right'] <= MinConcoleWidth=16` (закрыто) — обе границы канвасные, без edge-поправки.
  Эталон 4:3 (зонд CONSOLE, Enter x12, Tab x3): OPEN `left=0,right=450`; CLOSE
  `right=16,left=-434` (`bt43t2` лог, снимки bt43t2_tab1..3).
- `[P]` Правка по образцу XLeft: в `Update` (патч `patches/ui/battlemenu.py`) границы
  сдвинуты на `-sh = -HD_Shift()`: OPEN stop `rect['left'] >= -sh` (snap `left=-sh`),
  CLOSE stop `rect['right'] = MinConcoleWidth - sh`. Скорость ConcoleStep=60 канвас/кадр
  не меняется — число кадров = 4:3. При 4:3 sh=0 → побитово оригинал. Зонды
  `CmdConsole`/`CONSOLE OPEN|CLOSE rect` (пишут только при HD_LOG=1).
- `[P]` Результат 16:9 (зонд, Enter x12 Tab x3, `bt4kn9`): OPEN `left=-170,right=280`
  → engine левый край k*(-170)+dx = 0 (прижато к краю экрана), ширина engine
  k*280+dx = 337.9 = 4:3 k*450; CLOSE `right=-154` → engine 12.3 = та же тонкая
  полоска у края, что и в эталоне. Снимки `scratch_dir('ld')/bt4kn9_tab1.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (открыто у левого края с
  border-крышками), `scratch_dir('ld')/bt4kn9_tab2.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (закрыто — тонкая полоска), `scratch_dir('ld')/bt4kn9_tab3.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)
  (опять открыто) — посадка совпадает с 4:3 пропорциями.
- `[P]` Съёмка: скрипт `tools/archive/probes/probe_loadshot.ps1` расширен `-Tabs N -TabDelay S` (keybd VK 0x09,
  снимок после каждого Tab). Путь боя: `-EnterCount 12` (6-10+ по слову пользователя;
  часть Enter уходит на страницы уроков; в 16:9 уроки встают на странице «Выберите
  главного персонажа, щелкнув…» — нужен клик по юниту в бою, скрипт его не делает).
- `[P]` `hd_scale._log` лимит строк поднят 4000→12000 (в 16:9 прогон упирался в лимит —
  зонды CmdConsole/CONSOLE не писались).
- `[P]` Регресс 4:3 (`scratch_dir('ld')/bt43t3_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) vs `scratch_dir('ld')/bt43n_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит), region_diff battle):
  console_strip mean=0.0025 max=1, left_btns mean=0.0006 max=1, left_bhud 0.006/16
  (анимация绿стрелок), center_battle — учебный диалог (Enter x12 оставляет урок) —
  не регресс патча. Состояние: `hd_scale.pyc` (HD_LOG=0, cap 12000, GTH_F, hover-сдвиг), — компиляты `.pyc` в установке, в git не входит
  `ui/battlemenu.pyc` (границы консоли -sh + зонды), `ui/btutorial.pyc`, `ui/olmenu.pyc`;
  игра закрыта, `hd.cfg` = `3840 2160 32 1`.
- Остаток: заголовок учебного диалога в 16:9 обрезан («1: Игровая камера и панели») —
  ширина окна 384 канвас при k-масштабе; центральная лента иконок selection приподнята
  (двойное k-сжатие Y); посадку иконок/консоли подтверждает пользователь в живом 4K.



## R/E/F1: финальное решение — обычная k-схема для квадратных диалогов (16.11.2026)
- `[U]` Претензия к предыдущей итерации: R/E «меньше, чем просил», нажатия по кнопкам
  этих экранов срабатывают не там, и «сделано слишком сложно» (Q-листы шрифтов).
- `[P]` Корень ошибки: шаблоны объявляют фон R/E/F1 rect'ом `[0,0,1024,1024]`, но
  ВИДИМАЯ композиция этих экранов — стандартный канвас 1024x768 (в 4:3 они заполняют
  экран целиком). Q отображал весь 1024x1024-квадрат в экранный квадрат `h x h`, т.е.
  масштаб `q = h/w = 0.5625` вместо канвасного `h/768`; картинка выходила в `k = 0.75`
  раза меньше 4:3 (рамка R ~65% высоты вместо ~86%). Клики не совпадали потому, что
  прямоугольники шли q-трансформом (`ECQ/EYQ`), а хит-тест/курсор — k-схемой (`EC/EY`).
- `[P]` Q-листы шрифтов были костылём под тот же неверный масштаб: движок берёт размер
  глифов из pitch листа (`SetFont` игнорируется), поэтому, чтобы глифы в квадрате были
  в `q`, приходилось сжимать листы в `q/k = 3/4`. При k-схеме этого не нужно.
- `[P]` Решение (единственная установленная правка — `patches/hd_scale.py`):
  `DIALOG_TEX = ['re_bkg.tga (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z)','encyclopedy_scr.tga (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z)']` + префикс `qrs_`; такой фон при
  `HD_K<1` НЕ идёт в `_is_bg` pass-through, а получает обычный `EC/EY/SC/SY`
  (метка `self.HDDBG` живёт на элементе, `_push` её уважает). Итог: экран рисуется
  ровно как в 4:3 (канвас 1024x768 → 2880x2160 при 4K) по центру, вписан по высоте,
  дети — той же k-схемой (клики/скролл/тултипы уже корректны). 4:3 не затронут:
  при `HD_K==1` и `InitBillboard`, и `_push` выходят до правки (identity).
  `bspellbook.py` (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git) / enmenu.py (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git) / help.py (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git) остаются ОРИГИНАЛЬНЫМИ (в игре они == backup_orig).
- `[P]` Проверено съёмкой probe_keyshot (16:9 3840x2160 F=1, сейв+последовательность):
  R — центральная 4:3-колонка, рамка на всю высоту, ярлыки/иконки на местах; клик
  `x142:616` в E открывает главу «Персонажи» (caption+desc на месте); help открывается
  кликом по «?» (R: `x462:659`, E: `x378:661`) — даёт qrs_*-оверлей в той же 4:3-колонке.
  Центр 16:9-кадра, сжатый до 1920x1440, совпадает с 4:3-эталоном по геометрии
  (различия — только AA/ресемплинг краёв и 4x-листы шрифтов, mean|dRGB| 13.6 на краях
  при noise baseline 1.0 на плоских участках; контуры всех элементов совпадают).
- `[P]` 4:3 регрессия (та же последовательность, base vs fix): mean|dRGB| 0.19–1.30,
  `px>40` = 0–1.06% (анимация chaos-фона и курсор); шаги E/F1/Tab — 0 отличающихся
  пикселей >40. По построению при k=1 правка не исполняется.
- `[P]` Удалено как негодное: `gen_fontq.py` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z), `add_qfonts_tt.py` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z), `PATCH_Q_MODE.md` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z),
  `resources/fontsq/`, `patches/ui/{bspellbook,enmenu,help}.py(.pyc)`, `config[game.root]/help_game.pyc (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит).keep`.
  В игре Q-сайдкаров не было (data/fonts и config[game.root]/data/common.tt (файл установленной игры, в git не входит) чистые). hd.cfg — `3840 2160 32 1`.
- `[P]` `player_dialog.tga` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z) (фон `talk_dlg`, диалог с NPC из `ui/btalk.py`) добавлен в
  `DIALOG_TEX` того же патча 16.11.2026: rect шаблона `[0,0,1024,1024]`, но арт
  (альфа-bbox) = canvas `x134..890 y6..643`, т.е. композиция тоже канвас 1024x768 —
  в 16:9 он шёл `_is_bg` pass-through и растягивался на всю ширину. Из остальных
  2048x2048 вне патча: `sb_bkg` не упоминается нигде (мёртвый файл: нет в `.tt`,
  скриптах, EXE/DLL), `ter_fort01` — текстура мира (`config[game.root]/data/objects_tex.tt` (файл установленной игры, в git не входит)), не UI.

## Краевой скролл оверленда: патч `ui/olcamera` (17.11.2026)

- `[U]` Симптом: «не работает скролл экрана мышью вниз; вверх, влево и вправо —
  работает». Проверка пользователя: загрузить сейв и двигать мышь к краям экрана.
  Подозрение на `h+1` (фикс мигания +1 px по высоте окна) — опровергнуто замером:
  при `h=2160` (`k=0.75`) порог тоже недостижим, т.е. нижний скролл сломан во всех
  16:9-конфигурациях `F=1`, и `h+1` к поломке не причастен (он остаётся нужным).
- `[P]` Механика и A/B-измерения — `docs/OBSERVATIONS_ENGINE.md` (раздел «Краевой
  скролл оверленда…») и `docs/PITFALLS.md` (пункт «Скролл вниз мышью не работает в 16:9»).
  Кратко: `ui/olcamera.pyc` регистрирует `h_MouseMove` на сырое движковое событие
  `WMO_MOUSEMOVE` → `ms_lastx/ms_lasty` = движковые координаты; пороги краёв
  (`>1021`, `==0`, `>765`) — канвасные. Движковый Y-диапазон = `768*k` = 576 < 765
  → нижняя полоса пуста.
- `[P]` Правка (единственная): метод `HDEdgeY()` = `hd_scale.SY(self.screen_size[1] - 1)`
  (движковая координата нижнего края канваса; `SY = k*v` при `F=1`, identity при `F=0`)
  и условие `if self.ms_lasty > self.HDEdgeY():`. `try/except` возвращает родной порог,
  правка не умеет ронять оверленд-апдейт. Файл: `release/patches/ui_olcamera.patch`
  (2 ханка, 16 добавленных строк) поверх базы `decompiled_fixed/ui/olcamera.py`.
- `[P]` Паритет базы перед правкой: `python3 tools/pyc/pycbuild.py parity ui/olcamera`
  = близость **0.9974**, расхождений sig=0, 1 ops-артефакт декомпиллятора
  (`h_togglezoom_d`: лишний `RETURN_VALUE`) → правка поверх базы безопасна (стандарт
  тот же, что `ui/olmenu` = 0.9952).
- `[P]` Регрессия 4:3: при `k=1` (`HD_SY=1`) `SY(v)=v` → порог побитово родный 765;
  логика движка и остальные три края не тронуты.
- `[P]` Замер патча (патч-сборка в scratch-дереве, зонд `y/x/thrY/pos`, загрузка сейва,
  курсор ОС по краям): порог `SY(765)=574`; мышь у нижней видимой строки (физический
  OS y=2159) → движковый `y=575>574` → камера `(55.00,31.73)→(55.00,60.00)` = **+28.27**
  по z, скролл вниз работает; верх/лево/право — работают в обеих сборках
  (`-27.8`, `-27.96`, `+27.96`).
- `[P]` Манифест: entry `ui/olcamera` (target `data/scripts/ui/olcamera.pyc`);
  base-печать md5 `f67ae9904032…` (11910 B, crc32 `25112d1b`, `.py` md5
  `4bf8df324ee6…` 8671 B, `pyc_source_mtime` 1794778203); pristine-копия установки
  `ba346c67b543…` (10669 B) = `installed_recorded_md5`. Кодовый ярус: 11 .pyc.
- Применение (каноническая команда):
  `python3 tools/apply_patch.py ui/olcamera && python3 tools/pyc/install_patch.py ui/olcamera --install`.
  Состояние до установки патча в игру — `DRIFT` (в установке pristine `ba346c67…`);
  после `--install` — `MATCH` по base-печати.

## Боевая камера: тот же краевой скролл (17.11.2026)

- `[U]` После подтверждения правки в оверленде («На глобальной карте, сразу после
  загрузки сохранения, скролл работает») — «В бою всё так же не работает. Проверить
  можно начав новую игру и 15 раз нажав enter (для скипа обучения)».
- `[P]` Тот же механизм во втором модуле: `config[game.root]/data/scripts/battlecamera.pyc`
  (`BattleCamera.B_Init`) регистрирует `h_MouseMove` на сырое движковое событие
  `WMO_MOUSEMOVE` (строка 141), `screen_size = [1022, 766]` (строка 137), пороги
  в `hB_Update` (строки 335/341) канвасные. Движковый Y-диапазон `768*k` = 576 < 765
  → скролл вниз в бою мёртв; верх/лево/право работают. Правка: тот же `HDEdgeY()`
  (`hd_scale.SY(screen_size[1]-1)`) и условие `ms_lasty > self.HDEdgeY()`.
- `[P]` Патч `release/patches/battlecamera.patch` (2 ханка, +17 строк) поверх базы
  `decompiled_fixed/battlecamera.py`; паритет базы: `python3 tools/pyc/pycbuild.py parity
  battlecamera` = **PARITY-EQUAL[TOLERANT]** (47 кодовых объектов, операции совпадают).
  Манифест: entry `battlecamera` (target `data/scripts/battlecamera.pyc`), base-печать
  md5 `ddb3e6fa629e…` (25320 B, crc32 `d3218ac8`, `.py` md5 `63990fd1eeb4…` 19049 B,
  mtime 1794778203); pristine-копия `9efdb4d1dae9…` (23289 B) = `installed_recorded_md5`.
  Кодовый ярус: 12 .pyc.
- `[P]` A/B-замер в scratch-дереве (новая игра + 15 Enter → бой; зонд `y/x/thrY/thrX/pos`
  в `hB_Update`, курсор ОС по краям, прогон `probe_loadshot` + mover):
  патч `bc1`: порог `SY(765)=574`, мышь у нижней видимой строки (`y=575`) → камера
  `(93.50,122.62)→(113.43,132.07)` = скролл вниз работает; верх `y=0` →
  `(93.46,122.49)→(87.27,104.83)`; лево `x=0` и право `x=1023>1021` — движутся.
  Контроль базы `bc2`: тот же `y=575` при пороге 765 → камера стоит за 1.5 с
  (`(87.48,104.81)→(87.48,104.81)`, dpos 0); верх/лево/право в контроле работают.
  Смещения камеры в бою идут вдоль её направления (`MoveAlignedXZ`), поэтому dpos
  не чисто по z, как в оверленде.
- Применение: `python3 tools/apply_patch.py battlecamera && python3 tools/pyc/install_patch.py battlecamera --install`.

### Установка обоих патчей (17.11.2026, после подтверждения пользователя)

- `[U]` Оверленд: «На глобальной карте (сразу после загрузки сохранения) скролл
  работает»; бой (патченная сборка в scratch-дереве, новая игра + 15 Enter):
  «всё работает. ставь оба патча».
- `[P]` Установка: `python3 tools/apply_patch.py <м>` (base-печать == манифест) →
  `python3 tools/pyc/install_patch.py <м> --install`. Записаны
  `data/scripts/ui/olcamera.pyc` md5 `f67ae9904032…` и
  `data/scripts/battlecamera.pyc` md5 `ddb3e6fa629e…` (обе == base-печать).
  Снапшот md5+mtime до/после: diff = ровно две эти замены, остальная установка
  не тронута. `check_state --quick`: rc 0, 2 известных дрейфа (cursor, olmenu);
  `--full`: rc 0, нарушений 0; `--untouched`: rc 0 (CODE 17).
## Тултипы у курсора в 16:9: патч-обёртка `CalcPopupPos` (17.11.2026)

- `[U]` «Тултипы в игре в 16:9 возникают в верху экрана, а не под курсором (как
  было на 4:3). Уточнение: достаточно сделать так, чтобы тултипы были рядом с
  курсором, попиксельная точность не нужна».
- `[P]` Причина — расчёт позиции popup-блока `ui/ui.CalcPopupPos`
  (`decompiled_fixed/ui/ui.py` строка 199): он проверяет 4 кандидата (право-низ,
  право-верх, лево-верх, лево-низ от курсора) против `GetScreenRect()` = канвасного
  бокса `(0,0,1024,768)`. Круглые иконки левой панели — дети `overland_dlg_l` с
  rect `[0,256,256,512]` (шаблон `gui_templates.olmenu_dlg`, см. OBSERVATIONS_UI.md):
  в 16:9 они после `HD_EdgeFix`+`HD_Shift()` стоят при `x<0`, и все кандидаты дают
  отказ (`left<0`), а при курсоре у нижней панели отказ и по `bottom`; оригинал
  возвращает базовый rect `(1,1)` — тултип рисуется у верха экрана, независимо от
  курсора (замер `tt4kb` в 16:9: текст на canvas x≈14..218, y≈13..21 при курсоре
  на любой из трёх иконок; в 4:3 база `tt43b` — текст у курсора).
- `[P]` Правка — обёртка `CalcPopupPos` в авторском модуле `patches/hd_scale.py`
  (`ui.CalcPopupPos = CalcPopupPos` в `install()`): кандидаты проверяются против
  **видимого** канвас-бокса 16:9 `x = -HD_DX/HD_K .. (1024-HD_DX)/HD_K`, `y = 0..768`
  (строгие границы, как `IsRectInRect`: `right < rightD`, `bottom < bottomD`); если
  ни один не влез — тултип ставится у курсора (кандидат право-низ) и клампится
  в бокс. При `HD_K == 1` — вызов оригинала, 4:3 побитово.
- `[P]` A/B в scratch-дереве `tt_fix` (полная копия установки; прогон
  `tools/probes/probe_hovershot.ps1`, курсор мыши ОС наводит probe):
  - 16:9 (`tt4kf2`, курсор canvas −109/572, −145/740, −119/695 — сдвиг панели
    `HD_Shift()` = −170 учтён): текст тултипа на canvas −93..−15 (левая панель,
    y 626..634), −131..71 (y 690..711), −155..−149 (y 733..737) — offsets от
    курсора +16/+55, +13/−30, +14/−30, то есть тултип у курсора; до правки (`tt4kb`)
    он был прибит к верху.
  - 4:3 (`tt43f` vs база `tt43b`): текст тултипа побитово на тех же canvas-координатах
    (stats y 630 x 758..952; close y 711..718 x 49..242; diplomacy y 662..672
    x 64..206) — патч не трогает 4:3.
- `[P]` Установка (`python3 tools/pyc/install_patch.py hd_scale --install` после
  подтверждения съёмки в 4K): `data/scripts/hd_scale.pyc` md5
  `12d38f093d2cacd8b2892832e13649d5` (25618 B, crc32 `84c7ece3`, mtime
  `1794819372`), `.py` md5 `dc8314f90af0…` (31914 B). Манифест: entry `hd_scale`
  перечислен по новой сборке. Снапшот md5+mtime до/после: diff = ровно эта одна
  замена (файлы `hd_res.Prepare()` — 9 md5-совпадающих, mtime-дрейф ожидаемый;
  `hdlog.txt` из дерева убран). Живой прогон после установки (`tt4kfix`):
  «Главное меню (Escape)» и «Меню исследования» в левом нижнем углу у курсора;
  `check_state`: quick rc 1 (cursor DRIFT_RELEASE_AHEAD, olmenu DRIFT_KNOWN),
  full rc 0, untouched rc 0.
- `[P]` Ограничение: у правого края нижней панели (иконка close, canvas x≈−145)
  текст тултипа клампится к левому краю видимого бокса и частично ложится на
  саму панель — по правилу пользователя («рядом с курсором, точность не нужна»)
  принимается; центрирование тултипа над иконкой — отдельная итерация.


## Боевая камера: MMB-обзор и зум колесом (18.11.2026)

- `[U]` «В боевом режиме не работает средняя кнопка мыши, а колесо наклоняет камеру.
  Нужно: при зажатой MMB движение влево-вправо вращает камеру, вверх-вниз наклоняет,
  с небольшими мёртвыми зонами, и чем дальше отклонение по горизонтали — тем быстрее
  вращение. Колесо вверх — приближать, вниз — отдалять».
- `[P]` Реализация — только Python-слой, EXE/DLL не тронуты: `patches/battlecamera.py`
  (база `decompiled_fixed/battlecamera.py`), 5 ханков, `release/patches/battlecamera.patch`.
- `[P]` **MMB-обзор**: `B_Init` добавляет `mmb_look`, `mmb_anchor`, `mmb_dz = 6.0`,
  `mmb_omax = 260.0`, `mmb_sens = 0.012` (рад/ед.), `mmb_sens_y = 0.18` (град/ед.).
  `h_MouseMove` ловит `flags & globalscript.MK_MBUTTON` (16): при нажатии ставит
  якорь в точке нажатия, при отпускании — снимает режим. `hB_Update`, пока `mmb_look`:
  - `ox`/`oy` = отклонение курсора от якоря в **движковых** координатах, с насыщением
    на `±mmb_omax`;
  - мёртвая зона `±mmb_dz` по каждой оси;
  - рыскание: `RotateAroundY(-mmb_sens*(|ox|-dz)*time)` — вправо от якоря = поворот
    направо, влево = налево (проверено: φ падает при drag вправо, растёт при drag влево,
    как у клавиши «turn_left»);
  - наклон: `Rotate(0, -pitch, 0)` с клампом `cur_pitch ∈ [min_pitch, max_pitch]` =
    [-60, -45] (вверх = обзор поднимается к -45, вниз = к -60);
  - краевой скролл **подавляется** на время удержания MMB (иначе у края экрана шли
    одновременно вращение и перенос).
- `[P]` **Зум колесом**: `h_MouseWheel` вместо прежнего наклона/сдвига меняет
  `zoom_up` (высота камеры над землёй), шаг `zoom_step = 3.0` на щелчок (120 ед.),
  диапазон `[up_min, up_max] = [min_up, 33.5]`. `zoom_active` включается первым
  щелчком (старт от текущей высоты — без прыжка); `hB_Update` при `zoom_active`
  держит `prevYDelta = zoom_up`, terrain following сохранён. До первого щелчка
  поведение высоты — родное (`prevYDelta` из позиции, клампы `min_up`/`max_up`),
  т.е. 4:3 и базовое поведение не меняются, пока зум не тронут.
- `[P]` Почему зум сделан высотой, а не проекцией: в EXE fov = `camobj+0x144`, из
  Python недоступен; `SetScale` (единственный «масштабный» метод) на рендер боевой
  камеры не влияет (замер A/B), `SetOrtho` сбрасывает камеру в (0,0,0). Разбор —
  `docs/OBSERVATIONS_ENGINE.md`, «Боевая камера: API, единицы и зум».
- `[P]` Проверка в scratch-дереве (`tex_hook_tree`, полная копия установки; прогон
  `tools/probes/probe_camera.ps1`, окно 3840x2161 F=1):
  - зум: `zoom_up` 8.25 → 6.50 (3 щелчка вверх) и 6.50 → 15.50 → 24.50 → 33.50 (вниз),
    `cam.GetPos().y` 10.86 → 36.11; снимки: персонаж ~180 px (вверх) и ~40 px (вниз)
    против ~130 px базового, наклон и UI не меняются, артефактов нет;
  - MMB: якорь/отпускание, φ по `GetDirection()` — drag вправо: 108° → -77° (поворот
    направо, мир уехал влево), drag влево: -77° → 108° (налево), наклон упёрся в
    клампы -45/-60; интерфейс на месте;
  - ошибок Python нет (`PythonLogFile` — только доброкачественные `ImportError`
    деревьев), `check_state` после установки: `--untouched` rc 0, `--full` rc 1
    (единственное нарушение — известный cursor-дрейф).
- `[P]` Установка (18.11.2026): `data/scripts/battlecamera.pyc` md5
  `91be96e74662448a5672b3c6edcace28` (27536 B, crc32 `e143e240`, mtime `1794778203`),
  `.py` md5 `5f757d07164f1fb54bd568c47cd9ea02` (25459 B). Снапшот md5+mtime до/после:
  diff = ровно эта одна замена. Каноническая сборка: `tools/apply_patch.py battlecamera`
  → `.py`/`.pyc` md5 == манифест; `tools/release/update_entry.py battlecamera` —
  пересборка `.patch` + записи манифеста.
- `[P]` Уроки «Игровая камера и панели» **не снимаются Enter'ом** (15 и 30 Enter не
  помогли): урок ждёт действия игрока, кнопка «Далее» на нём заблокирована
  (`btutorial.EnableNext(0)`), а «Игра в паузе» держит сцену. Камера при этом
  обновляется (`hB_Update` идёт), поэтому замеры камеры на уроке валидны.
