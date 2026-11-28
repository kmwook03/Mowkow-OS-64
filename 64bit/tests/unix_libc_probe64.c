/* Real mlibc fixture: no replacement entry, allocator, stdio, TLS or errno. */
#include <errno.h>
#include <fcntl.h>
#include <mowkow/syscall64.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static _Thread_local volatile unsigned int initialized = 0x12345678;
static _Thread_local volatile unsigned int zeroed;
static int constructor_ran;
static int identifier;

static void __attribute__((constructor)) initialize(void)
{
	constructor_ran = initialized == 0x12345678 && zeroed == 0;
}

static void finished(void) { printf("libc %d: ATEXIT\n", identifier); }

static void __attribute__((destructor)) finalize(void)
{
	static const char marker[] = "libc: DTOR\n";

	(void) write(1, marker, sizeof(marker) - 1);
}

static void require(int condition, int line)
{
	if (condition) { return; }
	fprintf(stderr, "libc: FAIL %d errno %d\n", line, errno);
	exit(1);
}
#define REQUIRE(condition) require(!!(condition), __LINE__)

void signal_restorer64(void);
static volatile int signal_count64;
static volatile unsigned int private_data64 = 71;

static void fp_control64(int dirty, int check)
{
#ifdef __aarch64__
	uint64_t value;

	if (check) {
		__asm__ volatile ("mrs %0, fpcr" : "=r" (value));
		REQUIRE(value == (dirty ? 0x400000U : 0));
	} else {
		value = dirty ? 0x400000U : 0;
		__asm__ volatile ("msr fpcr, %0" :: "r" (value));
	}
#else
	uint32_t sse = dirty ? 0x5f80 : 0x1f80;
	uint16_t x87 = dirty ? 0x0b7f : 0x037f;

	if (check) {
		__asm__ volatile ("stmxcsr %0; fnstcw %1" : "=m" (sse), "=m" (x87));
		REQUIRE((sse & ~0x3fU) == (dirty ? 0x5f80U : 0x1f80U));
		REQUIRE(x87 == (dirty ? 0x0b7fU : 0x037fU));
	} else { __asm__ volatile ("ldmxcsr %0; fldcw %1" :: "m" (sse), "m" (x87)); }
#endif
}

static void signal_handler64(int number)
{
	REQUIRE(number == 32);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RAISE, 0, 0, 0, 0, 0, 0) == -EBUSY);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RETURN, 0, 0, 0, 0, 0, 0) == -EINVAL);
	fp_control64(0, 0);
	signal_count64++;
}

static void fork_probe64(void)
{
	int status = 0;
	pid_t parent = getpid();
	unsigned int saved_tls = initialized;
	char byte;
	int fd = open("libc.txt", O_RDONLY);

	REQUIRE(fd == 3);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RETURN, 0, 0, 0, 0, 0, 0) == -EINVAL);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_SET, (uintptr_t) &private_data64,
		(uintptr_t) signal_restorer64, 0, 0, 0, 0) == -EFAULT);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_SET, (uintptr_t) signal_handler64,
		(uintptr_t) signal_restorer64, 0, 0, 0, 0) == 0);
	fp_control64(1, 0);
	for (int round = 0; round < 8; round++) {
		REQUIRE(lseek(fd, 0, SEEK_SET) == 0);
		errno = EDOM;
		pid_t child = fork();

		REQUIRE(child >= 0);
		fp_control64(1, 1);
		if (child == 0) {
			REQUIRE(getppid() == parent && getpid() != parent);
			REQUIRE(initialized == saved_tls && private_data64 == 71 && errno == EDOM);
			initialized = 0xabcdef; private_data64 = 99;
			REQUIRE(read(fd, &byte, 1) == 1 && byte == 'l');
			REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RAISE, 0, 0, 0, 0, 0, 0) == 0);
			REQUIRE(signal_count64 == 1);
			fp_control64(1, 1);
			if (round % 2) {
				char *args[] = {"libcprobe", "child", NULL};
				char *env[] = {NULL};

				REQUIRE(open("libc.txt", O_RDONLY | O_CLOEXEC) == 4);
				REQUIRE(execve("absent", args, env) == -1 && errno == ENOENT);
				REQUIRE(initialized == 0xabcdef && private_data64 == 99);
				execve("libcprobe", args, env); REQUIRE(0);
			}
			_exit(43);
		}
		REQUIRE(unix64_syscall6(UNIX64_SYS_WAITPID, child, 1, 0, 0, 0, 0) == -EFAULT);
		REQUIRE(waitpid(child, &status, 0) == child);
		REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 43);
		REQUIRE(waitpid(child, &status, WNOHANG) == -1 && errno == ECHILD);
		REQUIRE(initialized == saved_tls && private_data64 == 71 && signal_count64 == 0);
		REQUIRE(lseek(fd, 0, SEEK_CUR) == 1);
		REQUIRE(read(fd, &byte, 1) == 1 && byte == 'i');
	}
	REQUIRE(close(fd) == 0);
	pid_t signaller = fork();

	REQUIRE(signaller >= 0);
	if (signaller == 0) {
		struct UNIX_TIMESPEC64 delay = {0, 100000000};
		int32_t word = 0;

		REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) &word, 0,
			(uintptr_t) &delay, 0, 0, 0) == -ETIMEDOUT);
		REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_SEND, parent, 0, 0, 0, 0, 0) == 0);
		REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) &word, 0,
			(uintptr_t) &delay, 0, 0, 0) == -ETIMEDOUT);
		_exit(44);
	}
	REQUIRE(waitpid(signaller, &status, 0) == -1 && errno == EINTR);
	REQUIRE(signal_count64 == 1);
	fp_control64(1, 1);
	REQUIRE(waitpid(signaller, &status, 0) == signaller && WEXITSTATUS(status) == 44);
	pid_t interruptor = fork();

	REQUIRE(interruptor >= 0);
	if (interruptor == 0) {
		struct UNIX_TIMESPEC64 delay = {0, 100000000};
		int32_t word = 0;

		REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) &word, 0,
			(uintptr_t) &delay, 0, 0, 0) == -ETIMEDOUT);
		REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_SEND, parent, 0, 0, 0, 0, 0) == 0);
		_exit(45);
	}
	/* No syscall in this bounded loop: delivery must cross a timer IRQ. */
	for (uint64_t spin = 0; signal_count64 == 1 && spin < 2000000000ULL; spin++) {
		__asm__ volatile ("" ::: "memory");
	}
	REQUIRE(signal_count64 == 2);
	fp_control64(1, 1);
	REQUIRE(waitpid(interruptor, &status, 0) == interruptor && WEXITSTATUS(status) == 45);
	pid_t faulty = fork();

	REQUIRE(faulty >= 0);
	if (faulty == 0) { __builtin_trap(); }
	REQUIRE(waitpid(faulty, &status, 0) == faulty);
	REQUIRE(WIFSIGNALED(status) && WTERMSIG(status) == 4);
	REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RAISE, 0, 0, 0, 0, 0, 0) == 0);
	REQUIRE(signal_count64 == 3 && initialized == saved_tls);
	fp_control64(1, 1); fp_control64(0, 0);
	printf("libc %d: FORK PASS\n", identifier);
}

int main(int argc, char **argv)
{
	char buffer[32];
	char *allocation;
	int fd, round;
	struct timespec before, after;
	struct UNIX_TIMESPEC64 timeout = {0, 10000000};
	int32_t futex = 7;
	pid_t pid = getpid();

	if (argc == 2 && strcmp(argv[1], "child") == 0) {
		REQUIRE(constructor_ran && initialized == 0x12345678 && zeroed == 0);
		REQUIRE(read(3, buffer, 1) == 1 && buffer[0] == 'i');
		REQUIRE(lseek(3, 1, SEEK_SET) == 1);
		REQUIRE(read(4, buffer, 1) == -1 && errno == EBADF);
		REQUIRE(unix64_syscall6(UNIX64_SYS_SIGNAL_RAISE, 0, 0, 0, 0, 0, 0) == -EINVAL);
		_exit(43);
	}
	REQUIRE(argc == 3 && constructor_ran);
	identifier = atoi(argv[1]); round = atoi(argv[2]);
	REQUIRE(identifier == 1 || identifier == 2);
	if (round < 2) {
		REQUIRE(read(3, buffer, sizeof(buffer)) == 6 && memcmp(buffer, "input\n", 6) == 0);
		REQUIRE(read(4, buffer, sizeof(buffer)) == -1 && errno == EBADF);
		REQUIRE(close(3) == 0);
	}
	REQUIRE(pid > 1 && getppid() == 1);
	REQUIRE(getenv("MOWKOW_LIBC") != NULL);
	REQUIRE(strcmp(getenv("MOWKOW_LIBC"), "static") == 0);
	REQUIRE(getauxval(AT_PAGESZ) == 4096 && getauxval(AT_PHENT) == 56);
	REQUIRE(getauxval(AT_PHNUM) > 0 && getauxval(AT_PHDR) != 0);
	REQUIRE(getauxval(AT_RANDOM) != 0);
	REQUIRE(initialized == 0x12345678 && zeroed == 0);
	initialized = (unsigned int) identifier; zeroed = (unsigned int) pid;
	allocation = calloc(257, 19);
	REQUIRE(allocation != NULL);
	for (int i = 0; i < 257 * 19; i++) { REQUIRE(allocation[i] == 0); }
	memset(allocation, identifier, 257 * 19);
	allocation = realloc(allocation, 16384);
	REQUIRE(allocation != NULL);
	for (int i = 0; i < 257 * 19; i++) { REQUIRE(allocation[i] == identifier); }
	free(allocation);
	allocation = malloc(2 * 1024 * 1024);
	REQUIRE(allocation != NULL);
	memset(allocation, 0x5a, 2 * 1024 * 1024);
	REQUIRE(allocation[0] == 0x5a && allocation[2 * 1024 * 1024 - 1] == 0x5a);
	free(allocation);
	REQUIRE(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
	REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) &futex,
		6, 0, 0, 0, 0) == -EAGAIN);
	REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) &futex,
		7, (uintptr_t) &timeout, 0, 0, 0) == -ETIMEDOUT);
	REQUIRE(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
	REQUIRE((after.tv_sec - before.tv_sec) * 1000000000L +
		after.tv_nsec - before.tv_nsec >= 10000000);
	REQUIRE(unix64_syscall6(UNIX64_SYS_FUTEX_WAKE, (uintptr_t) &futex, 0, 0, 0, 0, 0) == 0);
	errno = 0;
	REQUIRE(open("absent", O_RDONLY) == -1 && errno == ENOENT);
	REQUIRE(write(-1, "x", 1) == -1 && errno == EBADF);
	REQUIRE(mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == ENOSYS);
	fd = open("libc.txt", O_RDONLY | O_CLOEXEC);
	REQUIRE(fd >= 3);
	REQUIRE(read(fd, buffer, sizeof(buffer)) == 11 && memcmp(buffer, "libc-input\n", 11) == 0);
	REQUIRE(read(fd, buffer, sizeof(buffer)) == 0);
	/* FAT duplicates share the offset; descriptor flags stay per FD. */
	REQUIRE(fcntl(fd, F_GETFL) == O_RDONLY);
	REQUIRE(write(fd, "x", 1) == -1 && errno == EBADF);
	REQUIRE(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	int copy = fcntl(fd, F_DUPFD, fd);

	REQUIRE(copy > fd && fcntl(copy, F_GETFD) == 0);
	REQUIRE(lseek(copy, 0, SEEK_CUR) == 11);
	/* Status flags belong to the shared OFD; console NONBLOCK awaits A3. */
	REQUIRE(fcntl(copy, F_SETFL, O_RDWR | O_APPEND | O_NONBLOCK) == 0);
	REQUIRE(fcntl(fd, F_GETFL) == (O_RDONLY | O_APPEND | O_NONBLOCK));
	REQUIRE(fcntl(fd, F_SETFL, 0) == 0 && fcntl(copy, F_GETFL) == O_RDONLY);
	REQUIRE(fcntl(0, F_SETFL, O_NONBLOCK) == -1 && errno == ENOTSUP);
	REQUIRE(fcntl(1, F_GETFL) == O_WRONLY);
	REQUIRE(fcntl(copy, F_SETFD, FD_CLOEXEC) == 0 && fcntl(copy, F_GETFD) == FD_CLOEXEC);
	REQUIRE(dup2(fd, copy) == copy && fcntl(copy, F_GETFD) == 0);
	REQUIRE(close(copy) == 0);
	/* Stdio is an ordinary OFD: dup takes the lowest FD and stdin redirects. */
	int saved = dup(0);

	REQUIRE(saved == copy && isatty(saved));
	REQUIRE(write(saved, "x", 1) == -1 && errno == EBADF);
	REQUIRE(dup2(fd, 0) == 0 && !isatty(0) && errno == ENOTTY);
	REQUIRE(lseek(0, 0, SEEK_SET) == 0 && read(0, buffer, 4) == 4);
	REQUIRE(memcmp(buffer, "libc", 4) == 0 && lseek(fd, 0, SEEK_CUR) == 4);
	REQUIRE(dup2(saved, 0) == 0 && isatty(0) && close(saved) == 0);
	REQUIRE(lseek(fd, 5, SEEK_SET) == 5);
	REQUIRE(read(fd, buffer, sizeof(buffer)) == 6 && memcmp(buffer, "input\n", 6) == 0);
	REQUIRE(close(fd) == 0);
	REQUIRE(read(fd, buffer, 1) == -1 && errno == EBADF);
	REQUIRE(isatty(0) && isatty(1) && isatty(2));
	FILE *stream = fopen("libc.txt", "r");

	REQUIRE(stream != NULL);
	REQUIRE(fgets(buffer, sizeof(buffer), stream) != NULL);
	REQUIRE(strcmp(buffer, "libc-input\n") == 0);
	REQUIRE(fgetc(stream) == EOF && feof(stream) && !ferror(stream));
	REQUIRE(fclose(stream) == 0);
	if (round > 0) {
		char next[8];
		char *args[] = {"libcprobe", argv[1], next, NULL};
		char *env[] = {"MOWKOW_LIBC=static", NULL};

		REQUIRE(snprintf(next, sizeof(next), "%d", round - 1) > 0);
		REQUIRE(open("libc.txt", O_RDONLY) == 3 && lseek(3, 5, SEEK_SET) == 5);
		REQUIRE(open("libc.txt", O_RDONLY | O_CLOEXEC) == 4);
		REQUIRE(execve("absent", args, env) == -1 && errno == ENOENT);
		REQUIRE(execve("libc.txt", args, env) == -1 && errno == ENOEXEC);
		REQUIRE(lseek(3, 0, SEEK_CUR) == 5 && lseek(4, 0, SEEK_CUR) == 0);
		REQUIRE(initialized == (unsigned int) identifier && zeroed == (unsigned int) pid);
		execve("libcprobe", args, env);
		REQUIRE(0);
	}
	fork_probe64();
	REQUIRE(atexit(finished) == 0);
	printf("libc %d: WAIT\n", identifier);
	REQUIRE(fflush(stdout) == 0);
	errno = identifier == 1 ? EDOM : ERANGE;
	/* A blocking kernel read must preserve libc's TLS and thread-local errno. */
	REQUIRE(read(0, buffer, sizeof(buffer)) > 0);
	REQUIRE(errno == (identifier == 1 ? EDOM : ERANGE));
	REQUIRE(initialized == (unsigned int) identifier && zeroed == (unsigned int) pid);
	REQUIRE(puts("libc: STDIO PASS") >= 0);
	printf("libc %d: PASS\n", identifier);
	return 37;
}
