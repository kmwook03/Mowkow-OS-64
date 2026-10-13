#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpport_session64_mock.h"
#include "../src64/mpport/mpport_main.c"

struct MPPORT_SESSION_CTX64 mp_state_ctx;
struct MEMMAN64 memman64;
uint8_t stack_bottom[1];
int mp_plat_print;
const mp_obj_type_t mp_type_KeyboardInterrupt = { 1 };
const mp_obj_type_t mp_type_SystemExit = { 2 };
static const mp_obj_type_t ordinary_type64 = { 3 };
static const mp_obj_type_t reporting_type64 = { 4 };

static int irq64, claims64, releases64, init_calls64, gc_calls64, stack_calls64;
static int allocate_calls64, free_calls64, heap_oom64, free_failure64;
static int action_calls64, parse_calls64, compile_calls64, call_calls64, prints64;
static int console64, repl64[2], repl_toggles64, nested64, rejecting64;
static int fault_stage64, throw_kind64, print_fault64, outer_cleanups64;
static uintptr_t *vm_roots64, *mem_roots64;
static struct TASK64 task64;
static int use_task64 = 1;
static char log64[8192];
static size_t log_used64;
static struct MPPORT_SESSION_LEXER64 lexer64 = { 1 };
static mp_state_thread_t initial_thread64;

static void raise64(int kind)
{
	const mp_obj_type_t *type = kind == 1 ? &mp_type_KeyboardInterrupt :
		(kind == 2 ? &mp_type_SystemExit : &ordinary_type64);

	nlr_jump((void *) type);
}

static void fault64(int stage)
{
	assert(mp_busy == 1 && irq64 == 0);
	if (fault_stage64 == stage) { raise64(throw_kind64); }
}

uint64_t platform_irq_save64(void)
{
	int previous = irq64;

	irq64 = 1;
	return (uint64_t) previous;
}

void platform_irq_restore64(uint64_t flags)
{
	assert(irq64 == 1);
	if (mp_busy == 1) { claims64++; } else { releases64++; }
	irq64 = (int) flags;
}

void console64_puts(const char *message)
{
	assert(irq64 == 0);
	size_t size = strlen(message);

	assert(size < sizeof(log64) - log_used64);
	memcpy(log64 + log_used64, message, size + 1);
	log_used64 += size;
}

void console64_repl_set_active(int active)
{
	assert(mp_busy == 1 && irq64 == 0 && console64 == 0);
	repl64[console64] = active;
	repl_toggles64++;
}

struct TASK64 *task_now64(void) { return use_task64 ? &task64 : NULL; }

void mp_cstack_init_with_top(void *top, size_t size)
{
	assert(size > MICROPY_STACK_CHECK_MARGIN);
	stack_calls64++;
	mp_state_ctx.thread.stack_top = top;
	mp_state_ctx.thread.stack_limit = size - MICROPY_STACK_CHECK_MARGIN;
	fault64(1);
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	assert(man == &memman64 && size == MPPORT_GC_HEAP_SIZE);
	allocate_calls64++;
	if (heap_oom64) { return 0; }
	void *buffer = malloc(size);

	assert(buffer != NULL);
	return (uintptr_t) buffer;
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t ptr, size_t size)
{
	assert(man == &memman64 && size == MPPORT_GC_HEAP_SIZE && mp_busy == 1);
	for (size_t i = 0; i < 8; i++) {
		assert(mp_state_ctx.vm.roots[i] == 0 && mp_state_ctx.mem.roots[i] == 0);
	}
	assert(memcmp(&mp_state_ctx.thread, &initial_thread64, sizeof(initial_thread64)) == 0);
	free_calls64++;
	if (free_failure64) { return -12; }
	free((void *) ptr);
	return 0;
}

void gc_init(void *start, void *end)
{
	assert(start != NULL && (uint8_t *) end - (uint8_t *) start == MPPORT_GC_HEAP_SIZE);
	gc_calls64++;
	vm_roots64 = mp_state_ctx.vm.roots;
	mem_roots64 = mp_state_ctx.mem.roots;
	vm_roots64[0] = (uintptr_t) start;
	mem_roots64[0] = (uintptr_t) end;
	mp_state_ctx.thread.dict_globals = start;
	mp_state_ctx.thread.gc_lock_depth = 1;
	fault64(2);
}

void mp_init(void)
{
	init_calls64++;
	mp_state_ctx.thread.dict_locals = mem_roots64;
	vm_roots64[1] = 123;
	mem_roots64[1] = 456;
	fault64(3);
	mp_state_ctx.thread.gc_lock_depth = 0;
}

static void reject_nested64(void)
{
	if (!nested64 || rejecting64) { return; }
	int before_actions = action_calls64;
	int before_stacks = stack_calls64;
	int before_toggles = repl_toggles64;
	const char *arg = mpport_argv();
	char saved_arg[FD64_NAME_MAX];

	if (arg != NULL) { assert(str_copy64(saved_arg, sizeof(saved_arg), arg) == 0); }
	rejecting64 = 1;
	console64 = 1;
	mpport_repl();
	mpport_run_file("foreign.py");
	mpport_run_mowkow("foreign.mk");
	console64 = 0;
	rejecting64 = 0;
	assert(mp_busy == 1 && action_calls64 == before_actions && stack_calls64 == before_stacks);
	assert(repl_toggles64 == before_toggles && repl64[1] == 0);
	assert(arg == NULL ? mpport_argv() == NULL : strcmp(mpport_argv(), saved_arg) == 0);
}

int pyexec_friendly_repl(void)
{
	assert(repl64[0] == 1 && mpport_argv() == NULL);
	action_calls64++;
	reject_nested64();
	fault64(4);
	return 0;
}

mp_lexer_t *mpport_file_lexer64(const char *path)
{
	assert(strcmp(path, "file.py") == 0 && mpport_argv() == NULL && repl64[0] == 0);
	action_calls64++;
	reject_nested64();
	fault64(4);
	return fault_stage64 == 8 ? NULL : &lexer64;
}

qstr qstr_from_str(const char *source)
{
	assert(strcmp(source, "머꼬") == 0 && repl64[0] == 0);
	fault64(4);
	return 1;
}

mp_lexer_t *mp_lexer_new_from_str_len(qstr source, const char *text,
	size_t size, size_t free_size)
{
	assert(source == 1 && size == strlen(text) && free_size == 0);
	assert(strcmp(text, "import mowkow\nmowkow.main()\n") == 0);
	assert(mpport_argv() != NULL && strcmp(mpport_argv(), "file.mk") == 0);
	action_calls64++;
	reject_nested64();
	fault64(4);
	return &lexer64;
}

mp_parse_tree_t mp_parse(mp_lexer_t *lex, int mode)
{
	assert(lex == &lexer64 && mode == MP_PARSE_FILE_INPUT);
	parse_calls64++;
	fault64(5);
	return (mp_parse_tree_t) { 1 };
}

mp_obj_t mp_compile(mp_parse_tree_t *tree, qstr source, bool repl)
{
	assert(tree->root == 1 && source == 1 && !repl);
	assert((uintptr_t) (tree + 1) <= (uintptr_t) mp_state_ctx.thread.stack_top);
	compile_calls64++;
	fault64(6);
	return &lexer64;
}

mp_obj_t mp_call_function_0(mp_obj_t function)
{
	assert(function == &lexer64);
	call_calls64++;
	fault64(7);
	return NULL;
}

const mp_obj_type_t *mp_obj_get_type(mp_obj_t value)
{
	assert(mp_busy == 1 && irq64 == 0);
	return value;
}

void mp_obj_print_exception(const void *print, mp_obj_t value)
{
	assert(print == &mp_plat_print && value != NULL && mp_busy == 1 && irq64 == 0);
	prints64++;
	reject_nested64();
	if (print_fault64) { nlr_jump((void *) &reporting_type64); }
}

void gc_collect_start(void) { }
void gc_collect_end(void) { }
void gc_helper_collect_regs_and_stack(void) { }
int fd64_open(struct FDHANDLE64 *fh, const char *name) { (void) fh; (void) name; return 0; }

static void discard_heap64(void)
{
#ifdef __aarch64__
	free(gc_heap);
	gc_heap = NULL;
#endif
}

static void reset64(void)
{
	assert(mp_busy == 0 && mpport_argv() == NULL && repl64[0] == 0 && repl64[1] == 0);
	assert(mp_state_ctx.thread.nlr_top == NULL && mp_state_ctx.thread.nlr_jump_callback_top == NULL);
	discard_heap64();
	mp_state_ctx = (struct MPPORT_SESSION_CTX64) { 0 };
	initial_thread64 = mp_state_ctx.thread;
	mp_ready = 0;
	irq64 = claims64 = releases64 = init_calls64 = gc_calls64 = stack_calls64 = 0;
	allocate_calls64 = free_calls64 = heap_oom64 = free_failure64 = 0;
	action_calls64 = parse_calls64 = compile_calls64 = call_calls64 = prints64 = 0;
	console64 = nested64 = rejecting64 = repl_toggles64 = 0;
	fault_stage64 = throw_kind64 = print_fault64 = 0;
	log_used64 = 0;
	log64[0] = '\0';
	use_task64 = 1;
	/* Place a synthetic usable range around the host's current stack. */
	uintptr_t here = (uintptr_t) &here;

	task64 = (struct TASK64) {
		.stack_usable_base = here - 32768, .stack_usable_size = 65536
	};
}

static void run64(int mode)
{
	if (mode == 0) { mpport_repl(); }
	if (mode == 1) { mpport_run_file("file.py"); }
	if (mode == 2) { mpport_run_mowkow("file.mk"); }
	assert(mp_busy == 0 && mpport_argv() == NULL && repl64[0] == 0 && repl64[1] == 0);
	assert(irq64 == 0 && releases64 > 0);
	assert(mp_state_ctx.thread.nlr_top == NULL && mp_state_ctx.thread.nlr_jump_callback_top == NULL);
}

static void paths64(void)
{
	for (int mode = 0; mode < 3; mode++) {
		reset64();
		nested64 = 1;
		run64(mode);
		assert(mp_ready == 1 && init_calls64 == 1 && gc_calls64 == 1);
		assert(strstr(log64, "already running") != NULL);
		assert(repl_toggles64 == (mode == 0 ? 2 : 0));
		run64(mode);
		assert(init_calls64 == 1 && gc_calls64 == 1 && action_calls64 == 2);
		assert(stack_calls64 == 2 && mp_state_ctx.vm.roots[1] == 123);
		for (int stage = 1; stage <= 7; stage++) {
			if (mode == 0 && stage > 4) { continue; }
			reset64();
			fault_stage64 = stage;
			run64(mode);
			if (stage <= 3) {
				assert(mp_ready == 0 && action_calls64 == 0 && prints64 == 0);
				assert(memcmp(&mp_state_ctx.thread, &initial_thread64,
					sizeof(initial_thread64)) == 0);
				if (stage >= 2) {
					for (size_t i = 0; i < 8; i++) {
						assert(mp_state_ctx.vm.roots[i] == 0);
						assert(mp_state_ctx.mem.roots[i] == 0);
					}
#ifdef __aarch64__
					assert(gc_heap == NULL && free_calls64 == 1);
#endif
				}
			} else { assert(mp_ready == 1 && prints64 == 1); }
			fault_stage64 = 0;
			run64(mode);
			assert(mp_ready == 1);
		}
	}
	for (int mode = 0; mode < 3; mode++) {
		for (int kind = 1; kind <= 2; kind++) {
			reset64();
			fault_stage64 = 4;
			throw_kind64 = kind;
			run64(mode);
			assert(prints64 == (mode == 2 ? 0 : 1));
		}
	}
	reset64();
	fault_stage64 = 8;
	run64(1);
	assert(strstr(log64, "file not found") != NULL && call_calls64 == 0);
	for (int mode = 0; mode < 3; mode++) {
		reset64();
		nested64 = 1;
		fault_stage64 = 4;
		print_fault64 = 1;
		run64(mode);
		assert(prints64 == 1 && strstr(log64, "cannot print exception") != NULL);
		fault_stage64 = print_fault64 = 0;
		run64(1);
	}
}

static void platform_failures64(void)
{
	reset64();
	task64.stack_usable_base = UINTPTR_MAX;
	run64(0);
	assert(stack_calls64 == 0 && init_calls64 == 0 && strstr(log64, "invalid stack") != NULL);
	reset64();
	task64.stack_usable_size = 0;
	run64(1);
	assert(stack_calls64 == 0 && init_calls64 == 0);
	reset64();
	uintptr_t near_stack = (uintptr_t) &near_stack;

	task64.stack_usable_base = near_stack - 4096;
	run64(2);
	assert(stack_calls64 == 0 && init_calls64 == 0);
	reset64();
	use_task64 = 0;
	run64(1);
	assert(stack_calls64 == 1 && mp_ready == 1);
#ifdef __aarch64__
	for (int mode = 0; mode < 3; mode++) {
		reset64();
		heap_oom64 = 1;
		run64(mode);
		assert(mp_ready == 0 && allocate_calls64 == 1 && free_calls64 == 0);
		assert(strstr(log64, "cannot allocate GC heap") != NULL);
		heap_oom64 = 0;
		run64(mode);
		assert(mp_ready == 1 && allocate_calls64 == 2);
		reset64();
		fault_stage64 = 3;
		free_failure64 = 1;
		run64(mode);
		uint8_t *retained = gc_heap;

		assert(retained != NULL && mp_ready == 0 && free_calls64 == 1);
		assert(strstr(log64, "retained for retry") != NULL);
		fault_stage64 = free_failure64 = 0;
		run64(mode);
		assert(mp_ready == 1 && allocate_calls64 == 1 && gc_heap == retained);
	}
#endif
}

static void outer_callback64(void *context) { (void) context; outer_cleanups64++; }

static void invoke_failed64(void) { mpport_run_file("file.py"); }

static void outer_state64(void)
{
	reset64();
	nlr_buf_t outer;
	nlr_jump_callback_node_t callback;

	if (nlr_push(&outer) != 0) { abort(); }
	nlr_push_jump_callback(&callback, outer_callback64);
	mp_state_ctx.thread.mp_pending_exception = &lexer64;
	initial_thread64 = mp_state_ctx.thread;
	fault_stage64 = 3;
	invoke_failed64();
	assert(memcmp(&mp_state_ctx.thread, &initial_thread64, sizeof(initial_thread64)) == 0);
	assert(mp_busy == 0 && outer_cleanups64 == 0);
	nlr_pop_jump_callback(true);
	nlr_pop();
	assert(outer_cleanups64 == 1);
	fault_stage64 = 0;
	run64(1);
}

int main(void)
{
	paths64();
	platform_failures64();
	outer_state64();
	reset64();
	puts("MicroPython session claim, initialization rollback, NLR and console cleanup: PASS");
	return 0;
}
