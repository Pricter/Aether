#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/dlist.h>

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
} __attribute__((packed));

struct acpi_common_header {
	uint8_t sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t oemId[6];
	uint8_t oemTableId[8];
	uint32_t oemRevision;
	uint8_t creatorId[4];
	uint8_t creatorRevision[4];
} __attribute__((packed));

struct rsdt {
	struct acpi_common_header hdr;
	char data[];
} __attribute__((packed));

struct madt {
	struct acpi_common_header hdr;
	uint32_t lapic_address;
	uint32_t flags;
	char entries[];
} __attribute__((packed));

struct madt_header {
	uint8_t id;
	uint8_t length;
} __attribute__((packed));

struct madt_lapic {
	struct madt_header hdr;
	uint8_t acpi_processor_id;
	uint8_t apic_id;
	uint32_t flags;
};

struct madt_ioapic {
	struct madt_header hdr;
	uint8_t ioAPICId;
	uint8_t reserved;
	uint32_t ioAPICAddress;
	uint32_t systemIntBase;
};

struct madt_ioapic_so {
	struct madt_header hdr;
	uint8_t busSource;
	uint8_t irqSource;
	uint32_t gsi;
	uint16_t flags;
};

struct madt_ioapic_nmi {
	struct madt_header hdr;
	uint8_t nmiSource;
	uint8_t reserved;
	uint16_t flags;
	uint32_t gsi;
};

struct madt_lapic_nmi {
	struct madt_header hdr;
	uint8_t acpi_processor_id;
	uint16_t flags;
	uint8_t lint;
};

struct madt_lapic_new {
	struct madt_header hdr;
	uint16_t reserved;
	uint64_t lapicAddress;
};

extern dlist_node_t* madt_lapic;
extern dlist_node_t* madt_ioapic;
extern dlist_node_t* madt_ioapic_so;
extern dlist_node_t* madt_ioapic_nmi;
extern dlist_node_t* madt_lapic_nmi;