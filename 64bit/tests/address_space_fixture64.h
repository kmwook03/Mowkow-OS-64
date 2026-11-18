#ifndef MOWKOW64_ADDRESS_SPACE_FIXTURE64_H
#define MOWKOW64_ADDRESS_SPACE_FIXTURE64_H

/* Real common lifecycle and table builders; replace allocator and CPU only. */
#define ADDRESS_SPACE_HOST_TEST
#define AARCH64_CACHE_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src64/kernel/address_space64.c"
#ifdef TEST_AS_AARCH64
#include "../src64/arch/aarch64/address_space64.c"
#else
#include "../src64/arch/x86_64/address_space64.c"
#endif

#define ARENA_PAGES64 4096U
static unsigned char arena64[ARENA_PAGES64 * 4096U] __attribute__((aligned(4096)));
static size_t allocations64[ARENA_PAGES64];
static unsigned char occupied64[ARENA_PAGES64];
static size_t live_pages64;
static int alloc_fail64 = -1, free_fail64 = -1;
static int free_persistent64;
static unsigned int alloc_calls64, free_calls64;
static int irq_off64, nx64 = 1;
static uintptr_t installed64;
static unsigned int events64[1024], event_count64;
static uintptr_t cleaned64[1024];
static unsigned int clean_count64;
struct MEMMAN64 memman64;

static void event64(unsigned int event)
{
	assert(event_count64 < 1024);
	events64[event_count64++] = event;
}

uint64_t platform_irq_save64(void)
{
	uint64_t before = (uint64_t) irq_off64;

	irq_off64 = 1;
	return before;
}
void platform_irq_restore64(uint64_t state) { irq_off64 = (int) state; }

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	size_t pages = size / 4096;

	assert(man == &memman64 && size != 0 && (size & 4095) == 0);
	if ((int) alloc_calls64++ == alloc_fail64) { return 0; }
	for (size_t i = 0; i + pages <= ARENA_PAGES64; i++) {
		size_t j;

		for (j = 0; j < pages; j++) { if (occupied64[i + j]) { break; } }
		if (j != pages) { i += j; continue; }
		for (j = 0; j < pages; j++) { occupied64[i + j] = 1; }
		allocations64[i] = pages;
		live_pages64 += pages;
		memset(arena64 + i * 4096, 0xa5, size);
		return (uintptr_t) (arena64 + i * 4096);
	}
	return 0;
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t alias, size_t size)
{
	size_t index, pages = size / 4096;

	assert(man == &memman64 && alias >= (uintptr_t) arena64);
	assert(alias < (uintptr_t) arena64 + sizeof(arena64) && (alias & 4095) == 0);
	index = (alias - (uintptr_t) arena64) / 4096;
	assert(pages != 0 && size == pages * 4096 && allocations64[index] == pages);
	int call = (int) free_calls64++;

	if (call == free_fail64 || (free_persistent64 && call > free_fail64)) { return -12; }
	for (size_t j = 0; j < pages; j++) {
		assert(occupied64[index + j]); occupied64[index + j] = 0;
	}
	allocations64[index] = 0;
	live_pages64 -= pages;
	memset((void *) alias, 0xdd, size);
	return 0;
}

uintptr_t as_cpu_physical64(uintptr_t alias)
{
	if (alias >= (uintptr_t) arena64 && alias < (uintptr_t) arena64 + sizeof(arena64)) {
		return 0x800000 + alias - (uintptr_t) arena64;
	}
#ifndef TEST_AS_AARCH64
	if (alias >= (uintptr_t) shared_pd64 &&
		alias < (uintptr_t) shared_pd64 + sizeof(shared_pd64)) {
		return 0x200000 + alias - (uintptr_t) shared_pd64;
	}
#endif
	assert(0); return 0;
}

uintptr_t as_cpu_alias64(uintptr_t physical)
{
	assert(physical >= 0x800000 && physical < 0x800000 + sizeof(arena64));
	return (uintptr_t) arena64 + physical - 0x800000;
}
int as_cpu_irq_off64(void) { return irq_off64; }
void as_cpu_root64(uintptr_t root)
{
	assert(irq_off64 && (root & 4095) == 0);
	installed64 = root; event64(2);
}
#ifdef TEST_AS_AARCH64
void as_cpu_store_barrier64(void) { event64(1); }
void as_cpu_isb64(void) { event64(3); }
void as_cpu_tlb64(void) { event64(4); }
void as_cpu_completion64(void) { event64(5); }
void as_cpu_icache64(void) { event64(6); }
uint64_t a64_cache_ctr64(void) { return (4ULL << 16) | 3U; }
void a64_cache_line64(uintptr_t address, unsigned int operation)
{
	assert(operation == 2 && clean_count64 < 1024);
	cleaned64[clean_count64++] = address; event64(7);
}
void a64_cache_sync64(unsigned int instruction) { assert(!instruction); event64(8); }
#else
int as_cpu_nx64(void) { return nx64; }
void as_cpu_protect64(void) { assert(irq_off64); event64(1); }
#endif

/* A separate fresh boot per failure scenario, never a production recovery path. */
static void reset64(void)
{
	memset(&kernel_space64, 0, sizeof(kernel_space64));
	memset(spaces64, 0, sizeof(spaces64));
	memset(allocations64, 0, sizeof(allocations64));
	memset(occupied64, 0, sizeof(occupied64));
	active_space64 = NULL;
	live_pages64 = 0;
	alloc_calls64 = free_calls64 = 0;
	alloc_fail64 = free_fail64 = -1;
	free_persistent64 = 0;
	irq_off64 = 0; installed64 = 0; event_count64 = clean_count64 = 0;
	nx64 = 1;
}


static void snapshot64(const struct ADDRESS_SPACE64 *space, uint64_t pages[8][512])
{
	for (unsigned int i = 0; i < as_arch_table_count64(); i++) {
		memcpy(pages[i], (const void *) space->tables.page[i], 4096);
	}
}

static void unchanged64(const struct ADDRESS_SPACE64 *space, uint64_t pages[8][512])
{
	for (unsigned int i = 0; i < as_arch_table_count64(); i++) {
		assert(memcmp(pages[i], (const void *) space->tables.page[i], 4096) == 0);
	}
}

/* Independent hardware-format walk: no encode/decode/leaf helper is used.
 * Checks kernel alias permissions as well as holes outside public query APIs.
 */
static int walk64(const struct ADDRESS_SPACE64 *space, uintptr_t va,
	uintptr_t *pa, unsigned int *flags)
{
	const uint64_t *table = (const uint64_t *) space->tables.page[0];
#ifdef TEST_AS_AARCH64
	unsigned int shifts[] = {30, 21, 12};
	uint64_t entry = 0;

	if (va >= (1ULL << 39)) { return -1; }
	for (unsigned int level = 0; level < 3; level++) {
		entry = table[(va >> shifts[level]) & 511];
		if ((entry & 3) != 3) { return -1; }
		if (level < 2) { table = (const void *) as_cpu_alias64(entry & 0xfffffffff000ULL); }
	}
	*pa = (entry & 0xfffffffff000ULL) | (va & 4095);
	*flags = (entry & 64) ? 1 : 0;
	if (*flags) {
		if (!(entry & 128)) { *flags |= 2; }
		if (!(entry & (1ULL << 54))) { *flags |= 4; }
	}
	assert(entry & (1ULL << 53));
#else
	unsigned int shifts[] = {39, 30, 21, 12};
	int user = 1, writable = 1, executable = 1;
	uint64_t entry = 0;

	for (unsigned int level = 0; level < 4; level++) {
		entry = table[(va >> shifts[level]) & 511];
		if (!(entry & 1)) { return -1; }
		if (!(entry & 4)) { user = 0; }
		if (!(entry & 2)) { writable = 0; }
		if (entry & (1ULL << 63)) { executable = 0; }
		if (level == 2 && (entry & 128)) {
			*pa = (entry & 0xfffffffe00000ULL) | (va & 0x1fffff);
			*flags = user ? 1U | (writable ? 2U : 0) | (executable ? 4U : 0) : 0;
			return 0;
		}
		if (level < 3) {
			uintptr_t physical = entry & 0xffffffffff000ULL;

			if (physical >= 0x200000 && physical < 0x203000) {
				table = (const void *) ((uintptr_t) shared_pd64 + physical - 0x200000);
			} else { table = (const void *) as_cpu_alias64(physical); }
		}
	}
	*pa = (entry & 0xffffffffff000ULL) | (va & 4095);
	*flags = user ? 1U | (writable ? 2U : 0) | (executable ? 4U : 0) : 0;
#endif
	return 0;
}

#endif
