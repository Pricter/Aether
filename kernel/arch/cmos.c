#include <errno.h>
#include <deps/printf.h>
#include <kernel/ports.h>
#include <stdint.h>
#include <memory.h>

uint64_t arch_boot_time = 0; /**< Time (in seconds) according to the cmos right before we using TSC */
uint64_t tsc_basis_time = 0; /**< Accumulated time (in microseconds) on the TSC, when we timed it; eg. how long did boot take */
uint64_t tsc_mhz = 3500;     /**< MHz rating we determined for the TSC. Usually also the core speed? */

#define from_bcd(val) ((val / 16) * 10 + (val & 0xf))
#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

// Old CMOS code follows

enum {
    CMOS_SECOND = 0,
    CMOS_MINUTE = 2,
    CMOS_HOUR = 4,
    CMOS_DAY = 7,
    CMOS_MONTH = 8,
    CMOS_YEAR = 9
};

/**
 * @brief Read the contents of the RTC CMOS
 *
 * @param values (out) Where to stick the values read.
 */
static void cmos_dump(uint16_t* values) {
    for(uint16_t index = 0; index < 128; ++index) {
        outportb(CMOS_ADDRESS, index);
        values[index] = inportb(CMOS_DATA);
    }
}

/**
 * @brief Check if the CMOS is currently being updated.
 */
static int is_update_in_progress(void) {
	outportb(CMOS_ADDRESS, 0x0a);
	return inportb(CMOS_DATA) & 0x80;
}

/**
 * @brief Convert years to Unix timestamps
 * 
 * @param years Years since 2000
 * @returns Seconds since the Unix epoch
*/
static uint64_t secs_of_years(int years) {
    uint64_t days = 0;
    years += 2000;
    while (years > 1969) {
        days += 365;
        if(years % 4 == 0) {
            if(years % 100 == 0) {
                if(years % 400 == 0) {
                    days++;
                }
            } else {
                days++;
            }
        }
        years--;
    }
    return days * 86400;
}

/**
 * @brief How long was a month in a given year?
 *
 * Tries to do leap year stuff for February.
 *
 * @param months 1~12 calendar month
 * @param year   Years since 2000
 * @return Number of seconds in that month.
 */
static uint64_t secs_of_month(int months, int year) {
	year += 2000;

	uint64_t days = 0;
	switch(months) {
		case 11:
			days += 30; /* fallthrough */
		case 10:
			days += 31; /* fallthrough */
		case 9:
			days += 30; /* fallthrough */
		case 8:
			days += 31; /* fallthrough */
		case 7:
			days += 31; /* fallthrough */
		case 6:
			days += 30; /* fallthrough */
		case 5:
			days += 31; /* fallthrough */
		case 4:
			days += 30; /* fallthrough */
		case 3:
			days += 31; /* fallthrough */
		case 2:
			days += 28;
			if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
				days++;
			} /* fallthrough */
		case 1:
			days += 31; /* fallthrough */
		default:
			break;
	}
	return days * 86400;
}

/**
 * @brief Convert the CMOS time to a Unix timestamp.
 *
 * Reads BCD data from the RTC CMOS and does some dumb
 * math to convert the display time to a Unix timestamp.
 *
 * @return Current Unix time
 */
uint64_t read_cmos(void) {
	uint16_t values[128];
	uint16_t old_values[128];

	while (is_update_in_progress());
	cmos_dump(values);

	do {
		memcpy(old_values, values, 128);
		while (is_update_in_progress());
		cmos_dump(values);
	} while ((old_values[CMOS_SECOND] != values[CMOS_SECOND]) ||
		(old_values[CMOS_MINUTE] != values[CMOS_MINUTE]) ||
		(old_values[CMOS_HOUR] != values[CMOS_HOUR])     ||
		(old_values[CMOS_DAY] != values[CMOS_DAY])       ||
		(old_values[CMOS_MONTH] != values[CMOS_MONTH])   ||
		(old_values[CMOS_YEAR] != values[CMOS_YEAR]));

	/* Math Time */
	uint64_t time =
		secs_of_years(from_bcd(values[CMOS_YEAR]) - 1) +
		secs_of_month(from_bcd(values[CMOS_MONTH]) - 1,
		from_bcd(values[CMOS_YEAR])) +
		(from_bcd(values[CMOS_DAY]) - 1) * 86400 +
		(from_bcd(values[CMOS_HOUR])) * 3600 +
		(from_bcd(values[CMOS_MINUTE])) * 60 +
		from_bcd(values[CMOS_SECOND]) + 0;

	return time;
}

/**
 * @brief Helper to read timestamp counter
 */
static inline uint64_t read_tsc(void) {
	uint32_t lo, hi;
	asm volatile ( "rdtsc" : "=a"(lo), "=d"(hi) );
	return ((uint64_t)hi << 32UL) | (uint64_t)lo;
}

/**
 * @brief Exported interface to read timestamp counter.
 *
 * Used by various things in the kernel to get a quick performance
 * timer value. This is always scaled by @c cpu_mhz when it
 * needs to be converted to something user-friendly.
 */
uint64_t arch_perf_timer(void) {
	return read_tsc();
}

/**
 * @brief What to scale performance counter times by.
 */
size_t cpu_mhz(void) {
	return tsc_mhz;
}

/**
 * @brief Initializes boot time, system time, TSC rate, etc.
 *
 * We determine TSC rate with a one-shot PIT, which seems
 * to work fine... the PIT is the only thing with both reasonable
 * precision and actual known wall-clock configuration.
 *
 * In Bochs, this has a tendency to be 1) completely wrong (usually
 * about half the time that actual execution will run at, in my
 * experiences) and 2) loud, as despite the attempt to turn off
 * the speaker it still seems to beep it (the second channel of the
 * PIT controls the beeper).
 *
 * In QEMU, VirtualBox, VMware, and on all real hardware I've tested,
 * including everything from a ThinkPad T410 to a Surface Pro 6, this
 * has been surprisingly accurate and good enough to use the TSC as
 * our only wall clock source.
 */
void clock_initialize(void) {
	printf("tsc: Calibrating system timestamp counter\n");
	arch_boot_time = read_cmos();
	uintptr_t end_lo, end_hi;
	uint32_t start_lo, start_hi;
	asm volatile (
		/* Disabled and sets gating for channel 2 */
		"inb  $0x61, %%al\n"
		"andb $0xDD, %%al\n"
		"orb  $0x01, %%al\n"
		"outb %%al, $0x61\n"
		/* Configure channel 2 to one-shot, next two bytes are low/high */
		"movb $0xB2, %%al\n" /* 0b10110010 */
		"outb %%al, $0x43\n"
		/* 0x__9b */
		"movb $0x9B, %%al\n"
		"outb %%al, $0x42\n"
		"inb  $0x60, %%al\n"
		/* 0x2e__ */
		"movb $0x2E, %%al\n"
		"outb %%al, $0x42\n"
		/* Re-enable */
		"inb  $0x61, %%al\n"
		"andb $0xDE, %%al\n"
		"outb %%al, $0x61\n"
		/* Pulse high */
		"orb  $0x01, %%al\n"
		"outb %%al, $0x61\n"
		/* Read TSC and store in vars */
		"rdtsc\n"
		"movl %%eax, %2\n"
		"movl %%edx, %3\n"
		/* In QEMU and VirtualBox, this seems to flip low.
		 * On real hardware and VMware it flips high */
		"inb  $0x61, %%al\n"
		"andb $0x20, %%al\n"
		"jz   2f\n"
		/* Loop until output goes low? */
	"1:\n"
		"inb  $0x61, %%al\n"
		"andb $0x20, %%al\n"
		"jnz  1b\n"
		"rdtsc\n"
		"jmp 3f\n"
		/* Loop until output goes high */
	"2:\n"
		"inb  $0x61, %%al\n"
		"andb $0x20, %%al\n"
		"jz   2b\n"
		"rdtsc\n"
	"3:\n"
		: "=a" (end_lo), "=d" (end_hi), "=r" (start_lo), "=r" (start_hi)
	);

	uintptr_t end = ((end_hi & 0xFFFFffff) << 32) | (end_lo & 0xFFFFffff);
	uintptr_t start = ((uintptr_t)(start_hi & 0xFFFFffff) << 32) | (start_lo & 0xFFFFffff);
	tsc_mhz = (end - start) / 10000;
	if(tsc_mhz == 0) tsc_mhz = 2000;
	tsc_basis_time = start / tsc_mhz;

	printf("tsc: TSC timed at %lu MHz\n", tsc_mhz);
	printf("tsc: Boot time is %lus\n", arch_boot_time);
	printf("tsc: Initial TSC timestamp was %luus\n", tsc_basis_time);
}