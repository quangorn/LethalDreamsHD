/* sites_hd.cpp — сайты хуков HD-масштабирования (PLAN_proxy_dll.md, §2A):
 * site1 (0x41ce31), site2 (0x40b494), site3 (0x47dd80) — трамплины, impl'ы
 * и x87-хелпер site3_fp. Логика 1:1 с tools/exe/caves/fixall.s; guard'ы
 * (k==1 / 4h==3w) сохранены: 4:3 остаётся побитово прежним.
 * Установка хуков (install_site1/2/3) — в ldhook.cpp.
 */
#include <windows.h>
#include <math.h>

#include "ldhook_internal.h"

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

#define ASPECT_CTX     ((volatile unsigned char *)0x51cc7c)  /* VA контекста аспекта */
#define GLYPH_ADV      0x6c     /* advance в записи глифа (20 байт) */
#define GLYPH_STRIDE   0x14
#define GLYPH_TABLE_END 0x145c  /* конец таблицы глифов (this+0x145c) */
#define FONT_PITCH     0x40     /* pitch (int) */
#define SHEET_SCALE    0.25     /* лист апскейлен в 4x */
#define ADV_CORR       1.3384   /* 4*A */

/* Тело cave1. Возвращает управление в исходный caller (pop ecx; ret делает трамплин). */
volatile LONG g_site1_calls = 0;
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
__declspec(naked) void site1_hook(void)
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

#define SITE2_OWNER     0x294
#define FONT_REFCOUNT   0x24
#define FONT_NORMALIZED 0x58
#define FONT_SHEET      0x44

void *g_site2_norm;          /* FUN_0041cc90 (нормировка шрифта) */
volatile LONG g_site2_calls = 0;
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
__declspec(naked) void site2_hook(void)
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

#define MATRIX_X        0x00
#define MATRIX_X_SCALE  0x14                        /* matrix[5] */

void *g_site3_setter;         /* FUN_0046f970 */
void *g_site3_cont;           /* 0x47dd85: продолжение RenderWorld */
volatile LONG g_site3_calls = 0;
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
__declspec(naked) void site3_hook(void)
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
