#include <stdint.h>
#include <kernel/acpi.h>

struct hpet {
	struct acpi_common_header hdr;
	uint8_t hardwareRevisionId;
	uint8_t comparatorsCount : 5;
	uint8_t counterSize : 1;
	uint8_t reserved0 : 1;
	uint8_t legacyIRQCapable : 1;
	uint16_t pciVendorId;
	uint8_t addressSpaceId;
	uint8_t register_bit_width;
	uint8_t register_bit_offset;
	uint8_t reserved1;
	uint64_t address;
	uint8_t hpetNumber;
	uint16_t minimumTick;
	uint8_t pageProtection;
} __attribute__((packed));

void hpet_init(void);
void hpet_sleep(uint64_t us);
void hpet_reset_counter(void);
uint64_t hpet_timer_since(void);