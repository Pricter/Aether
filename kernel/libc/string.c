#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/mmu.h>
#include <memory.h>

size_t strlen(const char* s) {
	const char* a = s;
	const size_t *w;
	for(; (uintptr_t)s % ALIGN; s++) {
		if(!*s) {
			return s-a;
		}
	}
	for(w = (const void*)s; !HASZERO(*w); w++);
	for (s = (const void *)w; *s; s++);
	return s-a;
}

int strcmp(const char* s1, const char* s2) {
	while(*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strctrim(const char* s, char c) {
	size_t len = strlen(s);
	const char* end = s + len - 1;

	while(*end == c && end >= s) {
		--end;
	}

	size_t newLen = end - s + 1;
	char* trimmed = (char*)malloc(newLen + 1);
	memcpy(trimmed, s, newLen);
	trimmed[newLen] = '\0';
	
	return trimmed;
}

char* strdup(const char* s) {
	size_t length = strlen(s) + 1;
	char* new = (char*)malloc(length);
	memcpy(new, s, length);
	return new;
}