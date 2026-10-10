/*
 * syscall64.c -- 공용 시스템 콜 dispatcher
 *
 * x86은 int 0x80 프레임을 사용한다. AArch64의 SVC 경계도 x8과 x0-x5를
 * 같은 프레임으로 변환한다. syscall 번호·레지스터 규약·음수 오류는 유지한다.
 *
 * 유저가 준 포인터는 반드시 process64_user_range_valid로 확인한 뒤에 쓴다.
 * 아직 페이지 단위 보호가 없어서 이 검사가 유일한 방어선이다.
 */
#include <console64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <fd64.h>
#include <interrupt64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall64.h>
#include <timer64.h>

/* Return the copied length excluding NUL, or a negative errno. Never reread a
   user byte after copying it; parsing must use only the kernel snapshot. */
static int64_t copy_string_from_user64(char *dst, size_t capacity, const char *src)
{
	uintptr_t base = (uintptr_t) src;
	size_t i;
	char byte;
	int error = -36;

	if (dst == NULL || capacity == 0 || capacity > INT64_MAX) {
		return -22;
	}
	dst[0] = '\0';
	if (src == NULL) {
		return -14;
	}
	for (i = 0; i < capacity; i++) {
		/* Check the growing prefix before address arithmetic or dereference. */
		if (i >= UINTPTR_MAX - base ||
			process64_user_range_valid(src, i + 1) == 0) {
			error = -14;
			break;
		}
		byte = *(const char *) (base + i);
		dst[i] = byte;
		if (byte == '\0') {
			return (int64_t) i;
		}
	}
	dst[0] = '\0';
	return error;
}

/* Validate the bounded snapshot before passing it to the legacy UTF-16
   converter, which assumes every multibyte sequence is complete. */
static int validate_filename_utf864(const char *name, size_t length)
{
	const uint8_t *bytes = (const uint8_t *) name;
	size_t pos = 0;
	size_t width;
	size_t i;
	uint8_t lead;

	while (pos < length) {
		lead = bytes[pos];
		if (lead < 0x80) {
			pos++;
			continue;
		}
		if (lead >= 0xc2 && lead <= 0xdf) {
			width = 2;
		} else if (lead >= 0xe0 && lead <= 0xef) {
			width = 3;
		} else if (lead >= 0xf0 && lead <= 0xf4) {
			width = 4;
		} else {
			return -22;
		}
		if (width > length - pos) {
			return -22;
		}
		for (i = 1; i < width; i++) {
			if ((bytes[pos + i] & 0xc0) != 0x80) {
				return -22;
			}
		}
		/* Reject overlong forms, surrogates, and values above U+10FFFF. */
		if ((lead == 0xe0 && bytes[pos + 1] < 0xa0) ||
			(lead == 0xed && bytes[pos + 1] >= 0xa0) ||
			(lead == 0xf0 && bytes[pos + 1] < 0x90) ||
			(lead == 0xf4 && bytes[pos + 1] >= 0x90)) {
			return -22;
		}
		pos += width;
	}
	return 0;
}

static struct FDHANDLE64 *syscall_file64(struct PROCESS64 *process, uint64_t fd)
{
	if (fd < 3 || fd >= PROCESS64_MAX_FILES || process->files[fd].used == 0) {
		return NULL;
	}
	return &process->files[fd].fh;
}

static int syscall_open64(struct PROCESS64 *process, const char *path, uint64_t flags)
{
	struct FDHANDLE64 *fh;
	char name[FD64_NAME_MAX];
	int64_t length;
	size_t i;

	/* Check the full register before any narrowing or filesystem mutation. */
	if ((flags & ~(uint64_t) (O_CREAT | O_TRUNC)) != 0) {
		return -1;
	}
	length = copy_string_from_user64(name, sizeof(name), path);
	if (length < 0 || validate_filename_utf864(name, (size_t) length) != 0) {
		return -1;
	}
	for (i = 3; i < PROCESS64_MAX_FILES; i++) {
		if (process->files[i].used == 0) {
			fh = &process->files[i].fh;
			if (fd64_open(fh, name) == 0) {
				if ((flags & O_CREAT) == 0 || fd64_create(fh, name) == 0) {
					return -2;
				}
			} else if ((flags & O_TRUNC) != 0 && fd64_truncate(fh, 0) != 0) {
				return -2;
			}
			process->files[i].used = 1;
			return (int) i;
		}
	}
	return -3;
}

static int64_t syscall_write64(struct PROCESS64 *process, uint64_t fd,
	const void *buffer, uint64_t size)
{
	struct FDHANDLE64 *fh;

	if (size > INT64_MAX ||
		process64_user_range_valid(buffer, (size_t) size) == 0) {
		return -1;
	}
	if ((fd == 1 || fd == 2) && process->console != NULL) {
		console64_write_con(process->console, buffer, size);
		return (int64_t) size;
	}
	fh = syscall_file64(process, fd);
	return fh != NULL ? (int64_t) fd64_write(fh, buffer, (size_t) size) : -1;
}

static int64_t syscall_read64(struct PROCESS64 *process, uint64_t fd,
	void *buffer, uint64_t size)
{
	struct FDHANDLE64 *fh;

	if (size > INT64_MAX ||
		process64_user_range_valid(buffer, (size_t) size) == 0) {
		return -1;
	}
	if (fd == 0 && process->console != NULL) {
		return (int64_t) console64_read_con(process->console, buffer, size);
	}
	fh = syscall_file64(process, fd);
	return fh != NULL ? (int64_t) fd64_read(fh, buffer, (size_t) size) : -1;
}

static int syscall_seek64(struct PROCESS64 *process, uint64_t fd,
	int64_t offset, uint64_t whence)
{
	struct FDHANDLE64 *fh = syscall_file64(process, fd);

	if (fh == NULL || whence > 2) {
		return -1;
	}
	return fd64_seek(fh, offset, (int) whence);
}

static int syscall_close64(struct PROCESS64 *process, uint64_t fd)
{
	if (syscall_file64(process, fd) == NULL) {
		return -1;
	}
	process->files[fd].used = 0;
	return 0;
}

static uintptr_t syscall_alloc64(struct PROCESS64 *process, uint64_t request)
{
	uintptr_t p = process->heap_next;
	size_t size;

	if (request > UINT64_MAX - 15) {
		return 0;
	}
	size = (size_t) ((request + 15) & ~(uint64_t) 15);
	if (size == 0 || process->heap.size > UINTPTR_MAX - process->heap.base ||
		p < process->heap.base || p - process->heap.base > process->heap.size ||
		size > process->heap.size - (p - process->heap.base)) {
		return 0;
	}
	process->heap_next = p + size;
	return p;
}

static int syscall_free64(struct PROCESS64 *process, uintptr_t p, size_t size)
{
	if (process64_user_range_valid((void *) p, size) == 0 ||
		process->heap.size > UINTPTR_MAX - process->heap.base ||
		p < process->heap.base || p - process->heap.base > process->heap.size ||
		size > process->heap.size - (p - process->heap.base)) {
		return -1;
	}
	/* The bump allocator retains storage until process exit, as before. */
	return 0;
}

static uint64_t syscall_ticks64(void)
{
#ifdef __aarch64__
	return arch64_timer_ticks();
#else
	return timerctl64.count;
#endif
}

static uint64_t syscall_tty64(struct PROCESS64 *process,
	const struct INTERRUPT_FRAME64 *frame)
{
	uint64_t row = frame->rsi;
	uint64_t col = frame->rdx;
	uint64_t size;
	uint64_t rows;
	uint64_t cols;

	if (process->console == NULL) {
		return (uint64_t) -1;
	}
	switch (frame->rdi) {
	case TTY_MODE:
		if (row > 1) {
			return (uint64_t) -1;
		}
		console64_set_raw((int) row);
		break;
	case TTY_READKEY:
		return console64_is_raw() != 0 ? console64_read_key() : (uint64_t) -1;
	case TTY_SIZE:
		return console64_size();
	case TTY_MOVE:
	case TTY_CLEAR:
		size = console64_size();
		rows = TTY_SIZE_ROWS(size);
		cols = TTY_SIZE_COLS(size);
		if (row >= rows || col >= cols) {
			return (uint64_t) -1;
		}
		if (frame->rdi == TTY_MOVE) {
			console64_move((uint32_t) row, (uint32_t) col);
			break;
		}
		/* Subtraction validates endpoints without row+rows/col+cols overflow. */
		if (frame->r10 > rows - row || frame->r8 > cols - col) {
			return (uint64_t) -1;
		}
		if (frame->r10 != 0 && frame->r8 != 0) {
			console64_clear_cells((uint32_t) row, (uint32_t) col,
				(uint32_t) frame->r10, (uint32_t) frame->r8);
		}
		break;
	case TTY_ATTR:
		if (row > UINT8_MAX || col > UINT8_MAX) {
			return (uint64_t) -1;
		}
		console64_set_attr((uint8_t) row, (uint8_t) col);
		break;
	case TTY_FLUSH:
		console64_flush();
		break;
	default:
		return (uint64_t) -1;
	}
	return 0;
}

uint64_t syscall_handler64(struct INTERRUPT_FRAME64 *frame)
{
	struct PROCESS64 *process;
	uint64_t a0;
	uint64_t a1;
	uint64_t a2;

	if (frame == NULL) {
		return 0;
	}
	process = process64_current();
	if (process == NULL) {
		frame->rax = (uint64_t) -1;
		return 0;
	}
	a0 = frame->rdi;
	a1 = frame->rsi;
	a2 = frame->rdx;
	switch (frame->rax) {
	case SYS_EXIT:
		process64_exit_current((int) a0);
		return 1;
	case SYS_WRITE:
		frame->rax = (uint64_t) syscall_write64(process, a0, (const void *) a1, a2);
		break;
	case SYS_READ:
		frame->rax = (uint64_t) syscall_read64(process, a0, (void *) a1, a2);
		break;
	case SYS_OPEN:
		frame->rax = (uint64_t) syscall_open64(process, (const char *) a0, a1);
		break;
	case SYS_CLOSE:
		frame->rax = (uint64_t) syscall_close64(process, a0);
		break;
	case SYS_SEEK:
		frame->rax = (uint64_t) syscall_seek64(process, a0, (int64_t) a1, a2);
		break;
	case SYS_ALLOC:
		frame->rax = syscall_alloc64(process, a0);
		break;
	case SYS_FREE:
		frame->rax = (uint64_t) syscall_free64(process, (uintptr_t) a0, (size_t) a1);
		break;
	case SYS_TICKS:
		frame->rax = syscall_ticks64();
		break;
	case SYS_TTY:
		frame->rax = syscall_tty64(process, frame);
		break;
	default:
		frame->rax = (uint64_t) -1;
	}
	return 0;
}
