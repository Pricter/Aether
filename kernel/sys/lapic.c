#include <kernel/acpi.h>
#include <kernel/ports.h>
#include <kernel/pit.h>
#include <kernel/apic.h>
#include <kernel/cpu.h>

#define LAPIC_REG_SPURIOUS 0xf0
#define LAPIC_REG_EOI 0xb0 /* End of interrupt */
#define LAPIC_EOI_ACK 0x00 /* EOI Acknowledge */ 
#define LAPIC_REG_TIMER_INITCNT 0x380 /* Initial Count register */
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_TIMER_DIV 0x3e0
#define LAPIC_REG_TIMER_CURCNT 0x390

/* Disable the PIC because we are using the lapic */
void pic_disable(void) {
	outportb(0xA1, 0xff);
	outportb(0x21, 0xff);
}

uint32_t lapic_read(uint32_t reg) {
	return *(volatile uint32_t*)((uintptr_t)lapic_address + reg);
}

void lapic_write(uint32_t reg, uint32_t val) {
	*(volatile uint32_t*)((uintptr_t)lapic_address + reg) = val;
}

void lapic_timer_stop(void) {
	lapic_write(LAPIC_REG_TIMER_INITCNT, 0);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16);
}

/* Assume all local apics are enabled */
void lapic_init(void) {
	lapic_timer_calibrate();

	lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | (1 << 8) | 0xff);
}

void lapic_eoi(void) {
	lapic_write(LAPIC_REG_EOI, LAPIC_EOI_ACK);
}

uint64_t freq = 0;

void lapic_oneshot(uint64_t us, uint8_t vector) {
	bool old_int_state = interrupt_toggle(false);
	lapic_timer_stop();

    uint64_t ticks = us * (freq / 1000000);

    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_DIV, 0);
    lapic_write(LAPIC_REG_TIMER_INITCNT, ticks);

    interrupt_toggle(old_int_state);
}

void lapic_timer_calibrate(void) {
	lapic_timer_stop();

	lapic_write(LAPIC_REG_LVT_TIMER, (1 << 16) | 0xff); /* Vector 0xff, masked */
	lapic_write(LAPIC_REG_TIMER_DIV, 0);

	pit_reload_value(0xffff);

	uint64_t samples = 0xffff;

	uint16_t initial_tick = get_pit_count();

	lapic_write(LAPIC_REG_TIMER_INITCNT, (uint32_t)samples);
	while(lapic_read(LAPIC_REG_TIMER_CURCNT) != 0);

	uint16_t final_tick = get_pit_count();
	uint64_t total_ticks = initial_tick - final_tick;

	freq = (samples / total_ticks) * PIT_DIVIDEND;

	// TODO: Set lapic_freq

	lapic_timer_stop();
}