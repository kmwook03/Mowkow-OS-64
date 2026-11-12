#include <arch/address_space64.h>
#include "address_space_cpu64.h"

#define AS_ADDRESS_MASK64 0x0000fffffffff000ULL
#define AS_PXN64 (1ULL << 53)
#define AS_UXN64 (1ULL << 54)
/* Valid page, normal WB MAIR[0], EL0 access, inner-shareable, AF, nG, PXN. */
#define AS_USER_FLAGS64 (0xf43ULL | AS_PXN64)

/* Slots: L1, image L2, high L2, image L3[2], heap L3, stack L3.
 * TTBR1 is never modified or owned by these objects.
 */
_Static_assert((USER_IMAGE_BASE64 >> 21) == 2 &&
	(USER_IMAGE_END64 >> 21) == 4, "image L3 indices");
_Static_assert((USER_HEAP_BASE64 >> 30) == 4 &&
	((USER_HEAP_BASE64 >> 21) & 511) == 0 &&
	((USER_STACK_BASE64 >> 21) & 511) == 1 &&
	((USER_STACK_END64 - 1) >> 21) == (USER_STACK_BASE64 >> 21),
	"high user L1/L2 indices");

unsigned int as_arch_table_count64(void) { return 7; }
int as_arch_supported64(void) { return 0; }
uintptr_t as_arch_physical64(uintptr_t alias) { return as_cpu_physical64(alias); }
uintptr_t as_arch_alias64(uintptr_t physical) { return as_cpu_alias64(physical); }
int as_arch_irq_off64(void) { return as_cpu_irq_off64(); }

static uint64_t table_link64(uintptr_t alias)
{
	return as_arch_physical64(alias) | 3ULL;
}

int as_arch_build64(const struct AS_TABLES64 *tables)
{
	uint64_t *root = (uint64_t *) tables->page[0];
	uint64_t *image = (uint64_t *) tables->page[1];
	uint64_t *high = (uint64_t *) tables->page[2];

	root[0] = table_link64(tables->page[1]);
	root[4] = table_link64(tables->page[2]);
	image[2] = table_link64(tables->page[3]);
	image[3] = table_link64(tables->page[4]);
	high[0] = table_link64(tables->page[5]);
	high[1] = table_link64(tables->page[6]);
	return 0;
}

uint64_t *as_arch_leaf64(const struct AS_TABLES64 *tables, uintptr_t address)
{
	uint64_t *root = (uint64_t *) tables->page[0];
	unsigned int l1_index, l2_index, l2_slot, l3_slot;
	uint64_t *l2;

	if (address >= USER_IMAGE_BASE64 && address < USER_IMAGE_END64) {
		l1_index = 0; l2_slot = 1;
		l2_index = (unsigned int) (address >> 21);
		l3_slot = 3 + l2_index - 2;
	} else if (address >= USER_HEAP_BASE64 && address < USER_HEAP_END64) {
		l1_index = 4; l2_slot = 2; l2_index = 0; l3_slot = 5;
	} else if (address >= USER_STACK_BASE64 && address < USER_STACK_END64) {
		l1_index = 4; l2_slot = 2; l2_index = 1; l3_slot = 6;
	} else { return NULL; }
	l2 = (uint64_t *) tables->page[l2_slot];
	if (root[l1_index] != table_link64(tables->page[l2_slot]) ||
		l2[l2_index] != table_link64(tables->page[l3_slot])) { return NULL; }
	return &((uint64_t *) tables->page[l3_slot])[(address >> 12) & 511];
}

uint64_t as_arch_encode64(uintptr_t physical, unsigned int permissions)
{
	return physical | AS_USER_FLAGS64 | ((permissions & USER_WRITE64) ? 0 : 0x80ULL) |
		((permissions & USER_EXEC64) ? 0 : AS_UXN64);
}

int as_arch_decode64(uint64_t entry, uintptr_t *physical, unsigned int *permissions)
{
	if ((entry & AS_USER_FLAGS64) != AS_USER_FLAGS64 ||
		(entry & ~(AS_ADDRESS_MASK64 | AS_USER_FLAGS64 | AS_UXN64 | 0x80ULL)) != 0 ||
		(!(entry & 0x80) && !(entry & AS_UXN64))) { return -14; }
	*physical = (uintptr_t) (entry & AS_ADDRESS_MASK64);
	*permissions = USER_READ64 | ((entry & 0x80) ? 0 : USER_WRITE64) |
		((entry & AS_UXN64) ? 0 : USER_EXEC64);
	return 0;
}

void as_arch_install64(uintptr_t root)
{
	/* ASID 0, no reused translations. Finish publication before changing TTBR0,
	 * then invalidate even when reactivating the same root after offline edits.
	 */
	as_cpu_store_barrier64();
	as_cpu_root64(root);
	as_cpu_isb64();
	as_cpu_tlb64();
	as_cpu_completion64();
	as_cpu_isb64();
}

int as_arch_sync_code64(uintptr_t alias, size_t size)
{
	int status = a64_cache_walk64(alias, size, A64_CACHE_CODE_CLEAN64);

	if (status != 0) { return status; }
	a64_cache_sync64(0);
	/* User VA differs from the write alias. Invalidate all local instruction
	 * entries, rather than assuming alias-targeted IVAU covers the new user VA.
	 */
	as_cpu_icache64();
	as_cpu_completion64();
	as_cpu_isb64();
	return 0;
}
