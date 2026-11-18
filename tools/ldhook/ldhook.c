/* ldhook.pyd — Фазы 1–2 (PLAN_proxy_dll.md, §4): хуки в памяти.
 *
 * Расширение Python 2.2.3. initldhook() (вызывается движком через
 * imp.load_dynamic из патчимого ui/ui.pyc):
 *   1. пишет файл-маркер (диагностика);
 *   2. install_site1/2/3() — находит три сайта сканом сигнатур в .text EXE и
 *      ставит E9 rel32 на соответствующие трамплины (в памяти, не на диске).
 *
 *   site1 (0x41ce31, хвост FUN_0041cc90): масштаб листов/advance 4x-шрифтов,
 *          guard k==1 -> identity (порт cave1);
 *   site2 (0x40b494, сеттер шрифта): после сохранённых байт зовёт
 *          FUN_0041cc90(шрифт) — нормировка шрифта сразу при SetFont (cave2);
 *   site3 (0x47dd80, RenderWorld): matrix[0] = matrix[5]*h/w перед сеттером
 *          FUN_0046f970; guard 4*h == 3*w -> запись пропускается, 4:3
 *          остаётся побитово прежним (cave3).
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
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Диагностика                                                         */
/* ------------------------------------------------------------------ */

static void ldlog(const char *msg)
{
    OutputDebugStringA("ldhook: ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    /* Дублируем в файл рядом с EXE — для оффлайн-проверки (каталог игры). */
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (n != 0 && n < MAX_PATH) {
            char *p = strrchr(path, '\\');
            HANDLE h;
            DWORD w = 0;
            if (p != NULL) p[1] = '\0'; else path[0] = '\0';
            strncat(path, "ldhook_log.txt", sizeof(path) - strlen(path) - 1);
            h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                SetFilePointer(h, 0, NULL, FILE_END);
                WriteFile(h, "ldhook: ", 8, &w, NULL);
                WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
                WriteFile(h, "\r\n", 2, &w, NULL);
                CloseHandle(h);
            }
        }
    }
}

static void ldlogf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf) - 1] = '\0';
    va_end(ap);
    ldlog(buf);
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
            const char *msg = "ldhook.pyd loaded (phase 2: site1+site2+site3 hooks)\r\n";
            WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
            CloseHandle(h);
            ldlog("marker written");
        } else {
            ldlog("marker FAILED");
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
static volatile long g_site1_calls = 0;

static void __cdecl site1_impl(void *this_)
{
    unsigned char *self = (unsigned char *)this_;
    unsigned char *ctx;
    double k, keff, carry;
    int *p;

    if (InterlockedIncrement(&g_site1_calls) == 1) {
        ldlog("site1_impl: first call (hook FIRED)");
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
        if (g_site1_calls == 1) {
            char msg[128];
            sprintf(msg, "site1_impl: k==1.0 -> identity (4:3), w=%d h=%d",
                    *(int *)(ctx + 0x18), *(int *)(ctx + 0x1c));
            ldlog(msg);
        }
        return;                         /* identity: 4:3 / k==1 */
    }

    keff = k * SHEET_SCALE;             /* 4x-листы -> 0.25 */
    if (g_site1_calls == 1) {
        char msg[128];
        sprintf(msg, "site1_impl: k=%f -> scale path, keff=%f", k, keff);
        ldlog(msg);
    }

    /* pitch *= k_eff (FISTP — к ближайшему чётному). */
    p = (int *)(self + FONT_PITCH);
    *p = (int)nearbyint((double)(*p) * keff);

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
static volatile long g_site2_calls = 0;

/* Выполняет сохранённые байты site2 и решает, нужна ли нормировка.
 * Возвращает 1, если трамплин должен позвать g_site2_norm(font) (thiscall;
 * сам вызов делается в __asm — в C-режиме MSVC нет __thiscall). */
static __declspec(noinline) int __cdecl site2_impl(void *this_, void *font_)
{
    unsigned char *self = (unsigned char *)this_;
    unsigned char *font = (unsigned char *)font_;

    if (InterlockedIncrement(&g_site2_calls) == 1) {
        ldlogf("site2_impl: first call (hook FIRED) font=%p byte58=%d sheet=%p",
               (void *)font, (int)font[FONT_NORMALIZED],
               *(void **)(font + FONT_SHEET));
    }

    /* Сохранённые байты оригинала. */
    *(unsigned int *)(self + SITE2_OWNER) = (unsigned int)font;
    ++*(unsigned int *)(font + FONT_REFCOUNT);

    /* Cave2: нормировать шрифт сразу, до первой раскладки/SetText. */
    return (font[FONT_NORMALIZED] == 0 && *(void **)(font + FONT_SHEET) != NULL);
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
static volatile long g_site3_calls = 0;

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

    if (4 * h == 3 * w) {
        if (InterlockedIncrement(&g_site3_calls) == 1) {
            ldlogf("site3_impl: first call (hook FIRED) guard 4h==3w -> identity (w=%d h=%d)",
                   w, h);
        }
        return;                     /* identity: 4:3 / k==1 */
    }

    {
        float old = *(float *)(matrix + MATRIX_X);
        float m5 = *(float *)(matrix + MATRIX_X_SCALE);
        int first = (InterlockedIncrement(&g_site3_calls) == 1);
        float res;

        site3_fp(ctx, matrix);      /* matrix[0] = matrix[5]*h/w (x87, как cave3) */
        res = *(float *)(matrix + MATRIX_X);

        if (first) {
            ldlogf("site3_impl: first call (hook FIRED) w=%d h=%d matrix[0] %g (0x%08X) -> %g (0x%08X) m5=%g (0x%08X)",
                   w, h, old, *(unsigned int *)&old,
                   res, *(unsigned int *)&res,
                   m5, *(unsigned int *)&m5);
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

static unsigned char *find_in_text(unsigned char *base,
                                   const unsigned char *sig, size_t siglen)
{
    /* .text: RVA 0x1000, VirtualSize 0xeaf12 (base 0x400000). */
    return find_sig(base + 0x1000, 0xeaf12, sig, siglen);
}

static void install_site1(unsigned char *base)
{
    unsigned char *site;

    site = find_in_text(base, (const unsigned char *)SITE1_SIG, SITE1_SIG_LEN);
    if (site == NULL) {
        ldlog("install_site1: site1 signature NOT found (baseline EXE?)");
        return;
    }
    if (!write_jmp(site, (void *)site1_hook, SITE1_SIG_LEN)) {
        ldlog("install_site1: write_jmp FAILED");
        return;
    }
    ldlogf("install_site1: site1 found at 0x%08x, E9 rel32", (unsigned)site);
}

static void install_site2(unsigned char *base)
{
    unsigned char *site;

    site = find_in_text(base, (const unsigned char *)SITE2_SIG, SITE2_SIG_LEN);
    if (site == NULL) {
        ldlog("install_site2: site2 signature NOT found (baseline EXE?)");
        return;
    }
    g_site2_norm = (void *)(base + 0x1cc90);    /* FUN_0041cc90 */
    if (!write_jmp(site, (void *)site2_hook, SITE2_PATCH)) {
        ldlog("install_site2: write_jmp FAILED");
        return;
    }
    ldlogf("install_site2: site2 found at 0x%08x, E9 rel32+nop -> hook",
           (unsigned)site);
}

static void install_site3(unsigned char *base)
{
    unsigned char *site;
    int rel;

    site = find_in_text(base, (const unsigned char *)SITE3_SIG, SITE3_SIG_LEN);
    if (site == NULL) {
        ldlog("install_site3: site3 signature NOT found (baseline EXE?)");
        return;
    }
    /* Оригинал: call rel32 -> FUN_0046f970; продолжение RenderWorld = site+5. */
    rel = *(int *)(site + 1);
    g_site3_setter = (void *)(site + 5 + rel);
    g_site3_cont = (void *)(site + 5);
    if (!write_jmp(site, (void *)site3_hook, SITE3_SIG_LEN)) {
        ldlog("install_site3: write_jmp FAILED");
        return;
    }
    ldlogf("install_site3: site3 found at 0x%08x, call target 0x%08x, cont 0x%08x",
           (unsigned)site, (unsigned)g_site3_setter, (unsigned)g_site3_cont);
}

static PyMethodDef ldhook_methods[] = {
    {NULL, NULL, 0, NULL}
};

/* DL_EXPORT в 2.2.3 не экспортирует (USE_DL_EXPORT не определён для MSVC в
 * PC/pyconfig.h) — объявляем экспорт явно. methods не может быть NULL:
 * Py_InitModule4 в 2.2.3 разыменовывает methods без проверки. */
__declspec(dllexport) void initldhook(void)
{
    unsigned char *base;

    ldhook_marker();
    base = (unsigned char *)GetModuleHandleA(NULL);
    if (base == NULL) {
        ldlog("initldhook: no EXE base");
    } else {
        install_site1(base);
        install_site2(base);
        install_site3(base);
    }
    Py_InitModule("ldhook", ldhook_methods);
}