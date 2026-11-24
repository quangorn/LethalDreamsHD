/* battle/grid.cpp — доступ к сетке pathfinding'а: faithful-порты FUN_00456ed0,
 * FUN_004570d0, FUN_00456a70 (PLAN_BATTLE_AI_CPP.md, п. 2.1) и FUN_00457e20
 * (поиск ячейки по ключу, re/leaf_457e20.md).
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
#include <string.h>
#include <windows.h>

#include "../ldhook_internal.h"
#include "battle.h"
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
 * НЕ ВЫЗЫВАН: единственный вызывающий — 0x458050 (grid_anchor, call-site
 * 0x45811a), он ещё нативен. Порт 0x458050 вызовет s6_grid_key_lookup из своего тела
 * (сайт 0x45811a: this = тот же grid root, arg1 = float из кадра, arg2 = EDX,
 * arg3 = float, arg4 = EBP, результат читается только как EAX). В живом билде
 * до этого момента функция не вызывается. */
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
