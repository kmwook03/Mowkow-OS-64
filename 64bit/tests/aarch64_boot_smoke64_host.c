/* Boot probes use real HID/FIFO code; only hardware/time/files are mocked. */
#include "poll_test64.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <arch/arch64.h>
#include <arch/usb_xhci64.h>
#include <arch/aarch64_boot64.h>
#include <arch/platform64.h>
#include <fd64.h>
#include <mtask64.h>

static uint64_t counter64;
static uint64_t frequency64 = 1000;
static unsigned int polls64, arms64;
static int mode64;
static char log64[2048];
static size_t log_used64;

enum KEY_CASE64 {
	NORMAL64, PRESS_TIMEOUT64, RELEASE_TIMEOUT64, POLL_FAIL64,
	PROTOCOL_FAIL64, ARM_FAIL64, MALFORMED64
};

uint64_t aarch64_boot_counter64(void) { return counter64++; }
uint64_t aarch64_boot_frequency64(void) { return frequency64; }
void aarch64_boot_relax64(void) { counter64 += 1000; }
void aarch64_boot_log_hex64(uint32_t value) { (void) value; }
void arch64_dbg_puts(const char *text)
{
	size_t length = strlen(text);

	assert(length < sizeof(log64) - log_used64);
	memcpy(log64 + log_used64, text, length + 1);
	log_used64 += length;
}
void arch64_panic_blink(int code) { (void) code; abort(); }
uint64_t platform_irq_save64(void) { return 0; }
void platform_irq_restore64(uint64_t state) { (void) state; }
int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) task;
	(void) level;
	(void) priority;
	abort(); /* Boot smoke FIFO must have no task owner. */
}

int xhci64_keyboard_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	assert(rp1_base == 0x1000 && slot_id == 1 && hid->endpoint_address == 0x81);
	return mode64 == PROTOCOL_FAIL64 ? -5 : 0;
}
int xhci64_keyboard_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	assert(rp1_base == 0x1000 && slot_id == 1 && hid->endpoint_address == 0x81);
	arms64++;
	return mode64 == ARM_FAIL64 ? -5 : 0;
}
int xhci64_keyboard_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t report[8])
{
	assert(rp1_base == 0x1000 && slot_id == 1 && hid->endpoint_address == 0x81);
	polls64++;
	assert(polls64 < 100);
	memset(report, 0, 8);
	if (mode64 == PRESS_TIMEOUT64) { return 0; }
	if (mode64 == POLL_FAIL64) { return -5; }
	if (mode64 == MALFORMED64) {
		/* Rollover and short reports must be retried before accepting a key. */
		if (polls64 == 1) { report[2] = 1; return 8; }
		if (polls64 == 2) { report[2] = 4; return 3; }
		if (polls64 == 3) { report[2] = 4; return 8; }
		if (polls64 == 4) { report[2] = 1; return 8; }
		return 8;
	}
	if (polls64 == 1 || mode64 == RELEASE_TIMEOUT64) {
		report[2] = 4;
	}
	if (mode64 == RELEASE_TIMEOUT64) { counter64 += 1000; }
	return 8;
}

#if MOWKOW64_BOOT_TESTS
static const char payload64[] = "Mowkow OS M4 SDHCI write smoke\n";
static int exists64, corrupt64, write_fail64, sync_fail64;
static unsigned int creates64, writes64, syncs64;

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	assert(strcmp(name, "M4TEST.TXT") == 0);
	memset(fh, 0, sizeof(*fh));
	fh->info.size = sizeof(payload64) - 1;
	return exists64;
}
size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t size)
{
	(void) fh;
	assert(size == sizeof(payload64) - 1);
	memcpy(dst, payload64, size);
	if (corrupt64) { ((char *) dst)[0] ^= 1; }
	return size;
}
int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	(void) fh;
	assert(strcmp(name, "M4TEST.TXT") == 0);
	creates64++;
	exists64 = 1;
	return 1;
}
size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size)
{
	(void) fh;
	assert(size == sizeof(payload64) - 1 && memcmp(src, payload64, size) == 0);
	writes64++;
	return write_fail64 ? 0 : size;
}
int fd64_sync(void)
{
	syncs64++;
	return sync_fail64 ? -1 : 0;
}

static void storage64(void)
{
	assert(aarch64_boot_smoke_storage64() == 0);
	assert(creates64 == 1 && writes64 == 1 && syncs64 == 1);
	assert(aarch64_boot_smoke_storage64() == 1);
	assert(creates64 == 1 && writes64 == 1 && syncs64 == 1);
	corrupt64 = 1;
	assert(aarch64_boot_smoke_storage64() == -2);
	assert(creates64 == 1 && writes64 == 1 && syncs64 == 1);
	corrupt64 = 0;
	exists64 = 0;
	write_fail64 = 1;
	assert(aarch64_boot_smoke_storage64() == -1);
	assert(syncs64 == 1);
	write_fail64 = 0;
	exists64 = 0;
	sync_fail64 = 1;
	assert(aarch64_boot_smoke_storage64() == -1);
	assert(syncs64 == 2);
}
#endif

static void keyboard64(void)
{
	const struct XHCI64_HID_RESULT hid = { .endpoint_address = 0x81 };

	for (mode64 = NORMAL64; mode64 <= MALFORMED64; mode64++) {
		counter64 = UINT64_MAX - 50; /* Deadlines must survive counter wrap. */
		polls64 = 0;
		arms64 = 0;
		log_used64 = 0;
		log64[0] = '\0';
		int status = aarch64_boot_smoke_keyboard64(0x1000, 1, &hid);

		if (mode64 == NORMAL64 || mode64 == MALFORMED64) {
			assert(status == 0);
			assert(polls64 == (mode64 == NORMAL64 ? 2U : 5U));
			assert(arms64 == (mode64 == NORMAL64 ? 2U : 5U));
		} else {
			assert(status < 0);
			assert(strstr(log64, mode64 == RELEASE_TIMEOUT64 ?
				"M6l: USB HID translation failed" : "M6k: USB keyboard report failed"));
		}
	}
	for (unsigned int i = 0; i < 2; i++) {
		frequency64 = i == 0 ? 0 : (uint64_t) INT64_MAX / 30 + 1;
		polls64 = 0;
		log_used64 = 0;
		assert(aarch64_boot_smoke_keyboard64(0x1000, 1, &hid) < 0);
		assert(polls64 == 0 && strstr(log64, "clock frequency invalid"));
	}
}

int main(void)
{
	assert(aarch64_boot_smoke_mouse64() == 0);
	keyboard64();
#if MOWKOW64_BOOT_TESTS
	storage64();
#endif
	return 0;
}
