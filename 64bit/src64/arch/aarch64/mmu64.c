/* BCM2712 4 KiB translation tables used by the M3 paging bring-up. */
#include <arch/arch64.h>
#include <stdint.h>

#define TABLE_ENTRIES 512U
#define PAGE_SIZE 4096ULL
#define L2_SPAN (1ULL << 21)
#define LOW_RAM_SIZE (1ULL << 30)

#define MAILBOX_BASE 0x107c013880ULL
#define GIO_AON_BASE 0x107d517c00ULL
#define GIC_BASE 0x107fff9000ULL

#define DESC_VALID 0x1ULL
#define DESC_TABLE_OR_PAGE 0x3ULL
#define DESC_ATTR_INDEX(n) ((uint64_t) (n) << 2)
#define DESC_INNER_SHAREABLE (3ULL << 8)
#define DESC_ACCESS_FLAG (1ULL << 10)
#define DESC_PXN (1ULL << 53)
#define DESC_UXN (1ULL << 54)
#define DESC_ADDRESS_MASK 0x0000fffffffff000ULL

#define MAIR_NORMAL_WB 0xffULL
#define MAIR_DEVICE_NGNRNE (0x00ULL << 8)

static uint64_t bootstrap_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t user_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t ttbr1_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t low_l2[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t low_l3[TABLE_ENTRIES][TABLE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static uint64_t mmio_l2[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t mmio_l3[3][TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

static uint64_t table_descriptor64(const void *table)
{
	return ((uintptr_t) table & DESC_ADDRESS_MASK) | DESC_TABLE_OR_PAGE;
}

static uint64_t normal_page_descriptor64(uint64_t physical)
{
	return (physical & DESC_ADDRESS_MASK) | DESC_TABLE_OR_PAGE |
		DESC_ATTR_INDEX(0) | DESC_INNER_SHAREABLE | DESC_ACCESS_FLAG |
		DESC_UXN;
}

static uint64_t device_page_descriptor64(uint64_t physical)
{
	return (physical & DESC_ADDRESS_MASK) | DESC_TABLE_OR_PAGE |
		DESC_ATTR_INDEX(1) | DESC_ACCESS_FLAG | DESC_PXN | DESC_UXN;
}

static void map_device_window64(uint64_t address, unsigned int table_index)
{
	uint64_t window_base;
	unsigned int l2_index;
	unsigned int page;

	window_base = address & ~(L2_SPAN - 1);
	l2_index = (unsigned int) ((address >> 21) & 0x1ffULL);
	mmio_l2[l2_index] = table_descriptor64(mmio_l3[table_index]);
	for (page = 0; page < TABLE_ENTRIES; page++) {
		mmio_l3[table_index][page] = device_page_descriptor64(
			window_base + (uint64_t) page * PAGE_SIZE);
	}
}

void arch64_mmu_init(void)
{
	uint64_t tcr;
	uint64_t sctlr;
	unsigned int l2_index;
	unsigned int page;

	/* The first GiB contains the flat kernel, stacks, allocator and the
	   firmware framebuffer. Use genuine level-3 page descriptors throughout. */
	for (l2_index = 0; l2_index < TABLE_ENTRIES; l2_index++) {
		low_l2[l2_index] = table_descriptor64(low_l3[l2_index]);
		for (page = 0; page < TABLE_ENTRIES; page++) {
			uint64_t physical = (uint64_t) l2_index * L2_SPAN +
				(uint64_t) page * PAGE_SIZE;

			low_l3[l2_index][page] = normal_page_descriptor64(physical);
		}
	}

	/* All three devices live in L1 slot 65, but in separate 2 MiB windows. */
	map_device_window64(MAILBOX_BASE, 0);
	map_device_window64(GIO_AON_BASE, 1);
	map_device_window64(GIC_BASE, 2);

	/* TTBR0 retains the low bootstrap map while TTBR1 supplies the canonical
	   high-half kernel alias. A later user-mode step can replace TTBR0 without
	   rebuilding or disturbing the kernel half. */
	bootstrap_l1[0] = table_descriptor64(low_l2);
	bootstrap_l1[(MAILBOX_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);
	ttbr1_l1[0] = table_descriptor64(low_l2);
	ttbr1_l1[(MAILBOX_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);

	__asm__ volatile (
		"dsb sy\n\t"
		"tlbi vmalle1\n\t"
		"dsb sy\n\t"
		"isb"
		::: "memory");

	__asm__ volatile ("msr mair_el1, %0" :: "r" (MAIR_NORMAL_WB |
		MAIR_DEVICE_NGNRNE) : "memory");
	__asm__ volatile ("msr ttbr0_el1, %0" ::
		"r" ((uintptr_t) bootstrap_l1) : "memory");
	__asm__ volatile ("msr ttbr1_el1, %0" ::
		"r" ((uintptr_t) ttbr1_l1) : "memory");

	/* 39-bit TTBR0/TTBR1 VA, 4 KiB granules, inner-shareable WBWA tables,
	   40-bit physical addresses. TG1's 4 KiB encoding is binary 10. */
	tcr = 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
		(25ULL << 16) | (1ULL << 24) | (1ULL << 26) |
		(3ULL << 28) | (2ULL << 30) | (2ULL << 32);
	__asm__ volatile ("msr tcr_el1, %0\n\tisb" :: "r" (tcr) : "memory");

	__asm__ volatile ("mrs %0, sctlr_el1" : "=r" (sctlr));
	/* The bootstrap mapping is writable and executable at EL1. UXN prevents
	   accidentally executing it from EL0; WXN must therefore remain clear. */
	sctlr &= ~(1ULL << 19);
	sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
	__asm__ volatile ("msr sctlr_el1, %0\n\tisb" :: "r" (sctlr) : "memory");
}

void arch64_mmu_finish_high(void)
{
	uintptr_t root_physical;

	/* This function is entered through TTBR1. Drop the temporary identity map
	   and leave TTBR0 pointing at an empty, EL0-ready translation root. */
	root_physical = arch64_virt_to_phys((uintptr_t) user_l1);
	__asm__ volatile (
		"msr ttbr0_el1, %0\n\t"
		"dsb ishst\n\t"
		"tlbi vmalle1\n\t"
		"dsb ish\n\t"
		"isb"
		:: "r" (root_physical) : "memory");
}

static int translate_el1_64(uintptr_t virtual_address, uintptr_t *physical)
{
	uint64_t par;

	__asm__ volatile ("at s1e1r, %0\n\tisb\n\tmrs %1, par_el1" :
		"+r" (virtual_address), "=r" (par) :: "memory");
	if ((par & DESC_VALID) != 0) {
		return -1;
	}
	*physical = (uintptr_t) (par & DESC_ADDRESS_MASK) |
		(virtual_address & (PAGE_SIZE - 1));
	return 0;
}

static int accessible_el0_64(uintptr_t virtual_address)
{
	uint64_t par;

	__asm__ volatile ("at s1e0r, %0\n\tisb\n\tmrs %1, par_el1" :
		"+r" (virtual_address), "=r" (par) :: "memory");
	return (par & DESC_VALID) == 0;
}

int arch64_mmu_self_test(void)
{
	uintptr_t low;
	uintptr_t high;
	uintptr_t physical;

	high = (uintptr_t) arch64_mmu_self_test;
	low = arch64_virt_to_phys(high);
	if (translate_el1_64(low, &physical) == 0) {
		return -1;
	}
	if (translate_el1_64(high, &physical) != 0 || physical != low) {
		return -2;
	}
	if (translate_el1_64(arch64_phys_to_virt((uintptr_t) MAILBOX_BASE),
			&physical) != 0 ||
		physical != (uintptr_t) MAILBOX_BASE) {
		return -3;
	}
	/* The empty user root has no mappings yet. */
	if (translate_el1_64((uintptr_t) LOW_RAM_SIZE, &physical) == 0) {
		return -4;
	}
	/* TTBR1 kernel mappings are privileged-only even when translated as EL0. */
	if (accessible_el0_64(high) != 0) {
		return -5;
	}
	return 0;
}
