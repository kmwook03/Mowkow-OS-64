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

/* FAT has no permission bits, so mode is ignored. O_EXCL and other flags are
 * unsupported until the VFS; O_RDONLY with O_CREAT/O_TRUNC is EINVAL.
 */
int sys_open(const char *path, int flags, mode_t, int *fd)
{
	if (flags & ~(O_CLOEXEC | O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND)) { return ENOTSUP; }
	int access = flags & O_ACCMODE;
	if (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) { return EINVAL; }
	uint64_t wire = (access == O_WRONLY ? UNIX64_OPEN_WRONLY : 0) |
		(access == O_RDWR ? UNIX64_OPEN_RDWR : 0) |
		((flags & O_CLOEXEC) ? UNIX64_OPEN_CLOEXEC : 0) |
		((flags & O_CREAT) ? UNIX64_OPEN_CREAT : 0) |
		((flags & O_TRUNC) ? UNIX64_OPEN_TRUNC : 0) |
		((flags & O_APPEND) ? UNIX64_OPEN_APPEND : 0);
	int64_t result = call(UNIX64_SYS_OPEN, (uintptr_t) path, wire);
	int error = unix64_result_errno(result);
	if (!error) { *fd = (int) result; }
	return error;
}

/* dup3 flags are unsupported; the Linux option that passes them is disabled. */
int sys_dup(int fd, int flags, int *newfd)
{
	if (flags & ~O_CLOEXEC) { return EINVAL; }
	int64_t result = call(UNIX64_SYS_FCNTL, fd,
		(flags & O_CLOEXEC) ? UNIX64_F_DUPFD_CLOEXEC : UNIX64_F_DUPFD, 0);
	int error = unix64_result_errno(result);
	if (!error) { *newfd = (int) result; }
	return error;
}

int sys_dup2(int fd, int flags, int newfd)
{
	if (flags) { return EINVAL; }
	return unix64_result_errno(call(UNIX64_SYS_DUP2, fd, newfd));
}

/* Translate libc F_* and FD_CLOEXEC explicitly; the wire is not a libc ABI. */
int sys_fcntl(int fd, int request, va_list args, int *result)
{
	uint64_t command;
	uint64_t argument = 0;

	switch (request) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC: {
		int minimum = va_arg(args, int);
		if (minimum < 0) { return EINVAL; }
		command = request == F_DUPFD ? UNIX64_F_DUPFD : UNIX64_F_DUPFD_CLOEXEC;
		argument = (uint64_t) minimum;
		break;
	}
	case F_GETFD: command = UNIX64_F_GETFD; break;
	case F_GETFL: command = UNIX64_F_GETFL; break;
	case F_SETFL: {
		int flags = va_arg(args, int);
		if (flags & ~(O_ACCMODE | O_APPEND | O_NONBLOCK)) { return EINVAL; }
		command = UNIX64_F_SETFL;
		argument = ((flags & O_APPEND) ? UNIX64_FL_APPEND : 0) |
			((flags & O_NONBLOCK) ? UNIX64_FL_NONBLOCK : 0);
		break;
	}
	case F_SETFD: {
		int flags = va_arg(args, int);
		if (flags & ~FD_CLOEXEC) { return EINVAL; }
		command = UNIX64_F_SETFD;
		argument = (flags & FD_CLOEXEC) ? UNIX64_FD_CLOEXEC : 0;
		break;
	}
	default: return EINVAL;
	}
	int64_t value = call(UNIX64_SYS_FCNTL, fd, command, argument);
	int error = unix64_result_errno(value);
	if (error) { return error; }
	if (request == F_GETFD) {
		*result = (value & UNIX64_FD_CLOEXEC) ? FD_CLOEXEC : 0;
	} else if (request == F_GETFL) {
		uint64_t access = value & UNIX64_FL_ACCMODE;
		*result = (access == UNIX64_FL_WRONLY ? O_WRONLY :
			access == UNIX64_FL_RDWR ? O_RDWR : O_RDONLY) |
			((value & UNIX64_FL_APPEND) ? O_APPEND : 0) |
			((value & UNIX64_FL_NONBLOCK) ? O_NONBLOCK : 0);
	} else {
		*result = (int) value;
	}
	return 0;
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
