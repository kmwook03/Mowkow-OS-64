/* Actual deadline arithmetic and diagnostic ring; CPU/console primitives are hooks. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src64/arch/aarch64/poll64.c"

static unsigned int irq_depth64, outputs64, output_ready64;
static char output64[192];

uint64_t arch64_irq_save(void) { assert(!irq_depth64); irq_depth64++; return 0; }
void arch64_irq_restore(uint64_t state) { assert(state == 0 && irq_depth64 == 1); irq_depth64--; }
void arch64_dbg_puts(const char *value)
{
	assert(!irq_depth64 && strlen(value) < sizeof(output64));
	if (output_ready64) { memcpy(output64, value, strlen(value) + 1U); outputs64++; }
}
static void deadlines64(void)
{
	static const uint64_t frequencies[] = { 1, 999, 1000, 1000000, INT64_MAX, UINT64_MAX };
	static const uint32_t durations[] = { 0, 1, 1000, UINT32_MAX };
	static const uint32_t units[] = { 1, 1000, 1000000, UINT32_MAX };
	struct POLL_DEADLINE64 deadline;

	for (size_t f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++) {
		for (size_t d = 0; d < sizeof(durations) / sizeof(durations[0]); d++) {
			for (size_t u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
				__uint128_t expected = ((__uint128_t) frequencies[f] * durations[d] +
					units[u] - 1U) / units[u];
				uint64_t start = UINT64_MAX - 10U;
				int result = poll_deadline_init64(&deadline, start, frequencies[f],
					durations[d], units[u]);

				if (expected > INT64_MAX) {
					assert(result == -22 && poll_deadline_expired64(&deadline, start));
				} else {
					uint64_t ticks = (uint64_t) expected;

					assert(result == 0 && deadline.ticks == ticks);
					assert(poll_deadline_expired64(&deadline, start + ticks));
					if (ticks) { assert(!poll_deadline_expired64(&deadline, start + ticks - 1U)); }
				}
			}
		}
	}
	assert(poll_deadline_init64(&deadline, 9, 0, 1, 1000) == -22);
	assert(poll_deadline_expired64(&deadline, 9));
	assert(poll_deadline_init64(&deadline, 9, 1000, 1, 0) == -22);
	assert(poll_deadline_init64(NULL, 0, 1000, 1, 1000) == -22);
}
static void diagnostics64(void)
{
	struct AARCH64_POLL_RECORD64 snapshot[AARCH64_POLL_RECORDS64];
	uint64_t overwritten;
	static const char long_label[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	assert(aarch64_poll_snapshot64(snapshot, AARCH64_POLL_RECORDS64, &overwritten) == 0);
	aarch64_poll_fail64("mailbox", "response", "STATUS", 0x1234, 0x40000000, -110);
	assert(outputs64 == 0);
	assert(aarch64_poll_snapshot64(snapshot, 1, &overwritten) == 1 && overwritten == 0);
	assert(snapshot[0].address == 0x1234 && snapshot[0].value == 0x40000000 &&
		snapshot[0].error == -110 && strcmp(snapshot[0].device, "mailbox") == 0);
	output_ready64 = 1;
	for (unsigned int i = 0; i < 2U * AARCH64_POLL_RECORDS64; i++) {
		aarch64_poll_fail64("xhci1", "port-disable", "PORTSC", 0xabcd, i, -110);
	}
	assert(outputs64 == 64);
	assert(strstr(output64, "xhci1 port-disable PORTSC@0x000000000000abcd") != NULL);
	assert(strstr(output64, "value=0x000000000000003f error=0xffffffffffffff92\n") != NULL);
	assert(aarch64_poll_snapshot64(snapshot, AARCH64_POLL_RECORDS64, &overwritten) == 32);
	assert(overwritten == 33 && snapshot[0].value == 32 && snapshot[31].value == 63);
	assert(aarch64_poll_snapshot64(NULL, SIZE_MAX, &overwritten) == 0 && overwritten == 33);
	aarch64_poll_fail64(long_label, long_label, long_label, UINTPTR_MAX, UINT64_MAX, -22);
	assert(strlen(output64) < sizeof(output64));
	aarch64_poll_fail64(NULL, "stage", "status", 0, 0, -22);
	assert(outputs64 == 65 && !irq_depth64);
}
int main(void)
{
	deadlines64(); diagnostics64();
	puts("AArch64 deadlines and bounded failure diagnostics: PASS");
	return 0;
}
