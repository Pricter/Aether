.macro SWAPGS_CONDITIONAL
        testq   $3, 24(%rsp)
        jz      1f
        swapgs
1:
.endm

.global _switch
.type _switch, @function
_switch:
	push %r15
	push %r14
	push %r13
	push %r12
	push %rbp
	push %rbx
	mov %rsp, (%rdi)
	mov %rsi, %rsp
	pop %rbx
	pop %rbp
	pop %r12
	pop %r13
	pop %r14
	pop %r15
	ret

.global _thread_continue
.type _thread_continue, @function
_thread_continue:
	mov %rbx, %rsp

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
    add $16, %rsp
    iretq