.section .text
.balign 4
.global arm64_verify_saved_registers
.type arm64_verify_saved_registers, %function
/* Preserve the caller's ABI state, then seed every callee-saved integer register
 * and yield 32 times to a real peer. Each return must restore every seed. */
arm64_verify_saved_registers:
    sub sp, sp, #112
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x19, #275
    mov x20, #276
    mov x21, #277
    mov x22, #278
    mov x23, #279
    mov x24, #280
    mov x25, #281
    mov x26, #282
    mov x27, #283
    mov x28, #284
    mov x29, #285
    mov x9, #32
    str x9, [sp, #96]
.Lregister_round:
    bl schedule
    cmp x19, #275
    b.ne .Lregister_failure
    cmp x20, #276
    b.ne .Lregister_failure
    cmp x21, #277
    b.ne .Lregister_failure
    cmp x22, #278
    b.ne .Lregister_failure
    cmp x23, #279
    b.ne .Lregister_failure
    cmp x24, #280
    b.ne .Lregister_failure
    cmp x25, #281
    b.ne .Lregister_failure
    cmp x26, #282
    b.ne .Lregister_failure
    cmp x27, #283
    b.ne .Lregister_failure
    cmp x28, #284
    b.ne .Lregister_failure
    cmp x29, #285
    b.ne .Lregister_failure
    ldr x9, [sp, #96]
    subs x9, x9, #1
    str x9, [sp, #96]
    b.ne .Lregister_round
    mov x0, #0
    b .Lregister_restore
.Lregister_failure:
    mov x0, #1
.Lregister_restore:
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #112
    ret
.size arm64_verify_saved_registers, . - arm64_verify_saved_registers
