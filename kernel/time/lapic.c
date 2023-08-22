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

void __init lapic_timer_calibrate(void) {
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);

	hpet_sleep(10000000);
	uint32_t ticksIn10ms = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CURCNT);

	lapic_write(LAPIC_REG_LVT_TIMER, 32 | LAPIC_TIMER_PERIODIC);
	lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
	lapic_write(LAPIC_REG_TIMER_INITCNT, ticksIn10ms);
}

void lapic_irq_handler(struct regs* r) {
	(void)r;
	lapic_write(LAPIC_REG_EOI, LAPIC_EOI_ACK);
}

/* Assume all local apics are enabled */
void __init lapic_init(void) {
	/* We get the lapic address with HHDM_HIGHER_HALF already added */
	mmu_map_page(mmu_kernel_pagemap, lapic_address, lapic_address - HHDM_HIGHER_HALF, PTE_PRESENT | PTE_WRITABLE);

	lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | (1 << 8) | 0xff);
	// TODO: Do for all cores
	lapic_timer_calibrate();
}