#ifndef MOWKOW64_AARCH64_XHCI_PLATFORM64_H
#define MOWKOW64_AARCH64_XHCI_PLATFORM64_H

#include <arch/aarch64_cache64.h>
#include <stddef.h>
#include <stdint.h>

/* Host fixtures replace CPU/cache instructions while using the real MMIO/ring
 * implementation against modeled registers. No test hooks enter a Pi build.
 */
#ifdef XHCI64_HOST_TEST
uintptr_t xhci_physical64(uintptr_t address);
uintptr_t xhci_cache_size64(void);
uint64_t xhci_counter64(void);
uint64_t xhci_frequency64(void);
void xhci_relax64(void);
void xhci_barrier64(void);
void xhci_clean_cache64(const void *address, size_t bytes);
void xhci_invalidate_cache64(const void *address, size_t bytes);
#else
static inline uintptr_t xhci_physical64(uintptr_t address)
{
	return arch64_virt_to_phys(address);
}
static inline uintptr_t xhci_cache_size64(void) { return a64_cache_size64(0); }
static inline uint64_t xhci_counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static inline uint64_t xhci_frequency64(void)
{
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	return frequency;
}

static inline void xhci_relax64(void)
{
	__asm__ volatile ("yield");
}

static inline void xhci_barrier64(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline void xhci_clean_cache64(const void *address, size_t bytes)
{
	(void) a64_cache_dma64(address, bytes, 0);
}

static inline void xhci_invalidate_cache64(const void *address, size_t bytes)
{
	(void) a64_cache_dma64(address, bytes, 1);
}

#endif

#endif
