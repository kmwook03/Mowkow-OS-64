/*
 * mtask64.c -- priority scheduler with per-level round-robin queues
 *
 * Queue state is shared with timer IRQ handlers.  Public operations save and
 * restore the caller's IRQ state; helpers ending in _nolock require that
 * protection already to be held.  A wake never performs a context switch.
 */
#include <arch/platform64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stddef.h>

#define TASK64_STACK_GUARD_BYTE 0xd7U
#define TASK64_STACK_FILL_BYTE  0xa5U

struct TASKCTL64 taskctl64;

static int task_state_change_nolock(struct TASK64 *task,
	enum TASK64_STATE next)
{
	enum TASK64_STATE current;
	int allowed;

	if (task == NULL) {
		return TASK64_ERR_INVALID;
	}
	current = task->flags;
	allowed = 0;
	if (current == TASK64_FLAGS_UNUSED &&
			next == TASK64_FLAGS_ALLOCATED) {
		allowed = 1;
	} else if (current == TASK64_FLAGS_ALLOCATED &&
			(next == TASK64_FLAGS_UNUSED ||
			next == TASK64_FLAGS_RUNNING)) {
		allowed = 1;
	} else if (current == TASK64_FLAGS_RUNNING &&
			(next == TASK64_FLAGS_ALLOCATED ||
			next == TASK64_FLAGS_SLEEP_PENDING)) {
		allowed = 1;
	} else if (current == TASK64_FLAGS_SLEEP_PENDING &&
			(next == TASK64_FLAGS_ALLOCATED ||
			next == TASK64_FLAGS_RUNNING)) {
		allowed = 1;
	}
	if (allowed == 0) {
		return TASK64_ERR_INVALID;
	}
	task->flags = next;
	return 0;
}

static struct TASK64 *task_now_nolock(void)
{
	struct TASKLEVEL64 *level;

	if (taskctl64.now_lv >= MAX_TASKLEVELS64) {
		return NULL;
	}
	level = &taskctl64.level[taskctl64.now_lv];
	if (level->running == 0 || level->running > MAX_TASKS64_LV ||
		level->now >= level->running) {
		return NULL;
	}
	return level->tasks[level->now];
}

static int task_queue_count_nolock(const struct TASK64 *task,
	uint32_t *found_level, uint32_t *found_index)
{
	uint32_t count;
	uint32_t i;
	uint32_t j;
	uint32_t running;

	count = 0;
	for (i = 0; i < MAX_TASKLEVELS64; i++) {
		running = taskctl64.level[i].running;
		if (running > MAX_TASKS64_LV) {
			return TASK64_ERR_INVALID;
		}
		for (j = 0; j < running; j++) {
			if (taskctl64.level[i].tasks[j] == task) {
				if (found_level != NULL) {
					*found_level = i;
				}
				if (found_index != NULL) {
					*found_index = j;
				}
				count++;
			}
		}
	}
	return (int) count;
}

static int task_add_nolock(struct TASK64 *task)
{
	struct TASKLEVEL64 *level;

	if (task == NULL || task->flags != TASK64_FLAGS_ALLOCATED ||
			task->level >= MAX_TASKLEVELS64 ||
			task_queue_count_nolock(task, NULL, NULL) != 0) {
		return TASK64_ERR_INVALID;
	}
	level = &taskctl64.level[task->level];
	if (level->running >= MAX_TASKS64_LV) {
		return TASK64_ERR_NOMEM;
	}
	if (task_state_change_nolock(task, TASK64_FLAGS_RUNNING) != 0) {
		return TASK64_ERR_INVALID;
	}
	level->tasks[level->running] = task;
	level->running++;
	return 0;
}

static int task_remove_nolock(struct TASK64 *task)
{
	struct TASKLEVEL64 *level;
	uint32_t found_level;
	uint32_t i;

	if (task == NULL ||
			(task->flags != TASK64_FLAGS_RUNNING &&
			task->flags != TASK64_FLAGS_SLEEP_PENDING) ||
			task_queue_count_nolock(task, &found_level, &i) != 1 ||
			found_level != task->level) {
		return TASK64_ERR_INVALID;
	}
	level = &taskctl64.level[found_level];
	if (task_state_change_nolock(task, TASK64_FLAGS_ALLOCATED) != 0) {
		return TASK64_ERR_INVALID;
	}
	level->running--;
	if (i < level->now && level->now > 0) {
		level->now--;
	}
	if (level->running == 0) {
		level->now = 0;
	} else if (level->now >= level->running) {
		level->now = 0;
	}
	for (; i < level->running; i++) {
		level->tasks[i] = level->tasks[i + 1];
	}
	level->tasks[level->running] = NULL;
	return 0;
}

static void task_switchsub_nolock(void)
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

static struct TASK64 *task_alloc_nolock(void)
{
	uint32_t i;
	struct TASK64 *task;

	for (i = 0; i < MAX_TASKS64; i++) {
		if (taskctl64.tasks0[i].flags != TASK64_FLAGS_UNUSED) {
			continue;
		}
		task = &taskctl64.tasks0[i];
		if (task_state_change_nolock(task,
				TASK64_FLAGS_ALLOCATED) != 0) {
			return NULL;
		}
		task->level = 0;
		task->priority = 1;
		task->switches = 0;
		task->stack_base = 0;
		task->stack_size = 0;
		task->stack_usable_base = 0;
		task->stack_usable_size = 0;
		task->stack_high_water = 0;
		task->process = NULL;
		task->is_user = 0;
		task->kernel_rsp = 0;
		task->context.stack_pointer = 0;
		return task;
	}
	return NULL;
}

static int task_pointer_valid_nolock(const struct TASK64 *task)
{
	uint32_t i;

	for (i = 0; i < MAX_TASKS64; i++) {
		if (task == &taskctl64.tasks0[i]) {
			return 1;
		}
	}
	return 0;
}

static int task_stack_parameters64(uintptr_t stack_base, size_t stack_size,
	uintptr_t *usable_base, size_t *usable_size)
{
	if (usable_base == NULL || usable_size == NULL || stack_base == 0 ||
			stack_size < TASK64_STACK_MIN_SIZE ||
			(stack_base & (TASK64_STACK_ALIGNMENT - 1U)) != 0 ||
			(stack_size & (TASK64_STACK_ALIGNMENT - 1U)) != 0 ||
			stack_size > UINTPTR_MAX - stack_base) {
		return TASK64_ERR_INVALID;
	}
#if MOWKOW64_TASK_STACK_DEBUG
	if (stack_size <= TASK64_STACK_GUARD_SIZE) {
		return TASK64_ERR_INVALID;
	}
	*usable_base = stack_base + TASK64_STACK_GUARD_SIZE;
	*usable_size = stack_size - TASK64_STACK_GUARD_SIZE;
#else
	*usable_base = stack_base;
	*usable_size = stack_size;
#endif
	return 0;
}

static void task_stack_initialize64(uintptr_t stack_base, size_t stack_size)
{
#if MOWKOW64_TASK_STACK_DEBUG
	uint8_t *stack;
	size_t i;

	stack = (uint8_t *) stack_base;
	for (i = 0; i < TASK64_STACK_GUARD_SIZE; i++) {
		stack[i] = TASK64_STACK_GUARD_BYTE;
	}
	for (; i < stack_size; i++) {
		stack[i] = TASK64_STACK_FILL_BYTE;
	}
#else
	(void) stack_base;
	(void) stack_size;
#endif
}

static int task_stack_check_nolock(struct TASK64 *task, size_t *used)
{
	uintptr_t expected_base;
	uintptr_t stack_top;
	size_t expected_size;
#if MOWKOW64_TASK_STACK_DEBUG
	uint8_t *cursor;
	uint8_t *guard;
	size_t current_used;
	size_t i;
#endif

	if (task == NULL) {
		return TASK64_ERR_INVALID;
	}
	if (task->stack_base == 0) {
		if (task->stack_size != 0 || task->stack_usable_base != 0 ||
				task->stack_usable_size != 0 ||
				task->stack_high_water != 0) {
			return TASK64_ERR_INVALID;
		}
		if (used != NULL) {
			*used = 0;
		}
		return 0;
	}
	if (task_stack_parameters64(task->stack_base, task->stack_size,
			&expected_base, &expected_size) != 0 ||
			task->stack_usable_base != expected_base ||
			task->stack_usable_size != expected_size ||
			task->stack_high_water > expected_size) {
		return TASK64_ERR_INVALID;
	}
	stack_top = expected_base + expected_size;
	if (task->context.stack_pointer != 0 &&
			(task->context.stack_pointer < expected_base ||
			task->context.stack_pointer > stack_top)) {
		return TASK64_ERR_INVALID;
	}
#if MOWKOW64_TASK_STACK_DEBUG
	guard = (uint8_t *) task->stack_base;
	for (i = 0; i < TASK64_STACK_GUARD_SIZE; i++) {
		if (guard[i] != TASK64_STACK_GUARD_BYTE) {
			return TASK64_ERR_INVALID;
		}
	}
	cursor = (uint8_t *) expected_base;
	while ((uintptr_t) cursor < stack_top &&
			*cursor == TASK64_STACK_FILL_BYTE) {
		cursor++;
	}
	current_used = (size_t) (stack_top - (uintptr_t) cursor);
	if (current_used > task->stack_high_water) {
		task->stack_high_water = current_used;
	}
#endif
	if (used != NULL) {
		*used = task->stack_high_water;
	}
	return 0;
}

static int task_stack_debug_check_nolock(struct TASK64 *task)
{
#if MOWKOW64_TASK_STACK_DEBUG
	return task_stack_check_nolock(task, NULL);
#else
	(void) task;
	return 0;
#endif
}

static int task_validate_nolock(void)
{
	const struct TASK64 *current;
	struct TASK64 *task;
	const struct TASKLEVEL64 *level;
	uint32_t i;
	uint32_t j;
	int queue_count;

	if (taskctl64.now_lv >= MAX_TASKLEVELS64 || taskctl64.lv_change > 1) {
		return TASK64_ERR_INVALID;
	}
	current = task_now_nolock();
	for (i = 0; i < MAX_TASKLEVELS64; i++) {
		level = &taskctl64.level[i];
		if (level->running > MAX_TASKS64_LV ||
			(level->running == 0 && level->now != 0) ||
			(level->running > 0 && level->now >= level->running)) {
			return TASK64_ERR_INVALID;
		}
		for (j = 0; j < level->running; j++) {
			task = level->tasks[j];
			if (!task_pointer_valid_nolock(task) || task->level != i ||
				(task->flags != TASK64_FLAGS_RUNNING &&
				task->flags != TASK64_FLAGS_SLEEP_PENDING)) {
				return TASK64_ERR_INVALID;
			}
			if (task->flags == TASK64_FLAGS_SLEEP_PENDING &&
					task != current) {
				return TASK64_ERR_INVALID;
			}
		}
		for (; j < MAX_TASKS64_LV; j++) {
			if (level->tasks[j] != NULL) {
				return TASK64_ERR_INVALID;
			}
		}
	}
	for (i = 0; i < MAX_TASKS64; i++) {
		task = &taskctl64.tasks0[i];
		if (task->flags < TASK64_FLAGS_UNUSED ||
				task->flags > TASK64_FLAGS_SLEEP_PENDING) {
			return TASK64_ERR_INVALID;
		}
		queue_count = task_queue_count_nolock(task, NULL, NULL);
		if ((task->flags == TASK64_FLAGS_RUNNING ||
				task->flags == TASK64_FLAGS_SLEEP_PENDING) &&
				queue_count != 1) {
			return TASK64_ERR_INVALID;
		}
		if ((task->flags == TASK64_FLAGS_UNUSED ||
				task->flags == TASK64_FLAGS_ALLOCATED) &&
				queue_count != 0) {
			return TASK64_ERR_INVALID;
		}
		if (task->flags == TASK64_FLAGS_UNUSED) {
			if (task->stack_base != 0 || task->stack_size != 0 ||
					task->stack_usable_base != 0 ||
					task->stack_usable_size != 0 ||
					task->stack_high_water != 0) {
				return TASK64_ERR_INVALID;
			}
		} else if (task_stack_check_nolock(task, NULL) != 0) {
			return TASK64_ERR_INVALID;
		}
	}
	return 0;
}

static void taskctl_reset_nolock(void)
{
	uint32_t i;
	uint32_t j;

	for (i = 0; i < MAX_TASKS64; i++) {
		taskctl64.tasks0[i].flags = TASK64_FLAGS_UNUSED;
		taskctl64.tasks0[i].stack_base = 0;
		taskctl64.tasks0[i].stack_size = 0;
		taskctl64.tasks0[i].stack_usable_base = 0;
		taskctl64.tasks0[i].stack_usable_size = 0;
		taskctl64.tasks0[i].stack_high_water = 0;
	}
	for (i = 0; i < MAX_TASKLEVELS64; i++) {
		taskctl64.level[i].running = 0;
		taskctl64.level[i].now = 0;
		for (j = 0; j < MAX_TASKS64_LV; j++) {
			taskctl64.level[i].tasks[j] = NULL;
		}
	}
	taskctl64.now_lv = 0;
	taskctl64.lv_change = 0;
	taskctl64.switches = 0;
}

static void task_idle64(void)
{
	for (;;) {
		platform_halt_with_irq64();
	}
}

int task_validate64(void)
{
	uint64_t irq_state;
	int status;

	irq_state = platform_irq_save64();
	status = task_validate_nolock();
	platform_irq_restore64(irq_state);
	return status;
}

int task_stack_check64(struct TASK64 *task, size_t *used)
{
	uint64_t irq_state;
	int status;

	if (used == NULL) {
		return TASK64_ERR_INVALID;
	}
	irq_state = platform_irq_save64();
	if (!task_pointer_valid_nolock(task) ||
			task->flags == TASK64_FLAGS_UNUSED) {
		status = TASK64_ERR_INVALID;
	} else {
		status = task_stack_check_nolock(task, used);
	}
	platform_irq_restore64(irq_state);
	return status;
}

struct TASK64 *task_now64(void)
{
	struct TASK64 *task;
	uint64_t irq_state;

	irq_state = platform_irq_save64();
	task = task_now_nolock();
	platform_irq_restore64(irq_state);
	return task;
}

struct TASK64 *task_alloc64(void)
{
	struct TASK64 *task;
	uint64_t irq_state;

	irq_state = platform_irq_save64();
	task = task_alloc_nolock();
	platform_irq_restore64(irq_state);
	return task;
}

int task_set_entry64(struct TASK64 *task, void (*entry)(void),
	uintptr_t stack_base, size_t stack_size)
{
	uintptr_t usable_base;
	size_t usable_size;
	uint64_t irq_state;
	int status;

	if (task == NULL || entry == NULL ||
			task_stack_parameters64(stack_base, stack_size, &usable_base,
			&usable_size) != 0) {
		return TASK64_ERR_INVALID;
	}
	irq_state = platform_irq_save64();
	if (!task_pointer_valid_nolock(task) ||
			task->flags != TASK64_FLAGS_ALLOCATED || task->stack_base != 0) {
		platform_irq_restore64(irq_state);
		return TASK64_ERR_INVALID;
	}
	task_stack_initialize64(stack_base, stack_size);
	status = platform_task_context_init64(&task->context, entry, usable_base,
		usable_size);
	if (status == 0) {
		task->stack_base = stack_base;
		task->stack_size = stack_size;
		task->stack_usable_base = usable_base;
		task->stack_usable_size = usable_size;
		task->stack_high_water = 0;
		status = task_stack_check_nolock(task, NULL);
		if (status != 0) {
			task->stack_base = 0;
			task->stack_size = 0;
			task->stack_usable_base = 0;
			task->stack_usable_size = 0;
			task->stack_high_water = 0;
			task->context.stack_pointer = 0;
		}
	}
	platform_irq_restore64(irq_state);
	return status;
}

int task_run64(struct TASK64 *task, int level, int priority)
{
	struct TASKLEVEL64 *new_level;
	enum TASK64_STATE old_state;
	uint64_t irq_state;
	uint32_t old_level;
	int status;

	if (task == NULL) {
		return TASK64_ERR_INVALID;
	}
	irq_state = platform_irq_save64();
	if (!task_pointer_valid_nolock(task) ||
			task->flags == TASK64_FLAGS_UNUSED) {
		status = TASK64_ERR_INVALID;
		goto out;
	}
	if (level < 0) {
		level = (int) task->level;
	}
	if (level >= MAX_TASKLEVELS64) {
		level = MAX_TASKLEVELS64 - 1;
	}
	old_level = task->level;
	old_state = task->flags;
	new_level = &taskctl64.level[level];
	if (task->flags == TASK64_FLAGS_RUNNING &&
			task->level != (uint32_t) level &&
			new_level->running >= MAX_TASKS64_LV) {
		status = TASK64_ERR_NOMEM;
		goto out;
	}
	if (priority > 0) {
		task->priority = (uint32_t) priority;
	}
	/* A pending task is still current and queued until the timer removes it. */
	if (task->flags == TASK64_FLAGS_SLEEP_PENDING) {
		if (task != task_now_nolock() ||
				task_queue_count_nolock(task, NULL, NULL) != 1) {
			status = TASK64_ERR_INVALID;
			goto out;
		}
		status = task_state_change_nolock(task, TASK64_FLAGS_RUNNING);
		goto out;
	}
	if (task->flags == TASK64_FLAGS_RUNNING &&
			task->level != (uint32_t) level) {
		status = task_remove_nolock(task);
		if (status != 0) {
			goto out;
		}
	}
	if (task->flags == TASK64_FLAGS_ALLOCATED) {
		task->level = (uint32_t) level;
		status = task_add_nolock(task);
		if (status != 0) {
			task->level = old_level;
			if (old_state == TASK64_FLAGS_RUNNING) {
				task_add_nolock(task);
			}
			goto out;
		}
	}
	taskctl64.lv_change = 1;
	status = 0;
out:
	platform_irq_restore64(irq_state);
	return status;
}

int task_sleep64(struct TASK64 *task)
{
	struct TASK64 *new_task;
	struct TASK64 *now_task;
	uint64_t irq_state;
	int status;

	irq_state = platform_irq_save64();
	if (task == NULL || !task_pointer_valid_nolock(task) ||
			task->flags != TASK64_FLAGS_RUNNING) {
		platform_irq_restore64(irq_state);
		return TASK64_ERR_INVALID;
	}
	now_task = task_now_nolock();
	if (task == now_task && task_stack_debug_check_nolock(task) != 0) {
		platform_irq_restore64(irq_state);
		return TASK64_ERR_INVALID;
	}
	if (task == now_task) {
		status = task_state_change_nolock(task,
			TASK64_FLAGS_SLEEP_PENDING);
		platform_irq_restore64(irq_state);
		if (status != 0) {
			return status;
		}
		if (platform_task_sleep_current64(task) != 0) {
			return 0;
		}
		irq_state = platform_irq_save64();
		if (task->flags != TASK64_FLAGS_SLEEP_PENDING) {
			platform_irq_restore64(irq_state);
			return 0;
		}
	}
	status = task_remove_nolock(task);
	if (status != 0 || task != now_task) {
		platform_irq_restore64(irq_state);
		return status;
	}
	task_switchsub_nolock();
	new_task = task_now_nolock();
	if (new_task == NULL) {
		task_add_nolock(task);
		task_switchsub_nolock();
		platform_irq_restore64(irq_state);
		return TASK64_ERR_INVALID;
	}
	if (new_task != now_task) {
		taskctl64.switches++;
		new_task->switches++;
	}
	platform_irq_restore64(irq_state);
	if (new_task != now_task) {
		platform_task_switch64(&now_task->context, &new_task->context);
	}
	return 0;
}

int task_kill64(struct TASK64 *task)
{
	uint64_t irq_state;
	uintptr_t stack_base;
	size_t stack_size;
	int status;

	irq_state = platform_irq_save64();
	if (task == NULL || !task_pointer_valid_nolock(task) ||
			task->flags == TASK64_FLAGS_UNUSED ||
			task == task_now_nolock()) {
		platform_irq_restore64(irq_state);
		return TASK64_ERR_INVALID;
	}
	if (task->flags == TASK64_FLAGS_RUNNING ||
			task->flags == TASK64_FLAGS_SLEEP_PENDING) {
		status = task_remove_nolock(task);
		if (status != 0) {
			platform_irq_restore64(irq_state);
			return status;
		}
	}
	status = task_state_change_nolock(task, TASK64_FLAGS_UNUSED);
	if (status != 0) {
		platform_irq_restore64(irq_state);
		return status;
	}
	stack_base = task->stack_base;
	stack_size = task->stack_size;
	task->stack_base = 0;
	task->stack_size = 0;
	task->stack_usable_base = 0;
	task->stack_usable_size = 0;
	task->stack_high_water = 0;
	task->context.stack_pointer = 0;
	platform_irq_restore64(irq_state);
	if (stack_base != 0 &&
			memman64_free_4k(&memman64, stack_base, stack_size) != 0) {
		return TASK64_ERR_INVALID;
	}
	return 0;
}

static struct TASK64 *task_switch_prepare_nolock(void)
{
	struct TASKLEVEL64 *level;
	struct TASK64 *new_task;
	struct TASK64 *now_task;
	uint32_t old_level;
	uint32_t old_lv_change;
	uint32_t old_now;

	if (taskctl64.now_lv >= MAX_TASKLEVELS64) {
		return NULL;
	}
	level = &taskctl64.level[taskctl64.now_lv];
	if (level->running == 0) {
		task_switchsub_nolock();
		level = &taskctl64.level[taskctl64.now_lv];
		if (level->running == 0) {
			return NULL;
		}
	}
	now_task = level->tasks[level->now];
	if (task_stack_debug_check_nolock(now_task) != 0) {
		return NULL;
	}
	if (now_task->flags == TASK64_FLAGS_SLEEP_PENDING) {
		if (task_remove_nolock(now_task) != 0) {
			return NULL;
		}
		task_switchsub_nolock();
		new_task = task_now_nolock();
		if (new_task != NULL) {
			taskctl64.switches++;
			new_task->switches++;
		}
		return new_task;
	}
	old_level = taskctl64.now_lv;
	old_lv_change = taskctl64.lv_change;
	old_now = level->now;
	level->now++;
	if (level->now >= level->running) {
		level->now = 0;
	}
	if (taskctl64.lv_change != 0) {
		task_switchsub_nolock();
		level = &taskctl64.level[taskctl64.now_lv];
	}
	new_task = level->tasks[level->now];
	if (task_stack_debug_check_nolock(new_task) != 0) {
		taskctl64.level[old_level].now = old_now;
		taskctl64.now_lv = old_level;
		taskctl64.lv_change = old_lv_change;
		return NULL;
	}
	if (new_task != now_task) {
		taskctl64.switches++;
		new_task->switches++;
	}
	return new_task;
}

struct TASK64 *task_switch_prepare64(void)
{
	struct TASK64 *task;
	uint64_t irq_state;

	irq_state = platform_irq_save64();
	task = task_switch_prepare_nolock();
	platform_irq_restore64(irq_state);
	return task;
}

void task_switch64(void)
{
	struct TASK64 *new_task;
	struct TASK64 *now_task;
	uint64_t irq_state;

	irq_state = platform_irq_save64();
	now_task = task_now_nolock();
	new_task = task_switch_prepare_nolock();
	platform_irq_restore64(irq_state);
	if (now_task != NULL && new_task != NULL && new_task != now_task) {
		platform_task_switch64(&now_task->context, &new_task->context);
	}
}

int task_init64(void)
{
	struct TASK64 *idle_task;
	struct TASK64 *main_task;
	uint64_t irq_state;
	uintptr_t idle_stack;
	int status;

	irq_state = platform_irq_save64();
	taskctl_reset_nolock();
	main_task = task_alloc_nolock();
	if (main_task == NULL || task_add_nolock(main_task) != 0) {
		taskctl_reset_nolock();
		platform_irq_restore64(irq_state);
		return -1;
	}
	task_switchsub_nolock();

	idle_task = task_alloc_nolock();
	if (idle_task == NULL) {
		taskctl_reset_nolock();
		platform_irq_restore64(irq_state);
		return -2;
	}
	idle_stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	if (idle_stack == 0) {
		taskctl_reset_nolock();
		platform_irq_restore64(irq_state);
		return -3;
	}
	status = task_set_entry64(idle_task, task_idle64, idle_stack,
		TASK64_STACK_SIZE);
	if (status != 0) {
		memman64_free_4k(&memman64, idle_stack, TASK64_STACK_SIZE);
		taskctl_reset_nolock();
		platform_irq_restore64(irq_state);
		return -4;
	}
	idle_task->level = MAX_TASKLEVELS64 - 1;
	if (task_add_nolock(idle_task) != 0) {
		memman64_free_4k(&memman64, idle_stack, TASK64_STACK_SIZE);
		taskctl_reset_nolock();
		platform_irq_restore64(irq_state);
		return -5;
	}
	platform_irq_restore64(irq_state);
	return 0;
}
