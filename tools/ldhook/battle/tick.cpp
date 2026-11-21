/* battle/tick.cpp — верхний уровень боевого AI (FUN_0044bdd0, тик).
 *
 * Фаза 5.1b: порт тела порциями по диапазонам 1.3b. Этот чанк — R1
 * (`0x44bdf9..0x44bec1`, B1..B6 после пролога) и R2 (`0x44bec2..0x44bfb7`,
 * B7..B15); все листья — нативные `native::…`, один детур на функцию
 * (инвариант 5) — H1 `0x44bdf5` с возвратом в нативный эпилог `0x44d074`
 * (там же H2/capture, поэтому трассировка тика и захват продолжают работать).
 *
 * Кадр: `frame` = ESP на входе H1 = S−0x158 (SEH-пролог/эпилог остаются
 * нативными). `frame+0x15c` = param_1, `+0x160` = param_2 (слот-local),
 * `+0x164` = param_3, `+0x14c` = prev FS:[0], `+0x154` = trylevel. Локалы
 * кадра (`+0x1c`, `+0x20` (out-тройка), `+0x2c..0x38`, `+0x4c..0x87`,
 * `+0x80`) пишутся по листингу — порт кадр не сдвигает и регистры EBP/ESI/
 * EDI/EBX не восстанавливает (их снимает нативный эпилог).
 *
 * Детур разоружён, пока тело неполное: `b0_port_enabled()` = LDHOOK_SITE5=1
 * И `B0_BODY_COMPLETE` (ниже). Выходы за пределы R1/R2 — явные UNPORTED-
 * заглушки с адресом блока-цели; они возвращают 0 (= `xor al,al` @0x44d072).
 */
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"

/* B0_BODY_COMPLETE — 0: портирован только R1/R2, тело неполное (дальше —
 * UNPORTED-выходы), поэтому детур обязан быть разоружён. Определяется в 1
 * тем чанком, который доводит тело до единственного RET `0x44d08c`. */
#ifndef B0_BODY_COMPLETE
#define B0_BODY_COMPLETE 0
#endif

/* Нативный эпилог (единственный RET тела, `0x44d074`): H2/capture стоят там,
 * поэтому порт прыгает именно туда, а не на `0x44d072` (`xor al,al`).
 * Значение возврата порт держит в AL. */
#define B0_EPILOGUE_VA 0x44d074u

volatile LONG g_b0_port_on = 0;     /* 1 = H1 уходит в порт (ставит ldhook.cpp) */
void *g_b0_epi = NULL;              /* 0x44d074 (native::fn) */

/* env-гейт: LDHOOK_SITE5 задан и != 0. Отдельно от полноты тела — нужен
 * установщику (H1/H2 ставятся под этим флагом), тогда как сам детур требует
 * ещё и B0_BODY_COMPLETE. */
int b0_env_on(void)
{
    const char *s = getenv(B0_ENV);

    return (s != NULL && *s != '\0' && atoi(s) != 0) ? 1 : 0;
}

/* Гейт порта: env LDHOOK_SITE5=1 И полное тело. Пока тело неполное —
 * всегда 0, независимо от env (дефолт любого прогона — нативный путь). */
int b0_port_enabled(void)
{
#if !B0_BODY_COMPLETE
    /* UNPORTED: R3..R23 не портированы — детур не встаёт. */
    return 0;
#else
    return b0_env_on();
#endif
}

/* --- x87-гейты (решение 1.6: маска → C++-сравнение) --------------------- *
 * FCOMP выставляет C3/C2/C0: > = 000, < = 001, = = 100, unordered = 111.
 * Маска тестируется в AH: 0x41 = C3|C0, 0x44 = C3|C2.
 *   `test $0x41,%ah; jnz` — взят ⟺ NOT(a > b) (включая unordered);
 *   `test $0x41,%ah; jnp` — взят ⟺ a < b или a == b (упорядоченное ≤);
 *   `test $0x44,%ah; jp`  — взят ⟺ NOT(a == b) (unordered = «не равно»).
 * PC=53 (double) сверено (1.6), поэтому сравнение в C++ бит-эквивалентно. */
static int b0_gate_41_jnz(float a, float b)
{
    return !(a > b);                /* 0x44be07/0x44be0a (B1) */
}

static int b0_gate_41_jnp(float a, float b)
{
    return (a < b) || (a == b);     /* 0x44bf29/0x44bf2c (B13) */
}

static int b0_gate_44_jp(float a, float b)
{
    return !(a == b);               /* 0x44bebd/0x44bec0, 0x44beca, 0x44bed7 */
}

/* --- R1/R2: B0..B15 ---------------------------------------------------- */
static int b0_body_r1_r2(void *unit_, unsigned char *frame)
{
    unsigned char *unit = (unsigned char *)unit_;
    unsigned char snap_buf[0x20];   /* B2: снимок 0x20 Б (натив: [esp] после sub 0x20) */
    unsigned char msg_dst[0x40];    /* B15: копия msg-объекта (натив: esp после sub 0x3c) */
    float *out = (float *)(frame + 0x20);   /* B6: out-тройка 45c6f0 (= frame+0x20) */
    float phase;
    void *res;
    void *head;
    int count;

    __try {

    /* B0 (0x44bdf5..0x44bdf8) — вход после нативного пролога: param_1 == 0 →
     * пропуск B1/B2 (0x44bdf7 je 0x44be58). */
    if (*(const unsigned char *)(frame + 0x15c) == 0) {
        goto b3;                                        /* 0x44bdf7 */
    }

    /* B1 (0x44bdf9..0x44be0a) — гейт пригодности тика: `[unit+0x114] > 0.0f`. */
    if (b0_gate_41_jnz(*(const float *)(unit + 0x114), 0.0f)) {
        goto b3;                                        /* 0x44be0a jne 0x44be58 */
    }

    /* B2 (0x44be0c..0x44be57) — снимок состояния и world-notify.
     * Копия не ровно 0x20 Б: 6 dword (0x00..0x17), 2 байта (0x18/0x19),
     * dword (0x1c); байты 0x1a/0x1b остаются как в буфере (0x44be13..0x44be49).
     * Буфер натива — [esp] после `sub 0x20` (кадр-скретч под frame), порт
     * держит его локально: адрес в frame+0x1c в портируемом диапазоне мёртв. */
    res = native::game_state_get(unit, 0);              /* 0x44be0c..0x44be0e */
    memcpy(snap_buf + 0x00, res, 0x18);                 /* 0x44be13..0x44be37 */
    snap_buf[0x18] = ((const unsigned char *)res)[0x18];/* 0x44be3a */
    snap_buf[0x19] = ((const unsigned char *)res)[0x19];/* 0x44be40 */
    *(unsigned int *)(snap_buf + 0x1c) =
        *(const unsigned int *)((const unsigned char *)res + 0x1c);  /* 0x44be46 */
    *(void **)(frame + 0x1c) = snap_buf;                /* 0x44be51 mov %esp,0x3c(%esp) */
    native::world_notify_snapshot(unit, snap_buf);      /* 0x44be55 call *0x28(%edx) */

b3:
    /* B3/B4 (0x44be58..0x44be69) — флаг `[unit+0x118]` → bit3 слота param_2. */
    if (*(const unsigned char *)(unit + 0x118) != 0) {
        *(unsigned int *)(frame + 0x160) |= 8u;         /* 0x44be62 orl $8,0x160(%esp) */
    }

    /* B5 (0x44be6a..0x44be93) — курсор `unit+0x74` → frame+0x38, тройка
     * `unit+0x68..0x70` → frame+0x2c/0x30/0x34; гейт bit3 → B12. */
    {
        const unsigned int *p;
        unsigned int force;

        *(unsigned int *)(frame + 0x38) =
            *(const unsigned int *)(unit + 0x74);       /* 0x44be6a/0x44be7c */
        force = *(const unsigned int *)(frame + 0x160) & 8u;  /* 0x44be6d/0x44be74 */
        p = (const unsigned int *)(unit + 0x68);        /* 0x44be77 lea 0x68(%ebp),%ecx */
        *(unsigned int *)(frame + 0x2c) = p[0];         /* 0x44be80/0x44be86 */
        *(unsigned int *)(frame + 0x30) = p[1];         /* 0x44be83/0x44be8a */
        *(unsigned int *)(frame + 0x34) = p[2];         /* 0x44be86/0x44be8e */
        if (force != 0) {
            goto b12;                                   /* 0x44be92 jne 0x44bf07 */
        }
    }

    /* B6 (0x44be94..0x44bec1) + B7/B8 (0x44bec2..0x44bedb) — позиция из ctx
     * должна совпасть с result+4/+8/+0xc: три x87-сравнения, `jp` → B12.
     * Порядок: 45c6f0 → 442570(0) → первое сравнение. */
    native::ctx_pos_get(unit + 0x138, out);             /* 0x44be9e..0x44bea1 */
    res = native::game_state_get(unit, 0);              /* 0x44bea6..0x44beac */
    if (b0_gate_44_jp(*(const float *)((const unsigned char *)res + 4), out[0]) ||
        b0_gate_44_jp(*(const float *)((const unsigned char *)res + 8), out[1]) ||
        b0_gate_44_jp(*(const float *)((const unsigned char *)res + 12), out[2])) {
        goto b12;                           /* 0x44bec0 / 0x44becd / 0x44beda */
    }

    /* B9 (0x44bedc..0x44bee3) — код тика == 1. */
    if (*(const int *)(unit + 0x170) != 1) {
        goto b12;                                       /* 0x44bee3 jne 0x44bf07 */
    }

    /* B10/B11 (0x44bee5..0x44bf06) — голова ленты (дважды) и её длина;
     * count > 1 → ранний выход «успех» (B148, R22). */
    head = native::band_head(unit + 0x138);             /* 0x44bee5..0x44bee7 */
    if (head == NULL) {
        goto b12;                                       /* 0x44beec/0x44beee je */
    }
    head = native::band_head(unit + 0x138);             /* 0x44bef0..0x44bef2 */
    count = native::band_count(head);                   /* 0x44bef7..0x44bef9 */
    if (count > 1) {                                    /* 0x44befe/0x44bf01 jg 0x44d02e */
        /* TODO(5.1b-2) UNPORTED: B148 (R22) — ранний выход, AL=1.
         * Возврат 0 = путь `xor al,al` @0x44d072. */
        return 0;
    }

b12:
    /* B12 (0x44bf07..0x44bf18) — фаза battle должна быть 3. */
    res = native::game_state_get(unit, 0);              /* 0x44bf07..0x44bf0b */
    if (*(const int *)res != 3) {                       /* 0x44bf10/0x44bf13 jne 0x44c08b */
        goto b22;
    }

    /* B13 (0x44bf19..0x44bf2d) — `phase_time <= [unit+0xfc]` (упорядоченное):
     * jnp взят → B15; иначе ( больше или unordered) → B14. */
    native::phase_time(unit, &phase);                   /* 0x44bf1e call *0x2c(%eax) */
    if (b0_gate_41_jnp(phase, *(const float *)(unit + 0xfc))) {
        goto b15;                                       /* 0x44bf2c jnp 0x44bf54 */
    }

    /* B14 (0x44bf2e..0x44bf53) — player_get(unit, result[0x10]) и сравнение
     * строковых объектов; AL == 0 → B22.
     * Аргументы 439ae0 (cdecl, push справа налево): arg1 = vtable-результат
     * (последний push @0x44bf43), arg2 = player (@0x44bf3d). */
    res = native::game_state_get(unit, 0);              /* 0x44bf2e..0x44bf32 */
    {
        void *player = *(void **)((unsigned char *)res + 0x10);   /* 0x44bf37 */
        void *v = native::player_get(unit, (int)(INT_PTR)player); /* 0x44bf3d..0x44bf40 */
        if ((unsigned char)native::string_obj_cmp(v, player) == 0) {  /* 0x44bf43..0x44bf4c */
            goto b22;                                   /* 0x44bf4e je 0x44c08b */
        }
    }

b15:
    /* B15 (0x44bf54..0x44bfb7) — msg-строка (ctor → 0x3c-байтовая копия →
     * ai_time_update), затем vtable+0x40 без явного аргумента.
     * Натив адресует temps через ESP после `sub 0x3c` (esp1 = frame−0x3c):
     *   [esp1+0x8c]=4 → frame+0x50; rep movsl → frame+0x60;
     *   [esp1+0x88] → frame+0x4c (src msg-объекта); [esp1+0x58]=esp1 → frame+0x1c;
     *   [esp2+0xc0]=0 → frame+0x80; [esp2+0x194]=0 → frame+0x154 (SEH state).
     * Буфер назначения натива — frame−0x3c (скретч под frame); порт держит
     * его локально, иначе он затёр бы собственный кадр. */
    native::msg_ctor(frame + 0x4c);                     /* 0x44bf54..0x44bf58 */
    *(unsigned int *)(frame + 0x50) = 4;                /* 0x44bf60 */
    memcpy(frame + 0x60, unit + 0x64, 32);              /* 0x44bf6b..0x44bf7a */
    *(void **)(frame + 0x1c) = msg_dst;                 /* 0x44bf85 */
    *(unsigned int *)(frame + 0x154) = 0;               /* 0x44bf8a (SEH state) */
    *(unsigned int *)(frame + 0x80) = 0;                /* 0x44bf95 */
    native::msg_copy(msg_dst, frame + 0x4c);            /* 0x44bf89..0x44bfa0 */
    native::ai_time_update(unit, msg_dst);              /* 0x44bfa5..0x44bfa7 */
    /* vtable+0x40 БЕЗ push (0x44bfb1): `call` кладёт retaddr, поэтому callee
     * читает свой [esp+4] = *(frame+0) — слот сохранённого EDI пролога
     * (`push ebx($53); ebp($55); esi($56); edi($57)` @0x44bdf1..0x44bdf4,
     * адрес растёт от EDI). Порт передаёт ровно это значение как аргумент.
     * МОДЕЛЬ: слот читается как вход; in/out-запись callee не наблюдаема.
     * ПЕРЕПРОВЕРИТЬ, когда дизасм R3/листьев (тело callee vtable+0x40) станет
     * видимым — тогда решается, in/out это или чистый вход. */
    if (native::player_get(unit, *(int *)(frame + 0)) == NULL) {   /* 0x44bfb4 */
        goto b20;                                       /* 0x44bfb6 je 0x44c017 */
    }
    goto b16;                                           /* fall-through 0x44bfb8 */

b22:
    /* 0x44c08b — B22 (R3): общий вход ветки state != 3 и провала B14.
     * TODO(5.1b-2) UNPORTED: R3 (`0x44c08b..0x44c09c`).
     * Возврат 0 = AL пути `xor al,al` @0x44d072. */
    return 0;

b16:
    /* 0x44bfb8 — R3 (цепочка player-флагов).
     * TODO(5.1b-2) UNPORTED: R3 (`0x44bfb8..0x44c09c`). */
    return 0;

b20:
    /* 0x44c017 — R3 (msg-гейт `[unit+0x34]`).
     * TODO(5.1b-2) UNPORTED: R3. */
    return 0;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;            /* SEH-рамка 0x44bdd0, как s6_ai_update */
    }
}

/* Точка входа порта (ABI натива: ECX = unit, 3 стек-аргумента, RET 0xc).
 * Тело — под B0_BODY_COMPLETE: пока оно неполное, функция возвращает 0 и
 * детур разоружён (b0_port_enabled). Чанк R1/R2 остаётся скомпилированным и
 * типизированным — `(void)&` ниже, чтобы /W3 не снял его как неиспользуемый. */
int b0_battle_ai_dispatch(void *unit, unsigned char *frame)
{
#if B0_BODY_COMPLETE
    return b0_body_r1_r2(unit, frame);      /* TODO(5.1b-2): R3..R23 */
#else
    /* UNPORTED: тело неполное — единственный вход детура остаётся пустым. */
    (void)&b0_body_r1_r2;
    (void)unit;
    (void)frame;
    return 0;
#endif
}