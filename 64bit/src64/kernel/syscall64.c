/*
 * syscall64.c -- 공용 시스템 콜 dispatcher
 *
 * x86은 int 0x80 프레임을 사용한다. AArch64의 SVC 경계도 x8과 x0-x5를
 * 같은 프레임으로 변환한다. syscall 번호·레지스터 규약·음수 오류는 유지한다.
 *
 * User buffers are preflighted directionally and copied through supervisor
 * aliases. Filesystem/console operations receive only kernel-owned snapshots.
 */
#include <console64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <fd64.h>
#include <interrupt64.h>
#include <kstring64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall64.h>
#include <timer64.h>
#include <utf864.h>
#include <user_access64.h>

/* Filenames reject malformed UTF; display callers may use the replacement. */
static int validate_filename_utf864(const char *name, size_t length)
{
	size_t pos = 0;
	size_t consumed;
	uint32_t unicode;

	while (pos < length) {
		if (utf8_decode64(name + pos, length - pos, &unicode, &consumed) != 0) {
			return -22;
		}
		pos += consumed;
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
	void *snapshot;

	if (size > INT64_MAX ||
		!process64_user_range_valid(buffer, (size_t) size)) { return -1; }
	fh = syscall_file64(process, fd);
	if (fh == NULL && !((fd == 1 || fd == 2) && process->console != NULL)) { return -1; }
	snapshot = process64_io_buffer((size_t) size);
	if (snapshot == NULL || copy_from_user64(snapshot, buffer, (size_t) size) != 0) {
		return -1;
	}
	if (fd == 1 || fd == 2) {
		console64_write_con(process->console, snapshot, size);
		return (int64_t) size;
	}
	return (int64_t) fd64_write(fh, snapshot, (size_t) size);
}

/* Preserve one backend call, partial counts and negative errors. In particular,
 * do not split cooked input, UTF display runs or checked reads into chunks.
 * Failed checked reads publish no bytes; their file position may have advanced.
 */
static int64_t syscall_read64(struct PROCESS64 *process, uint64_t fd,
	void *buffer, uint64_t size, int checked)
{
	struct FDHANDLE64 *fh = syscall_file64(process, fd);
	void *snapshot;
	int64_t result;

	if (checked && fh == NULL) { return -9; }
	if (size > INT64_MAX) { return checked ? -22 : -1; }
	if (!process64_user_write_valid(buffer, (size_t) size)) { return checked ? -14 : -1; }
	if (fh == NULL && !(fd == 0 && process->console != NULL)) { return -1; }
	snapshot = process64_io_buffer((size_t) size);
	if (snapshot == NULL) { return checked ? -12 : -1; }
	if (checked) { result = fd64_read_checked(fh, snapshot, (size_t) size); }
	else if (fd == 0) { result = (int64_t) console64_read_con(process->console, snapshot, size); }
	else { result = (int64_t) fd64_read(fh, snapshot, (size_t) size); }
	if (result < 0) { return result; }
	if ((uint64_t) result > size) { return -5; }
	if (copy_to_user64(buffer, snapshot, (size_t) result) != 0) { return checked ? -14 : -1; }
	return result;
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

static int64_t syscall_file_call64(struct PROCESS64 *process,
	uint64_t operation, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
	struct FILE_INFO64 info;
	struct FDHANDLE64 *fh;
	char name[FD64_NAME_MAX];
	int64_t length;
	int status;

	if (operation == FILE_STAT64 || operation == FILE_OPEN64) {
		if (arg2 != 0) { return -22; }
		if (operation == FILE_STAT64 &&
			!process64_user_write_valid((void *) arg1, sizeof(info))) { return -14; }
		if (operation == FILE_OPEN64 && (arg1 & ~7UL)) { return -22; }
		length = copy_string_from_user64(name, sizeof(name), (const char *) arg0);
		if (length < 0) { return length; }
		if (length == 0 || validate_filename_utf864(name, (size_t) length) != 0) {
			return -22;
		}
		if (operation == FILE_STAT64) {
			status = fd64_stat_checked(NULL, name, &info);
			if (status == 0) { status = copy_to_user64((void *) arg1, &info, sizeof(info)); }
			return status;
		}
		for (size_t i = 3; i < PROCESS64_MAX_FILES; i++) {
			if (!process->files[i].used) {
				status = fd64_open_checked(&process->files[i].fh, name,
					(unsigned int) arg1);
				if (status != 0) { return status; }
				process->files[i].used = 1;
				return (int64_t) i;
			}
		}
		return -24;
	}
	if (operation != FILE_READ64 && operation != FILE_SYNC64 &&
		operation != FILE_FSTAT64) { return -38; }
	fh = syscall_file64(process, arg0);
	if (fh == NULL) { return -9; }
	if (operation == FILE_READ64) {
		return syscall_read64(process, arg0, (void *) arg1, arg2, 1);
	}
	if (arg2 != 0) { return -22; }
	if (operation == FILE_SYNC64) {
		if (arg1 != 0) { return -22; }
		return fd64_sync() < 0 ? -5 : 0;
	}
	if (!process64_user_write_valid((void *) arg1, sizeof(info))) { return -14; }
	status = fd64_stat_checked(fh, NULL, &info);
	if (status == 0) { status = copy_to_user64((void *) arg1, &info, sizeof(info)); }
	return status;
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
		if (frame->rdi == TTY_CURSOR || frame->rdi == TTY_INPUT_MODE) {
			return (uint64_t) -22;
		}
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
	case TTY_CURSOR:
		if (row > 1 || col < 1 || col > 2) { return (uint64_t) -22; }
		return (uint64_t) console64_cursor_con(process->console, (int) row,
			(unsigned int) col);
	case TTY_INPUT_MODE:
		return (uint64_t) console64_input_mode_con(process->console);
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
		frame->rax = (uint64_t) syscall_read64(process, a0, (void *) a1, a2, 0);
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
	case SYS_FILE:
		frame->rax = (uint64_t) syscall_file_call64(process, a0, a1, a2, frame->r10);
		break;
	case SYS_TTY:
		frame->rax = syscall_tty64(process, frame);
		break;
	default:
		frame->rax = (uint64_t) -1;
	}
	return 0;
}
