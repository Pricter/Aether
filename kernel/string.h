#pragma once

#include <stdint.h>
#include <stddef.h>

size_t strlen(const char* s);
int strcmp(const char* s1, const char* s2);
char* strctrim(const char* s, char c);
char* strdup(const char* s);