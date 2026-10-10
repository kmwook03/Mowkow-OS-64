#ifndef MOWKOW64_PROCESS64_H
#define MOWKOW64_PROCESS64_H

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

struct PROCESS64_RANGE {
	uintptr_t base;
	size_t size;
};

struct PROCESS64_FILE {
	int used;
	struct FDHANDLE64 fh;
};

struct CONSOLE64;

struct PROCESS64 {
	uint32_t pid;
	uintptr_t entry;
	struct PROCESS64_RANGE image;
	struct PROCESS64_RANGE stack;
	struct PROCESS64_RANGE heap;
	uintptr_t stack_backing;
	uintptr_t heap_backing;
	uintptr_t heap_next;
	/* Set before the first mapping attempt, including partial failure. */
	int user_mappings;
	uintptr_t saved_kernel_rsp;
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
struct PROCESS64 *process64_current(void);
/* Requires a current process and a non-NULL pointer. The whole buffer must fit
   one nonempty image/stack/heap range with nonzero base and valid end.
   Ranges are not joined.
   For size 0, the pointer must be within such a range or at its end. */
int process64_user_range_valid(const void *ptr, size_t size);
/* Mark termination from SYS_EXIT or a user fault. Release terminal/files and
   memory after returning to exec's kernel frame, not from the exception frame. */
void process64_exit_current(int status);
uintptr_t process64_current_exit_rsp(void);
int process64_current_exit_status(void);

#endif
