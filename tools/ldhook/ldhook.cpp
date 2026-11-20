/* ldhook.pyd — Фазы 1–3, 5–6 (PLAN_proxy_dll.md, §4) + Фаза 0 каркаса
 * боевого порта (PLAN_BATTLE_AI_CPP.md, п. 0.1).
 *
 * Этот TU: инициализация (`initldhook`), Python-API модуля, логгер, троттлинг
 * и замер времени, PE/.text-скан, установка хуков (install_site1/2/3/5/6) и
 * общие утилиты. Сайты HD-масштабирования (site1/2/3, naked-хуки, site3_fp) —
 * sites_hd.cpp; боевой слой — battle/* (structs.h, native.cpp, grid.cpp,
 * path.cpp, target.cpp, move.cpp, tick.cpp, trace.h). Общие объявления —
 * ldhook_internal.h.
 *
 * ПОРТ НА C++ (Вариант B: C++ + статический CRT /MT). Логика и ABI 1:1 с
 * ldhook.c (оставлен как reference, помечен DEPRECATED). Единственное
 * изменение — initldhook объявлен extern "C", чтобы Python 2.2.3
 * imp.load_dynamic нашёл символ по имени (без extern "C" C++ манглит имя).
 * Сборка: /LD /MT /EHsc /O2 /W3 /fp:precise (/fp:precise сохранён — битовая
 * identity 4:3 в site1/site3). См. tools/ldhook/README.md.
 *
 * Расширение Python 2.2.3. initldhook() (вызывается движком через
 * imp.load_dynamic из патчимого ui/ui.pyc):
 *   1. поднимает логгер (hdlog.txt в каталоге EXE + OutputDebugString);
 *   2. пишет файл-маркер (доказательство загрузки для зондов);
 *   3. install_site1/2/3/5/6() — находит сайты сканом сигнатур в .text EXE и
 *      ставит E9 rel32 на соответствующие трамплины (в памяти, не на диске).
 *
 *   site1 (0x41ce31, хвост FUN_0041cc90): масштаб листов/advance 4x-шрифтов,
 *          guard k==1 -> identity (порт cave1);
 *   site2 (0x40b494, сеттер шрифта): после сохранённых байт зовёт
 *          FUN_0041cc90(шрифт) — нормировка шрифта сразу при SetFont (cave2);
 *   site3 (0x47dd80, RenderWorld): matrix[0] = matrix[5]*h/w перед сеттером
 *          FUN_0046f970; guard 4*h == 3*w -> запись пропускается, 4:3
 *          остаётся побитово прежним (cave3);
 *   site5 (0x45e0a0 + 0x45e163): диагностика поиска цели FUN_0045e0a0 —
 *          логирование аргументов, кандидатов, метрик, порогов; site6 —
 *          полный detour на faithful-порт (LDHOOK_SITE6=1).
 *
 * Логика 1:1 с tools/exe/caves/fixall.s; guard'ы (k==1 / 4h==3w) сохранены.
 * EXE НЕ модифицируется на диске: правка ставится в памяти через VirtualProtect
 * (правило «игра только для чтения»; md5-цепочка не трогается).
 *
 * Сборка: python3 tools/ldhook/build.py (MSVC x86, python22.lib из vendor).
 * Загрузка: imp.load_dynamic('ldhook', '<путь>/ldhook.pyd') из ui/ui.pyc.
 */
#include <Python.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "battle/battle.h"
#include "battle/trace.h"
#include "battle/native.h"
#include "ldhook_internal.h"

/* ------------------------------------------------------------------ */
/* Диагностика (Фаза 3): уровни, hdlog.txt, OutputDebugString           *//*                                                                     */
/* Единый лог движка — hdlog.txt в каталоге EXE: тот же файл, куда      */
/* пишут игровые Python-патчи (hd_scale.py, tex_hook_*.py), поэтому    */
/* `probe_exitcode.ps1` уже копирует его хвост `<Tag>_hdlog.txt`.       */
/*                                                                     */
/* Уровни (флаг — env LDHOOK_LOG, дефолт 1):                           */
/*   0 = off    — файл не создаётся, OutputDebugString молчит;           */
/*   1 = info   — диагностика: загрузка, установка хуков, guard'ы,      */
/*                первый вызов каждого хука (дефолт; 4:3 пишет только   */
/*                эти строки);                                          */
/*   2 = debug  — трассировка масштабирующего пути (с троттлингом).     */
/* `ldhook.set_level(n)` из игрового скрипта перекрывает env на лету.    */
/* Ротация: файл > 8 MiB → hdlog.txt.1 (защита от роста в режиме debug). */
/* Маркер `ldhook_marker.txt` пишется всегда: это доказательство        */
/* загрузки .pyd для зондов, независимо от флага лога.                  */
/* ------------------------------------------------------------------ */

volatile LONG g_log_level = LD_INFO;
static HANDLE        g_log_file = INVALID_HANDLE_VALUE;
static unsigned long g_log_bytes = 0;
static CRITICAL_SECTION g_log_cs;
static int           g_log_cs_ready = 0;

static const char *ld_level_tag(int level)
{
    return (level >= LD_DEBUG) ? "D" : "I";
}

static void ld_close_log(void)
{
    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    g_log_bytes = 0;
}

/* Каталог EXE (там же, где hdlog.txt пишут игровые скрипты). */
static void ld_exe_dir(char *path, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, path, (DWORD)cap);
    char *p;
    if (n == 0 || (size_t)n >= cap) {
        path[0] = '\0';
        return;
    }
    p = strrchr(path, '\\');
    if (p != NULL) {
        p[1] = '\0';
    } else {
        path[0] = '\0';
    }
}

static void ld_open_log(void)
{
    char path[MAX_PATH];
    DWORD size = 0;

    ld_exe_dir(path, sizeof(path));
    if (sizeof(path) - strlen(path) <= strlen(LDHOOK_LOG_NAME)) {
        return;
    }
    strncat(path, LDHOOK_LOG_NAME, sizeof(path) - strlen(path) - 1);

    g_log_file = CreateFileA(path, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log_file == INVALID_HANDLE_VALUE) {
        return;
    }
    size = GetFileSize(g_log_file, NULL);
    g_log_bytes = (size == INVALID_FILE_SIZE) ? 0 : (unsigned long)size;
    if (g_log_bytes > LDHOOK_LOG_MAX) {
        char old[MAX_PATH];
        ld_close_log();
        _snprintf(old, sizeof(old), "%s.1", path);
        old[sizeof(old) - 1] = '\0';
        MoveFileExA(path, old, MOVEFILE_REPLACE_EXISTING);
        g_log_file = CreateFileA(path, FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log_file == INVALID_HANDLE_VALUE) {
            return;
        }
        g_log_bytes = 0;
    }
}

static void ld_vlog(int level, const char *fmt, va_list ap)
{
    char msg[512];
    char line[640];
    SYSTEMTIME st;
    DWORD w = 0;

    if (level < LD_INFO || level > (int)g_log_level) {
        return;                     /* уровень off: молчим полностью */
    }
    _vsnprintf(msg, sizeof(msg), fmt, ap);
    msg[sizeof(msg) - 1] = '\0';

    OutputDebugStringA("ldhook: ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\r\n");

    if (g_log_cs_ready) {
        EnterCriticalSection(&g_log_cs);
    }
    if (g_log_file == INVALID_HANDLE_VALUE) {
        ld_open_log();
    }
    if (g_log_file != INVALID_HANDLE_VALUE) {
        GetLocalTime(&st);
        _snprintf(line, sizeof(line),
                  "ldhook: %02u:%02u:%02u.%03u t%04x %s %s\r\n",
                  (unsigned)st.wHour, (unsigned)st.wMinute,
                  (unsigned)st.wSecond, (unsigned)st.wMilliseconds,
                  (unsigned)GetCurrentThreadId(), ld_level_tag(level), msg);
        line[sizeof(line) - 1] = '\0';
        SetFilePointer(g_log_file, 0, NULL, FILE_END);
        if (WriteFile(g_log_file, line, (DWORD)strlen(line), &w, NULL)) {
            g_log_bytes += (unsigned long)w;
            if (g_log_bytes > LDHOOK_LOG_MAX) {
                ld_close_log();     /* переоткроется с ротацией в .1 */
            }
        }
    }
    if (g_log_cs_ready) {
        LeaveCriticalSection(&g_log_cs);
    }
}

void ld_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ld_vlog(LD_INFO, fmt, ap);
    va_end(ap);
}

void ld_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ld_vlog(LD_DEBUG, fmt, ap);
    va_end(ap);
}

static int ld_parse_level(const char *s)
{
    if (s == NULL || *s == '\0') {
        return LD_INFO;
    }
    if (s[0] == '0' || _stricmp(s, "off") == 0 || _stricmp(s, "no") == 0 ||
        _stricmp(s, "false") == 0) {
        return LD_OFF;
    }
    if (s[0] == '2' || _stricmp(s, "debug") == 0 ||
        _stricmp(s, "verbose") == 0 || _stricmp(s, "all") == 0) {
        return LD_DEBUG;
    }
    return LD_INFO;
}

static void ld_log_init(void)
{
    if (!g_log_cs_ready) {
        InitializeCriticalSection(&g_log_cs);
        g_log_cs_ready = 1;
    }
    g_log_level = ld_parse_level(getenv(LDHOOK_LOG_ENV));
}

/* Троттлинг трассировки: первые first вызовов, затем каждый each-й. */
int ld_rate(volatile LONG *ctr, LONG first, LONG each)
{
    LONG n = InterlockedIncrement(ctr);
    return n <= first || (each > 0 && (n % each) == 0);
}

/* Замер времени (Фаза 4, A/B оптимизации site6): QueryPerformanceCounter,
 * microseconds. Частота кэшируется при первом вызове. */
static LONGLONG g_qpc_freq = 0;
LONGLONG ld_now_us(void)
{
    LARGE_INTEGER li;
    if (g_qpc_freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_qpc_freq = f.QuadPart;
    }
    QueryPerformanceCounter(&li);
    return li.QuadPart * 1000000 / g_qpc_freq;
}

static void ldhook_marker(void)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy(path, "ldhook_marker.txt");
    } else {
        char *p = strrchr(path, '\\');
        if (p != NULL) {
            p[1] = '\0';
        } else {
            path[0] = '\0';
        }
        strncat(path, "ldhook_marker.txt", sizeof(path) - strlen(path) - 1);
    }
    {
        HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            const char *msg = "ldhook.pyd loaded (phase 6: hooks + logger + target-search diag)\r\n";
            WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
            CloseHandle(h);
            ld_info("marker written: ldhook_marker.txt");
        } else {
            ld_info("marker FAILED");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Установка хука: скан сигнатуры в .text + E9 rel32 через VirtualProtect */
/* ------------------------------------------------------------------ */

static unsigned char *find_sig(unsigned char *start, size_t len,
                               const unsigned char *sig, size_t siglen)
{
    size_t i;
    if (len < siglen) {
        return NULL;
    }
    for (i = 0; i + siglen <= len; i++) {
        if (start[i] == sig[0] && memcmp(start + i, sig, siglen) == 0) {
            return start + i;
        }
    }
    return NULL;
}

/* Безопасная запись E9 rel32 по адресу target; total = длина патча
 * (5 байт для site1/site3, 6 для site2: E9+nop, как в exepatch12;
 * до 10 для trace-сайтов H1/H7: E9+nop-дополнение украденных байт). */
static int write_jmp(void *target, void *dest, int total)
{
    DWORD old;
    unsigned char patch[16];
    ptrdiff_t rel;
    if (total < 5 || total > (int)sizeof(patch)) {
        return 0;
    }
    rel = (unsigned char *)dest - ((unsigned char *)target + 5);
    patch[0] = 0xE9;
    *(int *)(patch + 1) = (int)rel;
    if (total > 5) {
        memset(patch + 5, 0x90, (size_t)(total - 5));
    }
    if (!VirtualProtect(target, (SIZE_T)total, PAGE_EXECUTE_READWRITE, &old)) {
        return 0;
    }
    memcpy(target, patch, (size_t)total);
    VirtualProtect(target, (SIZE_T)total, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, (SIZE_T)total);
    return 1;
}

/* .text текущего процесса: RVA/размер берём из PE-заголовков, а не из
 * констант (Фаза 3: делаем модуль безопасным вне игры — под python.exe в
 * локальном самотесте скан не выйдет за границу секции; и переживает
 * будущие правки EXE, сдвигающие .text). */
static unsigned char *g_text = NULL;
static size_t g_text_len = 0;

static int ld_find_text(unsigned char *base, unsigned char **addr, size_t *len)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    unsigned i;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            size_t size = sec[i].Misc.VirtualSize;
            if (size == 0) {
                size = sec[i].SizeOfRawData;
            }
            if (size == 0) {
                return 0;
            }
            *addr = base + sec[i].VirtualAddress;
            *len = size;
            return 1;
        }
    }
    return 0;
}

static unsigned char *find_in_text(const unsigned char *sig, size_t siglen)
{
    if (g_text == NULL) {
        return NULL;
    }
    return find_sig(g_text, g_text_len, sig, siglen);
}

static void install_site1(void)
{
    unsigned char *site;

    site = find_in_text((const unsigned char *)SITE1_SIG, SITE1_SIG_LEN);
    if (site == NULL) {
        ld_info("install_site1: site1 signature NOT found (baseline EXE?)");
        return;
    }
    if (!write_jmp(site, (void *)site1_hook, SITE1_SIG_LEN)) {
        ld_info("install_site1: write_jmp FAILED");
        return;
    }
    ld_info("install_site1: site1 found at 0x%08x, E9 rel32", (unsigned)site);
}

static void install_site2(void)
{
    unsigned char *site;

    site = find_in_text((const unsigned char *)SITE2_SIG, SITE2_SIG_LEN);
    if (site == NULL) {
        ld_info("install_site2: site2 signature NOT found (baseline EXE?)");
        return;
    }
    g_site2_norm = native::fn(0x41cc90);   /* FUN_0041cc90 (нормировка шрифта) */
    if (!write_jmp(site, (void *)site2_hook, SITE2_PATCH)) {
        ld_info("install_site2: write_jmp FAILED");
        return;
    }
    ld_info("install_site2: site2 found at 0x%08x, E9 rel32+nop -> hook",
            (unsigned)site);
}

static void install_site3(void)
{
    unsigned char *site;
    int rel;

    site = find_in_text((const unsigned char *)SITE3_SIG, SITE3_SIG_LEN);
    if (site == NULL) {
        ld_info("install_site3: site3 signature NOT found (baseline EXE?)");
        return;
    }
    /* Оригинал: call rel32 -> FUN_0046f970; продолжение RenderWorld = site+5. */
    rel = *(int *)(site + 1);
    g_site3_setter = (void *)(site + 5 + rel);
    g_site3_cont = (void *)(site + 5);
    if (!write_jmp(site, (void *)site3_hook, SITE3_SIG_LEN)) {
        ld_info("install_site3: write_jmp FAILED");
        return;
    }
    ld_info("install_site3: site3 found at 0x%08x, call target 0x%08x, cont 0x%08x",
            (unsigned)site, (unsigned)g_site3_setter, (unsigned)g_site3_cont);
}

/* E9 rel32 от site до трамплина должен влезать в знаковый диапазон
 * (общая проверка для всех сайтов: site5/site6 и trace-наблюдатели). */
int rel32_fits(void *site, void *dest)
{
    ptrdiff_t d = (unsigned char *)dest - (unsigned char *)site;
    return d > -0x7fff0000 && d < 0x7fff0000;
}

/* ------------------------------------------------------------------ */
/* site5 — диагностика поиска цели (FUN_0045e0a0): логирование.       */
/* Трамплины и счётчики — battle/target.cpp; установка — здесь.        */
/* Сайты: пролог 0x45e0a0 (7 Б) + хвост цикла 0x45e163 (6 Б) —       */
/* те же сигнатуры, что у site4.                                       */
/* ------------------------------------------------------------------ */

static void install_site5(void)
{
    unsigned char *entry_site;
    unsigned char *tail_site;
    int rel;

    entry_site = find_in_text((const unsigned char *)S5_ENTRY_SIG, S5_ENTRY_LEN);
    tail_site = find_in_text((const unsigned char *)S5_TAIL_SIG, S5_TAIL_LEN);
    if (entry_site == NULL || tail_site == NULL) {
        ld_info("install_site5: signature NOT found (entry=%p tail=%p)",
                (void *)entry_site, (void *)tail_site);
        return;
    }

    rel = (signed char)tail_site[5];
    g_site5_cont = (void *)(entry_site + S5_ENTRY_LEN);
    g_site5_nat = (void *)(tail_site + S5_TAIL_LEN);
    g_site5_top = (void *)(tail_site + S5_TAIL_LEN + rel);

    ld_info("install_site5: entry=0x%08x tail=0x%08x top=0x%08x exit=0x%08x",
            (unsigned)entry_site, (unsigned)tail_site,
            (unsigned)g_site5_top, (unsigned)g_site5_nat);

    if (!rel32_fits(entry_site, (void *)s5_entry_hook) ||
        !rel32_fits(tail_site, (void *)s5_tail_hook)) {
        ld_info("install_site5: E9 rel32 out of range");
        return;
    }
    if (!write_jmp(entry_site, (void *)s5_entry_hook, S5_ENTRY_LEN)) {
        ld_info("install_site5: entry write_jmp FAILED");
        return;
    }
    if (!write_jmp(tail_site, (void *)s5_tail_hook, S5_TAIL_LEN)) {
        ld_info("install_site5: tail write_jmp FAILED");
        return;
    }
    /* Эпилоги: все выходы функции (sentinel/1/4/3) идут через копии
     * add esp,0x34; ret 10h. Сканируем всю функцию, хукаем каждый сайт. */
    {
        unsigned char *p = entry_site;
        unsigned char *end = entry_site + 0x140;   /* FUN_0045e0a0 size ~0x120 */
        int n = 0;
        while (p + S5_EPI_LEN <= end) {
            if (memcmp(p, S5_EPI_SIG, S5_EPI_LEN) == 0) {
                if (!rel32_fits(p, (void *)s5_epi_hook)) {
                    ld_info("install_site5: epilogue E9 rel32 out of range at 0x%08x",
                            (unsigned)p);
                } else if (write_jmp(p, (void *)s5_epi_hook, S5_EPI_LEN)) {
                    ld_info("install_site5: epilogue hook at 0x%08x", (unsigned)p);
                    n++;
                } else {
                    ld_info("install_site5: epilogue write_jmp FAILED at 0x%08x",
                            (unsigned)p);
                }
                p += S5_EPI_LEN;
            } else {
                p++;
            }
        }
        ld_info("install_site5: %d epilogue hooks", n);
    }
    ld_info("install_site5: hooks installed (diagnostic)");
}

static void install_site6(void)
{
    unsigned char *entry_site;
    unsigned char *tail_site;
    const char *env;

    if (!ld_off_ok || !ld_off_ok2) {
        ld_info("install_site6: struct layout mismatch (LDObject/TreeNode) -> NOT installed");
        return;
    }
    env = getenv(S6_ENV);
    if (env == NULL || *env == '\0' || atoi(env) == 0) {
        ld_info("install_site6: disabled (%s not set) -> hook NOT installed", S6_ENV);
        return;
    }
    /* site6 активен: точный порт FUN_0045c3a0 (s6_path_tree_search) на месте,
     * fallback «левый объект» снят (п. 2.3) — при невалидном дереве цепочки
     * path search даёт NULL; callers правят п. 2.4/2.6.
     * Полный detour FUN_0045e0a0. Callees (dd70/c880/c5a0/ce80) портированы (battle/),
     * оригиналы НЕ вызываются (крашатся из .pyd). */
    entry_site = find_in_text((const unsigned char *)S6_ENTRY_SIG, S6_ENTRY_LEN);
    tail_site = find_in_text((const unsigned char *)S6_TAIL_SIG, S6_TAIL_LEN);
    if (entry_site == NULL || tail_site == NULL) {
        ld_info("install_site6: signature NOT found (entry=%p tail=%p)",
                (void *)entry_site, (void *)tail_site);
        return;
    }

    ld_info("install_site6: entry=0x%08x tail=0x%08x (callees ported to C)",
            (unsigned)entry_site, (unsigned)tail_site);

    /* Кэш timeGetTime из IAT EXE (VA 0x4ec27c) — игровой таймер предиката
     * s6_check_timeout (native `call *0x4ec27c`, п. 3.3). EXE без ASLR
     * (imagebase 0x400000) → base+RVA валиден как абсолютный адрес.
     * Предикат вызывается faithful-циклом s6_target_search (@0x45e14e). */
    {
        void *iat = native::fn(S6_VA_timeGetTime);
        g_s6_gettime = (iat != NULL) ? (PFN_timeGetTime)*(void **)iat : NULL;
    }
    (void)&s6_check_timeout;

    if (!rel32_fits(entry_site, (void *)s6_entry_hook)) {
        ld_info("install_site6: E9 rel32 out of range");
        return;
    }
    if (!write_jmp(entry_site, (void *)s6_entry_hook, S6_ENTRY_LEN)) {
        ld_info("install_site6: entry write_jmp FAILED");
        return;
    }
    ld_info("install_site6: hook installed (full detour, original NOT executed)");
}

/* ------------------------------------------------------------------ */
/* Trace-наблюдатели тика (H1/H2/H7) — только при LDHOOK_BATTLE_TRACE=1. */
/* H4 (0x45e0a0) отдельного детура не получает: сброс счётчиков встроен */
/* во вход site5/site6 (battle/target.cpp).                             */
/* ------------------------------------------------------------------ */

static void install_trace_sites(void)
{
    unsigned char *h1;
    unsigned char *h2;
    unsigned char *h7;

    if (!battle_trace_enabled()) {
        ld_info("install_trace_sites: %s off -> hooks NOT installed",
                BATTLE_TRACE_ENV);
        return;
    }
    h1 = find_in_text((const unsigned char *)H1_SIG, H1_SIG_LEN);
    h2 = find_in_text((const unsigned char *)H2_SIG, H2_SIG_LEN);
    h7 = find_in_text((const unsigned char *)H7_SIG, H7_SIG_LEN);
    if (h1 == NULL || h2 == NULL || h7 == NULL) {
        ld_info("install_trace_sites: signature NOT found (h1=%p h2=%p h7=%p)",
                (void *)h1, (void *)h2, (void *)h7);
        return;
    }

    g_h1_cont = (void *)(h1 + H1_SIG_LEN);
    g_h1_je = (void *)(h1 + 4 + (signed char)h1[3]);
    g_h2_cont = (void *)(h2 + 7);
    g_h7_cont = (void *)(h7 + H7_SIG_LEN);

    ld_info("install_trace_sites: h1=0x%08x h2=0x%08x h7=0x%08x je=0x%08x",
            (unsigned)h1, (unsigned)h2, (unsigned)h7, (unsigned)g_h1_je);

    if (!rel32_fits(h1, (void *)h1_tick_hook) ||
        !rel32_fits(h2, (void *)h2_epilogue_hook) ||
        !rel32_fits(h7, (void *)h7_visit_hook)) {
        ld_info("install_trace_sites: E9 rel32 out of range");
        return;
    }
    if (!write_jmp(h1, (void *)h1_tick_hook, H1_SIG_LEN)) {
        ld_info("install_trace_sites: h1 write_jmp FAILED");
        return;
    }
    if (!write_jmp(h2, (void *)h2_epilogue_hook, 7)) {
        ld_info("install_trace_sites: h2 write_jmp FAILED");
        return;
    }
    if (!write_jmp(h7, (void *)h7_visit_hook, H7_SIG_LEN)) {
        ld_info("install_trace_sites: h7 write_jmp FAILED");
        return;
    }
    ld_info("install_trace_sites: hooks installed (trace ON)");
}

/* Замена 7-байтного `call [IAT]; push eax` на `push imm32; nop; nop`. */
static int write_seed_push(void *target, unsigned long value)
{
    DWORD old;
    unsigned char patch[7];

    patch[0] = 0x68;                                /* push imm32 */
    *(unsigned int *)(patch + 1) = (unsigned int)value;
    patch[5] = 0x90;
    patch[6] = 0x90;
    if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) {
        return 0;
    }
    memcpy(target, patch, sizeof(patch));
    VirtualProtect(target, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    return 1;
}

/* R1 0x49ebcf + R2/R3 (сигнатура R2 встречается дважды: 0x4a7677/0x4ae87f). */
static void install_seed_sites(void)
{
    unsigned long seed;
    unsigned char *site;
    unsigned char *p;
    unsigned char *end;
    int n = 0;

    if (!battle_seed_get(&seed)) {
        ld_info("install_seed_sites: %s unset -> RNG seed hooks NOT installed",
                BATTLE_SEED_ENV);
        return;
    }

    site = find_in_text((const unsigned char *)R1_SIG, R_SIG_LEN);
    if (site != NULL && write_seed_push(site, seed)) {
        ld_info("install_seed_sites: r1 0x%08x -> push 0x%08x",
                (unsigned)site, (unsigned)seed);
        n++;
    } else {
        ld_info("install_seed_sites: r1 NOT installed (site=%p)", (void *)site);
    }

    p = g_text;
    end = g_text + g_text_len;
    while (p + R_SIG_LEN <= end) {
        site = find_sig(p, (size_t)(end - p),
                        (const unsigned char *)R2_SIG, R_SIG_LEN);
        if (site == NULL) {
            break;
        }
        if (write_seed_push(site, seed)) {
            ld_info("install_seed_sites: r2/r3 0x%08x -> push 0x%08x",
                    (unsigned)site, (unsigned)seed);
            n++;
        } else {
            ld_info("install_seed_sites: r2/r3 write FAILED at 0x%08x",
                    (unsigned)site);
        }
        p = site + R_SIG_LEN;
    }
    ld_info("install_seed_sites: %d site(s) pinned to seed=%lu",
            n, seed);
}

/* Игровой whrandom (Wichman-Hill) — сеется из загрузчика .pyd, без правки
 * ui.py/ui.pyc. Если PyRun недоступен — пишем deviation, игра не тронута. */
static void ld_seed_whrandom(void)
{
    unsigned long seed;

    if (!battle_seed_get(&seed)) {
        return;
    }
    if (PyRun_SimpleString("import whrandom; whrandom.seed(7, 11, 13)") != 0) {
        PyErr_Clear();
        ld_info("whrandom: seed FAILED (import/seed via PyRun_SimpleString); "
                "RNG pinning partial");
    } else {
        ld_info("whrandom: seeded (7,11,13) for %s=%lu",
                BATTLE_SEED_ENV, seed);
    }
}

/* ------------------------------------------------------------------ */
/* Python-API модуля (отладка из игровых скриптов)                      */
/* ------------------------------------------------------------------ */

static PyObject *ld_py_log(PyObject *self, PyObject *args)
{
    const char *msg;
    (void)self;
    if (!PyArg_ParseTuple(args, "s:log", &msg)) {
        return NULL;
    }
    ld_info("py: %s", msg);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *ld_py_set_level(PyObject *self, PyObject *args)
{
    int level;
    int old;
    (void)self;
    if (!PyArg_ParseTuple(args, "i:set_level", &level)) {
        return NULL;
    }
    if (level < LD_OFF) {
        level = LD_OFF;
    }
    if (level > LD_DEBUG) {
        level = LD_DEBUG;
    }
    old = (int)InterlockedExchange(&g_log_level, (LONG)level);
    ld_info("set_level: %d -> %d", old, level);
    return PyInt_FromLong((long)old);
}

/* stats(): счётчики API + faithful-цикл e0a0 (п. 3.4).
 *   (site1, site2, site3, level,
 *    fc_calls, fc_iters, fc_code1, fc_code3, fc_code4, fc_rot,
 *    fc_us_sum, fc_us_max)
 * fc_* — вызовы faithful-цикла, сумма итераций, коды выхода 1/3/4
 * (code=0 — пустой скан, = fc_calls − fc_code1 − fc_code3 − fc_code4),
 * ротации (rescan после не-сработавшего гейта), µs сумма/максимум. */
static PyObject *ld_py_stats(PyObject *self, PyObject *args)
{
    (void)self;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    return Py_BuildValue("(llllllllllLL)", (long)g_site1_calls,
                         (long)g_site2_calls, (long)g_site3_calls,
                         (long)g_log_level,
                         (long)g_site6_calls, (long)g_site6_iter_sum,
                         (long)g_site6_code1, (long)g_site6_code3,
                         (long)g_site6_code4, (long)g_site6_rot,
                         (LONGLONG)g_s6_us_sum, (LONGLONG)g_s6_us_max);
}

static PyObject *ld_py_site6_stats(PyObject *self, PyObject *args)
{
    (void)self;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    return Py_BuildValue("(llll)", (long)g_site6_calls,
                         (long)g_site6_heavy, (long)g_site6_max,
                         (long)g_log_level);
}

static PyMethodDef ldhook_methods[] = {
    {"log", ld_py_log, METH_VARARGS,
     "log(msg): write an info line to hdlog.txt (no-op at level 0)"},
    {"set_level", ld_py_set_level, METH_VARARGS,
     "set_level(n): 0=off, 1=info (default), 2=debug; returns previous level"},
    {"stats", ld_py_stats, METH_VARARGS,
     "stats(): (site1, site2, site3, level, fc_calls, fc_iters, fc_code1, "
     "fc_code3, fc_code4, fc_rot, fc_us_sum, fc_us_max)"},
    {"site6_stats", ld_py_site6_stats, METH_VARARGS,
     "site6_stats(): (calls, heavy, max_iter, log_level)"},
    {NULL, NULL, 0, NULL}
};

/* DL_EXPORT в 2.2.3 не экспортирует (USE_DL_EXPORT не определён для MSVC в
 * PC/pyconfig.h) — объявляем экспорт явно. methods не может быть NULL:
 * Py_InitModule4 в 2.2.3 разыменовывает methods без проверки.
 *
 * extern "C" — ОБЯЗАТЕЛЕН: imp.load_dynamic ищет символ по имени (без него
 * C++ манглит имя и загрузка .pyd ломается). Порядок спецификаторов важен:
 * linkage-specification (extern "C") должен предшествовать __declspec, иначе
 * MSVC ругается C4502/C4518 и dllexport игнорируется. */
extern "C" __declspec(dllexport) void initldhook(void)
{
    unsigned char *base;

    ld_log_init();
    native::init();
    battle_trace_init();
    base = native::base();
    if (base != NULL && ld_find_text(base, &g_text, &g_text_len)) {
        ld_info("=== ldhook.pyd phase 6 (hooks + logger + target-search diag) pid=%u base=0x%08x .text=0x%08x+0x%x level=%d ===",
                (unsigned)GetCurrentProcessId(), (unsigned)(size_t)base,
                (unsigned)(size_t)g_text, (unsigned)g_text_len,
                (int)g_log_level);
    } else {
        ld_info("=== ldhook.pyd phase 6 (hooks + logger + target-search diag) pid=%u base=0x%08x level=%d; .text NOT found ===",
                (unsigned)GetCurrentProcessId(), (unsigned)(size_t)base,
                (int)g_log_level);
    }

    ldhook_marker();
    if (base == NULL) {
        ld_info("initldhook: no EXE base");
    } else if (g_text == NULL) {
        ld_info("initldhook: no .text section, hooks skipped");
    } else {
        install_site1();
        install_site2();
        install_site3();
        /* site6 (полный порт e0a0) и site5 (диагностика) ставятся на ОДНИ И ТЕ ЖЕ
         * адреса (пролог 0x45e0a0 + хвост 0x45e163). Если site6 активен — site5
         * пропускаем, иначе E9 от site5 затрёт сигнатуру и site6 не встанет. */
        {
            const char *s6env = getenv(S6_ENV);
            int s6_on = (s6env && *s6env && atoi(s6env) != 0);
            if (!s6_on) {
                install_site5();
            } else {
                ld_info("initldhook: %s=1 -> site5 (diag) пропущен, ставится site6", S6_ENV);
            }
            install_site6();
        }
        install_trace_sites();
        install_seed_sites();
    }
    ld_seed_whrandom();
    Py_InitModule("ldhook", ldhook_methods);
}
