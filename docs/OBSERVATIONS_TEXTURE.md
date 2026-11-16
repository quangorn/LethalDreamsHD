# OBSERVATIONS_TEXTURE.md — наблюдения: раскод/формат `.btf`, packer, курсоры, иконки, апскейлы

Канон раскодирования blob'ов, формат манифестов `.tt`, замеры апскейлов и переписей.
Пайплайн unpack/pack — `docs/PIPELINE.md`; инструменты — `docs/TOOLS.md`; порядок чтения — `docs/INDEX.md`.


## Ресурсы текстур (`[P]` расшифковано офлайн)
- Архивы `.tt` (plain text, CRLF): `config[game.root]/data/menutextures.tt` (файл установленной игры, в git не входит) = список
  `имя.bmp | textures\имя.btf`; движок грузит `data/textures/<name>.btf`.
- `.btf` = `LDPACKER\` + `<u32 заголовка>` + zlib(zlib, magic 78 DA) поток;
  распакованный payload = `mdf ` (u32), блоки TLV: `(type=1, count=116)`,
  `(type=117, total)`, `(1,bpp=4)`, type 118: fields w,h (u32, при 1024x768: 1024,1024 и h=768)
  — пиксельный blob BGRA 1024x768 = 3145728 байт лежит в конце payload
  (`payload[-W*H*4:]`), header перед ним.
- Фон меню = `config[game.root]/data/textures/mm_bkg01.btf` (движок грузит texture по имени .bmp; бинар .btf лежит в установке, в git не входит) (mainmenu.py (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git):190, `BActionElement([0,0,1024,1024,...,'mm_bkg01.bmp', BBlend_Replace])`)
  = `config[game.root]/data/textures/mm_bkg01.btf` (файл установленной игры, в git не входит); раскодирован и положен на рабочий стол
  (`scratch_dir('desktop')/mm_bkg01.png`, 1024x768 RGBA).
- Отдельный 16:9-фон (решение пользователя): файл делает внешний агент;
  запакуем обратно в `LDPACKER+zlib+mdf` и подменим в архиве.
- `[U]` «это не та текстура, там серый прямоугольник»; «положи мне png на рабочий стол»;
  «16:9 сам не делай, я попрошу другого агента».

## Подтверждение раскода `.btf` (`[P]`)
- Раскод `config[game.root]/data/textures/mm_bkg01.btf` (файл установленной игры, в git не входит) (row-major, stride W*4, BGRA, blob в хвосте payload) даёт
  канвас с глифами-глазами; эти же глифы = `mm_eye.tga` (утрачен: покрытия нет ни в git, ни в приватных архивах сессий Project/1-3.7z, 5.7z, 6.7z, 7.7z, original.7z, hd_pack.7z, python_libs.7z)/`mm_r_*` (кнопки меню из
  `config[game.root]/data/menutextures.tt` (файл установленной игры, в git не входит)) -> layout раскода согласуется с UI-текстурами, раскод корректен.
- Фон меню при отрисовке = `config[game.root]/data/textures/mm_bkg01.btf` (движок грузит texture по имени .bmp; бинар .btf лежит в установке, в git не входит) (mainmenu.py (историческая ссылка; игровой модуль, база декомпилята — локальная, не в git):190, BBlend_Replace, [0,0,1024,1024]);
  движок берёт `config[game.root]/data/textures/mm_bkg01.btf` (файл установленной игры, в git не входит) (путь из `.tt`-манифеста).



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
- `config[game.root]/data/textures/qrs_battle.btf` (файл установленной игры, в git не входит) лежит зеркально ([P]: текст читается только при `--flip`); движок
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
  ресурс `config[game.root]/data/textures/mm_bkg01_169.btf` (файл установленной игры, в git не входит) лежит как отдельный файл `config[game.root]/data/textures/mm_bkg01_169.btf` (файл установленной игры, в git не входит)
  (оригинал `config[game.root]/data/textures/mm_bkg01.btf` (файл установленной игры, в git не входит) восстановлен), копия из архива
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
 (раскодер `tools/btf/unpack_btf.py`, canon: top-down, RGB/RGBA). В папке `scratch_dir('ld')/MANIFEST.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит)
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
  `config[game.root]/data/textures/ol_mc_black.btf` (файл установленной игры, в git не входит), `config[game.root]/data/textures/ol_mc_rt.btf` (файл установленной игры, в git не входит) (w=h=16, blob 217), `config[game.root]/data/textures/re_manadn.btf` (файл установленной игры, в git не входит) (blob 9),
  `config[game.root]/data/textures/ui_scrollbar_hlcnt.btf` (файл установленной игры, в git не входит) (blob 102).
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
  Превью: `tools/archive/texture_qc/lc_preview.py` → `scratch_dir('ld')/lc_*.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит)
 (orig слева, апскейл справа, nearest 8x).
- `[P]` **Перепись `data/textures` (`tools/archive/texture_qc/game_census.py`)** — состояние перед второй
  установкой: `installed-2x` (dims 2x) **604**, `original-size` **650** (это текущий набор
  `dst`), sidecar'ы 10, не парсятся 6. Проверено точечно: `lp_frame_r` = 2048x2048 (наш
    апскейл), `qrs_stats`/`st_char_verition`/`white`/`minimap_pt02` = родные размеры
    (mtime 2002–2003).
- `[P]` Установка второй партии (650 из `dst`, включая 5 Lanczos) — скрипт
  `tools/archive/texture_qc/replace_textures.py --names scratch_dir('ld')/pending.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит)
 (pack + побайтовая
  проверка + запись в игру). Итог: pack+verified 637, записано 637, ложных пролов 13
  (`mm_tt_resume`, 7 `mm_ttr_*`, `msg_btm`, `msg_top`, `pc_death01`, `spellshield01`,
  `winding_tower01`) — все они та же квадратная амбигуность dims; точная проверка по
  офсетам заголовка (`tools/archive/texture_qc/verify_staged.py`, эталон размеров из манифеста) дала 13/13 OK.
  Дописаны стендовыми файлами режимом `--copy --names` → 650 записано, пролов 0.
  Ожидаемое состояние после установки: installed-2x 604+650 = **1254**, sidecar 10,
  не парсятся 6. Финальная проверка `tools/archive/texture_qc/final_check.py` (w,h по офсетам стенда,
  blob по байтам, md5 game==staged по всем 650) — `scratch_dir('ld')/final_check.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит).
- `[U]` Pruning исходников для установленных 650 **не делать**: `LethalDreams_textures_src`
  остаётся полным эталоном оригиналов (пригодится для 4x/пересъёма апскейла),
  `scratch_dir('ld')/MANIFEST.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит) не трогать.
- `[P]` Финальная проверка второй партии: **650/650 OK** (w,h по офсетам заголовка,
  blob побайтово == PNG, md5 game == staged) — `scratch_dir('ld')/final_check.txt` (лог прогона; создаётся скриптами tools/archive/texture_qc/, в git не входит).
- `[P]` Перепись после второй установки: `installed-2x` 637 + «other» 13 (те же 13: парсер
  читает квадратную пару dims, blob = 262144/1048576/196608/6291456 = размеры апскейла,
  mtime 2026-11-15) = **1254 установленных** из 1270; `no-reference` 604 (первая партия,
  у неё эталонных строк в манифесте нет), sidecar'ы 10, не парсятся 6 индексированных
  глифов. Т.е. все раскодируемые текстуры игры сейчас в 2x.



## Lanczos-2x курсоров и иконок (`data/cursors`) (2026-11-15)
- `[U]` «Давай сделаем upscale всего, что находится в папке data\cursors в игре в 2 раза
  при помощи фильтра lanczos»; `.cur` не трогаем.
- `[P]` Содержимое: 10 курсорных `.btf` (32x32 RGBA, manifest `config[game.root]/data/common.tt` (файл установленной игры, в git не входит)),
  64 иконки `icons/*.btf` (32x32 RGBA, `config[game.root]/data/menutextures.tt` (файл установленной игры, в git не входит)), 5 `.cur`
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
- `[P]` Превью: `scratch_dir('ld')/cursors_preview.png` (снимок прогона съёмки; пересоздаётся скриптами, в git не входит) (789x874: оригинал nearest2x | Lanczos).
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
