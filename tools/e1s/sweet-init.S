#define SYS_write	4
#define SYS_open	5
#define SYS_getpid	20
#define SYS_select	93
#define SYS_mount	167
#define O_RDWR		2
#define MNT_RDONLY	1

	.text
	.globl	_start
	.p2align 2
_start:
	mov	x9, sp
	and	x9, x9, #~15
	mov	sp, x9

	adr	x0, path
	mov	x1, #O_RDWR
	mov	x2, #0
	mov	x16, #SYS_open
	svc	#0x80
	b.cs	hang			/* no console: nothing to report to */
	mov	x19, x0

	adr	x1, hello
	adr	x2, hello_tail
	sub	x2, x2, x1
	bl	write_console

	mov	x16, #SYS_getpid
	svc	#0x80
	add	w0, w0, #'0'		/* PID 1 expected: one digit */
	sub	sp, sp, #16
	strb	w0, [sp]
	mov	x1, sp
	mov	x2, #1
	bl	write_console
	add	sp, sp, #16

	adr	x1, hello_tail
	adr	x2, alive
	sub	x2, x2, x1
	bl	write_console

	/* mount("apfs", "/mnt", MNT_RDONLY, "/dev/md1") when the loader left one */
	adr	x0, type_apfs
	adr	x1, path_mnt
	mov	x2, #MNT_RDONLY
	adr	x3, path_md1
	mov	x16, #SYS_mount
	svc	#0x80
	adr	x1, md1_fail
	adr	x2, md1_fail_end
	b.cs	1f
	adr	x1, md1_ok
	adr	x2, md1_ok_end
1:
	sub	x2, x2, x1
	bl	write_console

tick:
	/* select(0, NULL, NULL, NULL, &5s) as a portable sleep */
	mov	x0, #0
	mov	x1, #0
	mov	x2, #0
	mov	x3, #0
	adr	x4, five_sec
	mov	x16, #SYS_select
	svc	#0x80

	adr	x1, alive
	adr	x2, alive_end
	sub	x2, x2, x1
	bl	write_console
	b	tick

/* write(x19, x1, x2) */
write_console:
	mov	x0, x19
	mov	x16, #SYS_write
	svc	#0x80
	ret

hang:
	b	hang

	.p2align 3
five_sec:
	.quad	5			/* tv_sec */
	.quad	0			/* tv_usec (int32 + padding) */
path:
	.asciz	"/dev/console"
hello:
	.ascii	"\n*** SWEET userspace: init running as PID "
hello_tail:
	.ascii	" ***\n"
alive:
	.ascii	"SWEET init: still alive\n"
alive_end:
type_apfs:
	.asciz	"apfs"
path_mnt:
	.asciz	"/mnt"
path_md1:
	.asciz	"/dev/md1"
md1_ok:
	.ascii	"SWEET init: /dev/md1 mounted\n"
md1_ok_end:
md1_fail:
	.ascii	"SWEET init: /dev/md1 not mounted\n"
md1_fail_end:
