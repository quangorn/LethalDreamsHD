# HANDOFF_ldhook.md — передача: `.pyd`-хуки в памяти (Фазы 0-бис–4)

Документ для следующего агента: что уже сделано и проверено, что **не** сделано,
какие есть варианты дальнейших действий и как их принимать. Канонический план —
`docs/PLAN_proxy_dll.md`; инструмент — `tools/ldhook/`; правила — `AGENTS.md`.

Состояние на 18.11.2026, HEAD `2fb0b39` (Фазы 0-бис–4 закоммичены).

## 1. Главное, что нужно понять сразу

**Переход установки на `.pyd` НЕ выполнялся.** Фаза 4 — это *решение*, а не
переход: установка осталась на cave-EXE `exepatch12`, а `ldhook.pyd` — dev-путь.

| что | механизм | где живёт |
|---|---|---|
| установка `D:/Games/LethalDreams` | cave `exepatch12` (`fixall.bin`, 236 Б в `0x4ebf12`, три `E9`) | EXE `171319bb…` |
| dev-прогоны | три хука **в памяти**, код в `.pyd` | baseline EXE `45e93d46…` + `ldhook.pyd` |

Отсюда два следствия, на которых легко ошибиться:

1. `fixall.bin` **нужен** — это и есть рабочий механизм установленной игры.
   Удалять/откатывать его нельзя без поставки `.pyd` в установку (иначе игра
   теряет шрифты 16:9 и x-коэффициент проекции).
2. `ldhook.pyd` в cave-установке **молча ничего не делает**: сигнатур сайтов там
   нет (`signature NOT found`), игра работает на cave. Это не поломка.

## 2. Факты и числа (`[P]`)

- Сайты: site1 `0x41ce31` (`c641580159`, 5 Б), site2 `0x40b494`
  (`898194020000`, 6 Б, `E9`+`nop`), site3 `0x47dd80` (`e8eb1bffff`, 5 Б).
- site4 (cap боя, 19.11.2026): `0x45e0a0` (`8b44241083ec34`, 7 Б, пролог) +
  `0x45e163` (`8bf03bf575ae`, 6 Б, хвост цикла поіска целі). Сайт цел і в cave-установке
  (участок не трогается cave-патчем). Живой прогон: 31% вызовов упираются в cap,
  лаги уходьят, **агро ломается** → в паставку не идёт (`docs/PERF_BATTLE.md`).
- Свободно в `.text` zero-run `0xebf12..0xec000` (=238 Б): **baseline 238**,
  **установка 2** (236 заняты `fixall.bin`). Фазы 1–4 в `.text` на диске не
  писали ничего — md5 обеих версий не изменились.
- Код хуков: `ldhook.pyd` 122 880 Б, его `.text` `0x1460e` = 83 470 Б.
- Эталонные результаты: site3 `matrix[0] 0x3FA646F1 -> 0x3F7987FA`
  (= `exepatch12`, `m5=0x3FDDB3ED`); 4:3 — identity-guard'ы, записей нет;
  кольцо домена A/B: hook 486x451 vs cave 485x453, центр совпадает.
- Живая матрица: 1920x1440, 1440x1080, 2880x2160 (identity) + 3840x2160
  (scale) — одни и те же VA и биты, крашей нет.
- Установка: 5095 файлов; `check_state --quick` rc 1 = 2 известных дрейфа
  (`hd.cfg` с пользовательской строкой-комментарием, `cursor.pyc`
  DRIFT_RELEASE_AHEAD); `--untouched` rc 0.
- Логгер: `hdlog.txt` в каталоге EXE (общий с Python-патчами), уровни
  `LDHOOK_LOG=0|1|2`, ротация `hdlog.txt.1` после 8 MiB, `ldhook.set_level/log/stats`.
- Аудит сайтов: `tools/ldhook/check_sites.py` — `installable` (baseline),
  `patched` (cave-установка), `UNEXPECTED` (rc 1).

## 3. Быстрый вход (команды)

```
# 1. сборка (нужны vendor/py223/{include,DLLs}, MSVC через vswhere)
PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/build.py

# 2. API-смоук без игры
PYTHONDONTWRITEBYTECODE=1 LDHOOK_LOG=2 vendor/py223/python.exe \
    tools/ldhook/smoke_api.py build/ldhook/ldhook.pyd

# 3. аудит сайтов (после любой правки EXE — обязательно)
PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/check_sites.py

# 4. dev-дерево: baseline EXE + ldhook.pyd + ui.pyc с загрузчиком
PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/spike.py --baseline
#    -> <scratch>/ldhook/tree, <scratch>/ldhook/config.toml

# 5. живой прогон (новая игра -> оверленд), лог в <Tag>_hdlog.txt
export LD_GAME="<scratch>/ldhook/tree" LD_SCRATCH="<scratch>" LD_REPO="<scratch>/repo"
LDHOOK_LOG=1 powershell -NoProfile -ExecutionPolicy Bypass \
    -File tools/probes/probe_exitcode.ps1 3840 2160 1 <tag> -Sub <sub>
#   4:3: заменить на "1440 1080 1"; после — вернуть hd.cfg = "3840 2160 32 1"

# 6. состояние
PYTHONDONTWRITEBYTECODE=1 python3 tools/check_state.py --quick      # ожидается rc 1 (2 дрейфа)
PYTHONDONTWRITEBYTECODE=1 python3 tools/check_state.py --untouched  # ожидается rc 0
```

`probe_exitcode.ps1` сам гасит игру после снимка. Если запускал вручную —
`taskkill /F /IM LETHALDREAMS.EXE` (правило пользователя: окно 4K перекрывает экран).

## 4. Варианты следующих действий

Выбор зависит от цели; по умолчанию — **A**.

### A. Не трогать (статус-кво) — если нет новой правки движка
Ничего не делать: установка стабильна, `.pyd` проверен. Это и есть решение
Фазы 4; критерии перехода — `docs/PLAN_proxy_dll.md` §5.

### B. Перевести установку на `.pyd` (только по критериям §5, целиком)
Чек-лист из `docs/PLAN_proxy_dll.md` §4 Фаза 4, в терминах файлов:

1. EXE установки → baseline: `python3 tools/exe/apply_baseline.py --install`
   (cave-патч больше не ставить; `exepatch12.py` остаётся откатом).
2. Вынести загрузчик `.pyd` из `spike.py` в канонический скрипт (например
   `tools/ldhook/patch_ui.py`): собрать `ui/ui.pyc` = `patches/ui/ui.py` + блок
   `imp.load_dynamic` воркером 2.2.3, поставить `--install`.
3. Новые файлы в установке: `ldhook.pyd` (артефакт, в git не идёт — `*.pyd`
   добавлен в `.gitignore`), изменённый `data/scripts/ui/ui.pyc`.
4. Обновить эталоны: `release/manifest.json` — `code_tier` запись exe
   (md5/size/crc32/`fix_command`/`chain` → baseline + `.pyd`), добавить запись
   `ldhook.pyd` (как `GENERATED`, как `hd_res.pyc`), `release/allowlist.json` —
   легализовать новые файлы; `check.exclude` (в `config.toml` **и**
   `config.example.toml`) — `hdlog.txt`, `hdlog.txt.1`, `ldhook_marker.txt`.
5. Гейт перехода: `check_sites.py` rc 0 (`installable`); 16:9 site3
   `0x3F7987FA`; 4:3 identity; `measure_disc` 1–2 px от cave-эталона;
   `--quick`/`--untouched` по новой базе.
6. Откат: baseline EXE + `python3 tools/exe/exepatch12.py --install`, вернуть
   `ui/ui.pyc` из манифеста, удалить `ldhook.pyd`.

Риск: `check_state` начнёт считать «эталоном» baseline — все старые снимки/замеры
cave остаются валидными как эталон A/B только по файлам, не по md5 установки.

### C. Четвёртый хук в `.pyd` (главный сценарий, ради которого строили)
Байтов `.text` на диске не тратится: `E9` пишется в память поверх инструкции
сайта, код — в `.pyd`.

1. Найти сайт: инструкция/`call` с известной семантикой, 5–6 байт под `E9`,
   граница инструкций, уникальная байтовая сигнатура (проверить
   `check_sites.py`-подобным сканом).
2. Трамплин `__declspec(naked)`: сохранить нужные регистры/флаги, вызвать
   `*_impl` (`__cdecl`), восстановить, исполнить сохранённые оригинальные байты
   или продолжить с `site+N` (как site1/site3). Абсолютные адреса EXE не
   хардкодить — брать из самого сайта (как `call rel32` в site3).
3. Guard 4:3 (`k==1` / `4h==3w`) — обязательный: 4:3 не должен получить ни
   одной записи (регрессионный критерий пользователя).
4. Лог: `ld_info` первый вызов, `ld_debug` — троттлинг.
5. Гейты: сборка; `check_sites.py`; прогон 16:9 + один 4:3; при правке,
   меняющей картинку, — A/B с cave/эталоном; `check_state` (установка не тронута).
6. Документация: `docs/PLAN_proxy_dll.md` §4, `tools/ldhook/README.md`,
   `docs/PATCHES.md` (in-memory), `docs/STATE.md` + `docs/archive/SESSION_LOG.md`.

### D. Остаточные (некритичные) пункты
- Полный пиксельный диф 4K hook-сборки против `exepatch12`-эталона
  (`tools/measure/pdiff.py`, `pngdiff.py`, `region_diff.py`) — сейчас замена:
  биты site3 + кольцо 1–2 px.
- Повторить «меню-спайк»-скриншот при активном дисплее (0-бис) — исторический
  хвост; живые снимки меню уже есть в Фазах 2–4.
- Проверить логгер под конкурентной записью Python (`hdlog.txt` открыт и у
  Python-патчей, и у `.pyd`): прогон с включёнными `tex_hook_*`/`hd_scale` +
  `LDHOOK_LOG=2`, файл не должен рваться/терять строки.

### F. Структурный фикс поиска цели (следующий шаг, 19.11.2026)
Проблема не в «одном долгом вызове»: `FUN_0045ce80` — это полный проход списка, а
`FUN_0045e0a0` зовёт его до ~4400 раз (O(итерации × n)). Cap 32 это подтвердил (лаги
уходят, агро ломается). Направление: (1) кеш ранжирования кандидатов за один проход —
поведение идентично, O(n²)→O(n); (2) по решению пользователя — удержание цели N кадров.
Решает зонд `tools/perf/perf_frida10.js` (повторы кандидатов, смысл `[obj+0x24]`),
разбор — `docs/PERF_BATTLE.md`, «Механика поиска цели».

### E. Резервный трек прокси `python22.dll` — **заморожен**
Только если `.pyd`-путь не хватит по таймингу/ограничениям движка. Фазы Пр-1…Пр-5
в `docs/PLAN_proxy_dll.md`; 53 символа (45 функций + 8 data), sub-подмена
`python22.dll` → `python22_real.dll`.

## 5. Критерии приёмки любой работы

- Установка не изменилась, если явно не требуется: `--quick` rc 1 = 2 известных
  дрейфа, `--untouched` rc 0.
- 16:9: site3 `0x3F7987FA`, три хука FIRED; 4:3: identity-guard'ы, записей нет.
- Игра погашена после прогона; снимки — только окно, DPI-aware.
- `PYTHONDONTWRITEBYTECODE=1`; канонические команды из корня репозитория.
- Никаких бинарников игры в git; `build/`, `vendor/`, `scratch/` — локальные.

## 6. Грабли, которые уже стоили времени (не повторять)

- `Py_InitModule(name, NULL)` в 2.2.3 → `0xC0000005`: передавать пустую таблицу.
- Скан `.text` по константам опасен вне игры; брать из PE-заголовков
  (`VirtualSize`), пещера лежит за `VirtualSize` — иначе `site2`-сигнатура
  «находится» в блобе (`0x4ebf91`) и даёт ложный `UNEXPECTED`.
- Логгер в per-frame хуке: не открывать файл на строку; троттлить трассировку.
- AppCompat `~ HIGHDPIAWARE` привязан к полному пути EXE: у установки флаг
  должен быть, для scratch его ставит `spike.py`.
- GDI-захват при заблокированной сессии отдаёт устаревший кадр; `PrintWindow`
  для GL-окна — чёрный.
- Скрипты, которые исполняет 2.2.3, не должны использовать синтаксис 2.3+
  (кодинг-декларации, тернарные выражения) — только 2.2.
- PowerShell: отрицательные числа в позиционных аргументах — `([int]-40)`.

Подробнее: `docs/PITFALLS.md` (разделы «Спайк `.pyd`», «Логгер …», «Скан `.text` …»).

## 7. Куда писать результат

`docs/STATE.md` (срез состояния + сессия), `docs/archive/SESSION_LOG.md`
(хроника), `docs/PLAN_proxy_dll.md` (статус фазы/решение), `tools/ldhook/README.md`
(команды/гейт), `docs/PATCHES.md` (если меняется модель патча); коммит — по
стилю `git log` (проза, что/зачем/чем проверено).
