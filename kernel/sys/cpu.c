#include <kernel/cpu.h>
#include <kernel/kprintf.h>
#include <kernel/spinlock.h>
#include <kernel/symbols.h>
#include <kernel/init.h>
#include <kernel/cpufeature.h>
#include <kernel/mmu.h>

spinlock_t paniclock = SPINLOCK_ZERO;

uint64_t cpu_features = 0;

extern struct limine_framebuffer *framebuffer;

/**
 * Handle fatal exceptions.
 * 
 * prints information and cause of the panic and dumps register state
 * 
 * @param desc Textual description
 * @param r Interrupt register context
*/
void panic(const char* desc, struct regs* r) {
	spinlock_acquire(&paniclock);

	kprintf("\033[0;41m");
	clear_screen();
	reset_cursor();
	kprintf("\nJeff kernel panic! (%s)\n", desc);

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
	kprintf("  gs=0x%016lx kgs=0x%016lx\n",
		rdmsr(0xc0000101), rdmsr(0xc0000102));
	kprintf("  cr0=0x%016lx cr2=0x%016lx cr3=0x%016lx cr4=0x%016lx\n",
		read_cr0(), read_cr2(), read_cr3(), read_cr4());

	goto _done;

_done:
	stacktrace();
	kprintf("\033[0m");
	spinlock_release(&paniclock);
	fatal();
}

void __init cpu_feature_init(void) {
	uint32_t unused, ecx = 0, edx = 0;
	__get_cpuid(1, &unused, &unused, &ecx, &edx);

	cpu_features = ((uint64_t)edx << 32) | ecx;
	kprintf("cpu: CPU Has features edx:eax of cpuid 1 = %p\n", cpu_features);
}