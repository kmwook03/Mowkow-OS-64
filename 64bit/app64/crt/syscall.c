/* App wrappers preserve signed syscall errors and partial I/O counts. */
#include <mowos.h>
#include <syscall.h>

long file_call64(unsigned int operation, unsigned long arg0,
	unsigned long arg1, unsigned long arg2)
{
	return syscall4(SYS_FILE, operation, (long) arg0, (long) arg1, (long) arg2);
}

long write(int fd, const void *buf, size_t len)
{
	return syscall3(SYS_WRITE, fd, (long) buf, (long) len);
}

long read(int fd, void *buf, size_t len)
{
	return syscall3(SYS_READ, fd, (long) buf, (long) len);
}

int open(const char *path, int flags)
{
	return (int) syscall2(SYS_OPEN, (long) path, flags);
}

int close(int fd)
{
	return (int) syscall1(SYS_CLOSE, fd);
}

long seek(int fd, long offset, int whence)
{
	return syscall3(SYS_SEEK, fd, offset, whence);
}

void exit(int status)
{
	syscall1(SYS_EXIT, status);
	for (;;) {
	}
}

void *alloc(size_t size)
{
	return (void *) syscall1(SYS_ALLOC, (long) size);
}

void free_alloc(void *ptr, size_t size)
{
	syscall2(SYS_FREE, (long) ptr, (long) size);
}

unsigned long ticks(void)
{
	return (unsigned long) syscall0(SYS_TICKS);
}

void tty_raw(int on)
{
	syscall2(SYS_TTY, TTY_MODE, on);
}

unsigned long tty_readkey(void)
{
	return (unsigned long) syscall1(SYS_TTY, TTY_READKEY);
}

unsigned long tty_size(void)
{
	return (unsigned long) syscall1(SYS_TTY, TTY_SIZE);
}

void tty_move(int row, int col)
{
	syscall3(SYS_TTY, TTY_MOVE, row, col);
}

void tty_clear(int row, int col, int rows, int cols)
{
	syscall5(SYS_TTY, TTY_CLEAR, row, col, rows, cols);
}

void tty_attr(int fg, int bg)
{
	syscall3(SYS_TTY, TTY_ATTR, fg, bg);
}

void tty_flush(void)
{
	syscall1(SYS_TTY, TTY_FLUSH);
}

long tty_cursor(int visible, int cells)
{
	return syscall3(SYS_TTY, TTY_CURSOR, visible, cells);
}

long tty_input_mode(void)
{
	return syscall1(SYS_TTY, TTY_INPUT_MODE);
}

/* A zero/oversized count cannot advance safely, even after a partial write. */
static int write_all64(const char *s, size_t len)
{
	while (len != 0) {
		long count = write(1, s, len);

		if (count < 0) {
			return count < (-__INT_MAX__ - 1L) ? -75 : (int) count;
		}
		if (count == 0 || (size_t) count > len) {
			return -5;
		}
		s += (size_t) count;
		len -= (size_t) count;
	}
	return 0;
}

int puts(const char *s)
{
	size_t len;
	int result;

	if (s == NULL) {
		return -22;
	}
	len = strlen(s);
	/* Preserve the historical int byte count, including the final newline. */
	if (len >= (size_t) __INT_MAX__) {
		return -75;
	}
	result = write_all64(s, len);
	if (result < 0) {
		return result;
	}
	result = write_all64("\n", 1);
	if (result < 0) {
		return result;
	}
	return (int) len + 1;
}
