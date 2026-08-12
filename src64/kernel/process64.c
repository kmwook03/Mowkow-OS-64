#include <asmfunc64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <elf64_loader.h>
#include <memory64.h>
#include <mtask64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 실측으로 정한 값 (roadmap64.md Phase 4 step 7). report_usage가 프로세스마다
 * COM1에 실제 사용량을 찍는다.
 *
 * 힙: 나노의 사용량은 파일 크기가 아니라 줄 수를 따라간다 - 줄마다 최소
 * 64바이트다. 8 KiB짜리 4000줄 파일이 256 KiB의 98.5%를 먹었다. 1 MiB면
 * 같은 파일이 25% 언저리고, 64 KiB(MAX_FILE) 소스 파일도 편집할 여유가 남는다.
 * 스택: 나노가 472바이트, 다른 앱은 그보다 적게 썼다. 64 KiB는 135배 여유라
 * 줄일 이유가 없어 그대로 둔다. 페이즈 1이 여기에 가드 페이지를 붙인다.
 */
#define USER_STACK_SIZE (64 * 1024)
#define USER_HEAP_SIZE  (1024 * 1024)

static struct PROCESS64 process_table[4];
static uint32_t next_pid = 1;
static struct PROCESS64 *current_process;

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

struct PROCESS64 *process64_current(void)
{
	return current_process;
}

int process64_user_range_valid(const void *ptr, size_t size)
{
	uintptr_t p;

	if (current_process == NULL || ptr == NULL) {
		return 0;
	}
	p = (uintptr_t) ptr;
	return range_contains(&current_process->image, p, size) != 0 ||
		range_contains(&current_process->stack, p, size) != 0 ||
		range_contains(&current_process->heap, p, size) != 0;
}

void process64_exit_current(int status)
{
	if (current_process == NULL) {
		return;
	}
	current_process->exited = 1;
	current_process->exit_status = status;
	/* raw 모드는 프로세스 상태다. 앱이 정리하지 않고 나가도 콘솔이 다시
	   줄 편집기로 돌아오게 커널이 되돌린다. */
	console64_set_raw(0);
}

uintptr_t process64_current_exit_rsp(void)
{
	return current_process != NULL ? current_process->saved_kernel_rsp : 0;
}

int process64_current_exit_status(void)
{
	return current_process != NULL ? current_process->exit_status : -1;
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
	/* image는 memman64가 아니라 고정 유저 이미지 창에서 온다(elf64_loader.c).
	   memman64에 돌려주면 커널 힙이 그 창을 나눠주게 되므로 건드리지 않는다. */
	if (process->stack.base != 0 && process->stack.size != 0) {
		memman64_free_4k(&memman64, process->stack.base, process->stack.size);
	}
	if (process->heap.base != 0 && process->heap.size != 0) {
		memman64_free_4k(&memman64, process->heap.base, process->heap.size);
	}
}

/*
 * 스택과 힙을 얼마나 썼는지 COM1으로 알린다.
 *
 * 힙은 커널이 정확히 안다: SYS_ALLOC이 범프라 heap_next - heap.base가 곧
 * 최대 사용량이다. 스택은 알 수 없으므로 들어가기 전에 무늬를 칠해 두고
 * 나올 때 어디까지 지워졌는지 본다.
 * USER_STACK_SIZE와 USER_HEAP_SIZE를 짐작이 아니라 실측으로 정하기 위한 것.
 */
#define STACK_PAINT 0xa5

static void stack_paint(struct PROCESS64 *process)
{
	uint8_t *p;
	size_t i;

	p = (uint8_t *) process->stack.base;
	for (i = 0; i < process->stack.size; i++) {
		p[i] = STACK_PAINT;
	}
}

static size_t stack_used(const struct PROCESS64 *process)
{
	const uint8_t *p;
	size_t i;

	p = (const uint8_t *) process->stack.base;
	for (i = 0; i < process->stack.size; i++) {
		if (p[i] != STACK_PAINT) {
			break;                  /* 스택은 위에서 아래로 자란다 */
		}
	}
	return process->stack.size - i;
}

static void serial_out(char c)
{
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

static void serial_dec(uint64_t v)
{
	char buf[20];
	int i;

	if (v == 0) {
		serial_out('0');
		return;
	}
	i = 0;
	while (v != 0 && i < 20) {
		buf[i++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (i > 0) {
		serial_out(buf[--i]);
	}
}

static void serial_text(const char *s)
{
	while (*s != '\0') {
		serial_out(*s++);
	}
}

static void report_usage(const struct PROCESS64 *process)
{
	serial_text("proc usage heap=");
	serial_dec(process->heap_next - process->heap.base);
	serial_text("/");
	serial_dec(process->heap.size);
	serial_text(" stack=");
	serial_dec(stack_used(process));
	serial_text("/");
	serial_dec(process->stack.size);
	serial_text("\r\n");
}

int process64_exec_file(const char *path, const char *cmdline)
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
	stack_paint(process);
	user_rsp = setup_args(process, cmdline != NULL ? cmdline : path, &argc, &argv);
	current_process = process;
	task = task_now64();
	if (task != NULL) {
		task->process = process;
		task->is_user = 1;
	}
	status = enter_user_mode64(process->entry, user_rsp,
		argc, argv, GDT64_USER_CODE, GDT64_USER_DATA, &process->saved_kernel_rsp);
	if (task != NULL) {
		task->kernel_rsp = process->saved_kernel_rsp;
	}
	if (process->exited != 0) {
		status = process->exit_status;
	}
	if (task != NULL) {
		task->process = NULL;
		task->is_user = 0;
		task->kernel_rsp = 0;
	}
	current_process = NULL;
	report_usage(process);
	process_free_memory(process);
	process->pid = 0;
	return status;
}
