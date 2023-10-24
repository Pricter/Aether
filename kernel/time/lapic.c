#include <kernel/acpi.h>
#include <kernel/apic.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/macros.h>
#include <kernel/cpufeature.h>
#include <kernel/int.h>
#include <kernel/kprintf.h>

uint64_t frequency = 0;
uint64_t kernel_ticks = 0;
uint32_t ticksIn10ms = 0;
void __init lapic_timer_calibrate(uint64_t ns) {
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);

	hpet_sleep(ns);
	ticksIn10ms = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CURCNT);

	lapic_write(LAPIC_REG_LVT_TIMER, 32 | LAPIC_TIMER_PERIODIC);
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, ticksIn10ms);

	frequency = ticksIn10ms * 1000;
}

struct regs* lapic_irq_handler(struct regs* r) {
	kernel_ticks += ticksIn10ms;
	lapic_write(LAPIC_REG_EOI, LAPIC_EOI_ACK);
	schedule(r);
	return r;
}

uint32_t lapic_get_current_count() {
	return lapic_read(LAPIC_REG_TIMER_CURCNT);
}

uint64_t lapic_get_frequency() {
	return frequency;
}

uint64_t get_kernel_ticks(void) {
	return kernel_ticks;
}

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