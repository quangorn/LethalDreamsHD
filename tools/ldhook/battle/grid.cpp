/* battle/grid.cpp — доступ к сетке pathfinding'а: faithful-порты FUN_00456ed0,
 * FUN_004570d0, FUN_00456a70 (PLAN_BATTLE_AI_CPP.md, п. 2.1), FUN_00457e20
 * (поиск ячейки по ключу, re/leaf_457e20.md), FUN_00456b60 (голова цепочки,
 * re/leaf_456b60.md) и FUN_00458050 (grid-якорь, re/leaf_458050.md).
 * Дизасм — артефакт разведки re/battle_ai_disasm3.md п. 1.1.13 (Ghidra на
 * baseline LETHALDREAMS.EXE.pre4, imagebase 0x400000).
 *
 * Решения по артефакту:
 * - FUN_004570d0 — только ПОЛУЧАЕТ узел сетки: она зовёт исключительно
 *   FUN_00456ed0 и не вызывает allocation-функций. Создание узла — в
 *   FUN_0045dd70 (CALL 0x45c2e0 @0x45df2c), а не здесь.
 * - Receiver FUN_00456ed0 = тот же grid root, что у FUN_004570d0 (ECX не
 *   меняется: 0x4570eb задаёт ESI, CALL @0x4570f5; в FUN_0045dd70 ECX =
 *   [ctx+0x08] с 0x45dee4).
 * - Тела трёх функций (35/52/14 байт) без SEH-фреймов: единственный ret-путь,
 *   разыменования оригинала не guarded. Порты без __try/__except — guard
 *   добавил бы поведение, которого в оригинале нет.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "ported.h"

#ifdef BATTLE_REPLAY
extern "C" int g_replay_override;
extern "C" void *replay_grid_cell(int x, int y);
#endif

/* FUN_00456ed0 (0x456ed0..0x456ef2, 35 байт) — ячейка сетки по индексу.
 * ABI: int __thiscall(GridLevel *level, int idx), RET 4; ECX = level.
 *   0x456ed4/0x456ed6: idx < 0 (знаковой JL) → 0 (0x456ee3: XOR EAX,EAX);
 *   0x456ed8..0x456ee1: спуск по [level+0x1c], пока следующий уровень не NULL;
 *   0x456ee8..0x456eee: leaf->[0x34] + idx*0x34. Проверки cells == NULL в
 *   оригинале нет: при пустом листе возвращается cells + idx*0x34.
 * Возврат: GridCell*, при idx < 0 — NULL. */
GridCell *s6_grid_cell(GridLevel *level, int index)
{
    GridLevel *leaf = level;

    if (index < 0) {
        return NULL;   /* 0x456ed6 JL → 0x456ee3 */
    }
    while (leaf->next_level != NULL) {   /* 0x456ed8..0x456ee1 */
        leaf = leaf->next_level;
    }
    return (GridCell *)((unsigned char *)leaf->cells +
                        index * S6_GRID_CELL_SIZE);   /* 0x456eeb IMUL, 0x456eee ADD */
}

/* FUN_004570d0 (0x4570d0..0x457103, 52 байта) — getter узла сетки по (x, y).
 * ABI: int __thiscall(GridLevel *grid, int x, int y), RET 8; ECX = grid.
 * Порядок проверок (знаковые JGE/JL, размеры [grid+0x2c]/[grid+0x30] — int):
 *   0x4570d4/0x4570d8: x >= size_x → 0;
 *   0x4570de/0x4570e1: y >= size_y → 0;
 *   0x4570e3/0x4570e5: x < 0 → 0; 0x4570e7/0x4570e9: y < 0 → 0;
 *   0x4570eb..0x4570f1: index = [grid+0x38][x][y] (массив рядов, row[x][y]);
 *   0x4570f4/0x4570f5: CALL 0x456ed0 с ECX = grid.
 * Узел не создаётся (см. решение в шапке). */
GridCell *s6_grid_lookup(GridLevel *grid, int x, int y)
{
    int *row;
    int index;

#ifdef BATTLE_REPLAY
    if (g_replay_override) {
        return (GridCell *)replay_grid_cell(x, y);
    }
#endif
    if (x >= grid->size_x) {   /* 0x4570d8 JGE */
        return NULL;
    }
    if (y >= grid->size_y) {   /* 0x4570e1 JGE */
        return NULL;
    }
    if (x < 0) {               /* 0x4570e5 JL */
        return NULL;
    }
    if (y < 0) {               /* 0x4570e9 JL */
        return NULL;
    }

    row = grid->rows[x];       /* 0x4570ee: [grid+0x38] + x*4 */
    index = row[y];            /* 0x4570f1: [row + y*4] */
    return s6_grid_cell(grid, index);
}

/* FUN_00456a70 (0x456a70..0x456a7d, 14 байт) — последний узел цепочки +0x28.
 * ABI: int __fastcall(node), ECX = node; только RET (ret n нет).
 *   0x456a72..0x456a7b: edx = [eax+0x28]; если NULL → возврат eax,
 *   иначе eax = edx и повтор.
 * +0x28 — link цепочки обхода (потребитель FUN_0045c3a0 @0x45c3d0), не левый сын
 * RB-дерева; поле GridCell.chain_next именовано пунктом 2.3. */
GridCell *s6_chain_last(GridCell *node)
{
    GridCell *cur = node;

    for (;;) {
        if (cur->chain_next == NULL) {
            return cur;   /* 0x456a77 JZ → 0x456a7d */
        }
        cur = cur->chain_next;   /* 0x456a79/0x456a7b */
    }
}

/* FUN_00456b20 (0x456b20..0x456b50, 49 байт) — масштабная запись позиции уровня
 * сетки (п. 2.5, вердикт «порт» — инвентарь 1.4). Оригинал: `int __thiscall
 * (node, float *out)` RET 4. Receiver = LDPos* (FUN_0045c5e0 @0x45c61b): уровень
 * берётся из [pos+0x1c] как лист, БЕЗ спуска по цепочке +0x1c.
 *   0x456b21: EAX=[ECX+0x1c] — лист; 0x456b24: FLDS [EAX+0x24] — ST2-операнд
 *   второй записи; 0x456b2a: FLDS [EAX+0x28] — ST1-операнд (scale_mul);
 *   0x456b27: EDX=[ECX+0x10] → slot ESP+0 (FILD-операнд @0x456b40, int-значение).
 *   0x456b31..0x456b3e: FILD [ECX+0xc]; FMUL [EAX+0x28]; FADD [EAX+0x20];
 *   FSTP [EAX] → out[0] = float (x*mul + add).
 *   0x456b40..0x456b48: FILD slot(y); FMUL ST1; FADD ST0,ST2 (ST2 = [EAX+0x24]);
 *   FSTP [EAX+4] → out[1] = float (y*mul + scale_load).
 * EAX на выходе = out и читается вызывающим (@0x45c62d/0x45c668) — порт
 * возвращает out; порядок операций соблюдён (plain double, PC=53 — п. 1.6).
 * SEH у оригинала нет: разыменования идут под __try вызывающего. */
float *s6_grid_scale_compute(LDPos *pos, float *out)
{
    GridLevel *level = pos->level;
    double mul, add, load, v;

    load = level->scale_load;   /* FLDS [EAX+0x24] @0x456b24 — ST2 @0x456b46 */
    mul = level->scale_mul;     /* FLDS [EAX+0x28] @0x456b2a; FMUL @0x456b34; ST1 @0x456b44 */
    add = level->scale_add;     /* FADD-операнд @0x456b37 (ST2-копия не участвует) */

    v = (double)pos->x * mul;   /* FILD [ECX+0xc] → FMUL @0x456b31..0x456b34 */
    v = v + add;                /* FADD [EAX+0x20] @0x456b37 */
    out[0] = (float)v;          /* FSTP [EAX] @0x456b3e — float, не int */

    v = (double)pos->y * mul;   /* FILD slot @0x456b40 (значение [ECX+0x10] @0x456b27) */
    v = v + load;               /* FADD ST0,ST2 @0x456b46 — scale_load, НЕ scale_add */
    out[1] = (float)v;          /* FSTP [EAX+4] @0x456b48 */
    return out;
}

/* --- FUN_00457e20 (0x457e20..0x458042, 547 байт, 181 инстр., 44 блока) --- *
 * Поиск ячейки сетки по ключу. ABI (байты + Ghidra `native_api`, RET 0x10):
 *   GridCell *__thiscall(GridLevel *grid [ECX], float arg1, undefined4 arg2,
 *                        float arg3, int key)
 * EAX = GridCell* или NULL; arg2 (@S+8) не читается ни разу. SEH нет,
 * аллокаций нет, записей в игровые объекты нет (только кадр-слоты) — порт без
 * __try/__except, как и оригинал. Листинг 1:1 (Ghidra ∩ capstone) —
 * re/leaf_457e20.md §3.
 *
 * Поток: x0/y0 = trunc((arg − смещение) * r1) с r1 = 1.0f/scale_mul; гейты
 * `int < (float)size` (узлы 1/2); idx = rows[x0][y0], idx < 0 → NULL; ячейка =
 * лист? cells + idx*0x34 : s6_grid_cell(next_level, idx); chain_next →
 * s6_chain_last; path A — обход цепочки +0x24 с поиском value ([rec+0x18],
 * цепочка +0x1c) == key, находка → текущая ячейка цепочки; иначе 3×3-скан
 * {x0−1..x0+1}×{y0−1..y0+1} (границы: `<0` и `< (float)size`) по цепочкам
 * соседей, и его «находка» возвращает слот [ESP+0x10].
 *
 * y-индекс: артефакт утверждал «0x4d17e0 делает FINIT ⇒ ST1 пуст ⇒ p2 = NaN ⇒
 * y_idx ≡ 0, arg3 мёртв». Проверка по листингу и исполнением это ОПРОВЕРГАЕТ:
 *   - байты 0x4d17e0: `55 8bec 83ec20 83e4f0 d9 c0 d9542418 df7c2410 …` —
 *     @0x4d17e9 стоит `d9 c0` = FLD ST(0); FINIT (9B D9 E0 / DB E3) в 117
 *     байтах отсутствует;
 *   - это штатный CRT __ftol2 (FISTP qword → FILD qword → FSUBP → поправки):
 *     он снимает со стека ТОЛЬКО ST0 (свой вход), ST1 и ниже сохраняет;
 *   - замер реальных байтов при CW = 0x027F (scratch-проба, 8 кейсов):
 *     регистр на верху стека после call равен ровно 1.0f/scale_mul, поэтому
 *     y0 = trunc((arg3 − scale_load) * r1), и в кейсе артефакта (a3=250,
 *     sload=5, smul=10) выходит y0 = 24, а не 0 (в его же файле вывода
 *     head_out3.txt записано y0=24). Значит arg3 — живой вход, r1 общий для
 *     обоих произведений, и порт это воспроизводит.
 *
 * Возврат 3×3-скана — ВСЕГДА NULL (а не «оригинал», как в §8 артефакта): слот
 * [ESP+0x10] пишется 0x45800d на каждом шаге path A (в т.ч. когда next == 0);
 * в 3×3 скан попадают только с пустым слотом (0x458013 — после исчерпания
 * цепочки; 0x457ee8 — после chain_last == 0, а его результат уже лежал в слоте
 * @0x457ee0). Скан оставлен как есть: он читает память (соседние rows/цепочки)
 * и потому наблюдаем ловушкой, но значение возврата не меняет.
 *
 * НЕ ВЫЗЫВАН на пяти старых захватах: единственный вызывающий — 0x458050
 * (grid_anchor, call-site 0x45811a), и он вызывает этот лист только в ветке
 * tree+key (node != 0 и key найден). Порт 0x458050 стоит в tick.cpp; сам лист
 * уходит в харнесс только из ветки `s6_grid_anchor_lookup` (см. ниже). */
static int b0_fbits(float f)
{
    int bits;

    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/* 0x4d17e0 — CRT double → int64 (вызывается дважды: 0x457e3f, 0x457e53).
 * Усечение К НУЛЮ (FISTP qword), а не к −∞: (long long) даёт то же самое.
 * Вне int64, а также NaN/inf — int64-indefinite 0x8000000000000000 (замер:
 * 2^63, −2^63, 1e19, ±inf, NaN); вызывающий читает только EAX, т.е. младшие
 * 32 бита (для indefinite это 0). Хелпер нужен явно: round/floor дал бы
 * расхождение на отрицательных индексах. */
static int b0_ftoll(double v)
{
    long long n;

    if (v >= -9223372036854775808.0 && v < 9223372036854775808.0) {
        n = (long long)v;                       /* trunc toward zero */
    } else {
        n = -9223372036854775807LL - 1;         /* 0x8000000000000000 */
    }
    return (int)(unsigned int)(unsigned long long)n;   /* EAX */
}

/* Значение записи по геттеру 0x456e70 (`mov eax,[ecx+0x18]; ret`) с обходом
 * цепочки [rec+0x1c]: первый ненулевой value, иначе 0 (@0x457f94/0x458000
 * `xor eax,eax`). Повторные вызовы геттера (0x458018/0x45802b) — no-op. */
static int b0_key_value(KeyRecord *rec)
{
    while (rec != NULL) {
        if (rec->value != 0) {                  /* 0x457f85/0x457ff5 jne */
            return rec->value;
        }
        rec = rec->next;                        /* 0x457f8d/0x457ff9 */
    }
    return 0;
}

GridCell *s6_grid_key_lookup(GridLevel *grid, float arg1, int arg2, float arg3, int key)
{
    double r1;
    float lim_x, lim_y;
    int x0, y0, idx, args[4];
    GridCell *cell, *slot, *res;

    (void)arg2;                                 /* @S+8: не читается */

    /* 0x457e29 `fld [this+0x28]` + 0x457e30 `fdivr [0x4ec380]` (=1.0f):
     * r1 = 1.0f/scale_mul. Оба FSUB/FMUL идут при PC=53, т.е. double. */
    r1 = 1.0 / (double)grid->scale_mul;

    /* 0x457e36..0x457e3d: p1 = (arg1 − scale_add) * r1; 0x457e3f call 0x4d17e0. */
    x0 = b0_ftoll(((double)arg1 - (double)grid->scale_add) * r1);

    /* 0x457e44..0x457e51: p2 = (arg3 − scale_load) * r1 (тот же ST1 = r1);
     * 0x457e53 call 0x4d17e0. 0x457e58 `fstp st(0)` снимает r1. */
    y0 = b0_ftoll(((double)arg3 - (double)grid->scale_load) * r1);

    /* Узлы 1/2: FILD int → FCOMP float → TEST AH,1 → JZ, т.е. «продолжаем» ⟺
     * int строго меньше (float)size. Сравнение в double: FILD даёт точное
     * целое, lim — float; int→float потерял бы биты при |x| > 2^24. */
    lim_x = (float)grid->size_x;                /* 0x457e5a FILD, 0x457e63 FSTP */
    if (!((double)x0 < (double)lim_x)) {        /* 0x457e74 je 0x457fd7 */
        res = NULL;
        goto done;
    }
    lim_y = (float)grid->size_y;                /* 0x457e7a..0x457e7d */
    if (!((double)y0 < (double)lim_y)) {        /* 0x457e8e je 0x457fd7 */
        res = NULL;
        goto done;
    }

    /* 0x457e94..0x457e9e: idx = rows[x0][y0]; 0x457ea3 JL → NULL. Знак x0 не
     * проверяется (как в оригинале: rows[x0] читается даже при x0 < 0). */
    idx = grid->rows[x0][y0];
    if (idx < 0) {
        res = NULL;
        goto done;
    }

    /* 0x457ea9..0x457ec6: next_level != 0 → s6_grid_cell (ECX = next_level),
     * иначе прямой лист cells + idx*0x34. */
    if (grid->next_level != NULL) {
        cell = s6_grid_cell(grid->next_level, idx);     /* 0x457ec1 */
    } else {
        cell = (GridCell *)((unsigned char *)grid->cells +
                            idx * S6_GRID_CELL_SIZE);   /* 0x457eb3/0x457eb6 */
    }
    if (cell == NULL) {                         /* 0x457ece je 0x457fd7 */
        res = NULL;
        goto done;
    }

    /* 0x457ed4..0x457ee0: chain_next != 0 → последний узел цепочки +0x28. */
    if (cell->chain_next != NULL) {
        cell = s6_chain_last(cell->chain_next);         /* 0x457edb */
    }
    slot = cell;                                /* [ESP+0x10]: 0x457eba/…/0x457ee0 */

    /* path A (0x457fe3..0x458013): обход цепочки +0x24; value из key_rec. */
    if (cell != NULL) {                         /* 0x457ee8 jne 0x457fe7 */
        GridCell *c;

        for (c = slot; c != NULL; c = c->next) {
            if (b0_key_value(c->key_rec) == key) {
                res = c;                        /* 0x45801f EAX = EDX: ячейка */
                goto done;
            }
            slot = c->next;                     /* 0x45800d (в т.ч. 0 при конце) */
        }
    }

    /* 3×3-скан (0x457eee..0x457fd6): сюда попадают только с slot == 0
     * (0x457ee8 — chain_last вернул 0; 0x458013 — цепочка исчерпана), поэтому
     * «находка» @0x458035 возвращает 0. Границы те же узлы 1/2, счётчики —
     * ровно 3 шага (k/−1 ≤ 1 и `jl` по j), адресация rows[i][j] со знаковым
     * сдвигом. Значение возврата не меняется (slot == NULL), чтения — как в
     * оригинале. */
    {
        unsigned ui, uj;

        for (ui = 0; ui < 3; ui++) {
            int i = (int)((unsigned)x0 - 1u + ui);   /* 0x457eee lea [esi-1] */

            if (i < 0) continue;                     /* 0x457f0a jl 0x457fb9 */
            if (!((double)i < (double)lim_x)) continue;   /* 0x457f1d je */
            for (uj = 0; uj < 3; uj++) {
                int j = (int)((unsigned)y0 - 1u + uj);    /* 0x457f2a dec ebx */
                GridCell *c2;

                if (j < 0) continue;                 /* 0x457f38 jl 0x457fa7 */
                if (!((double)j < (double)lim_y)) continue;   /* 0x457f47 je */
                /* 0x457f4d..0x457f58: ECX = this (слот [ESP+0x30]), не next_level. */
                c2 = s6_grid_cell(grid, grid->rows[i][j]);
                if (c2 == NULL) continue;            /* 0x457f61 je */
                if (c2->chain_next != NULL) {
                    c2 = s6_chain_last(c2->chain_next);   /* 0x457f6a */
                }
                if (c2 == NULL) continue;            /* 0x457f73 je */
                for (; c2 != NULL; c2 = c2->next) {  /* 0x457fa0/+0x24 */
                    if (b0_key_value(c2->key_rec) == key) {
                        res = slot;                  /* 0x458035: [ESP+0x10] == 0 */
                        goto done;
                    }
                }
            }
        }
    }
    res = NULL;                                      /* 0x457fd7 xor eax,eax */

done:
    /* Claim формы сайта 0x45811a (leaf_sites/ABI — при порте 0x458050):
     * ECX = grid, args = [arg1 float-биты, arg2, arg3 float-биты, key]. */
    args[0] = b0_fbits(arg1);
    args[1] = arg2;
    args[2] = b0_fbits(arg3);
    args[3] = key;
    B0_LEAF_CLAIM(B0_LEAF_GRID_KEY_LOOKUP, grid, res, args, 4);
    return res;
}

/* FUN_00456b60 (0x456b60..0x456bc9, 106 Б, 44 инстр., SEH нет; REA, артефакт
 * re/leaf_456b60.md) — голова цепочки со статусом 2. `__thiscall`: ECX =
 * GridCell* (node), 1 стек-аргумент = world, оба выхода `RET 4`; EAX =
 * GridCell* или 0.
 *   0x456b63..0x456b70  key = [[node+0x20]]; status = tree_lookup(world, key);
 *                       `TEST; JZ` — status == 0 → выход A (EAX = node);
 *   0x456b80  (голова)  key = [[node+0x20]]; status = tree_lookup(world, key);
 *                       `CMP 1; JZ` — status == 1 → выход A;
 *   0x456b92            тот же key; `CMP 2; JNZ` — status != 2 → выход A;
 *   0x456ba4            node = [node+0x28] (chain_next); `TEST; JZ` → выход B
 *                       (EAX = 0);
 *   0x456bab            key нового узла; status = tree_lookup(world, key);
 *                       `TEST; JNZ` — status != 0 → голова цикла; status == 0
 *                       → выход A (EAX = новый node).
 * Эквивалент: выход A (вернуть текущий узел) достижим при любом статусе != 2,
 * поэтому спуск сокращается до `status != 2 ? node : node->chain_next`, пока
 * цепочка не оборвётся. Натив пересчитывает статус одного узла до 3 раз
 * (0x456b88/0x456b9a/0x456bb3), но 0x449160 только читает дерево (world+0x104)
 * — при неизменном дереве ответы идентичны, один lookup эквивалентен (число
 * вызовов 0x449160 ни один слой не записывает).
 * Память: только чтения `[node+0x20]`, `[[node+0x20]]`, `[node+0x28]`; записей
 * и аллокаций нет. `key_rec == NULL` натив НЕ проверяет — guard не добавлен.
 * Claim: ECX — ВХОДНОЙ node (получатель сайта 0x458142), arg — world, ret —
 * указатель (cur); форма совпадает с `k_leaf_abi` (nargs = 1, ptr, ret_ptr).
 * Вызывается из `s6_grid_anchor_lookup` (сайт 0x458142) и только из него. */
GridCell *s6_grid_chain_head(GridCell *node, void *world)
{
    GridCell *cur = node;

    for (;;) {
        /* 0x456b63..0x456b70 / 0x456b80..0x456b88 / 0x456bab..0x456bb3 */
        int st = native::tree_lookup(world, (unsigned int)cur->key_rec->key);

        if (st != 2) {
            break;                      /* 0x456b77/0x456b90/0x456ba2: EAX = node */
        }
        cur = cur->chain_next;          /* 0x456ba4 */
        if (cur == NULL) {
            break;                      /* 0x456ba9: выход B, EAX = 0 */
        }
    }
    {
        int arg[1];

        arg[0] = (int)(size_t)world;    /* единственный стек-аргумент листа */
        B0_LEAF_CLAIM(B0_LEAF_CHAIN_HEAD, node, cur, arg, 1);
    }
    return cur;                         /* 0x456bbd/0x456bc4: EAX = ESI/0 */
}

/* --- FUN_00458050 (0x458050..0x4581f6, 423 Б, 158 инстр., 21 блок, 13 условных
 * переходов, 5 выходов, SEH нет, аллокаций нет) — grid-якорь.
 * Артефакт re/leaf_458050.md; ABI ниже сверен по байтам И по записям захвата
 * (в артефакте §2/§4 нумерация слотов S+0x10..S+0x18 сдвинута на один: у него
 * `arg3` = node, `arg4` = out-flag, `arg5` = мёртвый. По арифметике ESP =
 * E−0x18: [esp+0x28]=E+0x10, [esp+0x2c]=E+0x14, [esp+0x30]=E+0x18; запись
 * `"va":"458050"` в захватах подтверждает: args[2] — float (0x42A80000 = 84.0),
 * args[3] — указатель узла (0 или куча), args[4] — адрес в стеке вызывающего
 * (&байт-флаг), args[5] — unit. `[esp+0x30]` у 0x4580e3 и 0x45812e = E+0x18 =
 * arg6 (world): вложенный `call 0x457e20` — `ret 0x10`, ESP восстанавливается,
 * поэтому arg2 (float) tree-контекстом не становится ни у одного из двух слотов.
 * `RET 0x18`, EAX = GridCell* или 0:
 *
 *   GridCell *__thiscall f(GridLevel *grid [ECX],
 *                          float x        [S+4  ] — координата x-индекса
 *                                          (0x45805f `fld [esp+0x1c]`),
 *                          float y_metric [S+8  ] — ТОЛЬКО |min_metric − arg|
 *                                          в скане node == 0 (0x4581b9
 *                                          `fsub [esp+0x20]`) и мёртвый
 *                                          block[4] 0x457e20 (тот его не читает),
 *                          float y_coord  [S+0xc] — координата y-индекса
 *                                          (0x45807c `fld [esp+0x24]` при
 *                                          ESP = E−0x18) и ЖИВОЙ block[8]
 *                                          0x457e20 (по нему тот считает свой y0),
 *                          void *node     [S+0x10] — узел дерева (может быть 0),
 *                          uchar *flag    [S+0x14] — out-байт,
 *                          void *world    [S+0x18] — ECX для 0x449160)
 *
 * x87: `FDIV` (не умножение на 1/scale, как в 0x457e20) — обе координаты
 * считаются истинным делением по трём живым push'ам, поэтому индексы реальны.
 * Узлы сравнений (PC=53, double):
 *   x-гейт 0x4580a3..0x4580b1: FILD xi / FILD size_x / FCOMPP / `TEST AH,0x41;
 *     JNP` — выход ⟺ JNP взят ⟺ size_x ≤ xi; скан продолжается ⟺ JNP не взят ⟺
 *     size_x > xi (а = ST(0) = size_x; JNP ⟺ NOT(a>b)). Операнды —
 *     точные целые (FILD), поэтому unordered недостижим; граница сравнивается
 *     с ЦЕЛЫМ size (не с (float)size, как в 0x457e20, где лимит кладётся FSTP
 *     m32). Так же y-гейт 0x4580b7..0x4580c5.
 *   min-гейт 0x4581b6..0x4581cc: dist = fabs(min_metric − y_metric) в double, `FCOMPP
 *     dist,best` + `TEST AH,0x5; JP` — JP ⟺ NOT(dist < best) → пропуск
 *     (unordered тоже пропуск); обновление пишет best как float (`FSTP m32`),
 *     поэтому аккумулятор округляется на каждом шаге. Базис правила — таблица
 *     probe8 (re/battle_ai_x87.md §1): a<b → AH=0x01 → PF=0 → JP не взят;
 *     «C0 == C2» из FCOMP паритет не задаёт.
 * Переиспользование слота (моделируется только видимым эффектом): в ветке
 * node == 0 флаг НЕ пишется (0x4580d1 je 0x45817f), а слот S+0x14 занят
 * float-аккумулятором best (10000.0f @0x458196, затем FSTP @0x4581ce). Слоты
 * вызывающего после CALL не читаются ни у одного из трёх callers, поэтому
 * аккумулятор в порте — локальная переменная; наблюдаемы только записи
 * byte [flag] = 1 (0x4580fd) и = 0 (0x458161). */
GridCell *s6_grid_anchor_lookup(GridLevel *grid, float x, float y_metric,
                                float y_coord, void *node, unsigned char *flag,
                                void *world)
{
    const int *map;                                 /* [node+0x238]: первый dword = key */
    int xi, yi, idx;
    GridCell *cell, *anchor, *res;
    unsigned char *p = (unsigned char *)node;

    /* 0x458058..0x458070 / 0x458072..0x458091: trunc((coord − scale)/mul).
     * x-индекс считается от arg1, y-индекс — от arg3 (y_coord): `0x45807c fld
     * [esp+0x24]` при ESP = E−0x18. arg2 (y_metric) в индексы не входит. */
    xi = b0_ftoll(((double)x - (double)grid->scale_add) /
                  (double)grid->scale_mul);
    yi = b0_ftoll(((double)y_coord - (double)grid->scale_load) /
                  (double)grid->scale_mul);

    if (xi < 0) {                                   /* 0x458095 jl */
        res = NULL;
        goto claim;
    }
    if (yi < 0) {                                   /* 0x45809d jl */
        res = NULL;
        goto claim;
    }
    /* Узел 1 (0x4580a3..0x4580b1) / узел 2 (0x4580b7..0x4580c5). */
    if (!((double)xi < (double)grid->size_x)) {
        res = NULL;
        goto claim;
    }
    if (!((double)yi < (double)grid->size_y)) {
        res = NULL;
        goto claim;
    }

    if (node == NULL) {                             /* 0x4580cf/0x4580d1 je */
        /* 0x45817f..0x4581e8: flag не пишется; лучшая ячейка цепочки +0x24 по
         * минимуму |min_metric − y_metric| среди тех, чей ключ даёт статус 1. */
        float best = 10000.0f;                      /* 0x458196 */
        GridCell *bestc;

        idx = grid->rows[xi][yi];                   /* 0x45817f..0x458185 */
        cell = s6_grid_cell(grid, idx);             /* 0x45818b */
        bestc = cell;                               /* 0x458194 */
        if (cell == NULL) {                         /* 0x45819e je 0x4581df */
            res = bestc;
            goto claim;
        }
        for (cell = cell; cell != NULL; cell = cell->next) {   /* +0x24 @0x4581d8 */
            if (native::tree_lookup(world,
                    (unsigned int)cell->key_rec->key) == 1) {  /* 0x4581ac */
                double dist = fabs((double)cell->min_metric - (double)y_metric);

                if (dist < (double)best) {          /* 0x4581c5..0x4581cc */
                    best = (float)dist;             /* 0x4581ce FSTP m32 */
                    bestc = cell;                   /* 0x4581d2 */
                }
            }
        }
        res = bestc;
        goto claim;
    }

    map = *(const int **)(p + 0x238);               /* 0x4580d7 */
    if (map == NULL) {                              /* 0x4580df je */
        *flag = 0;                                  /* 0x458161 */
        idx = grid->rows[xi][yi];                   /* 0x458164..0x45816a */
        res = s6_grid_cell(grid, idx);              /* 0x458170 */
        goto claim;
    }
    if (native::tree_lookup(world, (unsigned int)*map) == 0) {   /* 0x4580e1..0x4580ef */
        *flag = 0;                                  /* 0x458161 */
        idx = grid->rows[xi][yi];
        res = s6_grid_cell(grid, idx);              /* 0x458170 */
        goto claim;
    }
    *flag = 1;                                      /* 0x4580fd */

    /* 0x4580f1..0x458117: block = {x, y_metric (как dword — 0x457e20 block[4]
     * не читает), y_coord (block[8] — ЖИВОЙ вход 0x457e20: его y0), node-as-key}. */
    anchor = s6_grid_key_lookup(grid, x, b0_fbits(y_metric), y_coord,
                                (int)(size_t)node);
    if (anchor == NULL) {                           /* 0x458123 je */
        res = NULL;
        goto claim;
    }
    if (native::tree_lookup(world,
            (unsigned int)anchor->key_rec->key) == 2) {   /* 0x458135..0x45813d */
        res = s6_grid_chain_head(anchor, world);    /* 0x458142 */
        goto claim;
    }
    res = anchor;                                   /* 0x458151 */

claim:
    /* v16: тело порта заявляет значение (ecx = grid, ret = узел, 6 аргументов
     * ABI сайта 0x44c7da). В живом билде B0_LEAF_CLAIM — no-op. */
    {
        int av[6];

        av[0] = b0_fbits(x);
        av[1] = b0_fbits(y_metric);
        av[2] = b0_fbits(y_coord);
        av[3] = (int)(size_t)node;
        av[4] = (int)(size_t)flag;
        av[5] = (int)(size_t)world;
        B0_LEAF_CLAIM(B0_LEAF_GRID_ANCHOR, grid, res, av, 6);
    }
    return res;
}
