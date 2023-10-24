#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>
#include <kernel/acpi.h>
#include <kernel/cpu.h>
#include <kernel/macros.h>

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

static inline uint32_t lapic_read(uint32_t reg) {
	return *(volatile uint32_t*)((uintptr_t)lapic_address + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
	*(volatile uint32_t*)((uintptr_t)lapic_address + reg) = val;
}

extern bool lapic_initialized;

uint32_t ioapic_read(struct madt_ioapic* ioapic, uint32_t reg);
void ioapic_write(struct madt_ioapic* ioapic, uint32_t reg, uint32_t value);
void ioapic_irq_redirect(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status);
static struct madt_ioapic *ioapic_from_gsi(uint32_t gsi);
void ioapic_set_gsi_redirect(uint32_t lapic_id, uint8_t vector, uint8_t gsi,
                              uint16_t flags, bool status);
struct regs* lapic_timer_handler(struct regs* r);
void lapic_timer_calibrate(uint64_t ns);
void lapic_issue_ipi(uint16_t core, uint8_t vector, uint8_t shorthand, uint8_t delivery);
uint32_t lapic_get_current_count();
uint64_t lapic_get_frequency();