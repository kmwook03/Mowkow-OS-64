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
#define SDHCI_BASE 0x1000fff000ULL
#define PCIE2_BASE 0x1000120000ULL
#define PCIE2_OUTBOUND_BASE 0x1f00000000ULL
#define PCIE2_OUTBOUND_SIZE 0x100000000ULL
#define L1_SPAN (1ULL << 30)

#define DESC_VALID 0x1ULL
#define DESC_TABLE_OR_PAGE 0x3ULL
#define DESC_ATTR_INDEX(n) ((uint64_t) (n) << 2)
#define DESC_INNER_SHAREABLE (3ULL << 8)
#define DESC_ACCESS_FLAG (1ULL << 10)
#define DESC_AP_EL0_RW (1ULL << 6)
#define DESC_PXN (1ULL << 53)
#define DESC_UXN (1ULL << 54)
#define DESC_ADDRESS_MASK 0x0000fffffffff000ULL

#define MAIR_NORMAL_WB 0xffULL
#define MAIR_DEVICE_NGNRNE (0x00ULL << 8)

static uint64_t bootstrap_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t user_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t user_l2[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
#define USER_L3_TABLES 8U
static uint64_t user_l3[USER_L3_TABLES][TABLE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static uint16_t user_l3_index[USER_L3_TABLES];
static uint64_t ttbr1_l1[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t low_l2[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t low_l3[TABLE_ENTRIES][TABLE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static uint64_t mmio_l2[TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t mmio_l3[5][TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

static uint64_t table_descriptor64(const void *table)
{
	return (arch64_virt_to_phys((uintptr_t) table) & DESC_ADDRESS_MASK) |
		DESC_TABLE_OR_PAGE;
}

static void flush_user_tlb64(void)
{
	__asm__ volatile (
		"dsb ishst\n\t"
		"tlbi vmalle1\n\t"
		"dsb ish\n\t"
		"isb"
		::: "memory");
}

void arch64_user_unmap_all(void)
{
	unsigned int i;
	unsigned int j;

	for (i = 0; i < TABLE_ENTRIES; i++) {
		user_l1[i] = 0;
		user_l2[i] = 0;
	}
	for (i = 0; i < USER_L3_TABLES; i++) {
		user_l3_index[i] = 0xffffU;
		for (j = 0; j < TABLE_ENTRIES; j++) {
			user_l3[i][j] = 0;
		}
	}
	flush_user_tlb64();
}

int arch64_user_map_range(uintptr_t base, size_t size, int executable)
{
	uintptr_t current;
	uintptr_t end;
	unsigned int slot;

	if (size == 0 || base + size < base || base + size > LOW_RAM_SIZE) {
		return -1;
	}
	current = base & ~(uintptr_t) (PAGE_SIZE - 1);
	end = (base + size + PAGE_SIZE - 1) & ~(uintptr_t) (PAGE_SIZE - 1);
	user_l1[0] = table_descriptor64(user_l2);
	while (current < end) {
		unsigned int l2_index = (unsigned int) ((current >> 21) & 0x1ffU);
		unsigned int l3_index = (unsigned int) ((current >> 12) & 0x1ffU);
		uint64_t flags = DESC_TABLE_OR_PAGE | DESC_ATTR_INDEX(0) |
			DESC_INNER_SHAREABLE | DESC_ACCESS_FLAG | DESC_AP_EL0_RW |
			DESC_PXN;

		for (slot = 0; slot < USER_L3_TABLES; slot++) {
			if (user_l3_index[slot] == l2_index) {
				break;
			}
		}
		if (slot == USER_L3_TABLES) {
			for (slot = 0; slot < USER_L3_TABLES; slot++) {
				if (user_l3_index[slot] == 0xffffU) {
					user_l3_index[slot] = (uint16_t) l2_index;
					user_l2[l2_index] = table_descriptor64(user_l3[slot]);
					break;
				}
			}
		}
		if (slot == USER_L3_TABLES) {
			arch64_user_unmap_all();
			return -2;
		}
		if (executable == 0) {
			flags |= DESC_UXN;
		}
		user_l3[slot][l3_index] = (current & DESC_ADDRESS_MASK) | flags;
		current += PAGE_SIZE;
	}
	flush_user_tlb64();
	return 0;
}

void arch64_sync_user_code(uintptr_t physical, size_t size)
{
	uintptr_t address;
	uintptr_t end;
	uintptr_t line_size;
	uint64_t ctr;

	if (size == 0) {
		return;
	}
	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	address = arch64_phys_to_virt(physical);
	end = address + size;
	line_size = 4ULL << ((ctr >> 16) & 0xfU);
	for (address &= ~(line_size - 1); address < end;
			address += line_size) {
		__asm__ volatile ("dc cvau, %0" :: "r" (address) : "memory");
	}
	__asm__ volatile ("dsb ish" ::: "memory");
	address = arch64_phys_to_virt(physical);
	line_size = 4ULL << (ctr & 0xfU);
	for (address &= ~(line_size - 1); address < end;
			address += line_size) {
		__asm__ volatile ("ic ivau, %0" :: "r" (address) : "memory");
	}
	__asm__ volatile ("dsb ish\n\tisb" ::: "memory");
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

static uint64_t device_block_descriptor64(uint64_t physical)
{
	return (physical & ~(L1_SPAN - 1)) | DESC_VALID |
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
	uint64_t outbound;
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

	/* SDHCI is in L1 slot 0x40; the other BCM2712 devices are in 0x41.
	   Their L2 indices do not overlap, so both slots can share this L2 table. */
	map_device_window64(MAILBOX_BASE, 0);
	map_device_window64(GIO_AON_BASE, 1);
	map_device_window64(GIC_BASE, 2);
	map_device_window64(SDHCI_BASE, 3);
	map_device_window64(PCIE2_BASE, 4);

	/* TTBR0 retains the low bootstrap map while TTBR1 supplies the canonical
	   high-half kernel alias. A later user-mode step can replace TTBR0 without
	   rebuilding or disturbing the kernel half. */
	bootstrap_l1[0] = table_descriptor64(low_l2);
	bootstrap_l1[(MAILBOX_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);
	bootstrap_l1[(SDHCI_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);
	ttbr1_l1[0] = table_descriptor64(low_l2);
	ttbr1_l1[(MAILBOX_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);
	ttbr1_l1[(SDHCI_BASE >> 30) & 0x1ffULL] = table_descriptor64(mmio_l2);

	/* PCIe2 translates its 32-bit non-prefetchable PCI address space to this
	   four-GiB CPU window. Map all four L1 blocks so firmware-assigned RP1 BARs
	   remain usable even when their PCI address is not zero. */
	for (outbound = PCIE2_OUTBOUND_BASE;
			outbound < PCIE2_OUTBOUND_BASE + PCIE2_OUTBOUND_SIZE;
			outbound += L1_SPAN) {
		unsigned int index = (unsigned int) ((outbound >> 30) & 0x1ffULL);
		uint64_t descriptor = device_block_descriptor64(outbound);

		bootstrap_l1[index] = descriptor;
		ttbr1_l1[index] = descriptor;
	}

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
	arch64_user_unmap_all();
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
	if (translate_el1_64(arch64_phys_to_virt((uintptr_t) SDHCI_BASE),
			&physical) != 0 || physical != (uintptr_t) SDHCI_BASE) {
		return -6;
	}
	if (translate_el1_64(arch64_phys_to_virt((uintptr_t) PCIE2_BASE),
			&physical) != 0 || physical != (uintptr_t) PCIE2_BASE) {
		return -7;
	}
	if (translate_el1_64(arch64_phys_to_virt(
			(uintptr_t) PCIE2_OUTBOUND_BASE), &physical) != 0 ||
			physical != (uintptr_t) PCIE2_OUTBOUND_BASE) {
		return -8;
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
