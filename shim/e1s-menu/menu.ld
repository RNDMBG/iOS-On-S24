ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { KEEP(*(.text.head)) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	ASSERT(. <= 0x20000, "menu stub text/data exceeds 0x20000")
	. = 0x20000;
	.menu_config : { KEEP(*(.menu_config)) }
	. = 0x21000;
	_bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	_bss_end = .;
	. = ALIGN(16);
	.stack : { . += 0x4000; _stack_top = .; }
	. = ALIGN(16);
	. = . + 0x2000;
	_exc_stack_top = .;
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) }
}
