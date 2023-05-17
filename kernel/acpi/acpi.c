#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <kernel/acpi.h>

static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0,
};

/* System descriptor tables */
rsdt_t* rsdt = NULL;
xsdt_t* xsdt = NULL;

bool usingXsdt = false;

void acpi_init(void) {
	struct limine_rsdp_response* rsdp_response = rsdp_request.response;
	rsdp_t* rsdp = rsdp_response->address;

	kprintf("acpi: OEMID: \"%s\"\n", rsdp->oemId);
	kprintf("acpi: Using XSDT? %s\n", (rsdp->revision == 2) ? "true" : "false");

	if(rsdp->revision == 2) {
		xsdt = rsdp->xsdtAddress;
		rsdt = rsdp->rsdtAddress;
		usingXsdt = true;
	} else {
		rsdt = rsdp->rsdtAddress;
		usingXsdt = false;
	}

	kprintf("acpi: SDT Length: %d\n", usingXsdt ? xsdt->length : rsdt->length);
	kprintf("acpi: Creator ID: \"%s\"\n",
		usingXsdt ? xsdt->creatorId : rsdt->creatorId);
	
	uint64_t entry_count = usingXsdt ?
		((xsdt->length - sizeof(xsdt_t)) / 8) :
		((rsdt->length - sizeof(rsdp_t)) / 4);

	kprintf("acpi: Entry count: %lu\n", entry_count);
}