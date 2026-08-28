/*
 * mpport_main.c -- 커널에 넣은 MicroPython의 시작과 끝
 *
 * GC 힙, 스택 한계, REPL과 스크립트 실행 진입점이 여기 있다. 파일 시스템과
 * 콘솔로 이어 주는 부분은 mphalport.c에 있다.
 */
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
 * 짐작이 아니라 실측으로 정한 크기다(Stage 4, python_porting.md). 재귀
 * (fib(15)), 작은 리스트 200개가 든 딕셔너리, 원소 300개짜리 리스트 표기,
 * 클래스 인스턴스 100개, 문자열 잇기, 예외 처리를 섞은 스크립트를 돌렸더니
 * 예전 256KiB 힙에서 최대 153KiB쯤 썼다(gc.collect() 앞뒤의 gc.mem_free()
 * 차이). 512KiB면 그 최댓값의 세 배쯤 여유가 있고, python_porting.md
 * Stage 0.6이 제안한 512KiB~1MiB 범위와도 맞는다. 처음에 그보다 적게 잡았던
 * 이유는 .bss 여유 때문이었는데, 그 문제는 MEMMAN64_EARLY_START를 함께
 * 올리면서 풀렸다(memory64.h).
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

	size = fh.info.size;
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
 * 링크된 코어(py/lexer.h)가 요구한다. MICROPY_READER_POSIX와
 * MICROPY_READER_VFS가 둘 다 꺼져 있으므로(Stage 1.3) 포트가 직접 줘야
 * 한다. fd64로 읽는 mp_reader는 아직 없다(Stage 3에서 만든다). 업스트림
 * ports/minimal/main.c와 같은 선택으로, 잘못된 lexer를 조용히 돌려주는
 * 대신 ENOENT를 낸다.
 */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	(void) filename;
	mp_raise_OSError(MP_ENOENT);
}

/*
 * MICROPY_VFS가 꺼져 있으므로 링크된 코어(py/builtin.h)가 요구한다. 파일
 * 시스템을 쓰는 import는 아직 없고(어차피 Stage 1.3에서
 * MICROPY_ENABLE_EXTERNAL_IMPORT도 꺼 두었다) 늘 "없음"이라고 답한다.
 * 업스트림 ports/minimal/main.c와 같다.
 */
mp_import_stat_t mp_import_stat(const char *path)
{
	(void) path;
	return MP_IMPORT_STAT_NO_EXIST;
}

/*
 * REPL을 쓰든 안 쓰든 링크된 코어(py/gc.c)가 요구한다.
 * gc_helper_collect_regs_and_stack()는 업스트림 그대로 가져다 쓴다
 * (shared/runtime/gchelper_generic.c). x86_64용 레지스터 저장 경로가 이미
 * 있어 아키텍처별 어셈블리가 필요 없다. 위의 mp_cstack_init_with_sp_here()
 * 가 설정하는 MP_STATE_THREAD(stack_top)에 기댄다.
 */
void gc_collect(void)
{
	gc_collect_start();
	gc_helper_collect_regs_and_stack();
	gc_collect_end();
}

/*
 * 링크된 코어(py/nlr.c)가 요구한다. 뛰어갈 NLR 문맥이 없는데 nlr_jump()에
 * 닿았을 때만 불린다. 보통의 파이썬 예외는 늘 문맥이 있으므로, 이건
 * MicroPython 내부의 버그다. 되살릴 방법이 없으니 망가진 상태로 계속
 * 가느니 멈춘다.
 */
void nlr_jump_fail(void *val)
{
	(void) val;
	console64_puts("MicroPython: fatal uncaught exception, halting\n");
	for (;;) {
	}
}

