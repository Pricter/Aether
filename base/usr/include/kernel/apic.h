#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>
#include <kernel/acpi.h>

static inline bool lapic_support() {
	uint32_t eax, unused, edx;
	__get_cpuid(1, &eax, &unused, &unused, &edx);
	return edx & CPUID_FEAT_EDX_APIC;
}

void lapic_timer_calibrate(void);
void lapic_init(void);
void lapic_eoi(void);
void pic_disable(void);
uint32_t ioapic_read(struct madt_ioapic* ioapic, uint32_t reg);
void ioapic_write(struct madt_ioapic* ioapic, uint32_t reg, uint32_t value);
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t val);
void ioapic_irq_redirect(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status);
static struct madt_ioapic *ioapic_from_gsi(uint32_t gsi);
void ioapic_set_gsi_redirect(uint32_t lapic_id, uint8_t vector, uint8_t gsi,
                              uint16_t flags, bool status);
void lapic_oneshot(uint64_t us, uint8_t vector);