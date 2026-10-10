/*
 * process64.c -- 유저 프로세스
 *
 * ELF64를 올리고, 스택과 힙을 붙이고, 링 3으로 들어갔다가 돌아오는 자리다.
 * 열린 파일 표도 프로세스마다 하나씩 갖고 있다.
 *
 * 페이즈 1(주소 공간 분리) 전이라 상주 프로세스는 한 번에 하나뿐이고,
 * 이미지는 모두 같은 고정 창에 올라간다.
 */
#include <asmfunc64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <console64.h>
#include <dsctbl64.h>
#include <elf64_loader.h>
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
 * 줄일 이유가 없어 그대로 둔다. 페이즈 1이 여기에 가드 페이지를 붙인다.
 */
#define USER_STACK_SIZE (64 * 1024)
#define USER_HEAP_SIZE  (1024 * 1024)
#define USER_STACK_ALIGNMENT64 16U

struct PROCESS64_ARGS {
	char line[PROCESS64_CMDLINE_MAX];
	size_t offsets[PROCESS64_MAX_ARGS];
	size_t lengths[PROCESS64_MAX_ARGS];
	size_t argc;
};

static struct PROCESS64 process_table[4];
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
	uint32_t i;

	for (i = 0; i < sizeof(process_table) / sizeof(process_table[0]); i++) {
		if (process_table[i].pid == 0) {
			memzero(&process_table[i], sizeof(process_table[i]));
			process_table[i].pid = next_pid++;
			return &process_table[i];
		}
	}
	return NULL;
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

int process64_user_range_valid(const void *ptr, size_t size)
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
	return range_contains64(&process->image, p, size) != 0 ||
		range_contains64(&process->stack, p, size) != 0 ||
		range_contains64(&process->heap, p, size) != 0;
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
	for (len = 0; len < sizeof(args->line); len++) {
		args->line[len] = cmdline[len];
		if (args->line[len] == '\0') {
			break;
		}
	}
	if (len == sizeof(args->line)) {
		return PROCESS64_ERR_ARGS;
	}
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
   while writes use the kernel backing alias on AArch64. */
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

static void process_free_memory(struct PROCESS64 *process)
{
#ifdef __aarch64__
	if (process->user_mappings != 0) {
		arch64_user_unmap_all();
		process->user_mappings = 0;
	}
#endif
	if (process->heap.base != 0 && process->heap.size != 0) {
		(void) memman64_free_4k(&memman64,
			process->heap_backing != 0 ? process->heap_backing :
			process->heap.base, process->heap.size);
		process->heap.base = 0;
		process->heap.size = 0;
		process->heap_backing = 0;
		process->heap_next = 0;
	}
	if (process->stack.base != 0 && process->stack.size != 0) {
		(void) memman64_free_4k(&memman64,
			process->stack_backing != 0 ? process->stack_backing :
			process->stack.base, process->stack.size);
		process->stack.base = 0;
		process->stack.size = 0;
		process->stack_backing = 0;
	}
	/* 이미지는 가장 먼저 얻은 자원이다. 풀 밖의 고정 창이라 memman에
	   돌려주는 대신 마지막에 소유권만 놓는다. */
	elf64_release_process(process);
	process->image.base = 0;
	process->image.size = 0;
}

static void process_cleanup64(struct PROCESS64 *process, struct TASK64 *task)
{
	if (task != NULL && task->process == process) {
		/* A console-less run must not reset the active console's terminal. */
		console64_set_raw_con(process->console, 0);
	}
	/* FAT handles are value copies, with no backend close allocation. Writes
	   already sync before returning, so closing cannot discard dirty cache data. */
	memzero(process->files, sizeof(process->files));
	if (task != NULL && task->process == process) {
		task->process = NULL;
		task->is_user = 0;
		task->kernel_rsp = 0;
	}
	process_free_memory(process);
	memzero(process, sizeof(*process));
}

int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console)
{
	char name[FD64_NAME_MAX];
	struct PROCESS64_ARGS args;
	struct PROCESS64 *process;
	uintptr_t stack;
	uintptr_t heap;
	uintptr_t user_rsp;
	uintptr_t argv;
	struct TASK64 *task = NULL;
	int status;

	status = copy_program_name64(path, name, sizeof(name));
	if (status != 0) {
		return status;
	}
	status = parse_args64(cmdline != NULL ? cmdline : path, &args);
	if (status != 0) {
		return status;
	}
	process = process_alloc();
	if (process == NULL) {
		return -1;
	}
	status = elf64_load_process(name, process);
	if (status != 0) {
		/* -8은 "이미지 창을 다른 앱이 쓰는 중"이다. 콘솔이 여럿이면
		   실제로 일어나므로 뭉개지 않고 그대로 올려보낸다. */
		status = status == -8 ? -8 : -2;
		goto cleanup;
	}
	stack = memman64_alloc_4k(&memman64, USER_STACK_SIZE);
	if (stack == 0) {
		status = -3;
		goto cleanup;
	}
	process->stack.base = stack;
	process->stack.size = USER_STACK_SIZE;
#ifdef __aarch64__
	process->stack_backing = stack;
	process->stack.base = arch64_virt_to_phys(stack);
#endif
	heap = memman64_alloc_4k(&memman64, USER_HEAP_SIZE);
	if (heap == 0) {
		status = -3;
		goto cleanup;
	}
	process->heap.base = heap;
	process->heap.size = USER_HEAP_SIZE;
#ifdef __aarch64__
	process->heap_backing = heap;
	process->heap.base = arch64_virt_to_phys(heap);
#endif
	process->heap_next = heap;
#ifdef __aarch64__
	process->heap_next = process->heap.base;
#endif
	process->console = console;
	status = setup_args64(process, &args, &user_rsp, &argv);
	if (status != 0) {
		goto cleanup;
	}
#ifdef __aarch64__
	/* Mapping can fail after publishing part of the shared translation tree. */
	process->user_mappings = 1;
	if (arch64_user_map_range(process->image.base, process->image.size, 1) != 0 ||
			arch64_user_map_range(process->stack.base,
			process->stack.size, 0) != 0 ||
			arch64_user_map_range(process->heap.base,
			process->heap.size, 0) != 0) {
		status = -5;
		goto cleanup;
	}
#endif
	task = task_now64();
	if (task == NULL) {
		/* 프로세스 소유자는 태스크다. 태스크가 없으면 syscall이 자기
		   프로세스를 찾을 수 없으므로 진입 자체를 막는다. */
		status = -4;
		goto cleanup;
	}
	task->process = process;
	task->is_user = 1;
	status = enter_user_mode64(process->entry, user_rsp,
		args.argc, argv, GDT64_USER_CODE, GDT64_USER_DATA, &process->saved_kernel_rsp);
	task->kernel_rsp = process->saved_kernel_rsp;
	if (process->exited != 0) {
		status = process->exit_status;
	}

cleanup:
	process_cleanup64(process, task);
	return status;
}
