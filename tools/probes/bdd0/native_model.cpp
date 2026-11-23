/* tools/probes/bdd0/native_model.cpp — модельный нативный слой (только для
 * bdd0_synth.exe, НЕ входит в харнесс).
 *
 * Десять логических листьев отвечают детерминированно и из снимка:
 *   45c6f0 ctx_pos_get      — пишет 3 dword `[ctx+0xc]` в out
 *   45c370 band_head        — begin==0 ? NULL : (count ? ai+0x3c : NULL)
 *   45af50 band_count       — begin==0 ? 0 : ([+0x24]−[+0x20])/0x18
 *   45c730 band_cursor      — `[[[ctx]+0xc]+0xc]+0x234` (в синтетике NULL)
 *   45b150 band_interp_gate — ST(0) = arg1 + cfg.interp_add
 *   45b3e0 band_target_moved— cfg.moved
 *   403e80 pos3_equal       — почленное равенство float-троек
 *   458050 grid_anchor      — cfg.anchor_null ? NULL : tree (сырой адрес)
 *   45e1d0 grid_recompute   — out3 = {1,2,3}f
 *   44bbf0 grid_query       — cfg.grid_query
 * v12: прочие RECORD-нативы тела (game_state_get, player_get, phase_time,
 * player_flag_get/flags, unit_ai_tick, ...) тоже пишут `bdd0_native` с тем же
 * общим order. Каждый вызов пишется в поток с `ret` в ПРОСТРАНСТВЕ ИГРЫ.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "../../ldhook/battle/ported.h"
#include "harness.h"
#include "model.h"

static ModelEnv g_env;
static ModelCfg g_cfg;
static std::vector<LeafRec> g_rec;
static std::vector<NativeRec> g_nat;
static int g_order = 0;

void model_set(const ModelEnv &env, const ModelCfg &cfg) { g_env = env; g_cfg = cfg; }
void model_leaf_reset(void) { g_rec.clear(); g_nat.clear(); g_order = 0; }
int model_leaf_count(void) { return (int)g_rec.size(); }
const LeafRec *model_leaf_at(int i)
{
    return (i >= 0 && i < (int)g_rec.size()) ? &g_rec[i] : NULL;
}
int model_native_count(void) { return (int)g_nat.size(); }
const NativeRec *model_native_at(int i)
{
    return (i >= 0 && i < (int)g_nat.size()) ? &g_nat[i] : NULL;
}

/* v12: реплей-слой в генераторе не читает поток. */
void bdd0_native_begin(int seq, const NativeRec *v, int n)
{ (void)seq; (void)v; (void)n; }
int bdd0_native_calls(void) { return 0; }
int bdd0_native_violations(void) { return 0; }
const char *bdd0_native_violation_why(void) { return ""; }
int bdd0_native_consumed(void) { return 0; }
const NativeRec *bdd0_native_pending(void) { return NULL; }
const NativeCallLogEnt *bdd0_native_calllog(int *n)
{ if (n) *n = 0; return NULL; }
const NativeRec *bdd0_native_record_at(int i) { (void)i; return NULL; }

static int fbits(float f) { int b; memcpy(&b, &f, 4); return b; }

/* Модель НЕ читает записанный поток (она его пишет): интерфейс replay-слоя
 * вырожден, чтобы bdd0_synth линковался без native_replay.cpp. */
void bdd0_leaf_begin(int seq, const LeafRec *v, int n) { (void)seq; (void)v; (void)n; }
int bdd0_leaf_calls(void) { return 0; }
int bdd0_leaf_violations(void) { return 0; }
const char *bdd0_leaf_violation_why(void) { return ""; }
const CallLogEnt *bdd0_calllog(int *n) { if (n) *n = 0; return NULL; }
void bdd0_calllog_reset(void) {}
/* Доступ проверки leaf_args к сопоставленной записи: в генераторе потока
 * записей нет (он их создаёт), поэтому «нечего сверять». */
const LeafRec *bdd0_leaf_record_at(int i) { (void)i; return NULL; }
int bdd0_leaf_applied_w(int i, int *w3) { (void)i; (void)w3; return 0; }
int bdd0_leaf_consumed(void) { return 0; }
const LeafRec *bdd0_leaf_pending(void) { return NULL; }
/* Заявки портированных листьев: генератор их не судит (поток он создаёт сам,
 * проверки не запускаются). */
int bdd0_leaf_claims(void) { return 0; }
const LeafClaimEnt *bdd0_leaf_claim_at(int i) { (void)i; return NULL; }

static int record(int va, const void *ecx, int nargs, const int *args, int ret,
                  const int *w, int nw)
{
    LeafRec r;
    memset(&r, 0, sizeof(r));
    r.order = g_order++;
    r.va = va;
    r.ecx = (int)(size_t)ecx;
    r.nargs = (nargs > 8) ? 8 : nargs;
    for (int k = 0; k < r.nargs; k++) r.args[k] = args[k];
    r.ret = ret;
    if (w != NULL && nw > 0) {
        r.nw = (nw > 3) ? 3 : nw;
        for (int k = 0; k < r.nw; k++) r.w[k] = w[k];
    }
    g_rec.push_back(r);
    return ret;
}

static int record_native(int site, int target, int slot, int kind, int ecx,
                         const int *args, int nargs, int ret, const int *w,
                         int nw)
{
    NativeRec r;
    r.order = g_order++;
    r.site = site;
    r.va = target;
    r.slot = slot;
    r.kind = kind;
    r.ecx = ecx;
    r.nargs = (nargs > 8) ? 8 : (nargs > 0 ? nargs : 0);
    for (int k = 0; k < r.nargs; k++) r.args[k] = (args != NULL) ? args[k] : 0;
    r.ret = ret;
    if (w != NULL && nw > 0) {
        r.nw = (nw > 3) ? 3 : nw;
        for (int k = 0; k < r.nw; k++) r.w[k] = w[k];
    }
    g_nat.push_back(r);
    return ret;
}

static unsigned rd(const void *p) { unsigned v; memcpy(&v, p, 4); return v; }

/* Заявка портированного листа (battle/ported.h): в генераторе это ПРОИЗВОДИТЕЛЬ
 * потока — запись bdd0_leaf пишется в игровом пространстве, как у модельных
 * листьев. Порт (b0_band_head) считает значение в shadow-адресах, поэтому
 * указатели переводятся в игровой адрес по правилу листа. Правило обязательно:
 * порт без него писал бы shadow-адрес арены прямо в синтетический захват. */
struct PortedLeafModel {
    unsigned va;
    int ret_ptr;               /* 1 — возврат указатель (нужно правило) */
};

static const PortedLeafModel k_ported_model[] = {
    { B0_LEAF_BAND_HEAD, 1 }
};

static const PortedLeafModel *ported_model_find(unsigned va)
{
    for (int i = 0; i < (int)(sizeof(k_ported_model) /
                               sizeof(k_ported_model[0])); i++) {
        if (k_ported_model[i].va == va) return &k_ported_model[i];
    }
    return NULL;
}

void bdd0_leaf_claim(unsigned int va, void *ecx, int ret, const int *args,
                     int nargs)
{
    const PortedLeafModel *pm = ported_model_find(va);
    int game_ecx = 0;
    int game_ret = ret;

    if (pm == NULL) {
        fprintf(stderr, "synth: ported leaf 0x%x has no model rule\n", va);
        exit(2);
    }
    if (ecx != NULL) {
        if (va == B0_LEAF_BAND_HEAD) {
            /* получатель 45c370 — BattleAI = self+0x138 */
            game_ecx = (int)g_env.self_game + BDD0_AI_OFF;
        } else {
            fprintf(stderr, "synth: ported leaf 0x%x has a receiver but no "
                            "game-space rule\n", va);
            exit(2);
        }
    }
    if (pm->ret_ptr) {
        if (va == B0_LEAF_BAND_HEAD) {
            /* 45c370: значение != 0 → лента BattleAI+0x3c, иначе NULL */
            game_ret = (ret != 0)
                ? (int)g_env.self_game + BDD0_AI_OFF + 0x3c : 0;
        } else {
            fprintf(stderr, "synth: ported leaf 0x%x returns a pointer but "
                            "has no game-space rule (shadow address would leak)\n",
                    va);
            exit(2);
        }
    }
    record((int)va, (void *)(size_t)game_ecx, nargs, args, game_ret, NULL, 0);
}

namespace native {

void *ctx_pos_get(void *ctx, float *out_f)
{
    int w[3] = { 0, 0, 0 };
    int a[1];
    if (ctx != NULL) memcpy(w, (const unsigned char *)ctx + 0xc, 12);
    if (out_f != NULL) memcpy(out_f, w, 12);
    a[0] = (int)(size_t)out_f;
    record(0x45c6f0, ctx, 1, a, 0, w, 3);
    return out_f;
}

void *band_head(void *ai)
{
    /* Голова ленты: длина 0 (begin == 0 или end == begin) → NULL, иначе
     * &band (то же правило, что в теле листа и в порте b0_band_head). Запись —
     * в ИГРОВОМ пространстве: адрес ленты = BattleAI+0x3c. Обёртка нужна
     * только синтетике/ABI: порт 0x45c370 не запрашивает (b0_band_head). */
    int n = 0;
    if (ai != NULL) {
        const unsigned char *band = (const unsigned char *)ai + 0x3c;
        unsigned begin = rd(band + 0x20);
        if (begin != 0) {
            n = (int)(rd(band + 0x24) - begin) / BDD0_BAND_ELEM;
        }
    }
    int ret = (n != 0) ? (int)g_env.self_game + BDD0_AI_OFF + 0x3c : 0;
    record(0x45c370, ai, 0, NULL, ret, NULL, 0);
    return (n != 0) ? (unsigned char *)ai + 0x3c : NULL;
}

int band_count(void *band)
{
    int n = 0;
    if (band != NULL) {
        unsigned begin = rd((const unsigned char *)band + 0x20);
        if (begin != 0) {
            n = (int)(rd((const unsigned char *)band + 0x24) - begin) /
                BDD0_BAND_ELEM;
        }
    }
    return record(0x45af50, band, 0, NULL, n, NULL, 0);
}

void *band_cursor(void *ai)
{
    /* Синтетика: цепочка курсора в снимке нулевая → NULL (как в нативе при
     * пустом game_ctx). Записывается ровно возвращённое значение. */
    (void)ai;
    record(0x45c730, ai, 0, NULL, 0, NULL, 0);
    return NULL;
}

void band_interp_gate(void *band, float p2, int p3, float p4, float *out_f)
{
    int a[3];
    a[0] = fbits(p2);
    a[1] = p3;
    a[2] = fbits(p4);
    float v = p2 + g_cfg.interp_add;
    int bits = fbits(v);
    if (out_f != NULL) *out_f = v;
    record(0x45b150, band, 3, a, bits, NULL, 0);
}

int band_target_moved(void *band, float p1, int p2, float p3)
{
    int a[3];
    a[0] = fbits(p1);
    a[1] = p2;
    a[2] = fbits(p3);
    return record(0x45b3e0, band, 3, a, g_cfg.moved, NULL, 0);
}

int pos3_equal(const void *a, const void *b)
{
    int av[1];
    av[0] = (int)(size_t)b;
    int eq = 1;
    for (int i = 0; i < 3; i++) {
        float x, y;
        memcpy(&x, (const unsigned char *)a + i * 4, 4);
        memcpy(&y, (const unsigned char *)b + i * 4, 4);
        if (!(x == y)) { eq = 0; break; }
    }
    return record(0x403e80, a, 1, av, eq, NULL, 0);
}

void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    int a[6];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    a[4] = (int)(size_t)flag_slot;
    a[5] = (int)(size_t)world;
    int ret = g_cfg.anchor_null ? 0 : (int)g_env.tree_game;
    if (flag_slot != NULL) {
        int keep = *flag_slot & ~0xff;
        *flag_slot = keep | (g_cfg.anchor_null ? 0 : 1);
    }
    record(0x458050, grid, 6, a, ret, NULL, 0);
    return g_cfg.anchor_null ? NULL : arena_resolve_ptr(g_env.tree_game);
}

void grid_recompute(void *rec_pf, void *pos, void *req, float out3[3])
{
    int a[2];
    a[0] = (int)(size_t)pos;
    a[1] = (int)(size_t)req;
    int w[3];
    w[0] = fbits(10.0f);
    w[1] = fbits(20.0f);
    w[2] = fbits(30.0f);
    if (out3 != NULL) memcpy(out3, w, 12);
    record(0x45e1d0, rec_pf, 2, a, (int)g_env.scratch_game, w, 3);
}

int grid_query(void *world, int a1, int a2, int a3, int a4)
{
    int a[4];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    return record(0x44bbf0, world, 4, a, g_cfg.grid_query, NULL, 0);
}

/* ---------------- v12: не-листовые RECORD-нативы (модель) --------------- */
static void *reg(const char *name) { return arena_region_w(arena_current(), name); }

void *game_state_get(void *unit, int flags)
{
    int a[1]; a[0] = flags;
    record_native(0x44be0e, 0x442570, 0, 4, (int)g_env.self_game, a, 1, 1, NULL, 0);
    return reg("gs");
}

int string_obj_cmp(void *a, void *b)
{
    int ar[2];
    ar[0] = (int)(size_t)a; ar[1] = (int)(size_t)b;
    record_native(0x44bf44, 0x439ae0, 0, 1, 0, ar, 2, 0, NULL, 0);
    return 0;
}

void *player_flag_get(void *player, int flag)
{
    int a[1]; a[0] = flag;
    record_native(0x44bfc4, 0x42da60, 0, 4, (int)g_env.self_game + 0x200, a, 1, 1,
                  NULL, 0);
    return reg("player");
}

void *player_flags(void *flags_obj)
{
    int w[1];
    w[0] = 0;
    if (flags_obj != NULL) w[0] = *(int *)flags_obj;
    record_native(0x44bfcb, 0x435d50, 0, 5, (int)g_env.self_game + 0x220, NULL, 0, 1,
                  w, 1);
    return reg("player");
}

void cursor_mode_update(void *unit, int arg)
{
    int a[1]; a[0] = arg;
    record_native(0x44c012, 0x448c30, 0, 3, (int)g_env.self_game, a, 1, 0,
                  NULL, 0);
}

void ai_time_update(void *unit, const void *msg60)
{
    int a[1]; a[0] = (int)(size_t)msg60;
    record_native(0x44bfa7, 0x446a80, 0, 3, (int)g_env.self_game, a, 1, 0,
                  NULL, 0);
}

void state_flag_log(void *unit, int flag)
{
    int a[1]; a[0] = flag;
    record_native(0x44d029, 0x448ee0, 0, 3, (int)g_env.self_game, a, 1, 0,
                  NULL, 0);
}

void player_pos_set(void *player, void *triple)
{
    int a[1]; a[0] = (int)g_env.scratch_game;
    record_native(0x44c240, 0x42d9c0, 0, 3, (int)g_env.self_game + 0x200, a, 1, 0,
                  NULL, 0);
}

void pathfinder_init(void *pf)
{
    record_native(0x44c754, 0x45f430, 0, 3, (int)g_env.scratch_game + 0xcc, NULL, 0, 0,
                  NULL, 0);
}

int unit_ai_tick(void *ai, void *rec)
{
    int a[1]; a[0] = (int)g_env.scratch_game + 0xa4;
    record_native(0x44cbd0, 0x45eee0, 0, 0, (int)g_env.self_game + 0x138, a, 1, 0, NULL, 0);
    return 0;
}

void phase_time(void *unit, float *out_f)
{
    int bits = fbits(0.0f);
    record_native(0x44bf1e, 0, 2, 2, (int)g_env.self_game, NULL, 0, bits, NULL, 0);
    if (out_f != NULL) memcpy(out_f, &bits, 4);
}

void *player_get(void *unit, int arg)
{
    int a[1]; a[0] = arg;
    record_native(0x44bf40, 0, 4, 4, (int)g_env.self_game, a, 1, 1, NULL, 0);
    return reg("player");
}

int vcall38_ptr(void *unit, void *ptr)
{
    int a[1]; a[0] = (int)g_env.scratch_game + 0xc0;
    record_native(0x44c72b, 0, 3, 0, (int)g_env.self_game, a, 1, 0, NULL, 0);
    return 0;
}

void world_notify_snapshot(void *unit, const void *snapshot32)
{
    int a[8];
    const int *p = (const int *)snapshot32;
    for (int i = 0; i < 8; i++) a[i] = (p != NULL) ? p[i] : 0;
    record_native(0x44be55, 0, 1, 3, (int)g_env.self_game, a, 8, 0, NULL, 0);
}

} /* namespace native */
