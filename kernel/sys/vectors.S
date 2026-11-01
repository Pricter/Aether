/* Sixteen 128-byte vector slots. EL1/EL0 IRQs and handled EL0 synchronous
 * traps restore integer context; unhandled kernel exceptions remain fatal. */
.section .text.vectors, "ax"
.balign 2048
.global arm64_exception_vectors
.type arm64_exception_vectors, %object
.macro exception_vector vector_index
    sub sp, sp, #304
    stp x0, x1, [sp]
    mov x0, #\vector_index
    b arm64_exception_entry
    .balign 128
.endm
arm64_exception_vectors:
    exception_vector 0
    exception_vector 1
    exception_vector 2
    exception_vector 3
    exception_vector 4
    exception_vector 5
    exception_vector 6
    exception_vector 7
    exception_vector 8
    exception_vector 9
    exception_vector 10
    exception_vector 11
    exception_vector 12
    exception_vector 13
    exception_vector 14
    exception_vector 15
.size arm64_exception_vectors, . - arm64_exception_vectors

/* Save original registers before using scratch registers. The 304-byte
 * frame preserves 16-byte stack alignment and matches exception.h. */
.type arm64_exception_entry, %function
arm64_exception_entry:
    msr daifset, #0xf
    str x0, [sp, #288]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    add x0, sp, #304
    str x0, [sp, #248]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [sp, #272]
    mrs x0, sp_el0
    str x0, [sp, #296]
    mov x0, sp
    bl arm64_exception_dispatch
    /* Restore architectural return state before scratch registers. ERET restores
     * the interrupted DAIF mask as well as PC; no IRQ can preempt this teardown. */
    .global arm64_restore_exception
arm64_restore_exception:
    ldr x0, [sp, #296]
    msr sp_el0, x0
    ldp x0, x1, [sp, #256]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldp x0, x1, [sp]
    add sp, sp, #304
    eret
.size arm64_exception_entry, . - arm64_exception_entry
