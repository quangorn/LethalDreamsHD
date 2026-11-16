# Lethal Dreams: The Circle Of Fate — запуск через DXWrapper

> **ВНИМАНИЕ (2026-11-14):** этот файл — архив прошлых исследований. Верить его пунктам
> на слово нельзя: часть утверждений не проверена и/или устарела. Актуальные
> **проверенные** знания ведутся в `docs/NOTES.md` (файл удалён; содержание распределено по docs/**, указатель docs/INDEX.md); этот файл — только исток и улики.

Игра: `config[game.root]` (Boolat LLC / Russobit-M, 2003), PE32 (i386), GUI.

## Что найдено в exe (RE: Ghidra/REA)

Импорты `LETHALDREAMS.EXE`: `python22.dll`, `config[game.root]/dsound.dll` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) (1 функция, ординал 1 = DirectSoundCreate), `WINMM`, `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `ole32`.
**Прямых импортов DirectX-графики нет**: нет `ddraw`, `d3d8`, `d3d9`, `dinput`.

Рендер — OpenGL:
- `FUN_00478230` → `LoadLibraryA("OpenGL32.dll")` (системная библиотека Windows, не файл проекта), `LoadLibraryA("GDI32.dll")` (системная библиотека Windows, не файл проекта), затем динамическая загрузка всех `gl*` через `GetProcAddress` (`FUN_00472c90` регистрирует сотни имён: `glBegin`, `glLightModelf`, `wglGetExtensionsStringARB`, `GL_ARB_multitexture`, `GL_EXT_texture_compression_s3tc` …).
- Т.е. путь перехвата — прокси `opengl32.dll` (системная библиотека Windows, не файл проекта), а не DXWrapper.

Ключевая цепочка разрешения:
1. `FUN_00478230(w, h, bpp, fullscreen)`:
   - перечисляет режимы: `EnumDisplaySettingsA(iModeNum)` → список в объекте (смещение `+0x48`);
   - `LoadLibraryA("OpenGL32.dll")` (системная библиотека Windows, не файл проекта);
   - вызывает `FUN_00477de0` = `SetDisplayMode`.
2. `FUN_00477de0(w, h, bpp, fullscreen)`:
   - если fullscreen: собирает `DEVMODEA` (bpp = `param_3*8`), `ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN)`; при неудаче выходит;
   - `SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, w, h, 0)` → **размер окна = размер рендера**;
   - создаёт GL-контекст, печатает `GL_VENDOR/GL_RENDERER/GL_VERSION/GL_EXTENSIONS`.
3. Окно: класс `LethalDreamsWndClass`, стиль `WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN` (0x86000000), ex-style `WS_EX_APPWINDOW`, **без рамок и заголовка**, стартовый размер 100x100 (`FUN_00424910`).
4. `WndProc` = `FUN_004245f0`: на `WM_SIZE` (0x10) делает `ShowWindow(h, 6)` (=SW_MINIMIZE) и `FUN_00424570` — **уничтожает GL-объект/рендерер**. Проверено экспериментом: внешний ресайз окна через `SetWindowPos` приводит к минимизации и слому контекста. Значит «просто растянуть окно» (DxWrapper `ForceWindowResize`, Borderless Gaming, SRWE) ломает игру; размер задаёт только сам движок.

Настройки хранятся в реестре: префиксы `SOFTWARE\Boolat llc` / `\Boolat Games\` / `Lethal Dreams\` (строки в `lethaldreams.dll` и exe). Ключи настроек — имена вида
`R_RESOLUTION`, `R_BITPERPIXEL`, `R_FULLSCREEN`, `R_SHADOWS`, `R_FASTSHADOWS`, `R_MUSIC`, `R_VOLUME`, `R_MOUSE_SENS`, `R_KEYBOARD_SENS`, `R_HIDE_MINIMAP`, … (注册руются в `FUN_00461c70`). Сейчас ключей в реестре нет — игра стартует с дефолтами и даёт **1024x768**.

Python-слой движка (`.pyc` в `data/scripts`): `SetDisplayMode`, `GetScreenSize`, `UpdateCfg`; диалог настроек — `data/scripts/ui/bpref.pyc`, дефолты — `data/scripts/cfg/cfg_default.pyc` (в нём есть `R_RESOLUTION`, `R_FULLSCREEN`).

Видео вставок — DirectShow/WMV (`data/video/*.wmv`, окно класса «ActiveMovie Window»), звук — DirectSound.
`BTexturer.dll` требует `MSVCR70/MSVCP70/MSVCR70D` (VC .NET 2002 CRT, их нет в системе) — игра стартует и без них, загрузка дороги идёт по надобности.

## DXWrapper: что он может здесь

Ставится: `config[game.root]/dxwrapper.dll` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) + `config[game.root]/dsound.dll` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) (stub) + `config[game.root]/dxwrapper.ini` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) в папку игры. Проверено: лог `config[game.root]/dxwrapper-lethaldreams.log` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит), «DxWrapper loaded by stub: config[game.root]/dsound.dll (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит)», GPU RTX 4080, Windows 10 19041.

Возможности, применимые к этой игре: звук (`EnableDsoundWrapper`, лимиты буферов), ввод, `WinVersionLie`, `SingleProcAffinity`, `ResetScreenRes` (восстановить разрешение при выходе), ASI/`LoadCustomDllPath` (загрузить свой DLL в процесс).

**Разрешение задать нельзя**: `CustomDisplayWidth/Height`, `EnableWindowMode`, `DdrawOverrideWidth`, LegacyD3DResolutionHack работают только поверх ddraw/d3d8/d3d9. У игры этих DLL нет.

## Разрешение — через Python-конфиг (найден основной путь)

Игра = C++ движок + скрипты **Python 2.2** (`python22.dll`, magic `0xED2D` = стандартный 2.2).
Скрипты — `data/scripts/**/*.pyc` (434 файла). Исходники восстановить можно: `uncompyle6`
поддерживает Python 2.2, декомпиляция работает без правок — скрипт `decompile_all.py`,
результат в `decompiled/`. Символы в `.pyc` есть полностью: имена функций, локальных
переменных, ключей, все константы и строки; нет только комментариев и форматирования.
Мини-парсер marshal — `pyc_scan.py`.

Ключевые файлы:
- `cfg/cfg_default.py` (в игре — `cfg_default.pyc`, дефолтный профиль):
  `R_RESOLUTION = [[640,480,0],[800,600,0],[1024,768,1],[1280,960,0]]`,
  `R_BITPERPIXEL = [[16,0,0],[32,0,1]]`, `R_FULLSCREEN = 1`.
  Третий элемент списка = признак «дефолт». Отсюда список разрешений в меню настроек.
- `ui/bpref.py` (диалог настроек) строит радиосписок из `len(preferences['R_RESOLUTION'])`,
  подписи `'WxH'` — список динамический, добавлять новые режимы можно.
- `globalscript.py`: `UpdateConfig()` сравнивает текущий и новый режим и вызывает
  `gamemodule.SetDisplayMode(x, y, bpp/8, R_FULLSCREEN)`, где `x,y = R_RESOLUTION[i][0..1]`.
- `ExecConfig()` = `import cfg_UserName` (UserName = `default`) и `UpdateConfig()`.

Механизм «конфига»: Python 2.2 при `import` берёт `.py`, если он новее `.pyc`.
Значит `data/scripts/cfg/cfg_default.py` со свежей mtime исполняется вместо `.pyc`
(и компилируется в `.pyc` — оригинал забиракаплен в `backup_orig/`).
Проверено экспериментом: после запуска игры `cfg_default.pyc` пересоздан движком
(8750 → 8829 байт, mtime = время запуска), т.е. наш `.py` реально исполняется.
Генератор: `python3 make_cfg.py 3840 2160 [--fs 1]` — добавляет режимы
1920x1080 / 2560x1440 / 3840x2160, помечает выбранный дефолтом, ставит `R_FULLSCREEN=0`
(чтобы desktop не переключался) и кладёт файл в игру.

Важно: список разрешений в **меню настроек** строит движок из `PrefObj.ExportPref()`
(`ui/bpref.py: preferences = copy.deepcopy(PrefObj.ExportPref())`), а не из `cfg_default`.
Поэтому новые разрешения (`1920x1080`, `3840x2160`) в меню не появляются — их список
формирует C++ движок (свои `EnumDisplaySettings`-фильтры, максимум 1280x960).
Меню и не нужно: навешиваем вызов на старт.

`make_cfg.py` генерит два файла и ставит в игру:
- `cfg/cfg_default.py` — список `R_RESOLUTION` + `R_FULLSCREEN` (конфиг),
- `globalscript.py` — к `ExecConfig()` (вызывается из `BDomain.__init__`) добавлен
  `ApplyResolution()`, который берёт режим с флагом `1` из `R_RESOLUTION`
  и bpp из `R_BITPERPIXEL` и зовёт `gamemodule.SetDisplayMode(w, h, bpp/8, R_FULLSCREEN)`.
`uncompyle6` восстанавливает `return` на уровне модуля (компилатор движка его
пропускает, стандартный парсер Python 2 — нет). Файл `.py` с такой строкой ломает запуск
(игра выходит кодом `0xFFFFFFFF`). `make_cfg.py` вырезает `return` из колонки 0; синтаксис
проверяется `python2.7 -m py_compile` (в Cygwin есть `/usr/bin/python2.7`).

Разрешение применяется при смене в диалоге настроек (после OK → `UpdateConfig()`),
на старте движок использует свой дефолт (окно 1280x960, 1280x960 = максимум из `.pyc`).

Масштаб UI: координаты в `ui/gui_templates.py` зашиты под канвас **1024x768**
(буквально `1024 - 256`, `768 - bm_st_h`, диалоги `512x1024`), движок масштабирует UI как `w / 1024`.
Поэтому при рендере 3840x2160 масштаб 3.75 — меню не влазит в экран;
1920x1080 → 1.875 (штатно), 1280x960 → 1.25. Вывод: рабочий максимум по UI ≈ 1920x1080,
остальное — апскейл ТВ (fullscreen-режим с `ChangeDisplaySettings`).

Подтверждено экспериментом (пользователь): в **4:3 всё помещается**. Значит UI масштабируется
по ширине, вертикальный канвас 768 режется в 16:9. Целевой режим: **1920x1440**
(`w/1024 = 1.875`, `768 * 1.875 = 1440` = высота окна), fullscreen через
`ChangeDisplaySettingsA` (desktop переходит в 1920x1440, TV/GPU апскейлит до 4K),
возврат desktop при выходе — `ResetScreenRes = 1` в `config[game.root]/dxwrapper.ini` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит) (`[Compatibility]`).

запрошенным (1280x960); до этого окно 1280x960 показывалось как 1792x1344 (виртуализация DPI).

## Подмена `python22.dll` и раскладка стандартной библиотеки

- Оригинальная DLL — альфа Python **`2.2a4+`** (строка версии в DLL, copyright 2001), магия `.pyc`
  `0xED2D` = 60717 (серия 2.2). Бэкап: `backup_orig/python22_2.2a4.dll`.
- В игре стоит **`python22.dll` из Python 2.2.3** (847920 байт, сборка 30.05.2003) — самый новый
  совместимый: 2.3 имеет магию 60720, оригинальные `.pyc` движка он не прочтёт. Все 51 символ
  C-API, импортируемый exe, в 2.2.3 есть (проверено по экспортам), зависимости новой DLL
  (`MSVCRT/kernel32/user32/advapi32/shell32`) на месте.
- Финальный 2.2.3 при `Py_Initialize` требует `scratch/rootpyc/site.py` (источник в git)/`config[game.root]/Lib/codecs.py` (файл установленной игры, в git не входит), которых в альфе не было.
  Вся stdlib 2.2.3 (694 модуля) лежит в `config[game.root]\Lib` (распаковка
  `Python-2.2.3.exe` через 7z: `py223.dir`). Находящийся там вариант
  (`Lib`) работает: базовый прогон с оригинальными `.pyc` — desktop/окно корректны, **меню целое**.
- Офлайн-анализ: `py223.dir\PYTHON.EXE` = Python 2.2.3 (даёт честный `dis`
  для сравнения байткода `.pyc` движка и компилятов из `.py`).
- Импорт stdlib движком не берёт подложенные `.py` (хуки в `scratch/rootpyc/site.py` (источник в git), `Lib/os.py`,
  `data/scripts/os.py` не исполнялись — логи пусты при работающей игре). Исследования
  декомпиляции и перехвата ошибок описаны в `RESEARCH_PY_DECOMPILE.md`.

## Принятый механизм: `.pyc` собирается офлайн (проверено live-прогоном)

Магия `.pyc` локального Python 2.2.3 (`py223.dir\PYTHON.EXE`) = `0xED2D` =
магия скриптов игры. Компилятор движка (2.2.3) идентичен нашему, поэтому `.pyc` можно собирать
офлайн и класть в `data\scripts\**` — движок load'ит его как есть. Проверено: тело установленного
`ui.pyc` совпадает с собранным байт-в-байт (отличается только mtime заголовка), меню строится,
выход `0x0`. `.py` рядом с `.pyc` не оставляем (иначе движок перекомпилирует, и его SyntaxError
нам не виден).

Декомпилят перед сборкой нормализуется (`pycbuild.py`): вырезается заголовок uncompyle6,
module-level `return`, и главное — байтовые литералы `b'...'` (синтаксис Python 3.11+,
Python 2.2 их не парсит) превращаются в обычные строки `'...'` с сохранением байтов cp1251.
Из-за этих литералов `ui/bgui.py` (каркас UI) вообще не компилировался движком → импорт обрывался
→ серый экран. После нормализации все 434 модуля компилируются 2.2.3 (`pycbuild.py gate`).

Тулчейн: `pin_decompiled.py` (снимок декомпилята + манифест), `pycbuild.py` (normalize/gate/parity/
build/install/uninstall), `tools/pyc/py22_worker.py` (режимы compile/build/dis-py/dis-pyc на 2.2.3),
`pyc_norm.py` (нормализация сравнения байткода), `restore_res.ps1` (вернуть desktop 3840x2160@120
после fullscreen-прогона). Рабочий интерпретатор требует
`PYTHONPATH='py223.dir;config[game.root]\Lib'`.

100% совпадения байткода с оригинальным `.pyc` не требуется и невозможно: оригиналы компилировались
альфой `2.2a4+` (другие peephole-оптимизации, порядок констант, `-x` как константа, отсутствие
`__name__/__module__` в теле класса, иная длина кода → jump-адреса). Паритет — диагностика
семантического дрейфа декомпиляции; критерий приемки: компиляция 2.2.3 + высокий паритет с
объяснимыми расхождениями + live-прогон (меню, отсутствие `0xFFFFFFFF`).


DXWrapper умеет писать байты (`[WriteMemory]`: пары `AddressPointer`/`BytesToWrite`,
`PatternString` = sigscan, поэтому ASLR не мешает; проверка через
`VerificationAddress=0xFF/VerificationBytes=0xFF`, `ResetMemoryAfter=0`).

Точка: `FUN_00478230` перед вызовом `SetDisplayMode` (`FUN_00477de0`), VA `0x478343`,
оригинальные 19 байт (уникальны в файле):
```
8b 55 18   mov edx,[ebp+18]   ; fullscreen
8b 4d 10   mov ecx,[ebp+10]   ; h
52         push edx            ; arg5 = fullscreen
8b 55 0c   mov edx,[ebp+0c]   ; w
89 46 3c   mov [esi+0x3c],eax ; сохранить HDC — трогать нельзя
8b 45 14   mov eax,[ebp+14]   ; bpp
50 51 52   push eax, ecx, edx ; arg4=bpp, arg3=h, arg2=w
8b ce      mov ecx,esi        ; this (0x478356 — не трогать)
e8 83 fa ff ff  call 0x477de0 (0x478358)
```
In-place патч (19 байт, kave не нужен): `push 0` (fs=0), `push 4` (32bpp),
`mov ecx,imm32`=h, `mov edx,imm32`=w, `mov [esi+0x3c],eax`, `push ecx`, `push edx`.
Генератор — `приватный архив сессий Project/1.7z, entry LethalDreams/make_patch.py` (исторический скрипт сессии; в git не входит). Не применён: байтовый путь не нужен, работает Python-конфиг.

## Масштаб UI: Python-слой (цель — честный 4K + полосы по бокам)

Целевая схема: рендер `3840x2160` (16:9, мир на весь экран), UI целиком и крупно
в области 4:3, по бокам чёрные полосы.

Мост Python↔движок для UI (всё в `ui/ui.py`, класс `BActionElement`):
- `InitBillboard(...)` → `BillBoard.Set(x, y, prior, w, h)` + текст
  (`SetFont(size)`, `SetTextLeading`, `SetTextRowSpacing`, `SetTextOffset`) — начальные позиции;
- `BActionElement.SetPos(name, x, y, w, h)` → `BillBoard.Set(...)` — все перемещения
  (через него идут `bgui.BGraphControl.SetControlRect`, `cursor.py`, `mainmenu.py`);
- `BActionElement.GetPos` возвращает Python-значения (`param['x'..]`), они остаются
  в канвас-единицах → вся логика раскладки (`CenterControlEx`, `ArrangeControls`,
  `PtInRect`) не трогается;
- мышиные координаты движок даёт в канвас-единицах → обрабочики `cursor.py`
  (`h_MouseMove/LBDown/RBDown/LBUp/RBUp/Wheel/LDblClk`) переводят их обратно.

Формула: движок рисует канвас с `s = w/1024`. Передаём движку координаты `k*c + d`,
где `k = h*1024/(w*768)`, `d = (1024-1024k)/2` по X и `(768-768k)/2` по Y.
Экранное масштабирование UI = `k*s = h/768`正好, высота канваса = высоте окна;
по бокам полосы `(w - k*1024*s)/2 = (3840-2880)/2 = 480 px`, центрирование за счёт `d`.
Для `3840x2160`: `k = 0.75`, `dx = 128` канвас-единиц, `dy = 0`; для `1920x1440`: `k = 1` (nothing changed).
Мышиные координаты: `c = (m - d)/k`.

 Реализация в `make_cfg.py`:
- `globalscript.py`: `ApplyResolution()` считает `HD_K/HD_DX/HD_DY`, пишет их в лог;
- `ui/ui.py`: правки `BillBoard.Set`/`SetFont`/`SetTextLeading`/`SetTextRowSpacing`/`SetTextOffset`
  и `SetPos` (значения оборачиваются в `int()` — движок принимает целые, float ломает отрисовку:
  был серый экран без меню);
- `ui/cursor.py`: во всех 7 обработчиках мыши `mx = (mx - HD_DX)/HD_K`, `my = (my - HD_DY)/HD_K`.
Ставится в `data/scripts/ui/ui.py`, `data/scripts/ui/cursor.py` (оригинальные `.pyc` —
`backup_orig/ui.pyc`, `backup_orig/cursor.pyc`).

## Текущее состояние (2026-11-14): точечный накат патчей

**Механизм полной переустановки (`install-all`, 434 `.pyc`) ОТМЕНЁН.** Полный накат декомпилята
ломает старт («Error while starting LethalDreams»), а масштабные правки прошлого агента рисуют
меню криво. Всё восстановлено к оригиналам (`restore-all`), проверено байт-в-байт.

**Рабочая база:** оригинальные `.pyc` + `python22.dll` 2.2.3 + `Lib/` (stdlib 2.2.3).
Запуск стабилен, fullscreen 1280x960 (движковый дефолт), меню целое.

**Команды (`pycbuild.py`):**
- `restore-all` — вернуть все оригиналы;
- `install-min` — **текущий метод**: ставит ровно 2 файла:
  - `gamescript.pyc`: в `Initialize()` после `import units_base, spells, ui` вставляются
    `import hd_res`, `w, h, bpp, f = hd_res.Read()`, `gamemodule.SetDisplayMode(w, h, bpp // 8, f)`;
  - `hd_res.pyc` — генерируется из `hd.cfg` (`hdpatch.hd_res_src`), `Read()` в рантайме открывает
    `hd.cfg` из корня игры. Смена разрешения = правка файла + перезапуск, **без перекомпиляции**;
- `install-hd` — полная версия (5 модулей: gamescript, globalscript, ui/ui, cfg/cfg_default,
  hd_res) со масштабированием UI; меню кривое, накатывать только по частям и проверять каждый шаг.

**Важные факты:**
- **Оригинальный `gamescript.pyc` НИКОГДА не зовёт `SetDisplayMode`** (строки нет в .pyc);
  стартовый дефолт движка — 1280x960 fullscreen. Шаблон
  `gamemodule.SetDisplayMode(1024, 768, 4, 0)` из `приватный архив сессий Project/1.7z, entry LethalDreams/_gs_min.py` (исторический скрипт сессии; в git не входит) — **старый черновик, его нет в
  декомпиляте**; `приватный архив сессий Project/1.7z, entry LethalDreams/_gs_min.py` (исторический скрипт сессии; в git не входит) как референс не использовать.
- `hd.cfg` — **одна строка** `W H BPP F` (F: 0=окно, 1=fullscreen) = стартовый режим.
  Многострочный список бесполезен: в меню настроек новые разрешения не появляются — список
  строит C++ движок из своих `EnumDisplaySettings` (макс. 1280x960).
- Масштаб UI: `k = h*1024/(w*768)`. Проверенные оконные режимы (живые прогоны):
  1920x1440, 1440x1080, 2880x2160 — у всех `k = 1`, `dx = 0`, **патч масштабирования не нужен**,
  штатный UI влезает сам; 2808x2160 — `k ≈ 1.0256`. Правки `HD_K/EC/EY` (ui/ui) нужны только
  для 16:9 (3840x2160, k = 0.75, полосы по бокам) — они и дают кривое меню; накатывать только
  при необходимости 4K, пошагово.
- Интро-ролики удалены из игры: **меню грузится ~1 с после запуска** (кадр в 5 с = меню, не интро).
- **Нельзя выставлять системное масштабирование DPI в свойствах `LETHALDREAMS.EXE`**
  (вкладка «Совместимость», «Переопределение масштабирования DPI» / `dpiAware`) — это ломает всё:
  окно виртуализуется DPI (1280x960 показывается как 1792x1344 при шкале 140%), съёмка и координаты
  рассинхронизируются. EXE должен сохранять собственное DPI-поведение; легитимный lever —
  `ConfigureDpiAwareness` в `config[game.root]/dxwrapper.ini` (артефакт прогона/эксперимента; в текущей установке отсутствует, в git не входит). Настройки совместимости сняты/записаны в
  `backup_orig/appcompat_lethal.txt` (реестр `HKCU\...\Compatibility` — ключей нет).

**Съёмка (`probe_measure.ps1`):** процесс делается DPI-aware (`shcore.SetProcessDpiAwareness(2)`);
без этого снимки растягивались (окно 1280x960 → png 1792x1344 при шкале 140%). По умолчанию
`-Shots 1 -WaitSec 5`, снимки в `приватный рабочий каталог\`. `-WinOnly` — снимает только окно
(главное окно = самое большое видимое окно процесса, поднимается `SetWindowPos` с
`SWP_NOMOVE|SWP_NOSIZE` — ресайз ломает игру, topmost безопасен). Полный экран: `scratch_dir('ld')/*_full.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит),
окно: `scratch_dir('ld')/*_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит). Замер desktop + кода выхода; `restore_res.ps1` — вернуть 3840x2160@120.


