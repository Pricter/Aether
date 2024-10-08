/**
 * hpet.c: HPET Timer
 * 
 * Gets HPET address from ACPI, used to calibrate LAPIC and as a precise timer.
 */

#include <kernel/acpi.h>
#include <kernel/kprintf.h>
#include <kernel/hpet.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/macros.h>
#include <stdbool.h>

/* HPET Structure */
struct hpet* sysHPET;

/* Number of comparators */
uint8_t comparatorsCount = 0;

/* Address of the HPET, needed to access it */
uint64_t hpetAddress = 0;

/* Tick period of the HPET */
uint32_t hpetTickPeriod = 0

/* Capabilities of the HPET, read as bits or sequence of bits in the uint64_t */;
uint64_t hpetGeneralCapabilities = 0;

bool hpet_initialized = false;

/* Macro to make configuration of the comparators easier */
#define HPET_TIMER_CONFIGURATION(N) (0x100 + 0x20 * (N))

/* Initialize HPET */
void __init hpet_init(void) {
	/* If no HPET then return */
	if(!acpi_exists("HPET")) {
		kprintf("hpet: There is no HPET available in this system\n");
		return;
	}

	/* Use ACPI to find HPET timer structure */
	sysHPET = (struct hpet*)acpi_find_table("HPET");

	/* Access HPET from HHDM from now */
	hpetAddress = sysHPET->address + HHDM_HIGHER_HALF;

	/* Map HPET's address to HHDM */
	mmu_map_page(mmu_kernel_pagemap, hpetAddress, sysHPET->address, PTE_PRESENT | PTE_WRITABLE);

	comparatorsCount = sysHPET->comparatorsCount + 1;
	hpetGeneralCapabilities = hpet_read(0);
	hpetTickPeriod = (uint32_t)(hpetGeneralCapabilities >> 32);

	/* Tick period is too long! */
	if(hpetTickPeriod > 0x05F5E100) {
		panic("HPET Tick period > 100ns", NULL);
	}

	/* Print HPET details */
	kprintf("hpet: Found HPET ");
	for(int i = 0; i < 4; i++) {
		kprintf("%c", sysHPET->hdr.creatorId[i]);
	}
	kprintf(":%d with %lu at %p with %luHz\n", sysHPET->hdr.creatorRevision, comparatorsCount,
		hpetAddress, 1000000000000000 / hpetTickPeriod);
	
	for(uint8_t i = 0; i < comparatorsCount; i++) {
		uint64_t config = HPET_TIMER_CONFIGURATION(i);
		hpet_write(config, hpet_read(config) & ~(1u << 2));
		kprintf("hpet: Disabled HPET comparator #%d with config: %p\n", i,
			hpet_read(config));
	}

	hpet_initialized = true;
}

/**
 * Use HPET to sleep, making it highly precise
 * 
 * @param ns: Nanoseconds to sleep
*/
void hpet_sleep(uint64_t ns) {
	uint64_t original = hpet_get_count();

	uint64_t ticks = (ns * 1000000) / hpetTickPeriod;

	hpet_write(0x10, hpet_read(0x10) & ~(1u << 0));
	hpet_write(0xF0, 0);
	hpet_write(0x10, hpet_read(0x10) || (1u << 0));

	while(hpet_read(0xF0) <= ticks) {
		asm ("pause");
	}

	hpet_write(0x10, hpet_read(0x10) & ~(1u << 0));
	hpet_write(0xF0, original + ticks);
	hpet_write(0x10, hpet_read(0x10) || (1u << 0));
}

/* Reset HPET Counter */
void hpet_reset_counter(void) {
	hpet_write(0x10, hpet_read(0x10) & ~(1u << 0));
	hpet_write(0xF0, 0);
	hpet_write(0x10, hpet_read(0x10) || (1u << 0));
}

/**
 * Returns in nanoseconds the time passed since HPET started
*/
uint64_t hpet_timer_since(void) {
	uint64_t ticks = hpet_get_count();
	return (ticks * hpetTickPeriod) / 1000000;
}