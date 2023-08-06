#pragma once

#include <stdint.h>
#include <kernel/mmu.h>
#include <stdbool.h>
#include <kernel/types.h>

extern uint64_t coreCount;

static inline void halt(void) {
	asm ("cli");
	for(;;) asm ("hlt");
}

static inline uint64_t read_cr0(void) {
    uint64_t cr0_value;
    asm volatile("mov %%cr0, %0" : "=r" (cr0_value));
    return cr0_value;
}

static inline uint64_t read_cr2(void) {
    uint64_t cr2_value;
    asm volatile("mov %%cr2, %0" : "=r" (cr2_value));
    return cr2_value;
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3_value;
    asm volatile("mov %%cr3, %0" : "=r" (cr3_value));
    return cr3_value;
}

static inline uint64_t read_cr4(void) {
    uint64_t cr4_value;
    asm volatile("mov %%cr4, %0" : "=r" (cr4_value));
    return cr4_value;
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

typedef struct core {
	/* Local APIC Id */
	uint32_t lapic_id;

	/* If our core is the one that ran start */
	bool bsp;
} core_t;

typedef struct cpu_info {
	char* vendorId; /* Vendor, Ex: Intel, AMD, Qemu */
	char* cpuName; /* The whole model name */
	uint32_t coreCount; /* The number of cores on the CPU */
	uint64_t cpuFeatures;
} cpu_info_t;

extern uint32_t bsp_lapic_id;

static inline bool interrupt_state(void) {
    uint64_t flags;
    asm volatile ("pushfq; pop %0" : "=rm"(flags) :: "memory");
    return flags & (1 << 9);
}

static inline void enable_interrupts(void) {
    asm ("sti");
}

static inline void disable_interrupts(void) {
    asm ("cli");
}

static inline bool interrupt_toggle(bool state) {
    bool ret = interrupt_state();
    if (state) {
        enable_interrupts();
    } else {
        disable_interrupts();
    }
    return ret;
}

/**
 * Register layout for interrupt context.
 */
struct regs {
	/* Pushed by common stub */
	uintptr_t r15, r14, r13, r12;
	uintptr_t r11, r10, r9, r8;
	uintptr_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

	/* Pushed by wrapper */
	uintptr_t int_no, err_code;

	/* Pushed by interrupt */
	uintptr_t rip, cs, rflags, rsp, ss;
};

void panic(const char* desc, struct regs* r);

static void fatal(void) {
	asm ("cli");
	for(;;) {
		asm volatile (
			"hlt\n"
		);
	}
}

static inline void *get_gs_register(void) {
    void *ret = NULL;
    asm volatile ("mov %%gs:0x0, %0" : "=r" (ret));
    return ret;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
	uint32_t low = value & 0xFFFFFFFF;
	uint32_t high = (value >> 32) & 0xFFFFFFFF;

	asm volatile ("wrmsr" : : "c" (msr), "a" (low), "d" (high));
}

static inline void set_gs_register(void* toSet) {
    wrmsr(0xC0000101, (uint64_t)toSet);
	wrmsr(0xC0000102, (uint64_t)toSet);
	asm volatile ("swapgs");
}