#include <arch/platform64.h>
#include <kstring64.h>
#include <process_file64.h>

static int64_t fat_read64(struct UNIX_FILE64 *file, void *buffer, size_t size);
static int64_t fat_seek64(struct UNIX_FILE64 *file, int64_t offset, int whence);
static const struct PROCESS_FILE_OPS64 fat_ops64 = {
	.read = fat_read64,
	.seek = fat_seek64,
};

static struct UNIX_FILE64 open_files64[PROCESS64_MAX * (PROCESS64_MAX_FILES - 3)];

/* Caller holds irq-save. A reserved reference prevents reuse during open. */
static struct UNIX_FILE64 *allocate_file64(void)
{
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		struct UNIX_FILE64 *shared = &open_files64[i];

		if (shared->references != 0) { continue; }
		memset(shared, 0, sizeof(*shared));
		shared->references = 1;
		shared->ops = &fat_ops64;
		return shared;
	}
	return NULL;
}

static int open_file64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int cloexec, int *newfd,
	int (*open_backend)(struct FDHANDLE64 *, const char *, unsigned int))
{
	if (process == NULL || name == NULL || newfd == NULL ||
		(cloexec != 0 && cloexec != 1)) { return -22; }
	uint64_t irq = platform_irq_save64();
	int fd;

	for (fd = 3; fd < PROCESS64_MAX_FILES; fd++) {
		if (!process->files[fd].used) { break; }
	}
	if (fd == PROCESS64_MAX_FILES) {
		platform_irq_restore64(irq);
		return -24;
	}
	struct PROCESS64_FILE prepared = {.cloexec = cloexec, .shared = allocate_file64()};

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
	return open_file64(process, name, flags, cloexec, newfd, fd64_open_checked);
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
	return open_file64(process, name, flags, 0, newfd, legacy_open_backend64);
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
	if (process == NULL || newfd == NULL || minimum < 3 ||
		minimum >= PROCESS64_MAX_FILES || (cloexec != 0 && cloexec != 1)) { return -22; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (oldfd < 3 || oldfd >= PROCESS64_MAX_FILES || !process->files[oldfd].used) {
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
	if (oldfd < 3 || oldfd >= PROCESS64_MAX_FILES ||
		newfd < 3 || newfd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (process->files[oldfd].used) {
		status = oldfd == newfd ? 0 :
			duplicate_file64(&process->files[oldfd], &process->files[newfd], 0);
	}
	platform_irq_restore64(irq);
	return status;
}

/* Caller holds irq-save; stdio has no OFD until console FD unification. */
static int descriptor_open64(const struct PROCESS64 *process, int fd)
{
	if (fd < 3) {
		return process->console != NULL && !(process->closed_stdio & (1U << fd));
	}
	return process->files[fd].used;
}

int process_file_get_cloexec64(struct PROCESS64 *process, int fd, int *cloexec)
{
	if (process == NULL || cloexec == NULL) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (descriptor_open64(process, fd)) {
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

	if (descriptor_open64(process, fd)) {
		process->files[fd].cloexec = cloexec;
		status = 0;
	}
	platform_irq_restore64(irq);
	return status;
}

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child)
{
	uint64_t irq = platform_irq_save64();
	int status = 0;

	for (size_t i = 3; i < PROCESS64_MAX_FILES; i++) {
		if (!parent->files[i].used) { continue; }
		struct UNIX_FILE64 *shared;

		status = retain_file64(&parent->files[i], &shared);
		if (status != 0) { break; }
		child->files[i] = parent->files[i];
	}
	child->closed_stdio = parent->closed_stdio;
	for (size_t i = 0; i < 3; i++) {
		child->files[i].cloexec = parent->files[i].cloexec;
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

/* Caller holds irq-save. Cleanup may also release an already closed slot. */
static void close_descriptor64(struct PROCESS64 *process, int fd)
{
	if (fd < 3) { process->closed_stdio |= 1U << fd; }
	process_file_close64(&process->files[fd]);
}

int process_fd_close64(struct PROCESS64 *process, int fd)
{
	if (process == NULL) { return -22; }
	if (fd < 0 || fd >= PROCESS64_MAX_FILES) { return -9; }
	uint64_t irq = platform_irq_save64();
	int status = -9;

	if (descriptor_open64(process, fd)) {
		close_descriptor64(process, fd);
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
			close_descriptor64(process, (int) i);
		}
	}
	platform_irq_restore64(irq);
}

static int64_t fat_read64(struct UNIX_FILE64 *file, void *buffer, size_t size)
{
	return fd64_read_checked(&file->handle, buffer, size);
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

static int lock_file64(struct PROCESS64_FILE *file)
{
	if (file == NULL || !file->used || file->shared == NULL) { return -9; }
	return mutex64_lock(&file->shared->mutex);
}

int64_t process_file_read64(struct PROCESS64_FILE *file, void *buffer, size_t size)
{
	if (file == NULL) { return -9; }
	if (buffer == NULL || size > INT64_MAX) { return -22; }
	int status = lock_file64(file);

	if (status != 0) { return status; }
	struct UNIX_FILE64 *shared = file->shared;
	int64_t result = shared->ops && shared->ops->read ?
		shared->ops->read(shared, buffer, size) : -95;

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
		shared->ops->seek(shared, offset, whence) : -95;

	process_file_unlock64(file);
	return result;
}

int process_file_lock64(struct PROCESS64_FILE *file, struct FDHANDLE64 **handle)
{
	int status = lock_file64(file);

	if (status == 0) { *handle = &file->shared->handle; }
	return status;
}

void process_file_unlock64(struct PROCESS64_FILE *file)
{
	if (file->shared != NULL) { (void) mutex64_unlock(&file->shared->mutex); }
}
