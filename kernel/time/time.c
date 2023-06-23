#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/time.h>

sleep_func timer_sleep;

void timer_init(void) {
	if(acpi_exists("HPET")) {
		timer_sleep = hpet_sleep;
	} else {
		panic("No supported timers present on this system", NULL);
	}
}