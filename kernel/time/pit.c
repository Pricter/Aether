#include <kernel/pit.h>
#include <stdint.h>
#include <kernel/ports.h>
#include <kernel/kprintf.h>
#include <kernel/apic.h>
#include <kernel/irq.h>
#include <kernel/cpu.h>
#include <kernel/time.h>

void pit_reload_value(uint16_t value) {
	/* 0x34 = 0b00110100, channel 0, lobyte/hibyte, rate generator */
	outportb(PIT_CONTROL, 0x34);

	/* A reload value is 16bit but outportb only sends 8 bits, so we send 2 8bits outportb */
	outportb(PIT_CHANNEL0, (uint8_t)value);
	outportb(PIT_CHANNEL0, (uint8_t)(value >> 8));
}

void pit_set_frequency(uint64_t frequency) {
	uint64_t divisor = PIT_DIVIDEND / frequency;
	if((PIT_DIVIDEND & frequency) > (frequency / 2)) divisor++;
	pit_reload_value((uint16_t)divisor);
}

void pit_timer_handler(struct regs* r) {
	__kernel_ticks++; /* A tick = 1ms FIXME: Its now 50ms */
	lapic_eoi();
	disable_interrupts();
}

void pit_init(void) {
	/* 1000Hz */
	pit_set_frequency(TIMER_FREQ);

	uint8_t timer_vector = idt_allocate();
	kdprintf("pit: Timer vector: %d\n", timer_vector);

	irq_install(pit_timer_handler, timer_vector);
	ioapic_irq_redirect(bsp_lapic_id, timer_vector, 0, true);

	kdprintf("pit: Set system clock frequency to %luhz or %luus\n", TIMER_FREQ,
		1000000 / TIMER_FREQ);
}