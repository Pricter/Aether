.macro SWAPGS_CONDITIONAL
        testq   $3, 24(%rsp)
        jz      1f
        swapgs
1:
.endm

.global switch_context
.type switch_context, @function
switch_context:
	mov %rdi, %rsp

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