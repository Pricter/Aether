#pragma once

#include <stdint.h>
#include <kernel/mmu.h>

static inline void halt() {
	asm ("cli");
	for(;;) asm ("hlt");
}

typedef struct core {
	/* Core number */
	uint64_t core_number;

	/* Local APIC Id */
	uint32_t lapic_id;

	/* Current core pagemap */
	pagemap_t *pagemap;
} core_t;