#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <mowos.h>
#include <syscall.h>
#include "../nano64/include/nano_file64.h"
#include "../nano64/include/nano_utf64.h"

/* Replace native gates only: the stream calls the actual CRT wrappers. */
static unsigned char input64[2048], output64[2048];
static size_t input_size64, input_at64, output_size64, chunk64;
static long read_replies64[32], write_replies64[32];
static size_t read_count64, write_count64, read_at64, write_at64;
static int opens64, closes64, reads64, writes64, open_flags64;
static long open_result64, close_result64;

static void reset64(const void *input, size_t size, size_t chunk)
{
	assert(size <= sizeof(input64) && chunk != 0);
	if (size != 0) { memcpy(input64, input, size); }
	input_size64 = size;
	input_at64 = output_size64 = 0;
	chunk64 = chunk;
	read_count64 = write_count64 = read_at64 = write_at64 = 0;
	opens64 = closes64 = reads64 = writes64 = 0;
	open_result64 = 3;
	close_result64 = 0;
	nano_errno64 = 0;
	memset(output64, 0xa5, sizeof(output64));
}

long syscall0(long nr)
{
	(void) nr;
	assert(0);
	return -1;
}

long syscall1(long nr, long a0)
{
	assert(nr == SYS_CLOSE && a0 >= 3);
	closes64++;
	return close_result64;
}

long syscall2(long nr, long a0, long a1)
{
	assert(nr == SYS_OPEN && strcmp((const char *) a0, "한글.TXT") == 0);
	open_flags64 = (int) a1;
	opens64++;
	return open_result64;
}

long syscall3(long nr, long a0, long a1, long a2)
{
	assert(a0 >= 1 && a1 != 0 && a2 > 0);
	size_t requested = (size_t) a2;
	size_t count = requested < chunk64 ? requested : chunk64;
	long result;

	if (nr == SYS_READ) {
		assert(a0 >= 3);
		reads64++;
		if (count > input_size64 - input_at64) {
			count = input_size64 - input_at64;
		}
		result = read_at64 < read_count64 ?
			read_replies64[read_at64++] : (long) count;
		if (result > 0 && (size_t) result <= requested) {
			assert((size_t) result <= input_size64 - input_at64);
			memcpy((void *) a1, input64 + input_at64, (size_t) result);
			input_at64 += (size_t) result;
		}
	} else {
		assert(nr == SYS_WRITE);
		writes64++;
		result = write_at64 < write_count64 ?
			write_replies64[write_at64++] : (long) count;
		if (result > 0 && (size_t) result <= requested) {
			assert((size_t) result <= sizeof(output64) - output_size64);
			memcpy(output64 + output_size64, (const void *) a1, (size_t) result);
			output_size64 += (size_t) result;
		}
	}
	return result;
}

long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	assert(nr == SYS_FILE);
	if (a0 == FILE_OPEN64) {
		return syscall2(SYS_OPEN, a1, a2 & (O_CREAT | O_TRUNC));
	}
	assert(a0 == FILE_READ64);
	return syscall3(SYS_READ, a1, a2, a3);
}

long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	(void) a4;
	return syscall4(nr, a0, a1, a2, a3);
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	(void) a5;
	return syscall5(nr, a0, a1, a2, a3, a4);
}

static void modes_and_ownership64(void)
{
	static const char *const rejected[] = { "a", "ab", "r+", "r+b", "w+", "wx", "bad" };

	reset64(NULL, 0, 512);
	for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
		assert(nano_fopen64("한글.TXT", rejected[i]) == NULL && nano_errno64 == 38);
		assert(nano_fdopen64(3, rejected[i]) == NULL && nano_errno64 == 38);
	}
	assert(nano_fopen64("한글.TXT", NULL) == NULL && nano_errno64 == 22);
	assert(nano_fopen64("한글.TXT", "") == NULL && nano_errno64 == 22);
	assert(nano_fopen64(NULL, "r") == NULL && nano_errno64 == 22);
	assert(nano_fopen64("", "w") == NULL && nano_errno64 == 22);
	assert(nano_fdopen64(2, "w") == NULL && nano_errno64 == 9);
	assert(opens64 == 0 && closes64 == 0);

	static const char *const supported[] = { "r", "rb", "w", "wb" };

	for (size_t i = 0; i < 4; i++) {
		struct NANO_FILE64 *stream = nano_fopen64("한글.TXT", supported[i]);

		assert(stream != NULL && nano_fileno64(stream) == 3);
		assert(open_flags64 == (i < 2 ? 0 : O_CREAT | O_TRUNC));
		assert(nano_fdopen64(3, "rb") == NULL && nano_errno64 == 22);
		assert(nano_fclose64(stream) == 0);
		assert(nano_fclose64(stream) == -1 && nano_errno64 == 9);
	}
	/* The additive open reports actual errno values. */
	open_result64 = -5;
	assert(nano_fopen64("한글.TXT", "rb") == NULL && nano_errno64 == 5);
	struct NANO_FILE64 *pool[NANO_STREAMS64];

	for (int i = 0; i < NANO_STREAMS64; i++) {
		pool[i] = nano_fdopen64(3 + i, "wb");
		assert(pool[i] != NULL);
	}
	int opened = opens64;
	int closed = closes64;

	assert(nano_fdopen64(30, "rb") == NULL && nano_errno64 == 24);
	assert(nano_fopen64("한글.TXT", "wb") == NULL && nano_errno64 == 24);
	assert(opens64 == opened && closes64 == closed);
	for (int i = 0; i < NANO_STREAMS64; i++) {
		assert(nano_fclose64(pool[i]) == 0);
	}
	assert(nano_fileno64((struct NANO_FILE64 *) (uintptr_t) 1) == -1);
	assert(nano_getc64(NULL) == -1 && nano_errno64 == 9);
}

static void read_boundaries64(void)
{
	static const unsigned char bytes[] = { 0xed, 0x95, 0x9c, 0, 0xff, '\r', '\n' };

	for (size_t chunk = 1; chunk <= sizeof(bytes); chunk++) {
		reset64(bytes, sizeof(bytes), chunk);
		struct NANO_FILE64 *stream = nano_fopen64("한글.TXT", "rb");
		unsigned char data[12];

		assert(stream != NULL && !nano_feof64(stream));
		memset(data, 0xa5, sizeof(data));
		assert(nano_fread64(data, 2, 4, stream) == 3);
		assert(memcmp(data, bytes, sizeof(bytes)) == 0 && data[7] == 0xa5);
		assert(nano_feof64(stream) && !nano_ferror64(stream));
		int called = reads64;

		assert(nano_getc64(stream) == -1 && reads64 == called);
		assert(nano_fclose64(stream) == 0);
	}
	/* getc caches bytes and returns unsigned 0xff, not EOF. */
	reset64(bytes, sizeof(bytes), 512);
	struct NANO_FILE64 *stream = nano_fdopen64(3, "r");

	for (size_t i = 0; i < sizeof(bytes); i++) {
		assert(nano_getc64(stream) == bytes[i] && reads64 == 1);
	}
	assert(!nano_feof64(stream));
	assert(nano_getc64(stream) == -1 && nano_feof64(stream) && reads64 == 2);
	nano_clearerr64(stream);
	assert(!nano_feof64(stream) && nano_getc64(stream) == -1 && reads64 == 3);
	assert(nano_fclose64(stream) == 0);

	/* Cross both sides of the private cache boundary using exact binary bytes. */
	unsigned char source[1025], target[1026];

	for (size_t i = 0; i < sizeof(source); i++) { source[i] = (unsigned char) i; }
	reset64(source, sizeof(source), 512);
	stream = nano_fdopen64(3, "rb");
	assert(nano_fread64(target, 1, 1, stream) == 1 && reads64 == 1);
	assert(nano_fread64(target + 1, 1, 1024, stream) == 1024 && reads64 == 3);
	assert(memcmp(source, target, sizeof(source)) == 0);
	assert(!nano_feof64(stream) && nano_getc64(stream) == -1 && nano_feof64(stream));
	assert(nano_fclose64(stream) == 0);
}

static void io_failures64(void)
{
	static const long failures[] = { -1, -2, -22, LONG_MIN, 0, LONG_MAX };
	static const char bytes[] = "한글\n";

	for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
		reset64(bytes, sizeof(bytes) - 1, 512);
		struct NANO_FILE64 *stream = nano_fdopen64(3, "rb");
		char data[12] = { 0 };

		read_replies64[0] = 3;
		read_replies64[1] = failures[i];
		read_count64 = 2;
		assert(nano_fread64(data, 2, 4, stream) == 1);
		assert(memcmp(data, "한", 3) == 0 && data[3] == 0);
		assert(reads64 == 2 && nano_feof64(stream) == (failures[i] == 0));
		assert(nano_ferror64(stream) == (failures[i] != 0));
		assert(nano_getc64(stream) == -1 && reads64 == 2);
		/* Clear errors explicitly; the successful prefix is never read twice. */
		nano_clearerr64(stream);
		assert(nano_fread64(data, 1, 4, stream) == 4);
		assert(memcmp(data, "글\n", 4) == 0);
		assert(nano_fclose64(stream) == 0);

		reset64(NULL, 0, 512);
		stream = nano_fdopen64(3, "wb");
		write_replies64[0] = 3;
		write_replies64[1] = failures[i];
		write_count64 = 2;
		assert(nano_fwrite64(bytes, 2, 3, stream) == 1);
		assert(writes64 == 2 && output_size64 == 3 && nano_errno64 == 5);
		assert(memcmp(output64, "한", 3) == 0 && output64[3] == 0xa5);
		assert(nano_ferror64(stream) && !nano_feof64(stream));
		assert(nano_putc64('x', stream) == -1 && writes64 == 2);
		assert(nano_fflush64(stream) == -1 && nano_fflush64(NULL) == -1);
		assert(nano_fclose64(stream) == -1 && closes64 == 1);
		assert(nano_fileno64(stream) == -1); /* Successful close released it. */
	}
	/* Every partition of a short write tests exact pointer/count advancement. */
	for (unsigned int partition = 0; partition < 64; partition++) {
		reset64(NULL, 0, 512);
		struct NANO_FILE64 *stream = nano_fopen64("한글.TXT", "w");
		long count = 1;

		for (unsigned int i = 0; i < 6; i++) {
			if ((partition & (1U << i)) != 0) {
				write_replies64[write_count64++] = count;
				count = 0;
			}
			count++;
		}
		write_replies64[write_count64++] = count;
		assert(nano_fwrite64(bytes, 1, 7, stream) == 7);
		assert(nano_putc64(0x1ff, stream) == 255);
		assert(output_size64 == 8 && memcmp(output64, bytes, 7) == 0);
		assert(output64[7] == 255 && output64[8] == 0xa5);
		int called = writes64;

		assert(nano_fflush64(stream) == 0 && nano_fflush64(NULL) == 0);
		assert(writes64 == called && nano_fclose64(stream) == 0);
	}
	reset64(NULL, 0, 512);
	struct NANO_FILE64 *stream = nano_fdopen64(3, "wb");

	close_result64 = -1;
	assert(nano_fclose64(stream) == -1 && nano_fileno64(stream) == 3);
	assert(nano_fdopen64(3, "rb") == NULL); /* Failed close retains ownership. */
	close_result64 = 0;
	assert(nano_fclose64(stream) == -1 && closes64 == 2);
	assert(nano_fileno64(stream) == -1);
}

static void invalid_requests64(void)
{
	reset64(NULL, 0, 512);
	struct NANO_FILE64 *stream = nano_fdopen64(3, "wb");
	char data[4] = "abc";

	assert(nano_fwrite64(NULL, 0, SIZE_MAX, stream) == 0 && !nano_ferror64(stream));
	assert(nano_fwrite64(NULL, SIZE_MAX, 0, stream) == 0 && !nano_ferror64(stream));
	assert(nano_fwrite64(NULL, 1, 1, stream) == 0 && nano_errno64 == 22);
	nano_clearerr64(stream);
	assert(nano_fwrite64(data, SIZE_MAX, 2, stream) == 0 && nano_errno64 == 75);
	nano_clearerr64(stream);
	assert(nano_fwrite64(data, 1, (size_t) LONG_MAX + 1, stream) == 0);
	assert(nano_errno64 == 75);
	nano_clearerr64(stream);
	assert(nano_fwrite64((void *) (UINTPTR_MAX - 1), 1, 2, stream) == 0);
	assert(nano_errno64 == 75);
	nano_clearerr64(stream);
	assert(nano_fread64(data, 1, 1, stream) == 0 && nano_errno64 == 9);
	assert(reads64 == 0 && writes64 == 0);
	nano_clearerr64(stream);
	assert(nano_fwrite64(data, 1, 3, stream) == 3 && !nano_ferror64(stream));
	assert(nano_fclose64(stream) == 0);
	stream = nano_fdopen64(3, "rb");
	assert(nano_putc64('x', stream) == -1 && nano_errno64 == 9);
	assert(writes64 == 1 && nano_fclose64(stream) == -1);
}

static void console_streams64(void)
{
	reset64(NULL, 0, 2);
	assert(nano_fileno64(nano_stdout64) == 1 && nano_fileno64(nano_stderr64) == 2);
	assert(nano_fwrite64("stdout", 1, 6, nano_stdout64) == 6 && writes64 == 3);
	assert(nano_putc64('!', nano_stderr64) == '!');
	assert(output_size64 == 7 && memcmp(output64, "stdout!", 7) == 0);
	write_replies64[0] = -1; write_count64 = 1;
	assert(nano_putc64('x', nano_stdout64) == -1 && nano_ferror64(nano_stdout64));
	assert(nano_fflush64(NULL) == -1);
	assert(nano_fclose64(nano_stdout64) == -1 && nano_errno64 == 9);
	assert(nano_fclose64(nano_stderr64) == -1 && closes64 == 0);
	nano_clearerr64(nano_stdout64);
	assert(nano_fflush64(NULL) == 0);
	assert(nano_putc64('y', nano_stdout64) == 'y');
	assert(output_size64 == 8 && memcmp(output64, "stdout!y", 8) == 0);
}

static void utf_conversion64(void)
{
	static const struct {
		uint32_t scalar;
		const char *bytes;
		size_t size;
	} cases[] = {
		{ 0, "\0", 1 }, { 0x7f, "\x7f", 1 }, { 0xa2, "\xc2\xa2", 2 },
		{ 0xd55c, "\xed\x95\x9c", 3 }, { 0x1f642, "\xf0\x9f\x99\x82", 4 },
		{ 0x10ffff, "\xf4\x8f\xbf\xbf", 4 },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		uint32_t wide = 123;
		char text[5] = { 1, 2, 3, 4, 5 };
		/* Each truncated buffer ends at an ASan redzone (no padded string). */
		for (size_t length = 0; length < cases[i].size; length++) {
			char *fragment = malloc(length != 0 ? length : 1);

			assert(fragment != NULL);
			memcpy(fragment, cases[i].bytes, length);
			assert(nano_mbtowc64(&wide, fragment, length) == -1);
			assert(nano_errno64 == 84 && wide == 123);
			free(fragment);
		}
		assert(nano_mbtowc64(&wide, cases[i].bytes, cases[i].size) ==
			(cases[i].scalar == 0 ? 0 : (int) cases[i].size));
		assert((uint32_t) wide == cases[i].scalar);
		assert(nano_wctomb64(text, wide) == (int) cases[i].size);
		assert(memcmp(text, cases[i].bytes, cases[i].size) == 0);
		assert(text[cases[i].size] == (char) (cases[i].size + 1));
	}
	static const char *const invalid[] = {
		"\x80", "\xc0\x80", "\xe0\x80\x80", "\xed\xa0\x80",
		"\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe3\x28\xa0",
	};

	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		uint32_t wide = 456;

		assert(nano_mbtowc64(&wide, invalid[i], strlen(invalid[i])) == -1);
		assert(nano_errno64 == 84 && wide == 456);
	}
	char text[4] = { 1, 2, 3, 4 };
	static const uint32_t scalars[] = { 0xd800, 0xdfff, 0x110000, UINT32_MAX };

	for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) {
		assert(nano_wctomb64(text, scalars[i]) == -1);
		assert(nano_errno64 == 84 && memcmp(text, "\1\2\3\4", 4) == 0);
		assert(nano_wcwidth64(scalars[i]) == -1);
	}
	assert(nano_mbtowc64(NULL, "한", 3) == 3);
	assert(nano_mbtowc64(NULL, NULL, 0) == 0 && nano_wctomb64(NULL, 0) == 0);
	assert(nano_wcwidth64(0) == 0 && nano_wcwidth64('\n') == -1);
	assert(nano_wcwidth64(0x7f) == -1 && nano_wcwidth64(0x9f) == -1);
	assert(nano_wcwidth64('A') == 1 && nano_wcwidth64(0xfffd) == 1);
	assert(nano_wcwidth64(0x3130) == 1 && nano_wcwidth64(0x3131) == 2);
	assert(nano_wcwidth64(0x3163) == 2 && nano_wcwidth64(0x3164) == 1);
	assert(nano_wcwidth64(0xabff) == 1 && nano_wcwidth64(0xac00) == 2);
	assert(nano_wcwidth64(0xd7a3) == 2 && nano_wcwidth64(0xd7a4) == 1);
	/* The renderer gives unsupported combining/CJK glyphs one fallback cell. */
	assert(nano_wcwidth64(0x301) == 1 && nano_wcwidth64(0x4e00) == 1);
}

int main(void)
{
	modes_and_ownership64();
	read_boundaries64();
	io_failures64();
	invalid_requests64();
	console_streams64();
	utf_conversion64();
	return 0;
}
