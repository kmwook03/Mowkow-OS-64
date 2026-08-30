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
 * 2MiB 근거: library_kor.scm을 올리고 나면 gc.mem_free()가 13KiB밖에 남지 않아
 * 식을 몇 개 계산하면 그대로 멈춘다. 
 * 힙은 .bss에 있고 커널 .bss 끝(약 2.2MiB)과 memman64 시작
 * (MEMMAN64_EARLY_START = 8MiB) 사이에 자리가 넉넉하므로 2MiB로 올린다.
 */
#define MPPORT_GC_HEAP_SIZE (4 * 1024 * 1024)

extern uint8_t stack_bottom[];

static uint8_t gc_heap[MPPORT_GC_HEAP_SIZE];

/*
 * MicroPython은 태생적으로 단일 인스턴스다: gc_heap도 하나, mp_state_ctx도
 * 하나(전역). 콘솔마다 py를 돌리려면 콘솔 수만큼의 512KiB .bss에 더해,
 * MP_STATE_VM이 바꿔 낄 수 있는 포인터를 거치도록 벤더링된 MicroPython 자체를
 * 고쳐야 한다. 따라서 시스템 전체에서 한 번에 하나, 두 번째 콘솔은 거절하도록 설계하였다.
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
		   거절당한 콘솔에 찍힌다. */
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

/*
 * 콘솔 명령이 스크립트에 넘기는 인자 하나. sys.argv를 켜지 않는다 --
 * 문자열 하나 때문에 그럴 값어치가 없다. 
 * 콘솔의 입력 줄은 다음 명령에 덮어씌워지므로 가리키지 않고 베껴 둔다.
 */
static char mp_argv[FD64_NAME_MAX];
static int mp_argv_set;

void mpport_set_argv(const char *arg)
{
	size_t i;

	if (arg == NULL) {
		mp_argv_set = 0;
		return;
	}
	for (i = 0; i + 1 < sizeof(mp_argv) && arg[i] != '\0'; i++) {
		mp_argv[i] = arg[i];
	}
	mp_argv[i] = '\0';
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
static void mpport_stack_limit(void)
{
	int here;
	size_t available;
	struct TASK64 *task;

	/*
	 * 콘솔이 자기 태스크에서 돌면 그 스택은 memman64가 준 64KiB지 커널 메인 스택이 아니다. 
	 * stack_bottom으로 측정하면 몇 MiB가 남은 줄 알고 넘침 검사가 걸리지 않아, 
	 * 깊은 재귀가 조용히 태스크 스택을 침범한다.
	 */
	task = task_now64();
	if (task != NULL && task->stack_base != 0) {
		available = (size_t) ((uintptr_t) &here - task->stack_base);
	} else {
		available = (size_t) ((uintptr_t) &here - (uintptr_t) stack_bottom);
	}
	mp_cstack_init_with_sp_here(available);
}

/*
 * 해석기는 부팅 뒤 한 번만 세운다. 머꼬의 바탕 환경을 만드는 데 약 1.7초가 드는데, 
 * 그것을 부를 때마다 해석기를 실행시킬 이유가 없다. 
 * 그래서 mp_deinit()도 없애고 GC 힙과 qstr 풀이 부팅 내내 살아 있으므로, 
 * 메모리 누수가 발생하면 세션을 넘겨 쌓인다.
 */
static int mp_ready;

static void mpport_init(void)
{
	mpport_stack_limit();
	if (mp_ready != 0) {
		return;
	}
	gc_init(gc_heap, gc_heap + sizeof(gc_heap));
	mp_init();
	mp_ready = 1;
}

/*
 * 파일 하나를 GC 힙으로 읽어 온다. 없으면 NULL, 있으면 크기를 *out_size에.
 *
 * 스크립트 실행과 import가 같은 것을 필요로 해서 한 곳에 둔다. 
 * memman64가 아니라 GC 힙에 담는 이유는 수명이다.
 * mp_reader_new_mem에 free_len을 함께 주면 lexer가 닫힐 때 리더가 알아서 m_del한다. 
 * import는 자기가 연 버퍼를 언제 놓아야 하는지 부르는 쪽에서 알 방법이 없다.
 *
 * m_new는 자리가 없으면 MemoryError를 던지고 NLR 문맥 안에서만 부른다.
 */
uint8_t *mpport_load_file(const char *path, size_t *out_size)
{
	struct FDHANDLE64 fh;
	byte *buf;
	size_t size;

	if (fd64_open(&fh, path) == 0) {
		return NULL;
	}
	size = fh.info.size;
	/* 빈 파일도 유효한 모듈이다. m_new(0)은 NULL을 돌려줄 수 있어서
	   "없음"과 헷갈리므로 1바이트를 잡아 둔다. */
	buf = m_new(byte, size == 0 ? 1 : size);
	fd64_read(&fh, buf, size);
	*out_size = size;
	return buf;
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

	mpport_release();
}

void mpport_run_file(const char *path)
{
	nlr_buf_t nlr;

	if (mpport_claim() != 0) {
		return;
	}
	mpport_init();

	if (nlr_push(&nlr) == 0) {
		byte *buf;
		size_t size;
		mp_reader_t reader;
		mp_lexer_t *lex;
		qstr source_name;
		mp_parse_tree_t parse_tree;
		mp_obj_t module_fun;

		/* 파일을 GC 힙에 담으므로 mpport_init 뒤에, 그리고 NLR 안에서
		   읽는다. 예전에는 memman64로 먼저 읽었다. */
		buf = mpport_load_file(path, &size);
		if (buf == NULL) {
			console64_puts("file not found\n");
		} else {
			mp_reader_new_mem(&reader, buf, size, size);
			lex = mp_lexer_new(qstr_from_str(path), reader);
			source_name = lex->source_name;
			parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
			module_fun = mp_compile(&parse_tree, source_name, false);
			mp_call_function_0(module_fun);
		}
		nlr_pop();
	} else {
		mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
	}

	mpport_release();
}

/*
 * `머꼬` 명령. 파일을 돌리는 게 아니라 두 줄짜리 소스를 그대로 컴파일해 돌린다:
 * mowkow.py는 스크립트가 아니라 모듈이라야 library_kor.scm으로 만든 바탕
 * 환경이 import 캐시에 남는다.
 *
 * 나가는 길은 넷인데 셋은 파이썬 쪽에서 끝난다. 빈 줄은 업스트림
 * eval_print_loop이 스스로 빠져나오고, Ctrl-D는 EOFError로 같은 자리에서
 * 잡히고, Ctrl-C는 KeyboardInterrupt로 여기까지 올라온다. 여기서는 그 둘
 * (KeyboardInterrupt, SystemExit)을 조용히 삼켜서 역추적이 찍히지 않게 한다.
 *
 * repl_active는 건드리지 않는다. 머꼬의 입력은 mowio.readline이고 그 길은
 * line_capture가 먼저 잡으므로(console64.c:1238) 플래그가 필요 없다. 켜지
 * 않으니 콘솔이 REPL 모드에 갇힐 길도 없다.
 */
void mpport_run_mowkow(const char *arg)
{
	static const char source[] = "import mowkow\nmowkow.main()\n";
	nlr_buf_t nlr;

	if (mpport_claim() != 0) {
		return;
	}
	mpport_init();
	mpport_set_argv(arg);

	if (nlr_push(&nlr) == 0) {
		mp_lexer_t *lex;
		qstr source_name;
		mp_parse_tree_t parse_tree;
		mp_obj_t module_fun;

		lex = mp_lexer_new_from_str_len(qstr_from_str("머꼬"), source,
				sizeof(source) - 1, 0);
		source_name = lex->source_name;
		parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
		module_fun = mp_compile(&parse_tree, source_name, false);
		mp_call_function_0(module_fun);
		nlr_pop();
	} else {
		mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
		const mp_obj_type_t *type = mp_obj_get_type(exc);

		if (type != &mp_type_KeyboardInterrupt && type != &mp_type_SystemExit) {
			mp_obj_print_exception(&mp_plat_print, exc);
		} else {
			console64_puts("\n");
		}
	}

	mpport_set_argv(NULL);
	mpport_release();
}

/*
 * 링크된 코어(py/lexer.h)가 요구한다. MICROPY_READER_POSIX와
 * MICROPY_READER_VFS가 둘 다 꺼져 있으므로 포트가 직접 준다. import가
 * 모듈 소스를 여기로 가져간다.
 *
 * free_len으로 size를 같이 넘겨 lexer가 닫힐 때 버퍼도 함께 풀리게 한다.
 */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	mp_reader_t reader;
	byte *buf;
	size_t size;

	buf = mpport_load_file(qstr_str(filename), &size);
	if (buf == NULL) {
		mp_raise_OSError(MP_ENOENT);
	}
	mp_reader_new_mem(&reader, buf, size, size);
	return mp_lexer_new(filename, reader);
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

