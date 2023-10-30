.macro SWAPGS_CONDITIONAL
        testq   $3, 24(%rsp)
        jz      1f
        swapgs
1:
.endm

.global _switch
.type _switch, @function
_switch:
	test %rdi, %rdi
	jz 1f
	sub $0x30, %rsp
	mov %rbx, 0x00(%rsp)
	mov %rbp, 0x08(%rsp)
	mov %r12, 0x10(%rsp)
    mov %r13, 0x18(%rsp)
    mov %r14, 0x20(%rsp)
    mov %r15, 0x28(%rsp)
    mov %rsp, (%rdi)
	1:
	mov %rsi, %rsp
	mov 0x00(%rsp), %rbx
	mov 0x08(%rsp), %rbp
	mov 0x10(%rsp), %r12
	mov 0x18(%rsp), %r13
	mov 0x20(%rsp), %r14
	mov 0x28(%rsp), %r15
	add $0x30, %rsp
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