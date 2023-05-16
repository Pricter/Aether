#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/types.h>

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