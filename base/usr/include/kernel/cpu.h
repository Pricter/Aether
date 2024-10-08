#pragma once

#include <stdint.h>
#include <kernel/mmu.h>
#include <stdbool.h>
#include <kernel/types.h>
#include <kernel/msr.h>

extern uint64_t coreCount;

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
	uintptr_t cr2, gs, fs, es, ds;
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
	asm volatile("1: hlt; jmp 1b");
}

static inline void* read_gs_register() {
	return (void*)rdmsr(0xC0000101);
}

static inline void set_gs_register(void* toSet) {
    wrmsr(0xC0000101, (uint64_t)toSet);
	wrmsr(0xC0000102, (uint64_t)toSet);
	asm volatile ("swapgs");
}