# TASK: Фаза 5 — верхний уровень `FUN_0044bdd0`

Рабочий документ для продолжения после compaction. Правила репозитория — `AGENTS.md`;
план и решения — `docs/PLAN_BATTLE_AI_CPP.md`; состояние — `docs/STATE.md`.

## Где мы

- Ветка `perf-battle`, HEAD `6e1cdb2` (запушено), дерево чистое.
- **Фазы 0–4 закрыты.** Гейт Фазы 3 — решение 3.7, Фазы 4 — решение 4.8 (оба в плане).
- Остаток (решения 3.8 / 4.8): после-состояние `map1`/`occ` и флаг `s30` расходятся на части
  вызовов — класс «порт находит путь там, где натив не находит»; решения и их аргументы
  совпадают. Фазу 5 не блокирует.
- Установка `D:/Games/LethalDreams` не тронута (`--full` 0 нарушений). `config.toml` переключён
  на `E:/ld_backup/9.7z` (личный файл, не в git), `cache/lib` перезаполнен из него.
- Scratch-дерево: `D:/tmp/cygwin/opencode/ld/ldhook/tree`, baseline EXE `45e93d46…`.

## Что сделано в Фазе 4 (для контекста)

- `battle/move.cpp` — faithful-порт тела `45eee0`, имя **`s6_unit_ai_tick`** (это тик юнит-AI,
  не «движение»): ctx из `rec`, флаги полос/сброса/карт, посев параметров и spec-окна,
  grid-якорь + `s6_path_search`, два поиска `e0a0` (filter 1 по якорю, filter 2 по pos первого
  узла), пост-обработка (band rebuild, сдвиг кода 1→2, порог dist²). Сигнатура нативная
  `int (BattleAI*, void*)`, `ret $4`, тело под `__try` (§8.1 п.9).
- Детур: `install_tick_hook()` в `ldhook.cpp`, gate `s6_port_enabled()` + проверка раскладки
  (`ld_off_ok*`); хук и трамплин — naked в `move.cpp`, `S7_ENTRY_SIG` = 5 Б
  (`sub $0x58,%esp; push ebx; push ebp`), продолжение `0x45eee5`. Байпас — `g_s7_tick_on`.
- Диспетчер `e0a0`: site6 включён → порт `s6_target_search`, выключен → `native::target_search`.
- Харнесс: capture **v9** (запись на каждый `eee0`-вызов: `rec`-блок, поля `self`, ctx, grid,
  map1/occ, якорь, результаты) + replay `eee0` в `battle_replay.cpp` (`EEE0:` сводка).
- Дизасм-карта: `D:/tmp/cygwin/opencode/ld/re/phase4_45eee0.md` (855 строк, §1 слоты, §2 ветки,
  §3 память, §5 листья, §8 split, §10 ESP-таблица). В ней есть ошибки — листинг главнее.

## Что известно про `44bdd0` до дизасма

- Единственный call site `45eee0`: `0x44cbd0` внутри `44bdd0`; на этом сайте есть хук **H3**
  (`h3_code_hook` в `battle/trace.cpp`, украденные `movl $0,[esp+0xc0]` + `call 45eee0`,
  продолжение `0x44cbd5`), в него уже встроены обёртки capture v9.
- `44bdd0` вызывает себя (единственный вызывающий) — рекурсия/обход юнитов.
- В capture уже есть `u170`/`al` — поля, которые гейт Фазы 5 требует сверять.
- Листинг: `D:/tmp/cygwin/opencode/ld/re/game_dis.txt`, смещение = VA − 0x400000.

## Инструменты (все opt-in; без env поведение не меняется)

```
python tools/ldhook/build.py
python tools/probes/battle_replay_build.py
# capture (нативный прогон; игра гасится после):
LDHOOK_SITE6=0 LDHOOK_BATTLE_TRACE=0 LDHOOK_LOG=1 \
LDHOOK_BATTLE_CAPTURE=<path>.jsonl LDHOOK_BATTLE_CAPTURE_MAX=2000 \
LDHOOK_SEED=1234567890  ->  tools/probes/battle_43.ps1 (один key:32 на входе в бой)
# replay + сверка:
build/ldhook/battle_replay.exe <capture>.jsonl <port>.jsonl
python tools/probes/battle_replay_diff.py <capture>.jsonl <port>.jsonl
python tools/probes/leaf_stream_diff.py <capture>.jsonl <port>.jsonl
python tools/probes/trace_diff.py <nat.log> <port.log> --align=ordinal
```

- Регрессионная база (не должна падать): `confirm_a` 119/125, `confirm_b` 108/111,
  `chain_v8_capture` 112/120 — все в `D:/tmp/cygwin/opencode/ld/`.
- Фаза 4: `eee0_v9_capture.jsonl` — 28 вызовов, `EEE0: equal=3` по всем полям, но
  решение (`ret`, `s38`, якорь, порог) **28/28**; `LD_REPLAY_DIV_DUMP=1` печатает `EEE0_DIV`.
- `--leaf-replay` — **не гейт**, `--chained` — только диагностика.

## Грабли, уже оплаченные временем

- `unit=` в trace — синтетический номер по первому появлению, не идентичность: живой A/B
  по `(tick,unit)`/`(unit,ordinal)` сравнивает разные юниты.
- `world[0x120]` — `(ctx+4)+0x120`, не `game_ctx+0x120` (иначе метрика гейта +2.0).
- Маски x87: `0x41` (C0|C3) и `0x44` (C2|C3) дают **разную** семантику. Для `0x44`/`jnp`:
  пропуск ⟺ значение ровно `0.0f` (отрицательные и NaN идут); для `0x41` — строгое `>`.
  Проверено пробой и разбором FCOMPS.
- Слоты `lower_bound` (`0x434bc0`): **ключ = первый push, out = второй**; обёртка
  `native::lower_bound_slots(map, out, key)`.
- `45c4b0` (копия по маске): аргументы натива `(src, dst, mask)`; порт — `(dst, src, mask)`.
- Живой прогон перезаписывает 4 шрифта установки → `--full` покажет `DRIFT hd_pack`;
  восстановление по `PITFALLS.md` (`7z` из `config[hd_pack]`). В последнем прогоне дрейфа не было.
- Подробности — `docs/PITFALLS.md`, раздел «Офлайн-харнесс e0a0».

## Порядок работы

- Исполнитель-сабагент по одному пункту; **ревью — всегда `strata`**; всё, кроме
  среднего и сложного, — `strata/q3.8-fn#medium`. Средние/сложные —
  `ernie-5.5-flash:free#high`: при обрыве до 10 раз `continue` той же сессией, после
  10-й неудачи — эскалация к человеку (или strata с обязательным ревью, если так решено).
  Перед запуском/продолжением — `<scratch>/ctx.py <sessionID>`: при ≥256k — новая сессия.
  Если человек сообщает, что суточные лимиты бесплатных моделей исчерпаны,
  средние/сложные идут на `@preset/ds-flash-41-cheapest#low`.
- Был случай: бесплатная модель отдавала пустой отчёт, но дерево меняла — после
  «completed без текста» сначала смотреть `git diff` и замер контекста, потом решать.
- Сабагенты **не коммитят**: коммит и docs — оркестратор. Один сабагент за раз.
- После каждой правки: сборки, `check_sites.py`, replay на регрессионной базе (числа не
  должны падать), `check_state.py --quick`.
- Игру запускать только на прогон гейта и сразу гасить `taskkill /F /IM LETHALDREAMS.EXE`.
- Фазу 5 **не начинать без одобрения пользователя**; Фазу 6 — после 5.

## С чего начинать Фазу 5

5.1 — скелет state machine по п. 1.3a, **все листья нативные**: логика переходов на C++,
детур, гейт «скелет ничего не меняет» (per-call replay + живой A/B санитарный).
Первый шаг — дизасм тела `44bdd0` (карта веток/полей, как в Фазе 4: артефакт в
`<scratch>/re/`, чанками, с обязательным `wc -l` в отчёте) на strata, затем аудит карты
на strata с проверкой адресов и полноты переходов.
