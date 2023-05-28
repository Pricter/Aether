#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/acpi.h>

static uint32_t ioapic_read(struct madt_ioapic* ioapic, uint32_t reg) {
	/* To read something we have to put the register index in IOREGSEL */
	uint64_t base = (uint64_t)ioapic->ioAPICAddress + HHDM_HIGHER_HALF;
	*(volatile uint32_t*)base = reg;
	
	/* And then read IOREGWIN */
	return *(volatile uint32_t*)(base + 16);
}

static void ioapic_write(struct madt_ioapic* ioapic, uint32_t reg, uint32_t value) {
	uint64_t base = (uint64_t)ioapic->ioAPICAddress + HHDM_HIGHER_HALF;
	*(volatile uint32_t*)base = reg;
	*(volatile uint32_t*)(base + 16) = value;
}