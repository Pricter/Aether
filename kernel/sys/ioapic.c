/* IOAPIC file */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/acpi.h>
#include <kernel/apic.h>
#include <kernel/dlist.h>
#include <kernel/kprintf.h>

/**
 * ioapic_read: Read from an IOAPIC register
 * 
 * @param ioapic: The IOAPIC whose register is to be read
 * @param reg: The register of the IOAPIC to read from
 */
uint32_t ioapic_read(struct madt_ioapic* ioapic, uint32_t reg) {
	/* To read something we have to put the register index in IOREGSEL */
	uint64_t base = (uint64_t)ioapic->ioAPICAddress + HHDM_HIGHER_HALF;
	*(volatile uint32_t*)base = reg;
	
	/* And then read IOREGWIN */
	return *(volatile uint32_t*)(base + 16);
}

/**
 * ioapic_write: Write to an IOAPIC register
 * 
 * @param ioapic: The IOAPIC whose register is to be written
 * @param reg: The register of the IOAPIC to write to
 * @param value: The value to write to the IOAPIC register
 */
void ioapic_write(struct madt_ioapic* ioapic, uint32_t reg, uint32_t value) {
    /* To write something we have to put the register index in IOREGSEL */
	uint64_t base = (uint64_t)ioapic->ioAPICAddress + HHDM_HIGHER_HALF;
	*(volatile uint32_t*)base = reg;

    /* Then write to IOREGWIN */
	*(volatile uint32_t*)(base + 16) = value;
}

/* Get GSI count of the IOAPIC */
static uint64_t ioapic_gsi_count(struct madt_ioapic *io_apic) {
    return (ioapic_read(io_apic, 1) & 0xff0000) >> 16;
}

/* Get IOAPIC from GSI */
static struct madt_ioapic *ioapic_from_gsi(uint32_t gsi) {
    for (uint64_t i = 0; i < dlist_get_length(madt_ioapic); i++) {
        struct madt_ioapic *ioapic = dlist_get(madt_ioapic, i);
        if (gsi >= ioapic->systemIntBase && gsi < ioapic->systemIntBase + ioapic_gsi_count(ioapic)) {
            return ioapic;
        }
    }

    return NULL;
}

/**
 * ioapic_irq_redirect: Setup an irq redirect
 * 
 * @param lapic_id: Which lapic's irq is to be redirected
 * @param vector: Interrupt on which vector is to be redirected
 * @param irq: The irq to redirect to
 */
void ioapic_irq_redirect(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status) {
    for (size_t i = 0; i < dlist_get_length(madt_ioapic_so); i++) {
        struct madt_ioapic_so *iso = dlist_get(madt_ioapic_so, i);
        if (iso->irqSource != irq) {
            continue;
        }

        ioapic_set_gsi_redirect(lapic_id, vector, iso->gsi, iso->flags, status);
        return;
    }

    ioapic_set_gsi_redirect(lapic_id, vector, irq, 0, status);
}

void ioapic_set_gsi_redirect(uint32_t lapic_id, uint8_t vector, uint8_t gsi,
                              uint16_t flags, bool status) {
    struct madt_ioapic *ioapic = ioapic_from_gsi(gsi);

    uint64_t redirect = vector;
    if ((flags & (1 << 1)) != 0) {
        redirect |= (1 << 13);
    }

    if ((flags & (1 << 3)) != 0) {
        redirect |= (1 << 15);
    }

    if (!status) {
        redirect |= (1 << 16);
    }

    redirect |= (uint64_t)lapic_id << 56;

    uint32_t ioredirect_table = 0x10 + (gsi * 2);
    ioapic_write(ioapic, ioredirect_table, (uint32_t)redirect);
    ioapic_write(ioapic, ioredirect_table + 1, (uint32_t)(redirect >> 32));
}