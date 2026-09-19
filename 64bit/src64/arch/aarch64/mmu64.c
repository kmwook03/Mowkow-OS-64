/* Minimal BCM2712 identity map used by the M2 framebuffer bring-up. */
#include <arch/arch64.h>
#include <stdint.h>

#define L1_ENTRIES 512
#define L1_BLOCK_SIZE (1ULL << 30)

#define DESC_BLOCK 0x1ULL
#define DESC_ATTR_INDEX(n) ((uint64_t) (n) << 2)
#define DESC_INNER_SHAREABLE (3ULL << 8)
#define DESC_ACCESS_FLAG (1ULL << 10)
#define DESC_PXN (1ULL << 53)
#define DESC_UXN (1ULL << 54)

#define MAIR_NORMAL_WB 0xffULL
#define MAIR_DEVICE_NGNRNE (0x00ULL << 8)

static uint64_t l1_table[L1_ENTRIES] __attribute__((aligned(4096)));

static int mmio_block64(unsigned int index)
{
	/* BCM2712 SoC window: 0x10_0000_0000 through 0x10_ffff_ffff. */
	if (index >= 64 && index < 68) {
		return 1;
	}
	/* Future RP1 PCIe window starts at 0x1f_0000_0000. */
	if (index >= 124 && index < 128) {
		return 1;
	}
	return 0;
}

void arch64_mmu_init(void)
{
	uint64_t tcr;
	uint64_t sctlr;
	unsigned int i;

	for (i = 0; i < L1_ENTRIES; i++) {
		uint64_t descriptor;

		/* Only the low 128 GiB is needed through the RP1 milestones. */
		if (i >= 128) {
			l1_table[i] = 0;
			continue;
		}
		descriptor = (uint64_t) i * L1_BLOCK_SIZE;
		descriptor |= DESC_BLOCK | DESC_ACCESS_FLAG;
		if (mmio_block64(i) != 0) {
			descriptor |= DESC_ATTR_INDEX(1) | DESC_PXN | DESC_UXN;
		} else {
			descriptor |= DESC_ATTR_INDEX(0) | DESC_INNER_SHAREABLE;
		}
		l1_table[i] = descriptor;
	}

	__asm__ volatile (
		"dsb sy\n\t"
		"tlbi vmalle1\n\t"
		"dsb sy\n\t"
		"isb"
		::: "memory");

	__asm__ volatile ("msr mair_el1, %0" :: "r" (MAIR_NORMAL_WB |
		MAIR_DEVICE_NGNRNE) : "memory");
	__asm__ volatile ("msr ttbr0_el1, %0" :: "r" ((uintptr_t) l1_table) : "memory");
	__asm__ volatile ("msr ttbr1_el1, xzr" ::: "memory");

	/* 39-bit VA, 4 KiB granule, inner-shareable WBWA, 40-bit physical. */
	tcr = 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
		(1ULL << 23) | (2ULL << 32);
	__asm__ volatile ("msr tcr_el1, %0\n\tisb" :: "r" (tcr) : "memory");

	__asm__ volatile ("mrs %0, sctlr_el1" : "=r" (sctlr));
	/* Do not let an inherited WXN setting make the writable 1 GiB kernel
	   block unexpectedly non-executable during this coarse M2 mapping. */
	sctlr &= ~(1ULL << 19);
	sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
	__asm__ volatile ("msr sctlr_el1, %0\n\tisb" :: "r" (sctlr) : "memory");
}
