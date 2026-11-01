#include <platform/serial.h>
#include <kernel/spinlock.h>

#define PL011_BASE 0x09000000UL
#define PL011_DATA 0x000UL
#define PL011_FLAGS 0x018UL
#define PL011_INTEGER_BAUD 0x024UL
#define PL011_FRACTIONAL_BAUD 0x028UL
#define PL011_LINE_CONTROL 0x02cUL
#define PL011_CONTROL 0x030UL
#define PL011_INTERRUPT_MASK 0x038UL
#define PL011_INTERRUPT_CLEAR 0x044UL
#define PL011_TRANSMIT_FULL (1U << 5)

#define PL011_ALL_INTERRUPTS 0x7ffU
/* 24 MHz / (16 * 115200) = 13 + 1/64 after divisor rounding. */
#define PL011_BAUD_INTEGER_115200 13U
#define PL011_BAUD_FRACTION_115200 1U
#define PL011_WORD_LENGTH_8 (3U << 5)
#define PL011_FIFO_ENABLE (1U << 4)
#define PL011_UART_ENABLE (1U << 0)
#define PL011_TRANSMITTER_ENABLE (1U << 8)
#define PL011_RECEIVER_ENABLE (1U << 9)

/* Access registers directly while the MMU is off. This address belongs
 * only to QEMU virt; physical boards will need their own platform driver. */
static volatile unsigned int *serial_register(unsigned long register_offset) {
    return (volatile unsigned int *)(PL011_BASE + register_offset);
}

/* Configure a polling-only 8-bit UART, 115200 baud at QEMU's 24 MHz clock. */
void platform_serial_init(void) {
    *serial_register(PL011_CONTROL) = 0;
    *serial_register(PL011_INTERRUPT_MASK) = 0;
    *serial_register(PL011_INTERRUPT_CLEAR) = PL011_ALL_INTERRUPTS;
    *serial_register(PL011_INTEGER_BAUD) = PL011_BAUD_INTEGER_115200;
    *serial_register(PL011_FRACTIONAL_BAUD) = PL011_BAUD_FRACTION_115200;
    *serial_register(PL011_LINE_CONTROL) = PL011_WORD_LENGTH_8 | PL011_FIFO_ENABLE;
    *serial_register(PL011_CONTROL) =
        PL011_UART_ENABLE | PL011_TRANSMITTER_ENABLE | PL011_RECEIVER_ENABLE;
}

/* Wait for FIFO space before emitting one byte; usable before the heap. */
static void serial_write_character(unsigned char character) {
    while (*serial_register(PL011_FLAGS) & PL011_TRANSMIT_FULL) {
        __asm__ volatile("yield");
    }
    *serial_register(PL011_DATA) = character;
}

/* Emit a terminated string and translate newlines for serial terminals. */
void platform_serial_write(const char *message) {
    while (*message) {
        if (*message == '\n') {
            serial_write_character('\r');
        }

        serial_write_character((unsigned char)*message++);
    }
}

static spinlock_t terminal_output_lock = SPINLOCK_ZERO;

/* A length-delimited console write is also usable by the shared tty device. */
void platform_serial_write_bytes(const void *buffer, size_t length) {
    bool interrupts_enabled = spinlock_acquire(&terminal_output_lock);
    const unsigned char *bytes = buffer;

    for (size_t byte_index = 0; byte_index < length; ++byte_index) {
        if (bytes[byte_index] == '\n') {
            serial_write_character('\r');
        }

        serial_write_character(bytes[byte_index]);
    }

    spinlock_release(&terminal_output_lock, interrupts_enabled);
}
