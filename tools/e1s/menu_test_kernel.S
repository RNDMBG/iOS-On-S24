/* SPDX-License-Identifier: GPL-2.0-only */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start
	.long	0
	.quad	0
	.quad	0x1000
	.quad	0xa
	.quad	0
	.quad	0
	.quad	0
	.ascii	"ARM\x64"
	.long	0

	.text
	.globl _start
_start:
	ldr	x0, =0x09000000
	adr	x2, msg
1:	ldrb	w3, [x2], #1
	cbz	w3, 2f
3:	ldr	w4, [x0, #0x18]
	tbnz	w4, #5, 3b
	str	w3, [x0]
	b	1b
2:	mov	x0, #0x18
	adr	x1, block
	hlt	#0xf000
4:	wfe
	b	4b

msg:
	.asciz	"E1S MENU JUMP\n"
	.balign	8
block:
	.quad	0x20026
	.quad	0
	.balign	8
_end:
