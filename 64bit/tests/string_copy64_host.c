#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src64/kernel/process64.c"
#include "../src64/kernel/gui64.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "String copy check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

static int copies64(void)
{
	char source[64], destination[80], saved[80];
	size_t used;

	for (size_t length = 0; length < sizeof(source); length++) {
		memset(source, 'x', length);
		source[length] = '\0';
		for (size_t capacity = 0; capacity <= sizeof(source); capacity++) {
			size_t copied = length < capacity ? length : capacity - (capacity != 0);

			memset(destination, 0xa5, sizeof(destination));
			CHECK64(str_copy64(destination + 8, capacity, source) ==
				(capacity == 0 ? -22 : (length < capacity ? 0 : -7)));
			CHECK64(memcmp(destination + 8, source, copied) == 0);
			if (capacity != 0) {
				CHECK64(destination[8 + copied] == '\0');
			}
			for (size_t i = 0; i < sizeof(destination); i++) {
				if (i < 8 || i >= 8 + capacity) {
					CHECK64((unsigned char) destination[i] == 0xa5);
				}
			}
		}
	}
	CHECK64(str_copy64(NULL, 1, source) == -22);
	CHECK64(str_copy64(destination, sizeof(destination), NULL) == -22);
	CHECK64(destination[0] == '\0');
	CHECK64(str_copy64(destination, sizeof(destination), "self") == 0);
	CHECK64(str_copy64(destination, sizeof(destination), destination) == 0);
	CHECK64(strcmp(destination, "self") == 0);
	used = 4;
	CHECK64(str_append64(destination, sizeof(destination), &used, destination) == 0);
	CHECK64(used == 8 && strcmp(destination, "selfself") == 0);
	memcpy(saved, destination, sizeof(saved));
	CHECK64(str_append64(destination, 8, &used, "x") == -22);
	CHECK64(str_append64(destination, 9, &used, "x") == -7);
	CHECK64(memcmp(saved, destination, sizeof(saved)) == 0 && used == 8);
	CHECK64(str_append64(destination, sizeof(destination), NULL, "x") == -22);
	used = SIZE_MAX;
	CHECK64(str_append64(destination, sizeof(destination), &used, "x") == -22);
	used = 7;
	CHECK64(str_append64(destination, sizeof(destination), &used, "x") == -22);
	CHECK64(memcmp(saved, destination, sizeof(saved)) == 0);
	return 0;
}

static int callers64(void)
{
	struct GUI64_WIN window = { 0 };
	struct PROCESS64_ARGS args;
	char long_title[80];

	memset(long_title, 'T', sizeof(long_title) - 1);
	long_title[sizeof(long_title) - 1] = '\0';
	set_title(&window, long_title);
	CHECK64(strlen(window.title) == sizeof(window.title) - 1);
	set_title(&window, "터미널");
	CHECK64(strcmp(window.title, "터미널") == 0);
	set_title(&window, NULL);
	CHECK64(window.title[0] == '\0');
	CHECK64(parse_args64("  hello   파일.txt tail  ", &args) == 0 && args.argc == 3);
	CHECK64(strcmp(args.line + args.offsets[0], "hello") == 0);
	CHECK64(strcmp(args.line + args.offsets[1], "파일.txt") == 0);
	CHECK64(args.lengths[1] == strlen("파일.txt"));
	CHECK64(parse_args64("a b c d e f g h", &args) == 0 && args.argc == 8);
	CHECK64(parse_args64("a b c d e f g h i", &args) == PROCESS64_ERR_ARGS);
	CHECK64(parse_args64(NULL, &args) == PROCESS64_ERR_INVALID);
	return 0;
}

static int guards64(void)
{
	long page = sysconf(_SC_PAGESIZE);
	char *source, *output, *source_end, *output_end;
	struct PROCESS64_ARGS args;
	size_t used;

	CHECK64(page >= PROCESS64_CMDLINE_MAX);
	source = mmap(NULL, (size_t) page * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	output = mmap(NULL, (size_t) page * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK64(source != MAP_FAILED && output != MAP_FAILED);
	source_end = source + page;
	output_end = output + page;
	CHECK64(mprotect(source_end, (size_t) page, PROT_NONE) == 0);
	CHECK64(mprotect(output_end, (size_t) page, PROT_NONE) == 0);
	CHECK64(str_copy64(output_end, 0, source_end) == -22);
	used = 0;
	CHECK64(str_append64(output_end, 0, &used, source_end) == -22);
	for (size_t capacity = 1; capacity <= 64; capacity++) {
		memset(source_end - capacity, 'x', capacity);
		CHECK64(str_copy64(output_end - capacity, capacity, source_end - capacity) == -7);
		source_end[-1] = '\0';
		CHECK64(str_copy64(output_end - capacity, capacity, source_end - capacity) == 0);
		/* Source NUL at its object end must bound a much larger destination. */
		source_end[-1] = '\0';
		CHECK64(str_copy64(output_end - capacity, capacity, source_end - 1) == 0);
		used = 0;
		output_end[-(long) capacity] = '\0';
		memset(source_end - capacity, 'x', capacity);
		CHECK64(str_append64(output_end - capacity, capacity, &used,
			source_end - capacity) == -7);
		CHECK64(used == 0 && output_end[-(long) capacity] == '\0');
		source_end[-1] = '\0';
		CHECK64(str_append64(output_end - capacity, capacity, &used,
			source_end - capacity) == 0);
		CHECK64(used == capacity - 1 && output_end[-1] == '\0');
	}
	memset(source_end - PROCESS64_CMDLINE_MAX, 'a', PROCESS64_CMDLINE_MAX);
	CHECK64(parse_args64(source_end - PROCESS64_CMDLINE_MAX, &args) == PROCESS64_ERR_ARGS);
	source_end[-1] = '\0';
	CHECK64(parse_args64(source_end - PROCESS64_CMDLINE_MAX, &args) == 0);
	CHECK64(args.argc == 1 && args.lengths[0] == PROCESS64_CMDLINE_MAX - 1);
	CHECK64(munmap(source, (size_t) page * 2) == 0);
	CHECK64(munmap(output, (size_t) page * 2) == 0);
	return 0;
}

int main(void)
{
	CHECK64(copies64() == 0);
	CHECK64(callers64() == 0);
	CHECK64(guards64() == 0);
	puts("Bounded copy/append, GUI/process callers and guard pages: PASS");
	return 0;
}
