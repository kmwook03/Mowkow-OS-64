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
 * MICROPY_FLOAT_IMPL left at its MINIMUM-level default (NONE), so
 * MICROPY_PY_BUILTINS_FLOAT is 0. Flip to MICROPY_FLOAT_IMPL_DOUBLE in
 * Stage 4, once FPU/SSE (Stage 0.1) is proven under real float workloads.
 */

/*
 * x86_64-elf-gcc (this toolchain) ships no libc, so no <alloca.h> exists
 * to declare it -- verified: `#include <alloca.h>` fails to compile here.
 * Route the core's few alloca() call sites through the GC heap instead.
 */
#define MICROPY_NO_ALLOCA (1)

#define MICROPY_HW_BOARD_NAME "Mowkow OS x86_64"
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
