/* battle/tick.cpp — верхний уровень боевого AI (FUN_0044bdd0, тик).
 *
 * Фаза 5.1b: порт тела порциями по диапазонам 1.3b. Этот чанк — R1
 * (`0x44bdf9..0x44bec1`, B1..B6 после пролога), R2 (`0x44bec2..0x44bfb7`,
 * B7..B15), R3 (`0x44bfb8..0x44c09c`, B16..B22), R4 (`0x44c09d..0x44c183`,
 * B23..B29), R5 (`0x44c184..0x44c229`, B30..B33), R6 (`0x44c22a..0x44c306`,
 * B34..B40, grid-probe + log + цепочки), R7 (`0x44c307..0x44c3ec`,
 * B41..B54, вход band-loop: string-блок C, ctx bit3, `45c730` cursor) и
 * R8 (`0x44c3ed..0x44c4dc`, B55..B60, тело band-loop A: count-гейты,
 * `403e80` equality, `45b150`), R9 (`0x44c4dd..0x44c5b0`, B61..B66, тело
 * band-loop B: `45b3e0`, log-блоки E/F), R10 (`0x44c5b1..0x44c67d`,
 * B67..B71, тело band-loop C: `45b150` vs 3.0f, log-блок G, `45b3e0`,
 * back-edge `0x44c66d` → B46) и R11 (`0x44c67e..0x44c74c`, B72..B81: log-блок
 * H + общий cursor-tail B73/B74, pos-слот из `45c6f0` B75/B76, flags-join
 * B77..B81), R12 (`0x44c74d..0x44c82b`, B82..B84: PathFinder-init на
 * `frame+0xcc`, grid-record `frame+0x88`, `458050` anchor, log при NULL) и
 * R13 (`0x44c82c..0x44c877`, B85..B87: flags-цепочка tail anchor-NULL пути,
 * `448c30(0x40)` join, все выходы — dtor-join `0x44cad2`) и R14
 * (`0x44c878..0x44c96f`, B88 core-A: world-notify + `45e1d0` → запись
 * `unit+0xa4..0xac`, save/write/restore pos-полей состояния, FSTP фазы),
 * R15 (`0x44c970..0x44ca4b`, B89..B95: float-гейт `[unit+0xfc]`,
 * string-клон msg + `446a80`, цепочки флагов, `448c30(0x40)`) и R16
 * (`0x44ca4c..0x44cb38`, B96..B102: cursor-хвост `0x4eec4c`, out-write
 * `*param_3 = 1`, msg-dtor `462780`, dtor-join PathFinder `45e5e0` на
 * `frame+0xcc`, sentinel −10000.0f в `unit+0xa4..0xac`) и R17
 * (`0x44cb39..0x44cc18`, B103..B108: per-tick запись из 7 dword на
 * `frame+0xa4`, `45eee0` через диспетчер, `[unit+0x170]==0` → assert-лог +
 * цепочка флагов), R18 (`0x44cc19..0x44cd09`, B109..B120: dispatch-каскад
 * кодов 2/3, GUI-блок K1 и out-guard `*param_3 = 0`) и R19
 * (`0x44cd0a..0x44cdf1`, B121..B126: GUI-блок K2, гейты code==4 — head/count,
 * `vtable+0x2c` против `[unit+0xfc]`, builder-ctor и mode-флаг 1/4), R20
 * (`0x44cdf2..0x44cee5`, B128..B134: state-копия, цепочки, cursor K3
 * `0x4eebc8`, builder+`446a80`), R21 (`0x44cee6..0x44cf77`, B135..B142:
 * join out=1 + dtor строки msg, фаза code==4 — цепочки 2/4, `448c30(0x40)`,
 * лог `0x51cc50`/0xa) и R22 (`0x44cf78..0x44d071`, B143..B156: cursor K,
 * финальные band-гейты, `448ee0`×2, out-writes, AL=1 пути); из 12 листьев
 * нативны 3 — `44bbf0`, `45c730`, `45e1d0`, остальные портированы
 * (список — battle/ported.h), один детур на функцию (инвариант 5) — H1
 * `0x44bdf5` с возвратом в нативный эпилог `0x44d074` (там же H2/capture,
 * поэтому трассировка тика и захват продолжают работать).
 *
 * Кадр: `frame` = ESP на входе H1 = S−0x158 (SEH-пролог/эпилог остаются
 * нативными). `frame+0x15c` = param_1, `+0x160` = param_2 (слот-local),
 * `+0x164` = param_3, `+0x14c` = prev FS:[0], `+0x154` = trylevel. Локалы
 * кадра (`+0x1c`, `+0x20` (out-тройка), `+0x2c..0x38`, `+0x4c..0x87`,
 * `+0x80`) пишутся по листингу — порт кадр не сдвигает и регистры EBP/ESI/
 * EDI/EBX не восстанавливает (их снимает нативный эпилог).
 *
 * Детур разоружён: `b0_port_enabled()` = LDHOOK_SITE5=1 И
 * `B0_BODY_COMPLETE` (ниже). Тело R1..R22 портировано целиком (до `0x44d071`);
 * R23 (`0x44d072..0x44d08e`) остаётся нативной — эпилог и H2/capture.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "ported.h"

#ifdef BATTLE_REPLAY
/* Офлайн-харнесс (tools/probes/bdd0/arena.cpp): 1 — арена несёт модель объекта
 * сетки, и порт 458050 может прочитать свои входы. Ни один формат захвата
 * (v12..v15) сетку не пишет, поэтому в replay сегодня 0. Полная процедура
 * включения (захват v16) — (а) эта функция → 1, (б) оба nested-сайта 0x458142
 * и 0x45811a снять из k_nested_sites (bdd0/scenario.cpp), (в) 0x458050 добавить
 * в b0_ported_leaf_va с claim из тела порта — без (в) запись потока va=0x458050
 * не потребляет никто, и leaf_seq падает на каждом тике. Всё разом: arena.cpp
 * bdd0_grid_level_modelled, battle/ported.h, bdd0/scenario.cpp. */
int bdd0_grid_level_modelled(void);
/* То же для листа 0x45b150: 1 — арена несёт геометрию ленты (на каждый
 * 24-байтный элемент — его LDPos и scale-триплет уровня). Ни один формат
 * захвата (v12..v15) её не пишет, поэтому сайты 0x44c4cb/0x44c5e0 (leaf
 * 0x45b150) и 0x44c554/0x44c666 (leaf 0x45b3e0) реплеят записанный натив.
 * Включение (захват v16) — (а) эта функция → 1, (б) ключ bdd0_in "band_pos"
 * в загрузчик, (в) оба VA в b0_ported_leaf_va с claim из тел портов.
 * Всё разом: arena.cpp bdd0_band_geometry_modelled, battle/ported.h. */
int bdd0_band_geometry_modelled(void);
#endif
/* === BEGIN LABEL TRACE === */
/* Label trace, gated by -DB0_TRACE_LABELS=1.
 * B0_LBL(id): label id -> ring; B0_TST(gate, cond): condition + direction as one
 * event (bit31 = condition event, bit30 = branch taken, low bits = gate);
 * B0_EXIT(128+k): exit of the body.  Without the flag all three expand to
 * nothing, so tick.obj is byte-identical and the in-game build is unaffected.
 * b0_last_label feeds the native/leaf call stamps of native_replay.cpp. */
#if B0_TRACE_LABELS
#define B0_TRACE_RING 2048
static volatile unsigned g_b0_ring[B0_TRACE_RING];
volatile int b0_trace_on = 1;
volatile unsigned b0_last_label = 0;
volatile int b0_tick_len = 0;
volatile int b0_ring_i = 0;
const unsigned *b0_trace_ring(void) { return (const unsigned *)g_b0_ring; }
/* arrays of pointers, not const arrays: a top-level const object has internal
 * linkage in C++, so the harness could not link against them. */
const char *b0_label_names[] = {
    "b3",
    "b12",
    "b15",
    "b22",
    "b23",
    "b28",
    "b29",
    "b30",
    "b31",
    "b34",
    "b39",
    "b40",
    "b41",
    "b42",
    "b46",
    "b47",
    "b49",
    "b73_cursor_tail",
    "b75",
    "b76",
    "b77_join",
    "b80",
    "b81",
    "b85_r13",
    "b87_r13",
    "b88_r14",
    "b89_r15",
    "b94_r15",
    "b95_r15",
    "b96_r16",
    "b97_r16",
    "b99_r16",
    "b100_join",
    "b101_r16",
    "b102_r16",
    "b103_r17",
    "b109_r18",
    "b111_r18",
    "b112_r18",
    "b116_r18",
    "b119_r18",
    "b121_r19",
    "b122_r19",
    "b128_r20",
    "b131_r20",
    "b132_r20",
    "b134_r20",
    "b138_r21",
    "b141_r21",
    "b142_r21",
    "b143_r22",
    "b144_r22",
    "b148_r22",
    "b151_r22",
    "b154_r22",
    "b74_cursor_tail",
    "b0_exit_44d061",
    "b61_r9",
    "b63_r9",
    "b66",
    "b67_r10",
    "b70_r10",
    "b72_r11",
    "b55",
    "b59",
    "b16",
    "b19",
    "b20",
    "b135_join",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "b40_ret_0",
    "b100_join_ret_0",
    "b112_r18_ret_0",
    "b148_r22_ret_1",
    "b151_r22_ret_1",
    "b0_exit_44d061_ret_0",
    "b135_join_ret_0",
    "__except_handler_ret_0",
};
const char *b0_cond_names[] = {
    "*(const unsigned char *)(frame + 0x15c) == 0",
    "b0_gate_41_jnz(*(const float *)(unit + 0x114), 0.0f)",
    "*(const unsigned char *)(unit + 0x118) != 0",
    "force != 0",
    "b0_gate_44_jp(*(const float *)((const unsigned char *)res + 4), out[0]) || b0_gate_44_jp(*(const float *)((const unsigned char *)res + 8), out[1]) || b0_gate_44_jp(*(const float *)((const unsigned char *)res + 12), out[2])",
    "*(const int *)(unit + 0x170) != 1",
    "head == NULL",
    "count > 1",
    "*(const int *)res != 3",
    "b0_gate_41_jnp(phase, *(const float *)(unit + 0xfc))",
    "(unsigned char)native::string_obj_cmp(v, player) == 0",
    "native::player_get(unit, *(int *)(frame + 0)) == NULL",
    "*(const int *)res == 3",
    "b0_gate_41_jp(phase, *(const float *)(unit_x + 0xfc))",
    "native::player_get(unit_x, *(int *)(frame + 0)) == NULL",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "*(const int *)res == 3",
    "*(const int *)res == 0",
    "b0_gate_41_jp(phase, 3.0f)",
    "grid == 0",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "p3 != NULL",
    "*(const unsigned char *)(frame + 0x15c) != 0",
    "ctx != NULL",
    "(v & 8u) == 0",
    "head == NULL",
    "ebx != 0",
    "ctx != NULL",
    "(v & 8u) != 0",
    "head == NULL",
    "count <= 1",
    "*(const int *)res != 3",
    "ebx != (unsigned int)edi",
    "grid != 0",
    "(ebx & 0x8u) == 0",
    "anchor != NULL",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const int *)res == 3",
    "b0_gate_41_jp(*(const float *)(frame + 0x1c), *(const float *)(unit_x + 0xfc))",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "p3 != NULL",
    "cl != 0",
    "*(const int *)(unit_x + 0x170) != edi",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) != 0",
    "p3 != NULL",
    "*(const int *)(unit_x + 0x170) != 2",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "*(const int *)(unit_x + 0x170) != 3",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "*(const int *)(unit_x + 0x170) != 4",
    "head != NULL",
    "count > 1",
    "b0_gate_41_jp(phase, *(const float *)(unit_x + 0xfc))",
    "!r19_state3",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "native::player_get(unit_x, *(const int *)(frame + 0)) == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "head == NULL",
    "count <= 1",
    "b0_gate_41_jnz(*(const float *)(unit_x + 0x114), 0.0f)",
    "*(const unsigned int *)(unit_x + 0x84) == 0",
    "p3 != NULL",
    "p3 != NULL",
    "p3 != NULL",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "(unsigned char)b0_band_target_moved(head, band_triple[0], b0_float_bits(band_triple[1]), band_triple[2]) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "*(const int *)(unit_x + 0x170) == 1",
    "b0_gate_41_jp(gate, 3.0f)",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "(unsigned char)b0_band_target_moved(head, band_triple[0], b0_float_bits(band_triple[1]), band_triple[2]) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "native::band_cursor(unit + 0x138) != *(void *const *)((const unsigned char *)res + 0x14)",
    "!b0_pos3_equal(out, (const unsigned char *)res + 4)",
    "*(const unsigned char *)(unit + 0x34) == 0",
    "*(const int *)(unit + 0x170) != 1",
    "b0_gate_41_jp(gate, 4.0f)",
    "flags_obj == NULL",
    "(*(const unsigned char *)flags_obj & 0x2) != 0",
    "(*(const unsigned char *)flags_obj & 0x4) == 0",
    "*(const unsigned char *)(unit_x + 0x34) == 0",
    "p3 != NULL",
};
static void b0_lbl(int id)
{
    b0_last_label = (unsigned)id;
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)id;
    b0_ring_i++;
}
static int b0_tst(int gate, int cond)
{
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)gate | 0x80000000u |
        (cond ? 0x40000000u : 0u);
    b0_ring_i++;
    return cond;
}
static void b0_exit(int id)
{
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)id;
    b0_ring_i++;
}
#define B0_LBL(id) b0_lbl(id)
#define B0_TST(gate, cond) b0_tst((gate), (cond))
#define B0_EXIT(id) b0_exit(id)
#else
#define B0_LBL(id) ((void)0)
#define B0_TST(gate, cond) (cond)
#define B0_EXIT(id) ((void)0)
#endif
/* === END LABEL TRACE === */


/* B0_BODY_COMPLETE — 0: тело R1..R22 портировано полностью, но детур остаётся
 * разоружённым — взвод (эта проверка) отдельное решение, не часть порта.
 * Включение = этот макрос в 1 (плюс env LDHOOK_SITE5=1 на прогоне). */
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

/* Гейт порта: env LDHOOK_SITE5=1 И взведённое тело (B0_BODY_COMPLETE). Пока
 * тело не взведено — всегда 0, независимо от env (дефолт — нативный путь). */
int b0_port_enabled(void)
{
#if !B0_BODY_COMPLETE
    /* Тело R1..R22 готово; взвод не включён — детур не встаёт. */
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

/* float32 из памяти — операнд `FLD m32`/`FADD m32` натива. */
static float b0_f32(const unsigned char *p)
{
    float f;

    memcpy(&f, p, 4);
    return f;
}

/* --- Порт листа 0x45c370 (band_head) ------------------------------------ *
 * Листинг: `MOV ESI,ECX; ADD ESI,0x3c` → `EAX = band_count(ESI)` →
 * `NEG EAX; SBB EAX,EAX; AND EAX,ESI` = длина != 0 ? ESI : 0, `RET`.
 * Лист 0x45af50 (длина ленты) теперь портирован: begin==0 → 0, иначе
 * (end-begin)/0x18 со знаковым усечением (`TEST EAX,EAX; JZ` @0x45af50).
 * Внутренний пересчёт головы идёт через b0_band_count_raw (claim НЕ снимает);
 * claim 45af50 выпускает отдельная обёртка b0_band_count_claim ниже.
 * Возврат головы — указатель ВНУТРИ блока BattleAI (ai+0x3c), поэтому в
 * харнессе он остаётся shadow-адресом: claim сверяет его проверка leaf_value. */
static int b0_band_count_raw(const void *band)
{
    unsigned begin, end;

    if (band == NULL) {
        return 0;
    }
    memcpy(&begin, (const unsigned char *)band + B0_BAND_BEGIN_OFF, sizeof(begin));
    if (begin == 0) {
        return 0;                       /* TEST EAX,EAX / JZ @0x45af50 */
    }
    memcpy(&end, (const unsigned char *)band + B0_BAND_END_OFF, sizeof(end));
    return (int)(end - begin) / B0_BAND_ELEM;
}

/* Лист 0x45af50 на сайте тела: считается значение и снимается claim с ECX =
 * band (длина 0 аргументов, `ret`). Запись 45af50 в потоке потребляет именно
 * этот claim — b0_band_head внутренний пересчёт не заявляет. */
static int b0_band_count_claim(const void *band)
{
    int count = b0_band_count_raw(band);

    B0_LEAF_CLAIM(B0_LEAF_BAND_COUNT, band, count, NULL, 0);
    return count;
}

static void *b0_band_head(void *ai_)
{
    unsigned char *ai = (unsigned char *)ai_;
    unsigned char *band = ai + B0_BATTLE_AI_BAND;
    void *res;

    /* ESI = ai + 0x3c; AND из NEG/SBB/AND: длина 0 → NULL.
     * ECX = unit+0x138 всегда (все 12 сайтов), поэтому NULL-получатель
     * недостижим; гейт оставлен, чтобы порт не разыменовывал мусор. */
    res = (ai != NULL && b0_band_count_raw(band) != 0) ? band : NULL;
    B0_LEAF_CLAIM(B0_LEAF_BAND_HEAD, ai, res, NULL, 0);
    return res;
}

/* --- Порт листа 0x45b150 (band_interp_gate) ----------------------------- *
 * Листинг `0x45b150..0x45b3da` (651 Б, 39 блоков) сверен с байтами baseline
 * `backup/LETHALDREAMS.EXE.pre4` (md5 45e93d46…, fileoff = VA − 0x400000):
 * `push ebp; mov ebp,esp; and esp,−8; sub esp,0x24; push ebx/esi/edi`, SEH нет,
 * jmp-таблиц нет, единственный callee 0x456b20, эпилог `ret 0xc`.
 *
 * ABI: __thiscall, ECX = лента (BattleAI+0x3c), 3 стек-аргумента:
 * [EBP+0x8] = px (float32), [EBP+0xc] = мёртвый dword (в теле нет ни одного
 * чтения), [EBP+0x10] = py (float32). Результат — ST(0): эпилог
 * `FLD float [ESP+0x10]` (0x45b3ce), т.е. ровно float32-биты слота BEST
 * (`FLD m32` точен; лишней точности в вызывающего не уходит). Порт возвращает
 * float — те же биты, что `ret` записи потока bdd0_leaf.
 *
 * Слоты кадра (E = ESP после `push ebx/esi/edi`): BEST E+0x10 (init 100.0f
 * @0x45b15e; пишется только `FSTP m32`), slope E+0x14 (@0x45b34f), X0 E+0x18,
 * Y0 E+0x1c, X1 E+0x20, Y1 E+0x24 (out-пары двух вызовов 0x456b20 + поля
 * узлов), FX E+0x28, FY E+0x2c. x87 при PC=53 = double (решение 1.6), каждый
 * слот получает float32 через `FSTP m32` → double-времянки + `(float)`
 * воспроизводят округления по шагам.
 *
 * Что считается: минимум d² от (px,py) до полилинии ленты. Для пары
 * (elem[i−1], elem[i]), i = 1..count−1:
 *   pos У КАЖДОГО КОНЦА СВОЙ: первый вызов 0x456b20 берёт
 *   `[begin+EDI−4]` = elem[i−1]+0x14 (@0x45b1a1 `mov -0x4(%edx,%edi,1)`),
 *   второй — elem[i]+0x14 (@0x45b1cb). (re/leaf_45b150.md §3 утверждал «один
 *   pos на обе точки» — опровергнуто по байтам; CONTRADICTIONS #21.)
 *   X0 = out0(pos(i−1)) + elem[i−1].x, Y0 = out1 + elem[i−1].y (@0x45b1aa..c7)
 *   X1 = out0(pos(i)) + elem[i].x,     Y1 = out1 + elem[i].y  (@0x45b1d3..eb)
 *   (Мутация «pos от elem[i] для обоих концов» расходится с нативом почти на
 *   всех конфигурациях A/B — расщепление подтверждено исполнением.)
 * Далее три взаимоисключающие ветви (D вертикаль / E горизонталь / F наклон),
 * затем d² = (FX−px)² + (FY−py)² и `if (d2 < best) best = (float)d2`.
 * «Ни один отрезок не подошёл» → BEST остаётся 100.0f (sentinel 0x42c80000;
 * тик сравнивает результат с 4.0f @0x44c4d0 и 3.0f @0x44c5e5).
 *
 * Предикаты (маски → C++, правило 1.6): `TEST AH,0x44; JP` ⟺ NOT(a==b)
 * (unordered = «не равно»); `TEST AH,0x5; JP` ⟺ NOT(a<b) (unordered → взято);
 * `TEST AH,0x1; JNZ` ⟺ a<b (unordered → взято); `TEST AH,0x41; JNZ` ⟺ NOT(a>b).
 * Условия ветвей ниже записаны как `a == b` / `x >= mn` / `x < mx`: на
 * УПОРЯДОЧЕННЫХ операндах C++-оператор даёт исход, сверенный с маской по всем
 * границам тела. Неупорядоченный случай границы `0x45b3b1` (`TEST AH,0x5; JP`,
 * d²=NaN) не измерен: ни один захват до неё не доходит; при NaN порт не
 * обновляет BEST — то же, что делает маска, но это вывод, а не замер.
 *
 * FX/FY натив НЕ инициализирует: слоты E+0x28/E+0x2c лежат в собственном
 * локальном окне функции (E+0xc..E+0x2f) и до первого чтения (@0x45b391/0x45b398)
 * не пишутся ничем — там чужой мусор стека ниже ESP вызывающего. Порт
 * инициализирует их нулём: значение всё равно произвольное, но явное и
 * детерминированное (та же мера, что зануление неинициализированной тройки
 * натива у B34/B78). Проверено на нативном коде baseline: конфигурации, где
 * первая итерация берёт хотя бы одну ветвь, совпадают бит-в-бит; все
 * записанные «sentinel»-вызовы пяти захватов имеют px²+py² > 100 — там нулевая
 * инициализация даёт тот же 100.0f. Окно расхождения — только до первой взятой
 * ветви, когда чужой мусор < 100 (docs/PITFALLS.md).
 *
 * Чтения состояния: [band+0x20] (begin, перечитывается — 0x45b170/0x45b199/
 * 0x45b1ae/0x45b1d7), [band+0x24] (end), elem[i−1]+0x00/+0x08,
 * elem[i]+0x00/+0x08, elem[i−1]+0x14 и elem[i]+0x14 (LDPos*), через 0x456b20 —
 * pos+0x0c/+0x10, [pos+0x1c] (GridLevel) и level+0x20/+0x24/+0x28. Записей в
 * игру нет (только кадр и out-слоты 0x456b20) — функция чистая. В теле
 * 0x44bdd0 два сайта: 0x44c4cb (B60, гейт 4.0f) и 0x44c5e0 (B67, гейт 3.0f).
 * Guard'ов на повреждённую ленту нет: `i` сравнивается с count беззнаково
 * (`JNB`), end < begin уводит цикл в память вызывающего — порт воспроизводит
 * это буквально (без «защит»).
 *
 * Офлайн: claim выпускается телом, но 0x45b150 пока НЕ в b0_ported_leaf_va —
 * его вход (pos/level каждого узла) ни один формат захвата не пишет; сайты
 * идут через b0_band_interp_gate (реплей натива, см. ниже). */
float s6_band_interp_gate(const void *band_, float px, int dead_arg, float py)
{
    const unsigned char *b = (const unsigned char *)band_;
    float best = 100.0f;            /* 0x45b15e MOV dword [E+0x10],0x42c80000 */
    unsigned i = 1;                 /* 0x45b166 EBX = 1 */
    unsigned off = B0_BAND_ELEM;    /* 0x45b16b EDI = 0x18 */
    float fx, fy;                   /* слоты E+0x28/E+0x2c — см. шапку */
    fx = 0.0f;                      /* см. «FX/FY» в шапке: натив их НЕ
                                     * инициализирует; значение выбрано */
    fy = 0.0f;
    for (;;) {
        unsigned begin, end;
        int count;

        memcpy(&begin, b + B0_BAND_BEGIN_OFF, 4);       /* 0x45b170 */
        if (begin == 0) {                               /* 0x45b175 JZ 0x45b3ce */
            break;
        }
        memcpy(&end, b + B0_BAND_END_OFF, 4);           /* 0x45b17b */
        count = (int)(end - begin) / B0_BAND_ELEM;      /* 0x45b17e..0x45b18f */
        if (i >= (unsigned)count) {                     /* 0x45b191 CMP/JNB */
            break;
        }
        {
            const unsigned char *e1 = (const unsigned char *)(size_t)begin + off;
            const unsigned char *e0 = e1 - B0_BAND_ELEM;
            LDPos *pos0, *pos1;
            float o0[2], o1[2];
            float x0, y0, x1, y1;
            int d, e, f;

            memcpy(&pos0, e0 + 0x14, sizeof(pos0));     /* 0x45b1a1 elem[i−1] */
            s6_grid_scale_compute(pos0, o0);            /* 0x45b1a5 CALL 0x456b20 */
            x0 = (float)((double)o0[0] + (double)b0_f32(e0 + 0x00)); /* 0x45b1aa..0x45b1bc */
            y0 = (float)((double)o0[1] + (double)b0_f32(e0 + 0x08)); /* 0x45b1c0..0x45b1c7 */
            memcpy(&pos1, e1 + 0x14, sizeof(pos1));     /* 0x45b1cb elem[i] */
            s6_grid_scale_compute(pos1, o1);            /* 0x45b1ce CALL 0x456b20 */
            x1 = (float)((double)o1[0] + (double)b0_f32(e1 + 0x00)); /* 0x45b1d3..0x45b1e0 */
            y1 = (float)((double)o1[1] + (double)b0_f32(e1 + 0x08)); /* 0x45b1e4..0x45b1eb */

            /* D (0x45b1ef..0x45b257): вертикальный отрезок. */
            d = (x0 == x1)                                  /* 0x45b1f9 JP */
                && (py >= ((y0 < y1) ? y0 : y1))            /* 0x45b208/0x45b220 */
                && (py <  ((y0 > y1) ? y0 : y1));           /* 0x45b22f/0x45b247 */
            /* E (0x45b25b..0x45b2c3): горизонтальный отрезок. */
            e = (y0 == y1)                                  /* 0x45b268 JP */
                && (px >= ((x0 < x1) ? x0 : x1))            /* 0x45b274/0x45b28c */
                && (px <  ((x0 > x1) ? x0 : x1));           /* 0x45b29b/0x45b2b3 */
            /* F (0x45b2c7..0x45b38f): наклонный отрезок. */
            f = (x0 != x1) && (y0 != y1)                    /* 0x45b2d1/0x45b2e7 */
                && (px >= ((x0 < x1) ? x0 : x1))            /* 0x45b2f7/0x45b30f */
                && (px <  ((x0 > x1) ? x0 : x1));           /* 0x45b31e/0x45b336 */

            if (d) {
                fx = x0;                        /* 0x45b24c/0x45b253 MOV dword'ы */
                fy = py;                        /* 0x45b250/0x45b257 */
            } else if (e) {
                fx = px;                        /* 0x45b2b8/0x45b2bf */
                fy = y0;                        /* 0x45b2bb/0x45b2c3 */
            } else if (f) {
                /* 0x45b33b..0x45b38f — проекция на прямую в реципрокальной
                 * форме: `b` считается по НЕокруглённому m (dy*x0/dx), а `1/m`
                 * и `1/m + m` — по float32-слоту slope (0x45b367/0x45b377).
                 * Алгебраически равносильная (px + m*(py−b))/(1+m²) даёт
                 * другие биты — так писать нельзя. */
                double dy = (double)y1 - (double)y0;    /* 0x45b33b FSUB m32 */
                double dx = (double)x1 - (double)x0;    /* 0x45b343 FSUB m32 */
                double m  = dy / dx;                    /* 0x45b34b/0x45b34d */
                float  mf = (float)m;                   /* 0x45b34f FSTP m32 */
                double bb = (double)y0 - (dy * (double)x0) / dx; /* 0x45b353..0x45b35b */
                double inv = 1.0 / (double)mf;          /* 0x45b361/0x45b367 */
                double num = (double)px * inv + (double)py; /* 0x45b36e/0x45b370 */
                double den = inv + (double)mf;          /* 0x45b377 FADD m32 */

                num = num - bb;                         /* 0x45b373 FSUB ST0,ST2 */
                fx = (float)(num / den);                /* 0x45b37b FDIVP/0x45b37d */
                fy = (float)((double)mf * (double)fx + bb); /* 0x45b381..0x45b38b */
            }

            /* G (0x45b391..0x45b3c9): d² и обновление минимума. */
            {
                double fxd = (double)fx - (double)px;   /* 0x45b391/0x45b395 */
                double fyd = (double)fy - (double)py;   /* 0x45b398/0x45b39c */
                double d2  = fxd * fxd + fyd * fyd;     /* 0x45b39f..0x45b3a9 */

                if (d2 < best) {                        /* 0x45b3b1 JP 0x45b3c3 */
                    best = (float)d2;                   /* 0x45b3b7 FSTP m32 */
                }
            }
            i++;                                        /* 0x45b3b6 INC EBX */
            off += B0_BAND_ELEM;                        /* 0x45b3bb ADD EDI,0x18 */
        }
    }
    {
        int av[3], rbits;

        av[0] = b0_float_bits(px);      /* [EBP+0x8]: сайт кладёт triple dword'ами */
        av[1] = dead_arg;               /* [EBP+0xc]: мёртвый, но в записи есть */
        av[2] = b0_float_bits(py);      /* [EBP+0x10] */
        memcpy(&rbits, &best, 4);       /* ST(0) = float32-биты BEST */
        B0_LEAF_CLAIM(B0_LEAF_INTERP_GATE, band_, rbits, av, 3);
    }
    return best;                        /* 0x45b3ce FLD float [E+0x10] */
}

/* Диспетчер листа на сайтах 0x44c4cb (B60) и 0x44c5e0 (B67). Порт исполняется
 * только там, где арена несёт модель геометрии ленты (для каждого 24-байтного
 * элемента — его LDPos и scale-триплет уровня). В захватах v12..v15 её нет
 * (arena.cpp bdd0_band_geometry_modelled): `[elem+0x14]` — указатель в кучу
 * игры, офлайн он не разыменовывается, поэтому реплеится записанный натив.
 * Живой билд (BATTLE_REPLAY не определён) всегда идёт в порт. */
static float b0_band_interp_gate(void *band, float px, int dead_arg, float py)
{
#ifdef BATTLE_REPLAY
    if (!bdd0_band_geometry_modelled()) {
        float out;

        native::band_interp_gate(band, px, dead_arg, py, &out);
        return out;
    }
#endif
    return s6_band_interp_gate(band, px, dead_arg, py);
}

/* --- Порт листа 0x45b3e0 (band_target_moved) ---------------------------- *
 * Листинг `0x45b3e0..0x45b52f` (336 Б, 117 инстр., 15 блоков) сверен с байтами
 * baseline `backup/LETHALDREAMS.EXE.pre4` (md5 45e93d46…, fileoff = VA −
 * 0x400000): `sub esp,0x10; push esi; mov esi,ecx`, SEH/jmp-таблиц нет,
 * callee — 0x456b20 (×2) и CRT-acos 0x4d1950, оба выхода `ret 0xc`.
 *
 * ABI: __thiscall, ECX = лента (BattleAI+0x3c), 3 стек-аргумента: [E+0x4] = p1
 * (float32), [E+0x8] = мёртвый (в теле нет ни одного чтения), [E+0xc] = p3
 * (float32). Результат — AL (0/1); старшие 24 бита EAX наследуют callee'ы.
 * Сайты тика: B63 0x44c554 (site 18) и B70 0x44c666 (site 22).
 *
 * Слоты `sub esp,0x10` (B = ESP после push esi): out0[0]/[1] = B+4/B+8 (первый
 * 0x456b20), out1[0]/[1] = B+0xc/B+0x10 (второй), Lx0 = B+4, Ly0 = B+8,
 * Lx1 = B+0xc. Слот B+0x10 (o1[1]) читается один раз и НЕ пишется в память —
 * Yn остаётся в x87-регистре.
 *
 * count = trunc((end−begin)/0x18) знаковым magic-делением (0x2aaaaaab…),
 * guard `CMP EDX,2; JB` БЕЗзнаковый — отрицательный count его проходит и уводит
 * `elem[count−1]`/`elem[count−2]` за начало ленты; порт воспроизводит это
 * буквально (без «защит»). Шаг элемента 0x18:
 *   pos(elem[0]) = [begin+0x14] → 0x456b20 → o0;
 *   Lx0 = fl(o0[0] + elem[0].x), Ly0 = fl(o0[1] + elem[0].y)   (два FSTP m32)
 *   pos(elem[count−1]) = [begin+24·count−4] → 0x456b20 → o1;
 *   Lx1 = fl(o1[0] + elem[count−2].x)                          (FSTP m32)
 *   Yn  = o1[1] + elem[count−2].y                              (FSTP НЕТ → double)
 *   dot = (Lx0−p1)·(Lx0−Lx1) + (Ly0−p3)·(Ly0−Yn)
 * Разности — в extended (PC=53): в C++ это `double`-разности float-операндов,
 * `(double)Lx0 − (double)p1`, а НЕ `(double)(Lx0 − p1)` (второе округлило бы в
 * float и дало другие биты для далёких операндов).
 *
 * Гейты (маска 0x5 в AH, правило battle_ai_x87.md §1):
 *   gate1 (0x45b4fb..0x45b506): FCOMP 0.0f; `TEST AH,0x5; JNP` взят ⟺ dot < 0
 *     → 0. Unordered (NaN) — PF=1, JNP не берёт → fall.
 *   gate2 (0x45b508..0x45b51c): `FST m32` (0x45b4f1 — float32-округление dot),
 *     FLD, CRT-acos 0x4d1950, FCOMP 0.34906542f; `TEST AH,0x5; JP` взят ⟺
 *     NOT(acos(s) < thr) (включая unordered) → 0; fall → 1.
 *     Литеральный перенос: `acos((double)s)` C-рантайма против порога
 *     `0.3490654230117798` (= (double)float 0x3eb2b8b4 из 0x4ef4b8). acos игры
 *     (0x4d1950) — отдельная реализация CRT; её 1-ULP-поведение на границе НЕ
 *     проверено (recon §9) — открытый пункт: побитовое сравнение после захвата
 *     v16 его и закроет. NaN → 0 через те же маски.
 *
 * Запись `FST [p1-слот]` в тройку вызывающего НЕ моделируется: после `ret 0xc`
 * тройка `result+4..0xc` мёртва (recon §6 — log-функции получают слоты ВЫШЕ
 * неё), значимый выход листа — только AL.
 *
 * Чтения: [band+0x20], [band+0x24], elem[0]+0x00/+0x08, elem[count−2]+0x00/+0x08,
 * elem+0x14 (LDPos → 0x456b20 читает pos+0x0c/+0x10, [pos+0x1c] и
 * level+0x20/+0x24/+0x28). Записей в игру нет — функция чистая.
 *
 * Офлайн: claim выпускается телом, но 0x45b3e0 НЕ в b0_ported_leaf_va — его вход
 * (`[elem+0x14]` → LDPos → scale уровня) ни один захват v12..v15 не пишет; сайты
 * 18/22 идут через b0_band_target_moved (реплей натива, см. ниже). */
int s6_band_target_moved(const void *band_, float p1, int dead_arg, float p3)
{
    const unsigned char *b = (const unsigned char *)band_;
    unsigned begin, end;
    int count;
    float o0[2], o1[2];
    float Lx0, Ly0, Lx1;
    double Yn, dot;
    float s;
    const unsigned char *e0, *en1, *en2;
    LDPos *pos;
    int ret = 0;

    memcpy(&begin, b + B0_BAND_BEGIN_OFF, 4);           /* 0x45b3e6 */
    if (begin == 0) {                                   /* 0x45b3e9/0x45b3eb */
        goto claim;
    }
    memcpy(&end, b + B0_BAND_END_OFF, 4);               /* 0x45b3f1 */
    count = (int)(end - begin) / B0_BAND_ELEM;          /* 0x45b3f4..0x45b407, trunc */
    if ((unsigned)count < 2u) {                         /* 0x45b40a JB — беззнаково */
        goto claim;
    }

    /* Адресация scaled-index натива (3·count → *8): 32-битное заворачивание. */
    e0  = (const unsigned char *)(size_t)begin;
    en1 = (const unsigned char *)(size_t)
          (begin + (unsigned)B0_BAND_ELEM * (unsigned)count);       /* elem[count−1]+0x18 */
    en2 = (const unsigned char *)(size_t)
          (begin + (unsigned)B0_BAND_ELEM * (unsigned)(count - 2)); /* elem[count−2] */

    memcpy(&pos, e0 + 0x14, sizeof(pos));               /* 0x45b419 elem[0]+0x14 */
    s6_grid_scale_compute(pos, o0);                     /* 0x45b41c CALL 0x456b20 */
    Lx0 = (float)((double)o0[0] + (double)b0_f32(e0 + 0x00)); /* 0x45b421..0x45b42e */
    Ly0 = (float)((double)o0[1] + (double)b0_f32(e0 + 0x08)); /* 0x45b432..0x45b439 */

    memcpy(&pos, en1 - 4 + 0x14, sizeof(pos));          /* 0x45b461 elem[count−1]+0x14 */
    s6_grid_scale_compute(pos, o1);                     /* 0x45b465 CALL 0x456b20 */
    Lx1 = (float)((double)o1[0] + (double)b0_f32(en2 + 0x00)); /* 0x45b48c..0x45b49e */
    Yn  = (double)o1[1] + (double)b0_f32(en2 + 0x08);   /* 0x45b4be..0x45b4c5, FSTP нет */

    dot = ((double)Lx0 - (double)p1) * ((double)Lx0 - (double)Lx1)
        + ((double)Ly0 - (double)p3) * ((double)Ly0 - Yn); /* 0x45b4c9..0x45b4ef */
    if (dot < 0.0) {                                    /* gate1 0x45b4fb..0x45b506 */
        goto claim;
    }
    s = (float)dot;                                     /* 0x45b4f1 FST m32 */
    if (!(acos((double)s) < 0.3490654230117798)) {      /* gate2 0x45b508..0x45b51c */
        goto claim;
    }
    ret = 1;                                            /* 0x45b51e mov al,1 */

claim:
    {
        int av[3];

        av[0] = b0_float_bits(p1);      /* [E+4]: сайт кладёт triple dword'ами */
        av[1] = dead_arg;               /* [E+8]: мёртвый, но в записи есть */
        av[2] = b0_float_bits(p3);      /* [E+0xc] */
        B0_LEAF_CLAIM(B0_LEAF_BAND_TARGET, band_, ret, av, 3);
    }
    return ret;
}

/* Диспетчер листа на сайтах 18/22 (0x44c554 B63, 0x44c666 B70). Порт
 * исполняется только там, где арена несёт модель геометрии ленты — тот же гейт,
 * что у 0x45b150: `[elem+0x14]` → LDPos → scale-триплет уровня в захватах
 * v12..v15 не записан, поэтому реплеится записанный натив. Живой билд всегда
 * идёт в порт. */
static int b0_band_target_moved(void *band, float p1, int dead_arg, float p3)
{
#ifdef BATTLE_REPLAY
    if (!bdd0_band_geometry_modelled()) {
        return native::band_target_moved(band, p1, dead_arg, p3);
    }
#endif
    return s6_band_target_moved(band, p1, dead_arg, p3);
}

/* --- Порт листа 0x45c6f0 (read_pos_from_ctx) ---------------------------- *
 * Листинг `0x45c6f0..0x45c71e` (сверено с двоичным кодом установки, REA):
 *   MOV EAX,[ECX]        ; 0x45c6f0 — EAX = *(this) = game_ctx
 *   XOR ECX,ECX          ; 0x45c6f2 — ECX обнуляется: `CMP EAX,ECX` без этой
 *                        ;   инструкции читать нельзя, `this` уже потерян
 *   CMP EAX,ECX; JNZ     ; 0x45c6f4/0x45c6f6 — нулевая ветка ⟺ ctx == 0
 *   (нулевая ветка) out[0..2] = 0; RET 4 (0x45c704); EAX = out
 *   0x45c707: ECX = [EAX+0xc] — указатель на тройку; [ECX], [ECX+4], [ECX+8]
 *             → out; RET 4; EAX = out.
 * Нулевая ветка — FIDELITY (не защита): в захватах все 108 вызовов с ai_ctx == 0
 * записали w = [0,0,0]. Значимый результат — записанная тройка, поэтому claim
 * идёт out-каналом (B0_LEAF_CLAIM_W).
 * Второй указатель (ECX = [EAX+0xc]) натив НЕ проверяет — на NULL он упал бы;
 * это единственный незахваченный вход (куча ctx), поэтому guard `p == NULL`
 * ниже — ТОЛЬКО для replay и fidelity не изображает. */
static void *b0_read_pos_from_ctx(void *this_, float *out)
{
    void *ctx = NULL;
    int w[3];

    w[0] = w[1] = w[2] = 0;
    if (this_ != NULL) {
        ctx = *(void **)this_;                      /* 0x45c6f0 MOV EAX,[ECX] */
    }
    if (ctx != NULL) {                              /* 0x45c6f4 CMP EAX,ECX */
        void *p = *(void **)((unsigned char *)ctx + 0xc);  /* 0x45c707 */

        if (p != NULL) {            /* guard replay-only: см. шапку */
            memcpy(w, p, 12);                       /* 0x45c70a..0x45c71b */
        }
    }
    if (out != NULL) {
        memcpy(out, w, 12);
    }
    {
        int a[1];

        a[0] = (int)(size_t)out;                    /* единственный арг — out */
        B0_LEAF_CLAIM_W(B0_LEAF_READ_POS, this_, out, a, 1, w, 3);
    }
    return out;                                     /* 0x45c6f8/0x45c70c: EAX=out */
}

/* --- Порт листа 0x403e80 (pos3_equal) ----------------------------------- *
 * Листинг `0x403e80..0x403eb5` (53 Б, сверено с двоичным кодом установки):
 *   FLD  dword [ECX]        ; 0x403e80 — a[0]
 *   MOV  EDX,[ESP+4]        ; 0x403e82 — EDX = b (единственный стек-аргумент)
 *   FCOMP dword [EDX]       ; 0x403e86
 *   FNSTSW AX; TEST AH,0x44 ; 0x403e88..0x403e8a — C2|C3
 *   JP   0x403eb1           ; 0x403e8d — «не равно» → XOR EAX,EAX; RET 4
 *   … те же 8 инструкций для пар +4 (0x403e8f) и +8 (0x403e9c)
 *   0x403ea9: MOV EAX,1; RET 4 — все три пары равны.
 * ABI (сверено по сайту 0x44c423 и телу): `int __thiscall(ECX = a,
 * stack = b)`, `RET 4` — один стек-аргумент, второй указатель в ECX; результат
 * в EAX, вызывающий тестирует AL (`TEST AL,AL` в порте — `!eq`). Не cdecl с
 * двумя аргументами: на стеке лежит ровно один (`RET 4`), второй берётся из
 * ECX через `FLD [ECX]`.
 * Маска 0x44 (C2|C3) тестируется через PF: JP взят при чётном числе битов
 * результата, т.е. при (C3,C2) ∈ {(0,0) — a>b и a<b, (1,1) — unordered};
 * JNP остаётся только на (1,0) = равенство. Значит JP взят ⟺ NOT(a == b),
 * unordered считается НЕравным — то же правило, что у b0_gate_44_jp (B6..B8),
 * поэтому 1 — только при почленном равенстве всех трёх пар.
 * Представление — float (24-битная мантисса), как у m32-операндов натива
 * (`FLD m32`/`FCOMP m32`): продвижение к double точно и на равенство не влияет,
 * но сравнение в double не отражало бы исходную разрядность; для NaN `==` ложно
 * ⇒ `!(x == y)` даёт 0 — совпадает с «unordered = не равно». /fp:precise даёт
 * IEEE-семантику `==` (без предположения «NaN нет»).
 * Указатели на сайте 0x44c423 не NULL (out = frame+0x20, b = result+4); натив
 * их не проверяет — guard не добавлен (fidelity). */
static int b0_pos3_equal(const void *a, const void *b)
{
    int i;
    int eq = 1;

    for (i = 0; i < 3; i++) {
        float x, y;

        memcpy(&x, (const unsigned char *)a + i * 4, sizeof(x)); /* FLD m32 */
        memcpy(&y, (const unsigned char *)b + i * 4, sizeof(y)); /* FCOMP m32 */
        if (!(x == y)) {                /* TEST AH,0x44 / JP → not_equal */
            eq = 0;
            break;
        }
    }
    {
        int arg[1];

        arg[0] = (int)(size_t)b;        /* единственный стек-аргумент листа */
        B0_LEAF_CLAIM(B0_LEAF_POS3_EQUAL, a, eq, arg, 1);
    }
    return eq;                          /* 0x403ea9 MOV EAX,1 / XOR EAX,EAX */
}

/* --- Порт листа 0x456b60 (chain_head) ----------------------------------- *
 * Перенесён в battle/grid.cpp как `s6_grid_chain_head`: единственный
 * вызывающий — порт 0x458050 (сайт 0x458142), а тот живёт в grid.cpp. Claim
 * листа (B0_LEAF_CHAIN_HEAD) выпускает сама функция, форма — k_leaf_abi
 * (nargs = 1, arg — world, ret — указатель). */

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

/* --- R12 B82: `458050` (grid-якорь) ------------------------------------- *
 * Тело портировано (`s6_grid_anchor_lookup`, battle/grid.cpp). Аргументы сайта
 * 0x44c7da: ECX = уровень сетки = `[[unit+0xe0]+0x44]+0x238`; на стеке ровно
 * четыре dword'а 16-Б temp'а `frame+0x2c..0x38` (pos-тройка B5/B76 + слот
 * +0x38), затем `&frame+0x3f` (байт-флаг) и unit — порядок = ABI порта
 * {float x, float y_metric, float y_coord, void *node, uchar *flag, void *world}
 * (re/leaf_458050.md; в §2 артефакта слоты S+0x10..S+0x18 сдвинуты на один —
 * см. разбор ABI в grid.cpp).
 * Запись этой пары — `bdd0_leaf` сайта 0x44c7da (лист 0x458050), не
 * `bdd0_native`: поток листьев двигает либо запрос `native::grid_anchor`,
 * либо порт. Claim тело 0x458050 НЕ выпускает (его нет в b0_ported_leaf_va:
 * границу записи держит сам сайт 0x44c7da), но ABI-строка у листа есть
 * (k_leaf_abi в checks_builtin.cpp: nargs 6, ptr[4]/ptr[5], ret_ptr).
 * Гейт: входы порта — объект сетки в куче игры (scale-триплет, size_x/size_y,
 * rows, cells, ячейки с цепочками). В захвате v12..v15 его НЕТ (есть только
 * указатель `bdd0_in.tree`; арена материализует под него нулевую область
 * 0x40 Б — «дальше тело его не разыменовывает», arena.cpp). Порт на такой
 * области упирается в гейт `size_x <= xi` и возвращает NULL там, где натив
 * вернул якорь: тик ушёл бы в B83/R13 вместо R14 (проверено принудительным
 * включением — fail≠0 на cap_v15). Грабля: порт 458050 на нулевой tree-области
 * → NULL-якорь → ветка R13/R16 вместо R14 (docs/PITFALLS). Поэтому офлайн порт
 * исполняется только если арена несёт модель сетки; иначе реплеится
 * записанный натив, и сводка печатает `grid_anchor_replayed` — подстановка
 * видима, а не тиха (как `eee0_synth`). В живом билде ветки нет: там всегда
 * порт. */
static void *b0_grid_anchor(void *grid, int x_bits, int y_metric_bits,
                            int y_coord_bits, int node, int *flag, void *world)
{
    float x, y_metric, y_coord;

#ifdef BATTLE_REPLAY
    if (!bdd0_grid_level_modelled()) {
        return native::grid_anchor(grid, x_bits, y_metric_bits, y_coord_bits,
                                   node, flag, world);
    }
#endif
    memcpy(&x, &x_bits, sizeof(x));
    memcpy(&y_metric, &y_metric_bits, sizeof(y_metric));
    memcpy(&y_coord, &y_coord_bits, sizeof(y_coord));
    return s6_grid_anchor_lookup((GridLevel *)grid, x, y_metric, y_coord,
                                 (void *)(size_t)(unsigned)node,
                                 (unsigned char *)flag, world);
}

/* --- R17: диспетчер `45eee0` -------------------------------------------- *
 * Сигнатура натива: `int __thiscall(ECX = BattleAI*, stack = rec)`, `ret $4`.
 * Гейт — `s6_port_enabled()` (target.cpp; тот же, что у site6): включён →
 * порт Фазы 4 `s6_unit_ai_tick`, выключен → нативный `45eee0` через чистую
 * thiscall-обёртку `native::unit_ai_tick` (НЕ накед-хук/трамплин site6 —
 * тот ставится только под LDHOOK_SITE6 и заменяет вход функции целиком).
 * Аргументы — ровно те, что уже стоят в call-site порта: ctx = unit+0x138,
 * rec = &per-tick записи (frame+0xa4). */
static int b0_45eee0_dispatch(void *ctx, void *rec)
{
    if (s6_port_enabled()) {
        return s6_unit_ai_tick((BattleAI *)ctx, rec);
    }
    return native::unit_ai_tick(ctx, rec);
}

static int b0_body_r1_r2(void *unit_, unsigned char *frame)
{
    unsigned char *unit = (unsigned char *)unit_;
    /* EBP в нативе сдвигается `ADD EBP,0x18` в cursor-блоках и НЕ
     * восстанавливается (снимает только эпилогный POP EBP). Адресация после
     * сдвига идёт через unit_x (= EBP); сайты: 0x44c035 (R3 B21), 0x44c197
     * (R5 B30), далее 0x44c9a7 (R15 B90). */
    unsigned char *unit_x = unit;
    unsigned char snap_buf[0x20];   /* B2: снимок 0x20 Б (натив: [esp] после sub 0x20) */
    unsigned char msg_dst[0x40];    /* B15/B24: копия msg-объекта (натив: esp после sub 0x3c) */
    float *out = (float *)(frame + 0x20);   /* B6: out-тройка 45c6f0 (= frame+0x20) */
    /* Один буфер out на три сайта 45c6f0: в нативе слот один и тот же —
     * B6/B76 `lea 0x20(%esp)` (0x44be94/0x44c6f5), B56 `lea 0x24(%esp)`
     * (0x44c415), но там уже лежит push `result+4` (0x44c414), т.е. адрес
     * снова frame+0x20. Каждый сайт потребляет тройку сразу (сравнения B6/B56,
     * копия в frame+0x2c в B76), поэтому одна переменная эквивалентна. */
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
    float band_triple[3];           /* R8 B60: тройка из result+4..0xc (натив: [esp] после sub 0xc) */
    float gate;                     /* R8 B60: ST(0) 45b150 (float-гейт ленты) */
    float world_triple[3];          /* R11 B78: тройка под vtable+0x38 (натив: [esp] после sub 0xc) */
    float r14_tri[3];               /* R14 B88: тройка из возврата 45e1d0 (0x44c8cd) */
    void *tail_str;                 /* R11 B73: EAX = строка из 413060 (вход 49ad70) */
    int edi;                        /* R11 B77: `XOR EDI,EDI` (модель регистра, читают R12/R14) */
    int r19_state3;                 /* R19 B126: `CMP [EAX],3` (флаг до записи mode-флага) */
    void *grid_tree;                /* R12 B82: [[unit+0xe0]+0x44]+0x238 (две независимые deref) */
    void *grid_recv;                /* R12 B82: вторая deref = ECX приёмника 458050 */
    void *anchor;                   /* R12 B82: возврат 458050 (слот record+0x10) */

    __try {

    /* B0 (0x44bdf5..0x44bdf8) — вход после нативного пролога: param_1 == 0 →
     * пропуск B1/B2 (0x44bdf7 je 0x44be58). */
    if (B0_TST(0, (*(const unsigned char *)(frame + 0x15c) == 0)))  {
        goto b3;                                        /* 0x44bdf7 */
    }

    /* B1 (0x44bdf9..0x44be0a) — гейт пригодности тика: `[unit+0x114] > 0.0f`. */
    if (B0_TST(1, (b0_gate_41_jnz(*(const float *)(unit + 0x114), 0.0f))))  {
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
B0_LBL(0);
    /* B3/B4 (0x44be58..0x44be69) — флаг `[unit+0x118]` → bit3 слота param_2. */
    if (B0_TST(2, (*(const unsigned char *)(unit + 0x118) != 0)))  {
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
        if (B0_TST(3, (force != 0)))  {
            goto b12;                                   /* 0x44be92 jne 0x44bf07 */
        }
    }

    /* B6 (0x44be94..0x44bec1) + B7/B8 (0x44bec2..0x44bedb) — позиция из ctx
     * должна совпасть с result+4/+8/+0xc: три x87-сравнения, `jp` → B12.
     * Порядок: 45c6f0 → 442570(0) → первое сравнение. */
    b0_read_pos_from_ctx(unit + 0x138, out);            /* 0x44be9e..0x44bea1 */
    res = native::game_state_get(unit, 0);              /* 0x44bea6..0x44beac */
    if (B0_TST(4, (b0_gate_44_jp(*(const float *)((const unsigned char *)res + 4), out[0]) || b0_gate_44_jp(*(const float *)((const unsigned char *)res + 8), out[1]) || b0_gate_44_jp(*(const float *)((const unsigned char *)res + 12), out[2]))))  {
        goto b12;                           /* 0x44bec0 / 0x44becd / 0x44beda */
    }

    /* B9 (0x44bedc..0x44bee3) — код тика == 1. */
    if (B0_TST(5, (*(const int *)(unit + 0x170) != 1)))  {
        goto b12;                                       /* 0x44bee3 jne 0x44bf07 */
    }

    /* B10/B11 (0x44bee5..0x44bf06) — голова ленты (дважды) и её длина;
     * count > 1 → ранний выход «успех» (B148, R22). */
    head = b0_band_head(unit + 0x138);                  /* 0x44bee5..0x44bee7 */
    if (B0_TST(6, (head == NULL)))  {
        goto b12;                                       /* 0x44beec/0x44beee je */
    }
    head = b0_band_head(unit + 0x138);                  /* 0x44bef0..0x44bef2 */
    count = b0_band_count_claim(head);                  /* 0x44bef7..0x44bef9 */
    if (B0_TST(7, (count > 1)))  {
        goto b148_r22;                                  /* 0x44befe/0x44bf01 jg 0x44d02e */
    }

b12:
B0_LBL(1);
    /* B12 (0x44bf07..0x44bf18) — фаза battle должна быть 3. */
    res = native::game_state_get(unit, 0);              /* 0x44bf07..0x44bf0b */
    if (B0_TST(8, (*(const int *)res != 3)))  {                       /* 0x44bf10/0x44bf13 jne 0x44c08b */
        goto b22;
    }

    /* B13 (0x44bf19..0x44bf2d) — `phase_time <= [unit+0xfc]` (упорядоченное):
     * jnp взят → B15; иначе ( больше или unordered) → B14. */
    native::phase_time(unit, &phase);                   /* 0x44bf1e call *0x2c(%eax) */
    if (B0_TST(9, (b0_gate_41_jnp(phase, *(const float *)(unit + 0xfc)))))  {
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
        if (B0_TST(10, ((unsigned char)native::string_obj_cmp(v, player) == 0)))  {  /* 0x44bf43..0x44bf4c */
            goto b22;                                   /* 0x44bf4e je 0x44c08b */
        }
    }

b15:
B0_LBL(2);
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
    if (B0_TST(11, (native::player_get(unit, *(int *)(frame + 0)) == NULL)))  {   /* 0x44bfb4 */
        goto b20;                                       /* 0x44bfb6 je 0x44c017 */
    }
    goto b16;                                           /* fall-through 0x44bfb8 */

b22:
B0_LBL(3);
    /* 0x44c08b..0x44c09c — B22 (R3): общий вход ветки `state != 3` и провала
     * B14. `442570(unit,0)`; `[result] == 3` → B31 (R5), иначе fall → R4. */
    res = native::game_state_get(unit, 0);              /* 0x44c08f */
    if (B0_TST(12, (*(const int *)res == 3)))  {                       /* 0x44c094/0x44c097 */
        goto b31;                                       /* je 0x44c1ed → R5 */
    }
    goto b23;                                           /* fall 0x44c09d → R4 */

b23:
B0_LBL(4);
    /* B23 (0x44c09d..0x44c0b5) — вход ветки `state != 3` (fall B22). EBP на
     * этом пути не сдвинут (cursor-блок B21 не исполнялся), поэтому EBP-
     * адресация идёт через unit_x == unit.
     * `phase_time` (vtable+0x2c) vs `[unit+0xfc]`; `TEST AH,0x41; JP` —
     * взято ⟺ NOT(phase ≤ threshold) (больше или unordered) → цепочка
     * state==3 (B31, R5). Направление обратно гейту B13 (`JNP`). */
    native::phase_time(unit_x, &phase);                 /* 0x44c09d..0x44c0a2 */
    if (B0_TST(13, (b0_gate_41_jp(phase, *(const float *)(unit_x + 0xfc)))))  {
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
    if (B0_TST(14, (native::player_get(unit_x, *(int *)(frame + 0)) == NULL)))  {
        goto b29;                                       /* 0x44c118 je 0x44c179 */
    }

    /* B25 (0x44c11a..0x44c134) — цепочка ×1 (те же 3 листа, что R3 B16),
     * NULL-гейт → B29. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c11a..0x44c12d */
    if (B0_TST(15, (flags_obj == NULL)))  {
        goto b29;                                       /* 0x44c134 je 0x44c179 */
    }

    /* B26 (0x44c136..0x44c151) — цепочка ×2, `TESTB $2,(%eax)`: бит → B28. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c136..0x44c149 */
    if (B0_TST(16, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b28;                                       /* 0x44c151 jne 0x44c170 */
    }

    /* B27 (0x44c153..0x44c16e) — цепочка ×3, `TESTB $4,(%eax)`: бит снят → B29. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c153..0x44c166 */
    if (B0_TST(17, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b29;                                       /* 0x44c16e je 0x44c179 */
    }

b28:
B0_LBL(5);
    /* B28 (0x44c170..0x44c178) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44c170..0x44c174 */

b29:
B0_LBL(6);
    /* B29 (0x44c179..0x44c183) — msg-флаг `unit+0x34`: 0 → join; иначе
     * fall в R5 B30 (`0x44c184`, cursor-блок B). */
    if (B0_TST(18, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b135_join;                                 /* 0x44c17e je 0x44cee6 */
    }
    goto b30;                                           /* fall 0x44c184 → R5 */

b30:
B0_LBL(7);
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
B0_LBL(8);
    /* B31 (0x44c1ed..0x44c1fe) — вход из B22 (R3) и B23 (R4): там EBP не
     * сдвинут (cursor-блоки B21/B30 на этих путях не исполнялись), поэтому
     * unit_x == unit. Состояние ==3 → B42 (band-loop, R7). */
    res = native::game_state_get(unit_x, 0);            /* 0x44c1ed..0x44c1f1 */
    if (B0_TST(19, (*(const int *)res == 3)))  {                       /* 0x44c1f6 */
        goto b42;                                       /* 0x44c1f9 je 0x44c344 */
    }

    /* B32 (0x44c1ff..0x44c210) — состояние ==0 → B42. */
    res = native::game_state_get(unit_x, 0);            /* 0x44c1ff..0x44c203 */
    if (B0_TST(20, (*(const int *)res == 0)))  {                       /* 0x44c208 */
        goto b42;                                       /* 0x44c20b je 0x44c344 */
    }

    /* B33 (0x44c211..0x44c229) — vtable+0x2c против литерала `[0x4eede8]` =
     * 3.0f (0x40400000, bit-exact): `TEST AH,0x41; JP` ⟺ NOT(phase ≤ 3.0f)
     * (больше или unordered) → B42; иначе fall → B34 (R6). */
    native::phase_time(unit_x, &phase);                 /* 0x44c216 */
    if (B0_TST(21, (b0_gate_41_jp(phase, 3.0f))))  {                   /* 0x44c219..0x44c224 */
        goto b42;                                       /* 0x44c224 jp 0x44c344 */
    }
    goto b34;                                           /* fall 0x44c22a → R6 */

b34:
B0_LBL(9);
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
    if (B0_TST(22, (grid == 0)))  {                    /* 0x44c27d `test; jbe` (unsigned ⇒ ==0) */
        goto b42;
    }

    /* B35 (0x44c283..0x44c29c) — assert/log только при grid > 0. */
    native::log_assert(unit_x, native::literal(0x51cc50), 0xa); /* 0x44c283..0x44c28c */
    /* 0x44c296 — геттер без push: callee читает [esp+4] = [frame] (модель B15). */
    if (B0_TST(23, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b40;                                       /* 0x44c29b je 0x44c2fc */
    }

    /* B36 (0x44c29d..0x44c2b8) — цепочка ×1, NULL → B40. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c29d..0x44c2b5 */
    if (B0_TST(24, (flags_obj == NULL)))  {
        goto b40;                                       /* 0x44c2b7 je */
    }

    /* B37 (0x44c2b9..0x44c2d5) — цепочка ×2, `TESTB $2`: бит → B39. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c2b9..0x44c2d1 */
    if (B0_TST(25, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b39;                                       /* 0x44c2d4 jne */
    }

    /* B38 (0x44c2d6..0x44c2f2) — цепочка ×3, `TESTB $4`: бит снят → B40. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c2d6..0x44c2ee */
    if (B0_TST(26, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b40;                                       /* 0x44c2f1 je */
    }

b39:
B0_LBL(10);
    /* B39 (0x44c2f3..0x44c2fb) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44c2f3..0x44c2f7 */

b40:
B0_LBL(11);
    /* B40 (0x44c2fc..0x44c306) — msg-флаг `unit+0x34`: 0 → выход 0x44d061
     * (out-guard param_3 = 0, AL = 0); иначе fall → B41 (R7). */
    if (B0_TST(27, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        void *p3 = *(void **)(frame + 0x164);           /* 0x44d061 out-guard */
        if (B0_TST(28, (p3 != NULL)))  {
            *(int *)p3 = 0;
        }
        B0_EXIT(128);
        return 0;                                       /* 0x44d072 xor al,al */
    }
    B0_EXIT(128);
    goto b41;                                           /* fall 0x44c307 */

b41:
B0_LBL(12);
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

        native::string_ctor(str_a, native::literal(0x4eed90)); /* 0x44c307..0x44c310 */
        /* 0x44c31a `ADD EBP,0x18` — сдвиг ровно здесь, не откатывается
         * (в теле нет `SUB EBP`, снимает только эпилогный `POP EBP`); все
         * последующие `[ebp+…]` — через unit_x. */
        unit_x += 0x18;                                 /* 0x44c31a */
        ebx = 6;                                        /* 0x44c322 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c328 (SEH-слот) */
        tail_str = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c32f */
        frame[0x154] = 7;                               /* 0x44c337 */
    }
    goto b73_cursor_tail;                               /* 0x44c33f jmp 0x44c6b6 */

b42:
B0_LBL(13);
    /* B42 (0x44c344..0x44c34e) — вход band-loop. Сюда ведут B31/B32 (0x44c1f9/
     * 0x44c20b), B33 (0x44c224 jp) и B34 `jbe` (0x44c27d) — на ВСЕХ этих путях
     * `ADD EBP,0x18` не исполнялся (сдвиги B21/B30 уходят в `0x44cee6`, B41 —
     * в `0x44c6b6`), поэтому здесь EBP НЕ сдвинут: unit_x == unit.
     * EBX на входе — значение R1 B5 (`[frame+0x160] & 8` @0x44be6d/0x44be74);
     * между B5 и R7 других записей EBX нет (проверено листингом), а param_2
     * слот дальше не пишется ⇒ пересчёт эквивалентен.
     * `[esp+0x15c]` = param_1 (cmd-флаг): ≠0 → B49. */
    ebx = *(const unsigned int *)(frame + 0x160) & 8u;  /* = EBX из B5 */
    if (B0_TST(29, (*(const unsigned char *)(frame + 0x15c) != 0)))  {
        goto b49;                                       /* 0x44c34d jne 0x44c393 */
    }

    /* B43/B44/B45 (0x44c34f..0x44c364) — ctx `unit+0x138`; guard `TEST` до
     * deref (NULL → 0), бит3 → B46 (clear cmd), иначе сразу loop head. */
    {
        void *ctx = *(void **)(unit_x + 0x138);         /* 0x44c34f */
        unsigned int v = 0;

        if (B0_TST(30, (ctx != NULL)))  {
            v = *(const unsigned int *)ctx;             /* 0x44c35f */
        }
        if (B0_TST(31, ((v & 8u) == 0)))  {
            goto b47;                                   /* 0x44c363 je 0x44c36d */
        }
    }

b46:
B0_LBL(14);
    /* B46 (0x44c365) — байт param_1 = 0 (clear cmd-флаг перед loop head).
     * Метка = точка back-edge R10 B70 (`0x44c66d je 0x44c365`): повторный вход
     * в единственный цикл тела идёт СЮДА, т.е. всегда со снятым cmd-флагом.
     * unit_x на любом входе здесь не сдвинут (см. b42). */
    *(unsigned char *)(frame + 0x15c) = 0;

b47:
B0_LBL(15);
    /* B47 (0x44c36d..0x44c37b) — курсор ленты (45c730, ECX = &ctx); NULL →
     * B75 (R11). */
    head = native::band_cursor(unit_x + 0x138);         /* 0x44c36f */
    if (B0_TST(32, (head == NULL)))  {
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
B0_LBL(16);
    /* B49 (0x44c393..0x44c39a) — EBX ≠ 0 → flags-join B77 (R11). */
    if (B0_TST(33, (ebx != 0)))  {
        goto b77_join;                                  /* 0x44c395 jne 0x44c715 */
    }

    /* B50..B52 (0x44c39b..0x44c3b4) — повторный ctx-гейт: заново
     * `LEA ESI,[ebp+0x138]` и deref-guard; бит3 → B77. */
    {
        void *ctx = *(void **)(unit_x + 0x138);         /* 0x44c39b */
        unsigned int v = 0;

        if (B0_TST(34, (ctx != NULL)))  {
            v = *(const unsigned int *)ctx;             /* 0x44c3ab */
        }
        if (B0_TST(35, ((v & 8u) != 0)))  {
            goto b77_join;                              /* 0x44c3af jne 0x44c715 */
        }
    }

    /* B53 (0x44c3b5..0x44c3c3) — голова ленты (45c370); NULL → B77. */
    head = b0_band_head(unit_x + 0x138);                /* 0x44c3b7 */
    if (B0_TST(36, (head == NULL)))  {
        goto b77_join;                                  /* 0x44c3be je 0x44c715 */
    }

    /* B54 (0x44c3c4..0x44c3ec) — повторный 45c370 → 45af50; длина ≤1
     * (знаковое `JLE`) → B77; затем 442570(ECX=EBP=unit,0) и фаза == 3,
     * иначе B77; fall → B55 (R8). */
    head = b0_band_head(unit_x + 0x138);                /* 0x44c3c6 */
    count = b0_band_count_claim(head);                  /* 0x44c3cd */
    if (B0_TST(37, (count <= 1)))  {
        goto b77_join;                                  /* 0x44c3d5 jle 0x44c715 */
    }
    res = native::game_state_get(unit_x, 0);            /* 0x44c3df */
    if (B0_TST(38, (*(const int *)res != 3)))  {
        goto b77_join;                                  /* 0x44c3e7 jne 0x44c715 */
    }
    goto b55;                                           /* fall 0x44c3ed → R8 */

/* --- R11: выходы loop + flags-join (`0x44c67e..0x44c74c`, LOGIC) -------- *
 * unit_x на входе каждой части:
 *  - B72 (fall B71): EBP НЕ сдвинут; сам B72 делает `ADD EBP,0x18` (0x44c691)
 *    и передаёт в 413060 сдвинутый unit+0x18;
 *  - B73/B74: входы — fall B72 и jmp B41/B62/B65/B69, т.е. на ВСЕХ путях
 *    EBP уже сдвинут (+0x18). Адресация B73/B74 — только ESP-слоты
 *    (`frame+0x154/0x20/0x10`, выход `0x44d061` читает `frame+0x164`),
 *    поэтому сдвиг на них не влияет;
 *  - B75/B76 (R7 B47 JZ / B48 jmp): EBP НЕ сдвинут; B75/B76 тоже только
 *    ESP-слоты (`frame+0x38`, `frame+0x20`, `frame+0x2c..0x34`) + ctx
 *    `unit+0x138` (ESI, снят в B45 при несдвинутом EBP);
 *  - B77..B81: входы R7 B49/B52/B53/B54 (+B76 fall), R8 B55, R9 B66 — на
 *    всех EBP НЕ сдвинут, поэтому `[EBP]` = vptr unit и ECX = unit. */
b73_cursor_tail:
B0_LBL(17);
    /* B73 (0x44c6b6..0x44c6ca) — общий cursor-tail: EAX = строка из 413060
     * (`MOV ECX,EAX; CALL 49ad70` — c_str) → `PUSH EAX; CALL IAT 0x4ec064`
     * (OutputDebugStringA, stdcall снимает аргумент) → `MOV BL,[esp+0x154]`
     * (ESP здесь = frame: байт trylevel = низ EBX). BL по путям: 6 (B41),
     * 0xa (B62), 0xc (B65), 0xe (B69), 0x10 (B72). fall → B74. */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44c6b8..0x44c6be */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44c6c4 mov %bl,0x154(%esp) */
    goto b74_cursor_tail;                               /* fall 0x44c6cb */

b75:
B0_LBL(18);
    /* B75 (0x44c6ed..0x44c6f4) — anchor `[esp+0x38]` = 0; fall B76. */
    *(unsigned int *)(frame + 0x38) = 0;
    goto b76;                                           /* fall 0x44c6f5 */

b76:
B0_LBL(19);
    /* B76 (0x44c6f5..0x44c714) — `45c6f0(ECX = ESI = &unit+0x138,
     * out = frame+0x20)`; три dword результата → pos-тройка
     * `frame+0x2c/0x30/0x34`. fall → B77. */
    b0_read_pos_from_ctx(unit + 0x138, out);            /* 0x44c6f5..0x44c6fc */
    memcpy(frame + 0x2c, out, 12);                      /* 0x44c701..0x44c711 */
    /* fall → b77_join */

b77_join:
B0_LBL(20);
    /* B77 (0x44c715..0x44c71a) — flags-join. `XOR EDI,EDI`; `CMP EBX,EDI;
     * JNE 0x44c73d`. EDI=0 моделируется локальной (его читают R12 B82 —
     * заполнение record-слотов — и R14: `PUSH EDI` ×4 в 442570). */
    edi = 0;
    if (B0_TST(39, (ebx != (unsigned int)edi)))  {
        goto b80;                                       /* 0x44c719 jne 0x44c73d */
    }

    /* B78 (0x44c71b..0x44c738) — grid-гейт: `MOV EDX,[EBP]` (vptr),
     * `PUSH EDI` (arg4 для 44bbf0), `SUB ESP,0xc` (тройка), `PUSH &тройки`
     * (arg1 геттера), `CALL *0x38(EDX)` с ECX = unit; затем `44bbf0`
     * (ECX = unit) читает тройку+0 как 4 dword-аргумента (arg4 = тот push
     * EDI). Слот `[esp+0x2c]=&тройка` (frame+0x1c) — мёртвая запись (R17
     * берёт &pos-тройки напрямую). Тройка натива неинициализирована; порт
     * зануляет её (как B34). */
    world_triple[0] = world_triple[1] = world_triple[2] = 0.0f;
    (void)native::vcall38_ptr(unit_x, world_triple);     /* 0x44c71b..0x44c72b */
    grid = native::grid_query(unit_x, b0_float_bits(world_triple[0]),
                              b0_float_bits(world_triple[1]),
                              b0_float_bits(world_triple[2]), 0); /* 0x44c72e..0x44c730 */
    if (B0_TST(40, (grid != 0)))  {                    /* 0x44c735/0x44c737 `jbe` (unsigned ⇒ ==0) */
        /* B79 (0x44c739..0x44c73c) — grid ≠ 0: `XOR EBX,EBX` → B81. */
        ebx = 0;
        goto b81;                                       /* 0x44c73b jmp 0x44c744 */
    }

b80:
B0_LBL(21);
    /* B80 (0x44c73d..0x44c743) — EBX := param_2 (`frame+0x160`): единственная
     * запись слота — B4 `orl $8` (CONTRADICTIONS #16). */
    ebx = *(const unsigned int *)(frame + 0x160);

b81:
B0_LBL(22);
    /* B81 (0x44c744..0x44c74c) — `TEST BL,0x8`: бит снят → B102 (R16,
     * sentinel-тройка −10000.0f); иначе fall → B82 (R12, grid-anchor 458050). */
    if (B0_TST(41, ((ebx & 0x8u) == 0)))  {
        goto b102_r16;                                  /* 0x44c74a jz 0x44cb07 */
    }

/* --- R12: grid-req + `458050` anchor + log при NULL (`0x44c74d..0x44c82b`) *
 * EBP на входе НЕ сдвинут: B81 достижим только из R11 (B77 join → B78 →
 * B79/B80), а все `ADD EBP,0x18` (B21/B30/B41/B58/B62/B65/B69/B72) уходят в
 * `0x44cee6` или `0x44c6b6`/`0x44c6cb` — ни один не возвращается в flags-join.
 * Поэтому `[EBP+…]` R12 = `unit_x == unit`. EDI = 0 (B77) и в R12 читается
 * только как 0: `[frame+0x90/+0x94/+0x98] = EDI` и `CMP EAX,EDI` (anchor != 0).
 * ESP на входе R12 = frame (B81 не трогает стек; 458050 снимает свои 0x18). */
    /* B82 (0x44c74d..0x44c7ef) — PathFinder-init по `frame+0xcc` (тот же
     * объект, что приёмник 45e1d0 в R14 и dtor 45e5e0 в R16), затем grid-
     * record `frame+0x88`: {+0 tag=0xeb, +4 unit, +8 tree, +0xc 0, +0x10 anchor}.
     * tree = `[[unit+0xe0]+0x44]+0x238`, читается ДВАЖДЫ независимо: первый
     * результат — record+8, второй — ECX приёмника `458050`. */
    native::pathfinder_init(frame + 0xcc);              /* 0x44c74d..0x44c754 */
    grid_tree = *(void **)(unit_x + 0xe0);              /* 0x44c759 */
    *(unsigned int *)(frame + 0x90) = 0;                /* 0x44c75f (EDI=0) */
    *(unsigned int *)(frame + 0x94) = 0;                /* 0x44c766 (EDI=0) */
    *(unsigned int *)(frame + 0x98) = 0;                /* 0x44c76d (EDI=0) */
    *(unsigned int *)(frame + 0x88) = 0xeb;             /* 0x44c774 */
    *(void **)(frame + 0x8c) = unit_x;                  /* 0x44c77f */
    grid_tree = *(void **)((unsigned char *)grid_tree + 0x44);   /* 0x44c786 */
    grid_tree = *(void **)((unsigned char *)grid_tree + 0x238);  /* 0x44c789 */
    *(void **)(frame + 0x90) = grid_tree;               /* 0x44c794 (esp=frame-4 → rec+8) */
    {
        /* Вторая независимая deref того же выражения → ECX 458050. */
        void *p = *(void **)(unit_x + 0xe0);            /* 0x44c7bc */

        p = *(void **)((unsigned char *)p + 0x44);      /* 0x44c7c2 */
        grid_recv = *(void **)((unsigned char *)p + 0x238);  /* 0x44c7c5 */
    }
    /* `458050` ABI (листинг 0x458050..0x4581f4): `RET 0x18` (6 аргументов),
     * ECX = tree. arg1..arg4 — 16-Б temp: [frame+0x2c..0x38] (pos-тройка из
     * B5/B76 + слот `frame+0x38` = `[unit+0x74]` либо 0 из B75) — это dword'ы
     * 0..3 блока (callee: arg4 → `[+0x238]`, т.е. указатель); arg5 = `&frame+0x3f`
     * — байт-флаг (callee `MOVB $1/$0,(%eax)` @0x4580fd/0x458161); arg6 = unit
     * (callee → `449160`, `0x4580e3`). Слот `frame+0x1c = &блока` (0x44c7d6) —
     * мёртвая запись, порт передаёт аргументы явно (как B34/B78). */
    *(unsigned int *)(frame + 0x154) = 0x12;            /* 0x44c7cb (trylevel) */
    anchor = b0_grid_anchor(grid_recv,
                            *(const int *)(frame + 0x2c),
                            *(const int *)(frame + 0x30),
                            *(const int *)(frame + 0x34),
                            *(const int *)(frame + 0x38),
                            (int *)(frame + 0x3f),
                            unit_x);                /* 0x44c7da */
    *(void **)(frame + 0x98) = anchor;                  /* 0x44c7e1 (и при NULL) */
    if (B0_TST(42, (anchor != NULL)))  {
        goto b88_r14;                                   /* 0x44c7ea jne 0x44c878 */
    }

    /* B83 (0x44c7f0..0x44c80b) — anchor == NULL: log-assert (литерал 0x51cc50,
     * код 0xa), затем vtable+0x40 БЕЗ push: `call` кладёт retaddr, поэтому
     * callee читает свой [esp+4] = верх ESP вызывающего = frame = слот
     * сохранённого EDI пролога (та же модель, что B15/B24/B35). NULL → join. */
    native::log_assert(unit_x, native::literal(0x51cc50), 0xa); /* 0x44c7f0..0x44c7f7 */
    if (B0_TST(43, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b100_join;                                 /* 0x44c806 je 0x44cad2 */
    }

    /* B84 (0x44c80c..0x44c82b) — flags-цепочка (`push 1` → vtable+0x40 →
     * 42da60 → 435d50 = b0_player_flag_chain); NULL → join, иначе fall → B85. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c80c..0x44c81f */
    if (B0_TST(44, (flags_obj == NULL)))  {
        goto b100_join;                                 /* 0x44c826 je 0x44cad2 */
    }
    goto b85_r13;                                       /* fall 0x44c82c → R13 */

b85_r13:
B0_LBL(23);
    /* --- R13 (`0x44c82c..0x44c877`): flags-цепочка tail anchor-NULL пути --- *
     * Единственный вход — fall B84 (`0x44c826 je` не взят), т.е. R13 на входе
     * в точности там, где и R12: `ADD EBP,0x18` не исполнялся (сдвиги
     * B21/B30/B41/B58/B62/B65/B69/B72 уходят из flags-join), поэтому
     * `[EBP+…]` = `unit_x == unit`. EDI = 0 (B77) — в R13 не читается ни в
     * одной инструкции листинга. ESP на входе = frame.
     * B85 (0x44c82c..0x44c847) — цепочка ×1 (`push 1` → vtable+0x40 → 42da60
     * → 435d50 = b0_player_flag_chain); `TESTB $2,(%eax)`: бит взят → B87.
     * NULL-guard в R13 нет: цепочка B84 защищена гейтом `0x44c826`. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c82c..0x44c841 */
    if (B0_TST(45, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b87_r13;                                   /* 0x44c847 jne 0x44c86a */
    }

    /* B86 (0x44c849..0x44c864) — цепочка ×2, `TESTB $4,(%eax)`: бит снят →
     * dtor-join `0x44cad2`; иначе fall → B87. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c849..0x44c85e */
    if (B0_TST(46, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b100_join;                                 /* 0x44c864 je 0x44cad2 */
    }

b87_r13:
B0_LBL(24);
    /* B87 (0x44c86a..0x44c877) — cursor/mode update с аргументом 0x40, затем
     * `JMP 0x44cad2`. Оба входа: B85 JNZ и B86 fall. Все выходы R13 — через
     * dtor-join (AL=0), return-значение порта = 0. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44c86a..0x44c870 */
    goto b100_join;                                     /* 0x44c873 jmp 0x44cad2 */

/* --- R14: core-A — grid-recompute + save/restore состояния и фаза -------- *
 * Единственный вход — R12 B82 `0x44c7ea jne` (anchor != NULL); на этом пути
 * `ADD EBP,0x18` не исполнялся (сдвиги уходят в `0x44cee6`/`0x44c6b6`/`0x44c6cb`),
 * поэтому EBP-адресация R14 идёт через `unit_x == unit`. ESP на входе = frame:
 * 458050 в R12 снимает свои `0x18`, и вся slot-арифметика R14 (0x44c8ed/0x44c8f4/
 * 0x44c8f8, save/restore 0x10..0x18, FSTP 0x1c) сходится только при RET 8 у
 * 45e1d0 — иначе тройка-сохранение и `FLDS 0x1c` R15 B89 разъехались бы.
 * EDI = 0 (B77 `XOR EDI,EDI`), поэтому все четыре `PUSH EDI` → flags = 0.
 * ESI переприсваивается: `LEA ESI,[EBP+0xa4]` @0x44c8cf (ctx больше не нужен);
 * модель — запись/чтение идёт прямо по `unit_x+0xa4..0xac`. */
b88_r14:
B0_LBL(25);
    /* B88 (0x44c878..0x44c96f). */
    r14_tri[0] = r14_tri[1] = r14_tri[2] = 0.0f;
    /* 0x44c878..0x44c88b — world-notify(&slot frame+0xc0), затем vtable+0x40
     * БЕЗ push (callee читает [esp+4] = слот сохранённого EDI пролога — модель
     * B15/B24/B35); возврат геттера НЕ читается (перетирается @0x44c88e). */
    (void)native::vcall38_ptr(unit_x, frame + 0xc0);        /* 0x44c87b..0x44c883 */
    (void)native::player_get(unit_x, *(const int *)(frame + 0)); /* 0x44c88b */
    /* Grid-record `frame+0x88` (R12 заполнил +0x00 tag/+0x04 unit/+0x08 tree/
     * +0x10 anchor): R14 добавляет +0x0c = &pos-тройки, +0x14 = dword
     * `unit+0x12c`, +0x18 = 0. */
    *(void **)(frame + 0x94) = frame + 0x2c;                /* 0x44c89f */
    *(unsigned int *)(frame + 0x9c) =
        *(const unsigned int *)(unit_x + 0x12c);            /* 0x44c8ae */
    *(unsigned int *)(frame + 0xa0) = 0;                    /* 0x44c8bd */
    /* 45e1d0: ECX = &frame+0xcc (тот же PathFinder-объект, что ctor 45f430 в
     * R12 и dtor 45e5e0 в R16), arg1 = &frame+0xc0 (слот world-notify),
     * arg2 = &frame+0x88 (record); RET 8. v17: если арена несёт вход листа
     * (bdd0_path_state_modelled) — тело исполняет порт (s6_grid_recompute),
     * иначе записанный натив. */
    {
        void *pf = b0_path_pf();
        if (pf != NULL && bdd0_path_state_modelled()) {
            s6_grid_recompute(pf, frame + 0xc0, frame + 0x88);
            memcpy(r14_tri, frame + 0xc0, 12);   /* out-тройка порта */
        } else {
            native::grid_recompute(frame + 0xcc, frame + 0xc0, frame + 0x88,
                                   r14_tri);
        }
    }
    /* 0x44c8cd..0x44c8e2 — возврат-тройка → `unit+0xa4..0xac` (без guard). */
    memcpy(unit_x + 0xa4, r14_tri, 12);
    /* 0x44c8e5..0x44c8f8 — pos-тройка `frame+0x2c/+0x30/+0x34` ← `unit+0xa4..
     * 0xac`. Порядок листинга: +0x34 (ESP=frame), затем push EDI и +0x2c/+0x30;
     * это НЕ «мёртвая двойная запись» (разные базы ESP) — слотами позже
     * пользуется R17 `[ESP+0x2c]` как &pos-тройкой пер-тик записи. */
    *(unsigned int *)(frame + 0x34) = *(const unsigned int *)(unit_x + 0xac);
    *(unsigned int *)(frame + 0x2c) = *(const unsigned int *)(unit_x + 0xa4);
    *(unsigned int *)(frame + 0x30) = *(const unsigned int *)(unit_x + 0xa8);
    /* 0x44c8fc..0x44c918 — save: pos-поля состояния (`result+4..0xc`) →
     * `frame+0x10/+0x14/+0x18`. */
    res = native::game_state_get(unit_x, edi);              /* 0x44c8fc */
    memcpy(frame + 0x10, (const unsigned char *)res + 4, 12);
    /* 0x44c91c..0x44c931 — write: `result+4..0xc` ← `unit+0xa4..0xac`. */
    res = native::game_state_get(unit_x, edi);              /* 0x44c91c */
    memcpy((unsigned char *)res + 4, unit_x + 0xa4, 12);
    /* 0x44c934..0x44c93c — vtable+0x2c (float) → FSTP m32 `frame+0x1c`.
     * Гейт по этому значению — уже R15 B89 (`FLDS [frame+0x1c]`, `FCOMPS
     * [unit+0xfc]`, `TEST AH,0x41; JP 0x44caee` = больше или unordered). */
    native::phase_time(unit_x, (float *)(frame + 0x1c));    /* 0x44c939..0x44c93c */
    /* 0x44c940..0x44c95c — restore: `result+4..0xc` ← сохранённые слоты. */
    res = native::game_state_get(unit_x, edi);              /* 0x44c943 */
    memcpy((unsigned char *)res + 4, frame + 0x10, 12);
    /* 0x44c95f..0x44c96a — состояние == 3 → B101 (dtor-join → R17), иначе
     * fall → R15 B89. */
    res = native::game_state_get(unit_x, edi);              /* 0x44c962 */
    if (B0_TST(47, (*(const int *)res == 3)))  {
        goto b101_r16;                                      /* 0x44c96a je 0x44caee */
    }
    goto b89_r15;                                           /* fall 0x44c970 */

b89_r15:
B0_LBL(26);
    /* --- R15: string-клон + `446a80` + цепочки + `448c30` (0x44c970..0x44ca4b) *
     * Единственный вход — fall R14 B88. На этом пути EBP не сдвинут (сдвиги
     * B21/B30/B41/B58/B62/B65/B69/B72 уходят из flags-join), поэтому `[EBP+…]`
     * = `unit_x == unit`. ESP на входе = frame.
     * B89 (0x44c970..0x44c984) — `FLDS [frame+0x1c]` (фаза из FSTP R14),
     * `FCOMPS [unit+0xfc]`, `TEST AH,0x41; JP 0x44caee`: взято ⟺ больше или
     * unordered → B101; fall → B90. */
    if (B0_TST(48, (b0_gate_41_jp(*(const float *)(frame + 0x1c), *(const float *)(unit_x + 0xfc)))))  {
        goto b101_r16;                                      /* 0x44c97f jp 0x44caee */
    }

    /* B90 (0x44c985..0x44c9e5) — msg-блок-клон R3 B15/R4 B24, значения R15:
     * `[frame+0x50]=1`, trylevel `[frame+0x154]=0x13` (байт). Прочее как в
     * B15/B24: msg_ctor(frame+0x4c); 32 Б из `unit+0x64` в frame+0x60; src-
     * указатель в frame+0x1c; [frame+0x80]=0; msg_copy в by-value буфер
     * (натив: esp после `sub 0x3c`); 446a80 (by-value 0x3c, faithful);
     * затем vtable+0x40 без push (аргумент — слот сохранённого EDI).
     * EDI моделируется явно: `LEA EDI,[frame+0x60]` перед копией, `rep movsl`
     * ×8 двигает его на 8 dword → EDI = frame+0x80. Читающих EDI блоков на
     * остатке пути R15 нет; R16 B96/B97/B99/B100 EDI не читают, а их выход —
     * `0x44d072`, поэтому ненулевой EDI до `0x44cb39` (R17, где EDI читается
     * @0x44cb4a..) НЕ доходит: туда ведут только B101/B102 с EDI=0. */
    native::msg_ctor(frame + 0x4c);                     /* 0x44c985..0x44c989 */
    *(unsigned int *)(frame + 0x50) = 1;                /* 0x44c991 */
    edi = (int)(INT_PTR)(frame + 0x60);                 /* 0x44c9a4 lea 0x9c(%esp),%edi */
    memcpy(frame + 0x60, unit_x + 0x64, 32);            /* 0x44c99c..0x44c9ab rep movsl */
    edi += 0x20;                                        /* rep movsl ×8: EDI → frame+0x80 */
    *(void **)(frame + 0x1c) = msg_dst;                 /* 0x44c9b6 */
    frame[0x154] = 0x13;                                /* 0x44c9bb (trylevel, байт) */
    *(unsigned int *)(frame + 0x80) = 0;                /* 0x44c9c3 */
    native::msg_copy(msg_dst, frame + 0x4c);            /* 0x44c9ce */
    native::ai_time_update(unit_x, msg_dst);            /* 0x44c9d3..0x44c9d5 */
    if (B0_TST(49, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b95_r15;                                   /* 0x44c9e2 je 0x44ca45 */
    }

    /* B91 (0x44c9e6..0x44ca01) — цепочка ×1 (`push 1` → vtable+0x40 → 42da60 →
     * 435d50); `TEST EAX` → NULL → B95. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44c9e6..0x44c9f9 */
    if (B0_TST(50, (flags_obj == NULL)))  {
        goto b95_r15;                                   /* 0x44ca00 je 0x44ca45 */
    }

    /* B92 (0x44ca02..0x44ca1e) — цепочка ×2, `TESTB $2,(%eax)`: бит → B94. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44ca02..0x44ca15 */
    if (B0_TST(51, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b94_r15;                                   /* 0x44ca1d jne 0x44ca3c */
    }

    /* B93 (0x44ca1f..0x44ca3b) — цепочка ×3, `TESTB $4,(%eax)`: бит снят → B95. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44ca1f..0x44ca32 */
    if (B0_TST(52, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b95_r15;                                   /* 0x44ca3a je 0x44ca45 */
    }

b94_r15:
B0_LBL(27);
    /* B94 (0x44ca3c..0x44ca44) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44ca3c..0x44ca40 */

b95_r15:
B0_LBL(28);
    /* B95 (0x44ca45..0x44ca4b) — msg-флаг байта `unit+0x34` (EBP не сдвинут):
     * 0 → B97 (R16, out-write, пропуск cursor-блока); иначе fall → B96 (R16,
     * cursor-блок I). Оба выхода — R16. */
    if (B0_TST(53, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b97_r16;                                   /* 0x44ca4a je 0x44cab0 */
    }
    goto b96_r16;                                       /* fall 0x44ca4c */

b96_r16:
B0_LBL(29);
    /* --- R16: cursor-хвост → out-код → dtors (`0x44ca4c..0x44cb38`) ------ *
     * Блоки: B96 `..0x44caaf`, B97/B98 `..0x44cac0`, B99 `..0x44cad1`,
     * B100 `..0x44caed` (join), B101 `..0x44cb06`, B102 `..0x44cb38`.
     *
     * ESP. Вход B96 — fall B95. В отличие от R9..R11 (там ESP блока =
     * frame−0xc), сюда R15 приходит с ESP = frame: B90 `SUB ESP,0x3c` +
     * `PUSH src` снимаются копией `4040f0` (`RET 4` @0x40415a) и
     * `446a80` (`RET 0x3c` @0x446aeb), итого 0x40 = 0x3c+4. Поэтому
     * (esp отсчитывается от frame на каждой инструкции):
     *   0x44ca4c `push lit`      esp frame−4
     *   0x44ca51 `lea 0x14(%esp)` esp frame−4 → frame+0x10  — string A
     *   0x44ca55 `call 49b0f0`   RET 4 ⇒ esp frame
     *   0x44ca5a `lea 0x10(%esp)` esp frame   → frame+0x10  — arg 413060
     *   0x44ca5e `push`          esp frame−4
     *   0x44ca62 `lea 0x24(%esp)` esp frame−4 → frame+0x20  — dst 413060
     *   0x44ca66/0x44ca69 push ×2 ⇒ esp frame−0xc
     *   0x44ca6a `[esp+0x160]`   esp frame−0xc → frame+0x154 (trylevel byte)
     *   0x44ca71 `call 413060` + `add $0xc` ⇒ esp frame
     *   0x44ca7b `[esp+0x154]`   esp frame   → frame+0x154
     *   0x44ca83 `49ad70`, 0x44ca88 push, 0x44ca89 `call *0x4ec064`
     *            (OutputDebugStringA, stdcall снимает аргумент) ⇒ esp frame
     *   0x44ca8f `lea 0x20(%esp)` esp frame   → frame+0x20  — dtor string B
     *   0x44ca9f `lea 0x10(%esp)` esp frame   → frame+0x10  — dtor string A
     * То есть B96 исполняется при ESP = frame, а «S−4» — только транзиент
     * после первого `push`; dtor-слоты +0x20/+0x10 (не +0x1c/+0xc): та же
     * пара, что в B73/B74 (0x44c6cb/0x44c6d4). `[esp+0x160]` = trylevel, не
     * param_2 (CONTRADICTIONS #16). */
    native::string_ctor(frame + 0x10, native::literal(0x4eec4c)); /* 0x44ca4c..0x44ca55 */
    /* 0x44ca5f `ADD EBP,0x18` — сдвиг ровно здесь; на остатке R16 (B97..B100)
     * EBP не читается (B97/B99 — ESP-слоты, B100 → 0x44d072), т.е. мёртв. */
    unit_x += 0x18;                                     /* 0x44ca5f */
    frame[0x154] = 0x14;                                /* 0x44ca6a (BL=0x14) */
    tail_str = native::string_format(frame + 0x20, unit_x, frame + 0x10); /* 0x44ca71 */
    frame[0x154] = 0x15;                                /* 0x44ca7b */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44ca83..0x44ca89 */
    frame[0x154] = 0x14;                                /* 0x44ca93 (BL) */
    native::string_dtor(frame + 0x20);                  /* 0x44ca8f..0x44ca9a */
    frame[0x154] = 0x13;                                /* 0x44caa3 */
    native::string_dtor(frame + 0x10);                  /* 0x44ca9f..0x44caab */

b97_r16:
B0_LBL(30);
    /* B97/B98 (0x44cab0..0x44cac0) — out-код: `TEST [frame+0x164]` (param_3,
     * ESP=frame) → NULL ⇒ B99, иначе `*param_3 = 1`. */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(54, (p3 != NULL)))  {
            *(int *)p3 = 1;                             /* 0x44cabb */
        }
    }
    goto b99_r16;                                       /* fall 0x44cac1 */

b99_r16:
B0_LBL(31);
    /* B99 (0x44cac1..0x44cad1) — dtor поля msg-объекта (`frame+0x58` =
     * `frame+0x4c + 0xc`), trylevel byte 0x12 перед вызовом; fall → B100. */
    frame[0x154] = 0x12;                                /* 0x44cac5 */
    native::msg_string_dtor(frame + 0x58);              /* 0x44cac1..0x44cacd */

b100_join:
B0_LBL(32);
    /* B100 (0x44cad2..0x44caed) — dtor-join: входы R12 B83/B84 (JZ), R13
     * B86/B87 (JZ/jmp), B99 fall. ESP = frame на всех входах. dtor объекта
     * PathFinder `frame+0xcc` (тот же, что ctor 45f430 в B82 и приёмник
     * 45e1d0 в R14), dword trylevel −1, затем `JMP 0x44d072` (эпилог,
     * AL=0) — out-guard `0x44d061` не исполняется. */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44cad9 */
    native::pathfinder_dtor(frame + 0xcc);              /* 0x44cad2..0x44cae4 */
    B0_EXIT(129);
    return 0;                                           /* 0x44cae9 jmp 0x44d072 */

b101_r16:
B0_LBL(33);
    B0_EXIT(129);
    /* B101 (0x44caee..0x44cb06) — входы R14 B88 JZ (`state == 3`) и R15 B89
     * JP (фаза > порога/unordered). Тот же dtor + trylevel −1, затем
     * `JMP 0x44cb39` → R17. */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44caf5 */
    native::pathfinder_dtor(frame + 0xcc);              /* 0x44caee..0x44cb00 */
    goto b103_r17;                                      /* 0x44cb05 jmp 0x44cb39 */

b102_r16:
B0_LBL(34);
    /* B102 (0x44cb07..0x44cb38) — sentinel «нет валидной точки»: тройка
     * `frame+0x10/0x14/0x18` = 0xc61c4000, затем без guard в
     * `unit+0xa4..0xac`. Литерал — **−10000.0f** (0xc61c4000; «−9990» в
     * disasm4 — опечатка). Стейджинг-слоты frame+0x10..0x18 после store
     * мёртвы (R14 restore уже выполнен), но порядок инструкций сохранён.
     * fall → 0x44cb39 (R17). */
    *(unsigned int *)(frame + 0x10) = 0xc61c4000u;      /* 0x44cb07 */
    *(unsigned int *)(frame + 0x14) = 0xc61c4000u;      /* 0x44cb1b */
    *(unsigned int *)(frame + 0x18) = 0xc61c4000u;      /* 0x44cb27 */
    *(unsigned int *)(unit_x + 0xa4) = *(const unsigned int *)(frame + 0x10); /* 0x44cb0f..0x44cb19 */
    *(unsigned int *)(unit_x + 0xa8) = *(const unsigned int *)(frame + 0x14); /* 0x44cb23..0x44cb33 */
    *(unsigned int *)(unit_x + 0xac) = *(const unsigned int *)(frame + 0x18); /* 0x44cb2f..0x44cb36 */
    /* fall 0x44cb39 */
b103_r17:
B0_LBL(35);
    /* --- R17: tick core — per-tick запись → `45eee0` → `[unit+0x170]==0`
     * лог (`0x44cb39..0x44cc18`, B103..B108) ---------------------------- *
     * Входы: B101 `0x44cb05 jmp` ×2 (R14 state==3 / R15 JP) и B102 fall.
     * На обоих EBP не сдвинут (`ADD EBP,0x18` B96 уходит в b97/b99/b100),
     * поэтому `[EBP+…]` == unit_x == unit.
     * EDI на обоих входах = 0: B77 `XOR EDI,EDI`; R14 его только читает,
     * R15 B90 пишет frame+0x80 уже ПОСЛЕ B89 JP — до R17 не доходит. R17
     * EDI не переписывает, а использует как ноль в pre-zero и `CMP`.
     * EBX = `frame+0x160` (B80) либо 0 (B79): R16 B96 пишет только BL, а
     * B101/B102 её не исполняют, поэтому EBX = значение B80/B79.
     * Запись — 7 dword (base `[ESP+0xa4]` = frame+0xa4; record[6] натив
     * пишет как `[ESP+0xc0]` уже после PUSH, т.е. frame+0xbc), приёмник
     * `45eee0` — ECX = `&unit+0x138`, стек-аргумент = `PUSH &record`,
     * `RET 4`, возврат EAX не читается. */
    {
        unsigned char cl;
        unsigned int flags;
        void *tree;
        void *pos_ptr;
        float rec5;

        /* B103 (0x44cb39..0x44cb9e) — сборка записи. Порядок = листинг. */
        cl = frame[0x15c];                              /* 0x44cb39 param_1 slot */
        tree = *(void **)(unit_x + 0xe0);               /* 0x44cb40 */
        *(unsigned int *)(frame + 0xac) = (unsigned int)edi;  /* 0x44cb4a rec[2]=EDI */
        *(unsigned int *)(frame + 0xb0) = (unsigned int)edi;  /* 0x44cb51 rec[3]=EDI */
        *(unsigned int *)(frame + 0xb4) = (unsigned int)edi;  /* 0x44cb58 rec[4]=EDI */
        *(void **)(frame + 0xa8) = unit_x;              /* 0x44cb5f rec[1]=EBP */
        /* 0x44cb46..0x44cb68: DL=CL; NEG DL; SBB EDX,EDX; AND EDX,0x80 —
         * bit7 = (CL != 0); далее `OR EBX`, `OR 0x63` (@0x44cb6e/0x44cb70). */
        flags = ebx | 0x63u;
        if (B0_TST(55, (cl != 0)))  {
            flags |= 0x80u;
        }
        /* 0x44cb73 `TEST CL,CL` — ZF живёт до ветви 0x44cb9e. */
        *(unsigned int *)(frame + 0xa4) = flags;        /* 0x44cb75 rec[0] */
        tree = *(void **)((unsigned char *)tree + 0x44);      /* 0x44cb7c */
        tree = *(void **)((unsigned char *)tree + 0x238);     /* 0x44cb7f */
        pos_ptr = frame + 0x2c;                         /* 0x44cb85 &pos-тройки */
        *(void **)(frame + 0xac) = tree;                /* 0x44cb89 rec[2] */
        *(unsigned int *)(frame + 0xb4) = (unsigned int)edi;  /* 0x44cb90 rec[4] re-zero */
        *(void **)(frame + 0xb0) = pos_ptr;             /* 0x44cb97 rec[3] */
        /* B104/B105 (0x44cba0/0x44cba8) — CL≠0 → `[unit+0x12c]`, иначе
         * `[unit+0x128]`; B106 FSTP m32 (`fstps`, x87 80 → float 32). */
        rec5 = (cl != 0) ? *(const float *)(unit_x + 0x12c)
                         : *(const float *)(unit_x + 0x128);
        *(float *)(frame + 0xb8) = rec5;                /* 0x44cbb5 rec[5] */
        *(unsigned int *)(frame + 0xbc) = 0;            /* 0x44cbc5 rec[6] */
        /* 0x44cbbc..0x44cbd0 — ECX = &unit+0x138 (ESI), PUSH &record. */
        (void)b0_45eee0_dispatch(unit_x + 0x138, frame + 0xa4);
    }
    /* B106 (0x44cbd5..0x44cbdd) — dispatch по `[unit+0x170]` (сравнение с
     * EDI=0); возврат `45eee0` в EAX не читается. */
    if (B0_TST(56, (*(const int *)(unit_x + 0x170) != edi)))  {
        goto b116_r18;                                  /* 0x44cbdd jne 0x44cc84 */
    }

    /* B107 (0x44cbe3..0x44cbfb) — assert-лог (код 0xa), затем vtable+0x40
     * БЕЗ push: callee читает свой [esp+4] = верх ESP вызывающего = frame =
     * слот сохранённого EDI пролога (модель B15/B24/B35). NULL → B112. */
    native::log_assert(unit_x, native::literal(0x51cc50), 0xa); /* 0x44cbe3..0x44cbec */
    if (B0_TST(57, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b112_r18;                                  /* 0x44cbfb je 0x44cc5c */
    }

    /* B108 (0x44cbfd..0x44cc17) — цепочка флагов (`push 1` → vtable+0x40 →
     * 42da60 → 435d50); NULL → B112, иначе fall → R18 B109. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44cbfd..0x44cc10 */
    if (B0_TST(58, (flags_obj == NULL)))  {
        goto b112_r18;                                  /* 0x44cc17 je 0x44cc5c */
    }
    goto b109_r18;                                      /* fall 0x44cc19 → R18 */

b109_r18:
B0_LBL(36);
    /* --- R18: dispatch-каскад 2/3 (`0x44cc19..0x44cd09`) ------------------ *
     * Входы: B109 — fall R17 B108 (0x44cc17 je не взят), B112 — B110 `je`
     * (0x44cc51) и R17 B108 `je` (0x44cc17), B116 — R17 B106 `jne`
     * (0x44cbdd). На ВСЕХ трёх входах `ADD EBP,0x18` не исполнялся (сдвиги
     * B21/B30/B41/B58/B62/B65/B69/B72/B96 уходят в 0x44cee6 / 0x44c6b6 /
     * 0x44c6cb / 0x44d072), поэтому `[EBP+…]` = `unit_x == unit`. EDI = 0
     * (B77 `XOR EDI,EDI`; R17/R18 его не пишут) — читается только как ноль:
     * `CMP EAX,EDI` и `MOV [EAX],EDI` B114/B115. EBX на входе — B80/B79
     * (`frame+0x160` либо 0); R18 переписывает EBX только в B118 (`0x16`),
     * уже после всех чтений EBX.
     *
     * B109/B110 (0x44cc19..0x44cc51) — две цепочки-клона
     * (`vtable[+0x40](unit,1)` → 42da60 → 435d50): `TESTB $2` → B111,
     * иначе `TESTB $4`; бит снят → B112. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44cc19..0x44cc31 */
    if (B0_TST(59, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b111_r18;                                  /* 0x44cc34 jne 0x44cc53 */
    }
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44cc36..0x44cc4e */
    if (B0_TST(60, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b112_r18;                                  /* 0x44cc51 je 0x44cc5c */
    }

b111_r18:
B0_LBL(37);
    /* B111 (0x44cc53..0x44cc57) — cursor/mode update 0x40, fall → B112. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44cc53..0x44cc57 */

b112_r18:
B0_LBL(38);
    /* B112/B113 (0x44cc5c..0x44cc68) — msg-гейт `unit+0x34`: ненулевой →
     * debug-строка `0x4eec24` ("PathFinder.GetResult() == PF_NOT_FOUND\n").
     * CALL `*0x4ec064` — это IAT **OutputDebugStringA** (CONTRADICTIONS #15:
     * LoadCursorFromFileA лежит на 0x4ec264 и в R18 не вызывается); литерал
     * идёт прямо в WinAPI как char*, без string-машины. */
    if (B0_TST(61, (*(const unsigned char *)(unit_x + 0x34) != 0)))  {
        native::output_debug_string_a(native::literal(0x4eec24)); /* 0x44cc63..0x44cc68 */
    }
    /* B114/B115 (0x44cc6e..0x44cc7f) — out-guard param_3:
     * `EAX=[ESP+0x164]; CMP EAX,EDI; JZ 0x44d072; [EAX]=EDI; jmp 0x44d072`
     * (EDI=0 ⇒ `*param_3 = 0`, AL=0). ESP = frame на этом пути (B112 не
     * трогал стек). */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(62, (p3 != NULL)))  {
            *(int *)p3 = 0;                             /* 0x44cc7d */
        }
    }
    B0_EXIT(130);
    return 0;                                           /* 0x44cc7f jmp 0x44d072 */

b116_r18:
B0_LBL(39);
    B0_EXIT(130);
    /* B116 (0x44cc84..0x44cc87) — `CMP EAX,2`; EAX = `[unit+0x170]`,
     * прочитанный R17 B106 (0x44cbd5) и не перетираемый до этого места
     * (переход 0x44cbdd — прямой). EDI = 0. */
    if (B0_TST(63, (*(const int *)(unit_x + 0x170) != 2)))  {
        goto b119_r18;                                  /* 0x44cc87 jne 0x44ccfa */
    }

    /* B117 (0x44cc89..0x44cc8e) — msg-гейт `unit+0x34`: 0 → B119. */
    if (B0_TST(64, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b119_r18;                                  /* 0x44cc8e je 0x44ccfa */
    }

    /* B118 (0x44cc90..0x44ccf5) — cursor-блок K1 (GUI, литерал 0x4eec10
     * " C++ PF_HALF_FOUND\n"). Форма 413060 — как B41/B62/B65/B69/B72, но
     * `ADD EBP,0x18` здесь НЕТ: аргумент — `lea 0x18(%ebp)` (unit+0x18),
     * сам EBP не сдвигается. Стек: после `push lit` ctor-объект
     * `[esp+0x14]` = frame+0x10 (ret 4 снимает push); три push
     * (arg3=frame+0x10, arg2=unit+0x18, dst=frame+0x20), `MOV EBX,0x16`,
     * `[esp+0x160]` при ESP=frame−0xc ⇒ **frame+0x154** dword 0x16
     * (CONTRADICTIONS #16); `ADD ESP,0xc` балансирует три push.
     * trylevel-байты: 0x17 → BL(0x16) → dword −1; fall-through → B119. */
    native::string_ctor(frame + 0x10, native::literal(0x4eec10)); /* 0x44cc90..0x44cc99 */
    ebx = 0x16;                                         /* 0x44ccab */
    *(unsigned int *)(frame + 0x154) = ebx;             /* 0x44ccb1 */
    tail_str = native::string_format(frame + 0x20, unit_x + 0x18, frame + 0x10); /* 0x44ccb8 */
    frame[0x154] = 0x17;                                /* 0x44ccc2 */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44ccca..0x44ccd0 */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44ccda mov %bl */
    native::string_dtor(frame + 0x20);                  /* 0x44ccd6..0x44cce1 */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44ccea */
    native::string_dtor(frame + 0x10);                  /* 0x44cce6..0x44ccf5 */

b119_r18:
B0_LBL(40);
    /* B119/B120 (0x44ccfa..0x44cd09) — код `[unit+0x170]` против 3, затем
     * msg-гейт. Оба провала → R19 B122 (0x44cd74, середина R19 — вход в
     * проверку кода 4); успех → fall 0x44cd0a (R19 B121, cursor K2). */
    if (B0_TST(65, (*(const int *)(unit_x + 0x170) != 3)))  {
        goto b122_r19;                                  /* 0x44cd01 jne 0x44cd74 */
    }
    if (B0_TST(66, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b122_r19;                                  /* 0x44cd08 je 0x44cd74 */
    }
    goto b121_r19;                                      /* fall 0x44cd0a → R19 */

b121_r19:
B0_LBL(41);
    /* --- R19 B121 (0x44cd0a..0x44cd72, GUI): cursor-блок K2 ------------- *
     * Единственный вход — fall B120 (0x44cd08 `je` не взят). `ADD EBP,0x18` в
     * блоке НЕТ (только `lea 0x18(%ebp),%edx` — arg2 413060), поэтому EBP на
     * всём R19 = unit: unit_x == unit. ESI на входе = unit+0x138
     * (`lea 0x138(%ebp),%esi` @0x44cbbc, R17 B103; до R19 ESI не пишется) —
     * это и есть ECX обоих `45c370` (B123/B124). EDI = 0 (B77), в R19 не
     * читается. Форма 413060 — как B118/K1: литерал 0x4eec10, arg2 = unit+0x18,
     * dst = frame+0x20, arg3 = строка A `frame+0x10`. trylevel: dword 0x18
     * (0x44cd2b `[esp+0x160]` при esp=frame−0xc ⇒ frame+0x154, CONTRADICTIONS
     * #16) → байт 0x19 → BL(0x18) → dword −1; dtors B (`frame+0x20`) и A. */
    native::string_ctor(frame + 0x10, native::literal(0x4eec10)); /* 0x44cd0a..0x44cd13 */
    ebx = 0x18;                                         /* 0x44cd25 */
    *(unsigned int *)(frame + 0x154) = ebx;             /* 0x44cd2b (dword) */
    tail_str = native::string_format(frame + 0x20, unit_x + 0x18, frame + 0x10); /* 0x44cd32 */
    frame[0x154] = 0x19;                                /* 0x44cd3c (байт) */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44cd44..0x44cd4a */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44cd54 mov %bl */
    native::string_dtor(frame + 0x20);                  /* 0x44cd50..0x44cd5b */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44cd64 (dword) */
    native::string_dtor(frame + 0x10);                  /* 0x44cd60..0x44cd6f */
    /* fall 0x44cd74 → B122 (каскад того же кода) */

b122_r19:
B0_LBL(42);
    /* --- R19 B122..B126 (0x44cd74..0x44cdf1, LOGIC+GUI): code==4 -------- *
     * Входы: fall B121 и `jne/je` R18 B119/B120 (0x44cd01/0x44cd08). */
    if (B0_TST(67, (*(const int *)(unit_x + 0x170) != 4)))  {
        goto b144_r22;                                  /* 0x44cd7b jne 0x44cfea */
    }

    /* B123/B124 (0x44cd81..0x44cd9d) — head (ECX = ESI = ctx) и count:
     * head == NULL → только float-гейт (count пропущен); count > 1
     * (`CMP EAX,1; JG`, знаковое) → R22 B144. */
    head = b0_band_head(unit_x + 0x138);                /* 0x44cd83 */
    if (B0_TST(68, (head != NULL)))  {
        head = b0_band_head(unit_x + 0x138);            /* 0x44cd8e */
        count = b0_band_count_claim(head);              /* 0x44cd95 */
        if (B0_TST(69, (count > 1)))  {
            goto b144_r22;                              /* 0x44cd9d jg 0x44cfea */
        }
    }

    /* B125 (0x44cda3..0x44cdb6) — `vtable[+0x2c]` (phase, ST(0)) против
     * `[unit+0xfc]`: `TEST AH,0x41; JP` ⟺ больше или unordered (PITFALLS
     * «x87-маска 0x41 + JP», CONTRADICTIONS #14) → R21 B138. */
    native::phase_time(unit_x, &phase);                 /* 0x44cda8 */
    if (B0_TST(70, (b0_gate_41_jp(phase, *(const float *)(unit_x + 0xfc)))))  {
        goto b138_r21;                                  /* 0x44cdb6 jp 0x44cf10 */
    }

    /* B126 (0x44cdbc..0x44cdf1) — builder ctor `4040a0` на `frame+0x4c` (тот
     * же msg-объект, что dtor `462780` в R21 B137), затем `442570(unit,0)`:
     * `CMP [EAX],3`; mode-флаг `[esp+0x54]` при esp=frame−4 ⇒ `frame+0x50`
     * = 1 (state≠3) / 4 (state==3); trylevel dword 0x1a @`[esp+0x158]` ⇒
     * `frame+0x154`. `push 0` @0x44cddc и ECX=unit @0x44cdde относятся к
     * вызову R20 (0x44cdf2) и в заглушке не исполняются. */
    native::msg_ctor(frame + 0x4c);                     /* 0x44cdbc..0x44cdc0 */
    *(unsigned int *)(frame + 0x154) = 0x1a;            /* 0x44cdc9 */
    res = native::game_state_get(unit_x, 0);            /* 0x44cdd4 */
    r19_state3 = (*(const int *)res == 3) ? 1 : 0;      /* 0x44cdd9 */
    *(unsigned int *)(frame + 0x50) = 1;                /* 0x44cde0 */
    if (B0_TST(71, (!r19_state3)))  {
        goto b128_r20;                                  /* 0x44cde8 jne 0x44cdf2 */
    }
    *(unsigned int *)(frame + 0x50) = 4;                /* 0x44cdea */
    goto b128_r20;                                      /* fall 0x44cdf2 → R20 */

b128_r20:
B0_LBL(43);
    /* --- R20: state-копия + цепочки + cursor K3 + builder/timer ----------- *
     * `0x44cdf2..0x44cee5`, B128..B134. Входы: B127 fall и B128 `jne`
     * (R19 B126 `0x44cde8`). На обоих EBP не сдвинут (`ADD EBP,0x18` есть
     * только в B21/B30/B41/B58/B62/B65/B69/B72/B96; все они уходят из этого
     * пути), поэтому `unit_x == unit`. ESI на входе мёртв: B128 сразу
     * `MOV ESI,EAX` (результат 442570) и использует его лишь как источник
     * `rep movsl`. EDI тоже перезаписывается B128 (`LEA EDI,[frame+0x60]`,
     * затем +0x20 от rep) и до конца R20 не читается. EBX читается только
     * как BL в B133, куда пишется `MOV BL,0x1b`.
     *
     * ESP: R19 B126 оставляет `push 0` под вызов 442570 @0x44cdf2; `ret 4`
     * callee снимает его, поэтому все слоты R20 `[esp+…]` = `frame+…`.
     * B134 `SUB ESP,0x3c` балансируется `446a80` (`ret $0x3c`), поэтому на
     * join `0x44cee6` ESP = frame (out-guard R21: `[esp+0x164]` = frame+0x164). */
    res = native::game_state_get(unit_x, 0);            /* 0x44cdf2 (push 0, ECX=unit_x, ret 4) */
    memcpy(frame + 0x60, res, 32);                      /* 0x44ce05 rep movsl ×8 */
    *(unsigned int *)(frame + 0x80) = 0;                /* 0x44ce09 */
    /* vtable+0x40 БЕЗ push: callee читает [esp+4] = frame (модель B15/B24/B35). */
    if (B0_TST(72, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b132_r20;                                  /* 0x44ce19 je 0x44ce5e */
    }

    /* B129 (0x44ce1b..0x44ce36) — цепочка ×1 (`push 1`), `TESTB $2`:
     * бит взят → B131. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44ce1b..0x44ce33 */
    if (B0_TST(73, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b131_r20;                                  /* 0x44ce36 jne 0x44ce55 */
    }

    /* B130 (0x44ce38..0x44ce53) — цепочка ×2, `TESTB $4`: бит снят → B132. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44ce38..0x44ce50 */
    if (B0_TST(74, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b132_r20;                                  /* 0x44ce53 je 0x44ce5e */
    }

b131_r20:
B0_LBL(44);
    /* B131 (0x44ce55..0x44ce5c) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44ce55..0x44ce59 */

b132_r20:
B0_LBL(45);
    /* B132 (0x44ce5e..0x44ce64) — msg-флаг `unit+0x34`: 0 → B134 (пропуск
     * cursor-блока). */
    if (B0_TST(75, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b134_r20;                                  /* 0x44ce63 je 0x44cec9 */
    }

    /* B133 (0x44ce65..0x44cec4) — cursor-блок K3, литерал 0x4eebc8. Форма
     * 413060 как B118/B121: ctor A `frame+0x10`; dst `frame+0x20`;
     * arg2 = `unit_x+0x18` (EBP НЕ сдвигается — только `lea`); 3 push
     * (`[esp+0x160]` при esp=frame−0xc ⇒ byte `frame+0x154`);
     * trylevel 0x1b → 0x1c → BL(0x1b) → 0x1a, все четыре — байтовые записи. */
    native::string_ctor(frame + 0x10, native::literal(0x4eebc8)); /* 0x44ce65..0x44ce6f */
    ebx = 0x1b;                                         /* 0x44ce80 */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44ce83 (byte) */
    tail_str = native::string_format(frame + 0x20, unit_x + 0x18, frame + 0x10); /* 0x44ce8a */
    frame[0x154] = 0x1c;                                /* 0x44ce94 (byte) */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44ce9c..0x44cea2 */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44ceac (byte, mov %bl) */
    native::string_dtor(frame + 0x20);                  /* 0x44cea8..0x44ceb3 */
    frame[0x154] = 0x1a;                                /* 0x44cebc (byte) */
    native::string_dtor(frame + 0x10);                  /* 0x44ceb8..0x44cec4 */

b134_r20:
B0_LBL(46);
    /* B134 (0x44cec9..0x44cee5) — builder-копия msg + таймер. Натив:
     * `SUB ESP,0x3c` (буфер by-value), src = `frame+0x4c` (msg-объект R19
     * B126), dst = ESP; `[frame+0x1c]` = ESP; `4040f0` (ret 4), затем
     * `446a80` (`ret 0x3c`, ECX=unit_x). Слот frame+0x1c держит указатель
     * на локальный буфер, как в B15/B24/B90; сам SUB в порт не переносится. */
    *(void **)(frame + 0x1c) = msg_dst;                 /* 0x44ced5 */
    native::msg_copy(msg_dst, frame + 0x4c);            /* 0x44ceda */
    native::ai_time_update(unit_x, msg_dst);            /* 0x44cee1 */
    goto b135_join;                                     /* fall 0x44cee6 → R21 */

b138_r21:
B0_LBL(47);
    /* --- R21 B138..B142 (0x44cf10..0x44cf77, GUI): фаза code==4 ---------- *
     * Вход — только R19 B125 `0x44cdb6 jp` (фаза > `[unit+0xfc]` или
     * unordered). ESP на входе = frame. EBP не сдвинут на всём пути
     * (B126/B134 без `ADD EBP,0x18`), поэтому адресация — unit_x == unit.
     * ESI/EDI/EBX в R21 не читаются (ESI после B121 — ctx, EDI = 0 из B77);
     * ECX = EAX-клоббер каждого вызова, EBX пишется только в R20 B133. */
    /* B138 (0x44cf10..0x44cf1a) — vtable+0x40 БЕЗ push: callee читает
     * `[esp+4]` = frame[0] (модель B15/B24/B35/B88). NULL → B142. */
    if (B0_TST(76, (native::player_get(unit_x, *(const int *)(frame + 0)) == NULL)))  {
        goto b142_r21;                                  /* 0x44cf1a je 0x44cf5f */
    }

    /* B139 (0x44cf1c..0x44cf37) — цепочка ×1 (`push 1`), `TESTB $2`:
     * бит взят → B141. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44cf1c..0x44cf2f */
    if (B0_TST(77, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b141_r21;                                  /* 0x44cf37 jne 0x44cf56 */
    }

    /* B140 (0x44cf39..0x44cf54) — цепочка ×2, `TESTB $4`: бит снят → B142. */
    flags_obj = b0_player_flag_chain(unit_x);           /* 0x44cf39..0x44cf4c */
    if (B0_TST(78, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b142_r21;                                  /* 0x44cf54 je 0x44cf5f */
    }

b141_r21:
B0_LBL(48);
    /* B141 (0x44cf56..0x44cf5a) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit_x, 0x40);           /* 0x44cf56..0x44cf5a */

b142_r21:
B0_LBL(49);
    /* B142 (0x44cf5f..0x44cf77) — лог `0x51cc50`/0xa, затем msg-гейт
     * `unit+0x34`: 0 → out-guard `0x44d061` (AL=0); иначе fall → R22 B143. */
    native::log_assert(unit_x, native::literal(0x51cc50), 0xa); /* 0x44cf5f..0x44cf68 */
    if (B0_TST(79, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44cf72 je 0x44d061 */
    }
    goto b143_r22;                                      /* fall 0x44cf78 → R22 B143 */

b143_r22:
B0_LBL(50);
    /* --- R22 B143 (0x44cf78..0x44cfe8): cursor-блок K -------------------- *
     * Единственный вход — fall R21 B142 (0x44cf78). ESP на входе = frame.
     * Отличие от прочих cursor-блоков: строка A — `frame+0x40` (0x44cf7d/
     * 0x44cf86 при esp=frame−4), dst 413060 — `frame+0xc0` (0x44cf8e).
     * Форма 413060: arg1 = dst, arg2 = EBP после `ADD EBP,0x18` (unit+0x18),
     * arg3 = A; три push балансируются `ADD ESP,0xc`. trylevel: dword 0x1d
     * (0x44cf9c `[esp+0x160]` при esp=frame−0xc ⇒ frame+0x154,
     * CONTRADICTIONS #16) → байт 0x1e → BL(0x1d) → dword −1.
     * Выход — `JMP 0x44d061` (out=0, AL=0). */
    native::string_ctor(frame + 0x40, native::literal(0x4eeb80)); /* 0x44cf78..0x44cf81 */
    unit_x += 0x18;                                     /* 0x44cf8b ADD EBP,0x18 */
    ebx = 0x1d;                                         /* 0x44cf96 */
    *(unsigned int *)(frame + 0x154) = ebx;             /* 0x44cf9c (dword) */
    tail_str = native::string_format(frame + 0xc0, unit_x, frame + 0x40); /* 0x44cfa3 */
    frame[0x154] = 0x1e;                                /* 0x44cfad (byte) */
    native::output_debug_string_a(native::string_c_str(tail_str)); /* 0x44cfb5..0x44cfbb */
    frame[0x154] = (unsigned char)ebx;                  /* 0x44cfc8 mov %bl */
    native::string_dtor(frame + 0xc0);                  /* 0x44cfc1..0x44cfcf */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44cfd8 (dword) */
    native::string_dtor(frame + 0x40);                  /* 0x44cfd4..0x44cfe3 */
    goto b0_exit_44d061;                                /* 0x44cfe8 jmp 0x44d061 */

b144_r22:
B0_LBL(51);
    /* --- R22 B144..B156 (0x44cfea..0x44d071): финальные гейты, AL --------- *
     * Входы B144: R19 B122 `jne` (0x44cd7b, code∉{0,2,3,4}) и R19 B124 `jg`
     * (0x44cd9d, code==4 и count>1). На обоих EBP не сдвинут (сдвиги
     * B21/B30/B41/B58/B62/B65/B69/B72/B96 уходят из этого пути, B143 — в
     * 0x44d061) ⇒ `[EBP+…]` == unit_x == unit. ESI на входах = unit+0x138
     * (R17 B103 @0x44cbbc; R18/R19 его не пишут, R20 переприсваивает ESI
     * только на своём пути → join, до R22 не доходит), поэтому оба `45c370`
     * получают ctx. EDI (=0, B77) и EBX в R22 не читаются (B143 только
     * пишет EBX). ESP = frame на всех трёх out-сайтах ⇒ param_3 =
     * frame+0x164.
     * B144/B145 (0x44cfea..0x44d006) — head, затем count; head == NULL →
     * B154, count ≤ 1 (знаковое `JLE`) → B154. */
    head = b0_band_head(unit_x + 0x138);                /* 0x44cfec */
    if (B0_TST(80, (head == NULL)))  {
        goto b154_r22;                                  /* 0x44cff3 je 0x44d058 */
    }
    head = b0_band_head(unit_x + 0x138);                /* 0x44cff7 */
    count = b0_band_count_claim(head);                  /* 0x44cffe */
    if (B0_TST(81, (count <= 1)))  {
        goto b154_r22;                                  /* 0x44d006 jle 0x44d058 */
    }

    /* B145 (0x44d008..0x44d019) — `FLD [unit+0x114]; FCOMPS 0.0f;
     * TEST AH,0x41; JNZ 0x44d043`: взят ⟺ NOT([unit+0x114] > 0.0f)
     * (маска C3|C0, PITFALLS) → B151. */
    if (B0_TST(82, (b0_gate_41_jnz(*(const float *)(unit_x + 0x114), 0.0f))))  {
        goto b151_r22;                                  /* 0x44d019 jne 0x44d043 */
    }

    /* B146 (0x44d01b..0x44d023) — ptr-гейт `[unit+0x84]`: 0 → B151. */
    if (B0_TST(83, (*(const unsigned int *)(unit_x + 0x84) == 0)))  {
        goto b151_r22;                                  /* 0x44d023 je 0x44d043 */
    }

    /* B147 (0x44d025..0x44d02c) — success: `448ee0(unit, 0)`; fall → B148. */
    native::state_flag_log(unit_x, 0);                  /* 0x44d029 */

b148_r22:
B0_LBL(52);
    /* B148..B150 (0x44d02e..0x44d041) — вход и из R2 B11 (`0x44bf01 jg
     * 0x44d02e`, count>1 — `448ee0` пропускается): out=1, AL=1 и
     * `JMP 0x44d074` (обход `XOR AL,AL`). */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(84, (p3 != NULL)))  {
            *(int *)p3 = 1;                             /* 0x44d039 */
        }
    }
    B0_EXIT(131);
    return 1;                                           /* 0x44d03f mov $1,%al */

b151_r22:
B0_LBL(53);
    B0_EXIT(131);
    /* B151..B153 (0x44d043..0x44d056) — гейт не пройден (фаза ≠ 0.0f или
     * `[unit+0x84]==0`): out=0, AL=1. */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(85, (p3 != NULL)))  {
            *(int *)p3 = 0;                             /* 0x44d04e */
        }
    }
    B0_EXIT(132);
    return 1;                                           /* 0x44d054 mov $1,%al */

b154_r22:
B0_LBL(54);
    B0_EXIT(132);
    /* B154..B156 (0x44d058..0x44d071) — нет пригодной band-цели (head NULL
     * или count ≤ 1): `448ee0(unit, 1)`, затем out-guard `0x44d061`
     * (out=0) и `XOR AL,AL` ⇒ AL=0. */
    native::state_flag_log(unit_x, 1);                  /* 0x44d05c */
    goto b0_exit_44d061;                                /* fall 0x44d061 */

b74_cursor_tail:
B0_LBL(55);
    /* B74 (0x44c6cb..0x44c6ec) — общий хвост cursor-блока: dtor строки
     * `frame+0x20`, dword trylevel = −1, dtor `frame+0x10`, затем
     * `JMP 0x44d061` (out-guard, AL=0). Входы: fall B73 (B41/B62/B65/B69/B72)
     * и jmp B58. */
    native::string_dtor(frame + 0x20);                  /* 0x44c6cb..0x44c6cf */
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44c6d8 (dword) */
    native::string_dtor(frame + 0x10);                  /* 0x44c6e3 */
    goto b0_exit_44d061;                                /* 0x44c6e8 jmp 0x44d061 */

b0_exit_44d061:
B0_LBL(56);
    /* 0x44d061..0x44d072 — out-guard param_3 = 0, затем `XOR AL,AL`. */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(86, (p3 != NULL)))  {
            *(int *)p3 = 0;                             /* 0x44d06c */
        }
    }
    B0_EXIT(133);
    return 0;                                           /* 0x44d072 xor al,al */

/* --- R9: band-loop тело B (`0x44c4dd..0x44c5b0`, LOGIC) ---------------- *
 B0_EXIT(133);
 * Входы — только из R8 B60: `JP` @0x44c4db не взят (45b150 == 4.0f) → B61
 * (0x44c4dd), взят (45b150 > 4.0f или unordered) → B63 (0x44c525).
 * unit_x на ОБОИХ входах НЕ сдвинут: единственный путь сюда — B55..B60, а
 * `ADD EBP,0x18` есть только в B21/B30 (уходят `jmp 0x44cee6`), B41
 * (`jmp 0x44c6b6`) и B58 (→0x44c6cb); сдвиги внутри R9 (B62/B65) стоят уже
 * ПОСЛЕ msg-гейта и уходят в тот же `0x44c6b6`. Поэтому B61/B63/B64/B66
 * адресуют unit_x == unit.
 * EDI: R8 B59 @0x44c48f ставит EDI=1, и B66 читает его —
 * `CMP [EBP+0x170],EDI`. Модель: сравнение с константой 1 (B77 затем
 * `XOR EDI,EDI`; других чтений EDI в R9 нет). */
b61_r9:
B0_LBL(57);
    /* B61 (0x44c4dd..0x44c4e7) — msg-гейт `unit+0x34`: 0 → exit 0x44d061. */
    if (B0_TST(87, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44c4e2 je 0x44d061 */
    }

    /* B62 (0x44c4e8..0x44c524) — log-block E, литерал 0x4eed1c. Клон B58:
     * ctor объекта A (`frame+0x10`); `ADD EBP,0x18`; dword trylevel 0xa
     * (0x44c509 `[esp+0x160]` при esp = frame−0xc ⇒ frame+0x154, CONTRADICTIONS
     * #16); 413060(dst `frame+0x20`, arg2 = unit_x, arg3 = A); байт trylevel
     * 0xb; `JMP 0x44c6b6` (B73, R11: c_str + OutputDebugStringA + dtor'ы). */
    {
        void *str_a = frame + 0x10;

        native::string_ctor(str_a, native::literal(0x4eed1c)); /* 0x44c4e8..0x44c4f1 */
        unit_x += 0x18;                                 /* 0x44c4fb ADD EBP,0x18 */
        ebx = 0xa;                                      /* 0x44c503 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c509 (dword) */
        tail_str = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c510 */
        frame[0x154] = 0xb;                             /* 0x44c518 */
    }
    goto b73_cursor_tail;                               /* 0x44c520 jmp 0x44c6b6 */

b63_r9:
B0_LBL(58);
    /* B63 (0x44c525..0x44c55c) — вторая тройка (result+4..0xc) на стек
     * (`sub 0xc`, указатель — `[esp+0x28]` ⇒ frame+0x1c), 45c370 → лента,
     * 45b3e0(ECX = лента, arg1..arg3 = dword'ы тройки; `RET $0xc` ⇒ после
     * вызова esp = frame) → AL. AL == 0 → B66; иначе fall → B64 (msg-гейт). */
    res = native::game_state_get(unit_x, 0);            /* 0x44c529 */
    memcpy(band_triple, (const unsigned char *)res + 4, 12); /* 0x44c52e..0x44c544 */
    *(void **)(frame + 0x1c) = band_triple;             /* 0x44c549 mov %esp,0x28(%esp) */
    head = b0_band_head(unit_x + 0x138);                /* 0x44c54d */
    if (B0_TST(88, ((unsigned char)b0_band_target_moved(head, band_triple[0], b0_float_bits(band_triple[1]), band_triple[2]) == 0)))  {
        goto b66;                                       /* 0x44c55b je 0x44c5a5 */
    }

    /* B64 (0x44c55d..0x44c567) — msg-гейт `unit+0x34`: 0 → exit 0x44d061. */
    if (B0_TST(89, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44c562 je 0x44d061 */
    }

    /* B65 (0x44c568..0x44c5a4) — log-block F, литерал 0x4eecc0; клон B62 с
     * dword trylevel 0xc и байтом 0xd; `JMP 0x44c6b6` (B73, R11). */
    {
        void *str_a = frame + 0x10;

        native::string_ctor(str_a, native::literal(0x4eecc0)); /* 0x44c568..0x44c571 */
        unit_x += 0x18;                                 /* 0x44c57b ADD EBP,0x18 */
        ebx = 0xc;                                      /* 0x44c583 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c589 (dword) */
        tail_str = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c590 */
        frame[0x154] = 0xd;                             /* 0x44c598 */
    }
    goto b73_cursor_tail;                               /* 0x44c5a0 jmp 0x44c6b6 */

b66:
B0_LBL(59);
    /* B66 (0x44c5a5..0x44c5b0) — `CMP [EBP+0x170],1`: равно → flags-join B77
     * (R11), иначе fall → B67 (R10). */
    if (B0_TST(90, (*(const int *)(unit_x + 0x170) == 1)))  {
        goto b77_join;                                  /* 0x44c5ab je 0x44c715 */
    }
    goto b67_r10;                                       /* fall 0x44c5b1 */

/* --- R10: band-loop тело C + back-edge (`0x44c5b1..0x44c67d`, LOGIC) ---- *
 * Входы в B67 два: R8 B59 (`0x44c496 jne`, код `unit+0x170` ≠ 1) и R9 B66
 * (`fall 0x44c5b1`). На обоих `ADD EBP,0x18` не исполнялся (сдвиги B58/B62/B65
 * уходят в `0x44c6cb`/`0x44c6b6`), поэтому весь R10, ВКЛЮЧАЯ back-edge,
 * адресует unit_x == unit. Сдвиг B69 @0x44c610 стоит на пути в `0x44c6b6`
 * (выход) и назад в цикл не возвращается — advance, без отката.
 * Единственный back-edge тела: B70 `je 0x44c365` → метка `b46` (R7), т.е.
 * повторный вход в цикл идёт через `[frame+0x15c] = 0`. */
b67_r10:
B0_LBL(60);
    /* B67 (0x44c5b1..0x44c5f0) — triple = result+4..0xc на стек (указатель
     * `[esp+0x28]` ⇒ frame+0x1c); 45c370 → лента; 45b150 (ECX = лента,
     * arg1..arg3 = triple, `RET $0xc`) → ST0; `FCOMPS [0x4eede8]` = 3.0f;
     * `TEST AH,0x41; JP 0x44c637`. JP взят ⟺ NOT(value ≤ 3.0f) (больше или
     * unordered; PITFALLS «x87-маска 0x41 + JP») → B70. Тело 45b150
     * портировано (s6_band_interp_gate); диспетчер — b0_band_interp_gate. */
    res = native::game_state_get(unit_x, 0);            /* 0x44c5b5 */
    memcpy(band_triple, (const unsigned char *)res + 4, 12); /* 0x44c5ba..0x44c5d0 */
    *(void **)(frame + 0x1c) = band_triple;             /* 0x44c5d5 mov %esp,0x28(%esp) */
    head = b0_band_head(unit_x + 0x138);                /* 0x44c5d9 */
    gate = b0_band_interp_gate(head, band_triple[0],
                               b0_float_bits(band_triple[1]),
                               band_triple[2]);         /* 0x44c5e0 */
    if (B0_TST(91, (b0_gate_41_jp(gate, 3.0f))))  {
        goto b70_r10;                                   /* 0x44c5f0 jp 0x44c637 */
    }

    /* B68 (0x44c5f2..0x44c5fc) — msg-гейт `unit+0x34`: 0 → exit 0x44d061. */
    if (B0_TST(92, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44c5f7 je 0x44d061 */
    }

    /* B69 (0x44c5fd..0x44c636) — log-block G, литерал 0x4eed1c (тот же, что
     * B62); ctor объекта A (`frame+0x10`); `ADD EBP,0x18`; dword trylevel 0xe
     * (0x44c61e `[esp+0x160]` при esp = frame−0xc ⇒ frame+0x154, слот SEH —
     * CONTRADICTIONS #16); 413060(dst `frame+0x20`, arg2 = unit_x, arg3 = A);
     * байт trylevel 0xf; `JMP 0x44c6b6` (B73, R11 — тот же cursor tail, что
     * B41/B62/B65). */
    {
        void *str_a = frame + 0x10;

        native::string_ctor(str_a, native::literal(0x4eed1c)); /* 0x44c5fd..0x44c606 */
        unit_x += 0x18;                                 /* 0x44c610 ADD EBP,0x18 */
        ebx = 0xe;                                      /* 0x44c618 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c61e (dword) */
        tail_str = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c625 */
        frame[0x154] = 0xf;                             /* 0x44c62d */
    }
    goto b73_cursor_tail;                               /* 0x44c635 jmp 0x44c6b6 */

b70_r10:
B0_LBL(61);
    /* B70 (0x44c637..0x44c672) — повторная сборка triple (result+4..0xc,
     * указатель `[esp+0x28]` ⇒ frame+0x1c); 45c370 → лента; 45b3e0 (ECX =
     * лента, arg1..arg3 = triple, `RET $0xc`) → AL. На сайте тестируется
     * `%al`, поэтому результат маскируется до `unsigned char` (как B63).
     * AL == 0 → back-edge `0x44c365` (метка b46); иначе fall → B71. */
    res = native::game_state_get(unit_x, 0);            /* 0x44c63b */
    memcpy(band_triple, (const unsigned char *)res + 4, 12); /* 0x44c640..0x44c656 */
    *(void **)(frame + 0x1c) = band_triple;             /* 0x44c65b mov %esp,0x28(%esp) */
    head = b0_band_head(unit_x + 0x138);                /* 0x44c65f */
    if (B0_TST(93, ((unsigned char)b0_band_target_moved(head, band_triple[0], b0_float_bits(band_triple[1]), band_triple[2]) == 0)))  {
        goto b46;                     /* 0x44c66d je 0x44c365 — back-edge в R7 B46 */
    }

    /* B71 (0x44c673..0x44c67d) — msg-гейт `unit+0x34`: 0 → exit 0x44d061;
     * иначе fall → B72 (R11). */
    if (B0_TST(94, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44c678 je 0x44d061 */
    }
    goto b72_r11;                                       /* fall 0x44c67e → R11 */

b72_r11:
B0_LBL(62);
    /* B72 (0x44c67e..0x44c6b5) — log-block H, литерал 0x4eec68; клон B62 ради
     * 413060-формы: ctor объекта A (`frame+0x10`); `ADD EBP,0x18`;
     * `MOV EBX,0x10`; dword trylevel (0x44c69f `[esp+0x160]` при esp = frame−0xc
     * ⇒ `frame+0x154`, CONTRADICTIONS #16); 413060(dst `frame+0x20`,
     * arg2 = unit_x, arg3 = A); байт trylevel 0x11; fall → B73. */
    {
        void *str_a = frame + 0x10;

        native::string_ctor(str_a, native::literal(0x4eec68)); /* 0x44c67e..0x44c687 */
        unit_x += 0x18;                                 /* 0x44c691 ADD EBP,0x18 */
        ebx = 0x10;                                     /* 0x44c699 */
        *(unsigned int *)(frame + 0x154) = ebx;         /* 0x44c69f (dword) */
        tail_str = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c6a6 */
        frame[0x154] = 0x11;                            /* 0x44c6ae */
    }
    goto b73_cursor_tail;                               /* fall 0x44c6b6 */

b55:
B0_LBL(63);
    /* B55 (0x44c3ed..0x44c407) — band-loop тело A: курсор ленты должен
     * указывать на pos-поле result (`[result+0x14]`, адресное поле).
     * unit_x на входе НЕ сдвинут: единственный вход — fall B54 (0x44c3ed),
     * где `ADD EBP,0x18` не исполнялся (сдвиги B21 @0x44c035 и B30 @0x44c197
     * уходят `jmp 0x44cee6`, B41 — `jmp 0x44c6b6`), т.е. unit_x == unit.
     * `push 0; CALL 442570` → EDI = result. */
    res = native::game_state_get(unit, 0);              /* 0x44c3ed..0x44c3f1 */
    if (B0_TST(95, (native::band_cursor(unit + 0x138) != *(void *const *)((const unsigned char *)res + 0x14))))  {
        goto b77_join;                                  /* 0x44c402 jne 0x44c715 */
    }

    /* B56 (0x44c408..0x44c42b) — позиция ctx → out-тройка `frame+0x20`
     * (`lea 0x24(%esp)` при уже pushed `result+4`), затем 403e80(out, result+4).
     * Натив: первый push (&result+4) остаётся на стеке до `ret $0x4` самого
     * 403e80 (ECX = EAX = out). AL=0 (позиция не совпала) → B59. */
    res = native::game_state_get(unit, 0);              /* 0x44c408..0x44c40c */
    b0_read_pos_from_ctx(unit + 0x138, out);            /* 0x44c41a..0x44c41c */
    if (B0_TST(96, (!b0_pos3_equal(out, (const unsigned char *)res + 4))))  {
        goto b59;                                       /* 0x44c42a je 0x44c489 */
    }

    /* B57 (0x44c42c..0x44c436) — msg-гейт `unit+0x34`: 0 → выход 0x44d061. */
    if (B0_TST(97, (*(const unsigned char *)(unit + 0x34) == 0)))  {
        goto b0_exit_44d061;                            /* 0x44c431 je 0x44d061 */
    }

    /* B58 (0x44c437..0x44c488) — cursor-блок D (литерал 0x4eed5c): ctor
     * строки A `frame+0x10`; `ADD EBP,0x18` (без отката — путь уходит в
     * хвост B74); dword trylevel = 8 (0x44c453); 413060 (dst `frame+0x20`,
     * arg2 = unit_x, arg3 = A); trylevel 9; c_str → OutputDebugStringA;
     * trylevel 8; `JMP 0x44c6cb`. */
    {
        void *str_a = frame + 0x10;
        void *str_r;

        native::string_ctor(str_a, native::literal(0x4eed5c)); /* 0x44c437..0x44c440 */
        unit_x += 0x18;                                 /* 0x44c44a ADD EBP,0x18 */
        *(unsigned int *)(frame + 0x154) = 8;           /* 0x44c453 (dword) */
        str_r = native::string_format(frame + 0x20, unit_x, str_a); /* 0x44c45e */
        frame[0x154] = 9;                               /* 0x44c468 */
        native::output_debug_string_a(native::string_c_str(str_r)); /* 0x44c470..0x44c476 */
        frame[0x154] = 8;                               /* 0x44c47c */
    }
    goto b74_cursor_tail;                               /* 0x44c484 jmp 0x44c6cb */

b59:
B0_LBL(64);
    /* B59 (0x44c489..0x44c49b) — код `unit+0x170` против 1; EDI := 1
     * (живёт до R9 B66). ≠1 → B67 (R10). unit_x здесь не сдвинут. */
    if (B0_TST(98, (*(const int *)(unit + 0x170) != 1)))  {
        goto b67_r10;                                   /* 0x44c496 jne 0x44c5b1 */
    }

    /* B60 (0x44c49c..0x44c4dc) — triple = result+4..+0xc на стек; указатель
     * на него — `frame+0x1c` (0x44c4c0); 45c370 → лента, 45b150 (ECX = лента,
     * arg1..arg3 = triple, `RET $0xc`) → ST0; сравнение с `[0x4eceb0]` = 4.0f
     * (0x40800000, bit-exact): `TEST AH,0x41; JP` ⟺ value > 4.0f или
     * unordered → B63 (R9); иначе fall → B61 (R9). Тело 45b150 портировано
     * (s6_band_interp_gate); диспетчер — b0_band_interp_gate. */
    res = native::game_state_get(unit, 0);              /* 0x44c49c..0x44c4a0 */
    memcpy(band_triple, (const unsigned char *)res + 4, 12); /* 0x44c4a5..0x44c4bb */
    *(void **)(frame + 0x1c) = band_triple;             /* 0x44c4c0 mov %esp,0x28(%esp) */
    head = b0_band_head(unit + 0x138);                  /* 0x44c4be..0x44c4c4 */
    gate = b0_band_interp_gate(head, band_triple[0],
                               b0_float_bits(band_triple[1]),
                               band_triple[2]);         /* 0x44c4c9..0x44c4cb */
    if (B0_TST(99, (b0_gate_41_jp(gate, 4.0f))))  {
        goto b63_r9;                                    /* 0x44c4db jp 0x44c525 */
    }
    goto b61_r9;                                        /* fall 0x44c4dd → R9 */


b16:
B0_LBL(65);
    /* B16 (0x44bfb8..0x44bfd3) — цепочка ×1, NULL-гейт → B20. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bfb8..0x44bfcb */
    if (B0_TST(100, (flags_obj == NULL)))  {
        goto b20;                                       /* 0x44bfd2 je 0x44c017 */
    }

    /* B17 (0x44bfd4..0x44bff0) — цепочка ×2, `TESTB $2,(%eax)`: взятый бит →
     * B19 (448c30), иначе — B18. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bfd4..0x44bfe7 */
    if (B0_TST(101, ((*(const unsigned char *)flags_obj & 0x2) != 0)))  {
        goto b19;                                       /* 0x44bfef jne 0x44c00e */
    }

    /* B18 (0x44bff1..0x44c00d) — цепочка ×3, `TESTB $4,(%eax)`: бит снят →
     * B20, иначе fall → B19. */
    flags_obj = b0_player_flag_chain(unit);             /* 0x44bff1..0x44c004 */
    if (B0_TST(102, ((*(const unsigned char *)flags_obj & 0x4) == 0)))  {
        goto b20;                                       /* 0x44c00c je 0x44c017 */
    }

b19:
B0_LBL(66);
    /* B19 (0x44c00e..0x44c016) — cursor/mode update с аргументом 0x40. */
    native::cursor_mode_update(unit, 0x40);             /* 0x44c00e..0x44c012 */

b20:
B0_LBL(67);
    /* B20 (0x44c017..0x44c021) — msg-флаг `unit+0x34`: 0 → join `0x44cee6`. */
    if (B0_TST(103, (*(const unsigned char *)(unit_x + 0x34) == 0)))  {  /* 0x44c017..0x44c01c */
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
B0_LBL(68);
    /* --- R21 B135..B137 (0x44cee6..0x44cf0b, GUI): join-хвост ------------- *
     * Входы: R3 B20 `je` (0x44c01c), R3 B21 `jmp` (0x44c086), R4 B29 `je`
     * (0x44c17e), R4 B30 `jmp` (0x44c1e8), R20 B134 fall. На всех ESP = frame
     * (`446a80` B134 снимает `sub 0x3c` через `ret 0x3c`), поэтому слот
     * `[esp+0x164]` = `frame+0x164` = param_3. EBP на входах B21/B30 сдвинут
     * (+0x18), но в R21 EBP не читается; ESI/EDI/EBX здесь тоже не читаются.
     * Порядок: out=1 → dword trylevel −1 (ДО вызова) → dtor строки msg
     * `frame+0x58` (поле +0x0c объекта R19 B126 `frame+0x4c`, как R16 B99) →
     * `0x44d072` (`xor al,al`, без out-write). */
    {
        void *p3 = *(void **)(frame + 0x164);

        if (B0_TST(104, (p3 != NULL)))  {
            *(int *)p3 = 1;                             /* 0x44cef1 movl $1,(%eax) */
        }
    }
    *(unsigned int *)(frame + 0x154) = 0xffffffffu;     /* 0x44cefb..0x44cf02 (dword) */
    native::msg_string_dtor(frame + 0x58);              /* 0x44cef7..0x44cf06 */
    B0_EXIT(134);
    return 0;                                           /* 0x44cf0b jmp 0x44d072 */

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        B0_EXIT(135);
        return 0;            /* SEH-рамка 0x44bdd0, как s6_ai_update */
    }
}

/* Точка входа порта (ABI натива: ECX = unit, 3 стек-аргумента, RET 0xc).
 B0_EXIT(134);
 * Тело — под B0_BODY_COMPLETE (R1..R22 портированы); пока макрос не взведён,
 * функция возвращает 0 и детур разоружён (b0_port_enabled). Тело остаётся
 * скомпилированным и типизированным — `(void)&` ниже, чтобы /W3 не снял его
 B0_EXIT(135);
 * как неиспользуемый. */
int b0_battle_ai_dispatch(void *unit, unsigned char *frame)
{
#if B0_BODY_COMPLETE
    return b0_body_r1_r2(unit, frame);      /* R1..R22; R23 — нативный эпилог */
#else
    /* Взвод не включён — единственный вход детура остаётся пустым. */
    (void)&b0_body_r1_r2;
    (void)unit;
    (void)frame;
    return 0;
#endif
}