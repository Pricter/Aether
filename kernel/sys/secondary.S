.section .text
.balign 4
.global arm64_secondary_entry
.type arm64_secondary_entry, %function
/* PSCI CPU_ON enters at EL1 with x0 = cleaned physical boot-block address.
 * No shared BSS clearing or C stack access is permitted before translation. */
arm64_secondary_entry:
    msr daifset, #0xf
    mrs x1, CurrentEL
    cmp x1, #4
    b.ne .Lsecondary_halt
    /* EL0 libc uses FP/SIMD; scheduler saves the complete bank. */
    mov x6, #(3 << 20)
    msr cpacr_el1, x6
    isb
    msr spsel, #1
    mov x19, x0
    ldp x1, x2, [x19, #0]
    mov sp, x1
    ldp x3, x4, [x19, #16]
    ldr x5, [x19, #32]
    ldr x20, [x19, #40]
    ldr x6, =arm64_exception_vectors
    msr vbar_el1, x6
    msr mair_el1, x4
    msr tcr_el1, x3
    msr ttbr0_el1, x2
    msr ttbr1_el1, xzr
    isb
    tlbi vmalle1
    ic iallu
    dsb sy
    isb
    msr sctlr_el1, x5
    isb
    msr tpidr_el1, x20
    isb
    mov x0, x20
    bl arm64_secondary_main
.Lsecondary_halt:
    wfi
    b .Lsecondary_halt
.size arm64_secondary_entry, . - arm64_secondary_entry
