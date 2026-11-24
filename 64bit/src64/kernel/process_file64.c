#include <arch/platform64.h>
#include <kstring64.h>
#include <process_file64.h>

static struct UNIX_FILE64 open_files64[PROCESS64_MAX * (PROCESS64_MAX_FILES - 3)];

/* Caller holds irq-save. A reserved reference prevents reuse during open. */
static struct UNIX_FILE64 *allocate_file64(void)
{
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		struct UNIX_FILE64 *shared = &open_files64[i];

		if (shared->references != 0) { continue; }
		memset(shared, 0, sizeof(*shared));
		shared->references = 1;
		return shared;
	}
	return NULL;
}

static struct UNIX_FILE64 *promote_file64(struct PROCESS64_FILE *file)
{
	if (file->shared == NULL) {
		struct UNIX_FILE64 *shared = allocate_file64();

		if (shared == NULL) { return NULL; }
		shared->handle = file->fh;
		file->shared = shared;
	}
	return file->shared;
}

int process_file_open64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int cloexec, int *newfd)
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
	int status = fd64_open_checked(&prepared.shared->handle, name, flags);

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

/* Caller holds irq-save through reference acquisition and FD publication. */
static int retain_file64(struct PROCESS64_FILE *file, struct UNIX_FILE64 **result)
{
	struct UNIX_FILE64 *shared = promote_file64(file);

	if (shared == NULL) { return -23; }
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

void process_files_close64(struct PROCESS64 *process, int cloexec_only)
{
	for (size_t i = 0; i < PROCESS64_MAX_FILES; i++) {
		if (!cloexec_only || process->files[i].cloexec) {
			process_file_close64(&process->files[i]);
		}
	}
}

int process_file_lock64(struct PROCESS64_FILE *file, struct FDHANDLE64 **handle)
{
	int status = file->shared != NULL ? mutex64_lock(&file->shared->mutex) : 0;

	if (status == 0) { *handle = file->shared != NULL ? &file->shared->handle : &file->fh; }
	return status;
}

void process_file_unlock64(struct PROCESS64_FILE *file)
{
	if (file->shared != NULL) { (void) mutex64_unlock(&file->shared->mutex); }
}
