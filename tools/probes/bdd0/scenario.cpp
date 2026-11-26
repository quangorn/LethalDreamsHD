/* tools/probes/bdd0/scenario.cpp — JSONL-захват v12 → Scenario.
 *
 * Парсер скопирован из tools/probes/battle_replay.cpp (JParser: тот же стиль,
 * те же отказы «json: <msg> at offset N»). Загружаются три рода записей:
 *   bdd0_in   — снапшот входа (frame/unit/ai/band/gs + p1/p2/p3/u170/gate114
 *               + raw-цепочка указателей e0/e0_44/tree/gctx0);
 *   bdd0_leaf — поток логических листьев; раскладывается по seq;
 *   bdd0_native — поток прочих RECORD-нативов (slot = vtable-вызов);
 *   bdd0_res  — записанный исход (al/out/u170/m0..m2) для --impl native.
 * Неизвестные ключи игнорируются (v10/v11-файлы читаются: bdd0_in нет →
 * ни одной записи, ядро вернёт «no v12 records» с rc 2).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.h"

/* --------------------------- минимальный JSON --------------------------- */
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t;
    long long n;
    bool b;
    std::string s;
    std::vector<JVal> a;
    std::map<std::string, JVal> o;
    JVal() : t(NUL), n(0), b(false) {}
};

class JParser {
    const std::string &s;
    size_t p;

    void fail(const char *msg) const
    {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "json: %s at offset %u",
                    msg, (unsigned)p);
        throw std::runtime_error(buf);
    }
    void ws()
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' ||
                                s[p] == '\r' || s[p] == '\n')) p++;
    }
    char peek()
    {
        ws();
        if (p >= s.size()) fail("unexpected end");
        return s[p];
    }
    void expect(char c)
    {
        if (peek() != c) fail("unexpected character");
        p++;
    }
    JVal parseString()
    {
        JVal v; v.t = JVal::STR;
        expect('"');
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return v;
            if (c == '\\') {
                if (p >= s.size()) fail("bad escape");
                char e = s[p++];
                switch (e) {
                case 'n': v.s += '\n'; break;
                case 'r': v.s += '\r'; break;
                case 't': v.s += '\t'; break;
                case '"': v.s += '"'; break;
                case '\\': v.s += '\\'; break;
                case '/': v.s += '/'; break;
                case 'u':
                    if (p + 4 > s.size()) fail("bad unicode escape");
                    p += 4; v.s += '?'; break;
                default: fail("bad escape");
                }
            } else {
                v.s += c;
            }
        }
        fail("unterminated string");
        return v;
    }
    JVal parseNum()
    {
        JVal v; v.t = JVal::NUM;
        size_t st = p;
        if (s[p] == '-' || s[p] == '+') p++;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == st || (p == st + 1 && (s[st] == '-' || s[st] == '+'))) {
            fail("bad number");
        }
        if (p < s.size() && (s[p] == '.' || s[p] == 'e' || s[p] == 'E')) {
            while (p < s.size() && (s[p] == '.' || s[p] == 'e' || s[p] == 'E' ||
                                    s[p] == '-' || s[p] == '+' ||
                                    (s[p] >= '0' && s[p] <= '9'))) p++;
        }
        std::string t = s.substr(st, p - st);
        v.n = _strtoi64(t.c_str(), NULL, 10);
        return v;
    }
    JVal parseValue()
    {
        char c = peek();
        if (c == '{') {
            JVal v; v.t = JVal::OBJ;
            p++;
            if (peek() == '}') { p++; return v; }
            for (;;) {
                if (peek() != '"') fail("object key must be string");
                JVal k = parseString();
                expect(':');
                JVal val = parseValue();
                v.o[k.s] = val;
                char d = peek();
                if (d == ',') { p++; continue; }
                if (d == '}') { p++; break; }
                fail("expected , or }");
            }
            return v;
        }
        if (c == '[') {
            JVal v; v.t = JVal::ARR;
            p++;
            if (peek() == ']') { p++; return v; }
            for (;;) {
                v.a.push_back(parseValue());
                char d = peek();
                if (d == ',') { p++; continue; }
                if (d == ']') { p++; break; }
                fail("expected , or ]");
            }
            return v;
        }
        if (c == '"') return parseString();
        if (c == 't') {
            if (s.compare(p, 4, "true") != 0) fail("bad literal");
            p += 4; JVal v; v.t = JVal::BOOL; v.b = true; return v;
        }
        if (c == 'f') {
            if (s.compare(p, 5, "false") != 0) fail("bad literal");
            p += 5; JVal v; v.t = JVal::BOOL; v.b = false; return v;
        }
        if (c == 'n') {
            if (s.compare(p, 4, "null") != 0) fail("bad literal");
            p += 4; return JVal();
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNum();
        fail("bad value");
        return JVal();
    }
public:
    explicit JParser(const std::string &text) : s(text), p(0) {}
    JVal parse()
    {
        JVal v = parseValue();
        ws();
        if (p != s.size()) fail("trailing data");
        return v;
    }
    /* Потоковое чтение: один объект за вызов, true — есть объект. */
    bool parse_next(JVal &v)
    {
        ws();
        if (p >= s.size()) return false;
        v = parseValue();
        ws();
        if (p < s.size() && s[p] == '\r') p++;
        if (p < s.size() && s[p] == '\n') p++;
        return true;
    }
};

/* Текущий загруженный Scenario: state-записи листа 0x45e1d0 (bdd0_leaf_state_*)
 * читаются по (seq,order) из него. Один Scenario на процесс (харнесс грузит
 * один файл). */
static Scenario *g_sc = NULL;

static bool jhas(const JVal &v, const char *key)
{
    return v.t == JVal::OBJ && v.o.find(key) != v.o.end();
}
static const JVal *jget(const JVal &v, const char *key)
{
    if (v.t != JVal::OBJ) return NULL;
    std::map<std::string, JVal>::const_iterator it = v.o.find(key);
    return (it == v.o.end()) ? NULL : &it->second;
}
static long long jint_opt(const JVal &v, const char *key, long long def)
{
    const JVal *x = jget(v, key);
    return (x != NULL && x->t == JVal::NUM) ? x->n : def;
}
static std::string jstr_opt(const JVal &v, const char *key)
{
    const JVal *x = jget(v, key);
    return (x != NULL && x->t == JVal::STR) ? x->s : std::string();
}

/* --------------------------------- base64 -------------------------------- */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Декодирует в dst (cap байт). Возврат — число байт или -1 (битый ввод). */
static int b64_decode(const std::string &in, unsigned char *dst, int cap)
{
    int o = 0;
    unsigned int acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = b64_val(c);
        if (v < 0) return -1;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            dst[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return o;
}

/* Блок фиксированного размера: отсутствующий/null/битый — has=0, не отказ. */
static void read_block(const JVal &v, const char *key, unsigned char *dst,
                       int len, int *has, int *exact)
{
    std::string s = jstr_opt(v, key);
    *has = 0;
    *exact = 0;
    if (s.empty()) return;
    int n = b64_decode(s, dst, len);
    if (n < 0) return;
    *has = 1;
    *exact = (n == len) ? 1 : 0;
}

/* --------------------------------- загрузка ------------------------------ */
struct ResRec {
    int al, out, u170, has, trap;
    int has_band;
    unsigned band_begin, band_end;
    unsigned m[3];
    ResRec() : al(0), out(0), u170(0), has(0), trap(0), has_band(0),
               band_begin(0), band_end(0) { m[0] = m[1] = m[2] = 0; }
};

/* Сайты вызовов ВНУТРИ листа 0x45e1d0 (grid_recompute, лист 0x45e1d0): их
 * записи bdd0_leaf выносятся из окна тика И хранятся по seq в порядке
 * исполнения (bdd0_nested_*, scenario.cpp) — будущий порт листа потребляет их
 * по одному. Список — harness.h (BDD0_NESTED_SITE_R*). */
static const unsigned int k_recompute_sites[] = {
    BDD0_NESTED_SITE_R1_NEW_REC,    /* 0x45e1e3 → 0x4cfa11 */
    BDD0_NESTED_SITE_R2_NEW_SUB,    /* 0x45e23c → 0x4cfa11 */
    BDD0_NESTED_SITE_R3_RESET,      /* 0x45e274 → 0x45c330 */
    BDD0_NESTED_SITE_R3_CLEAR,      /* 0x45e27b → 0x45ca70 */
    BDD0_NESTED_SITE_R4_SCALE_A,    /* 0x45e297 → 0x456b20 */
    BDD0_NESTED_SITE_R4_SCALE_B,    /* 0x45e2ab → 0x456b20 */
    BDD0_NESTED_SITE_R5_ANCHOR,     /* 0x45e316 → 0x458050 */
    BDD0_NESTED_SITE_R5_PARAMS,     /* 0x45e320 → 0x45c8c0 */
    BDD0_NESTED_SITE_R6_LOWER,      /* 0x45e32e → 0x434bc0 */
    BDD0_NESTED_SITE_R7_CTOR,       /* 0x45e340 → 0x45c2e0 */
    BDD0_NESTED_SITE_R7_DIST2,      /* 0x45e3a2 → 0x45c480 */
    BDD0_NESTED_SITE_R7_HEUR,       /* 0x45e3b4 → 0x45c690 */
    BDD0_NESTED_SITE_R7_COPY,       /* 0x45e3ea → 0x45c4b0 */
    BDD0_NESTED_SITE_R7_REGISTER,   /* 0x45e3f2 → 0x45dd00 */
    BDD0_NESTED_SITE_R8_SCAN,       /* 0x45e443 → 0x45ce80 */
    BDD0_NESTED_SITE_R9_UPDATE,     /* 0x45e46b → 0x45dd70 */
    BDD0_NESTED_SITE_R9_COPY,       /* 0x45e47e → 0x45c4b0 */
    BDD0_NESTED_SITE_R9_SCAN,       /* 0x45e487 → 0x45ce80 */
    BDD0_NESTED_SITE_R10_SCALE_C,   /* 0x45e49b → 0x456b20 */
    BDD0_NESTED_SITE_R10_SCALE_D    /* 0x45e4b1 → 0x456b20 */
};

/* Сайты ВНУТРИ листьев, которые исполняет порт: их записи bdd0_leaf принадлежат
 * телу, которое исполняет порт, и вне окна тика (harness.h).
 * 33/34 — внутри 0x458050 (grid_anchor): выносятся и НЕ хранятся (реплей
 * 0x458050 исполняет тело целиком, вложенные листья не наблюдаются).
 * 35..54 — внутри 0x45e1d0: выносятся И хранятся (k_recompute_sites). */
static const unsigned int k_grid_nested_sites[] = {
    BDD0_NESTED_SITE_GRID_ANCHOR,   /* 0x458142 → 0x456b60 */
    BDD0_NESTED_SITE_GRID_KEY       /* 0x45811a → 0x457e20 */
};

static int site_in(const unsigned int *tab, size_t n, int site)
{
    for (size_t i = 0; i < n; i++) {
        if ((unsigned)site == tab[i]) return 1;
    }
    return 0;
}

static int is_nested_site(int site)
{
    return site_in(k_grid_nested_sites,
                   sizeof(k_grid_nested_sites) / sizeof(k_grid_nested_sites[0]),
                   site) ||
           site_in(k_recompute_sites,
                   sizeof(k_recompute_sites) / sizeof(k_recompute_sites[0]),
                   site);
}

/* 1 — сайт принадлежит листу 0x45e1d0 (хранится в Scenario::nested). */
static int is_recompute_site(int site)
{
    return site_in(k_recompute_sites,
                   sizeof(k_recompute_sites) / sizeof(k_recompute_sites[0]),
                   site);
}

static void apply_in(TickRecord &t, const JVal &v, int *bad)
{
    t.seq = (int)jint_opt(v, "seq", 0);
    t.tick = (int)jint_opt(v, "tick", 0);
    t.unit_index = (int)jint_opt(v, "unit", -1);
    t.p1 = (int)jint_opt(v, "p1", 0);
    t.p2 = (int)jint_opt(v, "p2", 0);
    t.p3 = (int)jint_opt(v, "p3", 0);
    t.u170_in = (int)jint_opt(v, "u170", 0);
    t.gate114 = (unsigned)jint_opt(v, "gate114", 0);
    t.trap = (int)jint_opt(v, "trap", 0);   /* снимок снят при сбое */
    t.self_raw = (unsigned)jint_opt(v, "self", 0);
    t.band_raw = (unsigned)jint_opt(v, "band_begin", 0);
    /* v13 (аддитивно): "band":[begin,end] — входное состояние ленты (BattleAI
     * +0x3c+0x20/+0x24). Старые файлы ключа не имеют → has_band_state=0. */
    {
        const JVal *band = jget(v, "band");
        if (band != NULL && band->t == JVal::ARR && band->a.size() >= 2 &&
            band->a[0].t == JVal::NUM && band->a[1].t == JVal::NUM) {
            t.has_band_state = 1;
            t.band_in_begin = (unsigned)band->a[0].n;
            t.band_in_end = (unsigned)band->a[1].n;
        }
    }
    int exact = 0;
    read_block(v, "frame_b64", t.frame, BDD0_FRAME_LEN, &t.has_frame, &exact);
    if (t.has_frame && !exact && bad != NULL) *bad |= 1;
    read_block(v, "unit_b64", t.unit, BDD0_UNIT_LEN, &t.has_unit, &exact);
    if (t.has_unit && !exact && bad != NULL) *bad |= 2;
    read_block(v, "ai_b64", t.ai, BDD0_AI_LEN, &t.has_ai, &exact);
    if (t.has_ai && !exact && bad != NULL) *bad |= 4;

    /* лента: band_b64 (band_count*0x18) — необязательный ключ */
    {
        std::string s = jstr_opt(v, "band_b64");
        int n = s.empty() ? -1 : b64_decode(s, t.band, (int)sizeof(t.band));
        if (n >= 0) {
            t.has_band = (n > 0) ? 1 : 0;
            t.band_n = (n / BDD0_BAND_ELEM > BDD0_BAND_MAX)
                       ? BDD0_BAND_MAX : n / BDD0_BAND_ELEM;
        } else {
            t.band_n = (int)jint_opt(v, "band_count", 0);
            if (t.band_n > BDD0_BAND_MAX) t.band_n = BDD0_BAND_MAX;
            if (t.band_n < 0) t.band_n = 0;
        }
    }
    /* game_state (объект FUN_00442570): 0x20 байт */
    read_block(v, "gs_b64", t.gs, BDD0_GS_LEN, &t.has_gs, &exact);

    /* raw-цепочка указателей снимка (аддитивные поля v12) */
    if (jhas(v, "e0") && jhas(v, "e0_44") && jhas(v, "tree")) {
        t.e0 = (unsigned)jint_opt(v, "e0", 0);
        t.e0_44 = (unsigned)jint_opt(v, "e0_44", 0);
        t.tree = (unsigned)jint_opt(v, "tree", 0);
        t.has_chain = 1;
    }
    if (jhas(v, "gctx0")) {
        t.gctx0 = (unsigned)jint_opt(v, "gctx0", 0);
        t.has_gctx0 = 1;
    }
    /* v15 (аддитивно): "ctx_pos":[x,y,z] — независимая pos-тройка листа
     * 45c6f0 (`[*(unit+0x138)+0xc]`). null/отсутствие ключа — тройки нет,
     * старые файлы идут прежним путём (реконструкция из `w`). */
    {
        const JVal *cp = jget(v, "ctx_pos");
        if (cp != NULL && cp->t == JVal::ARR && cp->a.size() >= 3 &&
            cp->a[0].t == JVal::NUM && cp->a[1].t == JVal::NUM &&
            cp->a[2].t == JVal::NUM) {
            t.has_ctx_pos = 1;
            t.ctx_pos[0] = (int)cp->a[0].n;
            t.ctx_pos[1] = (int)cp->a[1].n;
            t.ctx_pos[2] = (int)cp->a[2].n;
        }
    }
    /* v16 (аддитивно): "band_pos" — геометрия ленты (параллельные массивы на
     * band_n элементов). Моделируется только когда ключ есть, длина совпадает
     * с band_n и все ok[i]==1; иначе has_band_pos=0 (старое поведение: порт
     * 45b150/45b3e0 реплеит натив). sa/sl/sm — сырые биты float-тройки уровня. */
    {
        const JVal *bp = jget(v, "band_pos");
        if (bp != NULL && bp->t == JVal::OBJ) {
            const JVal *ok = jget(*bp, "ok");
            const JVal *xs = jget(*bp, "x");
            const JVal *ys = jget(*bp, "y");
            const JVal *sa = jget(*bp, "sa");
            const JVal *sl = jget(*bp, "sl");
            const JVal *sm = jget(*bp, "sm");
            int n = (ok != NULL && ok->t == JVal::ARR) ? (int)ok->a.size() : 0;
            /* Правило загрузчика: тик, чей (end-begin)/0x18 превышает захваченное
             * число элементов, НЕ моделируется (порт прочитал бы за band_b64). */
            int tick_cnt = (t.has_band_state &&
                            t.band_in_end >= t.band_in_begin)
                           ? (int)((t.band_in_end - t.band_in_begin) / BDD0_BAND_ELEM)
                           : 0;
            int complete = (n > 0 && n <= BDD0_BAND_MAX && n == t.band_n &&
                            tick_cnt <= n &&
                            xs != NULL && xs->t == JVal::ARR &&
                            (int)xs->a.size() == n &&
                            ys != NULL && ys->t == JVal::ARR &&
                            (int)ys->a.size() == n &&
                            sa != NULL && sa->t == JVal::ARR &&
                            (int)sa->a.size() == n &&
                            sl != NULL && sl->t == JVal::ARR &&
                            (int)sl->a.size() == n &&
                            sm != NULL && sm->t == JVal::ARR &&
                            (int)sm->a.size() == n);
            for (int i = 0; complete && i < n; i++) {
                if (ok->a[i].t != JVal::NUM || ok->a[i].n != 1) complete = 0;
            }
            if (complete) {
                t.has_band_pos = 1;
                t.band_pos_n = n;
                for (int i = 0; i < n; i++) {
                    t.band_pos_ok[i] = (int)ok->a[i].n;
                    t.band_pos_x[i] = (int)xs->a[i].n;
                    t.band_pos_y[i] = (int)ys->a[i].n;
                    t.band_pos_sa[i] = (unsigned)sa->a[i].n;
                    t.band_pos_sl[i] = (unsigned)sl->a[i].n;
                    t.band_pos_sm[i] = (unsigned)sm->a[i].n;
                }
            }
        }
    }
}

static void apply_res(ResRec &r, const JVal &v)
{
    r.has = 1;
    r.trap = (int)jint_opt(v, "trap", 0);
    r.al = (int)jint_opt(v, "al", 0);
    r.out = (int)jint_opt(v, "out", -1);
    r.u170 = (int)jint_opt(v, "u170", 0);
    r.m[0] = (unsigned)jint_opt(v, "m0", 0);
    r.m[1] = (unsigned)jint_opt(v, "m1", 0);
    r.m[2] = (unsigned)jint_opt(v, "m2", 0);
    /* v13 (аддитивно): после-состояние ленты (begin,end) из bdd0_res. */
    {
        const JVal *band = jget(v, "band");
        if (band != NULL && band->t == JVal::ARR && band->a.size() >= 2 &&
            band->a[0].t == JVal::NUM && band->a[1].t == JVal::NUM) {
            r.has_band = 1;
            r.band_begin = (unsigned)band->a[0].n;
            r.band_end = (unsigned)band->a[1].n;
        }
    }
}

/* v18: bdd0_cand — снимок PathState, возвращённого сканом 45ce80 (сайт 49/52),
 * снятый на выходе сайта. Ключ — (seq, order) записи bdd0_leaf того же сайта
 * (писатель даёт им один order); после сборки окна снимок прикрепляется к своей
 * NestedRec. Без этих записей (v17 и старше) гейт порта листа = 0. */
struct CandRec {
    int seq, order;
    unsigned cand;
    int ps_ok;
    unsigned ps[0x58 / 4];
    CandRec() : seq(0), order(0), cand(0), ps_ok(0)
    {
        for (int i = 0; i < 0x58 / 4; i++) ps[i] = 0;
    }
};

static void apply_cand(CandRec &c, const JVal &v)
{
    int has = 0, exact = 0;

    c.seq = (int)jint_opt(v, "seq", 0);
    c.order = (int)jint_opt(v, "order", 0);
    c.cand = (unsigned)jint_opt(v, "cand", 0);
    read_block(v, "ps_b64", (unsigned char *)c.ps, 0x58, &has, &exact);
    c.ps_ok = (has && exact) ? 1 : 0;        /* усечённый блок не используем */
}

static void apply_leaf(LeafRec &l, const JVal &v)
{
    memset(&l, 0, sizeof(l));
    l.order = (int)jint_opt(v, "order", 0);
    const JVal *va = jget(v, "va");
    if (va != NULL && va->t == JVal::STR) {
        l.va = (int)strtoul(va->s.c_str(), NULL, 16);
    } else if (va != NULL && va->t == JVal::NUM) {
        l.va = (int)va->n;
    }
    {
        const JVal *site = jget(v, "site");
        if (site != NULL && site->t == JVal::STR) {
            l.site = (int)strtoul(site->s.c_str(), NULL, 16);
        } else if (site != NULL && site->t == JVal::NUM) {
            l.site = (int)site->n;
        }
    }
    l.ecx = (int)jint_opt(v, "ecx", 0);
    l.ret = (int)jint_opt(v, "ret", 0);
    l.trap = (int)jint_opt(v, "trap", 0);   /* старые записи поля не имеют */
    const JVal *args = jget(v, "args");
    if (args != NULL && args->t == JVal::ARR) {
        for (size_t i = 0; i < args->a.size() && l.nargs < 6; i++) {
            if (args->a[i].t == JVal::NUM) {
                l.args[l.nargs++] = (int)args->a[i].n;
            }
        }
    }
    const JVal *w = jget(v, "w");
    if (w != NULL && w->t == JVal::ARR) {
        for (size_t i = 0; i < w->a.size() && l.nw < 3; i++) {
            if (w->a[i].t == JVal::NUM) l.w[l.nw++] = (int)w->a[i].n;
        }
    }
}

/* v17: один узел карты из bdd0_leaf_* "maps"{m1,m2}.nodes[i]: a — адрес узла,
 * n — сырой 0x18-байт RB-узел (b64), ps — PathState 0x58 (b64, null → нет).
 * Сохраняем ВСЁ: адрес (identity для reg_add_fixed/bind_add), нормализованные
 * key/value/colour/is_nil (structs.h TreeNode) и PathState (+0x08/+0x20/+0x24).
 * При нечитаемом n узел помечается is_nil=1 (fail-closed, значения не выдуманы). */
static void map_node_from_json(const JVal &nd, MapNodeRec &out)
{
    unsigned char nb[0x18];
    int has = 0, exact = 0;
    memset(&out, 0, sizeof(out));
    out.addr = (unsigned)jint_opt(nd, "a", 0);
    read_block(nd, "n", nb, 0x18, &has, &exact);
    if (!has || !exact) { out.is_nil = 1; return; }
    out.has_node = 1;
    memcpy(out.node, nb, 0x18);           /* сырой узел — для арены */
    memcpy(&out.key, nb + 0x0c, 4);       /* key (int32) */
    memcpy(&out.value, nb + 0x10, 4);     /* value (Candidate*) */
    out.colour = nb[0x14];                /* colour (0=чёрный,1=красный) */
    out.is_nil = nb[0x15];                /* is_nil (0=узел,1=sentinel) */
    read_block(nd, "ps", (unsigned char *)out.ps, BDD0_PS_LEN,
               &out.has_ps, &exact);
    if (out.has_ps && !exact) out.has_ps = 0;   /* усечённый ps — не используем */
}

/* v17: bdd0_leaf_pre / bdd0_leaf_post / bdd0_site_pf — заголовок (seq/order/
 * site/va/ecx/args) + состояние (cap_write_leaf_state, capture.cpp). Разбор
 * переиспользует read_block/b64_decode/jint_opt. ok=0, если обязательный блок
 * pf отсутствует/усечён (fail-closed: порт этот вызов не использует). ctx
 * известен только двумя dword'ами (ctx120/ctx134); maps — head/n/trunc/trap +
 * узлы (адрес, RB-узел, PathState); pre пусты, post несут вставки. */
static void apply_leaf_state(LeafStateRec &s, const JVal &v)
{
    int exact = 0, bad = 0;
    memset(&s, 0, sizeof(s));
    s.seq = (int)jint_opt(v, "seq", 0);
    s.order = (int)jint_opt(v, "order", 0);
    {
        const JVal *site = jget(v, "site");
        if (site != NULL && site->t == JVal::STR)
            s.site = (int)strtoul(site->s.c_str(), NULL, 16);
        else if (site != NULL && site->t == JVal::NUM)
            s.site = (int)site->n;
    }
    {
        const JVal *va = jget(v, "va");
        if (va != NULL && va->t == JVal::STR)
            s.va = (int)strtoul(va->s.c_str(), NULL, 16);
        else if (va != NULL && va->t == JVal::NUM)
            s.va = (int)va->n;
    }
    s.ecx = (unsigned)jint_opt(v, "ecx", 0);
    {
        const JVal *args = jget(v, "args");
        if (args != NULL && args->t == JVal::ARR) {
            for (size_t i = 0; i < args->a.size() && s.nargs < 6; i++)
                if (args->a[i].t == JVal::NUM)
                    s.args[s.nargs++] = (int)args->a[i].n;
        }
    }
    /* pf 0x78 — обязателен: без него состояние неполно */
    read_block(v, "pf", (unsigned char *)s.pf, BDD0_LEAFPF_LEN,
               &s.has_pf, &exact);
    if (s.has_pf && !exact) bad = 1;
    s.rec = (unsigned)jint_opt(v, "rec", 0);
    read_block(v, "rec_b64", (unsigned char *)s.recb, BDD0_LEAFREC_LEN,
               &s.has_rec, &exact);
    if (s.has_rec && !exact) bad = 1;
    s.sub = (unsigned)jint_opt(v, "sub", 0);
    read_block(v, "sub_b64", (unsigned char *)s.subb, BDD0_LEAFSUB_LEN,
               &s.has_sub, &exact);
    if (s.has_sub && !exact) bad = 1;
    s.ctx = (unsigned)jint_opt(v, "ctx", 0);
    {
        const JVal *c120 = jget(v, "ctx120");
        const JVal *c134 = jget(v, "ctx134");
        if (c120 != NULL && c120->t == JVal::NUM &&
            c134 != NULL && c134->t == JVal::NUM) {
            s.has_ctx = 1;
            s.ctx120 = (unsigned)c120->n;
            s.ctx134 = (unsigned)c134->n;
        }
    }
    s.bandrec = (unsigned)jint_opt(v, "bandrec", 0);
    read_block(v, "bandrec_b64", (unsigned char *)s.bandb, BDD0_LEAFBAND_LEN,
               &s.has_band, &exact);
    if (s.has_band && !exact) bad = 1;
    /* maps{m1,m2}: head/n/trunc/trap + nodes[{a,n,ps}]. Узел — адрес (identity),
     * сырой 0x18-байт RB-узел (n) + PathState 0x58 (ps, может быть null). */
    {
        const JVal *maps = jget(v, "maps");
        if (maps != NULL && maps->t == JVal::OBJ) {
            const JVal *m1 = jget(*maps, "m1");
            const JVal *m2 = jget(*maps, "m2");
            s.has_maps = (m1 != NULL && m1->t == JVal::OBJ &&
                          m2 != NULL && m2->t == JVal::OBJ) ? 1 : 0;
            if (s.has_maps) {
                const JVal *n1 = jget(*m1, "nodes");
                const JVal *n2 = jget(*m2, "nodes");
                s.h1.head = (unsigned)jint_opt(*m1, "head", 0);
                s.h1.n = (int)jint_opt(*m1, "n", 0);
                s.h1.trunc = (int)jint_opt(*m1, "trunc", 0);
                s.h1.trap = (int)jint_opt(*m1, "trap", 0);
                s.h2.head = (unsigned)jint_opt(*m2, "head", 0);
                s.h2.n = (int)jint_opt(*m2, "n", 0);
                s.h2.trunc = (int)jint_opt(*m2, "trunc", 0);
                s.h2.trap = (int)jint_opt(*m2, "trap", 0);
                /* потолок узлов — fail-closed: лишние узлы не читаются */
                if (n1 != NULL && n1->t == JVal::ARR)
                    s.m1_n = (n1->a.size() > BDD0_MAPNODE_MAX)
                             ? BDD0_MAPNODE_MAX : (int)n1->a.size();
                if (n2 != NULL && n2->t == JVal::ARR)
                    s.m2_n = (n2->a.size() > BDD0_MAPNODE_MAX)
                             ? BDD0_MAPNODE_MAX : (int)n2->a.size();
                for (int i = 0; i < s.m1_n; i++)
                    map_node_from_json(n1->a[i], s.m1[i]);
                for (int i = 0; i < s.m2_n; i++)
                    map_node_from_json(n2->a[i], s.m2[i]);
            }
        }
    }
    s.has = 1;
    s.ok = (s.has_pf && !bad) ? 1 : 0;
}

static int slot_id_from_name(const std::string &s)
{
    if (s == "u+0x28") return 1;
    if (s == "u+0x2c") return 2;
    if (s == "u+0x38") return 3;
    if (s == "u+0x40") return 4;
    return 0;
}

static void apply_native(NativeRec &n, const JVal &v)
{
    n = NativeRec();
    n.order = (int)jint_opt(v, "order", 0);
    const JVal *site = jget(v, "site");
    if (site != NULL && site->t == JVal::STR) {
        n.site = (int)strtoul(site->s.c_str(), NULL, 16);
    } else if (site != NULL && site->t == JVal::NUM) {
        n.site = (int)site->n;
    }
    const JVal *va = jget(v, "va");
    if (va != NULL && va->t == JVal::STR) {
        n.va = (int)strtoul(va->s.c_str(), NULL, 16);
    } else if (va != NULL && va->t == JVal::NUM) {
        n.va = (int)va->n;
    }
    n.slot = slot_id_from_name(jstr_opt(v, "slot"));
    std::string rk = jstr_opt(v, "ret_kind");
    n.kind = (rk == "al") ? 1 : (rk == "f32") ? 2 : (rk == "void") ? 3 : 0;
    n.ecx = (int)jint_opt(v, "ecx", 0);
    n.ret = (int)jint_opt(v, "ret", 0);
    n.trap = (int)jint_opt(v, "trap", 0);
    const JVal *args = jget(v, "args");
    if (args != NULL && args->t == JVal::ARR) {
        for (size_t i = 0; i < args->a.size() && n.nargs < 8; i++) {
            if (args->a[i].t == JVal::NUM) n.args[n.nargs++] = (int)args->a[i].n;
        }
    }
    const JVal *w = jget(v, "w");
    if (w != NULL && w->t == JVal::ARR) {
        for (size_t i = 0; i < w->a.size() && n.nw < 3; i++) {
            if (w->a[i].t == JVal::NUM) n.w[n.nw++] = (int)w->a[i].n;
        }
    }
    const JVal *ww = jget(v, "ww");
    if (ww != NULL && ww->t == JVal::ARR) {
        for (size_t i = 0; i < ww->a.size() && n.nww < 32; i++) {
            if (ww->a[i].t == JVal::NUM) n.ww[n.nww++] = (int)ww->a[i].n;
        }
    }
}

static void apply_grid(GridModel &g, const JVal &v)
{
    const JVal *lv = jget(v, "levels");
    const JVal *rows = jget(v, "rows");
    const JVal *cells = jget(v, "cells");
    int i;

    /* Сброс скалярных полей БЕЗ временного GridModel() на стеке: массивы на
     * BDD0_GRID_MAX_CELLS=200000 (3.2 МБ) переполнили бы 1-МБ стек. Массивы
     * читаются только при valid==1, а он ставится лишь после полного заполнения
     * ncell записей — устаревшие хвосты безвредны. */
    g.valid = 0; g.nlev = 0; g.leaf = -1; g.ncell = 0;
    g.cells_game = 0; g.rows_game = 0;
    /* Офлайн-кэп BDD0_GRID_MAX_CELLS совпадает с живым (CAP_MAX_GRID_CELLS=200000),
     * BDD0_GRID_MAX_LEVELS=16 ≥ живого. Превышение офлайн-кэпа НЕ ошибка формата:
     * ncell/nlev обнуляются, g.valid остаётся 0 → has_grid=0 →
     * bdd0_grid_level_modelled()=0 (модель молча выключается, порт 458050 реплеит
     * натив — fail-closed). Читатель загрузчика: это осознанный потолок, не сбой. */
    if (jint_opt(v, "error", 0)) return;            /* grid error-запись */
    if (lv == NULL || lv->t != JVal::ARR || rows == NULL ||
        rows->t != JVal::ARR || cells == NULL || cells->t != JVal::OBJ) {
        return;
    }
    g.nlev = (int)lv->a.size();
    if (g.nlev <= 0 || g.nlev > BDD0_GRID_MAX_LEVELS) { g.nlev = 0; return; }
    for (i = 0; i < g.nlev; i++) {
        const JVal &L = lv->a[i];
        GridLevelModel &m = g.lev[i];
        m.sx = (int)jint_opt(L, "sx", 0);
        m.sy = (int)jint_opt(L, "sy", 0);
        m.rx = (int)jint_opt(L, "rx", 0);
        m.ry = (int)jint_opt(L, "ry", 0);
        m.sa = (unsigned)jint_opt(L, "sa", 0);
        m.sl = (unsigned)jint_opt(L, "sl", 0);
        m.sm = (unsigned)jint_opt(L, "sm", 0);
        m.next = (int)jint_opt(L, "next", -1);
    }
    g.cells_game = (unsigned)jint_opt(v, "cells_base", 0);   /* v16: база ячеек в игре */
    g.rows_game = (unsigned)jint_opt(v, "rows_base", 0);     /* v16: база rows в игре */
    g.leaf = (int)jint_opt(v, "leaf", g.nlev - 1);
    if (g.leaf < 0 || g.leaf >= g.nlev) g.leaf = g.nlev - 1;
    g.ncell = (int)rows->a.size();
    if (g.ncell <= 0 || g.ncell > BDD0_GRID_MAX_CELLS) { g.ncell = 0; return; }
    for (i = 0; i < g.ncell; i++) {
        g.rows[i] = (int)rows->a[i].n;
    }
    {
        const JVal *key = jget(*cells, "key");
        const JVal *nx = jget(*cells, "next");
        const JVal *mn = jget(*cells, "min");
        int n = (key != NULL && key->t == JVal::ARR) ? (int)key->a.size() : 0;
        if (n != g.ncell) { g.ncell = 0; return; }
        for (i = 0; i < g.ncell; i++) {
            g.key[i] = (int)key->a[i].n;
            g.nextc[i] = (nx != NULL && nx->t == JVal::ARR && i < (int)nx->a.size())
                         ? (int)nx->a[i].n : -1;
            g.minm[i] = (mn != NULL && mn->t == JVal::ARR && i < (int)mn->a.size())
                        ? (int)mn->a[i].n : 0;
        }
    }
    g.valid = 1;
}

Scenario *load_capture(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        throw std::runtime_error(std::string("cannot open capture: ") + path);
    }
    std::string text;
    {
        char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    }
    fclose(f);

    Scenario *sc = new Scenario;
    g_sc = sc;
    const char *base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;
    sc->name = base;

    std::map<int, ResRec> res_by_seq;
    std::vector<std::pair<int, LeafRec> > leaves;   /* (seq, leaf) */
    std::vector<std::pair<int, NativeRec> > natives; /* (seq, native) */
    /* v17: внутренние вызовы листа 0x45e1d0 — (seq, NestedRec) в порядке
     * исполнения; state-записи — по (seq,order). */
    std::vector<std::pair<int, NestedRec> > nested;
    std::vector<std::pair<int, LeafStateRec> > leaf_pre, leaf_post, site_pf;
    /* v18: снимки кандидатов скана — по (seq, order) записи bdd0_leaf сайта. */
    std::vector<CandRec> cands;

    JParser jp(text);
    JVal v;
    int bad_sizes = 0;
    try {
        for (;;) {
            /* оборванный хвост (захват убит на записи) — не формат-ошибка:
             * «unexpected end» на последнем чанке отбрасывается с пометкой. */
            try {
                if (!jp.parse_next(v)) break;
            } catch (const std::runtime_error &e) {
                if (strstr(e.what(), "unexpected end") != NULL &&
                    sc->records > 0) {
                    sc->truncated = 1;
                    break;
                }
                throw;
            }
            if (v.t != JVal::OBJ) {
                throw std::runtime_error("capture line is not an object");
            }
            sc->records++;
            std::string kind = jstr_opt(v, "kind");
            long long ver = jint_opt(v, "v", 0);
            if (kind == "bdd0_in") {
                if (ver < 12) continue;            /* не v12-снапшот */
                TickRecord t;
                apply_in(t, v, &bad_sizes);
                /* сценарий: ключ scenario, если он есть в записи */
                std::string sn = jstr_opt(v, "scenario");
                if (!sn.empty()) sc->name = sn;
                if (t.trap) sc->input_trap++;
                sc->ticks.push_back(t);
                sc->snap_records++;
            } else if (kind == "bdd0_res") {
                ResRec r;
                apply_res(r, v);
                res_by_seq[(int)jint_opt(v, "seq", 0)] = r;
            } else if (kind == "leaf_missing") {
                /* сайт листа не захукался на захвате: поток может быть неполон
                 * для ВСЕХ seq — запоминаем причину, leaf_seq её назовёт. */
                std::string site = jstr_opt(v, "site");
                std::string va = jstr_opt(v, "va");
                std::string why = jstr_opt(v, "why");
                sc->leaf_missing_n++;
                if (sc->leaf_missing_why.empty()) {
                    char buf[220];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                                "capture leaf_missing: site 0x%s (leaf 0x%s) "
                                "not hooked: %s", site.c_str(), va.c_str(),
                                why.empty() ? "?" : why.c_str());
                    sc->leaf_missing_why = buf;
                }
            } else if (kind == "bdd0_leaf") {
                if (ver < 12) continue;
                LeafRec l;
                apply_leaf(l, v);
                /* Сайты ВНУТРИ листа (0x458050 grid_anchor и 0x45e1d0
                 * grid_recompute): записи сняты, но claim/request их не
                 * получит — выносим из окна тика и считаем отдельно, иначе
                 * leaf_seq видел бы «лишнюю». Сайты 0x45e1d0 дополнительно
                 * хранятся по seq в порядке исполнения (bdd0_nested_*). */
                if (is_nested_site(l.site)) {
                    sc->nested_leaf_n++;
                    if (is_recompute_site(l.site)) {
                        NestedRec nr;
                        nr.order = l.order;
                        nr.site = l.site;
                        nr.va = l.va;
                        nr.ecx = l.ecx;
                        nr.nargs = l.nargs;
                        for (int k = 0; k < 6; k++) nr.args[k] = l.args[k];
                        nr.ret = l.ret;
                        nr.nw = l.nw;
                        for (int k = 0; k < 3; k++) nr.w[k] = l.w[k];
                        nr.trap = l.trap;
                        nested.push_back(std::make_pair(
                            (int)jint_opt(v, "seq", 0), nr));
                    }
                    continue;
                }
                leaves.push_back(std::make_pair(
                    (int)jint_opt(v, "seq", 0), l));
                sc->leaf_records++;
            } else if (kind == "bdd0_leaf_pre" ||
                       kind == "bdd0_leaf_post" ||
                       kind == "bdd0_site_pf") {
                /* v17: состояние листа 0x45e1d0 (pre/post/site_pf). */
                if (ver < 12) continue;
                LeafStateRec s;
                apply_leaf_state(s, v);
                std::pair<int, LeafStateRec> item(
                    (int)jint_opt(v, "seq", 0), s);
                if (kind == "bdd0_leaf_pre") leaf_pre.push_back(item);
                else if (kind == "bdd0_leaf_post") leaf_post.push_back(item);
                else site_pf.push_back(item);
                sc->leaf_state_records++;
            } else if (kind == "bdd0_cand") {
                /* v18 (аддитивно): снимок PathState-кандидата скана 45ce80. */
                if (ver < 12) continue;
                CandRec c;
                apply_cand(c, v);
                cands.push_back(c);
                sc->cand_records++;
            } else if (kind == "bdd0_native") {
                if (ver < 12) continue;
                NativeRec n;
                apply_native(n, v);
                natives.push_back(std::make_pair(
                    (int)jint_opt(v, "seq", 0), n));
                sc->native_records++;
            } else if (kind == "grid") {
                /* v16 (аддитивно): модель сетки из bdd0-пути (cap_write_grid_record).
                 * Один снимок на файл; последняя валидная запись побеждает. */
                apply_grid(sc->grid, v);
                sc->has_grid = sc->grid.valid;
            } else if (kind == "native_site_skipped") {
                std::string site = jstr_opt(v, "site");
                std::string va = jstr_opt(v, "va");
                std::string why = jstr_opt(v, "why");
                sc->native_missing_n++;
                if (sc->native_missing_why.empty()) {
                    char buf[220];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                                "capture native_site_skipped: site 0x%s "
                                "(target 0x%s): %s", site.c_str(), va.c_str(),
                                why.empty() ? "?" : why.c_str());
                    sc->native_missing_why = buf;
                }
            }
            /* прочие роды (cap, res, grid, eee0_*, bdd0_cap) игнорируются */
        }
    } catch (...) {
        delete sc;
        throw;
    }
    if (bad_sizes != 0) {
        delete sc;
        char buf[200];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "capture: base64 block size mismatch (mask=0x%x)",
                    (unsigned)bad_sizes);
        throw std::runtime_error(buf);
    }

    /* листья: стабильный порядок (seq, order) → окно на каждый тик */
    std::sort(leaves.begin(), leaves.end(),
              [](const std::pair<int, LeafRec> &a,
                 const std::pair<int, LeafRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    for (size_t i = 0; i < leaves.size(); i++) sc->leaves.push_back(leaves[i].second);

    /* bdd0_native: тот же стабильный порядок (seq, order) → окно на тик */
    std::sort(natives.begin(), natives.end(),
              [](const std::pair<int, NativeRec> &a,
                 const std::pair<int, NativeRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    for (size_t i = 0; i < natives.size(); i++) {
        sc->natives.push_back(natives[i].second);
    }

    /* v17: внутренние вызовы листа 0x45e1d0 — стабильный порядок (seq, order)
     * → окно на тик; state-записи — по (seq, order). */
    std::sort(nested.begin(), nested.end(),
              [](const std::pair<int, NestedRec> &a,
                 const std::pair<int, NestedRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    /* v18: к записи скана прикрепляется снимок кандидата того же (seq, order);
     * расхождение записанного `cand` с ret записи — аномалия, снимок не берём
     * (гейт порта листа тогда 0, fail-closed). */
    for (size_t i = 0; i < nested.size(); i++) {
        NestedRec nr = nested[i].second;
        int seq = nested[i].first;
        if (nr.va == BDD0_VA_SCAN_MAP) {
            for (size_t k = 0; k < cands.size(); k++) {
                if (cands[k].seq != seq || cands[k].order != nr.order) continue;
                if (!cands[k].ps_ok || cands[k].cand != (unsigned)nr.ret) break;
                nr.cand_ok = 1;
                nr.cand = cands[k].cand;
                for (int d = 0; d < 0x58 / 4; d++) nr.cand_ps[d] = cands[k].ps[d];
                break;
            }
        }
        sc->nested.push_back(nr);
    }
    std::sort(leaf_pre.begin(), leaf_pre.end(),
              [](const std::pair<int, LeafStateRec> &a,
                 const std::pair<int, LeafStateRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    std::sort(leaf_post.begin(), leaf_post.end(),
              [](const std::pair<int, LeafStateRec> &a,
                 const std::pair<int, LeafStateRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    std::sort(site_pf.begin(), site_pf.end(),
              [](const std::pair<int, LeafStateRec> &a,
                 const std::pair<int, LeafStateRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    for (size_t i = 0; i < leaf_pre.size(); i++)
        sc->leaf_pre.push_back(leaf_pre[i].second);
    for (size_t i = 0; i < leaf_post.size(); i++)
        sc->leaf_post.push_back(leaf_post[i].second);
    for (size_t i = 0; i < site_pf.size(); i++)
        sc->site_pf.push_back(site_pf[i].second);

    size_t li = 0;
    size_t ni = 0;
    size_t xi = 0;   /* nested */
    size_t pi = 0;   /* leaf_pre */
    size_t qi = 0;   /* leaf_post */
    size_t si = 0;   /* site_pf */
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        TickRecord &t = sc->ticks[i];
        while (li < leaves.size() && leaves[li].first < t.seq) li++;
        size_t first = li;
        while (li < leaves.size() && leaves[li].first == t.seq) li++;
        t.leaf_first = (int)first;
        t.leaf_n = (int)(li - first);
        while (ni < natives.size() && natives[ni].first < t.seq) ni++;
        size_t nfirst = ni;
        while (ni < natives.size() && natives[ni].first == t.seq) ni++;
        t.native_first = (int)nfirst;
        t.native_n = (int)(ni - nfirst);
        /* v17: окно вложенных вызовов + state-записи этого seq */
        while (xi < nested.size() && nested[xi].first < t.seq) xi++;
        size_t xfirst = xi;
        while (xi < nested.size() && nested[xi].first == t.seq) xi++;
        t.nested_first = (xfirst < nested.size()) ? (int)xfirst : -1;
        t.nested_n = (int)(xi - xfirst);
        while (pi < leaf_pre.size() && leaf_pre[pi].first < t.seq) pi++;
        t.leaf_pre_idx = (pi < leaf_pre.size() && leaf_pre[pi].first == t.seq)
                         ? (int)pi : -1;
        while (qi < leaf_post.size() && leaf_post[qi].first < t.seq) qi++;
        t.leaf_post_idx = (qi < leaf_post.size() && leaf_post[qi].first == t.seq)
                          ? (int)qi : -1;
        while (si < site_pf.size() && site_pf[si].first < t.seq) si++;
        t.site_pf_idx = (si < site_pf.size() && site_pf[si].first == t.seq)
                        ? (int)si : -1;
        t.native_eee0_no_ww = 0;
        t.grid_cells_raw = sc->grid.valid ? sc->grid.cells_game : 0;  /* v16 */
        t.grid_cells_len = sc->grid.valid ? (unsigned)sc->grid.ncell * 0x34u : 0;
        t.gs_raw = 0;          /* адрес объекта состояния — из потока 442570 */
        for (size_t k = nfirst; k < ni; k++) {
            if (natives[k].second.va == 0x45eee0 &&
                natives[k].second.nww == 0) {
                t.native_eee0_no_ww = 1;
            }
            if (natives[k].second.va == 0x442570 && natives[k].second.ret != 0) {
                if (t.gs_raw == 0) t.gs_raw = (unsigned)natives[k].second.ret;
                else if (t.gs_raw != (unsigned)natives[k].second.ret) {
                    /* объект состояния переехал внутри тика: привязка невозможна */
                    t.gs_raw = 0;
                }
            }
        }
        std::map<int, ResRec>::const_iterator it = res_by_seq.find(t.seq);
        if (it != res_by_seq.end()) {
            t.has_res = 1;
            t.res_trap = it->second.trap;
            if (t.res_trap) sc->outcome_trap++;
            t.res_al = it->second.al;
            t.res_out = it->second.out;
            t.res_u170 = it->second.u170;
            t.res_m[0] = it->second.m[0];
            t.res_m[1] = it->second.m[1];
            t.res_m[2] = it->second.m[2];
            t.has_res_band = it->second.has_band;
            t.band_post_begin = it->second.band_begin;
            t.band_post_end = it->second.band_end;
        }
    }
    /* хвост оборван (захват убит mid-tick): seq без bdd0_res и он последний по
     * seq в захвате — запись снята не до конца, тик недостоверен как данные.
     * Отсутствие res ниже последнего seq — настоящая аномалия захвата. */
    int max_seq = sc->ticks.empty() ? 0 : sc->ticks[0].seq;
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        if (sc->ticks[i].seq > max_seq) max_seq = sc->ticks[i].seq;
    }
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        TickRecord &t = sc->ticks[i];
        if (t.has_res) continue;
        if (t.seq == max_seq) {
            t.incomplete_tail = 1;
            sc->incomplete_tail++;
        } else {
            t.res_missing_nontail = 1;
            sc->res_missing_nontail++;
        }
    }
    return sc;
}

/* v17: доступ к state-записям листа 0x45e1d0 по (seq, order). Линейный поиск в
 * отсортированном по (seq,order) векторе; NULL — записи нет / другой вызов. */
static const LeafStateRec *leaf_state_find(const std::vector<LeafStateRec> &v,
                                           int seq, int order)
{
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].seq != seq) continue;
        if (v[i].order == order) return &v[i];
    }
    return NULL;
}

const LeafStateRec *bdd0_leaf_state_pre(int seq, int order)
{
    /* состояние живёт в Scenario, а не в арене: загрузчик держит указатель на
     * текущий Scenario в g_sc (см. load_capture). */
    if (g_sc == NULL) return NULL;
    return leaf_state_find(g_sc->leaf_pre, seq, order);
}

int bdd0_leaf_state_records(void)
{
    return (g_sc != NULL) ? g_sc->leaf_state_records : 0;
}

int bdd0_site_pf_records(void)
{
    return (g_sc != NULL) ? (int)g_sc->site_pf.size() : 0;
}

/* v18: сколько снимков кандидатов скана прочитано (0 — данные до v18). */
int bdd0_cand_records(void)
{
    return (g_sc != NULL) ? g_sc->cand_records : 0;
}

/* v17: pre-запись по индексу в Scenario::leaf_pre (окно тика). NULL — нет. */
const LeafStateRec *bdd0_leaf_pre_at(int idx)
{
    if (g_sc == NULL || idx < 0 || idx >= (int)g_sc->leaf_pre.size())
        return NULL;
    return &g_sc->leaf_pre[idx];
}

const LeafStateRec *bdd0_leaf_state_post(int seq, int order)
{
    if (g_sc == NULL) return NULL;
    return leaf_state_find(g_sc->leaf_post, seq, order);
}

const LeafStateRec *bdd0_leaf_state_site_pf(int seq, int order)
{
    /* site_pf пишется с order ВНУТРЕННЕГО сайта 0x45e320, не листа — order не
     * совпадает; сопоставляем по seq (один вызов листа на seq в v17). */
    (void)order;
    if (g_sc == NULL) return NULL;
    for (size_t i = 0; i < g_sc->site_pf.size(); i++)
        if (g_sc->site_pf[i].seq == seq) return &g_sc->site_pf[i];
    return NULL;
}

void scenario_free(Scenario *s) { delete s; }
