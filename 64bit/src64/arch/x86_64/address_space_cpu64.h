#ifndef MOWKOW64_X86_ADDRESS_SPACE_CPU64_H
#define MOWKOW64_X86_ADDRESS_SPACE_CPU64_H

#include <stdint.h>

#ifdef ADDRESS_SPACE_HOST_TEST
int as_cpu_nx64(void);
int as_cpu_irq_off64(void);
uintptr_t as_cpu_physical64(uintptr_t alias);
uintptr_t as_cpu_alias64(uintptr_t physical);
void as_cpu_protect64(void);
void as_cpu_root64(uintptr_t root);
#else
static inline int as_cpu_nx64(void)
{
	uint32_t a, b, c, d;

	__asm__ volatile ("cpuid" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) :
		"a" (0x80000000U), "c" (0));
	if (a < 0x80000001U) { return 0; }
	__asm__ volatile ("cpuid" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) :
		"a" (0x80000001U), "c" (0));
	return (d & (1U << 20)) != 0;
}

static inline int as_cpu_irq_off64(void)
{
	uint64_t flags;

	__asm__ volatile ("pushfq; popq %0" : "=r" (flags) :: "memory");
	return (flags & (1U << 9)) == 0;
}

static inline uintptr_t as_cpu_physical64(uintptr_t alias) { return alias; }
static inline uintptr_t as_cpu_alias64(uintptr_t physical) { return physical; }

static inline void as_cpu_protect64(void)
{
	uint32_t low, high;
	uintptr_t cr0;

	__asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (0xc0000080U));
	low |= 1U << 11; /* EFER.NXE before loading any NX leaf. */
	__asm__ volatile ("wrmsr" :: "a" (low), "d" (high), "c" (0xc0000080U) : "memory");
	__asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
	cr0 |= 1U << 16; /* Supervisor writes must respect read-only leaves. */
	__asm__ volatile ("mov %0, %%cr0" :: "r" (cr0) : "memory");
}

static inline void as_cpu_root64(uintptr_t root)
{
	/* No PCID or global user entries: CR3 reload invalidates old user translations. */
	__asm__ volatile ("mov %0, %%cr3" :: "r" (root) : "memory");
}
#endif

#endif
