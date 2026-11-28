#include <arch/platform64.h>
#include <console64.h>
#include <kstring64.h>
#include <process_file64.h>

static int64_t fat_read64(struct UNIX_FILE64 *file, void *buffer, size_t size);
static int64_t fat_write64(struct UNIX_FILE64 *file, const void *buffer, size_t size);
static int64_t fat_seek64(struct UNIX_FILE64 *file, int64_t offset, int whence);
static int fat_stat64(struct UNIX_FILE64 *file, struct FILE_INFO64 *info);
static const struct PROCESS_FILE_OPS64 fat_ops64 = {
	.read = fat_read64,
	.write = fat_write64,
	.seek = fat_seek64,
	.stat = fat_stat64,
};

static int64_t console_read64(struct UNIX_FILE64 *file, void *buffer, size_t size);
static int64_t console_write64(struct UNIX_FILE64 *file, const void *buffer, size_t size);
static const struct PROCESS_FILE_OPS64 console_input_ops64 = {.read = console_read64};
static const struct PROCESS_FILE_OPS64 console_output_ops64 = {.write = console_write64};

static int seekable64(const struct UNIX_FILE64 *shared)
{
	return shared->ops != NULL && shared->ops->seek != NULL;
}

/* OFDs are referenced only by file tables, so every process filling its whole
 * table with distinct OFDs still fits; -ENFILE is defensive.
 */
static struct UNIX_FILE64 open_files64[PROCESS64_MAX * PROCESS64_MAX_FILES];

/* Caller holds irq-save. A reserved reference prevents reuse during open. */
static struct UNIX_FILE64 *allocate_file64(unsigned int access)
{
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		struct UNIX_FILE64 *shared = &open_files64[i];

		if (shared->references != 0) { continue; }
		memset(shared, 0, sizeof(*shared));
		shared->references = 1;
		shared->flags = access;
		shared->ops = &fat_ops64;
		return shared;
	}
	return NULL;
}

static int open_file64(struct PROCESS64 *process, const char *name,
	unsigned int flags, unsigned int access, int cloexec, int first, int *newfd,
	int (*open_backend)(struct FDHANDLE64 *, const char *, unsigned int))
{
	if (process == NULL || name == NULL || newfd == NULL ||
		(cloexec != 0 && cloexec != 1)) { return -22; }
	uint64_t irq = platform_irq_save64();
	int fd;

	for (fd = first; fd < PROCESS64_MAX_FILES; fd++) {
		if (!process->files[fd].used) { break; }
	}
	if (fd == PROCESS64_MAX_FILES) {
		platform_irq_restore64(irq);
		return -24;
	}
	struct PROCESS64_FILE prepared = {.cloexec = cloexec, .shared = allocate_file64(access)};

	platform_irq_restore64(irq);
	if (prepared.shared == NULL) { return -23; }
	/* One execution flow owns the table. Keep the FD unpublished until the
	 * sleeping backend succeeds; no allocation may fail after file mutation.
	 */
	int status = open_backend(&prepared.shared->handle, name, flags);

	if (status != 0) {
		process_file_close64(&prepared);
		return status;
	}
	prepared.used = 1;
	irq = platform_irq_save64();
	process->files[fd] = prepared;
	*newfd = fd;
	platform_irq_restore64(irq);
	return 0;
}

int process_file_open64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int cloexec, int *newfd)
{
	unsigned int access = PROCESS_FILE_READ64 |
		(flags & FILE_WRITING64 ? PROCESS_FILE_WRITE64 : 0);

	return open_file64(process, name, flags, access, cloexec, 0, newfd,
		fd64_open_checked);
}

static int legacy_open_backend64(struct FDHANDLE64 *handle, const char *name,
	unsigned int flags)
{
	/* Preserve legacy create fallback and truncate-only-for-existing behavior. */
	if (fd64_open(handle, name) == 0) {
		if (!(flags & FILE_CREATE64) || fd64_create(handle, name) == 0) { return -5; }
	} else if ((flags & FILE_TRUNCATE64) && fd64_truncate(handle, 0) != 0) {
		return -5;
	}
	return 0;
}

int process_file_open_legacy64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int *newfd)
{
	if (flags & ~(unsigned int) (FILE_CREATE64 | FILE_TRUNCATE64)) { return -22; }
	return open_file64(process, name, flags,
		PROCESS_FILE_READ64 | PROCESS_FILE_WRITE64, 0, 3, newfd, legacy_open_backend64);
}

/* Caller holds irq-save through reference acquisition and FD publication. */
static int retain_file64(struct PROCESS64_FILE *file, struct UNIX_FILE64 **result)
{
	struct UNIX_FILE64 *shared = file->shared;

	if (shared == NULL) { return -9; }
	if (shared->references == ~0U) { return -75; }
	shared->references++;
	*result = shared;
	return 0;
}

static int duplicate_file64(struct PROCESS64_FILE *source,
	struct PROCESS64_FILE *target, int cloexec)
{
	struct UNIX_FILE64 *shared;
	int status = retain_file64(source, &shared);

	if (status != 0) { return status; }
	/* Acquire before dropping target: both descriptors may name one OFD. */
	process_file_close64(target);
	*target = *source;
	target->shared = shared;
	target->cloexec = cloexec;
	return 0;
}

int process_file_dup64(struct PROCESS64 *process, int oldfd, int minimum,
	int cloexec, int *newfd)
{
	if (process == NULL || newfd == NULL || minimum < 0 ||
		minimum >= PROCESS64_MAX_FILES || (cloexec != 0 && cloexec != 1)) { return -22; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (oldfd < 0 || oldfd >= PROCESS64_MAX_FILES || !process->files[oldfd].used) {
		goto out;
	}
	status = -24;
	for (int fd = minimum; fd < PROCESS64_MAX_FILES; fd++) {
		if (process->files[fd].used) { continue; }
		status = duplicate_file64(&process->files[oldfd], &process->files[fd], cloexec);
		if (status == 0) { *newfd = fd; }
		break;
	}
out:
	platform_irq_restore64(irq);
	return status;
}

int process_file_dup264(struct PROCESS64 *process, int oldfd, int newfd)
{
	if (process == NULL) { return -22; }
	if (oldfd < 0 || oldfd >= PROCESS64_MAX_FILES ||
		newfd < 0 || newfd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (process->files[oldfd].used) {
		status = oldfd == newfd ? 0 :
			duplicate_file64(&process->files[oldfd], &process->files[newfd], 0);
	}
	platform_irq_restore64(irq);
	return status;
}

int process_file_get_cloexec64(struct PROCESS64 *process, int fd, int *cloexec)
{
	if (process == NULL || cloexec == NULL) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (process->files[fd].used) {
		*cloexec = process->files[fd].cloexec;
		status = 0;
	}
	platform_irq_restore64(irq);
	return status;
}

int process_file_set_cloexec64(struct PROCESS64 *process, int fd, int cloexec)
{
	if (process == NULL || (cloexec != 0 && cloexec != 1)) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (process->files[fd].used) {
		process->files[fd].cloexec = cloexec;
		status = 0;
	}
	platform_irq_restore64(irq);
	return status;
}

int process_file_get_status64(struct PROCESS64 *process, int fd, unsigned int *flags)
{
	if (process == NULL || flags == NULL) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	struct UNIX_FILE64 *shared = process->files[fd].used ? process->files[fd].shared : NULL;

	if (shared != NULL) { *flags = shared->flags; }
	platform_irq_restore64(irq);
	return shared != NULL ? 0 : -9;
}

int process_file_set_status64(struct PROCESS64 *process, int fd, unsigned int flags)
{
	if (process == NULL || (flags & ~PROCESS_FILE_STATUS64)) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	struct UNIX_FILE64 *shared = process->files[fd].used ? process->files[fd].shared : NULL;
	int status = -9;

	if (shared != NULL) {
		/* ponytail: streams block in console reads; A3's TTY adds EAGAIN. */
		status = (flags & PROCESS_FILE_NONBLOCK64) && !seekable64(shared) ? -95 : 0;
	}
	if (status == 0) { shared->flags = (shared->flags & ~PROCESS_FILE_STATUS64) | flags; }
	platform_irq_restore64(irq);
	return status;
}

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child)
{
	uint64_t irq = platform_irq_save64();
	int status = 0;

	for (size_t i = 0; i < PROCESS64_MAX_FILES; i++) {
		if (!parent->files[i].used) { continue; }
		struct UNIX_FILE64 *shared;

		status = retain_file64(&parent->files[i], &shared);
		if (status != 0) { break; }
		child->files[i] = parent->files[i];
	}
	platform_irq_restore64(irq);
	if (status != 0) { process_files_close64(child, 0); }
	return status;
}

void process_file_close64(struct PROCESS64_FILE *file)
{
	uint64_t irq = platform_irq_save64();

	if (file->shared != NULL) { file->shared->references--; }
	memset(file, 0, sizeof(*file));
	platform_irq_restore64(irq);
}

int process_fd_close64(struct PROCESS64 *process, int fd)
{
	if (process == NULL) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (process->files[fd].used) {
		process_file_close64(&process->files[fd]);
		status = 0;
	}
	platform_irq_restore64(irq);
	return status;
}

void process_files_close64(struct PROCESS64 *process, int cloexec_only)
{
	uint64_t irq = platform_irq_save64();

	for (size_t i = 0; i < PROCESS64_MAX_FILES; i++) {
		if (!cloexec_only || process->files[i].cloexec) {
			process_file_close64(&process->files[i]);
		}
	}
	platform_irq_restore64(irq);
}

static int64_t fat_read64(struct UNIX_FILE64 *file, void *buffer, size_t size)
{
	return fd64_read_checked(&file->handle, buffer, size);
}

static int64_t fat_write64(struct UNIX_FILE64 *file, const void *buffer, size_t size)
{
	/* Legacy FAT returns 0 for every failure, including read-only media. */
	size_t written = fd64_write(&file->handle, buffer, size);

	return size != 0 && written == 0 ? -5 : (int64_t) written;
}

static int64_t fat_seek64(struct UNIX_FILE64 *file, int64_t offset, int whence)
{
	struct FDHANDLE64 *handle = &file->handle;
	struct FDHANDLE64 temporary;
	int64_t base = whence == 0 ? 0 : (whence == 1 ? handle->pos : handle->info.size);

	if (offset < -base) { return -22; }
	if (offset > (int64_t) handle->info.size - base) { return -95; }
	/* FAT seek may mutate chain traversal state before reporting failure. */
	temporary = *handle;
	if (fd64_seek(&temporary, offset, whence) != 0) { return -5; }
	*handle = temporary;
	return handle->pos;
}

static int fat_stat64(struct UNIX_FILE64 *file, struct FILE_INFO64 *info)
{
	return fd64_stat_checked(&file->handle, NULL, info);
}

static int64_t console_read64(struct UNIX_FILE64 *file, void *buffer, size_t size)
{
	struct PROCESS64 *process = process64_current();

	/* The console FIFO wakes only its owner task. A fork child would sleep the
	 * parent's reader; A3's TTY reader queue lifts this.
	 */
	if (process == NULL || process->fork_task != NULL) { return -95; }
	int64_t count = (int64_t) console64_read_con(file->console, buffer, size);

	return count < 0 ? -4 : count;
}

static int64_t console_write64(struct UNIX_FILE64 *file, const void *buffer, size_t size)
{
	console64_write_con(file->console, buffer, size);
	return (int64_t) size;
}

int process_file_stdio64(struct PROCESS64 *process, struct CONSOLE64 *console)
{
	if (process == NULL || console == NULL) { return -22; }
	uint64_t irq = platform_irq_save64();
	struct UNIX_FILE64 *input = allocate_file64(PROCESS_FILE_READ64);
	struct UNIX_FILE64 *output = allocate_file64(PROCESS_FILE_WRITE64);
	int status = -23;

	if (input != NULL && output != NULL) {
		input->ops = &console_input_ops64;
		output->ops = &console_output_ops64;
		input->console = output->console = console;
		output->references = 2;
		process->files[0] = (struct PROCESS64_FILE) {.used = 1, .shared = input};
		process->files[1] = (struct PROCESS64_FILE) {.used = 1, .shared = output};
		process->files[2] = process->files[1];
		status = 0;
	} else {
		if (input != NULL) { input->references = 0; }
		if (output != NULL) { output->references = 0; }
	}
	platform_irq_restore64(irq);
	return status;
}

int process_file_tty64(const struct PROCESS64_FILE *file)
{
	uint64_t irq = platform_irq_save64();
	const struct UNIX_FILE64 *shared = file != NULL && file->used ? file->shared : NULL;
	int tty = shared != NULL && (shared->ops == &console_input_ops64 ||
		shared->ops == &console_output_ops64);

	platform_irq_restore64(irq);
	return tty;
}

/* Access bits are immutable, so callers may test them without irq-save. */
static int can_access64(const struct UNIX_FILE64 *shared, int writing)
{
	if (shared->ops == NULL) { return 0; }
	return writing ? (shared->flags & PROCESS_FILE_WRITE64) && shared->ops->write :
		(shared->flags & PROCESS_FILE_READ64) && shared->ops->read;
}

int process_file_check64(const struct PROCESS64_FILE *file, int writing)
{
	uint64_t irq = platform_irq_save64();
	const struct UNIX_FILE64 *shared = file != NULL && file->used ? file->shared : NULL;
	int open = shared != NULL && can_access64(shared, writing);

	platform_irq_restore64(irq);
	return open ? 0 : -9;
}

/* Only seekable OFDs take the cursor mutex; streams synchronize internally. */
static int lock_file64(struct PROCESS64_FILE *file)
{
	if (file == NULL || !file->used || file->shared == NULL) { return -9; }
	return seekable64(file->shared) ? mutex64_lock(&file->shared->mutex) : 0;
}

static void process_file_unlock64(struct PROCESS64_FILE *file)
{
	if (file->shared != NULL && seekable64(file->shared)) {
		(void) mutex64_unlock(&file->shared->mutex);
	}
}

int64_t process_file_read64(struct PROCESS64_FILE *file, void *buffer, size_t size)
{
	if (file == NULL) { return -9; }
	if (buffer == NULL || size > INT64_MAX) { return -22; }
	int status = lock_file64(file);

	if (status != 0) { return status; }
	struct UNIX_FILE64 *shared = file->shared;
	int64_t result = can_access64(shared, 0) ?
		shared->ops->read(shared, buffer, size) : -9;

	process_file_unlock64(file);
	return result;
}

int64_t process_file_write64(struct PROCESS64_FILE *file, const void *buffer,
	size_t size)
{
	if (file == NULL) { return -9; }
	if (buffer == NULL || size > INT64_MAX) { return -22; }
	int status = lock_file64(file);

	if (status != 0) { return status; }
	struct UNIX_FILE64 *shared = file->shared;
	int64_t result = can_access64(shared, 1) ? 0 : -9;

	if (result == 0 && (shared->flags & PROCESS_FILE_APPEND64) && seekable64(shared)) {
		result = shared->ops->seek(shared, 0, 2);
	}
	if (result >= 0) { result = shared->ops->write(shared, buffer, size); }

	process_file_unlock64(file);
	return result;
}

int64_t process_file_seek64(struct PROCESS64_FILE *file, int64_t offset, int whence)
{
	if (file == NULL) { return -9; }
	if (whence < 0 || whence > 2) { return -22; }
	int status = lock_file64(file);

	if (status != 0) { return status; }
	struct UNIX_FILE64 *shared = file->shared;
	int64_t result = shared->ops && shared->ops->seek ?
		shared->ops->seek(shared, offset, whence) : -29;

	process_file_unlock64(file);
	return result;
}

int process_file_stat64(struct PROCESS64_FILE *file, struct FILE_INFO64 *info)
{
	struct FILE_INFO64 snapshot = {0};

	if (info == NULL) { return -22; }
	int status = lock_file64(file);

	if (status != 0) { return status; }
	struct UNIX_FILE64 *shared = file->shared;

	status = shared->ops && shared->ops->stat ? shared->ops->stat(shared, &snapshot) : -95;
	process_file_unlock64(file);
	if (status == 0) { *info = snapshot; }
	return status;
}
