#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the actual editor with bounded file/TTY mocks and allocation faults. */
#define main nano_main64
#define malloc nano_malloc64
#define free nano_free64
#define read nano_read64
#define write nano_write64
#define open nano_open64
#define close nano_close64
#define puts nano_puts64
#include "../app64/나노/나노.c"
#undef main
#undef malloc
#undef free
#undef read
#undef write
#undef open
#undef close
#undef puts
#include "../src64/include/utf864.h"

static const char *input64;
static size_t input_size64, input_at64, chunk64;
static char output64[256 * 1024];
static size_t output_size64;
static long read_fault64, write_fault64;
static int read_calls64, write_calls64, open_calls64, close_calls64;
static int fault_call64;
static int open_result64, close_result64;
static int allocation_fail64 = -1, live_allocations64;
static int tty_calls64;

void *nano_malloc64(size_t size)
{
	if (allocation_fail64 == 0) { return NULL; }
	if (allocation_fail64 > 0) { allocation_fail64--; }
	void *p = malloc(size);

	if (p != NULL) { live_allocations64++; }
	return p;
}

void nano_free64(void *p)
{
	if (p != NULL) { live_allocations64--; }
	free(p);
}

int nano_open64(const char *path, int flags)
{
	assert(path != NULL);
	assert(flags == 0 || flags == (O_CREAT | O_TRUNC));
	open_calls64++;
	return open_result64;
}

int nano_close64(int fd)
{
	assert(fd == 3);
	close_calls64++;
	return close_result64;
}

long nano_read64(int fd, void *buf, size_t size)
{
	assert(fd == 3 && size > 0);
	read_calls64++;
	if (read_fault64 != 0 && read_calls64 >= fault_call64) { return read_fault64; }
	size_t count = input_size64 - input_at64;

	if (count > size) { count = size; }
	if (count > chunk64) { count = chunk64; }
	memcpy(buf, input64 + input_at64, count);
	input_at64 += count;
	return (long) count;
}

long nano_write64(int fd, const void *buf, size_t size)
{
	assert(fd == 1 || fd == 3);
	if (fd == 3) {
		write_calls64++;
		if (write_fault64 != LONG_MIN && write_calls64 >= fault_call64) { return write_fault64; }
	}
	size_t count = size;

	if (fd == 3 && count > chunk64) { count = chunk64; }
	assert(count <= sizeof(output64) - output_size64);
	memcpy(output64 + output_size64, buf, count);
	output_size64 += count;
	return (long) count;
}

int nano_puts64(const char *s)
{
	return (int) nano_write64(1, s, strlen(s));
}

void tty_move(int row, int col)
{
	assert(row >= 0 && row < rows && col >= 0 && col < cols);
	tty_calls64++;
}

void tty_clear(int row, int col, int height, int width)
{
	assert(row >= 0 && row < rows && col >= 0 && col < cols);
	assert(height > 0 && height <= rows - row && width > 0 && width <= cols - col);
	tty_calls64++;
}

void tty_attr(int fg, int bg) { (void) fg; (void) bg; }
void tty_flush(void) { tty_calls64++; }
void tty_raw(int on) { (void) on; }
unsigned long tty_readkey(void) { return ((unsigned long) TTY_KIND_CHAR << 32) | 24; }
unsigned long tty_size(void) { return ((unsigned long) rows << 16) | (unsigned int) cols; }

static void reset64(const char *input, size_t size)
{
	allocation_fail64 = -1;
	clear_lines();
	assert(live_allocations64 == 0);
	input64 = input;
	input_size64 = size;
	input_at64 = output_size64 = 0;
	chunk64 = SIZE_MAX;
	fault_call64 = 2;
	read_fault64 = 0;
	write_fault64 = LONG_MIN;
	read_calls64 = write_calls64 = open_calls64 = close_calls64 = 0;
	open_result64 = 3;
	close_result64 = 0;
	cur_line = cur_byte = top_line = left_col = 0;
	goal_col = -1;
	modified = preedit = 0;
	filename = "fixture";
	has_name = 1;
	status[0] = '\0';
	rows = 37;
	cols = 100;
	tty_calls64 = 0;
}

static void decode_case64(const char *bytes, size_t size)
{
	/* Tight heap bounds make an over-read visible to ASan. */
	char *tight = malloc(size != 0 ? size : 1);
	uint32_t expected;
	size_t consumed;
	int length;

	memcpy(tight, bytes, size);
	(void) utf8_decode64(tight, size, &expected, &consumed);
	assert(utf8_decode(tight, (int) size, &length) == expected);
	assert(length == (int) consumed);
	struct LINE line = { tight, (int) size, (int) size };
	int at = 0;

	while (at < (int) size) {
		utf8_decode(tight + at, (int) size - at, &length);
		assert(length > 0 && length <= (int) size - at);
		assert(prev_char_start(&line, at + length) == at);
		at += length;
	}
	free(tight);
}

static void unicode64(void)
{
	static const char *const cases[] = {
		"A", "가", "\xc2\x80", "\xe0\xa0\x80", "\xf0\x90\x80\x80",
		"\xf4\x8f\xbf\xbf", "\xc0\xaf", "\xed\xa0\x80", "\xe0\x80\x80",
		"\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xe3\x28\xa1", "\xff",
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (size_t n = 0; n <= strlen(cases[i]); n++) { decode_case64(cases[i], n); }
	}
	for (unsigned int a = 0; a < 256; a++) {
		for (unsigned int b = 0; b < 256; b++) {
			char bytes[2] = { (char) a, (char) b };

			decode_case64(bytes, sizeof(bytes));
		}
	}
	unsigned int seed = 12345;

	for (int trial = 0; trial < 10000; trial++) {
		char bytes[12];

		for (size_t i = 0; i < sizeof(bytes); i++) {
			seed = seed * 1664525U + 1013904223U;
			bytes[i] = (char) (seed >> 24);
		}
		decode_case64(bytes, sizeof(bytes));
	}
	for (unsigned int cp = 0; cp <= 0x110000; cp += 17) {
		char expected[4], actual[4];
		int n = unicode_to_utf864(cp, expected, sizeof(expected));

		assert(utf8_encode(actual, cp) == n);
		assert(memcmp(actual, expected, (size_t) n) == 0);
	}
	const char bytes[] = "A가\xf0\x9f\x98\x80\xe3\x80Z\0\x1b\x7f";

	reset64(bytes, sizeof(bytes) - 1);
	assert(load_file("fixture") == 1);
	int previous = 0;

	while (cur_byte < lines[0].len) {
		move_right();
		assert(cur_byte > previous && cur_byte <= lines[0].len);
		assert(prev_char_start(&lines[0], cur_byte) == previous);
		int col = col_of_byte(&lines[0], cur_byte);

		assert(byte_of_col(&lines[0], col) == cur_byte);
		previous = cur_byte;
	}
	while (cur_byte > 0) { int end = cur_byte; move_left(); assert(cur_byte < end); }
	draw_line(1, 0);
	assert(output_size64 > 0);
	for (size_t i = 0; i < output_size64; i++) {
		assert((unsigned char) output64[i] >= 0x20 && output64[i] != 0x7f);
	}
	assert(memcmp(lines[0].text, bytes, sizeof(bytes) - 1) == 0);
	output_size64 = 0;
	modified = 1;
	assert(save_file() == 1 && modified == 0);
	assert(output_size64 == sizeof(bytes));
	assert(memcmp(output64, bytes, sizeof(bytes) - 1) == 0);
	assert(output64[sizeof(bytes) - 1] == '\n');
	cur_byte = lines[0].len;
	while (cur_byte > 0) { do_backspace(); }
	assert(lines[0].len == 0);
	insert_cp(0xd800);
	insert_cp(0x110000);
	assert(lines[0].len == 0);
	insert_cp(0x10ffff);
	assert(lines[0].len == 4);
	cur_byte = 0;
	do_delete();
	assert(lines[0].len == 0);
}

static void capacity64(void)
{
	int result = -1;

	assert(count_add(COUNT_MAX, 0, &result) && result == COUNT_MAX);
	assert(!count_add(COUNT_MAX, 1, &result) && result == COUNT_MAX);
	assert(!count_add(INT_MAX, INT_MAX, &result));
	assert(!count_add(-1, 2, &result));
	assert(grow_capacity(0, 33, 32, 1, &result) && result == 64);
	assert(grow_capacity(COUNT_MAX / 2 + 1, COUNT_MAX, 32, 1, &result));
	assert(result == COUNT_MAX);
	assert(!grow_capacity(0, INT_MAX, 32, 1, &result));
	assert(!grow_capacity(0, 32, 32, SIZE_MAX, &result));
	reset64("", 0);
	assert(line_insert(-1) == 0 && line_insert(1) == 0);
	assert(line_insert(0) == 1);
	assert(line_append(&lines[0], "abc", 3));
	assert(!line_append(&lines[0], "x", INT_MAX));
	assert(!line_insert_bytes(&lines[0], 4, "x", 1));
	assert(!line_insert_bytes(&lines[0], 0, "x", -1));
	line_delete_bytes(&lines[0], 1, INT_MAX);
	assert(lines[0].len == 3 && memcmp(lines[0].text, "abc", 3) == 0);
	cur_byte = 1;
	allocation_fail64 = 0;
	insert_newline();
	assert(nlines == 1 && cur_byte == 1 && modified == 0);
	assert(lines[0].len == 3 && memcmp(lines[0].text, "abc", 3) == 0);
	allocation_fail64 = -1;
	insert_newline();
	assert(nlines == 2 && lines[0].len == 1 && lines[1].len == 2);
	do_backspace();
	assert(nlines == 1 && lines[0].len == 3 && cur_byte == 1);
	for (int i = 0; i < 200; i++) { insert_cp('x'); }
	assert(lines[0].len == 203);
	for (int i = 0; i < 130; i++) { assert(line_insert(nlines)); }
	move_vertical(INT_MAX);
	assert(cur_line == nlines - 1);
	move_vertical(INT_MIN);
	assert(cur_line == 0);
}

static void file_io64(void)
{
	const char *text = "abc\r\ndef\n";

	reset64(text, strlen(text));
	chunk64 = 1;
	assert(load_file("fixture") == 1 && nlines == 2);
	assert(lines[0].len == 3 && lines[1].len == 3 && close_calls64 == 1);
	for (int failure = 0; failure < 3; failure++) {
		reset64(text, strlen(text));
		chunk64 = 1;
		read_fault64 = failure == 0 ? -5 : (failure == 1 ? LONG_MAX : LONG_MIN);
		assert(load_file("fixture") == LOAD_IO && nlines == 0 && close_calls64 == 1);
	}
	reset64(text, strlen(text));
	read_fault64 = -5;
	fault_call64 = 1;
	assert(load_file("fixture") == LOAD_IO && nlines == 0 && read_calls64 == 1);
	reset64(text, strlen(text));
	close_result64 = -5;
	assert(load_file("fixture") == LOAD_IO && nlines == 0 && close_calls64 == 1);
	for (int budget = 0; budget < 3; budget++) {
		reset64(text, strlen(text));
		allocation_fail64 = budget;
		assert(load_file("fixture") == 0 && nlines == 0 && live_allocations64 == 0);
	}
	reset64("", 0);
	open_result64 = -2;
	assert(load_file("new") == 1 && nlines == 1 && close_calls64 == 0);
	reset64("", 0);
	open_result64 = -5;
	assert(load_file("bad") == LOAD_IO && nlines == 0 && close_calls64 == 0);
	char *big = malloc(MAX_FILE + 1);

	memset(big, 'a', MAX_FILE + 1);
	big[MAX_FILE - 1] = '\n';
	reset64(big, MAX_FILE);
	assert(load_file("fixture") == 1 && lines[0].len == MAX_FILE - 1);
	modified = 1;
	chunk64 = 127;
	assert(save_file() == 1 && output_size64 == MAX_FILE && modified == 0);
	assert(memcmp(output64, big, MAX_FILE) == 0);
	reset64(big, MAX_FILE + 1);
	assert(load_file("fixture") == LOAD_LARGE && nlines == 0 && close_calls64 == 1);
	memset(big, 'a', MAX_FILE);
	reset64(big, MAX_FILE);
	assert(load_file("fixture") == 1 && lines[0].len == MAX_FILE);
	modified = 1;
	int before = open_calls64;

	assert(save_file() == 0 && modified == 1 && open_calls64 == before);
	free(big);
	static const long faults[] = { -5, LONG_MIN + 1, 0, LONG_MAX };

	for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
		reset64(text, strlen(text));
		assert(load_file("fixture") == 1);
		modified = 1;
		chunk64 = 1;
		write_fault64 = faults[i];
		assert(save_file() == 0 && modified == 1 && write_calls64 == 2);
		assert(close_calls64 == 2 && output_size64 == 1);
	}
	reset64(text, strlen(text));
	assert(load_file("fixture") == 1);
	modified = 1;
	write_fault64 = -5;
	fault_call64 = 1;
	assert(save_file() == 0 && modified == 1 && write_calls64 == 1);
	assert(output_size64 == 0 && close_calls64 == 2);
	reset64(text, strlen(text));
	assert(load_file("fixture") == 1);
	modified = 1;
	close_result64 = -5;
	assert(save_file() == 0 && modified == 1 && close_calls64 == 2);
}

static void screen64(void)
{
	reset64("가A\xe3", 5);
	assert(load_file("fixture") == 1);
	for (int width = 0; width <= 3; width++) {
		output_size64 = 0;
		assert(put_clipped("가A\xe3", width) <= width);
		assert(output_size64 <= (size_t) width * 3);
	}
	const char truncated[] = { (char) 0xe3, 0 };

	assert(str_cols(truncated) == 1);
	assert(put_clipped(truncated, -1) == 0);
	cols = 20;
	rows = 6;
	cur_byte = lines[0].len;
	draw_all();
	assert(tty_calls64 > 0);
	int calls = tty_calls64;

	rows = 0;
	cols = 0;
	draw_all();
	assert(tty_calls64 == calls && text_rows() == 0);
	rows = 65535;
	cols = 65535;
	draw_all();
	assert(tty_calls64 > calls);
}

int main(void)
{
	unicode64();
	capacity64();
	file_io64();
	screen64();
	reset64("", 0);
	char *args[] = { "nano", "fixture" };

	assert(nano_main64(2, args) == 0 && live_allocations64 == 0);
	reset64("", 0);
	rows = 1;
	cols = 1;
	assert(nano_main64(2, args) == 1 && live_allocations64 == 0);
	reset64("", 0);
	return 0;
}
