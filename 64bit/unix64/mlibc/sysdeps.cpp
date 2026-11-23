/* mlibc 6.3.1 adapter. No Linux gate and no legacy CRT dependencies. */
#include <errno.h>
#include <mlibc/all-sysdeps.hpp>
#include <mowkow/syscall64.h>
#include <stdint.h>
#include <string.h>

namespace {
int64_t call(uint64_t number, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0)
{
	return unix64_syscall6(number, a, b, c, 0, 0, 0);
}

int rounded_size(size_t size, size_t *rounded)
{
	if (!size || size > SIZE_MAX - (UNIX64_PAGE_SIZE - 1)) { return EINVAL; }
	*rounded = (size + UNIX64_PAGE_SIZE - 1) & ~(size_t) (UNIX64_PAGE_SIZE - 1);
	return 0;
}
}

namespace mlibc {
void sys_libc_log(const char *message)
{
	size_t length = strlen(message);
	(void) call(UNIX64_SYS_WRITE, 2, (uintptr_t) message, length);
	(void) call(UNIX64_SYS_WRITE, 2, (uintptr_t) "\n", 1);
}

[[noreturn]] void sys_exit(int status)
{
	(void) call(UNIX64_SYS_EXIT, status);
	__builtin_trap();
}

[[noreturn]] void sys_libc_panic() { sys_exit(127); }

int sys_tcb_set(void *pointer)
{
	return unix64_result_errno(call(UNIX64_SYS_TCB_SET, (uintptr_t) pointer));
}

/* Process-local identity: exactly one thread and private futexes until B1. */
int sys_futex_tid() { return 1; }

int sys_futex_wait(int *pointer, int expected, const struct timespec *timeout)
{
	struct UNIX_TIMESPEC64 time;
	if (timeout) { time = {timeout->tv_sec, timeout->tv_nsec}; }
	return unix64_result_errno(call(UNIX64_SYS_FUTEX_WAIT, (uintptr_t) pointer,
		(uint32_t) expected, timeout ? (uintptr_t) &time : 0));
}

int sys_futex_wake(int *pointer)
{
	return unix64_result_errno(call(UNIX64_SYS_FUTEX_WAKE, (uintptr_t) pointer));
}

int sys_anon_allocate(size_t size, void **pointer)
{
	size_t rounded;
	int error = rounded_size(size, &rounded);
	if (error) { return error; }
	int64_t result = call(UNIX64_SYS_ANON_ALLOC, rounded);
	error = unix64_result_errno(result);
	if (!error) { *pointer = (void *) result; }
	return error;
}

int sys_anon_free(void *pointer, size_t size)
{
	size_t rounded;
	int error = rounded_size(size, &rounded);
	return error ? error : unix64_result_errno(call(UNIX64_SYS_ANON_FREE,
		(uintptr_t) pointer, rounded));
}

int sys_vm_map(void *, size_t, int, int, int, off_t, void **) { return ENOSYS; }
int sys_vm_unmap(void *, size_t) { return ENOSYS; }
int sys_clock_get(int clock, time_t *seconds, long *nanoseconds)
{
	if (clock != CLOCK_MONOTONIC) { return ENOTSUP; }
	struct UNIX_TIMESPEC64 time;
	int error = unix64_result_errno(call(UNIX64_SYS_CLOCK_GET, (uintptr_t) &time));
	if (!error) { *seconds = time.seconds; *nanoseconds = time.nanoseconds; }
	return error;
}

/* 6.3.1's PthreadSignalInstaller explicitly accepts ENOSYS to opt out of
 * cancellation. Do not accept a handler without a delivery/return protocol.
 */
int sys_sigaction(int, const struct sigaction *, struct sigaction *) { return ENOSYS; }

int sys_open(const char *path, int flags, mode_t, int *fd)
{
	if (flags & ~(O_CLOEXEC | O_ACCMODE)) { return ENOTSUP; }
	if ((flags & O_ACCMODE) != O_RDONLY) { return EROFS; }
	int64_t result = call(UNIX64_SYS_OPEN, (uintptr_t) path,
		(flags & O_CLOEXEC) ? UNIX64_OPEN_CLOEXEC : 0);
	int error = unix64_result_errno(result);
	if (!error) { *fd = (int) result; }
	return error;
}

int sys_close(int fd) { return unix64_result_errno(call(UNIX64_SYS_CLOSE, fd)); }
int sys_isatty(int fd) { return unix64_result_errno(call(UNIX64_SYS_ISATTY, fd)); }

int sys_read(int fd, void *buffer, size_t size, ssize_t *count)
{
	int64_t result = call(UNIX64_SYS_READ, fd, (uintptr_t) buffer, size);
	int error = unix64_result_errno(result);
	if (!error) { *count = result; }
	return error;
}

int sys_write(int fd, const void *buffer, size_t size, ssize_t *count)
{
	int64_t result = call(UNIX64_SYS_WRITE, fd, (uintptr_t) buffer, size);
	int error = unix64_result_errno(result);
	if (!error) { *count = result; }
	return error;
}

int sys_seek(int fd, off_t offset, int whence, off_t *position)
{
	int64_t result = call(UNIX64_SYS_SEEK, fd, offset, whence);
	int error = unix64_result_errno(result);
	if (!error) { *position = result; }
	return error;
}

int sys_fork(pid_t *child)
{
	int64_t result = call(UNIX64_SYS_FORK);
	int error = unix64_result_errno(result);
	if (!error) { *child = (pid_t) result; }
	return error;
}

int sys_waitpid(pid_t pid, int *status, int flags, struct rusage *usage, pid_t *child)
{
	if (usage) { return ENOTSUP; }
	if (flags & ~WNOHANG) { return EINVAL; }
	int64_t result = call(UNIX64_SYS_WAITPID, (int64_t) pid, (uintptr_t) status,
		(flags & WNOHANG) ? UNIX64_WAIT_NOHANG : 0);
	int error = unix64_result_errno(result);
	if (!error) { *child = (pid_t) result; }
	return error;
}

pid_t sys_getpid() { return (pid_t) call(UNIX64_SYS_GETPID); }
pid_t sys_getppid() { return (pid_t) call(UNIX64_SYS_GETPPID); }

int sys_execve(const char *path, char *const argv[], char *const envp[])
{
	return unix64_result_errno(call(UNIX64_SYS_EXECVE,
		(uintptr_t) path, (uintptr_t) argv, (uintptr_t) envp));
}
}
