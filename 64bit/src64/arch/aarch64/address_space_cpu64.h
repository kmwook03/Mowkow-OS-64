#ifndef MOWKOW64_AARCH64_ADDRESS_SPACE_CPU64_H
#define MOWKOW64_AARCH64_ADDRESS_SPACE_CPU64_H

#include <arch/arch64.h>
#include <arch/aarch64_cache64.h>

#ifdef ADDRESS_SPACE_HOST_TEST
int as_cpu_irq_off64(void);
uintptr_t as_cpu_physical64(uintptr_t alias);
uintptr_t as_cpu_alias64(uintptr_t physical);
void as_cpu_store_barrier64(void);
void as_cpu_root64(uintptr_t root);
void as_cpu_isb64(void);
void as_cpu_tlb64(void);
void as_cpu_completion64(void);
void as_cpu_icache64(void);
#else
static inline int as_cpu_irq_off64(void)
{
	uint64_t daif;

	__asm__ volatile ("mrs %0, daif" : "=r" (daif));
	return (daif & (1U << 7)) != 0;
}
static inline uintptr_t as_cpu_physical64(uintptr_t alias)
{
	return arch64_virt_to_phys(alias);
}
static inline uintptr_t as_cpu_alias64(uintptr_t physical)
{
	return arch64_phys_to_virt(physical);
}
static inline void as_cpu_store_barrier64(void)
{
	__asm__ volatile ("dsb ishst" ::: "memory");
}
static inline void as_cpu_root64(uintptr_t root)
{
	__asm__ volatile ("msr ttbr0_el1, %0" :: "r" (root) : "memory");
}
static inline void as_cpu_isb64(void) { __asm__ volatile ("isb" ::: "memory"); }
static inline void as_cpu_tlb64(void) { __asm__ volatile ("tlbi vmalle1" ::: "memory"); }
static inline void as_cpu_completion64(void) { __asm__ volatile ("dsb ish" ::: "memory"); }
static inline void as_cpu_icache64(void) { __asm__ volatile ("ic iallu" ::: "memory"); }
#endif

#endif
