/*
 * process64.c -- 유저 프로세스
 *
 * ELF64를 올리고, 스택과 힙을 붙이고, 링 3으로 들어갔다가 돌아오는 자리다.
 * 열린 파일 표도 프로세스마다 하나씩 갖고 있다.
 *
 * Each process owns private user backing, page tables and an entry stack.
 * Exec resumes on its console stack before detaching or reclaiming resources.
 */
#include <asmfunc64.h>
#include <arch/platform64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <elf64_loader.h>
#include <kstring64.h>
#include <memory64.h>
#include <mtask64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 실측으로 정한 값. report_usage가 프로세스마다 COM1에 실제 사용량을 찍는다.
 *
 * 힙: 나노의 사용량은 파일 크기가 아니라 줄 수를 따라간다 - 줄마다 최소
 * 64바이트다. 8 KiB짜리 4000줄 파일이 256 KiB의 98.5%를 먹었다. 1 MiB면
 * 같은 파일이 25% 언저리고, 64 KiB(MAX_FILE) 소스 파일도 편집할 여유가 남는다.
 * 스택: 나노가 472바이트, 다른 앱은 그보다 적게 썼다. 64 KiB는 135배 여유라
 * 사용자 스택 범위와 가드 페이지는 user_layout64.h에서 정의한다.
 */
#define USER_STACK_ALIGNMENT64 16U

struct PROCESS64_ARGS {
	char line[PROCESS64_CMDLINE_MAX];
	size_t offsets[PROCESS64_MAX_ARGS];
	size_t lengths[PROCESS64_MAX_ARGS];
	size_t argc;
};

static struct PROCESS64 process_table[USER_SPACE_LIMIT64];
static uint32_t next_pid = 1;

static void memzero(void *ptr, size_t size)
{
	uint8_t *p;

	p = (uint8_t *) ptr;
	while (size-- > 0) {
		*p++ = 0;
	}
}

static void copy_bytes(void *dst, const void *src, size_t size)
{
	uint8_t *d;
	const uint8_t *s;

	d = (uint8_t *) dst;
	s = (const uint8_t *) src;
	while (size-- > 0) {
		*d++ = *s++;
	}
}

static struct PROCESS64 *process_alloc(void)
{
	uint64_t irq = platform_irq_save64();
	struct PROCESS64 *process = NULL;

	for (size_t i = 0; i < USER_SPACE_LIMIT64; i++) {
		if (process_table[i].state != PROCESS_FREE64) { continue; }
		process = &process_table[i];
		memzero(process, sizeof(*process));
		process->state = PROCESS_LOADING64;
		/* PID zero denotes no process; avoid collisions even across wrap. */
		for (;;) {
			int duplicate = 0;

			process->pid = next_pid++;
			if (process->pid == 0) { continue; }
			for (size_t j = 0; j < USER_SPACE_LIMIT64; j++) {
				if (&process_table[j] != process &&
					process_table[j].pid == process->pid) { duplicate = 1; }
			}
			if (!duplicate) { break; }
		}
		break;
	}
	platform_irq_restore64(irq);
	return process;
}

static int range_contains64(const struct PROCESS64_RANGE *range, uintptr_t ptr, size_t size)
{
	size_t offset;

	if (range->base == 0 || range->size == 0 ||
		range->size > UINTPTR_MAX - range->base || ptr < range->base) {
		return 0;
	}
	offset = ptr - range->base;
	/* Do not merge adjacent image/stack/heap ranges, even for contiguous RAM.
	   A zero-byte buffer may point at this valid range's one-past-end address. */
	return offset <= range->size && size <= range->size - offset;
}

/* 실행 중인 프로세스는 태스크마다 다르다. 콘솔이 여러 개면 전역 하나로는
   서로의 saved_kernel_rsp를 덮어쓴다. */
struct PROCESS64 *process64_current(void)
{
	struct TASK64 *task = task_now64();

	return task != NULL ? (struct PROCESS64 *) task->process : NULL;
}

static int process_user_access64(const void *ptr, size_t size, unsigned int access)
{
	struct PROCESS64 *process = process64_current();
	uintptr_t p;

	if (process == NULL || ptr == NULL) {
		return 0;
	}
	p = (uintptr_t) ptr;
	if (size > UINTPTR_MAX - p) {
		return 0;
	}
	if (!range_contains64(&process->image, p, size) &&
		!range_contains64(&process->stack, p, size) &&
		!range_contains64(&process->heap, p, size)) { return 0; }
	return size == 0 || address_space_validate64(process->space, p, size, access) == 0;
}

int process64_user_range_valid(const void *ptr, size_t size)
{
	return process_user_access64(ptr, size, USER_READ64);
}

int process64_user_write_valid(void *ptr, size_t size)
{
	return process_user_access64(ptr, size, USER_WRITE64);
}

void process64_exit_current(int status)
{
	struct PROCESS64 *process = process64_current();

	if (process == NULL) {
		return;
	}
	process->exited = 1;
	process->exit_status = status;
}

uintptr_t process64_current_exit_rsp(void)
{
	struct PROCESS64 *process = process64_current();

	return process != NULL ? process->saved_kernel_rsp : 0;
}

int process64_current_exit_status(void)
{
	struct PROCESS64 *process = process64_current();

	return process != NULL ? process->exit_status : -1;
}

static int copy_program_name64(const char *path, char *name, size_t capacity)
{
	size_t i;

	if (path == NULL || name == NULL || capacity == 0) {
		return PROCESS64_ERR_INVALID;
	}
	for (i = 0; i < capacity; i++) {
		if (path[i] == '\0' || path[i] == ' ') {
			name[i] = '\0';
			return i != 0 ? 0 : PROCESS64_ERR_INVALID;
		}
		if (i == capacity - 1) {
			return PROCESS64_ERR_ARGS;
		}
		name[i] = path[i];
	}
	return PROCESS64_ERR_ARGS;
}

static int parse_args64(const char *cmdline, struct PROCESS64_ARGS *args)
{
	size_t len;
	size_t pos;
	size_t start;

	if (cmdline == NULL || args == NULL) {
		return PROCESS64_ERR_INVALID;
	}
	/* Snapshot once so validation and stack construction use identical input. */
	if (str_copy64(args->line, sizeof(args->line), cmdline) != 0) {
		return PROCESS64_ERR_ARGS;
	}
	len = strlen(args->line);
	args->argc = 0;
	pos = 0;
	while (pos < len) {
		while (pos < len && args->line[pos] == ' ') {
			pos++;
		}
		if (pos == len) {
			break;
		}
		if (args->argc == PROCESS64_MAX_ARGS) {
			return PROCESS64_ERR_ARGS;
		}
		start = pos;
		while (pos < len && args->line[pos] != ' ') {
			pos++;
		}
		args->offsets[args->argc] = start;
		args->lengths[args->argc++] = pos - start;
		args->line[pos] = '\0';
		if (pos < len) {
			pos++;
		}
	}
	return 0;
}

/* Plan all byte ranges before writing; argv pointers refer to user addresses,
   while writes use the kernel backing alias on both architectures. */
static int setup_args64(struct PROCESS64 *process, const struct PROCESS64_ARGS *args,
	uintptr_t *rsp_out, uintptr_t *argv_out)
{
	uintptr_t base;
	uintptr_t backing;
	uintptr_t sp;
	uintptr_t argv[PROCESS64_MAX_ARGS];
	uintptr_t *user_argv;
	size_t size;
	size_t bytes;
	size_t i;

	if (process == NULL || args == NULL || rsp_out == NULL || argv_out == NULL ||
		args->argc > PROCESS64_MAX_ARGS) {
		return PROCESS64_ERR_INVALID;
	}
	base = process->stack.base;
	size = process->stack.size;
	backing = process->stack_backing != 0 ? process->stack_backing : base;
	if (base == 0 || backing == 0 || size == 0 ||
		size > UINTPTR_MAX - base || size > UINTPTR_MAX - backing ||
		(base & (USER_STACK_ALIGNMENT64 - 1)) != 0 ||
		(backing & (USER_STACK_ALIGNMENT64 - 1)) != 0 ||
		(size & (USER_STACK_ALIGNMENT64 - 1)) != 0) {
		return PROCESS64_ERR_STACK;
	}
	sp = base + size;
	for (i = 0; i < args->argc; i++) {
		if (args->offsets[i] >= sizeof(args->line) ||
			args->lengths[i] >= sizeof(args->line) - args->offsets[i] ||
			args->line[args->offsets[i] + args->lengths[i]] != '\0') {
			return PROCESS64_ERR_INVALID;
		}
		if (args->lengths[i] >= sp - base) {
			return PROCESS64_ERR_STACK;
		}
		sp -= args->lengths[i] + 1;
		argv[i] = sp;
	}
	/* argc is bounded by MAX_ARGS before the terminator and byte count. */
	bytes = (args->argc + 1) * sizeof(uintptr_t);
	if (bytes > sp - base) {
		return PROCESS64_ERR_STACK;
	}
	/* Fixed 16-byte ABI alignment rounds down without addition overflow. */
	sp = (sp - bytes) & ~(uintptr_t) (USER_STACK_ALIGNMENT64 - 1);
	/* Leave one aligned slot below argv for the CRT's first call frame. */
	if (sp < base || sp - base < USER_STACK_ALIGNMENT64) {
		return PROCESS64_ERR_STACK;
	}
	for (i = 0; i < args->argc; i++) {
		copy_bytes((void *) (backing + (argv[i] - base)),
			args->line + args->offsets[i], args->lengths[i] + 1);
	}
	user_argv = (uintptr_t *) (backing + (sp - base));
	for (i = 0; i < args->argc; i++) {
		user_argv[i] = argv[i];
	}
	user_argv[args->argc] = 0;
	*rsp_out = sp;
	*argv_out = sp;
	return 0;
}

static int runtime_ready64;

void process64_switch_task64(const struct TASK64 *task)
{
	const struct PROCESS64 *process = task != NULL ? task->process : NULL;

	/* Before the first prepared execution, boot still owns its legacy root. */
	if (!runtime_ready64) { return; }
	if (address_space_activate64(process != NULL ? process->space : NULL) != 0) {
		/* A bad scheduler/root invariant must not resume under another root. */
		__builtin_trap();
	}
	platform_process_stack64(process != NULL ? process->entry_storage.kernel_address +
		process->entry_storage.size : 0);
}

uintptr_t process64_entry_stack_top(void)
{
	struct PROCESS64 *process = process64_current();

	if (process == NULL || process->state != PROCESS_RUNNING64 ||
		process->entry_storage.size != PROCESS64_ENTRY_STACK_SIZE) { __builtin_trap(); }
	return process->entry_storage.kernel_address + process->entry_storage.size;
}

/* Only exec's detached failures are collected here; explicit prepare callers
 * keep ownership of their own failed handles. Each visit attempts one unwind.
 */
static void process_reap_pending64(void)
{
	for (size_t i = 0; i < USER_SPACE_LIMIT64; i++) {
		struct PROCESS64 *process = &process_table[i];
		uint64_t irq = platform_irq_save64();
		int claimed = process->reap_pending;

		process->reap_pending = 0;
		platform_irq_restore64(irq);
		if (!claimed) { continue; }
		if (process64_discard_prepared(&process) != 0) {
			irq = platform_irq_save64();
			process->reap_pending = 1;
			platform_irq_restore64(irq);
		}
	}
}

int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console)
{
	struct PROCESS64 *process = NULL;
	struct TASK64 *task = task_now64();
	uint64_t irq;
	int status;

	if (task == NULL || task->process != NULL) { return -4; }
	process_reap_pending64();
	status = process64_prepare_file(path, cmdline, console, &process);
	if (status != 0) { goto cleanup; }
	irq = platform_irq_save64();
	runtime_ready64 = 1;
	task->process = process;
	task->is_user = 1;
	task->entry_stack_base = process->entry_storage.kernel_address;
	task->entry_stack_size = process->entry_storage.size;
	process->state = PROCESS_RUNNING64;
	process64_switch_task64(task);
	status = enter_user_mode64(process->entry, process->initial_rsp,
		process->initial_argc, process->initial_argv, GDT64_USER_CODE,
		GDT64_USER_DATA, &process->saved_kernel_rsp);
	/* Assembly returns on the console stack, with IRQs still disabled. */
	if (process->exited) { status = process->exit_status; }
	task->process = NULL;
	task->is_user = 0;
	task->kernel_rsp = 0;
	task->entry_stack_base = 0;
	task->entry_stack_size = 0;
	task->context.stack_pointer = 0;
	process64_switch_task64(task);
	process->state = PROCESS_REAP64;
	platform_irq_restore64(irq);
	console64_set_raw_con(process->console, 0);
	memzero(process->files, sizeof(process->files));
cleanup:
	if (process != NULL && process64_discard_prepared(&process) != 0) {
		irq = platform_irq_save64();
		process->reap_pending = 1;
		platform_irq_restore64(irq);
	}
	return status;
}

int process64_discard_prepared(struct PROCESS64 **result)
{
	struct PROCESS64 *process;
	int owned = 0;
	int status;
	uint64_t irq;

	if (result == NULL) { return -22; }
	process = *result;
	if (process == NULL) { return 0; }
	for (size_t i = 0; i < USER_SPACE_LIMIT64; i++) {
		if (process == &process_table[i]) { owned = 1; break; }
	}
	if (!owned || (process->state != PROCESS_LOADING64 &&
		process->state != PROCESS_READY64 && process->state != PROCESS_REAP64)) {
		return -22;
	}
	/* Destroy refuses active roots. Keep backing until every table is gone. */
	status = address_space_destroy64(&process->space);
	if (status == -16) { return status; }
	irq = platform_irq_save64();
	process->state = PROCESS_REAP64;
	platform_irq_restore64(irq);
	if (status != 0) { return status; }
	status = user_backing_free64(&process->entry_storage);
	if (status == 0) { status = user_backing_free64(&process->heap_storage); }
	if (status == 0) { status = user_backing_free64(&process->stack_storage); }
	if (status == 0) { status = user_backing_free64(&process->image_storage); }
	if (status == 0) { status = user_backing_free64(&process->elf_snapshot); }
	if (status != 0) { return status; }
	irq = platform_irq_save64();
	memzero(process, sizeof(*process));
	*result = NULL;
	platform_irq_restore64(irq);
	return 0;
}

int process64_prepare_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console, struct PROCESS64 **result)
{
	char name[FD64_NAME_MAX];
	struct PROCESS64_ARGS args;
	struct PROCESS64 *process;
	uint64_t irq;
	int status;

	if (result == NULL || *result != NULL) { return -22; }
	status = copy_program_name64(path, name, sizeof(name));
	if (status != 0) { return status; }
	status = parse_args64(cmdline != NULL ? cmdline : path, &args);
	if (status != 0) { return status; }
	process = process_alloc();
	if (process == NULL) { return -12; }
	*result = process;
	process->console = console;
	status = address_space_create64(&process->space);
	if (status == 0) { status = elf64_prepare_image(name, process); }
	if (status != 0) { goto failed; }
	status = user_backing_alloc64(&process->stack_storage,
		USER_STACK_END64 - USER_STACK_BASE64);
	if (status != 0) { goto failed; }
	process->stack.base = USER_STACK_BASE64;
	process->stack.size = process->stack_storage.size;
	process->stack_backing = process->stack_storage.kernel_address;
	status = user_backing_alloc64(&process->heap_storage,
		USER_HEAP_END64 - USER_HEAP_BASE64);
	if (status != 0) { goto failed; }
	process->heap.base = USER_HEAP_BASE64;
	process->heap.size = process->heap_storage.size;
	process->heap_backing = process->heap_storage.kernel_address;
	process->heap_next = USER_HEAP_BASE64;
	status = setup_args64(process, &args, &process->initial_rsp, &process->initial_argv);
	if (status != 0) { goto failed; }
	process->initial_argc = args.argc;
	status = address_space_map64(process->space, process->stack.base,
		process->stack_storage.physical, process->stack.size, USER_READ64 | USER_WRITE64);
	if (status == 0) {
		status = address_space_map64(process->space, process->heap.base,
			process->heap_storage.physical, process->heap.size, USER_READ64 | USER_WRITE64);
	}
	if (status != 0) { goto failed; }
	status = user_backing_alloc64(&process->entry_storage, PROCESS64_ENTRY_STACK_SIZE);
	if (status != 0) { goto failed; }
	irq = platform_irq_save64();
	process->state = PROCESS_READY64;
	platform_irq_restore64(irq);
	return 0;
failed:
	/* One bounded unwind attempt. A retained handle must be retried by its owner. */
	(void) process64_discard_prepared(result);
	return status;
}
