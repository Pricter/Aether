#pragma once

#include <stdint.h>

typedef struct rsdp {
	/**
	 * “RSD PTR ” (Notice that this signature must contain a trailing blank
	 * character.)
	*/
	char signature[8];

	/**
	 * This is the checksum of the fields defined in the ACPI 1.0
	 * specification. This includes only the first 20 bytes of this table, bytes
	 * 0 to 19, including the checksum field. These bytes must sum to
	 * zero.
	*/
	char checksum;

	/* An OEM-supplied string that identifies the OEM. */
	char oemId[6];

	/**
	 * The revision of this structure. Larger revision numbers are backward
     * compatible to lower revision numbers. The ACPI version 1.0
	 * revision number of this table is zero. The ACPI version 1.0 RSDP
	 * Structure only includes the first 20 bytes of this table, bytes 0 to 19.
	 * It does not include the Length field and beyond. The current value
	 * for this field is 2.
	*/
	char revision;

	/* 32 bit physical address of the RSDT */
	uint32_t rsdtAddress;

	/**
	 * The length of the table, in bytes, including the header, starting from
	 * offset 0. This field is used to record the size of the entire table. This
	 * field is not available in the ACPI version 1.0 RSDP Structure
	*/
	uint32_t length;

	/* 64 bit physical address of the XSDT */
	uint64_t xsdtAddress;

	/* Checksum of the entire table, including both checksum fields */
	char extendedChecksum;

	/* Reserved */
	char reserved[3];
} rsdp_t;

typedef struct rsdt {
	/* RSDT Signature for the "Root System Description Table" */
	char signature[4];

	/* Length, in bytes, of the entire RSDT, implies the number of entry fields */
	uint32_t length;

	/* 1 */
	uint8_t revision;

	/* Entire table sum to zero */
	uint8_t checksum;

	/* OEM Id */
	char oemId[6];

	/* For the RSDT, the table ID is the manufacture model ID */
	char oemTableId[8];

	/* OEM revision of the RSDT table for supplied OEM Table ID */
	uint32_t oemRevision;

	/* Vendor ID of the utility that created the table */
	char creatorId[4];

	/* Revision of utility that created the table */
	uint32_t creatorRevision;

	/* Array of 32-bit physical addresses that point to other DESCRIPTION_HEADERs */
	uint32_t entries[];
} rsdt_t;

typedef struct xsdt {
	/* ‘XSDT’. Signature for the Extended System Description Table. */
	char signature[4];

	/**
	 * Length, in bytes, of the entire table. The length implies the
	 * number of Entry fields (n) at the end of the table.
	*/
	uint32_t length;

	/* 1 */
	char revision;

	/* Entire table must sum to zero. */
	char checksum;

	/* OEM Id */
	char oemId[6];

	/**
	 * For the XSDT, the table ID is the manufacture model ID. This
	* field must match the OEM Table ID in the FADT.
	*/
	char oemTableId[8];

	uint32_t oemRevision;

	/**
	 * Vendor ID of utility that created the table. For tables containing
	 * Definition Blocks, this is the ID for the ASL Compiler
	*/
	char creatorId[4];

	/**
	 * Revision of utility that created the table. For tables containing
	 * Definition Blocks, this is the revision for the ASL Compiler.
	*/
	uint32_t creatorRevision;

	uint64_t entry[];
} xsdt_t;

void acpi_init(void);