/* Embedded static AArch64 musl image; ELF loading uses only PT_LOAD bytes. */
.section .rodata.init, "a"
.balign 16
.global arm64_init_image, arm64_init_image_end
arm64_init_image:
.incbin ARM64_INIT_IMAGE_PATH
arm64_init_image_end:
