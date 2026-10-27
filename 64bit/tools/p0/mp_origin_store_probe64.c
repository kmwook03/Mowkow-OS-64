/* Independent host invariants for the compact, allocation-free store. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mp_origin_store64.h"

#define BLOCKS64 65536
static uint8_t ids64[BLOCKS64 + 1];
static uint64_t values64[BLOCKS64 + 1];

static void full64(void)
{
	struct MP_ORIGIN_STORE64 store;
	struct MP_ORIGIN_VALUE64 value;

	assert(mp_origin_init64(&store, 0x100000, 0x200000, 32, 0x800000,
		ids64, values64, BLOCKS64 + 1) == 0);
	for (size_t i = 0; i < BLOCKS64; i++) {
		assert(mp_origin_put64(&store, 0x100000 + i * 32,
			(struct MP_ORIGIN_VALUE64) { 154, UINT64_MAX - i }) == 0);
	}
	assert(mp_origin_put64(&store, 0x800000,
		(struct MP_ORIGIN_VALUE64) { 255, UINT64_MAX }) == 0);
	/* Last publication must not evict the first or any other live record. */
	for (size_t i = 0; i < BLOCKS64; i++) {
		assert(mp_origin_get64(&store, 0x100000 + i * 32, &value) == 0);
		assert(value.id == 154 && value.value == UINT64_MAX - i);
	}
	assert(mp_origin_get64(&store, 0x800000, &value) == 0);
	assert(value.id == 255 && value.value == UINT64_MAX);
	assert(mp_origin_drop64(&store, 0x100000) == 0);
	assert(mp_origin_get64(&store, 0x100000, &value) == 0);
	assert(value.id == 0 && value.value == 0);
	assert(mp_origin_put64(&store, 0x100000,
		(struct MP_ORIGIN_VALUE64) { 1, 0 }) == 0);
	assert(mp_origin_get64(&store, 0x100000, &value) == 0);
	assert(value.id == 1 && value.value == 0);
	printf("origin store: 65536 live records + emergency, no eviction; metadata=%zu\n",
		sizeof(ids64) + sizeof(values64) + sizeof(store));
}

static void invalid64(void)
{
	struct MP_ORIGIN_STORE64 store;
	struct MP_ORIGIN_STORE64 saved;
	struct MP_ORIGIN_VALUE64 value = { 123, UINT64_C(0xabcdef) };

	assert(mp_origin_init64(&store, 0x100000, 64, 32, 0x800000,
		ids64, values64, BLOCKS64 + 1) == 0);
	saved = store;
	assert(mp_origin_put64(&store, 0x100001, value) == -34);
	assert(mp_origin_put64(&store, 0x100040, value) == -34);
	assert(mp_origin_put64(&store, 0xfffff, value) == -34);
	assert(mp_origin_get64(&store, UINTPTR_MAX, &value) == -34);
	assert(value.id == 123 && value.value == UINT64_C(0xabcdef));
	assert(mp_origin_get64(&store, 0x100000, NULL) == -22);
	assert(mp_origin_get64(NULL, 0x100000, &value) == -22);
	assert(mp_origin_init64(&store, 0x100000, 64, 32, 0x800000,
		ids64, values64, 2) == -7);
	assert(mp_origin_init64(&store, 0x100001, 64, 32, 0x800000,
		ids64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, 0x100000, 64, 3, 0x800000,
		ids64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, 0x100000, 65, 32, 0x800000,
		ids64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, UINTPTR_MAX - 31, 64, 32, 0x800000,
		ids64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, 0x100000, 64, 32, 0x100020,
		ids64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, 0x100000, 64, 32, 0x800000,
		(uint8_t *) values64, values64, BLOCKS64 + 1) == -22);
	assert(mp_origin_init64(&store, 0x100000, 64, 32, 0x800000,
		(uint8_t *) (UINTPTR_MAX - 1), values64, BLOCKS64 + 1) == -22);
	assert(memcmp(&store, &saved, sizeof(store)) == 0);
	assert(ids64[0] == 0 && values64[0] == 0);
	puts("origin store: invalid endpoints/alignment/capacity/overlap preserve store and output");
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "full") == 0)
		full64();
	else if (strcmp(argv[1], "invalid") == 0)
		invalid64();
	else
		assert(0);
	return 0;
}
