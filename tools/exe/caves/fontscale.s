	.intel_syntax noprefix
	.text
	.globl cave
# ---------------------------------------------------------------------------
# cave1: хвост FUN_0041cc90 (ленивая нормировка UV шрифта) — сайт 0x41ce31.
# Ставит флаг, считает k = h*1024/(w*768), масштабирует pitch и все 256 advance'ов.
# Advance масштабируется с накоплением ошибки округления (carry), иначе сумма
# advance'ов в строке выходит на 1-2% шире k-масштаба: текст налезает на круг,
# а в списках с фиксированной шириной ломается перенос строк.
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
done:
	pop	ecx
	ret
	.align	16
# ---------------------------------------------------------------------------
# cave2: сеттер шрифта билборда (сайт 0x40b494, 6 байт `mov [ecx+0x294],eax`) —
# зовётся из SetFont ДО SetText. Нормирует/масштабирует шрифт сразу, а не на
# первой отрисовке: иначе перенос строк считается по несжатой таблице
# advance'ов против уже k-сжатого бокса -> лишние переносы (диалог настроек).
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
	cmp	dword ptr [edx+0x40], 0
	je	skip
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
