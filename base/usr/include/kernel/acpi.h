#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/dlist.h>

/**
 * \struct rsdp_structure
 * \brief RSDP Structure giving the address of RSDT and XSDT
 * 
 * This structure is given to the kernel by limine, it has
 * a signature for verifying the structure and the addresses
 * of both RSDT and XSDT. The XSDT is only available when
 * \var revision is equal to or more than 2.
*/
struct rsdp_structure {
	uint8_t sig[8]; /*!< Signature for verification, contains "RSD PTR " with a space at the end */
	uint8_t checksum; /*!< A checksum, all fields must add to 0 */
	uint8_t oemId[6]; /*!< An OEM-Supplied stringn that identifies the OEM */
	uint8_t revision; /*!< Revision of the structure, Larger revisions are backward compatilble */
	uint32_t rsdtAddr; /*!< 32-bit physical address of the RSDT */
	uint32_t length; /*!< The length of the table in bytes, including the header starting from offset 0 */
	uint64_t xsdtAddr; /*!< 64-bit physical address of the XSDT */
	uint8_t xchecksum; /*!< Checksum of the entire table, including both checksum fields */
	uint8_t reserved[3]; /*!< Reserved field */
} __attribute__((packed));

/**
 * \struct acpi_common_header
 * \brief Common header for all the ACPI tables
 * 
 * Since all the ACPI tables share the same header signature
 * it makes more sense to have a common header struct in a
 * table rather than redefining all the header fields again
*/
struct acpi_common_header {
	uint8_t sig[4]; /*!< Signature to find what table this is, the signature depends on the table */
	uint32_t length; /*!< The length of the table in bytes, including the header, starting from offset 0 */
	uint8_t revision; /*!< The revision of the structure, larger revisions are backwards compatible */
	uint8_t checksum; /*!< The entire table including the checksum field, must add to zero */
	uint8_t oemId[6]; /*!< An OEM-Supplied string that identifies the OEM */
	uint8_t oemTableId[8]; /*!< An OEM-Supplied string that the OEM uses to identity the data table */
	uint32_t oemRevision; /*!< An OEM-Supplied revision number. Larger numbers are assumed to be newer versions */
	uint8_t creatorId[4]; /*!< Vendor ID of the utility that created the table  */
	uint8_t creatorRevision[4]; /*!< Revision of the utility that created the table */
} __attribute__((packed));

/**
 * \struct rsdt
 * \brief RSDT structure that contains all other table's addresses
 * 
 * The RSDT Structure contains all other tables' physical addresses
 * The RSDT and the XSDT are defined the same with a char data[] at
 * the end of it. The structures can be distinguished by checking
 * if the revision field in the RSDP is greater than or equal to 2
*/
struct rsdt {
	struct acpi_common_header hdr; /*!< ACPI Common header */
	char data[]; /*!< Physical addresses of the tables, 32bit for RSDT and 64bit for XSDT */
} __attribute__((packed));

/**
 * \struct madt
 * \brief The MADT table header
 * 
 * MADT is used to get all the local and io apics on the system
*/
struct madt {
	struct acpi_common_header hdr; /*!< ACPI Common header */
	uint32_t lapic_address; /*!< Local APIC address, can be overriden by a MADT entry */
	uint32_t flags; /*!< Bit 1: Indicated that the system has a PC-AT-compatible dual-8259 setup */
	char entries[]; /*!< MADT local APIC, IO APIC and APIC NMI entries */
} __attribute__((packed));

/**
 * \struct madt_header
 * \brief A common header for all madt entries
 * 
 * All madt headers start with this header
*/
struct madt_header {
	uint8_t id; /*!< A number for identification of the entry */
	uint8_t length; /*!< Length of the entry */
} __attribute__((packed));

/**
 * \struct madt_lapic
 * \brief Defines a local APIC entry in the madt
*/
struct madt_lapic {
	struct madt_header hdr; /*!< madt common header */
	uint8_t acpi_processor_id; /*!< The processor id */
	uint8_t apic_id; /*!< APIC Id, every core has a different id */
	uint32_t flags; /*!< Bit0: Enabled, Bit1: Online Capable*/
} __attribute__((packed));

/**
 * \struct madt_ioapic
 * \brief Defines a madt IO APIC entry
*/
struct madt_ioapic {
	struct madt_header hdr; /*!< madt common header */
	uint8_t ioAPICId; /*!< APIC ID, for identification of which apic to use */
	uint8_t reserved; /*!< Reserved*/
	uint32_t ioAPICAddress; /*!< The base MMIO IO APIC Address */
	uint32_t systemIntBase; /*!< System interrupt base */
} __attribute__((packed));

/**
 * \struct madt_ioapic_so
 * \brief Defines a madt IO APIC Source override entry
*/
struct madt_ioapic_so {
	struct madt_header hdr; /*!< madt common header */
	uint8_t busSource; /*!< Constant, meaning ISA */
	uint8_t irqSource; /*!< Bus-relative interrut source (IRQ) */
	uint32_t gsi; /*!< The Global System Interrupt that this bus-relative interrupt source will signal */
	uint16_t flags; /*!< See https://uefi.org/sites/default/files/resources/ACPI_6_2.pdf#G10.1360422 for more info */
} __attribute__((packed));

/**
 * \struct madt_ioapic_nmi
 * \brief Defines a madt IO APIC Non-maskable-interrupt entry
*/
struct madt_ioapic_nmi {
	struct madt_header hdr; /*< madt common header */
	uint8_t nmiSource; /*!< Interrupt source */
	uint8_t reserved; /*!< Reserved */
	uint16_t flags; /*!< Same as MPS INTI flags */
	uint32_t gsi; /*!<  The Global System Interrupt that this NMI will signal */
} __attribute__((packed));

/**
 * \struct madt_lapic_nmi
 * \brief Defiend a madt local APIC Non-maskable-interrupt entry
*/
struct madt_lapic_nmi {
	struct madt_header hdr; /*!< madt common header */
	uint8_t acpi_processor_id; /*!< Processor ID */
	uint16_t flags; /*!< See https://uefi.org/sites/default/files/resources/ACPI_6_2.pdf#G10.1360422 for more info */
	uint8_t lint; /*!< Local APIC interrupt input LINTn to which NMI is connected */
} __attribute__((packed));

/**
 * \struct madt_lapic_new
 * \brief Defines an address override to the local apic structure
*/
struct madt_lapic_new {
	struct madt_header hdr; /*!< madt common header */
	uint16_t reserved; /*!< Reserved */
	uint64_t lapicAddress; /*!< New local APIC id */
} __attribute__((packed));

extern dlist_node_t* madt_lapic;
extern dlist_node_t* madt_ioapic;
extern dlist_node_t* madt_ioapic_so;
extern dlist_node_t* madt_ioapic_nmi;
extern dlist_node_t* madt_lapic_nmi;
extern uint64_t lapic_address;