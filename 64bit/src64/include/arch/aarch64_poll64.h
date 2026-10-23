#ifndef MOWKOW64_ARCH_AARCH64_POLL64_H
#define MOWKOW64_ARCH_AARCH64_POLL64_H

#include <arch/poll_deadline64.h>

#define AARCH64_POLL_RECORDS64 32U

struct AARCH64_POLL_RECORD64 {
	const char *device, *stage, *register_name;
	uintptr_t address;
	uint64_t value;
	int error;
};

/* Task/boot callers only. Strings must have static lifetime. Record the sampled
 * register before W1C acknowledgement, reset or rollback destroys the evidence.
 * The IRQ-save ring never allocates or waits; output occurs after restoring IRQs.
 * Framebuffer output may be unavailable, but the latest records remain readable.
 */
void aarch64_poll_fail64(const char *device, const char *stage,
	const char *register_name, uintptr_t address, uint64_t value, int error);
size_t aarch64_poll_snapshot64(struct AARCH64_POLL_RECORD64 *records,
	size_t capacity, uint64_t *overwritten);

#endif
