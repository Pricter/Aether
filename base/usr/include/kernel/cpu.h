#pragma once

#include <stdint.h>
#include <kernel/mmu.h>
#include <stdbool.h>

static inline void halt() {
	asm ("cli");
	for(;;) asm ("hlt");
}

static inline uint64_t rdmsr(uint32_t msr) {
	uint32_t edx = 0, eax = 0;
	asm volatile (
		"rdmsr\n\t"
		: "=a" (eax), "=d" (edx)
		: "c" (msr)
		: "memory"
	);
	return ((uint64_t)edx << 32) | eax;
}

static inline uint64_t wrmsr(uint32_t msr, uint64_t val) {
    uint32_t eax = (uint32_t)val;
    uint32_t edx = (uint32_t)(val >> 32);
    asm volatile (
        "wrmsr\n\t"
        :
        : "a" (eax), "d" (edx), "c" (msr)
        : "memory"
    );
    return ((uint64_t)edx << 32) | eax;
}

typedef struct core {
	/* Core number */
	uint64_t core_number;

	/* Local APIC Id */
	uint32_t lapic_id;

	/* Current core pagemap */
	pagemap_t *pagemap;

	/* If our core is the one that ran start */
	bool bsp;
} core_t;

typedef struct cpu_info {
	char* vendorId; /* Vendor, Ex: Intel, AMD, Qemu */
	char* cpuName; /* The whole model name */
	uint32_t coreCount; /* The number of cores on the CPU */
} cpu_info_t;

extern uint32_t bsp_lapic_id;