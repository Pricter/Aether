#include <kernel/acpi.h>
#include <kernel/ports.h>

/* Disable the PIC because we are using the lapic */
void pic_disable(void) {
	outportb(0xA1, 0xff);
	outportb(0x21, 0xff);
}

void lapic_init(void) {
	// TODO
}