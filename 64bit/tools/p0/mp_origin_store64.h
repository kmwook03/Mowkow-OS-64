#ifndef MOWKOW64_MP_ORIGIN_STORE64_H
#define MOWKOW64_MP_ORIGIN_STORE64_H

#include <stddef.h>
#include <stdint.h>

/* Single VM/task only. Metadata never roots objects and never allocates.
 * Every heap block and the emergency object have a dedicated slot; no live
 * record can be evicted by capacity pressure. Callers own both array spans
 * and keep the configuration immutable after successful initialization.
 */
struct MP_ORIGIN_STORE64 {
	uintptr_t heap;
	uintptr_t emergency;
	size_t bytes;
	size_t block;
	size_t count;
	uint8_t *ids;
	uint64_t *values;
};

struct MP_ORIGIN_VALUE64 {
	uint8_t id;
	uint64_t value;
};

_Static_assert(sizeof(uintptr_t) == 8, "the origin store targets the 64-bit ports");
_Static_assert(sizeof(struct MP_ORIGIN_STORE64) == 56, "origin budget changed");

int mp_origin_init64(struct MP_ORIGIN_STORE64 *store, uintptr_t heap, size_t bytes,
	size_t block, uintptr_t emergency, uint8_t *ids, uint64_t *values, size_t capacity);
int mp_origin_put64(struct MP_ORIGIN_STORE64 *store, uintptr_t address,
	struct MP_ORIGIN_VALUE64 value);
int mp_origin_get64(const struct MP_ORIGIN_STORE64 *store, uintptr_t address,
	struct MP_ORIGIN_VALUE64 *value);
int mp_origin_drop64(struct MP_ORIGIN_STORE64 *store, uintptr_t address);

#endif
