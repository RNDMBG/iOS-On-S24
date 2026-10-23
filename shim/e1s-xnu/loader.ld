ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { KEEP(*(.text.head)) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(16);
	_bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	_bss_end = .;
	. = ALIGN(4096);
	_payload_start = .;
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) }
}
