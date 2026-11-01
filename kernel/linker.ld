OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(arm64_boot)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* QEMU virt RAM starts at 0x40000000. Leave its first 512 KiB free
     * for a future explicit device-tree handoff before this image. */
    . = 0x40080000;
    __image_start = .;
    .text : {
        __text_start = .;
        KEEP(*(.text.boot)) *(.text .text.*)
        . = ALIGN(4096);
        __text_end = .;
    } :text
    .rodata ALIGN(4096) : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __rodata_end = .;
    } :text
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .bss ALIGN(64) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data
    . = ALIGN(4096);
    __kernel_end = .;
    .stack ALIGN(4096) (NOLOAD) : {
        __stack_bottom = .;
        . += 16384;
        __stack_top = .;
    } :data
    __image_end = .;
    __image_size = __image_end - __image_start;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
