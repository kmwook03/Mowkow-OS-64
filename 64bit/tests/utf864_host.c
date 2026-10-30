#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src64/kernel/console_render64.c"
#include "../src64/kernel/console_queue64.c"
#include "../src64/kernel/console_editor64.c"

static uint64_t irq_depth64;
uint64_t platform_irq_save64(void) { return irq_depth64++; }
void platform_irq_restore64(uint64_t state)
{
	assert(irq_depth64 == state + 1);
	irq_depth64--;
}

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "UTF check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

/* A visible marker allows assertions about replacement and ASCII placement. */
const uint8_t hankaku64[4096] = {
	['?' * 16] = 0x80,
	['A' * 16] = 0x40,
};
static char serial64[128];
static size_t serial_length64;

int keyboard64_shift(void)
{
	return 0;
}

int keyboard64_ctrl(void)
{
	return 0;
}

int keyboard64_alt(void)
{
	return 0;
}

uint8_t io_in8(uint16_t port)
{
	(void) port;
	return 0x20;
}

void io_out8(uint16_t port, uint8_t data)
{
	(void) port;
	if (serial_length64 < sizeof(serial64)) {
		serial64[serial_length64++] = (char) data;
	}
}

void sheet64_refresh(struct SHEET64 *sheet, int32_t x0, int32_t y0,
	int32_t x1, int32_t y1)
{
	(void) sheet;
	(void) x0;
	(void) y0;
	(void) x1;
	(void) y1;
}

void hangul64_draw_unicode(const struct SURFACE64 *surface, int32_t x,
	int32_t y, uint8_t color, const uint8_t *font, size_t font_size, unsigned int unicode)
{
	(void) surface;
	(void) x;
	(void) y;
	(void) color;
	(void) font;
	(void) font_size;
	(void) unicode;
}

static int scalars64(void)
{
	uint32_t scalar;
	uint32_t decoded;
	size_t consumed;
	size_t width;
	size_t short_width;
	char bytes[5];
	char encoded[5];
	uint16_t units[2];
	int unit_count;

	/* Independent encoder, including NUL and every scalar boundary. */
	for (scalar = 0; scalar <= 0x10ffff; scalar++) {
		if (scalar >= 0xd800 && scalar <= 0xdfff) {
			continue;
		}
		if (scalar < 0x80) {
			width = 1;
			bytes[0] = (char) scalar;
		} else if (scalar < 0x800) {
			width = 2;
			bytes[0] = (char) (0xc0 | (scalar >> 6));
		} else if (scalar < 0x10000) {
			width = 3;
			bytes[0] = (char) (0xe0 | (scalar >> 12));
		} else {
			width = 4;
			bytes[0] = (char) (0xf0 | (scalar >> 18));
		}
		for (size_t i = 1; i < width; i++) {
			bytes[i] = (char) (0x80 | ((scalar >> (6 * (width - i - 1))) & 0x3f));
		}
		bytes[width] = '\0';
		CHECK64(unicode_to_utf864(scalar, encoded, 4) == (int) width);
		CHECK64(memcmp(encoded, bytes, width) == 0);
		unit_count = scalar == 0 ? 0 : (scalar > 0xffff ? 2 : 1);
		CHECK64(utf8_to_utf16_64(bytes, units, 2) == unit_count);
		if (unit_count == 2) {
			CHECK64(units[0] == 0xd800 + ((scalar - 0x10000) >> 10));
			CHECK64(units[1] == 0xdc00 + ((scalar - 0x10000) & 0x3ff));
		} else if (unit_count == 1) {
			CHECK64(units[0] == scalar);
		}
		CHECK64(utf16_to_utf8_64(units, unit_count, encoded, 5) ==
			(scalar == 0 ? 0 : (int) width));
		CHECK64(strcmp(encoded, bytes) == 0);
		CHECK64(utf8_decode64(bytes, width, &decoded, &consumed) == 0);
		CHECK64(decoded == scalar && consumed == width);
		for (short_width = 0; short_width < width; short_width++) {
			CHECK64(utf8_decode64(bytes, short_width, &decoded, &consumed) == -84);
			CHECK64(decoded == UTF64_REPLACEMENT);
			CHECK64(consumed == (short_width != 0 ? 1U : 0U));
		}
	}
	return 0;
}

static int malformed64(void)
{
	static const char *const invalid[] = {
		"\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xc2" "A",
		"\xe0\x9f\xbf", "\xe1\x80" "A", "\xed\xa0\x80", "\xed\xbf\xbf",
		"\xf0\x8f\xbf\xbf", "\xf1\x80\x80" "A", "\xf4\x90\x80\x80",
		"\xf5\x80\x80\x80", "\xf8", "\xff",
	};
	uint32_t unicode = 123;
	size_t consumed = 456;
	int length;
	char byte;

	CHECK64(utf8_decode64(NULL, 0, &unicode, &consumed) == -22);
	CHECK64(unicode == 123 && consumed == 456);
	CHECK64(utf8_decode64("A", 1, NULL, &consumed) == -22);
	CHECK64(utf8_decode64("A", 1, &unicode, NULL) == -22);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK64(utf8_decode64(invalid[i], strlen(invalid[i]),
			&unicode, &consumed) == -84);
		CHECK64(unicode == UTF64_REPLACEMENT && consumed == 1);
		CHECK64(utf8_to_unicode64(invalid[i], &length) == UTF64_REPLACEMENT);
		CHECK64(length == 1);
		uint16_t units[4];

		CHECK64(utf8_to_utf16_64(invalid[i], units, 4) == -1);
	}
	for (unsigned int lead = 0; lead <= 255; lead++) {
		byte = (char) lead;
		CHECK64(utf8_decode64(&byte, 1, &unicode, &consumed) ==
			(lead < 128 ? 0 : -84));
		CHECK64(consumed == 1);
	}
	CHECK64(utf8_to_unicode64("", &length) == 0 && length == 1);
	CHECK64(utf8_to_unicode64("한", &length) == 0xd55c && length == 3);
	CHECK64(utf8_to_unicode64("\xf4\x8f\xbf\xbf", &length) == 0x10ffff && length == 4);
	CHECK64(utf8_to_unicode64(NULL, &length) == UTF64_REPLACEMENT && length == 0);
	CHECK64(utf8_to_unicode64("A", NULL) == UTF64_REPLACEMENT);
	return 0;
}

static int utf16_boundaries64(void)
{
	static const uint16_t malformed[][3] = {
		{ 0xd800, 0, 0 }, { 0xdbff, 0, 0 }, { 0xdc00, 0, 0 },
		{ 0xdfff, 0, 0 }, { 0xdc00, 0xd800, 0 }, { 0xd800, 0xd800, 0 },
		{ 0xd800, 'A', 0 }, { 'A', 0xdc00, 0 }, { 'A', 0xd800, 0 },
	};
	const uint16_t valid[] = { 'A', 0xd55c, 0xd83d, 0xde00, 0 };
	const uint16_t zero = 0;
	char output[10];
	uint16_t units[6];
	int result;

	for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
		CHECK64(utf16_to_utf8_64(malformed[i], 2, output, sizeof(output)) == -1);
		CHECK64(output[0] == '\0');
	}
	for (unsigned int surrogate = 0xd800; surrogate <= 0xdfff; surrogate++) {
		memset(output, 0x55, sizeof(output));
		CHECK64(unicode_to_utf864(surrogate, output, sizeof(output)) == 0);
		CHECK64(output[0] == 0x55);
	}
	CHECK64(unicode_to_utf864(0x110000, output, sizeof(output)) == 0);
	CHECK64(unicode_to_utf864(UINT32_MAX, output, sizeof(output)) == 0);
	CHECK64(unicode_to_utf864('A', NULL, 1) == 0);
	for (int capacity = -1; capacity <= 5; capacity++) {
		memset(output, 0x55, sizeof(output));
		result = unicode_to_utf864(0x1f600, output, capacity);
		CHECK64(result == (capacity < 4 ? 0 : 4));
		CHECK64(output[4] == 0x55);
		if (capacity < 4) {
			CHECK64(output[0] == 0x55);
		}
	}
	for (int capacity = -1; capacity <= 5; capacity++) {
		memset(units, 0x55, sizeof(units));
		result = utf8_to_utf16_64("A한\xf0\x9f\x98\x80", units, capacity);
		CHECK64(result == (capacity < 4 ? -1 : 4));
		CHECK64(units[4] == 0x5555);
		if (capacity == 3) {
			CHECK64(units[2] == 0x5555 && units[3] == 0x5555);
		}
	}
	for (int capacity = -1; capacity <= 10; capacity++) {
		memset(output, 0x55, sizeof(output));
		result = utf16_to_utf8_64(valid, 4, output, capacity);
		CHECK64(result == (capacity < 9 ? -1 : 8));
		if (capacity <= 0) {
			CHECK64(output[0] == 0x55);
		} else if (capacity < 9) {
			CHECK64(output[0] == '\0');
		} else {
			CHECK64(strcmp(output, "A한\xf0\x9f\x98\x80") == 0);
		}
		if (capacity >= 0 && capacity < 10) {
			CHECK64(output[capacity] == 0x55);
		}
	}
	CHECK64(utf8_to_utf16_64("", units, 0) == 0);
	CHECK64(utf8_to_utf16_64("A", units, 0) == -1);
	CHECK64(utf8_to_utf16_64(NULL, units, 6) == -1);
	CHECK64(utf8_to_utf16_64("A", NULL, 6) == -1);
	CHECK64(utf16_to_utf8_64(NULL, 0, output, 10) == -1 && output[0] == '\0');
	CHECK64(utf16_to_utf8_64(valid, -1, output, 10) == -1 && output[0] == '\0');
	CHECK64(utf16_to_utf8_64(valid, 4, NULL, 10) == -1);
	CHECK64(utf16_to_utf8_64(valid, 0, output, 1) == 0 && output[0] == '\0');
	CHECK64(utf16_to_utf8_64(&zero, 1, output, 2) == 1);
	CHECK64(output[0] == '\0' && output[1] == '\0');
	return 0;
}

static int bounds_and_display64(void)
{
	static const char *const sequences[] = { "\xc2\x80", "\xe2\x82\xac", "\xf0\x90\x80\x80" };
	struct CONSOLE64 con = { 0 };
	uint8_t pixels[64 * 16] = { 0 };
	struct SURFACE64 surface = { pixels, sizeof(pixels), 64, 64, 16, 0, 0, 64, 16 };
	long page_size = sysconf(_SC_PAGESIZE);
	char *mapping;
	char *end;
	uint32_t unicode;
	size_t consumed;
	uint64_t event;
	uint16_t units[2];
	char output[5];
	int length;

	CHECK64(page_size > 0);
	mapping = mmap(NULL, (size_t) page_size * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK64(mapping != MAP_FAILED);
	end = mapping + page_size;
	CHECK64(mprotect(end, (size_t) page_size, PROT_NONE) == 0);
	/* Even the first byte is inaccessible for an empty input. */
	CHECK64(utf8_decode64(end, 0, &unicode, &consumed) == -84 && consumed == 0);
	con.vram = pixels;
	con.vram_size = sizeof(pixels);
	con.width = 64;
	con.height = 16;
	con.stride = 64;
	con.color_fg = 7;
	con.color_bg = 0;
	for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); i++) {
		for (size_t n = 1; n <= strlen(sequences[i]); n++) {
			char *input = end - n;

			memcpy(input, sequences[i], n);
			CHECK64(utf8_decode64(input, n, &unicode, &consumed) ==
				(n == strlen(sequences[i]) ? 0 : -84));
			con.cursor_x = 0;
			serial_length64 = 0;
			put_bytes64(&con, input, n);
			CHECK64(con.cursor_x == (n == strlen(sequences[i]) ? 8 : n * 8));
			con.raw_mode = 1;
			CHECK64(append_input64(&con, input, (int) n) == 0);
			CHECK64(console_raw_pop64(&con, &event) == 1);
			CHECK64((uint32_t) event == unicode);
			con.raw_mode = 0;
			/* A NUL at the final readable byte bounds the compatibility path. */
			input = end - n - 1;
			memcpy(input, sequences[i], n);
			input[n] = '\0';
			utf8_to_unicode64(input, &length);
			CHECK64(length == (n == strlen(sequences[i]) ? (int) n : 1));
			CHECK64(utf8_to_utf16_64(input, units, 2) ==
				(n == strlen(sequences[i]) ? (i == 2 ? 2 : 1) : -1));
			con.cursor_x = 0;
			console_puts_con64(&con, input);
			putstr64(&surface, 0, 0, 7, input);
		}
	}
	con.cursor_x = 0;
	serial_length64 = 0;
	memset(pixels, 0, sizeof(pixels));
	put_bytes64(&con, "\xc2" "A", 2);
	CHECK64(con.cursor_x == 16 && pixels[0] == 7 && pixels[9] == 7);
	CHECK64(serial_length64 == 4 && memcmp(serial64, "\xef\xbf\xbd" "A", 4) == 0);
	memset(pixels, 0, sizeof(pixels));
	putstr64(&surface, 0, 0, 7, "\xe2" "A");
	CHECK64(pixels[0] == 7 && pixels[9] == 7);
	/* Guard the UTF-16 input itself, including a high half at its last unit. */
	uint16_t *last_unit = (uint16_t *) (void *) (end - sizeof(uint16_t));

	*last_unit = 0xd800;
	CHECK64(utf16_to_utf8_64(last_unit, 1, output, 5) == -1);
	CHECK64(utf16_to_utf8_64(last_unit, 0, output, 1) == 0);
	CHECK64(utf16_to_utf8_64(last_unit, 1, end, 0) == -1);
	*last_unit = 0xdfff;
	CHECK64(utf16_to_utf8_64(last_unit, 1, output, 5) == -1);
	uint16_t *pair = last_unit - 1;

	pair[0] = 0xdbff;
	pair[1] = 0xdfff;
	CHECK64(utf16_to_utf8_64(pair, 2, output, 5) == 4);
	CHECK64(strcmp(output, "\xf4\x8f\xbf\xbf") == 0);
	CHECK64(utf16_to_utf8_64(pair, 1, output, 5) == -1);
	/* Exact destination capacities must not split pairs or overrun NUL. */
	CHECK64(utf8_to_utf16_64("\xf4\x8f\xbf\xbf", last_unit, 1) == -1);
	CHECK64(*last_unit == 0xdfff);
	CHECK64(utf8_to_utf16_64("\xf0\x90\x80\x80", pair, 2) == 2);
	CHECK64(pair[0] == 0xd800 && pair[1] == 0xdc00);
	units[0] = 0xd800;
	units[1] = 0xdc00;
	CHECK64(utf16_to_utf8_64(units, 2, end - 4, 4) == -1 && end[-4] == '\0');
	CHECK64(utf16_to_utf8_64(units, 2, end - 5, 5) == 4);
	CHECK64(memcmp(end - 5, "\xf0\x90\x80\x80\0", 5) == 0);
	CHECK64(munmap(mapping, (size_t) page_size * 2) == 0);
	return 0;
}

int main(void)
{
	CHECK64(scalars64() == 0);
	CHECK64(malformed64() == 0);
	CHECK64(bounds_and_display64() == 0);
	CHECK64(utf16_boundaries64() == 0);
	puts("UTF scalars, surrogate pairs, malformed input, guard pages and display: PASS");
	return 0;
}
