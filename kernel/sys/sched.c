#include <kernel/sched.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <string.h>

void sched_unreachable(void) {
	asm ("cli");
	for(;;) asm ("hlt");
}