/* Each protection fault is a separate execution; no privileged test syscall. */
#include <mowos.h>
#include <syscall.h>

#define HEAP_END64 0x100100000UL
#define GUARD_LOW64 0x1001ff000UL
#define GUARD_HIGH64 0x100210000UL
#ifdef __aarch64__
#define KERNEL_ADDRESS64 0xffffff8000080000UL
#else
#define KERNEL_ADDRESS64 0x100000UL
#endif
#define CHECK64(test) do { if (!(test)) { puts("faulttest: FAIL " #test); return 1; } } while (0)

int main(int argc, char **argv);
static const char readonly64[] = "faulttest: readonly source\n";

static int pointers64(unsigned long peer)
{
	unsigned char *storage = alloc(12288);
	struct FILE_INFO64 info;
	unsigned long bad[] = {0, KERNEL_ADDRESS64, 0x200000000UL, GUARD_LOW64,
		GUARD_HIGH64, ~0UL - 3, peer};
	int fd;

	CHECK64(storage != NULL);
	fd = open("M4TEST.TXT", O_CREAT | O_TRUNC);
	CHECK64(fd >= 3);
	/* Cross two page boundaries while keeping a single backend operation. */
	unsigned char *data = storage + 4093;

	for (size_t i = 0; i < 8192; i++) { data[i] = (unsigned char) (i * 13); }
	CHECK64(write(fd, data, 8192) == 8192);
	CHECK64(seek(fd, 0, 0) == 0);
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		void *p = (void *) bad[i];

		CHECK64(read(fd, p, 8) == -1);
		CHECK64(read(0, p, 8) == -1);
		CHECK64(write(fd, p, 8) == -1);
		CHECK64(write(1, p, 8) == -1);
		CHECK64(open(p, O_TRUNC) == -1);
		CHECK64(file_call64(FILE_OPEN64, bad[i], 0, 0) == -14);
		CHECK64(file_call64(FILE_READ64, (unsigned long) fd, bad[i], 8) == -14);
		CHECK64(file_call64(FILE_FSTAT64, (unsigned long) fd, bad[i], 0) == -14);
		CHECK64(file_call64(FILE_STAT64, (unsigned long) "M4TEST.TXT", bad[i], 0) == -14);
	}
	CHECK64(read(fd, (void *) readonly64, 1) == -1);
	CHECK64(read(0, (void *) main, 1) == -1);
	CHECK64(file_call64(FILE_READ64, (unsigned long) fd, (unsigned long) main, 1) == -14);
	CHECK64(file_call64(FILE_FSTAT64, (unsigned long) fd, (unsigned long) readonly64, 0) == -14);
	CHECK64(write(fd, data, ~(size_t) 0) == -1);
	CHECK64(read(fd, data, ~(size_t) 0) == -1);
	CHECK64(file_call64(FILE_READ64, (unsigned long) fd, (unsigned long) data, ~0UL) == -22);
	CHECK64(read(fd, NULL, 0) == -1 && write(fd, NULL, 0) == -1);
	CHECK64(read(fd, (void *) HEAP_END64, 0) == 0);
	CHECK64(write(fd, (void *) HEAP_END64, 0) == 0);
	CHECK64(read(fd, (void *) (HEAP_END64 - 4), 8) == -1);
	CHECK64(write(fd, (void *) (HEAP_END64 - 4), 8) == -1);
	memset((void *) (HEAP_END64 - 4), 'x', 4);
	CHECK64(open((void *) (HEAP_END64 - 4), 0) == -1);
	CHECK64(file_call64(FILE_OPEN64, HEAP_END64 - 4, 0, 0) == -14);
	memset(data, 'x', 256);
	CHECK64(file_call64(FILE_OPEN64, (unsigned long) data, 0, 0) == -36);
	CHECK64(file_call64(FILE_OPEN64, (unsigned long) "\xff", 0, 0) == -22);
	memset(data, 0, 8192);
	CHECK64(read(fd, data, 8192) == 8192);
	for (size_t i = 0; i < 8192; i++) { CHECK64(data[i] == (unsigned char) (i * 13)); }
	CHECK64(file_call64(FILE_FSTAT64, (unsigned long) fd, (unsigned long) &info, 0) == 0);
	CHECK64(info.size64 == 8192);
	CHECK64(file_call64(FILE_SYNC64, (unsigned long) fd, 0, 0) == 0);
	CHECK64(close(fd) == 0);
	CHECK64(write(1, readonly64, sizeof(readonly64) - 1) == sizeof(readonly64) - 1);
	puts("faulttest: PASS pointers");
	return 0;
}

static unsigned long address64(const char *text)
{
	unsigned long address = 0;

	/* Hex digits without a prefix, supplied by the external test harness. */
	for (size_t i = 0; text[i]; i++) {
		unsigned int digit = text[i] >= '0' && text[i] <= '9' ?
			(unsigned int) (text[i] - '0') : text[i] >= 'a' && text[i] <= 'f' ?
			(unsigned int) (text[i] - 'a' + 10) : 16;

		if (i >= 16 || digit >= 16) { return 0; }
		address = address * 16 + digit;
	}
	return address;
}

int main(int argc, char **argv)
{
	unsigned long peer = argc == 3 ? address64(argv[2]) : 0;
	unsigned long target = 0;
	_Alignas(16) unsigned char stack_code[16] = {0};
	unsigned char *heap = alloc(4096);

	if (argc < 2 || argc > 3 || heap == NULL) { return 2; }
	if (strcmp(argv[1], "pointers") == 0) { return pointers64(peer); }
	if (strcmp(argv[1], "input") == 0) {
		char line[16] = {0};

		puts("faulttest: INPUT");
		CHECK64(read(0, line, sizeof(line)) == 3);
		CHECK64(memcmp(line, "ok\n", 3) == 0 && line[3] == 0);
		puts("faulttest: PASS input");
		return 0;
	}
	tty_raw(1); /* Fault cleanup must restore cooked mode. */
	puts("faulttest: READY");
	if (strcmp(argv[1], "kernel-read") == 0 || strcmp(argv[1], "kernel-write") == 0) {
		target = KERNEL_ADDRESS64;
	} else if (strcmp(argv[1], "peer-read") == 0 || strcmp(argv[1], "peer-write") == 0) {
		if (!peer) { return 2; }
		target = peer;
	} else if (strcmp(argv[1], "code-write") == 0) { target = (unsigned long) main; }
	else if (strcmp(argv[1], "rodata-write") == 0) { target = (unsigned long) readonly64; }
	else if (strcmp(argv[1], "guard-low") == 0) { target = GUARD_LOW64; }
	else if (strcmp(argv[1], "guard-high") == 0) { target = GUARD_HIGH64; }
	else if (strcmp(argv[1], "unmapped") == 0) { target = 0x200000000UL; }
	else if (strcmp(argv[1], "heap-exec") == 0 || strcmp(argv[1], "stack-exec") == 0) {
#ifdef __aarch64__
		const unsigned char instruction[] = {0xc0, 0x03, 0x5f, 0xd6}; /* ret */
#else
		const unsigned char instruction[] = {0xc3}; /* ret */
#endif
		memcpy(heap, instruction, sizeof(instruction));
		memcpy(stack_code, instruction, sizeof(instruction));
		target = (unsigned long) (argv[1][0] == 'h' ? heap : stack_code);
		((void (*)(void)) target)();
		puts("faulttest: FAIL executable data");
		return 1;
	} else { return 2; }
	if (strcmp(argv[1], "kernel-read") == 0 || strcmp(argv[1], "peer-read") == 0) {
		volatile unsigned char byte = *(volatile unsigned char *) target;

		(void) byte;
	} else { *(volatile unsigned char *) target = 0xa5; }
	puts("faulttest: FAIL accessible target");
	return 1;
}
