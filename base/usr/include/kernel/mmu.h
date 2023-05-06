#pragma once

#include <stdint.h>
#include <stddef.h>

void mmu_frame_clear(uintptr_t address);
void mmu_frame_set(uintptr_t address);
uintptr_t mmu_request_frame(void);