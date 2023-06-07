#pragma once

#include <stdint.h>
#include <kernel/ports.h>
#include <kernel/pit.h>

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_CONTROL 0x43

#define TIMER_FREQ 1000
#define PIT_DIVIDEND ((uint64_t)1193182)

void pit_init(void);
void pit_reload_value(uint16_t value);
void pit_set_frequency(uint64_t frequency);

static inline uint16_t get_pit_count(void) {
	/* Let pit know we want the count */
	outportb(PIT_CONTROL, 0x00);

	/* Get 16bit count as 2 8bit ints */
	uint8_t lo = inportb(PIT_CHANNEL1);
	uint8_t hi = inportb(PIT_CHANNEL1);

	/* Convert them to a single 16bit count */
	return ((uint64_t)hi << 8) | lo;
}

void play_sound(uint32_t frequency, uint32_t millis);