/* Raw A1.4 execution fixture, not libc. Legacy console/tick calls are used
 * solely for observation; UNIX read/write remain A1.5 work.
 */
#include <mowkow/syscall64.h>
#include <stddef.h>
#include <stdint.h>

void exec_probe_entry64(uintptr_t argument, char **legacy_argv);

static int64_t call64(uint64_t number, uintptr_t a, uintptr_t b, uintptr_t c)
{
	return unix64_syscall6(number, a, b, c, 0, 0, 0);
}

static void message64(const char *text)
{
	size_t size = 0;

	while (text[size] != 0) { size++; }
	(void) call64(2, 1, (uintptr_t) text, size);
}

static void stop64(int status)
{
#ifdef EXEC_LAUNCHER64
	(void) call64(1, (uintptr_t) status, 0, 0);
#else
	(void) call64(UNIX64_SYS_EXIT, (uintptr_t) status, 0, 0);
#endif
	__builtin_trap();
}

static void failure64(unsigned int line)
{
	char digits[5] = {(char) ('0' + line / 1000), (char) ('0' + line / 100 % 10),
		(char) ('0' + line / 10 % 10), (char) ('0' + line % 10), 0};

	message64("exec: FAIL "); message64(digits); message64("\n"); stop64(1);
}

#define REQUIRE64(condition) do { if (!(condition)) { failure64(__LINE__); } } while (0)

static uintptr_t read_tp64(void)
{
	uintptr_t pointer;

#ifdef __aarch64__
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (pointer));
#else
	__asm__ volatile ("mov %%fs:0, %0" : "=r" (pointer) :: "memory");
#endif
	return pointer;
}

#ifdef EXEC_LAUNCHER64
static volatile uint64_t witness64 = 0x7198;
static uint64_t tcb64[4] __attribute__((aligned(16)));

void exec_probe_entry64(uintptr_t argument, char **legacy_argv)
{
	char *args[] = {"uxprobe", argument > 1 ? legacy_argv[1] : "1", "3", NULL};
	char *env[] = {"LANG=ko", NULL};

	tcb64[0] = (uintptr_t) tcb64;
	REQUIRE64(call64(UNIX64_SYS_TCB_SET, (uintptr_t) tcb64, 0, 0) == 0);
	REQUIRE64(call64(UNIX64_SYS_EXECVE, (uintptr_t) "absent", 0, 0) == -2);
	REQUIRE64(call64(UNIX64_SYS_EXECVE, (uintptr_t) "uxprobe", UINTPTR_MAX, 0) == -14);
	REQUIRE64(call64(UNIX64_SYS_EXECVE, (uintptr_t) "uxbad", 0, 0) == -8);
	REQUIRE64(witness64 == 0x7198);
	REQUIRE64(read_tp64() == (uintptr_t) tcb64);
	message64("exec: ROLLBACK PASS\n");
	(void) call64(UNIX64_SYS_EXECVE, (uintptr_t) "uxprobe", (uintptr_t) args, (uintptr_t) env);
	REQUIRE64(0);
}
#else
struct PROBE_PHDR64 {
	uint32_t type, flags;
	uint64_t offset, address, physical, filesz, memsz, align;
};

static _Thread_local volatile uint64_t tls_value64 = 0x12345678;
static _Thread_local volatile int tls_errno64;
static unsigned char tls_area64[16384] __attribute__((aligned(4096)));

static int equal64(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static void initialize_tls64(const struct PROBE_PHDR64 *phdr, size_t count)
{
	const struct PROBE_PHDR64 *tls = NULL;
	uintptr_t tp = (uintptr_t) tls_area64 + 8192;
	uintptr_t template;

	for (size_t i = 0; i < count; i++) { if (phdr[i].type == 7) { tls = &phdr[i]; } }
	REQUIRE64(tls != NULL && tls->align != 0 && tls->align <= 4096);
	REQUIRE64(tls->filesz <= tls->memsz && tls->memsz < 4096);
#ifdef __aarch64__
	template = tp + ((16 + tls->align - 1) & ~(tls->align - 1));
#else
	template = tp - ((tls->memsz + tls->align - 1) & ~(tls->align - 1));
	*(uintptr_t *) tp = tp;
#endif
	for (size_t i = 0; i < tls->filesz; i++) {
		*(unsigned char *) (template + i) = *(const unsigned char *) (tls->address + i);
	}
	REQUIRE64(call64(UNIX64_SYS_TCB_SET, tp, 0, 0) == 0);
	REQUIRE64(tls_value64 == 0x12345678 && tls_errno64 == 0);
}

static void fp64(int dirty)
{
#ifdef __aarch64__
	uint64_t control;

	__asm__ volatile ("mrs %0, fpcr" : "=r" (control));
	if (!dirty) { REQUIRE64(control == 0); }
	else { control = 0x400000; __asm__ volatile ("msr fpcr, %0" :: "r" (control)); }
#else
	uint32_t mxcsr;
	uint16_t control;

	__asm__ volatile ("stmxcsr %0; fnstcw %1" : "=m" (mxcsr), "=m" (control));
	if (!dirty) { REQUIRE64(mxcsr == 0x1f80 && control == 0x037f); }
	else {
		mxcsr = 0x5f80; control = 0x0b7f;
		__asm__ volatile ("ldmxcsr %0; fldcw %1" :: "m" (mxcsr), "m" (control));
	}
#endif
}

void exec_probe_entry64(uintptr_t argument, char **legacy_argv)
{
	uint64_t *stack = (void *) argument;
	char **argv = (void *) (stack + 1);
	char **env = argv + 4;
	uint64_t *aux = (void *) (env + 2);
	const struct PROBE_PHDR64 *phdr = NULL;
	size_t count = 0;
	uintptr_t random = 0;
	char *execfn = NULL;
	int id, round;

	(void) legacy_argv;
	REQUIRE64((argument & 15) == 0 && stack[0] == 3 && argv[3] == NULL && env[1] == NULL);
	REQUIRE64(equal64(argv[0], "uxprobe") && equal64(env[0], "LANG=ko"));
	id = argv[1][0] - '0'; round = argv[2][0] - '0';
	REQUIRE64(id >= 1 && id <= 2 && round >= 0 && round <= 3);
	for (unsigned int i = 0; i < 16 && aux[2 * i] != 0; i++) {
		uint64_t type = aux[2 * i], value = aux[2 * i + 1];

		if (type == 3) { phdr = (void *) value; }
		if (type == 5) { count = value; }
		if (type == 4) { REQUIRE64(value == 56); }
		if (type == 6) { REQUIRE64(value == 4096); }
		if (type == 25) { random = value; }
		if (type == 31) { execfn = (void *) value; }
	}
	REQUIRE64(phdr != NULL && count > 0 && random != 0 && equal64(execfn, "uxprobe"));
	REQUIRE64(*(volatile unsigned char *) random || *(volatile unsigned char *) (random + 1));
	fp64(0);
	initialize_tls64(phdr, count);
	tls_value64 = 0x12340000U + (unsigned int) id;
	tls_errno64 = 100 + id;
	uint64_t start = (uint64_t) call64(9, 0, 0, 0);

	do {
		REQUIRE64(read_tp64() == (uintptr_t) tls_area64 + 8192);
		REQUIRE64(tls_value64 == 0x12340000U + (unsigned int) id && tls_errno64 == 100 + id);
	} while ((uint64_t) call64(9, 0, 0, 0) - start < 400);
	if (round != 0) {
		char next_round[2] = {(char) ('0' + round - 1), 0};
		char *next[] = {argv[0], argv[1], next_round, NULL};

		fp64(1);
		(void) call64(UNIX64_SYS_EXECVE, (uintptr_t) "uxprobe", (uintptr_t) next, (uintptr_t) env);
		REQUIRE64(0);
	}
	message64(id == 1 ? "exec 1: WAIT\n" : "exec 2: WAIT\n");
	char input[8];

	REQUIRE64(call64(3, 0, (uintptr_t) input, sizeof(input)) > 0);
	REQUIRE64(read_tp64() == (uintptr_t) tls_area64 + 8192);
	REQUIRE64(tls_value64 == 0x12340000U + (unsigned int) id && tls_errno64 == 100 + id);
	message64(id == 1 ? "exec 1: PASS\n" : "exec 2: PASS\n");
	stop64(0);
}
#endif
