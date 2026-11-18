# OBSERVATIONS_TEXTURE.md — наблюдения: раскод/формат `.btf`, packer, курсоры, иконки, апскейлы

Канон раскодирования blob'ов, формат манифестов `.tt`, замеры апскейлов и переписей.
Пайплайн unpack/pack — `docs/PIPELINE.md`; инструменты — `docs/TOOLS.md`; порядок чтения — `docs/INDEX.md`.


## Ресурсы текстур (`[P]` расшифковано офлайн)
- Архивы `.tt` (plain text, CRLF): `config[game.root]/data/menutextures.tt` (файл установки) = список
  `имя.bmp | textures\имя.btf`; движок грузит `data/textures/<name>.btf`.
- `.btf` = `LDPACKER\` + `<u32 заголовка>` + zlib(zlib, magic 78 DA) поток;
  распакованный payload = `mdf ` (u32), блоки TLV: `(type=1, count=116)`,
  `(type=117, total)`, `(1,bpp=4)`, type 118: fields w,h (u32, при 1024x768: 1024,1024 и h=768)
  — пиксельный blob BGRA 1024x768 = 3145728 байт лежит в конце payload
  (`payload[-W*H*4:]`), header перед ним.
- Фон меню = `config[game.root]/data/textures/mm_bkg01.btf` (движок грузит texture по имени .bmp; бинар .btf лежит в установке, в git не входит) (mainmenu.py (историческая ссылка):190, `BActionElement([0,0,1024,1024,...,'mm_bkg01.bmp', BBlend_Replace])`)
  = `config[game.root]/data/textures/mm_bkg01.btf` (файл установки); раскодирован и положен на рабочий стол
  (`scratch_dir('desktop')/mm_bkg01.png`, 1024x768 RGBA).
- Отдельный 16:9-фон (решение пользователя): файл делает внешний агент;
  запакуем обратно в `LDPACKER+zlib+mdf` и подменим в архиве.
- `[U]` «это не та текстура, там серый прямоугольник»; «положи мне png на рабочий стол»;
  «16:9 сам не делай, я попрошу другого агента».

## Подтверждение раскода `.btf` (`[P]`)
- Раскод `config[game.root]/data/textures/mm_bkg01.btf` (файл установки) (row-major, stride W*4, BGRA, blob в хвосте payload) даёт
  канвас с глифами-глазами; эти же глифы = `mm_eye.tga` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z)/`mm_r_*` (кнопки меню из
  `config[game.root]/data/menutextures.tt` (файл установки)) -> layout раскода согласуется с UI-текстурами, раскод корректен.
- Фон меню при отрисовке = `config[game.root]/data/textures/mm_bkg01.btf` (движок грузит texture по имени .bmp; бинар .btf лежит в установке, в git не входит) (mainmenu.py (историческая ссылка):190, BBlend_Replace, [0,0,1024,1024]);
  движок берёт `config[game.root]/data/textures/mm_bkg01.btf` (файл установки) (путь из `.tt`-манифеста).



## Канон раскода `.btf` (`[P]` 2026-11-14, подтверждено визуально)
- Инструменты: `tools/btf/unpack_btf.py` (unpack, CLI), `tools/btf/pack_btf.py` (pack).
- Два контейнера: **`LDPACKER`-файлы** = magic(8) + u32(plen) (+u32(clen)) + zlib-поток
  (`78 DA`) → payload `mdf `; и **raw-`mdf`** (27 файлов, без магии, без сжатия,
  payload = файл целиком: `bhud_spgauge`, `qrs_battle`, `cmd_run`, `hex_fog`, `lp_frame_r`...).
- Заголовок payload: TLV `(value,type)` пары `t/u/v/w/x`; имя файла (u32 len + ASCII);
  **w,h = u32 подряд (offset j, j+4) сразу после имени**; поля w*h, флаги; `'w'`=119
  blob_len (правило: u32 v по offset i, `i+4 == plen - v`, последнее совпадение).
- Классы по размеру blob: `w*h*3` → **24-bit interleaved RGB**; `w*h*4` → **32-bit
  interleaved RGBA** (альфа последним байтом); `w*h` → indexed (поле `'x'`, палитра
  внешняя — 7 мелких глифов, нерешено).
- Раскладка строк: **top-down** во всех классах. Порядок каналов: RGB/RGBA именно
  такие (не BGRA!) — подтверждено: `mm_bkg01` (коричневый фон+спираль, профиль
  R>G>B), `treant01` (зелёные кроны), `mm_eye` (кольцо), `ld_uncial20` (шрифт читается).
- Охват парсера: 1251/1258 (705×32bit, 546×24bit, фейлов 0); 7 мелких UI-глифов с
  blob < w*h (поле `'x'`) — отдельная задача.
- `config[game.root]/data/textures/qrs_battle.btf` (файл установки) лежит зеркально ([P]: текст читается только при `--flip`); движок
  отзеркаливает при blit. В packer — PNG подавай зеркальный.
- Движковый лимит текстуры: поле `33554432` в заголовке = 32 MiB blob.
- Ориентация контента: `mm_bkg01` содержит реальные чёрные строки сверху (~230 строк,
  RGB-средний ≈4) — это art-полотно, игра кадрирует центр; не padding.

## Packer `.btf` (`[P]`)
- `python3 tools/btf/pack_btf.py <in.png> <template.btf> -o <out.btf>`: шаблон даёт TLV-поля/имя/флаги; blob
  заменяется; патчатся w,h, `'t'`(plen-16), blob_len, контейнер (plen/clen), zlib level 9.
- **Размер берётся из PNG** (upscale поддержан); round-trip unpack→pack побайтово lossless
  (24-bit mm_bkg01, 32-bit treant01).
- `[P]` 16:9-фон `paths.assets_dir('<релатив>')/16_9.png` (эскиз апскейла, приватный каталог assets.base, в git не входит) (2048x2048) запакован и проверен в игре (`[U]` «всё ок»):
  ресурс `config[game.root]/data/textures/mm_bkg01_169.btf` (файл установки) лежит как отдельный файл `config[game.root]/data/textures/mm_bkg01_169.btf` (файл установки)
  (оригинал `config[game.root]/data/textures/mm_bkg01.btf` (файл установки) восстановлен), копия из архива
  распаковывается в `cache/hd_pack/data/textures/mm_bkg01_169.btf`
  (`python3 tools/extract_pristine.py extract`).
- Бэкапы оригиналов: `paths.btf_backup_dir()/` (родные бэкапы оригиналов; восстанавливаются из архивов: python3 tools/extract_pristine.py templates, в git не входит).
- `[U]` **Меню отображается сразу после запуска игры** — тот экран («Изменить» внизу
  справа) и есть главное меню; отдельного сплэш-экрана нет, ждать «поднятия меню» не нужно.
  Клик по глифу «Изменить» открывает диалог настроек; при 4K он у самого низа кадра.
- `[U]` **При 4K в `hd.cfg` обязателен флаг `1` (fullscreen)**: `3840 2160 32 1`.
  С флагом `0` (окно) масштаб меню ломается.
- `[U]` **Frida ломает масштабирование меню**: при подключённой сессии масштаб сбивается,
  глиф «Изменить» уезжает за нижний край, клики промахиваются. Frida на съёмках не
  используется; сессию закрывать (`close_frida_session`) до снимка.
- Шрифты игры: 4 листа, пара `.fdf` (таблица `char x y h`, header `name size sheet`) + `.btf`:
  `ld_uncial12` (256x256, pitch 25), `ld_uncial20` (512x256, pitch 38), `ld_tahoma11`
  (256x256, pitch 19), `ld_numbers15` (128x32, pitch 16).
- `[P]` Глиф-рендер EXE (`FUN_0041d020`): квад = advance (slot4) x межстрочный шаг
  (`obj+0x40` = pitch), UV-rect = `[slot0..slot2] x [slot1..slot3]`; масштаб передаётся
  полем `w = obj+0x1464 <- obj+0x54`. Парсер `.fdf` (`FUN_0041ce40`) пишет slot0=x, slot1=y,
  slot4=h (slot2/slot3 обнуляются парсером). Frida-хуки `FUN_0041d020`/`FUN_0041d460` при
  открытом диалоге НЕ срабатывают: UI-текст рисует BTexturer.dll (декомпилирована:
  `scratch_dir('btx_decompiled')/`).
- Родные листы шрифтов: восстановлены из PNG-декодов 18:18
 (`scratch_dir('ld')/font_ld_uncial12.png`, `scratch_dir('ld')/font_ld_uncial20.png` —
   распаковка листа шрифта скриптом `tools/btf/unpack_btf.py`; артефакт прогона, в git не входит) →
  `paths.btf_backup_dir()/ld_*_orig.btf` (родные бэкапы оригиналов; восстанавливаются из архивов: python3 tools/extract_pristine.py templates, в git не входит)
  (ink-пиксели 7242 / 13596; сжатые копии дают 5042..5431 / 11385..11926).
  Родные md5 до перезаписи: `ld_uncial12.btf` ac7edcd8258bee828746b10c865d5230,
  `ld_uncial20.btf` 50e16758d744cd1f14dfbb81085a9d85, `ld_tahoma11.btf`
  20a888bae16e42778d184a6a56097987, `ld_numbers15.btf` bb154bac8a96efa21e94b46a5f8c29dd.
- Загрязнение сайдкаров: генератор берёт базу из игрового файла, а Prepare после 16:9-прогона
  кладёт `_169` на load-имя → следующий прогон делает `_43` копией сжатого листа.
  Генератор обязан читать базу из бэкапа (`paths.btf_backup_dir()/ld_*_orig.btf` (родные бэкапы оригиналов; восстанавливаются из архивов: python3 tools/extract_pristine.py templates, в git не входит)),
  не из игры.
- GL error 0x502 при загрузке 2048-текстуры наблюдался, игра работала корректно (`[U]` ok).



## Выгрузка исходных текстур в PNG для апскейла (2026-11-15)
- `[U]` Задача: «сконвертируй оригинальные текстуры игры в png в отдельную папку на рабочем
  столе; текстуры, у которых уже есть sidecar-варианты, выгружать не надо» — пользователь
  будет делать апскейл и упаковывать обратно в игру. Ориентация выбрана пользователем
  **as-is (как в файле)** и обычные BMP из `wrender` — положить в подпапку.
- `[P]` Выгружено: **1249** текстур `data/textures/*.btf` → `scratch_dir('textures_src')/`
  (702 трёхканальных+альфа, 547 24-бит; ~98 МБ) + `bmp/borders/` (24) и `bmp/textures/` (120)
  из `data/wrender/figures/`. Скрипты: `tools/archive/texture_qc/export_textures.py`, `tools/archive/texture_qc/export_bmp.py`
 (раскодер `tools/btf/unpack_btf.py`, canon: top-down, RGB/RGBA). В папке `scratch_dir('ld')/MANIFEST.txt` (лог прогона)
  (`имя w h bpp blob`) и `README.md`. Все 1393 PNG сверены с манифестом (размер/RGBA/filter 0) — 0 расхождений.
- `[P]` Пропущено 17 файлов: сайдкары `config[game.root]/data/textures/*_43.btf` и
  `config[game.root]/data/textures/*_169.btf` (sidecar-пара в установке; создаёт пайплайн
  gen_font4x/pack_btf, в git не входит) и их базы
  (`mm_bkg01`, `ld_uncial12`, `ld_uncial20`, `ld_tahoma11`, `ld_numbers15`);
  `mm_bkg01_black_43/_169` — базы `mm_bkg01_black` в игре нет. Критерий: наличие
  `config[game.root]/data/textures/<base>_43.btf` или `config[game.root]/data/textures/<base>_169.btf`
 (sidecar-пара в установке; создаёт пайплайн gen_font4x/pack_btf, в git не входит) рядом.
- `[P]` **Ориентация as-is = правильный выбор для реупаковки**: апскейл перевёрнутого
  оригинала + `tools/btf/pack_btf.py` (PNG→blob) дают корректный результат без правил переворота,
  round-trip побайтово lossless. Подтверждено глазами: портреты (`st_char_verition`,
  `ilya`, `cruan`) и сцены (`lp_solar`, `lp_necro`) в stored-порядке **вверх ногами**
  (движок делает вертикальный флип при blit), `qrs_battle` — зеркален по горизонтали,
  `mm_eye` симметричен (неинформативен). НЕ переворачивать вручную при подготовке апскейла.
- `[P]` Новые детали `data/wrender/figures/*.bmp` (144 файла): заголовок BITMAPINFOHEADER
  declares `biBitCount` = 128/32 — **обман**, реальный пиксельный блок по длине данных:
  borders = 24-бит 128x128 (49152+2 байта), textures = 24-бит 32x32 (3072+2). Порядок
  каналов **RGB** (не BGR): тайлы воды в RGB — teal, в BGR — оливковый. Строки bottom-up
  (стандартный BMP) → PNG записан top-down. 9 `.tga` рядом (`figures/textures/*.tga`) —
  заголовок с w=h=bpp=0 (размер берётся из `.fgr`), не конвертированы.
- `[P]` Не раскодируются текущим парсером 4 файла — индексированные глифы с внешней
  палитрой (blob < w*h, поле `'x'`), та же открытая задача, что и «7 мелких UI-глифов»:
  `config[game.root]/data/textures/ol_mc_black.btf` (файл установки), `config[game.root]/data/textures/ol_mc_rt.btf` (файл установки) (w=h=16, blob 217), `config[game.root]/data/textures/re_manadn.btf` (файл установки) (blob 9),
  `config[game.root]/data/textures/ui_scrollbar_hlcnt.btf` (файл установки) (blob 102).
- `[P]` Ограничение апскейла по размеру: поле лимита blob = **32 MiB**. 1024x1024 источников
  56 (все `qrs_*`, `re_bkg`, `sb_bkg`, `ter_fort01`); 2x влезает, 4x 24-бит (~50 МБ) — нет.
  Классы размеров: 256x256=391, 64x64=282, 32x32=232, 128x128=210, 1024x1024=56, 512x512=12
  (1205 из 1393 — квадратные).
Грабли сравнения/классификации — `docs/PITFALLS.md`.


## Lanczos-апскейл мелких текстур и перепись текстур игры (2026-11-15, вечер)
- `[U]` «некоторые мелкие текстуры не апскейлятся нормально, перенёс их в
  `scratch_dir('textures_src')/tmp`, апскейль их сам через Lanczos»;
  папку стенда `LethalDreams_textures_btf2x` пользователь очистил сам, ставить в игру —
  «обрабатывай папку `LethalDreams_textures_dst` плюс твои lanczos».
- `[P]` В `tmp` 17 микроскопических RGBA-текстур: 8x8 (`minimap_pt02`), 8x32
  (`water_rings01`), 16x32 (`stake01`), 16x16 (13). Upscayl сделал 12, 5 — нет.
  Сделал `gm convert <in.png> -filter Lanczos -resize 200% <out.png>`
  (GraphicsMagick 1.3.36-q8): `-clamp` этой версией **не поддерживается**
  («Unrecognized option», gm падает на всей команде); выходной путь подавать
  windows-форматом `D:/...`, cygwin-путь gm не открывает.
- `[P]` QC (`tools/archive/texture_qc/lanczos_qc.py`: mass/cov/speck по альфе + mean|dRGB| при box-даунскейле):
  mass/cov/dA идеальны у `minimap_pt02`, `water_rings01`, `white` (flat без ringing);
  ringing на изолированных точках — `minimap_pt03` (cov 1.048, полупрозрачных пикселей
  141 против 23) и `minimap_pt04` (cov 0.9655, 288 против 80). Билинейный
  `-filter Triangle` хуже: cov 1.13/1.00, speck 181/324 → оставлен Lanczos.
  Превью: `tools/archive/texture_qc/lc_preview.py` → `scratch_dir('ld')/lc_*.png` (снимок прогона)
 (orig слева, апскейл справа, nearest 8x).
- `[P]` **Перепись `data/textures` (`tools/archive/texture_qc/game_census.py`)** — состояние перед второй
  установкой: `installed-2x` (dims 2x) **604**, `original-size` **650** (это текущий набор
  `dst`), sidecar'ы 10, не парсятся 6. Проверено точечно: `lp_frame_r` = 2048x2048 (наш
    апскейл), `qrs_stats`/`st_char_verition`/`white`/`minimap_pt02` = родные размеры
    (mtime 2002–2003).
- `[P]` Установка второй партии (650 из `dst`, включая 5 Lanczos) — скрипт
  `tools/archive/texture_qc/replace_textures.py --names scratch_dir('ld')/pending.txt` (лог прогона)
 (pack + побайтовая
  проверка + запись в игру). Итог: pack+verified 637, записано 637, ложных пролов 13
  (`mm_tt_resume`, 7 `mm_ttr_*`, `msg_btm`, `msg_top`, `pc_death01`, `spellshield01`,
  `winding_tower01`) — все они та же квадратная амбигуность dims; точная проверка по
  офсетам заголовка (`tools/archive/texture_qc/verify_staged.py`, эталон размеров из манифеста) дала 13/13 OK.
  Дописаны стендовыми файлами режимом `--copy --names` → 650 записано, пролов 0.
  Ожидаемое состояние после установки: installed-2x 604+650 = **1254**, sidecar 10,
  не парсятся 6. Финальная проверка `tools/archive/texture_qc/final_check.py` (w,h по офсетам стенда,
  blob по байтам, md5 game==staged по всем 650) — `scratch_dir('ld')/final_check.txt` (лог прогона).
- `[U]` Pruning исходников для установленных 650 **не делать**: `LethalDreams_textures_src`
  остаётся полным эталоном оригиналов (пригодится для 4x/пересъёма апскейла),
  `scratch_dir('ld')/MANIFEST.txt` (лог прогона) не трогать.
- `[P]` Финальная проверка второй партии: **650/650 OK** (w,h по офсетам заголовка,
  blob побайтово == PNG, md5 game == staged) — `scratch_dir('ld')/final_check.txt` (лог прогона).
- `[P]` Перепись после второй установки: `installed-2x` 637 + «other» 13 (те же 13: парсер
  читает квадратную пару dims, blob = 262144/1048576/196608/6291456 = размеры апскейла,
  mtime 2026-11-15) = **1254 установленных** из 1270; `no-reference` 604 (первая партия,
  у неё эталонных строк в манифесте нет), sidecar'ы 10, не парсятся 6 индексированных
  глифов. Т.е. все раскодируемые текстуры игры сейчас в 2x.



## Lanczos-2x курсоров и иконок (`data/cursors`) (2026-11-15)
- `[U]` «Давай сделаем upscale всего, что находится в папке data\cursors в игре в 2 раза
  при помощи фильтра lanczos»; `.cur` не трогаем.
- `[P]` Содержимое: 10 курсорных `.btf` (32x32 RGBA, manifest `config[game.root]/data/common.tt` (файл установки)),
  64 иконки `icons/*.btf` (32x32 RGBA, `config[game.root]/data/menutextures.tt` (файл установки)), 5 `.cur`
  (Windows CUR, 32x32 16 цветов, по 2 иконки; в манифестах нет) — `.cur` не тронуты.
- `[P]` Пайплайн тот же, что для текстур: `unpack_btf --batch` →
  `gm convert -filter Lanczos -resize 200%` → `pack_btf` шаблоном оригинала.
  Стенд: `scratch_dir('ld')/cursors_stand/{src,out,src_icons,out_icons,btf,btf_backup}` (стенд прогона; пересоздаётся скриптами, в git не входит)
  (`btf/` — курсоры, `btf_backup/` — иконки). gm не умеет писать CUR
  («No encode delegate for CUR», format list: CUR r--) — для `.cur` нужен свой билдер.
- `[P]` Поле имени в TLV у курсоров = `cursor_*.tga` (не `.bmp`) — эталон проверки.
- `[P]` Верификация всех 74 packed: dims 64x64, blob_len=16384, побайтово
  blob==png→blob rebuild, имя целое — 74/74 OK.
- `[P]` QC (mass/cov/dRGB/dA как `tools/archive/texture_qc/lanczos_qc.py`): mass 0.99–1.01; у всех 64 иконок
  cov ровно 1.0099, dRGB 12–20, dA 2–3; у курсорных глифов cov 0.98–1.10 и
  dRGB до 45 (ringing на жёстких тонких глифах — ожидаемо, как в прошлой партии).
- `[P]` Установка: бэкап 74 оригиналов (md5 в `scratch_dir('ld')/cursors_stand/INSTALL_LOG.txt` (стенд прогона; пересоздаётся скриптами, в git не входит))
  в `paths.btf_backup_dir()/cursors_orig/{,icons/}`; записано 74 в игру, побайтовая
  проверка game==staged, финальная поверка из игры: 74/74 dims 64x64, blob=16384.
- `[P]` Превью: `scratch_dir('ld')/cursors_preview.png` (снимок прогона) (789x874: оригинал nearest2x | Lanczos).
- `[P]` Открытый вопрос закрыт: движок рисует 2x-текстуры корректно (курсоры и иконки
  не вылезают за ячейки), проверка в игре пользователем — «всё ок» (2026-11-15).
- `[U]` «Сделал ai-апскейлинг в папках out_upscale и out_icons_upscale. Давай их
  поставим в игру» — AI-набор 74 PNG (64x64 RGBA, имена совпадают с оригиналами).
- `[P]` AI-партия упакована (`pack_btf`, шаблон = родной 32x32 из `btf_backup/cursors_orig`),
  packed верификация 74/74 (dims 64x64, blob=16384, побайтово blob==png rebuild, имя целое);
  QC: mass 0.993–1.009, cov 0.998–1.1405 (худшие `cursor_create`, `cursor_no`),
  dRGB max 47.2 (`cursor_create`) — не хуже Lanczos.
- `[P]` Установка: 74 записано в игру поверх Lanczos-версий, game==staged побайтово,
  лог размеров+md5: `scratch_dir('ld')/cursors_stand/INSTALL_LOG_ai.txt` (стенд прогона; пересоздаётся скриптами, в git не входит). Стенды AI:
  `btf_ai/` (курсоры), `btf_ai_icons/` (иконки); Lanczos-пакеты остались в `btf/` и `btf_backup/`.


## Почвенные текстуры карт (`.tex` внутри `.map`) (2026-11-17)

Инструменты: `tools/tex/scan_tex.py` (экстрактор), `tools/tex/unpack_tex.py`
(раскод blob→PNG; `--tiles` — блоки 32x32), `tools/tex/tex_tiles.py` (нарезка/сборка
блоков), `tools/tex/qc_tex.py` (round-trip+статистика),
`tools/tex/test_pattern.py` (тестовый паттерн), `tools/tex/patch_map_pattern.py`
(подмена записей в scratch-дереве; `--tiles` — сборка атласа из блоков), `tools/tex/hook_ground.py` (+
`patches/tex_hook_opaque.py`/`patches/tex_hook_alpha.py` — хук-материалы).

- `[P]` Контейнер `.map` — тот же, что `.btf`: `LDPACKER` + u32(plen) + u32(clen) +
  zlib → payload. В payload'е почвенные текстуры записями
  `u32 name_len | ASCII <map>_<N>.tex | шапка 40 B | blob`. (Уточнение вечера
  17.11: `name_len` u32 лежит в gap'е перед именем, fields шапки = 26 байт по
  `hdr_off = name_off + name_len`, перед записью — внешний TLV-заголовок:
  type по `name_off-32`, size по `name_off-28`; см. раздел
  «Разбор чтения `.tex` движком и TLV-размеры записей».) Шапка (u32 LE):
  `[0]=w`, `[4]=h` (w==h), `[8]=alpha` (0/1), `[12]=[16]=2`, `[20]=[24]=[28]=16777216`,
  `[32]=119` (TLV-тип `'w'`), в `[8..31]` лимит движка 33554432 (32 MiB),
  `[36]=blob_len = w*h*(3+alpha)`. Хвост payload'а после последней записи —
  геометрия карты, к текстуре не относится. mip-цепочки, alpha-плоскости, RLE нет.
  Замеров: 30 карт, 44 записи, имена уникальны, blob'ы не перекрываются,
  сумма blob'ов 41732096 B; размеры S ∈ {32,64,128,256,512,1024}.
- `[P]` **Канон раскода — raw interleaved raster**: 24-бит RGB (bpp 3) при alpha=0,
  32-бит RGBA (bpp 4) при alpha=1, строки top-down — тот же канон, что `.btf`
  (`docs/OBSERVATIONS_TEXTURE.md`, «Канон раскода .btf»). Доказательство: round-trip
  blob → PNG → blob побитово совпал для **44/44** записей (`tools/tex/qc_tex.py`).
- `[P]` Движок читает `.tex` по имени записи из генерируемых материалов
  `config[game.root]/data/materials/<map>_opaque_N.pyc` и `<map>_alpha_N.pyc` (92 файла;
  dis`bald_hill_opaque_0.pyc`/`bald_hill_alpha_0.pyc`): модуль задаёт
  `Passability = globalscript.GROUND`, `AcceptCamera = 1`,
  `InitMaterial(Mtl)`: `p = Mtl.GetPass(0)`, `p.AddTexture('<map>_<N>.tex')`,
  у alpha-варианта дополнительно `p.SetBlendFunc(globalscript.BBlend_Blend)`.
  Материалов больше, чем текстур: часть карт имеет 2 записи, часть материалы
  переиспользуют одну.
- `[P]` Стартовая карта новой игры = **`portal_domain`** (при запуске грузятся
  `portal_domain_opaque_0/1`, `portal_domain_alpha_0/1` — имена видны из зондов
  хук-модулей в `config[game.root]/hdlog.txt`; строки `TEXHOOK … module <имя>`).
- `[P]` **Подмена `.btf`-ресурса непригодна**: игра при запуске пересоздаёт
  `mm_bkg01.btf` из источника (md5 дерева после прогона == md5 установки), и
  `p.AddTexture('mm_bkg01.bmp')` в хук-материале даёт Texture `2048x2048` — родной
  ресурс, паттерн на землю не попадает (три прогона: 1024-паттерн, 256-паттерн,
  сплошной белый — земля тёмная). Сам хук грузится и AddTexture работает
  (зонды), т.е. отказ по резолву имени ресурса, не по загрузке модуля.
- `[P]` **Known-answer по отображению**: замена blob'ов `.tex` тестовым паттерном в
  scratch-дереве (`tools/tex/patch_map_pattern.py --tree <дерево> --band …`) — земля
 отображается по тайлам: периодическая решётка тайлов, швы со сглаживанием (блендинг).
  Белый квадрат (левая верхняя четверть записи) даёт светлые области; яркая полоса
  у **верхнего** края записи ложится на **дальние/левые** рёбра тайлов, полоса у
  **нижнего** края — на ближние/правые (`scratch_dir('ld')/taxpat_overland.png`,
  `taxband_overland.png`, `taxbandb_overland.png` (снимки прогона съёмки; пересоздаются
  скриптами, в git не входят), кропы `band_cropA.png`/`bandb_cropA.png`). Т.е. ряд 0
  blob'а = дальняя сторона тайла: **top-down канон совпадает с тем, как движок
  кладёт землю** — PNG из `unpack_tex.py` ориентирован как мир (north up).
- `[P]` Демонстрация цвета и формы: запись = жёлтый фон + красный круг по центру
  (круг задан долей стороны: `--frac 0.5`; превью 64x64 с кругом диаметра 32 px —
  `scratch_dir('ld')/tex_64_yellow_red.png` (лог прогона; пересоздаётся скриптами,
  в git не входит)). На земле появляется решётка тайлов: круг виден красным
  перспективно-искажённым quad со сглаживанием на швах
  (`scratch_dir('ld')/taxcircle_overland.png` (снимок прогона съёмки; пересоздаётся
  скриптами, в git не входят), кропы `circle_center1.png`, `circle_center2.png`);
  жёлтого 4.57M px, красного 0.31M. Паттерн масштабиется под размер записи
  (запись 1024 даёт круг 512 px), т.е. движок кладёт всю запись на тайл и
  фильтрует при отображении.
- `[P]` Полный Lanczos-3 2x (`scratch_dir('ld')/tex_up2x`, 44 PNG) после фикса `fix_chain` грузится движком целиком и отображается на земле: сцена новой игры — бой, снимки `scratch_dir('ld')/ld/up2xsh_overland.png`, `up2xsh_postlesson.png`, `up2xsh_click1.png`, `up2xsh_click2.png` (снимки прогона; пересоздаются, в git не входят): травяные поля глаже родных (`ovland2_overland.png` — шум пикселей), тени деревьев без лесенок, игра жива (ExitCode-крашей нет).
- `[P]` Новая игра стартует в боевой сцене (`docs/KNOWLEDGE.md`, путь пользователя: `overland.png` прогона = бой). Уроки при старте вешают паузу «Игра в паузе», 6 Enter мало — диалог остаётся; клики по канвасу во время урока не двигают героя в бой.
- `[P]` Кодирование записей **единообразно**: поля шапки всех 44 записей дают ровно
  9 сигнатур, отличных только по `w,h,alpha,blob_len`
  (`512²α0`×15, `32²α1`×12, `1024²α0`×9, `256²α0`×2, `32²α0`×2, `128²α1`, `64²α0`,
  `512²α1`, `128²α0`); других классов кодировки нет. Различие — в **семантике альфа-плоскости**
  12 bpp4-записей: `disputable_domain_1`, `dying_domain_1`, `etheral_stronghold_1`,
  `flying_dutchman_0`, `incarn_domain_2` — альфа **везде 0** (пасс полностью прозрачен,
  запись = заглушка); `crossroads_1`, `enchant_domain_1`, `lake_of_tears_1`,
  `pan_grove_1`, `portal_domain_1` — одинаковый набор из 63 альфа-значений
  (137..216) = полупрозрачный overlay; `evoker_domain_1` 0..243, `necro_domain_1` 184..221,
  `shiny_stronghold_1` 0..255 (959 пикселей 255), `volcanic_throne_1` (512²) все 256 значений.
  Ни одна alpha-запись не имеет альфы 255 повсеместно (кроме части `shiny_stronghold_1`).
- `[P]` Контрольный прогон отделил источники артефактов: `--alpha zero` (альфа-пасс
  полностью прозрачен) — красные четырёхугольники целых тайлов в ближней зоне ОСТАЮТСЯ
  (`scratch_dir('ld')/taxcircle3_overland.png`, кроп `circle_near3.png`), т.е. они идут
  от opaque-прохода: крупный тайл рисуется плоским цветом = выборка текстуры в точке
  (центр записи = красный круг), ~25% тайлов красные (площадь круга = четверть записи).
  Дальняя зона при `--alpha zero` даёт ступенчатые красные фигуры (nearest-neighbor
  раскода мелкого тайла), без гладких эллипсов — гладкая решётка эллипсов в
  `circle_far.png` (`taxcircle2_overland.png`) создаётся альфа-проходом с родной
  альфа-плоскостью (blend). Вывод: кодирование записей единообразно, «квадраты»
  вблизи = выборка движка по тайлам, геометрия паттерна видна только в дальней зоне.
- `[P]` Отображение почвы зависит от дистанции тайла, а не от кодирования записи:
  в дальней области тайлы мелкие, каждый тайл = вся запись → решётка эллипсов
  (`scratch_dir('ld')/circle_far.png` (кроп снимка прогона; пересоздаётся скриптами,
  в git не входит)); в ближней области тайлы крупные, видны красные четырёхугольники
  целых тайлов (`circle_near.png`) — движок берёт малое пятно выборки/альфа-пасс,
  геометрия круга на крупных тайлах не воспроизводится. Паттерн в данных лежит
  побитово (round-trip), «квадраты» — свойство выборки движка, а не разные кодировки.
  Режим альфа-плоскости патчера: `--alpha keep` (родная альфа записи) /
  `opaque` (255 из паттерна) / `zero` (0).
- `[P]` Статистика по 44 записям (`tools/tex/qc_tex.py`): uniq 2..62313, gray% 0..100.
  Записи 32x32 alpha=1 с uniq==2 (черный+белый) — заглушки; записи 100% gray
  (`atlantis_0`, `dry_lands_0`, `evoker_domain_0`, `shiny_stronghold_0`,
  `solar_colector_0`, `volcanic_throne_0`, `world_heart_0`, `distorted_mirrors_0`,
  `observatory_0`) — однотонная почва; самые насыщенные по цвету
  `portal_domain_0` (62313) и `crossroads_0` (59632).
- `[P]` Визуальный раскод родных карт (`scratch_dir('ld')/tex_src/<name>.png`
  (лог прогона; пересоздаётся скриптами, в git не входят)) похож на terrain: мозаика
  блоков 32/64/128 px, швы масштаба t=32/64/128 (ratio 3.2–3.6).
- `[P]` Фазу B (тайлы `data/wrender/figures/textures/*.bmp` × цвета `GROUPS`) закрытой
  не считаем: сетка 32 даёт средний остаток 15.0, доминируют чёрные блоки;
  сверка палитры генератора (`qc_tex.py --tiles`) не дала покрытия — путь
  генерации почвы из тайлов не подтверждён.
- `[P]` (18.11) **Сетка 32x32 подтверждена по геометрии карт**: UV мешей (TLV
  `type=132` payload'а) приходятся на центры текселей блока (`k*32+0.5` / `k*32+31.5`);
  множества «блок с ненулевыми пикселями» и «блок, на который ссылается меш»
  совпадают точно: world_heart 6/6, atlantis 19/19, bald_hill 116/116,
  portal_domain 632/632; ссылок в пустые блоки 0, неиспользуемых непустых 0.
  Т.е. мозаика — это упакованные патчи земли 32x32 (`BTexturer.dll`, `TexturizeIt`:
  грунт + маски `figures/borders/*`), а пустые области — незанятые слоты атласа;
  перестановка блоков не нужна, раскладку задают UV, а не картинка.
  Побочный счёт: 44 записи = 13490 блоков 32x32, из них 7438 пустых (6052 непустых;
  у `incarn_domain_1` пустых 976/1024).
- `[P]` (18.11) **Потайловая нарезка/сборка** (`tools/tex/tex_tiles.py` + режимы
  `unpack_tex.py --tiles`, `patch_map_pattern.py --tiles`): блок = PNG 32x32
  (`<stem>__tx<col>_ty<row>.png`), атлас собирается обратно по имени. Гейты: f=1
  сборка всех 44 записей побитово равна `unpack_tex.py` (с `--skip-empty` тоже —
  пустые слоты восстанавливаются как чёрный/альфа 255 у bpp 3 и нули у bpp 4);
  запись в scratch-дерево при f=1 даёт payload, идентичный установочному
  (9239857 B world_heart), при f=2 (блок 64) — 2048²-запись и цепочка TLV до EOF.
  Лимит 32 MiB прежний: 1024² при 2x — 12.6 MB, при 4x не влезает.
- `[P]` (18.11) **Сторона 3072 грузится и рисуется**: 3x-блоки UpScayl
  (`tex_tiles_ne/upscayl_png_remacri-4x_3x`, блок 96x96) собраны во все 30 карт
  (44 записи: 1024→3072, 512→1536, 256→768, 128→384, 64→192, 32→96; max blob
  28 311 552 B = 27.0 MB при лимите 32.0 MB, запас 5 MB). Гейт: scratch-дерево,
  `probe_exitcode.ps1 3840 2160 1 tex3x -Mode new` — игра стартовала, новая игра
  загрузилась, оверленд виден (terrain, трава/песок/камни без чёрных провалов),
  Application-лог пуст, процесс погашен сразу после съёмки
  (`scratch_dir('ld')/ld/tex3x_overland.png`). Значит ограничение движка — поле
  blob ≤ 32 MiB, а не степень двойки/NPOT: подтверждённый максимум стороны 2048 → 3072.
  Контроль данных: dims 3x родных у 44/44, blob_len = w*h*bpp, цепочки TLV всех
  30 карт до EOF, `world_heart_0` == сборка из блоков побитово, у `volcanic_throne_1`
  (bpp4) RGB == сборка, альфа == родная nearest.
- `[P]` (18.11) **Итог: в HD-пак вошёл вариант high-fidelity 3x.** Собраны три
  комплекта из одного набора блоков (`upscayl_png_{high-fidelity,upscayl-standard,remacri}-4x_3x`,
  блок 96x96); выбран `high-fidelity`, его 30 `.map` (44 записи ×3) добавлены в
  `config[hd_pack]` (`hd_pack.7z`: 1379 записей, из них 30 `data/maps/*.map`) и
  установлены в игру. `check_state --full` rc 0 (`hd_pack: 1379/1379` побитово),
  `--untouched` rc 0. Родная почва восстанавливается из `original.7z` (pristine),
  собственные правки — `patch_map_pattern.py --tiles … --install`.

## Разбор чтения `.tex` движком и TLV-размеры записей (2026-11-17, вечер)

Разбор REA (статика) + Frida (динамика) на scratch-дереве; полный протокол прогона
и снимки — `docs/ISSUE_tex_upscale.md` («РЕШЕНИЕ»).

- `[P]` **TLV-цепочка payload'а ходит `FUN_0049bf70` (ADV)**: заголовок = `(type u32,
  size u32)` по pos, содержимое `pos+8..pos+8+size`, следующий заголовок — `pos+8+size`.
  ADV в ветке потока-в-памяти: ищет `tell = size + 8`, читает пару `(pos, size)`,
  сверяет `ftell == pos+8`; при рассинхроне гасит байт-флаг потока (`+0x28`).
- `[P]` Цепочка `portal_domain.map` (clean payload len 10639424): @8 `type=142 size=24`,
  @40 `type=135 size=2986612`, @2986660 `type=130 size=3150006` — секция почвенных
  текстур, внутри внешние заголовки записей: rec0 @2986692 `size=3145811`,
  rec1 @6132487 `type=116 size=4179`; после секции @6136674 `type=131 size=265`,
  @6136947 `type=139 size=36`, @6136991 `type=132 size=4470866`,
  @10607865 `type=137 size=31551`.
- `[P]` Внешний заголовок записи: type u32 по `name_off-32`, **size u32 по `name_off-28`**,
  и size = длина содержимого блока; `name_len` u32 записи лежит в gap'е перед именем
  (`scan_tex`: `name_off` = строка имени, `hdr_off = name_off + name_len`). Запись =
  имя + 26 байт полей + blob.
- `[P]` Порядок чтения записи (`FUN_00495300`): 4 байта, ADV, строка имени,
  w(4) h(4) alpha(4) f58(4) f54(4), **2-байтный short (младший байт обязан быть 0)**,
  byte 0x51, u32 0x60, u32 0x5c, byte 0x50, ADV, blob `w*bpp*h`, GL-upload
  `FUN_004772e0`. Если short читает мусор (младший байт ≠ 0), разбор возвращает 0.
- `[P]` `FACT` = `FUN_00495720`: возвращает NULL, если флаг потока `+0x28` == 0 или
  разбор вернул 0. Диспетчер `FUN_00467eb0` снимает указатель без проверки на
  `0x467f54` (`MOV EDX,[ESI]`) → `0xC0000005`; WER сообщает fault offset `0x67f54`.
  При рассинхроне ADV (устаревший TLV-size) краш переезжал на `0x467566`
  (`FUN_00467540` — обход тайлов почвы, NULL по `obj+0x37c`).
- `[P]` Диспетчер `FUN_0046b100`: switch по типу блока `0x82..0x8e`; `0x8b` = чтение
  36-байтного почвенного тайла.
- `[P]` **Изменение размера blob'а требует правки ВСЕХ накрывающих TLV-размеров**:
  внешнего заголовка записи и заголовков цепочки (секция @2986660 и выше). Патчер
  `tools/tex/patch_map_pattern.py` (`chain_headers`, `fix_chain`) добавляет дельту
  blob'а (+/−) каждому накрывающему заголовку с пересчётом позиций после вставок.
  Записи пересобираются БЕЗ лишнего `name_len`-префикса.
- `[P]` Гейты на дереве (`tools/probes/probe_exitcode.ps1`, `probe_clicks.ps1`) — все
  грузятся, `ALIVE after enter2+15s`, `ExitCode=00000000`: `--rescale self --num 2 --den 1
  --record portal_domain_1` (32²→64²), полный `--up2x` (rec0 1024²→2048² + rec1 64²,
  plen 20088896), даунскейл `--num 1 --den 2` (16²), не-POT `--num 3 --den 4` (24²).
  Оверленд снимается (`scratch_dir('ld')/ld/up2x_gate2_overland.png`, кроп 4:3
  `up2x_gate2_overland_43.png`): terrain, учебный диалог, геометрия 4:3.
- `[P]` Зонд-хук `tools/tex/hook_ground.py --native --only portal_domain`
  (`patches/tex_hook_native_opaque.py`/`..._alpha.py`) грузит родную `.tex` по имени
  модуля и пишет `TEXHOOK PRE/POST` w/h в `hdlog.txt`. На последних прогонах хуки
  не срабатывали и давали диалог «Texture not found 'portal_domain_opaque_0'» —
  хуки надо восстанавливать (`--restore`) перед съёмкой оверленда.
- `[P]` Frida: attach по pid после shell-запуска (`launch_game.ps1` → `game_pid.txt`),
  spawn-режим не управляется кликами; клики `frida_click3.ps1`, лог через `send()`
  (File/NativeFile в этом рантайме недоступны). Стрим-параметр FACT/ADV — u32 по
  `sp+4` (return address по `sp+0`).

