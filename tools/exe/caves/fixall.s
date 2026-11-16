	.intel_syntax noprefix
	.text
# ---------------------------------------------------------------------------
# единый блоб (zero-run VA 0x4ebf12, .text) для exepatch12:
#   cave1 (сайт 0x41ce31): 4x-листы шрифта + масштаб advance/pitch; k=1 identity.
#   cave2 (сайт 0x40b494): нормировка шрифта сразу при SetFont.
#   cave3 (сайт 0x47dd80, RenderWorld FUN_0047dd30): перед сеттером матриц
#     FUN_0046f970 пишется source-матрице matrix[0] = matrix[5] * h / w.
#     Гuard 4*h == 3*w (k=1, 4:3) -> запись пропускается ПОБИТОВО: движок
#     хранит matrix[0] = fl32(0.75*cot80), а fl32(0.75*fl32(cot)) даёт ULP+1
#     (1.2990400791 vs 1.2990399599) -> без guard регрессия 4:3 не побитова.
#     Live-формула подтверждена (x_ld_proj.py): ratio кольца 1.4035 -> 1.0575.
# ---------------------------------------------------------------------------
	.globl cave
cave:
	mov	byte ptr [ecx+0x58], 1
	mov	eax, 0x51cc7c
	mov	eax, [eax]
	fild	dword ptr [eax+0x1c]
	fmul	qword ptr [c1024]
	fild	dword ptr [eax+0x18]
	fmul	qword ptr [c768]
	fdivp	st(1), st(0)
	fld1
	fcomip	st(0), st(1)
	jnc	keep
	fstp	st(0)
	fld1
keep:
	# k = 1.0 (4:3) -> identity; k < 1 (16:9) -> k_eff = k * 0.25 (4x-листы).
	fld1
	fcomip	st(0), st(1)
	je	isone
	fmul	dword ptr [csheet]
	fild	dword ptr [ecx+0x40]
	fmul	st(0), st(1)
	fistp	dword ptr [ecx+0x40]
	sub	esp, 8
	lea	eax, [ecx+0x145c]
	mov	[esp], eax
	mov	dword ptr [esp+4], 0
	lea	edx, [ecx+0x6c]
loop1:
	fild	dword ptr [edx]
	fsub	dword ptr [ccorr]
	fmul	st(0), st(1)
	fadd	dword ptr [esp+4]
	fist	dword ptr [edx]
	fild	dword ptr [edx]
	fsubp	st(1), st(0)
	fstp	dword ptr [esp+4]
	add	edx, 0x14
	cmp	edx, [esp]
	jb	loop1
	add	esp, 8
	fstp	st(0)
	jmp	done
isone:
	fstp	st(0)
done:
	pop	ecx
	ret

# ---------------------------------------------------------------------------
# cave2: сеттер шрифта билборда (сайт 0x40b494)
# ---------------------------------------------------------------------------
	.globl cave2
cave2:
	mov	dword ptr [ecx+0x294], eax
	inc	dword ptr [eax+0x24]
	cmp	byte ptr [eax+0x58], 0
	jne	skip
	mov	edx, [eax+0x44]
	test	edx, edx
	jz	skip
	push	ecx
	push	edx
	push	eax
	mov	ecx, eax
	mov	eax, 0x41cc90
	call	eax
	pop	eax
	pop	edx
	pop	ecx
skip:
	ret	8

# ---------------------------------------------------------------------------
# cave3: правка x-коэффициента world-проекции перед сеттером.
#   Вход: ecx = ctx, edx = матрица (camobj+0xf8), [esp]=idx(2), [esp+4]=matrix;
#   сеттер ret 8. eax/edx/edi — scratch (caller после 0x47dd85 их не читает,
#   edi POP в эпилоге RenderWorld). ebx callee сохранят.
#   Guard 4h==3w (k=1) -> ничего не пишем (побитовая identity).
# ---------------------------------------------------------------------------
	.globl cave3
cave3:
	mov	eax, edx              # матрица
	mov	edx, [ecx+0x1c]       # h
	lea	edi, [edx+edx]        # 2h
	add	edi, edi              # 4h
	mov	edx, [ecx+0x18]       # w
	add	edx, edx              # 2w
	add	edx, [ecx+0x18]       # 3w
	cmp	edi, edx
	je	skip3
	fild	dword ptr [ecx+0x1c]
	fild	dword ptr [ecx+0x18]
	fdivp	st(1), st(0)
	fmul	dword ptr [eax+0x14]
	fstp	dword ptr [eax]
skip3:
	.globl c3call
c3call:
	call	0x46f970
	.globl c3jmp
c3jmp:
	jmp	0x47dd85

c1024:	.double	1024.0
c768:	.double	768.0
csheet:	.float	0.25
ccorr:	.float	1.3384
