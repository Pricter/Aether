#include <stdint.h>
#include <stddef.h>
#include <kernel/mmu.h>
#include <limine.h>
#include <kernel/kprintf.h>

static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0,
};

struct rsdp_structure {
	uint8_t sig[8];
	uint8_t checksum;
	uint8_t oemId[6];
	uint8_t revision;
	uint32_t rsdtAddr;
	uint32_t length;
	uint64_t xsdtAddr;
	uint8_t xchecksum;
	uint8_t reserved[3];
};

struct rsdt {
	uint8_t sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t oemId[6];
	uint8_t oemTableId[8];
	uint32_t oemRevision;
	uint8_t creatorId[4];
	uint8_t creatorRevision[4];
	uint32_t entries[];
};

struct xsdt {
	uint8_t sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t oemId[6];
	uint8_t oemTableId[8];
	uint32_t oemRevision;
	uint8_t creatorId[4];
	uint8_t creatorRevision[4];
	uint64_t entries[];
};

struct rsdp_structure* rsdp = NULL;

struct rsdt* rsdt = NULL;
struct xsdt* xsdt = NULL;

static inline int using_xsdt(void) {
	return rsdp->revision >= 2 && rsdp->xsdtAddr != 0;
}

void acpi_init(void) {
	rsdp = (struct rsdp_structure*)(rsdp_request.response->address);
	// TODO: Panic if no acpi
	kdprintf("acpi: RDSP structure located at %p, signature: \"");
	for(int i = 0; i < 8; i++) kdprintf("%c", rsdp->sig[i]);
	kdprintf("\"\n");

	rsdt = (struct rsdt*)(rsdp->rsdtAddr + HHDM_HIGHER_HALF);
	if(using_xsdt()) {
		xsdt = (struct xsdt*)(rsdp->xsdtAddr + HHDM_HIGHER_HALF);
	}

	kprintf("acpi: Using XSDT? %s\n", using_xsdt() ? "true" : "false");
	kprintf("acpi: RSDT: %p, XSDT: %p\n", rsdt, xsdt);
	kprintf("acpi: RSDT Sig: \"");
	for(int i = 0; i < 4; i++) kprintf("%c", rsdt->sig[i]);
	kprintf("\"");
	if(using_xsdt()) {
		kprintf(", XSDT Sig: \"");
		for(int i = 0; i < 4; i++) kprintf("%c", xsdt->sig[i]);
		kprintf("\"\n");
	}
	kprintf("\n");
}