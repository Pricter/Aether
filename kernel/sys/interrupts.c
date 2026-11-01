#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/interrupts.h>
#include <kernel/exception.h>
#include <kernel/scheduler.h>

#define GIC_DISTRIBUTOR 0x08000000UL
#define GIC_CPU_INTERFACE 0x08010000UL
#define GIC_MAX_INTERRUPTS 1020U

/* GICv2 register map. Array registers use one 32-bit bank per 32 IRQs,
 * while priority/target registers pack four 8-bit fields per word. */
#define GIC_PRIVATE_INTERRUPTS 32U
#define GIC_SOFTWARE_INTERRUPTS 16U
#define GIC_REGISTER_BYTES 4U
#define GIC_BYTE_MASK 0xffU
#define GICD_CONTROL 0x000U
#define GICD_TYPE 0x004U
#define GICD_GROUP 0x080U
#define GICD_ENABLE_SET 0x100U
#define GICD_ENABLE_CLEAR 0x180U
#define GICD_PENDING_CLEAR 0x280U
#define GICD_PRIORITY 0x400U
#define GICD_TARGET 0x800U
#define GICD_PRIVATE_CONFIGURATION 0xc04U
#define GICD_SOFTWARE_GENERATE 0xf00U
#define GICD_SOFTWARE_PENDING_CLEAR 0xf10U
#define GICC_CONTROL 0x000U
#define GICC_PRIORITY_MASK 0x004U
#define GICC_BINARY_POINT 0x008U
#define GICC_ACKNOWLEDGE 0x00cU
#define GICC_END_INTERRUPT 0x010U
#define GIC_DEFAULT_PRIORITIES 0xa0a0a0a0U
#define GIC_BOOT_CPU_TARGETS 0x01010101U
#define GIC_TYPE_LINES_MASK 0x1fU
#define GIC_TYPE_CPU_SHIFT 5U
#define GIC_TYPE_CPU_MASK 7U
#define GIC_TIMER_CONFIGURATION_MASK (3U << 28)
#define GIC_ACKNOWLEDGE_ID_MASK 0x3ffU
#define GIC_SGI_TARGET_SHIFT 16U
#define GIC_SGI_SELF (2U << 24)

typedef struct {
    kernel_irq_handler_t handler;
    void *context;
} irq_registration_t;

/* SGI/PPI callbacks and hardware registers are CPU-banked. SPIs remain routed
 * exclusively to CPU 0; changing global SPI ownership is not supported yet. */
static irq_registration_t private_handlers[BOOT_MAX_CPUS][GIC_PRIVATE_INTERRUPTS];
static irq_registration_t shared_handlers[GIC_MAX_INTERRUPTS - GIC_PRIVATE_INTERRUPTS];
static unsigned int interrupt_count;
static bool controller_ready;

/* The MMU maps these pages as Device-nGnRnE, not cached RAM. */
static volatile unsigned int *gic_register(unsigned long base, unsigned int offset) {
    return (volatile unsigned int *)(base + offset);
}

/* Complete source/device writes before enabling IRQs or deactivating a source. */
static void gic_barrier(void) {
    __asm__ volatile("dsb sy; isb" ::: "memory");
}

/* Select CPU-local private callbacks, or the CPU-0-owned shared interrupt slot. */
static irq_registration_t *registration_for(unsigned int interrupt_id) {
    return interrupt_id < GIC_PRIVATE_INTERRUPTS
               ? &private_handlers[this_cpu()->cpu_id][interrupt_id]
               : &shared_handlers[interrupt_id - GIC_PRIVATE_INTERRUPTS];
}

/* Initialize only this CPU's banked distributor state and CPU interface. Never
 * reset another CPU's PPI enables, callbacks, or the global distributor here. */
bool arm64_interrupt_initialize_local(void) {
    if (!__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE) || kernel_interrupt_depth()) {
        return false;
    }

    bool saved_interrupts = kernel_irq_save();
    *gic_register(GIC_CPU_INTERFACE, GICC_CONTROL) = 0;
    *gic_register(GIC_DISTRIBUTOR, GICD_ENABLE_CLEAR) = ~0U;
    *gic_register(GIC_DISTRIBUTOR, GICD_PENDING_CLEAR) = ~0U;
    *gic_register(GIC_DISTRIBUTOR, GICD_GROUP) = 0;

    for (unsigned int interrupt_base = 0; interrupt_base < GIC_PRIVATE_INTERRUPTS;
        interrupt_base += GIC_REGISTER_BYTES) {
        *gic_register(GIC_DISTRIBUTOR, GICD_PRIORITY + interrupt_base) = GIC_DEFAULT_PRIORITIES;
    }
    *gic_register(GIC_DISTRIBUTOR, GICD_PRIVATE_CONFIGURATION) &= ~GIC_TIMER_CONFIGURATION_MASK;
    this_cpu()->gic_target_mask = *gic_register(GIC_DISTRIBUTOR, GICD_TARGET) & GIC_BYTE_MASK;

    /* A uniprocessor GIC may implement ITARGETSR as RAZ/WI. Its sole CPU is
     * still target bit zero; do not apply this fallback to a multi-CPU GIC. */
    if (!this_cpu()->gic_target_mask &&
        ((*gic_register(GIC_DISTRIBUTOR, GICD_TYPE) >> GIC_TYPE_CPU_SHIFT) & GIC_TYPE_CPU_MASK) ==
            0) {
        this_cpu()->gic_target_mask = 1;
    }
    *gic_register(GIC_CPU_INTERFACE, GICC_PRIORITY_MASK) = GIC_BYTE_MASK;
    *gic_register(GIC_CPU_INTERFACE, GICC_BINARY_POINT) = 0;
    *gic_register(GIC_CPU_INTERFACE, GICC_CONTROL) = 1;
    gic_barrier();

    bool valid = this_cpu()->gic_target_mask != 0;
    kernel_irq_restore(saved_interrupts);

    return valid;
}

/* CPU 0 establishes global SPI configuration once, then its local interface. */
bool kernel_interrupt_initialize(void) {
    if (__atomic_load_n(&controller_ready, __ATOMIC_RELAXED) || this_cpu()->cpu_id != 0) {
        return false;
    }

    bool saved_interrupts = kernel_irq_save();
    *gic_register(GIC_DISTRIBUTOR, GICD_CONTROL) = 0;
    interrupt_count = ((*gic_register(GIC_DISTRIBUTOR, GICD_TYPE) & GIC_TYPE_LINES_MASK) + 1) *
                      GIC_PRIVATE_INTERRUPTS;
    if (interrupt_count > GIC_MAX_INTERRUPTS) {
        interrupt_count = GIC_MAX_INTERRUPTS;
    }

    for (unsigned int interrupt_base = GIC_PRIVATE_INTERRUPTS; interrupt_base < interrupt_count;
        interrupt_base += GIC_PRIVATE_INTERRUPTS) {
        unsigned int bank_offset = (interrupt_base / GIC_PRIVATE_INTERRUPTS) * GIC_REGISTER_BYTES;
        *gic_register(GIC_DISTRIBUTOR, GICD_ENABLE_CLEAR + bank_offset) = ~0U;
        *gic_register(GIC_DISTRIBUTOR, GICD_PENDING_CLEAR + bank_offset) = ~0U;
        *gic_register(GIC_DISTRIBUTOR, GICD_GROUP + bank_offset) = 0;
    }

    for (unsigned int interrupt_base = GIC_PRIVATE_INTERRUPTS; interrupt_base < interrupt_count;
        interrupt_base += GIC_REGISTER_BYTES) {
        *gic_register(GIC_DISTRIBUTOR, GICD_PRIORITY + interrupt_base) = GIC_DEFAULT_PRIORITIES;
        *gic_register(GIC_DISTRIBUTOR, GICD_TARGET + interrupt_base) = GIC_BOOT_CPU_TARGETS;
    }

    /* secure=off uses Group 0 consistently with IAR/EOIR. */
    *gic_register(GIC_DISTRIBUTOR, GICD_CONTROL) = 1;
    gic_barrier();
    __atomic_store_n(&controller_ready, true, __ATOMIC_RELEASE);

    bool valid = arm64_interrupt_initialize_local();
    kernel_irq_restore(saved_interrupts);

    return valid;
}

/* Normal mutations mask local IRQs; callbacks belong to the current CPU. */
bool kernel_irq_register(unsigned int interrupt_id, kernel_irq_handler_t handler, void *context) {
    if (!__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE) || interrupt_id >= interrupt_count ||
        !handler || kernel_interrupt_depth() ||
        (interrupt_id >= GIC_PRIVATE_INTERRUPTS && this_cpu()->cpu_id != 0)) {
        return false;
    }

    bool saved_interrupts = kernel_irq_save();
    irq_registration_t *registration = registration_for(interrupt_id);

    if (registration->handler) {
        kernel_irq_restore(saved_interrupts);

        return false;
    }
    *registration = (irq_registration_t){handler, context};
    *gic_register(GIC_DISTRIBUTOR,
        GICD_ENABLE_SET + (interrupt_id / GIC_PRIVATE_INTERRUPTS) * GIC_REGISTER_BYTES) =
        1U << (interrupt_id % GIC_PRIVATE_INTERRUPTS);
    gic_barrier();
    kernel_irq_restore(saved_interrupts);

    return true;
}

/* Private callback removal is local. SPIs may be removed only by CPU 0. */
bool kernel_irq_unregister(unsigned int interrupt_id) {
    if (!__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE) || interrupt_id >= interrupt_count ||
        kernel_interrupt_depth() ||
        (interrupt_id >= GIC_PRIVATE_INTERRUPTS && this_cpu()->cpu_id != 0)) {
        return false;
    }

    bool saved_interrupts = kernel_irq_save();
    irq_registration_t *registration = registration_for(interrupt_id);

    if (!registration->handler) {
        kernel_irq_restore(saved_interrupts);

        return false;
    }
    *gic_register(GIC_DISTRIBUTOR,
        GICD_ENABLE_CLEAR + (interrupt_id / GIC_PRIVATE_INTERRUPTS) * GIC_REGISTER_BYTES) =
        1U << (interrupt_id % GIC_PRIVATE_INTERRUPTS);
    *gic_register(GIC_DISTRIBUTOR,
        GICD_PENDING_CLEAR + (interrupt_id / GIC_PRIVATE_INTERRUPTS) * GIC_REGISTER_BYTES) =
        1U << (interrupt_id % GIC_PRIVATE_INTERRUPTS);
    if (interrupt_id < GIC_SOFTWARE_INTERRUPTS) {
        *gic_register(
            GIC_DISTRIBUTOR, GICD_SOFTWARE_PENDING_CLEAR +
                                 (interrupt_id / GIC_REGISTER_BYTES) * GIC_REGISTER_BYTES) =
            GIC_BYTE_MASK << ((interrupt_id % GIC_REGISTER_BYTES) * 8);
    }

    gic_barrier();
    *registration = (irq_registration_t){0};
    kernel_irq_restore(saved_interrupts);

    return true;
}

/* Publish RAM state before the remote interrupt, using the discovered one-hot
 * GIC target mask rather than assuming logical IDs equal hardware target bits. */
bool kernel_irq_send_to(cpu_id_t target_cpu, unsigned int interrupt_id) {
    if (!__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE) || target_cpu >= coreCount ||
        interrupt_id >= GIC_SOFTWARE_INTERRUPTS ||
        !__atomic_load_n(&cpu_core_local[target_cpu].online, __ATOMIC_ACQUIRE)) {
        return false;
    }

    unsigned int target_mask = cpu_core_local[target_cpu].gic_target_mask;
    __asm__ volatile("dsb ishst" ::: "memory");
    *gic_register(GIC_DISTRIBUTOR, GICD_SOFTWARE_GENERATE) =
        (target_mask << GIC_SGI_TARGET_SHIFT) | interrupt_id;
    gic_barrier();

    return true;
}

/* Runtime self-SGI validation also runs before CPU discovery publishes online. */
bool kernel_irq_send_self(unsigned int interrupt_id) {
    if (!__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE) ||
        interrupt_id >= GIC_SOFTWARE_INTERRUPTS || !registration_for(interrupt_id)->handler) {
        return false;
    }
    *gic_register(GIC_DISTRIBUTOR, GICD_SOFTWARE_GENERATE) = GIC_SGI_SELF | interrupt_id;
    gic_barrier();

    return true;
}

/* IRQ depth belongs to the current CPU, not to a suspended thread's stack. */
unsigned long kernel_interrupt_depth(void) {
    core_t *cpu = this_cpu();

    return cpu ? __atomic_load_n(&cpu->interrupt_depth, __ATOMIC_RELAXED) : 0;
}

/* Finish the GIC transaction before allowing the scheduler to change stacks. */
void arm64_exception_dispatch(arm64_exception_frame_t *exception_frame) {
    if (exception_frame->vector_index == ARM64_VECTOR_USER_SYNC) {
        arm64_user_exception(exception_frame);

        return;
    }

    if (exception_frame->vector_index == ARM64_VECTOR_USER_IRQ && this_cpu()->current_thread &&
        this_cpu()->current_thread->user_entry) {
        __atomic_fetch_add(
            &this_cpu()->current_thread->parent->user_interrupts, 1, __ATOMIC_RELAXED);
    }

    if ((exception_frame->vector_index != ARM64_VECTOR_KERNEL_IRQ &&
            exception_frame->vector_index != ARM64_VECTOR_USER_IRQ) ||
        !__atomic_load_n(&controller_ready, __ATOMIC_ACQUIRE)) {
        arm64_exception_report(exception_frame);
    }

    core_t *cpu = this_cpu();
    __atomic_fetch_add(&cpu->interrupt_depth, 1, __ATOMIC_RELAXED);

    unsigned int acknowledge = *gic_register(GIC_CPU_INTERFACE, GICC_ACKNOWLEDGE);
    unsigned int interrupt_id = acknowledge & GIC_ACKNOWLEDGE_ID_MASK;

    if (interrupt_id < GIC_MAX_INTERRUPTS) {
        irq_registration_t *registration =
            interrupt_id < interrupt_count ? registration_for(interrupt_id) : NULL;
        bool handled = registration && registration->handler;

        if (handled) {
            registration->handler(registration->context);
        } else {
            *gic_register(GIC_DISTRIBUTOR,
                GICD_ENABLE_CLEAR + (interrupt_id / GIC_PRIVATE_INTERRUPTS) * GIC_REGISTER_BYTES) =
                1U << (interrupt_id % GIC_PRIVATE_INTERRUPTS);
        }

        gic_barrier();
        *gic_register(GIC_CPU_INTERFACE, GICC_END_INTERRUPT) = acknowledge;
        gic_barrier();

        if (!handled) {
            arm64_exception_report(exception_frame);
        }
    }

    __atomic_fetch_sub(&cpu->interrupt_depth, 1, __ATOMIC_RELAXED);
    scheduler_interrupt_exit();
}
