#include <stdint.h>
#include <stddef.h>
#include <kernel/macros.h>

/* GDT Descritor entry */
struct gdt_descriptor {
    uint16_t limit;
    uint16_t base_low16;
    uint8_t  base_mid8;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high8;
};

/* TSS */
struct tss_descriptor {
    uint16_t length;
    uint16_t base_low16;
    uint8_t  base_mid8;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high8;
    uint32_t base_upper32;
    uint32_t reserved;
};

/* All the entries */
struct gdt_descriptor gdt[5];

/* The gdtr itself */
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdtr gdtr;


void gdt_reload(void) {
    /* Load the GDT and reset the segment registers */
    asm volatile (
        "lgdt %0\n\t"
        "push $0x8\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "push %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%eax\n\t"
        "mov %%eax, %%ds\n\t"
        "mov %%eax, %%es\n\t"
        "mov %%eax, %%fs\n\t"
        "mov %%eax, %%gs\n\t"
        "mov %%eax, %%ss\n\t"
        :
        : "m"(gdtr)
        : "rax", "memory"
    );
}

void __init gdt_init(void) {
    // Null descriptor.
    gdt[0].limit       = 0;
    gdt[0].base_low16  = 0;
    gdt[0].base_mid8   = 0;
    gdt[0].access      = 0;
    gdt[0].granularity = 0;
    gdt[0].base_high8  = 0;

    // Kernel code 64.
    gdt[1].limit       = 0;
    gdt[1].base_low16  = 0;
    gdt[1].base_mid8   = 0;
    gdt[1].access      = 0x9A;
    gdt[1].granularity = 0x20;
    gdt[1].base_high8  = 0;

    // Kernel data 64.
    gdt[2].limit       = 0;
    gdt[2].base_low16  = 0;
    gdt[2].base_mid8   = 0;
    gdt[2].access      = 0x92;
    gdt[2].granularity = 0;
    gdt[2].base_high8  = 0;

    // User code 64.
    gdt[3].limit       = 0;
    gdt[3].base_low16  = 0;
    gdt[3].base_mid8   = 0;
    gdt[3].access      = 0xFA;
    gdt[3].granularity = 0x20;
    gdt[3].base_high8  = 0;

    // User data 64.
    gdt[4].limit       = 0;
    gdt[4].base_low16  = 0;
    gdt[4].base_mid8   = 0;
    gdt[4].access      = 0xF2;
    gdt[4].granularity = 0;
    gdt[4].base_high8  = 0;

    // Set the pointer.
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    gdt_reload();
}