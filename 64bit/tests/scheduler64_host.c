#include <arch/platform64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint64_t irq_enabled = 1;
static int context_init_fail;
static int wake_during_sleep;
static uint32_t context_switches;
_Alignas(TASK64_STACK_ALIGNMENT)
static uint8_t memory_pool[0x300000];

uint64_t platform_irq_save64(void)
{
	uint64_t previous;

	previous = irq_enabled;
	irq_enabled = 0;
	return previous;
}

void platform_irq_restore64(uint64_t state)
{
	irq_enabled = state;
}

void platform_irq_disable64(void)
{
	irq_enabled = 0;
}

void platform_irq_enable64(void)
{
	irq_enabled = 1;
}

void platform_halt64(void)
{
}

void platform_halt_with_irq64(void)
{
}

void platform_wait_for_interrupt64(void)
{
}

int platform_task_context_init64(struct CONTEXT64 *context,
	void (*entry)(void), uintptr_t stack_base, size_t stack_size)
{
	if (context == NULL || entry == NULL || stack_base == 0 ||
			stack_size == 0) {
		return TASK64_ERR_INVALID;
	}
	if (context_init_fail != 0) {
		return TASK64_ERR_INVALID;
	}
	context->stack_pointer = stack_base + stack_size;
	return 0;
}

int platform_task_sleep_current64(struct TASK64 *task)
{
	if (wake_during_sleep != 0) {
		CHECK64(task_run64(task, -1, 0) == 0);
		return 1;
	}
	return 0;
}

void platform_task_switch64(struct CONTEXT64 *old_context,
	struct CONTEXT64 *new_context)
{
	(void) old_context;
	(void) new_context;
	context_switches++;
}

static void test_entry64(void)
{
}

static int test_init_rollback64(void)
{
	size_t free_before;

	memman64_init(&memman64);
	CHECK64(memman64_add_pool(&memman64, (uintptr_t) memory_pool,
		TASK64_STACK_SIZE / 2U) == 0);
	free_before = memman64_total(&memman64);
	CHECK64(task_init64() == -3);
	CHECK64(memman64_total(&memman64) == free_before);
	CHECK64(task_validate64() == 0);

	memman64_init(&memman64);
	CHECK64(memman64_add_pool(&memman64, (uintptr_t) memory_pool,
		sizeof memory_pool) == 0);
	free_before = memman64_total(&memman64);
	context_init_fail = 1;
	CHECK64(task_init64() == -4);
	context_init_fail = 0;
	CHECK64(memman64_total(&memman64) == free_before);
	CHECK64(task_validate64() == 0);
	return 0;
}

static int init_scheduler64(void)
{
	memman64_init(&memman64);
	CHECK64(memman64_add_pool(&memman64, (uintptr_t) memory_pool,
		sizeof memory_pool) == 0);
	CHECK64(task_init64() == 0);
	CHECK64(task_validate64() == 0);
	CHECK64(irq_enabled == 1);
	return 0;
}

static int test_stack_contract64(void)
{
	struct TASK64 *task;
	uint8_t *bytes;
	size_t free_before;
	size_t used;
	uintptr_t stack;
	uint8_t guard;

	free_before = memman64_total(&memman64);
	task = task_alloc64();
	stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	CHECK64(task != NULL && stack != 0);
	CHECK64(task_set_entry64(task, test_entry64, stack,
		TASK64_STACK_MIN_SIZE - 16U) == TASK64_ERR_INVALID);
	CHECK64(task_set_entry64(task, test_entry64, stack + 1U,
		TASK64_STACK_SIZE) == TASK64_ERR_INVALID);
	CHECK64(task_set_entry64(task, test_entry64, stack,
		TASK64_STACK_SIZE - 16U) == TASK64_ERR_INVALID);
	CHECK64(task_set_entry64(task, test_entry64,
		UINTPTR_MAX - (TASK64_STACK_ALIGNMENT - 1U),
		TASK64_STACK_SIZE) == TASK64_ERR_INVALID);
	CHECK64(task_set_entry64(task, test_entry64, stack,
		TASK64_STACK_SIZE) == 0);
	CHECK64(task->stack_usable_base ==
		stack + TASK64_STACK_GUARD_SIZE);
	CHECK64(task->stack_usable_size ==
		TASK64_STACK_SIZE - TASK64_STACK_GUARD_SIZE);
	CHECK64(task_stack_check64(task, &used) == 0 && used == 0);
	bytes = (uint8_t *) task->stack_usable_base;
	bytes[task->stack_usable_size - 128U] = 0;
	CHECK64(task_stack_check64(task, &used) == 0 && used == 128U);
	bytes[task->stack_usable_size - 64U] = 0;
	CHECK64(task_stack_check64(task, &used) == 0 && used == 128U);
	guard = ((uint8_t *) stack)[0];
	((uint8_t *) stack)[0] ^= 1U;
	CHECK64(task_stack_check64(task, &used) == TASK64_ERR_INVALID);
	CHECK64(task_validate64() == TASK64_ERR_INVALID);
	((uint8_t *) stack)[0] = guard;
	CHECK64(task_validate64() == 0);
	CHECK64(task_set_entry64(task, test_entry64, stack,
		TASK64_STACK_SIZE) == TASK64_ERR_INVALID);
	CHECK64(task_kill64(task) == 0);
	CHECK64(memman64_total(&memman64) == free_before);
	return 0;
}

static int test_sleep_wake64(void)
{
	struct TASK64 *main_task;
	struct TASK64 *next;
	uint32_t switches_before;

	main_task = task_now64();
	CHECK64(main_task != NULL);
	/* A wake between wait-queue insertion and commit must cancel sleep. */
	switches_before = context_switches;
	CHECK64(task_sleep_prepare64(main_task) == 0);
	CHECK64(main_task->flags == TASK64_FLAGS_SLEEP_PENDING);
	CHECK64(task_run64(main_task, -1, 0) == 0);
	CHECK64(task_sleep_commit64(main_task) == 0);
	CHECK64(main_task->flags == TASK64_FLAGS_RUNNING);
	CHECK64(context_switches == switches_before);
	wake_during_sleep = 1;
	CHECK64(task_sleep64(main_task) == 0);
	CHECK64(main_task->flags == TASK64_FLAGS_RUNNING);
	CHECK64(task_validate64() == 0);

	wake_during_sleep = 0;
	CHECK64(task_sleep64(main_task) == 0);
	CHECK64(main_task->flags == TASK64_FLAGS_ALLOCATED);
	CHECK64(context_switches == 1);
	CHECK64(task_validate64() == 0);
	CHECK64(task_run64(main_task, 0, 1) == 0);
	next = task_switch_prepare64();
	CHECK64(next == main_task);
	CHECK64(task_validate64() == 0);
	return 0;
}

static int test_entry_and_kill64(void)
{
	struct TASK64 *task;
	size_t free_before;
	uintptr_t stack;

	free_before = memman64_total(&memman64);
	task = task_alloc64();
	stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	CHECK64(task != NULL && stack != 0);
	CHECK64(task_set_entry64(task, test_entry64, stack,
		TASK64_STACK_SIZE) == 0);
	CHECK64(task_run64(task, 1, 3) == 0);
	CHECK64(task->flags == TASK64_FLAGS_RUNNING);
	CHECK64(task_sleep64(task) == 0);
	CHECK64(task->flags == TASK64_FLAGS_ALLOCATED);
	CHECK64(task_kill64(task) == 0);
	CHECK64(task->flags == TASK64_FLAGS_UNUSED);
	CHECK64(memman64_total(&memman64) == free_before);
	CHECK64(task_validate64() == 0);
	return 0;
}

static int test_queue_capacity64(void)
{
	struct TASK64 *full[MAX_TASKS64_LV];
	struct TASK64 *extra;
	uint32_t i;

	for (i = 0; i < MAX_TASKS64_LV; i++) {
		full[i] = task_alloc64();
		CHECK64(full[i] != NULL);
		CHECK64(task_run64(full[i], 2, 1) == 0);
	}
	extra = task_alloc64();
	CHECK64(extra != NULL);
	CHECK64(task_run64(extra, 2, 1) == TASK64_ERR_NOMEM);
	CHECK64(extra->flags == TASK64_FLAGS_ALLOCATED);
	CHECK64(task_run64(extra, 3, 1) == 0);
	CHECK64(task_run64(extra, 2, 1) == TASK64_ERR_NOMEM);
	CHECK64(extra->flags == TASK64_FLAGS_RUNNING && extra->level == 3);
	CHECK64(task_validate64() == 0);

	CHECK64(task_kill64(extra) == 0);
	for (i = 0; i < MAX_TASKS64_LV; i++) {
		CHECK64(task_kill64(full[i]) == 0);
	}
	CHECK64(task_validate64() == 0);
	return 0;
}

static int test_invariant_detection64(void)
{
	struct TASK64 *task;
	struct TASKLEVEL64 *level;

	task = task_alloc64();
	CHECK64(task != NULL);
	CHECK64(task_run64(task, 4, 1) == 0);
	level = &taskctl64.level[4];
	CHECK64(level->running == 1);
	level->tasks[1] = task;
	level->running = 2;
	CHECK64(task_validate64() < 0);
	level->running = 1;
	level->tasks[1] = NULL;
	CHECK64(task_validate64() == 0);
	CHECK64(task_kill64(task) == 0);
	CHECK64(task_kill64(task_now64()) < 0);
	CHECK64(task_validate64() == 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_init_rollback64();
	if (status != 0) {
		return status;
	}
	status = init_scheduler64();
	if (status != 0) {
		return status;
	}
	status = test_sleep_wake64();
	if (status != 0) {
		return status;
	}
	status = test_stack_contract64();
	if (status != 0) {
		return status;
	}
	status = test_entry_and_kill64();
	if (status != 0) {
		return status;
	}
	status = test_queue_capacity64();
	if (status != 0) {
		return status;
	}
	return test_invariant_detection64();
}
