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
};

int process64_exec_file(const char *path, const char *cmdline);
struct PROCESS64 *process64_current(void);
int process64_user_range_valid(const void *ptr, size_t size);
void process64_exit_current(int status);
uintptr_t process64_current_exit_rsp(void);
int process64_current_exit_status(void);

#endif
