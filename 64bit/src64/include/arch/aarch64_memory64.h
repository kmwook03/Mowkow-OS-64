#ifndef MOWKOW64_ARCH_AARCH64_MEMORY64_H
#define MOWKOW64_ARCH_AARCH64_MEMORY64_H

#include <stddef.h>
#include <stdint.h>

#define AARCH64_RAM_BYTES64 (1ULL << 30)
#define RP1_DMA_BASE64 0x1000000000ULL
#define RP1_BAR_BYTES64 0x400000U
#define PCIE_OUTBOUND_BASE64 0x1f00000000ULL
#define PCIE_OUTBOUND_BYTES64 (1ULL << 32)

/* Only the first GiB has a normal-RAM direct mapping in this kernel. */
static inline int aarch64_ram_span64(uintptr_t physical, size_t bytes)
{
	return bytes != 0 && physical < AARCH64_RAM_BYTES64 &&
		bytes <= AARCH64_RAM_BYTES64 - physical;
}

static inline int rp1_dma_range64(uintptr_t physical, size_t bytes,
	size_t alignment, uint64_t *dma)
{
	if (dma == NULL || alignment == 0 || (alignment & (alignment - 1U)) != 0 ||
		(physical & (alignment - 1U)) != 0 || !aarch64_ram_span64(physical, bytes)) {
		return -22;
	}
	*dma = RP1_DMA_BASE64 + physical;
	return 0;
}

/* RP1 BAR1 is a non-prefetchable 32-bit, four-MiB peripheral resource.
 * Never mask a 64-bit/IO/reserved BAR into a plausible 32-bit address.
 */
static inline int rp1_bar_range64(uint32_t bar, uint32_t root_base,
	uint32_t root_limit, uint64_t *physical)
{
	uint32_t address = bar & 0xfffffff0U;

	if (physical == NULL || (bar & 15U) != 0 || address == 0 ||
		(address & (RP1_BAR_BYTES64 - 1U)) != 0 || (root_base & 0xfffffU) != 0 ||
		(root_limit & 0xfffffU) != 0xfffffU || root_limit < root_base ||
		address < root_base || address > root_limit ||
		RP1_BAR_BYTES64 - 1U > root_limit - address) { return -22; }
	*physical = PCIE_OUTBOUND_BASE64 + ((uint64_t) address - root_base);
	return 0;
}

/* Validated PCI probe supplies the missing root-limit proof to legacy RP1 APIs. */
int pcie_rp1_range64(uint32_t bar, uint32_t root_base, size_t offset,
	size_t bytes, uintptr_t *physical);

#endif
