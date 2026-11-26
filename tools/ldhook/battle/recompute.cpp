/* tools/ldhook/battle/recompute.cpp — порт листа FUN_0045e1d0 (grid_recompute).
 *
 * Тело 0x45e1d0..0x45e504 (821 Б, 254 инстр., один `ret 8`), план
 * re/leaf_45e1d0_port_plan.md §1 (P0..P11). Кадр: `sub esp,0x5c` + 4 push ⇒
 * B = entry−0x6c; param_1 = [B+0x70] (out-тройка caller'а), param_2 = [B+0x74]
 * (grid-record; после 0x45e293 — scratch). ECX = PathFinder (pf).
 *
 * Разделение (план §0/§4):
 *   считает сам — pf+0x00 (ленивый rec), pf+0x28, pf+0x2c, шаблон PathState
 *   (x87 P7), dist² (s6_distance2), out-тройку, флаг-расход кандидатов
 *   (cand[0x08]=3), счётчик регистраций pf+0x70 (nreg);
 *   берёт из записей — всё, что касается кучи и RB: out-пара 456b20 (w[0..1]),
 *   состояние после 45c8c0 (site_pf), h гейта 45c690 (его тело зовёт 44bbf0,
 *   которой в окне листа не записано), вставки карт (45dd00/45dd70 — реплей),
 *   сброс 45c330 (его записи вне change-set), PathState-кандидатов скана
 *   (bdd0_cand, v18).
 * Граница порта (path_boundary) — (site, va, ECX-приёмник, стек-аргументы):
 * в replay она сверяет приёмник и аргументы с записанной записью окна
 * (b0_path_args_*), в живом билде вызывает тот же нативный путь по VA.
 *
 * ЛИСТИНГ (game_dis.txt, 0x45e1d0..0x45e504):
 *   P0 45e1d0 sub esp,0x5c / push ebx,ebp,esi,edi; esi=ecx; eax=[esi]; ebx=0
 *   P1 45e1d1..45e1ff  if([esi]==0){ eax=new(0x1c); if(eax){[eax+4..0x10]=0}
 *                      else eax=0; [esi]=eax }
 *   P2 45e201 edi=[esp+0x74]=param_2; rec=[esi]; rec[0]=[edi]; rec[0x14]=[edi+0x14];
 *      rec[0x18]=[edi+0x18]; rec[8]=[edi+8]; rec[0x10]=[edi+0x10]; rec[4]=[edi+4];
 *      if(rec[0xc]==0){eax=new(0x10); if(eax)[eax+0xc]=0; else eax=0; rec[0xc]=eax}
 *      sub=rec[0xc]; src=[edi+0xc]; sub[0..0xc]=src[0..0xc]
 *   P3 45e274 call 45c330(pf); 45e27b call 45ca70(pf)
 *   P4 45e280 eax=[rec+4][0x120] (ctx120); ecx=&B+0x18; push; ecx=[edi+0x10];
 *      [B+0x74]=ctx120 (scratch); call 456b20 → out1=B+0x18; edx=[out1+4];
 *      ecx=[edi+0x10]; eax=&B+0x20; push; [B+0x14]=edx; call 456b20 → out2=B+0x20;
 *      ecx=[out2]; eax=[B+0x14]; [B+0x34]=eax(out1[1]); [B+0x2c]=ecx(out2[0]);
 *      [B+0x30]=ctx120; edi=&pf+0x28; eax=[esi]; ecx=[eax+0x10];
 *      if(ecx!=0)[edi]=ecx; else [edi]=[[eax+4]+0x134] (ctx134)
 *   P5 45e2df [B+0x13]=0; ecx=[rec+4]; eax=[rec+0xc]=sub; push ecx(world);
 *      push &B+0x13(flag); 16-Б by-value блок = sub[0..0xc];
 *      ecx=[rec+0x8] (tree); call 458050 → EAX; [esi+0x2c]=EAX;
 *      ecx=esi; call 45c8c0
 *   P6 45e325 push edi(&pf+0x28); push &B+0x74(out); ecx=&pf+0x68;
 *      call 434bc0; eax=[eax]; cmp eax,[esi+0x6c]; jne 45e3f7 (P7 пропущен)
 *   P7 45e33e eax=45c2e0(pf) → PathState (ebp); fld [rec+4][0x120];
 *      edi=[edi]=[pf+0x28]; eax=[pf+0x2c]; fadd [edi+0x14]; push eax; push edi;
 *      fstp t+0x10; t+0x00=edi; t+0x08=1; t+0x28=0xcb18967f; t+0x2c=0x4b18967f;
 *      call 45c480(edi,pf+0x2c) → dist²; t+0x1c=dist²; call 45c690(&t) → h;
 *      imul h,0x2710; t+0x20=h*10000; t+0x24=dist²+h*10000; call 45c4b0(&t,ps,0x1fff);
 *      call 45dd00(ps)
 *   P8 45e3f7 t[0..0x30]=0 (кроме t+0x28/0x2c сентинелов); call 45ce80(1) → cand
 *   P9 45e448 edi=cand; if(edi==0) → P11; ebp=3;
 *      45e457 if([edi+0x20]==0) → P10; push ctx134; push edi; call 45dd70;
 *      t+0x08=3; push 4; push edi; push &t; call 45c4b0(&t,edi,4); call 45ce80(1);
 *      edi=eax; if(edi!=0) → 45e457; else → P11
 *   P10 45e494 outC=&B+0x20; push; ecx=[edi]; call 456b20; fld [outC+4];
 *      fadd [edi+0x14]; fstp [B+0x74](y); ecx=[edi]; outD=&B+0x18; push;
 *      call 456b20; fld [edi+0xc]; fadd [outD]; [B+0x24]=0; edx=[B+0x24];
 *      fstp [B+0x20](x); ecx=[B+0x20]; [B+0x28]=y; [B+0x2c]=x; [B+0x30]=0;
 *      [B+0x34]=y  ⇒ out = {x, 0, y}
 *   P11 45e4e3 eax=param_1; fld [B+0x2c]; ecx=[B+0x30]; edx=[B+0x34];
 *      [eax]=x; [eax+4]=ecx; [eax+8]=edx; эпилог
 *
 * ВНИМАНИЕ к арифметике: pf — сырой блок арены, все смещения — БАЙТОВЫЕ
 * (unsigned char*), не LDPos/GridCell-масштабированные.
 *
 * ОГРАНИЧЕНИЕ, закрытое v18: приёмник 456b20 = [param_2+0x10] — GridCell ВНУТРИ
 * массива cells, а в grid-запись попадают только key/next/min_metric ячейки (нет
 * +0x0c/+0x10 координат и +0x1c уровня). Поэтому out-пара берётся из записанных
 * w[0..1] сайтов 39/40/53/54 (род LS_K_INT_W2_ARG1), а не вычисляется.
 */
#include <string.h>

#include "battle.h"
#include "native.h"
#include "ported.h"
#include "../ldhook_internal.h"   /* LD_CRASH_SCOPE (LDHOOK_CRASHLOG) */

#ifdef BATTLE_REPLAY
#define PATH_REPLAY 1
#else
#define PATH_REPLAY 0
#endif

static unsigned rd32(const void *p) { unsigned v; memcpy(&v, p, 4); return v; }
static void wr32(void *p, unsigned v) { memcpy(p, &v, 4); }
static float rdf(const void *p) { float f; memcpy(&f, p, 4); return f; }
static unsigned fb(float f) { unsigned u; memcpy(&u, &f, 4); return u; }

/* ------------------------- границы (replay / live) ------------------------ *
 * В replay граница потребляет следующую запись bdd0_nested с этим VA и сверяет
 * приёмник/аргументы с записью (b0_path_args_*), возвращает записанный EAX и w
 * (dword'ы, записанные через указатель). В живой — нативный вызов по VA.
 * ecx — приёмник (__thiscall; для cdecl `4cfa11` не используется, там NULL).
 * args/nargs — СТЕК-аргументы сайта в порядке нативного вызова: у 434bc0 это
 * {out, key} при ECX = &pf+0x68, у 458050 — {x, y_metric, y_coord, node, &flag,
 * world} при ECX = tree (16-байтный by-value блок — первые четыре).
 * has_ret/ret_expect — вычисленное портом значение сверяется с записью
 * (расхождение считает b0_path_boundary_mismatch, тихой подстановки нет). */
static int path_boundary(unsigned site, unsigned va, const void *ecx,
                         const int *args, int nargs,
                         int *w3, int has_ret, int ret_expect)
{
    int wv[3] = { 0, 0, 0 }, nwv = 0, ret = 0;

#if PATH_REPLAY
    ret = b0_nested_call(site, va, (int)(size_t)ecx, args, nargs, wv, &nwv,
                         has_ret, ret_expect);
#else
    (void)site; (void)nargs; (void)has_ret; (void)ret_expect;
    switch (va) {
    case 0x4cfa11u: ret = (int)(size_t)native::operator_new((unsigned)args[0]); break;
    case 0x45c330u: s6_ai_reset((BattleAI *)ecx); ret = 0; break;
    case 0x45ca70u: native::clear_maps((void *)ecx); ret = 0; break;
    case 0x45c8c0u: native::search_params_update((void *)ecx); ret = 0; break;
    case 0x456b20u: ret = (int)(size_t)s6_grid_scale_compute((LDPos *)ecx,
                                        (float *)(size_t)args[0]); break;
    case 0x45c480u: ret = s6_distance2((void *)(size_t)args[0], (void *)(size_t)args[1]); break;
    case 0x45c690u: ret = s6_heuristic_gate((void *)ecx, (int *)(size_t)args[0]); break;
    case 0x45c4b0u: s6_copy_fields(args[1], args[0], (unsigned)args[2]); ret = 0; break;
    case 0x45dd00u: native::register_occupied_slot((void *)ecx, (LDObject *)(size_t)args[0]); ret = 0; break;
    case 0x45ce80u: ret = (int)(size_t)s6_scan_map((void *)ecx, args[0]); break;
    case 0x45dd70u: ret = s6_ai_update((void *)ecx, (void *)(size_t)args[0], args[1]); break;
    case 0x434bc0u: ret = (int)(size_t)native::lower_bound_slots((void *)ecx,
                                        (int *)(size_t)args[0],
                                        (int *)(size_t)args[1]); break;
    case 0x458050u: ret = (int)(size_t)native::grid_anchor((void *)ecx, args[0],
                                        args[1], args[2], args[3],
                                        (int *)(size_t)args[4],
                                        (void *)(size_t)args[5]); break;
    default: break;
    }
#endif
    if (w3 != NULL) { w3[0] = wv[0]; w3[1] = wv[1]; w3[2] = wv[2]; }
    return ret;
}

/* Аллокация границы (4cfa11 / 45c2e0): replay — записанный указатель, привязанный
 * к занулённому блоку арены; живой — native::operator_new. */
static void *path_alloc(unsigned site, unsigned va, unsigned size, int *ok)
{
#if PATH_REPLAY
    int o = 0;
    void *p = b0_nested_alloc(site, va, size, &o);
    if (ok != NULL) *ok = o;
    return p;
#else
    (void)site; (void)va;
    if (ok != NULL) *ok = 1;
    return native::operator_new(size);
#endif
}

/* Кандидат скана 45ce80: в replay — материализованный PathState из bdd0_cand,
 * привязанный к записанному адресу (v18); в живом билде порт не исполняется. */
static void *path_cand_take(unsigned site)
{
#if PATH_REPLAY
    return b0_cand_materialise(site);
#else
    (void)site;
    return NULL;
#endif
}

/* dist² = (a[0xc]−b[0xc])² + (a[0x10]−b[0x10])² в 32-битной арифметике —
 * та же формула, что FUN_0045c480 (s6_distance2, target.cpp). Выражение
 * продублировано здесь потому, что офлайн-харнесс не линкует target.cpp
 * (тянет весь replay-реестр); эквивалентность проверяется через pfs: +0x1c
 * (=dist²) сверяется побитово с post-узлом карты (path_post_match, уровень 3). */
static int path_dist2(const void *a, const void *b)
{
    int dx = (int)rd32((const unsigned char *)a + 0x0c) -
             (int)rd32((const unsigned char *)b + 0x0c);
    int dy = (int)rd32((const unsigned char *)a + 0x10) -
             (int)rd32((const unsigned char *)b + 0x10);
    return (int)((unsigned)dx * (unsigned)dx + (unsigned)dy * (unsigned)dy);
}

/* P6 (45e333..45e338): `mov (%eax),%eax; cmp eax,[esi+0x6c]; jne 45e3f7` —
 * итератор lower_bound(map1, &pf+0x28) сравнивается с головой map1; `jne`
 * пропускает P7. Карты очищены вызовом 45ca70 (@0x45e27b) непосредственно
 * перед этим, pre-карты пусты (n1=n2=0 в каждом записанном вызове), поэтому
 * корень пуст и итератор = end = голова ⇒ P7 исполняется. Условие считается по
 * состоянию арены; вставки карт реплеятся (RB-куча не моделируется), полный
 * обход дерева здесь не нужен. */
static int path_map1_at_end(const unsigned char *pf)
{
    unsigned head = rd32(pf + 0x6c);
    unsigned root;

    if (head == 0) return 1;
    root = rd32((const unsigned char *)(size_t)head + 0x04);  /* TreeNode.parent */
    if (root == 0) return 1;
    return (((const unsigned char *)(size_t)root)[0x15] != 0) ? 1 : 0;
}

/* 458050 (grid-якорь): при модели сетки — тело порта, иначе записанный натив
 * (арена без grid-модели не даёт ни coordinate-полей, ни строк). Запись окна
 * потребляется В ЛЮБОМ случае (порядок потока). */
static void *path_grid_anchor(unsigned char *tree, const void *block,
                              unsigned char *flag, void *world)
{
    int av[6];
    const unsigned char *blk = (const unsigned char *)block;

    av[0] = (int)rd32(blk + 0x00);      /* x */
    av[1] = (int)rd32(blk + 0x04);      /* y_metric */
    av[2] = (int)rd32(blk + 0x08);      /* y_coord */
    av[3] = (int)rd32(blk + 0x0c);      /* node */
    av[4] = (int)(size_t)flag;
    av[5] = (int)(size_t)world;
#if PATH_REPLAY
    if (bdd0_grid_level_modelled()) {
        void *res;

        /* Вызов идёт изнутри тела листа: его запись — в окне bdd0_nested, а не
         * в потоке тика, поэтому заявки вложенных листьев (456b60/457e20)
         * подавляются, а значение сверяется с записью символьно. */
        b0_leaf_claim_suspend(1);
        res = s6_grid_anchor_lookup((GridLevel *)tree, rdf(blk + 0x00),
                                    rdf(blk + 0x04), rdf(blk + 0x08),
                                    (void *)(size_t)(unsigned)av[3], flag, world);
        b0_leaf_claim_suspend(0);
        path_boundary(0x45e316u, 0x458050u, tree, av, 6, NULL, 0, 0);
        b0_nested_expect_ptr(0x458050u, res);
        return res;
    }
#endif
    return (void *)(size_t)path_boundary(0x45e316u, 0x458050u, tree, av, 6,
                                         NULL, 0, 0);
}

/* ------------------------------ тело листа -------------------------------- */
int s6_grid_recompute(void *pf_raw, void *arg1, void *arg2)
{
    LD_CRASH_SCOPE(5);           /* LDHOOK_CRASHLOG: stage 5 (no-op off) */
    unsigned char *pf = (unsigned char *)pf_raw;
    unsigned char *bandrec = (unsigned char *)arg2;
    unsigned char *rec = NULL, *sub = NULL, *ctx = NULL;
    unsigned char *tpl = NULL;          /* шаблон PathState: локальный буфер B+0x38 */
    unsigned char tpl_buf[0x34];
    unsigned char *pfs = NULL;          /* PathState из 45c2e0 (heap, реплей) */
    void *cell = NULL;                  /* pf+0x28 — приёмник 456b20/45c480 */
    void *anchor = NULL;                /* pf+0x2c — результат 458050 */
    int ok = 1, i, nreg = 0;
    int out0 = 0, out1 = 0, out2 = 0;   /* out-тройка (биты) */

    if (pf == NULL || arg1 == NULL || bandrec == NULL) return 0;

    /* ---- P1 (45e1d1..45e1ff): ленивый rec 0x1c ---- */
    if (rd32(pf + 0x00) == 0) {
        void *r = path_alloc(0x45e1e3u, 0x4cfa11u, 0x1c, &ok);
        wr32(pf + 0x00, (ok && r != NULL) ? (unsigned)(size_t)r : 0u);
    }
    rec = (unsigned char *)(size_t)rd32(pf + 0x00);
    if (rec == NULL) return 0;          /* аллокация не удалась — тело не идёт */

    /* ---- P2 (45e201..45e26f): перенос рекорда + ленивый sub 0x10 ---- */
    wr32(rec + 0x00, rd32(bandrec + 0x00));
    wr32(rec + 0x14, rd32(bandrec + 0x14));
    wr32(rec + 0x18, rd32(bandrec + 0x18));
    wr32(rec + 0x08, rd32(bandrec + 0x08));
    wr32(rec + 0x10, rd32(bandrec + 0x10));
    wr32(rec + 0x04, rd32(bandrec + 0x04));
    if (rd32(rec + 0x0c) == 0) {
        void *s = path_alloc(0x45e23cu, 0x4cfa11u, 0x10, &ok);
        wr32(rec + 0x0c, (ok && s != NULL) ? (unsigned)(size_t)s : 0u);
    }
    sub = (unsigned char *)(size_t)rd32(rec + 0x0c);
    if (sub != NULL) {
        unsigned char *psrc = (unsigned char *)(size_t)rd32(bandrec + 0x0c);
        if (psrc != NULL) {
            for (i = 0; i < 4; i++) wr32(sub + i * 4, rd32(psrc + i * 4));
        }
    }
    ctx = (unsigned char *)(size_t)rd32(rec + 0x04);   /* = unit (world) */

    /* ---- P3 (45e274/45e27b): сброс AI + очистка карт ---- */
    /* 45c330 (s6_ai_reset, move.cpp) пишет pf+0x04,0x0c,0x08,0x10,0x14,RECT
     * +0x18..0x27, +0x28/+0x2c (их тут же перезаписывают P4/P5), байт +0x30 и
     * +0x34=0x0098967f — все вне change-set. В replay вызов реплеится (запись
     * окна расходуется, тело не дублируется), в живом билде граница зовёт
     * s6_ai_reset(ecx). */
    path_boundary(0x45e274u, 0x45c330u, pf, NULL, 0, NULL, 0, 0);
    path_boundary(0x45e27bu, 0x45ca70u, pf, NULL, 0, NULL, 0, 0);

    /* ---- P4 (45e280..45e2dd): grid-scale ×2 (out-пара из записей) + опора ---- */
    {
        int wa[3] = { 0, 0, 0 }, wb[3] = { 0, 0, 0 };
        int av[1];
        int b18[2] = { 0, 0 }, b20[2] = { 0, 0 };   /* out-слоты B+0x18 / B+0x20 */
        void *recv;
        unsigned ctx120 = (ctx != NULL) ? rd32(ctx + 0x120) : 0;

        recv = (void *)(size_t)rd32(rec + 0x10);    /* ECX = [edi+0x10] (anchor) */
        av[0] = (int)(size_t)b18;
        path_boundary(0x45e297u, 0x456b20u, recv, av, 1, wa, 0, 0);
        av[0] = (int)(size_t)b20;
        path_boundary(0x45e2abu, 0x456b20u, recv, av, 1, wb, 0, 0);
        /* [B+0x2c] = out2[0], [B+0x30] = ctx120, [B+0x34] = out1[1] (45e2b0..).
         * В replay значения приходят записанными (w), в живом билде — из
         * out-слотов, которые заполнил сам вызов. */
#if PATH_REPLAY
        out0 = wb[0];
        out2 = wa[1];
#else
        out0 = b20[0];
        out2 = b18[1];
#endif
        out1 = (int)ctx120;

        cell = (void *)(size_t)rd32(rec + 0x10);
        if (rd32(rec + 0x10) == 0 && ctx != NULL)
            cell = (void *)(size_t)rd32(ctx + 0x134);       /* ctx134 */
        wr32(pf + 0x28, (unsigned)(size_t)cell);
    }

    /* ---- P5 (45e2df..45e323): anchor lookup + search_params ---- */
    {
        unsigned char flag = 0;
        unsigned char *tree = (unsigned char *)(size_t)rd32(rec + 0x08);

        anchor = path_grid_anchor(tree, sub, &flag, ctx);   /* ECX = [rec+8] */
        wr32(pf + 0x2c, (unsigned)(size_t)anchor);          /* @0x45e31d */
        path_boundary(0x45e320u, 0x45c8c0u, pf, NULL, 0, NULL, 0, 0);
#if PATH_REPLAY
        /* выход 45c8c0 (pf+0x08..0x27) офлайн не выводим — он в bdd0_site_pf */
        b0_path_apply_site_pf(pf);
#endif
    }

    /* ---- P6 (45e325..45e339): lower_bound(map1, &pf+0x28) ---- */
    {
        int av[2];
        int lbout = 0;                          /* out-слот B+0x74 */

        av[0] = (int)(size_t)&lbout;            /* arg1 = out */
        av[1] = (int)(size_t)(pf + 0x28);       /* arg2 = key */
        path_boundary(0x45e32eu, 0x434bc0u, pf + 0x68, av, 2, NULL, 0, 0);
        /* `jne 45e3f7` — P7 пропускается, если итератор не голова карты. */
        if (!path_map1_at_end(pf)) goto l_p8;
    }

    /* ---- P7 (45e33e..45e3f5): PathState ctor + шаблон + register ---- */
    {
        int av[2], w0, w1, h, dist2;

#if PATH_REPLAY
        /* ctor 45c2e0: граница отдаёт записанный адрес, привязанный к арене */
        pfs = (unsigned char *)path_alloc(0x45e340u, 0x45c2e0u, 0x58, &ok);
#else
        /* живой билд: тело ctor'а — тот же путь, что у натива (нули + сентинелы
         * + nbr[9], native::operator_new(0x58)) */
        pfs = (unsigned char *)s6_pfs_ctor();
#endif
        if (pfs == NULL) return 0;

        tpl = tpl_buf;
        memset(tpl, 0, sizeof(tpl_buf));
        wr32(tpl + 0x00, (unsigned)(size_t)cell);           /* t+0x00 = [pf+0x28] */
        wr32(tpl + 0x08, 1u);                               /* filter */
        wr32(tpl + 0x10, fb(((ctx != NULL) ? rdf(ctx + 0x120) : 0.0f) +
                            ((cell != NULL) ? rdf((unsigned char *)cell + 0x14)
                                            : 0.0f)));
        wr32(tpl + 0x28, 0xcb18967fu);
        wr32(tpl + 0x2c, 0x4b18967fu);

        av[0] = (int)(size_t)cell;
        av[1] = (int)(size_t)anchor;
        /* dist² считается портом (та же формула, что FUN_0045c480) и сверяется с
         * записью границы: расхождение — не тихая подстановка, а провал тика. */
        dist2 = path_dist2(cell, anchor);
        path_boundary(0x45e3a2u, 0x45c480u, pf, av, 2, NULL, 1, dist2);
        wr32(tpl + 0x1c, (unsigned)dist2);

        av[0] = (int)(size_t)tpl;
        h = path_boundary(0x45e3b4u, 0x45c690u, pf, av, 1, NULL, 0, 0);
        w0 = h * 0x2710;                                    /* imul eax,0x2710 */
        wr32(tpl + 0x20, (unsigned)w0);
        w1 = dist2 + w0;                                    /* add edi,eax */
        wr32(tpl + 0x24, (unsigned)w1);

        /* 45c4b0: src = &t, dst = pfs, mask 0x1fff (нативный порядок src,dst).
         * Маска 0x1fff — ровно биты 0..12 = поля +0x00..+0x30, т.е. первые
         * 0x34 байта: copy_fields вырождается в копию этих байт (поля 0x34..
         * 0x54 остаются от ctor). */
        av[0] = (int)(size_t)tpl; av[1] = (int)(size_t)pfs;
        {
            int av3[3];
            av3[0] = av[0]; av3[1] = av[1]; av3[2] = 0x1fff;
            path_boundary(0x45e3eau, 0x45c4b0u, pf, av3, 3, NULL, 0, 0);
        }
        memcpy(pfs, tpl, 0x34);                             /* mask 0x1fff */
#if PATH_REPLAY
        /* регистрационная граница dd00: ключ map1 = [ps] (указатель pos);
         * PathState — для сверки с post-узлом карты (path_post_match). */
        b0_path_note_insert(rd32(pfs + 0x00), pfs);
#endif
        av[0] = (int)(size_t)pfs;
        path_boundary(0x45e3f2u, 0x45dd00u, pf, av, 1, NULL, 0, 0);
        nreg = 1;
    }

l_p8:
    /* ---- P8 (45e3f7..45e451): обнуление шаблона + первый скан ---- */
    {
        int av[1];
        void *cand;

        tpl = tpl_buf;
        memset(tpl, 0, sizeof(tpl_buf));
        wr32(tpl + 0x28, 0xcb18967fu);
        wr32(tpl + 0x2c, 0x4b18967fu);

        av[0] = 1;                                          /* filter */
        path_boundary(0x45e443u, 0x45ce80u, pf, av, 1, NULL, 0, 0);
        cand = path_cand_take(0x45e443u);
        if (cand == NULL) goto l_p11;

        /* ---- P9 (45e452..45e492): цикл по кандидатам ---- */
        {
            int guard = 0;
            while (cand != NULL) {
                unsigned char *c = (unsigned char *)cand;
                int av2[3];

                if (rd32(c + 0x20) == 0) break;             /* → P10 */
                av2[0] = (int)(size_t)cand;
                av2[1] = (ctx != NULL) ? (int)rd32(ctx + 0x134) : 0;
                path_boundary(0x45e46bu, 0x45dd70u, pf, av2, 2, NULL, 0, 0);
                av2[0] = (int)(size_t)tpl;
                av2[1] = (int)(size_t)cand;
                av2[2] = 4;
                path_boundary(0x45e47eu, 0x45c4b0u, pf, av2, 3, NULL, 0, 0);
                wr32(tpl + 0x08, 3u);                       /* [B+0x40]=ebp=3 */
                wr32((unsigned char *)cand + 0x08, 3u);     /* mask 4 → +0x08 */
                av2[0] = 1;
                path_boundary(0x45e487u, 0x45ce80u, pf, av2, 1, NULL, 0, 0);
                cand = path_cand_take(0x45e487u);
                if (++guard > 100000) break;
            }

            /* ---- P10 (45e494..45e4e2): терминальный кандидат ---- */
            if (cand != NULL) {
                unsigned char *c = (unsigned char *)cand;
                int wa[3] = { 0, 0, 0 }, wb[3] = { 0, 0, 0 }, av1[1];
                int b20[2] = { 0, 0 }, b18[2] = { 0, 0 };   /* outC / outD */
                float y, x;

                av1[0] = (int)(size_t)b20;                  /* arg1 = outC */
                path_boundary(0x45e49bu, 0x456b20u,
                              (void *)(size_t)rd32(c + 0x00), av1, 1, wa, 0, 0);
                av1[0] = (int)(size_t)b18;                  /* arg1 = outD */
                path_boundary(0x45e4b1u, 0x456b20u,
                              (void *)(size_t)rd32(c + 0x00), av1, 1, wb, 0, 0);
#if PATH_REPLAY
                y = rdf((unsigned char *)c + 0x14) + rdf(&wa[1]);
                x = rdf((unsigned char *)c + 0x0c) + rdf(&wb[0]);
#else
                /* живой билд: out-пара лежит в out-слотах, заполненных вызовом */
                y = rdf((unsigned char *)c + 0x14) + rdf(&b20[1]);
                x = rdf((unsigned char *)c + 0x0c) + rdf(&b18[0]);
#endif
                out0 = (int)fb(x);
                out1 = 0;
                out2 = (int)fb(y);
            }
        }
    }

l_p11:
    /* ---- P11 (45e4e3..45e504): out-тройка в param_1 ---- */
    wr32((unsigned char *)arg1 + 0x0, (unsigned)out0);
    wr32((unsigned char *)arg1 + 0x4, (unsigned)out1);
    wr32((unsigned char *)arg1 + 0x8, (unsigned)out2);

    /* pf+0x70 — размер map1. Порт пишет СВОЙ счётчик успешных регистраций
     * (nreg: столько раз он подал PathState в 45dd00). Родная RB-вставка растит
     * размер ещё и от вставок ВНУТРИ реплеемого 45dd70 (dd00 зовётся из его
     * тела, в окне листа их нет) — расхождение с записанным post на таких
     * вызовах считает path_post_match, а не копирование post. */
    wr32(pf + 0x70, (unsigned)nreg);

#if PATH_REPLAY
    {
        int av[2], w3[3];
        av[0] = (int)(size_t)arg1;      /* pos-слот caller'а */
        av[1] = (int)(size_t)arg2;      /* grid-record */
        w3[0] = out0; w3[1] = out1; w3[2] = out2;
        B0_LEAF_CLAIM_W(B0_LEAF_PATH_RECOMPUTE, pf_raw, (int)(size_t)arg1, av, 2,
                        w3, 3);
    }
#endif
    return 0;
}
