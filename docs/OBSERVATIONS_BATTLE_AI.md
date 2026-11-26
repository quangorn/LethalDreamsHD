# OBSERVATIONS_BATTLE_AI.md — как устроен боевой AI

Источники: `tools/ldhook/battle/{tick,target,path,grid,recompute,structs}.h/cpp`;
адреса — якоря в скобках (`LETHALDREAMS.EXE.pre4`, imagebase `0x400000`).
Разбор ходов, которыми это установлено, — `docs/PLAN_BATTLE_AI_CPP.md`.

## 1. Тик `44bdd0` — конечный автомат R1..R23

Кадр: `frame = S − 0x158` (S — ESP на входе). Слоты: `frame+0x15c` = param_1,
`+0x160` = param_2, `+0x164` = param_3 (out-указатель), `+0x14c` = prev FS:[0],
`+0x154` = trylevel. Фазовые локалы: `+0x1c` (msg-буфер), `+0x20..0x28` (out-тройка),
`+0x2c..0x38`, `+0x4c..0x87`, `+0x88` (grid-record), `+0xa4..0xbc` (per-tick запись
из 7 dword), `+0xcc` (PathFinder). Порт кадр не сдвигает; EBP/ESI/EDI/EBX снимает
нативный эпилог.

| R | диапазон | содержание / ветви |
|---|---|---|
| R1 | `0x44bdf9..0x44bec1` | state-геттер, phase-гейт, msg-строка, `446a80` |
| R2 | `0x44bec2..0x44bfb7` | band-гейт count>1 (`0x44bf01 jg` → `0x44d02e`, обход `448ee0`), цепочка флагов |
| R3 | `0x44bfb8..0x44c09c` | player+флаги (биты 2/4), cursor-блок A, msg-флаг `unit+0x34` |
| R4 | `0x44c09d..0x44c183` | phase-гейт, строка B, cursor, out-guard |
| R5 | `0x44c184..0x44c229` | game_state, phase против 3.0f (`TEST AH,0x41; JP`) |
| R6 | `0x44c22a..0x44c306` | grid-probe: два `44bbf0`, assert-лог, цепочки, msg-флаг → выход 0 |
| R7 | `0x44c307..0x44c3ec` | строка C (`ADD EBP,0x18` здесь), ctx bit3, `45c730` cursor, вход band-loop |
| R8 | `0x44c3ed..0x44c4dc` | band-loop тело A: count-гейты, `403e80`, `45b150` |
| R9 | `0x44c4dd..0x44c5b0` | тело B: `45b3e0`, log-блоки E/F |
| R10 | `0x44c5b1..0x44c67d` | тело C: `45b150` против 3.0f, `45b3e0`, back-edge `0x44c66d` → B46 |
| R11 | `0x44c67e..0x44c74c` | log-блок H, cursor-tail, pos-слот `45c6f0`, flags-join |
| R12 | `0x44c74d..0x44c82b` | PathFinder-init `frame+0xcc`, grid-record `frame+0x88`, `458050` anchor |
| R13 | `0x44c82c..0x44c877` | flags-цепочки anchor-NULL; все выходы → dtor-join `0x44cad2` |
| R14 | `0x44c878..0x44c96f` | world-notify + `45e1d0` → запись `unit+0xa4..0xac`, save/restore pos-полей, FSTP фазы |
| R15 | `0x44c970..0x44ca4b` | float-гейт `[unit+0xfc]`, msg-клон + `446a80`, `448c30(0x40)` |
| R16 | `0x44ca4c..0x44cb38` | cursor-хвост, out-write `*param_3=1`, msg-dtor, dtor PathFinder `45e5e0`, sentinel −10000.0f (`0xc61c4000`) |
| R17 | `0x44cb39..0x44cc18` | per-tick запись 7 dword (`frame+0xa4`, ECX=`&unit+0x138`) → `45eee0`; `[unit+0x170]==0` → assert-лог (код 0xa) |
| R18 | `0x44cc19..0x44cd09` | dispatch-каскад кодов 2/3 (`[unit+0x170]`), GUI K1, out-guard `*param_3=0` |
| R19 | `0x44cd0a..0x44cdf1` | GUI K2; гейты code==4 (head/count); `vtable+0x2c` против `[unit+0xfc]`; mode-флаг 1/4 (state≠3/==3) |
| R20 | `0x44cdf2..0x44cee5` | state-копия 32 B, cursor K3, builder + `446a80` (`ret 0x3c`) |
| R21 | `0x44cee6..0x44cf77` | join out=1 + msg-dtor; фаза code==4: цепочки 2/4, `448c30(0x40)`, лог `0x51cc50`/0xa |
| R22 | `0x44cf78..0x44d071` | финальные band-гейты, `448ee0(unit,0/1)`, out-writes, AL |
| R23 | `0x44d072..0x44d08e` | нативный эпилог (сохранён) |

Выходы (`B0_EXIT(128..135)` → AL, запись `*param_3`): 128/133
(`0x44d061..0x44d072`) — 0/0; 129 (`0x44cae9`, cursor-хвост R16) — AL 0, out 1
ранее; 130 (`0x44cc7f`, sentinel-путь) — AL 0; 131 (`0x44d03f`, B148) — 1/1;
132 (`0x44d054`, B151) — 1/0; 134 (`0x44cf0b`, join R21) — 0/1; 135 — SEH, 0.
Гейт B144/B145 (`0x44cfea..0x44d019`): head==NULL или count≤1 (знаковое `JLE`) →
AL=0; `NOT([unit+0x114] > 0.0f)` (маска 0x41 + JP) или `[unit+0x84]==0` → AL=1,
out=0; иначе `448ee0(unit,0)`, AL=1, out=1.

## 2. Поиск цели `45e0a0` и скан `45ce80`

Цикл (`0x45e0a0..0x45e1c0`, cdecl, 5 аргументов: filter, pos_obj, timeout,
out-code): `*param_4 = 0` (`0x45e0b2`); `cur = scan(filter)` (`0x45e103`); пока
`cur != 0`: `45dd70(this, cur, pos_obj)` (`0x45e11b`), `45c4b0(&{+8=3}, cur, 4)`
→ `cur->filter = 3` (`0x45e132`, permanently, restore нет), гейты в родном
порядке (`cur->pos == cmp` → code=1; `45c880` → code=4; `45c5a0` → code=3), затем
**rescan** (`0x45e15e`); пустой скан → NULL, код не пишется. Инвариант: каждый
кандидат помечается `filter=3` и выпадает из следующего прохода ⇒ скан монотонно
убывает, цикл завершается за число кандидатов.

Скан `45ce80`: один in-order проход map1 (`this+0x6c` = sentinel-голова, шаг —
`47f800`); фильтр `[obj+0x08] == filter`; минимум **беззнакового** `[obj+0x24]`;
возврат `LDObject*` или NULL (`best == head`). Состояния фильтра: 1 = кандидат,
2 = текущий, 3 = посещён. Гейт ветки 2 `45dd70` (`0x45dfb0/0x45dfba/0x45dfc0`):
работает при `cur.filter==2 && nbr.filter!=3`; рекурсия `45dd70(nbr, anchor)`
при `nbr.filter==1` (`0x45dfd2`). `45dd70` поднимает метрику узла:
`metric = h·10000 + dist² + (g+1)` (`0x45df95`, `0x45e032`) — её скан и считает
минимумом, поэтому следующий скан отдаёт другого кандидата.

## 3. A*: PathState, карты, маски

PathState (0x58; ctor `45c2e0` аллоцирует сам через `4cfa11`; Ghidra-подпись
`thiscall(this)` неверна — ECX в теле не читается):

| поле | offset | смысл |
|---|---|---|
| pos / parent | `+0x00` / `+0x04` | LDPos* (`+0x0c`=X, `+0x10`=Y) / родитель |
| filter / reserved_0c | `+0x08` / `+0x0c` | 1/2/3 / ctor обнуляет, потребитель не найден |
| out_metric / metric_cand | `+0x10` / `+0x14` | float-выход цепочки `45c3a0` / float-метрика |
| g_cost / dist2 | `+0x18` / `+0x1c` | int g пути / dist² к цели |
| h10000 / metric | `+0x20` / `+0x24` | h·10000 / h·10000 + dist² + g |
| min/max sentinel | `+0x28` / `+0x2c` | `0xcb18967f` (float −1e7) / `0x4b18967f` (float +1e7) |
| reserved_30 / nbr[9] | `+0x30` / `+0x34..0x54` | обнуляется ctor'ом / соседи 3×3 |

Кольцо 3×3: `idx = 3·(x−x_start) + (y−y_start)` (`0x45de46`), обратная ссылка
`nbr[8−idx] = cur` (`0x45e050`). Окно: `x_start = max(x−1,0)`,
`x_max = min(x+1, size_x−1)`; `x_start > x_max` → цикл пропускается. Карты:
**map1** (`this+0x68..0x70`) — ключ = **указатель pos-блока**; **map2**
(`this+0x74..0x7c`) — ключ = `(Y<<16)|X` (младшие слова X/Y). `lower_bound` —
`434bc0`: узел == head → клетка свободна; иначе занята, `node->value` = PathState.
Регистрация — `45dd00` (две RB-вставки). Маски `45c4b0` (каллер `(src,dst,mask)`,
порт `(dst,src,mask)`): бит0→`+0x00` … бит6→`+0x18`, бит7 (знаковый тест)→`+0x1c`,
DH.0→`+0x20`, DH.1→`+0x24`, DH.2→`+0x28`, DH.3→`+0x2c`, DH.4→`+0x30`. Ветки
`45dd70`: `0x1fff` (полная), `0x3c6` = {`+0x04,+0x08,+0x14,+0x20,+0x24,+0x28`},
`4` = только `filter`.

Сетка: `456ed0(level, idx)` — лист по цепочке `+0x1c`, `cells + idx·0x34`;
`4570d0(grid,x,y)` — **только получает** (`rows[x][y]` → `456ed0`); создание узла
— в `45dd70`. Обход цепочки `45c3a0`: начало `[p1+0x28]` → `456a70`, шаг
`node = [node+0x24]` (капы нет); статус `tree_lookup(world, key_rec->key)`:
1 → кандидат, 0 → сравнение `best->min_metric` vs `node->max_metric` (JP-маска
0x5: при `a>=b` сброс), >1 → пропуск; store `*p4 = world[0x120] + best->min_metric`;
гейт `|*p4 − *(float*)p3| / p7 > dist_threshold` → сброс. Полный поиск `45cb40`:
первый `45c3a0` от pos (p3=0, p7=1); при game_ctx bit1 — двойной цикл: outer
`pos.y ± x_radius`, inner `pos.x ± y_radius`, центральная клетка пропускается,
ключ клетки, `lower_bound(map2)`, `4570d0`, рекурсия `45c3a0` с
`p7 = imax(|dy|,|dx|)`; финальный гейт — успех при `JP(0x5)` по
`out_metric vs L[0x10]` **или** `JNE(0x41)` по `final_threshold + out_metric
vs L[0x10]`, иначе NULL.

## 4. Метрика и выбор цели

- `45c480(a,b)` — dist² = `dx² + dy²`, 32-битная wrap; сравнение в `45c880`
  беззнаковое (`JA`). `45c880`: `tracking` (`+0x30`) = 0 → 0; `dist²(pos, anchor) >
  distance_threshold` (`+0x34`) → `anchor = pos`, 1; иначе
  `distance_threshold = dist²`, 0.
- `45c5a0(timeout)`: `timeGetTime` → секунды (`fmull 0.001`, коррекция `2^32`),
  минус `timer_start_sec` (`+0x04`); x87: `≥` или unordered → 1.
- Гейт эвристики `45c690`: `world[0x110] != 0` → 0; биты 2/3 vtable ctx;
  `PtInRect(this+0x18, POINT)` — **оси POINT swapped** (`pt_x = pos+0x10`,
  `pt_y = pos+0x0c`); хвост отдаёт **сырой EAX** `45c5e0` (не 0/1).
- `45c5e0`: масштаб из `456b20(pos)` (`out[0] = x·mul + add`,
  `out[1] = y·mul + scale_load`); цепочки chain1/chain2 (x87 над int-слотами);
  лист `44bbf0` с 4 стек-аргументами.
- `45c8c0` (сайты `0x45e320`, `0x45efd0`/`0x45f0c7`): радиусы
  `+0x10/+0x14 = round((viewport[0x34] + 0.5)/scale_mul)`; `+0x08` = dword
  `viewport[0x30]`; `+0x0c = tanf(world[0x124])·scale_mul` (FPTAN, asm-bridge);
  RECT `+0x18` строится при бите 3 vtable:
  `left = x_radius·7 + Y + 1`, `top = X − y_radius·7`, `right = Y − x_radius·7`,
  `bottom = y_radius·7 + X + 1` (X/Y из якоря `this+0x2c`; **оси swapped**,
  масштаб сетки = 7 px).
- Выбор цели тиком: голова ленты `45c370`, длина `45af50 = (end−begin)/0x18`;
  pos-тройка из `45c6f0` (`ctx+0xc`); предикат движения ленты `45b3e0`
  (1 ⟺ dot ≥ 0 И `acos(dot) < 0.34906542f`); float-гейт расстояния до полилинии
  ленты `45b150` (BEST init 100.0f, arg2 мёртв).

## 5. Граф вызовов одного тика

```
44bdd0 (тик)
 ├─ 442570 game_state · vtable[+0x40] player · 42d9c0/435d50 flags · 44bbf0 grid_query
 ├─ 45c6f0 pos-тройка · 403e80 pos3-equal · 45c730 cursor · 448c30 cursor(0x40)
 ├─ 458050 grid-якорь (внутри: 457e20, 456b60) · band-loop: 45af50 длина ·
 │  45b150 float-гейт · 45b3e0 предикат движения
 ├─ 45e1d0 grid-recompute (≤1 на тик; фазы P0..P11 ниже)
 │   ├─ P3: 45c330 · 45ca70 · P4/P10: 456b20 ×2 · P5: 458050, 45c8c0 · P6: 434bc0
 │   ├─ P7: 45c2e0 · 45c480 · 45c690 · 45c4b0 · 45dd00 · P8: 45ce80
 │   └─ P9: 45dd70 + 45c4b0(mask 4) → 434bc0 · 456ed0 · 45c2e0 · 45cb40 → 45c3a0
 │       (рекурсия) · 45c480 · 45c690→45c5e0→44bbf0 · 45c4b0 · 45dd00; при
 │       nbr.filter==1 — рекурсия 45dd70(nbr, anchor)
 ├─ 45eee0 unit-AI tick (запись из frame+0xa4, ECX = &unit+0x138)
 └─ 448ee0 state-flag · 446a80 msg · vtable[+0x2c] phase
```

Портированные листья (claim из тела): `45c370`, `45af50`, `45c6f0`, `403e80`,
`45b150`, `45b3e0`, `456b60`, `457e20`, `458050`/`45e1d0` (условно — при наличии
модели данных). Нативны: `44bbf0`, `45c730`. Фазы листа `45e1d0` (кадр
`B = entry−108`, один `ret 8`, out-тройка `param_1[0..2]`):

| фаза | VA | callee'ы |
|---|---|---|
| P0/P1/P2 пролог, lazy alloc (`4cfa11` 0x1c/0x10), перенос рекорда/pos-блок | `0x45e1d0..0x45e272` | `4cfa11` ×2 |
| P3 сброс/очистка карт · P4 grid-scale ×2 + опора | `0x45e272..0x45e2de` | `45c330`, `45ca70`, `456b20` ×2 |
| P5 anchor + search_params · P6 lower_bound(map1, `pf+0x28`) | `0x45e2df..0x45e339` | `458050`, `45c8c0`, `434bc0` |
| P7 регистрация занятого слота | `0x45e33e..0x45e3f6` | `45c2e0`, `45c480`, `45c690`, `45c4b0`, `45dd00` |
| P8 обнуление шаблона + первый скан | `0x45e3f7..0x45e451` | `45ce80` |
| P9 цикл по кандидатам | `0x45e452..0x45e493` | `45dd70`, `45c4b0(4)`, `45ce80` |
| P10 терминальный кандидат · P11 out-тройка + эпилог | `0x45e494..0x45e504` | `456b20` ×2 |

Цикл P9: выход при `cand==0` (null-выход) и при `cand[0x20]==0` (h·10000 = 0);
тело монотонно снимает флаги, поэтому цикл конечен.

## 6. Бюджет вызовов на тик (наблюдаемые диапазоны)

| метрика | значение |
|---|---|
| leaf-вызовов на тик, медиана | 28 |
| p95 | 76–79 |
| максимум | 88 |
| `45e1d0` на тик | ≤ 1 |
| цикл P9 (`45e1d0`) | 0..22 итераций, среднее 4.5–6.1 |
| длина ленты (элементов по 0x18) | моделируется 1..64 |

Тик потребляет весь leaf-поток: непотреблённая запись — нарушение потока
(`leaf_seq`), а не молчаливое расхождение.

## 7. Открытые семантики

- `0x4d1950` (CRT-acos в `45b3e0`): точное поведение на 1 ULP от порога
  `0.3490654230117798` — **не установлено**; порт воспроизводит гейт через
  двойную точность и сверяется с записями, но сама CRT-функция не моделируется.
- `ctx+0x110` (byte, гейт `45c690`): смысл nonzero — **не установлено**;
  известно только «≠0 → эвристика 0».
- GridCell `min_metric` (`+0x14`) / `max_metric` (`+0x18`): кроме обхода
  `45c3a0` (посев ±1e7, сравнение) записей не найдено; наполнение при построении
  сетки — **не установлено**.
- Ключ map2 = `(Y<<16)|X` требует координат GridCell (`+0x0c/+0x10`); в записях
  сетки эти слова не сохраняются, поэтому офлайн ключ не выводится и сверяется по
  идентичности PathState. Формальная связь ключа с индексом `rows[x][y]` —
  **не доказана**.
- `45c8c0` пишет dword `viewport[0x30]` в поле `+0x08`, подписанное как порог;
  порог читается как float (`final_threshold` `0x45cd6a`). Подписи расходятся.
- `reserved_70` (число узлов map1) ведёт только родная RB-вставка (`45d810`).

## 8. Трассировка (`LDHOOK_BATTLE_TRACE`)

`LDHOOK_BATTLE_TRACE=1` — одна строка `BATTLE` на вызов `44bdd0` (эпилог
`0x44d074`): `tick unit code u170 al out`, затем вызов `e0a0` №1
(`c1 x1 y1 i1 fx fy lx ly`), №2 (`c2 x2 y2 i2`), узел последнего вызова
(`g h10k cost`), `trk`/`bd2` (поля `+0x30`/`+0x34`), размеры карт `s1`/`s2`
(`+0x70`/`+0x7c`), `vis` (число входов `45dd70` за тик), `m0..m2`
(pos-тройка `unit+0xa4..0xac`), `us` (QPC) и адрес `ai` (последние два не
сравниваются). Дополнительно `STATEFP` на вход `e0a0` (`H4`): `call filter`
+ `s1 s2` + `h1 h2` — FNV-1a (basis `0x811c9dc5`, prime `0x01000193`) по
in-order содержимому map1 (pos, filter, `g`, dist2, `h10000`, metric) и map2
(ключи занятости); указатели в хэш не входят. Полные поля и носители хуков —
`tools/ldhook/battle/trace.h`.
