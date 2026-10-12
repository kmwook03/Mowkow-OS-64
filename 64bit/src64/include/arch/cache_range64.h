#ifndef MOWKOW64_ARCH_CACHE_RANGE64_H
#define MOWKOW64_ARCH_CACHE_RANGE64_H

#include <stddef.h>
#include <stdint.h>

struct CACHE_RANGE64 {
	uintptr_t first, last; /* Inclusive cache-line bases; last may abut UINTPTR_MAX. */
};

static inline int cache_range_init64(struct CACHE_RANGE64 *range,
	uintptr_t address, size_t bytes, uintptr_t line_size)
{
	if (range == NULL || bytes == 0 || bytes > UINTPTR_MAX - address ||
		line_size == 0 || (line_size & (line_size - 1U)) != 0) { return -22; }
	range->first = address & ~(line_size - 1U);
	range->last = (address + bytes - 1U) & ~(line_size - 1U);
	return 0;
}

#endif
