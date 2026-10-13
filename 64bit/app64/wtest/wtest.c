/* FAT32 write/readback smoke. --verify modes only read persisted files.
 * Run writes on disposable images; close errors and partial I/O are checked.
 */
#include <mowos.h>

#define BIG_SIZE 1500U
#define CHUNK_SIZE 4096U
#define HUGE_SIZE (384U * CHUNK_SIZE)

static const char text[] = "phase0 fat12 write ok\n";
static unsigned char chunk[CHUNK_SIZE];

static int fail(const char *stage)
{
	puts(stage);
	puts("wtest: FAIL");
	return 1;
}

/* Reject non-progress and impossible counts before advancing the buffer. */
static int transfer(int fd, void *buf, size_t size, int writing)
{
	unsigned char *bytes = buf;
	size_t offset = 0;

	while (offset < size) {
		long count = writing ? write(fd, bytes + offset, size - offset) :
			read(fd, bytes + offset, size - offset);

		if (count <= 0 || (size_t) count > size - offset) { return -1; }
		offset += (size_t) count;
	}
	return 0;
}

static unsigned char pattern(size_t offset, int huge)
{
	return huge ? (unsigned char) (offset % 251) :
		(unsigned char) ('a' + offset % 26);
}

static int test_file(const char *path, size_t size, int kind, int verify_only)
{
	int fd;
	int result = 0;
	size_t offset;
	unsigned char extra;

	if (!verify_only) {
		fd = open(path, O_CREAT | O_TRUNC);
		if (fd < 0) { return fail("wtest: create error"); }
		for (offset = 0; offset < size; ) {
			size_t count = size - offset;
			size_t i;

			if (count > CHUNK_SIZE) { count = CHUNK_SIZE; }
			for (i = 0; i < count; i++) {
				chunk[i] = kind == 0 ? (unsigned char) text[offset + i] :
					pattern(offset + i, kind == 2);
			}
			if (transfer(fd, chunk, count, 1) != 0) {
				result = 1;
				break;
			}
			offset += count;
		}
		/* Always close, including a failed write; either error fails the test. */
		if (close(fd) < 0) { result = 1; }
		if (result) { return fail("wtest: write/close error"); }
	}

	fd = open(path, 0);
	if (fd < 0) { return fail("wtest: reopen error"); }
	for (offset = 0; offset < size; ) {
		size_t count = size - offset;
		size_t i;

		if (count > CHUNK_SIZE) { count = CHUNK_SIZE; }
		if (transfer(fd, chunk, count, 0) != 0) {
			result = 1;
			break;
		}
		for (i = 0; i < count; i++) {
			unsigned char expected = kind == 0 ? (unsigned char) text[offset + i] :
				pattern(offset + i, kind == 2);

			if (chunk[i] != expected) { result = 1; }
		}
		if (result) { break; }
		offset += count;
	}
	/* Exact size matters: a correct prefix with trailing bytes is a failure. */
	if (!result && read(fd, &extra, 1) != 0) { result = 1; }
	if (close(fd) < 0) { result = 1; }
	return result ? fail("wtest: readback/close error") : 0;
}

int main(int argc, char **argv)
{
	const char *path = "TEST.TXT";
	int huge = 0;
	int verify_only = 0;

	if (argc > 2) { return fail("wtest: usage error"); }
	if (argc == 2) {
		if (strcmp(argv[1], "HUGE") == 0) { huge = 1; }
		else if (strcmp(argv[1], "--verify") == 0) { verify_only = 1; }
		else if (strcmp(argv[1], "--verify-huge") == 0) {
			huge = 1;
			verify_only = 1;
		} else { path = argv[1]; }
	}
	if (test_file(path, sizeof(text) - 1, 0, verify_only) != 0 ||
		test_file("BIG.TXT", BIG_SIZE, 1, verify_only) != 0 ||
		(huge && test_file("HUGE.TXT", HUGE_SIZE, 2, verify_only) != 0)) {
		return 1;
	}
	return puts("wtest: PASS") < 0 ? 1 : 0;
}
