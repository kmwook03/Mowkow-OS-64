#ifndef MOWKOW64_USER_ACCESS64_H
#define MOWKOW64_USER_ACCESS64_H

#include <stddef.h>
#include <stdint.h>
#include <address_space64.h>

/* Task-context owned lease, zero-initialize and never copy it while live.
 * The mm pin retains mappings/backing across preemption and blocking I/O.
 * No unmap/remap API yet: mutation/teardown must retry after end. Byte contents
 * are not locked; concurrent user writers need separate synchronization.
 */
struct USER_ACCESS64 {
	struct ADDRESS_SPACE64 *space;
	uintptr_t address;
	size_t size;
	unsigned int permissions;
};

int user_access_begin64(struct USER_ACCESS64 *access, const void *user,
	size_t size, unsigned int permissions);
void user_access_end64(struct USER_ACCESS64 *access);
/* Copy a checked prefix into the original pinned destination, without looking
 * up the current process again. Partial reads retain one backend operation.
 */
int copy_to_user_access64(const struct USER_ACCESS64 *access, const void *source, size_t size);

/* Task context, current process only; each operation retains its own mm pin.
 * Validate the entire user span before copying through supervisor aliases.
 * Kernel buffers must be valid and disjoint from user backing. Return 0 or
 * -14 for a bad user span, -22 for a bad kernel argument. Zero sizes retain
 * process64's non-NULL, within-range/one-past-end contract.
 */
int copy_from_user64(void *destination, const void *source, size_t size);
int copy_to_user64(void *destination, const void *source, size_t size);
/* Snapshot once; length excludes NUL. Failure empties destination. -36 means
 * no NUL within capacity; -14 means an inaccessible byte before termination.
 */
int64_t copy_string_from_user64(char *destination, size_t capacity, const char *source);

#endif
