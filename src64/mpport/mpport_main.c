#include <mpport64.h>
#include <mtask64.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mpstate.h"
#include "py/parse.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

#include <asmfunc64.h>
#include <console64.h>
#include <fd64.h>
#include <memory64.h>
#include <stdint.h>

/*
 * Right-sized (Stage 4, python_porting.md) from a real measured workload
 * rather than the Stage 0.6 guess: a synthetic script mixing recursion
 * (fib(15)), a 200-entry dict of small lists, a 300-element list
 * comprehension, 100 class instances, string-building, and exception
 * handling peaked at ~153KiB of the old 256KiB heap (gc.mem_free() before
 * vs after gc.collect()). 512KiB gives ~3x headroom over that peak,
 * matching the original 512KiB-1MiB range python_porting.md's Stage 0.6
 * suggested (this port under-shot it for .bss-headroom reasons that
 * MEMMAN64_EARLY_START being raised alongside this, memory64.h, resolves).
 */
#define MPPORT_GC_HEAP_SIZE (512 * 1024)

extern uint8_t stack_bottom[];

static uint8_t gc_heap[MPPORT_GC_HEAP_SIZE];

/*
 * MicroPython은 태생적으로 단일 인스턴스다: gc_heap도 하나, mp_state_ctx도
 * 하나(전역). 콘솔마다 py를 돌리려면 콘솔 수만큼의 512KiB .bss에 더해,
 * MP_STATE_VM이 바꿔 낄 수 있는 포인터를 거치도록 벤더링된 MicroPython 자체를
 * 고쳐야 한다. third_party/micropython은 v1.28.0에 고정된 서브미듈이라 로컬
 * 패치는 새로 clone하면 사라진다 -- 5단계의 파서 재귀 검사를 포기한 것과 같은
 * 이유다. 그래서 console_plan.md 8단계가 제시한 싼 쪽을 택한다: 시스템 전체에서
 * 한 번에 하나, 두 번째 콘솔은 거절.
 */
static int mp_busy;

static int mpport_claim(void)
{
	uint64_t flags;

	flags = io_load_rflags();
	io_cli();
	if (mp_busy != 0) {
		io_store_rflags(flags);
		/* console64_puts는 도는 태스크로 콘솔을 찾으므로 거절 메시지는
		   거절당한 콘솔에 찍힌다 (5단계). */
		console64_puts("py already running in another console\n");
		return -1;
	}
	mp_busy = 1;
	io_store_rflags(flags);
	return 0;
}

static void mpport_release(void)
{
	mp_busy = 0;
}

static void mpport_init(void)
{
	int here;
	size_t available;
	struct TASK64 *task;

	/*
	 * Real remaining C stack from here down to stack_bottom (asmfunc64.asm),
	 * not a guessed constant -- matches python_porting.md Stage 0.4/0.6's
	 * "conservative bound computed from the known stack range."
	 */
	/*
	 * 콘솔이 자기 태스크에서 돌면 (console_plan.md 5단계) 그 스택은
	 * memman64가 준 64KiB지 커널 메인 스택이 아니다. stack_bottom으로
	 * 재면 몇 MiB가 남은 줄 알고 넘침 검사가 걸리지 않아, 깊은 재귀가
	 * 조용히 태스크 스택을 뭉갠다.
	 */
	task = task_now64();
	if (task != NULL && task->stack_base != 0) {
		available = (size_t) ((uintptr_t) &here - task->stack_base);
	} else {
		available = (size_t) ((uintptr_t) &here - (uintptr_t) stack_bottom);
	}
	mp_cstack_init_with_sp_here(available);
	gc_init(gc_heap, gc_heap + sizeof(gc_heap));
	mp_init();
}

void mpport_repl(void)
{
	if (mpport_claim() != 0) {
		return;
	}
	mpport_init();

	console64_repl_set_active(1);
	pyexec_friendly_repl();
	console64_repl_set_active(0);

	mp_deinit();
	mpport_release();
}

void mpport_run_file(const char *path)
{
	struct FDHANDLE64 fh;
	uintptr_t buf_addr;
	size_t size;
	nlr_buf_t nlr;

	if (fd64_open(&fh, path) == 0) {
		console64_puts("file not found\n");
		return;
	}

	size = fh.finfo->size;
	buf_addr = memman64_alloc_4k(&memman64, size);
	if (buf_addr == 0) {
		console64_puts("out of memory\n");
		return;
	}
	fd64_read(&fh, (void *) buf_addr, size);

	if (mpport_claim() != 0) {
		memman64_free_4k(&memman64, buf_addr, size);
		return;
	}
	mpport_init();

	if (nlr_push(&nlr) == 0) {
		mp_reader_t reader;
		mp_lexer_t *lex;
		qstr source_name;
		mp_parse_tree_t parse_tree;
		mp_obj_t module_fun;

		mp_reader_new_mem(&reader, (const uint8_t *) buf_addr, size, 0);
		lex = mp_lexer_new(qstr_from_str(path), reader);
		source_name = lex->source_name;
		parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
		module_fun = mp_compile(&parse_tree, source_name, false);
		mp_call_function_0(module_fun);
		nlr_pop();
	} else {
		mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
	}

	mp_deinit();
	mpport_release();
	memman64_free_4k(&memman64, buf_addr, size);
}

/*
 * Required by the linked-in core (py/lexer.h): with MICROPY_READER_POSIX
 * and MICROPY_READER_VFS both off (Stage 1.3), the port must supply this.
 * No fd64-backed mp_reader exists yet (Stage 3's job) -- matches upstream's
 * own ports/minimal/main.c reference for exactly this situation: raise
 * ENOENT rather than silently returning an invalid lexer.
 */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	(void) filename;
	mp_raise_OSError(MP_ENOENT);
}

/*
 * Required by the linked-in core (py/builtin.h) since MICROPY_VFS is off.
 * No filesystem-backed import exists yet (and MICROPY_ENABLE_EXTERNAL_IMPORT
 * is off per Stage 1.3 regardless) -- always report "not found", matching
 * upstream's own ports/minimal/main.c reference.
 */
mp_import_stat_t mp_import_stat(const char *path)
{
	(void) path;
	return MP_IMPORT_STAT_NO_EXIST;
}

/*
 * Required by the linked-in core (py/gc.c) regardless of whether the REPL
 * is active yet. gc_helper_collect_regs_and_stack() is vendored as-is
 * (shared/runtime/gchelper_generic.c) -- has a real x86_64 register-capture
 * path needing no arch-specific asm. Relies on MP_STATE_THREAD(stack_top),
 * which mp_cstack_init_with_sp_here() sets above.
 */
void gc_collect(void)
{
	gc_collect_start();
	gc_helper_collect_regs_and_stack();
	gc_collect_end();
}

/*
 * Required by the linked-in core (py/nlr.c): called only if nlr_jump() is
 * reached with no active NLR context to jump to -- an internal MicroPython
 * bug, not a normal Python exception (those always have a context). No
 * recovery is possible; halt rather than continue with corrupted state.
 */
void nlr_jump_fail(void *val)
{
	(void) val;
	console64_puts("MicroPython: fatal uncaught exception, halting\n");
	for (;;) {
	}
}

