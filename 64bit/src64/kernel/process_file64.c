#include <arch/platform64.h>
#include <kstring64.h>
#include <process_file64.h>

static struct UNIX_FILE64 open_files64[PROCESS64_MAX * (PROCESS64_MAX_FILES - 3)];

static struct UNIX_FILE64 *promote_file64(struct PROCESS64_FILE *file)
{
	if (file->shared != NULL) { return file->shared; }
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		struct UNIX_FILE64 *shared = &open_files64[i];

		if (shared->references != 0) { continue; }
		memset(shared, 0, sizeof(*shared));
		shared->handle = file->fh;
		shared->references = 1;
		file->shared = shared;
		return shared;
	}
	return NULL;
}

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child)
{
	uint64_t irq = platform_irq_save64();
	int status = 0;

	for (size_t i = 3; i < PROCESS64_MAX_FILES; i++) {
		if (!parent->files[i].used) { continue; }
		struct UNIX_FILE64 *shared = promote_file64(&parent->files[i]);

		if (shared == NULL) { status = -23; break; }
		shared->references++;
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
