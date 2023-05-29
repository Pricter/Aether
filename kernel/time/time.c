#include <kernel/time.h>
#include <kernel/pit.h>
#include <kernel/kprintf.h>

uint64_t __kernel_ticks = 0;

void sleep(uint64_t millis) {
	uint64_t toReach = __kernel_ticks + millis;
	while (__kernel_ticks != toReach) {
		asm ("pause");
	}
}