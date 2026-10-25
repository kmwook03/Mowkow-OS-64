#ifndef MOWKOW64_TEST_BLOCK_FAULT64_H
#define MOWKOW64_TEST_BLOCK_FAULT64_H

#include <block64.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Test-only transport: failure can follow a byte prefix, matching real devices
 * that modify DMA/media before returning an error. Callers still get 0/-1,
 * exactly as BLOCK64_OPS requires; no short-count ABI is invented here.
 */
struct BLOCK_FAULT_EVENT64 {
	uint64_t lba;
	uint32_t count;
	size_t completed;
	int write;
	int status;
};

struct BLOCK_FAULT64 {
	uint8_t *media;
	uint64_t sectors;
	uint64_t target;
	size_t prefix;
	uint32_t failures;
	int write;
	int armed;
	int persistent;
	struct BLOCK_FAULT_EVENT64 events[256];
	size_t event_count;
	int trace_overflow;
};

static void block_fault_arm64(struct BLOCK_FAULT64 *backend, int write,
	uint64_t target, size_t prefix, int persistent)
{
	backend->target = target;
	backend->write = write;
	backend->prefix = prefix;
	backend->persistent = persistent;
	backend->failures = 0;
	backend->armed = 1;
}

static int block_fault_transfer64(struct BLOCK_FAULT64 *backend,
	uint64_t lba, uint32_t count, void *buffer, int write)
{
	struct BLOCK_FAULT_EVENT64 *event;
	size_t bytes;
	size_t completed;
	int status = 0;

	if (buffer == NULL || lba >= backend->sectors ||
			count > backend->sectors - lba ||
			backend->sectors > SIZE_MAX / 512) {
		return -1;
	}
	bytes = (size_t) count * 512;
	completed = bytes;
	if (backend->armed != 0 && backend->write == write &&
			backend->target >= lba && backend->target - lba < count) {
		status = -1;
		completed = backend->prefix < bytes ? backend->prefix : bytes;
		backend->failures++;
		backend->armed = backend->persistent;
	}
	if (write != 0) {
		memcpy(backend->media + (size_t) lba * 512, buffer, completed);
	} else {
		memcpy(buffer, backend->media + (size_t) lba * 512, completed);
	}
	if (backend->event_count < 256) {
		event = &backend->events[backend->event_count++];
		event->lba = lba;
		event->count = count;
		event->completed = completed;
		event->write = write;
		event->status = status;
	} else {
		backend->trace_overflow = 1;
	}
	return status;
}

#endif
