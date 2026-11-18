# tools/ldhook — .pyd-хуки в памяти (PLAN_proxy_dll.md, §2A, Фазы 0-бис–3)

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

Guard'ы перенесены 1:1: 4:3 остаётся побитово прежним (никаких записей).
site3 считает FP-последовательность `cave3` (x87: `fild`/`fild`/`fdivp`/
`fmul m32`/`fstp m32`) в отдельном naked-хелпере `site3_fp` — результат
побитово совпадает с `exepatch12` (`matrix[0] = 0x3F7987FA`).

`.text` для скана берётся из PE-заголовков (VA + VirtualSize), не из констант:
под чужим EXE (`vendor/py223/PYTHON.EXE`) модуль грузится без риска выйти за
секцию, а сдвиг `.text` в игре не ломает установку хуков.

Канва: `docs/PLAN_proxy_dll.md` (§2A, §4 Фазы 1–3). Правила: `AGENTS.md`.

## Файлы

| файл | роль |
|---|---|
| `ldhook.c` | .pyd: логгер + `install_site1/2/3` (скан + E9) + `site1_hook`/`site2_hook`/`site3_hook` + `site3_fp` (x87) + Python-API отладки |
| `fetch_headers.py` | официальный Python 2.2.3 (python.org): `Include/*.h` + `PC/pyconfig.h` → `vendor/py223/include/` (untracked) |
| `build.py` | MSVC x86 (`/fp:precise`) → `build/ldhook/ldhook.pyd` (gitignored) |
| `smoke_api.py` | локальный смоук Python-API под `vendor/py223` (без игры): `log`/`set_level`/`stats`, уровень 0, TypeError, безопасный скан `.text` |
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

`ldhook.set_level(n)` из игрового скрипта перекрывает env на лету (возвращает
прежний уровень); `ldhook.log(msg)` — строка info из Python; `ldhook.stats()`
→ `(site1, site2, site3, level)`. Ротация: файл > 8 MiB → `hdlog.txt.1`.

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

- Решение по EXE: in-memory хуки стабильны на всех трёх сайтах →
  EXE не трогаем; `exepatch12.py` — эталон/откат.
