#include <kernel/acpi.h>
#include <kernel/kprintf.h>
#include <kernel/hpet.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>

struct hpet* sysHPET;
uint8_t comparatorsCount = 0;
uint64_t hpetAddress = 0;
uint32_t hpetTickPeriod = 0;
uint64_t hpetGeneralCapabilities = 0;

#define HPET_TIMER_CONFIGURATION(N) (0x100 + 0x20 * (N))

static inline void hpet_write(uint64_t reg, uint64_t value) {
	*(volatile uint64_t*)(reg + hpetAddress) = value;
}

static inline uint64_t hpet_read(uint64_t reg) {
	return *(volatile uint64_t*)(reg + hpetAddress);
}

void hpet_init(void) {
	sysHPET = (struct hpet*)acpi_find_table("HPET");

	hpetAddress = sysHPET->address + HHDM_HIGHER_HALF;
	mmu_map_page(mmu_kernel_pagemap, hpetAddress, sysHPET->address, PTE_PRESENT | PTE_WRITABLE);

	comparatorsCount = sysHPET->comparatorsCount + 1;
	hpetGeneralCapabilities = hpet_read(0);
	hpetTickPeriod = (uint32_t)(hpetGeneralCapabilities >> 32);

	if(hpetTickPeriod > 0x05F5E100) {
		panic("HPET Tick period > 100ns", NULL);
	}

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
}

void hpet_sleep(uint64_t us) {
	uint64_t ticks = (us * 1000000000) / hpetTickPeriod;

	hpet_write(0x10, hpet_read(0x10) & ~(1u << 0));
	hpet_write(0xF0, 0);
	hpet_write(0x10, hpet_read(0x10) || (1u << 0));

	while(hpet_read(0xF0) <= ticks) {
		asm ("pause");
	}
}