/* Exercise the real mailbox transport with asynchronous firmware and cache hooks. */
#define AARCH64_IO_HOST_TEST
#define AARCH64_CACHE_HOST_TEST
#include "poll_test64.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src64/arch/aarch64/mailbox64.c"

static uint32_t message64[1024] __attribute__((aligned(4096)));
static uint64_t clock64, frequency64 = 1000;
static uintptr_t physical64 = 0x200000U;
static unsigned int writes64, cache64, irq64, reply64, full64, nested64;
static int mode64;

uint64_t arch64_irq_save(void) { unsigned int old = irq64; irq64 = 1; return old; }
void arch64_irq_restore(uint64_t state) { irq64 = (unsigned int) state; }
uint64_t a64_cache_ctr64(void) { return 4ULL << 16; }
void a64_cache_line64(uintptr_t address, unsigned int operation)
{
	assert(address >= (uintptr_t) mailbox_buffer64 &&
		address < (uintptr_t) mailbox_buffer64 + sizeof(mailbox_buffer64));
	assert(operation <= A64_CACHE_INVALIDATE64 && irq64 == 0);
	cache64++;
}
void a64_cache_sync64(unsigned int instruction) { assert(instruction == 0); }
uintptr_t a64_physical64(uintptr_t address)
{
	if (address == (uintptr_t) mailbox_buffer64) { return 0x100000U; }
	assert(address == (uintptr_t) message64);
	return physical64;
}
uint64_t a64_counter64(void) { assert(irq64 == 0); return clock64++; }
uint64_t a64_frequency64(void) { return frequency64; }
void a64_relax64(void) { clock64 += 100; }
void a64_barrier64(void) { assert(irq64 == 0); }

static void response64(void)
{
	mailbox_buffer64[1] = 0x80000000U;
	mailbox_buffer64[4] = 0x80000004U;
	mailbox_buffer64[5] = 0xabcdefU;
	if (mode64 == 1) { mailbox_buffer64[0] -= 4; }
	if (mode64 == 2) { mailbox_buffer64[1] |= 1; }
	if (mode64 == 3) { mailbox_buffer64[4] = 4; }
	if (mode64 == 4) { mailbox_buffer64[3] = UINT32_MAX; }
	if (mode64 == 5) { mailbox_buffer64[2]++; }
	if (mode64 == 6) { mailbox_buffer64[6] = 1; }
	if (mode64 == 7) { mailbox_buffer64[4] = UINT32_MAX; }
	if (mode64 == 8) { mailbox_buffer64[2] = 0; }
}
uint32_t a64_read32_64(uintptr_t address)
{
	assert(irq64 == 0);
	if (address == mailbox_reg64(MAILBOX_STATUS1)) { return full64 ? MAILBOX_FULL : 0; }
	if (address == mailbox_reg64(MAILBOX_STATUS)) { return reply64 ? 0 : MAILBOX_EMPTY; }
	assert(address == mailbox_reg64(MAILBOX_READ) && reply64);
	if (reply64 == 2) { reply64 = 1; return 0xdead0008U; }
	reply64 = 0;
	return mailbox_request64;
}
void a64_write32_64(uintptr_t address, uint32_t value)
{
	assert(address == mailbox_reg64(MAILBOX_WRITE) && value == 0x100008U && !irq64);
	writes64++;
	if (nested64) {
		nested64 = 0;
		assert(aarch64_mailbox_call(8, message64, 28) == -16);
	}
	if (mode64 == 9) { return; }
	response64();
	reply64 = 2; /* Ignore an unrelated mailbox response before the matching one. */
}

static void request64(void)
{
	memset(message64, 0, sizeof(message64));
	message64[0] = 28; message64[2] = 0x40008; message64[3] = 4;
}
static void unchanged64(int expected)
{
	uint32_t before[7];

	memcpy(before, message64, sizeof(before));
	assert(aarch64_mailbox_call(8, message64, 28) == expected);
	assert(memcmp(before, message64, sizeof(before)) == 0);
	assert(!mailbox_busy64 && !irq64);
}
int main(void)
{
	unsigned int writes, cache;

	request64(); nested64 = 1;
	assert(aarch64_mailbox_call(8, message64, 28) == 0);
	assert(message64[5] == 0xabcdefU && writes64 == 1 && cache64 == 2);
	for (mode64 = 1; mode64 <= 8; mode64++) {
		request64();
		if (mode64 == 7) {
			/* Valid truncation keeps the advertised length, even above capacity. */
			assert(aarch64_mailbox_call(8, message64, 28) == 0);
			assert(message64[4] == UINT32_MAX);
		} else { unchanged64(-71); }
	}
	mode64 = 0; request64(); writes = writes64; cache = cache64;
	assert(aarch64_mailbox_call(7, message64, 28) == -22);
	assert(aarch64_mailbox_call(8, NULL, 28) == -22);
	assert(aarch64_mailbox_call(8, message64 + 1, 28) == -22);
	assert(aarch64_mailbox_call(8, message64, SIZE_MAX) == -22);
	assert(aarch64_mailbox_call(8, message64, 0) == -22);
	physical64 = AARCH64_RAM_BYTES64 - 16; unchanged64(-22);
	physical64 = UINTPTR_MAX - 15U; unchanged64(-22);
	physical64 = 0x200000U;
	message64[0] = 24; unchanged64(-71);
	request64(); message64[3] = UINT32_MAX; unchanged64(-71);
	request64(); message64[4] = 0x80000000U; unchanged64(-71);
	request64(); message64[6] = 1; unchanged64(-71);
	assert(writes64 == writes && cache64 == cache);
	clock64 = UINT64_MAX - 500U;
	request64(); full64 = 1; unchanged64(-110);
	assert(strcmp(poll_last64.device, "mailbox") == 0 &&
		strcmp(poll_last64.stage, "write-space") == 0 && poll_last64.value == MAILBOX_FULL);
	assert(clock64 < 1000U); /* The timeout crossed the counter wrap. */
	assert(writes64 == writes && !mailbox_pending64);
	full64 = 0; mode64 = 9; request64(); unchanged64(-110);
	assert(mailbox_pending64 && writes64 == writes + 1);
	assert(strcmp(poll_last64.stage, "response") == 0 && poll_last64.value == MAILBOX_EMPTY);
	/* A second timeout must not overwrite a still firmware-owned DMA buffer. */
	mailbox_buffer64[5] = 0x12345678;
	request64(); message64[5] = 99; unchanged64(-110);
	assert(mailbox_buffer64[5] == 0x12345678 && writes64 == writes + 1);
	assert(strcmp(poll_last64.stage, "drain-reply") == 0);
	mode64 = 0; response64(); reply64 = 1;
	assert(aarch64_mailbox_call(8, message64, 28) == 0);
	assert(!mailbox_pending64 && writes64 == writes + 2 && message64[5] == 0xabcdefU);
	request64(); frequency64 = 0; writes = writes64;
	unchanged64(-22);
	assert(writes64 == writes && !mailbox_pending64 && poll_last64.value == 0 &&
		strcmp(poll_last64.register_name, "CNTFRQ_EL0") == 0);
	frequency64 = 1000;
	puts("aarch64 mailbox: PASS");
	return 0;
}
