/* tools/probes/bdd0/native_model.cpp — модельный нативный слой (только для
 * bdd0_synth.exe, НЕ входит в харнесс).
 *
 * Десять логических листьев отвечают детерминированно и из снимка:
 *   45c6f0 ctx_pos_get      — пишет 3 dword `[ctx+0xc]` в out
 *   45c370 band_head        — `[ai+0x3c] != 0 ? ai+0x3c : NULL`
 *   45af50 band_count       — `([+0x24]−[+0x20])/0x18`
 *   45c730 band_cursor      — `[[[ctx]+0xc]+0xc]+0x234` (в синтетике NULL)
 *   45b150 band_interp_gate — ST(0) = arg1 + cfg.interp_add
 *   45b3e0 band_target_moved— cfg.moved
 *   403e80 pos3_equal       — почленное равенство float-троек
 *   458050 grid_anchor      — cfg.anchor_null ? NULL : tree (сырой адрес)
 *   45e1d0 grid_recompute   — out3 = {1,2,3}f
 *   44bbf0 grid_query       — cfg.grid_query
 * Каждый вызов пишется в поток bdd0_leaf с `ret` в ПРОСТРАНСТВЕ ИГРЫ.
 */
#include <stdio.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "harness.h"
#include "model.h"

static ModelEnv g_env;
static ModelCfg g_cfg;
static std::vector<LeafRec> g_rec;

void model_set(const ModelEnv &env, const ModelCfg &cfg) { g_env = env; g_cfg = cfg; }
void model_leaf_reset(void) { g_rec.clear(); }
int model_leaf_count(void) { return (int)g_rec.size(); }
const LeafRec *model_leaf_at(int i)
{
    return (i >= 0 && i < (int)g_rec.size()) ? &g_rec[i] : NULL;
}

static int fbits(float f) { int b; memcpy(&b, &f, 4); return b; }

/* Модель НЕ читает записанный поток (она его пишет): интерфейс replay-слоя
 * вырожден, чтобы bdd0_synth линковался без native_replay.cpp. */
void bdd0_leaf_begin(int seq, const LeafRec *v, int n) { (void)seq; (void)v; (void)n; }
int bdd0_leaf_calls(void) { return 0; }
int bdd0_leaf_violations(void) { return 0; }
const char *bdd0_leaf_violation_why(void) { return ""; }
const CallLogEnt *bdd0_calllog(int *n) { if (n) *n = 0; return NULL; }
void bdd0_calllog_reset(void) {}

static int record(int va, const void *ecx, int nargs, const int *args, int ret,
                  const int *w, int nw)
{
    LeafRec r;
    memset(&r, 0, sizeof(r));
    r.order = (int)g_rec.size();
    r.va = va;
    r.ecx = (int)(size_t)ecx;
    r.nargs = (nargs > 6) ? 6 : nargs;
    for (int k = 0; k < r.nargs; k++) r.args[k] = args[k];
    r.ret = ret;
    if (w != NULL && nw > 0) {
        r.nw = (nw > 3) ? 3 : nw;
        for (int k = 0; k < r.nw; k++) r.w[k] = w[k];
    }
    g_rec.push_back(r);
    return ret;
}

static unsigned rd(const void *p) { unsigned v; memcpy(&v, p, 4); return v; }

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
    int found = (ai != NULL && rd((const unsigned char *)ai + 0x3c) != 0);
    int ret = found ? (int)g_env.self_game : 0;   /* &BattleAI[0] == unit */
    record(0x45c370, ai, 0, NULL, ret, NULL, 0);
    return found ? (unsigned char *)ai + 0x3c : NULL;
}

int band_count(void *band)
{
    int n = 0;
    if (band != NULL) {
        n = (int)(rd((const unsigned char *)band + 0x24) -
                  rd((const unsigned char *)band + 0x20)) / BDD0_BAND_ELEM;
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

} /* namespace native */
