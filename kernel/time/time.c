#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/time.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/scheduler.h>
#include <stdbool.h>

sleep_func timer_sleep;
timer_reset_func timer_reset;
timer_since_func timer_since;

bool timer_initialized = false;

/**
 * These timer functions are only ment to be used as general purpose
 * thus the timer that the scheduler uses is not used as the general purpose timer
 * as changes in the timer can mess up the scheduler
*/
void __init timer_init(void) {
	if(hpet_enabled == true && scheduler_timer != SCHEDULER_USING_HPET) {
		timer_sleep = hpet_sleep;
		timer_reset = hpet_reset_counter;
		timer_since = hpet_timer_since;
	} else {
		panic("No supported timers present on this system", NULL);
	}

	timer_initialized = true;
}