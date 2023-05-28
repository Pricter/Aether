#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>

static inline bool lapic_support() {
	uint32_t eax, unused, edx;
	__get_cpuid(1, &eax, &unused, &unused, &edx);
	return edx & CPUID_FEAT_EDX_APIC;
}

void lapic_init(void);
void pic_disable(void);
uint32_t ioapic_read(struct madt_ioapic* ioapic, uint32_t reg);
static void ioapic_write(struct madt_ioapic* ioapic, uint32_t reg, uint32_t value);