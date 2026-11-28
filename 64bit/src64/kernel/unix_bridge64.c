/* Temporary v1 read-only FAT and libc-arena boundary until VFS/VM. */
#include <kstring64.h>
#include <process64.h>
#include <process_file64.h>
#include <syscall64.h>
#include <uapi/unix64.h>
#include <unix_wait64.h>
#include <user_access64.h>

_Static_assert(UNIX_HEAP_SIZE64 / USER_PAGE_SIZE64 < UINT16_MAX,
	"arena allocation lengths must not alias tail markers");

static int valid_fd64(const struct PROCESS64 *process, uint64_t fd)
{
	return fd < PROCESS64_MAX_FILES && process->files[fd].used;
}

static int64_t unix_io64(struct PROCESS64 *process, uint64_t fd,
	void *buffer, uint64_t size, int writing)
{
	struct USER_ACCESS64 access = {0};
	struct PROCESS64_FILE *file;
	void *snapshot;
	int64_t result;

	if (!valid_fd64(process, fd)) { return -9; }
	file = &process->files[fd];
	result = process_file_check64(file, writing);
	if (result != 0) { return result; }
	/* FAT's legacy unsigned write result cannot express a reliable errno. */
	if (writing && !process_file_tty64(file)) { return -30; }
	if (size > INT64_MAX) { return -22; }
	if (size == 0) { return 0; }
	if (writing ? !process64_user_range_valid(buffer, size) :
		!process64_user_write_valid(buffer, size)) { return -14; }
	snapshot = process64_io_buffer(size);
	if (snapshot == NULL) { return -12; }
	if (writing) {
		if (copy_from_user64(snapshot, buffer, size) != 0) { return -14; }
		return process_file_write64(file, snapshot, size);
	}
	result = user_access_begin64(&access, buffer, size, USER_WRITE64);
	if (result != 0) { return result; }
	result = process_file_read64(file, snapshot, size);
	if (result >= 0) {
		if ((uint64_t) result > size) { result = -5; }
		else if (copy_to_user_access64(&access, snapshot, result) != 0) { result = -14; }
	}
	user_access_end64(&access);
	return result;
}

/* Translate internal PROCESS_FILE_* bits to the wire status word. */
static int64_t unix_getfl64(struct PROCESS64 *process, int fd)
{
	unsigned int flags = 0;
	int status = process_file_get_status64(process, fd, &flags);
	unsigned int access = flags & (PROCESS_FILE_READ64 | PROCESS_FILE_WRITE64);

	if (status != 0) { return status; }
	return (access == PROCESS_FILE_WRITE64 ? UNIX64_FL_WRONLY :
		access == PROCESS_FILE_READ64 ? UNIX64_FL_RDONLY : UNIX64_FL_RDWR) |
		(flags & PROCESS_FILE_APPEND64 ? UNIX64_FL_APPEND : 0) |
		(flags & PROCESS_FILE_NONBLOCK64 ? UNIX64_FL_NONBLOCK : 0);
}

static int64_t unix_setfl64(struct PROCESS64 *process, int fd, uint64_t wire)
{
	if (wire & ~(uint64_t) (UNIX64_FL_ACCMODE | UNIX64_FL_APPEND |
		UNIX64_FL_NONBLOCK)) { return -22; }
	return process_file_set_status64(process, fd,
		(wire & UNIX64_FL_APPEND ? PROCESS_FILE_APPEND64 : 0) |
		(wire & UNIX64_FL_NONBLOCK ? PROCESS_FILE_NONBLOCK64 : 0));
}

static int64_t unix_open64(struct PROCESS64 *process, const char *path, uint64_t flags)
{
	char name[FD64_NAME_MAX];
	int64_t length;
	int status;
	int fd;

	if (flags & ~(uint64_t) UNIX64_OPEN_CLOEXEC) { return -22; }
	length = copy_string_from_user64(name, sizeof(name), path);
	if (length < 0) { return length; }
	if (length == 0) { return -2; }
	/* fd64_open_checked validates UTF-8 and the bounded FAT root name. */
	status = process_file_open64(process, name, 0, !!(flags & UNIX64_OPEN_CLOEXEC), &fd);
	return status != 0 ? status : fd;
}

static int64_t unix_anon64(struct PROCESS_IMAGE64 *memory,
	uint64_t pointer, uint64_t size, int freeing)
{
	size_t pages, limit, first;

	if (size == 0 || size % UNIX64_PAGE_SIZE != 0) { return -22; }
	pages = size / UNIX64_PAGE_SIZE;
	limit = memory->heap.size / UNIX64_PAGE_SIZE;
	if (limit > sizeof(memory->anon_pages) / sizeof(memory->anon_pages[0])) { return -12; }
	if (pages > limit) { return freeing ? -22 : -12; }
	if (freeing) {
		if (pointer < memory->heap.base || (pointer - memory->heap.base) %
			UNIX64_PAGE_SIZE != 0) { return -22; }
		first = (pointer - memory->heap.base) / UNIX64_PAGE_SIZE;
		if (first >= limit || pages > limit - first ||
			memory->anon_pages[first] != pages) { return -22; }
		memset(&memory->anon_pages[first], 0, pages * sizeof(memory->anon_pages[0]));
		return 0;
	}
	/* One execution flow owns this arena. It is not mmap/munmap: backing and
	 * mappings stay with the image, and free permits reuse until exec/exit.
	 */
	for (first = 0; first <= limit - pages; first++) {
		size_t count;

		for (count = 0; count < pages && memory->anon_pages[first + count] == 0; count++) { }
		if (count != pages) { first += count; continue; }
		memset((void *) (memory->heap_backing + first * UNIX64_PAGE_SIZE), 0, size);
		memory->anon_pages[first] = (uint16_t) pages;
		for (count = 1; count < pages; count++) { memory->anon_pages[first + count] = UINT16_MAX; }
		return (int64_t) (memory->heap.base + first * UNIX64_PAGE_SIZE);
	}
	return -12;
}

int64_t unix_bridge_call64(struct PROCESS64 *process, uint64_t number,
	uint64_t a0, uint64_t a1, uint64_t a2)
{
	int status;
	int cloexec;
	int newfd;

	switch (number) {
	case UNIX64_SYS_SIGNAL_SET: return process64_signal_set64(a0, a1);
	case UNIX64_SYS_SIGNAL_SEND: return process64_signal_send64((int64_t) a0);
	case UNIX64_SYS_FUTEX_WAIT: return unix_futex_wait64(a0, (int32_t) a1, a2);
	case UNIX64_SYS_FUTEX_WAKE: return unix_futex_wake64(a0);
	case UNIX64_SYS_CLOCK_GET: return unix_clock_get64(a0);
	case UNIX64_SYS_GETPID: return process->pid;
	case UNIX64_SYS_GETPPID: return process->parent_pid;
	case UNIX64_SYS_WAITPID: return process64_wait_user64((int64_t) a0, (void *) a1, a2);
	case UNIX64_SYS_READ: return unix_io64(process, a0, (void *) a1, a2, 0);
	case UNIX64_SYS_WRITE: return unix_io64(process, a0, (void *) a1, a2, 1);
	case UNIX64_SYS_OPEN: return unix_open64(process, (const char *) a0, a1);
	case UNIX64_SYS_DUP2:
		if (!valid_fd64(process, a0) || a1 >= PROCESS64_MAX_FILES) { return -9; }
		status = process_file_dup264(process, (int) a0, (int) a1);
		return status != 0 ? status : (int64_t) a1;
	case UNIX64_SYS_FCNTL:
		/* Validate full-width wire values before narrowing to internal ints. */
		if (!valid_fd64(process, a0)) { return -9; }
		if (a1 == UNIX64_F_DUPFD || a1 == UNIX64_F_DUPFD_CLOEXEC) {
			if (a2 >= PROCESS64_MAX_FILES) { return -22; }
			status = process_file_dup64(process, (int) a0, (int) a2,
				a1 == UNIX64_F_DUPFD_CLOEXEC, &newfd);
			return status != 0 ? status : newfd;
		}
		if (a1 == UNIX64_F_GETFL) { return unix_getfl64(process, (int) a0); }
		if (a1 == UNIX64_F_SETFL) { return unix_setfl64(process, (int) a0, a2); }
		if (a1 == UNIX64_F_GETFD) {
			status = process_file_get_cloexec64(process, (int) a0, &cloexec);
			return status != 0 ? status : (cloexec ? UNIX64_FD_CLOEXEC : 0);
		}
		if (a1 != UNIX64_F_SETFD || (a2 & ~(uint64_t) UNIX64_FD_CLOEXEC)) {
			return -22;
		}
		return process_file_set_cloexec64(process, (int) a0, !!(a2 & UNIX64_FD_CLOEXEC));
	case UNIX64_SYS_ANON_ALLOC: return unix_anon64(process->memory, 0, a0, 0);
	case UNIX64_SYS_ANON_FREE: return unix_anon64(process->memory, a0, a1, 1);
	case UNIX64_SYS_CLOSE:
		if (a0 >= PROCESS64_MAX_FILES) { return -9; }
		return process_fd_close64(process, (int) a0);
	case UNIX64_SYS_ISATTY:
		if (!valid_fd64(process, a0)) { return -9; }
		return process_file_tty64(&process->files[a0]) ? 0 : -25;
	case UNIX64_SYS_SEEK:
		if (!valid_fd64(process, a0)) { return -9; }
		if (a2 > 2) { return -22; }
		return process_file_seek64(&process->files[a0], (int64_t) a1, (int) a2);
	default: return -38;
	}
}
