/* battle/move.cpp — движение и выбор точки (FUN_0045eee0, PLAN_BATTLE_AI_CPP.md).
 *
 * Фаза 4: faithful-порт тела по дизасму 1.2 (все ветки, включая пост-обработку
 * результата FUN_0045e0a0 и хвост 0x45f3a5), нативные листья — по инвентарю 1.4,
 * один детур на функцию (инвариант 5). Имя функции — по смыслу, который даст
 * дизасм (п. 4.3). Поле trace-строки — решение движения и его аргументы.
 *
 * Статус: портирован sub-step 4.1a — пролог 0x45eee0..0x45ef7f (B0/B1/B2;
 * карта re/phase4_45eee0.md §8.2). B3 и далее не портированы; функция static и
 * ниоткуда не вызывается (детур и wiring — 4.2).
 */
#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "trace.h"

/* FUN_0045eee0 — тик выбора точки движения (__thiscall, ECX = BattleAI*,
 * 1 стек-аргумент rec, RET 4). Кадр: F = entry−104 (sub $0x58 + push
 * ebx/ebp/esi/edi @0x45eee0..0x45eeee); слоты F+k моделируются массивом dword
 * по паттерну LS(k) из path.cpp. rec — стек-аргумент [entry+4] = слот F+0x6c.
 *
 * 4.1a = B0 (ленивый ctx через игровой operator new), B1 (6 dword-копий
 * rec→ctx), B2 (аллокация sub-объекта ctx[0xc] + 4-dword копия из rec[0xc]).
 * Регистр-носитель нуля: EBX = 0 с @0x45eeea до B16 (перечитывается @0x45f3da). */
static void s6_tick_move(BattleAI *self, void *rec_arg)
{
    int l[28];                       /* слоты F+0x00..F+0x6c (rec — последний) */
    unsigned char *ctx;
    const unsigned char *rec;

#define LS(k) (l[(k) >> 2])

    LS(0x6c) = (int)(INT_PTR)rec_arg;   /* rec = [entry+4] = слот F+0x6c */

    /* B0 — ленивая аллокация ctx (0x1c байт) игровым operator new.
     * EBX = 0 (@0x45eeea) — носитель нуля для записей ниже. */
    if (self->game_ctx == NULL) {                        /* @0x45eee8/0x45eeec/0x45eeef */
        ctx = (unsigned char *)native::operator_new(0x1c);   /* @0x45eef1/0x45eef3 */
        if (ctx != NULL) {                               /* @0x45eefb/0x45eefd je 0x45ef0d */
            *(int *)(ctx + 0x04) = 0;                    /* @0x45eeff */
            *(int *)(ctx + 0x08) = 0;                    /* @0x45ef02 */
            *(int *)(ctx + 0x0c) = 0;                    /* @0x45ef05 */
            *(int *)(ctx + 0x10) = 0;                    /* @0x45ef08 */
        } else {
            ctx = NULL;                                  /* @0x45ef0d xor %eax,%eax */
        }
        self->game_ctx = ctx;                            /* @0x45ef0f mov %eax,(%esi) — обе ветви */
    }

    /* B1 — шесть dword-копий rec → ctx; порядок листинга:
     * +0x00, +0x14, +0x18, +0x08, +0x10, +0x04. */
    rec = (const unsigned char *)(INT_PTR)LS(0x6c);      /* @0x45ef11 mov 0x6c(%esp),%edi */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef15 */
    *(int *)(ctx + 0x00) = *(const int *)(rec + 0x00);   /* @0x45ef17/0x45ef19 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef1b */
    *(int *)(ctx + 0x14) = *(const int *)(rec + 0x14);   /* @0x45ef1d/0x45ef20 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef23 */
    *(int *)(ctx + 0x18) = *(const int *)(rec + 0x18);   /* @0x45ef25/0x45ef28 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef2b */
    *(int *)(ctx + 0x08) = *(const int *)(rec + 0x08);   /* @0x45ef2d/0x45ef30 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef33 */
    *(int *)(ctx + 0x10) = *(const int *)(rec + 0x10);   /* @0x45ef35/0x45ef38 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef3b */
    *(int *)(ctx + 0x04) = *(const int *)(rec + 0x04);   /* @0x45ef3d/0x45ef40 */

    /* B2 — sub-объект ctx[0xc] (new 0x10) + 4-dword копия из объекта rec[0xc]. */
    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef43 */
    if (*(void **)(ctx + 0x0c) == NULL) {                /* @0x45ef45/0x45ef48 jne 0x45ef64 */
        void *sub = native::operator_new(0x10);          /* @0x45ef4a/0x45ef4c */
        if (sub != NULL) {                               /* @0x45ef54/0x45ef56 je 0x45ef5d */
            *(int *)((unsigned char *)sub + 0x0c) = 0;   /* @0x45ef58 */
        }
        ctx = (unsigned char *)self->game_ctx;           /* @0x45ef5f */
        *(void **)(ctx + 0x0c) = sub;                    /* @0x45ef61 mov %eax,0xc(%ecx) */
    }
    /* Копия выполняется без проверки dst на NULL — как в оригинале
     * (new 0x10 вернул NULL → разыменование @0x45ef6e, AV). */
    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef64 */
    {
        unsigned char *dst = (unsigned char *)*(void **)(ctx + 0x0c);   /* @0x45ef66 */
        const unsigned char *src =
            (const unsigned char *)*(const void **)(rec + 0x0c);        /* @0x45ef69 */
        *(int *)(dst + 0x00) = *(const int *)(src + 0x00);  /* @0x45ef6c/0x45ef6e */
        *(int *)(dst + 0x04) = *(const int *)(src + 0x04);  /* @0x45ef70/0x45ef73 */
        *(int *)(dst + 0x08) = *(const int *)(src + 0x08);  /* @0x45ef76/0x45ef79 */
        *(int *)(dst + 0x0c) = *(const int *)(src + 0x0c);  /* @0x45ef7c/0x45ef7f */
    }

#undef LS
}
