.arch armv8-a+fp+simd
.text
.global arm64_enter_user
.type arm64_enter_user, %function
/* Reset SP_EL1 to the owned kernel-stack top. No C frames survive this ERET;
 * every subsequent EL0 exception enters on that kernel stack, never SP_EL0. */
arm64_enter_user:
    msr daifset, #15
    msr elr_el1, x0
    msr sp_el0, x1
    mov sp, x2
    msr spsr_el1, x3
    /* Initial user state must not reveal registers from the previous task. */
    movi v0.16b, #0
    movi v1.16b, #0
    movi v2.16b, #0
    movi v3.16b, #0
    movi v4.16b, #0
    movi v5.16b, #0
    movi v6.16b, #0
    movi v7.16b, #0
    movi v8.16b, #0
    movi v9.16b, #0
    movi v10.16b, #0
    movi v11.16b, #0
    movi v12.16b, #0
    movi v13.16b, #0
    movi v14.16b, #0
    movi v15.16b, #0
    movi v16.16b, #0
    movi v17.16b, #0
    movi v18.16b, #0
    movi v19.16b, #0
    movi v20.16b, #0
    movi v21.16b, #0
    movi v22.16b, #0
    movi v23.16b, #0
    movi v24.16b, #0
    movi v25.16b, #0
    movi v26.16b, #0
    movi v27.16b, #0
    movi v28.16b, #0
    movi v29.16b, #0
    movi v30.16b, #0
    movi v31.16b, #0
    msr fpcr, xzr
    msr fpsr, xzr
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    eret
.size arm64_enter_user, . - arm64_enter_user

#ifdef KERNEL_SELF_TESTS
/* Position-independent test image, copied into a private RX user page. All
 * absolute addresses below name the test's user VMAs, never kernel symbols. */
.section .rodata.user, "a"
.balign 16
.global arm64_user_payload, arm64_user_payload_end
arm64_user_payload:
    mov x19, #0x2000000
    ldr x20, [x19]
    msr tpidr_el0, x20
    str x20, [sp, #-16]!
    mov x21, #32
1:
    mov x22, #0x3000000
    str x20, [x22]
    ldr x23, [x19]
    cmp x23, x20
    b.ne 3f
    add x20, x20, #1
    str x20, [x19]
    mov x24, #0x2
    lsl x24, x24, #16
2:
    subs x24, x24, #1
    b.ne 2b
    mov x8, #124
    svc #0
    subs x21, x21, #1
    b.ne 1b
    mrs x23, tpidr_el0
    ldr x24, [sp], #16
    cmp x23, x24
    b.ne 3f
    add x24, x24, #32
    cmp x20, x24
    b.ne 3f
    mov x8, #0x7fff
    svc #0
    cmn x0, #38
    b.ne 3f
    mov x0, #0
    b 4f
3:
    mov x0, #1
4:
    mov x8, #93
    svc #0
    b .
arm64_user_payload_end:

.balign 16
.global arm64_tlb_payload, arm64_tlb_payload_end
/* Warm target translation, acknowledge through a distinct control page, then
 * wait without SVCs. The test enters with IRQs masked to exclude local flushes. */
arm64_tlb_payload:
    mov x19, #0x2000000
    ldr x20, [x19]
    mov x21, #0x3000000
    mov x22, #1
    stlr x22, [x21]
1:
    add x23, x21, #8
    ldar x22, [x23]
    cbz x22, 1b
    cmp x22, #1
    b.ne 2f
    str x20, [x19]
    b 3f
2:
    ldr x20, [x19]
3:
    mov x0, #77
    mov x8, #93
    svc #0
    b .
arm64_tlb_payload_end:

#endif /* KERNEL_SELF_TESTS */
