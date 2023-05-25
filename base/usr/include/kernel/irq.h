#pragma once

#include <kernel/regs.h>
#include <stdint.h>
#include <stddef.h>

extern struct regs *_isr0(struct regs*);
extern struct regs *_isr1(struct regs*);
extern struct regs *_isr2(struct regs*);
extern struct regs *_isr3(struct regs*);
extern struct regs *_isr4(struct regs*);
extern struct regs *_isr5(struct regs*);
extern struct regs *_isr6(struct regs*);
extern struct regs *_isr7(struct regs*);
extern struct regs *_isr8(struct regs*);
extern struct regs *_isr9(struct regs*);
extern struct regs *_isr10(struct regs*);
extern struct regs *_isr11(struct regs*);
extern struct regs *_isr12(struct regs*);
extern struct regs *_isr13(struct regs*);
extern struct regs *_isr14(struct regs*);
extern struct regs *_isr15(struct regs*);
extern struct regs *_isr16(struct regs*);
extern struct regs *_isr17(struct regs*);
extern struct regs *_isr18(struct regs*);
extern struct regs *_isr19(struct regs*);
extern struct regs *_isr20(struct regs*);
extern struct regs *_isr21(struct regs*);
extern struct regs *_isr22(struct regs*);
extern struct regs *_isr23(struct regs*);
extern struct regs *_isr24(struct regs*);
extern struct regs *_isr25(struct regs*);
extern struct regs *_isr26(struct regs*);
extern struct regs *_isr27(struct regs*);
extern struct regs *_isr28(struct regs*);
extern struct regs *_isr29(struct regs*);
extern struct regs *_isr30(struct regs*);
extern struct regs *_isr31(struct regs*);
extern struct regs *_isr32(struct regs*); /* Timer interrupt */
extern struct regs *_isr128(struct regs*); /* Syscall */

typedef struct regs * (*interrupt_handler_t)(struct regs *);

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

typedef void (*irq_t)(void);

#define IRQ_COUNT 1
extern irq_t irqs[IRQ_COUNT];

extern void idt_load(void *);
void idt_reload(void);