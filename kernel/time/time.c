#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/time.h>
#include <kernel/cpu.h>

sleep_func timer_sleep;
timer_reset_func timer_reset;
timer_since_func timer_since_reset;

void timer_init(void) {
	if(acpi_exists("HPET")) {
		timer_sleep = hpet_sleep;
		timer_reset = hpet_reset_counter;
		timer_since_reset = hpet_timer_since;
	} else {
		panic("No supported timers present on this system", NULL);
	}
}