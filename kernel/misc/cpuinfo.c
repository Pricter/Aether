#include <kernel/kprintf.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>

cpu_info_t* cpu_info = NULL;

void cpuinfo_init(void) {
	cpu_info = malloc(sizeof(cpu_info_t));
	cpu_info->vendorId = malloc(13); /* 12 Characters is the Vendor ID string size */
	cpu_info->cpuName = malloc(49); /* CPU name has a maximum of 48 bytes (4 * 4 * 3) */

	uint32_t eax, ebx, ecx, edx;
	__get_cpuid(0, &eax, &ebx, &ecx, &edx);

	/* Get the vendorID */
	memcpy(cpu_info->vendorId, &ebx, sizeof(uint32_t));
	memcpy(cpu_info->vendorId + sizeof(uint32_t), &edx, sizeof(uint32_t));
	memcpy(cpu_info->vendorId + (2 * sizeof(uint32_t)), &ecx, sizeof(uint32_t));
	cpu_info->vendorId[12] = '\0';

	/* To get the CPU name, you need to execute the CPUID instruction with eax = 0x80000000 */
	kprintf("CPU: %s, ", cpu_info->vendorId);

	__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);

    // Check if the CPU supports extended functions
    if (eax >= 0x80000004) {
        uint32_t* name_ptr = (uint32_t*)cpu_info->cpuName;

        // Retrieve the CPU name using multiple CPUID calls
        __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
        memcpy(name_ptr, &eax, sizeof(uint32_t));
        memcpy(name_ptr + 1, &ebx, sizeof(uint32_t));
        memcpy(name_ptr + 2, &ecx, sizeof(uint32_t));
        memcpy(name_ptr + 3, &edx, sizeof(uint32_t));

        __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
        memcpy(name_ptr + 4, &eax, sizeof(uint32_t));
        memcpy(name_ptr + 5, &ebx, sizeof(uint32_t));
        memcpy(name_ptr + 6, &ecx, sizeof(uint32_t));
        memcpy(name_ptr + 7, &edx, sizeof(uint32_t));

        __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
        memcpy(name_ptr + 8, &eax, sizeof(uint32_t));
        memcpy(name_ptr + 9, &ebx, sizeof(uint32_t));
        memcpy(name_ptr + 10, &ecx, sizeof(uint32_t));
        memcpy(name_ptr + 11, &edx, sizeof(uint32_t));

        cpu_info->cpuName[48] = '\0'; // Null-terminate the string
        kprintf("%s, ", cpu_info->cpuName);
    } else {
        kprintf("Unknown, ");
    }

	__get_cpuid(0x1, &eax, &ebx, &ecx, &edx);

	uint32_t cores = (ebx >> 16) & 0xFF;
	cpu_info->coreCount = cores;
	kprintf("%d cores\n", cores);
}