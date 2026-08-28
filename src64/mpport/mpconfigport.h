#ifndef MOWKOW64_MPCONFIGPORT_H
#define MOWKOW64_MPCONFIGPORT_H

#include <stdint.h>

/*
 * 머꼬(mowkow_porting.md) 이식이 CORE 등급 기능을 여럿 쓴다: 왈러스(:=),
 * 슬라이스, enumerate. 하나씩 켜는 대신 등급을 CORE_FEATURES로 올린다.
 * 한 줄이면 되고, 업스트림 파이썬 코드가 대체로 가정하는
 * MICROPY_CPYTHON_COMPAT도 같이 딸려 온다. EXTRA로는 올리지 않는다.
 */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC       (1)
#define MICROPY_HELPER_REPL     (1)
#define MICROPY_STACK_CHECK     (1)
/*
 * 기본값은 0이다 -- 그러면 C 스택을 다 쓴 뒤에야 검사가 걸리므로, 검사와
 * 검사 사이에 쌓인 프레임이 이미 스택 아래를 넘어 쓴 뒤다. 커널 메인
 * 스택에서 돌 때는 넘친 자리가 .bss라 티가 안 났지만, 콘솔이 자기 태스크에서
 * 돌면 (console_plan.md 5단계) 그 아래는 memman64가 내준 남의 메모리라
 * 곧바로 죽는다. 여유를 두어 검사가 스택 안에서 걸리게 한다.
 */
#define MICROPY_STACK_CHECK_MARGIN (8192)
/*
 * `import gc` with gc.mem_free()/gc.mem_alloc() -- Stage 4 uses this to
 * right-size the GC heap from real usage instead of the Stage 0.6 guess;
 * kept on afterward since it's a standard, expected capability.
 */
#define MICROPY_PY_GC           (1)
/*
 * 링크할 때 필요하다. shared/runtime/pyexec.c(REPL 루프)가 조건 없이
 * mp_hal_set_interrupt_char()를 부르는데, 그 함수는 이 설정이 켜져 있을
 * 때만 shared/runtime/interrupt_char.c에 생긴다. 지금 이어 둔 것은 장부
 * 정리뿐이다(빈 프롬프트에서 Ctrl-C는 readline() 자체 검사로 동작한다).
 * 돌고 있는 스크립트에 KeyboardInterrupt를 던지려면 키보드 IRQ 핸들러가
 * mp_sched_keyboard_interrupt()를 불러야 하는데 아직 하지 않았다.
 * 그래서 MICROPY_ASYNC_KBD_INTR은 꺼진 채로 둔다(기본값).
 */
#define MICROPY_KBD_EXCEPTION   (1)

/*
 * 우리 콘솔(console64.c)은 프레임버퍼에 글자를 그릴 뿐 VT100 escape를
 * 해석하지 않는다. readline.c의 기본 커서 이동 경로는 "\x1b[<N>D" 바이트를
 * 그대로 보내는데, 우리는 그걸 이상한 글자로 찍게 된다. 대신 백스페이스로만
 * 움직이는 판을 mphalport.c에서 준다.
 */
#define MICROPY_HAL_HAS_VT100 (0)

/*
 * 아직 파일 시스템을 쓰는 reader가 없다. 스크립트 실행은 업스트림 것 대신
 * fd64로 읽어 오는 우리 mp_reader를 쓴다(python_porting.md Stage 3).
 */
#define MICROPY_READER_POSIX (0)
#define MICROPY_READER_VFS   (0)

/*
 * 이 툴체인에는 <errno.h>가 없다(qstr 코드 생성 붙이면서 확인, Stage 1.4).
 * py/mperrno.h는 이 설정을 켜지 않으면 시스템 errno.h를 찾는다. 켜면
 * MicroPython 자체 MP_Exxx 상수를 쓴다.
 */
#define MICROPY_USE_INTERNAL_ERRNO (1)

/*
 * OS를 전제하는 sys 기능은 쓰지 않는다. sys 모듈 자체는 코어 여기저기서
 * 참조하므로 남기고, OS/파일 시스템 냄새가 나는 하위 기능만 끈다.
 *
 * 다만 import는 켠다. 머꼬는 네 모듈로 나뉜 채로 올린다(결정 3). 검색 경로는
 * 없다 -- MICROPY_PY_SYS_PATH가 꺼져 있으면 py/builtinimport.c:147이 받은
 * 이름을 그대로 stat하므로, import _data가 곧 루트의 _data.py다.
 */
#define MICROPY_PY_SYS_MODULES         (0)
/* 머꼬 REPL의 exit() 종료 경로(mowkow_porting.md 결정 8)에 필요하다. */
#define MICROPY_PY_SYS_EXIT            (1)
#define MICROPY_PY_SYS_PATH            (0)
#define MICROPY_PY_SYS_ARGV            (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)

/*
 * Stage 4: 실제 부동소수점 작업으로 FPU/SSE(Stage 0.1)가 확인되어
 * MICROPY_PY_BUILTINS_FLOAT을 배정밀도로 켠다(x86_64는 double을 하드웨어로
 * 처리하므로 float으로 낮출 이유가 없다).
 * MICROPY_PY_BUILTINS_COMPLEX는 기본값이 FLOAT을 따라가지만 일부러 끈다.
 * 계획에 없는 기능이고, 복소수를 켜면 FLOAT이 필요로 한 두 함수(pow, nan)
 * 보다 훨씬 많은 초월함수(csqrt, cexp, ...)를 새로 만들어야 한다.
 */
#define MICROPY_FLOAT_IMPL      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_COMPLEX (0)

/*
 * ROM 등급을 CORE_FEATURES로 올리면 math 모듈이 딸려 오는데, 그러면
 * mpport/libc/math.c에 없는 libm 함수가 통째로 필요해진다(acos/asin은
 * 선언조차 없어 컴파일이 멈추고, sin/cos/tan/log/exp는 선언만 있어 링크에서
 * 터진다). 머꼬는 math를 쓰지 않으므로(업스트림 어디에도 import math가
 * 없다) 모듈째 끈다. 필요해지면 그때 libm을 채운다.
 */
#define MICROPY_PY_MATH (0)

/*
 * io도 같은 이유로 끈다. modio.c의 open은 포트가 mp_builtin_open_obj를
 * 내놓아야 링크되는데(modio.c:208) 우리에게는 POSIX 파일이 없다. 머꼬의
 * 파일 읽기는 io.open이 아니라 mowio.readfile(fd64)로 간다
 * (mowkow_porting.md 5단계).
 */
#define MICROPY_PY_IO (0)

/*
 * x86_64-elf-gcc는 _Float16을 기본으로 지원한다(__FLT16_MAX__를 정의한다).
 * 그런데 그 변환에는 compiler-rt/libgcc의 __extendhfsf2/__truncsfhf2가
 * 필요한데 우리는 그것들을 링크하지 않는다(-nostdlib, -lgcc 없음). 링크
 * 오류로 확인했다. 꺼 두면 런타임 도우미가 필요 없는 binary.c의 소프트웨어
 * 변환 경로를 탄다.
 */
#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)

/*
 * 이 툴체인에는 libc가 없어서 alloca를 선언해 줄 <alloca.h>도 없다
 * (#include <alloca.h>가 컴파일되지 않는 것을 확인했다). 코어에 몇 군데
 * 있는 alloca() 자리를 GC 힙으로 돌린다.
 */
#define MICROPY_NO_ALLOCA (1)

/*
 * EXTRA 등급 기능이지만 하나만 따로 켠다. 머꼬 파서의 0육 16진 리터럴이
 * s[:2] == "0육"과 enumerate("ㄱㄴㄷㄹㅁㅂ")로 한글을 문자 단위로 다룬다
 * (_parse.py:217-220). 이게 없으면 s[:2]가 두 '바이트'라 분기가 아예 안
 * 걸리고 0육ㄱ이 조용히 심볼이 된다. 오류가 아니라 틀린 답이 나온다.
 */
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)

/*
 * 이것도 EXTRA 등급이지만 따로 켠다. 업스트림 머꼬 네 모듈에 f-string이
 * 스물세 군데 있다(오류 메시지와 __str__이 거의 다 f-string이다). 끄고 가려면
 * 그 스물세 줄을 다 고쳐야 하는데, 그건 결정 1이 막는 핵심 로직 수정에
 * 해당한다. 컴파일러 쪽 기능이라 모듈이 늘지는 않는다.
 */
#define MICROPY_PY_FSTRINGS (1)

/*
 * 이것도 EXTRA에서 따로 하나 켠다. _data.py:134가 내장 함수의 이름을 찍는데
 * (#<내장 함수: 머>), 함수 객체에서 이름을 꺼낼 방법이 이것뿐이다. 없으면
 * __name__ 자체가 없어 이름 자리가 "?"가 된다(mowkow_porting.md 6단계).
 */
#define MICROPY_PY_FUNCTION_ATTRS (1)

/* REPL 배너에 찍히는 이름 */
#define MICROPY_HW_BOARD_NAME "머꼬 OS"
#define MICROPY_HW_MCU_NAME   "x86_64"

/*
 * mp_int_t/mp_uint_t는 기본값이 intptr_t/uintptr_t(64비트)라 이 대상에
 * 이미 맞다. 따로 바꿀 것이 없다.
 *
 * MICROPY_NLR_X64도 py/nlr.h가 __x86_64__를 보고 스스로 고르므로,
 * 비지역 복귀(non-local return)에도 setjmp나 libc가 필요 없다.
 */

typedef long mp_off_t;

#define MP_STATE_PORT MP_STATE_VM

#endif
