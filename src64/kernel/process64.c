#include <asmfunc64.h>
#include <dsctbl64.h>
#include <elf64_loader.h>
#include <memory64.h>
#include <mtask64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>

#define USER_STACK_SIZE (64 * 1024)
#define USER_HEAP_SIZE  (256 * 1024)

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

static int range_contains(const struct PROCESS64_RANGE *range, uintptr_t ptr, size_t size)
{
	uintptr_t end;

	if (size == 0) {
		return 1;
	}
	end = ptr + size;
	return ptr >= range->base && end >= ptr && end <= range->base + range->size;
}

/* 실행 중인 프로세스는 태스크마다 다르다. 콘솔이 여러 개면 전역 하나로는
   서로의 saved_kernel_rsp를 덮어쓴다 (console_plan.md 블로커 2). */
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
	return range_contains(&process->image, p, size) != 0 ||
		range_contains(&process->stack, p, size) != 0 ||
		range_contains(&process->heap, p, size) != 0;
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

/* returns the initial user rsp: below the argv block, so the app's own
   frames cannot overwrite its arguments. */
static uintptr_t setup_args(struct PROCESS64 *process, const char *cmdline, uint64_t *argc_out, uintptr_t *argv_out)
{
	uintptr_t sp;
	uintptr_t argv[PROCESS64_MAX_ARGS];
	uint64_t argc;
	size_t len;
	const char *p;
	const char *start;
	char *dst;
	uint64_t i;

	sp = process->stack.base + process->stack.size;
	argc = 0;
	p = cmdline;
	while (*p != '\0' && argc < PROCESS64_MAX_ARGS) {
		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		start = p;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
		len = (size_t) (p - start);
		sp -= len + 1;
		dst = (char *) sp;
		copy_bytes(dst, start, len);
		dst[len] = '\0';
		argv[argc++] = sp;
	}
	sp &= ~(uintptr_t) 0x0f;
	sp -= ((argc + 1) * sizeof(uintptr_t) + 15) & ~(uintptr_t) 15;
	for (i = 0; i < argc; i++) {
		((uintptr_t *) sp)[i] = argv[i];
	}
	((uintptr_t *) sp)[argc] = 0;
	*argc_out = argc;
	*argv_out = sp;
	return sp;
}

static void process_free_memory(struct PROCESS64 *process)
{
	/* 이미지는 풀 밖의 고정 창이라 memman에 돌려주는 게 아니라
	   소유권만 놓는다 (console_plan.md 1.5단계). */
	elf64_release_process(process);
	if (process->stack.base != 0 && process->stack.size != 0) {
		memman64_free_4k(&memman64, process->stack.base, process->stack.size);
	}
	if (process->heap.base != 0 && process->heap.size != 0) {
		memman64_free_4k(&memman64, process->heap.base, process->heap.size);
	}
}

int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console)
{
	char name[16];
	size_t name_len;
	struct PROCESS64 *process;
	uintptr_t stack;
	uintptr_t heap;
	uintptr_t user_rsp;
	uint64_t argc;
	uintptr_t argv;
	struct TASK64 *task;
	int status;

	/* "cat test.txt" arrives as one string: the executable is the first
	   token, the rest is argv for setup_args(). */
	for (name_len = 0; name_len < sizeof(name) - 1; name_len++) {
		if (path[name_len] == '\0' || path[name_len] == ' ') {
			break;
		}
		name[name_len] = path[name_len];
	}
	name[name_len] = '\0';
	process = process_alloc();
	if (process == NULL) {
		return -1;
	}
	if (elf64_load_process(name, process) != 0) {
		process->pid = 0;
		return -2;
	}
	stack = memman64_alloc_4k(&memman64, USER_STACK_SIZE);
	heap = memman64_alloc_4k(&memman64, USER_HEAP_SIZE);
	if (stack == 0 || heap == 0) {
		process_free_memory(process);
		process->pid = 0;
		return -3;
	}
	process->stack.base = stack;
	process->stack.size = USER_STACK_SIZE;
	process->heap.base = heap;
	process->heap.size = USER_HEAP_SIZE;
	process->heap_next = heap;
	process->console = console;
	user_rsp = setup_args(process, cmdline != NULL ? cmdline : path, &argc, &argv);
	task = task_now64();
	if (task == NULL) {
		/* 프로세스 소유자는 태스크다. 태스크가 없으면 syscall이 자기
		   프로세스를 찾을 수 없으므로 진입 자체를 막는다. */
		process_free_memory(process);
		process->pid = 0;
		return -4;
	}
	task->process = process;
	task->is_user = 1;
	status = enter_user_mode64(process->entry, user_rsp,
		argc, argv, GDT64_USER_CODE, GDT64_USER_DATA, &process->saved_kernel_rsp);
	task->kernel_rsp = process->saved_kernel_rsp;
	if (process->exited != 0) {
		status = process->exit_status;
	}
	task->process = NULL;
	task->is_user = 0;
	task->kernel_rsp = 0;
	process_free_memory(process);
	process->pid = 0;
	return status;
}
