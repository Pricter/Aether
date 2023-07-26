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
 
    /* Align .init sections to 0x1000 */
	text_start = .;
    .init ALIGN(0x1000) :
    {
        *(.init .init.*)
    } :text
 
    .text : {
        *(.text)
    } :text
    text_end = .;
 
    . = ALIGN(0x1000);
 
    rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata
    rodata_end = .;
 
    . = ALIGN(0x1000);
 
    data_start = .;
    .data : {
        *(.data)
    } :data
 
    .bss : {
        *(COMMON)
        *(.bss)
    } :data
    data_end = .;
 
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
		*(.debug_info)
		*(.debug_abbrev)
		*(.debug_loclists)
		*(.debug_aranges)
		*(.debug_rnglists)
		*(.debug_line)
		*(.debug_str)
		*(.debug_line_str)
		*(.comment)
    }
}