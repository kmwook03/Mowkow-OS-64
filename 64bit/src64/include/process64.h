#ifndef MOWKOW64_PROCESS64_H
#define MOWKOW64_PROCESS64_H

#include <fd64.h>
#include <stddef.h>
#include <stdint.h>

#define PROCESS64_MAX_FILES 8
#define PROCESS64_MAX_ARGS 8

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
	uintptr_t heap_next;
	uintptr_t saved_kernel_rsp;
	int exited;
	int exit_status;
	struct PROCESS64_FILE files[PROCESS64_MAX_FILES];
	/* stdin/stdout이 향할 콘솔. 앱을 띄운 콘솔이지 그때그때 활성인 콘솔이
	   아니다 -- 콘솔 2에서 띄운 앱이 콘솔 1에 찍으면 안 된다. */
	struct CONSOLE64 *console;
};

int process64_exec_file(const char *path, const char *cmdline,
	struct CONSOLE64 *console);
struct PROCESS64 *process64_current(void);
int process64_user_range_valid(const void *ptr, size_t size);
void process64_exit_current(int status);
uintptr_t process64_current_exit_rsp(void);
int process64_current_exit_status(void);

#endif
