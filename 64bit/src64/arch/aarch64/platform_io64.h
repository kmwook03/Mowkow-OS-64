#ifndef MOWKOW64_AARCH64_PLATFORM_IO64_H
#define MOWKOW64_AARCH64_PLATFORM_IO64_H

#include <arch/arch64.h>
#include <stddef.h>
#include <stdint.h>

#ifdef AARCH64_IO_HOST_TEST
uint32_t a64_read32_64(uintptr_t address);
void a64_write32_64(uintptr_t address, uint32_t value);
uint16_t a64_read16_64(uintptr_t address);
void a64_write16_64(uintptr_t address, uint16_t value);
uint64_t a64_counter64(void);
uint64_t a64_frequency64(void);
void a64_barrier64(void);
void a64_relax64(void);
uintptr_t a64_physical64(uintptr_t address);
#else
static inline uint32_t a64_read32_64(uintptr_t address)
{
	return *(volatile uint32_t *) address;
}
static inline void a64_write32_64(uintptr_t address, uint32_t value)
{
	*(volatile uint32_t *) address = value;
}
static inline uint16_t a64_read16_64(uintptr_t address)
{
	return *(volatile uint16_t *) address;
}
static inline void a64_write16_64(uintptr_t address, uint16_t value)
{
	*(volatile uint16_t *) address = value;
}
static inline uint64_t a64_counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}
static inline uint64_t a64_frequency64(void)
{
	uint64_t value;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (value));
	return value;
}
static inline void a64_barrier64(void) { __asm__ volatile ("dsb sy" ::: "memory"); }
static inline void a64_relax64(void) { __asm__ volatile ("yield"); }
static inline uintptr_t a64_physical64(uintptr_t address)
{
	return arch64_virt_to_phys(address);
}
#endif

#endif
