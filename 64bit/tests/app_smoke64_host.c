#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define main app_main64
#define write fixture_write64
#define read fixture_read64
#define open fixture_open64
#define close fixture_close64
#define puts fixture_puts64
#define malloc fixture_malloc64
#define free fixture_free64
#define alloc fixture_alloc64
#define tty_raw fixture_raw64
#define tty_readkey fixture_key64
#define tty_size fixture_size64
#define tty_move fixture_move64
#define tty_clear fixture_clear64
#define tty_attr fixture_attr64
#define tty_flush fixture_flush64
#if defined(APP_KTEST64)
#include "../app64/ktest/ktest.c"
#elif defined(APP_WTEST64)
#include "../app64/wtest/wtest.c"
#elif defined(APP_MTEST64)
#include "../app64/crt/malloc.c"
#include "../app64/mtest/mtest.c"
#endif
#undef main
#undef puts

static int pass64, fail64;
static const char *output_fault64;

int fixture_puts64(const char *s)
{
	if (output_fault64 != NULL && strstr(s, output_fault64) != NULL) { return -5; }
	if (strstr(s, ": PASS") != NULL) { pass64++; }
	if (strstr(s, ": FAIL") != NULL) { fail64++; }
	return (int) strlen(s) + 1;
}

#if defined(APP_WTEST64)
static unsigned char files64[3][HUGE_SIZE + 1];
static size_t lengths64[3], offset64;
static int active64 = -1, writing64;
static int calls64[4], fault_op64 = -1, fault_at64, fault_kind64;
static int corrupt64;

static int fault64(int operation)
{
	calls64[operation]++;
	return operation == fault_op64 && calls64[operation] == fault_at64;
}

int fixture_open64(const char *path, int flags)
{
	if (fault64(0)) { return -5; }
	assert(active64 == -1);
	active64 = strcmp(path, "BIG.TXT") == 0 ? 1 : strcmp(path, "HUGE.TXT") == 0 ? 2 : 0;
	writing64 = flags != 0;
	offset64 = 0;
	if (writing64) { lengths64[active64] = 0; }
	return 3;
}

int fixture_close64(int fd)
{
	assert(fd == 3 && active64 >= 0);
	active64 = -1;
	return fault64(3) ? -5 : 0;
}

long fixture_write64(int fd, const void *buf, size_t size)
{
	if (fd == 1) { return (long) size; }
	assert(fd == 3 && active64 >= 0 && writing64);
	if (fault64(2)) {
		return fault_kind64 == 0 ? -5 : fault_kind64 == 1 ? 0 : (long) size + 1;
	}
	/* Deliberate short writes exercise progress without changing stored bytes. */
	if (size > 97) { size = 97; }
	assert(size <= sizeof(files64[0]) - offset64);
	memcpy(files64[active64] + offset64, buf, size);
	offset64 += size;
	lengths64[active64] = offset64;
	return (long) size;
}

long fixture_read64(int fd, void *buf, size_t size)
{
	assert(fd == 3 && active64 >= 0 && !writing64);
	if (fault64(1)) {
		return fault_kind64 == 0 ? -5 : fault_kind64 == 1 ? 0 : (long) size + 1;
	}
	if (size > lengths64[active64] - offset64) { size = lengths64[active64] - offset64; }
	if (size > 71) { size = 71; }
	memcpy(buf, files64[active64] + offset64, size);
	if (corrupt64 && size != 0) { ((unsigned char *) buf)[size - 1] ^= 1; }
	offset64 += size;
	return (long) size;
}

static void reset64(void)
{
	assert(active64 == -1);
	memset(calls64, 0, sizeof(calls64));
	pass64 = fail64 = corrupt64 = 0;
	fault_op64 = -1;
	output_fault64 = NULL;
}

static void run64(int expected, const char *argument)
{
	char *argv[] = { "wtest", (char *) argument, NULL };

	assert(app_main64(argument == NULL ? 1 : 2, argv) == expected);
	assert(active64 == -1);
	assert(pass64 == (expected == 0));
}

int main(void)
{
	int baseline[4];

	reset64();
	run64(0, NULL);
	memcpy(baseline, calls64, sizeof(baseline));
	/* Every open/read/write/close boundary can fail; none may report PASS. */
	for (int operation = 0; operation < 4; operation++) {
		for (int call = 1; call <= baseline[operation]; call++) {
			for (int kind = 0; kind < 3; kind++) {
				/* EOF read returning zero is valid only at the exact end. */
				if (operation == 1 && kind == 1 && (call == 2 ||
					call == baseline[operation])) { continue; }
				reset64();
				fault_op64 = operation;
				fault_at64 = call;
				fault_kind64 = kind;
				run64(1, NULL);
				assert(fail64 == 1);
			}
		}
	}
	reset64();
	run64(0, "HUGE");
	reset64();
	run64(0, "--verify-huge");
	assert(calls64[2] == 0); /* Verification must never truncate/write. */
	reset64();
	files64[2][HUGE_SIZE - 1] ^= 1;
	run64(1, "--verify-huge");
	reset64();
	run64(0, NULL);
	reset64();
	lengths64[0]++;
	run64(1, "--verify");
	reset64();
	lengths64[0] -= 2;
	run64(1, "--verify");
	reset64();
	corrupt64 = 1;
	run64(1, NULL);
	reset64();
	output_fault64 = ": PASS";
	run64(1, NULL);
	return 0;
}
#elif defined(APP_KTEST64)
static unsigned long events64[3];
static unsigned int event_count64;
static int raw64, flushes64, small64;

void fixture_raw64(int on) { raw64 = on; }
unsigned long fixture_size64(void) { return small64 ? 0 : 80UL | (24UL << 16); }
unsigned long fixture_key64(void)
{
	assert(event_count64 < 3);
	return events64[event_count64++];
}
void fixture_move64(int row, int col) { (void) row; (void) col; }
void fixture_clear64(int row, int col, int rows, int cols)
{
	assert(row == 0 && col == 0 && rows == 24 && cols == 80);
}
void fixture_attr64(int fg, int bg) { (void) fg; (void) bg; }
void fixture_flush64(void) { flushes64++; }
long fixture_write64(int fd, const void *buf, size_t size)
{
	assert(fd == 1 && buf != NULL);
	return (long) size;
}

static void reset64(void)
{
	events64[0] = ((unsigned long) TTY_KIND_CHAR << 32) | 'a';
	events64[1] = ((unsigned long) TTY_KIND_KEY << 32) | KEY_LEFT;
	events64[2] = ((unsigned long) TTY_KIND_CHAR << 32) | 24 |
		((unsigned long) TTY_MOD_CTRL << 40);
	event_count64 = pass64 = fail64 = raw64 = flushes64 = small64 = 0;
	output_fault64 = NULL;
}

int main(void)
{
	char *argv[] = { "ktest", "--auto", NULL };

	reset64();
	assert(app_main64(2, argv) == 0 && pass64 == 1 && fail64 == 0);
	assert(raw64 == 1 && flushes64 >= 2 && event_count64 == 3);
	for (int i = 0; i < 3; i++) {
		reset64();
		events64[i] ^= 1;
		assert(app_main64(2, argv) == 1 && pass64 == 0 && fail64 == 1);
		assert(raw64 == 1);
	}
	reset64();
	events64[2] &= ~(0xffUL << 40);
	assert(app_main64(2, argv) == 1 && fail64 == 1);
	reset64();
	small64 = 1;
	assert(app_main64(2, argv) == 1 && raw64 == 0 && fail64 == 1);
	reset64();
	output_fault64 = "READY";
	assert(app_main64(2, argv) == 1 && event_count64 == 0 && pass64 == 0);
	reset64();
	output_fault64 = "PASS";
	assert(app_main64(2, argv) == 1 && pass64 == 0);
	reset64();
	argv[1] = "--bad";
	assert(app_main64(2, argv) == 1 && fail64 == 1);
	reset64();
	events64[0] = events64[2];
	assert(app_main64(1, argv) == 0 && pass64 == 1 && raw64 == 1);
	return 0;
}
#elif defined(APP_MTEST64)
static _Alignas(16) unsigned char heap64[1024 * 1024];
static size_t next64, capacity64;

void *fixture_alloc64(size_t size)
{
	if (size > capacity64 - next64) { return NULL; }
	void *result = heap64 + next64;

	next64 += size;
	return result;
}
long fixture_write64(int fd, const void *buf, size_t size)
{
	assert(fd == 1 && buf != NULL);
	return (long) size;
}
static void reset64(void)
{
	blocks64 = NULL;
	allocator_failed64 = failures = next64 = pass64 = fail64 = 0;
	capacity64 = sizeof(heap64);
	output_fault64 = NULL;
}
int main(void)
{
	char *argv[] = { "mtest", NULL };

	reset64();
	assert(app_main64(1, argv) == 0 && pass64 == 1 && fail64 == 0);
	reset64();
	capacity64 = 0;
	assert(app_main64(1, argv) == 1 && pass64 == 0 && fail64 == 1);
	reset64();
	output_fault64 = "PASS";
	assert(app_main64(1, argv) == 1 && pass64 == 0);
	reset64();
	output_fault64 = "basic alloc";
	assert(app_main64(1, argv) == 1 && pass64 == 0 && fail64 == 1);
	return 0;
}
#endif
