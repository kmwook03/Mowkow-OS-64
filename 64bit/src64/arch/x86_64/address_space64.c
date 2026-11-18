#include <arch/address_space64.h>
#include "address_space_cpu64.h"

#define AS_ADDRESS_MASK64 0x000ffffffffff000ULL
#define AS_NX64 (1ULL << 63)
#define AS_PARENT64 7ULL

/* Slots: PML4, PDPT, low PD, image PT[2], high PD, heap PT, stack PT.
 * Shared PDs cover [1,4) GiB with the boot map's supervisor identity/cache
 * attributes. The low PD is private because [4,8) MiB is the user image.
 */
static uint64_t shared_pd64[3][512] __attribute__((aligned(4096)));
static int shared_ready64;

_Static_assert(USER_IMAGE_BASE64 == 2ULL * (1ULL << 21) &&
	USER_IMAGE_END64 == 4ULL * (1ULL << 21), "image PT indices");
_Static_assert((USER_HEAP_BASE64 >> 30) == 4 &&
	((USER_HEAP_BASE64 >> 21) & 511) == 0 &&
	((USER_STACK_BASE64 >> 21) & 511) == 1 &&
	((USER_STACK_END64 - 1) >> 21) == (USER_STACK_BASE64 >> 21),
	"high user table indices");

unsigned int as_arch_table_count64(void) { return 8; }
int as_arch_supported64(void) { return as_cpu_nx64() ? 0 : -95; }
uintptr_t as_arch_physical64(uintptr_t alias) { return as_cpu_physical64(alias); }
uintptr_t as_arch_alias64(uintptr_t physical) { return as_cpu_alias64(physical); }
int as_arch_irq_off64(void) { return as_cpu_irq_off64(); }

static uint64_t table_link64(uintptr_t alias)
{
	return as_arch_physical64(alias) | AS_PARENT64;
}

int as_arch_build64(const struct AS_TABLES64 *tables)
{
	uint64_t *pml4 = (uint64_t *) tables->page[0];
	uint64_t *pdpt = (uint64_t *) tables->page[1];
	uint64_t *low = (uint64_t *) tables->page[2];
	uint64_t *high = (uint64_t *) tables->page[5];

	if (!shared_ready64) {
		for (unsigned int i = 0; i < 3; i++) {
			uintptr_t pa = as_arch_physical64((uintptr_t) shared_pd64[i]);

			if ((pa & 4095) != 0 || pa >= USER_IMAGE_BASE64) { return -22; }
		}
		for (unsigned int i = 0; i < 3; i++) {
			for (unsigned int j = 0; j < 512; j++) {
				shared_pd64[i][j] = ((uint64_t) (i + 1) << 30) |
					((uint64_t) j << 21) | 0x83ULL;
			}
		}
		shared_ready64 = 1;
	}
	pml4[0] = table_link64(tables->page[1]);
	pdpt[0] = table_link64(tables->page[2]);
	for (unsigned int i = 0; i < 3; i++) {
		pdpt[i + 1] = as_arch_physical64((uintptr_t) shared_pd64[i]) | 3ULL;
	}
	pdpt[4] = table_link64(tables->page[5]);
	for (unsigned int i = 0; i < 512; i++) { low[i] = ((uint64_t) i << 21) | 0x83ULL; }
	low[2] = table_link64(tables->page[3]);
	low[3] = table_link64(tables->page[4]);
	high[0] = table_link64(tables->page[6]);
	high[1] = table_link64(tables->page[7]);
	return 0;
}

uint64_t *as_arch_leaf64(const struct AS_TABLES64 *tables, uintptr_t address)
{
	uint64_t *pml4 = (uint64_t *) tables->page[0];
	uint64_t *pdpt = (uint64_t *) tables->page[1];
	unsigned int pd_slot, pt_slot, pdpt_index, pd_index;
	uint64_t *pd;

	if (address >= USER_IMAGE_BASE64 && address < USER_IMAGE_END64) {
		pd_slot = 2; pdpt_index = 0;
		pd_index = (unsigned int) (address >> 21);
		pt_slot = 3 + pd_index - 2;
	} else if (address >= USER_HEAP_BASE64 && address < USER_HEAP_END64) {
		pd_slot = 5; pdpt_index = 4; pd_index = 0; pt_slot = 6;
	} else if (address >= USER_STACK_BASE64 && address < USER_STACK_END64) {
		pd_slot = 5; pdpt_index = 4; pd_index = 1; pt_slot = 7;
	} else { return NULL; }
	pd = (uint64_t *) tables->page[pd_slot];
	/* Hardware may set Accessed in parents after this root has run. */
	if ((pml4[0] & ~0x20ULL) != table_link64(tables->page[1]) ||
		(pdpt[pdpt_index] & ~0x20ULL) != table_link64(tables->page[pd_slot]) ||
		(pd[pd_index] & ~0x20ULL) != table_link64(tables->page[pt_slot])) { return NULL; }
	return &((uint64_t *) tables->page[pt_slot])[(address >> 12) & 511];
}

uint64_t as_arch_encode64(uintptr_t physical, unsigned int permissions)
{
	return physical | 5ULL | ((permissions & USER_WRITE64) ? 2ULL : 0) |
		((permissions & USER_EXEC64) ? 0 : AS_NX64);
}

int as_arch_decode64(uint64_t entry, uintptr_t *physical, unsigned int *permissions)
{
	if ((entry & 5) != 5 || (entry & ~(AS_ADDRESS_MASK64 | AS_NX64 | 0x67ULL)) != 0 ||
		((entry & 2) && !(entry & AS_NX64))) { return -14; }
	*physical = (uintptr_t) (entry & AS_ADDRESS_MASK64);
	*permissions = USER_READ64 | ((entry & 2) ? USER_WRITE64 : 0) |
		((entry & AS_NX64) ? 0 : USER_EXEC64);
	return 0;
}

void as_arch_install64(uintptr_t root)
{
	as_cpu_protect64();
	as_cpu_root64(root);
}

int as_arch_sync_code64(uintptr_t alias, size_t size)
{
	(void) alias;
	(void) size;
	/* Coherent x86 instruction/data caches; the later CR3 load serializes entry. */
	__asm__ volatile ("" ::: "memory");
	return 0;
}
