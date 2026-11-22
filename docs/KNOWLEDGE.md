# KNOWLEDGE.md — выученные факты (термины, формат, механика)

## Игра и её движок

- Lethal Dreams (рус. «Смертельные Грёзы», студия Boolat Games, издатель Russobit-M,
  2003). Доказательства из установки: `LETHALDREAMS.EXE` VERSION-ресурс
  `LegalCopyright="Copyright (C) Boolat 2001-2003"`, `ProductName="Lethal Dreams"`;
  ярлыки `boolatgames.com`, `russobit-m.ru`, `lethaldreams.com`; реестровые префиксы
  `SOFTWARE\Boolat llc` / `SOFTWARE\Boolat Games`. EXE (Delphi, PE, base 0x400000) + игровой
  скриптовый движок на встроенном Python-подобном интерпретаторе:
  bytecode magic 13 («base», compiler 1995 Delphi) и magic 10
  (`ed2d0d0a`, Python 2.2.3-совместимая печать — modern-модули мода).
  Оба живут в установке (`ui/ui.pyc` base, `ui/types.pyc` modern) —
  терминология: «base» = оригинальные игровые pyc, «modern» = наши печати.
- `hd.cfg`: `w h bpp fullscreen` (при 4K обязателен флаг 1; с окном масштаб
  меню ломается). `hdlog.txt` — runtime-лог (пересоздаётся игрой; в git не входит).
- System scaling: App Compat Flag `~ HIGHDPIAWARE` (`LETHALDREAMS.EXE`)
  отключает системное масштабание exe; окно, двигавшееся системным масштабом,
  Windows не масштабирует (ограничение).

## Форматы

- `.btf`: texture-архив (имя TLV, w/h, `'t'` plen-16, blob zlib level 9);
  layout canon top-down, RGB/RGBA; round-trip unpack→pack побайтово lossless.
  Пакер берёт размер из PNG (апскейл поддержан), шаблон даёт TLV-поля/флаги.
- `.fdf`: таблица глифов `char x y h`, header `name size sheet`; парсер EXE
  (`FUN_0041ce40`) пишет slot0=x, slot1=y, slot4=h (slot2/slot3 обнуляет).
  Рендер глифа `FUN_0041d020`: квад = advance(slot4) × межстрочный шаг
  (`obj+0x40` = pitch), UV-rect `[slot0..2]×[slot1..3]`, масштаб поле
  `w = obj+0x1464 <- obj+0x54`. Термины slot0..slot4 — поля таблицы (не файлы).
- Шрифты: 4 листа `.fdf+.btf`: `ld_uncial12` (256×256, pitch 25),
  `ld_uncial20` (512×256, pitch 38), `ld_tahoma11` (256×256, pitch 19),
  `ld_numbers15` (128×32, pitch 16). Межбуквенный интервал НЕ общий:
  pitch=14 RowS=0 (диалог настроек, 54 эл.), pitch=14 RowS=-3 (блок описания
  персонажа), pitch=29 RowS=-10 (заголовки); глобальный коэффициент двигает
  все три класса — требуется поклассовая проверка.
- Листы 4x: remacri-апскейл (Lanczos), `.fdf` ×4 (`x,y,adv,pitch`), сайдкары
  `*_169` (4x) + `*_43` (родные); коэффициент A=0.3346 (CORR=4A=1.3384)
  снимает остаток строки точно (+0.29 % при 4x); при k=1 пещера identity
  (критично: иначе 4:3 даёт текст 0.25×).

## Механика пещер (cave)

- Пещеры — zero-run'ы exe (0x4ebf12, 0x4f5201, 0x50ec70, 0x51beb1, 0x51cb10,
  0x5228f9); cave-патч (236 B) ставит флаг масштаба, считает `k_eff`,
  `adv = (a_sheet - CORR)*k_eff + carry`; sites: 0x41ce31, 0x40b494, 0x47dd80;
  полярность `fld1; fcomip; je isone`, зажим k>1→1. 224-B версия: k_eff=k*0.25.
- Кодировки пещер сверены gcc/disex; бисекция крашей (siteK page 0x477 →
  0xC000041D) зафиксирована в истории.

## Съёмки и замеры

- Снимок только окна (`topmost`-подъём, курсор ОС за пределы), лог-зонды
  (`SCREEN`, `INITBB`, `HDINITBB`) пишут `hdlog.txt` (создаётся игрой, в git не входит) append; Frida ломает
  масштабирование меню — на съёмках не используется, сессию закрывать до снимка.
- 16:9-центр кадра, сжатый до 1920×1440, совпадает с 4:3-эталоном по геометрии;
  регрессия k=1 (4:3): mean|dRGB| 0.19–1.30, правка не исполняется.

## Прочее

- `dxwrapper` (сторонний) не входит в проект; borderless-механика описана
  в `docs/archive/PLAN_borderless.md`.
- Игровая установка содержит `LD-diag.bat` (чистится), `hdlog.txt` (создаётся игрой, снимок прогона);
  `LD-setup.bat` удалён из установки по решению пользователя (2026-11-18): в `original.7z` его нет,
  копия есть только в snapshot `6.7z`. RE-дебрис
  `__pycache__/re.cpython-312.pyc` (чистится).
- Q-режим шрифтов (`gen_fontq.py` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z), PATCH_Q_MODE.md) — удалён как негодный
  (в игре Q-сайдкаров не было).


## Поведение игры (проверено)
- `[U]` Intro-ролики вырезаны из игры, меню появляется сразу; кадр splash = меню.
- `[U]` В главном меню курсор — белая жирная точка (нормально). Обычный (осевой) курсор
  начинает отображаться при нажатии на любой пункт меню; проще всего проверять по
  пункту «Изменить» (справа снизу).
- `[P]` Курсор был сломан (не отрисовывался) при установленных `ui.pyc`/`cursor.pyc` из
  декомпилята (с зондами); после отката к оригинальным `.pyc`
  (`tools/pyc/pycbuild.py uninstall ui/ui`, `ui/cursor`) — **курсор в порядке**. Клик по глифу работает и на декомпиляте.
  Вывод: правки поверх целых декомпилированных модулей ломают игру — декомпилят
  (`cursor.py` (историческая ссылка) паритет 0.963) непригоден как база правок; база правок = **оригинальные
  `.pyc` + минимальный собственный модуль**, не трогая UI-модули.



## Chokepoint'ы Python→движок (подтверждены live-логом зондов)
- `[P]` Вся геометрия UI идёт через `ui.BActionElement`:
  - `InitBillboard(...)` → `BillBoard.Set(x, y, prior, w, h)` (+ `SetFont/SetTextLeading/
    SetTextRowSpacing/SetTextOffset`);
  - `SetPos(name, x, y, w, h)` → `billboard.Set(x, y, prior, w, h)` — все перемещения
    (`cursor.py` (историческая ссылка), `mainmenu.py` (историческая ссылка), `bpref.py` (историческая ссылка) идут через него);
  - `AutoPosition(name)` и `SetPrior(name, prior)` → `.Set(param['x'], param['y'], prior,
    param['w'], param['h'])`;
  - `CheckRefInPoint(name, x, y)` — hit-test движка;
  - `GetPos` возвращает канвас-значения (из `param`), логика раскладки остаётся в канвасе.
- `[P]` Мышь: глобальные обработчики `ui/cursor.py` (`h_MouseMove/LBDown/RBDown/LBUp/
  RBUp/Wheel/LDblClk`) получают движковые канвас-единицы; `h_MouseMove` сохраняет их в
  `ms_x/ms_y` и шлёт `SendMessage(WT_MOUSE, ...)` — движок трактует эти значения как
  свои канвас-единицы (hit-test по billboards движка).



## Рендер и dxwrapper (проверено реверсом бинарников)
- `[P]` Движок **OpenGL** (не DirectX): в `LETHALDREAMS.EXE` строки `opengl32.dll` (системная библиотека Windows, не файл проекта),
  `wglGetProcAddress`, `wglChoosePixelFormatARB`, `glBegin`, «OpenGl init failure».
  Импорт-таблица EXE: WINMM, DSOUND(ordinal 1 = DirectSoundCreate), KERNEL32, USER32,
  GDI32, ADVAPI32, ole32, python22.dll — **никаких ddraw/d3d8/d3d9**.
  `lethaldreams.dll` — **не используется в финальной сборке** (StarForce/SFORCE-протектор,
    отключён): EXE её ни статически не импортирует, ни загружает в рантайме, игра работает
    без неё (проверено 2026-11-18: запуск с удалённой DLL доходит до меню, в списке
    загруженных модулей её нет). Удалена из установки (бэкап `backup_orig/lethaldreams.dll`,
    оригинал в `original.7z`); `BTexturer.dll` — MSVC CRT, без DirectX.
- `[P]` Fullscreen реализован самим движком: EXE импортирует `ChangeDisplaySettingsA` +
  `EnumDisplaySettingsA`; `SetDisplayMode(w,h,bpp,f)` при f=1 делает **реальную смену
  режима** (`ChangeDisplaySettingsA(&devmode, CDS_FULLSCREEN)`) и затем popup-окно
  `HWND_NOTOPMOST` на размер w x h. Раньше это ошибочно считалось `CDS_TEST`; в
  действии это и был эксклюзивный fullscreen. После патча `CDS_FULLSCREEN -> CDS_TEST`
  (см. «Borderless fullscreen») режим дисплея не меняется и F=1 = borderless.
  Проверено прогоном f1b: desktop 3840x2160@120 до/во время/после — не менялся.
- `[P]` DXWrapper для рендера не нужен. Его активные функции в текущем конфиге:
  `__COMPAT_LAYER=HighDpiAware` (DPI-aware процесса), `ResetScreenRes=1`,
  `FullscreenWindowMode`, dsound-stub (`EnableDsoundWrapper=0` — подмена выключена).
  Все ddraw/d3d9/enable-флаги = 0. `FullscreenWindowMode=1` не трогает OpenGL-рендер;
  эффект на мышиные единицы в hd8 (пер-axis) был следствием окна, не рендера.



## Runtime-зонд API движка (`[P]` hd31-hd34)
- `LoadTexture('mm_bkg01.bmp')` = None; фон грузится `LoadMaterial('mm_bkg01.bmp')`
  (1 pass; после AddTexture по имени — Texture 1024x1024, имя 'mm_bkg01.bmp').
- Python API движка НЕ отдаёт пиксели текстуры (GetFromScreenRect без аргументов = 0,
  pickle Texture = segfault). Надёжный офлайн-раскод: payload LDPACKER+zlib+mdf,
  blob BGRA row-major stride W*4 (w=1024,h=1024 из mdf).
- Зонд с вызовами методов pass'ов (сеттеры без аргументов) рушит движок (0xC0000005);
  безопасны только Get* и AddTexture(имя).
- `[U]` «ты опять какой-то мусор сохранил на рабочем столе»; «а по коду непонятно какой ресурс берётся? при отрисовке меню»;
  «ищи в ресурсах самые большие изображения»; «1 за раз»; «сначала сам смотри его и только потом клади на рабочий стол»;
  «это не та текстура, там серый прямоугольник».
- `[U]` «напиши md-файл для задания агенту на написание offline-распаковщика .btf в .bmp или .png»
  -> задание: `docs/archive/TASK_btf_unpacker.md`.



## Путь в режим боя и baseline боевого HUD (2026-11-15, вечер)

- `[U]` «как зайти в режим боя: левый глиф в меню "Вообразить" -> 2 клика для скипа
  ролика -> любой глиф персонажа -> 2 раза нажать enter и дождаться 2с загрузки».
  **Левый глиф = «Вообразить» = НОВАЯ ИГРА** (`COORDS_MM_NewGame` rect
  `[136,272,128,256]` -> центр канвас **(200,400)**), НЕ «Вспомнить» (сейв). Путь через
  сейв («Вспомнить», глиф (350,107), сейв-строка (280,161), Enter) приводит в оверленд,
  где панелей боя нет.
- `[P]` Экран выбора персонажа (круг): глифы ≈ канвас **(164,384)**, **(345,82)**,
  **(676,103)**, **(349,658)**, **(676,658)**; кнопка «Начать» ≈ канвас **(512,569)**.
  Скрипт: `tools/archive/probes/probe_loadshot.ps1 <W> <H> <F> <Tag> -Mode new -SkipClicks 2 -CharX 349 -CharY 658 -EnterCount 2 -LoadWait 3`
 (после Enter снимает
  `scratch_dir('ld')/<tag>_overland.png` (снимок прогона) = бой).


## Почва карт: `.tex` в `.map` (2026-11-17)

- `[P]` `config[game.root]/data/maps/*.map` = 30 карт; почвенные текстуры — 44 записи
  `<map>_<N>.tex` внутри payload'а контейнера `LDPACKER`+zlib (формат записи и поля
  шапки — `docs/OBSERVATIONS_TEXTURE.md`, «Почвенные текстуры карт»).
  Размеры сторон S ∈ {32,64,128,256,512,1024}, bpp 3 при alpha=0, bpp 4 при alpha=1;
  blob = raw raster без mip/alpha-плоскости/RLE; движковый лимит шапки 33554432 B.
- `[P]` Почву рисуют генерируемые материалы `data/materials/<map>_opaque_N.pyc` /
  `<map>_alpha_N.pyc` (92 файла): `Passability = globalscript.GROUND`,
  `AcceptCamera = 1`, `InitMaterial` вызывает `Mtl.GetPass(0).AddTexture('<map>_<N>.tex')`;
  alpha-материалы добавляют `SetBlendFunc(globalscript.BBlend_Blend)`.
  Стартовая карта новой игры — `portal_domain` (записи 1024² bpp3 и 32² bpp4).
- `[P]` Игра при каждом запуске пересоздаёт ресурсы: `Lib/**/*.pyc` (9), `hd.cfg`,
  `hdlog.txt`, font-сайкары `data/fonts/*.fdf` + `data/textures/ld_*.btf`,
  `data/textures/mm_bkg01.btf`. Поэтому в scratch-дереве эти файлы обязаны быть
  настоящими копиями (hardlink = запись в установку), и подмена `.btf`-ресурса
  тестовым содержимым не работает: движок резолвит имя из источника.
- `[P]` `GROUPS` = `id|group|class|name|path|color` (6 полей, color — 0xRRGGBB
  десятичное); тайловые BMP `data/wrender/figures/textures/*.bmp` — 32x32 24-bit RGB
  с обманным `biBitCount`, строки bottom-up. Генерация почвы из тайлов не подтверждена
  (остаток сетки 32 = 15.0, палитра не покрывает blob'ы).


## Движок читает `.map`: TLV-цепочка и разбор записи (2026-11-17, вечер)

- `[P]` Payload `.map` — цепочка TLV-заголовков, которую ходит `FUN_0049bf70` (ADV):
  заголовок = `(type u32, size u32)` по pos, содержимое `pos+8..pos+8+size`, следующий
  заголовок — `pos+8+size`. ADV сверяет потреблённое с размером (`ftell == pos+8`) и
  при рассинхроне гасит флаг потока (байт `+0x28`).
- `[P]` `FACT` (`FUN_00495720`) возвращает NULL, если флаг `+0x28` == 0 или разбор
  записи (`FUN_00495300`) вернул 0; диспетчер (`FUN_00467eb0`) снимает указатель без
  проверки на `0x467f54` → `0xC0000005` (WER fault offset `0x67f54`).
- `[P]` Поэтому **изменение длины blob'а `.tex`-записи требует правки всех
  накрывающих TLV-размеров** (внешний заголовок записи + цепочка секций), а запись
  пересобирается без дублирующего `name_len` u32 (он живёт в gap'е перед именем).
  Реализовано в `tools/tex/patch_map_pattern.py` (`chain_headers`, `fix_chain`);
  все прогоны с изменёнными размерами грузятся (`ExitCode=00000000`).
  Полный разбор — `docs/OBSERVATIONS_TEXTURE.md` (раздел вечера 17.11), протокол
  эксперимента и решение — `docs/archive/ISSUE_tex_upscale.md`, грабли — `docs/PITFALLS.md`.

