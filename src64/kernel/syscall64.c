#include <console64.h>
#include <fd64.h>
#include <interrupt64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall64.h>
#include <timer64.h>

static size_t strn_len_user(const char *s, size_t max)
{
	size_t i;

	for (i = 0; i < max; i++) {
		if (process64_user_range_valid(s + i, 1) == 0) {
			return max;
		}
		if (s[i] == '\0') {
			return i;
		}
	}
	return max;
}

static int syscall_open(struct PROCESS64 *process, const char *path, int flags)
{
	struct FDHANDLE64 *fh;
	size_t i;

	if (process64_user_range_valid(path, 1) == 0 || strn_len_user(path, 64) >= 64) {
		return -1;
	}
	for (i = 3; i < PROCESS64_MAX_FILES; i++) {
		if (process->files[i].used == 0) {
			fh = &process->files[i].fh;
			if (fd64_open(fh, path) == 0) {
				if ((flags & O_CREAT) == 0 || fd64_create(fh, path) == 0) {
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

uint64_t syscall_handler64(struct INTERRUPT_FRAME64 *frame)
{
	struct PROCESS64 *process;
	uint64_t nr;
	uint64_t a0;
	uint64_t a1;
	uint64_t a2;

	process = process64_current();
	if (process == NULL) {
		frame->rax = (uint64_t) -1;
		return 0;
	}
	nr = frame->rax;
	a0 = frame->rdi;
	a1 = frame->rsi;
	a2 = frame->rdx;
	if (nr == SYS_EXIT) {
		process64_exit_current((int) a0);
		return 1;
	}
	if (nr == SYS_WRITE) {
		if (process64_user_range_valid((const void *) a1, (size_t) a2) == 0) {
			frame->rax = (uint64_t) -1;
		} else if (a0 == 1 || a0 == 2) {
			console64_write((const char *) a1, a2);
			frame->rax = a2;
		} else if (a0 >= 3 && a0 < PROCESS64_MAX_FILES && process->files[a0].used != 0) {
			frame->rax = fd64_write(&process->files[a0].fh, (const void *) a1, (size_t) a2);
		} else {
			frame->rax = (uint64_t) -1;
		}
		return 0;
	}
	if (nr == SYS_OPEN) {
		frame->rax = (uint64_t) syscall_open(process, (const char *) a0, (int) a1);
		return 0;
	}
	if (nr == SYS_READ) {
		if (a0 == 0 && process64_user_range_valid((void *) a1, (size_t) a2) != 0) {
			frame->rax = console64_read((char *) a1, a2);
		} else if (a0 < PROCESS64_MAX_FILES && process->files[a0].used != 0 &&
				process64_user_range_valid((void *) a1, (size_t) a2) != 0) {
			frame->rax = fd64_read(&process->files[a0].fh, (void *) a1, (size_t) a2);
		} else {
			frame->rax = (uint64_t) -1;
		}
		return 0;
	}
	if (nr == SYS_SEEK) {
		if (a0 < PROCESS64_MAX_FILES && process->files[a0].used != 0) {
			frame->rax = fd64_seek(&process->files[a0].fh, (int64_t) a1, (int) a2);
		} else {
			frame->rax = (uint64_t) -1;
		}
		return 0;
	}
	if (nr == SYS_CLOSE) {
		if (a0 < PROCESS64_MAX_FILES && process->files[a0].used != 0) {
			process->files[a0].used = 0;
			frame->rax = 0;
		} else {
			frame->rax = (uint64_t) -1;
		}
		return 0;
	}
	if (nr == SYS_ALLOC) {
		uintptr_t p;
		size_t size;

		size = (size_t) ((a0 + 15) & ~(uint64_t) 15);
		p = process->heap_next;
		if (size == 0 || p + size < p || p + size > process->heap.base + process->heap.size) {
			frame->rax = 0;
		} else {
			process->heap_next += size;
			frame->rax = p;
		}
		return 0;
	}
	if (nr == SYS_FREE) {
		if (process64_user_range_valid((void *) a0, (size_t) a1) != 0 &&
				a0 >= process->heap.base &&
				a0 + a1 >= a0 &&
				a0 + a1 <= process->heap.base + process->heap.size) {
			frame->rax = 0;
		} else {
			frame->rax = (uint64_t) -1;
		}
		return 0;
	}
	if (nr == SYS_TICKS) {
		frame->rax = timerctl64.count;
		return 0;
	}
	frame->rax = (uint64_t) -1;
	return 0;
}
