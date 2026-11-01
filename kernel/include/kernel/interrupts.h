#pragma once
#include <kernel/irq_control.h>

typedef void (*kernel_irq_handler_t)(void *context);

/* QEMU virt/GICv2 boot-CPU distributor initialization; requires device mappings active and IRQs off.
 * Secondaries initialize their banked private interface separately. SGI/PPI
 * registration belongs to the calling CPU; global SPIs are CPU-0-only.
 * Registration borrows handler/context until unregister succeeds. Normal
 * context only, no allocation. Handlers run IRQ-masked and must not allocate,
 * block, sleep, or unregister themselves. Unknown IRQs are masked and fatal. */
bool kernel_interrupt_initialize(void);
bool kernel_irq_register(unsigned int interrupt_id, kernel_irq_handler_t handler, void *context);
bool kernel_irq_unregister(unsigned int interrupt_id);

/* Send a GIC software interrupt to the calling CPU; ID must be 0..15. */
bool kernel_irq_send_self(unsigned int interrupt_id);

/* Send an SGI to an online CPU with a registered private callback. Callers own
 * callback-ID coordination; sending an unregistered ID is a fatal target IRQ. */
bool kernel_irq_send_to(cpu_id_t target_cpu, unsigned int interrupt_id);
bool arm64_interrupt_initialize_local(void);
