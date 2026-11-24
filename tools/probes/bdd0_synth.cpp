/* tools/probes/bdd0_synth.cpp — генератор синтетического захвата v12.
 *
 * Зачем: реальный захват v12 требует прогона игры (LDHOOK_BATTLE_CAPTURE_SNAP=1),
 * а существующие файлы (ab2_native.jsonl) сняты до v12. Генератор даёт
 * САМОСОГЛАСОВАННЫЙ сценарий: арена материализуется тем же arena.cpp, тело
 * b0_battle_ai_dispatch исполняется с модельным нативным слоем
 * (native_model.cpp), и каждый вызов листа пишется в поток bdd0_leaf. Поэтому
 * replay в харнессе воспроизводит ровно тот же тик — это проверка механики
 * харнесса, а не сверка с игрой.
 *
 * Запуск: bdd0_synth.exe <out.jsonl> [profile]
 *   profiles: grid (R12/R14/R17, цепочка unit+0xe0), simple (B1..B21),
 *             band (B10/B11 → R22), band0 (begin=0/end!=0 → голова NULL;
 *             отдельный файл, в "all" не входит), all (grid+simple+band).
 * Сырые (игровые) адреса — фиктивные, но консистентные: self, unit+0xe0,
 * [+0x44], [+0x238], band_begin, ai_ctx.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "bdd0/harness.h"
#include "bdd0/model.h"
#include "../ldhook/battle/battle.h"

/* ------------------------------ base64 ---------------------------------- */
static const char b64_tab[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64(const unsigned char *in, int n)
{
    std::string out;
    int i = 0;
    while (i + 2 < n) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) |
                     in[i + 2];
        out += b64_tab[(v >> 18) & 63];
        out += b64_tab[(v >> 12) & 63];
        out += b64_tab[(v >> 6) & 63];
        out += b64_tab[v & 63];
        i += 3;
    }
    if (n - i == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out += b64_tab[(v >> 18) & 63];
        out += b64_tab[(v >> 12) & 63];
        out += "==";
    } else if (n - i == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        out += b64_tab[(v >> 18) & 63];
        out += b64_tab[(v >> 12) & 63];
        out += b64_tab[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

static int fbits(float f) { int b; memcpy(&b, &f, 4); return b; }

/* ------------------------------ профили --------------------------------- */
#define GAME_SELF    0x10000000u
#define GAME_E0      0x10010000u
#define GAME_E0_44   0x10020000u
#define GAME_TREE    0x10030000u
#define GAME_CTX     0x10040000u
#define GAME_BAND    0x10050000u
#define GAME_P3      0x10060000u
#define GAME_SCRATCH 0x0012F000u

struct Profile {
    const char *name;
    int p1, p2;
    float gate114;
    float unit_fc;
    unsigned phase;        /* gs[0] — состояние игры (0/non-3 → R4/R5) */
    int u170;
    int band_n;
    int band_begin_zero;   /* лента длиной band_n, но begin = 0 (end != 0) */
    int no_all;            /* не включать в профиль "all" (отдельный кейс) */
    unsigned gctx0;
    int unit34;            /* msg-гейт unit+0x34 */
    int unit118;           /* → bit3 param_2 */
    float ai_pos[3];       /* B5-тройка unit+0x68..0x70 и кадр+0x2c (ai+0xc);
                            * она же — тройка [ctx+0xc] листа 45c6f0 */
    float gs_pos[3];       /* game state +4..0xc */
    int grid_query;
    int anchor_null;
    unsigned a4n;          /* frame+0x38 (unit+0x74) */
    int post_band_n;       /* v14: длина ленты ПОСЛЕ тика (45eee0 перестроил);
                            * < 0 — ключ "band" в bdd0_res не писать (старый
                            * захват, resync-fallback) */
    int post_band_extra;   /* байты сверх post_band_n*0x18 (некратный шаг —
                            * проверка усечения 45af50); 0 — обычная лента */
    int ctx_null;          /* ai_ctx = 0 → нулевая ветка листа 45c6f0 */
    int gs_nan;            /* 0 — обычные значения; k+1 — gs_pos[k] = NaN
                            * (проверка правила маски 0x44 на unordered) */
    /* v15: ключ "ctx_pos" в bdd0_in — независимый снимок тройки
     * `[*(unit+0x138)+0xc]`. 0 — ключа нет (старый формат, тройка
     * восстанавливается из `w`); 1 — ключ = истинная тройка (лист 45c6f0
     * сверяется как значение); 2 — ключ ИСПОРЧЕН (первый dword иной), запись
     * листа несёт истину — фикстура «битый захват». */
    int ctx_pos_mode;
    /* v15: 458050 пишет вложенный лист 0x456b60 (сайт 0x458142) — проверка,
     * что такие записи не ломают поток тика (харнесс выносит их из окна). */
    int nested_chain;
};

static const Profile k_profiles[] = {
    /* grid: state != 3, фаза > порога → R4 B23 → R5 → band-loop entry → R12
     * (grid-anchor на цепочке unit+0xe0) → R14 (grid_recompute) → R17
     * (per-tick record, вторая deref цепочки). */
    { "grid", 1, 8, 0.0f, -1.0f, 0u, 0, 2, 0, 0, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0x20000000u, 2 },
    /* simple: фаза 3, u170=0 → B1..B8 → B9(≠1) → B12..B21 → join (out=1). */
    { "simple", 1, 0, 0.0f, 1.0f, 3u, 0, 0, 0, 0, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1 },
    /* band: u170=1 и лента длиной 2 → B10/B11 → ранний выход R22. */
    { "band", 1, 0, 0.0f, 1.0f, 3u, 1, 2, 0, 0, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, 2 },
    /* band0: u170=1, end != 0, но begin = 0 → 45af50 даёт 0 (TEST/JZ) →
     * голова NULL. Отличает правило begin==0 от «end != begin». */
    { "band0", 1, 0, 0.0f, 1.0f, 3u, 1, 2, 1, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, 2 },
    /* band_rec: v14-кейс — входная лента ПУСТА (begin=end=0), u170=0
     * (B9 не уводит в R22), тик доходит до R17, где 45eee0 перестраивает
     * ленту в post_band_n элементов. bdd0_res несёт "band":[begin,end];
     * bdd0_in — "band":[0,0]. Так харнесс применяет recorded-band
     * (leaf_value PASS, не SKIP), а не resync. */
    { "band_rec", 1, 8, 0.0f, -1.0f, 3u, 1, 0, 0, 0, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, 1, 0 },
    /* band_trunc: как band_rec, но после-лента некратна шагу: 1*0x18 + 1 = 25.
     * 45af50 обязан усечь 25/0x18 до 1 (не 2): claim сверяет именно усечённый
     * результат. no_all=1. */
    { "band_trunc", 1, 8, 0.0f, -1.0f, 3u, 1, 0, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, 1, 1 },
    /* band_trunc_neg: diff = -25 (end = begin - 25): знаковое деление обязано
     * усечь К НУЛЮ → -1 (а floor дал бы -2). extra = -25 - 0x18 = -49. no_all=1. */
    { "band_trunc_neg", 1, 8, 0.0f, -1.0f, 3u, 1, 0, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, 1, -49 },
    /* pos_null: ai_ctx = 0 → лист 45c6f0 идёт НУЛЕВОЙ веткой натива
     * (`MOV EAX,[ECX]; XOR ECX,ECX; CMP EAX,ECX; JNZ` @0x45c6f0..0x45c6f6):
     * out[0..2] = 0. Это fidelity-кейс: в захватах все 108 вызовов с ai_ctx == 0
     * записали w = [0,0,0]. Отдельный файл (no_all=1). */
    { "pos_null", 1, 0, 0.0f, 1.0f, 3u, 0, 0, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1, 0, 1 },
    /* pos3_eq: тело band-loop B55/B56 (лист 403e80) с РАВНЫМИ тройками.
     * Путь: state == 3 (B12 не уводит в B22) → фаза 0.0 >
     * unit_fc = −1.0 (B13 `jp`) → B14 cmp строк == 0 → B22 `state == 3` → B31
     * `state == 3` → B42. Дальше p1 ≠ 0 → B49, EBX = p2&8 == 0, ctx bit3 снят,
     * лента непустая и count = 2 > 1, state == 3 → fall в B55. band_cursor
     * (модель) и `[gs+0x14]` оба 0 → B55 не уходит; B56 сравнивает тройку
     * ctx с result+4 — они равны ({1,2,3}) ⇒ pos3_equal возвращает 1 (истинная
     * ветка листа, в захватах не наблюдаемая). no_all=1. */
    { "pos3_eq", 1, 0, 0.0f, -1.0f, 3u, 0, 2, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1 },
    /* pos3_neq: тот же путь, но третья пара тройки различается (ctx = 3.0,
     * gs+0xc = 4.0). B6 уводит в b12 (это НОРМАЛЬНЫЙ вход остального тика, а
     * не выход), путь B12→B13→B14→B22→B31→B42→B49→B55 проходит, и на B56
     * 403e80 обязан вернуть 0. Так синтетика покрывает обе ветки листа и
     * ловит реализацию «сравнил только две пары» (она вернула бы 1 и уронила
     * самопроверку генератора). no_all=1. */
    { "pos3_neq", 1, 0, 0.0f, -1.0f, 3u, 0, 2, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 4.0f }, 1, 0, 0u, -1 },
    /* pos3_nan: тот же путь, что pos3_neq, но третья пара — NaN при равных
     * первых двух (1.0/2.0 совпадают, gs+0xc = 0x7fc00000). Маска 0x44
     * тестируется через PF, поэтому unordered идёт в «не равно» (JP) и лист
     * обязан вернуть 0; прочтение маски как «равно или unordered» вернуло бы 1
     * — этот профиль его и ловит. no_all=1. */
    { "pos3_nan", 1, 0, 0.0f, -1.0f, 3u, 0, 2, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1, 0, 0, 3 },
    /* ctx_pos: как pos3_eq по пути, но v15-захват несёт независимую тройку
     * `[*(unit+0x138)+0xc]`. Заявка листа 45c6f0 тогда сверяется побитово
     * (leaf_value PASS на w), а не идёт в «несравнимо». no_all=1. */
    { "ctx_pos", 1, 0, 0.0f, -1.0f, 3u, 0, 2, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1, 0, 0, 0, 1 },
    /* ctx_pos_bad: тот же тик, что "simple" (единственный вызов 45c6f0,
     * тройка не участвует в ветвлениях), но ключ "ctx_pos" ИСПОРЧЕН (x = 4.0
     * вместо 1.0), а запись листа 45c6f0 несёт истинную тройку. Харнесс обязан
     * взять ctx_pos из записи и FAIL leaf_value с `w[0] port=… native=…` —
     * доказательство, что тройка сверяется, а не вакуумна. no_all=1. */
    { "ctx_pos_bad", 1, 0, 0.0f, 1.0f, 3u, 0, 0, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0u, -1, 0, 0, 0, 2 },
    /* chain_nested: путь grid (R12 → 458050), поэтому 0x458050 пишет вложенный
     * лист 0x456b60 (сайт 0x458142, args[0] = world, ret = узел). Запись
     * выносится из окна тика ⇒ rc 0, leaf_seq 1/0/0, nested_leaf=1;
     * ctx_pos есть (как в любом v15-захвате). no_all=1. */
    { "chain_nested", 1, 8, 0.0f, -1.0f, 0u, 0, 2, 0, 1, 0u, 0, 0,
      { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 1, 0, 0x20000000u, 2, 0, 0,
      0, 1, 1 }
};

/* Снимок по профилю: unit 0x180, встроенный BattleAI 0x80 (unit+0x138),
 * кадр 0x168, лента band_n*0x18, game state 0x20. */
static void fill_record(TickRecord &t, const Profile &pf, int seq, int tick)
{
    memset(&t, 0, sizeof(t));
    t.seq = seq;
    t.tick = tick;
    t.unit_index = 1;
    t.p1 = pf.p1;
    t.p2 = pf.p2;
    t.p3 = (int)GAME_P3;
    t.u170_in = pf.u170;
    t.gate114 = (unsigned)fbits(pf.gate114);
    t.self_raw = GAME_SELF;
    t.has_frame = t.has_unit = t.has_ai = t.has_gs = 1;

    /* кадр: param_1/2/3 */
    memcpy(t.frame + 0x15c, &t.p1, 4);
    memcpy(t.frame + 0x160, &t.p2, 4);
    memcpy(t.frame + 0x164, &t.p3, 4);
    memcpy(t.frame + 0x38, &pf.a4n, 4);
    memcpy(t.frame + 0x2c, &pf.ai_pos[0], 4);
    memcpy(t.frame + 0x30, &pf.ai_pos[1], 4);
    memcpy(t.frame + 0x34, &pf.ai_pos[2], 4);

    /* unit */
    memcpy(t.unit + 0x114, &t.gate114, 4);
    memcpy(t.unit + 0xfc, &pf.unit_fc, 4);
    memcpy(t.unit + 0x170, &pf.u170, 4);
    t.unit[0x34] = (unsigned char)pf.unit34;
    t.unit[0x118] = (unsigned char)pf.unit118;
    memcpy(t.unit + 0xe0, &t.e0, 4);           /* e0 заполняется ниже */
    t.e0 = GAME_E0;
    t.e0_44 = GAME_E0_44;
    t.tree = GAME_TREE;
    t.has_chain = 1;
    memcpy(t.unit + 0xe0, &t.e0, 4);
    t.unit[0x138] = 0;                          /* ai_ctx пишется ниже (4 Б) */
    {
        unsigned ctx = pf.ctx_null ? 0u : GAME_CTX;
        memcpy(t.unit + 0x138, &ctx, 4);
    }
    memcpy(t.unit + 0x128, &pf.unit_fc, 4);
    memcpy(t.unit + 0x12c, &pf.unit_fc, 4);
    memcpy(t.unit + 0xa4, &pf.gs_pos[0], 4);
    memcpy(t.unit + 0xa8, &pf.gs_pos[1], 4);
    memcpy(t.unit + 0xac, &pf.gs_pos[2], 4);
    memcpy(t.unit + 0x74, &pf.a4n, 4);

    /* встроенный BattleAI (unit+0x138..+0x1b7): game_ctx, pos-тройка +0xc,
     * лента: vector+0x00 = 1 (непустая), begin/end/cap/dirty. */
    memset(t.ai, 0, sizeof(t.ai));
    {
        unsigned ctx = pf.ctx_null ? 0u : GAME_CTX;
        unsigned begin = (pf.band_n > 0 && !pf.band_begin_zero) ? GAME_BAND : 0u;
        unsigned end = begin + (unsigned)pf.band_n * BDD0_BAND_ELEM;
        unsigned cap = end;
        unsigned dirty = 1;
        unsigned one = 1;
        memcpy(t.ai + 0x00, &ctx, 4);
        memcpy(t.ai + 0x0c, &pf.ai_pos[0], 4);
        memcpy(t.ai + 0x10, &pf.ai_pos[1], 4);
        memcpy(t.ai + 0x14, &pf.ai_pos[2], 4);
        memcpy(t.ai + 0x3c, &one, 4);          /* первое dword объекта ленты */
        memcpy(t.ai + 0x5c, &begin, 4);
        memcpy(t.ai + 0x60, &end, 4);
        memcpy(t.ai + 0x64, &cap, 4);
        memcpy(t.ai + 0x7c, &dirty, 4);
    }
    /* unit-снимок 0x180 и ai-блок (unit+0x138..+0x1b7) перекрываются: в живом
     * захвате это одна и та же память. Приводим их к согласию — u170 лежит
     * внутри BattleAI (unit+0x170 = ai+0x38). */
    memcpy(t.ai + 0x38, &t.u170_in, 4);

    /* лента */
    t.band_n = pf.band_n;
    t.has_band = (pf.band_n > 0) ? 1 : 0;
    /* begin = 0 → вектор пуст: сырой адрес ленты в снимке тоже 0 */
    t.band_raw = (pf.band_n > 0 && !pf.band_begin_zero) ? GAME_BAND : 0u;
    memset(t.band, 0, sizeof(t.band));
    for (int i = 0; i < pf.band_n; i++) {
        float x = (float)i;
        memcpy(t.band + i * BDD0_BAND_ELEM + 0x00, &x, 4);
    }

    /* game state: фаза 0.., pos-тройка, player (не NULL), cursor */
    memset(t.gs, 0, sizeof(t.gs));
    memcpy(t.gs + 0x00, &pf.phase, 4);
    memcpy(t.gs + 0x04, &pf.gs_pos[0], 4);
    memcpy(t.gs + 0x08, &pf.gs_pos[1], 4);
    memcpy(t.gs + 0x0c, &pf.gs_pos[2], 4);
    /* pos3_nan: один элемент pos-тройки состояния — quiet NaN (0x7fc00000). */
    if (pf.gs_nan > 0 && pf.gs_nan <= 3) {
        unsigned nan = 0x7fc00000u;

        memcpy(t.gs + 0x04 + (pf.gs_nan - 1) * 4, &nan, 4);
    }
    {
        unsigned player = GAME_SELF + 0x800;
        memcpy(t.gs + 0x10, &player, 4);
    }
    t.has_gctx0 = 1;
    t.gctx0 = pf.gctx0;
    /* v15: независимый снимок тройки `[*(unit+0x138)+0xc]` — та же позиция
     * юнита, что в профиле (ai_pos). Арена получает её от записи, а не от
     * модели; модель тройку только читает через объектный граф. */
    t.has_ctx_pos = (pf.ctx_pos_mode != 0) ? 1 : 0;
    for (int k = 0; k < 3; k++) {
        t.ctx_pos[k] = fbits(pf.ai_pos[k]);
    }
}

/* -------------------------------- вывод --------------------------------- */
static void write_jsonl(FILE *f, const TickRecord &in, const TickRecord &post,
                        const Profile &pf, int al, int out, int u170,
                        const unsigned m[3], int seq)
{
    unsigned self = in.self_raw;
    unsigned ai_ctx;
    unsigned band_begin = in.band_raw;
    unsigned band_end = band_begin + (unsigned)in.band_n * BDD0_BAND_ELEM;
    memcpy(&ai_ctx, in.ai, 4);       /* game_ctx = *(unit+0x138); 0 — нет ctx */
    /* v14: после-состояние ленты — из профиля (45eee0 перестроил). */
    unsigned post_begin = (pf.post_band_n > 0) ? GAME_BAND : 0u;
    unsigned post_end = post_begin + (unsigned)pf.post_band_n * BDD0_BAND_ELEM +
                        (unsigned)pf.post_band_extra;
    int has_post = (pf.post_band_n >= 0);
    std::string fb = b64(in.frame, BDD0_FRAME_LEN);
    std::string ub = b64(in.unit, BDD0_UNIT_LEN);
    std::string ab = b64(in.ai, BDD0_AI_LEN);
    std::string bb = (in.band_n > 0) ? b64(in.band, in.band_n * BDD0_BAND_ELEM) : "";
    std::string gb = b64(in.gs, BDD0_GS_LEN);
    /* v15: ключ "ctx_pos" (независимый снимок). mode 2 — ИСПОРЧЕННЫЙ: первый
     * dword иной, чем у записи листа (модель пишет истину) — фикстура дефекта. */
    int cp[3];
    memcpy(cp, in.ctx_pos, sizeof(cp));
    if (pf.ctx_pos_mode == 2) {
        float tx = pf.ai_pos[0] + 3.0f;

        memcpy(&cp[0], &tx, 4);
    }

    fprintf(f, "{\"v\":15,\"kind\":\"bdd0_in\",\"seq\":%d,\"tick\":%d,\"unit\":%d,"
               "\"fn\":\"44bdd0\",\"self\":%u,\"p1\":%d,\"p2\":%d,\"p3\":%d,"
               "\"ai_ctx\":%u,\"u170\":%d,\"gate114\":%u,\"band\":[%u,%u],"
               "\"frame_b64\":\"%s\","
               "\"unit_b64\":\"%s\",\"ai_b64\":\"%s\",\"band_begin\":%u,"
               "\"band_end\":%u,\"band_cap\":%u,\"band_dirty\":1,\"band_count\":%d,"
               "\"band_b64\":\"%s\",\"gs_b64\":\"%s\",\"e0\":%u,\"e0_44\":%u,"
               "\"tree\":%u,\"gctx0\":%u",
            in.seq, in.tick, in.unit_index, self, in.p1, in.p2, in.p3,
            ai_ctx, in.u170_in, in.gate114, band_begin, band_end,
            fb.c_str(), ub.c_str(), ab.c_str(),
            band_begin, band_end, band_end, in.band_n, bb.c_str(), gb.c_str(),
            in.e0, in.e0_44, in.tree, in.gctx0);
    /* v15: независимая pos-тройка листа 45c6f0 (`[*(unit+0x138)+0xc]`).
     * Ключ пишут только профили, которые его моделируют (реальные v15-захваты
     * пишут его всегда: null при NULL-ctx или сбое чтения). */
    if (pf.ctx_pos_mode != 0) {
        fprintf(f, ",\"ctx_pos\":[%d,%d,%d]", cp[0], cp[1], cp[2]);
    }
    fputs(",\"trap\":0}\n", f);

    for (int i = 0; i < model_leaf_count(); i++) {
        const LeafRec *r = model_leaf_at(i);
        fprintf(f, "{\"v\":12,\"kind\":\"bdd0_leaf\",\"seq\":%d,\"order\":%d,"
                   "\"site\":\"%x\",\"va\":\"%x\",\"ecx\":%d,\"args\":[",
                seq, r->order, r->site, r->va, r->ecx);
        for (int k = 0; k < r->nargs; k++) {
            fprintf(f, "%s%d", k ? "," : "", r->args[k]);
        }
        fputs("],\"ret\":", f);
        fprintf(f, "%d", r->ret);
        if (r->nw > 0) {
            fputs(",\"w\":[", f);
            for (int k = 0; k < r->nw; k++) {
                fprintf(f, "%s%d", k ? "," : "", r->w[k]);
            }
            fputc(']', f);
        }
        fputs("}\n", f);
    }

    for (int i = 0; i < model_native_count(); i++) {
        const NativeRec *r = model_native_at(i);
        fprintf(f, "{\"v\":12,\"kind\":\"bdd0_native\",\"seq\":%d,\"order\":%d,"
                   "\"site\":\"%x\",\"va\":\"%x\",",
                seq, r->order, r->site, r->va);
        if (r->slot == 1) fputs("\"slot\":\"u+0x28\",", f);
        else if (r->slot == 2) fputs("\"slot\":\"u+0x2c\",", f);
        else if (r->slot == 3) fputs("\"slot\":\"u+0x38\",", f);
        else if (r->slot == 4) fputs("\"slot\":\"u+0x40\",", f);
        fprintf(f, "\"ecx\":%d,\"args\":[", r->ecx);
        for (int k = 0; k < r->nargs; k++) {
            fprintf(f, "%s%d", k ? "," : "", r->args[k]);
        }
        fprintf(f, "],\"ret\":%d,\"ret_kind\":\"%s\"", r->ret,
                (r->kind == 1) ? "al" : (r->kind == 2) ? "f32"
                : (r->kind == 3) ? "void" : "int");
        if (r->nw > 0) {
            fputs(",\"w\":[", f);
            for (int k = 0; k < r->nw; k++) {
                fprintf(f, "%s%d", k ? "," : "", r->w[k]);
            }
            fputc(']', f);
        }
        /* v14: ww-пары [unit_off, value] — после-состояние натива
         * (45eee0: BattleAI+0x00..0x38; band_rec: +0x3c+0x20/+0x24). */
        if (r->nww > 0) {
            fputs(",\"ww\":[", f);
            for (int k = 0; k < r->nww; k++) {
                fprintf(f, "%s%d", k ? "," : "", r->ww[k]);
            }
            fputc(']', f);
        }
        fputs(",\"trap\":0}\n", f);
    }

    fprintf(f, "{\"v\":11,\"kind\":\"bdd0_res\",\"seq\":%d,\"tick\":%d,\"unit\":%d,"
               "\"fn\":\"44bdd0\",\"al\":%d,\"u170\":%d,\"out\":%d,"
               "\"m0\":%u,\"m1\":%u,\"m2\":%u,\"anc\":%d,\"tag\":%u,"
               "\"tri0\":%u,\"tri1\":%u,\"tri2\":%u,\"a4n\":%d,",
            seq, in.tick, in.unit_index, al, u170, out, m[0], m[1], m[2],
            pf.anchor_null ? 0 : 1, 0xebu, m[0], m[1], m[2], pf.a4n ? 1 : 0);
    if (has_post) fprintf(f, "\"band\":[%u,%u],", post_begin, post_end);
    fputs("\"trap\":0}\n", f);
}

/* Один тик: арена → модельный тик → запись. Возврат 0 при успехе. */
static int run_tick(FILE *f, const Profile &pf, int seq, int verbose)
{
    TickRecord in;
    fill_record(in, pf, seq, seq);

    Arena *arena = arena_build(&in);
    if (arena == NULL) {
        fprintf(stderr, "arena build failed\n");
        return 1;
    }
    arena_set_current(arena);

    ModelEnv env;
    env.self_game = GAME_SELF;
    env.e0_game = in.e0;
    env.e0_44_game = in.e0_44;
    env.tree_game = in.tree;
    env.band_game = in.band_raw;
    env.scratch_game = GAME_SCRATCH;
    env.ctx_pos[0] = pf.ai_pos[0];
    env.ctx_pos[1] = pf.ai_pos[1];
    env.ctx_pos[2] = pf.ai_pos[2];
    /* v14: после-состояние ленты для модели 45eee0 (ww-пара [0x5c,0x60]).
     * post_band_n < 0 — старая семантика без ww (resync-fallback). */
    env.band_post_game = (pf.post_band_n > 0) ? GAME_BAND : 0u;
    env.band_post_n = (pf.post_band_n > 0) ? pf.post_band_n : 0;
    env.band_post_extra = (pf.post_band_n > 0) ? pf.post_band_extra : 0;
    ModelCfg cfg;
    cfg.grid_query = pf.grid_query;
    cfg.anchor_null = pf.anchor_null;
    cfg.interp_add = 0.0f;
    cfg.moved = 0;
    cfg.nested_chain = pf.nested_chain;
    model_set(env, cfg);
    model_leaf_reset();

    bdd0_leaf_begin(seq, NULL, 0);      /* модельный слой не читает поток */
    int al = b0_battle_ai_dispatch(arena_region_w(arena, "unit"),
                                   arena_region_w(arena, "frame"));
    int out = -1;
    if (arena_out_known(arena)) memcpy(&out, arena_out_slot(arena), 4);
    int u170 = 0;
    memcpy(&u170, arena_region_r(arena, "unit") + 0x170, 4);
    unsigned m[3];
    memcpy(m, arena_region_r(arena, "unit") + 0xa4, 12);

    TickRecord post = in;               /* снапшот до тика уже снят */
    write_jsonl(f, in, post, pf, al & 0xff, out, u170, m, seq);
    if (verbose) {
        unsigned f164 = 0;
        memcpy(&f164, arena_region_r(arena, "frame") + 0x164, 4);
        fprintf(stderr, "synth[%s] seq=%d al=%d out=%d u170=%d out_known=%d "
                        "f164=0x%08x out_slot=0x%08x m=0x%08x,0x%08x,0x%08x "
                        "leaves=%d\n",
                pf.name, seq, al & 0xff, out, u170, arena_out_known(arena),
                f164, (unsigned)(size_t)arena_out_slot(arena), m[0], m[1], m[2],
                model_leaf_count());
        for (int i = 0; i < model_leaf_count(); i++) {
            fprintf(stderr, "  leaf %d va=0x%x ecx=0x%08x ret=0x%08x\n",
                    model_leaf_at(i)->order, model_leaf_at(i)->va,
                    (unsigned)model_leaf_at(i)->ecx,
                    (unsigned)model_leaf_at(i)->ret);
        }
    }
    arena_set_current(NULL);
    arena_release(arena);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("usage: bdd0_synth.exe <out.jsonl> "
              "[grid|simple|band|band0|band_rec|band_trunc|band_trunc_neg|"
              "pos_null|pos3_eq|pos3_neq|pos3_nan|ctx_pos|ctx_pos_bad|"
              "chain_nested|all] [-v]\n", stderr);
        return 2;
    }
    const char *out = argv[1];
    const char *which = (argc > 2 && argv[2][0] != '-') ? argv[2] : "all";
    int verbose = (argc > 2 && strcmp(argv[argc - 1], "-v") == 0) ? 1 : 0;

    FILE *f = fopen(out, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", out);
        return 2;
    }
    int rc = 0, seq = 0;
    for (size_t i = 0; i < sizeof(k_profiles) / sizeof(k_profiles[0]); i++) {
        const Profile &pf = k_profiles[i];
        if (pf.no_all && strcmp(which, "all") == 0) continue;
        if (strcmp(which, "all") != 0 && strcmp(which, pf.name) != 0) continue;
        seq++;
        rc |= run_tick(f, pf, seq, verbose);
    }
    fclose(f);
    if (rc == 0) fprintf(stderr, "synth: %s (%d ticks)\n", out, seq);
    return rc;
}
