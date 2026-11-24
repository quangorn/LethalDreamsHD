/* tools/probes/bdd0/native_replay.cpp — replay-слой логических листьев тика.
 *
 * Шесть модельных листьев тела FUN_0044bdd0 (45c730, 45b150, 45b3e0, 458050,
 * 45e1d0, 44bbf0) не исполняются офлайн: каждый
 * запрос «снимает» следующую запись потока bdd0_leaf ЭТОГО seq и отдаёт её
 * ret. Поток строгий: другой va или исчерпание — нарушение, которое ловит
 * проверка `leaf_seq` (тик при этом доигрывается на модельных значениях, чтобы
 * отчёт дошёл до конца, а не оборвался).
 *
 * Указатели: ret листа — адрес в пространстве игры. Он переводится в
 * shadow-адрес арены (arena_resolve_ptr: привязки снимка, иначе opaque-объект).
 * Так `grid_anchor → record` и т.п. не разыменовывают
 * игровую память.
 *
 * Портированные листья (battle/ported.h, b0_ported_leaf_va) в потоке НЕ
 * запрашиваются: их запись снимает claim (`bdd0_leaf_claim_w`) — порт считает
 * значение сам и сообщает его, поток продвигается, значение сверяет проверка
 * `leaf_value`. Запрос портированного листа — нарушение потока (leaf_seq).
 *
 * Побочные записи листа (dword'ы, которые натив писал ЧЕРЕЗ указатель) берутся
 * из необязательного ключа `w` записи; если его нет — из документированной
 * семантики листа (45c6f0 = три dword `[ctx+0xc]`), см. native.h. Портированный
 * 45c6f0 читает эту тройку из арены ("ctx_pos"): с capture v15 её содержимое —
 * независимый снимок `[*(unit+0x138)+0xc]` (bdd0_in "ctx_pos"), и claim
 * сверяется как обычное значение; у файлов без ключа тройку восстанавливает
 * bdd0_leaf_begin из `w` первой записи 45c6f0 тика (куча ctx в снимок не
 * попадает), и тогда claim помечается resync — leaf_value считает поля w
 * несравнимыми (B0_POS_RESYNC_WHY), а не PASS.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "../../ldhook/battle/ported.h"
#include "harness.h"

#define LEAF_LOG_MAX 512

static int fbits(float f)
{
    int b;
    memcpy(&b, &f, 4);
    return b;
}

static const LeafRec *g_v = NULL;
static int g_n = 0;
static int g_i = 0;             /* потреблено записей */
static int g_seq = 0;
static int g_viol = 0;
static char g_why[192];
static int g_last_leaf_order = -1;
static int g_call_seq = 0;
static CallLogEnt g_log[LEAF_LOG_MAX];
static int g_log_n = 0;
/* Заявки портированных листьев (battle/ported.h): значение считает порт, запись
 * потока снимает claim. Своя нумерация (order) — журнал заявок не смешивается
 * с журналом запросов, но поток у них общий (g_i). */
#define LEAF_CLAIM_MAX 128
static LeafClaimEnt g_claim[LEAF_CLAIM_MAX];
static int g_claim_n = 0;
/* Сработавшие mutator'ы (battle/ported.h, b0_leaf_mutators): натив отработал —
 * вход портированного листа мог измениться, заявка перестаёт быть сверяемой. */
static int g_mut_hit[B0_LEAF_MUTATOR_COUNT];
/* 1 — после-состояние, меняемое mutator'ом, восстановлено из потока
 * (band_resync_from_stream): вход листа известен, но сверка головы с длиной из
 * bdd0_leaf была бы тавтологией — claim помечается resync и leaf_value → SKIP. */
static int g_band_resynced = 0;
/* v13: 1 — после-состояние ленты (begin,end) применено из bdd0_res ("band"):
 * вход листа известен и НЕ является тавтологией — claim головы сверяется
 * (mutator'ы 45eee0 больше не форсируют SKIP). */
static int g_band_applied = 0;
/* v15: тик, в котором запись bdd0_native для 0x45eee0 отсутствует и была
 * восстановлена из bdd0_res (см. B0_EEE0_SYNTH_WHY). 1 — u170 подан в арену
 * из записи исхода, поэтому res_match не считает его независимым. */
static int g_eee0_synth = 0;

/* Мутация «состоялась» (см. b0_leaf_mutators). Для 45eee0 это так и после
 * восстановления ленты: либо вход неизвестен, либо он реконструирован из
 * потока (тавтология) — claim в обоих случаях не сверяем. */
static int mut_active(int k)
{
    return g_mut_hit[k] ? 1 : 0;
}
/* Параллельно журналу: запись, с которой сопоставлен вызов (для проверки
 * `leaf_args`), и dword'ы, которые обёртка реально записала ЧЕРЕЗ указатель
 * (их сверяют с `w` записи; сами по себе из записи не берутся). */
static const LeafRec *g_match[LEAF_LOG_MAX];
static int g_aw_nw[LEAF_LOG_MAX];
static int g_aw[LEAF_LOG_MAX][3];

/* 45c6f0: троица-результат лежит в куче игры (`[*(unit+0x138)+0xc]`). Арена
 * даёт под неё область "ctx_pos" (arena.cpp: ctx+0xc → эта область). Источник
 * содержимого:
 *   - capture v15 (`bdd0_in` "ctx_pos") — НЕЗАВИСИМЫЙ снимок цепочки
 *     `[*(unit+0x138)+0xc]`, его уже положил arena_build; заявка 45c6f0 тогда
 *     сверяется как обычное значение (resync НЕ ставится);
 *   - старые файлы без ключа — реконструкция из `w` первой записи 45c6f0 этого
 *     тика (позиция одна на тик: в захватах при двух вызовах w совпадают);
 *     зеркально claim помечается resync, и w идёт в «несравнимо», а не в PASS.
 * Записи без `w` (совсем старый формат) оставляют нули — leaf_value SKIP. */
static int g_ctx_pos_captured = 0;
/* 458050: последний тик потребил запись потока (native::grid_anchor) — порт
 * не исполнялся. Сбрасывается в bdd0_leaf_begin, читает сводка. */
static int g_grid_anchor_replayed = 0;
/* 45b150: то же для float-гейта ленты (порт не исполнялся — геометрии ленты в
 * захвате нет: tick.cpp b0_band_interp_gate + bdd0_band_geometry_modelled). */
static int g_band_gate_replayed = 0;

static void ctx_pos_from_stream(void)
{
    unsigned char *pos = arena_region_w(arena_current(), "ctx_pos");
    int i;

    if (pos == NULL || g_ctx_pos_captured) return;
    for (i = 0; i < g_n; i++) {
        if (g_v[i].va != (int)B0_LEAF_READ_POS || g_v[i].trap) continue;
        if (g_v[i].nw < 3) continue;
        memcpy(pos, g_v[i].w, 12);
        return;
    }
}

void bdd0_leaf_begin(int seq, const LeafRec *v, int n)
{
    const TickRecord *tr = arena_record(arena_current());

    g_v = v; g_n = n; g_i = 0; g_seq = seq;
    g_viol = 0; g_why[0] = '\0';
    g_last_leaf_order = -1;
    g_call_seq = 0;
    g_grid_anchor_replayed = 0;
    g_band_gate_replayed = 0;
    g_log_n = 0;
    g_claim_n = 0;
    for (int k = 0; k < LEAF_LOG_MAX; k++) {
        g_match[k] = NULL;
        g_aw_nw[k] = 0;
        g_aw[k][0] = g_aw[k][1] = g_aw[k][2] = 0;
    }
    /* v15: тик несёт независимую тройку ctx_pos — реконструкция из потока не
     * нужна и была бы тавтологией. */
    g_ctx_pos_captured = (tr != NULL && tr->has_ctx_pos) ? 1 : 0;
    ctx_pos_from_stream();
}

int bdd0_leaf_calls(void) { return g_log_n; }
int bdd0_leaf_violations(void) { return g_viol; }
/* 1, если тик потребил запись потока 0x458050 (реплей вместо порта). */
int bdd0_grid_anchor_replayed(void) { return g_grid_anchor_replayed; }
/* 45b150: тик потребил запись потока (порт не исполнялся — геометрии ленты в
 * захвате нет; tick.cpp b0_band_interp_gate). Сбрасывается в bdd0_leaf_begin. */
int bdd0_band_gate_replayed(void) { return g_band_gate_replayed; }
const char *bdd0_leaf_violation_why(void) { return g_why; }

const CallLogEnt *bdd0_calllog(int *n)
{
    if (n != NULL) *n = g_log_n;
    return g_log;
}

/* Доступ к сопоставленной записи последнего вызова (проверка `leaf_args`):
 * аргументы порта живут в CallLogEnt, аргументы натива — в этой записи.
 * Объявлено не в harness.h: контракт ядра не меняется. */
const LeafRec *bdd0_leaf_record_at(int i)
{
    if (i < 0 || i >= g_log_n) return NULL;
    return g_match[i];
}

/* Сколько записей потока РЕАЛЬНО потреблено (сопоставлено) за тик и первая
 * непотреблённая запись — для `leaf_seq`: тик обязан израсходовать весь
 * поток, а не только те вызовы, которые тело успело сделать. */
int bdd0_leaf_consumed(void)
{
    return g_i;
}

const LeafRec *bdd0_leaf_pending(void)
{
    return (g_i >= 0 && g_i < g_n) ? &g_v[g_i] : NULL;
}

/* Сколько dword'ов обёртка записала через указатель и какие (== источник для
 * `w` записи). 0 — лист ничего не писал: «нет» не подменяется нулями. */
int bdd0_leaf_applied_w(int i, int *w3)
{
    if (i < 0 || i >= g_log_n) return 0;
    if (w3 != NULL) {
        for (int k = 0; k < 3; k++) w3[k] = g_aw[i][k];
    }
    return g_aw_nw[i];
}

/* Снять dword'ы, реально записанные в буфер вызывающего (out-тройка). */
static void note_applied_w(const void *buf, int nw)
{
    int i = g_log_n - 1;
    if (i < 0 || i >= LEAF_LOG_MAX || buf == NULL) return;
    if (nw > 3) nw = 3;
    g_aw_nw[i] = nw;
    if (nw > 0) memcpy(g_aw[i], buf, (size_t)nw * 4);
}

void bdd0_calllog_reset(void)
{
    g_log_n = 0;
    g_viol = 0;
    g_why[0] = '\0';
}

static void viol(const char *fmt, ...)
{
    if (g_viol) return;                    /* первое нарушение информативно */
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g_why, sizeof(g_why), fmt, ap);
    va_end(ap);
    g_why[sizeof(g_why) - 1] = '\0';
    g_viol = 1;
}

/* Сопоставление запроса/заявки со следующей записью потока: trap, va, порядок,
 * исчерпание. Возврат NULL — нарушение потока (g_viol), его судит leaf_seq;
 * «не NULL» — запись отдана (поток продвинут). */
static const LeafRec *match_next(unsigned va, const char *what, int idx)
{
    const LeafRec *r;

    if (g_viol) return NULL;
    if (g_i >= g_n) {
        viol("%s #%d: leaf stream exhausted, requested va=0x%x, %d records "
             "for seq %d", what, idx, va, g_n, g_seq);
        return NULL;
    }
    r = &g_v[g_i];
    if (r->trap) {
        /* запись снята после сбоя/рассинхрона: ret/args/w в ней недостоверны.
         * Не потребляем её и не отдаём значения — поток разошёлся. */
        viol("%s #%d: capture record trap=1 va=0x%x (seq %d): unreliable",
             what, idx, r->va, g_seq);
        return NULL;
    }
    if (r->va != (int)va) {
        viol("%s #%d: requested va=0x%x, recorded va=0x%x (seq %d)",
             what, idx, va, r->va, g_seq);
        return NULL;
    }
    if (r->order != g_i) {
        /* старый поток: order = индекс листа; новый: общий order с
         * bdd0_native, поэтому проверяется только возрастание. */
        if (r->order <= g_last_leaf_order) {
            viol("%s #%d: order %d after %d (seq %d)", what, idx, r->order,
                 g_last_leaf_order, g_seq);
            return NULL;
        }
    }
    g_i++;
    g_last_leaf_order = r->order;
    return r;
}

/* Запрос листа: журнал + строгое сопоставление с потоком. NULL — нарушения
 * нет и запись отдана; иначе NULL-запись (вызывающий берёт модель). */
static const LeafRec *request(int va, const void *ecx, int nargs, const int *args)
{
    int idx = g_log_n;
    const LeafRec *r;

    if (g_log_n < LEAF_LOG_MAX) {
        CallLogEnt &e = g_log[g_log_n];
        e.order = g_log_n;
        e.call_seq = g_call_seq++;
        e.va = va;
        e.ecx = (int)(size_t)ecx;
        e.nargs = nargs;
        for (int k = 0; k < 6; k++) e.args[k] = (nargs > k) ? args[k] : 0;
        e.ret = 0;
#ifdef B0_TRACE_LABELS
        e.last_label = (int)b0_last_label;
#else
        e.last_label = -1;
#endif
        g_match[g_log_n] = NULL;
        g_aw_nw[g_log_n] = 0;
        g_log_n++;
    } else {
        viol("leaf call log overflow at call %d (va=0x%x)", g_log_n, va);
    }
    if (g_viol) return NULL;
    if (b0_leaf_ported((unsigned)va)) {
        /* Тело листа портировано: порт обязан сообщить значение claim'ом
         * (проверка leaf_value), иначе запись потока никто не сверяет. */
        viol("leaf call #%d: va=0x%x is ported, the port must claim it "
             "(seq %d)", idx, va, g_seq);
        return NULL;
    }
    r = match_next((unsigned)va, "leaf call", idx);
    if (r != NULL && g_log_n > 0) {
        g_log[g_log_n - 1].ret = r->ret;
        g_match[g_log_n - 1] = r;
    }
    return r;
}

/* Заявленная тройка — ровно та, что лежит в реконструкции "ctx_pos": сравнение
 * с `w` записи было бы тавтологией (см. battle/ported.h b0_leaf_w_recon).
 * Только для файлов без v15-ключа "ctx_pos": когда тройка снята независимо из
 * игры, совпадение с ней заявки НЕ делает сверку тавтологией. */
static int claim_w_is_recon(const LeafClaimEnt &e)
{
    unsigned char *pos = arena_region_w(arena_current(), "ctx_pos");

    if (g_ctx_pos_captured || pos == NULL || e.nw != 3) return 0;
    return memcmp(pos, e.w, 12) == 0;
}

/* Заявка портированного листа (battle/ported.h, b0_ported_leaf_va): журнал
 * заявок + сопоставление с потоком. Запись потока ПОТРЕБЛЯЕТСЯ (leaf_seq не
 * ждёт запроса, leaf_args не видит лист), значение сверяет leaf_value.
 * w/nw — dword'ы, записанные листом ЧЕРЕЗ указатель (out-тройка 45c6f0);
 * сверяются с `w` записи (нет `w` — leaf_value SKIP, не PASS). */
void bdd0_leaf_claim_w(unsigned int va, void *ecx, int ret, const int *args,
                       int nargs, const int *w, int nw)
{
    int idx = g_claim_n;
    int appended = 0;
    const LeafRec *r;

    /* Канал несёт не более трёх dword'ов (LeafRec.w[3]). Больше — нарушение
     * потока, а не тихое усечение: иначе лишняя запись порта прошла бы через
     * проверку счётчика. */
    if (nw > 3) {
        viol("leaf claim #%d: va=0x%x wrote %d dwords through the pointer "
             "(the claim channel carries at most 3)", idx, va, nw);
        return;
    }
    if (g_claim_n < LEAF_CLAIM_MAX) {
        LeafClaimEnt &e = g_claim[g_claim_n];
        int k;

        e.order = g_claim_n;
        e.va = (int)va;
        e.ecx = (int)(size_t)ecx;
        e.ret = ret;
        e.call_seq = g_call_seq++;
        e.nargs = (nargs > 0 && nargs <= 6) ? nargs : 0;
        for (k = 0; k < 6; k++) {
            e.args[k] = (args != NULL && k < e.nargs) ? args[k] : 0;
        }
        e.nw = (w != NULL && nw > 0) ? nw : 0;
        for (k = 0; k < 3; k++) {
            e.w[k] = (k < e.nw) ? w[k] : 0;
        }
        e.rec = NULL;
        e.mut = 0;
        e.resync = 0;
        /* вход листа менялся нативом внутри тика → значение не сверяемо.
         * v13: если после-состояние ленты применено из bdd0_res ("band"),
         * вход листа известен и сверка НЕ тавтология — claim сравнивается
         * (mutator'ы 45eee0 не форсируют SKIP). */
        for (k = 0; k < B0_LEAF_MUTATOR_COUNT; k++) {
            if (b0_leaf_mutators[k].leaf == va && mut_active(k) && !g_band_applied) {
                e.mut = k + 1;
                e.resync = (b0_leaf_mutators[k].mutator == B0_VA_UNIT_AI_TICK) &&
                           g_band_resynced;
                break;
            }
        }
        g_claim_n++;
        appended = 1;
    } else {
        viol("leaf claim log overflow at claim %d (va=0x%x)", g_claim_n, va);
    }
    if (!b0_leaf_ported(va)) {
        viol("leaf claim #%d: va=0x%x is not a ported leaf (seq %d)", idx, va,
             g_seq);
        return;
    }
    r = match_next(va, "leaf claim", idx);
    /* запись — только реально добавленной заявке (иначе перезапишем чужую) */
    if (r != NULL && appended) {
        g_claim[idx].rec = r;
        /* out-записи листьев из b0_leaf_w_recon: у файлов БЕЗ v15 "ctx_pos"
         * заявленная тройка — ровно реконструкция из потока ("ctx_pos"),
         * сравнивать её с записью бессмысленно (тавтология). Заявка
         * помечается resync, и leaf_value считает поля w несравнимыми
         * (B0_POS_RESYNC_WHY), а не pass. С независимым снимком ctx_pos
         * (v15) тройка пришла из игры — resync не ставится, w сверяется
         * побитово как обычное значение. Тройка, отличная от реконструкции,
         * сверяется побитово и в старом формате. */
        if (b0_leaf_w_recon_why(va) != NULL && claim_w_is_recon(g_claim[idx])) {
            g_claim[idx].resync = 1;
        }
    }
}

int bdd0_leaf_claims(void)
{
    return g_claim_n;
}

const LeafClaimEnt *bdd0_leaf_claim_at(int i)
{
    if (i < 0 || i >= g_claim_n) return NULL;
    return &g_claim[i];
}

static unsigned read_u32(const void *p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

/* ------------------------- v12: поток bdd0_native ------------------------ *
 * Не-листовые RECORD-нативы (game_state_get, player_get, phase_time, ...).
 * Записи потребляются в порядке order для своего seq; ключ — target VA для
 * прямых сайтов, slot id для косвенных (va там — фактическая цель и от
 * машины к машине различается).  Если записей этого seq нет (старый захват),
 * слой НЕ потребляет ничего и отдаёт модельные значения — проверка
 * `native_seq` тогда SKIP, а не fail. */
#define NATIVE_LOG_MAX 768
static const NativeRec *g_nv = NULL;
static int g_nn = 0;
static int g_ni = 0;
static int g_nlast = -1;
static int g_nviol = 0;
static char g_nwhy[192];
static int g_not_present = 1;
static NativeCallLogEnt g_nlog[NATIVE_LOG_MAX];
static const NativeRec *g_nmatch[NATIVE_LOG_MAX];
static int g_nlog_n = 0;
/* v13: запись текущего тика (recorded-band: bdd0_res "band":[begin,end]). */
static const TickRecord *g_rec = NULL;

void bdd0_native_begin(int seq, const NativeRec *v, int n,
                       const TickRecord *rec)
{
    (void)seq;
    g_rec = rec;
    g_nv = v;
    g_nn = n;
    g_ni = 0;
    g_nlast = -1;
    g_nviol = 0;
    g_nwhy[0] = '\0';
    g_nlog_n = 0;
    for (int k = 0; k < B0_LEAF_MUTATOR_COUNT; k++) g_mut_hit[k] = 0;
    g_band_resynced = 0;
    g_band_applied = 0;
    g_eee0_synth = 0;
    g_call_seq = 0;
    for (int k = 0; k < NATIVE_LOG_MAX; k++) g_nmatch[k] = NULL;
    g_not_present = (n == 0);
}

int bdd0_native_calls(void) { return g_nlog_n; }
int bdd0_native_violations(void) { return g_nviol; }
const char *bdd0_native_violation_why(void) { return g_nwhy; }
int bdd0_native_consumed(void) { return g_ni; }
const NativeCallLogEnt *bdd0_native_calllog(int *n)
{
    if (n != NULL) *n = g_nlog_n;
    return g_nlog;
}
const NativeRec *bdd0_native_record_at(int i)
{
    if (i < 0 || i >= g_nlog_n) return NULL;
    return g_nmatch[i];
}
const NativeRec *bdd0_native_pending(void)
{
    return (g_ni >= 0 && g_ni < g_nn) ? &g_nv[g_ni] : NULL;
}

static void nviol(const char *fmt, ...)
{
    if (g_nviol) return;
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g_nwhy, sizeof(g_nwhy), fmt, ap);
    va_end(ap);
    g_nwhy[sizeof(g_nwhy) - 1] = '\0';
    g_nviol = 1;
}

/* NULL — нет записи (не-present режим или уже нарушение).  ecx/args — то,
 * что вычислил порт; журнал нужен проверке native_args. */
static const NativeRec *nrequest(int slot, int va, int ecx, const int *args,
                                 int nargs)
{
    const NativeRec *r;
    NativeCallLogEnt *le = NULL;
    int i;

    if (g_nlog_n < NATIVE_LOG_MAX) {
        le = &g_nlog[g_nlog_n];
        le->order = g_nlog_n;
        le->call_seq = g_call_seq++;
        le->site = 0;
        le->va = va;
        le->slot = slot;
        le->ecx = ecx;
        le->nargs = (nargs > 0 && nargs <= 8) ? nargs : 0;
        for (i = 0; i < 8; i++) {
            le->args[i] = (args != NULL && i < le->nargs) ? args[i] : 0;
        }
        g_nmatch[g_nlog_n] = NULL;
#ifdef B0_TRACE_LABELS
        le->last_label = (int)b0_last_label;
#else
        le->last_label = -1;
#endif
        g_nlog_n++;
    }
    if (g_not_present) return NULL;
    if (g_nviol) return NULL;
    if (g_ni >= g_nn) {
        nviol("native stream exhausted at call %d: requested %s0x%x, "
              "%d records", g_ni, slot ? "slot " : "va ",
              slot ? slot : va, g_nn);
        return NULL;
    }
    r = &g_nv[g_ni];
    if (r->trap) {
        nviol("native #%d trap=1 site=0x%x: capture record is unreliable",
              g_ni, r->site);
        return NULL;
    }
    if (r->slot != slot) {
        nviol("native #%d: requested slot %d, recorded slot %d (site 0x%x)",
              g_ni, slot, r->slot, r->site);
        return NULL;
    }
    if (slot == 0 && r->va != va) {
        nviol("native #%d: requested va=0x%x, recorded va=0x%x (site 0x%x)",
              g_ni, va, r->va, r->site);
        return NULL;
    }
    if (r->order <= g_nlast) {
        nviol("native #%d: order %d after %d (site 0x%x)", g_ni, r->order,
              g_nlast, r->site);
        return NULL;
    }
    g_ni++;
    g_nlast = r->order;
    /* натив отработал: входы связанных портированных листьев могли измениться */
    for (i = 0; i < B0_LEAF_MUTATOR_COUNT; i++) {
        if (b0_leaf_mutators[i].mutator == (unsigned)r->va) g_mut_hit[i] = 1;
    }
    if (le != NULL) {
        le->site = r->site;
        g_nmatch[le->order] = r;
    }
    return r;
}

static void *shadow_region(const char *name)
{
    return arena_region_w(arena_current(), name);
}

static float f32_of(int bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

/* Возврат листа → shadow-адрес (игровые указатели не утекают в тело).
 * Сайт и VA взяты из записи: пары из k_identity_rets привязываются по
 * идентичности (неразыменовываемый сентинел, handles pass). */
static void *ret_ptr(const LeafRec *r)
{
    if (r == NULL || r->ret == 0) return NULL;
    return arena_resolve_ptr_at((unsigned)r->ret,
                                (unsigned)r->site, (unsigned)r->va);
}

/* Записи bdd0_native для 0x45eee0 нет, хотя поток тика присутствует: захват
 * снят с LDHOOK_BATTLE_CAPTURE_E0=0 (точка записи юнит-AI тика жила в
 * e0a0-слое — исправлено в capture.cpp, v15). Восстанавливать её из одной
 * bdd0_res можно только там, где исход тика записан и не помечен trap.
 * Строгие условия (иначе — обычное нарушение потока, а не подстановка):
 *   - записей 0x45eee0 в потоке этого тика НЕТ ВООБЩЕ (в норме она одна: в
 *     cap_v14_trim 392 записи на 400 тиков, ни одного тика с двумя);
 *   - подстановка — не более одной на тик;
 *   - у тика есть bdd0_res с не-trap исходом.
 * Возврат 1 — на этой позиции потока записи 0x45eee0 нет. */
static int eee0_record_missing(void)
{
    const NativeRec *p;
    int i;

    if (g_not_present || g_nv == NULL || g_nviol) return 0;
    if (g_rec == NULL || !g_rec->has_res || g_rec->res_trap) return 0;
    if (g_eee0_synth) return 0;                 /* одна подстановка на тик */
    for (i = 0; i < g_nn; i++) {
        if (g_nv[i].slot == 0 && g_nv[i].va == (int)B0_VA_UNIT_AI_TICK) {
            return 0;                           /* запись в потоке есть */
        }
    }
    p = bdd0_native_pending();
    (void)p;
    return 1;                                   /* записи нет вовсе */
}

int bdd0_eee0_synth(void)
{
    return g_eee0_synth;
}

/* ------------------------------- листья --------------------------------- */
namespace native {
/* 45c730 — курсор ленты: [[[ctx]+0xc]+0xc]+0x234 с NULL-guard'ами. */
void *band_cursor(void *ai)
{
    const LeafRec *r = request(0x45c730, ai, 0, NULL);
    if (r != NULL) return ret_ptr(r);
    if (ai == NULL || !arena_addr_ok(ai, 4)) return NULL;
    unsigned p = read_u32(ai);
    if (p == 0) return NULL;
    void *s1 = arena_resolve_ptr(p);
    if (s1 == NULL || !arena_addr_ok((const unsigned char *)s1 + 0xc, 4)) return NULL;
    unsigned p2 = read_u32((const unsigned char *)s1 + 0xc);
    if (p2 == 0) return NULL;
    void *s2 = arena_resolve_ptr(p2);
    if (s2 == NULL || !arena_addr_ok((const unsigned char *)s2 + 0xc, 4)) return NULL;
    unsigned p3 = read_u32((const unsigned char *)s2 + 0xc);
    if (p3 == 0) return NULL;
    void *s3 = arena_resolve_ptr(p3);
    if (s3 == NULL || !arena_addr_ok((const unsigned char *)s3 + 0x234, 4)) return NULL;
    unsigned p4 = read_u32((const unsigned char *)s3 + 0x234);
    return (p4 == 0) ? NULL : arena_resolve_ptr(p4);
}

/* 45b150 — float-гейт ленты; результат ST(0) → *out_f. */
void band_interp_gate(void *band, float p2, int p3, float p4, float *out_f)
{
    int a[3];
    a[0] = fbits(p2);
    a[1] = p3;
    a[2] = fbits(p4);
    const LeafRec *r = request(0x45b150, band, 3, a);
    if (r != NULL) g_band_gate_replayed = 1;  /* порт не исполнялся */
    if (out_f == NULL) return;
    if (r != NULL) {
        memcpy(out_f, &r->ret, 4);           /* ret = биты ST(0) */
    } else {
        *out_f = 0.0f;
    }
}

/* 45b3e0 — предикат ленты «цель сдвинулась/удалена», AL. */
int band_target_moved(void *band, float p1, int p2, float p3)
{
    int a[3];
    a[0] = fbits(p1);
    a[1] = p2;
    a[2] = fbits(p3);
    const LeafRec *r = request(0x45b3e0, band, 3, a);
    if (r != NULL) return r->ret;
    return 0;
}

/* 458050 — grid-якорь: arg5 — байт-флаг, и он НЕ пишется. Натив пишет его
 * только в ветке node != 0: 1 @0x4580fd (tree+key), 0 в двух fallback'ах
 * @0x458161; при node == 0 (0x4580d1 je 0x45817f) байт не трогается. Ветку из
 * записи не восстановить: сайт 0x44c7da — LS_K_INT, листовая запись листа
 * 0x458050 не содержит out-записи `w` этого байта; в frame_b64 по смещению 0x3f
 * лежит начальное значение кадра (захват снимает кадр при входе), не поствызовное,
 * и по нему ветку не восстановить. Тик флаг не читает (tick.cpp только передаёт
 * &frame+0x3f листу). Писать `(res != NULL)` = фабрикация 1 там, где натив писал
 * 0 (fallback возвращает не-NULL ячейку), поэтому байт оставляем как есть. */
void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    int a[6];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    a[4] = (int)(size_t)flag_slot;
    a[5] = (int)(size_t)world;
    const LeafRec *r = request(0x458050, grid, 6, a);
    if (r != NULL) g_grid_anchor_replayed = 1;   /* порт не исполнялся */
    return ret_ptr(r);
}

/* 45e1d0 — grid-recompute: 3 dword возвращённой тройки копируются в out3. */
void grid_recompute(void *rec_pf, void *pos, void *req, float out3[3])
{
    int a[2];
    a[0] = (int)(size_t)pos;
    a[1] = (int)(size_t)req;
    const LeafRec *r = request(0x45e1d0, rec_pf, 2, a);
    if (out3 == NULL) return;
    if (r != NULL && r->nw == 3) {
        memcpy(out3, r->w, 12);
    } else {
        out3[0] = out3[1] = out3[2] = 0.0f;
    }
    note_applied_w(out3, 3);
}

/* 44bbf0 — grid_query: возврат — int аккумулятора. */
int grid_query(void *world, int a1, int a2, int a3, int a4)
{
    int a[4];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    const LeafRec *r = request(0x44bbf0, world, 4, a);
    if (r != NULL) return r->ret;
    return 0;
}

/* ====================== v12: не-листовые RECORD-нативы =================== *
 * Возврат/побочные значения берутся из потока bdd0_native.  Если потока нет
 * (старый захват), поведение совпадает с прежними no-op обёртками: gs/player —
 * shadow-области арены, phase=0.0f, остальные — 0/void.  Это видно в сводке
 * (`native_seq` SKIP). */

/* 442570 — состояние игры: возврат — shadow-объект "gs" (содержимое из
 * bdd0_in gs_b64).  Записанный non-NULL факт отличает "объекта нет". */
void *game_state_get(void *unit, int flags)
{
    int a[1];
    a[0] = flags;
    const NativeRec *r = nrequest(0, 0x442570, (int)(size_t)unit, a, 1);
    if (r == NULL) return shadow_region("gs");
    if (r->ret == 0) return NULL;
    return shadow_region("gs");
}

/* 439ae0 — сравнение string-объектов; тело читает только AL. */
int string_obj_cmp(void *a, void *b)
{
    int av[2];
    av[0] = (int)(size_t)a;
    av[1] = (int)(size_t)b;
    const NativeRec *r = nrequest(0, 0x439ae0, 0, av, 2);
    if (r == NULL) return 0;
    return r->ret & 0xff;
}

/* 42da60 — player-флаги: возврат — объект; тело передаёт его в 435d50. */
void *player_flag_get(void *player, int flag)
{
    int a[1];
    a[0] = flag;
    const NativeRec *r = nrequest(0, 0x42da60, (int)(size_t)player, a, 1);
    if (r == NULL) return player;                  /* прежний no-op */
    if (r->ret == 0) return NULL;
    return shadow_region("flags");
}

/* 435d50 — флаги игрока: возвращаемые байты тело тестирует (&2, &4), поэтому
 * записанный w[0] кладётся в shadow-объект перед возвратом. */
void *player_flags(void *flags_obj)
{
    const NativeRec *r = nrequest(0, 0x435d50, (int)(size_t)flags_obj, NULL, 0);
    if (r == NULL) return flags_obj;               /* прежний no-op */
    void *sh = shadow_region("flags");
    if (sh != NULL && r->nw > 0) {
        memcpy(sh, r->w, 4);
    }
    return (r->ret == 0) ? NULL : sh;
}

/* 448c30 — cursor/mode update: void, сторона не читается телом. */
void cursor_mode_update(void *unit, int arg)
{
    int a[1];
    a[0] = arg;
    (void)nrequest(0, 0x448c30, (int)(size_t)unit, a, 1);
    (void)unit;
}

/* 446a80 — message timer: void; пишет unit+0x54, тело его не читает. */
void ai_time_update(void *unit, const void *msg60)
{
    int a[1];
    a[0] = (int)(size_t)msg60;
    (void)nrequest(0, 0x446a80, (int)(size_t)unit, a, 1);
    (void)unit;
}

/* 448ee0 — state flag: пишет unit+0x114 (1.0f при флаге и нуле), а входной
 * гейт следующего вызова это читает.  Побочное значение известно из args. */
void state_flag_log(void *unit, int flag)
{
    int a[1];
    a[0] = flag;
    const NativeRec *r = nrequest(0, 0x448ee0, (int)(size_t)unit, a, 1);
    if (r != NULL && unit != NULL && (flag & 0xff) != 0) {
        unsigned char *p = (unsigned char *)unit + 0x114;
        if (arena_addr_ok(p, 4)) {
            float cur;
            memcpy(&cur, p, 4);
            if (cur == 0.0f) {
                float one = 1.0f;
                memcpy(p, &one, 4);
            }
        }
    }
}

/* 42d9c0 — player pos store: void; пишет позицию игрока в [arg0..+8].
 * v13: записанные значения (`ww` с ключами -1..-3) replay-ются в out-буфер
 * порта, иначе grid_query @0x44c247/0x44c276 получает нули. */
void player_pos_set(void *player, void *triple)
{
    int a[1];
    int k;
    a[0] = (int)(size_t)triple;
    const NativeRec *r = nrequest(0, 0x42d9c0, (int)(size_t)player, a, 1);
    (void)player;
    if (r != NULL && triple != NULL) {
        for (k = 0; k + 1 < r->nww; k += 2) {
            int key = r->ww[k];
            if (key < 0 && key >= -8) {
                int v = r->ww[k + 1];
                memcpy((unsigned char *)triple + 4 * (-key - 1), &v, 4);
            }
        }
    }
}

/* 45f430 — PathFinder ctor: void; объект кадра потребляет 45e1d0 (replay). */
void pathfinder_init(void *pf)
{
    (void)nrequest(0, 0x45f430, (int)(size_t)pf, NULL, 0);
    (void)pf;
}

/* Лента BattleAI+0x3c: для захватов без записанного после-состояния ленты
 * (bdd0_res "band") длина восстанавливается из следующей записи 45af50 того же
 * seq — её `ret` есть (end-begin)/0x18 после перестройки 45eee0, откуда
 * end = begin + ret*0x18. Реконструкция нужна, чтобы порт прошёл тот же путь
 * листовых вызовов: иначе на B144 (0x44cfec) head==NULL обрывает тик и leaf_seq
 * видит «port made fewer calls». Claims таких тиков помечаются resync и
 * leaf_value даёт SKIP (B0_BAND_RESYNC_WHY), поэтому тавтологического PASS не
 * фиксируется. Возврат 1 — состояние восстановлено. */
static int band_resync_from_stream(void)
{
    unsigned char *unit = arena_region_w(arena_current(), "unit");
    unsigned char *band;
    unsigned begin;
    int i;

    if (unit == NULL) return 0;
    begin = (unsigned)(size_t)arena_region_w(arena_current(), "band");
    if (begin == 0) return 0;
    band = unit + BDD0_AI_OFF + B0_BATTLE_AI_BAND;
    for (i = g_i; i < g_n; i++) {
        unsigned end;

        if (g_v[i].va != (int)B0_LEAF_BAND_COUNT || g_v[i].trap) continue;
        end = begin + (unsigned)(g_v[i].ret * (int)BDD0_BAND_ELEM);
        memcpy(band + B0_BAND_BEGIN_OFF, &begin, sizeof(begin)); /* [band+BEGIN] */
        memcpy(band + B0_BAND_END_OFF, &end, sizeof(end));       /* [band+END]   */
        return 1;
    }
    return 0;
}

/* v13: после-состояние ленты (begin,end) записано в bdd0_res ("band").
 * Реплей пишет begin/end в shadow unit+0x138+0x3c+0x20/+0x24 (та же область,
 * что unit-снимок: BattleAI встроен), и claim головы становится сверяемым —
 * band_resync (реконструкция длины из потока) выключается. Возврат 1 —
 * состояние применено; 0 — поля нет (старый захват), работает resync. */
static int band_apply_recorded(void)
{
    unsigned char *unit = arena_region_w(arena_current(), "unit");
    unsigned begin, end;

    if (unit == NULL || g_rec == NULL || !g_rec->has_res_band) return 0;
    if (!arena_addr_ok(unit + BDD0_AI_OFF + B0_BATTLE_AI_BAND + B0_BAND_BEGIN_OFF, 8)) return 0;
    begin = g_rec->band_post_begin;
    end = g_rec->band_post_end;
    memcpy(unit + BDD0_AI_OFF + B0_BATTLE_AI_BAND + B0_BAND_BEGIN_OFF, &begin, sizeof(begin));
    memcpy(unit + BDD0_AI_OFF + B0_BATTLE_AI_BAND + B0_BAND_END_OFF, &end, sizeof(end));
    return 1;
}

/* 45eee0 — unit-AI tick: код 0..4 управляет каскадом. */
int unit_ai_tick(void *ai, void *rec)
{
    static NativeRec synth;
    const NativeRec *r;
    int a[1];

    a[0] = (int)(size_t)rec;
    if (eee0_record_missing()) {
        /* v15: записи нет (см. eee0_record_missing). Тело после вызова читает
         * только BattleAI+0x38 — подаём независимо снятое `bdd0_res.u170`;
         * поток не потребляется, позиции прочих записей не сдвигаются. */
        g_eee0_synth = 1;
        synth = NativeRec();
        synth.va = (int)B0_VA_UNIT_AI_TICK;
        synth.site = 0x44cbd0;
        synth.ecx = (int)(size_t)ai;
        synth.nargs = 1;
        synth.args[0] = a[0];
        synth.ret = 0;                          /* EAX телом не читается */
        synth.ww[0] = 0x170;
        synth.ww[1] = (int)g_rec->res_u170;
        synth.nww = 2;
        r = &synth;
        g_band_applied = band_apply_recorded();
        g_band_resynced = g_band_applied ? 0 : band_resync_from_stream();
    } else {
        r = nrequest(0, B0_VA_UNIT_AI_TICK, (int)(size_t)ai, a, 1);
        /* v13: recorded-band имеет приоритет над resync (тот остаётся
         * fallback'ом для захватов без "band" и там claim → SKIP, как раньше). */
        g_band_applied = band_apply_recorded();
        g_band_resynced = g_band_applied ? 0 : band_resync_from_stream();
    }
    if (r == NULL) return 0;
    /* v12 additive: 45eee0 writes BattleAI+0x00..0x38 (unit+0x138..0x170). */
    if (r->nww > 0 && ai != NULL) {
        unsigned char *u = (unsigned char *)ai - 0x138;
        for (int k = 0; k + 1 < r->nww; k += 2) {
            int off = r->ww[k];
            unsigned val = (unsigned)r->ww[k + 1];
            if (off >= 0 && arena_addr_ok(u + off, 4)) {
                memcpy(u + off, &val, 4);
            }
        }
    }
    return r->ret;
}

/* vtable+0x2c — phase_time: результат ST(0) (биты в записи). */
void phase_time(void *unit, float *out_f)
{
    const NativeRec *r = nrequest(2, 0, (int)(size_t)unit, NULL, 0); /* u+0x2c */
    if (out_f == NULL) return;
    *out_f = (r != NULL) ? f32_of(r->ret) : 0.0f;
    (void)unit;
}

/* vtable+0x40 — player_get: возврат — объект игрока (NULL/не NULL важно). */
void *player_get(void *unit, int arg)
{
    int a[1];
    a[0] = arg;
    const NativeRec *r = nrequest(4, 0, (int)(size_t)unit, a, 1); /* u+0x40 */
    if (r == NULL) return shadow_region("player");
    return (r->ret == 0) ? NULL : shadow_region("player");
}

/* vtable+0x38 — pointer call: возврат телом не читается. */
int vcall38_ptr(void *unit, void *ptr)
{
    int a[1];
    a[0] = (int)(size_t)ptr;
    const NativeRec *r = nrequest(3, 0, (int)(size_t)unit, a, 1); /* u+0x38 */
    (void)unit;
    return (r != NULL) ? r->ret : 0;
}

/* vtable+0x28 — world notify: void, снимок 0x20 Б по значению. */
void world_notify_snapshot(void *unit, const void *snapshot32)
{
    int a[8];
    int i;
    for (i = 0; i < 8; i++) a[i] = 0;
    if (snapshot32 != NULL) {
        __try {
            memcpy(a, snapshot32, sizeof(a));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            for (i = 0; i < 8; i++) a[i] = 0;
        }
    }
    (void)nrequest(1, 0, (int)(size_t)unit, a, 8); /* slot u+0x28 */
}

} /* namespace native */
