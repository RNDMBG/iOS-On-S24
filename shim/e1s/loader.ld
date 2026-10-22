ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { *(.text.head) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(8);
	_bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(8);
	_bss_end = .;
	. = ALIGN(16);
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) }
}
