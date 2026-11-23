/* Temporary v1 console/read-only FAT and libc-arena boundary until VFS/VM. */
#include <console64.h>
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
	if (fd >= PROCESS64_MAX_FILES) { return 0; }
	if (fd < 3) {
		return process->console != NULL && !(process->closed_stdio & (1U << fd));
	}
	return process->files[fd].used;
}

static int64_t unix_io64(struct PROCESS64 *process, uint64_t fd,
	void *buffer, uint64_t size, int writing)
{
	struct USER_ACCESS64 access = {0};
	void *snapshot;
	int64_t result;

	if (!valid_fd64(process, fd) || (writing && fd == 0) ||
		(!writing && (fd == 1 || fd == 2))) { return -9; }
	/* FAT's legacy unsigned write result cannot express a reliable errno. */
	if (writing && fd >= 3) { return -30; }
	if (size > INT64_MAX) { return -22; }
	if (size == 0) { return 0; }
	/* The temporary console has one owner task/FIFO reader. Shared TTY input
	 * needs A3's reader queue; never sleep a fork child's parent task here.
	 */
	if (!writing && fd == 0 && process->fork_task != NULL) { return -95; }
	if (writing ? !process64_user_range_valid(buffer, size) :
		!process64_user_write_valid(buffer, size)) { return -14; }
	snapshot = process64_io_buffer(size);
	if (snapshot == NULL) { return -12; }
	if (writing) {
		if (copy_from_user64(snapshot, buffer, size) != 0) { return -14; }
		console64_write_con(process->console, snapshot, size);
		return (int64_t) size;
	}
	result = user_access_begin64(&access, buffer, size, USER_WRITE64);
	if (result != 0) { return result; }
	if (fd == 0) {
		result = (int64_t) console64_read_con(process->console, snapshot, size);
		if (result < 0) { result = -4; }
	} else {
		struct FDHANDLE64 *file;

		result = process_file_lock64(&process->files[fd], &file);
		if (result == 0) {
			result = fd64_read_checked(file, snapshot, size);
			process_file_unlock64(&process->files[fd]);
		}
	}
	if (result >= 0) {
		if ((uint64_t) result > size) { result = -5; }
		else if (copy_to_user_access64(&access, snapshot, result) != 0) { result = -14; }
	}
	user_access_end64(&access);
	return result;
}

static int64_t unix_open64(struct PROCESS64 *process, const char *path, uint64_t flags)
{
	char name[FD64_NAME_MAX];
	int64_t length;
	int status;

	if (flags & ~(uint64_t) UNIX64_OPEN_CLOEXEC) { return -22; }
	length = copy_string_from_user64(name, sizeof(name), path);
	if (length < 0) { return length; }
	if (length == 0) { return -2; }
	/* fd64_open_checked validates UTF-8 and the bounded FAT root name. */
	for (size_t i = 3; i < PROCESS64_MAX_FILES; i++) {
		if (process->files[i].used) { continue; }
		status = fd64_open_checked(&process->files[i].fh, name, 0);
		if (status != 0) { return status; }
		process->files[i].used = 1;
		process->files[i].cloexec = !!(flags & UNIX64_OPEN_CLOEXEC);
		return (int64_t) i;
	}
	return -24;
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
	struct FDHANDLE64 temporary;
	struct FDHANDLE64 *file;
	int64_t base, offset;
	int status;

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
	case UNIX64_SYS_ANON_ALLOC: return unix_anon64(process->memory, 0, a0, 0);
	case UNIX64_SYS_ANON_FREE: return unix_anon64(process->memory, a0, a1, 1);
	case UNIX64_SYS_CLOSE:
		if (!valid_fd64(process, a0)) { return -9; }
		if (a0 < 3) { process->closed_stdio |= 1U << a0; }
		else { process_file_close64(&process->files[a0]); }
		return 0;
	case UNIX64_SYS_ISATTY:
		if (!valid_fd64(process, a0)) { return -9; }
		return a0 < 3 ? 0 : -25;
	case UNIX64_SYS_SEEK:
		if (!valid_fd64(process, a0)) { return -9; }
		if (a0 < 3) { return -29; }
		if (a2 > 2) { return -22; }
		status = process_file_lock64(&process->files[a0], &file);
		if (status != 0) { return status; }
		base = a2 == 0 ? 0 : (a2 == 1 ? file->pos : file->info.size);
		offset = (int64_t) a1;
		if (offset < -base) { status = -22; goto seek_out; }
		if (offset > (int64_t) file->info.size - base) { status = -95; goto seek_out; }
		/* Legacy seek may mutate its cursor before a chain read fails. */
		temporary = *file;
		status = fd64_seek(&temporary, offset, (int) a2);
		if (status != 0) { status = -5; }
		else { *file = temporary; }
seek_out:
		base = file->pos;
		process_file_unlock64(&process->files[a0]);
		return status != 0 ? status : base;
	default: return -38;
	}
}
