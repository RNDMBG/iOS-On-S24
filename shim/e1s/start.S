/* SPDX-License-Identifier: GPL-2.0-only */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start
	.long	0
	.quad	0x80000
	.quad	_end
	.quad	0xa
	.quad	0
	.quad	0
	.quad	0
	.ascii	"ARM\x64"
	.long	0

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
	mrs	x2, CurrentEL
	cmp	x2, #8
	b.lo	1f
	msr	vbar_el2, x1
1:	msr	vbar_el1, x1
	isb
	adr	x1, _bss_start
	adr	x2, _bss_end
2:	cmp	x1, x2
	b.hs	3f
	str	xzr, [x1], #8
	b	2b
3:	mov	x0, x19
	bl	probe_main

	.globl halt
halt:
	wfe
	b	halt

.macro vent slot
	.balign 0x80
	mov	x0, #\slot
	b	exc_common
.endm

	.balign 0x800
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	adr	x1, _exc_stack_top
	mov	sp, x1
	bl	probe_exception
	b	halt

	.section .data, "aw"
	.balign 16
	.space 0x10000
_stack_top:
	.space 0x4000
_exc_stack_top:
