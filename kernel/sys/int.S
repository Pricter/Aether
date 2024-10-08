.section .text
.align 4

/* Check if swapgs needs to be executed and execute it if so */
.macro SWAPGS_CONDITIONAL
        testq   $3, 24(%rsp)
        jz      1f
        swapgs
1:
.endm

/* Declare isr_handler function exists (see idt.c) */
.extern isr_handler
.type isr_handler, @function

/* Common ISR Function, all isrs reach here */
isr_common:
	cld
	SWAPGS_CONDITIONAL

    /* Push all registers and context */
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
    mov %ds, %rax
    push %rax
    mov %es, %rax
    push %rax
    mov %fs, %rax
    push %rax
    mov %gs, %rax
    push %rax
    mov %cr2, %rdi
    push %rdi

    /* Set kernel segments */
    mov $0x10, %rdi
    mov %rdi, %ds
    mov %rdi, %es

    /* Call the interrupt handler */
    mov %rsp, %rdi
    call isr_handler
    mov %rax, %rsp

    /* Restore all registers */
    addq $24, %rsp /* CR2, GS, FS will not be popped */  
    pop %rax
    mov %rax, %es
    pop %rax
    mov %rax, %ds
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

	SWAPGS_CONDITIONAL
    
    /* Cleanup error code and interrupt number */
    add $16, %rsp

    /* Return from interrupt */
    iretq

/* Create a list of ISRs for each vector */

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