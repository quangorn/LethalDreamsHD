/* ldhook.pyd — Фазы 1–3, 5–6 (PLAN_proxy_dll.md, §4): хуки в памяти + логгер.
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
 *   3. install_site1/2/3/5() — находит сайты сканом сигнатур в .text EXE и
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
 *          логирование аргументов, кандидатов, метрик, порогов (site4 cap
 *          сохранён в коде, но не вызывается).
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
#include <math.h>

/* ------------------------------------------------------------------ */
/* Диагностика (Фаза 3): уровни, hdlog.txt, OutputDebugString           */
/*                                                                     */
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

#define LDHOOK_LOG_NAME "hdlog.txt"
#define LDHOOK_LOG_ENV  "LDHOOK_LOG"
#define LDHOOK_LOG_MAX  (8u * 1024u * 1024u)

#define LD_OFF   0
#define LD_INFO  1
#define LD_DEBUG 2

static volatile LONG g_log_level = LD_INFO;
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

static void ld_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ld_vlog(LD_INFO, fmt, ap);
    va_end(ap);
}

static void ld_debug(const char *fmt, ...)
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
static int ld_rate(volatile LONG *ctr, LONG first, LONG each)
{
    LONG n = InterlockedIncrement(ctr);
    return n <= first || (each > 0 && (n % each) == 0);
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
/* site1 — порт cave1 (fixall.s): масштаб листов/advance 4x-шрифтов.    */
/*                                                                     */
/* ecx (this) = объект шрифта. Логика 1:1 с fixall.s cave1:            */
/*   this->byte[0x58] = 1;                                             */
/*   ctx = *(void**)0x51cc7c;  k = (h*1024)/(w*768), k=min(k,1);       */
/*   k==1.0 -> identity (4:3, листы родные);                           */
/*   иначе k_eff = k*0.25; this->pitch(0x40) = (int)(pitch*k_eff);     */
/*   advance: v=(a-CORR)*k_eff+carry; a_store=(int)v; carry=v-a_store. */
/* CORR=1.3384 (4*A, A=0.3346 — компенсация межбуквенного интервала).  */
/* Округление к int — к ближайшему чётному (FIST при round-to-nearest). */
/* ------------------------------------------------------------------ */

#define SITE1_SIG      "\xc6\x41\x58\x01\x59"      /* mov byte[ecx+0x58],1; pop ecx */
#define SITE1_SIG_LEN  5
#define ASPECT_CTX     ((volatile unsigned char *)0x51cc7c)
#define GLYPH_ADV      0x6c     /* advance в записи глифа (20 байт) */
#define GLYPH_STRIDE   0x14
#define GLYPH_TABLE_END 0x145c  /* конец таблицы глифов (this+0x145c) */
#define FONT_PITCH     0x40     /* pitch (int) */
#define SHEET_SCALE    0.25     /* лист апскейлен в 4x */
#define ADV_CORR       1.3384   /* 4*A */

/* Тело cave1. Возвращает управление в исходный caller (pop ecx; ret делает трамплин). */
static volatile LONG g_site1_calls = 0;
static volatile LONG g_site1_dbg = 0;

static void __cdecl site1_impl(void *this_)
{
    unsigned char *self = (unsigned char *)this_;
    unsigned char *ctx;
    double k, keff, carry;
    int *p;
    int trace = 0;
    long n = InterlockedIncrement(&g_site1_calls);

    if (n == 1) {
        ld_info("site1_impl: first call (hook FIRED)");
    }

    self[0x58] = 1;
    ctx = (unsigned char *)*(void **)ASPECT_CTX;

    /* k = (h*1024.0) / (w*768.0); при k>1 зажимаем в 1.0 (не растягиваем). */
    {
        int h = *(int *)(ctx + 0x1c);
        int w = *(int *)(ctx + 0x18);
        k = ((double)h * 1024.0) / ((double)w * 768.0);
    }
    if (k > 1.0) {
        k = 1.0;
    }
    if (k == 1.0) {
        if (n == 1) {
            ld_info("site1_impl: k==1.0 -> identity (4:3), w=%d h=%d",
                    *(int *)(ctx + 0x18), *(int *)(ctx + 0x1c));
        }
        return;                         /* identity: 4:3 / k==1 */
    }

    keff = k * SHEET_SCALE;             /* 4x-листы -> 0.25 */
    if (n == 1) {
        ld_info("site1_impl: k=%f -> scale path, keff=%f", k, keff);
    }
    trace = ld_rate(&g_site1_dbg, 4, 1024);

    /* pitch *= k_eff (FISTP — к ближайшему чётному). */
    p = (int *)(self + FONT_PITCH);
    {
        int pitch_old = *p;
        *p = (int)nearbyint((double)(*p) * keff);
        if (trace) {
            ld_debug("site1_impl: call %ld k=%f keff=%f pitch %d -> %d",
                     n, k, keff, pitch_old, *p);
        }
    }

    /* advance: v=(a-CORR)*k_eff+carry; a=(int)v; carry=v-a. */
    carry = 0.0;
    {
        unsigned char *rec = self + GLYPH_ADV;
        unsigned char *end = self + GLYPH_TABLE_END;
        for (; rec < end; rec += GLYPH_STRIDE) {
            int *adv = (int *)rec;
            double v = ((double)(*adv) - ADV_CORR) * keff + carry;
            int stored = (int)nearbyint(v);
            *adv = stored;
            carry = v - (double)stored;
        }
    }
    if (trace) {
        ld_debug("site1_impl: call %ld advance table done, carry=%f", n, carry);
    }
}

/* Трамплин: вход — E9 из site1, ecx=this, [esp]=saved ecx (push ecx в начале
 * FUN_0041cc90), [esp+4]=retaddr. Хвост оригинала (pop ecx; ret) исполняем сами.
 * site1_impl — __cdecl(arg) — аргумент снимает caller (add esp,4). */
static __declspec(naked) void site1_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx                 /* аргумент site1_impl(this); ecx уже = this */
        call site1_impl
        add esp, 4               /* cdecl: снять аргумент */
        pop edx
        pop eax
        pop ecx                  /* оригинальный хвост site1: pop ecx (saved ecx) */
        ret
    }
}

/* ------------------------------------------------------------------ */
/* site2 — порт cave2 (fixall.s): нормировка шрифта сразу при SetFont.  */
/*                                                                     */
/* Вход: ecx = this (куда пишется шрифт), eax = шрифт; [esp] = ret,     */
/* [esp+4]/[esp+8] = два аргумента сайта (функция возвращает ret 8).    */
/* Оригинал (0x40b494, 6 байт): mov [ecx+0x294],eax; inc [eax+0x24];    */
/*   ret 8. Cave2 добавляет: если byte[font+0x58]==0 и [font+0x44]!=0,  */
/*   зовёт FUN_0041cc90(font) — нормировка/масштаб до первой раскладки. */
/* ------------------------------------------------------------------ */

#define SITE2_SIG       "\x89\x81\x94\x02\x00\x00"  /* mov [ecx+0x294],eax */
#define SITE2_SIG_LEN   6
#define SITE2_PATCH     6                           /* E9 rel32 + nop */
#define SITE2_OWNER     0x294
#define FONT_REFCOUNT   0x24
#define FONT_NORMALIZED 0x58
#define FONT_SHEET      0x44

static void *g_site2_norm;          /* FUN_0041cc90 (нормировка шрифта) */
static volatile LONG g_site2_calls = 0;
static volatile LONG g_site2_dbg = 0;

/* Выполняет сохранённые байты site2 и решает, нужна ли нормировка.
 * Возвращает 1, если трамплин должен позвать g_site2_norm(font) (thiscall;
 * сам вызов делается в __asm — в C-режиме MSVC нет __thiscall). */
static __declspec(noinline) int __cdecl site2_impl(void *this_, void *font_)
{
    unsigned char *self = (unsigned char *)this_;
    unsigned char *font = (unsigned char *)font_;
    int need;
    long n = InterlockedIncrement(&g_site2_calls);

    if (n == 1) {
        ld_info("site2_impl: first call (hook FIRED) font=%p byte58=%d sheet=%p",
                (void *)font, (int)font[FONT_NORMALIZED],
                *(void **)(font + FONT_SHEET));
    }

    /* Сохранённые байты оригинала. */
    *(unsigned int *)(self + SITE2_OWNER) = (unsigned int)font;
    ++*(unsigned int *)(font + FONT_REFCOUNT);

    /* Cave2: нормировать шрифт сразу, до первой раскладки/SetText. */
    need = (font[FONT_NORMALIZED] == 0 && *(void **)(font + FONT_SHEET) != NULL);
    if (ld_rate(&g_site2_dbg, 4, 1024)) {
        ld_debug("site2_impl: call %ld font=%p byte58=%d sheet=%p -> norm=%d",
                 n, (void *)font, (int)font[FONT_NORMALIZED],
                 *(void **)(font + FONT_SHEET), need);
    }
    return need;
}

/* Трамплин: сохраняет eax/edx/ecx (как cave2), зовёт site2_impl; если он вернул 1,
 * зовёт FUN_0041cc90(font) (ecx=font, как в cave2), затем ret 8. */
static __declspec(naked) void site2_hook(void)
{
    __asm {
        push ecx
        push edx
        push eax
        push eax                 /* arg2: font */
        push ecx                 /* arg1: this */
        call site2_impl
        add esp, 8               /* cdecl: снять аргументы */
        test eax, eax
        jz skip_norm
        mov ecx, [esp]           /* сохранённый eax = font */
        call dword ptr [g_site2_norm]
    skip_norm:
        pop eax
        pop edx
        pop ecx
        ret 8                    /* оригинальный хвост site2 (ret 8) */
    }
}

/* ------------------------------------------------------------------ */
/* site3 — порт cave3 (fixall.s): x-коэффициент world-проекции.        */
/*                                                                     */
/* Вход: ecx = ctx, edx = матрица (camobj+0xf8); [esp]=idx(2),          */
/* [esp+4]=matrix (сеттер FUN_0046f970 — thiscall, ret 8). Перед        */
/* сеттером: matrix[0] = matrix[5] * h/w (w,h = ctx+0x18/+0x1c).        */
/* Guard 4*h == 3*w (k=1, 4:3) -> запись пропускается ПОБИТОВО: движок  */
/* хранит fl32(0.75*cot), а fl32(0.75*fl32(cot)) даёт ULP+1             */
/* (docs/OBSERVATIONS_ENGINE.md).                                       */
/* ------------------------------------------------------------------ */

#define SITE3_SIG       "\xe8\xeb\x1b\xff\xff"      /* call FUN_0046f970 */
#define SITE3_SIG_LEN   5
#define MATRIX_X        0x00
#define MATRIX_X_SCALE  0x14                        /* matrix[5] */

static void *g_site3_setter;        /* FUN_0046f970 */
static void *g_site3_cont;          /* 0x47dd85: продолжение RenderWorld */
static volatile LONG g_site3_calls = 0;
static volatile LONG g_site3_dbg = 0;

/* Точная копия FP-последовательности cave3 (x87, 80 бит):
 *   fild h; fild w; fdivp (h/w); fmul m32 matrix[5]; fstp m32 matrix[0].
 * C-вызов cdecl: [esp]=ret, [esp+4]=ctx, [esp+8]=matrix. */
static void __cdecl site3_fp(void *ctx, void *matrix);
static __declspec(naked) void site3_fp(void *ctx, void *matrix)
{
    __asm {
        mov ecx, [esp+4]         /* ctx */
        mov eax, [esp+8]         /* matrix */
        fild dword ptr [ecx+0x1c]
        fild dword ptr [ecx+0x18]
        fdivp st(1), st(0)
        fmul dword ptr [eax+0x14]
        fstp dword ptr [eax]
        ret
    }
}

static __declspec(noinline) void __cdecl site3_impl(void *ctx_, void *matrix_)
{
    unsigned char *ctx = (unsigned char *)ctx_;
    unsigned char *matrix = (unsigned char *)matrix_;
    int h = *(int *)(ctx + 0x1c);
    int w = *(int *)(ctx + 0x18);
    long n;

    if (4 * h == 3 * w) {
        if (InterlockedIncrement(&g_site3_calls) == 1) {
            ld_info("site3_impl: first call (hook FIRED) guard 4h==3w -> identity (w=%d h=%d)",
                    w, h);
        }
        return;                     /* identity: 4:3 / k==1 */
    }

    {
        float old = *(float *)(matrix + MATRIX_X);
        float m5 = *(float *)(matrix + MATRIX_X_SCALE);
        int first;
        float res;

        n = InterlockedIncrement(&g_site3_calls);
        first = (n == 1);
        site3_fp(ctx, matrix);      /* matrix[0] = matrix[5]*h/w (x87, как cave3) */
        res = *(float *)(matrix + MATRIX_X);

        if (first) {
            ld_info("site3_impl: first call (hook FIRED) w=%d h=%d matrix[0] %g (0x%08X) -> %g (0x%08X) m5=%g (0x%08X)",
                    w, h, old, *(unsigned int *)&old,
                    res, *(unsigned int *)&res,
                    m5, *(unsigned int *)&m5);
        }
        if (ld_rate(&g_site3_dbg, 3, 300)) {
            ld_debug("site3_impl: call %ld w=%d h=%d matrix[0] %g (0x%08X) -> %g (0x%08X) m5=%g",
                     n, w, h, old, *(unsigned int *)&old,
                     res, *(unsigned int *)&res, m5);
        }
    }
}

/* Трамплин: сохраняет ecx/edx, зовёт site3_impl, затем оригинальный
 * call FUN_0046f970 и jmp 0x47dd85. call/jmp — через указатели, чтобы код
 * .pyd не зависел от абсолютных адресов EXE; eax/edi — scratch (как в cave3). */
static __declspec(naked) void site3_hook(void)
{
    __asm {
        push ecx                 /* save ctx */
        push edx                 /* save matrix */
        push edx                 /* arg2: matrix */
        push ecx                 /* arg1: ctx */
        call site3_impl
        add esp, 8
        pop edx                  /* restore matrix */
        pop ecx                  /* restore ctx */
        call dword ptr [g_site3_setter]
        jmp dword ptr [g_site3_cont]
    }
}

/* ------------------------------------------------------------------ */
/* site4 — cap итераций цикла поиска цели (FUN_0045e0a0): тормоза боя. */
/*                                                                    */
/* Сайт цикла (установленный EXE, проверено 18.11.2026):               */
/*   0x45e117 верх итерации; хвост 0x45e163:                          */
/*     mov esi,eax (2) | cmp esi,ebp (2) | jne 0x45e117 (2) = 6 байт; */
/*   0x45e0a0 пролог: mov eax,[esp+0x10] (4) | sub esp,0x34 (3)=7 Б;  */
/*   0x45e169 штатный выход цикла (pop ebx; ...; ret 0x10).           */
/*                                                                    */
/* Патология (docs/PERF_BATTLE.md): до ~4400 итераций, 71–73 мс; норма */
/* ~2 итерации. Cap ограничивает итерации и выходит штатным «не        */
/* найдено»: eax=esi=0 -> 0x45e169 (та же точка, что у обычного        */
/* NULL-выхода цикла: вызывающий обрабатывает «цель не выбрана»).      */
/*                                                                    */
/* Счётчик итераций сбрасывается на ВХОДЕ функции (второй крошечный    */
/* хук): иначе после «найдено» остаток счётчика переезжает на следующий */
/* вызов. Если FUN_0045e0a0 окажется реентерабельной, внутренний вызов */
/* просто сбросит счётчик (cap ослабнет, но не ужесточится).           */
/* Cap: env LDHOOK_CAP (дефолт 32; 0 = хук не ставится, чистый A/B).    */
/* ------------------------------------------------------------------ */

#define S4_ENTRY_SIG   "\x8b\x44\x24\x10\x83\xec\x34"   /* mov eax,[esp+0x10]; sub esp,0x34 */
#define S4_ENTRY_LEN   7
#define S4_TAIL_SIG    "\x8b\xf0\x3b\xf5\x75\xae"       /* mov esi,eax; cmp esi,ebp; jne -0x52 */
#define S4_TAIL_LEN    6
#define S4_HEAVY_ITER  20        /* > нормы (~2): считаем вызов тяжёлым */
#define S4_CAP_DEFAULT 32
#define S4_CAP_ENV     "LDHOOK_CAP"

static volatile LONG g_site4_iter = 0;   /* итерации текущего вызова */
static volatile LONG g_site4_calls = 0;  /* вызовов FUN_0045e0a0 */
static volatile LONG g_site4_caps = 0;   /* срабатываний cap */
static volatile LONG g_site4_heavy = 0;  /* вызовов с >S4_HEAVY_ITER итерациями */
static volatile LONG g_site4_max = 0;    /* максимум итераций на вызов */
static volatile LONG g_site4_dbg = 0;
static volatile LONG g_site4_cap_dbg = 0;
static LONG g_site4_cap = S4_CAP_DEFAULT;

static void *g_site4_cont = NULL;        /* entry+7: продолжение пролога */
static void *g_site4_top = NULL;         /* верх цикла (цель jne) */
static void *g_site4_nat = NULL;         /* tail+6: штатный выход цикла */

/* Вход функции: зафиксировать итерации прошлого вызова и сбросить счётчик. */
static void __cdecl s4_entry_impl(void)
{
    LONG prev = g_site4_iter;

    InterlockedIncrement(&g_site4_calls);
    if (prev > g_site4_max) {
        g_site4_max = prev;
    }
    if (prev > S4_HEAVY_ITER) {
        InterlockedIncrement(&g_site4_heavy);
    }
    g_site4_iter = 0;
    if (ld_rate(&g_site4_dbg, 4, 512)) {
        ld_debug("site4: entry #%ld prev_iter=%ld max=%ld heavy=%ld caps=%ld cap=%ld",
                 (long)g_site4_calls, prev, (long)g_site4_max,
                 (long)g_site4_heavy, (long)g_site4_caps, g_site4_cap);
    }
}

/* Cap сработал: выход «цель не найдена». */
static void __cdecl s4_cap_impl(void)
{
    LONG caps = InterlockedIncrement(&g_site4_caps);

    if (caps <= 3 || (caps % 128) == 0) {
        ld_info("site4: cap %ld fired (cap=%ld, call #%ld)",
                caps, g_site4_cap, (long)g_site4_calls);
    }
}

/* Трамплин входа: повторяет украденные 7 байт и уходит в entry+7. */
static __declspec(naked) void s4_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        call s4_entry_impl
        pop ecx
        pop edx
        pop eax
        mov eax, dword ptr [esp + 0x10]
        sub esp, 0x34
        jmp dword ptr [g_site4_cont]
    }
}

/* Трамплин хвоста: повторяет mov/cmp, затем либо штатный выход (esi==0),
 * либо счёт итераций с проверкой cap. Cap -> eax=esi=0 -> штатный выход. */
static __declspec(naked) void s4_tail_hook(void)
{
    __asm {
        mov esi, eax
        cmp esi, ebp
        jne count_iter
        jmp dword ptr [g_site4_nat]
    count_iter:
        inc dword ptr [g_site4_iter]
        mov eax, dword ptr [g_site4_iter]
        cmp eax, dword ptr [g_site4_cap]
        jbe loop_again
        xor eax, eax
        xor esi, esi
        push eax
        push edx
        push ecx
        call s4_cap_impl
        pop ecx
        pop edx
        pop eax
        jmp dword ptr [g_site4_nat]
    loop_again:
        mov eax, esi
        jmp dword ptr [g_site4_top]
    }
}

static LONG s4_parse_cap(void)
{
    const char *s = getenv(S4_CAP_ENV);
    long v;

    if (s == NULL || *s == '\0') {
        return S4_CAP_DEFAULT;
    }
    v = strtol(s, NULL, 10);
    if (v < 0) {
        v = 0;
    }
    if (v > 100000) {
        v = 100000;
    }
    return (LONG)v;
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
 * (5 байт для site1/site3, 6 для site2: E9+nop, как в exepatch12). */
static int write_jmp(void *target, void *dest, int total)
{
    DWORD old;
    unsigned char patch[8];
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
    g_site2_norm = (void *)((unsigned char *)GetModuleHandleA(NULL) + 0x1cc90);
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

/* Cap итераций поиска цели (site4): пролог + хвост цикла FUN_0045e0a0.
 * Цели переходов берём из самой функции (entry+7, tail+6, tail+6+rel8),
 * а не из констант VA — переживает любые сдвиги кода при правках EXE. */
static int s4_rel_fits(void *site, void *dest)
{
    ptrdiff_t d = (unsigned char *)dest - (unsigned char *)site;
    return d > -0x7fff0000 && d < 0x7fff0000;
}

static void install_site4(void)
{
    unsigned char *entry_site;
    unsigned char *tail_site;
    long cap = s4_parse_cap();
    int rel;

    g_site4_cap = cap;
    if (cap == 0) {
        ld_info("install_site4: cap=0 (%s) -> hook NOT installed (A/B baseline)",
                S4_CAP_ENV);
        return;
    }
    entry_site = find_in_text((const unsigned char *)S4_ENTRY_SIG, S4_ENTRY_LEN);
    tail_site = find_in_text((const unsigned char *)S4_TAIL_SIG, S4_TAIL_LEN);
    if (entry_site == NULL || tail_site == NULL) {
        ld_info("install_site4: signature NOT found (entry=%p tail=%p)",
                (void *)entry_site, (void *)tail_site);
        return;
    }
    rel = (signed char)tail_site[5];
    g_site4_cont = (void *)(entry_site + S4_ENTRY_LEN);
    g_site4_nat = (void *)(tail_site + S4_TAIL_LEN);
    g_site4_top = (void *)(tail_site + S4_TAIL_LEN + rel);
    ld_info("install_site4: cap=%ld entry=0x%08x tail=0x%08x top=0x%08x exit=0x%08x",
            cap, (unsigned)entry_site, (unsigned)tail_site,
            (unsigned)g_site4_top, (unsigned)g_site4_nat);
    if (!s4_rel_fits(entry_site, (void *)s4_entry_hook) ||
        !s4_rel_fits(tail_site, (void *)s4_tail_hook)) {
        ld_info("install_site4: E9 rel32 out of range (EXE 0x%08x, hook 0x%08x)",
                (unsigned)entry_site, (unsigned)s4_entry_hook);
        return;
    }
    if (!write_jmp(entry_site, (void *)s4_entry_hook, S4_ENTRY_LEN)) {
        ld_info("install_site4: entry write_jmp FAILED");
        return;
    }
    if (!write_jmp(tail_site, (void *)s4_tail_hook, S4_TAIL_LEN)) {
        ld_info("install_site4: tail write_jmp FAILED");
        return;
    }
    ld_info("install_site4: hooks installed (entry E9+nop*2, tail E9+nop)");
}
/* ------------------------------------------------------------------ */
/* site5 — диагностика поиска цели (FUN_0045e0a0): логирование.       */
/*                                                                    */
/* Заменяет cap-подход site4. На входе и каждой итерации цикла        */
/* пишет в hdlog.txt: аргументы, кандидата, метрику [obj+0x24],       */
/* порог расстояния [this+0x34], состояние трекинга [this+0x30].      */
/*                                                                    */
/* Уровни логирования:                                                */
/*   LDHOOK_LOG=1 (дефолт): только «first call» + сводка;            */
/*   LDHOOK_LOG=2 (debug): первые 4 итерации + каждый 256-й;          */
/*                                                                    */
/* Сайты: пролог 0x45e0a0 (7 Б) + хвост цикла 0x45e163 (6 Б) —       */
/* те же сигнатуры, что у site4.                                       */
/* ------------------------------------------------------------------ */

#define S5_ENTRY_SIG   "\x8b\x44\x24\x10\x83\xec\x34"
#define S5_ENTRY_LEN   7
#define S5_TAIL_SIG    "\x8b\xf0\x3b\xf5\x75\xae"
#define S5_TAIL_LEN    6
#define S5_HEAVY_ITER  20

static volatile LONG g_site5_calls = 0;
static volatile LONG g_site5_iter = 0;
static volatile LONG g_site5_max = 0;
static volatile LONG g_site5_heavy = 0;
static volatile LONG g_site5_dbg = 0;
static volatile LONG g_site5_idbg = 0;

static void *g_site5_cont = NULL;
static void *g_site5_top = NULL;
static void *g_site5_nat = NULL;

/* Вход: фиксирует статистику прошлого вызова, сбрасывает счётчик. */
static void __cdecl s5_entry_impl(void *this_, void *arg0, void *arg1,
                                   void *arg2, void *arg3)
{
    unsigned char *self = (unsigned char *)this_;
    LONG n = InterlockedIncrement(&g_site5_calls);
    LONG prev = g_site5_iter;
    int trace;

    if (prev > g_site5_max) {
        g_site5_max = prev;
    }
    if (prev > S5_HEAVY_ITER) {
        InterlockedIncrement(&g_site5_heavy);
    }
    g_site5_iter = 0;

    if (n == 1) {
        ld_info("site5: first call (hook FIRED)");
    }

    /* Первые 4 вызова — всегда (info); далее — троттлинг (debug). */
    if (n <= 4) {
        /* always log at info level */
    } else {
        trace = ld_rate(&g_site5_dbg, 0, 128);
        if (!trace || g_log_level < LD_DEBUG) {
            return;
        }
    }

    {
        unsigned char tracking = *(unsigned char *)(self + 0x30);
        int threshold = *(int *)(self + 0x34);
        void *stored = *(void **)(self + 0x2c);
        void *list_head = *(void **)(self + 0x6c);

        if (n <= 4)
            ld_info("site5: entry #%ld this=%p a0=%p a1=%p a2=%p a3=%p "
                     "list=%p trk=%d thr=%d stored=%p prev_iter=%ld",
                     (long)n, this_, arg0, arg1, arg2, arg3,
                     list_head, (int)tracking, threshold, stored, (long)prev);
        else
            ld_debug("site5: entry #%ld this=%p a0=%p a1=%p a2=%p a3=%p "
                     "list=%p trk=%d thr=%d stored=%p prev_iter=%ld",
                     (long)n, this_, arg0, arg1, arg2, arg3,
                     list_head, (int)tracking, threshold, stored, (long)prev);
    }
}

/* Трамплин входа. ecx=this, [esp]=ret, [esp+4..0x10]=arg0..arg3. */
static __declspec(naked) void s5_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push dword ptr [esp + 28]    /* arg3 */
        push dword ptr [esp + 28]    /* arg2 */
        push dword ptr [esp + 28]    /* arg1 */
        push dword ptr [esp + 28]    /* arg0 */
        push ecx                     /* this */
        call s5_entry_impl
        add esp, 20
        pop ecx
        pop edx
        pop eax
        mov eax, dword ptr [esp + 0x10]
        sub esp, 0x34
        jmp dword ptr [g_site5_cont]
    }
}

/* Хвост цикла: логирует кандидата, метрику, порог, дистанцию. */
static void __cdecl s5_tail_impl(void *this_, void *candidate)
{
    unsigned char *self = (unsigned char *)this_;
    unsigned char *cand = (unsigned char *)candidate;
    LONG iter = InterlockedIncrement(&g_site5_iter);
    int trace;

    /* Первые 4 итерации — всегда (info); далее — троттлинг (debug). */
    if (iter <= 4) {
        /* always log at info level */
    } else {
        trace = ld_rate(&g_site5_idbg, 0, 256);
        if (!trace || g_log_level < LD_DEBUG) {
            return;
        }
    }

    if (cand == NULL) {
        if (iter <= 4)
            ld_info("site5: iter=%ld cand=NULL", (long)iter);
        else
            ld_debug("site5: iter=%ld cand=NULL", (long)iter);
        return;
    }

    {
        int filter = *(int *)(cand + 0x08);
        int metric = *(int *)(cand + 0x24);
        int x = *(int *)(cand + 0x0c);
        int y = *(int *)(cand + 0x10);
        int threshold = *(int *)(self + 0x34);
        void *stored = *(void **)(self + 0x2c);
        int tracking = *(unsigned char *)(self + 0x30);
        int dist2 = -1;

        if (stored != NULL) {
            int dx = x - *(int *)((unsigned char *)stored + 0x0c);
            int dy = y - *(int *)((unsigned char *)stored + 0x10);
            dist2 = dx * dx + dy * dy;
        }

        if (iter <= 4)
            ld_info("site5: iter=%ld cand=%p [%d,%d] f=%d m=%d d2=%d thr=%d trk=%d",
                     (long)iter, cand, x, y, filter, metric, dist2, threshold,
                     (int)tracking);
        else
            ld_debug("site5: iter=%ld cand=%p [%d,%d] f=%d m=%d d2=%d thr=%d trk=%d",
                     (long)iter, cand, x, y, filter, metric, dist2, threshold,
                     (int)tracking);
    }
}

/* Трамплин хвоста: s5_tail_impl(this, candidate), затем mov/cmp/jne. */
static __declspec(naked) void s5_tail_hook(void)
{
    __asm {
        push eax                    /* save candidate */
        push ecx                    /* save ecx */
        push eax                    /* arg1: candidate */
        push edi                    /* arg0: this */
        call s5_tail_impl
        add esp, 8
        pop ecx
        pop eax                     /* restore candidate */
        mov esi, eax
        cmp esi, ebp
        jne s5_loop_again
        jmp dword ptr [g_site5_nat]

    s5_loop_again:
        jmp dword ptr [g_site5_top]
    }
}

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

    if (!s4_rel_fits(entry_site, (void *)s5_entry_hook) ||
        !s4_rel_fits(tail_site, (void *)s5_tail_hook)) {
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
    ld_info("install_site5: hooks installed (diagnostic)");
}

/* ------------------------------------------------------------------ */
/* site6 — Фаза 1 (PLAN_BATTLE_AI_PORT.md): полная замена (detour)     */
/* поиска цели FUN_0045e0a0 + скана FUN_0045ce80 на C++ в .pyd.        */
/*                                                                    */
/* Горячий путь: O(итерации × n) native (до ~4400 полных сканов дерева */
/* на поиск) → один проход s6_scan_map на скан. Логика 1:1 с оригиналом*/
/* (дизасм Ghidra, подтверждён на установленном EXE):                  */
/*   FUN_0045e0a0(this, param_1, param_2, param_3, param_4):           */
/*     if (param_4) *param_4 = 0;                                     */
/*     cur = scan(this, param_1);                                     */
/*     while (cur) {                                                  */
/*       FUN_0045dd70(this, cur, param_2);   // AI update (ОРИГИНАЛ)   */
/*       FUN_0045c4b0(&local, cur, 4);       // мёртвый код, пропуск   */
/*       if (*(int*)cur == param_2) { *param_4=1; return cur; }       */
/*       if (FUN_0045c880(this, cur)) { *param_4=4; return cur; }     */
/*       if (FUN_0045c5a0(this, param_3)) { *param_4=3; return cur; } */
/*       cur = scan(this, param_1);                                   */
/*     }                                                              */
/*     return NULL;                                                   */
/*   FUN_0045ce80(this, filter): один in-order проход std::map this+0x6c*/
/*     ((TreeNode*)this->[0x6c] = sentinel; obhod ot head->left);     */
/*     фильтр [obj+8]==filter, минимум [obj+0x24], возврат value1.     */
/*                                                                    */
/* Оригинальные предикаты/AI-update НЕ вызываются — портированы на C (выше):*/
/*   FUN_0045dd70 = s6_ai_update (порт: сетка 3×3 + lower_bound map this+0x74,*/
/*     поднимает метрику кандидата [obj+0x24] → scan возвращает другой объект),*/
/*   FUN_0045c480 = s6_distance2, FUN_0045c880 = s6_check_distance,     */
/*   FUN_0045c5a0 = s6_check_timeout. Убирает краш (оригиналы ждут SEH). */
/* Гейт: env LDHOOK_SITE6 (дефолт 0 = выкл); 1/true = поставить хук.     */
/* ------------------------------------------------------------------ */

#define S6_ENTRY_SIG   "\x8b\x44\x24\x10\x83\xec\x34"   /* mov eax,[esp+0x10]; sub esp,0x34 */
#define S6_ENTRY_LEN   7
#define S6_TAIL_SIG    "\x8b\xf0\x3b\xf5\x75\xae"       /* mov esi,eax; cmp esi,ebp; jne -0x52 */
#define S6_TAIL_LEN    6
#define S6_HEAVY_ITER  20        /* > нормы (~2): считаем вызов тяжёлым */
#define S6_CAP         1000000  /* защитный cap итераций (fallback, не норма) */
#define S6_ENV         "LDHOOK_SITE6"

/* Оффсеты (подтверждены дизасмом Ghidra; x86, little-endian). */
#define S6_OFF_filter   0x08     /* LDObject.filter */
#define S6_OFF_x        0x0c     /* LDObject.x (int32) */
#define S6_OFF_y        0x10     /* LDObject.y (int32) */
#define S6_OFF_metric   0x24     /* LDObject.metric (int, min в скане) */
#define S6_OFF_head     0x6c     /* BattleAI: TreeNode* sentinel (std::map) */

/* LDPos — объект позиции (на него ссылается LDObject.pos, [obj+0x00]).
 * Координаты +0x0c/+0x10 читают c480 (distance²) и c880 (предикат дистанции),
 * а dd70 берёт их как центр сетки pathfinding. */
struct LDPos {
    int pad0;                   /* +0x00 */
    int pad4;                   /* +0x04 */
    int pad8;                   /* +0x08 */
    int x;                      /* +0x0c X (int32) */
    int y;                      /* +0x10 Y (int32) */
    int f14;                    /* +0x14 */
};

/* LDObject (кандидат) — node->value1 дерева this+0x6c. Разметка MSVC x86.
 * [obj+0x00] — указатель на LDPos (в e0a0 сравнивается с arg1, в c880 идёт
 * в c480 как объект с координатами); [obj+0x24] — метрика (минимум в ce80). */
struct LDObject {
    LDPos *pos;                 /* +0x00 → LDPos (координаты +0x0c/+0x10) */
    int pad4;                   /* +0x04 */
    int filter;                 /* +0x08 фильтр/тип */
    int x;                      /* +0x0c (не координаты — координаты в pos) */
    int y;                      /* +0x10 */
    int f14;                    /* +0x14 */
    int f18;                    /* +0x18 счётчик (инкремент в dd70) */
    int f1c;                    /* +0x1c */
    int f20;                    /* +0x20 */
    int metric;                 /* +0x24 метрика (минимум в ce80) */
};

/* TreeNode (MSVC std::_Tree node, 0x18 байт). */
struct TreeNode {
    TreeNode *left;             /* +0x00 */
    TreeNode *parent;           /* +0x04 */
    TreeNode *right;            /* +0x08 */
    int       value0;           /* +0x0c ключ */
    void     *value1;           /* +0x10 значение (= LDObject*) */
    unsigned char color;        /* +0x14 (0=чёрный,1=красный) */
    unsigned char is_nil;       /* +0x15 (0=узел,1=sentinel) */
};

static char ld_off_ok =
    (offsetof(LDObject, filter) == S6_OFF_filter &&
     offsetof(LDObject, x)      == S6_OFF_x      &&
     offsetof(LDObject, y)      == S6_OFF_y      &&
     offsetof(LDObject, metric) == S6_OFF_metric) ? 1 : 0;
static char ld_off_ok2 =
    (offsetof(TreeNode, left)   == 0x00 &&
     offsetof(TreeNode, parent) == 0x04 &&
     offsetof(TreeNode, right)  == 0x08 &&
     offsetof(TreeNode, value1) == 0x10 &&
     offsetof(TreeNode, is_nil) == 0x15) ? 1 : 0;

static volatile LONG g_site6_calls = 0;
static volatile LONG g_site6_iter = 0;    /* сканов (итераций) в прошлом вызове */
static volatile LONG g_site6_max = 0;
static volatile LONG g_site6_heavy = 0;
static volatile LONG g_site6_dbg = 0;
static volatile LONG g_site6_depth = 0;   /* защита от рекурсии: dd70 → e0a0 → site6 */
static volatile LONG g_s6_visit = 0;      /* счётчик визитов dd70 (metric rotation) */

/* In-order successor по std::_Tree (порт FUN_0047f800, без вызова). */
static TreeNode *s6_tree_next(TreeNode *node)
{
    TreeNode *next;
    if (node->is_nil) {
        return node;
    }
    next = node->right;
    if (!next->is_nil) {
        while (!next->left->is_nil) {
            next = next->left;
        }
        return next;
    }
    next = node->parent;
    while (!next->is_nil) {
        if (node != next->right) {
            break;
        }
        node = next;
        next = next->parent;
    }
    return next;
}

/* Один in-order проход std::map (порт FUN_0045ce80): this+0x6c = sentinel.
 * Фильтр [obj+8]==filter, минимум беззнакового [obj+0x24]; возврат LDObject*. */
static LDObject *s6_scan_map(void *this_, int filter)
{
    TreeNode *head = *(TreeNode **)((unsigned char *)this_ + S6_OFF_head);
    TreeNode *cur;
    TreeNode *best = head;
    unsigned int best_metric = 999999999u;
    LDObject *obj;
    int steps = 0;

    if (head == NULL) {
        return NULL;
    }
    cur = head->left;                    /* первый (левый) узел дерева */
    while (cur != head && cur != NULL) {
        /* Защита от повреждённого дерева: не больше 10000 шагов. */
        if (++steps > 10000) {
            return NULL;
        }
        obj = (LDObject *)cur->value1;
        if (obj != NULL && obj->filter == filter) {
            unsigned int m = (unsigned int)obj->metric;
            if (m < best_metric) {
                best_metric = m;
                best = cur;
            }
        }
        cur = s6_tree_next(cur);
    }
    if (best != head) {
        return (LDObject *)best->value1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Порты callees FUN_0045e0a0 (site6, Фаза 1). Оригиналы НЕ вызываются:  */
/* они ждут специфический SEH/регистровый контекст и крашатся при вызове */
/* из .pyd (prolog sub esp,0x60; push ebx читает [esp+0x68] → мусор).    */
/* Дизасм REA/Ghidra, build 171319bb…; VA = imagebase 0x400000 + RVA.    */
/* ------------------------------------------------------------------ */

/* FUN_0045c480 — distance²: (a->x−b->x)² + (a->y−b->y)², коорд. +0x0c/+0x10.
 * 32-битная арифметика (как IMUL/ADD оригинала): младшие 32 бита знакового
 * произведения; сравнение в c880 — беззнаковое (JA). */
static int s6_distance2(void *a, void *b)
{
    int dx = *(int *)((unsigned char *)a + 0x0c) -
             *(int *)((unsigned char *)b + 0x0c);
    int dy = *(int *)((unsigned char *)a + 0x10) -
             *(int *)((unsigned char *)b + 0x10);
    return (int)((unsigned int)dx * (unsigned int)dx +
                 (unsigned int)dy * (unsigned int)dy);
}

/* FUN_0045c880 — предикат дистанции (thiscall, RET 4, AL; порт 1:1).
 * this+0x30 (байт tracking): 0 → return 0 (cand не разыменовывается).
 * Иначе dist² между cand->pos (LDObject->[0]) и якорем this->[0x2c]:
 * dist² > this->[0x34] (unsigned) → this->[0x2c]=cand->pos, return 1;
 * иначе this->[0x34]=dist², return 0. Все разыменования под SEH. */
static int s6_check_distance(void *this_, void *cand)
{
    unsigned char *self = (unsigned char *)this_;
    __try {
        unsigned char tracking = *(unsigned char *)(self + 0x30);
        void *anchor;
        LDObject *obj;
        void *pos;
        unsigned int thr;
        int dist2;

        if (tracking == 0) {
            return 0;                       /* трекинг выключен → cand не трогаем */
        }
        anchor = *(void **)(self + 0x2c);   /* this->[0x2c] — якорь (LDPos*) */
        obj = (LDObject *)cand;
        pos = obj->pos;                     /* cand->[0] */
        if (pos == NULL) {
            return 0;
        }
        dist2 = s6_distance2(pos, anchor);  /* c480(pos, anchor) */
        thr = *(unsigned int *)(self + 0x34);
        if ((unsigned int)dist2 > thr) {
            *(void **)(self + 0x2c) = pos;  /* this->[0x2c] = pos */
            return 1;                       /* предикат сработал → выход code=4 */
        }
        *(unsigned int *)(self + 0x34) = (unsigned int)dist2;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;                           /* битые указатели → не нашли */
    }
}

/* FUN_0045c5a0 — таймаут (thiscall, RET 4, AL; x87; порт 1:1). arg = float-
 * порог (4 Б, передан по значению). ms = timeGetTime() (IAT base+0xec27c);
 * сек = (double)ms*0.001 (коррекция знака 2^32); elapsed = сек − this->[0x4]
 * (float); return elapsed > порог (NaN → 1). Под SEH. */
typedef unsigned long (__stdcall *PFN_timeGetTime)(void);
#define S6_RVA_timeGetTime 0xec27cUL       /* PTR_timeGetTime_004ec27c */

static PFN_timeGetTime g_s6_gettime = NULL; /* кэш указателя timeGetTime (IAT) */

static int s6_check_timeout(void *this_, void *arg)
{
    unsigned char *self = (unsigned char *)this_;
    __try {
        unsigned long ms;
        double sec;
        float start, threshold, elapsed;

        ms = g_s6_gettime ? g_s6_gettime() : (unsigned long)GetTickCount();
        sec = (double)(unsigned long)ms * 0.001;   /* мс→сек (unsigned-коррекция) */
        start = *(float *)(self + 0x04);           /* this->[0x4] — старт (сек) */
        memcpy(&threshold, &arg, sizeof(float));   /* порог (float по значению) */
        elapsed = (float)sec - start;
        if (elapsed != elapsed) {                  /* NaN → timeout */
            return 1;
        }
        return (elapsed > threshold) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

/* FUN_00434bc0 — std::map<uint32,V>::lower_bound(key) (thiscall, RET 8).
 * this = map: [this+0x04] head (sentinel), [this+0x08] size. Возврат: первый
 * узел с value0 >= key (или head = end, если такого нет). Ключ — value0 (+0x0C). */
static TreeNode *s6_map_lower_bound(TreeNode *head, unsigned int key)
{
    TreeNode *cand;
    TreeNode *node;

    if (head == NULL) {
        return NULL;
    }
    cand = head;
    node = head->parent;                 /* root (== head если дерево пусто) */
    while (node != NULL && !node->is_nil) {
        if ((unsigned int)node->value0 >= key) {   /* jnc: value0 >= key (unsigned) */
            cand = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    if (cand != head && key >= (unsigned int)cand->value0) {  /* jnc: key >= value0 */
        return cand;
    }
    return head;                         /* end-итератор */
}

/* ------------------------------------------------------------------ */
/* Порты callees FUN_0045dd70 (Фаза 2/3).                                */
/* PFS-ctor (0x45c2e0), slot-insert (0x45d810), slot-register (0x45dd00)*/
/* path_tree_search (0x45c3a0) и path_search (0x45cb40) — точные порты    */
/* (все разыменования под SEH; «дерево» PFS в нашем harness'е пустое,    */
/* поэтому tree-walk безопасно возвращает входной узел).                 */
/* position-update (0x45c690→0x45c5e0) — no-op (x87/vtable).             */
/* Метрику [obj+0x24] обновляет s6_ai_update (замена pathfinding-фронтира)*/
/* ------------------------------------------------------------------ */

/* FUN_0045c2e0 — PathfinderState constructor (0x58 байт). */
static void *s6_pfs_ctor(void)
{
    unsigned char *pfs = (unsigned char *)malloc(0x58);
    if (pfs == NULL) {
        return NULL;
    }
    memset(pfs, 0, 0x58);
    *(unsigned int *)(pfs + 0x28) = 0xcb18967f;  /* sentinel -1e7 */
    *(unsigned int *)(pfs + 0x2c) = 0x4b18967f;  /* sentinel +1e7 */
    return pfs;
}

/* s6_slot_result — out-пара для s6_slot_insert (аналог pair<TreeNode*,byte>). */
typedef struct {
    TreeNode *node;
    int       inserted;
} s6_slot_result;

/* FUN_0045d810 — std::map<uint32,V>::insert с hint (упрощённый, без rb-rebalance).
 * this = map: [this+0x04]=head(sentinel), [this+0x08]=size.
 * key = value0 (uint32). out: node + inserted-флаг. */
static void s6_slot_insert(void *map, unsigned int key, s6_slot_result *out)
{
    TreeNode *head;
    TreeNode *node;
    TreeNode *candidate;
    TreeNode *new_node;
    int *size_ptr;
    int flag = 1;  /* 1 = insert left, 0 = insert right */

    out->node = NULL;
    out->inserted = 0;

    if (map == NULL) {
        return;
    }
    head = *(TreeNode **)((unsigned char *)map + 0x04);
    if (head == NULL) {
        return;
    }

    /* Спуск от корня (head->parent) к листу. */
    candidate = head;
    node = head->parent;
    while (node != NULL && !node->is_nil) {
        if (key < (unsigned int)node->value0) {
            candidate = node;
            flag = 1;
            node = node->left;
        } else if (key > (unsigned int)node->value0) {
            candidate = node;
            flag = 0;
            node = node->right;
        } else {
            /* Ключ уже существует — вернуть существующий узел. */
            out->node = node;
            out->inserted = 0;
            return;
        }
    }

    /* Создать новый узел (0x47abc0: alloc 0x18 + init). */
    new_node = (TreeNode *)malloc(sizeof(TreeNode));
    if (new_node == NULL) {
        return;
    }
    memset(new_node, 0, sizeof(TreeNode));
    new_node->left   = head;
    new_node->parent = head;
    new_node->right  = head;
    new_node->value0 = (int)key;
    new_node->value1 = NULL;
    new_node->color  = 1;   /* red */
    new_node->is_nil = 0;

    /* Инкремент size. */
    size_ptr = (int *)((unsigned char *)map + 0x08);
    if (size_ptr != NULL) {
        (*size_ptr)++;
    }

    /* Привязка к parent / head. */
    if (candidate == head) {
        /* Пустое дерево — вставить как корень. */
        new_node->parent = head;
        head->parent = new_node;
        head->left   = new_node;
        head->right  = new_node;
    } else if (flag) {
        /* Вставить левым сыном. */
        new_node->parent = candidate;
        candidate->left  = new_node;
        if (head->left == candidate) {
            head->left = new_node;  /* новый минимум */
        }
    } else {
        /* Вставить правым сыном. */
        new_node->parent = candidate;
        candidate->right = new_node;
        if (head->right == candidate) {
            head->right = new_node;  /* новый максимум */
        }
    }

    out->node = new_node;
    out->inserted = 1;
}

/* FUN_0045dd00 — slot lookup caller (запись объекта в 2 map'а).
 * this = BattleAI: this+0x68=map1 (ключ 0), this+0x74=map2 (ключ (y<<16)|x).
 * value1 узла = объект (LDObject*). */
static void s6_slot_register(void *this_, LDObject *obj)
{
    /* ОТКЛЮЧЕНО: s6_slot_insert использует malloc (CRT .pyd) для вставки в родной
     * std::map игры (this+0x68/+0x74) без rb-баланса → порча ку[чи → краш в ntdll.
     * Дефект C из аудита. Нужен Another подход (не вставлять в родные map'ы). */
    (void)this_;
    (void)obj;
}

/* FUN_0456ed0 — grid cell process (доступ к ячейке сетки по индексу).
 * Спускается по цепочке next_level (+0x1c) до листа, затем
 * возвращает leaf->cells + index * 0x34. */
static void *s6_grid_cell(void *level, int index)
{
    unsigned char *lvl = (unsigned char *)level;
    void *next;
    unsigned char *cells;

    if (index < 0 || level == NULL) {
        return NULL;
    }

    /* Спуститься до листового уровня. */
    while (1) {
        next = *(void **)(lvl + 0x1c);
        if (next == NULL) {
            break;
        }
        lvl = (unsigned char *)next;
    }

    cells = *(unsigned char **)(lvl + 0x34);
    if (cells == NULL) {
        return NULL;
    }
    return (void *)(cells + (unsigned int)index * 0x34);
}

/* Левый (первый) узел std::map this+0x6c → LDObject* (fallback для
 * s6_path_tree_search: дерево pathfinding'а ->[0x28] в .pyd невалидно).
 * this_ = BattleAI (голова map в +0x6c, head->left = min). NULL если пусто. */
static LDObject *s6_map_leftmost(void *this_)
{
    unsigned char *self = (unsigned char *)this_;
    __try {
        TreeNode *head = *(TreeNode **)(self + S6_OFF_head);
        TreeNode *cur;
        if (head == NULL) {
            return NULL;
        }
        cur = head->left;                /* MSVC _Tree: head->left = min */
        if (cur == NULL || cur == head) {
            return NULL;
        }
        return (LDObject *)cur->value1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

/* FUN_0045c3a0 — path search по дереву кандидатов (221 байт).
 * this (ecx) = pathfinder/BattleAI; node (param_1) = iVar7 (grid-cell объект,
 *   ->[0x28] корень дерева, ->[0x20] пара, ->[0x24] next, ->[0x14]/[0x18] метрики).
 * arg2 (param_2) = iVar7; arg3 (param_3) = float* внешняя метрика (NULL в 1-м вызове);
 * out4 (param_4) = float* base+field_14; out5 (param_5) = int* min field_18;
 * out6 (param_6) = int* min field_14; param_7 = делитель дистанции.
 * Обходит дерево (FUN_456a70 в leftmost через ->[0x28] → next по ->[0x24]);
 * тип 1 = сохранить кандидата (EDI), тип 0 = обновить min/max метрику. Возврат: узел.
 * Все разыменования под SEH. Упрощения: FUN_449160 (тип) → filter&1; x87 → float.
 * FALLBACK: дерево ->[0x28] в .pyd невалидно (нет реальных grid-cell) → первый
 * (левый) узел std::map this+0x6c, чтобы dd70 получил кандидата и цикл двигался. */
static void *s6_path_tree_search(void *this_, void *node, void *arg2, void *arg3,
                                 float *out4, int *out5, int *out6, int param7)
{
    unsigned char *self = (unsigned char *)this_;
    void *result = NULL;

    __try {
        unsigned char *cur;
        void *best = NULL;
        void *container;
        void *ctx;
        int steps = 0;

        if (node == NULL || out5 == NULL || out6 == NULL) {
            return NULL;
        }
        /* Sentinel ±1e7 (FUN_45c3a0: *param_6=-1e7, *param_5=+1e7). */
        *(int *)out6 = 0xcb18967f;
        *(int *)out5 = 0x4b18967f;

        /* Спуститься до левого листа (FUN_456a70): идём по ->[0x28] (left). */
        cur = (unsigned char *)node;
        container = *(void **)(cur + 0x28);
        if (container != NULL) {
            unsigned char *c = (unsigned char *)container;
            int guard = 0;
            /* FUN_456a70: eax=ecx; do { left=[eax+0x28]; if(!left) break; eax=left; } */
            while (c != NULL && guard++ < 4096) {
                void *left = *(void **)(c + 0x28);
                if (left == NULL || left == c) {
                    break;
                }
                c = (unsigned char *)left;
            }
            cur = c;
        }

        /* ctx = this->[0]->[4] (FUN_449160 / ctx->[0x120]). */
        ctx = NULL;
        {
            void *vt = *(void **)self;
            if (vt != NULL) {
                ctx = *(void **)((unsigned char *)vt + 0x04);
            }
        }

        /* Обход дерева через ->[0x24] (next). */
        while (cur != NULL) {
            void *pair;
            void *obj;
            int type;

            if (++steps > 100000) {          /* защита от повреждённого дерева */
                break;
            }

            pair = *(void **)(cur + 0x20);
            obj = (pair != NULL) ? *(void **)pair : NULL;

            /* Тип узла: FUN_449160 → упрощённо filter&1. */
            type = (obj != NULL)
                ? (*(int *)((unsigned char *)obj + S6_OFF_filter) & 1) : 0;

            if (type == 1) {
                best = cur;                  /* EDI = cur (сохранить кандидата) */
            } else {                         /* type == 0 */
                if (best == NULL) {
                    best = NULL;             /* кандидата нет — нечего сравнивать */
                } else {
                    float bf, cf;
                    memcpy(&bf, (unsigned char *)best + 0x14, sizeof(float));
                    memcpy(&cf, cur + 0x18, sizeof(float));
                    if (!(bf != bf) && !(cf != cf)) {   /* не NaN */
                        int v14, v18;
                        memcpy(&v14, cur + 0x14, sizeof(int));
                        memcpy(&v18, cur + 0x18, sizeof(int));
                        *(int *)out6 = v14;
                        *(int *)out5 = v18;
                    }
                }
            }

            cur = (unsigned char *)(*(void **)(cur + 0x24));  /* ESI = node->next */
        }

        if (best != NULL) {
            /* *out4 = ctx->[0x120] + best->[0x14]. */
            if (ctx != NULL && out4 != NULL) {
                float base, f14;
                memcpy(&base, (unsigned char *)ctx + 0x120, sizeof(float));
                memcpy(&f14, (unsigned char *)best + 0x14, sizeof(float));
                *(float *)out4 = base + f14;
            }
            result = best;

            /* Проверка дистанции: флаг this->vtable[0] & 1. */
            if (arg3 != NULL) {
                void *vt = *(void **)self;
                unsigned char flag = (vt != NULL) ? (*(unsigned char *)vt & 0x1) : 0;
                if (flag && out4 != NULL) {
                    float val4, val3, f, thr;
                    int d = param7;
                    memcpy(&val4, (unsigned char *)out4, sizeof(float));
                    val3 = *(float *)arg3;
                    f = val4 - val3;
                    if (f < 0.0f) {
                        f = -f;              /* FABS */
                    }
                    if (d != 0) {
                        f = f / (float)d;    /* FIDIV */
                    }
                    memcpy(&thr, self + 0x0c, sizeof(float));
                    if (!(f >= thr)) {
                        result = NULL;       /* < порога → сбросить кандидата */
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = NULL;
    }

    /* FALLBACK: дерево pathfinding'а ->[0x28] в .pyd невалидно (node = LDObject*,
     * реальных grid-cell нет) → обход даёт NULL/SEH. Возвращаем левый узел
     * std::map this+0x6c, чтобы dd70 получил кандидата и цикл двигался (не NULL).
     * Срабатывает и когда реальный кандидат найден, но отвергнут проверкой
     * дистанции (arg3 != NULL) — в harness'е этот путь только в grid-цикле. */
    if (result == NULL) {
        result = (void *)s6_map_leftmost(this_);
    }

    (void)arg2;
    return result;
}

/* FUN_0045cb40 — path search (587 байт).
 * this (ecx) = pathfinder/BattleAI: [this+0x00]->ctx (vtable), [this+0x10] Y-радиус,
 *   [this+0x14] X-радиус, [this+0x74] map занятых ячеек (head в [this+0x78]).
 * pfs = PathfinderState*: pfs->[0x00] = iVar7 (объект); центр = iVar7->[0x0c]/[0x10].
 * Первый вызов 0x45c3a0(this, iVar7, iVar7, NULL, &pfs[0x10], &pfs[0x2c], &pfs[0x28], 1).
 * Если ctx->vtable[0] & 1 — двойной цикл (outer=«y» this->[0x14], inner=«x» this->[0x10]):
 *   ключ (outer<<16)|inner, lower_bound в map this+0x74; занятые → обновить min/max;
 *   свободные в границах → grid-узел + рекурсивный 0x45c3a0 (метрика 0x45c390).
 * Финальная проверка порога; возврат узла-цели или 0. Все разыменования под SEH. */
static void *s6_path_search(void *this_, void *pfs)
{
    unsigned char *self = (unsigned char *)this_;
    void *result = NULL;

    __try {
        unsigned char *ivar7;
        void *node;
        int center0c, center10;
        int y_radius, x_radius;
        unsigned char flag;
        void *vt;
        TreeNode *head;

        if (pfs == NULL) {
            ld_debug("s6_path_search: pfs NULL -> return NULL");
            return NULL;
        }
        ivar7 = *(unsigned char **)pfs;      /* iVar7 = *pfs (LDObject*) */
        if (ivar7 == NULL) {
            ld_debug("s6_path_search: ivar7 NULL -> return NULL");
            return NULL;
        }

        /* Центр: координаты из LDObject->pos (+0x00 → LDPos с +0x0c/+0x10). */
        {
            LDObject *obj = (LDObject *)ivar7;
            LDPos *pos = (obj != NULL) ? obj->pos : NULL;
            if (pos == NULL) {
                ld_debug("s6_path_search: pos NULL -> return NULL");
                return NULL;
            }
            center0c  = pos->y;       /* LDPos +0x10 */
            center10  = pos->x;       /* LDPos +0x0c */
        }
        ld_debug("s6_path_search: center=(%d,%d)", center0c, center10);

        /* Радиусы: this->[0x10] (Y), this->[0x14] (X). */
        y_radius = *(int *)(self + 0x10);
        x_radius = *(int *)(self + 0x14);
        ld_debug("s6_path_search: radii y=%d x=%d", y_radius, x_radius);

        /* Sentinel ±1e7 в PFS +0x28/+0x2c; первый 0x45c3a0 (min-метрика в дереве).
         * ВАЖНО: node = ivar7 (LDObject*), tree-search читает obj->[0x28] (container). */
        *(int *)((unsigned char *)pfs + 0x28) = 0xcb18967f;
        *(int *)((unsigned char *)pfs + 0x2c) = 0x4b18967f;
        node = s6_path_tree_search(self, ivar7, ivar7, NULL,
                                   (float *)((unsigned char *)pfs + 0x10),
                                   (int *)((unsigned char *)pfs + 0x2c),
                                   (int *)((unsigned char *)pfs + 0x28), 1);
        ld_debug("s6_path_search: first tree_search -> %p", node);
        if (node == NULL) {
            ld_debug("s6_path_search: first tree_search NULL -> return NULL");
            return NULL;                     /* узел не найден → return 0 */
        }
        result = node;

        /* Флаг «выполнять grid-поиск»: ctx->vtable[0] & 1. */
        vt = *(void **)self;
        flag = (vt != NULL) ? (*(unsigned char *)vt & 0x1) : 0;

        head = *(TreeNode **)(self + 0x78);  /* map2.head (end-итератор) */
        if (flag && head != NULL) {
            int local4 = 0;
            int local34 = 0x4b18967f;        /* best min v2 */
            int local38 = 0xcb18967f;        /* best max v2 */
            int outer, inner;

            /* Внешний цикл (outer = «y», this->[0x14]). */
            for (outer = center10 - x_radius; outer <= center10 + x_radius; outer++) {
                /* Внутренний цикл (inner = «x», this->[0x10]). */
                for (inner = center0c - y_radius; inner <= center0c + y_radius; inner++) {
                    unsigned int key;
                    TreeNode *occ;

                    if (outer == center10 && inner == center0c) {
                        continue;            /* центр пропускаем */
                    }

                    key = ((unsigned int)(unsigned short)outer << 16) |
                          (unsigned int)(unsigned short)inner;
                    occ = s6_map_lower_bound(head, key);
                    if (occ != head) {
                        /* Клетка занята — обновить min/max метрику (no-op в упрощении). */
                        continue;
                    }

                    /* Клетка свободна: границы (inner<0 / outer<0 → return 0). */
                    if (inner < 0 || outer < 0) {
                        return NULL;
                    }
                    {
                        void *ctx = *(void **)self;
                        if (ctx != NULL) {
                            float bound, f;
                            memcpy(&bound, (unsigned char *)ctx + 0x2c, sizeof(float));
                            f = (float)inner;
                            if (!(f < bound)) {  /* inner >= min → вне сетки */
                                return NULL;
                            }
                            memcpy(&bound, (unsigned char *)ctx + 0x30, sizeof(float));
                            f = (float)outer;
                            if (!(f < bound)) {
                                return NULL;
                            }
                        }
                    }

                    /* Узел сетки (0x4570d0 → s6_grid_cell, упрощённо) + рекурсивный
                     * 0x45c3a0 с метрикой 0x45c390 = max(|dx|,|dy|) (Чебышёв). */
                    {
                        int dx = outer - center10;
                        int dy = inner - center0c;
                        int dist;
                        void *r;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;
                        dist = (dx > dy) ? dx : dy;   /* 0x45c390 — max(|dx|,|dy|) */
                        r = s6_path_tree_search(self, ivar7, ivar7, ivar7,
                                                (float *)&local4, &local34, &local38,
                                                dist);
                        if (r == NULL) {
                            return NULL;
                        }
                        result = r;
                    }
                }
            }

            /* Финальная проверка порога (x87, упрощённо: best_min vs this->[0x8]). */
            {
                float best_min, thr;
                memcpy(&best_min, (unsigned char *)pfs + 0x2c, sizeof(float));
                memcpy(&thr, self + 0x08, sizeof(float));
                if (best_min != best_min) {      /* NaN → оставить кандидата */
                    return result;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = NULL;
    }

    return result;
}

/* FUN_0045c690 → FUN_0045c5e0 — position update (упрощённый, no-op).
 * Оригинал: x87 FPU, vtable[0x40/0x48], FUN_456b20, FUN_44bbf0.
 * Для выхода цикла не критичен — пропускаем. */
static void s6_position_update(void *this_, LDObject *obj)
{
    (void)this_;
    (void)obj;
}

/* Обновляет метрики СОСЕДЕЙ кандидата в std::map this+0x6c (замена
 * pathfinding-фронтира FUN_0045cb40/FUN_0045c3a0). Оригинал обходит grid-
 * клетки вокруг кандидата и обновляет [obj+0x24] через PFS; в .pyd дерево
 * ->[0x28] невалидно, поэтому fallback: in-order проход map this+0x6c,
 * обновление метрик объектов рядом с cand (dist² < S6_NEIGHBOR_D2).
 * Сам cand получает штраф (metric = max), чтобы scan вернул другого.
 * Все разыменования под SEH. */
#define S6_NEIGHBOR_D2  400   /* порог dist² для «соседа» (20×20 клеток) */

static void s6_map_update_neighbors(void *this_, LDObject *cand, void *target)
{
    unsigned char *self = (unsigned char *)this_;
    TreeNode *head;
    TreeNode *cur;
    LDObject *cand_obj = cand;
    int steps = 0;

    if (cand_obj == NULL || cand_obj->pos == NULL) {
        return;
    }

    __try {
        head = *(TreeNode **)(self + S6_OFF_head);
        if (head == NULL) {
            return;
        }

        cur = head->left;                /* первый (левый) узел */
        while (cur != head && cur != NULL) {
            LDObject *obj;

            if (++steps > 10000) {
                break;
            }

            obj = (LDObject *)cur->value1;
            if (obj != NULL && obj != cand_obj && obj->pos != NULL) {
                /* Только объекты с тем же фильтром, что и cand. */
                if (obj->filter == cand_obj->filter) {
                    int dx, dy, d2;
                    int obj_d2;

                    /* Проверка «соседства»: dist²(cand, obj) < порог. */
                    dx = obj->pos->x - cand_obj->pos->x;
                    dy = obj->pos->y - cand_obj->pos->y;
                    d2 = dx * dx + dy * dy;

                    if (d2 < S6_NEIGHBOR_D2) {
                        /* Сосед: обновить метрику = dist² до цели + визиты. */
                        if (target != NULL) {
                            obj_d2 = s6_distance2(obj->pos, target);
                        } else {
                            obj_d2 = d2;
                        }
                        obj->f18++;
                        {
                            unsigned int m = (unsigned int)obj_d2 +
                                             (unsigned int)obj->f18;
                            if (m >= 999999999u) {
                                m = 0x10000000u + (m % 0x20000000u);
                            }
                            obj->metric = (int)m;
                        }
                    }
                }
            }
            cur = s6_tree_next(cur);
        }

        /* Штраф кандидату УБРАН: 0x7ffffff0 убивал всех кандидатов → scan NULL → краш.
         * Вместо этого кандидат получает метрику = dist² до цели (как соседи). */
        cand_obj->f18++;
        {
            unsigned int d2_self = 0;
            LDPos *tgt = (LDPos *)target;
            if (cand_obj->pos != NULL && tgt != NULL) {
                d2_self = (unsigned int)s6_distance2(cand_obj->pos, tgt);
            }
            cand_obj->metric = (int)d2_self;
        }
        (void)InterlockedIncrement(&g_s6_visit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* FUN_0045dd70 — AI/pathfinding update (~807 байт asm). this = BattleAI (ecx),
 * cand = LDObject* (arg1), arg2 = target pos_obj (в e0a0 это arg1/cmp_val).
 * Оригинал: PFS-ctor (0x45c2e0), slot-register (0x45dd00), path_search (0x45cb40)
 * → 0x45c3a0 (обновление метрик [obj+0x24] СОСЕДЕЙ через pathfinding-фронтир),
 * position-update (0x45c690→0x45c5e0, no-op).
 * Порт: slot-register + path_search (точные, SEH). Метрики СОСЕДЕЙ обновляет
 * s6_map_update_neighbors (fallback: дерево ->[0x28] в .pyd невалидно → обход
 * std::map this+0x6c). Сам cand получает штраф (metric = max) → scan возвращает
 * другого кандидата → цикл s6_target_search продвигается.
 * Все разыменования под SEH. */
static int s6_ai_update(void *this_, void *cand, int arg2)
{
    unsigned char *self = (unsigned char *)this_;
    LDObject *obj = (LDObject *)cand;

    if (obj == NULL) {
        return 0;
    }

    __try {
        void *pfs;

        /* (1) Slot-register (FUN_0045dd00): объект в map1 (ключ 0) и
         * map2 (ключ (pos->y<<16)|pos->x). Регистрирует занятые ячейки. */
        s6_slot_register(self, obj);

        /* (2) Path search (FUN_0045cb40 → 0x45c3a0): PFS-ctor + grid-обход с
         * проверкой занятости map2 (this+0x74). Центр берётся из *pfs (объекта).
         * ВАЖНО: pfs->[0] = obj (LDObject*), координаты читать из obj->pos. */
        pfs = s6_pfs_ctor();
        if (pfs != NULL) {
            *(void **)pfs = obj;                         /* pfs->[0x00] = LDObject* */
            s6_path_search(self, pfs);
            free(pfs);
        }

        /* (3) Position update (FUN_0045c690 → FUN_0045c5e0): no-op (x87/vtable). */
        s6_position_update(self, obj);

        /* (4) Метрика [obj+0x24] — замена pathfinding-фронтира (0x45cb40/0x45c3a0
         * обновляли метрики СОСЕДЕЙ через PFS). В .pyd дерево ->[0x28] невалидно →
         * fallback: s6_map_update_neighbors обходит std::map this+0x6c и обновляет
         * метрики соседей cand; сам cand получает штраф (metric = max), чтобы scan
         * вернул другого кандидата → цикл s6_target_search продвигается. */
        s6_map_update_neighbors(self, obj, (void *)arg2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    (void)arg2;
    return 0;
}

/* Замена FUN_0045e0a0 (cdecl; вызывается из трамплина). */
static void *__cdecl s6_target_search(void *this_, void *arg0, void *arg1,
                                      void *arg2, void *arg3)
{
    int filter = (int)(int)arg0;
    void *cmp_val = arg1;                /* target pos_obj (сравнение с cur->pos) */
    void *timeout_arg = arg2;
    int *out_code = (int *)arg3;
    int code = 0;
    LDObject *cur;
    LONG n = InterlockedIncrement(&g_site6_calls);
    LONG iters = 0;

    if (out_code != NULL) {
        *out_code = 0;
    }

    /* Защита от рекурсии: FUN_0045dd70 (AI/pathfinding) внутри вызывает
     * FUN_0045e0a0, который перехвачен site6. Без защиты — stack overflow.
     * При рекурсивном вызове возвращаем NULL (цель не найдена на этом уровне),
     * внешний цикл продолжит работу. */
    if (InterlockedIncrement(&g_site6_depth) > 1) {
        if (ld_rate(&g_site6_dbg, 4, 64)) {
            ld_debug("site6: recursive call (depth=%ld) -> return NULL",
                     (long)g_site6_depth);
        }
        InterlockedDecrement(&g_site6_depth);
        return NULL;
    }

    if (n == 1) {
        ld_info("site6: first call (hook FIRED) this=%p filter=%d cmp=%p "
                "layout_ld=%d layout_node=%d",
                this_, filter, cmp_val, (int)ld_off_ok, (int)ld_off_ok2);
    }

    cur = s6_scan_map(this_, filter);
    if (ld_rate(&g_site6_dbg, 4, 64)) {
        ld_debug("site6: call %ld scan -> %p (filter=%d cmp=%p depth=%ld)",
                 (long)n, (void *)cur, filter, cmp_val, (long)g_site6_depth);
    }
    while (cur != NULL) {
        iters++;
        /* Cap — защитный fallback (в норме цикл выходит через c880/c5a0/cmp/
         * rescan-NULL, как оригинал). При cap возвращаем ТЕКУЩИЙ cur (валидный
         * кандидат), а не NULL: оригинальный FUN_0045e0a0 на timeout-выходе
         * возвращает cur (0x45e1b5/0x45e183); NULL — только когда скан пуст. */
        if (iters > S6_CAP) {
            if (ld_rate(&g_site6_dbg, 4, 64)) {
                ld_debug("site6: call %ld CAP %d hit -> exit cur=%p (code=3)",
                         (long)n, S6_CAP, (void *)cur);
            }
            code = 3;          /* как timeout-выход оригинала */
            break;             /* cur валидный (НЕ NULL) */
        }
        /* AI/pathfinding update — порт FUN_0045dd70 (Фаза 2): двойной цикл по
         * сетке 3×3 + lower_bound map this+0x74, поднимает метрику кандидата
         * [obj+0x24], чтобы s6_scan_map вернул другой объект. Оригинальный
         * dd70 зовётся с arg2 = arg1 (target pos_obj), см. 0x45e117 push ebx. */
        if (ld_rate(&g_site6_dbg, 4, 64)) {
            ld_debug("site6: call %ld iter=%ld cur=%p metric=%d pos=%p",
                     (long)n, (long)iters, (void *)cur, cur->metric, cur->pos);
        }
        s6_ai_update(this_, cur, (int)(int)cmp_val);  /* AI/pathfinding (порт dd70) */
        if (ld_rate(&g_site6_dbg, 4, 64)) {
            ld_debug("site6: call %ld iter=%ld -> ai_update done, metric now=%d pos=%p",
                     (long)n, (long)iters, cur->metric, (void *)cur->pos);
        }
        /* FUN_0045c4b0(&local, cur, 4) — копирование в мёртвую локальную
         * структуру (не читается); поведение эквивалентно no-op. */
        if (cur->pos == (LDPos *)cmp_val) {
            if (ld_rate(&g_site6_dbg, 4, 64)) {
                ld_debug("site6: call %ld iter=%ld -> pos==cmp -> code=1",
                         (long)n, (long)iters);
            }
            code = 1;
            break;
        }
        if (s6_check_distance(this_, cur)) {
            if (ld_rate(&g_site6_dbg, 4, 64)) {
                ld_debug("site6: call %ld iter=%ld -> c880 dist -> code=4",
                         (long)n, (long)iters);
            }
            code = 4;
            break;
        }
        if (s6_check_timeout(this_, timeout_arg)) {
            if (ld_rate(&g_site6_dbg, 4, 64)) {
                ld_debug("site6: call %ld iter=%ld -> c5a0 timeout -> code=3",
                         (long)n, (long)iters);
            }
            code = 3;
            break;
        }
        cur = s6_scan_map(this_, filter);
        if (ld_rate(&g_site6_dbg, 4, 64)) {
            ld_debug("site6: call %ld iter=%ld rescan -> %p",
                     (long)n, (long)iters, (void *)cur);
        }
    }

    if (out_code != NULL) {
        *out_code = code;
    }

    InterlockedDecrement(&g_site6_depth);

    /* Статистика. */
    g_site6_iter = iters;
    if (iters > g_site6_max) {
        g_site6_max = iters;
    }
    if (iters > S6_HEAVY_ITER) {
        InterlockedIncrement(&g_site6_heavy);
    }
    if (ld_rate(&g_site6_dbg, 4, 64)) {
        ld_debug("site6: call %ld iters=%ld max=%ld heavy=%ld -> %p code=%d",
                 (long)n, (long)iters, (long)g_site6_max,
                 (long)g_site6_heavy, (void *)cur, code);
    }
    return (void *)cur;
}

/* Трамплин: вход — E9 из пролога FUN_0045e0a0 (7 Б), ecx=this,
 * [esp]=ret, [esp+4..0x10]=arg0..arg3. Полная замена: оригинальный код
 * не исполняется. Результат в eax, калlee (трамплин) чистит 0x10 (RET 0x10). */
static __declspec(naked) void s6_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push dword ptr [esp + 28]    /* arg3 */
        push dword ptr [esp + 28]    /* arg2 */
        push dword ptr [esp + 28]    /* arg1 */
        push dword ptr [esp + 28]    /* arg0 */
        push ecx                     /* this */
        call s6_target_search
        add esp, 20                  /* cdecl: снять 5 аргументов */
        pop ecx
        pop edx
        add esp, 4                   /* пропустить сохранённый eax (результат уже в eax) */
        ret 0x10                     /* оригинальный хвост: thiscall, 4 арг */
    }
}

static void install_site6(void)
{
    unsigned char *entry_site;
    unsigned char *tail_site;
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
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
     * дерево ->[0x28] в .pyd невалидно → fallback на std::map this+0x6c (не NULL).
     * Полный detour FUN_0045e0a0. Callees (dd70/c880/c5a0/ce80) портированы на C
     * (s6_ai_update/s6_check_distance/s6_check_timeout/s6_scan_map) — оригиналы
     * НЕ вызываются (крашатся из .pyd). */
    entry_site = find_in_text((const unsigned char *)S6_ENTRY_SIG, S6_ENTRY_LEN);
    tail_site = find_in_text((const unsigned char *)S6_TAIL_SIG, S6_TAIL_LEN);
    if (entry_site == NULL || tail_site == NULL) {
        ld_info("install_site6: signature NOT found (entry=%p tail=%p)",
                (void *)entry_site, (void *)tail_site);
        return;
    }

    ld_info("install_site6: entry=0x%08x tail=0x%08x (callees ported to C)",
            (unsigned)entry_site, (unsigned)tail_site);

    /* Кэш timeGetTime из IAT EXE (base+0xec27c) для s6_check_timeout. EXE без
     * ASLR (imagebase 0x400000) → base+RVA валиден как абсолютный адрес. */
    g_s6_gettime = (base != NULL)
        ? (PFN_timeGetTime)*(void **)(base + S6_RVA_timeGetTime) : NULL;

    if (!s4_rel_fits(entry_site, (void *)s6_entry_hook)) {
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

static PyObject *ld_py_stats(PyObject *self, PyObject *args)
{
    (void)self;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    return Py_BuildValue("(llll)", (long)g_site1_calls, (long)g_site2_calls,
                         (long)g_site3_calls, (long)g_log_level);
}

static PyObject *ld_py_set_cap(PyObject *self, PyObject *args)
{
    int cap;
    long old;
    (void)self;
    if (!PyArg_ParseTuple(args, "i:set_cap", &cap)) {
        return NULL;
    }
    if (cap < 0) {
        cap = 0;
    }
    old = (long)g_site4_cap;
    g_site4_cap = (LONG)cap;
    ld_info("set_cap: %ld -> %d (iterations per FUN_0045e0a0 call)",
            old, cap);
    return PyInt_FromLong(old);
}

static PyObject *ld_py_diag_stats(PyObject *self, PyObject *args)
{
    (void)self;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    return Py_BuildValue("(lllll)", (long)g_site5_calls,
                         (long)g_site5_heavy, (long)g_site5_max,
                         (long)g_site5_iter, (long)g_log_level);
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

static PyObject *ld_py_cap_stats(PyObject *self, PyObject *args)
{
    (void)self;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    return Py_BuildValue("(lllll)", (long)g_site4_calls, (long)g_site4_caps,
                         (long)g_site4_heavy, (long)g_site4_max,
                         (long)g_site4_cap);
}

static PyMethodDef ldhook_methods[] = {
    {"log", ld_py_log, METH_VARARGS,
     "log(msg): write an info line to hdlog.txt (no-op at level 0)"},
    {"set_level", ld_py_set_level, METH_VARARGS,
     "set_level(n): 0=off, 1=info (default), 2=debug; returns previous level"},
    {"stats", ld_py_stats, METH_VARARGS,
     "stats(): (site1_calls, site2_calls, site3_calls, log_level)"},
    {"set_cap", ld_py_set_cap, METH_VARARGS,
     "set_cap(n): battle target-search iteration cap (0=off); returns previous"},
    {"cap_stats", ld_py_cap_stats, METH_VARARGS,
     "cap_stats(): (calls, caps, heavy, max_iter, cap)"},
    {"diag_stats", ld_py_diag_stats, METH_VARARGS,
     "diag_stats(): (calls, heavy, max_iter, cur_iter, log_level)"},
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
    base = (unsigned char *)GetModuleHandleA(NULL);
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
    }
    Py_InitModule("ldhook", ldhook_methods);
}