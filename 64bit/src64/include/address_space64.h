#ifndef MOWKOW64_ADDRESS_SPACE64_H
#define MOWKOW64_ADDRESS_SPACE64_H

#include <user_layout64.h>

#define USER_READ64 1U
#define USER_WRITE64 2U
#define USER_EXEC64 4U
#define USER_VMA_LIMIT64 256U

struct ADDRESS_SPACE64;

/* Stable-address owner descriptor; never copy/move it while referenced.
 * Mapping refs and transient pins prevent free. The owner retries a failed
 * free after releasing its mappings/pins; unpin never frees implicitly.
 */
struct USER_BACKING64 {
	uintptr_t kernel_address;
	uintptr_t physical;
	size_t size;
	unsigned int pins;
	int busy;
};

struct USER_PAGE64 {
	uintptr_t physical;
	uintptr_t kernel_address;
	unsigned int permissions;
};

/* Single CPU, task context, one lifecycle owner per space. No IRQ callers
 * except activate. Short IRQ-save state changes protect lifecycle claims;
 * allocation, table work and copies run outside those guards.
 */
int address_space_init64(void);
/* *space must start NULL. On failure it is NULL after successful rollback,
 * otherwise a retained, unusable handle that destroy must retry.
 */
int address_space_create64(struct ADDRESS_SPACE64 **space);
int address_space_destroy64(struct ADDRESS_SPACE64 **space);
/* Freeze mappings and retain tables/backing across preemption or I/O. A pin
 * does not freeze bytes: the caller must serialize writers for snapshots.
 * Map/destroy return -EBUSY until all pins are returned. No sleeping.
 */
int address_space_pin64(struct ADDRESS_SPACE64 *space);
int address_space_unpin64(struct ADDRESS_SPACE64 *space);
/* Eager private copy, including R/RX, preserving VA/permissions/holes and
 * aliases within one backing. Source must be current or quiesced by its owner.
 * IRQs must be enabled; source stays pinned through copy and rollback. Result
 * starts NULL; failure can retain an unusable handle for destroy to retry.
 * Borrowed physical mappings cannot be cloned (-ENOTSUP). No user entry or FD
 * inheritance is performed. Clone backing is owned/released by the new space.
 */
int address_space_clone64(struct ADDRESS_SPACE64 *source, struct ADDRESS_SPACE64 **result);
/* Inactive spaces only. Exact page-aligned, nonempty spans; no replacement,
 * W+X, guards, MMIO, or executable heap/stack. Preflight precedes all writes.
 * This low-level borrowed mapping does not retain physical storage. Its owner
 * must keep it alive until destroy. Process mappings use map_backing instead.
 */
int address_space_map64(struct ADDRESS_SPACE64 *space, uintptr_t address,
	uintptr_t physical, size_t size, unsigned int permissions);
/* Retain backing, recording a VMA with checked offset/length. Adjacent spans
 * with identical backing/permissions/offset progression coalesce. No mutation
 * on collision, VMA exhaustion (-ENOMEM), bad ranges or failed pin.
 */
int address_space_map_backing64(struct ADDRESS_SPACE64 *space, uintptr_t address,
	struct USER_BACKING64 *backing, size_t offset, size_t size, unsigned int permissions);
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
int user_backing_pin64(struct USER_BACKING64 *backing);
int user_backing_unpin64(struct USER_BACKING64 *backing);
/* After final code writes and before publication. Handles distinct user and
 * kernel aliases; validates a nonempty subset of this backing.
 */
int user_backing_sync_code64(const struct USER_BACKING64 *backing,
	size_t offset, size_t size);

#endif
