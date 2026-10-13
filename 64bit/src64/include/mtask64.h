#ifndef MOWKOW64_MTASK64_H
#define MOWKOW64_MTASK64_H

#include <stddef.h>
#include <stdint.h>

#define MAX_TASKS64       32
#define MAX_TASKS64_LV    16
#define MAX_TASKLEVELS64  10
#define TASK64_STACK_SIZE (64 * 1024)
#define TASK64_STACK_MIN_SIZE 4096U
#define TASK64_STACK_ALIGNMENT 4096U
#define TASK64_STACK_GUARD_SIZE 64U

#ifndef MOWKOW64_TASK_STACK_DEBUG
#define MOWKOW64_TASK_STACK_DEBUG 0
#endif

#define TASK64_ERR_NOMEM   (-12)
#define TASK64_ERR_INVALID (-22)

enum TASK64_STATE {
	TASK64_FLAGS_UNUSED = 0,
	TASK64_FLAGS_ALLOCATED = 1,
	TASK64_FLAGS_RUNNING = 2,
	TASK64_FLAGS_SLEEP_PENDING = 3,
};

struct CONTEXT64 {
	uintptr_t stack_pointer;
};

struct TASK64 {
	enum TASK64_STATE flags;
	uint32_t level;
	uint32_t priority;
	uint64_t switches;
	uintptr_t stack_base;
	size_t stack_size;
	uintptr_t stack_usable_base;
	size_t stack_usable_size;
	size_t stack_high_water;
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

/* Queue operations are IRQ-safe and never sleep while holding the queue lock. */
int task_init64(void);
int task_validate64(void);
/* In debug builds, also validates the guard and returns peak stack use. */
int task_stack_check64(struct TASK64 *task, size_t *used);
struct TASK64 *task_now64(void);
struct TASK64 *task_alloc64(void);
int task_set_entry64(struct TASK64 *task, void (*entry)(void),
	uintptr_t stack_base, size_t stack_size);
int task_run64(struct TASK64 *task, int level, int priority);
/* Split sleep closes the wait-queue lost-wakeup window: prepare marks the
   current task pending, then commit either sleeps or observes an early wake. */
int task_sleep_prepare64(struct TASK64 *task);
int task_sleep_commit64(struct TASK64 *task);
int task_sleep64(struct TASK64 *task);
int task_kill64(struct TASK64 *task);
void task_switch64(void);
struct TASK64 *task_switch_prepare64(void);

#endif
