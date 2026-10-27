#include "mp_origin_store64.h"

static int overlap64(uintptr_t first, size_t first_bytes, uintptr_t second, size_t second_bytes)
{
	return first <= second ? second - first < first_bytes : first - second < second_bytes;
}

static int origin_index64(const struct MP_ORIGIN_STORE64 *store, uintptr_t address,
	size_t *index)
{
	if (store == NULL || index == NULL || store->count == 0 || store->block == 0 ||
		store->ids == NULL || store->values == NULL)
		return -22;
	if (address == store->emergency) {
		*index = store->count - 1;
		return 0;
	}
	if (address < store->heap || address - store->heap >= store->bytes ||
		(address - store->heap) % store->block != 0)
		return -34;
	*index = (address - store->heap) / store->block;
	return 0;
}

int mp_origin_init64(struct MP_ORIGIN_STORE64 *store, uintptr_t heap, size_t bytes,
	size_t block, uintptr_t emergency, uint8_t *ids, uint64_t *values, size_t capacity)
{
	if (store == NULL || ids == NULL || values == NULL || heap == 0 || emergency == 0 ||
		block == 0 || (block & (block - 1)) != 0 || heap % block != 0 ||
		bytes == 0 || bytes % block != 0 || bytes > UINTPTR_MAX - heap ||
		(emergency >= heap && emergency - heap < bytes) ||
		(uintptr_t) values % _Alignof(uint64_t) != 0)
		return -22;
	if (bytes / block == SIZE_MAX || bytes / block + 1 > capacity ||
		capacity > SIZE_MAX / sizeof(*values))
		return -7;
	struct MP_ORIGIN_STORE64 prepared = {
		.heap = heap, .bytes = bytes, .block = block, .emergency = emergency,
		.count = bytes / block + 1, .ids = ids, .values = values,
	};
	size_t value_bytes = prepared.count * sizeof(*values);

	if (prepared.count > UINTPTR_MAX - (uintptr_t) ids ||
		value_bytes > UINTPTR_MAX - (uintptr_t) values ||
		overlap64((uintptr_t) ids, prepared.count, (uintptr_t) values, value_bytes) ||
		overlap64((uintptr_t) ids, prepared.count, heap, bytes) ||
		overlap64((uintptr_t) values, value_bytes, heap, bytes) ||
		overlap64((uintptr_t) ids, prepared.count, (uintptr_t) store, sizeof(*store)) ||
		overlap64((uintptr_t) values, value_bytes, (uintptr_t) store, sizeof(*store)))
		return -22;

	for (size_t i = 0; i < prepared.count; i++) {
		ids[i] = 0;
		values[i] = 0;
	}
	*store = prepared;
	return 0;
}

int mp_origin_put64(struct MP_ORIGIN_STORE64 *store, uintptr_t address,
	struct MP_ORIGIN_VALUE64 value)
{
	size_t index;
	int result = origin_index64(store, address, &index);

	if (result < 0)
		return result;
	store->values[index] = value.id == 0 ? 0 : value.value;
	store->ids[index] = value.id;
	return 0;
}

int mp_origin_get64(const struct MP_ORIGIN_STORE64 *store, uintptr_t address,
	struct MP_ORIGIN_VALUE64 *value)
{
	if (value == NULL)
		return -22;
	size_t index;
	int result = origin_index64(store, address, &index);

	if (result < 0)
		return result;
	*value = (struct MP_ORIGIN_VALUE64) {
		.id = store->ids[index], .value = store->values[index],
	};
	return 0;
}

int mp_origin_drop64(struct MP_ORIGIN_STORE64 *store, uintptr_t address)
{
	return mp_origin_put64(store, address, (struct MP_ORIGIN_VALUE64) { 0, 0 });
}
