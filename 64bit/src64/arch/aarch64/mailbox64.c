/* VideoCore property mailbox transport for BCM2712. */
#include <arch/arch64.h>
#include <stddef.h>
#include <stdint.h>

#define MAILBOX_BASE 0x107c013880ULL
#define MAILBOX_READ 0x00
#define MAILBOX_STATUS 0x18
#define MAILBOX_WRITE 0x20
#define MAILBOX_STATUS1 0x38
#define MAILBOX_EMPTY (1U << 30)
#define MAILBOX_FULL (1U << 31)
#define CACHE_LINE_SIZE 64U
#define MAILBOX_TIMEOUT_MS 1000U

static volatile uint32_t *mailbox_reg64(uint32_t offset)
{
	return (volatile uint32_t *) arch64_phys_to_virt(
		(uintptr_t) MAILBOX_BASE + offset);
}

static uint64_t counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static uint64_t deadline64(uint32_t milliseconds)
{
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	return counter64() + (frequency / 1000) * milliseconds;
}

static void cache_clean64(const void *buffer, size_t bytes)
{
	uintptr_t address;
	uintptr_t end;

	address = (uintptr_t) buffer & ~(uintptr_t) (CACHE_LINE_SIZE - 1);
	end = ((uintptr_t) buffer + bytes + CACHE_LINE_SIZE - 1) &
		~(uintptr_t) (CACHE_LINE_SIZE - 1);
	while (address < end) {
		__asm__ volatile ("dc cvac, %0" :: "r" (address) : "memory");
		address += CACHE_LINE_SIZE;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void cache_invalidate64(const void *buffer, size_t bytes)
{
	uintptr_t address;
	uintptr_t end;

	address = (uintptr_t) buffer & ~(uintptr_t) (CACHE_LINE_SIZE - 1);
	end = ((uintptr_t) buffer + bytes + CACHE_LINE_SIZE - 1) &
		~(uintptr_t) (CACHE_LINE_SIZE - 1);
	while (address < end) {
		__asm__ volatile ("dc ivac, %0" :: "r" (address) : "memory");
		address += CACHE_LINE_SIZE;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

int aarch64_mailbox_call(uint8_t channel, volatile uint32_t *message, size_t bytes)
{
	uint32_t request;
	uint32_t response;
	uint64_t deadline;
	uintptr_t message_physical;

	message_physical = arch64_virt_to_phys((uintptr_t) message);
	if (((message_physical & 0x0f) != 0) ||
		message_physical > 0xfffffff0ULL) {
		return -1;
	}
	request = (uint32_t) message_physical | (channel & 0x0f);
	cache_clean64((const void *) message, bytes);
	deadline = deadline64(MAILBOX_TIMEOUT_MS);
	while ((*mailbox_reg64(MAILBOX_STATUS1) & MAILBOX_FULL) != 0) {
		if ((int64_t) (counter64() - deadline) >= 0) {
			return -1;
		}
	}
	*mailbox_reg64(MAILBOX_WRITE) = request;
	__asm__ volatile ("dsb sy" ::: "memory");

	deadline = deadline64(MAILBOX_TIMEOUT_MS);
	for (;;) {
		while ((*mailbox_reg64(MAILBOX_STATUS) & MAILBOX_EMPTY) != 0) {
			if ((int64_t) (counter64() - deadline) >= 0) {
				return -1;
			}
		}
		response = *mailbox_reg64(MAILBOX_READ);
		if (response == request) {
			cache_invalidate64((const void *) message, bytes);
			return 0;
		}
		if ((int64_t) (counter64() - deadline) >= 0) {
			return -1;
		}
	}
}
