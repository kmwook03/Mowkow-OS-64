/* Bounded boot/runtime hardware-failure evidence, independent of framebuffer readiness. */
#include <arch/arch64.h>
#include <arch/aarch64_poll64.h>
#include <kstring64.h>

static struct AARCH64_POLL_RECORD64 poll_records64[AARCH64_POLL_RECORDS64];
static size_t poll_next64, poll_count64;
static uint64_t poll_overwritten64;

static int label64(char *text, size_t capacity, size_t *used,
	const char *value, size_t limit)
{
	char prefix[33];
	int status;

	if (limit >= sizeof(prefix)) { return -22; }
	/* Deliberate ASCII display-prefix truncation; the ring retains full labels. */
	status = str_copy64(prefix, limit + 1U, value);
	if (status != 0 && status != -7) { return status; }
	return str_append64(text, capacity, used, prefix);
}

static int hex64(char *text, size_t capacity, size_t *used, uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	char number[19];
	size_t index = 2;

	number[0] = '0'; number[1] = 'x';
	for (int shift = 60; shift >= 0; shift -= 4) { number[index++] = digits[(value >> shift) & 15U]; }
	number[index] = '\0';
	return str_append64(text, capacity, used, number);
}

void aarch64_poll_fail64(const char *device, const char *stage,
	const char *register_name, uintptr_t address, uint64_t value, int error)
{
	struct AARCH64_POLL_RECORD64 record = { device, stage, register_name, address, value, error };
	char line[192] = { 0 };
	size_t used = 0;
	uint64_t irq;

	if (device == NULL || stage == NULL || register_name == NULL) { return; }
	irq = arch64_irq_save();
	poll_records64[poll_next64] = record;
	poll_next64 = (poll_next64 + 1U) % AARCH64_POLL_RECORDS64;
	if (poll_count64 < AARCH64_POLL_RECORDS64) { poll_count64++; }
	else if (poll_overwritten64 != UINT64_MAX) { poll_overwritten64++; }
	arch64_irq_restore(irq);

	/* At most 144 bytes including terminator. Retain the record even if output fails. */
	if (str_append64(line, sizeof(line), &used, "[poll] ") != 0 ||
		label64(line, sizeof(line), &used, device, 16) != 0 ||
		str_append64(line, sizeof(line), &used, " ") != 0 ||
		label64(line, sizeof(line), &used, stage, 32) != 0 ||
		str_append64(line, sizeof(line), &used, " ") != 0 ||
		label64(line, sizeof(line), &used, register_name, 16) != 0 ||
		str_append64(line, sizeof(line), &used, "@") != 0 ||
		hex64(line, sizeof(line), &used, address) != 0 ||
		str_append64(line, sizeof(line), &used, " value=") != 0 ||
		hex64(line, sizeof(line), &used, value) != 0 ||
		str_append64(line, sizeof(line), &used, " error=") != 0 ||
		hex64(line, sizeof(line), &used, (uint64_t) (int64_t) error) != 0 ||
		str_append64(line, sizeof(line), &used, "\n") != 0) { return; }
	arch64_dbg_puts(line);
}

size_t aarch64_poll_snapshot64(struct AARCH64_POLL_RECORD64 *records,
	size_t capacity, uint64_t *overwritten)
{
	uint64_t irq = arch64_irq_save();
	size_t count = records == NULL ? 0 : (capacity < poll_count64 ? capacity : poll_count64);
	size_t first = (poll_next64 + AARCH64_POLL_RECORDS64 - poll_count64) % AARCH64_POLL_RECORDS64;

	for (size_t i = 0; i < count; i++) {
		records[i] = poll_records64[(first + i) % AARCH64_POLL_RECORDS64];
	}
	if (overwritten != NULL) { *overwritten = poll_overwritten64; }
	arch64_irq_restore(irq);
	return count;
}
