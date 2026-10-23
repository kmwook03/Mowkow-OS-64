/* VideoCore property mailbox. One CPU owner and one outstanding DMA request. */
#include <arch/aarch64_memory64.h>
#include <arch/aarch64_poll64.h>
#include <arch/aarch64_cache64.h>
#include "platform_io64.h"

#define MAILBOX_BASE 0x107c013880ULL
#define MAILBOX_READ 0x00U
#define MAILBOX_STATUS 0x18U
#define MAILBOX_WRITE 0x20U
#define MAILBOX_STATUS1 0x38U
#define MAILBOX_EMPTY (1U << 30)
#define MAILBOX_FULL (1U << 31)
#define MAILBOX_TIMEOUT_MS 1000U
#define MAILBOX_CAPACITY64 4096U

static uint32_t mailbox_buffer64[MAILBOX_CAPACITY64 / 4U] __attribute__((aligned(4096)));
static int mailbox_busy64;
static int mailbox_pending64;
static uint32_t mailbox_request64;
static size_t mailbox_clean_bytes64;

static uintptr_t mailbox_reg64(uint32_t offset)
{
	return arch64_phys_to_virt((uintptr_t) MAILBOX_BASE + offset);
}

static int mailbox_deadline64(struct POLL_DEADLINE64 *deadline)
{
	uint64_t frequency = a64_frequency64();
	int status = poll_deadline_init64(deadline, a64_counter64(), frequency,
		MAILBOX_TIMEOUT_MS, 1000U);

	if (status != 0) {
		aarch64_poll_fail64("mailbox", "deadline", "CNTFRQ_EL0", 0, frequency, status);
	}
	return status;
}

/* The advertised response length may exceed the supplied value capacity.
 * Firmware truncates such responses; traversal always uses buffer capacity.
 */
static int property_valid64(const volatile uint32_t *message, size_t bytes, int response)
{
	size_t words = bytes / 4U;
	size_t index = 2;

	if (message[0] != bytes || message[1] != (response ? 0x80000000U : 0)) { return -71; }
	while (index < words) {
		uint32_t capacity;
		size_t value_words;

		if (message[index] == 0) { return 0; }
		if (words - index < 3U) { return -71; }
		capacity = message[index + 1U];
		if (capacity > bytes || (response && !(message[index + 2U] & 0x80000000U))) {
			return -71;
		}
		if (!response && message[index + 2U] > capacity) { return -71; }
		value_words = ((size_t) capacity + 3U) / 4U;
		if (value_words > words - index - 3U) { return -71; }
		index += 3U + value_words;
	}
	return -71;
}

static int response_matches64(const volatile uint32_t *request, size_t bytes)
{
	size_t index = 2;

	if (property_valid64(mailbox_buffer64, bytes, 1) != 0) { return -71; }
	while (request[index] != 0) {
		if (mailbox_buffer64[index] != request[index] ||
			mailbox_buffer64[index + 1U] != request[index + 1U]) { return -71; }
		index += 3U + ((size_t) request[index + 1U] + 3U) / 4U;
	}
	return mailbox_buffer64[index] == 0 ? 0 : -71;
}

static int mailbox_wait64(const char *stage)
{
	struct POLL_DEADLINE64 deadline;
	uint32_t status;

	if (mailbox_deadline64(&deadline) != 0) { return -22; }

	for (;;) {
		status = a64_read32_64(mailbox_reg64(MAILBOX_STATUS));
		if (!(status & MAILBOX_EMPTY) &&
				a64_read32_64(mailbox_reg64(MAILBOX_READ)) == mailbox_request64) {
			if (a64_cache_dma64(mailbox_buffer64, mailbox_clean_bytes64, 1) != 0) {
				return -22;
			}
			mailbox_pending64 = 0;
			return 0;
		}
		if (poll_deadline_expired64(&deadline, a64_counter64())) {
			aarch64_poll_fail64("mailbox", stage, "STATUS", mailbox_reg64(MAILBOX_STATUS),
				status, -110);
			return -110;
		}
		a64_relax64();
	}
}

int aarch64_mailbox_call(uint8_t channel, volatile uint32_t *message, size_t bytes)
{
	uintptr_t physical;
	uintptr_t line = a64_cache_size64(0);
	uint64_t irq;
	struct POLL_DEADLINE64 deadline;
	uint32_t write_status;
	int status = -22;

	if (channel != 8U || message == NULL || ((uintptr_t) message & 15U) != 0 ||
		bytes < 12U || bytes > MAILBOX_CAPACITY64 || (bytes & 3U) != 0 ||
		bytes > UINTPTR_MAX - (uintptr_t) message ||
		!aarch64_ram_span64(a64_physical64((uintptr_t) message), bytes) ||
		line > 4096U || ((uintptr_t) mailbox_buffer64 & (line - 1U)) != 0) { return -22; }
	physical = a64_physical64((uintptr_t) mailbox_buffer64);
	if (!aarch64_ram_span64(physical, sizeof(mailbox_buffer64)) ||
		(physical & 15U) != 0 || (physical & (line - 1U)) != 0) {
		return -22;
	}
	if (property_valid64(message, bytes, 0) != 0) { return -71; }
	irq = arch64_irq_save();
	if (mailbox_busy64) { arch64_irq_restore(irq); return -16; }
	mailbox_busy64 = 1;
	arch64_irq_restore(irq);
	/* A timed-out VC transaction still owns the bounce buffer until its reply. */
	if (mailbox_pending64) {
		status = mailbox_wait64("drain-reply");
		if (status != 0) { goto out; }
	}
	mailbox_clean_bytes64 = (bytes + line - 1U) & ~(line - 1U);
	for (size_t i = 0; i < mailbox_clean_bytes64 / 4U; i++) {
		mailbox_buffer64[i] = i < bytes / 4U ? message[i] : 0;
	}
	status = a64_cache_dma64(mailbox_buffer64, mailbox_clean_bytes64, 0);
	if (status != 0) { goto out; }
	mailbox_request64 = (uint32_t) physical | channel;
	status = mailbox_deadline64(&deadline);
	if (status != 0) { goto out; }
	while ((write_status = a64_read32_64(mailbox_reg64(MAILBOX_STATUS1))) & MAILBOX_FULL) {
		if (poll_deadline_expired64(&deadline, a64_counter64())) {
			status = -110;
			aarch64_poll_fail64("mailbox", "write-space", "STATUS1", mailbox_reg64(MAILBOX_STATUS1),
				write_status, status);
			goto out;
		}
		a64_relax64();
	}
	mailbox_pending64 = 1;
	a64_write32_64(mailbox_reg64(MAILBOX_WRITE), mailbox_request64);
	a64_barrier64();
	status = mailbox_wait64("response");
	if (status != 0) { goto out; }
	status = response_matches64(message, bytes);
	if (status == 0) {
		for (size_t i = 0; i < bytes / 4U; i++) { message[i] = mailbox_buffer64[i]; }
	}
out:
	irq = arch64_irq_save();
	mailbox_busy64 = 0;
	arch64_irq_restore(irq);
	return status;
}
