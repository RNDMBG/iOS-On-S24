/* SPDX-License-Identifier: GPL-2.0-only */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start
	.long	0
	.quad	0			/* text_offset: 2 MiB aligned load */
	.quad	_end			/* patched to the memory footprint */
	.quad	0xa			/* LE, 4K pages, place anywhere */
	.quad	0
	.quad	0
	.quad	0
	.ascii	"ARM\x64"
	.long	0

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0			/* S-Boot DTB */
	adr	x20, _head		/* load base */

	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:
	adr	x1, vectors
	msr	vbar_el1, x1
	mrs	x2, CurrentEL
	cmp	x2, #0x8
	b.ne	3f
	msr	vbar_el2, x1
3:
	mov	x0, x20
	mov	x1, x19
	bl	menu_main
	cbz	x0, halt
	mov	x16, x0

	mov	x0, x19		/* stock kernel / XNU loader DTB */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	dsb	sy
	ic	iallu
	dsb	sy
	isb
	br	x16

	.globl halt
halt:
	wfe
	b	halt

	.globl psci_reset
psci_reset:
	movz	x0, #0x0009
	movk	x0, #0x8400, lsl #16
	smc	#0
	movz	x0, #0x0009
	movk	x0, #0x8400, lsl #16
	hvc	#0
4:	wfe
	b	4b

.macro vent slot
	.balign	0x80
	mov	x0, #\slot
	b	exc_common
.endm

	.balign	0x800
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
	bl	menu_exception
	b	halt
