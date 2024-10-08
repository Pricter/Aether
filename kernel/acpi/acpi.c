/**
 * acpi.c: ACPI Headers and tables
 * 
 * Used for accessing ACPI headers and tables
 */

#include <stdint.h>
#include <stddef.h>
#include <kernel/mmu.h>
#include <limine.h>
#include <kernel/kprintf.h>
#include <kernel/acpi.h>
#include <memory.h>
#include <kernel/misc.h>
#include <kernel/dlist.h>
#include <stdbool.h>
#include <kernel/cpu.h>
#include <kernel/macros.h>

/* Request Limine for RSDP address */
__attribute__((used, section(".requests")))
static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0,
};

/* Lists of lapic, ioapic, ioapic_so, ioapic_nmi and lapic_nmi */
node_t* madt_lapic = NULL;
node_t* madt_ioapic = NULL;
node_t* madt_ioapic_so = NULL;
node_t* madt_ioapic_nmi = NULL;
node_t* madt_lapic_nmi = NULL;

struct rsdp_structure* rsdp = NULL;
struct rsdt* rsdt = NULL;
uint64_t lapic_address = 0;

char* acpiTableAddresses = NULL;

/* Check if XSDT can be used */
static inline int using_xsdt(void) {
	return rsdp->revision >= 2 && rsdp->xsdtAddr != 0;
}

/* Find ACPI table based on 4 character signature */
struct acpi_common_header* acpi_find_table(char t_sig[static 4]) {
	size_t entry_count = (rsdt->hdr.length - sizeof(struct acpi_common_header)) / (using_xsdt() ? 8 : 4);
	for(size_t i = 0; i < entry_count; i++) {
		struct acpi_common_header *hdr = NULL;
		if (using_xsdt()) {
            hdr = (struct acpi_common_header*)(*((uint64_t*)acpiTableAddresses + i) + HHDM_HIGHER_HALF);
        } else {
            hdr = (struct acpi_common_header*)(*((uint32_t*)acpiTableAddresses + i) + HHDM_HIGHER_HALF);
        }
		if(!memcmp(t_sig, hdr->sig, 4)) return hdr;
	}

	kprintf("acpi: Could not find table \"");
	for(int i = 0; i < 4; i++) kprintf("%c", t_sig[i]);
	kprintf("\"\n");
	panic("Could not find the required header", NULL);
	return NULL;
}

/* Check if header exists based on 4 character signature */
bool acpi_exists(char t_sig[static 4]) {
	size_t entry_count = (rsdt->hdr.length - sizeof(struct acpi_common_header)) / (using_xsdt() ? 8 : 4);
	for(size_t i = 0; i < entry_count; i++) {
		struct acpi_common_header *hdr = NULL;
		if (using_xsdt()) {
            hdr = (struct acpi_common_header*)(*((uint64_t*)acpiTableAddresses + i) + HHDM_HIGHER_HALF);
        } else {
            hdr = (struct acpi_common_header*)(*((uint32_t*)acpiTableAddresses + i) + HHDM_HIGHER_HALF);
        }
		if(!memcmp(t_sig, hdr->sig, 4)) return 1;
	}

	return 0;
}

struct fadt *fadt = NULL;

bool apic_enabled;

/* Initialize ACPI, Find and add all lapic, ioapic, ioiapic_so, lapic_nmi, ioapic_nmi to their respective lists */
void __init acpi_init(void) {
	madt_lapic = dlist_create_empty();
	madt_ioapic = dlist_create_empty();
	madt_ioapic_so = dlist_create_empty();
	madt_ioapic_nmi = dlist_create_empty();
	madt_lapic_nmi = dlist_create_empty();

	/* System has no ACPI, panic because we cant access some crucial tables */
	if(rsdp_request.response->address == NULL) panic("System has no ACPI", NULL);
	rsdp = (struct rsdp_structure*)(rsdp_request.response->address);
	kprintf("acpi: RDSP structure located at %p, signature: \"", rsdp);
	for(int i = 0; i < 8; i++) kprintf("%c", rsdp->sig[i]);
	kprintf("\"\n");	

	if(using_xsdt()) {
		rsdt = (struct rsdt*)(rsdp->xsdtAddr + HHDM_HIGHER_HALF);
	} else {
		rsdt = (struct rsdt*)(rsdp->rsdtAddr + HHDM_HIGHER_HALF);
	}

	kprintf("acpi: Using XSDT? %s\n", using_xsdt() ? "true" : "false");
	kprintf("acpi: System Descriptor Table: %p\n", rsdt);

	acpiTableAddresses = malloc((rsdt->hdr.length - sizeof(struct acpi_common_header)) + 8);
	acpiTableAddresses = (char*)align_pointer((void*)acpiTableAddresses, using_xsdt() ? 8 : 4);
	memcpy(acpiTableAddresses, rsdt->data, (rsdt->hdr.length - sizeof(struct acpi_common_header)));

	struct madt* madt = (struct madt*)acpi_find_table("APIC");
	lapic_address = madt->lapic_address + HHDM_HIGHER_HALF;
	kprintf("acpi: Local APIC address: %p\n", lapic_address);
	if(madt == NULL) panic("System has no MADT structure\n", NULL);

	uint64_t offset = 0;
	for(;;) {
		if(madt->hdr.length - sizeof(struct madt) - offset < 2) {
			break;
		}

		struct madt_header* header = (struct madt_header*)(madt->entries + offset);

		switch(header->id) {
		case 0:
			apic_enabled = (((struct madt_lapic*)header)->flags & 1) != 0;
			bool online_capable = (((struct madt_lapic*)header)->flags >> 1) & 1;
			kprintf("acpi: Found local APIC #%d, { enabled: %s, online-capable: %s }\n",
				dlist_get_length(madt_lapic),
				apic_enabled ? "true" : "false", online_capable ? "true" : "false");
			dlist_push(madt_lapic, (struct madt_lapic*)header);
			break;
		case 1:
			kprintf("acpi: Found IOAPIC #%d at %p\n", dlist_get_length(madt_ioapic),
				((struct madt_ioapic*)header)->ioAPICAddress + HHDM_HIGHER_HALF);
			mmu_map_page(mmu_kernel_pagemap, (((struct madt_ioapic*)header)->ioAPICAddress + HHDM_HIGHER_HALF), ((struct madt_ioapic*)header)->ioAPICAddress, PTE_PRESENT | PTE_WRITABLE);
			dlist_push(madt_ioapic, (struct madt_ioapic*)header);
			break;
		case 2:
			kprintf("acpi: Found IOAPIC source override #%lu, IRQ #%lu -> #%lu\n",
				dlist_get_length(madt_ioapic_so), ((struct madt_ioapic_so*)header)->irqSource,
				((struct madt_ioapic_so*)header)->gsi);
			dlist_push(madt_ioapic_so, (struct madt_ioapic_so*)header);
			break;
		case 3:
			kprintf("acpi: Found IOAPIC NMI #%lu\n", dlist_get_length(madt_ioapic_nmi));
			dlist_push(madt_ioapic_nmi, (struct madt_ioapic_nmi*)header);
			break;
		case 4:
			kprintf("acpi: Found local APIC NMI #%lu\n", dlist_get_length(madt_lapic_nmi));
			dlist_push(madt_lapic_nmi, (struct madt_lapic_nmi*)header);
			break;
		case 5:
			lapic_address = ((struct madt_lapic_new*)header)->lapicAddress + HHDM_HIGHER_HALF;
			kprintf("acpi: Found new local APIC address at %p\n", lapic_address);
		}

		offset += MAX(header->length, 2);
	}

	fadt = (struct fadt*)acpi_find_table("FACP");
}