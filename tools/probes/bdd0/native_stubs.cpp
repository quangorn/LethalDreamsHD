/* tools/probes/bdd0/native_stubs.cpp -- NOOP_OK native wrappers for the
 * offline harness.  Compiled INSTEAD OF tools/ldhook/battle/native.cpp.
 *
 * Since v12 the RECORD natives of the body (game_state_get, player_get,
 * player_flag_get/flags, phase_time, vcall38_ptr, world_notify_snapshot,
 * cursor_mode_update, ai_time_update, state_flag_log, player_pos_set,
 * pathfinder_init, unit_ai_tick, string_obj_cmp) are replayed from the
 * recorded `bdd0_native` stream in native_replay.cpp.  Only the NOOP_OK
 * targets remain here -- GUI/string/log/dtor plumbing that cannot affect the
 * body's control flow:
 *
 *   msg_ctor/msg_copy/msg_string_dtor, log_assert, string_ctor/string_format/
 *   string_c_str/string_dtor, output_debug_string_a, pathfinder_dtor.
 *
 * Every call increments the `not-modelled` counter printed in the run summary,
 * so these stubs stay visible instead of looking verified.  Other helpers
 * (band_free, pathfinder, tree, target_search, ...) are only used by layers
 * that the offline harness does not execute; they remain inert.
 */
#include <stdlib.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "../../ldhook/ldhook_internal.h"
#include "harness.h"

/* --------------------------- счётчики прогона ---------------------------- */
static int g_unmodelled = 0;
static int g_unit_ai_tick = 0;

int bdd0_unmodelled_calls(void) { return g_unmodelled; }
int bdd0_unmodelled_unit_ai_tick(void) { return g_unit_ai_tick; }

static void noop(const char *what)
{
    (void)what;
    g_unmodelled++;
}

/* ------------------------------ логгер ---------------------------------- */
volatile LONG g_log_level = 0;
static int trace_level(void)
{
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("BDD0_TRACE");
        v = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    return v;
}
void ld_info(const char *fmt, ...) { (void)fmt; }
void ld_debug(const char *fmt, ...) { (void)fmt; }
int ld_rate(volatile LONG *ctr, LONG first, LONG each)
{
    (void)ctr; (void)first; (void)each;
    return (trace_level() >= 2) ? 1 : 0;
}
LONGLONG ld_now_us(void) { return 0; }

/* --------------- s6-гейты, которые нужны тику (не порт site6) ------------ */
/* Порт юнит-AI (site6) офлайн выключен — ровно как прогон без LDHOOK_SITE6.
 * Если гейт когда-нибудь станет 1, harness обязан линковать move.cpp; сейчас
 * s6_unit_ai_tick — громкий счётчик, а не тихая подстановка. */
int s6_port_enabled(void) { return 0; }
int s6_unit_ai_tick(BattleAI *self, void *rec_arg)
{
    (void)self; (void)rec_arg;
    g_unit_ai_tick++;
    return 0;
}

/* ---------------------------- native-слой ------------------------------- */
namespace native {

unsigned char *base(void) { return NULL; }        /* офлайн образа игры нет */
void init(void) {}
void *fn(unsigned int va) { (void)va; return NULL; }

TreeNode *lower_bound(void *map, unsigned int key, void *hint)
{
    (void)map; (void)key; (void)hint; noop("lower_bound"); return NULL;
}
TreeNode *lower_bound_slots(void *map, int *out, int *key)
{
    (void)map; (void)out; (void)key; noop("lower_bound_slots"); return NULL;
}
TreeNode *tree_next(TreeNode *node) { (void)node; noop("tree_next"); return NULL; }
int tree_lookup(void *world, unsigned int key)
{
    (void)world; (void)key; noop("tree_lookup"); return 0;
}
void register_occupied_slot(void *ai, LDObject *obj)
{
    (void)ai; (void)obj; noop("register_occupied_slot");
}

void band_free(void *band) { (void)band; noop("band_free"); }
void native_45af10(void *band) { (void)band; noop("native_45af10"); }
void clear_maps(void *ai) { (void)ai; noop("clear_maps"); }
void search_params_update(void *ai) { (void)ai; noop("search_params_update"); }
void band_rebuild(void *ai, void *node, int bits)
{
    (void)ai; (void)node; (void)bits; noop("band_rebuild");
}
void *target_search(void *ai, int filter, void *cmp_pos, int arg3, int *out_code)
{
    (void)ai; (void)filter; (void)cmp_pos; (void)arg3;
    if (out_code != NULL) *out_code = 0;
    noop("target_search");
    return NULL;
}
void pathfinder_dtor(void *pf) { (void)pf; noop("pathfinder_dtor"); }

int set_rect_empty(void *rect)
{
    noop("set_rect_empty");
    if (rect != NULL) memset(rect, 0, 16);
    return 1;
}

int vcall0(void *receiver, unsigned int voffset)
{
    (void)receiver; (void)voffset; noop("vcall0"); return 0;
}
int vcall1(void *receiver, unsigned int voffset, int arg)
{
    (void)receiver; (void)voffset; (void)arg; noop("vcall1"); return 0;
}

long av_count(void) { return 0; }
int av_filter(void) { return EXCEPTION_EXECUTE_HANDLER; }

/* PtInRect — чистый предикат WinAPI, не игровой код: моделируется точно
 * (rect = [left, top, right, bottom], полуинтервал по каждой оси). */
int pt_in_rect(const int *rect, int x, int y)
{
    if (rect == NULL) return 0;
    return (rect[0] <= x && x < rect[2] && rect[1] <= y && y < rect[3]) ? 1 : 0;
}

void msg_ctor(void *this_) { (void)this_; noop("msg_ctor"); }
void msg_copy(void *dst, const void *src) { (void)dst; (void)src; noop("msg_copy"); }
void msg_string_dtor(void *obj) { (void)obj; noop("msg_string_dtor"); }

void *string_ctor(void *obj, const char *s)
{
    (void)s; noop("string_ctor");
    if (obj != NULL) memset(obj, 0, 12);
    return obj;
}
void *string_format(void *dst, const void *a2, const void *a3)
{
    (void)a2; (void)a3; noop("string_format");
    return dst;
}
const char *string_c_str(void *obj) { (void)obj; noop("string_c_str"); return ""; }
void string_dtor(void *obj) { (void)obj; noop("string_dtor"); }
void log_assert(void *unit, const char *msg, int code)
{
    (void)unit; (void)msg; (void)code; noop("log_assert");
}
void output_debug_string_a(const char *s) { (void)s; noop("output_debug_string_a"); }

/* Игровой литерал по VA: офлайн строки нет — пустая строка, не «похожая». */
const char *literal(unsigned int va) { (void)va; noop("literal"); return ""; }

void *operator_new(unsigned int size) { return calloc(1, size); }
void deallocate(void *ptr) { free(ptr); }

} /* namespace native */
