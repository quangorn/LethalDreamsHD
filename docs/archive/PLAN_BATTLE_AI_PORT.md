# PLAN_BATTLE_AI_PORT.md — порт боевого AI + pathfinding в `.pyd`

## Контекст

`FUN_0045e0a0` (поиск цели) делает ~100 итераций × скан списка ~800 элементов =
~80K операций на поиск. 6723 поиска за 30с боя = **~537M операций** — причина
тормозов (`docs/PERF_BATTLE.md`). Cap 32 (site4) убрал лаги, но сломал агро.

Цель — заменить непрозрачный нативный код читаемым C-кодом в `ldhook.pyd`,
затем переписать на эффективную логику.

## Call graph

```
FUN_0044bdd0  (боевое AI, ~1400 строк asm, SEH, x87)  ← ВЕРХНИЙ УРОВЕНЬ
├── 0x442570  (get game state, многократно)            ✗
├── 0x446a80  (AI state update)                         ✗
├── 0x45c6f0  (get pathfinder data)                     ✗
├── FUN_0045eee0  (движение, ~120 строк)                ✗
│   └── FUN_0045e0a0  (поиск цели, ~80 строк)  ← ГОРЯЧИЙ ПУТЬ ✓
│       ├── FUN_0045ce80  (скан списка, ~40 строк)      ✓
│       │   └── 0x47f800  (advance iterator)             ✗
│       ├── FUN_0045dd70  (AI/pathfinding, ~268 строк)  ✓
│       │   ├── 0x434bc0  (tree lookup)                  ✗
│       │   ├── 0x45cb40  (path search)                  ✗
│       │   │   └── 0x45c3a0                             ✗
│       │   ├── 0x45dd00 → 0x45d810                      ✗
│       │   │   ├── 0x45d2e0                             ✗
│       │   │   └── 0x4703d0 (tree traverse)             ✗
│       │   ├── 0x45c2e0  (getter)                       ✗
│       │   ├── 0x45c690 → 0x45c5e0                      ✗
│       │   ├── 0x456ed0  (grid cell process)            ✗
│       │   ├── 0x4d17e0  (coord wrap math)              ✗
│       │   ├── FUN_0045c480 (distance²)                 ✓
│       │   └── FUN_0045c4b0 (field copy)               ✓
│       ├── FUN_0045c880 (distance predicate)            ✓
│       └── FUN_0045c5a0 (timeout)                      ✓
└── (GUI/blit updates)
```

## Фаза 0: Полная разведка ✅

### 0.1 Дизассемблировать оставшиеся функции ✅

Все функции дизассемблированы через REA (Ghidra-провайдер):
`0x47f800`, `0x45c3a0`, `0x45d2e0`, `0x4703d0`, `0x45c2e0`, `0x45c5e0`, `0x456ed0`, `0x442570`, `0x446a80`, `0x45c6f0`, `0x434bc0`, `0x45cb40`, `0x45d810`, `0x45dd00`, `0x45c690`, `0x45c390`, `0x4570d0`, `0x456a70`, `0x449160`, `0x4d17e0`, `0x45c480`, `0x45c4b0`.

Артефакты: `<scratch>/battle_ai_disasm.md`, `battle_ai_disasm2.md`, `battle_ai_structs.md`, `site6_audit.md`.

### 0.2 Определить структуры данных ✅

```c
// LDPos — объект позиции (на него ссылается LDObject.pos, [obj+0x00])
struct LDPos {
    void *field0;      // +0x00
    int   x;           // +0x0C — координата X (int32)
    int   y;           // +0x10 — координата Y (int32)
};

// LDObject (кандидат) — node->field_0x10
struct LDObject {
    LDPos *pos;        // +0x00 — указатель на LDPos (координаты)
    int   field_08;    // +0x08 — фильтр/тип (f=1)
    int   x;           // +0x0C — координата X (int32)
    int   y;           // +0x10 — координата Y (int32)
    int   field_14;    // +0x14
    int   field_18;    // +0x18 — счётчик визитов (dd70)
    int   metric;      // +0x24 — МЕТРИКА (минимум в FUN_0045ce80)
    void *field_28;    // +0x28 — container/tree pathfinding внутри объекта
};

// TreeNode (MSVC std::_Tree node, 0x18 байт)
struct TreeNode {
    TreeNode *left;    // +0x00
    TreeNode *parent;  // +0x04
    TreeNode *right;   // +0x08
    int       value0;  // +0x0C — ключ
    void     *value1;  // +0x10 — значение (= LDObject*)
    uint8_t   color;   // +0x14
    uint8_t   is_nil;  // +0x15 (0=узел,1=sentinel)
};

// Tree container (std::map)
struct TreeMap {
    char pad0[4];      // +0x00
    TreeNode *head;    // +0x04 (sentinel)
    int       size;    // +0x08
};

// PathfinderState (объект 0x58 байт)
struct PathfinderState {
    char pad0[0x28];
    int  best_max;     // +0x28 = 0xcb18967f (-1e7)
    int  best_min;     // +0x2C = 0x4b18967f (+1e7)
    char pad1[0x24];
};

// BattleAI (this)
struct BattleAI {
    void *vtable;        // +0x00
    int   field_04;      // +0x04 — время/таймаут
    LDPos *pos;          // +0x0C — позиция (косвенно)
    void *stored_target; // +0x2C — сохранённая цель (anchor)
    int   tracking;      // +0x30 — флаг трекинга (0 = выключен)
    int   distance_thr;  // +0x34 — порог дистанции (9999999)
    void *map1;          // +0x68 — map занятых ячеек (ключ 0)
    TreeMap *map2;       // +0x6C — главный std::map кандидатов
    TreeMap *map3;       // +0x74 — map занятых ячеек pathfinding
    void *pathfinder;    // +0x74 — pathfinder state
};
```

### 0.3 Логирование на границах (site6 entry hooks) ✅

site6 логирует: `first call`, `scan`, `iter`, `ai_update done`, `rescan`, `code=`, `CAP`.

## Фаза 1: Порт горячего пути (target search) ✅

site6 — полная замена (detour) `FUN_0045e0a0` (E9 на пролог `0x45e0a0`, 7 Б).
`LDHOOK_SITE6=1` → агро работает, поведение идентично baseline.

Реализовано: `s6_target_search`, `s6_scan_map` (порт `FUN_0045ce80`), `s6_entry_hook` (трамплин).

## Фаза 2: Порт AI/pathfinding (FUN_0045dd70) ✅

`s6_ai_update` — порт `FUN_0045dd70`: slot_register + pfs_ctor + path_search + position_update + map_update_neighbors.

Реализовано: `s6_map_lower_bound` (0x434bc0), `s6_pfs_ctor` (0x45c2e0), `s6_slot_register` (0x45dd00), `s6_grid_cell` (0x456ed0), `s6_path_search` (0x45cb40), `s6_path_tree_search` (0x45c3a0), `s6_position_update` (no-op).

## Фаза 3: Порт верхнего уровня (FUN_0044bdd0) ❌

~1400 строк asm. SEH, x87. Не портирован.

## Фаза 4: Оптимизация ❌

Не начата.

## Текущее состояние (19.11.2026)

site6 **работает** (не крашится), но **неэффективно**:
- 114 вызовов `FUN_0045e0a0` за бой.
- До 1710 итераций на вызов (scan O(n) на каждую итерацию).
- Тормоза сохраняются (CPU ~100% одного ядра).
- Агро работает частично (scan возвращает разных кандидатов, но медленно).
- Проверено в бою контрольным прогоном `tools/probes/battle_test.ps1` (принятые дефолты): портированные
  методы вызываются из C (`site6: call/iter/ai_update/rescan`), снимки боя/призыва сходятся; грабля —
  `docs/PITFALLS.md`, «Ротация лога затирает контрольные строки».

### Что исправлено (3 критических дефекта из аудита):

1. **Дефект A (трамплин):** `pop eax` → `add esp, 4` — трамплин возвращает результат `s6_target_search`, не stale eax.
2. **Дефект B (штраф кандидата):** штраф 0x7ffffff0 убран — кандидат получает dist² до цели (как соседи).
3. **Дефект C (чужой аллокатор):** `s6_slot_register` отключён — не вставляет узлы в родные std::map игры (не corrupt'ит кучу).

## План дальнейших улучшений

### Шаг 1: Оптимизация scan (убрать O(n) на итерацию)

**Проблема:** `s6_scan_map` делает полный in-order обход std::map `this+0x6c` (~800 элементов) на каждую итерацию. 1710 итераций × 800 = ~1.4M операций на вызов.

**Решение:** заменить `FUN_0045ce80` (scan) на C с кешированием:
- Один проход по std::map → сортировка кандидатов по метрике → инкрементальное обновление (O(n) вместо O(итерации × n)).
- Или — заменить `FUN_0045e0a0` на C с эффективным алгоритмом: один проход по std::map, min по метрике, без внешнего цикла.

**Приоритет:** высокий (убрать тормоза).

### Шаг 2: Улучшить s6_ai_update (быстрее обновлять метрики)

**Проблема:** `s6_map_update_neighbors` обновляет метрики соседей (dist² < 400), но scan всё равно выбирает того же кандидата (min). Нужен более агрессивный bump соседей.

**Решение:**
- Увеличить радиус соседей (S6_NEIGHBOR_D2 = 400 → 10000, т.е. 100×100 клеток).
- Или — обновлять метрики всех кандидатов в this+0x6c (не только соседей).
- Или — bump'ить метрику кандидата до значения, которое больше текущего min, но меньше 999999999.

**Приоритет:** средний (улучшить агро).

### Шаг 3: Порт FUN_0044bdd0 (верхний уровень)

**Проблема:** `FUN_0044bdd0` (~1400 строк asm, SEH, x87) — верхний уровень боевого AI. Не портирован.

**Решение:** портировать на C++. Это даст полный контроль над боевым AI и уберёт тормоза.

**Приоритет:** низкий (сложно, но даст максимальный эффект).

### Шаг 4: A/B с baseline

**Проблема:** site6 работает, но нужно сравнить поведение с baseline (без site6) — агро, монстры, боевая сцена.

**Решение:** запустить игру с `LDHOOK_SITE6=0` (baseline) и с `LDHOOK_SITE6=1` (site6) на том же сейве, сравнить скриншоты и поведение.

**Приоритет:** высокий (валидация).

### Шаг 5: Замер CPU

**Проблема:** site6 работает, но CPU ~100% одного ядра. Нужно замерить CPU в бою.

**Решение:** запустить `perf_cpuload.ps1` или `perf_sampler3.exe` в бою с site6 и без.

**Приоритет:** высокий (валидация).

## Риски

1. `FUN_0045dd70` модифицирует метрики не напрямую — механизм в 0x45c3a0 или 0x4703d0.
2. Рекурсивные вызовы (FUN_0045dd70 → сама себя, FUN_0044bdd0 → FUN_0045eee0).
3. Структура BattleAI (this) — нужно полностью понять.
4. Pathfinding grid — 2D-сетка по offset 0x34, формат неизвестен.
5. Метрики `[obj+0x24]` — семантика неизвестна (distance? threat? priority?).

## Артефакты

- Дизассемблинги: `<scratch>/battle_ai_disasm.md`, `battle_ai_disasm2.md`, `battle_ai_structs.md`, `site6_audit.md`
- ldhook.cpp: `tools/ldhook/ldhook.cpp` (site1-6 реализованы)
- Scratch-дерево: `<scratch>/ldhook/tree/` (baseline EXE + ldhook.pyd)
- Сейвы: `<user>/Documents/Boolat Games/Lethal Dreams/Saves/` (SLOT1-3)