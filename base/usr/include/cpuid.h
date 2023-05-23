/**
 * cpuid.h
 * 
 * This will only implement cpuid for 64bit as I do not
 * plan to turn this os to 32bit
*/

#pragma once

#include <stdint.h>

#if !(__x86_64)
#error Header included in a non x86 cpu
#endif

#define __cpuid(__leaf, __eax, __ebx, __ecx, __edx) \
	__asm__ (" xchgq %%rbx, %q1\n" \
			 " cpuid\n" \
			 " xchgq %%rbx, %q1" \
			: "=a" (__eax), "=r" (__ebx), "=c" (__ecx), "=d" (__edx) \
			: "0" (__leaf))

#define __cpuid_count(__leaf, __count, __eax, __ebx, __ecx, __edx) \
	__asm__ (" xchgq %%rbx, %q1\n" \
			 " cpuid\n" \
			 " xchgq %%rbx, %q1" \
			: "=a" (__eax), "=r" (__ebx), "=c" (__ecx), "=d" (__edx) \
			: "0" (__leaf), "2" (__count))

static inline uint32_t __get_cpuid_max (uint32_t __leaf, uint32_t *__sig) {
	uint32_t __eax, __ebx, __ecx, __edx;
	__cpuid(__leaf, __eax, __ebx, __ecx, __edx);
	if(__sig) *__sig = __ebx;
	return __eax;
}

static inline uint32_t __get_cpuid(uint32_t __leaf, uint32_t* __eax, uint32_t* __ebx,
							  uint32_t* __ecx, uint32_t* __edx)
{
	uint32_t __max_leaf = __get_cpuid_max(__leaf & 0x80000000, 0);
	if(__max_leaf == 0 || __max_leaf < __leaf) return 0;
	__cpuid(__leaf, *__eax, *__ebx, *__ecx, *__edx);
	return 1;
}

static inline uint32_t __get_cpuid_count(uint32_t __leaf, uint32_t __subleaf,
									uint32_t* __eax, uint32_t* __ebx, uint32_t* __ecx,
									uint32_t* __edx)
{
	uint32_t __max_leaf = __get_cpuid_max(__leaf & 0x80000000, 0);
	if(__max_leaf == 0 || __max_leaf < __leaf) return 0;
	__cpuid_count(__leaf, __subleaf, *__eax, *__ebx, *__ecx, *__edx);
	return 1;
}