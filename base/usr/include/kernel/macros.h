#pragma once

#include <kernel/mmu.h>

#define __section(section) __attribute__((__section__(section)))

#define __init __section(".init.text")
#define __initvar __section(".init.data")
#define __initconst __section(".init.rodata")

#define malloc_t(type) (type*)malloc(sizeof(type))