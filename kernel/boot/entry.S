.section .text.boot, "ax"
.global arm64_boot
.type arm64_boot, %function
.extern arm64_early_main

/* Standard arm64 Image header lets QEMU supply its generated DTB in x0.
 * The flags select little endian, 4 KiB pages, and placement near RAM base. */
.global arm64_image_start
arm64_image_start:
    b arm64_boot
    nop
    .quad 0x80000
    .quad __image_size
    .quad 2
    .quad 0, 0, 0
    .long 0x644d5241
    .long 0

/* Entry contract: QEMU virt arm64 Image boot, one CPU, nonsecure EL1,
 * MMU off, x0 = DTB physical address. The kernel consumes the ARM64 Image/DTB protocol directly.
 * Architectural register names remain fixed by the instruction set. */
arm64_boot:
    mov x19, x0
    msr daifset, #0xf
    mrs x0, CurrentEL
    cmp x0, #4
    b.ne .Lunsupported_level

    /* Select the dedicated EL1 stack before entering any C function. */
    /* EL0 libc uses FP/SIMD; scheduler saves the complete bank. */
    mov x6, #(3 << 20)
    msr cpacr_el1, x6
    isb
    msr spsel, #1
    ldr x0, =__stack_top
    mov sp, x0
    mov x1, sp

#ifdef KERNEL_SELF_TESTS
    /* Seed a BSS probe so the boot check verifies our clearing loop,
     * rather than relying on initially zeroed emulator RAM. */
    ldr x2, =arm64_bss_probe
    mov x3, #0xa5
    str x3, [x2]
#endif
    ldr x2, =__bss_start
    ldr x3, =__bss_end
.Lclear_bss:
    cmp x2, x3
    b.hs .Lbss_ready
    str xzr, [x2], #8
    b .Lclear_bss
.Lbss_ready:
    /* Install all sixteen vectors before C. ISB makes VBAR visible before
     * any subsequent instruction can raise a synchronous exception. */
    ldr x0, =arm64_exception_vectors
    msr vbar_el1, x0
    isb
    /* Pass the original stack value for an independent alignment check. */
    mov x0, x1
    mov x1, x19
    bl arm64_early_main
.Lunsupported_level:
    /* Unsupported exception levels cannot use the EL1 boot contract. */
    wfi
    b .Lunsupported_level
.size arm64_boot, . - arm64_boot
