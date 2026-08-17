#ifndef MOWKOW64_MPCONFIGPORT_H
#define MOWKOW64_MPCONFIGPORT_H

#include <stdint.h>

/* 최소 설정에서 시작해서, 아래에 적은 것만 따로 켠다. */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC       (1)
#define MICROPY_HELPER_REPL     (1)
#define MICROPY_STACK_CHECK     (1)
/*
 * gc.mem_free()/gc.mem_alloc()를 쓰려고 켠다. Stage 4에서 GC 힙 크기를
 * 짐작이 아니라 실제 사용량으로 정할 때 썼고, 흔히 기대하는 기능이라
 * 그 뒤로도 켜 둔다.
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
 * 동적 import도, OS를 전제하는 sys 기능도 쓰지 않는다. 업스트림 minimal
 * 포트와 같은 선택이다. sys 모듈 자체는 코어 여기저기서 참조하므로 남기고,
 * OS/파일 시스템 냄새가 나는 하위 기능만 끈다.
 */
#define MICROPY_PY_SYS_MODULES         (0)
#define MICROPY_PY_SYS_EXIT            (0)
#define MICROPY_PY_SYS_PATH            (0)
#define MICROPY_PY_SYS_ARGV            (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)

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
