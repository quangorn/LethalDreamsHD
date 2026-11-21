# tools/ldhook — .pyd-хуки в памяти (PLAN_proxy_dll.md, §2A, Фазы 0-бис–5)

Механизм доставки кода без правки EXE: расширение Python 2.2.3 (`ldhook.pyd`)
грузится из патчимого игрового скрипта явным `imp.load_dynamic()`; `initldhook`
ставит три хука **в памяти** — скан сигнатур в `.text`, `VirtualProtect`, `E9 rel32`.
EXE на диске не меняется (md5-цепочка цела), правило «игра только для чтения»
соблюдено.

| сайт | VA (baseline) | логика | источник |
|---|---|---|---|
| site1 | `0x41ce31` | масштаб листов/advance 4x-шрифтов; guard `k==1` → identity | cave1 |
| site2 | `0x40b494` | нормировка шрифта сразу при SetFont (`FUN_0041cc90`) | cave2 |
| site3 | `0x47dd80` | `matrix[0] = matrix[5]*h/w` перед сеттером `FUN_0046f970`; guard `4*h==3*w` → identity | cave3 |
| site5 | `0x45e0a0` + `0x45e163` | диагностика поиска цели: логирование кандидатов, метрик, порогов (пролог + хвост цикла + эпилоги `ret 10h`) | `docs/PERF_BATTLE.md` |
| site6 | `0x45e0a0` | полный detour: faithful-цикл `FUN_0045e0a0` на C++ (`LDHOOK_SITE6=1`) | `docs/PLAN_BATTLE_AI_CPP.md` |

Guard'ы перенесены 1:1: 4:3 остаётся побитово прежним (никаких записей).
site3 считает FP-последовательность `cave3` (x87: `fild`/`fild`/`fdivp`/
`fmul m32`/`fstp m32`) в отдельном naked-хелпере `site3_fp` — результат
побитово совпадает с `exepatch12` (`matrix[0] = 0x3F7987FA`).

`.text` для скана берётся из PE-заголовков (VA + VirtualSize), не из констант:
под чужим EXE (`vendor/py223/PYTHON.EXE`) модуль грузится без риска выйти за
секцию, а сдвиг `.text` в игре не ломает установку хуков.

Канва: `docs/PLAN_proxy_dll.md` (§2A, §4 Фазы 1–3). Порт боевой логики и поиска
пути — `docs/PLAN_BATTLE_AI_CPP.md` (цель — читаемость, fidelity-first; нативные
листья для GUI/плумбинга допустимы). Правила: `AGENTS.md`.

## Файлы

| файл | роль |
|---|---|
| `ldhook.cpp` | .pyd: логгер + `install_site1/2/3/5/6` (скан + E9) + общие утилиты + Python-API отладки + `initldhook`. Порт на C++ (Вариант B: C++ + статический CRT `/MT`); `initldhook` — `extern "C"` (C-ABI для `imp.load_dynamic`). `ldhook.c` оставлен как reference (DEPRECATED, не собирается). |
| `ldhook_internal.h` | общие объявления между translation units: логгер/троттлинг/замер времени, `rel32_fits`, трамплины (`site*_hook`, `s5/s6_*_hook`) и счётчики сайтов; сигнатуры сайтов (только для установки). |
| `sites_hd.cpp` | site1/site2/site3: impl'ы, naked-трамплины, x87-хелпер `site3_fp` (перенесено из `ldhook.cpp`, логика 1:1). |
| `battle/structs.h` | именованные структуры `BattleAI`, `Candidate` (=`LDObject`), `TreeNode`, `GridLevel`, `GridCell`, `PathState`, `LDPos` с семантическими полями и layout-`static_assert`; размер `BattleAI` уточняется Фазой 1. |
| `battle/battle.h` | объявления слоёв боевого AI (grid/path/target/tick). |
| `battle/native.{h,cpp}` | `native::fn(VA)` = base + (VA−0x400000) + именованные обёртки листьев (`lower_bound` 0x434bc0, `tree_next` 0x47f800, `tree_lookup` 0x449160, `register_occupied_slot` 0x45dd00). |
| `battle/path.cpp` | pathfinding: `s6_tree_next`, `s6_map_lower_bound`, `s6_pfs_ctor`, `s6_imax` (0x45c390), `s6_path_tree_search` (0x45c3a0), `s6_path_search`. |
| `battle/grid.cpp` | доступ к сетке: спуск по `next_level` (+0x1c), ячейка `cells + idx*0x34`. |
| `battle/target.cpp` | цикл поиска цели `FUN_0045e0a0` и callees (`ce80`/`c880`/`c5a0`/`c4b0`/`dd70`), хуки site5 (диагностика) и site6 (faithful-порт + счётчики цикла). |
| `battle/move.cpp` | `s6_unit_ai_tick` — тик юнит-AI `FUN_0045eee0` (Фаза 4: порт тела + детур). |
| `battle/tick.{cpp,h} + trace.h` | тик `FUN_0044bdd0` (Фаза 5) и trace-каркас `LDHOOK_BATTLE_TRACE` (формат строки — п. 1.7 плана; API/формат — `trace.h`). |
| `battle/trace.cpp` | наблюдатели тика H1/H2/H3/H7 + trace-строка (29 полей; H5/H6 — точки site5/порта), общий счётчик `vis` с портом `s6_ai_update`, пиннинг seed'а R1..R3. |
| `fetch_headers.py` | официальный Python 2.2.3 (python.org): `Include/*.h` + `PC/pyconfig.h` → `vendor/py223/include/` (untracked) |
| `build.py` | MSVC x86 (`/LD /MT /EHsc /O2 /W3 /fp:precise`) по списку источников → `build/ldhook/ldhook.pyd` (gitignored). `/fp:precise` обязателен — битовая identity 4:3 в site1/site3 |
| `smoke_api.py` | локальный смоук Python-API под `vendor/py223` (без игры): `log`/`set_level`/`stats`/`site6_stats`, уровень 0, TypeError, безопасный скан `.text` |
| `patch_ui.py` | каноническая установка загрузчика в игру: `--build` (ui.pyc + блок `imp.load_dynamic`), `--install` (бэкап + `.pyd` + `ui.pyc`), `--restore` |
| `check_sites.py` | аудит Фазы 4 (без игры): уникальность трёх сигнатур в `.text`, вердикт `installable`/`patched`/`UNEXPECTED` (rc 1) для установки и baseline |
| `spike.py` | полная копия установки → `<scratch>/ldhook/tree`, ldhook.pyd в корень, `ui/ui.pyc` = `patches/ui/ui.py` + блок `imp.load_dynamic`, config для scratch; `--baseline` — EXE = baseline |
| `probe_spike.ps1` | спайк 0-бис: запуск из scratch, снимок ОКНА, гашение, проверка маркера |
| `probe_phase1.ps1` | Фаза 1: «Новая игра» → экран выбора персонажа (site1), снимок окна, печать `hdlog.txt` с «FIRED» |

## Логгер (Фаза 3)

Один лог движка — `hdlog.txt` в каталоге EXE: тот же файл, куда пишут игровые
Python-патчи (`hd_scale.py`, `tex_hook_*.py`), поэтому `probe_exitcode.ps1`
уже копирует его хвост в `<Tag>_hdlog.txt`. `ldhook_log.txt` прежних фаз
больше не пишется.

Формат строки: `ldhook: 12:34:56.789 t3b58 I ...` (время, tid, уровень
`I`/`D`), дубль — `OutputDebugStringA`.

| флаг | поведение |
|---|---|
| `LDHOOK_LOG=0` (`off`) | файл не создаётся, ODS молчит; маркер `ldhook_marker.txt` пишется всегда |
| `LDHOOK_LOG=1` (дефолт) | диагностика: загрузка, установка сайтов, guard identity, первый вызов хука |
| `LDHOOK_LOG=2` (`debug`) | + числовая трассировка масштабирующего пути (троттлинг: site1 4+1024, site2 4+1024, site3 3+300) |
| `LDHOOK_BATTLE_TRACE=1` | трассировка боевого тика (`battle/trace.cpp`, формат — `battle/trace.h`): одна строка `BATTLE …` на вызов `FUN_0044bdd0`; хуки H1/H2/H3/H7 ставятся только при включённом флаге (H5/H6 читаются из трамплинов site5 либо из порта site6) |
| `LDHOOK_SEED=<uint32>` | пиннинг RNG для A/B: `push <константа>` на R1..R3 (`0x49ebcf`, `0x4a7677`, `0x4ae87f`) + `whrandom.seed(7,11,13)` из загрузчика `.pyd`; decimal или `0x`-hex. Не задан — seed-хуки не ставятся |

`ldhook.set_level(n)` из игрового скрипта перекрывает env на лету (возвращает
прежний уровень); `ldhook.log(msg)` — строка info из Python; `ldhook.stats()`
→ `(site1, site2, site3, level, fc_calls, fc_iters, fc_code1, fc_code3,
fc_code4, fc_rot, fc_us_sum, fc_us_max)` — счётчики faithful-цикла `e0a0`
(коды 1/3/4; `code=0` пустой скан = `fc_calls − fc_code1 − fc_code3 − fc_code4`).
Ротация: файл > 8 MiB → `hdlog.txt.1`.

В 4:3 при дефолтном уровне в лог попадают только диагностические строки:
`site3_impl: guard 4h==3w -> identity`, `site1_impl: k==1.0 -> identity (4:3)`
— трассировка уровня 2 относится к 16:9-ветке.

## Прогон

```
python3 tools/ldhook/fetch_headers.py
python3 tools/ldhook/build.py
PYTHONDONTWRITEBYTECODE=1 LDHOOK_LOG=2 vendor/py223/python.exe \
    tools/ldhook/smoke_api.py build/ldhook/ldhook.pyd   # API без игры
python3 tools/ldhook/spike.py --baseline           # scratch с baseline EXE (cave нет)
```

Дальше — канонический зонд `tools/probes/probe_exitcode.ps1` (новая игра →
оверленд, там работают site2 и site3); `LDHOOK_LOG` пробрасывается в игру
через окружение:

```
$env:LD_GAME="<scratch>/ldhook/tree"; $env:LD_SCRATCH="<scratch>"; $env:LD_REPO="<repo>"
$env:LDHOOK_LOG="1"      # или 2 для трассировки
powershell -NoProfile -ExecutionPolicy Bypass -File tools/probes/probe_exitcode.ps1 `
  3840 2160 1 ldhook_p3 -Sub ldhook_p3
```

4:3 identity (site3 guard + site1 identity) — тот же зонд с `1440 1080 1`;
`LD_REPO` лучше указать в scratch, чтобы не переписывать `hd.cfg` репозитория
(после 4:3 вернуть `hd.cfg` = `3840 2160 32 1`).

`spike.py` без `--baseline` — EXE = установленный (cave): сигнатуры сайтов уже
`E9 rel32`, хуки не встают (лог `signature NOT found`, это нормально). Такой
scratch удобен как эталон `exepatch12` для A/B при том же сейве/камере.

## Гейт (Фазы 1–3)

- `<scratch>/ldhook_p3/<tag>_hdlog.txt` (копия `hdlog.txt`) содержит
  `install_site1/2/3: ... found` и `site1_impl/site2_impl/site3_impl: first
  call (hook FIRED)`;
- 16:9 (`hd.cfg` = `3840 2160 32 1`): `site3_impl: ... matrix[0] 1.29904
  (0x3FA646F1) -> 0.974731 (0x3F7987FA) m5=1.73205 (0x3FDDB3ED)` — новые биты
  совпадают с эталоном `exepatch12`; `site1_impl: k=0.750347 -> scale path,
  keff=0.187587`;
- 4:3 (`hd.cfg` = `1440 1080 32 1`): `site3_impl: ... guard 4h==3w -> identity`,
  `site1_impl: k==1.0 -> identity (4:3)` — ни одной записи; site2 при этом
  срабатывает и зовёт `FUN_0041cc90` (в логе виден `site1_impl`);
- при `LDHOOK_LOG=1` строки только `I`; при `LDHOOK_LOG=2` появляется
  `D`-трассировка site3/site2/site1, троттлинг виден по номерам вызовов;
- игра доходит до оверленда без крашей; снимок окна не чёрный (средняя
  яркость 4:3 0.271 / 4K 0.268 на `*_overland.png`);
- A/B с установленным cave-EXE на том же сейве/камере: кольцо домена
  `measure_disc` даёт hook **486x451** против cave **485x453**, центр совпадает
  (`2780,1309`) — геометрия идентична. Базовые замеры `a169d` = **1.4035** и
  `p12b` = **1.0531** воспроизводятся `measure_disc.ps1` на регионе
  `1540 1040 700 540`.

## Дальше (Фаза 4, PLAN_proxy_dll.md §4)

Фаза 4 выполнена (18.11.2026): **EXE не трогаем**, установка остаётся на
cave `exepatch12` (`171319bb…`), `ldhook.pyd` в поставку не входит — это
проверенный путь следующих правок движка. Аудит сайтов без игры:

```
python3 tools/ldhook/check_sites.py                      # установка + baseline
python3 tools/ldhook/check_sites.py --exe <путь к EXE>   # произвольный файл
```

Вердикты: `installable` (все 3 сигнатуры по одному разу в `.text` — baseline
`45e93d46…`, хуки встанут), `patched` (сигнатур нет, на сайтах `E9` — cave
`exepatch12`, `.pyd` штатно no-op), `UNEXPECTED` (rc 1 — сайты сдвинулись или
сигнатуры перестали быть уникальными; правку EXE пересмотреть, fallback А1).
Правило: **после любой правки EXE — `check_sites.py`**.

Живая матрица стабильности (Фаза 4): 4:3 `1920x1440`, `1440x1080`, `2880x2160`
— identity-guard'ы, записей нет; 16:9 `3840x2160` (повтор) — те же VA
`0x41ce31/0x40b494/0x47dd80` и те же биты `0x3FA646F1 -> 0x3F7987FA`, что и в
Фазах 2–3, крашей нет. Условия перехода на `.pyd` в установке (baseline EXE,
allowlist, `ui/ui.pyc`, исключение runtime-логов, гейт) — `docs/PLAN_proxy_dll.md`
§4 Фаза 4.

## Боевой поиск цели: диагностика (site5) и faithful-порт (site6)

**site5** (диагностика, активен при `LDHOOK_SITE6=0`) — логирует аргументы,
кандидатов, метрику `[obj+0x24]`, порог расстояния `[this+0x34]`, трекинг
`[this+0x30]`. Сайты: пролог `0x45e0a0` (7 Б), хвост цикла `0x45e163` (6 Б) и
эпилоги `add esp,0x34; ret 10h`. Уровни: `LDHOOK_LOG=1` — только «first call»;
`LDHOOK_LOG=2` — первые 4 итерации + каждый 256-й.

**site6** (`LDHOOK_SITE6=1`) — полный detour `FUN_0045e0a0`: faithful-цикл на
C++ (`battle/target.cpp`), callees портированы. Счётчики цикла отдаёт
`ldhook.stats()` (см. «Логгер»).

```
LDHOOK_LOG=2 <EXE>                 # диагностика поиска цели
python3 tools/ldhook/patch_ui.py --build     # ui.pyc + загрузчик (.pyd)
python3 tools/ldhook/patch_ui.py --install   # бэкап + .pyd + ui.pyc в установку
python3 tools/ldhook/patch_ui.py --restore   # откат: вернуть ui.pyc, снять .pyd
```

История: cap итераций (`site4`, 19.11.2026) лаги убирал, но ломал агро (31%
поисков → «цель не найдена»); код и API сняты в п. 3.4 плана. Разбор —
`docs/PERF_BATTLE.md`, «Ревизия плана».

## Офлайн replay-харнесс e0a0 (LDHOOK_BATTLE_CAPTURE)

Опционально; без `LDHOOK_BATTLE_CAPTURE` файл не пишется, хуки листьев не
ставятся. Хуки — на входе cb40/c690/c5a0 (`E9`, 5 Б), поток листьев пишется в
`res`. Прогон в scratch-дереве (`$env:LD_GAME`), не в установке:

```
python3 tools/ldhook/build.py
python3 tools/probes/battle_replay_build.py
build/ldhook/battle_replay.exe [--chained] [--leaf-replay|--real-path] <capture.jsonl> <res_port.jsonl>
python3 tools/probes/battle_replay_diff.py <capture.jsonl> <res_port.jsonl> [--prefix=N] [--fields=a,b,…] [--limit=N]
python3 tools/probes/leaf_stream_diff.py <capture.jsonl> <res_port.jsonl> [--extra-limit=N]
```

`battle_replay_diff.py` — сравнение по seq (node, code, iters, iter_seq,
`map1_after`, `occ_after`, leaf; `--fields=leaf` — только поток листьев); rc 0 равно,
1 расхождение, 2 битый вход, 3 prefix не выполнен; `DIVERGE` + ±2 контекста в stdout.
`leaf_stream_diff.py` — поэлементное сравнение `res.leaf.path` с
`res_port.leaf.path` по seq (общий префикс + маркер `EXTRA` хвоста); rc 0/1/2.

Захват v8 (по умолчанию), ключи по версиям: v3 — `ai_words` (скалярный блок
BattleAI), v4 — `ai_words_after`; v5 — запись `grid` перед первой `cap` (уровни/размеры,
`cells` (LDPos-вид, метрики, ключи, связи), `rows`, статусы world-дерева, внешние входы
эвристики, `res.timer`); v6 — значения гейта листьев + стек-арги cb40; v7 — `scale`/`scale_ok`:
float `world[0x120]` на входе каждого cb40-вызова по цепочке self→[0]→+4→+0x120, харнесс
подаёт его world-масштабу перед каждым cb40-вызовом порта; v8 — снимок цепочки c3a0 на каждый
cb40-вызов (count, idx, min/max, key, next, chain, status), replay применяет снимок,
собственные регистрации порта питают `tree_lookup`. Порт исполняет
`s6_path_search`/`s6_pfs_ctor`/`s6_tree_next`/`s6_heuristic` сам; `res_port.leaf` — его
поток листьев. Отсутствие ключей в захвате — сводки `GATE:`/`SCALE:`/`CHAINSTATE:`,
счётные значения годны только при skipped = 0.

Гейт (решение 3.7) — режим по умолчанию (real pathfinding): replay сам сравнивает результат
и leaf-поток (`LEAF:`/`RESULT:`, rc 1 на расхождении), полевой уровень —
`battle_replay_diff.py`. `--leaf-replay` — A/B харнесса с подменой листьев, **без сравнения**
(блок сравнения в `if (!leaf_replay)`), его rc 0 вакуумный, не гейт. Захваты без
используемого `grid` ⇒ `ERROR`, rc 2. Живой A/B — санитарный:
`trace_diff.py --align=ordinal`; `unit=` в trace синтетический, кросс-прогонная сверка
сравнивает разные юниты.

`--chained` — **диагностика, не гейт**: игра очищает карты кандидатов/занятых
**между** вызовами e0a0 — `FUN_0045ca70` на `0x45efd7` перед регистрацией seed,
условно по биту 7 флагов `[obj]` (в захвате `cap.map1` следующего вызова = 1
узел против сотен в `map1_after`), поэтому `entry_state_mismatch` в цепи ожидаем
и не дефект порта; faithful-гейт — обычный per-call replay (`battle_replay_diff.py`).
Реплика инициализируется один раз из первой полной `cap`, дальше в каждый вызов подаётся
СОБСТВЕННОЕ after-state порта; после каждого схода сравниваются `node/code/iters/iter_seq`
и after-state (`map1_after`/`occ_after`/`ai_words_after`). Класс `MUTATE` = результат совпал,
а состояние нет; в выводе — дельта (pos/pos_addr/filter/g/cost) и ветка порта
(`dd70` branch 1 → `0x45dd00`). Итог `CHAIN:` даёт chain_length, survived (= число совпавших
подряд вызовов до первого расхождения), entry_state_mismatch, first_div_seq/class,
ai_after_compared/ai_after_skipped/ai_words_mismatch; rc 0 равно, 1 расхождение, 2 битый вход.
Скалярный блок сравнивается только при наличии ключа (с v3/v4), отсутствие — сказано в
строке `CHAIN`. `LD_REPLAY_TRACE=1/2` — лог порта в stderr.
