/*
 * mtask64.c -- 태스크 전환기 (우선순위 스케줄링 + 라운드 로빈)
 *
 * 롱 모드에는 하드웨어 태스크 전환이 없다. 그래서 레지스터를 손으로 저장
 * 하고 되살린다(asmfunc64.asm의 task_switch_asm64).
 *
 * 레벨이 높은 쪽이 우선이고, 같은 레벨 안에서는 차례대로 돈다. 잠든 태스크는
 * 목록에서 빠지므로 깨우기 전까지는 아예 돌지 않는다.
 */
#include <asmfunc64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stddef.h>

struct TASKCTL64 taskctl64;

static void task_add64(struct TASK64 *task)
{
	struct TASKLEVEL64 *tl;

	tl = &taskctl64.level[task->level];
	if (tl->running >= MAX_TASKS64_LV) {
		return;
	}
	tl->tasks[tl->running] = task;
	tl->running++;
	task->flags = TASK64_FLAGS_RUNNING;
}

static void task_remove64(struct TASK64 *task)
{
	struct TASKLEVEL64 *tl;
	uint32_t i;

	tl = &taskctl64.level[task->level];
	for (i = 0; i < tl->running; i++) {
		if (tl->tasks[i] == task) {
			break;
		}
	}
	if (i == tl->running) {
		return;
	}
	tl->running--;
	if (i < tl->now && tl->now > 0) {
		tl->now--;
	}
	if (tl->running == 0) {
		tl->now = 0;
	} else if (tl->now >= tl->running) {
		tl->now = 0;
	}
	task->flags = TASK64_FLAGS_ALLOCATED;
	for (; i < tl->running; i++) {
		tl->tasks[i] = tl->tasks[i + 1];
	}
}

static void task_switchsub64(void)
{
	uint32_t i;

	for (i = 0; i < MAX_TASKLEVELS64; i++) {
		if (taskctl64.level[i].running > 0) {
			break;
		}
	}
	if (i >= MAX_TASKLEVELS64) {
		i = MAX_TASKLEVELS64 - 1;
	}
	taskctl64.now_lv = i;
	taskctl64.lv_change = 0;
}

static void task_idle64(void)
{
	for (;;) {
		io_stihlt();
	}
}

struct TASK64 *task_now64(void)
{
	struct TASKLEVEL64 *tl;

	tl = &taskctl64.level[taskctl64.now_lv];
	if (tl->running == 0) {
		return NULL;
	}
	return tl->tasks[tl->now];
}

struct TASK64 *task_alloc64(void)
{
	uint32_t i;
	struct TASK64 *task;

	for (i = 0; i < MAX_TASKS64; i++) {
		if (taskctl64.tasks0[i].flags == TASK64_FLAGS_UNUSED) {
			task = &taskctl64.tasks0[i];
			task->flags = TASK64_FLAGS_ALLOCATED;
			task->level = 0;
			task->priority = 1;
			task->switches = 0;
			task->stack_base = 0;
			task->stack_size = 0;
			task->process = NULL;
			task->is_user = 0;
			task->kernel_rsp = 0;
			task->context.rsp = 0;
			return task;
		}
	}
	return NULL;
}

int task_set_entry64(struct TASK64 *task, void (*entry)(void), uintptr_t stack_base, size_t stack_size)
{
	uint64_t *sp;

	if (task == NULL || entry == NULL || stack_base == 0 || stack_size < 128) {
		return -1;
	}
	task->stack_base = stack_base;
	task->stack_size = stack_size;
	sp = (uint64_t *) align_down64(stack_base + stack_size, 16);
	*--sp = 0;
	*--sp = (uint64_t) entry;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	task->context.rsp = (uintptr_t) sp;
	return 0;
}

void task_run64(struct TASK64 *task, int level, int priority)
{
	if (task == NULL) {
		return;
	}
	if (level < 0) {
		level = (int) task->level;
	}
	if (level >= MAX_TASKLEVELS64) {
		level = MAX_TASKLEVELS64 - 1;
	}
	if (priority > 0) {
		task->priority = (uint32_t) priority;
	}
	if (task->flags == TASK64_FLAGS_RUNNING && task->level != (uint32_t) level) {
		task_remove64(task);
	}
	if (task->flags != TASK64_FLAGS_RUNNING) {
		task->level = (uint32_t) level;
		task_add64(task);
	}
	taskctl64.lv_change = 1;
}

void task_sleep64(struct TASK64 *task)
{
	struct TASK64 *now_task;
	struct TASK64 *new_task;

	if (task == NULL || task->flags != TASK64_FLAGS_RUNNING) {
		return;
	}
	now_task = task_now64();
	task_remove64(task);
	if (task != now_task) {
		return;
	}
	task_switchsub64();
	new_task = task_now64();
	if (new_task != NULL && new_task != now_task) {
		taskctl64.switches++;
		new_task->switches++;
		context_switch64(&now_task->context, &new_task->context);
	}
}

/* 태스크를 없앤다. 큐에서 빼고 스택을 돌려주고 슬롯을 비운다. 자고 있던
   태스크의 문맥은 그 스택 위에 있지만 다시 깨우지 않으니 상관없다.
   지금 도는 태스크는 죽이지 않는다 -- 돌아갈 스택이 사라진다. */
int task_kill64(struct TASK64 *task)
{
	uint64_t flags;

	if (task == NULL || task->flags == TASK64_FLAGS_UNUSED || task == task_now64()) {
		return -1;
	}
	flags = io_load_rflags();
	io_cli();
	if (task->flags == TASK64_FLAGS_RUNNING) {
		task_remove64(task);
	}
	task->flags = TASK64_FLAGS_UNUSED;
	io_store_rflags(flags);
	if (task->stack_base != 0) {
		memman64_free_4k(&memman64, task->stack_base, task->stack_size);
		task->stack_base = 0;
		task->stack_size = 0;
	}
	return 0;
}

void task_switch64(void)
{
	struct TASKLEVEL64 *tl;
	struct TASK64 *now_task;
	struct TASK64 *new_task;

	tl = &taskctl64.level[taskctl64.now_lv];
	if (tl->running == 0) {
		task_switchsub64();
		tl = &taskctl64.level[taskctl64.now_lv];
		if (tl->running == 0) {
			return;
		}
	}
	now_task = tl->tasks[tl->now];
	tl->now++;
	if (tl->now >= tl->running) {
		tl->now = 0;
	}
	if (taskctl64.lv_change != 0) {
		task_switchsub64();
		tl = &taskctl64.level[taskctl64.now_lv];
	}
	new_task = tl->tasks[tl->now];
	if (new_task != now_task) {
		taskctl64.switches++;
		new_task->switches++;
		context_switch64(&now_task->context, &new_task->context);
	}
}

void task_init64(void)
{
	uint32_t i;
	struct TASK64 *main_task;
	struct TASK64 *idle_task;
	uintptr_t idle_stack;

	for (i = 0; i < MAX_TASKS64; i++) {
		taskctl64.tasks0[i].flags = TASK64_FLAGS_UNUSED;
	}
	for (i = 0; i < MAX_TASKLEVELS64; i++) {
		taskctl64.level[i].running = 0;
		taskctl64.level[i].now = 0;
	}
	taskctl64.now_lv = 0;
	taskctl64.lv_change = 0;
	taskctl64.switches = 0;

	main_task = task_alloc64();
	if (main_task == NULL) {
		return;
	}
	main_task->flags = TASK64_FLAGS_RUNNING;
	main_task->priority = 1;
	main_task->level = 0;
	task_add64(main_task);
	task_switchsub64();

	idle_task = task_alloc64();
	idle_stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	if (idle_task != NULL && idle_stack != 0 &&
			task_set_entry64(idle_task, task_idle64, idle_stack, TASK64_STACK_SIZE) == 0) {
		task_run64(idle_task, MAX_TASKLEVELS64 - 1, 1);
	}
}
