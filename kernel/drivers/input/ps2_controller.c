#include <stdint.h>
#include <kernel/ports.h>
#include <kernel/cpu.h>
#include <stdbool.h>
#include <kernel/kprintf.h>
#include <kernel/apic.h>
#include <kernel/irq.h>
#include <kernel/acpi.h>
#include <drivers/input/ps2_controller.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

uint8_t ps2_read_data() {
	uint8_t resp = inportb(PS2_STATUS);
	if(!(resp & 0x01)) panic("PS2 Expected a response but did not receive it", NULL);

	return inportb(PS2_DATA); 
}

uint8_t ps2_write_command(uint8_t command, bool status) {
	outportb(PS2_COMMAND, command);
	if(!status) return 0;

	return ps2_read_data();
}

void ps2_write_data(uint8_t data) {
	outportb(PS2_DATA, data);
}

bool dual_channel = false;

void ps2_controller_init(void) {
	if(fadt->IAPC_BOOT_ARCH & 0x02) {
		kprintf("ps2: PS2 controller is available\n");
	} else {
		kprintf("ps2: PS2 controller is not available\n");
		return;
	}

	/* Disable devices */
	ps2_write_command(0xAD, false);
	ps2_write_command(0xA7, false);

	/* Flush output buffer by reading without checking status register bit 0 */
	inportb(PS2_DATA);

	/* Get the configuration byte */
	uint8_t config = ps2_write_command(0x20, true);
	if(!(config & (1 << 4))) {
		dual_channel = true;
		kprintf("ps2: Using a dual channel controller, config byte: 0x%x\n", config);
	} else {
		dual_channel = false;
		kprintf("ps2: Using a single channel controller, config byte: 0x%x\n", config);
	}

	/* Disable IRQs 0, 1 and translation */
	ps2_write_command(0x60, false);
	ps2_write_data(config & 0xBC);

	/* Perform self test, it may reset on some hardwares, so we need to restore config byte */
	config = ps2_write_command(0x20, true);
	if(ps2_write_command(0xAA, true) != 0x55) {
		panic("ps2 controller self test failed", NULL);
	}
	ps2_write_data(config);

	/* Determine if the controller is actually dual channel */
	ps2_write_command(0xA8, false);
	config = ps2_write_command(0x20, true);
	if((config & (1 << 4)) && dual_channel) {
		panic("Dual channel ps2 controller does not enable port 2", NULL);
	}
	ps2_write_command(0xA7, false);

	/* Interface tests */
	uint8_t status = ps2_write_command(0xAB, true);
	if(status != 0x00) {
		kprintf("ps2: Received 0x%x from self test\n", status);
		panic("ps2 port 1 self test failed", NULL);
	}
	if(dual_channel) {
		status = ps2_write_command(0xA9, true);
		if(status != 0x00) {
			kprintf("ps2: Received 0x%x from self test\n", status);
			panic("using dual channel controller but port 2 self test failed", NULL);
		}
	}

	/* Enable devices */
	ps2_write_command(0xAE, false);
	if(dual_channel) ps2_write_command(0xA8, false);

	/* Enable IRQs */
	config = ps2_write_command(0x20, true);
	config |= (dual_channel ? 0x03 : 0x01);
	ps2_write_command(0x60, false);
	ps2_write_data(config);
	kprintf("ps2: Config byte: 0b%b\n", ps2_write_command(0x20, true));
	kprintf("ps2: Initialized the ps2 controller\n");
}