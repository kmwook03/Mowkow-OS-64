#ifndef MOWKOW64_PROCESS64_H
#define MOWKOW64_PROCESS64_H

#include <address_space64.h>
#include <fd64.h>
#include <stddef.h>
#include <stdint.h>

#define PROCESS64_MAX_FILES 8
#define PROCESS64_MAX_ARGS 8
/* Includes the terminating NUL; MAX_ARGS includes argv[0] when present. */
#define PROCESS64_CMDLINE_MAX 1024U
#define PROCESS64_ERR_ARGS (-7)
#define PROCESS64_ERR_INVALID (-22)
#define PROCESS64_ERR_STACK (-34)
#define PROCESS64_EXIT_FAULT_BASE 128
#define PROCESS64_EXIT_CLOSED 143
#define PROCESS64_ENTRY_STACK_SIZE 65536U
#define PROCESS64_MAX 32U
#define PROCESS64_PID_MAX 0x7fffffffU
#define PROCESS64_WAIT_NOHANG 1U

struct PROCESS64_RANGE {
	uintptr_t base;
	size_t size;
};

struct PROCESS64_FILE {
	int used;
	struct FDHANDLE64 fh;
};

struct CONSOLE64;
struct TASK64;

enum PROCESS_STATE64 {
	PROCESS_FREE64, PROCESS_LOADING64, PROCESS_READY64,
	PROCESS_RUNNING64, PROCESS_REAP64, PROCESS_ZOMBIE64
};

/* One execution flow per process for now. TASK64 owns CPU/FP context and the
 * scheduler stack; this object owns user-entry state and the privilege stack.
 * A detached zombie has no task or live execution storage.
 */
struct PROCESS_THREAD64 {
	struct TASK64 *task;
	struct USER_BACKING64 entry_storage;
	uintptr_t initial_rsp;
	uintptr_t initial_argv;
	size_t initial_argc;
	uintptr_t saved_kernel_rsp;
};

struct PROCESS64 {
	enum PROCESS_STATE64 state;
	struct PROCESS_THREAD64 thread;
	struct ADDRESS_SPACE64 *space;
	struct USER_BACKING64 image_storage;
	struct USER_BACKING64 stack_storage;
	struct USER_BACKING64 heap_storage;
	struct USER_BACKING64 elf_snapshot;
	/* Supervisor-only syscall snapshot, retained for reuse until teardown. */
	struct USER_BACKING64 io_storage;
	int io_ready;
	uint8_t io_empty;
	int reap_pending;
	uint32_t pid;
	uint32_t parent_pid;
	struct TASK64 *waiter;
	int started;
	int orphan;
	int auto_reap;
	int legacy_owner;
	int wait_status;
	uintptr_t entry;
	struct PROCESS64_RANGE image;
	struct PROCESS64_RANGE stack;
	struct PROCESS64_RANGE heap;
	uintptr_t stack_backing;
	uintptr_t heap_backing;
	uintptr_t heap_next;
	int exited;
	int exit_status;
	struct PROCESS64_FILE files[PROCESS64_MAX_FILES];
	/* stdin/stdout이 향할 콘솔. 앱을 띄운 콘솔이지 그때그때 활성인 콘솔이
	   아니다 -- 콘솔 2에서 띄운 앱이 콘솔 1에 찍으면 안 된다. */
	struct CONSOLE64 *console;
};

/* Kernel-owned NUL-terminated input; NULL cmdline uses path as the command line.
   Oversized names/lines or excess arguments return PROCESS64_ERR_ARGS. */
int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console);
/* Prepare only: no root switch, task attachment, or user execution. One task
 * owns each handle. *result starts NULL; a non-NULL result on failure retains
 * resources and must be discarded again.
 */
int process64_prepare_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console, struct PROCESS64 **result);
/* Inactive prepared processes only; failed frees retain the handle and slot. */
int process64_discard_prepared(struct PROCESS64 **process);
/* Task-context lifecycle API; kernel pointers only. reserve publishes no child
 * to wait until run_prepared starts it. NULL parent selects kernel PID 1.
 * Slot exhaustion is -EAGAIN; mm backing/table exhaustion remains -ENOMEM.
 */
int process64_reserve_child(struct PROCESS64 *parent, struct PROCESS64 **result);
/* Runs on the calling, unattached scheduler task, independent of console slots.
 * Keeps the detached exit record for wait; returns 0 on execution completion
 * and negative errno before entry. Failed release is retried by collect.
 * A successful run transfers the handle to wait/init collection; the runner
 * must not dereference it after return.
 */
int process64_run_prepared(struct PROCESS64 *process);
/* pid > 0 or -1; only NOHANG is supported. Outputs are unchanged on error.
 * Returns 0 with reaped_pid=0 when NOHANG finds only live children.
 * One waiter per parent (A: one execution flow); second waiter gets -EBUSY.
 * A console close interrupts blocking wait with -EINTR; caller unwinds to
 * the user-return boundary before termination. Kernel waiters own their task
 * for the entire call.
 */
int process64_wait_child(struct PROCESS64 *parent, int32_t pid,
	unsigned int options, int32_t *reaped_pid, int32_t *wait_status);
/* Bounded task-context retry and kernel PID 1 orphan collection, no IRQ caller. */
void process64_collect(void);
/* IRQ-off scheduler/entry boundary. No allocation or sleeping. */
void process64_switch_task64(const struct TASK64 *task);
uintptr_t process64_entry_stack_top(void);
/* IRQ-off return boundary only; never abandon a suspended kernel operation. */
int process64_return_to_user64(int user_mode);
/* Task context, outside GUI guards. Retry detached frees once; -16 while owned. */
int process64_reap_console64(const struct CONSOLE64 *console);
/* IRQ-safe ownership query, including detached cleanup in progress. */
int process64_console_busy64(const struct CONSOLE64 *console);
struct PROCESS64 *process64_current(void);
/* Current task only, no nested use. Lazily grows a supervisor-only I/O buffer
 * up to the largest user range. Failed frees retain ownership; return NULL.
 * Size zero returns a valid kernel address without allocating.
 */
void *process64_io_buffer(size_t size);
/* Requires a current process and a non-NULL pointer. The whole buffer must fit
   one nonempty image/stack/heap range with nonzero base and valid end.
   Ranges are not joined; nonempty ranges also require readable mapped pages.
   For size 0, the pointer must be within such a range or at its end. */
int process64_user_range_valid(const void *ptr, size_t size);
/* Same range contract, requiring writable leaves for kernel output. */
int process64_user_write_valid(void *ptr, size_t size);
/* Mark termination from SYS_EXIT, user fault or close at a user-return boundary.
   Release terminal/files/memory after returning to exec's kernel frame. */
void process64_exit_current(int status);
/* Preserve legacy console status, record signal termination separately for wait. */
void process64_terminate_current(int status, unsigned int signal);
uintptr_t process64_current_exit_rsp(void);
int process64_current_exit_status(void);

#endif
