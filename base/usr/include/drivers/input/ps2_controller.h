#pragma once

#include <stdint.h>
#include <stdbool.h>

uint8_t ps2_read_data(void);
uint8_t ps2_write_command(uint8_t command, bool status);
void ps2_write_data(uint8_t data);

extern bool dual_channel;