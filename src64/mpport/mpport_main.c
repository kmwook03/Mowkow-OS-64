#include <mpport64.h>

#include "py/builtin.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mpstate.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

#include <console64.h>
#include <stdint.h>

#define MPPORT_GC_HEAP_SIZE (256 * 1024)

extern uint8_t stack_bottom[];

static uint8_t gc_heap[MPPORT_GC_HEAP_SIZE];

void mpport_repl(void)
{
	int here;
	size_t available;

	/*
	 * Real remaining C stack from here down to stack_bottom (asmfunc64.asm),
	 * not a guessed constant -- matches python_porting.md Stage 0.4/0.6's
	 * "conservative bound computed from the known stack range."
	 */
	available = (size_t) ((uintptr_t) &here - (uintptr_t) stack_bottom);
	mp_cstack_init_with_sp_here(available);
	gc_init(gc_heap, gc_heap + sizeof(gc_heap));
	mp_init();

	console64_repl_set_active(1);
	pyexec_friendly_repl();
	console64_repl_set_active(0);

	mp_deinit();
}

void mpport_run_file(const char *path)
{
	(void) path;
	console64_puts("MicroPython script execution not wired up yet (python_porting.md Stage 3)\n");
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
