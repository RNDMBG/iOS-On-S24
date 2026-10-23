/* SPDX-License-Identifier: GPL-2.0-only
 * Experimental Galaxy S24 (SM-S921B, e1s) XNU loader entry.
 *
 * This is an arm64 Linux Image so Samsung S-Boot treats it as a kernel. It
 * is position independent and expects to be entered with the MMU off. The
 * SWEETXNU payload is appended after _payload_start by
 * tools/e1s/append_payload.py.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start
	.long	0
	.quad	0x80000
	.quad	_payload_start
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
	mrs	x20, CurrentEL
	cmp	x20, #0x8
	b.eq	from_el2
	cmp	x20, #0x4
	b.ne	halt
	b	el1_entry

from_el2:
	/* XNU expects EL1. Open EL1 timer/counter and FP/SIMD, then eret. */
	mov	x1, #(1 << 31)		/* HCR_EL2.RW */
	msr	hcr_el2, x1
	mov	x1, #3			/* CNTHCTL_EL2.EL1PCTEN|EL1PCEN */
	msr	cnthctl_el2, x1
	msr	cptr_el2, xzr
	adr	x1, el1_entry
	msr	elr_el2, x1
	mov	x1, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x1
	eret

el1_entry:
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb

	/* CNTFRQ may be zero after S-Boot; the e1s timer runs at 25.6 MHz. */
	mrs	x1, cntfrq_el0
	cbnz	x1, 1f
	movz	x1, #0xa000
	movk	x1, #0x186, lsl #16
	msr	cntfrq_el0, x1
1:
	mov	x1, #(3 << 20)		/* CPACR_EL1.FPEN */
	mrs	x2, cpacr_el1
	orr	x2, x2, x1
	msr	cpacr_el1, x2
	isb

	adr	x1, _bss_start
	adr	x2, _bss_end
2:	cmp	x1, x2
	b.hs	3f
	str	xzr, [x1], #8
	b	2b
3:
	mov	x0, x19
	lsr	x1, x20, #2
	mrs	x2, sctlr_el1
	bl	loader_main
	cbz	x0, halt
	mov	x16, x0
	ldr	x0, boot_args_pa
	mrs	x14, sctlr_el1
	bic	x14, x14, #1		/* M */
	bic	x14, x14, #4		/* C */
	bic	x14, x14, #0x1000	/* I */
	msr	sctlr_el1, x14
	ic	iallu
	dsb	sy
	isb
	br	x16

	.globl halt
halt:
	wfe
	b	halt

/* Runtime address of the appended payload. adr is PC relative, so this works
 * wherever S-Boot loaded the Image (the rest of the loader is position
 * independent too). */
	.globl payload_addr
payload_addr:
	adr	x0, _payload_start
	ret

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
	bl	loader_exception
	b	halt

	.section .data, "aw"
	.balign 16
	.space 0x10000
_stack_top:
	.space 0x4000
_exc_stack_top:
