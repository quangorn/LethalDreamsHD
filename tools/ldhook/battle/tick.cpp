/* battle/tick.cpp — верхний уровень боевого AI (FUN_0044bdd0, тик).
 *
 * Фаза 5.1b: порт тела порциями по диапазонам 1.3b. Этот чанк — R1
 * (`0x44bdf9..0x44bec1`, B1..B6 после пролога), R2 (`0x44bec2..0x44bfb7`,
 * B7..B15), R3 (`0x44bfb8..0x44c09c`, B16..B22), R4 (`0x44c09d..0x44c183`,
 * B23..B29), R5 (`0x44c184..0x44c229`, B30..B33), R6 (`0x44c22a..0x44c306`,
 * B34..B40, grid-probe + log + цепочки) и R7 (`0x44c307..0x44c3ec`,
 * B41..B54, вход band-loop: string-блок C, ctx bit3, `45c730` cursor);
 * все листья — нативные
 * `native::…`, один детур на функцию (инвариант 5) — H1 `0x44bdf5` с возвратом
 * в нативный эпилог `0x44d074` (там же H2/capture, поэтому трассировка тика и
 * захват продолжают работать).
 *
 * Кадр: `frame` = ESP на входе H1 = S−0x158 (SEH-пролог/эпилог остаются
 * нативными). `frame+0x15c` = param_1, `+0x160` = param_2 (слот-local),
 * `+0x164` = param_3, `+0x14c` = prev FS:[0], `+0x154` = trylevel. Локалы
 * кадра (`+0x1c`, `+0x20` (out-тройка), `+0x2c..0x38`, `+0x4c..0x87`,
 * `+0x80`) пишутся по листингу — порт кадр не сдвигает и регистры EBP/ESI/
 * EDI/EBX не восстанавливает (их снимает нативный эпилог).
 *
 * Детур разоружён, пока тело неполное: `b0_port_enabled()` = LDHOOK_SITE5=1
 * И `B0_BODY_COMPLETE` (ниже). Выходы за пределы R1..R6 — явные UNPORTED-
 * заглушки с адресом блока-цели; они возвращают 0 (= `xor al,al` @0x44d072).
 */
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"

/* B0_BODY_COMPLETE — 0: портирован только R1..R7, тело неполное (дальше —
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
    /* UNPORTED: R8..R23 не портированы — детур не встаёт. */
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
 *   `test $0x41,%ah; jp`  — взят ⟺ NOT(a ≤ b) (больше или unordered);
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

static int b0_gate_41_jp(float a, float b)
{
    return !((a < b) || (a == b));  /* 0x44c0ad/0x44c0b0 (B23) */
}

static int b0_gate_44_jp(float a, float b)
{
    return !(a == b);               /* 0x44bebd/0x44bec0, 0x44beca, 0x44bed7 */
}

/* Биты float для grid_query: лист 0x44bbf0 получает 4 dword со стека
 * вызывающего и трактует их как целые (на сайте 0x45c67b — `FSTP m32`). */
static int b0_float_bits(float f)
{
    int bits;

    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/* --- R1..R4: B0..B29 --------------------------------------------------- *
 * Цепочка player-флагов (B16..B18 и её клоны B25..B27): player_get(unit,1) →
 * 42da60(player,1) → 435d50; три идентичных прогона, различаются только
 * пост-тестом. */
static void *b0_player_flag_chain(void *unit)
{
    void *player = native::player_get(unit, 1);          /* 0x44bfbf */
    void *flag = native::player_flag_get(player, 1);     /* 0x44bfc4 */
    return native::player_flags(flag);                   /* 0x44bfcb */
}

static int b0_body_r1_r2(void *unit_, unsigned char *frame)
{
    unsigned char *unit = (unsigned char *)unit_;
    /* EBP в нативе сдвигается `ADD EBP,0x18` в cursor-блоках и НЕ
     * восстанавливается (снимает только эпилогный POP EBP). Адресация после
     * сдвига идёт через unit_x (= EBP); сайты: 0x44c035 (R3 B21), 0x44c197
     * (R5 B30), далее 0x44c9a7/0x44ce7c (R14/R20). */
    unsigned char *unit_x = unit;
    unsigned char snap_buf[0x20];   /* B2: снимок 0x20 Б (натив: [esp] после sub 0x20) */
    unsigned char msg_dst[0x40];    /* B15/B24: копия msg-объекта (натив: esp после sub 0x3c) */
    float *out = (float *)(frame + 0x20);   /* B6: out-тройка 45c6f0 (= frame+0x20) */
    float phase;
    void *res;
    void *head;
    void *flags_obj;
    void *player;
    int count;
    int grid;
    unsigned int ebx;               /* R7 B49: значение EBX из R1 B5 (`[frame+0x160] & 8`) */
    float pos_triple[3];            /* B34: тройка позиции игрока (натив: [esp] после sub 0xc) */
    float cur_triple[3];            /* B34: тройка из result+4..0xc (второй grid_query) */

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
    /* 0x44c08b..0x44c09c — B22 (R3): общий вход ветки `state != 3` и провала
     * B14. `442570(unit,0)`; `[result] == 3` → B31 (R5), иначе fall → R4. */
    res = native::game_state_get(unit, 0);              /* 0x44c08f */
    if (*(const int *)res == 3) {                       /* 0x44c094/0x44c097 */
        goto b31;                                       /* je 0x44c1ed → R5 */
    }
    goto b23;                                           /* fall 0x44c09d → R4 */

b23:
    /* B23 (0x44c09d..0x44c0b5) — вход ветки `state != 3` (fall B22). EBP на
     * этом пути не сдвинут (cursor-блок B21 не исполнялся), поэтому EBP-
     * адресация идёт через unit_x == unit.
     * `phase_time` (vtable+0x2c) vs `[unit+0xfc]`; `TEST AH,0x41; JP` —
     * взято ⟺ NOT(phase ≤ threshold) (больше или unordered) → цепочка
     * state==3 (B31, R5). Направление обратно гейту B13 (`JNP`). */
    native::phase_time(unit_x, &phase);                 /* 0x44c09d..0x44c0a2 */
    if (b0_gate_41_jp(phase, *(const float *)(unit_x + 0xfc))) {
        goto b31;                                       /* 0x44c0b0 jp 0x44c1ed */
    }

    /* B24 (0x44c0b6..0x44c119) — msg-блок-клон R3 B15 с другими значениями:
     * `[frame+0x50]=1` (R3: 4), trylevel `[frame+0x154]=3` (R3: 0). Остальное
     * совпадает: msg_ctor(frame+0x4c); 32 Б из unit+0x64 в frame+0x60;
     * src-указатель в frame+0x1c; [frame+0x80]=0; msg_copy в by-value буфер
     * (натив: esp после `sub 0x3c`); ai_time_update; затем vtable+0x40 без
     * push (аргумент — слот сохранённого EDI, модель R3 @0x44bfb1). */
    native::msg_ctor(frame + 0x4c);                     /* 0x44c0b6..0x44c0ba */
    *(unsigned int *)(frame + 0x50) = 1;                /* 0x44c0c2 */
    memcpy(frame + 0x60, unit_x + 0x64, 32);            /* 0x44c0cd..0x44c0dc */
    *(void **)(frame + 0x1c) = msg_dst;                 /* 0x44c0e7 */
    *(unsigned int *)(frame + 0x154) = 3;               /* 0x44c0ec (trylevel) */
    *(unsigned int *)(frame + 0x80) = 0;                /* 0x44c0f7 */
    native::msg_copy(msg_dst, frame + 0x4c);            /* 0x44c102 */
    native::ai_time_update(unit_x, msg_dst);            /* 0x44c109 */
    if (native::player_get(unit_x, *(int *)(frame + 0)) == NULL) {
        goto b29;                                       /* 0x44c118 je 0x44c179 */
    }

    /* B25 (0x44c11a..0x44c134) — цепочка ×1 (те же 3 листа, что R3 B16),
     * NULL-гейт → B29. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c11a..0x44c12d */
    if (flags_obj == NULL) {
        goto b29;                                       /* 0x44c134 je 0x44c179 */
    }

    /* B26 (0x44c136..0x44c151) — цепочка ×2, `TESTB $2,(%eax)`: бит → B28. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c136..0x44c149 */
    if ((*(const unsigned char *)flags_obj & 0x2) != 0) {
        goto b28;                                       /* 0x44c151 jne 0x44c170 */
    }

    /* B27 (0x44c153..0x44c16e) — цепочка ×3, `TESTB $4,(%eax)`: бит снят → B29. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c153..0x44c166 */
    if ((*(const unsigned char *)flags_obj & 0x4) == 0) {
        goto b29;                                       /* 0x44c16e je 0x44c179 */
    }

b28:
    /* B28 (0x44c170..0x44c178) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44c170..0x44c174 */

b29:
    /* B29 (0x44c179..0x44c183) — msg-флаг `unit+0x34`: 0 → join; иначе
     * fall в R5 B30 (`0x44c184`, cursor-блок B). */
    if (*(const unsigned char *)(unit_x + 0x34) == 0) {
        goto b135_join;                                 /* 0x44c17e je 0x44cee6 */
    }
    goto b30;                                           /* fall 0x44c184 → R5 */

b30:
    /* B30 (0x44c184..0x44c1ec) — string/log-блок B (в disasm4 «cursor block B»;
     * 0x4ec064 = OutputDebugStringA, как B21). Клон B21 с литералом 0x4eedec и
     * байтами trylevel 4→5→4→3; в нативе все четыре — по `[ESP+0x154]` при
     * ESP = frame после `add $0xc` (0x44c1b2/0x44c1ca/0x44c1db) и
     * frame−0xc при трёх push 413060 (0x44c1a0 → frame+0x154), т.е. один слот
     * `frame+0x154`. */
    {
        void *str_a = frame + 0x10;
        void *str_d = frame + 0x40;
        void *str_r;

        native::string_ctor(str_a, native::literal(0x4eedec));  /* 0x44c184..0x44c18d */
        /* 0x44c197 `ADD EBP,0x18` — EBP (unit) сдвигается здесь и НЕ
         * восстанавливается (в теле нет `SUB EBP`; снимает только эпилог
         * `POP EBP`). unit_x фиксирует сдвиг в этой точке: все последующие
         * `[ebp+…]` идут от unit+0x18. */
        unit_x += 0x18;                                 /* 0x44c197 */
        frame[0x154] = 4;                               /* 0x44c1a0 */
        str_r = native::string_format(str_d, unit_x, str_a); /* 0x44c1a8 */
        frame[0x154] = 5;                               /* 0x44c1b2 */
        native::output_debug_string_a(native::string_c_str(str_r)); /* 0x44c1ba..0x44c1c0 */
        frame[0x154] = 4;                               /* 0x44c1ca */
        native::string_dtor(str_d);                     /* 0x44c1d2 */
        frame[0x154] = 3;                               /* 0x44c1db */
        native::string_dtor(str_a);                     /* 0x44c1e3 */
    }
    goto b135_join;                                     /* 0x44c1e8 jmp 0x44cee6 */

b31:
    /* B31 (0x44c1ed..0x44c1fe) — вход из B22 (R3) и B23 (R4): там EBP не
     * сдвинут (cursor-блоки B21/B30 на этих путях не исполнялись), поэтому
     * unit_x == unit. Состояние ==3 → B42 (band-loop, R7). */
    res = native::game_state_get(unit_x, 0);            /* 0x44c1ed..0x44c1f1 */
    if (*(const int *)res == 3) {                       /* 0x44c1f6 */
        goto b42;                                       /* 0x44c1f9 je 0x44c344 */
    }

    /* B32 (0x44c1ff..0x44c210) — состояние ==0 → B42. */
    res = native::game_state_get(unit_x, 0);            /* 0x44c1ff..0x44c203 */
    if (*(const int *)res == 0) {                       /* 0x44c208 */
        goto b42;                                       /* 0x44c20b je 0x44c344 */
    }

    /* B33 (0x44c211..0x44c229) — vtable+0x2c против литерала `[0x4eede8]` =
     * 3.0f (0x40400000, bit-exact): `TEST AH,0x41; JP` ⟺ NOT(phase ≤ 3.0f)
     * (больше или unordered) → B42; иначе fall → B34 (R6). */
    native::phase_time(unit_x, &phase);                 /* 0x44c216 */
    if (b0_gate_41_jp(phase, 3.0f)) {                   /* 0x44c219..0x44c224 */
        goto b42;                                       /* 0x44c224 jp 0x44c344 */
    }
    goto b34;                                           /* fall 0x44c22a → R6 */

b34:
    /* R6 (0x44c22a..0x44c306) — grid-probe B34..B40.
     * unit_x на входе: в 0x44c22a попадает только fall B33 (0x44c224 jp не
     * взят); B31/B32 достижимы из B22 (0x44c08b) и B23 (0x44c09d), где сдвига
     * `ADD EBP,0x18` нет (сдвиги B21 @0x44c035 и B30 @0x44c197 уходят
     * `jmp 0x44cee6`). Значит EBP не сдвинут: unit_x == unit. */
    {
        /* B34 (0x44c22a..0x44c282). Натив: `PUSH 0; SUB ESP,0xc` даёт на стеке
         * [esp]=&triple, [esp+4..0xc]=triple, [esp+0x10]=0 (arg4). Геттер
         * vtable+0x40 получает [esp+4]=&triple (ret 0 — указатель остаётся);
         * 42d9c0 снимает его (`ret $0x4`) и пишет позицию игрока в triple.
         * Следом 0x44bbf0 читает 4 dword прямо из [esp..esp+0xc] (ret 0x10),
         * т.е. triple и 0. Слот-записи `[esp+0x2c]=esp` в порт не нужны:
         * grid_query получает аргументы явно (контракт native.h). */
        pos_triple[0] = pos_triple[1] = pos_triple[2] = 0.0f;
        player = native::player_get(unit_x, (int)(INT_PTR)pos_triple); /* 0x44c23b */
        native::player_pos_set(player, pos_triple);                   /* 0x44c240 */
        (void)native::grid_query(unit_x, b0_float_bits(pos_triple[0]),
                                 b0_float_bits(pos_triple[1]),
                                 b0_float_bits(pos_triple[2]), 0);     /* 0x44c247 */
    }
    /* 0x44c24c..0x44c276 — второй grid_query по result+4..0xc (+arg4=0). */
    res = native::game_state_get(unit_x, 0);              /* 0x44c252 */
    memcpy(cur_triple, (const unsigned char *)res + 4, 12); /* 0x44c257..0x44c26d */
    grid = native::grid_query(unit_x, b0_float_bits(cur_triple[0]),
                              b0_float_bits(cur_triple[1]),
                              b0_float_bits(cur_triple[2]), 0);  /* 0x44c276 */
    if (grid == 0) {                    /* 0x44c27d `test; jbe` (unsigned ⇒ ==0) */
        goto b42;
    }

    /* B35 (0x44c283..0x44c29c) — assert/log только при grid > 0. */
    native::log_assert(unit_x, native::literal(0x51cc50), 0xa); /* 0x44c283..0x44c28c */
    /* 0x44c296 — геттер без push: callee читает [esp+4] = [frame] (модель B15). */
    if (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL) {
        goto b40;                                       /* 0x44c29b je 0x44c2fc */
    }

    /* B36 (0x44c29d..0x44c2b8) — цепочка ×1, NULL → B40. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c29d..0x44c2b5 */
    if (flags_obj == NULL) {
        goto b40;                                       /* 0x44c2b7 je */
    }

    /* B37 (0x44c2b9..0x44c2d5) — цепочка ×2, `TESTB $2`: бит → B39. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c2b9..0x44c2d1 */
    if ((*(const unsigned char *)flags_obj & 0x2) != 0) {
        goto b39;                                       /* 0x44c2d4 jne */
    }

    /* B38 (0x44c2d6..0x44c2f2) — цепочка ×3, `TESTB $4`: бит снят → B40. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c2d6..0x44c2ee */
    if ((*(const unsigned char *)flags_obj & 0x4) == 0) {
        goto b40;                                       /* 0x44c2f1 je */
    }

b39:
    /* B39 (0x44c2f3..0x44c2fb) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44c2f3..0x44c2f7 */

b40:
    /* B40 (0x44c2fc..0x44c306) — msg-флаг `unit+0x34`: 0 → выход 0x44d061
     * (out-guard param_3 = 0, AL = 0); иначе fall → B41 (R7). */
    if (*(const unsigned char *)(unit_x + 0x34) == 0) {
        void *p3 = *(void **)(frame + 0x164);           /* 0x44d061 out-guard */
        if (p3 != NULL) {
            *(int *)p3 = 0;
        }
        return 0;                                       /* 0x44d072 xor al,al */
    }
    goto b41;                                           /* fall 0x44c307 */

b41:
    /* B41 (0x44c307..0x44c343) — string-блок C; единственный вход — fall B40
     * (R6), где EBP ещё не сдвинут.
     * Натив: `push 0x4eed90` → ctor `[esp+0x14]` (= frame+0x10); затем
     * `push &[esp+0x10]` (объект A), `ADD EBP,0x18`, `&[esp+0x24]` (= frame+0x20
     * после push A — dst 413060), `push ebp` (unit+0x18), `push edx`;
     * `MOV EBX,6`, `[esp+0x160]` при ESP=frame−0xc = frame+0x154 — это SEH-слот
     * (ниже байтом пишется 7), НЕ param_2 (= frame+0x160); 413060; `ADD ESP,0xc`;
     * байт `[esp+0x154]` = 7; `JMP 0x44c6b6` (cursor tail B73, R11 → выход). */
    {
        void *str_a = frame + 0x10;
        void *str_r;

        native::string_ctor(str_a, native::literal(0x4eed90)); /* 0x44c307..0x44c310 */
        /* 0x44c31a `ADD EBP,0x18` — сдвиг ровно здесь, не откатывается
         * (в теле нет `SUB EBP`, снимает только эпилогный `POP EBP`); все
         * последующие `[ebp+…]` — через unit_x. */
        unit_x += 0x18;                                 /* 0x44c31a */
        ebx = 6;                                        /* 0x44c322 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c328 (SEH-слот) */
        str_r = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c32f */
        (void)str_r;                                    /* аргумент уходит в B73 (R11) */
        frame[0x154] = 7;                               /* 0x44c337 */
    }
    goto b73_cursor_tail;                               /* 0x44c33f jmp 0x44c6b6 */

b42:
    /* B42 (0x44c344..0x44c34e) — вход band-loop. Сюда ведут B31/B32 (0x44c1f9/
     * 0x44c20b), B33 (0x44c224 jp) и B34 `jbe` (0x44c27d) — на ВСЕХ этих путях
     * `ADD EBP,0x18` не исполнялся (сдвиги B21/B30 уходят в `0x44cee6`, B41 —
     * в `0x44c6b6`), поэтому здесь EBP НЕ сдвинут: unit_x == unit.
     * EBX на входе — значение R1 B5 (`[frame+0x160] & 8` @0x44be6d/0x44be74);
     * между B5 и R7 других записей EBX нет (проверено листингом), а param_2
     * слот дальше не пишется ⇒ пересчёт эквивалентен.
     * `[esp+0x15c]` = param_1 (cmd-флаг): ≠0 → B49. */
    ebx = *(const unsigned int *)(frame + 0x160) & 8u;  /* = EBX из B5 */
    if (*(const unsigned char *)(frame + 0x15c) != 0) {
        goto b49;                                       /* 0x44c34d jne 0x44c393 */
    }

    /* B43/B44/B45 (0x44c34f..0x44c364) — ctx `unit+0x138`; guard `TEST` до
     * deref (NULL → 0), бит3 → B46 (clear cmd), иначе сразу loop head. */
    {
        void *ctx = *(void **)(unit_x + 0x138);         /* 0x44c34f */
        unsigned int v = 0;

        if (ctx != NULL) {
            v = *(const unsigned int *)ctx;             /* 0x44c35f */
        }
        if ((v & 8u) == 0) {
            goto b47;                                   /* 0x44c363 je 0x44c36d */
        }
    }

    /* B46 (0x44c365) — байт param_1 = 0 (clear cmd-флаг перед loop head). */
    *(unsigned char *)(frame + 0x15c) = 0;

b47:
    /* B47 (0x44c36d..0x44c37b) — курсор ленты (45c730, ECX = &ctx); NULL →
     * B75 (R11). */
    head = native::band_cursor(unit_x + 0x138);         /* 0x44c36f */
    if (head == NULL) {
        goto b75;                                       /* 0x44c376 je 0x44c6ed */
    }

    /* B48 (0x44c37c..0x44c392) — повторный 45c730; на возврате
     * `MOV EDX,[EAX]; MOV ECX,EAX; CALL *0x40(EDX)` — vtable+0x40 БЕЗ push,
     * т.е. callee читает свой [esp+4] = *(frame+0) = слот сохранённого EDI
     * (модель B15/B35). Результат — anchor `[esp+0x38]`; `JMP 0x44c6f5`. */
    head = native::band_cursor(unit_x + 0x138);         /* 0x44c37e */
    *(void **)(frame + 0x38) =
        native::player_get(head, *(const int *)(frame + 0)); /* 0x44c387..0x44c38a */
    goto b76;                                           /* 0x44c38e jmp 0x44c6f5 */

b49:
    /* B49 (0x44c393..0x44c39a) — EBX ≠ 0 → flags-join B77 (R11). */
    if (ebx != 0) {
        goto b77_join;                                  /* 0x44c395 jne 0x44c715 */
    }

    /* B50..B52 (0x44c39b..0x44c3b4) — повторный ctx-гейт: заново
     * `LEA ESI,[ebp+0x138]` и deref-guard; бит3 → B77. */
    {
        void *ctx = *(void **)(unit_x + 0x138);         /* 0x44c39b */
        unsigned int v = 0;

        if (ctx != NULL) {
            v = *(const unsigned int *)ctx;             /* 0x44c3ab */
        }
        if ((v & 8u) != 0) {
            goto b77_join;                              /* 0x44c3af jne 0x44c715 */
        }
    }

    /* B53 (0x44c3b5..0x44c3c3) — голова ленты (45c370); NULL → B77. */
    head = native::band_head(unit_x + 0x138);           /* 0x44c3b7 */
    if (head == NULL) {
        goto b77_join;                                  /* 0x44c3be je 0x44c715 */
    }

    /* B54 (0x44c3c4..0x44c3ec) — повторный 45c370 → 45af50; длина ≤1
     * (знаковое `JLE`) → B77; затем 442570(ECX=EBP=unit,0) и фаза == 3,
     * иначе B77; fall → B55 (R8). */
    head = native::band_head(unit_x + 0x138);           /* 0x44c3c6 */
    count = native::band_count(head);                   /* 0x44c3cd */
    if (count <= 1) {
        goto b77_join;                                  /* 0x44c3d5 jle 0x44c715 */
    }
    res = native::game_state_get(unit_x, 0);            /* 0x44c3df */
    if (*(const int *)res != 3) {
        goto b77_join;                                  /* 0x44c3e7 jne 0x44c715 */
    }
    goto b55;                                           /* fall 0x44c3ed → R8 */

/* --- R7: далёкие цели (UNPORTED, точные адреса) ----------------------- *
 * Логика этих блоков не изобретается: заглушки возвращают 0 (путь
 * `xor al,al` @0x44d072 / out-guard @0x44d061). */
b73_cursor_tail:
    /* 0x44c6b6 (B73/B74, R11): c_str(`EAX`) → OutputDebugStringA, байт
     * trylevel = BL, два dtor'а, `JMP 0x44d061` (out-guard param_3 = 0, AL = 0). */
    return 0;

b75:
    /* 0x44c6ed (B75, R11): anchor `[esp+0x38]` = 0; fall B76. */
    return 0;

b76:
    /* 0x44c6f5 (B76, R11): 45c6f0(ECX=&ctx, &[esp+0x20]) → копия
     * result+0/+4/+8 в `[esp+0x2c/0x30/0x34]`; fall B77. */
    return 0;

b77_join:
    /* 0x44c715 (B77, R11): flags-join (EBX re-load, world-notify/grid). */
    return 0;

b55:
    /* 0x44c3ed (B55, R8): тело band-loop — UNPORTED. */
    return 0;


b16:
    /* B16 (0x44bfb8..0x44bfd3) — цепочка ×1, NULL-гейт → B20. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bfb8..0x44bfcb */
    if (flags_obj == NULL) {
        goto b20;                                       /* 0x44bfd2 je 0x44c017 */
    }

    /* B17 (0x44bfd4..0x44bff0) — цепочка ×2, `TESTB $2,(%eax)`: взятый бит →
     * B19 (448c30), иначе — B18. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bfd4..0x44bfe7 */
    if ((*(const unsigned char *)flags_obj & 0x2) != 0) {
        goto b19;                                       /* 0x44bfef jne 0x44c00e */
    }

    /* B18 (0x44bff1..0x44c00d) — цепочка ×3, `TESTB $4,(%eax)`: бит снят →
     * B20, иначе fall → B19. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bff1..0x44c004 */
    if ((*(const unsigned char *)flags_obj & 0x4) == 0) {
        goto b20;                                       /* 0x44c00c je 0x44c017 */
    }

b19:
    /* B19 (0x44c00e..0x44c016) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit, 0x40);             /* 0x44c00e..0x44c012 */

b20:
    /* B20 (0x44c017..0x44c021) — msg-флаг `unit+0x34`: 0 → join `0x44cee6`. */
    if (*(const unsigned char *)(unit_x + 0x34) == 0) {  /* 0x44c017..0x44c01c */
        goto b135_join;                                 /* je 0x44cee6 */
    }

    /* B21 (0x44c022..0x44c08a) — GUI-блок A (в disasm4 «cursor-block A»:
     * имя ошибочно, 0x4ec064 = OutputDebugStringA, не LoadCursorFromFileA):
     * ctor строки из литерала 0x4eee1c (объект `frame+0x10`, натив `[esp+0x14]`
     * при esp=frame−4), 413060(dst=`frame+0x40`, unit+0x18, объект) → c_str →
     * запись в debug-вывод, затем два dtor'а. Байты `[esp+0x160]` при
     * esp=frame−0xc — SEH-scope-state в нативном слоте trylevel `frame+0x154`
     * (как B15), значения 1→2→1→0. */
    {
        void *str_a = frame + 0x10;
        void *str_d = frame + 0x40;
        void *str_r;

        native::string_ctor(str_a, native::literal(0x4eee1c));  /* 0x44c022..0x44c02b */
        unit_x += 0x18;                                 /* 0x44c035 ADD EBP,0x18 */
        frame[0x154] = 1;                               /* 0x44c03e */
        str_r = native::string_format(str_d, unit_x, str_a); /* 0x44c046 */
        frame[0x154] = 2;                               /* 0x44c050 */
        native::output_debug_string_a(native::string_c_str(str_r)); /* 0x44c058..0x44c05e */
        frame[0x154] = 1;                               /* 0x44c068 */
        native::string_dtor(str_d);                     /* 0x44c070 */
        frame[0x154] = 0;                               /* 0x44c07c */
        native::string_dtor(str_a);                     /* 0x44c081 */
    }
    goto b135_join;                                     /* 0x44c086 jmp 0x44cee6 */

b135_join:
    /* 0x44cee6 — TODO(5.1b-2) UNPORTED: join R21 (входы B20 JZ, B21 jmp).
     * Возврат 0 = путь `xor al,al` @0x44d072. */
    return 0;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;            /* SEH-рамка 0x44bdd0, как s6_ai_update */
    }
}

/* Точка входа порта (ABI натива: ECX = unit, 3 стек-аргумента, RET 0xc).
 * Тело — под B0_BODY_COMPLETE: пока оно неполное, функция возвращает 0 и
 * детур разоружён (b0_port_enabled). Тело R1..R7 остаётся скомпилированным и
 * типизированным — `(void)&` ниже, чтобы /W3 не снял его как неиспользуемый. */
int b0_battle_ai_dispatch(void *unit, unsigned char *frame)
{
#if B0_BODY_COMPLETE
    return b0_body_r1_r2(unit, frame);      /* TODO(5.1b-2): R8..R23 */
#else
    /* UNPORTED: тело неполное — единственный вход детура остаётся пустым. */
    (void)&b0_body_r1_r2;
    (void)unit;
    (void)frame;
    return 0;
#endif
}