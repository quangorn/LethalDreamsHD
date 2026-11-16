# OBSERVATIONS_ENGINE.md — наблюдения: рендер, окно/DPI, геометрия мира, проекция

Наблюдения, измеренные в сессиях реверса и живых прогонов. Патч-реализация — `docs/PATCHES.md`;
общие факты о движке — `docs/KNOWLEDGE.md`; порядок чтения — `docs/INDEX.md`.


## Геометрия и DPI (проверено замерами)
- `[P]` Система: desktop 3840x2160, системный масштаб DPI **75%**; `LETHALDREAMS.EXE`
  DPI-unaware, окно виртуализуется DWM: движок просит 1920x1440 → физический размер окна
  **1440x1080** (коэффициент 0.75). Снимки снимаются с `SetProcessDpiAwareness(2)`
  (как в `tools/archive/probes/probe_measure.ps1`) — тогда координаты снимка = физические пиксели.
- `[P]` `gamemodule.GetScreenSize()` в оконном режиме возвращает **размер desktop**
  (3840x2160), а НЕ размер рендера — в прогоне 1920x1440 вернул `[3840, 2160]`.
  Значит `k` нельзя считать из `GetScreenSize()`: источник — `hd.cfg`.
- `[P]` Движок отдаёт мышиные координаты в **канвас-единицах**: `m = физ_px / s`,
  `s = физ_ширина_окна / 1024`. Проверено по 6 точкам при окне 1440x1080
  (`s = 1.406`): OS abs (720,540) → движок сообщил (512,384);
  (1296,918) → (921,652); (144,108) → (102,76). Все шесть точек легли на `s = 1.406`.
- `[P]` Канвас = 1024x768 (подтверждает и `AutoPosition`, клампящий x≤1004, y≤748).
- `[P]` Масштаб движка: канвас-единицы умножаются на `s = w_окна/1024` (обе оси); при
  полном экране `s = 3840/1024 = 3.75`, физическое окно = весь экран, виртуализации нет.

Статический патч (exepatch12) — реализация; цепочка exe — `docs/PATCHES.md` и `docs/STATE.md`.


## Геометрия мира: aspect зашит в матрицу проекции (2026-11-15, вечер)

- `[U]` «возможно этот код находится в одном из python скриптов игры (они уже декомпилированы)»
  — проверено: Python-слой (`ui/olcamera.py`, `battlecamera.py` (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git)) задаёт только позицию/поворот
  камеры через `gamemodule` API; проекцию строит движок в EXE. В Python нет fov/aspect/frustum.
- `[P]` Искажение мира в 16:9 (3840x2160) — НЕ UI и НЕ hd_scale: движок bake-ит aspect в
  матрицу проекции. Замер ratio кольца домена (bright-blue ring bbox, `tools/archive/probes/measure_disc.ps1`):
  16:9 = **1.456**, 4:3 = **1.06**; отношение = `w*768/(h*1024)` = k.
- `[P]` Live-дампы матриц (`tools/archive/frida_ext/x_ld_mat.py` hook на `FUN_0046f970(ctx,idx,matrix)`, thiscall,
  args: idx=`[esp+4]`, matrix=`[esp+8]`): projection (idx=2) мира =
  `[1.29904,0,0, 0,1.732053,0, 0,0,-1.0005,-1, 0,0,-0.20005,0]`; x-коэф/y-коэф = **0.75** = k.
  Console overlay (idx=2) = ortho `matrix[0]=2/1024, matrix[5]=-2/576` (её трогать нельзя).
- `[P]` Live-запись h (`[DAT_0051cc7c]+0x1c`=2880) НЕ меняет рендер — масштаб берётся из
  матрицы, которую движок ставит каждый кадр, а не из stored h.
- `[P]` Live-патч (`tools/archive/frida_ext/x_ld_proj.py`): hook на `FUN_0046f970`, idx==2, по pointer'у запоминается
  оригинальный x-коэф (фильтр `1<cx<3` — отсевает console ortho), пишется
  `matrix[0] = matrix[5] * h/w` (эквивалент `cx/k`). **Результат: ratio кольца**
  `scratch_dir('ld')/a169d_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) **1.4035** → `scratch_dir('ld')/patch5_scr.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) **1.0575** (4:3 = 1.06), гексы
  правильные, портал/сферы круглые, мир заполняет окно, UI цел (`scratch_dir('ld')/view_patch5b.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)).
- `[P]` Механика движка: мир рендерится в канвас 1024x768 с x-коэфом `cot(fov/2)*0.75`
  (x-сжатие канваса), затем blit канвас→окно растягивает X на k. Итог: X мира растянут в k.
  Правильный мир = x-коэф канваса, сжатый ещё на 1/k, тогда после blit X и Y изотропны.
  При k=1 (4:3) — identity ✓ (regression-safe).
- `[P]` Направление «make x==y» (`matrix[0]=matrix[5]`) даёт ratio **1.8876** — ХУЖЕ;
  `matrix[5]=matrix[0]` → **1.8735**. Т.е. изо-проекция канваса НЕ эквивалентна изо-окну.
- `[P]` World projection matrix set при: `FUN_0047dd80` (RenderWorld `FUN_0047dd30`,
  callers `0x4209df`, `0x4819ff`), `0x40c68d` (FUN_0040c2d0), `0x433971`, `0x465336`,
  `0x481ac2` (BScene::Render `FUN_00481990`). Хранилище: `scene+0x40` → camobj, projection
  = camobj+0xf8; сеттер копирует в `ctx+0x104`.
- `[P]` `FUN_0046f970` = thiscall(ctx); table-jump по idx; case2: `mov esi,[esp+0x14]`,
  `lea edi,[ctx+0x104]`, diff-merge `FUN_0046f310` + `rep movsd` 16 floats, `flag ctx+0x1c6=1`,
  `ret 8`. Байты `8b442404 83f803 53 8bd9 0f87b1000000 56 57 ffe48538fa4600`.
- `[P]` `FUN_0046a070` (canvas-quad blit?) — единственные xref константы `0.75f @ 0x4efbd0`,
  сайт `0x46a1a6`: `x_scale = 1/(0.75*(cos/sin))`, `y_scale = 1/(cos/sin)`, fsincos из fov
  camobj+0x144; матрица camobj+0xb8. Пещерный патч этого сайта (0.75 → h/w) — кандидат.
- `[P]` Live-hуки: `tools/archive/frida_ext/x_ld_mat.py` (дамп матриц+viewport), `tools/archive/frida_ext/x_ld_proj.py` (live-фикс/LEARN callers),
  `tools/archive/frida_ext/x_ld_aspect.py` (w/h ctx). Frida через Windows Python:
  `config[python3]` (frida 17.24.1);
  returnAddress = `this.context.esp.readPointer()`; long-attach (18с) рендер не ломает.
- Следующий шаг: статический EXE-патч пещерой (`matrix[0] = matrix[5]*h/w` на сетте projection,
  только для world-матрицы), 4:3 identity, замер ratio + регрессия + живой тест кликов.
- `[P]` **4:3 live-регрессия хука (k=1)**: matrix[0] при 1280x960 = 1.29904 (canvas-проекция
  фиксирована, не зависит от режима). Хук при k=1 → target = cx (identity). Замер того же
  tight-региона кольца: `scratch_dir('ld')/b43live_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) w=331 h=200 ratio **1.655**, `scratch_dir('ld')/b43patched_win.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)
  w=331 h=200 ratio **1.655** (px 3256 vs 3316, снимок `scratch_dir('ld')/view_b43patched.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)) — хук identity
  в 4:3 ✓. (Ранние замеры ratio в 4:3 1.06/1.24/1.39/1.46/1.67/1.655 — bbox-меру надо
  задавать tight вокруг одного кольца; число зависит от региона, но пара baseline/patched
  совпадает.)

### Статический EXE-патч геометрии (exepatch12, 2026-11-15 вечер) — применён

- `[P]` `tools/exe/exepatch12.py` = exepatch10 (шрифт) + правка проекции мира. EXE md5
  `171319bbc170cf6e57c8d36a8ae5f50d`; baseline `backup/LETHALDREAMS.EXE.pre4`
  (`45e93d46...`), снимок exepatch10-state `backup/LETHALDREAMS.EXE.patch10`
  (`51f69974...`). Откат: `python3 tools/exe/exepatch12.py restore`.
- `[P]` Единый блоб `tools/exe/caves/fixall.s` -> `paths.cache_dir('cave')/fixall.bin` (собираемый артефакт: sh tools/exe/build_cave.sh fixall, в git не входит) (**236 байт**, zero-run
  VA 0x4ebf12, .text; единственный исполняемый slack, int3-runs в .text max 15):
  cave1 (шрифт, null-проверки вырезаны, `.align 16` убран, cave2 +0x7f),
  cave2 (SetFont, +0x7f), cave3 (проекция, +0xa7), константы без выравнивания.
  Сборка: gcc -m32 + objcopy; релят в tools/exe/exepatch12.py: x87 disp32 -> CAVE_VA+disp (4),
  rel32 call/jmp cave3 (c3call +0xca, c3jmp +0xcf) -> точные цели.
- `[P]` Сайт 3: `0x47dd80` (RenderWorld FUN_0047dd30, 5 байт `call FUN_0046f970`)
  -> jmp cave3 (+0xa7). Cave3: `mov eax,edx`; guard **4h==3w** (k=1, точное целое)
  -> пропуск записи; иначе `matrix[0] = matrix[5]*h/w` (fild/fild/fdivp/fmul/fstp);
  `call 0x46f970; jmp 0x47dd85`. eax/edx/edi scratch; ebx сохранён callee'ами.
- `[P]` **Почему guard**: движок хранит matrix[0]=fl32(0.75*cot80)=1.2990399599075317;
  `fl32(0.75d*fl32(cot))` = 1.2990400791168213 (ULP+1) — без guard 4:3 НЕ побитова
  (первый прогон: w=338 vs baseline 331). С guard live-READ в 4:3: cx точно
  1.2990399599075317 (запись пропускается) ✓; console ortho (2/1024) не тронута.
- `[P]` Замер кольца (tight 1540 1040 700 540): baseline **1.4035** ->
  exepatch12 16:9 **1.0531** (`scratch_dir('ld')/p12b_overland.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит); live 1.0575, live-таргет
  matrix[0]=0.974731, статический READ = 0.9747310876). Мир заполняет окно, гексы
  ровные, портал/сферы/кольцо круглые, UI цел, 4x-шрифт sharp (`scratch_dir('ld')/view_p12a.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)).
- `[P]` Хвост zero-run за блобом остался нулями; сайты: 0x41ce31 `e9 dc f0 0c 00`,
  0x40b494 `e9 f8 0a 0e 00 90`, 0x47dd80 `e9 34 e2 06 00`.

## Python-слой: `python22.dll`, `Lib/` (stdlib 2.2.3), корневые `.pyc`

- `[P]` Оригинальная установка (`archive.pristine`) **не содержит `Lib/` и корневых `.pyc`
  вообще**; там альфа `python22.dll` = Python **2.2a4+** (строка версии в DLL, copyright 2001;
  бэкап `backup_orig/python22_2.2a4.dll`). Альфе `site.py`/`codecs.py` не нужны. — `site.py`/`codecs.py` лежат в `Lib` установки, в git не входит
- `[P]` В установке стоит финальный **Python 2.2.3**: `python22.dll` = 847920 B,
  md5 `4ee79da1533f8ac2957fd46c6ba1cb2d`, сборка 30.05.2003 (PE TimeDateStamp
  1054332733 = 2003-05-30 22:12 UTC; строка `2.2.3` в DLL есть, строки `30.05.2003` там нет).
  Магия `.pyc` `0xED2D` = 60717 —
  та же серия, что игровые `.pyc` (2.3 = 60720, игровые `.pyc` он не прочтёт). EXE импортирует из
  `python22.dll` **53** символа C-API, и все 53 есть среди **753** экспортов DLL; зависимости самой
  DLL (`MSVCRT` 162 импорта, `KERNEL32` 39, `ADVAPI32` 6, `USER32` 1, `SHELL32` 1) на месте.
  Измерено по import-таблицам PE: `LETHALDREAMS.EXE` — 210 импортов (`KERNEL32` 102, `python22.dll` 53,
  `USER32` 48, `ole32` 3, `GDI32` 2, `WINMM` 1, `ADVAPI32` 1, `DSOUND`), из них 53 в `python22.dll`.
  Прежняя оценка «51 символ» — измеренное число **53**.
- `[P]` Финальный 2.2.3 при `Py_Initialize` требует `site.py`/`codecs.py`; вся stdlib 2.2.3 — `site.py`/`codecs.py` лежат в `Lib` установки, в git не входит
  (694 модуля) лежит в `Lib` — **713 файлов** (694 `.py` + 19 `.pyc`). `Lib/site.py` = 10720 B,
  `Lib/codecs.py` = 20167 B.
- `[P]` **Эксперимент 16.11.2026 (пользователь):** родная альфа `python22.dll` + пустой `Lib`
  + без корневых `.pyc` → игра **не запускается**. Слой возвращён; слой заархивирован в
  `python_libs.7z` (приватный архив, путь — приватный ключ config): 725 файлов
  (`python22.dll` + `Lib/` 713 + 11 корневых `.pyc`), crc32-сверка: все побитово равны
  текущей установке. Python-слой — **обязательная часть базы**, не опциональная.
- `[P]` Корневые `.pyc` — 11 модулей (`__future__`, `copy`, `linecache`, `ntpath`, `os`,
  `site`, `stat`, `string`, `traceback`, `types`, `whrandom`): компиляты тех же stdlib-источников,
  собранные движком из файлов, лежавших **в корне** игры; `.py` из корня удалены, `.pyc` остались.
  Доказательство: последовательности оперов (worker 2.2.3) компилята `Lib/<mod>.py`, корневого `.pyc`
  и `Lib/<mod>.pyc` совпадают поштучно — site 946 ops, os 1731, types 250, stat 171
 (мера `tools/pyc/pyc_norm.norm_ops`: dis-строки worker'а, `SET_LINENO` исключён, хвостовой
  `LOAD_CONST None`/`RETURN_VALUE` — одна пара; полный dis с `SET_LINENO` — 1117/2099/308/236).
  Размер
  корневого `.pyc` меньше Lib-компилята на величину, кратную 4 = длина префикса `Lib\` в
  `co_filename` × число кодовых объектов: `stat` 40/4=10, `site` 56/4=14, `os` 180/4=45,
  `types` 24/4=6, `__future__` 24/4=6.
- `[P]` Header-mtime слоёв: все 11 корневых `.pyc` — один прогон (2026-11-14 01:16:04).
  Lib `.pyc`: 9 — движок (header == mtime `.py`: 7 при 22:24:36, `os`+`site` при 22:30:32);
  10 (`re`, `sre`, `sre_compile`, `pickle`, `py_compile`, `dis`, `copy_reg`, `difflib`,
  `linecache`, `traceback`) — header 2026-11-14 01:24:36 при mtime `.py` 22:24:36, т.е. сделаны
  инструментом (`tools/pyc/py22_worker.py build` / `py_compile`), для движка они «устаревшие».
- `[P]` Импорт stdlib движком не берёт подложенные `.py`: хуки в `Lib/site.py`, `Lib/os.py`,
  `data/scripts/os.py` не исполнялись (логи пусты при работающей игре) — детали в
  `docs/archive/RESEARCH_PY_DECOMPILE.md`, грабля — `docs/PITFALLS.md`.
- `[P]` Статус в проверке: `Lib` и корневые `.pyc` — легальный базовый слой как SNAPSHOT_ONLY
  (`release/allowlist.json`: 713 записей `lib/*` + 11 корневых `.pyc`), `--untouched` rc 0.
  `archive.pristine` их не покрывает (их там нет); покрытие — snapshot-архив и `python_libs.7z`.
- `[P]` Причина, почему альфа-установка без `Lib` запускалась: игра несёт **свои клоны 7 stdlib
  модулей** в `data/scripts` (`copy.pyc`, `os.pyc`, `types.pyc`, `string.pyc`, `whrandom.pyc`,
  `stat.pyc`, `ntpath.pyc`; всего 44 `.pyc` в `data/scripts`). Игровые модули импортируют
  `copy`/`sys`/`math` (`decompiled_fixed/**`), и движок разрешает их из `data/scripts`, а не из
  stdlib. Клоны **не равны** stdlib-компилятам: `copy` — `81fb2a2d3081` vs `c0d57e3df1a6`,
  `os` — `61cffc2a1621` vs `bb3494b99800`, `types` — `414bd2aa0857` vs `34d060c208c0`,
  `string` — `10c368f8f8e0` vs `698e4d33a160`.
- `[P]` Нашему дереву полный stdlib нужен: цепочка инициализации тянет `re`, `sre`,
  `sre_compile`, `pickle`, `py_compile`, `dis`, `copy_reg`, `difflib`, `linecache`,
  `traceback`, которых в `data/scripts` нет. Отсюда обязательность `Lib` + финальной DLL.
  Гипотезу «альфа читает наши компиляты» эксперимент опровергает: альфа + пустой `Lib` +
  без корневых `.pyc` игра не запускается (16.11.2026, пользователь).
