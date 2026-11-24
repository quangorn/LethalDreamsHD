/* tools/probes/bdd0/native_model.cpp — модельный нативный слой (только для
 * bdd0_synth.exe, НЕ входит в харнесс).
 *
 * Шесть логических листьев отвечают детерминированно и из снимка
 * (45c6f0, 45c370, 45af50, 403e80 — портированные, их запись снимает claim):
 *   45c730 band_cursor      — `[[[ctx]+0xc]+0xc]+0x234` (в синтетике NULL)
 *   45b150 band_interp_gate — ST(0) = arg1 + cfg.interp_add
 *   45b3e0 band_target_moved— cfg.moved
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
void bdd0_native_begin(int seq, const NativeRec *v, int n,
                       const TickRecord *rec)
{ (void)seq; (void)v; (void)n; (void)rec; }
int bdd0_native_calls(void) { return 0; }
int bdd0_native_violations(void) { return 0; }
const char *bdd0_native_violation_why(void) { return ""; }
int bdd0_native_consumed(void) { return 0; }
const NativeRec *bdd0_native_pending(void) { return NULL; }
const NativeCallLogEnt *bdd0_native_calllog(int *n)
{ if (n) *n = 0; return NULL; }
const NativeRec *bdd0_native_record_at(int i) { (void)i; return NULL; }

static int fbits(float f) { int b; memcpy(&b, &f, 4); return b; }

/* Адрес объекта состояния игры в сценарном (игровом) пространстве: натив
 * 442570 возвращает `self+0x64` (подтверждено захватами: ret = ecx+0x64),
 * поэтому модель пишет тот же адрес. Из него выводятся gs_raw (scenario.cpp),
 * привязка области "gs" (arena.cpp) и классификация указателей на gs-поля
 * (checks_builtin.cpp native_ptr_base — gs проверяется раньше unit). */
#define MODEL_GS_OFF 0x64

/* Модель НЕ читает записанный поток (она его пишет): интерфейс replay-слоя
 * вырожден, чтобы bdd0_synth линковался без native_replay.cpp. Единственная
 * работа — заполнить "ctx_pos" (тройка [ctx+0xc] листа 45c6f0): в генераторе
 * она задана профилем, в replay — восстанавливается из потока. */
void bdd0_leaf_begin(int seq, const LeafRec *v, int n)
{
    unsigned char *pos = arena_region_w(arena_current(), "ctx_pos");
    const TickRecord *tr = arena_record(arena_current());
    (void)seq; (void)v; (void)n;
    /* v15: запись несёт независимый снимок ctx_pos — его уже положил
     * arena_build, перезапись профилем сделала бы фикстуру неотличимой от
     * реконструкции (и скрыла бы испорченный ctx_pos). */
    if (pos != NULL && (tr == NULL || !tr->has_ctx_pos)) {
        memcpy(pos, g_env.ctx_pos, 12);
    }
}
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
/* 458050: генератор линкуется с BATTLE_REPLAY, bdd0_grid_level_modelled() = 0 —
 * диспетчер tick.cpp всегда берёт модельную ветку (native::grid_anchor из этого
 * файла), тело порта в генераторе не исполняется. native_replay.cpp сюда не линкуется:
 * счётчика реплея нет, stub 0. */
int bdd0_grid_anchor_replayed(void) { return 0; }
/* 45b150: генератор линкуется с BATTLE_REPLAY, bdd0_band_geometry_modelled() = 0 —
 * диспетчер tick.cpp всегда берёт модельную ветку (native::band_interp_gate из этого
 * файла), тело порта в генераторе не исполняется. native_replay.cpp сюда не линкуется:
 * счётчика реплея нет, stub 0. */
int bdd0_band_gate_replayed(void) { return 0; }
/* v15: восстановленной записи 0x45eee0 в генераторе нет — поток пишет он сам. */
int bdd0_eee0_synth(void) { return 0; }
/* Заявки портированных листьев: генератор их не судит (поток он создаёт сам,
 * проверки не запускаются). */
int bdd0_leaf_claims(void) { return 0; }
const LeafClaimEnt *bdd0_leaf_claim_at(int i) { (void)i; return NULL; }

static int record(int va, const void *ecx, int nargs, const int *args, int ret,
                  const int *w, int nw, int site = 0)
{
    LeafRec r;
    memset(&r, 0, sizeof(r));
    r.order = g_order++;
    r.va = va;
    r.site = site;
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
                         int nw, const int *ww = NULL, int nww = 0)
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
    /* v14: ww-пары [unit_off, value] — после-состояние, записанное нативом
     * (45eee0: BattleAI+0x00..0x38; band_rec: +0x3c+0x20/+0x24). */
    if (ww != NULL && nww > 0) {
        r.nww = (nww > 32) ? 32 : nww;
        for (int k = 0; k < r.nww; k++) r.ww[k] = ww[k];
    }
    g_nat.push_back(r);
    return ret;
}

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
    { B0_LEAF_BAND_HEAD, 1 },
    { B0_LEAF_BAND_COUNT, 0 },
    { B0_LEAF_READ_POS, 1 },
    { B0_LEAF_POS3_EQUAL, 0 }
};

static const PortedLeafModel *ported_model_find(unsigned va)
{
    for (int i = 0; i < (int)(sizeof(k_ported_model) /
                               sizeof(k_ported_model[0])); i++) {
        if (k_ported_model[i].va == va) return &k_ported_model[i];
    }
    return NULL;
}

void bdd0_leaf_claim_w(unsigned int va, void *ecx, int ret, const int *args,
                       int nargs, const int *w, int nw)
{
    const PortedLeafModel *pm = ported_model_find(va);
    int game_ecx = 0;
    int game_ret = ret;

    if (pm == NULL) {
        fprintf(stderr, "synth: ported leaf 0x%x has no model rule\n", va);
        exit(2);
    }
    if (va == B0_LEAF_READ_POS) {
        /* 45c6f0: правило = то же чтение, что у порта (this → ctx = *(this) →
         * p = [ctx+0xc] → тройка). Значения в shadow-адресах арены: "ctx_pos"
         * заполнен bdd0_leaf_begin из env.ctx_pos, ctx+0xc — связка arena.cpp.
         * Запись потока — в игровом пространстве: ecx = &BattleAI
         * (self+0x138), ret/arg[0] = буфер out (scratch_game), w = тройка.
         * Самопроверка: модель обязана записать ровно то, что посчитала —
         * иначе синтетический захват перестанет быть согласован с портом. */
        int w3[3] = { 0, 0, 0 };
        int a[1];
        void *ctx = (ecx != NULL) ? *(void **)ecx : NULL;

        if (ctx != NULL) {
            void *p = *(void **)((unsigned char *)ctx + 0xc);

            if (p != NULL) memcpy(w3, p, 12);
        }
        if (nw != 3 || w == NULL || w[0] != w3[0] || w[1] != w3[1] ||
            w[2] != w3[2]) {
            fprintf(stderr, "synth: 45c6f0 claim w disagrees with the model "
                            "rule\n");
            exit(2);
        }
        a[0] = (int)g_env.scratch_game;
        record((int)va, (void *)(size_t)((int)g_env.self_game + BDD0_AI_OFF),
               1, a, (int)g_env.scratch_game, w3, 3);
        return;
    }
    if (va == B0_LEAF_POS3_EQUAL) {
        /* 403e80: правило = почленное равенство float-троек (unordered = «не
         * равно»: в C `==` на NaN ложно). В синтетике оба указателя — shadow
         * арены, поэтому модель пересчитывает ответ по ним и требует, чтобы
         * порт заявил ровно его (иначе поток разойдётся с портом).
         * Запись потока — в игровом пространстве: ECX = out вызывающего
         * (кадровый скретч scratch_game), arg[0] = result+4 = объект состояния
         * +4 (MODEL_GS_OFF), ret = 0/1 как есть. */
        int eq = 1;
        int a1[1];
        int k;

        if (ecx == NULL || nargs != 1 || args == NULL) {
            fprintf(stderr, "synth: 403e80 claim without both pointers\n");
            exit(2);
        }
        for (k = 0; k < 3; k++) {
            float x, y;

            memcpy(&x, (const unsigned char *)ecx + k * 4, 4);
            memcpy(&y, (const unsigned char *)(size_t)args[0] + k * 4, 4);
            if (!(x == y)) { eq = 0; break; }
        }
        if (eq != ret) {
            fprintf(stderr, "synth: 403e80 claim ret=%d disagrees with the "
                            "model rule (%d)\n", ret, eq);
            exit(2);
        }
        a1[0] = (int)g_env.self_game + MODEL_GS_OFF + 4;
        record((int)va, (void *)(size_t)g_env.scratch_game, 1, a1, ret, NULL, 0);
        return;
    }
    if (ecx != NULL) {
        if (va == B0_LEAF_BAND_HEAD) {
            /* получатель 45c370 — BattleAI = self+0x138 */
            game_ecx = (int)g_env.self_game + BDD0_AI_OFF;
        } else if (va == B0_LEAF_BAND_COUNT) {
            /* получатель 45af50 — лента = BattleAI+0x3c = self+0x138+0x3c */
            game_ecx = (int)g_env.self_game + BDD0_AI_OFF + B0_BATTLE_AI_BAND;
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
                ? (int)g_env.self_game + BDD0_AI_OFF + B0_BATTLE_AI_BAND : 0;
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

void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    int a[6];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    a[4] = (int)(size_t)flag_slot;
    a[5] = (int)(size_t)world;
    int ret = g_cfg.anchor_null ? 0 : (int)g_env.tree_game;
    /* Ветка, пишющая флаг (1 @0x4580fd / 0 @0x458161), не выводится из модельного
     * состояния, а байт ниже по потоке не читается — не пишем его. */
    record(0x458050, grid, 6, a, ret, NULL, 0);
    /* v15: вложенный вызов сайта 0x458142 → 0x456b60 (chain_head) внутри
     * 0x458050 (`CALL 0x456b60` после проверки статуса == 2). Порт 0x458050
     * не исполняет, поэтому запись выносится загрузчиком из окна тика
     * (BDD0_NESTED_SITE_GRID_ANCHOR); профиль nested_chain её и создаёт,
     * чтобы корпус показывал: такой захват остаётся самосогласованным. */
    if (g_cfg.nested_chain) {
        int na[1];

        na[0] = (int)(size_t)world;          /* args[0] = world */
        record(0x456b60, (void *)(size_t)ret, 1, na, ret, NULL, 0,
               (int)BDD0_NESTED_SITE_GRID_ANCHOR);
    }
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
    record_native(0x44be0e, 0x442570, 0, 4, (int)g_env.self_game, a, 1,
                  (int)g_env.self_game + MODEL_GS_OFF, NULL, 0);
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
    int ww[4] = { 0, 0, 0, 0 };
    int nww = 0;
    /* v14: 45eee0 перестраивает ленту — модель пишет после-состояние
     * (begin,end) в BattleAI+0x3c+0x20/+0x24 (unit+0x194/+0x198) и отдаёт
     * ww-пару [unit_off, value], как натив. Без этого запись остаётся в старой
     * семантике (ww нет → resync-fallback). */
    if (g_env.band_post_game != 0 && ai != NULL) {
        unsigned begin = g_env.band_post_game;
        unsigned end = begin + (unsigned)g_env.band_post_n * BDD0_BAND_ELEM +
                       (unsigned)g_env.band_post_extra;
        memcpy((unsigned char *)ai + B0_BATTLE_AI_BAND + B0_BAND_BEGIN_OFF, &begin, 4);
        memcpy((unsigned char *)ai + B0_BATTLE_AI_BAND + B0_BAND_END_OFF, &end, 4);
        ww[0] = 0x138 + B0_BATTLE_AI_BAND + B0_BAND_BEGIN_OFF;
        ww[1] = (int)begin;
        ww[2] = 0x138 + B0_BATTLE_AI_BAND + B0_BAND_END_OFF;
        ww[3] = (int)end;
        nww = 4;
    }
    record_native(0x44cbd0, 0x45eee0, 0, 0, (int)g_env.self_game + 0x138, a, 1, 0,
                  NULL, 0, ww, nww);
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
