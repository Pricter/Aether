#include <kernel/cpu.h>
#include <kernel/kprintf.h>
#include <kernel/spinlock.h>
#include <kernel/symbols.h>

spinlock_t paniclock = SPINLOCK_ZERO;

/**
 * Handle fatal exceptions.
 * 
 * prints information and cause of the panic and dumps register state
 * 
 * @param desc Textual description
 * @param r Interrupt register context
 * @param faulting_address When available, the address leading to this fault
*/
void panic(const char* desc, struct regs* r) {
	spinlock_acquire(&paniclock);

	kprintf("\nJeff kernel panic! (%s)n", desc);

	if(r == NULL) goto _done;

	kprintf("Registers at interrupt:\n");
	kprintf("  $rip=0x%016lx\n", r->rip);
	kprintf("  $rsi=0x%016lx, $rdi=0x%016lx, $rbp=0x%016lx, $rsp=0x%016lx\n",
		r->rsi, r->rdi, r->rbp, r->rsp);
	kprintf("  $rax=0x%016lx, $rbx=0x%016lx, $rcx=0x%016lx, $rdx=0x%016lx\n",
		r->rax, r->rbx, r->rcx, r->rdx);
	kprintf("  $r8=0x%016lx,  $r9=0x%016lx,  $r10=0x%016lx, $r11=0x%016lx\n",
		r->r8, r->r9, r->r10, r->r11);
	kprintf("  $r12=0x%016lx, $r13=0x%016lx, $r14=0x%016lx, $r15=0x%016lx\n",
		r->r12, r->r13, r->r14, r->r15);
	kprintf("  cs=0x%016lx ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->cs, r->ss, r->rflags, r->int_no, r->err_code);
	
	uint32_t gs_base_low, gs_base_high;
	asm volatile ( "rdmsr" : "=a" (gs_base_low), "=d" (gs_base_high): "c" (0xc0000101) );
	uint32_t kgs_base_low, kgs_base_high;
	asm volatile ( "rdmsr" : "=a" (kgs_base_low), "=d" (kgs_base_high): "c" (0xc0000102) );
	kprintf("  gs=0x%08x%08x kgs=0x%08x%08x\n",
		gs_base_high, gs_base_low, kgs_base_high, kgs_base_low);
	
	stacktrace();

	goto _done;

_done:
	spinlock_release(&paniclock);
	fatal();
}