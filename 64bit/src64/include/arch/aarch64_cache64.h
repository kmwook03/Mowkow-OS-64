#ifndef MOWKOW64_ARCH_AARCH64_CACHE64_H
#define MOWKOW64_ARCH_AARCH64_CACHE64_H

#include <arch/cache_range64.h>

#define A64_CACHE_CLEAN64 0U
#define A64_CACHE_INVALIDATE64 1U
#define A64_CACHE_CODE_CLEAN64 2U
#define A64_CACHE_CODE_INVALIDATE64 3U

#ifdef AARCH64_CACHE_HOST_TEST
uint64_t a64_cache_ctr64(void);
void a64_cache_line64(uintptr_t address, unsigned int operation);
void a64_cache_sync64(unsigned int instruction);
#else
static inline uint64_t a64_cache_ctr64(void)
{
	uint64_t value;

	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (value));
	return value;
}
static inline void a64_cache_line64(uintptr_t address, unsigned int operation)
{
	switch (operation) {
	case A64_CACHE_CLEAN64:
		__asm__ volatile ("dc cvac, %0" :: "r" (address) : "memory"); break;
	case A64_CACHE_INVALIDATE64:
		__asm__ volatile ("dc ivac, %0" :: "r" (address) : "memory"); break;
	case A64_CACHE_CODE_CLEAN64:
		__asm__ volatile ("dc cvau, %0" :: "r" (address) : "memory"); break;
	case A64_CACHE_CODE_INVALIDATE64:
		__asm__ volatile ("ic ivau, %0" :: "r" (address) : "memory"); break;
	}
}
static inline void a64_cache_sync64(unsigned int instruction)
{
	if (instruction) { __asm__ volatile ("dsb ish\n\tisb" ::: "memory"); }
	else { __asm__ volatile ("dsb sy" ::: "memory"); }
}
#endif

static inline uintptr_t a64_cache_size64(int instruction)
{
	uint64_t ctr = a64_cache_ctr64();

	return (uintptr_t) 4U << ((ctr >> (instruction ? 0 : 16)) & 15U);
}

/* End at the last enclosing line before advancing can wrap. No barrier here:
 * framebuffer callers batch rows, while DMA callers use the synchronous form.
 */
static inline int a64_cache_walk64(uintptr_t address, size_t bytes,
	unsigned int operation)
{
	struct CACHE_RANGE64 range;
	uintptr_t line = a64_cache_size64(operation == A64_CACHE_CODE_INVALIDATE64);

	if (operation > A64_CACHE_CODE_INVALIDATE64 ||
		cache_range_init64(&range, address, bytes, line) != 0) { return -22; }
	for (address = range.first;; address += line) {
		a64_cache_line64(address, operation);
		if (address == range.last) { break; }
	}
	return 0;
}

static inline int a64_cache_dma64(const void *address, size_t bytes, int invalidate)
{
	if (a64_cache_walk64((uintptr_t) address, bytes,
		invalidate ? A64_CACHE_INVALIDATE64 : A64_CACHE_CLEAN64) != 0) { return -22; }
	a64_cache_sync64(0);
	return 0;
}

#endif
