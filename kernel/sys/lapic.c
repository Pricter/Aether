#include <stdint.h>
#include <stddef.h>
#include <kernel/cpu.h>
#include <kernel/kprintf.h>
#include <kernel/mmu.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_ENABLE 0x800
#define LAPIC_REG_SPURIOUS 0xF0

static inline uint32_t lapic_read(uint32_t reg) {
	return *((volatile uint32_t*)((uintptr_t)0xfee00000 + HHDM_HIGHER_HALF + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
	*((volatile uint32_t*)((uintptr_t)0xfee00000 + HHDM_HIGHER_HALF + reg)) = value;
}

void lapic_init(void) {
	if(rdmsr(IA32_APIC_BASE_MSR) & 0xfffff000 != 0xfee00000) {
		kprintf("lapic: FATAL: The IA32_APIC_BASE_MSR address is not as specified in the SDM\n");
		halt();
	}

	/* Configure spurious IRQ */
	lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | (1 << 8) | 0xff);
}