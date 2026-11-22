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

/* PID 1 is a permanent kernel reaper, not a user init image. */
static struct PROCESS64 init_process64 = {
	.state = PROCESS_RUNNING64, .pid = 1
};
static struct PROCESS64 process_table[PROCESS64_MAX - 1];
static uint32_t next_pid = 2;

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

/* Registry fields are protected by short IRQ-save transactions. Allocation,
 * address-space teardown, I/O and sleeping always happen outside this guard.
 */
static int process_owned64(const struct PROCESS64 *process)
{
	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		if (process == &process_table[i]) { return 1; }
	}
	return 0;
}

static struct PROCESS64 *process_by_pid64(uint32_t pid)
{
	if (pid == 1) { return &init_process64; }
	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		if (process_table[i].state != PROCESS_FREE64 && process_table[i].pid == pid) {
			return &process_table[i];
		}
	}
	return NULL;
}

int process64_reserve_child(struct PROCESS64 *parent, struct PROCESS64 **result)
{
	uint64_t irq;
	struct PROCESS64 *process = NULL;

	if (result == NULL || *result != NULL) { return -22; }
	if (parent == NULL) { parent = &init_process64; }
	irq = platform_irq_save64();
	if ((parent != &init_process64 && !process_owned64(parent)) ||
		(parent->state != PROCESS_RUNNING64 && parent->state != PROCESS_READY64) ||
		parent->exited) {
		platform_irq_restore64(irq);
		return -3;
	}
	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		if (process_table[i].state == PROCESS_FREE64) {
			process = &process_table[i];
			break;
		}
	}
	if (process != NULL) {
		/* At most MAX-1 IDs are occupied; MAX probes must find a free PID. */
		for (size_t attempt = 0; attempt < PROCESS64_MAX; attempt++) {
			uint32_t pid = next_pid;

			if (pid < 2 || pid > PROCESS64_PID_MAX) { pid = 2; }
			next_pid = pid == PROCESS64_PID_MAX ? 2 : pid + 1;
			if (process_by_pid64(pid) != NULL) { continue; }
			memzero(process, sizeof(*process));
			process->pid = pid;
			process->parent_pid = parent->pid;
			process->state = PROCESS_LOADING64;
			*result = process;
			platform_irq_restore64(irq);
			return 0;
		}
	}
	platform_irq_restore64(irq);
	return -11;
}

static struct PROCESS64 *process_alloc(void)
{
	struct PROCESS64 *process = NULL;

	(void) process64_reserve_child(process64_current(), &process);
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

	return task != NULL ? task->process : NULL;
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

void *process64_io_buffer(size_t size)
{
	struct PROCESS64 *process = process64_current();
	uintptr_t rounded;

	if (process == NULL || size > USER_IMAGE_END64 - USER_IMAGE_BASE64) { return NULL; }
	if (size == 0) { return &process->io_empty; }
	if (!process->io_ready || size > process->io_storage.size) {
		if (align_up_checked64(size, USER_PAGE_SIZE64, &rounded) != 0 ||
			user_backing_free64(&process->io_storage) != 0) { return NULL; }
		process->io_ready = 0;
		if (user_backing_alloc64(&process->io_storage, rounded) != 0) { return NULL; }
		process->io_ready = 1;
	}
	return (void *) process->io_storage.kernel_address;
}

void process64_exit_current(int status)
{
	struct PROCESS64 *process = process64_current();

	if (process == NULL) {
		return;
	}
	process->exited = 1;
	process->exit_status = status;
	process->wait_status = ((unsigned int) status & 0xffU) << 8;
}

void process64_terminate_current(int status, unsigned int signal)
{
	struct PROCESS64 *process = process64_current();

	if (process == NULL || signal == 0 || signal > 127) { return; }
	process64_exit_current(status);
	process->wait_status = (int) signal;
}

int process64_return_to_user64(int user_mode)
{
	struct PROCESS64 *process = process64_current();

	if (!user_mode || process == NULL) { return 0; }
	if (!process->exited && console64_close_requested(process->console)) {
		process64_terminate_current(PROCESS64_EXIT_CLOSED, 15);
	}
	return process->exited;
}

uintptr_t process64_current_exit_rsp(void)
{
	struct PROCESS64 *process = process64_current();

	return process != NULL ? process->thread.saved_kernel_rsp : 0;
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
	platform_process_stack64(process != NULL ? process->thread.entry_storage.kernel_address +
		process->thread.entry_storage.size : 0);
}

uintptr_t process64_entry_stack_top(void)
{
	struct PROCESS64 *process = process64_current();

	if (process == NULL || process->state != PROCESS_RUNNING64 ||
		process->thread.entry_storage.size != PROCESS64_ENTRY_STACK_SIZE) { __builtin_trap(); }
	return process->thread.entry_storage.kernel_address + process->thread.entry_storage.size;
}

/* No resource work under the registry guard. A failed free retains the owner
 * in REAP; only fully released processes may publish a waitable zombie.
 */
static int process_release64(struct PROCESS64 *process)
{
	int status = address_space_destroy64(&process->space);

	if (status != 0) { return status; }
	status = user_backing_free64(&process->io_storage);
	if (status == 0) { status = user_backing_free64(&process->thread.entry_storage); }
	if (status == 0) { status = user_backing_free64(&process->heap_storage); }
	if (status == 0) { status = user_backing_free64(&process->stack_storage); }
	if (status == 0) { status = user_backing_free64(&process->image_storage); }
	if (status == 0) { status = user_backing_free64(&process->elf_snapshot); }
	return status;
}

static void process_reparent64(const struct PROCESS64 *parent)
{
	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		struct PROCESS64 *child = &process_table[i];

		if (child->state != PROCESS_FREE64 && child->parent_pid == parent->pid) {
			child->parent_pid = 1;
			child->orphan = 1;
		}
	}
}

static void process_wake_parent64(uint32_t pid)
{
	uint64_t irq = platform_irq_save64();
	struct PROCESS64 *parent = process_by_pid64(pid);
	struct TASK64 *waiter = parent != NULL ? parent->waiter : NULL;

	/* No queue lock is held. Keep preemption disabled through admission: a
	 * close wake could otherwise let the waiter return and recycle its task
	 * between taking this pointer and task_run. The scheduler never sleeps
	 * here; failed admission is retried by collect.
	 */
	if (waiter != NULL) { (void) task_run64(waiter, -1, 0); }
	platform_irq_restore64(irq);
}

static int process_finish64(struct PROCESS64 *process)
{
	int status = process_release64(process);
	uint64_t irq;
	uint32_t parent_pid;

	if (status != 0) { return status; }
	irq = platform_irq_save64();
	process->console = NULL;
	process->state = PROCESS_ZOMBIE64;
	parent_pid = process->parent_pid;
	platform_irq_restore64(irq);
	process_wake_parent64(parent_pid);
	return 0;
}

void process64_collect(void)
{
	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		struct PROCESS64 *process = &process_table[i];
		uint64_t irq = platform_irq_save64();
		int claimed = process->reap_pending;

		process->reap_pending = 0;
		platform_irq_restore64(irq);
		if (claimed) {
			int status = process->started ? process_finish64(process) :
				process64_discard_prepared(&process);

			if (status != 0) {
				irq = platform_irq_save64();
				process->reap_pending = 1;
				platform_irq_restore64(irq);
			}
		}
		/* Do not retain a raw pointer across wake/scheduling after a record
		 * becomes waitable: wait may already have consumed and reused it.
		 */
		irq = platform_irq_save64();
		process = &process_table[i];
		uint32_t parent_pid = process->state == PROCESS_ZOMBIE64 ?
			process->parent_pid : 0;

		if (parent_pid == 1 && (process->orphan || process->auto_reap)) {
			memzero(process, sizeof(*process));
			parent_pid = 0;
		}
		platform_irq_restore64(irq);
		if (parent_pid != 0) { process_wake_parent64(parent_pid); }
	}
}

int process64_console_busy64(const struct CONSOLE64 *console)
{
	uint64_t irq = platform_irq_save64();
	int busy = 0;

	for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
		if (process_table[i].state != PROCESS_FREE64 &&
			process_table[i].state != PROCESS_ZOMBIE64 &&
			process_table[i].console == console) { busy = 1; break; }
	}
	platform_irq_restore64(irq);
	return busy;
}

int process64_reap_console64(const struct CONSOLE64 *console)
{
	if (console == NULL) { return -22; }
	process64_collect();
	return process64_console_busy64(console) ? -16 : 0;
}

int process64_wait_child(struct PROCESS64 *parent, int32_t pid,
	unsigned int options, int32_t *reaped_pid, int32_t *wait_status)
{
	struct TASK64 *task = task_now64();

	if (reaped_pid == NULL || wait_status == NULL ||
		(pid != -1 && pid <= 0) || (options & ~PROCESS64_WAIT_NOHANG) != 0) {
		return -22;
	}
	if (parent == NULL) { parent = &init_process64; }
	for (;;) {
		uint64_t irq = platform_irq_save64();
		int children = 0;
		int status;

		if ((parent != &init_process64 && !process_owned64(parent)) ||
			(parent->state != PROCESS_READY64 && parent->state != PROCESS_RUNNING64) ||
			parent->exited || (parent != &init_process64 &&
			parent->state == PROCESS_RUNNING64 && parent != process64_current())) {
			platform_irq_restore64(irq);
			return -3;
		}
		if (parent->waiter != NULL && parent->waiter != task) {
			platform_irq_restore64(irq);
			return -16;
		}
		for (size_t i = 0; i < PROCESS64_MAX - 1; i++) {
			struct PROCESS64 *child = &process_table[i];

			if (child->state == PROCESS_FREE64 || !child->started || child->legacy_owner ||
				child->parent_pid != parent->pid ||
				(pid != -1 && child->pid != (uint32_t) pid)) { continue; }
			children = 1;
			if (child->state != PROCESS_ZOMBIE64) { continue; }
			*reaped_pid = (int32_t) child->pid;
			*wait_status = child->wait_status;
			memzero(child, sizeof(*child));
			platform_irq_restore64(irq);
			return 0;
		}
		if (!children || (options & PROCESS64_WAIT_NOHANG)) {
			if (children) { *reaped_pid = 0; }
			platform_irq_restore64(irq);
			return children ? 0 : -10;
		}
		if (parent != &init_process64 && console64_close_requested(parent->console)) {
			platform_irq_restore64(irq);
			return -4;
		}
		if (task == NULL) {
			platform_irq_restore64(irq);
			return -22;
		}
		/* Register and prepare sleep with the condition still protected. */
		parent->waiter = task;
		status = task_sleep_prepare64(task);
		if (status != 0) { parent->waiter = NULL; }
		platform_irq_restore64(irq);
		if (status != 0) { return status; }
		status = task_sleep_commit64(task);
		irq = platform_irq_save64();
		parent->waiter = NULL;
		platform_irq_restore64(irq);
		if (status != 0) { return status; }
	}
}

int process64_run_prepared(struct PROCESS64 *process)
{
	struct TASK64 *task = task_now64();
	uint64_t irq = platform_irq_save64();
	int status;

	if (!process_owned64(process) || process->state != PROCESS_READY64 ||
		task == NULL || task->process != NULL || process->thread.task != NULL ||
		process->waiter != NULL) {
		platform_irq_restore64(irq);
		return -22;
	}
	if (console64_close_requested(process->console)) {
		platform_irq_restore64(irq);
		return -125;
	}
	runtime_ready64 = 1;
	task->process = process;
	task->is_user = 1;
	task->entry_stack_base = process->thread.entry_storage.kernel_address;
	task->entry_stack_size = process->thread.entry_storage.size;
	process->thread.task = task;
	process->state = PROCESS_RUNNING64;
	process->started = 1;
	process64_switch_task64(task);
	status = enter_user_mode64(process->entry, process->thread.initial_rsp,
		process->thread.initial_argc, process->thread.initial_argv, GDT64_USER_CODE,
		GDT64_USER_DATA, &process->thread.saved_kernel_rsp);
	/* Assembly returns to the runner's kernel stack with IRQs disabled. */
	if (!process->exited) { process64_exit_current(status); }
	task->process = NULL;
	task->is_user = 0;
	task->kernel_rsp = 0;
	task->entry_stack_base = 0;
	task->entry_stack_size = 0;
	task->context.stack_pointer = 0;
	process->thread.task = NULL;
	process64_switch_task64(task);
	process->state = PROCESS_REAP64;
	process_reparent64(process);
	platform_irq_restore64(irq);
	process_wake_parent64(1);
	console64_set_raw_con(process->console, 0);
	memzero(process->files, sizeof(process->files));
	if (process_finish64(process) != 0) {
		irq = platform_irq_save64();
		process->reap_pending = 1;
		platform_irq_restore64(irq);
	}
	return 0;
}

int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console)
{
	struct PROCESS64 *process = NULL;
	struct TASK64 *task = task_now64();
	uint64_t irq;
	int status;

	if (task == NULL || task->process != NULL) { return -4; }
	if (console64_close_requested(console)) { return PROCESS64_EXIT_CLOSED; }
	process64_collect();
	status = process64_prepare_file(path, cmdline, console, &process);
	if (status != 0) { goto cleanup; }
	process->legacy_owner = 1;
	status = process64_run_prepared(process);
	if (status != 0) {
		if (status == -125) { status = PROCESS64_EXIT_CLOSED; }
		goto cleanup;
	}
	/* The legacy console owns this synchronous result. It has no user parent
	 * waiting for a zombie, so consume it here or after a retained free retry.
	 */
	irq = platform_irq_save64();
	status = process->exit_status;
	if (process->state == PROCESS_ZOMBIE64) { memzero(process, sizeof(*process)); }
	else { process->auto_reap = 1; }
	platform_irq_restore64(irq);
	return status;
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
	int status;
	uint64_t irq;

	if (result == NULL) { return -22; }
	process = *result;
	if (process == NULL) { return 0; }
	if (!process_owned64(process) || process->started ||
		(process->state != PROCESS_LOADING64 && process->state != PROCESS_READY64 &&
		process->state != PROCESS_REAP64)) { return -22; }
	if (process->waiter != NULL) { return -16; }
	status = address_space_destroy64(&process->space);
	if (status == -16) { return status; }
	irq = platform_irq_save64();
	process->state = PROCESS_REAP64;
	platform_irq_restore64(irq);
	if (status != 0) { return status; }
	status = process_release64(process);
	if (status != 0) { return status; }
	irq = platform_irq_save64();
	process_reparent64(process);
	memzero(process, sizeof(*process));
	*result = NULL;
	platform_irq_restore64(irq);
	process_wake_parent64(1);
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
	if (process == NULL) { return -11; }
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
	status = setup_args64(process, &args, &process->thread.initial_rsp, &process->thread.initial_argv);
	if (status != 0) { goto failed; }
	process->thread.initial_argc = args.argc;
	status = address_space_map_backing64(process->space, process->stack.base,
		&process->stack_storage, 0, process->stack.size, USER_READ64 | USER_WRITE64);
	if (status == 0) {
		status = address_space_map_backing64(process->space, process->heap.base,
			&process->heap_storage, 0, process->heap.size, USER_READ64 | USER_WRITE64);
	}
	if (status != 0) { goto failed; }
	status = user_backing_alloc64(&process->thread.entry_storage, PROCESS64_ENTRY_STACK_SIZE);
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
