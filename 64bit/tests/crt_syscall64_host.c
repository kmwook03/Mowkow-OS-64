#include <assert.h>
#include <limits.h>
#include <string.h>

#include <mowos.h>
#include <syscall.h>

static long replies64[32];
static size_t reply_count64, calls64;
static long number64, arguments64[6];
static char output64[128];
static size_t output_size64;
static int capture64;
static const char large_string64[] = "large";
static size_t large_length64;

size_t fixture_strlen64(const char *s)
{
	if (s == large_string64) { return large_length64; }
	size_t length = 0;

	while (s[length] != '\0') { length++; }
	return length;
}

static void reset64(const long *replies, size_t count)
{
	assert(count <= sizeof(replies64) / sizeof(replies64[0]));
	memcpy(replies64, replies, count * sizeof(*replies));
	reply_count64 = count;
	calls64 = output_size64 = 0;
	capture64 = 0;
	memset(output64, 0xa5, sizeof(output64));
	memset(arguments64, 0, sizeof(arguments64));
}

long syscall0(long nr)
{
	number64 = nr;
	assert(calls64 < reply_count64);
	return replies64[calls64++];
}

long syscall1(long nr, long a0)
{
	arguments64[0] = a0;
	return syscall0(nr);
}

long syscall2(long nr, long a0, long a1)
{
	arguments64[1] = a1;
	return syscall1(nr, a0);
}

long syscall3(long nr, long a0, long a1, long a2)
{
	arguments64[2] = a2;
	long result = syscall2(nr, a0, a1);

	if (capture64) {
		assert(nr == SYS_WRITE && a0 == 1 && a2 > 0);
		if (result > 0 && result <= a2) {
			assert((size_t) result <= sizeof(output64) - output_size64);
			memcpy(output64 + output_size64, (const void *) a1, (size_t) result);
			output_size64 += (size_t) result;
		}
	}
	return result;
}

long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	arguments64[3] = a3;
	return syscall3(nr, a0, a1, a2);
}

long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	arguments64[4] = a4;
	return syscall4(nr, a0, a1, a2, a3);
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	arguments64[5] = a5;
	return syscall5(nr, a0, a1, a2, a3, a4);
}

static void io_wrappers64(void)
{
	char buffer[16] = "keep";
	static const long results[] = { LONG_MIN, INT_MIN, -22, -5, -1, 0, 1, 7, 16 };

	for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		reset64(&results[i], 1);
		assert(write(7, buffer, sizeof(buffer)) == results[i]);
		assert(calls64 == 1 && number64 == SYS_WRITE);
		assert(arguments64[0] == 7 && arguments64[1] == (long) buffer);
		assert(arguments64[2] == sizeof(buffer));
		reset64(&results[i], 1);
		assert(read(9, buffer, sizeof(buffer)) == results[i]);
		assert(calls64 == 1 && number64 == SYS_READ);
		assert(arguments64[0] == 9 && arguments64[1] == (long) buffer);
		assert(arguments64[2] == sizeof(buffer));
		assert(memcmp(buffer, "keep", 5) == 0);
	}
	long result = 0;

	reset64(&result, 1);
	assert(read(-1, NULL, 0) == 0 && calls64 == 1 && arguments64[2] == 0);
	reset64(&result, 1);
	assert(write(-1, NULL, 0) == 0 && calls64 == 1 && arguments64[2] == 0);
	result = -22;
	reset64(&result, 1);
	assert(write(1, buffer, (size_t) LONG_MAX) == -22);
	assert(arguments64[2] == LONG_MAX);
	result = -5;
	reset64(&result, 1);
	assert(file_call64(FILE_READ64, 7, (unsigned long) buffer, 16) == -5);
	assert(calls64 == 1 && number64 == SYS_FILE && arguments64[0] == FILE_READ64);
	assert(arguments64[1] == 7 && arguments64[2] == (long) buffer && arguments64[3] == 16);
	result = -22;
	/* File wrappers also preserve their historical signed ABI. */
	reset64(&result, 1);
	assert(open("file", O_CREAT | O_TRUNC) == -22 && number64 == SYS_OPEN);
	assert(arguments64[1] == (O_CREAT | O_TRUNC));
	reset64(&result, 1);
	assert(close(7) == -22 && number64 == SYS_CLOSE && arguments64[0] == 7);
	result = LONG_MAX;
	reset64(&result, 1);
	assert(seek(9, -123, 2) == LONG_MAX && number64 == SYS_SEEK);
	assert(arguments64[0] == 9 && arguments64[1] == -123 && arguments64[2] == 2);
}

static void puts_case64(const char *s, const long *replies, size_t count,
	int expected, const char *prefix)
{
	reset64(replies, count);
	capture64 = 1;
	assert(puts(s) == expected);
	assert(calls64 == count);
	assert(output_size64 == fixture_strlen64(prefix));
	assert(memcmp(output64, prefix, output_size64) == 0);
	assert((unsigned char) output64[output_size64] == 0xa5);
}

static void puts_progress64(void)
{
	/* Every partition of the body checks exact pointer/length advancement. */
	for (unsigned int mask = 0; mask < 32; mask++) {
		long replies[7];
		size_t count = 0;
		long chunk = 1;

		for (unsigned int i = 0; i < 5; i++) {
			if ((mask & (1U << i)) != 0) {
				replies[count++] = chunk;
				chunk = 0;
			}
			chunk++;
		}
		replies[count++] = chunk;
		replies[count++] = 1;
		puts_case64("abcdef", replies, count, 7, "abcdef\n");
	}
	static const long failures[] = { -1, -5, -22, INT_MIN, LONG_MIN, 0, 100 };

	for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
		int expected = failures[i] == LONG_MIN ? -75 :
			(failures[i] >= 0 ? -5 : (int) failures[i]);
		long replies[] = { failures[i] };

		puts_case64("abcdef", replies, 1, expected, "");
		long partial[] = { 2, failures[i] };

		puts_case64("abcdef", partial, 2, expected, "ab");
		long newline[] = { 6, failures[i] };

		puts_case64("abcdef", newline, 2, expected, "abcdef");
		puts_case64("", replies, 1, expected, "");
	}
	long newline = 1;

	puts_case64("", &newline, 1, 1, "\n");
	long oversized_tail[] = { 2, 5 };
	long oversized_newline[] = { 6, 2 };

	puts_case64("abcdef", oversized_tail, 2, -5, "ab");
	puts_case64("abcdef", oversized_newline, 2, -5, "abcdef");
	long utf[] = { 1, 1, 1, 1, 1, 1, 1 };

	puts_case64("가나", utf, 7, 7, "가나\n");
	long unused = -5;

	reset64(&unused, 0);
	assert(puts(NULL) == -22 && calls64 == 0);
	large_length64 = INT_MAX;
	assert(puts(large_string64) == -75 && calls64 == 0);
	large_length64 = (size_t) -1;
	assert(puts(large_string64) == -75 && calls64 == 0);
	/* The largest representable success count must pass the length check. */
	large_length64 = INT_MAX - 1U;
	reset64(&unused, 1);
	assert(puts(large_string64) == -5 && calls64 == 1);
	assert(arguments64[2] == INT_MAX - 1L);
}

static void tty_wrappers64(void)
{
	long reply = -22;

	reset64(&reply, 1);
	assert(tty_cursor(-1, 2) == -22 && calls64 == 1 && number64 == SYS_TTY);
	assert(arguments64[0] == 7 && arguments64[1] == -1 && arguments64[2] == 2);
	reply = 0x1ffffffffL;
	reset64(&reply, 1);
	assert(tty_input_mode() == reply && calls64 == 1 && number64 == SYS_TTY);
	assert(arguments64[0] == 8);
	assert(TTY_INPUT_HANGUL(reply) == 1 && TTY_INPUT_GEN(reply) == 0xffffffffU);
	reply = -22;
	reset64(&reply, 1);
	assert(tty_input_mode() == -22);
}

int main(void)
{
	io_wrappers64();
	puts_progress64();
	tty_wrappers64();
	return 0;
}
