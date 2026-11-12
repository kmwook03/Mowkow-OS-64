#include "address_space_fixture64.h"

static void initialization64(void)
{
	unsigned int count = as_arch_table_count64();

#ifndef TEST_AS_AARCH64
	reset64(); nx64 = 0;
	assert(address_space_init64() == -95 && live_pages64 == 0 && alloc_calls64 == 0);
	irq_off64 = 1;
	assert(address_space_activate64(NULL) == -16 && event_count64 == 0);
	irq_off64 = 0; nx64 = 1;
	assert(address_space_init64() == 0 && event_count64 == 0);
#endif
	for (unsigned int i = 0; i < count; i++) {
		reset64(); alloc_fail64 = (int) i;
		assert(address_space_init64() == -12 && live_pages64 == 0);
		assert(!irq_off64 && installed64 == 0 && event_count64 == 0);
		alloc_fail64 = -1;
		assert(address_space_init64() == 0 && live_pages64 == count);
	}
	reset64(); alloc_fail64 = 3; free_fail64 = 0;
	assert(address_space_init64() == -12 && live_pages64 == 3);
	assert(kernel_space64.state == AS_REAP64 && kernel_space64.tables.page[0] != 0);
	alloc_fail64 = free_fail64 = -1;
	assert(address_space_init64() == 0 && live_pages64 == count);
	assert(address_space_init64() == 0 && live_pages64 == count);
}

static void failures64(void)
{
	unsigned int count = as_arch_table_count64();

	for (unsigned int allocation = 0; allocation < count; allocation++) {
		struct ADDRESS_SPACE64 *space = NULL;

		reset64(); assert(address_space_init64() == 0);
		alloc_fail64 = (int) (alloc_calls64 + allocation);
		assert(address_space_create64(&space) == -12 && space == NULL);
		assert(live_pages64 == count && !irq_off64);
		alloc_fail64 = -1;
		assert(address_space_create64(&space) == 0);
		assert(address_space_destroy64(&space) == 0 && space == NULL);
		assert(live_pages64 == count);
		for (unsigned int freeing = 0; freeing < allocation; freeing++) {
			reset64(); assert(address_space_init64() == 0);
			alloc_fail64 = (int) (alloc_calls64 + allocation);
			free_fail64 = (int) freeing;
			assert(address_space_create64(&space) == -12 && space != NULL);
			assert(live_pages64 == count + allocation - freeing);
			assert(space->state == AS_REAP64);
			irq_off64 = 1;
			assert(address_space_activate64(space) == -16 && !event_count64);
			irq_off64 = 0;
			assert(address_space_map64(space, 0x400000, 0x900000, 4096, 1) == -16);
			free_fail64 = -1;
			assert(address_space_destroy64(&space) == 0 && !space);
			assert(live_pages64 == count);
		}
	}
	for (unsigned int i = 0; i < count; i++) {
		struct ADDRESS_SPACE64 *space = NULL;

		reset64(); assert(address_space_create64(&space) == 0);
		free_fail64 = (int) i;
		assert(address_space_destroy64(&space) == -12 && space != NULL);
		assert(live_pages64 == 2 * count - i && space->state == AS_REAP64);
		free_fail64 = -1;
		assert(address_space_destroy64(&space) == 0 && !space && live_pages64 == count);
	}
}

static void mappings64(void)
{
	struct ADDRESS_SPACE64 *first = NULL, *second = NULL;
	struct USER_BACKING64 one = {0}, two = {0};
	struct USER_PAGE64 page = {0};
	static uint64_t before[8][512];
	uintptr_t pa;
	unsigned int flags;

	reset64(); assert(address_space_create64(&first) == 0);
	assert(address_space_create64(&second) == 0);
	assert(user_backing_alloc64(&one, 0x400000) == 0);
	assert(user_backing_alloc64(&two, 0x400000) == 0);
	for (size_t i = 0; i < one.size; i++) { assert(((unsigned char *) one.kernel_address)[i] == 0); }
	assert(address_space_map64(first, 0x400000, one.physical, 0x400000, 5) == 0);
	assert(address_space_map64(second, 0x400000, two.physical, 4096, 3) == 0);
	assert(address_space_map64(first, 0x100000000ULL, two.physical, 0x100000, 3) == 0);
	assert(address_space_map64(first, 0x100200000ULL, two.physical + 0x100000, 0x10000, 3) == 0);
	/* Test aliases use real, distinct allocations despite equal user VAs. */
	assert(address_space_query64(first, 0x400123, &page) == 0);
	assert(page.physical == one.physical + 0x123 && page.permissions == 5);
	*(unsigned char *) page.kernel_address = 0x31;
	assert(address_space_query64(second, 0x400123, &page) == 0);
	assert(*(unsigned char *) page.kernel_address == 0);
	*(unsigned char *) page.kernel_address = 0x72;
	assert(((unsigned char *) one.kernel_address)[0x123] == 0x31);
	assert(address_space_validate64(first, 0x400001, 0x3fffff, 5) == 0);
	assert(address_space_validate64(first, 0x400001, 1, 2) == -14);
	assert(address_space_validate64(first, 0x100000fffULL, 0x100000 - 0xfff, 3) == 0);
	assert(address_space_validate64(first, 0x100200000ULL, 0x10000, 4) == -14);
	assert(address_space_validate64(first, 0x10020ffffULL, 2, 1) == -14);
	assert(address_space_validate64(second, 0x400fff, 2, 1) == -14);
	assert(address_space_validate64(first, UINTPTR_MAX - 1, 3, 1) == -14);
	assert(address_space_validate64(first, 0x400000, 0, 1) == -22);
	assert(address_space_validate64(first, 0x400000, 1, 0) == -22);
	assert(address_space_validate64(first, 0x400000, 1, 8) == -22);
	assert(address_space_query64(first, 0x1001fffffULL, &page) == -14);
	assert(address_space_query64(first, 0x100210000ULL, &page) == -14);
	assert(address_space_query64(first, 0x800000, &page) == -14);
	assert(address_space_query64(first, 0xffffff8000400000ULL, &page) == -14);
	assert(walk64(first, 0x7fffff, &pa, &flags) == 0 && pa == one.physical + 0x3fffff);
	assert(flags == 5);
	assert(walk64(first, 0x1000fffffULL, &pa, &flags) == 0 && flags == 3);
	assert(walk64(first, 0x10020ffffULL, &pa, &flags) == 0 && flags == 3);
	assert(walk64(first, 0x1001ff000ULL, &pa, &flags) == -1);
	assert(walk64(first, 0x100210000ULL, &pa, &flags) == -1);
	assert(walk64(first, 0x100100000ULL, &pa, &flags) == -1);
	assert(walk64(&kernel_space64, 0x400000, &pa, &flags) == -1);
	assert(walk64(&kernel_space64, 0x100000000ULL, &pa, &flags) == -1);
#ifdef TEST_AS_AARCH64
	assert(walk64(first, 0x800000, &pa, &flags) == -1);
	assert(walk64(first, 0x1000120000ULL, &pa, &flags) == -1);
	assert(*(uint64_t *) (first->tables.page[3]) == (one.physical | 0x20000000000fc3ULL));
#else
	assert(walk64(first, 0x100000, &pa, &flags) == 0 && pa == 0x100000 && flags == 0);
	assert(walk64(first, 0x800000, &pa, &flags) == 0 && pa == 0x800000 && flags == 0);
	assert(walk64(first, 0x1fffffff, &pa, &flags) == 0 && pa == 0x1fffffff && flags == 0);
	assert(walk64(first, 0xfed00000, &pa, &flags) == 0 && pa == 0xfed00000 && flags == 0);
	assert(*(uint64_t *) (first->tables.page[3]) == (one.physical | 5));
#endif
	/* A collision at the END of a request must not publish its valid prefix. */
	assert(address_space_map64(second, 0x600000, two.physical + 4096, 4096, 1) == 0);
	snapshot64(second, before);
	assert(address_space_map64(second, 0x5ff000, two.physical, 8192, 1) == -17);
	unchanged64(second, before);
	assert(address_space_map64(second, 0x401001, two.physical, 4096, 1) == -22);
	assert(address_space_map64(second, 0x401000, two.physical + 1, 4096, 1) == -22);
	assert(address_space_map64(second, 0x401000, two.physical, 0, 1) == -22);
	assert(address_space_map64(second, 0x401000, two.physical, 4095, 1) == -22);
	assert(address_space_map64(second, 0x401000, two.physical, 4096, 7) == -22);
	assert(address_space_map64(second, 0x401000, two.physical, 4096, 2) == -22);
	assert(address_space_map64(second, 0x401000, 0x7ff000, 4096, 1) == -22);
	assert(address_space_map64(second, 0x401000, 0x20000000, 4096, 1) == -22);
	assert(address_space_map64(second, 0x401000, 0x1ffff000, 8192, 1) == -22);
	assert(address_space_map64(second, 0x401000, UINTPTR_MAX - 4095, 8192, 1) == -22);
	assert(address_space_map64(second, UINTPTR_MAX - 4095, two.physical, 8192, 1) == -22);
	assert(address_space_map64(second, 0x7ff000, two.physical, 8192, 1) == -22);
	assert(address_space_map64(second, 0x1001ff000ULL, two.physical, 4096, 3) == -22);
	assert(address_space_map64(second, 0x100000000ULL, two.physical, 4096, 5) == -22);
	unchanged64(second, before);
	/* Corrupt parent ownership cannot redirect the software walk. */
	((uint64_t *) second->tables.page[0])[0] ^= 4096;
	snapshot64(second, before);
	assert(address_space_map64(second, 0x401000, two.physical, 4096, 1) == -22);
	assert(address_space_query64(second, 0x400000, &page) == -14);
	unchanged64(second, before);
	((uint64_t *) second->tables.page[0])[0] ^= 4096;
	assert(address_space_destroy64(&first) == 0);
	assert(address_space_destroy64(&second) == 0);
	assert(user_backing_free64(&one) == 0 && user_backing_free64(&two) == 0);
	assert(live_pages64 == as_arch_table_count64());
}

static void activation64(void)
{
	struct ADDRESS_SPACE64 *all[5] = {0};
	struct USER_PAGE64 page;
	unsigned int allocations, frees;
	uintptr_t pa;
	unsigned int flags;

	reset64();
	assert(address_space_create64(NULL) == -22);
	assert(address_space_destroy64(NULL) == -22);
	for (unsigned int i = 0; i < 4; i++) { assert(address_space_create64(&all[i]) == 0); }
	assert(address_space_create64(&all[4]) == -12 && all[4] == NULL);
	assert(address_space_create64(&all[0]) == -22);
	assert(address_space_map64(all[0], 0x400000, 0x1000000, 4096, 1) == 0);
	assert(walk64(all[0], 0x400000, &pa, &flags) == 0 && flags == 1);
	assert(address_space_query64(all[0], 0x400000, &page) == 0 && page.permissions == 1);
	assert(address_space_activate64(all[0]) == -1 && event_count64 == 0);
	allocations = alloc_calls64; frees = free_calls64;
	irq_off64 = 1;
	assert(address_space_activate64(all[0]) == 0);
	assert(installed64 == as_cpu_physical64(all[0]->tables.page[0]));
#ifdef TEST_AS_AARCH64
	const unsigned int sequence[] = {1, 2, 3, 4, 5, 3};
#else
	const unsigned int sequence[] = {1, 2};
#endif
	assert(event_count64 == sizeof(sequence) / sizeof(sequence[0]));
	assert(memcmp(events64, sequence, sizeof(sequence)) == 0);
	assert(address_space_destroy64(&all[0]) == -16);
	assert(address_space_map64(all[0], 0x401000, 0x1000000, 4096, 1) == -16);
	assert(address_space_activate64(all[1]) == 0);
	assert(installed64 == as_cpu_physical64(all[1]->tables.page[0]));
	assert(address_space_activate64(NULL) == 0);
	assert(installed64 == as_cpu_physical64(kernel_space64.tables.page[0]));
	assert(alloc_calls64 == allocations && free_calls64 == frees && irq_off64);
	irq_off64 = 0;
	assert(address_space_destroy64(&all[0]) == 0);
	assert(address_space_create64(&all[4]) == 0);
	assert(address_space_query64(all[4], 0x400000, &page) == -14);
	for (unsigned int i = 0; i < 5; i++) { assert(address_space_destroy64(&all[i]) == 0); }
	assert(live_pages64 == as_arch_table_count64());
}

static void preservation64(void)
{
	struct ADDRESS_SPACE64 *running = NULL, *failed = NULL;
	struct USER_BACKING64 backing = {0};
	struct USER_PAGE64 page;
	static uint64_t before[8][512];
	unsigned int count = as_arch_table_count64();
	size_t live;

	reset64(); assert(address_space_create64(&running) == 0);
	assert(user_backing_alloc64(&backing, 4096) == 0);
	memset((void *) backing.kernel_address, 0x51, 4096);
	assert(address_space_map64(running, 0x400000, backing.physical, 4096, 3) == 0);
	irq_off64 = 1; assert(address_space_activate64(running) == 0); irq_off64 = 0;
	live = live_pages64; snapshot64(running, before);
	for (unsigned int i = 0; i < count; i++) {
		alloc_fail64 = (int) (alloc_calls64 + i);
		assert(address_space_create64(&failed) == -12 && failed == NULL);
		assert(live_pages64 == live && active_space64 == running);
		unchanged64(running, before);
		assert(address_space_query64(running, 0x400fff, &page) == 0);
		assert(*(unsigned char *) page.kernel_address == 0x51);
	}
	alloc_fail64 = -1;
	/* A failed free reserves the slot until the owner retries cleanup. */
	assert(address_space_create64(&failed) == 0);
	free_fail64 = (int) free_calls64;
	assert(address_space_destroy64(&failed) == -12 && failed != NULL);
	struct ADDRESS_SPACE64 *others[3] = {0};

	assert(address_space_create64(&others[0]) == 0);
	assert(address_space_create64(&others[1]) == 0);
	assert(address_space_create64(&others[2]) == -12);
	assert(address_space_destroy64(&failed) == 0);
	assert(address_space_create64(&others[2]) == 0);
	for (unsigned int i = 0; i < 3; i++) { assert(address_space_destroy64(&others[i]) == 0); }
	assert(live_pages64 == live); unchanged64(running, before);
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	assert(address_space_destroy64(&running) == 0 && user_backing_free64(&backing) == 0);
	assert(live_pages64 == count);
}

static void backing64(void)
{
	struct USER_BACKING64 backing = {0}, saved;
	unsigned int frees;

	reset64();
	assert(user_backing_alloc64(&backing, 0) == -22);
	assert(user_backing_alloc64(&backing, SIZE_MAX) == -22);
	assert(user_backing_alloc64(&backing, 4095) == -22);
	alloc_fail64 = 0;
	assert(user_backing_alloc64(&backing, 4096) == -12 && backing.kernel_address == 0);
	alloc_fail64 = -1;
	assert(user_backing_alloc64(&backing, 8192) == 0);
	assert(user_backing_alloc64(&backing, 4096) == -22);
	assert(user_backing_sync_code64(&backing, 63, 2) == 0);
#ifdef TEST_AS_AARCH64
	const unsigned int sequence[] = {7, 7, 8, 6, 5, 3};

	assert(event_count64 == 6 && memcmp(events64, sequence, sizeof(sequence)) == 0);
	assert(clean_count64 == 2 && cleaned64[0] == backing.kernel_address);
	assert(cleaned64[1] == backing.kernel_address + 64);
#else
	assert(event_count64 == 0 && clean_count64 == 0);
	(void) cleaned64;
#endif
	event_count64 = 0;
	assert(user_backing_sync_code64(&backing, 8191, 2) == -22);
	assert(user_backing_sync_code64(&backing, 0, 0) == -22);
	assert(user_backing_sync_code64(&backing, SIZE_MAX, 1) == -22 && event_count64 == 0);
	saved = backing;
	backing.physical = 0;
	frees = free_calls64;
	assert(user_backing_free64(&backing) == -22 && free_calls64 == frees);
	backing = saved;
	saved = backing; free_fail64 = (int) free_calls64;
	assert(user_backing_free64(&backing) == -12 && memcmp(&backing, &saved, sizeof(saved)) == 0);
	assert(live_pages64 == 2);
	free_fail64 = -1;
	assert(user_backing_free64(&backing) == 0 && backing.size == 0 && live_pages64 == 0);
	frees = free_calls64;
	assert(user_backing_free64(&backing) == 0 && free_calls64 == frees);
}

int main(void)
{
	initialization64(); failures64(); mappings64(); activation64(); preservation64(); backing64();
	puts("address spaces: lifecycle, isolation, permissions, rollback, activation PASS");
	return 0;
}
