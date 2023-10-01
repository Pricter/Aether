#pragma once

#include <kernel/cpu.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Interrupt descriptor table
*/
typedef struct {
    uint16_t base_low;
    uint16_t selector;

    uint8_t zero;
    uint8_t flags;

    uint16_t base_mid;
    uint32_t base_high;
    uint32_t pad;
} __attribute__((packed)) idt_entry_t;

struct idt_pointer {
	uint16_t  limit;
	uintptr_t base;
} __attribute__((packed));

typedef struct regs* (*irq_t)(struct regs* r);

#define IRQ_COUNT 2
extern irq_t *irqs;

void idt_init(void);
void idt_reload(void);
uint8_t idt_allocate(void);

void irq_install(irq_t irq, int index);