#include <kernel/acpi.h>
#include <kernel/apic.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>
#include <kernel/hpet.h>
#include <kernel/macros.h>
#include <kernel/cpufeature.h>
#include <kernel/irq.h>
#include <kernel/kprintf.h>

#define LAPIC_REG_SPURIOUS 0xf0
#define LAPIC_REG_EOI 0xb0 /* End of interrupt */
#define LAPIC_EOI_ACK 0x00 /* EOI Acknowledge */ 
#define LAPIC_REG_TIMER_INITCNT 0x380 /* Initial Count register */
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_TIMER_DIV 0x3e0
#define LAPIC_REG_TIMER_CURCNT 0x390
#define LAPIC_TIMER_PERIODIC 0x20000
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310

void __init lapic_timer_calibrate(uint64_t ns) {
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);

	hpet_sleep(ns);
	uint32_t ticksIn10ms = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CURCNT);

	lapic_write(LAPIC_REG_LVT_TIMER, 32 | LAPIC_TIMER_PERIODIC);
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, ticksIn10ms);
}

void lapic_irq_handler(struct regs* r) {
	// core_t *core = get_gs_register();
	// kprintf("LAPIC INTERRUPT %lu\n", core->lapic_id);
	lapic_write(LAPIC_REG_EOI, LAPIC_EOI_ACK);
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