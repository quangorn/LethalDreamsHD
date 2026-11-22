# NARRATIVE_2026-11.md — дословные датированные повествования из docs/PITFALLS.md и docs/PATCHES.md

Файл — архив: содержание секций перенесено дословно из `docs/PITFALLS.md` (22.11.2026); личные пути
нейтрализованы. Компактные блоки (симптом → причина → обход) остались в `docs/PITFALLS.md`.
Сюда же перенесены дословные повествования секций `docs/PATCHES.md` (22.11.2026, группа ниже);
компактные записи патчей остались в `docs/PATCHES.md`.
Канонические команды — `docs/PIPELINE.md`; правила архива — `docs/archive/README.md`.

## Итерация 2026-11-15 (вечер): фильтр/гейт на сайте 0x470932 — отрицательные результаты и разгадка
- `[P]` **Полный «рабочий» статический cave (exepatch6, PARTS full) математически = тождество.**
  Точный дизасембл байтов (fileoff 0xebf12, 234 байта): loop1 суммирует поля `+12/+16`
  (`w=-1`, `color`) всех 4 записей; center = sum/4 = среднее тех же значений; loop2 пишет
  `x' = 0.75·x + 0.25·center = x`. Запись байтов обратно идентична исходным. Следовательно
  «сжатие 538→300», зарегистрированное в прошлой итерации, было **артефактом состояния меню**
  (hover/no-hover), а не эффектом патча. Все мои реплики честно воспроизводили тождество —
  этим объясняются нулевые результаты каждой проверки.
- `[P]` Точная раскладка записи: record base = `esi + idx*32` (stride 0x20); поля: `+0=0`,
  `+4 u`, `+8 v` (координаты сигнатур, значения 538/682/586…), `+12 w=-1`, `+16 x`, `+20 y`,
  `+24..` флаги corners (1,0)/(1,1)/(0,1)/(0,0) (`f5,f6`). Сигнатуры стрипов
  (min u, min v, w, h) в hooklog4 = поля +4/+8 (это `u,v`, НЕ экранные x,y).
- `[P]` **Запись в поля записей пула (`u,v`, `x,y`, `w`, `color`, флаги) не меняет рендер** —
  многократно проверено на живом сайте при нативно сработавшем фильтре (debug-счётчик в
  `.data 0x51d500` ≈200 инкrementов за 2 с при hover (2400,1750)): масштаб `u,v`, `x,y`, `w`,
  flags — визуально ноль. Пул не является живым источником вершин glyph-квадов (geometry
  загружается один раз).
- `[P]` Инструментальная lesson: `movl 16(%esi,%edx,1), %eax` затирать счётчик цикла (`test
  eax,eax`) → вылет за пределы индексов → краш (`wrec` ALIVE False). Поле читать в `%ecx`.
- `[P]` Debug-счётчик cave обязан лежать в writable `.data` (VA `0x51d500`); `incl` в RX
  `.text` (0x4f5310) — краш игры. Счётчик читается Frida `rpc.exports`, сброс через
  `Memory.patchCode(base+0x1d500)`.
- `[P]` Гейт по значению окна через user32 из cave (`call [GetForegroundWindow 0x4ec1c0]`,
  `GetClientRect 0x4ec1cc`, сравнение right==3840) — **убивает игру** даже без scale-блока
  (проверено и в нативном cave, и в Frida-эмуляции NativeFunction). Не использовать.
- `[P]` Глобальной переменной разрешения нет: скан процесса (модуль + все rw-регіоны) на
  int-пару (3840,2160)/(1024,768) и float-пару (3840.0,2160.0) — 0 совпадений. В куче есть
  float 3840.0 в адресе bddfba0 (+2161.0) — адрес плавает, для статического патча непригоден.
- `[P]` Уникальные для 4K сигнатуры записей: `minx=417` (282/phase при hover; в 4:3 есть
  только 414/423), max u=682/703 (4:3 max 547, но 600..1024 значения есть и в 4:3) —
  точный band-гейт `415<minx<419` работает (фильтр fires), эффект масштаба = ноль.
- `[P]` 20 «низкочастотных» сайтов (235-239/phase) = per-frame GL state-вызовы, НЕ glyph
  draws; draw-сайты в файле: только `0x4707b7` (в меню не стреляет) и `0x470932`.
- `[P]` Итого за итерацию: ни одна комбинация «поле + фильтр + scale» на сайте 0x470932
  не дала видимого сжатия текста; все прежние «положительные»测量 — артефакты hover-состояния.
  **Задача (уменьшить шрифт при 4K правкой сайта draw) на этом сайте закрыта как бесперспективная:**
  geometry glyph-квадов не читается из пула при draw.
- `[P]` Состояние: EXE = чистый baseline (md5 `45e93d46aad6ceb3dfc17e86d04e4f82`), игра
  остановлена. Рабочие скрипты итерации: `tools/archive/cave_gen/cavesrc.py` (range/eq-фильтры), `tools/archive/cave_gen/cavew.py`
  (record-band + w scale, исправлен), `tools/archive/cave_gen/caveext.py` (extent-фильтр), `tools/archive/cave_gen/cavegate.py` (u>600),
  `tools/archive/cave_gen/cavegate417.py` (minx-гейт 4K-only), `tools/archive/cave_gen/cavegatew.py` (оконный гейт — краш), `tools/archive/cave_gen/cavefilt.py`
  (corruption на text strip).
- Следующий шаг: точка **загрузки** геометрии: site `0x470909` (`push eax; push 0x20; push
  0x1401; push 4; call [0x51cd50]` — загрузка шрифт-атласа, GL_TEXTURE_2D) — хук/патч там;
  разрешение брать из `hd.cfg` (читается при старте), а не из рендера.

## Апскейл альфы (маски) — попробовали, не пошло (2026-11-15, ночь)
- `[U]` Идея пользователя: прогнать через Upscayl не RGBA, а отдельную маску покрытия,
  чтобы модель восстановила силуэт глифа (в RGBA-прогоне альфу ресемплит не модель).
  Маски: `tools/archive/font_gen/fontmask_export.py` → `scratch_dir('fonts_alpha_src')`
  (8-бит grayscale, белый = штрих). Результаты: `..._alpha_dst/{remacri,high-fidelity}-4x`.
- `[P]` **QC (`tools/archive/font_gen/maskqc.py` (собираемый блоб), оригинал = alpha из `paths.btf_backup_dir()/*_orig.btf (родной бэкап оригинала (восстанавлируется из архивов: `python3 tools/extract_pristine.py templates`), в git не входит)`)** — контроль: та же маска,
  Lanczos-4x локально (`gm convert -filter Lanczos -resize 400%`):

  | вариант | mass | cov | dcy (px ориг.) | soft | bleed% | dA1x | inkpx |
  |---|---|---|---|---|---|---|---|
  | Lanczos-4x (контроль) | 0.994–0.995 | 1.00–1.02 | 0.17–0.38 | 0.77–0.86 | 0.25–1.5 | 1.33–1.39 | 35 % листа |
  | remacri-4x | 1.011–1.032 | 0.949–0.971 | 0.37–**2.54** | 0.84–0.90 | 1.0–2.4 | 1.9–4.8 | **~100 % листа** |
  | high-fidelity-4x | 1.044–1.059 | 0.88–1.02 | 0.27–**3.28** | **1.000** | 2.1–6.7 | 6.6–**20.8** | **100 %** |

  Плюс замер фона (пустая нижняя полоса листа): Lanczos 0, **remacri ровно 2/255**,
  high-fidelity 3±1 с шумом. То есть GAN добавляет DC-подъём на всю текстуру (0.8–1.2 %
  полупрозрачной вуали на каждый квад), раздувает массу штриха на 3–6 %, сужает силуэт
  (cov<1), уводит тахому вниз на 2.5–3.3 px оригинала и в 4–6 раз увеличивает ink вне
  ячеек (при рендере это кроп по UV). Визуально (`masks/maskcmp.png` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z)) remacri — «жирнее
  и мыльнее» Lanczos, high-fidelity — с ореолом.
- `[P]` Причина простая: маска — уже гладкая карта покрытия (сплошные формы, а не контур);
  новой информации в ней нет, и верная реконструкция = честная интерполяция (Lanczos),
  а не «додумывание» модели. AI в этой задаче только портит.
- `[P]` **Установленные сейчас 4x-листы (RGBA remacri) уже оптимальны по альфе**: фон
  альфы в них ровно 0 (Upscayl ресемплит альфу чисто), mass 0.995, bleed 0.25–1.5 %,
  dA1x 1.34–1.39 — то есть совпадает с Lanczos-контролем. Переустановка не нужна.
- Вывод: альфа остаётся как есть; RGB — remacri (лучший `rgbD` 4.63). Дальнейший рост
  резкости этим путём невозможен: 4x уже даёт 0.70 экранных px на тексель (уменьшение).


## Черезстрочные PNG: квадратная dims-амбигуность (2026-11-15, вечер)
- `[U]` «часть текстур криво сконвертировалось в png: `mm_ng_tt_transmuter`, `mm_tt_save`,
  `msg_btm_alt` — в png черезстрочные паттерны». бекап оригиналов:
  `scratch_dir('textures_original_btf2x')` (1270 `.btf`, mtime 2002–2003).
- `[P]` **Канонические dims** = первые два u32 после поля имени (`w`, `h`),
  `bpp = blob_len/(w*h)`; `blob_len` — последний u32 `i` c `i+4 == plen - v`
  (`hdr_end = plen - blob_len`). Поле имени: `(len u32, ascii)` — длина не кратна 4,
  поэтому поля после имени идут **со сдвигом**, и `parse_btf`, который сканирует пары по
  всем 4-выравненным офсетам и **предпочитает квадратную пару**, для `w != h` берёт
  ложную квадрaту: blob 262144 подходит и как 512x128 (bpp4), и как 256x256 (bpp4).
- `[P]` Затронутые файлы (`.tmp/state_scan.py`, сравнение канонических dims с игрой):
  **5** — `fire01`, `mm_ng_tt_transmuter`, `mm_tt_save`, `msg_btm_alt` (512x128 bpp4) и
  `ship_deck01` (512x128 bpp3). Экспорт сделал PNG 256x256/256x256 (shear → черезстрочные
  линии), packer пропатчил **не** w,h (пара (256,256) в заголовке — ложная), а поле
  `w*h`/`blob_len`, так что в игре эти файлы = апскейл-blob при родных w,h: движок читает
  первые `w*h*bpp` байт = верхнюю полосу кривого апскейла. Остальные 1244 установленных
  файла — `installed-ok` (w,h и blob = 2x канонических), sidecar'ы 12, 5 баз шрифтов
  (`mm_bkg01`, `ld_uncial*`, `ld_tahoma11`, `ld_numbers15`) не тронуты, 4 индексированных
  глифа парсятся канонически не могут.
- `[P]` Починено: `.tmp/reexport.py` (декод по каноническим dims) — PNG переписаны в
  `LethalDreams_textures_src`; глазами (`.tmp/layout_variants.py` + flatten на тёмном фоне,
  `scratch_dir('ld')/fixed_*.png`) — `mm_tt_save` = читаемый текст глифов, `msg_btm_alt` = целая
  декоративная рамка, `fire01` = 4 анимационных кадра огня, `ship_deck01` = кирпичная
  палуба без sheared-полос, `mm_ng_tt_transmuter` = зеркальная надпись TRANSMUTER (как и
  хранилась). В `tools/btf/unpack_btf.py` добавлен `canon_dims()`, `parse_btf` ставит канонические
  dims первым и отдаёт ключ `canon`; `pack_btf.pack_one` патчит w,h по каноническим
  офсетам (`cd[4]`, `cd[4]+4`, blob_len = `cd[5]-4`).
- `[P]` Проверка: round-trip оригинал → PNG (канонические dims) → pack (`pack_one`) —
  md5 совпадает побайтово для **всех 5** (`scratch_dir('ld')/rt/*.btf`), т.е. layout (top-down,
  RGBA/RGB интерлейс) и канонические dims подтверждены.
- `[P]` Сканирование всей игры после фиксации dims (`tools/archive/texture_qc/all_check.py`, сверка заголовков
  бекапа и игры + побайтовая сверка blob'ов с `dst`-PNG): `dims-ok` **1244**, из них
  побайтовое совпадение с апскейлом **646/646** (0 расхождений), 598 batch-1 файлов
  сверены только по dims (их PNG удалены), `game-unparsable` 5 = ровно кривые 5,
  `untouched-original` 5 (базы шрифтов + `mm_bkg01`), sidecar'ы 12, `canon-unparsable` 4
  индексированных глифа. Полный список: `scratch_dir('ld')/all_check.txt` (лог прогона).
- `[U]` «удали из `LethalDreams_textures_src` все файлы кроме этих 5» — `[P]` удалено 784
  объектов (`scratch_dir('ld')/MANIFEST.txt` (лог прогона), `README.md`, `bmp/borders`, `bmp/textures`, `tmp`, 629 PNG);
  в `src` остались только `paths.assets_dir('<релатив>')/fire01.png` (эскиз апскейла, приватный каталог assets.base, в git не входит), `paths.assets_dir('<релатив>')/mm_ng_tt_transmuter.png` (эскиз апскейла, приватный каталог assets.base, в git не входит), `paths.assets_dir('<релатив>')/mm_tt_save.png` (эскиз апскейла, приватный каталог assets.base, в git не входит),
  `paths.assets_dir('<релатив>')/msg_btm_alt.png` (эскиз апскейла, приватный каталог assets.base, в git не входит), `paths.assets_dir('<релатив>')/ship_deck01.png` (эскиз апскейла, приватный каталог assets.base, в git не входит). Всё удалённое восстанавливается из
  `LethalDreams_textures_original_btf2x` + `tools/btf/unpack_btf.py` (канонические dims).
  Устаревшие 4 кривых 2x-PNG удалены из `dst` (`mm_ng_tt_transmuter`, `mm_tt_save`,
  `msg_btm_alt`, `ship_deck01`; `fire01` в `dst` не было), `dst` = 646 PNG.
  Репак 5 — по шаблонам из бекапа: `.tmp/repack_fixed.py [--copy]`.
- `[U]` «готово» (Upscayl сделал 5 файлов 1024x256). `[P]` `.tmp/repack_fixed.py --copy`:
  pack по шаблонам `LethalDreams_textures_original_btf2x` (канонические w,h 512x128 →
  1024x256, blob 1048576 / 786432), строгая проверка (w,h по каноническим офсетам, поле
  blob_len, blob побайтово == PNG, заголовок совпадает с шаблоном вне пропатченных клеток)
  — **5/5 OK**, записано в игру 5. Итоговая сверка `tools/archive/texture_qc/all_check.py`:
  `dims-ok` **1249**, побайтовое совпадение с апскейлом **651/651** (0 расхождений),
  598 batch-1 файлов — только по dims, `untouched-original` 5, sidecar'ы 12,
  `canon-unparsable` 4 индексированных глифа. Игра не запускалась — визуальную проверку
  делает пользователь.

## Декомпилят пишет ИНДЕКС константы вместо литерала (2026-11-15, проверено live)

Ключевой дефект базы правок. Декомпиллятор в списках параметров движка печатает
**индекс `co_consts` как целое** вместо значения:

```
Оригинал (dis backup_orig/ui/battlemenu.pyc, DM_Pane):
  [750, 0, 244, 50, ui.GUI_TNORMAL, '', 0, [ CONST7 ''(строка), CONST8 'ld_uncial12',
                                            FONT_H, CONST3 1, CONST2 0, CONST2 0 ]]
Декомпилят (decompiled_snap/ui/battlemenu.py, там же):
  [750, 0, 244, 50, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]]
```

- `[P]` Итог в игре: `TextParams[0] = 7` (int) -> движок в `ui/ui.py` (InitBillboard)
  зовёт `ui_macros.RT_ParseText(7)` -> `string.find(int, …)` ->
  `AttributeError: 'int' object has no attribute 'find'`. Создание `CBattlemenu`
  обрывается, **боевой HUD (BHUD/миникарта/кнопки/учебный диалог) исчезает целиком**
  в 4K и в 4:3. Точное место найдено логированием исключений
  (`ui/battlemenu.py: _plog/_pexc`, `traceback.extract_tb`):

```
BM-B0EXC 'int' object has no attribute 'find'
  | patches/ui/battlemenu.py:961 self.StatDisplay = ui.BActionElement([30,100,300,300, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]], [], [])
  | ./Data/Scripts/ui/ui.py:1169 | patches/hd_scale.py:267 | ./Data/Scripts/ui/ui.py:386
  | ./Data/Scripts/ui/ui_macros.py:123 | lib/string.py:179
```

- `[P]` Паритетный гейт эту порчу не замечает: `pycbuild.signature` сравнивает таблицу
  `co_consts`, а индексы 7,8,3,2,2 совпадают с уже имеющимися int-константами того же
  объекта -> `sig=0`, близость 0.9950. Вывод «патч поверх декомпилята безопасен»
 (старая запись в `docs/NOTES.md` (файл удалён; содержание распределено по docs/**, указатель docs/INDEX.md), файл удалён; содержание распределено по docs/**,
  указатель `docs/INDEX.md`) **ложный**.
- `[P]` Средства восстановления литералов, стоящие в проекте, для battlemenu **не работают**:
  `python3 tools/pyc/pycbuild.py fixints ui/battlemenu` -> `заменено 0`;
  `python3 tools/pyc/pycbuild.py fixlits ui/battlemenu` -> `литералов 96, подставлено 0`.
  Причина fixints: выравнивание кодовых объектов по индексу дерева; декомпилят содержит
  124 кодовых объекта против 126 в оригинале (декомпиллятор теряет функции) -> после
  первого расхождения все потоки токенов расходятся. `fixlits` (`reconstruct_literals`)
  ловит только простые `self.X = литерал`/`x['k']=v`, списочные литералы он не восстанавливает.
- `[P]` Диагностика выравнивания: `tools/archive/tmp_scratch/fixints_debug.py` (A=126, B=124, кандидатов 3);
  `tools/pyc/parity_vs.py (собираемый блоб) <module> <orig.pyc>` — паритет против оригинала из `backup_orig`
  (`tools/pyc/pycbuild.py parity <m>` сравнивает с ЖИВОЙ игрой, где уже накатан наш патч!).
- `[P]` В `tools/pyc/py22_worker.py` добавлен режим `litfix2`: печатает `CODE side idx имя`,
  `CONST side idx hexrepr`, `TOK side idx j tok`, `SLINE idx j line` — позволяет
  выравнивать кодовые объекты **по именам** (готового декомпиллятора всё ещё нет).
- `[P]` Состояние игры: `ui/battlemenu.pyc` = **оригинал** (откат
  `python3 tools/pyc/pycbuild.py uninstall ui/battlemenu`, md5 совпадает с `backup_orig`).
  Правки боевого HUD (`XLeft`, `HD_EdgeFix`) остаются в `patches/ui/battlemenu.py`
  как запись идеи, **не устанавливаются**.
- Задание на литерально-точный декомпиллятор оформлено в `docs/archive/TASK_decompiler.md`
  (гейты: 0 расхождений `LOAD_CONST` по имени-выравниванию, `grep -c ld_uncial > 0`
  для battlemenu, отчёт по всем 434 модулям).

## Откат патчей и регресс 4 симптомов (2026-11-16)

- `[U]` «другой агент случайно откатил твои патчи, примени их ещё раз»; после перенкатывания:
  «перестаёт добавлять 1 пиксель к вертикальному разрешению в 4k -> возвращается мигание окна
  при alt+tab», «ролики проигрываются не отмасштабированными на весь экран», «курсор становится
  очень мелким», «ломается масштаб меню в окне 4:3»; пользователь вернул игру в хорошее состояние.
- `[P]` Причина: `patches/*.py` для `gamescript` и `ui/cursor` - СТАРЫЕ эксперименты (good-сборки
  собраны из других источников, не сохранённых в patches/):
  - `build/gamescript.py` (14.11 20:42, = good-сборка) Initialize: `w,h,bpp,f = hd_res.Read();
    sw,sh = gamemodule.GetScreenSize(); if sw==w and sh==h: h = h+1; SetDisplayMode(w,h,bpp//8,f);
    hd_res.Prepare(); hd_scale.HD_Init/install`. В `patches/gamescript.py` не было
    проверки GetScreenSize + `h+1` (фикс мигания alt+tab = +1 пиксель вертикали) и
    не было `hd_res.Prepare()`, добавлен зонд hp1.
  - `hd_res.Prepare()` (в игре hd_res.pyc, мой модуль `build/hd_res.py` (историческая ссылка; рабочая директория build/, в git не входит)): копирует активные
    ресурсы по режиму: `mm_bkg01_{43,169}.btf`/`mm_bkg01_black_*` -> `config[game.root]/data/textures/mm_bkg01.btf` (файл установки),
    и для ld_uncial12/ld_uncial20/ld_tahoma11/ld_numbers15 `.btf_43/_169` и `.fdf_43/_169` ->
    активные. Без Prepare активные шрифты/фон остаются от ПРОШЛОГО режима ->
    масштаб меню в 4:3-окне ломается, ролики/фон не отмасштабированы.
  - good `ui/cursor.pyc` cur_table: все курсоры `[32,32]`, hot `2,2` (arrow/move/deny) и
    `16,16` (spell/spell_deny/disband/disband_deny/ready); `hm` - заглушка. В `patches/ui/cursor.py`
    были `[2,2]`+hot `3,3/7,7` (эксперимент) -> мелкий курсор.
- `[P]` Фиксы: `cp build/gamescript.py patches/gamescript.py`; `patches/ui/cursor.py`: cur_table
  -> 32,32 с hot 2,2/16,16; `hm` и `ui/ui._hp` -> заглушки (dev-зонды, запись выключена).
  Перенкатаны gamescript, ui/cursor, ui/ui, hd_scale, ui/battlemenu, ui/btutorial, ui/olmenu.
  Проверки: installed gamescript Initialize побитово == build/gamescript.pyc (good);
  cursor cur_table consts = good; hd_scale/battlemenu/btutorial/olmenu дельты = 16:9-фиксы
  (hover-сдвиг EdgeFix, HD_GTH_F, console-границы/-sh, console в EdgeFix) + зонды, identity при k=1.
- `[P]` Съёмки после перенкатывания: 4:3 `bt43chk_*` - окно 1920x1440, Prepare применил `_43`
  (md5 ld_uncial12.fdf == _43, config[game.root]/data/textures/mm_bkg01.btf (файл установки) == _43); 4K `bt4kchk2_*` - окно 3840x**2161** (+1 px
  восстановлен), Prepare применил `_169`; tab1..3 (Enter x12, Tab x3) - журнал боя открывается
  прижатым к левому краю, закрывается тонкой полоской у края (16:9-фиксы живы).
- Состояние: EXE = exepatch12 (md5 171319bb...), игра закрыта, hd.cfg = 3840 2160 32 1.
  Урок: источники installed good-сборок должны храниться в patches/ (старые .pyc в игре
  перезаписываются tools/pyc/install_patch.py из patches/*.py).


- `[P]` Удалены испорченные артефакты курсора (файлы в `build/ui/` и снимок установленного
  pyc в `backup_current_pycinstalled/ui/` с таблицей `[2,2]`/hot `3,3,7,7` из ручных правок
  build-файла). `decompiled_snap/ui/cursor.py`
  ЧИСТЫЙ: таблица записана индексами констант оригинала -> [1,2,2,3,3] = cursor_arrow,32,32,2,2
  (значения good); `decompiled_fixed/ui/cursor.py` содержит шум декомпилятора (h=2 вместо 32 в
  spell/disband/ready) - исследовательский артефакт, для сборок не используется. Правильные
  значения живут в `patches/ui/cursor.py`; игра перенкатана (md5 game == staged).


### Рамка `WS_BORDER` вместо `h+1` — мигание уходит, но способ непригоден (16.11.2026)

**Симптом:** borderless `3840x2160` (окно ровно = экран) даёт OSD монитора и мигание на
запуске/alt-tab/выходе.

- `[P]` Эксперимент: добавить `WS_BORDER` к стилю главного окна — единственный байт
  fileoff 149932, `push 0x80000000` → `push 0x80800000` (`tools/exe/exepatch_frame1.py`).
  Окно остаётся `3840x2160` в (0,0), клиентская (WGL) поверхность становится
  `3838x2158`, 1 px не-клиентской рамки рисует Windows.
- `[U]` «мигание пропало, но скролл у краев экрана стал нестабильным, плюс рамка в 1 пиксель
  видна». `[P]` Причина нестабильного скролла: движок хранит `w,h` из аргумента
  `SetDisplayMode` (3840x2160), а реальная поверхность `3838x2158` — краевые зоны
  курсора уходят за.drawable и детект краевое прокрутки рвётся. Рамка видна → вариант отклонён.
- `[P]` Откат: `python3 tools/exe/exepatch_frame1.py --restore` (EXE = эталон
  `171319bbc170…`) + `h+1` в `patches/gamescript.py` (установленный `gamescript.pyc`
  md5 `3ccbe44584db…`). `check_state.py --quick` — только два известных дрейфа.
- `[P]` MPO-путь тоже проверен и отклонён: `HKLM\SOFTWARE\Microsoft\Windows\Dwm
  OverlayTestMode=5`, перезагрузка — `[U]` «перезагрузился, не помогло, откатывай реестр».
  Ключ удалён.

### Пещера в `.rdata` для 4:3 роликов — игра падает, путь закрыт (16.11.2026)

**Симптом:** после применения патча игра не запускается/падает через несколько секунд
(`[U]` «Игра не запускается»).

- `[P]` Правка: сайт `0x49a322` (`FUN_0049a1a0`, вызов `IVideoWindow::SetWindowPosition`,
  vtable+0x9c) -> `e9 rel32` + 17 nop; пещера 49 B в zero-run fileoff 1109104
  (VA `0x50ec70`, зона 916 B) внутри **`.rdata`**; секция `.rdata` сделана исполняемой
  (flags fileoff 604: `0x40000040` -> `0xe0000040`). Инструмент:
  `tools/exe/exepatch_video43.py` (`--install`/`--restore`), пещера
  `tools/exe/caves/video43.s` (сборка `sh tools/exe/build_cave.sh video43`).
- `[P]` Падение воспроизводится и на **identity**-пещере (аргументы те же, что в оригинале),
  и на пещере, которая только **пропускает** вызов (`jmp` обратно) — т.е. дело не в
  числах 4:3, а в механике site-jmp + пещеры вне `.text`.
- `[P]` WER: `0xc0000005`, faulting address `0x45046000`, faulting module = unknown
  (адрес вне образа) — признака падения в самой пещере нет. ASLR выключен, relocate-таблица
  пуста (`DLL characteristics 010f`, `reloc dir size=0`), т.е. абсолютные адреса в пещере
  корректны, и причина не в rebasing.
- `[P]` Свободного места в `.text` нет: zero-run'ов >=16 B в `.text` (`file 4096..966656`)
  ноль, `nop`/`int3`-паддинга тоже нет; рабочая зона пещер — только `0x4ebf12..0x4ec000`
  (238 B, из них 236 занято `fixall.bin`).

### Скролл вниз мышью не работает в 16:9 (17.11.2026)

**Симптом:** `[U]` «не работает скролл экрана мышью вниз; вверх, влево и вправо —
работает; клавиша «вниз» работает». Проверка: загрузить сейв и двигать мышь к краям
экрана. Подозрение пользователя было на `h+1` (фикс мигания окном +1 px) — оно
**не подтвердилось**: при `h=2160` (`k=0.75`) порог тоже недостижим, т.е. нижний
скролл сломан во всех 16:9-конфигурациях `F=1`.

- `[P]` Причина: `config[game.root]/data/scripts/ui/olcamera.pyc` регистрирует
  `h_MouseMove` на **сырое** движковое событие `WMO_MOUSEMOVE` (10), а не на
  релейтированный курсором `WM_MOUSEMOVE` (11). Значит `ms_lastx/ms_lasty` в
  `h_Update` — **движковые** координаты мыши, а пороги краёв в том же `h_Update`
  (`screen_size = [1022, 766]`) — **канвасные** числа. При `3840x2161 F=1`
  (`k=0.750347`, `s=w/1024=3.75`) движковый диапазон X = `w/s` = 1024 (достигает
  порога 1021), а Y = `h/s` = `768*k` = 576 < 765 — нижний порог недостижим никогда.
  Верх/лево (`x==0`, `y==0`) и право (`x>1021`) достижимы → работают.
- `[P]` Измерение (scratch-дерево, зонд `y/x/thrY/pos`, загрузка сейва, курсор ОС
  по краям): мышь у нижней видимой строки даёт движковый `y=575`; базовый порог 765
  не достигается (скролл вниз мёртв, камера за 1.5 с не двинулась), порог правки
  `SY(765)=574` достигается → камера `(55.00,31.73)→(55.00,60.00)` = скролл вниз
  работает (A/B: патч `sf4` vs база `sf6`, один зонд, одно дерево).
- `[P]` Обход/правка: `release/patches/ui_olcamera.patch` — порог низа = движковая
  координата нижнего края канваса `hd_scale.SY(screen_size[1]-1)` (метод `HDEdgeY`).
  При `k=1` `SY` identity → порог побитово родный 765, 4:3 не затронут. Установка:
  `python3 tools/apply_patch.py ui/olcamera && python3 tools/pyc/install_patch.py ui/olcamera --install`.
- `[P]` `[U]` После подтверждения скролла на глобальной карте — «в бою всё так же не
  работает» (проверка: новая игра + 15 Enter для скипа обучения). Тот же механизм во
  втором модуле: `config[game.root]/data/scripts/battlecamera.pyc` (`B_Init` регистрирует
  `h_MouseMove` на `WMO_MOUSEMOVE`, строка 141; `screen_size=[1022,766]` строка 137;
  пороги в `hB_Update` строки 335/341). Тот же патч по тому же образцу —
  `release/patches/battlecamera.patch`.
- `[P]` Не путать с механизмом рамки `WS_BORDER` (пункт «Рамка `WS_BORDER` вместо `h+1`»):
  там мышь уходит в зону вне движковых `w,h` (клиент 2 px меньше), а здесь порог
  недостижим по единицам измерения при корректном окне.

## Спайк `.pyd` (Фаза 0-бис, 18.11.2026): грабли расширений 2.2.3 и скриншотов

- `[P]` **`Py_InitModule(name, NULL)` в 2.2.3 = `0xC0000005` внутри `python22.dll`.**
  Симптом: `imp.load_dynamic` из игрового скрипта исполняет `initldhook`
  (маркер записан), затем процесс падает; offset падения в `python22.dll`
  `0x4e4de` (WER Application Error). Причина: `Py_InitModule4`
  (`Python/modsupport.c`) разыменовывает `methods` без проверки NULL
  (`for (ml = methods; ml->ml_name != NULL; ml++)`) — NULL-таблица читается по
  адресу ~0. **Обход:** передавать пустую таблицу `{NULL, NULL, 0, NULL}`.
  Локальный репро без игры: `vendor/py223/PYTHON.EXE -c "import imp;
  imp.load_dynamic('ldhook', '<путь>/ldhook.pyd')"` (rc=139 до фикса, rc=0 после).
- `[P]` **AppCompat-флаг `~ HIGHDPIAWARE` привязан к ПОЛНОМУ ПУТИ exe.**
  Установка (`config[game.root]/LETHALDREAMS.EXE`) имеет флаг в
  `HKCU\...\AppCompatFlags\Layers`; копия игры в scratch-дереве — нет, поэтому
  окно игры масштабируется системой на 175%: `GetWindowRect` = 6720x3780
  вместо 3840x2161, снимок через `CopyFromScreen` промахивается/ловит рабочий стол.
  **Обход:** ставить тот же флаг для scratch-пути (PowerShell `Set-ItemProperty`,
  Cygwin-python без `winreg`) + снимать с `SetProcessDpiAwareness(2)` ДО загрузки
  `System.Drawing` (иначе WinForms кэширует логические координаты 2194x1234 и
  `Screen.Bounds` врёт; границы экрана брать через `GetSystemMetrics(0/1)`).
- `[P]` **Окно игры может быть свёрнуто в момент снимка** (игра стартует и
  теряет фокус): `GetWindowRect` свёрнутого окна возвращает restore-rect, а
  `CopyFromScreen` этого места снимает рабочий стол («на скриншоте игра свёрнута»).
  **Обход:** перед снимком `IsIconic` → `ShowWindow(h, 9)` (SW_RESTORE),
  `SetWindowPos(HWND_TOP)`, `SetForegroundWindow`, выдержка ~2 с, затем снимок
  пересечения окна с экраном (`tools/ldhook/probe_spike.ps1`).
- `[P]` **GDI-захват (`CopyFromScreen`) отдаёт УСТАРЕВШИЙ кадр при заблокированной
  сессии/погасшем мониторе.** Симптом: два снимка с разницей в минуту показывают
  одно и то же (часы на панели задач застыли, окно игры — foreground/topmost/visible
  по Win32 — на снимке отсутствует). Проверка: кроп часов tasks — время не
  двигается. **Обход:** снимать только при активном разблокированном дисплее;
  `PrintWindow` для GL-окна игры даёт чёрный кадр (36 КБ на 3840x2161) — не спасает.
- `[P]` **Логгер в per-frame хуке нельзя писать «открыл файл на строку».**
  site3 (`0x47dd80`) зовётся каждый кадр (~60/с), а `CreateFile`+`CloseHandle`
  на каждую строку — синхронный I/O в рендер-потоке: кадр проседает, файл
  растёт безгранично при включённой трассировке. **Обход (Фаза 3):** дескриптор
  открывается лениво и живёт до конца процесса, запись под `CRITICAL_SECTION` +
  `SetFilePointer(END)`; трассировка уровня 2 троттлится (site1/site2 — первые 4
  и каждый 1024-й, site3 — первые 3 и каждый 300-й); файл > 8 MiB ротируется в
  `hdlog.txt.1`. Info-строки (install/guard/FIRED) пишутся один раз.
- `[P]` **Скан `.text` по константам `0x1000/0xeaf12` опасен вне игры.**
  При локальной загрузке под `vendor/py223/PYTHON.EXE` (смоук Python-API
  `ldhook.log/set_level/stats`) `.text` python.exe — `0x1e2` байта, и жёсткий
  диапазон `0xeaf12` уходит за отображённую секцию → `0xC0000005`.
  **Обход (Фаза 3):** границы `.text` брать из PE-заголовков (`IMAGE_NT_HEADERS`
  → `IMAGE_SECTION_HEADER` `.text`: `VirtualAddress` + `VirtualSize`, при нуле —
  `SizeOfRawData`); если секция не найдена — хуки пропускаются с info-строкой.

---

# docs/PATCHES.md (перенос 22.11.2026)

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
- `[P]` dxwrapper вынесен из игры (`config[game.root]/dxwrapper.dll` (в установке отсутствует), `config[game.root]/dxwrapper.ini` (в установке отсутствует), `dsound_stub.bak`,
  `config[game.root]/dxwrapper-lethaldreams.log` (в установке отсутствует) -> `scratch_dir('removed')/`); звук — системный dsound.
- `[P]` Проверка `tools/archive/probes/probe_f1.ps1` (прогон f1b, `hd.cfg = 3840 2160 32 1`):
  `MODE before/during/after_switch_out/after_switch_back` = `3840x2160 bpp=32 hz=120`
  (режим не меняется); `WINDOW rect=3840x2160 at (0,0) style=0x94000000`
  (= WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS) `exstyle=0x00040000` (APPWINDOW);
  `ALT-TAB` в notepad 1 ms, назад 75 ms. Снимки `scratch_dir('ld')/f1b_win.png` (снимок прогона), `scratch_dir('ld')/f1b_desktop.png` (снимок прогона).
- `[?]` За пользователем: визуально подтвердить отсутствие мигания при alt-tab/выходе и
  «точку строго под пальцем» в F=1 (`MY = m/k`).
- `[P]` Фон 16:9 (`config[game.root]/data/textures/mm_bkg01_169.btf` (файл установки)) = 2048x2048, арт в строках 896..2047 (замер офлайн
  по payload: first-non-black=896, last=2044). **Исправлено 2026-11-14: `_bg_fix` =
  pass-through** (фон передаётся движку без пересчёта, канвас 1024x1024). В F=1 движок
  растягивает текстуру по rect единым `s=w/1024` и клиппит по окну, blit инвертирует
  строки (как `qrs_battle`): видимые канвас-строки 0..h/s = арт заполняет окно ровно,
  aspect един (3840/2160 = 2048/1152), чёрного прямоугольника снизу нет.
  Старое `sy2 = HD_SY^2` сжатие давало неединый Y-масштаб (x=1.875, y~1.05) и чёрную
  полосу ~945 px снизу. Замер снимка `scratch_dir('ld')/bgpt_5s_win.png` (снимок прогона) (`tools/archive/probes/measure_bgrows.ps1`):
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
- `[U]` Прежний референс `scratch_dir('ld')/ref43_dialog_nocursor.png` (снимок прогона) снят в момент fade-in
  (текст тёмный) -> замер «доля 0.56» по нему неточен. Свежий эталон:
  `scratch_dir('ld')/r43new_dialog_nocursor.png` (снимок прогона) (1280x960, родные листы).
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
  Сравнение родной/новый лист: `scratch_dir('ld')/cmp_nast20.png` (снимок прогона), `scratch_dir('ld')/cmp_tahoma_body.png` (снимок прогона), `scratch_dir('ld')/cmp_numbers.png` (снимок прогона).
- `[P]` Живые замеры (`tools/archive/probes/measure_caption.ps1` — полосы + ink bbox, `tools/archive/probes/stack_crops.ps1` — base/new):
  - 4K `newgamemenu`: межстрочный шаг 45 px (не изменился), высота текстовой строки
    28 -> 24 px (**0.857**), позиции строк те же; ширина строк ink не изменилась.
  - 4K диалог настроек: полоса заголовка на тёмном фоне 67 -> 47 px;
    ink x-бокс строки идентичен (ширина строки прежняя).
  - 2K диалог: левый заголовок 36 -> 30 px (**0.833**), ink x 625..809 (ширина та же).
  - 4:3 (`1280 960 32 0`): load-имена = родные `*_43`, diff со свежим эталоном
    `scratch_dir('ld')/r43new_dialog_nocursor.png` (снимок прогона): meanDelta 0.35, шум дизеринга — побитово прежнее.
- Инструменты: `tools/archive/font_gen/rescale_font4.py` (генератор + `--install`), `tools/fonts/font_stats.py`,
  `tools/fonts/font_preview.py`, `tools/archive/probes/measure_inkbox.ps1`, `tools/archive/probes/measure_caption.ps1`, `tools/measure/crop_rect.ps1`,
  `tools/archive/probes/stack_crops.ps1`, `tools/measure/downscale.ps1`, `tools/measure/pngdiff.py`.
- Итог установки: `python3 tools/archive/font_gen/rescale_font4.py -kx 0.75 -fx 0.7125 -fy 0.85 --install`
  (выход в `scratch_dir('ld169b')`).
## Патч EXE: глифы UI (сессия 2026-11-15)
- `[P]` Живая линия отрисовки глифов найдена: сайт `0x470932` — `push edi; push 0x1403; push ecx; push edx; call [0x51cd90] (glDrawElements)`; возврат `0x470938`; функция заканчивается `pop edi; pop esi; ret 0x14`. Запись вершины: база `esi+0xc`, stride `0x20`, **x = float@+0xc, y = float@+0x10**; индексы — ushort по `edi`, count = `ecx`.
- `[P]` Live-мутация ×0.75 вокруг центра квада на этом сайте доказана: цифровая полоса интро 87→57 px (`scratch_dir('ld')/scale75_win.png` (снимок прогона)) — рычаг работает.
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
  `0x40b494`, пещера `0x4ebf12`) работают — см. `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py` (исторический скрипт (не в git)).
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
- `[P]` `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch8.py` (исторический скрипт (не в git)): сайт `0x41ce31` (5 байт `c6 41 58 01 59`) → `jmp 0x4ebf12`;
  пещера 120 байт (`tools/exe/caves/fontscale.s` (собираемый блоб), сборка `tools/exe/build_cave.sh`) ставит флаг, считает
  `k = h*1024/(w*768)` из `[0x51cc7c]+0x18/+0x1c`, зажимает `k>1` в 1.0 (4:3 → прежнее),
  умножает `pitch` и все 256 `slot4`, затем `pop ecx; ret`. `python3 приватный архив сессий Project/1.7z, entry LethalDreams/exepatch8.py (исторический скрипт (не в git)) restore`
  возвращает `backup/LETHALDREAMS.EXE.pre4`. md5 патча `cce4537307ba83525f8ca0554c3584eb`.
- `[P]` Проверка: 4K + РОДНЫЕ листы (`tools/fonts/restore_fonts.py`) — `scratch_dir('ld')/base8_4k_dialog_nocursor.png` (снимок прогона)
  (заголовок 478 px) → `scratch_dir('ld')/p8_4k_dialog_nocursor.png` (снимок прогона) (365 px, ×0.764), резко, без кропа;
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
- `[P]` `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py` (исторический скрипт (не в git)) (два сайта + блоб 192 Б в 0x4ebf12: cave1 0x00..0x7a, cave2 0x80..0xad,
  data 0xb0/0xb8). md5 патча `49976478943f0e2871872a2c6fb55f28`, откат `приватный архив сессий Project/1.7z, entry LethalDreams/exepatch9.py (исторический скрипт (не в git)) restore`.
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
  (`tools/exe/caves/fontscale4x.s` (собираемый блоб) + `tools/archive/cave_gen/build_cave4x.py` (собираемый блоб)): `k_eff = k*0.25`, **при `k == 1` — identity**
  (это критично: иначе 4:3 с родными листами получил бы текст в 0.25x; первый прогон это поймал),
  и `adv = (a_sheet - CORR)*k_eff + carry`, `CORR = 4*A = 1.3384`. Полярность проверки:
  `fld1; fcomip; je isone`, зажим `k>1 -> 1`.
- `[P]` Статически: пещера 224 Б влезает в zero-run 238 Б; константы (`c1024`,`c768`,`csheet`,
  `ccorr`) — последними, чтобы не платить padding раздела; из `cave2` убран guard `pitch!=0`
  (нужен был 8 байт), сам вызов `FUN_0041cc90` остался.
- Инструменты: `tools/archive/font_gen/fontqc.py` (собираемый блоб) (QC апскейла), `tools/archive/font_gen/fontsrc_export.py` (родные PNG + сетка
  ячеек), `tools/fonts/gen_font4x.py`, `tools/archive/exe_patch/exepatch10.py`, `tools/archive/cave_gen/build_cave4x.py` (собираемый блоб), `tools/measure/logrows.py` (сводка
  `HDINITBB` по классам pitch/RowS).
- `[P]` Состояние на диск: EXE = `exepatch10` (md5 `51f69974e1dada6e7b191961e853a1ef`),
  сайдкары `*_169` (4x remacri) + `*_43` (родные) в `data/textures`+`data/fonts`, `hd.cfg` =
  `3840 2160 32 1`, игра остановлена.
- `[P]` **In-memory эквивалент (18.11.2026, `tools/ldhook/`)**: те же три сайта
  (`0x41ce31`, `0x40b494`, `0x47dd80`) ставятся сканом сигнатур в памяти
  (`ldhook.pyd`, `E9 rel32` через `VirtualProtect`), тело — C-порт `fixall.s`;
  EXE на диске не меняется. Гейт: site3 даёт `matrix[0] = 0x3F7987FA`
  (= exepatch12), 4:3 — guard identity (записей нет), кольцо A/B с cave-EXE
  совпадает в пределах 1–2 px. Статус/замеры — `docs/PLAN_proxy_dll.md` §4
  Фазы 1–2, `tools/ldhook/README.md`.
- `[P]` **Решение Фазы 4 (18.11.2026): EXE не трогаем, установка остаётся на
  `exepatch12`** (`171319bb…`), `ldhook.pyd` в поставку не входит — это
  проверенный dev-путь для следующих правок движка (логгер Фазы 3, скан
  `check_sites.py`: baseline `installable`, cave `patched`). Переход на `.pyd`
  (baseline EXE + `ldhook.pyd` в allowlist + `ui/ui.pyc` с `imp.load_dynamic`)
  выполняется целиком по чек-листу `docs/PLAN_proxy_dll.md` §4 Фаза 4, когда
  сработают критерии §5; fallback — вариант А1 (статические `E9`, тело в DLL).
  `exepatch12.py` остаётся эталоном и откатом. `docs/PLAN_proxy_dll.md`.
Base-требования к декомпиляту — `docs/PIPELINE.md`; запись о дефекте старой базы — `docs/PITFALLS.md`.
## Боевой HUD поверх recovered-базы (2026-11-15, вечер)

Другой агент написал `tools/pyc/decompile_lit.py`; выход — `decompiled_fixed/` (все 434 модуля).
Для `ui/battlemenu`: `grep -c ld_uncial` = 9 (в декомпиляте 0), пример
`self.StatDisplay = ui.BActionElement([30, 100, 300, 300, ui.GUI_TNORMAL, '', 0,
['', 'ld_uncial12', FONT_H, 1, 0, 0]], [], [])`; паритет против
`backup_orig/ui/battlemenu.pyc`: code objects 124 = 124, близость **0.9979**,
`sig=0`, `ops=9` (только артефакты `RETURN_VALUE` и LOAD_GLOBAL/LOAD_NAME) ->
база пригодна для правок. Сравнение делает `tools/pyc/parity_file.py (собираемый блоб) <orig.pyc> <file.py>`.

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
- `[P]` Результат 4K (`scratch_dir('ld')/p4kbt4_overland.png` (снимок прогона), сравнение с baseline `scratch_dir('ld')/base4kbt3_overland.png` (снимок прогона)):
  левый BHUD-блок прижат к левому и нижнему краю (кроп `scratch_dir('ld')/cmp4k_left_new.png` (снимок прогона)
  0..720 x 1440..2160: артка рамки начинается у x=0; в baseline блок начинается
  у x=340), миникарта прижата к правому/нижнему краю
  (кроп `scratch_dir('ld')/cmp4k_right_new.png` (снимок прогона) 3120..3840); пропорции арта и кнопок = 4:3, растяжения нет.
  Консольная чёрная полоска у левого края осталась (её не сдвигаем).
- `[P]` Регрессия 4:3 (`scratch_dir('ld')/p43bt5_overland.png` (снимок прогона) vs baseline `scratch_dir('ld')/base43bt2_overland.png` (снимок прогона),
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
  Кадр `scratch_dir('ld')/bt4kn7_overland.png` (снимок прогона): большой чёрный прямоугольник исчез. При 4:3 сдвиг=0 — identity.
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
  полоска у края, что и в эталоне. Снимки `scratch_dir('ld')/bt4kn9_tab1.png` (снимок прогона) (открыто у левого края с
  border-крышками), `scratch_dir('ld')/bt4kn9_tab2.png` (снимок прогона) (закрыто — тонкая полоска), `scratch_dir('ld')/bt4kn9_tab3.png` (снимок прогона)
  (опять открыто) — посадка совпадает с 4:3 пропорциями.
- `[P]` Съёмка: скрипт `tools/archive/probes/probe_loadshot.ps1` расширен `-Tabs N -TabDelay S` (keybd VK 0x09,
  снимок после каждого Tab). Путь боя: `-EnterCount 12` (6-10+ по слову пользователя;
  часть Enter уходит на страницы уроков; в 16:9 уроки встают на странице «Выберите
  главного персонажа, щелкнув…» — нужен клик по юниту в бою, скрипт его не делает).
- `[P]` `hd_scale._log` лимит строк поднят 4000→12000 (в 16:9 прогон упирался в лимит —
  зонды CmdConsole/CONSOLE не писались).
- `[P]` Регресс 4:3 (`scratch_dir('ld')/bt43t3_overland.png` (снимок прогона) vs `scratch_dir('ld')/bt43n_overland.png` (снимок прогона), region_diff battle):
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
  `bspellbook.py` (историческая ссылка) / enmenu.py (историческая ссылка) / help.py (историческая ссылка) остаются ОРИГИНАЛЬНЫМИ (в игре они == backup_orig).
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
  `resources/fontsq/`, `patches/ui/{bspellbook,enmenu,help}.py(.pyc)`, `config[game.root]/help_game.pyc (в установке отсутствует).keep`.
  В игре Q-сайдкаров не было (data/fonts и config[game.root]/data/common.tt (файл установки) чистые). hd.cfg — `3840 2160 32 1`.
- `[P]` `player_dialog.tga` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z) (фон `talk_dlg`, диалог с NPC из `ui/btalk.py`) добавлен в
  `DIALOG_TEX` того же патча 16.11.2026: rect шаблона `[0,0,1024,1024]`, но арт
  (альфа-bbox) = canvas `x134..890 y6..643`, т.е. композиция тоже канвас 1024x768 —
  в 16:9 он шёл `_is_bg` pass-through и растягивался на всю ширину. Из остальных
  2048x2048 вне патча: `sb_bkg` не упоминается нигде (мёртвый файл: нет в `.tt`,
  скриптах, EXE/DLL), `ter_fort01` — текстура мира (`config[game.root]/data/objects_tex.tt` (файл установки)), не UI.
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
  `1794819372`), `.py` md5 `dc8314f90af0…` (31914 B); манифест перечислен по новой сборке.
  Живой прогон после установки: тултипы у курсора в левом нижнем углу.
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
  - ошибок Python нет (`PythonLogFile` — только доброкачественные `ImportError` деревьев).
- `[P]` Установка (18.11.2026): `data/scripts/battlecamera.pyc` md5
  `91be96e74662448a5672b3c6edcace28` (27536 B, crc32 `e143e240`, mtime `1794778203`),
  `.py` md5 `5f757d07164f1fb54bd568c47cd9ea02` (25459 B). Каноническая сборка:
  `tools/apply_patch.py battlecamera` → `.py`/`.pyc` md5 == манифест;
  `tools/release/update_entry.py battlecamera` — пересборка `.patch` + записи манифеста.
- `[P]` Уроки «Игровая камера и панели» **не снимаются Enter'ом** (15 и 30 Enter не
  помогли): урок ждёт действия игрока, кнопка «Далее» на нём заблокирована
  (`btutorial.EnableNext(0)`), а «Игра в паузе» держит сцену. Камера при этом
  обновляется (`hB_Update` идёт), поэтому замеры камеры на уроке валидны.
