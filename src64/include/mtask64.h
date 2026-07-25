#ifndef MOWKOW64_MTASK64_H
#define MOWKOW64_MTASK64_H

#include <stddef.h>
#include <stdint.h>

#define MAX_TASKS64       32
#define MAX_TASKS64_LV    16
#define MAX_TASKLEVELS64  10
#define TASK64_STACK_SIZE (64 * 1024)

#define TASK64_FLAGS_UNUSED    0
#define TASK64_FLAGS_ALLOCATED 1
#define TASK64_FLAGS_RUNNING   2

struct CONTEXT64 {
	uint64_t rsp;
};

struct TASK64 {
	uint32_t flags;
	uint32_t level;
	uint32_t priority;
	uint64_t switches;
	uintptr_t stack_base;
	size_t stack_size;
	void *process;
	uint32_t is_user;
	uintptr_t kernel_rsp;
	struct CONTEXT64 context;
};

struct TASKLEVEL64 {
	uint32_t running;
	uint32_t now;
	struct TASK64 *tasks[MAX_TASKS64_LV];
};

struct TASKCTL64 {
	uint32_t now_lv;
	uint32_t lv_change;
	uint64_t switches;
	struct TASKLEVEL64 level[MAX_TASKLEVELS64];
	struct TASK64 tasks0[MAX_TASKS64];
};

extern struct TASKCTL64 taskctl64;

void context_switch64(struct CONTEXT64 *old_context, struct CONTEXT64 *new_context);
void task_init64(void);
struct TASK64 *task_now64(void);
struct TASK64 *task_alloc64(void);
int task_set_entry64(struct TASK64 *task, void (*entry)(void), uintptr_t stack_base, size_t stack_size);
void task_run64(struct TASK64 *task, int level, int priority);
void task_sleep64(struct TASK64 *task);
void task_switch64(void);

#endif
