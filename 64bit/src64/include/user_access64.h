#ifndef MOWKOW64_USER_ACCESS64_H
#define MOWKOW64_USER_ACCESS64_H

#include <stddef.h>
#include <stdint.h>

/* Task context, current process only. Its mappings/backing stay immutable until
 * exec detaches it; no concurrent user threads or asynchronous teardown exist.
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
