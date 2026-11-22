/* tools/probes/bdd0_harness.cpp — ядро офлайн-харнесса боевого тика 44bdd0.
 *
 * Прогон: <capture.jsonl> [--impl port|native] [--check NAME]... [--from-seq N]
 *         [--limit N] [--json out.jsonl] [--verbose]
 *         [--budget-ms N] [--budget-calls N] [--no-check NAME]
 *
 * Что делает ядро (и только это):
 *   1. загружает захват v12 (scenario.cpp): одна запись bdd0_in = один тик;
 *   2. материализует области состояния из base64-блоков + карты указателей
 *      (arena.cpp) — игровая память не читается;
 *   3. зовёт реализацию РОВНО ОДИН раз за записанный тик в защищённом режиме
 *      (VEH первого шанса + worker-поток + watchdog);
 *   4. снимает пост-состояние, решения (AL, *param_3, unit+0x170), журнал
 *      вызовов, wall-clock;
 *   5. гоняет включённые проверки (registry.cpp / checks_builtin.cpp) и пишет
 *      отчёт об отказе; тик с отказом не обрывает прогон.
 *
 * Чего ядро НЕ делает: не сравнивает решения с записанным исходом (это
 * свойство проверки), не знает доменов (out∈{-1,0,1}, u170∈{0..4} — в
 * contract_min), не материализует объекты «по умолчанию» (немартированный
 * указатель = unmapped handle, а не подстановка).
 *
 * rc: 0 — все включённые проверки ok; 1 — есть fail; 2 — usage/формат/IO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "bdd0/harness.h"

/* ------------------------------ base64 ---------------------------------- */
static const char b64_tab[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const unsigned char *in, int n)
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

static std::string json_esc(const char *s)
{
    std::string out;
    if (s == NULL) return out;
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char buf[8];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

/* --------------------------- перехват ловушек ---------------------------- */
/* SEH-рамка самого тела 44bdd0 (`__except (EXCEPTION_EXECUTE_HANDLER)`) глушит
 * любую аварию и возвращает 0, поэтому ловушку видит только VEH ПЕРВОГО шанса
 * (он вызывается до кадровых обработчиков). Обработчик ничего не исправляет —
 * EXCEPTION_CONTINUE_SEARCH — он только записывает код/адрес. */
static volatile LONG g_armed = 0;
static volatile LONG g_trap_n = 0;
static unsigned long g_trap_code = 0;
static unsigned long g_trap_pc = 0;
static unsigned long g_trap_fault = 0;
static int g_trap_is_write = 0;

static LONG CALLBACK bdd0_veh(PEXCEPTION_POINTERS p)
{
    if (!g_armed || p == NULL || p->ExceptionRecord == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_trap_n == 0) {
        EXCEPTION_RECORD *er = p->ExceptionRecord;
        g_trap_code = er->ExceptionCode;
        g_trap_pc = (unsigned long)(ULONG_PTR)er->ExceptionAddress;
        g_trap_fault = 0;
        g_trap_is_write = 0;
        if (er->NumberParameters >= 2 &&
            (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
             er->ExceptionCode == 0x80000001u /* EXCEPTION_GUARD_PAGE */)) {
            g_trap_is_write = (er->ExceptionInformation[0] == 1) ? 1 : 0;
            g_trap_fault = (unsigned long)er->ExceptionInformation[1];
        }
    }
    g_trap_n++;
    return EXCEPTION_CONTINUE_SEARCH;
}

/* --------------------------- worker + watchdog --------------------------- */
struct TickRun {
    TickFn fn;
    void *unit;
    void *frame;
    int al;
    volatile LONG done;
    int thr_trap;
};

static DWORD WINAPI tick_thread(LPVOID p)
{
    TickRun *r = (TickRun *)p;
    __try {
        r->al = r->fn(r->unit, r->frame);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r->thr_trap = (int)GetExceptionCode();
        r->al = 0;
    }
    InterlockedExchange((volatile LONG *)&r->done, 1);
    return 0;
}

/* ------------------------------- CLI ------------------------------------ */
struct Options {
    const char *capture;
    const char *impl;
    const char *json;
    int from_seq, limit;
    int verbose;
    std::vector<std::string> checks;      /* --check (пусто = все) */
    std::vector<std::string> no_checks;   /* --no-check */
    Options() : capture(NULL), impl("port"), json(NULL), from_seq(-1), limit(-1),
                verbose(0) {}
};

static int usage(void)
{
    fputs("usage: bdd0_harness.exe <capture.jsonl> [--impl port|native]\n"
          "       [--check NAME]... [--no-check NAME] [--from-seq N] [--limit N]\n"
          "       [--json out.jsonl] [--verbose] [--budget-ms N] [--budget-calls N]\n",
          stderr);
    return 2;
}

static int parse_args(int argc, char **argv, Options &o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--impl") == 0 && i + 1 < argc) {
            o.impl = argv[++i];
        } else if (strcmp(a, "--check") == 0 && i + 1 < argc) {
            o.checks.push_back(argv[++i]);
        } else if (strcmp(a, "--no-check") == 0 && i + 1 < argc) {
            o.no_checks.push_back(argv[++i]);
        } else if (strcmp(a, "--from-seq") == 0 && i + 1 < argc) {
            o.from_seq = atoi(argv[++i]);
        } else if (strcmp(a, "--limit") == 0 && i + 1 < argc) {
            o.limit = atoi(argv[++i]);
        } else if (strcmp(a, "--json") == 0 && i + 1 < argc) {
            o.json = argv[++i];
        } else if (strcmp(a, "--verbose") == 0) {
            o.verbose = 1;
        } else if (strcmp(a, "--budget-ms") == 0 && i + 1 < argc) {
            bdd0_budget_set(atoi(argv[++i]), -1);
        } else if (strcmp(a, "--budget-calls") == 0 && i + 1 < argc) {
            bdd0_budget_set(-1, atoi(argv[++i]));
        } else if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "unknown option: %s\n", a);
            return 0;
        } else if (o.capture == NULL) {
            o.capture = a;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", a);
            return 0;
        }
    }
    return (o.capture != NULL) ? 1 : 0;
}

/* ---------------------------- прогон одного тика -------------------------- */
struct Rollup {
    int pass, fail, skip;
    Rollup() : pass(0), fail(0), skip(0) {}
};

/* Тик с недостоверным входом/исходом: ни одна проверка не получает pass,
 * все помечаются SKIP с одной причиной (счётчики — в сводке). */
static void skip_all_checks(const std::vector<int> &enabled,
                            std::vector<Rollup> &roll,
                            std::map<std::string, int> &skip_why,
                            const char *reason)
{
    for (size_t k = 0; k < enabled.size(); k++) {
        const CheckDef *cd = check_at(enabled[k]);
        roll[enabled[k]].skip++;
        char key[256];
        _snprintf_s(key, sizeof(key), _TRUNCATE, "%s: %s", cd->name, reason);
        skip_why[std::string(key)]++;
    }
}


static void write_report(FILE *f, const char *scenario, const TickRecord &rec,
                         const char *impl, const char *check, const char *why,
                         int al, int u170, int out, long long wall_ms,
                         long long leaf_calls, const char *capture,
                         const char *replay)
{
    fprintf(f, "{\"v\":1,\"kind\":\"harness_fail\",\"scenario\":\"%s\","
               "\"seq\":%d,\"tick\":%d,\"unit\":%d,\"impl\":\"%s\","
               "\"check\":\"%s\",\"why\":\"%s\",\"al\":%d,\"u170\":%d,"
               "\"out\":%d,\"wall_ms\":%lld,\"leaf_calls\":%lld,\"input\":{",
            json_esc(scenario).c_str(), rec.seq, rec.tick, rec.unit_index,
            json_esc(impl).c_str(), json_esc(check).c_str(), json_esc(why).c_str(),
            al, u170, out, wall_ms, leaf_calls);
    std::string fb = rec.has_frame ? b64_encode(rec.frame, BDD0_FRAME_LEN) : "";
    std::string ub = rec.has_unit ? b64_encode(rec.unit, BDD0_UNIT_LEN) : "";
    std::string ab = rec.has_ai ? b64_encode(rec.ai, BDD0_AI_LEN) : "";
    if (rec.has_frame) fprintf(f, "\"frame_b64\":\"%s\",", fb.c_str());
    else fputs("\"frame_b64\":null,", f);
    if (rec.has_unit) fprintf(f, "\"unit_b64\":\"%s\",", ub.c_str());
    else fputs("\"unit_b64\":null,", f);
    if (rec.has_ai) fprintf(f, "\"ai_b64\":\"%s\",", ab.c_str());
    else fputs("\"ai_b64\":null,", f);
    fprintf(f, "\"p1\":%d,\"p2\":%d,\"p3\":%d},"
               "\"replay\":\"%s --impl %s --check %s --from-seq %d --limit 1\"}\n",
            rec.p1, rec.p2, rec.p3, json_esc(replay).c_str(),
            json_esc(impl).c_str(), json_esc(check).c_str(), rec.seq);
    (void)capture;
}

int main(int argc, char **argv)
{
    Options opt;
    if (!parse_args(argc, argv, opt)) return usage();

    register_builtin_checks();
    register_builtin_impls();

    const ImplDef *impl = find_impl(opt.impl);
    if (impl == NULL) {
        fprintf(stderr, "unknown impl: %s (have:", opt.impl);
        for (int i = 0; i < impl_count(); i++) {
            fprintf(stderr, " %s", impl_at(i)->name);
        }
        fputs(")\n", stderr);
        return 2;
    }
    /* фильтр проверок: явные --check включают только себя */
    std::vector<int> enabled;
    for (int i = 0; i < check_count(); i++) {
        const CheckDef *cd = check_at(i);
        int on = opt.checks.empty() ? 1 : 0;
        for (size_t k = 0; k < opt.checks.size(); k++) {
            if (opt.checks[k] == cd->name) on = 1;
        }
        for (size_t k = 0; k < opt.no_checks.size(); k++) {
            if (opt.no_checks[k] == cd->name) on = 0;
        }
        if (on) enabled.push_back(i);
    }
    if (enabled.empty()) {
        fprintf(stderr, "no checks selected\n");
        return 2;
    }

    Scenario *sc = NULL;
    try {
        sc = load_capture(opt.capture);
    } catch (const std::exception &e) {
        fprintf(stderr, "capture error: %s\n", e.what());
        return 2;
    }
    if (sc->snap_records == 0) {
        fprintf(stderr, "capture %s has no v12 bdd0_in records "
                        "(v11 and older carry no snapshot: nothing to replay)\n",
                opt.capture);
        scenario_free(sc);
        return 2;
    }
    if (sc->truncated) {
        fprintf(stderr, "note: capture tail truncated (%d records read, "
                        "last chunk dropped)\n", sc->records);
    }

    FILE *rf = stdout;
    if (opt.json != NULL) {
        rf = fopen(opt.json, "wb");
        if (rf == NULL) {
            fprintf(stderr, "cannot write report: %s\n", opt.json);
            scenario_free(sc);
            return 2;
        }
    }

    AddVectoredExceptionHandler(1, bdd0_veh);

    std::vector<Rollup> roll((size_t)check_count());
    /* причины SKIP: печатаются в сводке, чтобы «пропущено» не было молчаливым */
    std::map<std::string, int> skip_why;
    int ran = 0, fails = 0, traps = 0, hangs = 0, gs_missing = 0, leaf_nodes = 0;
    long long sum_wall = 0;
    int skipped_ticks = 0;

    for (size_t ti = 0; ti < sc->ticks.size(); ti++) {
        TickRecord &rec = sc->ticks[ti];
        if (opt.from_seq > 0 && rec.seq < opt.from_seq) { skipped_ticks++; continue; }
        if (opt.limit >= 0 && ran >= opt.limit) break;

        /* (1) снимок снят при сбое: ни одно поле не считается достоверным,
         *     реализация не исполняется, ни одна проверка не даёт pass. */
        if (rec.trap) {
            ran++;
            skip_all_checks(enabled, roll, skip_why,
                            "input trap=1 (capture incomplete)");
            if (opt.verbose) {
                fprintf(rf, "{\"kind\":\"tick_skip\",\"scenario\":\"%s\","
                            "\"seq\":%d,\"impl\":\"%s\","
                            "\"why\":\"input trap=1\"}\n",
                        json_esc(sc->name.c_str()).c_str(), rec.seq, impl->name);
            }
            continue;
        }
        /* (2) тело не исполняется, а записанный исход помечен trap=1: это не
         *     данные для калибровки — весь тик SKIP. */
        if (!impl->runs_body && rec.res_trap) {
            ran++;
            skip_all_checks(enabled, roll, skip_why, "outcome trap=1");
            if (opt.verbose) {
                fprintf(rf, "{\"kind\":\"tick_skip\",\"scenario\":\"%s\","
                            "\"seq\":%d,\"impl\":\"%s\","
                            "\"why\":\"outcome trap=1\"}\n",
                        json_esc(sc->name.c_str()).c_str(), rec.seq, impl->name);
            }
            continue;
        }

        Arena *arena = arena_build(&rec);
        if (arena == NULL) {
            fprintf(stderr, "tick %d: arena build failed (VirtualAlloc)\n", rec.tick);
            continue;
        }
        arena_set_current(arena);

        LeafRec *lv = (rec.leaf_n > 0) ? &sc->leaves[rec.leaf_first] : NULL;
        bdd0_leaf_begin(rec.seq, lv, rec.leaf_n);

        /* --- тик --- */
        int al = 0, hang = 0;
        long long wall_ms = 0;
        g_armed = 0;
        g_trap_n = 0;
        g_trap_code = g_trap_pc = g_trap_fault = 0;
        g_trap_is_write = 0;

        int ran_body = impl->runs_body && impl->tick != NULL;
        if (ran_body) {
            TickRun tr;
            memset(&tr, 0, sizeof(tr));
            tr.fn = impl->tick;
            tr.unit = arena_region_w(arena, "unit");
            tr.frame = arena_region_w(arena, "frame");
            if (impl->prepare != NULL) impl->prepare(arena);

            g_armed = 1;
            LARGE_INTEGER f0, t0, t1;
            QueryPerformanceFrequency(&f0);
            QueryPerformanceCounter(&t0);
            DWORD th = 0;
            HANDLE h = CreateThread(NULL, 0, tick_thread, &tr, 0, &th);
            if (h == NULL) {
                fprintf(stderr, "tick %d: CreateThread failed\n", rec.tick);
                arena_release(arena);
                continue;
            }
            DWORD w = WaitForSingleObject(h, (DWORD)bdd0_budget_ms());
            if (w == WAIT_TIMEOUT) {
                hang = 1;
                TerminateThread(h, 0);
            }
            QueryPerformanceCounter(&t1);
            g_armed = 0;
            CloseHandle(h);
            wall_ms = (long long)((t1.QuadPart - t0.QuadPart) * 1000 /
                                  (f0.QuadPart ? f0.QuadPart : 1));
            al = tr.al & 0xff;
            if (impl->release != NULL) impl->release(arena);
        } else {
            /* replay записанного исхода: тело не исполняется */
            al = rec.has_res ? (rec.res_al & 0xff) : 0;
            if (rec.has_res) {
                if (arena_out_known(arena)) arena_set_out_slot(arena, rec.res_out);
                /* записанный исход кладётся в поля ПО ИМЕНАМ таблицы: ядро
                 * оффсетов не знает (m0/m1/m2 — pos-тройка R14) */
                field_write(arena, "u170", (unsigned)rec.res_u170);
                field_write(arena, "m0", rec.res_m[0]);
                field_write(arena, "m1", rec.res_m[1]);
                field_write(arena, "m2", rec.res_m[2]);
            }
            g_trap_n = 0;
        }

        /* --- пост-состояние и механические факты --- */
        char why_addr[192];
        int out_known = arena_out_known(arena);
        int out_val = -1;
        if (out_known) {
            memcpy(&out_val, arena_out_slot(arena), 4);
            if ((unsigned)out_val == 0x7fffffffu) out_val = -1;  /* не писано */
        }
        int u170 = 0;
        {
            /* значение читается ПО ИМЕНИ из таблицы полей, не по оффсету */
            TickContext probe;
            memset(&probe, 0, sizeof(probe));
            probe.regions = arena_views(arena);
            probe.region_n = arena_region_n(arena);
            u170 = (int)probe.field("u170");
        }

        char write_why[192];
        int write_outside = 0;
        write_why[0] = '\0';
        if (ran_body && !hang) {
            write_outside = arena_scan_writes(arena, write_why, sizeof(write_why));
        }
        if (ran_body && g_trap_n > 0 && g_trap_is_write) {
            write_outside = 1;
            if (write_why[0] == '\0') {
                _snprintf_s(write_why, sizeof(write_why), _TRUNCATE,
                            "write at %s outside declared region",
                            arena_why_addr(arena, g_trap_fault, why_addr,
                                           sizeof(why_addr)));
            }
        }

        /* --- контекст проверки --- */
        TickContext tc;
        memset(&tc, 0, sizeof(tc));
        tc.seq = rec.seq;
        tc.tick = rec.tick;
        tc.unit_index = rec.unit_index;
        tc.scenario = sc->name.c_str();
        tc.impl = impl->name;
        tc.al = al;
        tc.out_slot = out_val;
        tc.u170 = u170;
        tc.out_known = out_known;
        tc.wall_ms = wall_ms;
        tc.leaf_calls = bdd0_leaf_calls();
        tc.ran_body = ran_body;
        tc.hang = hang;
        tc.trapped = (g_trap_n > 0) ? 1 : 0;
        tc.trap_addr = g_trap_fault;
        static char trap_buf[192];
        if (tc.trapped) {
            _snprintf_s(trap_buf, sizeof(trap_buf), _TRUNCATE,
                        "trap 0x%08x (%s) at %s; pc=0x%08x", g_trap_code,
                        g_trap_is_write ? "write" : "read/exec",
                        arena_why_addr(arena, g_trap_fault, why_addr,
                                       sizeof(why_addr)),
                        g_trap_pc);
            tc.trap_why = trap_buf;
        } else {
            tc.trap_why = "";
        }
        tc.regions = arena_views(arena);
        tc.region_n = arena_region_n(arena);
        tc.regions_before = arena_pre_views(arena);
        tc.region_before_n = arena_region_n(arena);
        tc.log = bdd0_calllog(&tc.log_n);
        tc.rec = &rec;
        tc.write_outside = write_outside;
        tc.write_outside_why = write_why;
        tc.handle_miss = arena_handle_misses(arena);
        tc.handle_miss_why = arena_handle_miss_why(arena);
        tc.leaf_violation = bdd0_leaf_violations();
        tc.leaf_violation_why = bdd0_leaf_violation_why();
        if (!tc.leaf_violation && sc->leaf_missing_n > 0) {
            /* маркер захвата: сайт не захукался → поток листьев мог быть
             * неполон, «листья не понадобились» доказательством не является */
            tc.leaf_violation = 1;
            tc.leaf_violation_why = sc->leaf_missing_why.c_str();
        }
        static char ctx_why[192];
        tc.why_buf = ctx_why;
        tc.why_cap = sizeof(ctx_why);

        tc.avail = NEED_TIMING;
        if (rec.has_frame && rec.has_unit && rec.has_ai) {
            tc.avail |= NEED_POST_STATE;
        }
        if (rec.has_frame && rec.has_unit) tc.avail |= NEED_HANDLES;
        if (rec.has_gs) tc.avail |= NEED_GAMESTATE;
        else gs_missing++;
        if (ran_body) tc.avail |= NEED_BODY;
        if (rec.leaf_n > 0 || !ran_body) tc.avail |= NEED_CALLLOG;
        if (ran_body || rec.has_res) tc.avail |= NEED_DECISIONS;

        if (tc.trapped) traps++;
        if (hang) hangs++;
        sum_wall += wall_ms;
        ran++;

        if (opt.verbose) {
            fprintf(rf, "{\"kind\":\"tick\",\"scenario\":\"%s\",\"seq\":%d,"
                        "\"impl\":\"%s\",\"al\":%d,\"out\":%d,\"u170\":%d,"
                        "\"wall_ms\":%lld,\"leaf_calls\":%lld}\n",
                    json_esc(sc->name.c_str()).c_str(), rec.seq, impl->name,
                    al, out_val, u170, wall_ms, tc.leaf_calls);
        }

        for (size_t k = 0; k < enabled.size(); k++) {
            const CheckDef *cd = check_at(enabled[k]);
            CheckResult r;
            unsigned missing = cd->needs & ~tc.avail;
            if (missing != 0) {
                r.ok = 1;
                r.skip = 1;
                char buf[128];
                unsigned bit = 0;
                for (unsigned b = 1; b <= NEED_BODY; b <<= 1) {
                    if (missing & b) { bit = b; break; }
                }
                _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                            "data_unavailable:%s", need_name(bit));
                r.why = buf;
            } else {
                r = cd->fn(tc);
            }
            Rollup &ru = roll[enabled[k]];
            if (r.skip) {
                ru.skip++;
                char key[256];
                _snprintf_s(key, sizeof(key), _TRUNCATE, "%s: %s", cd->name,
                            r.why != NULL && r.why[0] != '\0' ? r.why : "?");
                skip_why[std::string(key)]++;
            } else if (r.ok) {
                ru.pass++;
            } else {
                ru.fail++;
                fails++;
                write_report(rf, sc->name.c_str(), rec, impl->name, cd->name,
                             r.why != NULL ? r.why : "", al, u170, out_val,
                             wall_ms, tc.leaf_calls, opt.capture, opt.capture);
            }
        }

        leaf_nodes += arena_leaf_nodes(arena);
        arena_set_current(NULL);
        arena_release(arena);
    }
    (void)skipped_ticks;

    /* --- сводка --- */
    int sum_pass = 0, sum_skip = 0;
    for (size_t i = 0; i < roll.size(); i++) {
        sum_pass += roll[i].pass;
        sum_skip += roll[i].skip;
    }
    printf("bdd0_harness summary: capture=%s impl=%s ticks=%d pass=%d skip=%d "
           "fail=%d traps=%d hangs=%d avg_wall_ms=%lld\n",
           sc->name.c_str(), impl->name, ran, sum_pass, sum_skip, fails, traps,
           hangs, ran ? sum_wall / ran : 0);

    for (size_t k = 0; k < enabled.size(); k++) {
        const CheckDef *cd = check_at(enabled[k]);
        const Rollup &ru = roll[enabled[k]];
        printf("  %-13s pass=%d skip=%d fail=%d\n", cd->name, ru.pass,
               ru.skip, ru.fail);
    }
    if (!skip_why.empty()) {
        printf("  skips:");
        for (std::map<std::string, int>::const_iterator it = skip_why.begin();
             it != skip_why.end(); ++it) {
            printf(" [%dx %s]", it->second, it->first.c_str());
        }
        printf("\n");
    }
    printf("  not-modelled: no-op native calls=%d (player/string/log/vtable/"
           "phase), unit_ai_tick=%d\n",
           bdd0_unmodelled_calls(), bdd0_unmodelled_unit_ai_tick());
    /* game_state: видно, был ли объект FUN_00442570 в захвате. Нулевой shadow
     * помечается как stub, чтобы contract_min не выглядел проверенным. */
    if (gs_missing == 0) {
        printf("  game_state=gs_present (gs_b64, %d ticks)\n", ran);
    } else if (gs_missing == ran) {
        printf("  game_state=gs_absent(zeros) STUB: not modelled, "
               "contract_min degraded in %d ticks\n", gs_missing);
    } else {
        printf("  game_state=gs_present in %d ticks, gs_absent(zeros) STUB in "
               "%d ticks (contract_min degraded there)\n",
               ran - gs_missing, gs_missing);
    }
    if (sc->input_trap > 0) {
        printf("  input_trap=%d (bdd0_in trap=1: snapshot not trusted, ticks "
               "skipped)\n", sc->input_trap);
    }
    if (sc->outcome_trap > 0) {
        printf("  outcome_trap=%d (bdd0_res trap=1: recorded outcome not "
               "trusted)\n", sc->outcome_trap);
    }
    if (sc->leaf_missing_n > 0) {
        printf("  leaf_missing=%d (%s)\n", sc->leaf_missing_n,
               sc->leaf_missing_why.c_str());
    }
    printf("  leaf_nodes=%d (opaque sentinels for unbound leaf pointers; "
           "0 = every leaf pointer was materialised)\n", leaf_nodes);

    if (rf != stdout) fclose(rf);
    scenario_free(sc);
    return fails > 0 ? 1 : 0;
}
