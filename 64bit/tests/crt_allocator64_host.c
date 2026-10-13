#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define malloc crt_malloc64
#define free crt_free64
#define alloc fixture_alloc64
#include "../app64/crt/malloc.c"
#undef malloc
#undef free
#undef alloc

#define HEAP_BYTES64 (1024U * 1024U)
static _Alignas(16) uint8_t heap64[HEAP_BYTES64];
static size_t next64, capacity64, calls64, requests64[8192];
static uintptr_t forced64;

void *fixture_alloc64(size_t bytes)
{
	assert(calls64 < sizeof(requests64) / sizeof(requests64[0]));
	requests64[calls64++] = bytes;
	if (forced64 != 0) { return (void *) forced64; }
	if (bytes > capacity64 - next64) { return NULL; }
	void *result = heap64 + next64;
	next64 += bytes;
	return result;
}

static void reset64(void)
{
	blocks64 = NULL;
	allocator_failed64 = 0;
	next64 = calls64 = forced64 = 0;
	capacity64 = sizeof(heap64);
	memset(heap64, 0xa5, sizeof(heap64));
}

static void verify64(void)
{
	assert(!allocator_failed64 && blocks_valid64());
	for (struct BLOCK64 *block = blocks64; block != NULL; block = block->next) {
		uintptr_t end;

		assert(block_end64(block, &end) == 0);
		assert((uintptr_t) block >= (uintptr_t) heap64);
		assert(end <= (uintptr_t) heap64 + next64);
		if (block->state == BLOCK_FREE64 && block->next != NULL &&
			block->next->state == BLOCK_FREE64) {
			assert(end != (uintptr_t) block->next);
		}
	}
}

static void arithmetic64(void)
{
	reset64();
	assert(crt_malloc64(0) == NULL && calls64 == 0);
	for (size_t distance = 0; distance < HEADER64 + ALIGN64 - 1U; distance++) {
		assert(crt_malloc64(SIZE_MAX - distance) == NULL);
	}
	assert(calls64 == 0 && blocks64 == NULL);
	size_t result = 123;

	assert(merge_size64(SIZE_MAX - HEADER64, 0, &result) == 0 && result == SIZE_MAX);
	result = 123;
	assert(merge_size64(SIZE_MAX - HEADER64, 1, &result) == -22 && result == 123);
	assert(merge_size64(SIZE_MAX, 0, &result) == -22 && result == 123);
	assert(merge_size64(ALIGN64, SIZE_MAX, &result) == -22 && result == 123);
	struct BLOCK64 header = { .size = SIZE_MAX };
	uintptr_t end = 123;

	assert(block_end64(&header, &end) == -22 && end == 123);
	/* Preserve all existing heap bytes on failed syscall growth. */
	capacity64 = 0;
	assert(crt_malloc64(1) == NULL && calls64 == 2 && next64 == 0);
	assert(requests64[0] == CHUNK64 && requests64[1] == HEADER64 + ALIGN64);
	assert(blocks64 == NULL);
	for (size_t i = 0; i < sizeof(heap64); i++) { assert(heap64[i] == 0xa5); }
	reset64();
	capacity64 = HEADER64 + ALIGN64;
	assert(crt_malloc64(1) == heap64 + HEADER64 && calls64 == 2);
	assert(crt_malloc64(1) == NULL);
	verify64();
}

static void split_and_merge64(void)
{
	for (size_t spare = 0; spare <= HEADER64 + ALIGN64; spare += ALIGN64) {
		reset64();
		capacity64 = HEADER64 + ALIGN64 + spare;
		void *whole = crt_malloc64(ALIGN64 + spare);

		assert(whole != NULL);
		crt_free64(whole);
		void *ptr = crt_malloc64(1);

		assert(ptr == heap64 + HEADER64);
		assert(blocks64->size == (spare < HEADER64 + ALIGN64 ? ALIGN64 + spare : ALIGN64));
		assert((blocks64->next != NULL) == (spare == HEADER64 + ALIGN64));
		crt_free64(ptr);
		verify64();
	}
	static const int orders[][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
		{ 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
	};
	for (size_t order = 0; order < sizeof(orders) / sizeof(orders[0]); order++) {
		reset64();
		uint8_t *ptrs[3];
		for (int i = 0; i < 3; i++) { ptrs[i] = crt_malloc64(2048); assert(ptrs[i]); }
		size_t before = calls64;
		for (int i = 0; i < 3; i++) { crt_free64(ptrs[orders[order][i]]); verify64(); }
		assert(crt_malloc64(6000) == ptrs[0] && calls64 == before);
	}
	reset64();
	uint8_t *first = crt_malloc64(CHUNK64 - HEADER64);
	uint8_t *gap = fixture_alloc64(ALIGN64);
	uint8_t *second = crt_malloc64(CHUNK64 - HEADER64);

	memset(gap, 0x7c, ALIGN64);
	crt_free64(first);
	crt_free64(second);
	assert(blocks64->next != NULL); /* Direct alloc storage must never be merged over. */
	verify64();
	capacity64 = next64;
	assert(crt_malloc64(CHUNK64) == NULL);
	for (size_t i = 0; i < ALIGN64; i++) { assert(gap[i] == 0x7c); }
}

static void invalid_free64(void)
{
	reset64();
	uint8_t *first = crt_malloc64(256), *second = crt_malloc64(128);
	uint8_t before[CHUNK64];
	void *unmapped = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	assert(unmapped != MAP_FAILED);
	/* A plausible, sealed fake header inside a live payload is still not a block. */
	struct BLOCK64 *fake = (struct BLOCK64 *) (void *) first;
	*fake = (struct BLOCK64) { ALIGN64, NULL, BLOCK_USED64, 0 };
	fake->seal = block_seal64(fake);
	memcpy(before, heap64, sizeof(before));
	void *invalid[] = {
		NULL, first + 1, first + ALIGN64, first + HEADER64,
		heap64, heap64 + next64, unmapped, (void *) (UINTPTR_MAX - 15U), before
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		crt_free64(invalid[i]);
		assert(memcmp(before, heap64, sizeof(before)) == 0);
		verify64();
	}
	assert(munmap(unmapped, 4096) == 0);
	crt_free64(first);
	memcpy(before, heap64, sizeof(before));
	crt_free64(first);
	assert(memcmp(before, heap64, sizeof(before)) == 0);
	crt_free64(second); /* Merges away second's header. */
	memcpy(before, heap64, sizeof(before));
	crt_free64(second);
	assert(memcmp(before, heap64, sizeof(before)) == 0);
	assert(crt_malloc64(256) == first);
	verify64();
}

static void damage64(void)
{
	for (int field = 0; field < 7; field++) {
		reset64();
		uint8_t *first = crt_malloc64(32), *second = crt_malloc64(32);
		struct BLOCK64 *header = (struct BLOCK64 *) (void *) (second - HEADER64);
		uint8_t before[CHUNK64];

		if (field == 0) { header->size = SIZE_MAX; }
		if (field == 1) { header->size += ALIGN64; }
		if (field == 2) { header->state = BLOCK_FREE64; }
		if (field == 3) { header->state = 17; }
		if (field == 4) { header->next = (struct BLOCK64 *) (uintptr_t) 16; }
		if (field == 5) { header->next = blocks64; }
		if (field == 6) { header->seal ^= 1; }
		memcpy(before, heap64, sizeof(before));
		size_t before_calls = calls64;
		crt_free64(first); /* Damage later in the list must prevent an earlier free. */
		assert(allocator_failed64 && memcmp(before, heap64, sizeof(before)) == 0);
		assert(crt_malloc64(16) == NULL && calls64 == before_calls);
		crt_free64(second);
		assert(memcmp(before, heap64, sizeof(before)) == 0);
	}
	/* Every single-bit header fault, including pointer/seal bits, is caught
	 * before following a link or altering any earlier block.
	 */
	for (size_t byte = 0; byte < HEADER64; byte++) {
		for (unsigned int bit = 0; bit < 8; bit++) {
			reset64();
			uint8_t *first = crt_malloc64(32), *second = crt_malloc64(32);
			uint8_t before[CHUNK64];

			(second - HEADER64)[byte] ^= (uint8_t) (1U << bit);
			memcpy(before, heap64, sizeof(before));
			crt_free64(first);
			assert(allocator_failed64 && memcmp(before, heap64, sizeof(before)) == 0);
		}
	}
	/* Even a recomputed seal cannot authorize an overlapping/backward link or wrap. */
	for (int field = 0; field < 4; field++) {
		reset64();
		assert(crt_malloc64(32) != NULL);
		if (field == 0) { blocks64->next = blocks64; }
		if (field == 1) { blocks64->next = (struct BLOCK64 *) (heap64 + ALIGN64); }
		if (field == 2) { blocks64->size = SIZE_MAX & ~(size_t) (ALIGN64 - 1U); }
		if (field == 3) { blocks64->size = 0; }
		blocks64->seal = block_seal64(blocks64);
		assert(crt_malloc64(1) == NULL && allocator_failed64);
	}
	reset64();
	uint8_t *ptr = crt_malloc64(32);
	struct BLOCK64 *header = blocks64;
	struct BLOCK64 saved = *header;

	header->seal ^= 1;
	assert(crt_malloc64(1) == NULL);
	*header = saved;
	assert(crt_malloc64(1) == NULL); /* Corruption is sticky, never silently repaired. */
	crt_free64(ptr);
	assert(header->state == BLOCK_USED64);
}

static void bad_growth64(void)
{
	reset64();
	forced64 = UINTPTR_MAX - 15U;
	assert(crt_malloc64(1) == NULL && blocks64 == NULL);
	forced64 = (uintptr_t) heap64 + 1;
	assert(crt_malloc64(1) == NULL && blocks64 == NULL);
	reset64();
	uint8_t *ptr = crt_malloc64(CHUNK64 - HEADER64);
	uint8_t before[CHUNK64];

	memcpy(before, heap64, sizeof(before));
	forced64 = (uintptr_t) heap64;
	assert(crt_malloc64(1) == NULL);
	assert(memcmp(before, heap64, sizeof(before)) == 0);
	forced64 = (uintptr_t) heap64 + ALIGN64;
	assert(crt_malloc64(1) == NULL);
	assert(memcmp(before, heap64, sizeof(before)) == 0);
	forced64 = 0;
	crt_free64(ptr);
	verify64();
}

static uint32_t random64(uint32_t *seed)
{
	*seed = *seed * 1664525U + 1013904223U;
	return *seed >> 8;
}

static void churn64(void)
{
	reset64();
	for (int i = 0; i < 4000; i++) {
		uint8_t *ptr = crt_malloc64(1024);
		assert(ptr != NULL && ((uintptr_t) ptr & 15U) == 0);
		memset(ptr, (uint8_t) i, 1024);
		crt_free64(ptr);
	}
	assert(calls64 == 1 && next64 == CHUNK64);
	uint8_t *ptrs[64] = { 0 };
	size_t sizes[64] = { 0 };
	uint32_t seed = 0x12486321U;

	for (int action = 0; action < 10000; action++) {
		unsigned int slot = random64(&seed) % 64;
		if (ptrs[slot] != NULL) { crt_free64(ptrs[slot]); ptrs[slot] = NULL; }
		else {
			sizes[slot] = 1U + random64(&seed) % 2048U;
			ptrs[slot] = crt_malloc64(sizes[slot]);
			assert(ptrs[slot] != NULL && ((uintptr_t) ptrs[slot] & 15U) == 0);
			memset(ptrs[slot], (uint8_t) slot, sizes[slot]);
		}
		for (unsigned int i = 0; i < 64; i++) {
			if (ptrs[i] == NULL) { continue; }
			for (size_t n = 0; n < sizes[i]; n++) { assert(ptrs[i][n] == (uint8_t) i); }
			for (unsigned int j = i + 1; j < 64; j++) {
				if (ptrs[j] == NULL) { continue; }
				assert((uintptr_t) ptrs[i] + sizes[i] <= (uintptr_t) ptrs[j] ||
					(uintptr_t) ptrs[j] + sizes[j] <= (uintptr_t) ptrs[i]);
			}
		}
		verify64();
	}
	for (unsigned int i = 0; i < 64; i++) { crt_free64(ptrs[i]); }
	verify64();
	assert(blocks64->next == NULL && blocks64->size == next64 - HEADER64);
	capacity64 = next64;
	assert(crt_malloc64(next64 - HEADER64) == heap64 + HEADER64);
	assert(crt_malloc64(1) == NULL);
}

int main(void)
{
	arithmetic64();
	split_and_merge64();
	invalid_free64();
	damage64();
	bad_growth64();
	churn64();
	puts("CRT allocator overflow, free validation, corruption, coalescing and churn: PASS");
	return 0;
}
