#ifndef MOWKOW64_MPCONFIGPORT_H
#define MOWKOW64_MPCONFIGPORT_H

#include <stdint.h>

/* Minimal starting configuration; only the overrides below are enabled. */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

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
 * Needed at link time: shared/runtime/pyexec.c (Stage 2's REPL loop) calls
 * mp_hal_set_interrupt_char() unconditionally, which shared/runtime/
 * interrupt_char.c only defines when this is on. Only the bookkeeping is
 * wired (Ctrl-C at an empty prompt works via readline()'s own check);
 * actually delivering an async KeyboardInterrupt into a running script
 * needs the keyboard IRQ handler to call mp_sched_keyboard_interrupt(),
 * not done yet -- MICROPY_ASYNC_KBD_INTR stays off (default).
 */
#define MICROPY_KBD_EXCEPTION   (1)

/*
 * Our console (console64.c) draws glyphs to a framebuffer; it doesn't
 * parse VT100 escape sequences. readline.c's default VT100 cursor-back
 * path would send raw "\x1b[<N>D" bytes that we'd just draw as garbage
 * glyphs. Supply plain backspace-loop versions instead (mphalport.c).
 */
#define MICROPY_HAL_HAS_VT100 (0)

/*
 * No filesystem-backed reader yet -- script execution uses a custom
 * fd64-backed mp_reader (python_porting.md Stage 3), not upstream's.
 */
#define MICROPY_READER_POSIX (0)
#define MICROPY_READER_VFS   (0)

/*
 * No <errno.h> exists on this toolchain -- confirmed while wiring the
 * qstr codegen (Stage 1.4), py/mperrno.h needs real system errno.h unless
 * this is set, in which case it uses its own portable MP_Exxx constants.
 */
#define MICROPY_USE_INTERNAL_ERRNO (1)

/*
 * No dynamic import, no OS-assuming sys features. Mirrors upstream's own
 * "minimal" port: the sys module itself stays available since core code
 * paths reference it, but the OS/filesystem-shaped submodules are off.
 */
#define MICROPY_PY_SYS_MODULES         (0)
#define MICROPY_PY_SYS_EXIT            (0)
#define MICROPY_PY_SYS_PATH            (0)
#define MICROPY_PY_SYS_ARGV            (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)

/*
 * Stage 4: FPU/SSE (Stage 0.1) proven under real float workloads, so
 * MICROPY_PY_BUILTINS_FLOAT is enabled via a real double-precision impl
 * (x86_64 has hardware double support, no reason to restrict to float).
 * MICROPY_PY_BUILTINS_COMPLEX defaults to tracking FLOAT -- kept off
 * explicitly, out of scope (plan only calls for re-enabling FLOAT), and
 * complex support would pull in a much bigger set of real transcendental
 * math functions (csqrt, cexp, ...) than the two (pow, nan) FLOAT alone
 * needed.
 */
#define MICROPY_FLOAT_IMPL      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_COMPLEX (0)

/*
 * x86_64-elf-gcc's _Float16 support (default on, since it defines
 * __FLT16_MAX__) needs __extendhfsf2/__truncsfhf2 from compiler-rt/libgcc,
 * which we don't link (-nostdlib, no -lgcc). Confirmed via undefined
 * references at link time. Falls back to binary.c's software conversion
 * path instead, which needs no runtime helpers.
 */
#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)

/*
 * x86_64-elf-gcc (this toolchain) ships no libc, so no <alloca.h> exists
 * to declare it -- verified: `#include <alloca.h>` fails to compile here.
 * Route the core's few alloca() call sites through the GC heap instead.
 */
#define MICROPY_NO_ALLOCA (1)

// #define MICROPY_HW_BOARD_NAME "Mowkow OS x86_64"
#define MICROPY_HW_BOARD_NAME "머꼬 OS"
#define MICROPY_HW_MCU_NAME   "x86_64"

/*
 * mp_int_t/mp_uint_t default to intptr_t/uintptr_t (64-bit), already
 * correct for this target -- no override needed.
 *
 * MICROPY_NLR_X64 is auto-selected by py/nlr.h from __x86_64__, so
 * non-local return needs no setjmp/libc dependency either.
 */

typedef long mp_off_t;

#define MP_STATE_PORT MP_STATE_VM

#endif
