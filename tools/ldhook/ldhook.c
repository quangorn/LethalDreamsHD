// DEPRECATED: порт в ldhook.cpp (C++), см. tools/ldhook/README.md
/* ldhook.pyd — Фазы 1–3, 5–6 (PLAN_proxy_dll.md, §4): хуки в памяти + логгер.
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
    {NULL, NULL, 0, NULL}
};

/* DL_EXPORT в 2.2.3 не экспортирует (USE_DL_EXPORT не определён для MSVC в
 * PC/pyconfig.h) — объявляем экспорт явно. methods не может быть NULL:
 * Py_InitModule4 в 2.2.3 разыменовывает methods без проверки. */
__declspec(dllexport) void initldhook(void)
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
        install_site5();
    }
    Py_InitModule("ldhook", ldhook_methods);
}