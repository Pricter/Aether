.arch armv8-a+fp+simd
.section .text
.balign 4
.global context_switch
.type context_switch, %function
/* AAPCS64 context call: x0 = outgoing, x1 = incoming. Caller-saved state is
 * already handled by C ABI or, for preemption, the full exception frame. */
context_switch:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
    add x9, x0, #112
    stp q0, q1, [x9, #0]
    stp q2, q3, [x9, #32]
    stp q4, q5, [x9, #64]
    stp q6, q7, [x9, #96]
    stp q8, q9, [x9, #128]
    stp q10, q11, [x9, #160]
    stp q12, q13, [x9, #192]
    stp q14, q15, [x9, #224]
    stp q16, q17, [x9, #256]
    stp q18, q19, [x9, #288]
    stp q20, q21, [x9, #320]
    stp q22, q23, [x9, #352]
    stp q24, q25, [x9, #384]
    stp q26, q27, [x9, #416]
    stp q28, q29, [x9, #448]
    stp q30, q31, [x9, #480]
    mrs x9, fpcr
    mrs x10, fpsr
    str x9, [x0, #624]
    str x10, [x0, #632]
    mov x0, x1
    b context_enter
.size context_switch, . - context_switch

.global context_enter
.type context_enter, %function
/* Restore a saved call frame or a fresh thread's trampoline return address. */
context_enter:
    /* Do not carry an outgoing thread's local exclusive reservation into
     * another context, including a switch that does not pass through ERET. */
    clrex
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    add x9, x0, #112
    ldp q0, q1, [x9, #0]
    ldp q2, q3, [x9, #32]
    ldp q4, q5, [x9, #64]
    ldp q6, q7, [x9, #96]
    ldp q8, q9, [x9, #128]
    ldp q10, q11, [x9, #160]
    ldp q12, q13, [x9, #192]
    ldp q14, q15, [x9, #224]
    ldp q16, q17, [x9, #256]
    ldp q18, q19, [x9, #288]
    ldp q20, q21, [x9, #320]
    ldp q22, q23, [x9, #352]
    ldp q24, q25, [x9, #384]
    ldp q26, q27, [x9, #416]
    ldp q28, q29, [x9, #448]
    ldp q30, q31, [x9, #480]
    ldr x9, [x0, #624]
    ldr x10, [x0, #632]
    msr fpcr, x9
    msr fpsr, x10
    ldr x9, [x0, #96]
    mov sp, x9
    /* Publish the outgoing context only after its save and this SP change.
     * Preserve the incoming link/frame across the C completion hook. */
    stp x29, x30, [sp, #-16]!
    bl scheduler_switch_complete
    ldp x29, x30, [sp], #16
    ret
.size context_enter, . - context_enter

.global thread_entry_trampoline
.type thread_entry_trampoline, %function
/* A fresh stack starts 16-byte aligned. x19 holds the borrowed entry function;
 * returning from it terminates the thread rather than falling off its stack. */
thread_entry_trampoline:
    msr daifclr, #2
    blr x19
    bl thread_exit
.Linvalid_thread_return:
    msr daifset, #0xf
    wfi
    b .Linvalid_thread_return
.size thread_entry_trampoline, . - thread_entry_trampoline
