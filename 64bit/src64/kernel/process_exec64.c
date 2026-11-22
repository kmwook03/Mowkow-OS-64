#include <arch/platform64.h>
#include <console64.h>
#include <elf64_loader.h>
#include <kstring64.h>
#include <mtask64.h>
#include <process64.h>
#include <uapi/unix64.h>
#include <user_access64.h>

#define EXEC_BYTES64 65536U
#define EXEC_VECTOR64 256U

struct EXEC_ARGS64 {
	char path[FD64_NAME_MAX];
	char bytes[EXEC_BYTES64];
	size_t offsets[2][EXEC_VECTOR64];
	size_t count[2];
	size_t used;
};

static int snapshot_vector64(struct EXEC_ARGS64 *args, const void *vector, unsigned int which)
{
	uintptr_t base = (uintptr_t) vector;

	if (vector == NULL) { return 0; }
	for (size_t i = 0; i <= EXEC_VECTOR64; i++) {
		uintptr_t pointer;
		size_t offset = i * sizeof(pointer);
		int64_t length;

		if (offset > UINTPTR_MAX - base ||
			copy_from_user64(&pointer, (const void *) (base + offset), sizeof(pointer)) != 0) {
			return -14;
		}
		if (pointer == 0) { return 0; }
		if (i == EXEC_VECTOR64 || args->used == EXEC_BYTES64) { return -7; }
		length = copy_string_from_user64(args->bytes + args->used,
			EXEC_BYTES64 - args->used, (const char *) pointer);
		if (length < 0) { return length == -36 ? -7 : (int) length; }
		args->offsets[which][i] = args->used;
		args->used += (size_t) length + 1;
		args->count[which]++;
		if ((args->count[0] + args->count[1] + 2) * sizeof(pointer) >
			EXEC_BYTES64 - args->used) { return -7; }
	}
	return -7;
}

static int initial_stack64(struct PROCESS_IMAGE64 *image, const struct EXEC_ARGS64 *args)
{
	size_t path_size = strlen(args->path) + 1;
	size_t payload, words;
	uintptr_t strings, execfn, random, sp;
	uintptr_t alias = image->stack_storage.kernel_address;
	uint64_t *out;
	static uint64_t nonce64;
	uint64_t seed, irq;

	if (args->used > EXEC_BYTES64 || args->count[0] > EXEC_VECTOR64 ||
		args->count[1] > EXEC_VECTOR64 || path_size > FD64_NAME_MAX ||
		image->stack.size > UINTPTR_MAX - image->stack.base) { return -7; }
	payload = args->used + path_size + 16;
	words = 1 + args->count[0] + 1 + args->count[1] + 1 + 32;
	if (payload > image->stack.size || words * 8 > image->stack.size - payload) { return -7; }
	strings = image->stack.base + image->stack.size - payload;
	execfn = strings + args->used;
	random = execfn + path_size;
	sp = (strings - words * 8) & ~(uintptr_t) 15;
	if (sp < image->stack.base) { return -7; }
	uint64_t aux[][2] = {
		{3, image->phdr}, {4, 56}, {5, image->phnum}, {6, USER_PAGE_SIZE64},
		{7, 0}, {9, image->entry}, {11, 0}, {12, 0}, {13, 0}, {14, 0},
		{16, 0}, {23, 0}, {25, random}, {26, 0}, {31, execfn}, {0, 0}
	};

	irq = platform_irq_save64();
	seed = ++nonce64 ^ 0x9e3779b97f4a7c15ULL;
	platform_irq_restore64(irq);
	memcpy((void *) (alias + strings - image->stack.base), args->bytes, args->used);
	memcpy((void *) (alias + execfn - image->stack.base), args->path, path_size);
	/* Bring-up canary bytes only, NOT cryptographic entropy or an ASLR seed.
	 * Authentication must acquire real entropy before relying on AT_RANDOM.
	 */
	for (unsigned int i = 0; i < 16; i++) {
		seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
		*(uint8_t *) (alias + random - image->stack.base + i) = (uint8_t) seed;
	}
	out = (uint64_t *) (alias + sp - image->stack.base);
	*out++ = args->count[0];
	for (unsigned int which = 0; which < 2; which++) {
		for (size_t i = 0; i < args->count[which]; i++) {
			*out++ = strings + args->offsets[which][i];
		}
		*out++ = 0;
	}
	memcpy(out, aux, sizeof(aux));
	image->initial_rsp = sp;
	return 0;
}

static int prepare_unix64(struct PROCESS_IMAGE64 *image, const struct EXEC_ARGS64 *args)
{
	int status = address_space_create64(&image->space);

	if (status == 0) { status = elf64_prepare_image_abi(args->path, image, UNIX64_ABI_VERSION); }
	if (status != 0) { return status; }
	status = user_backing_alloc64(&image->stack_storage, USER_STACK_END64 - USER_STACK_BASE64);
	if (status == 0) {
		status = user_backing_alloc64(&image->heap_storage, USER_HEAP_END64 - USER_HEAP_BASE64);
	}
	if (status != 0) { return status; }
	image->stack = (struct PROCESS64_RANGE) {USER_STACK_BASE64, image->stack_storage.size};
	image->heap = (struct PROCESS64_RANGE) {USER_HEAP_BASE64, image->heap_storage.size};
	image->stack_backing = image->stack_storage.kernel_address;
	image->heap_backing = image->heap_storage.kernel_address;
	image->heap_next = USER_HEAP_BASE64;
	status = initial_stack64(image, args);
	if (status == 0) {
		status = address_space_map_backing64(image->space, image->stack.base,
			&image->stack_storage, 0, image->stack.size, USER_READ64 | USER_WRITE64);
	}
	if (status == 0) {
		status = address_space_map_backing64(image->space, image->heap.base,
			&image->heap_storage, 0, image->heap.size, USER_READ64 | USER_WRITE64);
	}
	return status;
}

int process64_execve_user64(const char *path, const void *argv, const void *envp)
{
	struct PROCESS64 *process = process64_current();
	struct PROCESS_IMAGE64 *next, *old;
	struct EXEC_ARGS64 *args;
	uintptr_t bytes;
	uint64_t irq;
	int status;
	int64_t length;

	if (process == NULL || process->state != PROCESS_RUNNING64) { return -22; }
	old = process->memory;
	next = old == &process->images[0] ? &process->images[1] : &process->images[0];
	status = process64_release_image(next);
	if (status == 0) { status = user_backing_free64(&process->exec_storage); }
	if (status != 0) { return status; }
	if (align_up_checked64(sizeof(*args), USER_PAGE_SIZE64, &bytes) != 0) { return -7; }
	status = user_backing_alloc64(&process->exec_storage, bytes);
	if (status != 0) { return status; }
	args = (struct EXEC_ARGS64 *) process->exec_storage.kernel_address;
	length = copy_string_from_user64(args->path, sizeof(args->path), path);
	status = length < 0 ? (int) length : length == 0 ? -2 : 0;
	if (status == 0) { status = snapshot_vector64(args, argv, 0); }
	if (status == 0) { status = snapshot_vector64(args, envp, 1); }
	if (status == 0) { status = prepare_unix64(next, args); }
	if (status != 0) { goto failed; }
	irq = platform_irq_save64();
	if (console64_close_requested(process->console)) { status = -4; }
	else { status = address_space_replace64(old->space, next->space); }
	if (status == 0) {
		process->memory = next;
		process->thread.initial_rsp = next->initial_rsp;
		process->thread.initial_argc = 0;
		process->thread.initial_argv = 0;
		process->thread.task->context.thread_pointer = 0;
		process->thread.task->context.readonly_thread_pointer = 0;
		platform_user_tls64(0);
		for (size_t i = 0; i < PROCESS64_MAX_FILES; i++) {
			if (process->files[i].cloexec) { memset(&process->files[i], 0, sizeof(process->files[i])); }
		}
	}
	platform_irq_restore64(irq);
	if (status != 0) { goto failed; }
	/* Failed retirement is retained in the inactive bank, not reported as a
	 * failed exec after committing. The next exec/exit retries it once.
	 */
	(void) process64_release_image(old);
	(void) user_backing_free64(&process->exec_storage);
	return 0;
failed:
	(void) process64_release_image(next);
	(void) user_backing_free64(&process->exec_storage);
	return status;
}

int process64_set_tls64(uintptr_t pointer)
{
	struct PROCESS64 *process = process64_current();
	uint64_t irq;

	if (process == NULL || process->state != PROCESS_RUNNING64 ||
		process->thread.task == NULL || (pointer & 15) != 0 ||
		!process64_user_write_valid((void *) pointer, 16)) { return -14; }
	irq = platform_irq_save64();
	process->thread.task->context.thread_pointer = pointer;
	platform_user_tls64(pointer);
	platform_irq_restore64(irq);
	return 0;
}
