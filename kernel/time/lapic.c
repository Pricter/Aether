/**
 * lapic.c: LAPIC Timer
 * 
 * Uses HPET to calibrate
 * Calibrates, initializes, and resets lapic timer. Irq handler and sleep functions are present here
 */

#include <kernel/acpi.h>
#include <kernel/apic.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/macros.h>
#include <kernel/cpufeature.h>
#include <kernel/int.h>
#include <kernel/kprintf.h>

/* Frequency at which lapic ticks */
uint64_t frequency = 0;

/* How many kernel ticks have passed since lapic has been initialized, increased every 10ms */
uint64_t kernel_ticks = 0;

/* How many times the lapic timer ticks in 10ms */
uint32_t ticksIn10ms = 0;

/* Calibrate the LAPIC timer  */
void __init lapic_timer_calibrate(uint64_t ns) {
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);

	/* Use the HPET timer to calibrate the LAPIC */
	hpet_sleep(ns);
	ticksIn10ms = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CURCNT);

	lapic_write(LAPIC_REG_LVT_TIMER, 32 | LAPIC_TIMER_PERIODIC);
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, ticksIn10ms);

	/* Calculate frequency */
	frequency = ticksIn10ms * 1000;
}

/* IRQ Issued by LAPIC once every 10ms */
struct regs* lapic_irq_handler(struct regs* r) {
	kernel_ticks += ticksIn10ms;

	/* Send signal saying interrupt has ended */
	lapic_write(LAPIC_REG_EOI, LAPIC_EOI_ACK);
	return r;
}

/* Getter functions */
uint32_t lapic_get_current_count() {
	return lapic_read(LAPIC_REG_TIMER_CURCNT);
}

uint64_t lapic_get_frequency() {
	return frequency;
}

uint64_t get_kernel_ticks(void) {
	return kernel_ticks;
}

/**
 * lapic_issue_ipi: Issues IPIs
 * 
 * Interrupts another code no the desired vector
 * 
 * @param core: The code to issue IPI on
 * @param vector: The vector on which interrupt is issued
 * @param shorthand: See section 11.6.1 Intel Software developer manual Volume 3
 * @param delivery: See section 11.6.1 Intel Software developer manual Volume 3
 */
void lapic_issue_ipi(uint16_t core, uint8_t vector, uint8_t shorthand, uint8_t delivery) {
	uint64_t ipi_value = 0;
	ipi_value |= (shorthand & 0x03) << 18;
	ipi_value |= ((uint64_t)(core & 0xFF) << 56);
	ipi_value |= ((delivery & 0x07) << 8);
	ipi_value |= (vector & 0xFF);
	lapic_write(LAPIC_ICR_LOW, (uint32_t)ipi_value);
	lapic_write(LAPIC_ICR_HIGH, (uint32_t)(ipi_value >> 32));
}

/* Assume all local apics are enabled */
void __init lapic_init(void) {
	/* We get the lapic address with HHDM_HIGHER_HALF already added */
	mmu_map_page(mmu_kernel_pagemap, lapic_address, lapic_address - HHDM_HIGHER_HALF, PTE_PRESENT | PTE_WRITABLE);

	lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | (1 << 8) | 0xff);
}