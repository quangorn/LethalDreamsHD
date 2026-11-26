/* tools/probes/bdd0/harness.h — офлайн-харнесс боевого тика 44bdd0: общие типы.
 *
 * Ядро (tools/probes/bdd0_harness.cpp) знает ровно три вещи:
 *   1. имена и длины областей состояния (frame/unit/ai/band/gs/ctx/out/node),
 *   2. имена полей (декларативная таблица fields в registry.cpp),
 *   3. формат журнала вызовов (CallLogEnt).
 * Доменных правил в ядре нет: проверки живут в checks_builtin.cpp и видят только
 * read-only TickContext; реализация — за ImplDef; нативный слой — native_*.cpp.
 *
 * Данные захвата v12 (bdd0_in / bdd0_leaf / bdd0_native / bdd0_res) и v15
 * (bdd0_in "ctx_pos" — независимая pos-тройка листа 45c6f0) описаны в
 * tools/ldhook/battle/capture.h; загрузчик — scenario.cpp.
 */
#ifndef BDD0_HARNESS_H
#define BDD0_HARNESS_H

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

/* --------------------------- данные захвата v12 --------------------------- */
#define BDD0_FRAME_LEN  0x168     /* локалы 0x00..0x15b + param_1/2/3 */
#define BDD0_UNIT_LEN   0x180     /* все чтения unit в tick.cpp до +0x17f */
#define BDD0_AI_LEN     0x80      /* встроенный BattleAI, unit+0x138..+0x1b7 */
#define BDD0_AI_OFF     0x138
#define BDD0_BAND_ELEM  0x18      /* элемент ленты (45af50: (end-begin)/0x18) */
#define BDD0_BAND_MAX   64        /* потолок материализуемых элементов ленты */
#define BDD0_GS_LEN     0x20      /* объект FUN_00442570, читаемый телом */

/* Сайты вызовов ВНУТРИ листа 0x458050 (grid_anchor): записи bdd0_leaf от них
 * принадлежат телу, которое НЕ реплеится записью целиком — они выносятся из окна
 * тика в счётчик nested (leaf_seq от них не зависит). У 0x458050 в порте есть
 * свой claim только у вложенных листьев (0x456b60/0x457e20), а сам он записи не
 * заявляет: границу записи держит сайт 0x44c7da, а не тело.
 *   BDD0_NESTED_SITE_GRID_ANCHOR — 0x458142 → 0x456b60 (голова цепочки +0x28);
 *   BDD0_NESTED_SITE_GRID_KEY    — 0x45811a → 0x457e20 (поиск ячейки по ключу).
 * Второй сайт появился в leaf_sites после порта 0x458050 (захваты v12..v15 его
 * ещё не писали: хук не стоял).
 * Когда появится захват с моделью сетки, порт 0x458050 исполняет своё тело —
 * снять ВСЁ СРАЗУ: (а) bdd0_grid_level_modelled() → 1 (arena.cpp), (б) оба
 * сайта из этого списка, (в) 0x458050 — в b0_ported_leaf_va (battle/ported.h)
 * с claim из тела: без (в) запись потока va=0x458050 не потребляет никто, и
 * leaf_seq падает на каждом тике. */
#define BDD0_NESTED_SITE_GRID_ANCHOR 0x458142
#define BDD0_NESTED_SITE_GRID_KEY    0x45811a

/* v17: внутренние сайты листа 0x45e1d0 (20 вызовов внутри его тела,
 * leaf_sites.cpp №35..54, VA сайта → VA callee). Как и 33/34, их записи
 * bdd0_leaf выносятся из окна тика (иначе leaf_seq видел бы «лишнюю»), но,
 * в отличие от 33/34, они сохраняются ПО seq в порядке исполнения и
 * предъявляются будущему телу порта (recompute.cpp) через bdd0_nested_*.
 * Порядок = порядок адресов = порядок фаз тела (§1 плана порта):
 *   P1 45e1e3 4cfa11 new(0x1c)   P2 45e23c 4cfa11 new(0x10)
 *   P3 45e274 45c330 reset       P3 45e27b 45ca70 clear_maps
 *   P4 45e297 456b20 scale       P4 45e2ab 456b20 scale
 *   P5 45e316 458050 anchor      P5 45e320 45c8c0 search_params
 *   P6 45e32e 434bc0 lower_bound P7 45e340 45c2e0 PathState ctor
 *   P7 45e3a2 45c480 distance2   P7 45e3b4 45c690 heuristic_gate
 *   P7 45e3ea 45c4b0 copy_fields P7 45e3f2 45dd00 register_slot
 *   P8 45e443 45ce80 scan_map
 *   P9 45e46b 45dd70 ai_update / 45e47e 45c4b0 copy / 45e487 45ce80 scan
 *   P10 45e49b 456b20 scale      P10 45e4b1 456b20 scale */
#define BDD0_NESTED_SITE_R1_NEW_REC   0x45e1e3
#define BDD0_NESTED_SITE_R2_NEW_SUB   0x45e23c
#define BDD0_NESTED_SITE_R3_RESET     0x45e274
#define BDD0_NESTED_SITE_R3_CLEAR     0x45e27b
#define BDD0_NESTED_SITE_R4_SCALE_A   0x45e297
#define BDD0_NESTED_SITE_R4_SCALE_B   0x45e2ab
#define BDD0_NESTED_SITE_R5_ANCHOR    0x45e316
#define BDD0_NESTED_SITE_R5_PARAMS    0x45e320
#define BDD0_NESTED_SITE_R6_LOWER     0x45e32e
#define BDD0_NESTED_SITE_R7_CTOR      0x45e340
#define BDD0_NESTED_SITE_R7_DIST2     0x45e3a2
#define BDD0_NESTED_SITE_R7_HEUR      0x45e3b4
#define BDD0_NESTED_SITE_R7_COPY      0x45e3ea
#define BDD0_NESTED_SITE_R7_REGISTER  0x45e3f2
#define BDD0_NESTED_SITE_R8_SCAN      0x45e443
#define BDD0_NESTED_SITE_R9_UPDATE    0x45e46b
#define BDD0_NESTED_SITE_R9_COPY      0x45e47e
#define BDD0_NESTED_SITE_R9_SCAN      0x45e487
#define BDD0_NESTED_SITE_R10_SCALE_C  0x45e49b
#define BDD0_NESTED_SITE_R10_SCALE_D  0x45e4b1
/* VA каллее-скана (45ce80): v18 пишет для них снимок PathState (bdd0_cand). */
#define BDD0_VA_SCAN_MAP              0x45ce80

/* v17: внутренний вызов листа 0x45e1d0 (сайт из BDD0_NESTED_SITE_R*, тело
 * leaf_sites.cpp №35..54). Записи bdd0_leaf этих сайтов выносятся из окна
 * тика (см. BDD0_NESTED_SITE_R1_NEW_REC) и хранятся по seq В ПОРЯДКЕ
 * ИСПОЛНЕНИЯ — будущее тело порта (recompute.cpp) потребляет их по одному
 * (bdd0_nested_next/peek/consume). Поля — как у LeafRec (ecX/args/ret — сырые
 * адреса игры, переводятся ареной через k_fixups); w/nw/trap — как в записи.
 * order — порядок сайта в потоке тика (общий с bdd0_leaf/native). */
struct NestedRec {
    int order, site, va, ecx, nargs, args[6], ret, w[3], nw, trap;
    /* v18: bdd0_cand — снимок PathState 0x58, возвращённого сканом 45ce80, снятый
     * на выходе сайта 49/52. cand_ok=0 — записи нет (v17 и старше) или ps не
     * прочитан: гейт bdd0_path_state_modelled() тогда 0, порт не исполняется. */
    int cand_ok;
    unsigned cand;
    unsigned cand_ps[0x58 / 4];
    NestedRec()
        : order(0), site(0), va(0), ecx(0), nargs(0), ret(0), nw(0), trap(0),
          cand_ok(0), cand(0)
    {
        for (int i = 0; i < 6; i++) args[i] = 0;
        for (int i = 0; i < 3; i++) w[i] = 0;
        for (int i = 0; i < 0x58 / 4; i++) cand_ps[i] = 0;
    }
};

/* v17: состояние листа 0x45e1d0 (PathFinder/record/sub/ctx/bandrec + обе
 * RB-карты) из bdd0_leaf_pre / bdd0_leaf_post / bdd0_site_pf. Индексируется
 * по (seq, order) — один вызов листа на seq в v17. has_* — какие блоки
 * присутствуют; ok — запись полна и пригодна (fail-closed: отсутствующий
 * усечённый блок → ok=0, порт этот вызов не использует, check считает).
 * pf 0x78, rec 0x1c, sub 0x10, bandrec 0x24; ctx в pre НЕ записан (rec/sub/ctx
 * лениво аллоцируются внутри листа — сайты 35/36/44), известны только два
 * dword'а ctx120/ctx134 (их несёт bdd0_site_pf, identity-check по ecx). maps —
 * head/n/trunc/trap + узлы (адрес, сырой 0x18 RB-узел, PathState 0x58):
 * в pre карты пусты (n=0), в post несут вставки. */
#define BDD0_LEAFPF_LEN   0x78
#define BDD0_LEAFREC_LEN  0x1c
#define BDD0_LEAFSUB_LEN  0x10
#define BDD0_LEAFCTX_LEN  0x138
#define BDD0_LEAFBAND_LEN 0x24
#define BDD0_MAPNODE_MAX  64      /* потолок узлов карты на одно состояние */
#define BDD0_PS_LEN       0x58    /* PathState узла карты (потребители +08/+20/+24) */
struct MapNodeRec {
    unsigned addr;                /* a — адрес узла в куче игры (identity) */
    unsigned key, value;          /* TreeNode +0x0c / +0x10 */
    int colour, is_nil;           /* TreeNode +0x14 / +0x15 */
    int has_node;                 /* сырой 0x18-байт узел прочитан */
    int has_ps;                   /* PathState 0x58 прочитан (ps != null) */
    unsigned char node[0x18];     /* сырой RB-узел (для reg_add_fixed+bind_add) */
    unsigned ps[BDD0_PS_LEN / 4]; /* PathState: +0x08 filter, +0x20/+0x24 min-ключ */
};
struct MapHeadRec {
    unsigned head;                /* голова списка (pf+0x6c / pf+0x78) */
    int n;                        /* узлов в захвате */
    int trunc, trap;              /* флаги писателя (усечение/сбой) */
};
struct LeafStateRec {
    int seq, order;
    int has, ok;                  /* запись присутствует / полна */
    int has_pf, has_rec, has_sub, has_ctx, has_band, has_maps;
    int site, va;                 /* заголовок: VA сайта / VA callee */
    unsigned ecx;
    int nargs, args[6];
    unsigned pf[BDD0_LEAFPF_LEN / 4];
    unsigned rec, sub, ctx, bandrec;
    unsigned ctx120, ctx134;      /* известны только эти два поля ctx */
    unsigned recb[BDD0_LEAFREC_LEN / 4];
    unsigned subb[BDD0_LEAFSUB_LEN / 4];
    unsigned bandb[BDD0_LEAFBAND_LEN / 4];
    MapHeadRec h1, h2;            /* maps.m1 / maps.m2 */
    int m1_n, m2_n;
    MapNodeRec m1[BDD0_MAPNODE_MAX], m2[BDD0_MAPNODE_MAX];
    LeafStateRec()
        : seq(0), order(0), has(0), ok(0), has_pf(0), has_rec(0), has_sub(0),
          has_ctx(0), has_band(0), has_maps(0), site(0), va(0), ecx(0),
          nargs(0), rec(0), sub(0), ctx(0), bandrec(0), ctx120(0), ctx134(0),
          m1_n(0), m2_n(0)
    {
        for (int i = 0; i < 6; i++) args[i] = 0;
        for (int i = 0; i < BDD0_LEAFPF_LEN / 4; i++) pf[i] = 0;
        for (int i = 0; i < BDD0_LEAFREC_LEN / 4; i++) recb[i] = 0;
        for (int i = 0; i < BDD0_LEAFSUB_LEN / 4; i++) subb[i] = 0;
        for (int i = 0; i < BDD0_LEAFBAND_LEN / 4; i++) bandb[i] = 0;
        h1.head = h2.head = 0;
        h1.n = h2.n = 0;
        h1.trunc = h1.trap = h2.trunc = h2.trap = 0;
    }
};

/* v12: один вызов логического листа из потока bdd0_leaf.
 * w/nw — dword'ы, которые лист записал ЧЕРЕЗ указатель (45c6f0 out-тройка,
 * 45e1d0 возврат-тройка); в захвате это необязательный ключ `w`.
 * trap — захват записал запись после сбоя/рассинхрона LIFO (`"trap":1`):
 * значения (ret/args/w) в ней недостоверны, replay её НЕ потребляет. */
struct LeafRec {
    int order, va, ecx, nargs, args[6], ret, w[3], nw, trap;
    int site;                  /* v12: call-site VA (0 = older record) */
};

/* v12: не-листовой нативный вызов тела (`bdd0_native`).  slot != 0 — косвенный
 * вызов через vtable unit'а; тогда va — фактическая цель, slot — id слота.
 * kind: 0 int / 1 al / 2 f32 / 3 void / 4 ptr / 5 ptr+w. */
struct NativeRec {
    int order, site, va, slot, kind, ecx, nargs, args[8], ret, w[3], nw, trap;
    int ww[32], nww;             /* [unit_off, value] pairs (45eee0 state) */
    NativeRec()
        : order(0), site(0), va(0), slot(0), kind(0), ecx(0), nargs(0), ret(0),
          nw(0), trap(0), nww(0)
    {
        for (int i = 0; i < 8; i++) args[i] = 0;
        for (int i = 0; i < 3; i++) w[i] = 0;
        for (int i = 0; i < 32; i++) ww[i] = 0;
    }
};

/* v12: запрос порта на не-листовой натив (для native_args).  site/order
 * заполняются после сопоставления с записью bdd0_native.  last_label — id метки
 * тела, последней посещённой перед этим вызовом (-1 в не-trace сборке). */
struct NativeCallLogEnt {
    int order, site, va, slot, ecx, nargs, args[8];
    int call_seq;              /* глобальный номер запроса (leaf+native) */
    int last_label;
    NativeCallLogEnt()
        : order(0), site(0), va(0), slot(0), ecx(0), nargs(0), call_seq(0),
          last_label(-1)
    {
        for (int i = 0; i < 8; i++) args[i] = 0;
    }
};

struct TickRecord {
    int seq, tick, unit_index;
    int p1, p2, p3;            /* bdd0_in p1/p2/p3 (= frame+0x15c/0x160/0x164) */
    int u170_in;
    unsigned gate114;
    int has_frame, has_unit, has_ai, has_band, has_gs;
    int band_n;
    int has_band_state;        /* bdd0_in "band":[begin,end] присутствует */
    unsigned band_in_begin, band_in_end;   /* входное состояние ленты (v13) */
    int has_res_band;          /* bdd0_res "band":[begin,end] присутствует */
    unsigned band_post_begin, band_post_end;  /* после-состояние ленты (v13) */
    int has_chain;             /* e0/e0_44/tree присутствуют в записи */
    unsigned e0, e0_44, tree;
    int has_gctx0;
    unsigned gctx0;            /* *(int*)ai_ctx — первый dword game_ctx */
    /* v15: независимая pos-тройка листа 45c6f0 = `[*(unit+0x138)+0xc]`.
     * Есть ключ "ctx_pos" (массив из 3) — тройка берётся отсюда, а не из `w`
     * записи листа, поэтому заявка 45c6f0 сверяется как обычное значение. */
    int has_ctx_pos;
    int ctx_pos[3];
    /* v16 (аддитивно): геометрия ленты — параллельные массивы на band_n
     * элементов (bdd0_in "band_pos"). has_band_pos=1 только когда ключ есть,
     * длина совпадает с (end-begin)/0x18 и все ok[i]==1 (правило загрузчика:
     * неполная геометрия не моделируется). sa/sl/sm — сырые биты float-тройки
     * уровня (GridLevel +0x20/+0x24/+0x28). */
    int has_band_pos;
    int band_pos_n;
    int band_pos_ok[BDD0_BAND_MAX];
    int band_pos_x[BDD0_BAND_MAX];
    int band_pos_y[BDD0_BAND_MAX];
    unsigned band_pos_sa[BDD0_BAND_MAX];
    unsigned band_pos_sl[BDD0_BAND_MAX];
    unsigned band_pos_sm[BDD0_BAND_MAX];
    unsigned self_raw;         /* bdd0_in self = адрес unit в игре */
    unsigned band_raw;         /* bdd0_in band_begin = адрес ленты */
    unsigned gs_raw;           /* возврат 442570 = адрес объекта состояния игры */
    unsigned grid_cells_raw;   /* v16: база массива ячеек листа (grid cells_base) */
    unsigned grid_cells_len;   /* v16: длина массива ячеек (ncell*0x34), 0 — нет */
    unsigned char frame[BDD0_FRAME_LEN];
    unsigned char unit[BDD0_UNIT_LEN];
    unsigned char ai[BDD0_AI_LEN];
    unsigned char band[BDD0_BAND_MAX * BDD0_BAND_ELEM];
    unsigned char gs[BDD0_GS_LEN];
    int trap;                  /* bdd0_in `"trap"`: снимок снят при сбое */
    /* записанный исход пары bdd0_res (только --impl native / отчёт) */
    int has_res;
    int res_trap;              /* bdd0_res `"trap"`: исход недостоверен */
    int res_al, res_out, res_u170;
    unsigned res_m[3];
    /* окно листьев этого seq (индексы в Scenario::leaves) */
    int leaf_first, leaf_n;
    /* окно bdd0_native этого seq (индексы в Scenario::natives) */
    int native_first, native_n;
    /* v17: окно внутренних вызовов листа 0x45e1d0 этого seq (индексы в
     * Scenario::nested) и state-записи (индексы в Scenario::leaf_pre/post/
     * site_pf). -1 — нет данных (старые захваты). */
    int nested_first, nested_n;
    int leaf_pre_idx, leaf_post_idx, site_pf_idx;
    int native_eee0_no_ww;     /* 45eee0 record without post-state writes */
    int incomplete_tail;       /* seq is the last in the capture and has no res */
    int res_missing_nontail;   /* no res below the last seq: capture anomaly */
    TickRecord()
        : seq(0), tick(0), unit_index(0), p1(0), p2(0), p3(0), u170_in(0),
          gate114(0), has_frame(0), has_unit(0), has_ai(0), has_band(0),
          has_gs(0), band_n(0), has_band_state(0), band_in_begin(0),
          band_in_end(0), has_res_band(0), band_post_begin(0), band_post_end(0),
          has_chain(0), e0(0), e0_44(0), tree(0),
          has_gctx0(0), gctx0(0), has_ctx_pos(0), self_raw(0), band_raw(0),
          gs_raw(0), grid_cells_raw(0), grid_cells_len(0), trap(0),
          has_band_pos(0), band_pos_n(0),
          has_res(0), res_trap(0), res_al(0), res_out(0), res_u170(0),
          leaf_first(0), leaf_n(0), native_first(0), native_n(0),
          nested_first(-1), nested_n(0),
          leaf_pre_idx(-1), leaf_post_idx(-1), site_pf_idx(-1),
          native_eee0_no_ww(0), incomplete_tail(0), res_missing_nontail(0)
    {
        for (int i = 0; i < BDD0_FRAME_LEN; i++) frame[i] = 0;
        for (int i = 0; i < BDD0_UNIT_LEN; i++) unit[i] = 0;
        for (int i = 0; i < BDD0_AI_LEN; i++) ai[i] = 0;
        for (int i = 0; i < (int)sizeof(band); i++) band[i] = 0;
        for (int i = 0; i < BDD0_GS_LEN; i++) gs[i] = 0;
        ctx_pos[0] = ctx_pos[1] = ctx_pos[2] = 0;
        res_m[0] = res_m[1] = res_m[2] = 0;
        for (int i = 0; i < BDD0_BAND_MAX; i++) {
            band_pos_ok[i] = 0;
            band_pos_x[i] = band_pos_y[i] = 0;
            band_pos_sa[i] = band_pos_sl[i] = band_pos_sm[i] = 0;
        }
    }
};

/* v16: модель сетки pathfinding'а из записи kind:"grid" (cap_write_grid_record,
 * tools/ldhook/battle/capture.cpp). Один снимок на файл; арена материализует
 * под неё GridLevel/ячейки/rows, чтобы порт 0x458050 (s6_grid_anchor_lookup)
 * читал свои входы. Уровни: scale-тройка (биты float), size_x/size_y, next
 * (индекс следующего уровня, -1 — лист). Ячейки листа: key_rec->key, next
 * (цепочка +0x24), min_metric (float-биты). rows[x][y] — индекс ячейки. */
#define BDD0_GRID_MAX_LEVELS 16
/* Совпадает с живым писателем (capture.cpp CAP_MAX_GRID_CELLS=200000): реальный
 * лист 300x300 = 90000 ячеек, старый офлайн-кэп 256 молча выключал модель. */
#define BDD0_GRID_MAX_CELLS  200000
struct GridLevelModel {
    int sx, sy, rx, ry;
    unsigned sa, sl, sm;       /* float-биты scale_add/scale_load/scale_mul */
    int next;                  /* индекс следующего уровня, -1 — лист */
};
struct GridModel {
    int valid;
    int nlev, leaf;
    GridLevelModel lev[BDD0_GRID_MAX_LEVELS];
    int ncell;                 /* leaf.sx * leaf.sy */
    unsigned cells_game;       /* v16: база массива ячеек в игре (grid "cells_base"; 0 — нет) */
    unsigned rows_game;        /* v16: база rows[x][y] в игре (grid "rows_base"; 0 — нет) */
    int rows[BDD0_GRID_MAX_CELLS];   /* rows[x][y] = индекс ячейки */
    int key[BDD0_GRID_MAX_CELLS];    /* key_rec->key */
    int nextc[BDD0_GRID_MAX_CELLS];  /* cell.next (индекс, -1) */
    int minm[BDD0_GRID_MAX_CELLS];   /* cell.min_metric (float-биты) */
    GridModel() : valid(0), nlev(0), leaf(-1), ncell(0), cells_game(0),
                  rows_game(0) {}
};

/* v17: материализованный вход для тела порта листа 0x45e1d0 (arena.cpp).
 * Адреса — shadow-указатели арены (pf/rec/sub/ctx/bandrec), arg-значения —
 * сырые из записи. ok=0 — материализация не удалась (fail-closed, тело порт
 * не исполняет, check считает). m1/m2 — пред-карты: head (pf+0x6c/pf+0x78)
 * и узлы, материализованные по записанным адресам; в pre n=0 (карты пусты),
 * поэтому m1_n/m2_n = 0, а head — сырые адреса сентинелей.
 * rec/sub/ctx в pre NULL: их аллоцируют сами сайты листа (35/36/44) — порт
 * берёт адреса из bdd0_nested, а не отсюда. ctx120/ctx134 — из site_pf. */
struct PathCall {
    int ok;
    unsigned ecx_raw;
    void *pf, *rec, *sub, *ctx, *bandrec;
    unsigned pf_head1, pf_head2;   /* maps m1/m2: сырые головы (pf+0x6c/+0x78) */
    unsigned ctx120, ctx134;       /* из bdd0_site_pf (identity по ecx) */
    int nargs, args[6];
    int m1_n, m2_n;
    MapNodeRec m1[BDD0_MAPNODE_MAX], m2[BDD0_MAPNODE_MAX];
    PathCall()
        : ok(0), ecx_raw(0), pf(NULL), rec(NULL), sub(NULL), ctx(NULL),
          bandrec(NULL), pf_head1(0), pf_head2(0), ctx120(0), ctx134(0),
          nargs(0), m1_n(0), m2_n(0)
    {
        for (int i = 0; i < 6; i++) args[i] = 0;
    }
};

struct Scenario {
    std::string name;
    std::vector<TickRecord> ticks;
    std::vector<LeafRec> leaves;
    std::vector<NativeRec> natives;
    GridModel grid;            /* v16: модель сетки из kind:"grid" (bdd0-путь) */
    int has_grid;              /* 1 — grid-запись разобрана (bdd0_grid_level_modelled) */
    int snap_records;          /* сколько bdd0_in прочитано */
    int leaf_records;
    int native_records;
    int truncated;             /* файл оборван (захват убит): хвост отброшен */
    int records;               /* сколько объектов прочитано вообще */
    int leaf_missing_n;        /* маркеры leaf_missing: сайт не захукался */
    std::string leaf_missing_why;
    int native_missing_n;      /* native_site_skipped: RECORD-сайт не захукался */
    std::string native_missing_why;
    int nested_leaf_n;         /* записи bdd0_leaf из сайтов ВНУТРИ 0x458050
                                  (0x458142 → 0x456b60 и 0x45811a → 0x457e20,
                                  k_grid_nested_sites в scenario.cpp): в окно
                                  тика не входят, см. BDD0_NESTED_SITE_GRID_ANCHOR /
                                  _GRID_KEY */
    /* v17: внутренние сайты листа 0x45e1d0 — по seq в порядке исполнения. */
    std::vector<NestedRec> nested;
    /* v17: bdd0_leaf_pre/post/site_pf — по (seq,order). */
    std::vector<LeafStateRec> leaf_pre;
    std::vector<LeafStateRec> leaf_post;
    std::vector<LeafStateRec> site_pf;
    int leaf_state_records;    /* сколько state-записей прочитано (pre+post+pf) */
    int cand_records;          /* v18: сколько bdd0_cand прочитано */
    int input_trap;            /* bdd0_in с trap=1 (снимок недостоверен) */
    int outcome_trap;          /* bdd0_res с trap=1 (исход недостоверен) */
    int incomplete_tail;       /* тиков, у которых seq без res и он последний */
    int res_missing_nontail;   /* res отсутствует ниже последнего seq: аномалия */
    Scenario() : snap_records(0), leaf_records(0), native_records(0),
                 truncated(0), records(0), leaf_missing_n(0),
                 native_missing_n(0), nested_leaf_n(0), input_trap(0),
                 outcome_trap(0), incomplete_tail(0), res_missing_nontail(0),
                 has_grid(0), leaf_state_records(0), cand_records(0) {}
};

/* Загрузчик (scenario.cpp). Ошибка формата → std::runtime_error (rc 2). */
Scenario *load_capture(const char *path);
void scenario_free(Scenario *s);

/* ---------------------------- области состояния --------------------------- */
struct RegionView {
    const char *name;
    const unsigned char *base;
    unsigned len;
};

struct CallLogEnt {
    int order;                 /* 0-based внутри тика */
    int va;
    int ecx;
    int nargs;
    int args[6];
    int ret;
    int call_seq;              /* глобальный номер запроса (leaf+native) */
    int last_label;            /* id метки тела перед вызовом (-1 не-trace) */
};

/* Заявка портированного листа (battle/ported.h: b0_ported_leaf_va): порт считает
 * значение сам и сообщает его claim'ом, replay-слой сопоставляет заявку со
 * следующей записью потока bdd0_leaf (leaf_seq) и продвигает поток. Значение
 * сверяет проверка leaf_value: ret — то, что вернул порт (указатель — shadow-
 * адрес), rec — сопоставленная запись (NULL — расхождение потока). args/nargs —
 * необязательные аргументы листа (0 — сверять нечего); w/nw — dword'ы, которые
 * лист записал через указатель (out-тройка 45c6f0), сверяются с `w` записи. */
struct LeafClaimEnt {
    int order;                 /* 0-based внутри тика */
    int call_seq;              /* сквозной номер вызова в тике (как в CallLogEnt) */
    int va;
    int ecx;                   /* получатель (this) — символьно, как в leaf_args */
    int ret;
    int nargs;
    int args[6];
    int nw;                    /* dword'ов записано через указатель (0..3) */
    int w[3];
    const LeafRec *rec;
    int mut;                   /* b0_leaf_mutators: индекс+1 натива, менявшего
                                  вход листа до заявки (0 — не менялся) */
    int resync;                /* 1 — вход/значение листа восстановлены из
                                  bdd0_leaf: сравнение было бы тавтологией.
                                  Голова ленты (band_resync_from_stream) —
                                  SKIP заявки; out-записи 45c6f0
                                  (b0_leaf_w_recon) — только поля w идут в nno
                                  с B0_POS_RESYNC_WHY, ret/args/ecx сверяются */
    LeafClaimEnt()
        : order(0), call_seq(0), va(0), ecx(0), ret(0), nargs(0), nw(0),
          rec(NULL), mut(0), resync(0)
    {
        for (int i = 0; i < 6; i++) args[i] = 0;
        for (int i = 0; i < 3; i++) w[i] = 0;
    }
};

/* --------------------------- контракт проверок ---------------------------- */
enum {
    NEED_DECISIONS  = 1u << 0, /* al/out/u170/m0..m2 тика */
    NEED_POST_STATE = 1u << 1, /* снапшоты v12: frame/unit/ai/band */
    NEED_CALLLOG    = 1u << 2, /* поток листьев bdd0_leaf */
    NEED_TIMING     = 1u << 3, /* ядро: wall-clock */
    NEED_HANDLES    = 1u << 4, /* карта указателей снимка */
    NEED_GAMESTATE  = 1u << 5, /* gs_b64 (объект 442570) */
    NEED_BODY       = 1u << 6  /* нужен реально исполненный тик */
};

struct TickContext {
    int seq, tick, unit_index;
    const char *scenario, *impl;
    int al, out_slot, u170;
    int out_known;             /* p3 != NULL: исход наблюдаем */
    long long wall_ms, leaf_calls;
    int ran_body;              /* реализация исполняла порт (не replay исхода) */
    int hang;                  /* watchdog сработал раньше реализации */
    int trapped;
    const char *trap_why;
    unsigned long trap_addr;
    int trap_unbound;            /* trap at an unbound materialisation pointer */
    const RegionView *regions;       /* пост-состояние тика */
    int region_n;
    const RegionView *regions_before;/* входной снимок (тот же порядок имён) */
    int region_before_n;
    const CallLogEnt *log;
    int log_n;
    const TickRecord *rec;
    unsigned avail;
    /* механические факты ядра, которые читают проверки */
    int write_outside;                /* запись вне объявленных областей */
    const char *write_outside_why;
    int handle_miss;                  /* unmapped handle снимка */
    const char *handle_miss_why;
    int leaf_violation;               /* расхождение потока листьев */
    const char *leaf_violation_why;
    int native_violation;             /* расхождение потока bdd0_native */
    const char *native_violation_why;
    int native_calls;                 /* запросов не-листовых нативов в тике */
    char *why_buf;
    unsigned why_cap;

    unsigned field(const char *name) const;
    const unsigned char *region(const char *name) const;
    const unsigned char *region_before(const char *name) const;
};

/* --path-diff diagnostic (not a check): recorded vs consumed call order. */
void bdd0_path_diff(const Scenario *sc, const TickRecord *t,
                    const TickContext &tc, FILE *json);

/* CheckResult: why — стабильная строка проверки (ядро её только печатает).
 * skip=1 — проверке нечем мерить (ядро считает SKIP, а не pass). */
struct CheckResult {
    int ok;
    const char *why;
    int skip;
};

typedef CheckResult (*CheckFn)(const TickContext &);

struct CheckDef {
    const char *name;
    CheckFn fn;
    unsigned needs;
};

/* Реестр проверок (registry.cpp): единственный способ добавить проверку.
 * Пример добавления (ядро НЕ трогается — одна строка):
 *   register_check({ "target_in_grid", target_in_grid, NEED_DECISIONS }); */
void register_check(const CheckDef &def);
int check_count(void);
const CheckDef *check_at(int i);
const char *need_name(unsigned bit);
const char *whyf(const TickContext &tc, const char *fmt, ...);
int field_count(void);

/* Встроенные проверки (checks_builtin.cpp): no_trap, bounds, budget,
 * contract_min + механические leaf_seq и handles. */
int bdd0_path_state_calls(void);
int bdd0_path_state_p9_total(void);
int bdd0_path_state_inputs_fail(void);
int bdd0_path_recompute_port(void);
int bdd0_path_recompute_replayed(void);
int bdd0_path_post_match_fail(void);
/* v18: расхождения вычисленных значений границ листа 0x45e1d0 с записями
 * (порт не подставляет записанное молча — счётчик виден в сводке). */
int bdd0_path_boundary_mismatch(void);

void register_builtin_checks(void);
/* Политика бюджета (CLI --budget-ms/--budget-calls); домены — в проверке. */
void bdd0_budget_set(int ms, int calls);
int bdd0_budget_ms(void);
int bdd0_budget_calls(void);

/* ---------------------------- реестр реализаций --------------------------- */
struct Arena;                 /* непрозрачная арена (arena.cpp) */

typedef int (*TickFn)(void *unit, void *frame);

struct ImplDef {
    const char *name;
    void (*prepare)(Arena *a);   /* связать синтетику с областями (опц.) */
    TickFn tick;                 /* NULL — тело не исполняется (replay исхода) */
    void (*release)(Arena *a);   /* (опц.) */
    int runs_body;               /* 1 — тик реально исполняет порт */
};

void register_impl(const ImplDef &def);
const ImplDef *find_impl(const char *name);
int impl_count(void);
const ImplDef *impl_at(int i);
void register_builtin_impls(void);

/* ------------------------------ арена тика -------------------------------- */
/* Сборка арены из записи: материализация областей + карта указателей снимка
 * (см. таблицу k_fixups в arena.cpp). Возврат копии pre-state для bounds. */
Arena *arena_build(const TickRecord *rec);
void arena_release(Arena *a);
void arena_set_current(Arena *a);
Arena *arena_current(void);
/* v16: модель сетки на весь файл (kind:"grid"). Ставится загрузчиком один раз;
 * arena_build копирует указатель в арену, bdd0_grid_level_modelled() его читает. */
void arena_set_grid_model(const GridModel *g);
/* v16: генератор синтетики ставит 1 — гейты модели (bdd0_*_modelled) читают 0
 * во время записи файла (запись ведёт модельный натив); харнесс оставляет 0. */
void arena_set_model_gates_off(int off);
/* v16: сколько bind_add отброшено из-за переполнения (диагностика, не молчание). */
int arena_bind_overflow(const Arena *a);
/* v17: shadow-указатель → сырой адрес игры (0 — не из арены). */
unsigned arena_raw_of(const void *p);

const RegionView *arena_regions(const Arena *a);
int arena_region_n(const Arena *a);
/* 1 — арена несёт модель объекта сетки, и тело может исполнять порт 0x458050
 * (см. arena.cpp). Сегодня 0: сетку не пишет ни один формат захвата. */
int bdd0_grid_level_modelled(void);
/* Запись, из которой собрана арена (ctx_pos и т.п. — источники захвата). */
const TickRecord *arena_record(const Arena *a);
/* v17: материализованный вход листа 0x45e1d0 (NULL — данных нет / не ok). */
const PathCall *arena_path_call(const Arena *a);
/* v17: привязать записанный сырой адрес к занулённому блоку (replay-аллокации).
 * name — уникальное имя области (у каждой replay-аллокации своё: повтор имени
 * вернул бы первый блок). Возврат — сам блок арены, NULL — не удалось. */
void *arena_bind_alloc(const char *name, unsigned raw, unsigned size);
/* v18: блок по имени с переиспользованием (создаётся при первом обращении, затем
 * привязывается новый сырой адрес) — PathState-кандидаты скана. */
void *arena_bind_reuse(const char *name, unsigned raw, unsigned size);
const RegionView *arena_views(const Arena *a);
const RegionView *arena_pre_views(const Arena *a);
unsigned char *arena_region_w(Arena *a, const char *name);
const unsigned char *arena_region_r(const Arena *a, const char *name);
const unsigned char *arena_region_pre(const Arena *a, const char *name);
unsigned char *arena_out_slot(Arena *a);
int arena_out_known(const Arena *a);
void arena_set_out_slot(Arena *a, int value);

/* Мэппинг адреса сбоя в `region+0xNN` (или NULL). */
const char *arena_why_addr(const Arena *a, unsigned long addr, char *buf, int cap);
/* Изменённые после тика байты вне объявленных областей / канарейки.
 * Возврат 1 при нарушении, why — стабильное описание. */
int arena_scan_writes(Arena *a, char *why, int cap);
/* Пропущенные handle'ы снимка (указатель не был материализован). */
int arena_handle_misses(const Arena *a);
const char *arena_handle_miss_why(const Arena *a);
/* 1 — адрес ловушки пришёл от неcвязанного указателя (см. handles). */
int arena_fault_unbound(Arena *a, unsigned long addr);
/* Перевод указателя из пространства игры в shadow-адрес (нативный слой).
 * arena_resolve_ptr_at знает сайт вызова (VA call site + VA натива) и привязывает
 * по идентичности пары из k_identity_rets (arena.cpp); arena_resolve_ptr — без
 * сайта, привязка по идентичности неприменима. */
void *arena_resolve_ptr(unsigned raw);
void *arena_resolve_ptr_at(unsigned raw, unsigned site, unsigned va);
/* Сколько leaf rets привязано по идентичности (sentinels из k_identity_rets). */
int arena_leaf_identity(const Arena *a);
/* Сколько opaque-узлов выдано листам вместо материализации (диагностика:
 * непривязанный адрес даёт сентинел, а не живой объект). */
int arena_leaf_nodes(const Arena *a);
/* Проверка «адрес лежит внутри объявленной области (len байт от p)». */
int arena_addr_ok(const void *p, unsigned len);
/* Запись поля по имени: оффсеты живут только в таблице полей (registry.cpp),
 * ядро адресов не считает. */
int field_write(Arena *a, const char *name, unsigned value);

/* Нативный слой (native_replay.cpp) для replay-режима:
 * поток листьев текущего seq + журнал вызовов. */
void bdd0_leaf_begin(int seq, const LeafRec *v, int n);
/* v17: окно внутренних вызовов листа 0x45e1d0 для этого seq (массив + длина);
 * NULL/0 — данных нет. Ставится ядром сразу за bdd0_leaf_begin. */
void bdd0_nested_set_window(const NestedRec *v, int n);
/* v18: сколько записей bdd0_cand прочитано (0 — данные до v18). */
int bdd0_cand_records(void);
/* v18: счётчики порта листа 0x45e1d0 для сводки/проверки path_post_match:
 * insert_count/at — что порт отдал регистрационной границе dd00 (ключи map1/map2
 * + PathState 0x58), boundary_mismatch — расхождения вычисленных значений границ
 * с записанными. */
/* v18: что порт отдал регистрационной границе dd00 (для path_post_match).
 * key1_raw — ключ map1 = указатель pos в сыром адресе игры. Ключ map2
 * (Y<<16)|X = (u16 cell[0x10]<<16)|u16 cell[0x0c] (0x45dd32..0x45dd3d) офлайн
 * НЕ выводим: поля ячейки +0x0c/+0x10 в grid-запись не попадают (write_grid
 * несёт только key_rec/next/min_metric), поэтому карта map2 сверяется по
 * идентичности PathState (тот же объект стоит значением в обеих картах). */
struct B0PathInsert {
    unsigned key1_raw;            /* ключ map1 = pos-указатель (сырой адрес) */
    unsigned ps[BDD0_PS_LEN / 4];
};
int b0_path_insert_count(void);
const B0PathInsert *b0_path_insert_at(int i);
int b0_path_boundary_mismatch(void);
const char *b0_path_boundary_why(void);
/* v18: заявки портированных листьев приостановлены — вызов идёт ВНУТРИ тела
 * другого листа (его запись лежит в окне bdd0_nested, а не в потоке тика). */
void b0_leaf_claim_suspend(int on);
/* v18: сверка указателя-результата границы с ret последней потреблённой записи
 * окна (символьно: shadow → сырой адрес игры). */
void b0_nested_expect_ptr(unsigned va, const void *p);
int bdd0_leaf_calls(void);
int bdd0_leaf_violations(void);
const char *bdd0_leaf_violation_why(void);
const CallLogEnt *bdd0_calllog(int *n);
void bdd0_calllog_reset(void);
/* 458050: 1, если последний тик реально потребил запись потока bdd0_leaf
 * va=0x458050 (запрос native::grid_anchor, т.е. порт не исполнялся — модель
 * сетки в захвате отсутствует). Считает тики с подстановкой реплея. */
int bdd0_grid_anchor_replayed(void);
/* 45b150: то же для float-гейта ленты — заявка порта не выпускалась, тик
 * потребил запись потока (геометрия ленты в захвате отсутствует). */
int bdd0_band_gate_replayed(void);
/* 45b3e0: то же для предиката ленты — sites 18/22 реплеятся, порт не исполнялся
 * (геометрия ленты в захвате отсутствует). */
int bdd0_band_target_replayed(void);
/* v16: порт реально исполнился (заявка снята) — счётчики исполнения. */
int bdd0_band_gate_port(void);
int bdd0_band_target_port(void);
int bdd0_grid_anchor_port(void);
const LeafRec *bdd0_leaf_record_at(int i);

/* v17: внутренние вызовы листа 0x45e1d0 (scenario.cpp хранит, native_replay
 * потребляет). Окно текущего тика ставит bdd0_nested_set_window (ядро, сразу
 * за bdd0_leaf_begin); тела порта (recompute.cpp) читают по одному в порядке
 * исполнения. Возврат NULL — записей нет / поток исчерпан. */
int bdd0_nested_count(void);                    /* записей в окне тика */
const NestedRec *bdd0_nested_peek(int i);       /* i-я запись окна (0-based) */
const NestedRec *bdd0_nested_next(void);        /* следующая, курсор +1 */
int bdd0_nested_consumed(void);                 /* сколько выдано next() */
void bdd0_nested_reset(void);                   /* курсор в 0 (per tick) */
const LeafRec *bdd0_tick_leaf_at(int i);        /* запись потока bdd0_leaf тика */

/* v17: состояние листа 0x45e1d0 (pre/post/site_pf) — доступ по (seq,order).
 * Возврат NULL — записи нет (старый захват / другой вызов). ok=0 — запись
 * неполна, порт её не использует (fail-closed). */
const LeafStateRec *bdd0_leaf_state_pre(int seq, int order);
const LeafStateRec *bdd0_leaf_state_post(int seq, int order);
const LeafStateRec *bdd0_leaf_state_site_pf(int seq, int order);
int bdd0_leaf_state_records(void);   /* v17: сколько state-записей в файле */
int bdd0_site_pf_records(void);      /* v17: сколько bdd0_site_pf (только v17) */
const LeafStateRec *bdd0_leaf_pre_at(int idx);  /* v17: pre-запись по индексу */

/* v12: заявки портированных листьев (battle/ported.h) текущего тика. */
int bdd0_leaf_claims(void);
const LeafClaimEnt *bdd0_leaf_claim_at(int i);

/* v12: поток bdd0_native текущего seq (replay не-листовых RECORD-нативов). */
void bdd0_native_begin(int seq, const NativeRec *v, int n,
                       const TickRecord *rec);
int bdd0_native_calls(void);
int bdd0_native_violations(void);
const char *bdd0_native_violation_why(void);
int bdd0_native_consumed(void);
const NativeRec *bdd0_native_pending(void);
/* v15: 1 — запись bdd0_native для 0x45eee0 отсутствовала и восстановлена из
 * bdd0_res (захват с LDHOOK_BATTLE_CAPTURE_E0=0); u170 тогда не независим. */
int bdd0_eee0_synth(void);const NativeCallLogEnt *bdd0_native_calllog(int *n);
const NativeRec *bdd0_native_record_at(int i);

/* Счётчики «не смоделировано» (native_stubs.cpp): печатаются в сводке прогона,
 * чтобы no-op'ы не выглядели проверенными. */
int bdd0_unmodelled_calls(void);
int bdd0_unmodelled_unit_ai_tick(void);

/* v12: uninitialised native arguments skipped by leaf_args/native_args. */
void bdd0_uninit_reset(void);
int bdd0_uninit_skipped(void);

/* v12: аргументы, которые НЕ сравнимы (by-value область кадра вызывающего),
 * skips по нативным сайтам 0x44bfa7/0x44c109/0x44c9d5/0x44cee1. */
void bdd0_noncomp_reset(void);
int bdd0_noncomp_skipped(void);

#ifdef B0_TRACE_LABELS
/* Label trace (tools/ldhook/battle/tick.cpp): существует только в trace-сборке
 * харнесса (-DB0_TRACE_LABELS=1).  События кольца тика: id метки
 * (< B0_EXIT_BASE), id выхода (B0_EXIT_BASE..B0_COND_NAME_BASE), событие
 * условия (бит 31 — событие условия, бит 30 — ветка взята, младшие биты —
 * номер гейта).  Имена меток/условий — b0_label_names[]/b0_cond_names[]. */
#define B0_EXIT_BASE 128
#define B0_COND_NAME_BASE 256
#define B0_TRACE_RING 2048
extern volatile int b0_trace_on;
extern volatile unsigned b0_last_label;
extern volatile int b0_tick_len;
extern volatile int b0_ring_i;
extern const unsigned *b0_trace_ring(void);
extern const char *b0_label_names[];
extern const char *b0_cond_names[];
#endif

#endif /* BDD0_HARNESS_H */
