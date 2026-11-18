#ifndef MOWKOW64_ADDRESS_SPACE64_H
#define MOWKOW64_ADDRESS_SPACE64_H

#include <user_layout64.h>

#define USER_READ64 1U
#define USER_WRITE64 2U
#define USER_EXEC64 4U

struct ADDRESS_SPACE64;

/* Process-owned storage. Do not copy ownership or free until every referring
 * space has been destroyed. Only kernel callers may map owned pool pages.
 */
struct USER_BACKING64 {
	uintptr_t kernel_address;
	uintptr_t physical;
	size_t size;
};

struct USER_PAGE64 {
	uintptr_t physical;
	uintptr_t kernel_address;
	unsigned int permissions;
};

/* Single CPU, task context, one lifecycle owner per space. No IRQ callers
 * except activate. Init prepares an immutable kernel root without installing
 * it; M1 deliberately leaves the legacy loader and scheduler in control.
 */
int address_space_init64(void);
/* *space must start NULL. On failure it is NULL after successful rollback,
 * otherwise a retained, unusable handle that destroy must retry.
 */
int address_space_create64(struct ADDRESS_SPACE64 **space);
int address_space_destroy64(struct ADDRESS_SPACE64 **space);
/* Inactive spaces only. Exact page-aligned, nonempty spans; no replacement,
 * W+X, guards, MMIO, or executable heap/stack. Preflight precedes all writes.
 * Physical storage remains caller-owned and must already be initialized.
 */
int address_space_map64(struct ADDRESS_SPACE64 *space, uintptr_t address,
	uintptr_t physical, size_t size, unsigned int permissions);
/* Byte address translation with leaf permissions; never validates a kernel
 * alias as a user address. Zero-sized validation is -22; syscall-specific
 * zero-size semantics belong at the syscall boundary.
 */
int address_space_query64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	struct USER_PAGE64 *page);
int address_space_validate64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	size_t size, unsigned int access);
/* IRQs must already be disabled. NULL selects the prepared kernel root.
 * No allocation, sleeping, freeing, or legacy-root fallback on this path.
 */
int address_space_activate64(struct ADDRESS_SPACE64 *space);
/* Page-multiple size, zero-filled before return; output must start all zero.
 * Failed frees preserve the whole descriptor for a bounded caller retry.
 */
int user_backing_alloc64(struct USER_BACKING64 *backing, size_t size);
int user_backing_free64(struct USER_BACKING64 *backing);
/* After final code writes and before publication. Handles distinct user and
 * kernel aliases; validates a nonempty subset of this backing.
 */
int user_backing_sync_code64(const struct USER_BACKING64 *backing,
	size_t offset, size_t size);

#endif
