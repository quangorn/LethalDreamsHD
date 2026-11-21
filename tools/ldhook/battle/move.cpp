/* battle/move.cpp — движение и выбор точки (FUN_0045eee0, PLAN_BATTLE_AI_CPP.md).
 *
 * Фаза 4: faithful-порт тела по дизасму 1.2 (все ветки, включая пост-обработку
 * результата FUN_0045e0a0 и хвост 0x45f3a5), нативные листья — по инвентарю 1.4,
 * один детур на функцию (инвариант 5). Имя функции — по смыслу, который даст
 * дизасм (п. 4.3). Поле trace-строки — решение движения и его аргументы.
 *
 * Статус: портированы sub-steps 4.1a — пролог 0x45eee0..0x45ef7f (B0/B1/B2),
 * 4.1b — 0x45ef82..0x45efdb (B3/B4/B5) и 4.1c — 0x45efdc..0x45f0c7 (B6/B7/B8;
 * карта re/phase4_45eee0.md §8.2). B9 и далее не портированы; функция static и
 * ниоткуда не вызывается (детур и wiring — 4.2).
 */
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "trace.h"

/* float ↔ int-биты слота (x87-операнды `FLDS/FSTPS [ESP+off]` над int-слотами
 * кадра; паттерн path.cpp). */
static void s6_bits_set(int *slot, float f)
{
    memcpy(slot, &f, sizeof(int));
}

static float s6_bits_get(int slot)
{
    float f;

    memcpy(&f, &slot, sizeof(float));
    return f;
}

/* FUN_0045c330 (0x45c330..0x45c364) — сброс AI-состояния, тонкий порт (инвентарь
 * §5: «порт (тривиальный)»; §8.2 — этот файл). __thiscall, ECX = BattleAI,
 * 0 стек-аргументов (`RET` @0x45c364). Порядок записей = листинг: +0x38, +0x04,
 * +0x08, +0x10, +0x14, +0x0c (все dword = EBX = 0, `xor %ebx,%ebx` @0x45c334),
 * SetRectEmpty(&this+0x18) (@0x45c34c; lea/push — @0x45c336/0x45c339), затем
 * +0x28, +0x2c (dword 0), байт +0x30 (`mov %bl,0x30(%esi)` @0x45c358) и
 * `+0x34 = 0x0098967f` (@0x45c35b). Поле +0x00 (game_ctx) не трогается.
 * Единственный нативный лист — IAT SetRectEmpty (native::set_rect_empty). */
static void s6_ai_reset(BattleAI *self)
{
    unsigned char *p = (unsigned char *)self;

    *(int *)(p + 0x38) = 0;                            /* @0x45c33a mov %ebx,0x38(%esi) */
    *(int *)(p + 0x04) = 0;                            /* @0x45c33d mov %ebx,0x4(%esi) */
    *(int *)(p + 0x08) = 0;                            /* @0x45c340 mov %ebx,0x8(%esi) */
    *(int *)(p + 0x10) = 0;                            /* @0x45c343 mov %ebx,0x10(%esi) */
    *(int *)(p + 0x14) = 0;                            /* @0x45c346 mov %ebx,0x14(%esi) */
    *(int *)(p + 0x0c) = 0;                            /* @0x45c349 mov %ebx,0xc(%esi) */
    native::set_rect_empty(p + 0x18);                  /* @0x45c34c call *0x4ec268 (&this+0x18) */
    *(int *)(p + 0x28) = 0;                            /* @0x45c352 mov %ebx,0x28(%esi) */
    *(int *)(p + 0x2c) = 0;                            /* @0x45c355 mov %ebx,0x2c(%esi) */
    *(unsigned char *)(p + 0x30) = 0;                  /* @0x45c358 mov %bl,0x30(%esi) */
    *(unsigned int *)(p + 0x34) = 0x0098967fu;         /* @0x45c35b movl $0x98967f,0x34(%esi) */
}

/* FUN_0045eee0 — тик выбора точки движения (__thiscall, ECX = BattleAI*,
 * 1 стек-аргумент rec, RET 4). Кадр: F = entry−104 (sub $0x58 + push
 * ebx/ebp/esi/edi @0x45eee0..0x45eeee); слоты F+k моделируются массивом dword
 * по паттерну LS(k) из path.cpp. rec — стек-аргумент [entry+4] = слот F+0x6c.
 *
 * 4.1a = B0 (ленивый ctx через игровой operator new), B1 (6 dword-копий
 * rec→ctx), B2 (аллокация sub-объекта ctx[0xc] + 4-dword копия из rec[0xc]).
 * 4.1b = B3 (x87-гейт world[0x120] → AND флагов ctx, биты 1/5), B4 (бит6 →
 * band_free/native_45af10 по &self[0x3c]), B5 (бит7 → s6_ai_reset/clear_maps).
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

    /* B3 (0x45ef82..0x45efa4) — x87-гейт масштаба мира → сброс одного из битов
     * флагов ctx. Гейт: FLDS world[0x120] (@0x45ef85; world = *(rec+4)
     * @0x45ef82 — читается из rec, не из ctx), FCOMPS 0.0f [0x4ec37c]
     * @0x45ef8b, FNSTSW %ax @0x45ef91, TEST $0x41,%ah @0x45ef93 (C3|C0 статуса
     * x87). `jne`@0x45ef9a взят ⟺ test != 0 ⟺ NOT(ST строго > 0) — т.е. меньше,
     * равно или unordered(NaN); тот же гейт `JNZ(маска 0x41)`, что в path.cpp
     * (п. 1.1.7): строгое `>` для fall-through, всё прочее — взятие.
     * Записи: fall-through `&= ~0x02` (bit 1, `and $0xfffffffd` @0x45ef9c);
     * взятие `&= ~0x20` (bit 5, `and $0xffffffdf` @0x45efa1); одна запись
     * @0x45efa4. Флаги читаются в ECX до ветвления (@0x45ef96/0x45ef98). */
    {
        const unsigned char *world =
            (const unsigned char *)*(const void **)(rec + 0x04);   /* @0x45ef82 mov 0x4(%edi),%ecx */
        float world_scale;
        int flags;

        memcpy(&world_scale, world + 0x120, sizeof(float));  /* @0x45ef85 flds 0x120(%ecx) */
        ctx = (unsigned char *)self->game_ctx;               /* @0x45ef96 mov (%esi),%eax */
        flags = *(int *)(ctx + 0x00);                        /* @0x45ef98 mov (%eax),%ecx */
        if (world_scale > 0.0f) {                            /* @0x45ef8b/0x45ef91/0x45ef93, jne @0x45ef9a NOT taken */
            flags &= ~0x02;                                  /* @0x45ef9c and $0xfffffffd (fall-through) */
        } else {
            flags &= ~0x20;                                  /* @0x45efa1 and $0xffffffdf (jne взят) */
        }
        *(int *)(ctx + 0x00) = flags;                        /* @0x45efa4 mov %ecx,(%eax) */
    }

    /* B4 (0x45efa6..0x45efc1) — бит 6 флагов ctx: чистка ленты по &self[0x3c]
     * (EDI = `lea 0x3c(%esi)` @0x45efb1 — переиспользуется как this двух
     * thiscall-вызовов). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efa6 mov (%esi),%edx */
    if (((*(const unsigned int *)ctx >> 6) & 1) != 0) {      /* @0x45efa8/0x45efaa/0x45efad/0x45efaf je 0x45efc2 */
        void *band = (void *)((unsigned char *)self + 0x3c); /* @0x45efb1 lea 0x3c(%esi),%edi */

        native::band_free(band);                             /* @0x45efb4 mov %edi,%ecx / @0x45efb6 call 0x45b6a0 */
        native::native_45af10(band);                         /* @0x45efbb mov %edi,%ecx / @0x45efbd call 0x45af10 */
    }

    /* B5 (0x45efc2..0x45efdb) — бит 7 флагов ctx: сброс AI-состояния (тонкий порт
     * s6_ai_reset) и очистка карт (ECX = self в обоих вызовах: `mov %esi,%ecx`
     * @0x45efce/0x45efd5). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efc2 mov (%esi),%ecx */
    if (((*(const unsigned int *)ctx >> 7) & 1) != 0) {      /* @0x45efc4/0x45efc6/0x45efc9/0x45efcc je 0x45efdc */
        s6_ai_reset(self);                                   /* @0x45efce mov %esi,%ecx / @0x45efd0 call 0x45c330 */
        native::clear_maps(self);                            /* @0x45efd5 mov %esi,%ecx / @0x45efd7 call 0x45ca70 */
    }

    /* B6 (0x45efdc..0x45f065) — масштабы таймаута S(0x10)/S(0x18)/S(0x1c) и
     * ветка кодов {1,2,4}. ctx перечитывается; EDI = 32-бит флаги ctx
     * (@0x45efde), EAX = ctx[0x14] как int-биты (@0x45efe0). Слоты сеются в
     * порядке листинга: S(0x1c)=0, S(0x10)=ctx[0x14], S(0x18)=0; бит 4 →
     * x87-перезапись S(0x1c)/S(0x10). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efdc mov (%esi),%ecx */
    {
        unsigned int flags = *(const unsigned int *)ctx;     /* @0x45efde mov (%ecx),%edi */
        int ctx14 = *(const int *)(ctx + 0x14);              /* @0x45efe0 mov 0x14(%ecx),%eax */
        int code;                                            /* EAX ветки кодов (@0x45f020) */

        LS(0x1c) = 0;                                        /* @0x45efeb movl $0x0,0x1c(%esp) */
        LS(0x10) = ctx14;                                    /* @0x45eff3 mov %eax,0x10(%esp) */
        LS(0x18) = 0;                                        /* @0x45eff7 movl $0x0,0x18(%esp) */
        if (((flags >> 4) & 1) != 0) {                       /* @0x45efe3/0x45efe5/0x45efe8/0x45efff je 0x45f019 */
            float c14 = s6_bits_get(ctx14);                  /* @0x45f001 flds 0x14(%ecx) */
            float s1c = c14 * 0.0f;                          /* @0x45f004 fmuls [0x4ec37c] = 0.0f */
            s6_bits_set(&LS(0x1c), s1c);                     /* @0x45f00a fstps 0x1c(%esp) */
            s6_bits_set(&LS(0x10), c14 - s1c);               /* @0x45f00e flds 0x14 / @0x45f011 fsubs 0x1c / @0x45f015 fstps 0x10 */
        }
        LS(0x14) = (int)(INT_PTR)((unsigned char *)self + 0x38);  /* @0x45f019 lea 0x38(%esi),%eax / @0x45f01c mov %eax,0x14(%esp) */
        code = *(const int *)((unsigned char *)self + 0x38); /* @0x45f020 mov (%eax),%eax — код выхода e0a0 */
        if (code == 1 || code == 2 || code == 4) {           /* @0x45f022/0x45f025/0x45f027/0x45f02a/0x45f02c/0x45f02f je 0x45f056 */
            LS(0x18) = LS(0x10);                             /* @0x45f056 mov 0x10(%esp),%edx / @0x45f05a mov %edx,0x18(%esp) */
            LS(0x10) = 0;                                    /* @0x45f05e movl $0x0,0x10(%esp) */
        } else if (((flags >> 7) & 1) == 0) {                /* @0x45f031/0x45f033/0x45f036/0x45f038 jne 0x45f066 */
            float s10 = s6_bits_get(LS(0x10));               /* @0x45f03a/0x45f03e flds 0x10(%esp) ×2 (ST1 — копия старого) */
            float p = s10 * 0.6f;                            /* @0x45f042 fmuls [0x4ef4fc] = 0.6f */
            s6_bits_set(&LS(0x10), p);                       /* @0x45f048 fstps 0x10(%esp) */
            s6_bits_set(&LS(0x18), s10 - p);                 /* @0x45f04c fsubs 0x10(%esp) — старый S10 − новый / @0x45f050 fstps 0x18 */
        }
    }

    /* B7 (0x45f066..0x45f0c3) — grid-root `this+0x28` и посев 13-dword spec-окна
     * с базой F+0x34 (= spec+0x00..+0x30). Источник — ctx[0x10] (ECX здесь = ctx
     * с @0x45efdc, не world), запись @0x45f0b6 безусловная, fallback при нуле. */
    {
        int grid = *(const int *)(ctx + 0x10);               /* @0x45f066 mov 0x10(%ecx),%eax */
        unsigned char *dst = (unsigned char *)self + 0x28;   /* @0x45f06b lea 0x28(%esi),%edi */

        LS(0x34) = 0;                                        /* @0x45f06e spec+0x00 */
        LS(0x38) = 0;                                        /* @0x45f072 spec+0x04 */
        LS(0x3c) = 0;                                        /* @0x45f076 spec+0x08 */
        LS(0x40) = 0;                                        /* @0x45f07a spec+0x0c */
        LS(0x44) = 0;                                        /* @0x45f082 spec+0x10 */
        LS(0x48) = 0;                                        /* @0x45f08a spec+0x14 */
        LS(0x4c) = 0;                                        /* @0x45f092 spec+0x18 */
        LS(0x50) = 0;                                        /* @0x45f096 spec+0x1c */
        LS(0x54) = 0;                                        /* @0x45f09a spec+0x20 */
        LS(0x58) = 0;                                        /* @0x45f09e spec+0x24 */
        LS(0x5c) = (int)0xcb18967fu;                         /* @0x45f0a2 spec+0x28 = −1e7f */
        LS(0x60) = 0x4b18967f;                               /* @0x45f0aa spec+0x2c = +1e7f */
        LS(0x64) = 0;                                        /* @0x45f0b2 spec+0x30 */
        *(int *)dst = grid;                                  /* @0x45f0b6 mov %eax,(%edi) — безусловно */
        if (grid == 0) {                                     /* @0x45f069 cmp %ebx,%eax / @0x45f0b8 jne 0x45f0c5 */
            const unsigned char *world =
                (const unsigned char *)*(const void **)(ctx + 0x04);   /* @0x45f0ba mov 0x4(%ecx),%eax */
            *(int *)dst = *(const int *)(world + 0x134);     /* @0x45f0bd mov 0x134(%eax),%ecx / @0x45f0c3 mov %ecx,(%edi) */
        }
    }

    native::search_params_update(self);                      /* @0x45f0c5 mov %esi,%ecx / @0x45f0c7 call 0x45c8c0 */

#undef LS
}
