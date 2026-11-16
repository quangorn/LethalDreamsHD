	.intel_syntax noprefix
	.text
	.globl cave
# ---------------------------------------------------------------------------
# cave1 (4x-листы): хвост FUN_0041cc90, сайт 0x41ce31.
#   * ставит флаг нормировки UV;
#   * k = h*1024/(w*768) из [0x51cc7c]+0x18/+0x1c, k>1 зажимается в 1.0;
#   * k_eff = k * SHEET (лист апскейлен в 4x -> 0.25), поэтому при 4:3 (k=1)
#     правка остаётся identity и работает с РОДНЫМИ листами;
#   * pitch *= k_eff;
#   * advance: v = (a_sheet - CORR) * k_eff + carry, CORR = 4*A, A = ошибка
#     межбуквенного интервала в канвасных единицах (RowS=-3 -> A=0.3346);
#     CORR = 0 -> чисто масштаб листа, без компенсации ширины.
# ---------------------------------------------------------------------------
cave:
	mov	byte ptr [ecx+0x58], 1
	mov	eax, 0x51cc7c
	mov	eax, [eax]
	test	eax, eax
	jz	done
	mov	edx, [eax+0x18]
	test	edx, edx
	jz	done
	mov	edx, [eax+0x1c]
	test	edx, edx
	jz	done
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
	# k = 1.0 (4:3 и все k=1 режимы) -> identity: листы родные, ничего не масштабируем.
	# k < 1 (16:9) -> листы 4x-сайдкары, поэтому k_eff = k * 0.25.
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
	.align	16
# ---------------------------------------------------------------------------
# cave2: сеттер шрифта билборда (сайт 0x40b494) - нормирует/масштабирует шрифт
# сразу при SetFont, до SetText (иначе переносы считаются по несжатой таблице).
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
	.align	8
c1024:	.double	1024.0
c768:	.double	768.0
csheet:	.float	0.25
ccorr:	.float	1.3384
