OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata  PT_LOAD    FLAGS((1 << 2)) ;            /* Read only */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ; /* Write + Read */
}

SECTIONS
{
    . = 0xffffffff80000000;

	text_start = .;
    .text : {
        *(.text .init.text)
    } :text
	text_end = .;

    . = ALIGN(0x1000);

	rodata_start = .;
    .rodata : {
        *(.rodata .rodata.* .init.rodata)
    } :rodata
	rodata_end = .;

    . = ALIGN(0x1000);

	data_start = .;
    .data : {
        *(.data .init.data)
    } :data

    .bss : {
        *(COMMON)
        *(.bss)
    } :data
	data_end = .;

    /* Discard .note.* and .eh_frame since they may cause issues on some hosts. */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
