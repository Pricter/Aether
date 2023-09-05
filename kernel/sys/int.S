.section .text
.align 4

.extern isr_handler
.type isr_handler, @function

.global isr_common
isr_common:
    /* Save all registers */
    swapgs
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    cld

    /* Call the interrupt handler */
    mov %rsp, %rdi
    call isr_handler
    mov %rax, %rsp

    /* Restore all registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    swapgs
    
    /* Cleanup error code and interrupt number */
    add $16, %rsp

    /* Return from interrupt */
    iretq

.macro ISR index
.global _isr\index
.type _isr\index, @function
_isr\index:
.if 0x\index != 8 && 0x\index != 10 && 0x\index != 11 && 0x\index != 12 && 0x\index != 13 && 0x\index != 14 && 0x\index != 17 && 0x\index != 30
    pushq $0
.endif
    pushq $0x\index
    jmp isr_common
.endm

.macro ISRADDR index
    .quad _isr\index
.endm

.section .data

.irpc i, 0123456789abcdef
.irpc j, 0123456789abcdef
    ISR \i\j
.endr
.endr

.global isrs
.align 8
isrs:
.irpc i, 0123456789abcdef
.irpc j, 0123456789abcdef
    ISRADDR \i\j
.endr
.endr