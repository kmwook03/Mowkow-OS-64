/*
 * mpport_main.c -- 커널에 넣은 MicroPython의 시작과 끝
 *
 * GC 힙, 스택 한계, REPL과 스크립트 실행 진입점이 여기 있다. 파일 시스템과
 * 콘솔은 mphalport.c, GC 파일 버퍼는 mpport_file64.c가 이어 준다.
 */
#include <mpport64.h>
#include <mtask64.h>

#include "mpport_file_internal64.h"

#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mpstate.h"
#include "py/parse.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

#include <arch/platform64.h>
#include <console64.h>
#include <fd64.h>
#include <memory64.h>
#include <string_copy64.h>
#include <stdint.h>
#include <string.h>

/*
 * library_kor.scm까지 장기 유지하려면 4 MiB가 필요하다. x86_64는 기존 계약대로
 * 정적 .bss를 쓰고, AArch64는 image와 8 MiB early allocator 경계의 여유를 지키기
 * 위해 최초 mp_init 때 memory64에서 같은 크기를 한 번 할당한다.
 */
#define MPPORT_GC_HEAP_SIZE (4 * 1024 * 1024)

extern uint8_t stack_bottom[];

#ifdef __aarch64__
static uint8_t *gc_heap;
#else
static uint8_t gc_heap[MPPORT_GC_HEAP_SIZE];
#endif

/* One persistent VM/GC heap is shared by every console. The claim serializes
 * initialization, execution, exception reporting and session cleanup.
 */
static int mp_busy;

static int mpport_claim64(void)
{
	uint64_t flags;

	flags = platform_irq_save64();
	if (mp_busy != 0) {
		platform_irq_restore64(flags);
		/* console64_puts는 도는 태스크로 콘솔을 찾으므로 거절 메시지는
		   거절당한 콘솔에 찍힌다. */
		console64_puts("py already running in another console\n");
		return -1;
	}
	mp_busy = 1;
	platform_irq_restore64(flags);
	return 0;
}

static void mpport_release64(void)
{
	uint64_t flags = platform_irq_save64();

	mp_busy = 0;
	platform_irq_restore64(flags);
}

/*
 * 콘솔 명령이 스크립트에 넘기는 인자 하나. sys.argv를 켜지 않는다 --
 * 문자열 하나 때문에 그럴 값어치가 없다.
 * 콘솔의 입력 줄은 다음 명령에 덮어씌워지므로 가리키지 않고 베껴 둔다.
 */
static char mp_argv[FD64_NAME_MAX];
static int mp_argv_set;

void mpport_set_argv(const char *arg)
{
	if (arg == NULL) {
		mp_argv_set = 0;
		return;
	}
	/* Preserve the port's bounded-prefix argument convention. */
	(void) str_copy64(mp_argv, sizeof(mp_argv), arg);
	mp_argv_set = 1;
}

const char *mpport_argv(void)
{
	return mp_argv_set != 0 ? mp_argv : NULL;
}

/*
 * C 스택 한계는 부를 때마다 다시 잰다. 콘솔마다 태스크가 따로고 스택도 따로라
 * (console64.c), 다른 콘솔에서 측정한 값을 물려받으면 넘침 검사가 엉뚱한 자리를 본다.
 * mp_init과 달리 이건 되풀이해도 되는 일이다.
 */
static int mpport_stack_limit64(uintptr_t stack_top)
{
	int here;
	size_t available;
	uintptr_t here_address = (uintptr_t) &here;
	struct TASK64 *task;

	/*
	 * 콘솔이 자기 태스크에서 돌면 그 스택은 memman64가 준 64KiB지 커널 메인 스택이 아니다.
	 * stack_bottom으로 측정하면 몇 MiB가 남은 줄 알고 넘침 검사가 걸리지 않아,
	 * 깊은 재귀가 조용히 태스크 스택을 침범한다.
	 */
	task = task_now64();
	if (task != NULL && task->stack_usable_base != 0) {
		if (here_address < task->stack_usable_base ||
			here_address - task->stack_usable_base > task->stack_usable_size) {
			return -22;
		}
		if (stack_top < here_address || stack_top - task->stack_usable_base >
			task->stack_usable_size) {
			return -22;
		}
		available = (size_t) (stack_top - task->stack_usable_base);
	} else {
		if (here_address < (uintptr_t) stack_bottom || stack_top < here_address) {
			return -22;
		}
		available = (size_t) (stack_top - (uintptr_t) stack_bottom);
	}
	if (available <= MICROPY_STACK_CHECK_MARGIN) {
		return -22;
	}
	mp_cstack_init_with_top((void *) stack_top, available);
	return 0;
}

enum MPPORT_MODE64 {
	MPPORT_REPL64,
	MPPORT_FILE64,
	MPPORT_MOWKOW64
};

struct MPPORT_SESSION64 {
	uintptr_t stack_top;
	int init_started;
	int repl_active;
};

/* Successful initialization persists across sessions to retain modules/qstrs.
 * A failed first initialization publishes no VM and can be retried.
 */
static int mp_ready;

static int mpport_init64(volatile struct MPPORT_SESSION64 *session)
{
	if (mpport_stack_limit64(session->stack_top) != 0) {
		console64_puts("MicroPython: invalid stack bounds\n");
		return -22;
	}
	if (mp_ready != 0) {
		return 0;
	}
#ifdef __aarch64__
	/* Retain ownership if a previous failed initialization could not free it. */
	if (gc_heap == NULL) {
		gc_heap = (uint8_t *) memman64_alloc_4k(&memman64, MPPORT_GC_HEAP_SIZE);
		if (gc_heap == NULL) {
			console64_puts("MicroPython: cannot allocate GC heap\n");
			return -12;
		}
	}
#endif
	session->init_started = 1;
	gc_init(gc_heap, gc_heap + MPPORT_GC_HEAP_SIZE);
	mp_init();
	mp_ready = 1;
	return 0;
}

static void mpport_finish64(const volatile struct MPPORT_SESSION64 *session,
	const mp_state_thread_t *thread_before)
{
	if (session->repl_active != 0) {
		console64_repl_set_active(0);
	}
	mpport_set_argv(NULL);
	if (mp_ready == 0) {
		mp_state_ctx.thread = *thread_before;
	}
	if (session->init_started != 0 && mp_ready == 0) {
		/* No port init hooks or external VM resources are configured. Discard
		 * partial GC roots before releasing the heap, while preserving the
		 * caller's outer NLR/callback/thread state captured before our catcher.
		 */
		memset(&mp_state_ctx.vm, 0, sizeof(mp_state_ctx.vm));
		memset(&mp_state_ctx.mem, 0, sizeof(mp_state_ctx.mem));
#ifdef __aarch64__
		if (memman64_free_4k(&memman64, (uintptr_t) gc_heap,
			MPPORT_GC_HEAP_SIZE) == 0) {
			gc_heap = NULL;
		} else {
			console64_puts("MicroPython: cannot free GC heap; retained for retry\n");
		}
#endif
	}
	/* Console/memory cleanup may not raise NLR. Publish availability last. */
	mpport_release64();
}

static void mpport_compile_call64(mp_lexer_t *lex)
{
	qstr source_name = lex->source_name;
	mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
	mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);

	mp_call_function_0(module_fun);
}

/* Keep execution roots below the session anchor even under compiler inlining. */
static __attribute__((noinline)) void mpport_execute64(enum MPPORT_MODE64 mode, const char *arg,
	volatile struct MPPORT_SESSION64 *session)
{
	static const char mowkow_source[] = "import mowkow\nmowkow.main()\n";
	mp_lexer_t *lex;

	if (mode == MPPORT_REPL64) {
		session->repl_active = 1;
		console64_repl_set_active(1);
		(void) pyexec_friendly_repl();
		return;
	}
	if (mode == MPPORT_MOWKOW64) {
		mpport_set_argv(arg);
		lex = mp_lexer_new_from_str_len(qstr_from_str("머꼬"), mowkow_source,
			sizeof(mowkow_source) - 1, 0);
	} else {
		lex = mpport_file_lexer64(arg);
		if (lex == NULL) {
			console64_puts("file not found\n");
			return;
		}
	}
	mpport_compile_call64(lex);
}

static void mpport_report64(enum MPPORT_MODE64 mode, void *exception)
{
	nlr_buf_t nlr;

	/* Keep the claim while printing: another console must not mutate the VM.
	 * A reporting exception must still reach the common cleanup path.
	 */
	if (nlr_push(&nlr) == 0) {
		mp_obj_t exc = MP_OBJ_FROM_PTR(exception);
		const mp_obj_type_t *type = mp_obj_get_type(exc);

		if (mode == MPPORT_MOWKOW64 &&
			(type == &mp_type_KeyboardInterrupt || type == &mp_type_SystemExit)) {
			console64_puts("\n");
		} else {
			mp_obj_print_exception(&mp_plat_print, exc);
		}
		nlr_pop();
	} else {
		console64_puts("MicroPython: cannot print exception\n");
	}
}

static void mpport_session64(enum MPPORT_MODE64 mode, const char *arg)
{
	/* Flags modified below the catcher remain defined after setjmp-based NLR. */
	volatile struct MPPORT_SESSION64 session = { 0 };
	mp_state_thread_t thread_before;
	nlr_buf_t nlr;

	if (mpport_claim64() != 0) {
		return;
	}
	thread_before = mp_state_ctx.thread;
	/* GC must scan every session root and all descendant execution frames.
	 * Anchoring inside the init helper excludes caller parse/compile roots.
	 * Use one-past object endpoints so compiler local ordering cannot hide a
	 * root. The validated task stack supplies the readable lower bound.
	 */
	session.stack_top = (uintptr_t) (&thread_before + 1);
	if (session.stack_top < (uintptr_t) (&nlr + 1)) {
		session.stack_top = (uintptr_t) (&nlr + 1);
	}
	if (session.stack_top < (uintptr_t) (&session + 1)) {
		session.stack_top = (uintptr_t) (&session + 1);
	}
	if (session.stack_top < (uintptr_t) (&arg + 1)) {
		session.stack_top = (uintptr_t) (&arg + 1);
	}
	if (nlr_push(&nlr) == 0) {
		if (mpport_init64(&session) == 0) {
			mpport_execute64(mode, arg, &session);
		}
		nlr_pop();
	} else if (mp_ready == 0) {
		/* A partial VM cannot be trusted to format its own exception objects. */
		console64_puts("MicroPython: initialization failed\n");
	} else {
		mpport_report64(mode, nlr.ret_val);
	}
	mpport_finish64(&session, &thread_before);
}

void mpport_repl(void)
{
	mpport_session64(MPPORT_REPL64, NULL);
}

void mpport_run_file(const char *path)
{
	mpport_session64(MPPORT_FILE64, path);
}

/* Mowkow uses mowio.readline, not the Python REPL queue. KeyboardInterrupt and
 * SystemExit end this session quietly; EOFError is handled by the Python loop.
 */
void mpport_run_mowkow(const char *arg)
{
	mpport_session64(MPPORT_MOWKOW64, arg);
}

/*
 * MICROPY_VFS가 꺼져 있으므로 링크된 코어(py/builtin.h)가 요구한다.
 * MICROPY_PY_SYS_PATH가 없어 py/builtinimport.c는 모듈 이름을 그대로 넘기고,
 * fd64는 VFAT 긴 이름을 대소문자 무시하고 견주므로 import _data가 루트의
 * _data.py로 간다.
 *
 * 디렉터리는 늘 "없음"이다. fd64의 디렉터리 훑기가 0x10 항목을 걸러 내므로
 * fd64_open이 디렉터리를 열어 주는 일이 없고, 그래서 패키지(__init__.py)는
 * 없다. 머꼬는 평평한 모듈 네 개라 필요하지 않다.
 */
mp_import_stat_t mp_import_stat(const char *path)
{
	struct FDHANDLE64 fh;

	if (fd64_open(&fh, path) == 0) {
		return MP_IMPORT_STAT_NO_EXIST;
	}
	return MP_IMPORT_STAT_FILE;
}

/*
 * REPL을 쓰든 안 쓰든 링크된 코어(py/gc.c)가 요구한다.
 * gc_helper_collect_regs_and_stack()는 업스트림 그대로 가져다 쓴다
 * (shared/runtime/gchelper_generic.c). x86_64용 레지스터 저장 경로가 이미
 * 있어 아키텍처별 어셈블리가 필요 없다. 세션의 mp_cstack_init_with_top()
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
