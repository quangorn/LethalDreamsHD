/* battle_replay.cpp — офлайн-харнесс FUN_0045e0a0 (battle_ai_ab_method.md §3.2).
 *
 * Читает JSONL-захват нативного вызова (battle/capture.cpp), строит приватную
 * реплику BattleAI/map1/map2, прогоняет порт (battle/path.cpp + battle/target.cpp)
 * с подставленными native::-обёртками и захваченными результатами нативных
 * листьев (cb40/c690/c5a0) и пишет res_port-записи.
 *
 * Сборка: tools/probes/battle_replay_build.py (MSVC x86, /O2 /fp:precise,
 * те же флаги, что tools/ldhook/build.py; макрос BATTLE_REPLAY включает
 * точки подмены листьев в battle/*.cpp). Запуск:
 *   battle_replay.exe <capture.jsonl> <res_port.jsonl>
 *
 * Точки сравнения — replay_note_* / native::-stub'ы; режим по умолчанию
 * (--real-path) прогоняет порт на реконструированной сетке; --leaf-replay
 * подставляет нативные результаты листьев из захвата.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>

#include "../ldhook/battle/battle.h"
#include "../ldhook/battle/native.h"
#include "../ldhook/ldhook_internal.h"

/* Краш-диагностика (9.c1) — no-op в реплее: этот драйвер не линкует
 * ldhook.cpp, где живут глобалы, поэтому определяем их здесь с нулями.
 * bdd0_harness вместо этого линкует tick.cpp со своими определениями
 * (там же), дублирования между бинарями нет. */
volatile LONG g_crash_on = 0;
volatile LONG g_depth = 0;
volatile unsigned char g_stage = 0;
unsigned char g_stage_stack[LD_CRASH_MAX_STK];

/* --------------------------- минимальный JSON --------------------------- */

static int replay_trace_level(void);      /* определён ниже (логгер) */

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

    void fail(const char *msg) const {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "json: %s at offset %u", msg, (unsigned)p);
        throw std::runtime_error(buf);
    }
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' ||
                                s[p] == '\r' || s[p] == '\n')) p++;
    }
    char peek() {
        ws();
        if (p >= s.size()) fail("unexpected end");
        return s[p];
    }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        p++;
    }
    JVal parseString() {
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
                case 'u': {
                    if (p + 4 > s.size()) fail("bad unicode escape");
                    p += 4; v.s += '?'; break;
                }
                default: fail("bad escape");
                }
            } else {
                v.s += c;
            }
        }
        fail("unterminated string");
        return v;
    }
    JVal parseNum() {
        JVal v; v.t = JVal::NUM;
        size_t st = p;
        if (s[p] == '-' || s[p] == '+') p++;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == st || p == st + 1 && (s[st] == '-' || s[st] == '+')) {
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
    JVal parseValue() {
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
                if (v.o.count(k.s)) fail("duplicate key");
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
    JVal parse() {
        JVal v = parseValue();
        ws();
        if (p != s.size()) fail("trailing data");
        return v;
    }
};

static const JVal &jget(const JVal &v, const char *key, const char *where) {
    if (v.t != JVal::OBJ) {
        throw std::runtime_error(std::string(where) + ": expected object");
    }
    std::map<std::string, JVal>::const_iterator it = v.o.find(key);
    if (it == v.o.end()) {
        throw std::runtime_error(std::string(where) + ": missing '" + key + "'");
    }
    return it->second;
}
static long long jint(const JVal &v, const char *key, const char *where) {
    const JVal &x = jget(v, key, where);
    if (x.t != JVal::NUM) {
        throw std::runtime_error(std::string(where) + "." + key + ": expected number");
    }
    return x.n;
}
static const JVal &jarr(const JVal &v, const char *key, const char *where) {
    const JVal &x = jget(v, key, where);
    if (x.t != JVal::ARR) {
        throw std::runtime_error(std::string(where) + "." + key + ": expected array");
    }
    return x;
}
static bool jisnull(const JVal &v) { return v.t == JVal::NUL; }
static bool jhas(const JVal &v, const char *key) {
    return v.t == JVal::OBJ && v.o.find(key) != v.o.end();
}
static long long jint_opt(const JVal &v, const char *key, long long def) {
    return jhas(v, key) ? jint(v, key, "record") : def;
}
static long long jnum(const JVal &v, const char *where) {
    if (v.t != JVal::NUM) {
        throw std::runtime_error(std::string(where) + ": expected number");
    }
    return v.n;
}

/* ---------------------------- формат захвата ---------------------------- */

struct CapNode {
    int x, y, filter, g, d2, h10k, cost;
    int px, py, occ_key;
    int pos_addr;                   /* LDPos* (ключ map1, identity-гейт) */
    int nbr[9][2];
};
/* v6: native gate inputs captured in the cb40 return trampoline:
 * [0]=out_metric, [1]=final_threshold, [2]=min L[0x10], [3]=max L[0x18]. */
struct GateVals { unsigned v[4]; bool present; };
struct PathLeaf { bool null; int w[5]; int addr; GateVals gate; };

/* v3: значение узла map2 (PathState*) — носитель значений dd70-ветки 2.
 * Занятый ключ без записи значения остаётся NULL (ветка «свободного пути»). */
#define RP_AI_WORDS 32
#define RP_PS_WORDS 22
struct OccVal {
    bool has;
    int value_addr, pos_addr;
    unsigned int w[RP_PS_WORDS];
    int ptr[11];                    /* LDPos*: pos, parent, nbr[9] */
    int pos[2];
    int parent[2];
    int nbr[9][2];
    OccVal() : has(false), value_addr(0), pos_addr(0) {
        memset(w, 0, sizeof(w)); memset(ptr, 0, sizeof(ptr));
        pos[0] = pos[1] = -1;
        parent[0] = parent[1] = -1;
        for (int i = 0; i < 9; i++) { nbr[i][0] = nbr[i][1] = -1; }
    }
};

struct Call {
    JVal cap;                       /* исходная cap-запись */
    int seq, tick, unit, filter, out_ptr;
    int cmp[2];
    int cmp_addr;
    unsigned timeout_bits;
    unsigned timer_bits, final_bits, dist_bits, ctx_flags, world_scale_bits;
    int yrad, xrad, rect[4], anchor[2], trk;
    unsigned bd2;
    int ai_code;
    std::vector<CapNode> map1;
    std::vector<int> occ;
    int version;                    /* cap.v; v1/v2 имеют только occ-ключи */
    bool has_ai_words;
    unsigned int ai_words[RP_AI_WORDS];
    std::map<int, OccVal> occ_vals; /* key -> value (v3) */
    int grid_x, grid_y;
    int w110;                       /* v5: world[0x110] на этом вызове */
    bool tree_overflow, cell_overflow;
    std::vector<std::pair<int, int> > tree_delta;  /* индекс ячейки → статус */
    std::vector<std::vector<int> > cell_delta;     /* idx + 11 полей ячейки */
    std::vector<std::pair<int, int> > row_delta;   /* индекс rows → ячейка */
    std::vector<PathLeaf> leaves_path;
    std::vector<int> leaves_heur, leaves_timeout, leaves_timer;
    /* v7: биты float world[0x120] на входе каждого cb40-вызова + флаг снятия. */
    std::vector<unsigned int> leaves_scale;
    std::vector<int> leaves_scale_ok;
    /* v8: per-cb40 chain snapshot [count, (idx,min,max,key,next,chain,status)*].
     * Empty for v1..v7 captures — the replay then keeps the e0a0-level grid. */
    std::vector< std::vector<int> > leaves_chain;
    /* v5: пор-produced leaf-поток (реальный прогон, без подмены). */
    std::vector<PathLeaf> port_path;
    std::vector<int> port_heur, port_timeout;
    JVal res;
    bool has_res;
    /* v9: per-call FUN_0045eee0 (s6_unit_ai_tick) fields */
    bool is_eee0;
    int eee0_rec_addr, eee0_rec_ok, eee0_rec_w[7];
    int eee0_sub_ok, eee0_sub_w[4];
    int eee0_s68, eee0_s6c, eee0_s28, eee0_s2c;
    int eee0_anchor[2], eee0_s30, eee0_s38;
    unsigned eee0_s34, eee0_ctx_flags, eee0_world_134;
    int eee0_ctx_present, eee0_grid_ok, eee0_grid_w[8];
    int eee0_ret;
    int eee0_s28_after, eee0_s2c_after, eee0_s30_after, eee0_s38_after;
  unsigned eee0_s34_after, eee0_ctx_flags_after;
    int eee0_anchor_addr, eee0_anchor_pos[2], eee0_anchor_f14;
    bool eee0_has_b9, eee0_b9_null;
    int eee0_b9_w[5], eee0_b9_addr;
    unsigned eee0_b9_scale;
    int eee0_b9_scale_ok, eee0_b9_gate[4];
    std::vector<int> eee0_b9_chain;
    std::vector<int> eee0_tree_delta;
    std::vector<std::vector<int> > eee0_cell_delta;
    std::vector<int> eee0_row_delta;
    std::vector<unsigned int> eee0_timer_restart; /* v9: cap.ai.timer_bits per nested e0a0 */
    Call() : is_eee0(false), eee0_rec_addr(0), eee0_rec_ok(0),
             eee0_sub_ok(0), eee0_s68(0), eee0_s6c(0), eee0_s28(0),
             eee0_s2c(0), eee0_s30(-1), eee0_s38(-1), eee0_s34(0),
             eee0_ctx_flags(0), eee0_world_134(0), eee0_ctx_present(0),
             eee0_grid_ok(0), eee0_ret(0), eee0_s28_after(0),
             eee0_s2c_after(0), eee0_s30_after(-1), eee0_s38_after(-1),
             eee0_s34_after(0), eee0_ctx_flags_after(0), eee0_anchor_addr(0),
             eee0_anchor_f14(0), eee0_has_b9(false), eee0_b9_null(true),
             eee0_b9_addr(0), eee0_b9_scale(0), eee0_b9_scale_ok(0) {
        memset(eee0_rec_w, 0, sizeof(eee0_rec_w));
        memset(eee0_sub_w, 0, sizeof(eee0_sub_w));
        eee0_anchor[0] = eee0_anchor[1] = -1;
        memset(rect, 0, sizeof(rect));
        memset(eee0_grid_w, 0, sizeof(eee0_grid_w));
        eee0_anchor_pos[0] = eee0_anchor_pos[1] = -1;
        memset(eee0_b9_w, 0, sizeof(eee0_b9_w));
        memset(eee0_b9_gate, 0, sizeof(eee0_b9_gate));
        version = 9;
        has_ai_words = false;
        w110 = 0;
        tree_overflow = false;
        cell_overflow = false;
    }
};

/* --------------------- v5: реконструкция статической сетки -------------- */
/* Порт читает (grid.cpp + path.cpp + target.cpp):
 *   GridLevel +0x1c next_level, +0x2c/+0x30 размеры, +0x34 cells (шаг 0x34),
 *   +0x38 rows[x][y] = индекс ячейки, +0x20/24/28 scale, +0x0c/+0x10 rec;
 *   GridCell +0x0c/+0x10 координаты (LDPos-раскладка), +0x14/+0x18 метрики,
 *   +0x20 key_rec->key, +0x24 next, +0x28 chain_next, +0x1c level;
 *   world+0x104 tree_lookup(key) → статус, world+0x110/0x120 — гейты/масштаб.
 * Всё это статично на бой, поэтому снимается одной записью `grid`. */
struct RebuiltGrid {
    int sx, sy, n, nlev, leaf;
    GridLevel levels[16];
    GridCell *cells;
    KeyRecord *krecs;
    int **rowptr;
    int *rowdata;
    std::map<unsigned int, int> tree;
    std::vector<int> cell_keys;
    unsigned int w110, vt, a1, obj4, wscale;
    RebuiltGrid() : sx(0), sy(0), n(0), nlev(0), leaf(0), cells(NULL),
                    krecs(NULL), rowptr(NULL), rowdata(NULL),
                    w110(0), vt(0), a1(0), obj4(0), wscale(0) {}
};

static RebuiltGrid *g_grid = NULL;
static int g_grid_error = 0;
/* Синтетические объекты для внешних чтений эвристики. */
static unsigned char g_worldbuf[0x140]; /* world[0x110], world[0x120], world[0x134] */
static unsigned char g_obj3buf[0x100];
static unsigned char g_obj4buf[0x100];
static unsigned char g_obj2buf[0x248];  /* +0x228 = &g_obj3buf; порт читает
                                          * [obj2+0x244] и [obj2+0x228] — буфер
                                          * обязан покрывать оба чтения, иначе
                                          * obj3 зависит от раскладки статики */
static int g_last_grid_x = -1, g_last_grid_y = -1;
static int g_reg_overflow = 0;      /* resource guard в harness stub */

/* Захваченные слова LDPos (+0x00/+0x04/+0x08 из leaf-потока) по адресу носителя:
 * нужны, чтобы носитель ключа был заполнен снятыми значениями, а не нулями. */
struct PosW { int w[5]; PosW() { memset(w, 0, sizeof(w)); } };
std::map<unsigned long long, PosW> g_pos_w;

static void read_pos(const JVal &v, int *xy, const char *where) {
    if (jisnull(v)) { xy[0] = -1; xy[1] = -1; return; }
    if (v.t != JVal::ARR || v.a.size() != 2) {
        throw std::runtime_error(std::string(where) + ": expected [x,y] or null");
    }
    xy[0] = (int)jnum(v.a[0], where);
    xy[1] = (int)jnum(v.a[1], where);
}

static void read_leaves(Call &c, const JVal &res);

/* v9: чтение per-call записей FUN_0045eee0 (`eee0_cap` / `eee0_res`). */
static void read_eee0(Call &c, const JVal &v, bool is_res) {
    c.is_eee0 = true;
    c.has_res = is_res;
    c.res = v;
    c.version = (int)jint_opt(v, "v", 9);
    c.has_ai_words = false;
    if (!is_res) {
        c.w110 = (int)jint_opt(jget(v, "ai", "eee0_cap"), "w110", 0);
        const JVal &rec = jget(v, "rec", "eee0_cap");
        c.eee0_rec_addr = (int)jint_opt(rec, "addr", 0);
        c.eee0_rec_ok = (int)jint_opt(rec, "ok", 0);
        const JVal &rw = jarr(rec, "w", "eee0_cap.rec");
        for (size_t i = 0; i < 7 && i < rw.a.size(); i++) {
            c.eee0_rec_w[i] = (int)jnum(rw.a[i], "eee0_cap.rec.w");
        }
        c.eee0_sub_ok = (int)jint_opt(rec, "sub_ok", 0);
        const JVal &rs = jarr(rec, "sub", "eee0_cap.rec");
        for (size_t i = 0; i < 4 && i < rs.a.size(); i++) {
            c.eee0_sub_w[i] = (int)jnum(rs.a[i], "eee0_cap.rec.sub");
        }
        const JVal &ai = jget(v, "ai", "eee0_cap");
        c.eee0_s68 = (int)jint(ai, "s68", "eee0_cap.ai");
        c.eee0_s6c = (int)jint(ai, "s6c", "eee0_cap.ai");
        c.eee0_s28 = (int)jint(ai, "s28", "eee0_cap.ai");
        c.eee0_s2c = (int)jint(ai, "s2c", "eee0_cap.ai");
        read_pos(jget(ai, "anchor", "eee0_cap.ai"), c.eee0_anchor, "eee0_cap.ai.anchor");
        c.eee0_s30 = (int)jint(ai, "s30", "eee0_cap.ai");
        c.eee0_s34 = (unsigned)jint(ai, "s34", "eee0_cap.ai");
        c.eee0_s38 = (int)jint(ai, "s38", "eee0_cap.ai");
        c.eee0_ctx_present = (int)jint(ai, "ctx_present", "eee0_cap.ai");
        c.eee0_ctx_flags = (unsigned)jint(ai, "ctx_flags", "eee0_cap.ai");
        c.ctx_flags = (unsigned)c.eee0_ctx_flags;
        c.eee0_world_134 = (int)jint_opt(ai, "world_134", 0);
        c.eee0_grid_ok = (int)jint_opt(ai, "grid_ok", 0);
        const JVal &gw = jarr(ai, "grid", "eee0_cap.ai");
        for (size_t i = 0; i < 8 && i < gw.a.size(); i++) {
            c.eee0_grid_w[i] = (int)jnum(gw.a[i], "eee0_cap.ai.grid");
        }
    } else {
        c.eee0_ret = (int)jint(v, "ret", "eee0_res");
        const JVal &ai = jget(v, "ai", "eee0_res");
        c.eee0_s28_after = (int)jint(ai, "s28", "eee0_res.ai");
        c.eee0_s2c_after = (int)jint(ai, "s2c", "eee0_res.ai");
        c.eee0_s30_after = (int)jint(ai, "s30", "eee0_res.ai");
        c.eee0_s34_after = (unsigned)jint(ai, "s34", "eee0_res.ai");
        c.eee0_s38_after = (int)jint(ai, "s38", "eee0_res.ai");
        c.eee0_ctx_flags_after = (unsigned)jint_opt(ai, "ctx_flags", 0);
        const JVal &anc = jget(v, "anchor", "eee0_res");
        c.eee0_anchor_addr = (int)jint_opt(anc, "addr", 0);
        read_pos(jget(anc, "pos", "eee0_res.anchor"), c.eee0_anchor_pos,
                 "eee0_res.anchor.pos");
        c.eee0_anchor_f14 = (int)jint_opt(anc, "f14", 0);
        if (jhas(v, "b9_leaf") && !jisnull(jget(v, "b9_leaf", "eee0_res"))) {
            const JVal &b9 = jget(v, "b9_leaf", "eee0_res");
            PathLeaf pl;
            memset(&pl, 0, sizeof(pl));
            pl.null = (jint_opt(b9, "null", 1) != 0);
            if (!pl.null) {
                const JVal &w = jarr(b9, "w", "eee0_res.b9_leaf");
                for (size_t k = 0; k < 5 && k < w.a.size(); k++) {
                    pl.w[k] = (int)jnum(w.a[k], "eee0_res.b9_leaf.w");
                }
                pl.addr = (int)jint_opt(b9, "ptr", 0);
            }
            c.leaves_path.push_back(pl);
            c.leaves_scale.push_back((unsigned)jint_opt(b9, "scale", 0));
            c.leaves_scale_ok.push_back((int)jint_opt(b9, "scale_ok", 0));
            std::vector<int> ch;
            if (jhas(b9, "chain")) {
                const JVal &ca = jget(b9, "chain", "eee0_res.b9_leaf");
                for (size_t k = 0; k < ca.a.size(); k++) {
                    ch.push_back((int)jnum(ca.a[k], "eee0_res.b9_leaf.chain"));
                }
            }
            c.leaves_chain.push_back(ch);
            c.eee0_has_b9 = true;
            c.eee0_b9_null = pl.null;
            for (int k = 0; k < 5; k++) c.eee0_b9_w[k] = pl.w[k];
            c.eee0_b9_addr = pl.addr;
            c.eee0_b9_scale = c.leaves_scale.back();
            c.eee0_b9_scale_ok = c.leaves_scale_ok.back();
            if (jhas(b9, "gate")) {
                const JVal &ga = jget(b9, "gate", "eee0_res.b9_leaf");
                for (size_t k = 0; k < 4 && k < ga.a.size(); k++) {
                    c.eee0_b9_gate[k] = (int)jnum(ga.a[k], "eee0_res.b9_leaf.gate");
                }
            }
            c.eee0_b9_chain = ch;
        }
    }
}

static void read_call(Call &c, const JVal &cap, const JVal *res) {
    const JVal &args = jget(cap, "args", "cap");
    const JVal &ai = jget(cap, "ai", "cap");

    c.version = (int)jint_opt(cap, "v", 1);
    c.w110 = (int)jint_opt(cap, "w110", 0);
    c.tree_overflow = (jint_opt(cap, "tree_overflow", 0) != 0);
    c.cell_overflow = (jint_opt(cap, "cell_overflow", 0) != 0);
    if (jhas(cap, "cell_delta")) {
        const JVal &cd = jarr(cap, "cell_delta", "cap.cell_delta");
        for (size_t i = 0; i < cd.a.size(); i++) {
            const JVal &e = cd.a[i];
            if (e.t != JVal::ARR || e.a.size() != 12) {
                throw std::runtime_error("cap.cell_delta: expected 12 ints");
            }
            std::vector<int> row(12);
            for (size_t k = 0; k < 12; k++) row[k] = (int)jnum(e.a[k], "cell_delta");
            c.cell_delta.push_back(row);
        }
    }
    if (jhas(cap, "row_delta")) {
        const JVal &rd = jarr(cap, "row_delta", "cap.row_delta");
        for (size_t i = 0; i < rd.a.size(); i++) {
            const JVal &e = rd.a[i];
            if (e.t != JVal::ARR || e.a.size() != 2) {
                throw std::runtime_error("cap.row_delta: expected [idx,cell]");
            }
            c.row_delta.push_back(std::make_pair((int)jnum(e.a[0], "row_delta"),
                                                  (int)jnum(e.a[1], "row_delta")));
        }
    }
    if (jhas(cap, "tree_delta")) {
        const JVal &td = jarr(cap, "tree_delta", "cap.tree_delta");
        for (size_t i = 0; i < td.a.size(); i++) {
            const JVal &e = td.a[i];
            if (e.t != JVal::ARR || e.a.size() != 2) {
                throw std::runtime_error("cap.tree_delta: expected [idx,status]");
            }
            c.tree_delta.push_back(std::make_pair((int)jnum(e.a[0], "tree_delta"),
                                                  (int)jnum(e.a[1], "tree_delta")));
        }
    }
    c.has_ai_words = false;
    memset(c.ai_words, 0, sizeof(c.ai_words));
    c.seq = (int)jint(cap, "seq", "cap");
    c.tick = (int)jint(cap, "tick", "cap");
    c.unit = (int)jint(cap, "unit", "cap");
    c.filter = (int)jint(args, "filter", "cap.args");
    read_pos(jget(args, "cmp", "cap.args"), c.cmp, "cap.args.cmp");
    c.cmp_addr = (int)jint_opt(args, "cmp_addr", 0);
    c.timeout_bits = (unsigned)jint(args, "timeout_bits", "cap.args");
    c.out_ptr = (int)jint(args, "out_ptr", "cap.args");

    c.timer_bits = (unsigned)jint(ai, "timer_bits", "cap.ai");
    c.final_bits = (unsigned)jint(ai, "final_bits", "cap.ai");
    c.dist_bits = (unsigned)jint(ai, "dist_bits", "cap.ai");
    c.yrad = (int)jint(ai, "yrad", "cap.ai");
    c.xrad = (int)jint(ai, "xrad", "cap.ai");
    const JVal &rect = jarr(ai, "rect", "cap.ai");
    if (rect.a.size() != 4) throw std::runtime_error("cap.ai.rect: expected 4 ints");
    for (int i = 0; i < 4; i++) c.rect[i] = (int)jnum(rect.a[i], "cap.ai.rect");
    read_pos(jget(ai, "anchor", "cap.ai"), c.anchor, "cap.ai.anchor");
    c.trk = (int)jint(ai, "trk", "cap.ai");
    c.bd2 = (unsigned)jint(ai, "bd2", "cap.ai");
    c.ai_code = (int)jint(ai, "code", "cap.ai");
    c.ctx_flags = (unsigned)jint(ai, "ctx_flags", "cap.ai");
    c.world_scale_bits = (unsigned)jint(ai, "world_scale_bits", "cap.ai");

    const JVal &m1 = jarr(cap, "map1", "cap");
    for (size_t i = 0; i < m1.a.size(); i++) {
        const JVal &e = m1.a[i];
        CapNode n;
        memset(&n, 0, sizeof(n));
        read_pos(jget(e, "pos", "cap.map1"), &n.x, "cap.map1.pos");
        n.filter = (int)jint(e, "filter", "cap.map1");
        n.g = (int)jint(e, "g", "cap.map1");
        n.d2 = (int)jint(e, "d2", "cap.map1");
        n.h10k = (int)jint(e, "h10k", "cap.map1");
        n.cost = (int)jint(e, "cost", "cap.map1");
        read_pos(jget(e, "parent", "cap.map1"), &n.px, "cap.map1.parent");
        n.occ_key = (int)jint(e, "occ_key", "cap.map1");
        n.pos_addr = (int)jint_opt(e, "pos_addr", 0);
        const JVal &nb = jarr(e, "nbr", "cap.map1");
        for (int k = 0; k < 9 && k < (int)nb.a.size(); k++) {
            read_pos(nb.a[k], n.nbr[k], "cap.map1.nbr");
        }
        c.map1.push_back(n);
    }
    const JVal &occ = jarr(cap, "occ", "cap");
    for (size_t i = 0; i < occ.a.size(); i++) {
        c.occ.push_back((int)jnum(occ.a[i], "cap.occ"));
    }
    /* v3: полный скалярный блок BattleAI и значения узлов map2. Их отсутствие
     * в v1/v2 — норма (старые захваты), поэтому всё под jhas(). */
    if (c.version >= 3 && jhas(cap, "ai_words")) {
        const JVal &aw = jarr(cap, "ai_words", "cap");
        if (aw.a.size() != RP_AI_WORDS) {
            throw std::runtime_error("cap.ai_words: expected 32 dwords");
        }
        for (size_t i = 0; i < aw.a.size(); i++) {
            c.ai_words[i] = (unsigned int)(int)jnum(aw.a[i], "cap.ai_words");
        }
        c.has_ai_words = true;
    }
    if (c.version >= 3 && jhas(cap, "occ_nodes")) {
        const JVal &ons = jarr(cap, "occ_nodes", "cap");
        if (ons.a.size() != occ.a.size()) {
            throw std::runtime_error("cap.occ_nodes: size differs from occ");
        }
        for (size_t i = 0; i < ons.a.size(); i++) {
            const JVal &o = ons.a[i];
            OccVal v;
            int key = (int)jint(o, "key", "cap.occ_nodes");
            const JVal &val = jget(o, "value", "cap.occ_nodes");
            if (!jisnull(val)) {
                const JVal &w = jarr(val, "w", "cap.occ_nodes.value");
                const JVal &pt = jarr(val, "ptr", "cap.occ_nodes.value");
                const JVal &po = jarr(val, "pos", "cap.occ_nodes.value");
                const JVal &pa = jarr(val, "parent", "cap.occ_nodes.value");
                const JVal &nb = jarr(val, "nbr", "cap.occ_nodes.value");
                if (w.a.size() != RP_PS_WORDS || pt.a.size() != 11 ||
                    po.a.size() != 2 || pa.a.size() != 2 || nb.a.size() != 9) {
                    throw std::runtime_error("cap.occ_nodes.value: bad sizes");
                }
                v.has = true;
                v.value_addr = (int)jint(val, "value_addr", "cap.occ_nodes.value");
                v.pos_addr = (int)jint(val, "pos_addr", "cap.occ_nodes.value");
                for (size_t k = 0; k < w.a.size(); k++) {
                    v.w[k] = (unsigned int)(int)jnum(w.a[k], "cap.occ_nodes.value.w");
                }
                for (size_t k = 0; k < pt.a.size(); k++) {
                    v.ptr[k] = (int)jnum(pt.a[k], "cap.occ_nodes.value.ptr");
                }
                for (int k = 0; k < 2; k++) {
                    v.pos[k] = (int)jnum(po.a[k], "cap.occ_nodes.value.pos");
                    v.parent[k] = (int)jnum(pa.a[k], "cap.occ_nodes.value.parent");
                }
                for (size_t k = 0; k < nb.a.size(); k++) {
                    read_pos(nb.a[k], v.nbr[k], "cap.occ_nodes.value.nbr");
                }
            }
            c.occ_vals[key] = v;
        }
    }
    const JVal &grid = jarr(cap, "grid", "cap");
    c.grid_x = c.grid_y = 0;
    if (!grid.a.empty()) {
        c.grid_x = (int)jint(grid.a[0], "maxrow", "cap.grid");
        c.grid_y = (int)jint(grid.a[0], "maxcol", "cap.grid");
    }
    c.has_res = (res != NULL);
    if (res != NULL) {
        c.res = *res;
        read_leaves(c, *res);
    }
}

static void read_eee0_state(Call &c, const JVal &cap) {
    const JVal &ai = jget(cap, "ai", "eee0_cap");
    c.timer_bits = 0;
    c.final_bits = (unsigned)jint_opt(ai, "ctx_flags", 0);
    c.dist_bits = 0;
    c.yrad = 3;
    c.xrad = 3;
    for (int i = 0; i < 4; i++) c.rect[i] = 0;
    c.world_scale_bits = (g_grid && !g_grid_error) ? g_grid->wscale : 0;
    c.has_ai_words = false;
    /* v9: eee0_cap carries the same raw BattleAI block as e0a0 cap. Use it
     * for the scalar fields the port reads, so rep_build/rep_rebind start
     * from the native state instead of zero defaults. */
    if (c.version >= 3 && jhas(cap, "ai_words")) {
        const JVal &aw = jarr(cap, "ai_words", "eee0_cap");
        if (aw.a.size() == RP_AI_WORDS) {
            for (size_t i = 0; i < aw.a.size(); i++) {
                c.ai_words[i] = (unsigned int)(int)jnum(aw.a[i], "eee0_cap.ai_words");
            }
            c.has_ai_words = true;
            c.timer_bits = c.ai_words[1];
            c.final_bits = c.ai_words[2];
            c.dist_bits = c.ai_words[3];
            c.yrad = (int)c.ai_words[4];
            c.xrad = (int)c.ai_words[5];
            for (int i = 0; i < 4; i++) c.rect[i] = (int)c.ai_words[6 + i];
            c.trk = (int)(c.ai_words[12] & 0xffu);
            c.bd2 = c.ai_words[13];
            c.ai_code = (int)c.ai_words[14];
        }
    }
    const JVal &m1 = jarr(cap, "map1", "eee0_cap");
    for (size_t i = 0; i < m1.a.size(); i++) {
        const JVal &e = m1.a[i];
        CapNode n;
        memset(&n, 0, sizeof(n));
        read_pos(jget(e, "pos", "cap.map1"), &n.x, "cap.map1.pos");
        n.filter = (int)jint(e, "filter", "cap.map1");
        n.g = (int)jint(e, "g", "cap.map1");
        n.d2 = (int)jint(e, "d2", "cap.map1");
        n.h10k = (int)jint(e, "h10k", "cap.map1");
        n.cost = (int)jint(e, "cost", "cap.map1");
        read_pos(jget(e, "parent", "cap.map1"), &n.px, "cap.map1.parent");
        n.occ_key = (int)jint(e, "occ_key", "cap.map1");
        n.pos_addr = (int)jint_opt(e, "pos_addr", 0);
        const JVal &nb = jarr(e, "nbr", "cap.map1");
        for (int k = 0; k < 9 && k < (int)nb.a.size(); k++) {
            read_pos(nb.a[k], n.nbr[k], "cap.map1.nbr");
        }
        c.map1.push_back(n);
    }
    const JVal &occ = jarr(cap, "occ", "eee0_cap");
    for (size_t i = 0; i < occ.a.size(); i++) {
        c.occ.push_back((int)jnum(occ.a[i], "cap.occ"));
    }
    if (jhas(cap, "occ_nodes")) {
        const JVal &ons = jarr(cap, "occ_nodes", "cap");
        for (size_t i = 0; i < ons.a.size(); i++) {
            const JVal &o = ons.a[i];
            OccVal v;
            int key = (int)jint(o, "key", "cap.occ_nodes");
            const JVal &val = jget(o, "value", "cap.occ_nodes");
            if (!jisnull(val)) {
                const JVal &w = jarr(val, "w", "cap.occ_nodes.value");
                const JVal &pt = jarr(val, "ptr", "cap.occ_nodes.value");
                const JVal &po = jarr(val, "pos", "cap.occ_nodes.value");
                const JVal &pa = jarr(val, "parent", "cap.occ_nodes.value");
                const JVal &nb = jarr(val, "nbr", "cap.occ_nodes.value.nbr");
                v.has = true;
                v.value_addr = (int)jint(val, "value_addr", "cap.occ_nodes.value");
                v.pos_addr = (int)jint(val, "pos_addr", "cap.occ_nodes.value");
                for (size_t k = 0; k < w.a.size(); k++) {
                    v.w[k] = (unsigned int)(int)jnum(w.a[k], "cap.occ_nodes.value.w");
                }
                for (size_t k = 0; k < pt.a.size(); k++) {
                    v.ptr[k] = (int)jnum(pt.a[k], "cap.occ_nodes.value.ptr");
                }
                for (int k = 0; k < 2; k++) {
                    v.pos[k] = (int)jnum(po.a[k], "cap.occ_nodes.value.pos");
                    v.parent[k] = (int)jnum(pa.a[k], "cap.occ_nodes.value.parent");
                }
                for (size_t k = 0; k < nb.a.size(); k++) {
                    read_pos(nb.a[k], v.nbr[k], "cap.occ_nodes.value.nbr");
                }
            }
            c.occ_vals[key] = v;
        }
    }
    if (c.version >= 3 && jhas(cap, "ai_words")) {
        const JVal &aw = jarr(cap, "ai_words", "cap");
        if (aw.a.size() == RP_AI_WORDS) {
            for (size_t i = 0; i < aw.a.size(); i++) {
                c.ai_words[i] = (unsigned int)(int)jnum(aw.a[i], "cap.ai_words");
            }
            c.has_ai_words = true;
        }
    }
}

static void read_leaves(Call &c, const JVal &res) {
    {
        const JVal &leaf = jget(res, "leaf", "res");
        const JVal &lp = jarr(leaf, "path", "res.leaf");
        const JVal *ptr = jhas(leaf, "ptr") ? &jget(leaf, "ptr", "res.leaf") : NULL;
        if (ptr != NULL && ptr->t != JVal::ARR) {
            ptr = NULL;
        }
        /* v6: gate stream is optional (v1..v5 captures have no such key). */
        const JVal *garr = jhas(leaf, "gate") ? &jget(leaf, "gate", "res.leaf") : NULL;
        if (garr != NULL && garr->t != JVal::ARR) {
            garr = NULL;
        }
        for (size_t i = 0; i < lp.a.size(); i++) {
            PathLeaf pl;
            memset(&pl, 0, sizeof(pl));
            if (garr != NULL && i < garr->a.size() &&
                (*garr).a[i].t == JVal::ARR) {
                const JVal &ge = garr->a[i];
                for (size_t k = 0; k < 4 && k < ge.a.size(); k++) {
                    pl.gate.v[k] = (unsigned)(int)jnum(ge.a[k], "res.leaf.gate");
                }
                pl.gate.present = true;
            }
            pl.null = jisnull(lp.a[i]);
            if (!pl.null) {
                if (lp.a[i].t != JVal::ARR) {
                    throw std::runtime_error("res.leaf.path: expected array or null");
                }
                for (size_t k = 0; k < 5 && k < lp.a[i].a.size(); k++) {
                    pl.w[k] = (int)jnum(lp.a[i].a[k], "res.leaf.path");
                }
            }
            if (ptr != NULL && i < ptr->a.size()) {
                pl.addr = (int)jnum(ptr->a[i], "res.leaf.ptr");
                if (pl.addr == 0) pl.null = true;   /* NULL-маркер указателя */
            } else if (!pl.null) {
                throw std::runtime_error("res.leaf.path: 'ptr' missing "
                                         "(capture without LDPos addresses)");
            }
            if (!pl.null && pl.addr != 0) {
                PosW pw;
                for (int k = 0; k < 5; k++) pw.w[k] = pl.w[k];
                g_pos_w[(unsigned long long)(unsigned)pl.addr] = pw;
            }
            c.leaves_path.push_back(pl);
        }
        const JVal &lh = jarr(leaf, "heur", "res.leaf");
        for (size_t i = 0; i < lh.a.size(); i++) {
            c.leaves_heur.push_back((int)jnum(lh.a[i], "res.leaf.heur"));
        }
        const JVal &lt = jarr(leaf, "timeout", "res.leaf");
        for (size_t i = 0; i < lt.a.size(); i++) {
            c.leaves_timeout.push_back((int)jnum(lt.a[i], "res.leaf.timeout"));
        }
        if (jhas(leaf, "timer")) {
            const JVal &lm = jarr(leaf, "timer", "res.leaf");
            for (size_t i = 0; i < lm.a.size(); i++) {
                c.leaves_timer.push_back((int)jnum(lm.a[i], "res.leaf.timer"));
            }
        }
        /* v7: scale/scale_ok — опциональные ключи (v1..v6 их не пишут). */
        if (jhas(leaf, "scale") && jhas(leaf, "scale_ok")) {
            const JVal &ls = jarr(leaf, "scale", "res.leaf");
            const JVal &lok = jarr(leaf, "scale_ok", "res.leaf");
            for (size_t i = 0; i < ls.a.size(); i++) {
                c.leaves_scale.push_back(
                    (unsigned int)(int)jnum(ls.a[i], "res.leaf.scale"));
            }
            for (size_t i = 0; i < lok.a.size(); i++) {
                c.leaves_scale_ok.push_back(
                    (int)jnum(lok.a[i], "res.leaf.scale_ok"));
            }
        }
        /* v8: optional per-cb40 chain state (old captures have no key). */
        if (jhas(leaf, "chain")) {
            const JVal &ch = jarr(leaf, "chain", "res.leaf.chain");
            for (size_t i = 0; i < ch.a.size(); i++) {
                std::vector<int> rec;
                if (ch.a[i].t == JVal::ARR) {
                    for (size_t k = 0; k < ch.a[i].a.size(); k++) {
                        rec.push_back((int)jnum(ch.a[i].a[k], "res.leaf.chain"));
                    }
                }
                c.leaves_chain.push_back(rec);
            }
        }
    }
}

/* v5: построение сетки из записи `grid`. Адреса ячеек и рядов фиксируются
 * один раз на прогон — указатели из GridCell/GridLevel живут до его конца. */
static void replay_grid_build(const JVal &v)
{
    RebuiltGrid *g = new RebuiltGrid;
    g_grid = g;
    g_grid_error = 0;
    if (jint_opt(v, "error", 0) != 0) { g_grid_error = 1; return; }
    g->sx = (int)jint(v, "sx", "grid");
    g->sy = (int)jint(v, "sy", "grid");
    g->leaf = (int)jint_opt(v, "leaf", 0);
    if (g->sx <= 0 || g->sy <= 0 ||
        (long long)g->sx * g->sy > 2000000LL) {
        g_grid_error = 1; return;
    }
    g->n = g->sx * g->sy;

    const JVal &lv = jarr(v, "levels", "grid");
    if (lv.a.empty() || lv.a.size() > 16) { g_grid_error = 1; return; }
    g->nlev = (int)lv.a.size();
    for (size_t i = 0; i < lv.a.size(); i++) {
        const JVal &e = lv.a[i];
        GridLevel *l = &g->levels[i];
        memset(l, 0, sizeof(*l));
        l->rec_x = (int)jint(e, "rx", "grid.levels");
        l->rec_y = (int)jint(e, "ry", "grid.levels");
        unsigned int sa = (unsigned)jint(e, "sa", "grid.levels");
        unsigned int sl = (unsigned)jint(e, "sl", "grid.levels");
        unsigned int sm = (unsigned)jint(e, "sm", "grid.levels");
        memcpy(&l->scale_add, &sa, 4);
        memcpy(&l->scale_load, &sl, 4);
        memcpy(&l->scale_mul, &sm, 4);
        l->size_x = (int)jint(e, "sx", "grid.levels");
        l->size_y = (int)jint(e, "sy", "grid.levels");
        int nx = (int)jint(e, "next", "grid.levels");
        l->next_level = (nx >= 0 && nx < (int)lv.a.size())
                        ? &g->levels[nx] : NULL;
    }
    const JVal &w = jget(v, "world", "grid");
    g->w110 = (unsigned)jint(w, "w110", "grid.world");
    g->vt = (unsigned)jint(w, "vt", "grid.world");
    g->a1 = (unsigned)jint(w, "a1", "grid.world");
    g->obj4 = (unsigned)jint(w, "obj4", "grid.world");
    g->wscale = (unsigned)jint(w, "wscale", "grid.world");

    g->cells = new GridCell[g->n];
    g->krecs = new KeyRecord[g->n];
    g->cell_keys.resize(g->n);
    g->rowptr = new int *[g->sx];
    g->rowdata = new int[g->n];
    memset(g->cells, 0, sizeof(GridCell) * g->n);
    memset(g->krecs, 0, sizeof(KeyRecord) * g->n);
    memset(g->rowptr, 0, sizeof(int *) * g->sx);
    for (int x = 0; x < g->sx; x++) g->rowptr[x] = &g->rowdata[x * g->sy];

    const JVal &cs = jget(v, "cells", "grid");
    const JVal &ck = jarr(cs, "key", "grid.cells");
    const JVal &ct = jarr(cs, "status", "grid.cells");
    const JVal &cn = jarr(cs, "min", "grid.cells");
    const JVal &cx = jarr(cs, "max", "grid.cells");
    const JVal &cne = jarr(cs, "next", "grid.cells");
    const JVal &cch = jarr(cs, "chain", "grid.cells");
    const JVal *cw0 = jhas(cs, "w0") ? &jarr(cs, "w0", "grid.cells") : NULL;
    const JVal *cw1 = jhas(cs, "w1") ? &jarr(cs, "w1", "grid.cells") : NULL;
    const JVal *cw2 = jhas(cs, "w2") ? &jarr(cs, "w2", "grid.cells") : NULL;
    const JVal *cw3 = jhas(cs, "w3") ? &jarr(cs, "w3", "grid.cells") : NULL;
    const JVal *cw4 = jhas(cs, "w4") ? &jarr(cs, "w4", "grid.cells") : NULL;
    if (cw0 == NULL || cw1 == NULL || cw2 == NULL || cw3 == NULL || cw4 == NULL) {
        g_grid_error = 1; return;               /* нужен полный LDPos-вид ячейки */
    }
    if (ck.a.size() != (size_t)g->n || ct.a.size() != (size_t)g->n ||
        cn.a.size() != (size_t)g->n || cx.a.size() != (size_t)g->n ||
        cne.a.size() != (size_t)g->n || cch.a.size() != (size_t)g->n ||
        (cw0 != NULL && cw0->a.size() != (size_t)g->n) ||
        (cw1 != NULL && cw1->a.size() != (size_t)g->n) ||
        (cw2 != NULL && cw2->a.size() != (size_t)g->n) ||
        (cw3 != NULL && cw3->a.size() != (size_t)g->n) ||
        (cw4 != NULL && cw4->a.size() != (size_t)g->n)) {
        g_grid_error = 1; return;
    }
    for (int i = 0; i < g->n; i++) {
        GridCell *c = &g->cells[i];
        /* Первые 5 dword ячейки (в т.ч. LDPos.x/+0x0c, y/+0x10) снимаются
         * с натива: индекс в cells НЕ совпадает с координатами ячейки. */
        for (int k = 0; k < 5; k++) {
            const JVal *wv = (k == 0) ? cw0 : (k == 1) ? cw1 :
                             (k == 2) ? cw2 : (k == 3) ? cw3 : cw4;
            int w = (wv != NULL) ? (int)jnum(wv->a[i], "grid.cells.w")
                                 : (k >= 3 ? (k == 3 ? i / g->sy : i % g->sy) : 0);
            *(int *)((unsigned char *)c + k * 4) = w;
        }
        *(void **)((unsigned char *)c + 0x1c) = (void *)&g->levels[g->leaf];
        int mn = (int)jnum(cn.a[i], "grid.cells.min");
        int mx = (int)jnum(cx.a[i], "grid.cells.max");
        memcpy(&c->min_metric, &mn, 4);
        memcpy(&c->max_metric, &mx, 4);
        g->krecs[i].key = (int)jnum(ck.a[i], "grid.cells.key");
        g->cell_keys[i] = g->krecs[i].key;
        c->key_rec = &g->krecs[i];
        int ni = (int)jnum(cne.a[i], "grid.cells.next");
        int ci = (int)jnum(cch.a[i], "grid.cells.chain");
        c->next = (ni >= 0 && ni < g->n) ? &g->cells[ni] : NULL;
        c->chain_next = (ci >= 0 && ci < g->n) ? &g->cells[ci] : NULL;
        g->tree[(unsigned int)g->krecs[i].key] =
            (int)jnum(ct.a[i], "grid.cells.status");
    }
    const JVal &rw = jarr(v, "rows", "grid");
    if (rw.a.size() != (size_t)g->n) { g_grid_error = 1; return; }
    for (int i = 0; i < g->n; i++) g->rowdata[i] = (int)jnum(rw.a[i], "grid.rows");

    GridLevel *leaf = &g->levels[g->leaf];
    leaf->cells = g->cells;
    leaf->rows = g->rowptr;
    /* Порт читает размеры из root (ctx+8) — в захвате root == leaf. */
    for (int i = 0; i < g->nlev; i++) {
        if (g->levels[i].cells == NULL) {
            g->levels[i].cells = leaf->cells;
            g->levels[i].rows = leaf->rows;
        }
    }
    memset(g_worldbuf, 0, sizeof(g_worldbuf));
    g_worldbuf[0x110] = (unsigned char)g->w110;
    memcpy(g_worldbuf + 0x120, &g->wscale, 4);
    memset(g_obj3buf, 0, sizeof(g_obj3buf));
    memset(g_obj2buf, 0, sizeof(g_obj2buf));
    memset(g_obj4buf, 0, sizeof(g_obj4buf));
    *(int *)(g_obj2buf + 0x228) = (int)(size_t)g_obj3buf;
    memcpy(g_obj4buf + 0x34, &g->obj4, 4);
}

/* Применение дельты ячейки: w0..w4, min, max, key, next, chain, level. */
static void replay_apply_cell_delta(RebuiltGrid *g, const std::vector<int> &row)
{
    int i = row[0];
    if (g == NULL || g_grid_error || i < 0 || i >= g->n) return;
    GridCell *c = &g->cells[i];
    for (int k = 0; k < 5; k++) {
        *(int *)((unsigned char *)c + k * 4) = row[1 + k];
    }
    memcpy(&c->min_metric, &row[6], 4);
    memcpy(&c->max_metric, &row[7], 4);
    unsigned int oldkey = (unsigned int)g->krecs[i].key;
    unsigned int newkey = (unsigned int)row[8];
    g->krecs[i].key = row[8];
    g->cell_keys[i] = row[8];
    if (oldkey != newkey) {
        std::map<unsigned int, int>::const_iterator it = g->tree.find(oldkey);
        int st = (it == g->tree.end()) ? 0 : it->second;
        g->tree.erase(oldkey);
        g->tree[newkey] = st;
    }
    int ni = row[9], ci = row[10];
    c->next = (ni >= 0 && ni < g->n) ? &g->cells[ni] : NULL;
    c->chain_next = (ci >= 0 && ci < g->n) ? &g->cells[ci] : NULL;
    /* +0x1c: уровень ячейки — всегда синтетический лист реконструкции. */
    *(void **)((unsigned char *)c + 0x1c) = (void *)&g->levels[g->leaf];
}

/* --------------- chained mode: сравнение результата и after-state -------- */
/* Обычный режим: реплика строится заново из cap каждого вызова, сравнение
 * делает battle_replay_diff.py. Chained mode: реплика живёт между вызовами,
 * поэтому сравнение (result + after-state) делается здесь, сразу после
 * каждого схода порта. Класс MUTATE = результат совпал, а состояние нет. */

struct RegEvent {
    int seq, x, y, pos_addr, filter, g, cost;
};
static std::vector<RegEvent> g_reg_events;   /* единственный источник новых узлов map1 */
#define RP_REG_EVENT_MAX 100000              /* потолок накопленных reg-событий (--chained) */

struct AStateNode {
    int x, y, pos_addr;
    int filter, g, d2, h10k, cost, px, py;
};
struct AState {
    std::vector<AStateNode> map1;
    std::vector<int> occ;
    bool has_ai;
    unsigned int ai[RP_AI_WORDS];
    AState() : has_ai(false) { memset(ai, 0, sizeof(ai)); }
};

static void after_from_res(const JVal &res, AState &out) {
    if (jhas(res, "map1_after")) {
        const JVal &m = jarr(res, "map1_after", "res.map1_after");
        for (size_t i = 0; i < m.a.size(); i++) {
            const JVal &e = m.a[i];
            AStateNode n;
            memset(&n, 0, sizeof(n));
            read_pos(jget(e, "pos", "res.map1_after"), &n.x, "res.map1_after.pos");
            n.filter = (int)jint(e, "filter", "res.map1_after");
            n.g = (int)jint(e, "g", "res.map1_after");
            n.d2 = (int)jint(e, "d2", "res.map1_after");
            n.h10k = (int)jint(e, "h10k", "res.map1_after");
            n.cost = (int)jint(e, "cost", "res.map1_after");
            read_pos(jget(e, "parent", "res.map1_after"), &n.px,
                     "res.map1_after.parent");
            n.pos_addr = (int)jint_opt(e, "pos_addr", 0);
            out.map1.push_back(n);
        }
    }
    if (jhas(res, "occ_after")) {
        const JVal &o = jarr(res, "occ_after", "res.occ_after");
        for (size_t i = 0; i < o.a.size(); i++) {
            out.occ.push_back((int)jnum(o.a[i], "res.occ_after"));
        }
    }
    /* После-состояние скалярного блока BattleAI захватывается не всегда;
     * если оно есть — сравниваем, игнорируя слоты-указатели (см. diff_after). */
    const char *awkeys[2] = {"ai_words_after", "ai_words"};
    for (int k = 0; k < 2; k++) {
        if (!jhas(res, awkeys[k])) continue;
        const JVal &aw = jarr(res, awkeys[k], "res.ai_words");
        if (aw.a.size() != RP_AI_WORDS) {
            throw std::runtime_error("res.ai_words: expected 32 dwords");
        }
        for (size_t i = 0; i < aw.a.size(); i++) {
            out.ai[i] = (unsigned int)(int)jnum(aw.a[i], "res.ai_words");
        }
        out.has_ai = true;
        break;
    }
}

struct NResult {
    int nx, ny, code, iters;
    std::vector<int> seq;
};
static void nres_read(const JVal &res, NResult &out) {
    read_pos(jget(res, "node", "res"), &out.nx, "res.node");
    out.code = (int)jint(res, "code", "res");
    out.iters = (int)jint(res, "iters", "res");
    const JVal &sq = jarr(res, "iter_seq", "res");
    for (size_t i = 0; i < sq.a.size(); i++) {
        int xy[2];
        read_pos(sq.a[i], xy, "res.iter_seq");
        out.seq.push_back(xy[0]);
        out.seq.push_back(xy[1]);
    }
}

static bool result_equal(const NResult &a, int px, int py, int code, int iters,
                         const std::vector<int> &pseq, std::string &why) {
    if (a.nx != px || a.ny != py) {
        char b[128];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "node native=[%d,%d] port=[%d,%d]",
                    a.nx, a.ny, px, py);
        why = b;
        return false;
    }
    if (a.code != code) {
        char b[128];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "code native=%d port=%d", a.code, code);
        why = b;
        return false;
    }
    if (a.iters != iters) {
        char b[128];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "iters native=%d port=%d",
                    a.iters, iters);
        why = b;
        return false;
    }
    if (a.seq != pseq) {
        size_t i = 0;
        while (i < a.seq.size() && i < pseq.size() && a.seq[i] == pseq[i]) i++;
        char b[160];
        _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "iter_seq native=%u port=%u pairs; first diff at pair %u "
                    "(%d,%d) vs (%d,%d)",
                    (unsigned)(a.seq.size() / 2), (unsigned)(pseq.size() / 2),
                    (unsigned)(i / 2),
                    i + 1 < a.seq.size() ? a.seq[i] : -2,
                    i + 1 < a.seq.size() ? a.seq[i + 1] : -2,
                    i + 1 < pseq.size() ? pseq[i] : -2,
                    i + 1 < pseq.size() ? pseq[i + 1] : -2);
        why = b;
        return false;
    }
    return true;
}

static std::string anode_key(const AStateNode &n) {
    char b[64];
    if (n.pos_addr) {
        _snprintf_s(b, sizeof(b), _TRUNCATE, "a:%08x", (unsigned)n.pos_addr);
    } else {
        _snprintf_s(b, sizeof(b), _TRUNCATE, "p:%d,%d", n.x, n.y);
    }
    return std::string(b);
}
static std::string anode_str(const AStateNode &n) {
    char b[192];
    _snprintf_s(b, sizeof(b), _TRUNCATE,
                "pos=[%d,%d] pos_addr=0x%x filter=%d g=%d d2=%d h10k=%d cost=%d "
                "parent=[%d,%d]",
                n.x, n.y, (unsigned)n.pos_addr, n.filter, n.g, n.d2, n.h10k,
                n.cost, n.px, n.py);
    return std::string(b);
}
static std::string reg_branch_for(const AStateNode &n, int seq) {
    (void)seq;    /* carried nodes were registered in an earlier seq */
    for (size_t i = 0; i < g_reg_events.size(); i++) {
        const RegEvent &e = g_reg_events[i];
        if ((n.pos_addr && e.pos_addr == n.pos_addr) ||
            (!n.pos_addr && e.x == n.x && e.y == n.y)) {
            char b[192];
            _snprintf_s(b, sizeof(b), _TRUNCATE,
                        "dd70 branch 1 (free path) -> 0x45dd00 "
                        "register_occupied_slot @seq=%d filter=%d g=%d cost=%d",
                        e.seq, e.filter, e.g, e.cost);
            return std::string(b);
        }
    }
    return "no register event this seq (not a branch-1 insertion)";
}

static std::string diff_after(const AState &nat, const AState &port, int seq,
                              int *p_ai_diff = NULL, bool *p_ai_compared = NULL,
                              bool ai_words_too = true) {
    if (p_ai_diff) *p_ai_diff = 0;
    if (p_ai_compared) *p_ai_compared = false;
    std::map<std::string, const AStateNode *> an, bn;
    for (size_t i = 0; i < nat.map1.size(); i++) an[anode_key(nat.map1[i])] = &nat.map1[i];
    for (size_t i = 0; i < port.map1.size(); i++) bn[anode_key(port.map1[i])] = &port.map1[i];

    int added = 0, removed = 0, changed = 0, occ_add = 0, occ_rm = 0, ai_diff = 0;
    std::string s;
    char b[320];
    for (std::map<std::string, const AStateNode *>::const_iterator it = bn.begin();
         it != bn.end(); ++it) {
        if (an.count(it->first)) continue;
        added++;
        if (added <= 12) {
            s += "\n  + map1 " + anode_str(*it->second) + " branch=" +
                 reg_branch_for(*it->second, seq);
        }
    }
    for (std::map<std::string, const AStateNode *>::const_iterator it = an.begin();
         it != an.end(); ++it) {
        if (bn.count(it->first)) continue;
        removed++;
        if (removed <= 12) s += "\n  - map1 " + anode_str(*it->second);
    }
    for (std::map<std::string, const AStateNode *>::const_iterator it = an.begin();
         it != an.end(); ++it) {
        std::map<std::string, const AStateNode *>::const_iterator jt = bn.find(it->first);
        if (jt == bn.end()) continue;
        const AStateNode &a = *it->second, &p = *jt->second;
        std::string f;
        if (a.filter != p.filter) f += " filter";
        if (a.g != p.g) f += " g";
        if (a.d2 != p.d2) f += " d2";
        if (a.h10k != p.h10k) f += " h10k";
        if (a.cost != p.cost) f += " cost";
        if (a.px != p.px || a.py != p.py) f += " parent";
        if (f.empty()) continue;
        changed++;
        if (changed <= 12) {
            _snprintf_s(b, sizeof(b), _TRUNCATE,
                        "\n  ~ map1 %s fields:%s native(filter=%d,g=%d,d2=%d,"
                        "h10k=%d,cost=%d,parent=[%d,%d]) "
                        "port(filter=%d,g=%d,d2=%d,h10k=%d,cost=%d,parent=[%d,%d])",
                        anode_key(a).c_str(), f.c_str(), a.filter, a.g, a.d2,
                        a.h10k, a.cost, a.px, a.py, p.filter, p.g, p.d2, p.h10k,
                        p.cost, p.px, p.py);
            s += b;
        }
    }
    std::map<int, int> ao, bo;
    for (size_t i = 0; i < nat.occ.size(); i++) ao[nat.occ[i]]++;
    for (size_t i = 0; i < port.occ.size(); i++) bo[port.occ[i]]++;
    for (std::map<int, int>::const_iterator it = bo.begin(); it != bo.end(); ++it) {
        std::map<int, int>::const_iterator jt = ao.find(it->first);
        int d = it->second - (jt == ao.end() ? 0 : jt->second);
        if (d <= 0) continue;
        occ_add += d;
        if (occ_add <= 16) {
            _snprintf_s(b, sizeof(b), _TRUNCATE, "\n  + occ key=%d count=%d", it->first, d);
            s += b;
        }
    }
    for (std::map<int, int>::const_iterator it = ao.begin(); it != ao.end(); ++it) {
        std::map<int, int>::const_iterator jt = bo.find(it->first);
        int d = it->second - (jt == bo.end() ? 0 : jt->second);
        if (d <= 0) continue;
        occ_rm += d;
        if (occ_rm <= 16) {
            _snprintf_s(b, sizeof(b), _TRUNCATE, "\n  - occ key=%d count=%d", it->first, d);
            s += b;
        }
    }
    if (ai_words_too && nat.has_ai && port.has_ai) {
        /* Слоты-указатели BattleAI (+0x00 game_ctx, +0x2c anchor, +0x6c/0x70/
         * 0x74/0x78 map-поля, +0x7c) в реплике адреса харнеса — их сравнение
         * бессмысленно. */
        static const int ptr_words[8] = {0, 10, 11, 27, 28, 29, 30, 31};
        for (int i = 0; i < RP_AI_WORDS; i++) {
            bool skip = false;
            for (int k = 0; k < 8; k++) if (ptr_words[k] == i) skip = true;
            if (skip || nat.ai[i] == port.ai[i]) continue;
            ai_diff++;
            if (ai_diff <= 16) {
                _snprintf_s(b, sizeof(b), _TRUNCATE,
                            "\n  ~ ai_words[%d] (+0x%02x) native=0x%08x port=0x%08x",
                            i, i * 4, nat.ai[i], port.ai[i]);
                s += b;
            }
        }
    }
    if (!added && !removed && !changed && !occ_add && !occ_rm && !ai_diff) {
        return std::string();
    }
    char head[320];
    _snprintf_s(head, sizeof(head), _TRUNCATE,
                " map1 native=%u port=%u (+%d -%d ~%d); occ native=%u port=%u "
                "(+%d -%d); ai_words_diff=%d",
                (unsigned)nat.map1.size(), (unsigned)port.map1.size(),
                added, removed, changed, (unsigned)nat.occ.size(),
                (unsigned)port.occ.size(), occ_add, occ_rm, ai_diff);
    if (p_ai_diff) *p_ai_diff = ai_diff;
    if (p_ai_compared) *p_ai_compared = (nat.has_ai && port.has_ai);
    return std::string(head) + s;
}

/* ------------------------------- реплика -------------------------------- */

extern "C" {
    int g_replay_override = 1;
    int g_replay_leaf_underflow = 0;
    static Call *g_cur = NULL;
    static size_t g_pi = 0, g_hi = 0, g_ti = 0, g_ms_i = 0, g_ms_ri = 0;
    static std::vector<int *> g_fake_path;
    /* Identity: нативный LDPos* — ключ map1 и операнд гейта cur->pos==cmp
     * (@0x45e137). Ключ карты — реальный адрес из захвата; объект(ы) харнеса,
     * выступающие его носителем, отображаются в тот же адрес. 0x100000000|
     * occ_key — запасной ключ для захватов без адресов (координаты). */
    static std::map<unsigned long long, int *> g_pos_obj;
    static std::map<int *, unsigned long long> g_obj_key;
    static std::vector<int> g_iter_seq;      /* пары x,y; -1/-1 = null */
    static int g_exit_node[2];
    static int g_exit_code = -1, g_exit_iters = -1;
    static bool g_have_exit = false;
}

/* v7: world[0x120] — масштаб метрики c3a0/cb40. Харнесс подаёт снятое на входе
 * cb40 значение (`res.leaf.scale`) перед каждым вызовом поиска порта: точка
 * применения — stub tree_lookup, который порт вызывает в обходе цепочки до
 * чтения масштаба (path.cpp @0x45c3e7 → 0x45c430). Нет значения — масштаб
 * остаётся статическим из записи `grid`. Арифметика порта не меняется.
 * scale_compared/scale_skipped — вызовы с захваченным масштабом / с фолбэком. */
static int g_scale_compared = 0, g_scale_skipped = 0;

/* v8: per-cb40 chain reconstruction. The port's own node registrations are
 * recorded in g_port_tree and win over the captured status when the chain
 * state is applied, so tree_lookup sees the PORT's state, not a stale capture.
 * Old captures have no leaf.chain -> skipped, zero behaviour change. */
static std::map<unsigned int, int> g_port_tree;
static int g_chain_applied = 0, g_chain_skipped = 0;

extern "C" void replay_apply_chain_state(int leaf_index)
{
    const Call *c = g_cur;
    static int disabled = -1;
    if (disabled < 0) {
        const char *s = getenv("LD_REPLAY_DISABLE_CHAIN");
        disabled = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    if (disabled) {
        g_chain_skipped++;
        return;
    }
    if (c == NULL || g_grid == NULL || g_grid_error) {
        return;
    }
    if (leaf_index < 0 || leaf_index >= (int)c->leaves_chain.size() ||
        c->leaves_chain[leaf_index].empty()) {
        g_chain_skipped++;
        return;
    }
    const std::vector<int> &rec = c->leaves_chain[leaf_index];
    int cnt = rec[0];
    if (cnt < 0 || cnt > 8 || rec.size() < (size_t)(1 + cnt * 7)) {
        g_chain_skipped++;
        return;
    }
    for (int i = 0; i < cnt; i++) {
        const int *e = &rec[1 + i * 7];
        int idx = e[0];
        unsigned key;
        if (idx < 0 || idx >= g_grid->n) continue;
        GridCell *cell = &g_grid->cells[idx];
        memcpy(&cell->min_metric, &e[1], sizeof(int));
        memcpy(&cell->max_metric, &e[2], sizeof(int));
        g_grid->krecs[idx].key = e[3];
        g_grid->cell_keys[idx] = e[3];
        cell->next = (e[4] >= 0 && e[4] < g_grid->n)
                     ? &g_grid->cells[e[4]] : NULL;
        cell->chain_next = (e[5] >= 0 && e[5] < g_grid->n)
                           ? &g_grid->cells[e[5]] : NULL;
        key = (unsigned)e[3];
        if (g_port_tree.find(key) == g_port_tree.end()) {
            g_grid->tree[key] = e[6];   /* world+0x104 status through native seam */
        }
    }
    g_chain_applied++;
}

static void rep_reset_world_scale(const Call *c)
{
    if (g_grid != NULL && !g_grid_error) {
        memcpy(g_worldbuf + 0x120, &g_grid->wscale, 4);
    }
    if (c != NULL && !c->leaves_scale_ok.empty()) g_scale_compared++;
    else g_scale_skipped++;
}

static void rep_apply_world_scale(void)
{
    const Call *c = g_cur;
    size_t i = g_pi;
    if (c == NULL || g_grid == NULL || g_grid_error) return;
    if (i < c->leaves_scale_ok.size() && i < c->leaves_scale.size() &&
        c->leaves_scale_ok[i] != 0) {
        unsigned int bits = c->leaves_scale[i];
        memcpy(g_worldbuf + 0x120, &bits, 4);
    }
}

struct RNode {
    TreeNode tn;
    PathState ps;
    LDPos pos;
    PathState parentStub;
    LDPos parentPos;
    PathState nbrStub[9];
    LDPos nbrPos[9];
    RNode() { memset(this, 0, sizeof(*this)); }
};
struct RepMap2 {
    TreeNode head;
    std::vector<TreeNode *> nodes;
};
struct Rep {
    unsigned char aiBuf[0x100];     /* BattleAI + запас под +0x7c */
    unsigned char ctxBuf[0x130];    /* game_ctx: flags +0, grid +8, scale +0x120 */
    unsigned char lvlBuf[0x40];     /* GridLevel: size_x +0x2c, size_y +0x30 */
    LDPos cmpPos, anchorPos;        /* адресная идентичность cmp/anchor по pos */
    LDPos *cmpPtr;
    TreeNode m1head;
    RepMap2 m2;
    std::vector<RNode *> nodes;
    std::vector<TreeNode *> m1nodes;  /* map1: tn->value = PathState (объект) */
    /* Полный 64-битный ключ map1 по индексу m1nodes. Поле TreeNode.key — 32 бита
     * (нативная раскладка узла), поэтому ключ 0x100000000|occ_key в нём обрезается
     * до 0 и порядок сортировки смешивает адресные и координатные узлы. Порядок
     * важен: s6_scan_map берёт строго минимум метрики (первый in-order при равенстве).
     * tn->key остаётся обрезанным — он уходит в игру и в сравнение захвата. */
    std::vector<unsigned long long> m1keys;
    std::vector<PathLeaf> pathPool;
    int nilBuf[8];
};

static TreeNode *rep_nil(Rep *r) {
    return (TreeNode *)(void *)r->nilBuf;
}

/* Текущая реплика вызова: нативный ABI map2 — объект std::map ВНУТРИ BattleAI
 * (dd70 `lea 0x74(%edi),%ecx` @0x45dec8, head по +0x78), поэтому target.cpp
 * передаёт &self->map2 = aiBuf+0x74. Харнес держит RepMap2 отдельно — резолв
 * по этому адресу. */
static Rep *g_rep = NULL;

static int occ_key_of(LDPos *p) {
    return (int)(((unsigned short)p->y << 16) | (unsigned short)p->x);
}

/* Ключи identity-карты: реальный адрес LDPos из захвата; запасной режим —
 * 0x100000000|occ_key (захваты без адресов LDPos). */
static unsigned long long pos_key_addr(int addr) {
    return (unsigned long long)(unsigned)addr;
}
static unsigned long long pos_key_coord(int x, int y) {
    return 0x100000000ULL |
           (unsigned long long)((((unsigned short)y << 16) | (unsigned short)x));
}

/* Порядок map1 задаётся КЛЮЧОМ узла — указателем LDPos (dd00 передаёт
 * &obj->pos в insert, 0xc(%узла) сравнивается в 45d810). tn->key = реальный
 * адрес из захвата (pos_addr); новые узлы получают адрес своего
 * cb40-результата (leaf ptr). Запасной режим (захват без адресов LDPos):
 * (x,y)-мажор — так отсортированы все map1/map1_after таких записей.
 * Порядок важен: s6_scan_map берёт строго минимум метрики (при равных —
 * первый in-order). */
static bool m1_coord_less(TreeNode *a, TreeNode *b) {
    PathState *pa = (PathState *)a->value;
    PathState *pb = (PathState *)b->value;
    LDPos *qa = pa ? pa->pos : NULL;
    LDPos *qb = pb ? pb->pos : NULL;
    int ax = qa ? qa->x : -1, ay = qa ? qa->y : -1;
    int bx = qb ? qb->x : -1, by = qb ? qb->y : -1;
    if (ax != bx) return ax < bx;
    return ay < by;
}

static bool m1_less(std::pair<unsigned long long, TreeNode *> a,
                    std::pair<unsigned long long, TreeNode *> b)
{
    /* Ключ 0 = адреса из захвата нет — порядок задаёт (x,y)-фолбэк.
     * Полные ключи: адресные узлы (адрес < 0x100000000) идут первыми,
     * внутри группы — по возрастанию ключа. */
    if (a.first != 0 && b.first != 0) return a.first < b.first;
    return m1_coord_less(a.second, b.second);
}

static void rep_rebuild_m1(Rep *r) {
    TreeNode *nil = rep_nil(r);
    size_t i, n = r->m1nodes.size();
    std::vector<std::pair<unsigned long long, TreeNode *> > ord;
    ord.reserve(n);
    for (i = 0; i < n; i++) ord.push_back(std::make_pair(r->m1keys[i], r->m1nodes[i]));
    std::sort(ord.begin(), ord.end(), m1_less);
    for (i = 0; i < n; i++) {
        TreeNode *tn = ord[i].second;
        tn->left = nil;
        tn->right = (i + 1 < n) ? ord[i + 1].second : &r->m1head;
        tn->parent = (i > 0) ? ord[i - 1].second : &r->m1head;
        tn->is_nil = 0;
    }
    r->m1head.left = n ? ord[0].second : &r->m1head;
    r->m1head.parent = n ? ord[n - 1].second : &r->m1head;
    r->m1head.right = &r->m1head;
    r->m1head.is_nil = 1;
}

static void rep_rebuild_m2(Rep *r) {
    TreeNode *nil = rep_nil(r);
    size_t i, n = r->m2.nodes.size();
    std::sort(r->m2.nodes.begin(), r->m2.nodes.end(),
              [](TreeNode *a, TreeNode *b) {
                  return (unsigned)a->key < (unsigned)b->key;
              });
    for (i = 0; i < n; i++) {
        r->m2.nodes[i]->left = nil;
        r->m2.nodes[i]->right = (i + 1 < n) ? r->m2.nodes[i + 1] : &r->m2.head;
        r->m2.nodes[i]->parent = (i > 0) ? r->m2.nodes[i - 1] : &r->m2.head;
        r->m2.nodes[i]->is_nil = 0;
    }
    r->m2.head.left = n ? r->m2.nodes[0] : &r->m2.head;
    r->m2.head.parent = n ? r->m2.nodes[n - 1] : &r->m2.head;
    r->m2.head.right = &r->m2.head;
    r->m2.head.is_nil = 1;
}

/* ---------------- v3: значения узлов map2 в реплике --------------------- */
/* Native/class FUN_0045dd00 кладёт в map2 тот же PathState, что уже лежит в
 * map1 (ветка 1 dd70 @0x45ded7 читает value как PathState* и идёт в ветку 2).
 * Запись значения даёт identity pos_addr; без записи ключ считается свободным.
 * Identity восстанавливается по pos_addr. */
static std::map<int *, PathState *> g_pos_ps;
static std::vector<RNode *> g_occ_stubs;

/* Ячейка сетки по координатам — та же цепочка, что s6_grid_lookup (rows[x][y]
 * → лист cells + idx*0x34). Нужна, чтобы снять слова LDPos с захваченной ячейки. */
static GridCell *grid_cell_at(int x, int y)
{
    if (g_grid == NULL || g_grid_error) return NULL;
    if (x < 0 || y < 0 || x >= g_grid->sx || y >= g_grid->sy) return NULL;
    int idx = g_grid->rowptr[x][y];
    if (idx < 0 || idx >= g_grid->n) return NULL;
    return &g_grid->cells[idx];
}

static int g_fill_cell = 0, g_fill_leaf = 0, g_fill_stub = 0;

static int *rp_pos_obj(int addr, int x, int y)
{
    unsigned long long key = (addr != 0) ? pos_key_addr(addr)
                                         : pos_key_coord(x, y);
    std::map<unsigned long long, int *>::const_iterator it = g_pos_obj.find(key);
    if (it != g_pos_obj.end()) return it->second;
    int *obj = (int *)calloc(16, sizeof(int));
    obj[3] = x; obj[4] = y;
    /* Слова +0x00/+0x04/+0x08 и level +0x1c: носитель — grid-клетка этих
     * координат (spec->pos, path.cpp), поэтому снимаются с захваченной ячейки.
     * Ключ пришёл из адреса — берём слова этого адреса из leaf-потока захвата.
     * Ни ячейки, ни адреса в захвате нет — остаётся минимальная заглушка (нули):
     * этих данных в захвате v5 нет. */
    GridCell *c = grid_cell_at(x, y);
    if (c != NULL && *(int *)((unsigned char *)c + 0x0c) == x &&
        *(int *)((unsigned char *)c + 0x10) == y) {
        obj[0] = *(int *)((unsigned char *)c + 0x00);
        obj[1] = *(int *)((unsigned char *)c + 0x04);
        obj[2] = *(int *)((unsigned char *)c + 0x08);
        obj[7] = *(int *)((unsigned char *)c + 0x1c);   /* LDPos.level */
        g_fill_cell++;
    } else {
        std::map<unsigned long long, PosW>::const_iterator pw = g_pos_w.find(key);
        if (pw != g_pos_w.end()) {
            obj[0] = pw->second.w[0];
            obj[1] = pw->second.w[1];
            obj[2] = pw->second.w[2];
            g_fill_leaf++;
        } else {
            g_fill_stub++;
        }
        obj[7] = (g_grid && !g_grid_error)
                 ? (int)(size_t)&g_grid->levels[g_grid->leaf] : 0;  /* LDPos.level */
    }
    g_pos_obj[key] = obj;
    g_obj_key[obj] = key;
    return obj;
}

static PathState *rp_ps_ref(int addr, int x, int y)
{
    if (x == -1 && y == -1) return NULL;         /* NULL-сосед в захвате */
    int *obj = rp_pos_obj(addr, x, y);
    std::map<int *, PathState *>::const_iterator it = g_pos_ps.find(obj);
    if (it != g_pos_ps.end()) return it->second;
    RNode *rn = new RNode;
    g_occ_stubs.push_back(rn);
    rn->pos.x = obj[3]; rn->pos.y = obj[4];
    rn->ps.pos = (LDPos *)(void *)obj;
    g_pos_ps[obj] = &rn->ps;
    return &rn->ps;
}

static PathState *rp_make_occ_ps(const OccVal &v)
{
    int *pobj = rp_pos_obj(v.pos_addr, v.pos[0], v.pos[1]);
    std::map<int *, PathState *>::const_iterator known = g_pos_ps.find(pobj);
    /* Обычный dd00: один и тот же объект в map1 и map2 — переиспользуем его. */
    if (v.value_addr != 0 && known != g_pos_ps.end()) return known->second;
    RNode *rn = new RNode;
    g_occ_stubs.push_back(rn);
    memcpy(&rn->ps, v.w, sizeof(v.w));           /* 22 dword == sizeof(PathState) */
    rn->pos.x = v.pos[0]; rn->pos.y = v.pos[1];
    rn->ps.pos = (LDPos *)(void *)pobj;
    rn->ps.parent = rp_ps_ref(v.ptr[1], v.parent[0], v.parent[1]);
    for (int k = 0; k < 9; k++) {
        rn->ps.nbr[k] = rp_ps_ref(v.ptr[2 + k], v.nbr[k][0], v.nbr[k][1]);
    }
    g_pos_ps[pobj] = &rn->ps;
    return &rn->ps;
}

/* v5: состояния финального гейта cb40 на каждый вызов порта: spec_in[22]
 * (PathState на входе), spec_out[22] (на выходе), gate[16] — out_metric, L[0x10],
 * L[0x2c], L[0x14], L[0x18], thr, reason, res, best_min, best_max, best_key,
 * chain, p3, p7, L[0x40], &out. Хранилище СТАТИЧЕСКОЕ: харнесс чувствителен к
 * адресам объектов (identity-карты), любая аллокация в прогоне меняет порядок
 * map1 и расхождение, поэтому запись не должна ничего аллоцировать.
 * Индекс = номер cb40-вызова порта (параллельно port_path). */
#define RP_GATE_MAX 17072
static int g_gate_rec[RP_GATE_MAX][60];
static int g_gi = 0;
/* v6: number of native-vs-port gate comparisons attempted / skipped because
 * the capture has no `res.leaf.gate` key (old v1..v5 captures). */
static int g_gate_compared = 0, g_gate_skipped = 0;

extern "C" int replay_gate_index(void) { return g_gi; }
extern "C" int replay_cell_index(void *cell)
{
    if (g_grid == NULL || g_grid_error || cell == NULL) return -1;
    long d = ((unsigned char *)cell - (unsigned char *)g_grid->cells);
    if (d < 0 || d >= (long long)sizeof(GridCell) * g_grid->n) return -1;
    return (int)(d / sizeof(GridCell));
}

static void rep_build(Rep *r, const Call &c) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *nil = rep_nil(r);
    size_t i;
    std::map<int, RNode *> bypos;

    memset(r, 0, sizeof(*r));
    if (g_grid && !g_grid_error) {
        g_worldbuf[0x110] = (unsigned char)c.w110;
    }
    rep_reset_world_scale(&c);          /* v7: статический масштаб + счётчик вызовов */
    g_pos_ps.clear();
    for (size_t si = 0; si < g_occ_stubs.size(); si++) delete g_occ_stubs[si];
    g_occ_stubs.clear();
    /* v3: сырой скалярный блок BattleAI (dd70/e0a0 читают и поля за +0x38).
     * Указательные поля ниже перезаписываются объектами реплики. */
    if (c.has_ai_words) {
        memcpy(r->aiBuf, c.ai_words, sizeof(c.ai_words));
    }
    nil->is_nil = 1;
    nil->left = nil; nil->right = nil; nil->parent = nil;

    memcpy(&ai->timer_start_sec, &c.timer_bits, 4);
    memcpy(&ai->final_threshold, &c.final_bits, 4);
    memcpy(&ai->dist_threshold, &c.dist_bits, 4);
    ai->y_radius = c.yrad;
    ai->x_radius = c.xrad;
    ai->canvas_rect.left = c.rect[0];
    ai->canvas_rect.top = c.rect[1];
    ai->canvas_rect.right = c.rect[2];
    ai->canvas_rect.bottom = c.rect[3];
    ai->tracking = (unsigned char)c.trk;
    ai->distance_threshold = c.bd2;
    ai->anchor = &r->anchorPos;        /* уточняется после построения map1 */
    r->anchorPos.x = c.anchor[0];
    r->anchorPos.y = c.anchor[1];
    r->cmpPos.x = c.cmp[0];
    r->cmpPos.y = c.cmp[1];
    r->cmpPtr = &r->cmpPos;
    *(int *)((unsigned char *)ai + 0x38) = c.ai_code;

    /* game_ctx: v5 — настоящая сетка и синтетический world вместо заглушки */
    *(unsigned int *)(r->ctxBuf + 0) = c.ctx_flags;
    *(unsigned int *)(r->ctxBuf + 0x120) = c.world_scale_bits;
    *(void **)(r->ctxBuf + 4) = (g_grid && !g_grid_error)
                                ? (void *)g_worldbuf : NULL;
    *(void **)(r->ctxBuf + 8) = (g_grid && !g_grid_error)
                                ? (void *)&g_grid->levels[0]
                                : (void *)r->lvlBuf;
    ai->game_ctx = (void *)r->ctxBuf;
    *(int *)(r->lvlBuf + 0x2c) = c.grid_x;
    *(int *)(r->lvlBuf + 0x30) = c.grid_y;

    /* map1 */
    for (i = 0; i < c.map1.size(); i++) {
        const CapNode &src = c.map1[i];
        RNode *n = new RNode;
        n->pos.x = src.x; n->pos.y = src.y;
        n->pos.level = (g_grid && !g_grid_error)
                       ? &g_grid->levels[g_grid->leaf] : NULL;
        n->ps.pos = &n->pos;
        n->ps.filter = src.filter;
        n->ps.g_cost = src.g;
        n->ps.dist2 = src.d2;
        n->ps.h10000 = src.h10k;
        n->ps.metric = src.cost;
        n->ps.min_sentinel = 0xcb18967f;
        n->ps.max_sentinel = 0x4b18967f;
        n->parentPos.x = src.px; n->parentPos.y = src.py;
        n->parentStub.pos = &n->parentPos;
        n->ps.parent = (src.px == -1 && src.py == -1) ? NULL : &n->parentStub;
        for (int k = 0; k < 9; k++) {
            n->nbrPos[k].x = src.nbr[k][0];
            n->nbrPos[k].y = src.nbr[k][1];
            n->nbrStub[k].pos = &n->nbrPos[k];
            /* (-1,-1) — NULL-сосед в захвате (cap_read_ps_pos пишет -1,-1). */
            n->ps.nbr[k] = (src.nbr[k][0] == -1 && src.nbr[k][1] == -1)
                           ? NULL : &n->nbrStub[k];
        }
        r->nodes.push_back(n);
        n->tn.value = &n->ps;
        n->tn.key = src.pos_addr;
        r->m1nodes.push_back(&n->tn);
        r->m1keys.push_back(src.pos_addr != 0 ? pos_key_addr(src.pos_addr) : 0ULL);
        bypos[occ_key_of(&n->pos)] = n;
    }
    /* Связки: nbr/parent указывают на узлы с той же позицией; если узла в
     * map1 нет — создаём заглушку с теми же координатами (filter=0). */
    for (i = 0; i < c.map1.size(); i++) {
        RNode *n = r->nodes[i];
        const CapNode &src = c.map1[i];
        int k;
        if (src.px != -1 || src.py != -1) {
            std::map<int, RNode *>::const_iterator it = bypos.find(occ_key_of(&n->parentPos));
            if (it != bypos.end()) {
                n->ps.parent = &it->second->ps;
            }
        }
        for (k = 0; k < 9; k++) {
            if (src.nbr[k][0] == -1 && src.nbr[k][1] == -1) continue;
            std::map<int, RNode *>::const_iterator it = bypos.find(occ_key_of(&n->nbrPos[k]));
            if (it != bypos.end()) {
                n->ps.nbr[k] = &it->second->ps;
            }
        }
    }
    /* cmp/anchor: нативная gate1 — сравнение УКАЗАТЕЛЕЙ cur->pos == cmp;
     * identity воспроизводится по реальным адресам LDPos из захвата. */
    g_pos_obj.clear();
    g_obj_key.clear();
    for (i = 0; i < c.map1.size(); i++) {
        RNode *n = r->nodes[i];
        unsigned long long k = (c.map1[i].pos_addr != 0)
            ? pos_key_addr(c.map1[i].pos_addr)
            : pos_key_coord(n->pos.x, n->pos.y);
        g_pos_obj[k] = (int *)(void *)&n->pos;
        g_obj_key[(int *)(void *)&n->pos] = k;
        g_pos_ps[(int *)(void *)&n->pos] = &n->ps;
    }
    {
        unsigned long long key;
        std::map<unsigned long long, int *>::const_iterator it;
        r->cmpPos.x = c.cmp[0]; r->cmpPos.y = c.cmp[1];
        key = (c.cmp_addr != 0) ? pos_key_addr(c.cmp_addr)
                                : pos_key_coord(c.cmp[0], c.cmp[1]);
        it = g_pos_obj.find(key);
        if (it == g_pos_obj.end()) {
            g_pos_obj[key] = (int *)(void *)&r->cmpPos;
            g_obj_key[(int *)(void *)&r->cmpPos] = key;
            r->cmpPtr = &r->cmpPos;
        } else {
            r->cmpPtr = (LDPos *)(void *)it->second;
        }
    }
    {
        unsigned long long key = pos_key_coord(c.anchor[0], c.anchor[1]);
        std::map<unsigned long long, int *>::const_iterator it =
            g_pos_obj.find(key);
        ai->anchor = (it == g_pos_obj.end())
                     ? &r->anchorPos : (LDPos *)(void *)it->second;
    }
    if (c.is_eee0) {
        if (c.eee0_grid_ok) {
            int *gobj = rp_pos_obj(c.eee0_s28, c.eee0_grid_w[3], c.eee0_grid_w[4]);
            for (int k = 0; k < 8; k++) {
                if (k != 7) gobj[k] = c.eee0_grid_w[k];
            }
            if (gobj[7] == 0) {
                gobj[7] = (g_grid && !g_grid_error)
                          ? (int)(size_t)&g_grid->levels[g_grid->leaf] : 0;
            }
            *(int *)((unsigned char *)ai + 0x28) = (int)(size_t)gobj;
        }
        *(unsigned int *)(g_worldbuf + 0x134) = (unsigned int)c.eee0_world_134;
    } else {
        memset(g_worldbuf + 0x134, 0, sizeof(unsigned int));
    }
    rep_rebuild_m1(r);
    ai->cand_head = &r->m1head;
    ai->reserved_70 = (int)r->nodes.size();

    for (i = 0; i < c.occ.size(); i++) {
        TreeNode *tn = new TreeNode;
        memset(tn, 0, sizeof(*tn));
        tn->key = c.occ[i];
        tn->is_nil = 0;
        r->m2.nodes.push_back(tn);
    }
    /* v3: восстанавливаем значения занятых узлов (ветка 2 dd70). Запись значения
     * отсутствует или явный null — value остаётся NULL. */
    if (!c.occ_vals.empty()) {
        for (i = 0; i < r->m2.nodes.size(); i++) {
            std::map<int, OccVal>::const_iterator oi =
                c.occ_vals.find(c.occ[i]);
            if (oi == c.occ_vals.end() || !oi->second.has) continue;
            r->m2.nodes[i]->value = rp_make_occ_ps(oi->second);
        }
    }
    rep_rebuild_m2(r);
    ai->map2 = (void *)&r->m2;
    ai->occ_head = &r->m2.head;

    /* leaf-поток текущего вызова */
    g_cur = const_cast<Call *>(&c);
    g_pi = g_hi = g_ti = 0;
    g_ms_i = 0;
    g_fake_path.clear();
    g_iter_seq.clear();
    g_have_exit = false;
    g_replay_leaf_underflow = 0;      /* флаг — на вызов, не на прогон */
    g_reg_overflow = 0;
    g_gi = 0;                          /* gate-записи — на вызов */
}

/* Chained mode: реплика НЕ пересобирается из cap. Сохраняем map1/map2, все
 * identity-карты и новые узлы, которые порт вставил сам, а из cap берём только
 * аргументы вызова и скалярные входы (таймер/пороги/SETKA/leaf-поток). Это и
 * позволяет увидеть расхождение накопления состояния, которое обычный replay
 * маскирует полной пересборкой на каждом вызове. */
static void rep_rebind(Rep *r, const Call &c)
{
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    unsigned long long key;
    std::map<unsigned long long, int *>::const_iterator it;

    /* Скалярный блок BattleAI (указательные слоты ниже перезаписываются). */
    if (c.has_ai_words) {
        memcpy(r->aiBuf, c.ai_words, sizeof(c.ai_words));
    }
    if (g_grid && !g_grid_error) {
        g_worldbuf[0x110] = (unsigned char)c.w110;
    }
    rep_reset_world_scale(&c);          /* v7: статический масштаб + счётчик вызовов */
    memcpy(&ai->timer_start_sec, &c.timer_bits, 4);
    memcpy(&ai->final_threshold, &c.final_bits, 4);
    memcpy(&ai->dist_threshold, &c.dist_bits, 4);
    ai->y_radius = c.yrad;
    ai->x_radius = c.xrad;
    ai->canvas_rect.left = c.rect[0];
    ai->canvas_rect.top = c.rect[1];
    ai->canvas_rect.right = c.rect[2];
    ai->canvas_rect.bottom = c.rect[3];
    ai->tracking = (unsigned char)c.trk;
    ai->distance_threshold = c.bd2;
    r->anchorPos.x = c.anchor[0];
    r->anchorPos.y = c.anchor[1];
    r->cmpPos.x = c.cmp[0];
    r->cmpPos.y = c.cmp[1];
    *(int *)((unsigned char *)ai + 0x38) = c.ai_code;

    /* game_ctx / grid — v5: реконструированная статическая сетка. */
    *(unsigned int *)(r->ctxBuf + 0) = c.ctx_flags;
    *(unsigned int *)(r->ctxBuf + 0x120) = c.world_scale_bits;
    *(void **)(r->ctxBuf + 4) = (g_grid && !g_grid_error)
                                ? (void *)g_worldbuf : NULL;
    *(void **)(r->ctxBuf + 8) = (g_grid && !g_grid_error)
                                ? (void *)&g_grid->levels[0]
                                : (void *)r->lvlBuf;
    ai->game_ctx = (void *)r->ctxBuf;
    *(int *)(r->lvlBuf + 0x2c) = c.grid_x;
    *(int *)(r->lvlBuf + 0x30) = c.grid_y;

    /* cmp: identity по адресу из захвата, носитель — из накопленной карты. */
    key = (c.cmp_addr != 0) ? pos_key_addr(c.cmp_addr)
                            : pos_key_coord(c.cmp[0], c.cmp[1]);
    it = g_pos_obj.find(key);
    if (it == g_pos_obj.end()) {
        g_pos_obj[key] = (int *)(void *)&r->cmpPos;
        g_obj_key[(int *)(void *)&r->cmpPos] = key;
        r->cmpPtr = &r->cmpPos;
    } else {
        r->cmpPtr = (LDPos *)(void *)it->second;
    }
    /* anchor: те же правила, что в rep_build. */
    key = pos_key_coord(c.anchor[0], c.anchor[1]);
    it = g_pos_obj.find(key);
    ai->anchor = (it == g_pos_obj.end())
                 ? &r->anchorPos : (LDPos *)(void *)it->second;

    /* Карты — СОБСТВЕННОЕ состояние порта, не из cap. */
    ai->cand_head = &r->m1head;
    ai->reserved_70 = (int)r->m1nodes.size();
    ai->map2 = (void *)&r->m2;
    ai->occ_head = &r->m2.head;

    /* Поток листьев и нотисы — на вызов. identity-карты НЕ трогаем. */
    g_cur = const_cast<Call *>(&c);
    g_pi = g_hi = g_ti = 0;
    g_ms_i = 0;
    g_fake_path.clear();
    g_iter_seq.clear();
    g_have_exit = false;
    g_replay_leaf_underflow = 0;
    g_reg_overflow = 0;
}

static void after_from_cap(const Call &c, AState &out) {
    for (size_t i = 0; i < c.map1.size(); i++) {
        const CapNode &s = c.map1[i];
        AStateNode n;
        memset(&n, 0, sizeof(n));
        n.x = s.x; n.y = s.y;
        n.pos_addr = s.pos_addr;
        n.filter = s.filter; n.g = s.g; n.d2 = s.d2;
        n.h10k = s.h10k; n.cost = s.cost;
        n.px = s.px; n.py = s.py;
        out.map1.push_back(n);
    }
    for (size_t i = 0; i < c.occ.size(); i++) out.occ.push_back(c.occ[i]);
    if (c.has_ai_words) {
        memcpy(out.ai, c.ai_words, sizeof(out.ai));
        out.has_ai = true;
    }
}

static void after_from_rep(Rep *r, AState &out) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *head = ai->cand_head;
    TreeNode *cur = head ? head->left : NULL;
    for (size_t guard = 0; cur != NULL && cur != head && guard < 200000; guard++) {
        PathState *ps = (PathState *)cur->value;
        if (ps != NULL) {
            AStateNode n;
            memset(&n, 0, sizeof(n));
            LDPos *p = ps->pos;
            LDPos *pp = ps->parent ? ps->parent->pos : NULL;
            n.x = p ? p->x : -1;
            n.y = p ? p->y : -1;
            n.filter = ps->filter;
            n.g = ps->g_cost;
            n.d2 = ps->dist2;
            n.h10k = ps->h10000;
            n.cost = ps->metric;
            n.px = pp ? pp->x : -1;
            n.py = pp ? pp->y : -1;
            n.pos_addr = (int)cur->key;
            out.map1.push_back(n);
        }
        cur = s6_tree_next(cur);
    }
    head = ai->occ_head;
    cur = head ? head->left : NULL;
    for (size_t guard = 0; cur != NULL && cur != head && guard < 200000; guard++) {
        out.occ.push_back((int)cur->key);
        cur = s6_tree_next(cur);
    }
    memcpy(out.ai, r->aiBuf, sizeof(out.ai));
    out.has_ai = true;
}

static const Call *rep_cur(void) { return g_cur; }

/* ---------------------- точки подмены (BATTLE_REPLAY) ------------------- */

/* Портированные листья боевого тика сообщают значение claim'ом (ported.h).
 * В этом офлайн-реплее тик 44bdd0 не исполняется, поэтому заявок нет —
 * заглушка нужна только чтобы линковать grid.cpp, который её вызывает. */
void bdd0_leaf_claim_w(unsigned int va, void *ecx, int ret, const int *args,
                       int nargs, const int *w, int nw)
{
    (void)va; (void)ecx; (void)ret; (void)args; (void)nargs; (void)w; (void)nw;
}

extern "C" int replay_leaf_next(int kind) {
    const Call *c = rep_cur();
    if (c == NULL) { g_replay_leaf_underflow = 1; return 0; }
    if (kind == 1) {
        if (g_hi < c->leaves_heur.size()) return c->leaves_heur[g_hi++];
        g_replay_leaf_underflow = 1; return 0;
    }
    if (g_ti < c->leaves_timeout.size()) return c->leaves_timeout[g_ti++];
    g_replay_leaf_underflow = 1; return 0;
}

extern "C" void *replay_path_next(void) {
    const Call *c = rep_cur();
    if (c == NULL || g_pi >= c->leaves_path.size()) {
        g_replay_leaf_underflow = 1;
        return NULL;
    }
    PathLeaf pl = c->leaves_path[g_pi++];
    if (pl.null) return NULL;
    /* Объект-носитель реального адреса LDPos из захвата: нативный cb40
     * возвращает тот же LDPos, что потом лежит в pfs->pos, поэтому identity
     * cur->pos == cmp — это равенство адресов, а не координат. */
    unsigned long long key = (pl.addr != 0)
        ? pos_key_addr(pl.addr)
        : pos_key_coord(pl.w[3], pl.w[4]);
    std::map<unsigned long long, int *>::const_iterator it = g_pos_obj.find(key);
    if (it != g_pos_obj.end()) return it->second;
    int *obj = (int *)calloc(8, sizeof(int));   /* +0x00..+0x1f */
    for (int k = 0; k < 5; k++) obj[k] = pl.w[k];
    g_fake_path.push_back(obj);
    g_pos_obj[key] = obj;
    g_obj_key[obj] = key;
    return obj;
}

extern "C" void *replay_grid_cell(int x, int y) {
    /* Заглушка режима подмены листьев (g_replay_override==1); в режиме реального
     * прогона s6_grid_lookup работает с реконструированной сеткой. Координаты
     * нужны для указания ветки при расхождении листа. */
    static int cell[16];
    g_last_grid_x = x; g_last_grid_y = y;
    cell[3] = x; cell[4] = y;    /* +0x0c/+0x10 */
    return cell;
}

extern "C" void replay_note_rescan(void *cand) {
    int xy[2];
    if (cand == NULL) { xy[0] = -1; xy[1] = -1; }
    else {
        LDPos *p = *(LDPos **)cand;
        xy[0] = p ? p->x : -1;
        xy[1] = p ? p->y : -1;
    }
    g_iter_seq.push_back(xy[0]);
    g_iter_seq.push_back(xy[1]);
}

/* ---------------- v5: наблюдатели реальных листьев порта ---------------- */
/* Identity: replay_note_path_res возвращает КАНОНИЧЕСКИЙ носитель ключа
 * (один LDPos = один объект). Цель скана @0x45e137 (cur->pos == cmp) отдаёт
 * cmp-носитель, иначе гейт сравнивает два разных объекта и путь @0x45e173
 * (code=1) не срабатывает (defect: node native=[246,109] port=null). */
extern "C" void *replay_note_path_res(void *result)
{
    const Call *c = rep_cur();
    PathLeaf pl;
    memset(&pl, 0, sizeof(pl));
    if (result == NULL) {
        pl.null = true;
        if (c != NULL) const_cast<Call *>(c)->port_path.push_back(pl);
        /* Нативный null-лист потребляет свой индекс: addr-уточнение ниже
         * читает индекс грядущего листа, пары native/port остаются парами. */
        if (c != NULL && g_pi < c->leaves_path.size()) g_pi++;
        return NULL;
    }
    for (int k = 0; k < 5; k++) pl.w[k] = ((int *)result)[k];
    /* Identity: нативный адрес LDPos из захвата по совпавшим координатам —
     * только для порядка map1, не как подмена результата поиска. */
    unsigned long long key = pos_key_coord(pl.w[3], pl.w[4]);
    if (c != NULL && g_pi < c->leaves_path.size()) {
        const PathLeaf &nat = c->leaves_path[g_pi];
        if (!nat.null && nat.w[3] == pl.w[3] && nat.w[4] == pl.w[4] &&
            nat.addr != 0) {
            key = pos_key_addr(nat.addr);
        }
    }
    g_pi++;
    std::map<unsigned long long, int *>::const_iterator canon =
        g_pos_obj.find(key);
    if (canon != g_pos_obj.end()) {
        /* Один LDPos — один объект: цель скана отдаёт cmp-носитель. */
        if (c != NULL) const_cast<Call *>(c)->port_path.push_back(pl);
        return canon->second;
    }
    g_pos_obj[key] = (int *)result;
    g_obj_key[(int *)result] = key;
    if (c != NULL) const_cast<Call *>(c)->port_path.push_back(pl);
    return result;
}

/* v5: запись состояния гейта cb40 (входы 0x45cd3e..0x45cd73) на каждый вызов порта.
 * spec_in = PathState на входе, spec_out = PathState на выходе, gate = 16 dword
 * (out_metric, L[0x10], L[0x2c], L[0x14], L[0x18], thr, reason, res, chain-агрегат).
 * Индекс записи = индекс port_path (вызов note_state предшествует note_path_res). */
extern "C" void replay_note_gate_state(const int *spec_in, const int *spec_out,
                                       const int *gate)
{
    if (g_gi >= RP_GATE_MAX) return;
    memcpy(g_gate_rec[g_gi], spec_in, 22 * sizeof(int));
    memcpy(g_gate_rec[g_gi] + 22, spec_out, 22 * sizeof(int));
    memcpy(g_gate_rec[g_gi] + 44, gate, 16 * sizeof(int));
    g_gi++;
}

/* Opt-in per-c3a0 chain trace (LD_REPLAY_CHAIN_SEQ=<seq> + LD_REPLAY_TRACE=2):
 * shows what the reconstructed chain looks like when the port walks it.
 * Behaviour-neutral: printing only, no state changes. */
extern "C" void replay_chain_trace(const char *what, const void *p1,
                                   int n_walked, const int *walk,
                                   int p1idx, int lastidx, int bestidx,
                                   int p5before, int p6before,
                                   int p5after, int p6after, int p3, int p7)
{
    static int target = -2;
    if (target == -2) {
        const char *s = getenv("LD_REPLAY_CHAIN_SEQ");
        target = (s != NULL && *s != '\0') ? atoi(s) : -1;
    }
    if (replay_trace_level() < 2 || target < 0 || g_cur == NULL ||
        g_cur->seq != target) {
        return;
    }
    fprintf(stderr,
            "CHAIN %s seq=%d p1=%p idx=%d last=%d best=%d n=%d "
            "p5[0x%x->0x%x] p6[0x%x->0x%x] p3=0x%x p7=%d nodes:",
            what, g_cur->seq, p1, p1idx, lastidx, bestidx, n_walked,
            (unsigned)p5before, (unsigned)p5after,
            (unsigned)p6before, (unsigned)p6after,
            (unsigned)p3, p7);
    for (int i = 0; i < n_walked && i < 16; i++) {
        const int *e = &walk[i * 5];
        fprintf(stderr, " [i=%d k=%d st=%d mn=0x%x mx=0x%x]",
                e[0], e[1], e[2], (unsigned)e[3], (unsigned)e[4]);
    }
    fputc('\n', stderr);
}

extern "C" void replay_note_heur(int value)
{
    const Call *c = rep_cur();
    if (c != NULL) const_cast<Call *>(c)->port_heur.push_back(value);
}

extern "C" void replay_note_timeout(int value)
{
    const Call *c = rep_cur();
    if (c != NULL) const_cast<Call *>(c)->port_timeout.push_back(value);
}

extern "C" unsigned long replay_timeout_ms(void)
{
    const Call *c = rep_cur();
    if (c == NULL || g_ms_i >= c->leaves_timer.size()) {
        g_replay_leaf_underflow = 1;
        return 0UL;                     /* v1..v4: нет внешнего времени */
    }
    return (unsigned long)(unsigned int)c->leaves_timer[g_ms_i++];
}

extern "C" unsigned long replay_timer_restart_ms(void)
{
    const Call *c = rep_cur();
    if (c != NULL && g_ms_ri < c->eee0_timer_restart.size()) {
        unsigned int bits = c->eee0_timer_restart[g_ms_ri++];
        float sec;
        memcpy(&sec, &bits, sizeof(sec));
        /* cap stores this+0x04 as float seconds; s6_timer_restart expects
         * milliseconds and converts back with *0.001. */
        return (unsigned long)(sec * 1000.0f + 0.5f);
    }
    if (c == NULL || c->leaves_timer.empty()) {
        return 0UL;
    }
    size_t idx = (g_ms_i < c->leaves_timer.size())
                 ? g_ms_i : c->leaves_timer.size() - 1;
    return (unsigned long)(unsigned int)c->leaves_timer[idx];
}

extern "C" unsigned int replay_world_vt(void)
{
    /* c690 читает *(int*)(game_ctx) — это ctx_flags текущего вызова (225/233…),
     * а не статический снимок сетки. */
    return (g_grid != NULL && !g_grid_error && g_rep != NULL)
           ? *(unsigned int *)g_rep->ctxBuf : 0;
}

/* -------------------------- v5: сравнение листьев ------------------------ */
static std::string path_leaf_str(const PathLeaf &p)
{
    char b[128];
    if (p.null) return std::string("null");
    _snprintf_s(b, sizeof(b), _TRUNCATE, "[%d,%d,%d,%d,%d]",
                p.w[0], p.w[1], p.w[2], p.w[3], p.w[4]);
    return std::string(b);
}
static bool path_leaf_eq(const PathLeaf &a, const PathLeaf &b)
{
    if (a.null || b.null) return a.null == b.null;
    for (int k = 0; k < 5; k++) if (a.w[k] != b.w[k]) return false;
    return true;
}
struct LeafCounts {
    int path_n, path_eq, heur_n, heur_eq, tout_n, tout_eq;
    LeafCounts() : path_n(0), path_eq(0), heur_n(0), heur_eq(0),
                   tout_n(0), tout_eq(0) {}
};

static bool compare_leaf_stream(const Call &c, LeafCounts *cnt, std::string &why)
{
    size_t i;
    size_t pn = c.leaves_path.size() > c.port_path.size()
                ? c.leaves_path.size() : c.port_path.size();
    for (i = 0; i < pn; i++) {
        bool have_n = i < c.leaves_path.size(), have_p = i < c.port_path.size();
        cnt->path_n++;
        if (have_n && have_p &&
            path_leaf_eq(c.leaves_path[i], c.port_path[i])) {
            cnt->path_eq++;
            continue;
        }
        char b[256];
        PathLeaf nn, pp;
        memset(&nn, 0, sizeof(nn)); memset(&pp, 0, sizeof(pp));
        nn.null = true; pp.null = true;
        if (have_n) nn = c.leaves_path[i];
        if (have_p) pp = c.port_path[i];
        int gx = have_p ? pp.w[3] : (have_n ? nn.w[3] : -1);
        int gy = have_p ? pp.w[4] : (have_n ? nn.w[4] : -1);
        _snprintf_s(b, sizeof(b), _TRUNCATE,
                    "path[%u] native=%s port=%s "
                    "port branch=free-path cb40 grid=(%d,%d)",
                    (unsigned)i, path_leaf_str(nn).c_str(),
                    path_leaf_str(pp).c_str(), gx, gy);
        why = b;
        return false;
    }
    size_t hn = c.leaves_heur.size() > c.port_heur.size()
                ? c.leaves_heur.size() : c.port_heur.size();
    for (i = 0; i < hn; i++) {
        cnt->heur_n++;
        if (i < c.leaves_heur.size() && i < c.port_heur.size() &&
            c.leaves_heur[i] == c.port_heur[i]) {
            cnt->heur_eq++;
            continue;
        }
        char b[192];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "heur[%u] native=%d port=%d",
                    (unsigned)i,
                    i < c.leaves_heur.size() ? c.leaves_heur[i] : -999999,
                    i < c.port_heur.size() ? c.port_heur[i] : -999999);
        why = b;
        return false;
    }
    /* g_hi считает вызовы внешнего grid_query, а не входы в c690: гейт
     * вернуть 0 не вызывает c5e0. Сравнивается сам поток port_heur. */
    size_t tn = c.leaves_timeout.size() > c.port_timeout.size()
                ? c.leaves_timeout.size() : c.port_timeout.size();
    for (i = 0; i < tn; i++) {
        cnt->tout_n++;
        if (i < c.leaves_timeout.size() && i < c.port_timeout.size() &&
            c.leaves_timeout[i] == c.port_timeout[i]) {
            cnt->tout_eq++;
            continue;
        }
        char b[192];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "timeout[%u] native=%d port=%d",
                    (unsigned)i,
                    i < c.leaves_timeout.size() ? c.leaves_timeout[i] : -1,
                    i < c.port_timeout.size() ? c.port_timeout[i] : -1);
        why = b;
        return false;
    }
    return true;
}

extern "C" void replay_note_exit(void *node, int code, int iters) {
    if (node == NULL) {
        g_exit_node[0] = -1; g_exit_node[1] = -1;
    } else {
        LDPos *p = *(LDPos **)node;
        g_exit_node[0] = p ? p->x : -1;
        g_exit_node[1] = p ? p->y : -1;
    }
    g_exit_code = code;
    g_exit_iters = iters;
    g_have_exit = true;
}

/* --------------------------- native:: stub'ы ---------------------------- */

namespace native {
unsigned char *base(void) { return (unsigned char *)0x400000; }
void init(void) {}
void *fn(unsigned int va) { (void)va; return NULL; }
TreeNode *lower_bound(void *map, unsigned int key, void *hint) {
    (void)hint;
    return s6_map_lower_bound(&((RepMap2 *)map)->head, key);
}
TreeNode *lower_bound_slots(void *map, int *out, int *key) {
    RepMap2 *m;
    TreeNode *found;
    if (g_rep != NULL &&
        map == (void *)((unsigned char *)g_rep->aiBuf + 0x68)) {
        found = &g_rep->m1head;
        for (size_t i = 0; i < g_rep->m1nodes.size(); i++) {
            unsigned ek = (unsigned)g_rep->m1nodes[i]->key;
            if (ek == (unsigned)*key) {
                found = g_rep->m1nodes[i];
                break;
            }
            if (ek > (unsigned)*key) {
                break;
            }
        }
        if (replay_trace_level() >= 2) {
            fprintf(stderr, "lb1: key=%d size=%u found=%s val=%p\n", *key,
                    (unsigned)g_rep->m1nodes.size(),
                    found == &g_rep->m1head ? "head" : "node", found->value);
        }
        *out = (int)(size_t)found;
        return found;
    }
    if (g_rep != NULL &&
        map == (void *)((unsigned char *)g_rep->aiBuf + 0x74)) {
        m = &g_rep->m2;
    } else {
        m = (RepMap2 *)map;
    }
    found = &m->head;
    /* Нативный FUN_00434bc0 (lower_bound) + проверка @0x45ded2: занятым
     * считается только ТОЧНЫЙ ключ; lower_bound ушёл вправо → head (end). */
    for (size_t i = 0; i < m->nodes.size(); i++) {
        unsigned ek = (unsigned)m->nodes[i]->key;
        if (ek == (unsigned)*key) {
            found = m->nodes[i];
            break;
        }
        if (ek > (unsigned)*key) {
            break;                      /* exact-match нет → head */
        }
    }
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "lb: key=%d size=%u found=%s val=%p\n", *key,
                (unsigned)m->nodes.size(),
                found == &m->head ? "head" : "node", found->value);
    }
    *out = (int)(size_t)found;
    return found;
}
int tree_lookup(void *world, unsigned int key)
{
    (void)world;   /* synthetic world; name-keyed table from capture */
    rep_apply_world_scale();   /* v7: масштаб этого cb40-вызова до чтения @0x45c430 */
    if (g_grid == NULL || g_grid_error) return 0;
    std::map<unsigned int, int>::const_iterator it = g_grid->tree.find(key);
    int r = (it == g_grid->tree.end()) ? 0 : it->second;
    return r;
}
void register_occupied_slot(void *ai, LDObject *obj) {
    Rep *r = (Rep *)ai;
    PathState *ps = (PathState *)obj;
    LDPos *p;
    TreeNode *tn;
    TreeNode *m2tn;

    if (ps == NULL) return;
    /* Ресурсный предохранитель харнесса: при расхождении поиска порт может
     * начать лавинообразно регистрировать узлы. Ограничиваем рост map1 по
     * нативному размеру вызова, чтобы прогон завершился и показал divergence. */
    {
        /* Множитель cap (`LD_REPLAY_CAP_MULT`): cap не влияет на префикс
         * leaf- потока; по умолчанию 1 (cap без изменений). */
        static int mult = -1;
        if (mult < 0) {
            const char *s = getenv("LD_REPLAY_CAP_MULT");
            mult = (s != NULL && *s != '\0') ? atoi(s) : 1;
            if (mult < 1) mult = 1;
        }
        size_t cap = (g_cur != NULL) ? g_cur->map1.size() * 4 + 100 : 5000;
        if (cap < 500) cap = 500;
        cap *= (size_t)mult;
        if (r->m1nodes.size() >= cap) {
            g_reg_overflow = 1;
            return;
        }
    }
    p = ps->pos;
    /* FUN_0045dd00: объект в map1 (ключ = LDPos*, ключ узла = pos_addr) и в
     * map2 (ключ = координаты). map1 хранит сам объект — identity
     * cur->pos == cmp сравнивает указатели. */
    tn = new TreeNode;
    memset(tn, 0, sizeof(*tn));
    tn->value = ps;
    tn->is_nil = 0;
    {
        /* Ключ map1 = адрес LDPos (нативный dd00); у new-узла это адрес его
         * cb40-результата из replay_path_next. Захват без адресов LDPos — ключ 0,
         * порядок задаёт (x,y)-фолбэк в m1_less. */
        std::map<int *, unsigned long long>::const_iterator ai_it =
            g_obj_key.find((int *)(void *)p);
        unsigned long long full = (ai_it != g_obj_key.end()) ? ai_it->second : 0ULL;
        /* Игровой ключ узла остаётся 32-битным (раскладка TreeNode); полный
         * ключ нужен только для сортировки map1. */
        tn->key = (int)(unsigned)full;
        r->m1keys.push_back(full);
    }
    r->m1nodes.push_back(tn);
    m2tn = new TreeNode;
    memset(m2tn, 0, sizeof(*m2tn));
    m2tn->key = p ? occ_key_of(p) : 0;
    m2tn->value = obj;
    m2tn->is_nil = 0;
    /* v8: port's own registration is a tree-status mutation. replay_apply_chain_state
     * keeps this value instead of overwriting it with the captured status. */
    if (p != NULL) {
        g_port_tree[(unsigned)m2tn->key] = 1;
    }
    r->m2.nodes.push_back(m2tn);
    {
        RegEvent ev;
        ev.seq = g_cur ? g_cur->seq : -1;
        ev.x = p ? p->x : -1;
        ev.y = p ? p->y : -1;
        ev.pos_addr = (int)tn->key;
        ev.filter = ps->filter;
        ev.g = ps->g_cost;
        ev.cost = ps->metric;
        g_reg_events.push_back(ev);
        if (g_reg_events.size() > RP_REG_EVENT_MAX) {
            g_reg_events.erase(g_reg_events.begin());   /* накопление под потолок */
        }
    }
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "register: seq=%d obj=%p pos=(%d,%d) pos_addr=0x%x "
                        "filter=%d g=%d cost=%d key=%d m2size=%u\n",
                g_cur ? g_cur->seq : -1, (void *)obj, p ? p->x : -1,
                p ? p->y : -1, (unsigned)tn->key, ps->filter, ps->g_cost,
                ps->metric, m2tn->key, (unsigned)r->m2.nodes.size());
    }
    rep_rebuild_m1(r);
    rep_rebuild_m2(r);
    ((BattleAI *)(void *)r->aiBuf)->reserved_70 = (int)r->m1nodes.size();
}
int vcall0(void *receiver, unsigned int off)
{
    /* c5e0: vcall0(world,0x40) → obj1/obj2 (+0x244,+0x228),
     *        vcall0(obj3,0x48) → obj4 (+0x34). Значения сняты в `grid.world`. */
    if (g_grid == NULL || g_grid_error) return 0;
    if (receiver == (void *)g_worldbuf && off == 0x40) {
        *(int *)(g_obj2buf + 0x244) = (int)g_grid->a1;
        return (int)(size_t)g_obj2buf;
    }
    if (receiver == (void *)g_obj3buf && off == 0x48) {
        return (int)(size_t)g_obj4buf;
    }
    return 0;
}
int vcall1(void *receiver, unsigned int off, int arg) {
    if (off == 0x38 && arg != 0) {
        ((int *)arg)[0] = 0;
        ((int *)arg)[1] = 0;
        ((int *)arg)[2] = 0;
    }
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "vcall1: receiver=%p off=0x%x arg=%d\n",
                receiver, off, arg);
    }
    (void)receiver;
    return 0;
}
void search_params_update(void *ai) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "search_params_update: ai=%p\n", ai);
    }
    const Call *c = rep_cur();
    if (c != NULL && g_rep != NULL && ai == (void *)g_rep->aiBuf) {
        BattleAI *self = (BattleAI *)ai;
        memcpy(&self->final_threshold, &c->final_bits, 4);
        memcpy(&self->dist_threshold, &c->dist_bits, 4);
        self->y_radius = c->yrad;
        self->x_radius = c->xrad;
        self->canvas_rect.left = c->rect[0];
        self->canvas_rect.top = c->rect[1];
        self->canvas_rect.right = c->rect[2];
        self->canvas_rect.bottom = c->rect[3];
    }
}
void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "grid_anchor: grid=%p a1=%d a2=%d a3=%d a4=%d world=%p\n",
                grid, a1, a2, a3, a4, world);
    }
    if (flag_slot != NULL) {
        *flag_slot = 0;
    }
    const Call *c = rep_cur();
    if (c != NULL && c->is_eee0 &&
        (c->eee0_anchor_addr != 0 || c->eee0_anchor_pos[0] >= 0)) {
        unsigned long long key = (c->eee0_anchor_addr != 0)
            ? pos_key_addr(c->eee0_anchor_addr)
            : pos_key_coord(c->eee0_anchor_pos[0], c->eee0_anchor_pos[1]);
        /* Native 458050 returns a grid cell (GridCell*), and 45cb40 treats
         * it as such: path_search reads +0x20 key_rec / +0x28 chain_next.
         * Use the replay grid cell so those fields are real. */
        GridCell *cell = grid_cell_at(c->eee0_anchor_pos[0],
                                      c->eee0_anchor_pos[1]);
        if (cell != NULL) {
            g_pos_obj[key] = (int *)(void *)cell;
            g_obj_key[(int *)(void *)cell] = key;
            if (flag_slot != NULL) {
                *flag_slot = (*flag_slot & ~0xff) | 1;
            }
            return cell;
        }
        int *obj = rp_pos_obj(c->eee0_anchor_addr, c->eee0_anchor_pos[0],
                              c->eee0_anchor_pos[1]);
        if (c->eee0_has_b9 && !c->eee0_b9_null &&
            c->eee0_b9_w[3] == ((LDPos *)(void *)obj)->x &&
            c->eee0_b9_w[4] == ((LDPos *)(void *)obj)->y) {
            for (int k = 0; k < 5; k++) ((int *)obj)[k] = c->eee0_b9_w[k];
        }
        ((LDPos *)(void *)obj)->f14 = c->eee0_anchor_f14;
        g_pos_obj[key] = obj;
        g_obj_key[obj] = key;
        if (flag_slot != NULL) {
            *flag_slot = (*flag_slot & ~0xff) | 1;
        }
        return obj;
    }
    return NULL;
}
void band_free(void *band) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "band_free: band=%p\n", band);
    }
}
void native_45af10(void *band) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "native_45af10: band=%p\n", band);
    }
}
void clear_maps(void *ai) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "clear_maps: ai=%p\n", ai);
    }
    if (g_rep != NULL && ai == (void *)g_rep->aiBuf) {
        g_rep->m1nodes.clear();
        g_rep->m1keys.clear();
        rep_rebuild_m1(g_rep);
        g_rep->m2.nodes.clear();
        rep_rebuild_m2(g_rep);
        BattleAI *self = (BattleAI *)(void *)g_rep->aiBuf;
        self->cand_head = &g_rep->m1head;
        self->reserved_70 = 0;
        self->map2 = (void *)&g_rep->m2;
        self->occ_head = &g_rep->m2.head;
        *(int *)((unsigned char *)g_rep->aiBuf + 0x7c) = 0;
    }
}
int set_rect_empty(void *rect) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "set_rect_empty: rect=%p\n", rect);
    }
    if (rect != NULL) {
        ((int *)rect)[0] = 0;
        ((int *)rect)[1] = 0;
        ((int *)rect)[2] = 0;
        ((int *)rect)[3] = 0;
    }
    return 1;
}
void band_rebuild(void *ai, void *node, int bits) {
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "band_rebuild: ai=%p node=%p bits=0x%x\n", ai, node, bits);
    }
}
void *target_search(void *ai, int filter, void *cmp_pos, int arg3, int *out_code)
{
    return s6_target_search(ai, (void *)(size_t)filter, cmp_pos,
                            (void *)(size_t)arg3, out_code);
}
long av_count(void) { return 0; }
int av_filter(void) { return EXCEPTION_EXECUTE_HANDLER; }
int grid_query(void *world, int a1, int a2, int a3, int a4)
{
    (void)world; (void)a1; (void)a2; (void)a3; (void)a4;
    /* FUN_0044bbf0 — внешний мирный запрос (vtable/массивы мира). Его_RETURN
     * проходит через c5e0 без изменений; харнесс подаёт его как внешний вход,
     * чтобы арифметика и гейты FUN_0045c5e0/c690 выполнялись по-настоящему. */
    const Call *c = rep_cur();
    if (c != NULL && g_hi < c->leaves_heur.size()) {
        return c->leaves_heur[g_hi++];
    }
    g_replay_leaf_underflow = 1;
    return 0;
}
int pt_in_rect(const int *rect, int x, int y) {
    return (rect[0] <= x && x < rect[2] && rect[1] <= y && y < rect[3]) ? 1 : 0;
}
void *operator_new(unsigned int size) { return calloc(1, size); }
} /* namespace native */

/* Логгер/утилиты, нужные battle/*.cpp. LD_REPLAY_TRACE=1/2 включает вывод
 * порта в stderr (1 — info, 2 — info+debug), по умолчанию тишина. */
volatile LONG g_log_level = 0;
static int replay_trace_level(void) {
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("LD_REPLAY_TRACE");
        v = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    return v;
}
static void replay_trace(const char *fmt, va_list ap) {
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void ld_info(const char *fmt, ...) {
    va_list ap;
    if (replay_trace_level() < 1) return;
    va_start(ap, fmt);
    replay_trace(fmt, ap);
    va_end(ap);
}
void ld_debug(const char *fmt, ...) {
    va_list ap;
    if (replay_trace_level() < 2) return;
    va_start(ap, fmt);
    replay_trace(fmt, ap);
    va_end(ap);
}
int ld_rate(volatile LONG *ctr, LONG first, LONG each) {
    (void)ctr; (void)first; (void)each;
    return (replay_trace_level() >= 2) ? 1 : 0;   /* LD_REPLAY_TRACE=2 → всё */
}
LONGLONG ld_now_us(void) { return 0; }
void battle_trace_e0a0_enter(void *this_, int filter) { (void)this_; (void)filter; }
void battle_trace_e0a0_exit(void *node) { (void)node; }
void battle_trace_e0a0_latch(void *cand) { (void)cand; }
void battle_trace_visit(void) {}
void battle_capture_e0a0_enter(void *, int, void *, void *, void *) {}
void battle_capture_latch(void *) {}
void battle_capture_e0a0_exit(void *) {}

/* ------------------------------- вывод ---------------------------------- */

static void write_out_node(FILE *f, int x, int y, bool is_null) {
    if (is_null) fputs("null", f);
    else fprintf(f, "[%d,%d]", x, y);
}

static void write_out_map1(FILE *f, Rep *r) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *head = ai->cand_head;
    TreeNode *cur;
    bool first = true;
    fputc('[', f);
    if (head != NULL) {
        cur = head->left;
        for (size_t guard = 0; cur != NULL && cur != head && guard < 200000;
             guard++) {
            PathState *ps = (PathState *)cur->value;
            if (ps != NULL) {
                LDPos *p = ps->pos;
                int x = p ? p->x : -1, y = p ? p->y : -1;
                PathState *pp = ps->parent;
                LDPos *ppp = pp ? pp->pos : NULL;
                fprintf(f, "%s{\"pos\":[%d,%d],\"filter\":%d,\"g\":%d,"
                        "\"d2\":%d,\"h10k\":%d,\"cost\":%d,\"parent\":[%d,%d],"
                        "\"nbr\":[",
                        first ? "" : ",", x, y, ps->filter, ps->g_cost,
                        ps->dist2, ps->h10000, ps->metric,
                        ppp ? ppp->x : -1, ppp ? ppp->y : -1);
                for (int k = 0; k < 9; k++) {
                    PathState *nb = ps->nbr[k];
                    LDPos *nbp = nb ? nb->pos : NULL;
                    fprintf(f, "%s[%d,%d]", k ? "," : "",
                            nbp ? nbp->x : -1, nbp ? nbp->y : -1);
                }
                fprintf(f, "],\"occ_key\":%d}",
                        (int)(((unsigned short)y << 16) | (unsigned short)x));
                first = false;
            }
            cur = s6_tree_next(cur);
        }
    }
    fputc(']', f);
}

static void write_out_occ(FILE *f, Rep *r) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *head = ai->occ_head;
    TreeNode *cur;
    bool first = true;
    fputc('[', f);
    if (head != NULL) {
        cur = head->left;
        for (size_t guard = 0; cur != NULL && cur != head && guard < 200000;
             guard++) {
            fprintf(f, "%s%d", first ? "" : ",", cur->key);
            first = false;
            cur = s6_tree_next(cur);
        }
    }
    fputc(']', f);
}

static void write_res_port(FILE *f, const Call &c, Rep *r) {
    int out_code = 0;
    fprintf(f, "{\"v\":1,\"kind\":\"res_port\",\"seq\":%d,\"node\":", c.seq);
    write_out_node(f, g_exit_node[0], g_exit_node[1], g_exit_node[0] == -1);
    /* s6_target_search пишет код в out_code; дублируем из replay-нотиса. */
    (void)out_code;
    fprintf(f, ",\"code\":%d,\"iters\":%d,\"iter_seq\":[",
            g_exit_code, g_exit_iters);
    for (size_t i = 0; i < g_iter_seq.size(); i += 2) {
        bool null = (g_iter_seq[i] == -1 && g_iter_seq[i + 1] == -1);
        fprintf(f, "%s", i ? "," : "");
        write_out_node(f, g_iter_seq[i], g_iter_seq[i + 1], null);
    }
    fputs("],\"map1_after\":", f);
    write_out_map1(f, r);
    fputs(",\"occ_after\":", f);
    write_out_occ(f, r);
    /* after-state скалярного блока BattleAI (симметрично нативному полю,
     * если захват его пишет). */
    {
        unsigned int aw[RP_AI_WORDS];
        int k;
        memcpy(aw, r->aiBuf, sizeof(aw));
        fputs(",\"ai_words_after\":[", f);
        for (k = 0; k < RP_AI_WORDS; k++) {
            fprintf(f, "%s%u", k ? "," : "", aw[k]);
        }
        fputc(']', f);
    }
    fputs(",\"leaf\":{\"path\":", f);
    fputc('[', f);
    for (size_t i = 0; i < c.port_path.size(); i++) {
        fprintf(f, "%s", i ? "," : "");
        if (c.port_path[i].null) {
            fputs("null", f);
        } else {
            fputc('[', f);
            for (int k = 0; k < 5; k++) {
                fprintf(f, "%s%d", k ? "," : "", c.port_path[i].w[k]);
            }
            fputc(']', f);
        }
    }
    fputs("],\"heur\":[", f);
    for (size_t i = 0; i < c.port_heur.size(); i++) {
        fprintf(f, "%s%d", i ? "," : "", c.port_heur[i]);
    }
    fputs("],\"timeout\":[", f);
    for (size_t i = 0; i < c.port_timeout.size(); i++) {
        fprintf(f, "%s%d", i ? "," : "", c.port_timeout[i]);
    }
    fputs("],\"gate\":[", f);
    for (int i = 0; i < g_gi; i++) {
        fprintf(f, "%s[", i ? "," : "");
        for (int k = 0; k < 60; k++) {
            fprintf(f, "%s%d", k ? "," : "", g_gate_rec[i][k]);
        }
        fputc(']', f);
    }
    fputs("]},\"leaf_mode\":", f);
    fputs(g_replay_override ? "\"replay\"" : "\"real\"", f);
    fprintf(f, ",\"leaf_used\":[%u,%u,%u],\"leaf_cap\":[%u,%u,%u]"
            ",\"leaf_underflow\":%d,\"reg_overflow\":%d}\n",
            (unsigned)g_pi, (unsigned)g_hi, (unsigned)g_ti,
            (unsigned)c.leaves_path.size(), (unsigned)c.leaves_heur.size(),
            (unsigned)c.leaves_timeout.size(), g_replay_leaf_underflow,
            g_reg_overflow);
}

/* Дамп расхождения (`LD_REPLAY_DIV_DUMP=1`): что харнесс-сетка
 * отдаёт на координатах первого расхождения, и сколько map1-узлов без адреса
 * из захвата (полный ключ 0 → порядок задаёт (x,y)-фолбэк). */
static int replay_div_dump_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("LD_REPLAY_DIV_DUMP");
        v = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    return v;
}

static void replay_dump_div(const Call &c, Rep *r)
{
    size_t i = 0, k;
    while (i < c.leaves_path.size() && i < c.port_path.size() &&
           path_leaf_eq(c.leaves_path[i], c.port_path[i])) i++;
    const PathLeaf *nat = (i < c.leaves_path.size()) ? &c.leaves_path[i] : NULL;
    const PathLeaf *prt = (i < c.port_path.size()) ? &c.port_path[i] : NULL;
    int x = nat ? nat->w[3] : -1, y = nat ? nat->w[4] : -1;
    GridCell *cell = grid_cell_at(x, y);
    unsigned key = (unsigned)(((unsigned short)y << 16) | (unsigned short)x);
    int rowidx = -1, status = 0, minb = 0, maxb = 0, lvl = 0;
    size_t zero = 0, coord = 0;
    if (g_grid != NULL && !g_grid_error) {
        if (x >= 0 && y >= 0 && x < g_grid->sx && y < g_grid->sy)
            rowidx = g_grid->rowptr[x][y];
        std::map<unsigned int, int>::const_iterator it = g_grid->tree.find(key);
        status = (it == g_grid->tree.end()) ? 0 : it->second;
        if (cell != NULL) {
            minb = *(int *)((unsigned char *)cell + 0x14);
            maxb = *(int *)((unsigned char *)cell + 0x18);
            lvl = *(int *)((unsigned char *)cell + 0x1c);
        }
    }
    for (k = 0; k < r->m1keys.size(); k++) {
        if (r->m1keys[k] == 0ULL) zero++;
        if (r->m1keys[k] >= 0x100000000ULL) coord++;   /* ключ 0x100000000|occ_key */
    }
    fprintf(stderr, "DIV seq=%d idx=%u nat=%s port=%s cell=(%d,%d) row_idx=%d "
                    "min_bits=0x%x max_bits=0x%x status=%d level=0x%x "
                    "m1_nodes=%u m1_key_zero=%u m1_key_coord=%u "
                    "fill_cell=%d fill_leaf=%d fill_stub=%d "
                    "underflow=%d overflow=%d\n",
            c.seq, (unsigned)i,
            (nat == NULL) ? "EOF" : (nat->null ? "null" : path_leaf_str(*nat).c_str()),
            (prt == NULL) ? "EOF" : (prt->null ? "null" : path_leaf_str(*prt).c_str()),
            x, y, rowidx, minb, maxb, status, (unsigned)lvl,
            (unsigned)r->m1nodes.size(), (unsigned)zero, (unsigned)coord,
            g_fill_cell, g_fill_leaf, g_fill_stub,
            g_replay_leaf_underflow, g_reg_overflow);
    /* Состояние финального гейта порта на этом вызове cb40 (запись индекса i
     * совпадает с индексом port_path): spec[+0x10]=out_metric, +0x28/-1e7,
     * +0x2c/+1e7; gate[] = out_metric, L[0x10], L[0x2c], L[0x14], L[0x18],
     * thr, reason, res, best_min_bits, best_max_bits, best_key, n_chain,
     * p3, p7, L[0x40], &out. */
    if (i < (size_t)g_gi) {
        const int *g = g_gate_rec[i];
        fprintf(stderr, "GATE seq=%d idx=%u out_bits=0x%x min_slot_bits=0x%x "
                        "maxp_bits=0x%x minp_bits=0x%x max_pair_bits=0x%x "
                        "thr_bits=0x%x reason=%d res=%d best_min=0x%x "
                        "best_max=0x%x best_key=%d chain=%d p3=0x%x p7=%d "
                        "spec_out=0x%x spec_in_out=0x%x spec_in_min=0x%x "
                        "spec_in_max=0x%x\n",
                c.seq, (unsigned)i, g[44], g[45], g[46], g[47], g[48], g[49],
                g[50], g[51], g[52], g[53], g[54], g[55], g[56], g[57],
                g[58], g[59], g[11], g[12]);
    }
    /* v6: native gate inputs (captured at cb40 return) vs the port's record.
     * g[44]=out_metric, g[49]=thr, g[45]=min_slot(L[0x10]), g[48]=max_pair(L[0x18]). */
    if (nat != NULL && nat->gate.present) {
        if (i < (size_t)g_gi) {
            const int *g = g_gate_rec[i];
            fprintf(stderr, "GATECMP seq=%d idx=%u native[out=0x%x thr=0x%x "
                            "min=0x%x max=0x%x] port[out=0x%x thr=0x%x "
                            "min_slot=0x%x max_pair=0x%x] reason=%d\n",
                    c.seq, (unsigned)i,
                    nat->gate.v[0], nat->gate.v[1], nat->gate.v[2], nat->gate.v[3],
                    (unsigned)g[44], (unsigned)g[49], (unsigned)g[45],
                    (unsigned)g[48], g[50]);
        } else {
            fprintf(stderr, "GATECMP seq=%d idx=%u native[out=0x%x thr=0x%x "
                            "min=0x%x max=0x%x] port=NO_RECORD\n",
                    c.seq, (unsigned)i,
                    nat->gate.v[0], nat->gate.v[1], nat->gate.v[2], nat->gate.v[3]);
        }
        g_gate_compared++;
    } else {
        g_gate_skipped++;
        fprintf(stderr, "GATECMP seq=%d idx=%u native=NO_GATE (old capture)\n",
                c.seq, (unsigned)i);
    }
}

/* --------------------------------- main --------------------------------- */

static bool read_line(FILE *f, std::string &out) {
    out.clear();
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') return true;
        out.push_back((char)ch);
    }
    out.clear();          /* хвостовая недописанная запись (игра погашена
                             посреди записи) не является строкой захвата */
    return false;
}

int main(int argc, char **argv) {
    const char *inpath = NULL, *outpath = NULL;
    FILE *fin, *fout;
    std::string line;
    int rc = 0, npos = 0;
    bool chained = false, leaf_replay = false;
    std::map<int, Call *> caps;
    std::map<int, Call *> eee0_caps;
    std::vector<Call *> calls;
    std::vector<Call *> eee0_calls;
    Call *cur_eee0 = NULL;
    char err[512];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--chained") == 0) {
            chained = true;
        } else if (strcmp(argv[i], "--leaf-replay") == 0) {
            leaf_replay = true;            /* режим подмены нативных листьев из захвата */
        } else if (strcmp(argv[i], "--real-path") == 0) {
            leaf_replay = false;           /* по умолчанию: реальный прогон листьев порта */
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "ERROR: unknown flag '%s'\n", argv[i]);
            return 2;
        } else if (npos < 2) {
            if (npos == 0) inpath = argv[i]; else outpath = argv[i];
            npos++;
        } else {
            fprintf(stderr, "ERROR: too many arguments\n");
            return 2;
        }
    }
    if (npos != 2) {
        fprintf(stderr, "usage: battle_replay [--chained] [--leaf-replay|--real-path] "
                        "<capture.jsonl> <res_port.jsonl>\n");
        return 2;
    }
    g_replay_override = leaf_replay ? 1 : 0;
    if (replay_trace_level() >= 2) {
        g_log_level = 2;                /* LD_DEBUG (ldhook_internal.h) */
    }
    fin = fopen(inpath, "rb");
    if (fin == NULL) {
        fprintf(stderr, "ERROR: cannot open capture '%s'\n", inpath);
        return 2;
    }
    fout = fopen(outpath, "wb");
    if (fout == NULL) {
        fprintf(stderr, "ERROR: cannot open output '%s'\n", outpath);
        fclose(fin);
        return 2;
    }
    try {
        while (read_line(fin, line)) {
            JVal v = JParser(line).parse();
            const JVal &kv = jget(v, "kind", "record");
            if (kv.t != JVal::STR) {
                throw std::runtime_error("record.kind: expected string");
            }
            std::string kind = kv.s;
            if (kind == "grid") {
                replay_grid_build(v);
                continue;
            }
            int seq = (int)jint(v, "seq", "record");
            if (kind == "eee0_cap") {
                Call *c = new Call;
                c->cap = v;
                c->seq = seq;
                c->tick = (int)jint_opt(v, "tick", -1);
                c->unit = (int)jint_opt(v, "unit", -1);
                read_eee0(*c, v, false);
                read_eee0_state(*c, v);
                eee0_caps[seq] = c;
                eee0_calls.push_back(c);
                cur_eee0 = c;
            } else if (kind == "eee0_res") {
                Call *c = (eee0_caps.count(seq) && eee0_caps[seq]->is_eee0)
                          ? eee0_caps[seq]
                          : ((cur_eee0 != NULL && (cur_eee0->seq == seq || cur_eee0->seq + 1 == seq))
                                 ? cur_eee0 : NULL);
                if (c == NULL || !c->is_eee0) {
                    throw std::runtime_error("eee0_res without eee0_cap");
                }
                read_eee0(*c, v, true);
                cur_eee0 = NULL;
            } else if (kind == "cap") {
                if (caps.count(seq)) throw std::runtime_error("duplicate cap seq");
                Call *c = new Call;
                c->cap = v;
                read_call(*c, v, NULL);
                caps[seq] = c;
                calls.push_back(c);
                if (cur_eee0 != NULL) {
                    /* v9: nested e0a0 calls mutate the shared grid; carry
                     * their deltas and world gate into the enclosing tick. */
                    cur_eee0->tree_delta.insert(cur_eee0->tree_delta.end(),
                                                c->tree_delta.begin(),
                                                c->tree_delta.end());
                    cur_eee0->cell_delta.insert(cur_eee0->cell_delta.end(),
                                                c->cell_delta.begin(),
                                                c->cell_delta.end());
                    cur_eee0->row_delta.insert(cur_eee0->row_delta.end(),
                                               c->row_delta.begin(),
                                               c->row_delta.end());
                    cur_eee0->eee0_timer_restart.push_back(c->timer_bits);
                    /* B8 search_params_update is a native call that reads
                     * world/grid state; use the values captured at the first
                     * nested e0a0 entry as its result. */
                    cur_eee0->final_bits = c->final_bits;
                    cur_eee0->dist_bits = c->dist_bits;
                    cur_eee0->yrad = c->yrad;
                    cur_eee0->xrad = c->xrad;
                    for (int k = 0; k < 4; k++) cur_eee0->rect[k] = c->rect[k];
                    cur_eee0->w110 = c->w110;
                }
            } else if (kind == "res") {
                if (!caps.count(seq)) throw std::runtime_error("res without cap");
                Call *c = caps[seq];
                if (c->has_res) throw std::runtime_error("duplicate res seq");
                c->has_res = true;
                c->res = v;
                read_leaves(*c, v);
                if (cur_eee0 != NULL && !c->is_eee0) {
                    cur_eee0->leaves_path.insert(cur_eee0->leaves_path.end(),
                                                 c->leaves_path.begin(),
                                                 c->leaves_path.end());
                    cur_eee0->leaves_heur.insert(cur_eee0->leaves_heur.end(),
                                                 c->leaves_heur.begin(),
                                                 c->leaves_heur.end());
                    cur_eee0->leaves_timeout.insert(cur_eee0->leaves_timeout.end(),
                                                    c->leaves_timeout.begin(),
                                                    c->leaves_timeout.end());
                    cur_eee0->leaves_timer.insert(cur_eee0->leaves_timer.end(),
                                                  c->leaves_timer.begin(),
                                                  c->leaves_timer.end());
                    cur_eee0->leaves_scale.insert(cur_eee0->leaves_scale.end(),
                                                  c->leaves_scale.begin(),
                                                  c->leaves_scale.end());
                    cur_eee0->leaves_scale_ok.insert(cur_eee0->leaves_scale_ok.end(),
                                                     c->leaves_scale_ok.begin(),
                                                     c->leaves_scale_ok.end());
                    cur_eee0->leaves_chain.insert(cur_eee0->leaves_chain.end(),
                                                  c->leaves_chain.begin(),
                                                  c->leaves_chain.end());
                }
            } else if (kind == "error") {
                throw std::runtime_error("capture contains error record");
            }
        }
    } catch (const std::exception &e) {
        _snprintf_s(err, sizeof(err), _TRUNCATE, "ERROR: bad capture: %s", e.what());
        fprintf(stderr, "%s\n", err);
        fclose(fin); fclose(fout);
        return 2;
    }
    fclose(fin);
    if (!leaf_replay && (g_grid == NULL || g_grid_error)) {
        fprintf(stderr, "ERROR: capture has no usable 'grid' record "
                        "(old v1..v4 or truncated) — real pathfinding needs "
                        "a v5 capture or --leaf-replay\n");
        fclose(fout);
        return 2;
    }

    /* v9: per-call replay FUN_0045eee0 (s6_unit_ai_tick) */
    if (!eee0_calls.empty()) {
        int eee0_calls_n = 0, eee0_equal_n = 0, eee0_first_div_seq = -1;
        std::string eee0_first_div;
        int div_dump = replay_div_dump_on();
        Rep *r_eee0 = chained ? new Rep : NULL;
        for (size_t i = 0; i < eee0_calls.size(); i++) {
            Call *c = eee0_calls[i];
            if (!c->has_res) {
                if (i + 1 == eee0_calls.size()) {
                    fprintf(stderr, "WARN: trailing eee0_cap seq=%d without eee0_res ignored\n",
                            c->seq);
                    break;
                }
                fprintf(stderr, "ERROR: eee0_cap seq=%d has no eee0_res record\n", c->seq);
                if (rc == 0) rc = 2;
                break;
            }
            if (!chained) r_eee0 = new Rep;
            try {
                if (!leaf_replay && g_grid != NULL && !g_grid_error) {
                    for (size_t d = 0; d < c->cell_delta.size(); d++) {
                        replay_apply_cell_delta(g_grid, c->cell_delta[d]);
                    }
                    for (size_t d = 0; d < c->row_delta.size(); d++) {
                        int idx = c->row_delta[d].first;
                        if (idx >= 0 && idx < (int)g_grid->n) {
                            g_grid->rowdata[idx] = c->row_delta[d].second;
                        }
                    }
                    for (size_t d = 0; d < c->tree_delta.size(); d++) {
                        int idx = c->tree_delta[d].first;
                        if (idx >= 0 && idx < (int)g_grid->cell_keys.size()) {
                            g_grid->tree[(unsigned int)g_grid->cell_keys[idx]] =
                                c->tree_delta[d].second;
                        }
                    }
                }
                if (chained && i > 0) {
                    rep_rebind(r_eee0, *c);
                    if (c->eee0_grid_ok) {
                        int *gobj = rp_pos_obj(c->eee0_s28, c->eee0_grid_w[3],
                                               c->eee0_grid_w[4]);
                        for (int k = 0; k < 8; k++) {
                            if (k != 7) gobj[k] = c->eee0_grid_w[k];
                        }
                        if (gobj[7] == 0) {
                            gobj[7] = (int)(size_t)&g_grid->levels[g_grid->leaf];
                        }
                        *(int *)((unsigned char *)r_eee0->aiBuf + 0x28) = (int)(size_t)gobj;
                    }
                    if (c->eee0_anchor_addr != 0 || c->eee0_anchor[0] >= 0) {
                        unsigned long long akey =
                            (c->eee0_anchor_addr != 0)
                            ? pos_key_addr(c->eee0_anchor_addr)
                            : pos_key_coord(c->eee0_anchor[0], c->eee0_anchor[1]);
                        int *aobj = rp_pos_obj(c->eee0_anchor_addr, c->eee0_anchor[0],
                                               c->eee0_anchor[1]);
                        g_pos_obj[akey] = aobj;
                        g_obj_key[aobj] = akey;
                    }
                } else {
                    rep_build(r_eee0, *c);
                }
                BattleAI *ai = (BattleAI *)(void *)r_eee0->aiBuf;
                ai->tracking = (unsigned char)c->eee0_s30;
                ai->distance_threshold = (unsigned int)c->eee0_s34;
                *(int *)((unsigned char *)ai + 0x38) = c->eee0_s38;
                /* B7 falls back to world[0x134] for the grid root. Point it at
                 * the synthetic grid object built from eee0_cap.ai.grid. */
                if (c->eee0_grid_ok) {
                    *(unsigned int *)(g_worldbuf + 0x134) =
                        *(unsigned int *)(r_eee0->aiBuf + 0x28);
                } else {
                    *(unsigned int *)(g_worldbuf + 0x134) = 0;
                }

                int sub_buf[4];
                int rec_buf[7];
                for (int k = 0; k < 4; k++) sub_buf[k] = c->eee0_sub_w[k];
                for (int k = 0; k < 7; k++) rec_buf[k] = c->eee0_rec_w[k];
                rec_buf[1] = (int)(size_t)(void *)g_worldbuf;  /* native world ptr -> synthetic */
                rec_buf[2] = (g_grid != NULL && !g_grid_error)
                    ? (int)(size_t)(void *)&g_grid->levels[0] : 0; /* grid level */
                rec_buf[3] = (int)(size_t)sub_buf;

                g_rep = r_eee0;
                g_cur = c;
                if (!chained) g_reg_events.clear();
                g_exit_node[0] = -1; g_exit_node[1] = -1;
                g_exit_code = -1; g_exit_iters = -1;
                g_have_exit = false;
                g_pi = 0; g_gi = 0; g_hi = 0; g_ti = 0; g_ms_i = 0; g_ms_ri = 0;
                g_iter_seq.clear();
                g_replay_leaf_underflow = 0;
                g_reg_overflow = 0;

                int port_ret = s6_unit_ai_tick(ai, (void *)rec_buf);
                int port_s38 = *(const int *)((const unsigned char *)ai + 0x38);
                int port_s2c = (int)(size_t)ai->anchor;
                int port_s30 = (int)ai->tracking;
                unsigned int port_s34 = ai->distance_threshold;
                int port_ax = -1, port_ay = -1;
                if (ai->anchor != NULL) {
                    port_ax = ai->anchor->x;
                    port_ay = ai->anchor->y;
                }

                std::string div_why;
                if (port_ret != c->eee0_ret) {
                    char b[128];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                                "ret native=%d port=%d", c->eee0_ret, port_ret);
                    div_why += b;
                }
                if (port_s38 != c->eee0_s38_after) {
                    char b[128];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                                " %ss38 native=%d port=%d", div_why.empty() ? "" : ";",
                                c->eee0_s38_after, port_s38);
                    div_why += b;
                }
                if (port_ax != c->eee0_anchor_pos[0] ||
                    port_ay != c->eee0_anchor_pos[1]) {
                    char b[192];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                                " %ss2c native=%d(%d,%d) port=%d(%d,%d)",
                                div_why.empty() ? "" : ";",
                                c->eee0_s2c_after, c->eee0_anchor_pos[0],
                                c->eee0_anchor_pos[1], port_s2c, port_ax, port_ay);
                    div_why += b;
                }
                if (port_s30 != c->eee0_s30_after) {
                    char b[128];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                                " %ss30 native=%d port=%d", div_why.empty() ? "" : ";",
                                c->eee0_s30_after, port_s30);
                    div_why += b;
                }
                if (port_s34 != c->eee0_s34_after) {
                    char b[128];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                                " %ss34 native=%u port=%u", div_why.empty() ? "" : ";",
                                c->eee0_s34_after, port_s34);
                    div_why += b;
                }
                AState nat_st, port_st;
                after_from_res(c->res, nat_st);
                after_from_rep(r_eee0, port_st);
                /* Replay LDPos carriers are synthetic; compare map1 by
                 * coordinates, not by the native pos_addr identity. */
                for (size_t k = 0; k < nat_st.map1.size(); k++) nat_st.map1[k].pos_addr = 0;
                for (size_t k = 0; k < port_st.map1.size(); k++) port_st.map1[k].pos_addr = 0;
                std::string st_diff = diff_after(nat_st, port_st, c->seq, NULL, NULL, false);
                if (!st_diff.empty()) {
                    div_why += st_diff;
                }

                eee0_calls_n++;
                if (div_why.empty()) {
                    eee0_equal_n++;
                } else {
                    if (eee0_first_div_seq < 0) {
                        eee0_first_div_seq = c->seq;
                        eee0_first_div = div_why;
                    }
                    if (div_dump) {
                        fprintf(stderr, "EEE0_DIV seq=%d ret=%d/%d s38=%d/%d "
                                        "s2c=%d/%d(%d,%d)/(%d,%d) s30=%d/%d s34=%u/%u "
                                        "m1=%u/%u occ=%u/%u%s\n",
                                c->seq, c->eee0_ret, port_ret,
                                c->eee0_s38_after, port_s38,
                                c->eee0_s2c_after, port_s2c,
                                c->eee0_anchor_pos[0], c->eee0_anchor_pos[1],
                                port_ax, port_ay,
                                c->eee0_s30_after, port_s30,
                                c->eee0_s34_after, port_s34,
                                (unsigned)nat_st.map1.size(),
                                (unsigned)port_st.map1.size(),
                                (unsigned)nat_st.occ.size(),
                                (unsigned)port_st.occ.size(),
                                div_why.c_str());
                    }
                    if (rc == 0) rc = 1;
                }
            } catch (const std::exception &e) {
                fprintf(stderr, "ERROR: eee0 replay seq=%d failed: %s\n", c->seq, e.what());
                if (rc == 0) rc = 2;
            }
            g_rep = NULL;
            g_cur = NULL;
            if (!chained) {
                delete r_eee0;
                r_eee0 = NULL;
            }
        }
        if (chained && r_eee0 != NULL) {
            delete r_eee0;
            r_eee0 = NULL;
        }
        printf("EEE0: calls=%d equal=%d first_div_seq=%d %s\n",
               eee0_calls_n, eee0_equal_n, eee0_first_div_seq,
               eee0_first_div.empty() ? "" : eee0_first_div.c_str());
    }

    {
        Rep *r = chained ? new Rep : NULL;
        int complete = 0, compared = 0, chain_len = 0, first_div_seq = -1;
        int entry_mismatch = 0, first_entry_seq = -1;
        int chain_matched = 0, ai_compared = 0, ai_skipped = 0, ai_mismatch = 0;
        int leaf_calls = 0, leaf_equal = 0, res_calls = 0, res_equal = 0;
        int first_leaf_seq = -1, first_res_seq = -1;
        LeafCounts totals;
        std::string first_class, first_detail, first_entry_detail;
        std::string first_leaf_detail, first_res_detail;
        for (size_t i = 0; i < calls.size(); i++) {
            Call *c = calls[i];
            if (c->has_res) complete++;
        }
        for (size_t i = 0; i < calls.size(); i++) {
            Call *c = calls[i];
            if (!c->has_res) {
                if (i + 1 == calls.size()) {
                    /* Захват обрезан при гашении игры — последний вызов без res. */
                    fprintf(stderr, "WARN: trailing cap seq=%d without res ignored\n",
                            c->seq);
                    break;
                }
                fprintf(stderr, "ERROR: cap seq=%d has no res record\n", c->seq);
                rc = 2;
                break;
            }
            if (!chained) r = new Rep;
            try {
                if (!leaf_replay && c->tree_overflow) {
                    throw std::runtime_error("tree_delta overflow in cap");
                }
                if (!leaf_replay && c->cell_overflow) {
                    throw std::runtime_error("cell_delta overflow in cap");
                }
                if (!leaf_replay && g_grid != NULL && !g_grid_error) {
                    for (size_t d = 0; d < c->cell_delta.size(); d++) {
                        replay_apply_cell_delta(g_grid, c->cell_delta[d]);
                    }
                    for (size_t d = 0; d < c->row_delta.size(); d++) {
                        int idx = c->row_delta[d].first;
                        if (idx >= 0 && idx < (int)g_grid->n) {
                            g_grid->rowdata[idx] = c->row_delta[d].second;
                        }
                    }
                    for (size_t d = 0; d < c->tree_delta.size(); d++) {
                        int idx = c->tree_delta[d].first;
                        if (idx >= 0 && idx < (int)g_grid->cell_keys.size()) {
                            g_grid->tree[(unsigned int)g_grid->cell_keys[idx]] =
                                c->tree_delta[d].second;
                        }
                    }
                }
                if (chained && i > 0) {
                    rep_rebind(r, *c);       /* feed the PORT's own state forward */
                } else {
                    rep_build(r, *c);        /* initial state / non-chained mode */
                }
                /* Chained:.compare carried entry state with the native cap.
                 * A mismatch here means the game reset its candidate maps
                 * between calls — then a chain divergence is NOT a port
                 * mutation defect. g_reg_events still holds the previous
                 * call's insertions, so branch provenance is available. */
                if (chained && i > 0) {
                    AState capst, repst;
                    after_from_cap(*c, capst);
                    after_from_rep(r, repst);
                    std::string ed = diff_after(capst, repst, c->seq);
                    if (!ed.empty()) {
                        entry_mismatch++;
                        if (first_entry_seq < 0) {
                            first_entry_seq = c->seq;
                            first_entry_detail = ed;
                        }
                    }
                }
                g_rep = r;
                /* Chained: события накапливаются — carried-узлы были зарегистрированы
                 * в прошлых seq, их reg-события нужны reg_branch_for. Non-chained:
                 * реплика на каждый вызов, события не нужны — чистим на seq. */
                if (!chained) g_reg_events.clear();
                BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
                void *cmpPos = (void *)r->cmpPtr;
                int out_code = 0;
                s6_target_search(ai, (void *)(size_t)c->filter, cmpPos,
                                 (void *)(size_t)c->timeout_bits, &out_code);
                write_res_port(fout, *c, r);
                if (!leaf_replay) {
                    /* v5 gate: результат + пор-produced leaf-поток. */
                    NResult nat;
                    nres_read(c->res, nat);
                    std::string rw;
                    bool req = result_equal(nat, g_exit_node[0], g_exit_node[1],
                                            g_exit_code, g_exit_iters,
                                            g_iter_seq, rw);
                    res_calls++;
                    if (req) res_equal++;
                    else if (first_res_seq < 0) {
                        first_res_seq = c->seq;
                        first_res_detail = rw;
                    }
                    std::string lw;
                    bool leq = compare_leaf_stream(*c, &totals, lw);
                    leaf_calls++;
                    if (leq) leaf_equal++;
                    else if (first_leaf_seq < 0) {
                        first_leaf_seq = c->seq;
                        first_leaf_detail = lw;
                    }
                    if (!req || !leq) {
                        if (replay_div_dump_on()) replay_dump_div(*c, r);
                        fprintf(stderr, "DIVERGE seq=%d result=%s leaf=%s%s%s\n",
                                c->seq, req ? "equal" : "DIFF",
                                leq ? "equal" : "DIFF",
                                req ? "" : " result:", req ? "" : rw.c_str());
                        if (!leq) fprintf(stderr, "  leaf: %s\n", lw.c_str());
                        if (rc == 0) rc = 1;
                    }
                }
                if (chained) {
                    NResult nat;
                    nres_read(c->res, nat);
                    std::string why;
                    bool res_eq = result_equal(nat, g_exit_node[0], g_exit_node[1],
                                               g_exit_code, g_exit_iters,
                                               g_iter_seq, why);
                    AState anat, apost;
                    after_from_res(c->res, anat);
                    after_from_rep(r, apost);
                    int ai_diff = 0; bool ai_comp = false;
                    std::string ad = diff_after(anat, apost, c->seq,
                                                &ai_diff, &ai_comp);
                    bool after_eq = ad.empty();
                    compared++;
                    chain_len++;
                    if (ai_comp) { ai_compared++; ai_mismatch += ai_diff; }
                    else { ai_skipped++; }
                    if (res_eq && after_eq) {
                        /* survived = число цепных вызовов, совпавших подряд до
                         * первого расхождения. */
                        if (first_div_seq < 0) chain_matched++;
                    } else {
                        const char *cls = res_eq ? "MUTATE" : "RESULT";
                        fprintf(stderr, "DIVERGE seq=%d class=%s result=%s "
                                        "after=%s leaf_underflow=%d\n",
                                c->seq, cls, res_eq ? "equal" : "DIFF",
                                after_eq ? "equal" : "DIFF",
                                g_replay_leaf_underflow);
                        if (!res_eq) fprintf(stderr, "  result: %s\n", why.c_str());
                        if (!after_eq) fprintf(stderr, "  after:%s\n", ad.c_str());
                        if (first_div_seq < 0) {
                            first_div_seq = c->seq;
                            first_class = cls;
                            first_detail = res_eq ? ad : why;
                        }
                    }
                }
            } catch (const std::exception &e) {
                fprintf(stderr, "ERROR: replay seq=%d failed: %s\n", c->seq, e.what());
                rc = 2;
            }
            g_rep = NULL;
            if (!chained) {
                delete r;
                r = NULL;
            }
        }
        if (chained && r != NULL) {
            delete r;
            r = NULL;
        }
        fclose(fout);
        if (rc == 0) {
            printf("replay: %u calls -> %s\n", (unsigned)calls.size(), outpath);
        }
        if (!leaf_replay) {
            printf("LEAF: calls=%d equal=%d path=%d/%d heur=%d/%d timeout=%d/%d "
                   "first_div_seq=%d %s\n",
                   leaf_calls, leaf_equal, totals.path_eq, totals.path_n,
                   totals.heur_eq, totals.heur_n, totals.tout_eq, totals.tout_n,
                   first_leaf_seq,
                   first_leaf_seq < 0 ? "" : first_leaf_detail.c_str());
            printf("RESULT: calls=%d equal=%d first_div_seq=%d %s\n",
                   res_calls, res_equal, first_res_seq,
                   first_res_seq < 0 ? "" : first_res_detail.c_str());
        }
        /* v6: tolerant gate compare counters (old captures have no leaf.gate). */
        printf("GATE: compared=%d skipped=%d\n",
               g_gate_compared, g_gate_skipped);
        /* v7: вызовы с захваченным world[0x120] / со статическим фолбэком. */
        printf("SCALE: compared=%d skipped=%d\n", g_scale_compared, g_scale_skipped);
        printf("CHAINSTATE: applied=%d skipped=%d port_status=%u\n",
               g_chain_applied, g_chain_skipped,
               (unsigned)g_port_tree.size());
        if (chained) {
            /* survived = число цепных вызовов, совпавших подряд до первого
             * расхождения (0, если первый же вызов разошёлся). */
            printf("CHAIN: calls=%d complete=%d chain_length=%d compared=%d "
                   "survived=%d entry_state_mismatch=%d first_entry_seq=%d "
                   "first_div_seq=%d first_class=%s "
                   "ai_after_compared=%d ai_after_skipped=%d ai_words_mismatch=%d\n",
                   (int)calls.size(), complete, chain_len, compared,
                   chain_matched, entry_mismatch,
                   first_entry_seq, first_div_seq,
                   first_div_seq < 0 ? "NONE" : first_class.c_str(),
                   ai_compared, ai_skipped, ai_mismatch);
            if (ai_skipped == 0) {
                printf("CHAIN: ai_words_after present in all %d calls — scalar "
                       "block compared, %d slot mismatches\n",
                       ai_compared, ai_mismatch);
            } else {
                printf("CHAIN: ai_words_after absent in %d of %d calls (capture "
                       "v1..v3) — scalar block NOT compared for those\n",
                       ai_skipped, compared);
            }
            if (first_entry_seq >= 0) {
                printf("CHAIN: native cap entry state differs from carried port "
                       "state at seq=%d (game resets candidate maps between "
                       "e0a0 calls):%s\n",
                       first_entry_seq, first_entry_detail.c_str());
            }
            if (first_div_seq < 0) {
                printf("CHAIN: EQUAL - result and after-state matched on every "
                       "chained call\n");
            } else {
                printf("CHAIN: first divergence seq=%d class=%s detail:%s\n",
                       first_div_seq, first_class.c_str(), first_detail.c_str());
                rc = 1;
            }
        }
    }
    return rc;
}
