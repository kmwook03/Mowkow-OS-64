/*
 * mutex64.c -- task-context sleepable mutex
 *
 * Mutex state and scheduler sleep preparation share an IRQ-off boundary, so
 * unlock cannot be lost between enqueue and sleep. Wakeup happens after that
 * boundary is released. This is a uniprocessor primitive; SMP is out of scope.
 */
#include <arch/platform64.h>
#include <mutex64.h>
#include <stddef.h>

static struct TASK64 boot_owner;

static struct TASK64 *mutex_owner_current64(void)
{
	struct TASK64 *task;

	task = task_now64();
	return task != NULL ? task : &boot_owner;
}

static int waiter_index64(const struct MUTEX64 *mutex, struct TASK64 *task)
{
	uint32_t i;

	for (i = 0; i < mutex->waiter_count; i++) {
		if (mutex->waiters[i] == task) {
			return (int) i;
		}
	}
	return -1;
}

static void waiter_remove64(struct MUTEX64 *mutex, uint32_t index)
{
	if (index >= mutex->waiter_count) {
		return;
	}
	mutex->waiter_count--;
	for (; index < mutex->waiter_count; index++) {
		mutex->waiters[index] = mutex->waiters[index + 1];
	}
	mutex->waiters[mutex->waiter_count] = NULL;
}

void mutex64_init(struct MUTEX64 *mutex)
{
	uint32_t i;

	if (mutex == NULL) {
		return;
	}
	mutex->owner = NULL;
	mutex->waiter_count = 0;
	mutex->locked = 0;
	for (i = 0; i < MAX_TASKS64; i++) {
		mutex->waiters[i] = NULL;
	}
}

int mutex64_lock(struct MUTEX64 *mutex)
{
	struct TASK64 *owner;
	struct TASK64 *task;
	uint64_t irq_state;
	int index;
	int status;

	if (mutex == NULL) {
		return MUTEX64_ERR_INVALID;
	}
	for (;;) {
		task = task_now64();
		owner = task != NULL ? task : &boot_owner;
		irq_state = platform_irq_save64();
		if (mutex->locked == 0) {
			mutex->locked = 1;
			mutex->owner = owner;
			platform_irq_restore64(irq_state);
			return 0;
		}
		if (mutex->owner == owner) {
			platform_irq_restore64(irq_state);
			return MUTEX64_ERR_BUSY;
		}
		/* Before the scheduler exists only one boot context may execute. */
		if (task == NULL) {
			platform_irq_restore64(irq_state);
			return MUTEX64_ERR_BUSY;
		}
		index = waiter_index64(mutex, task);
		if (index < 0) {
			if (mutex->waiter_count >= MAX_TASKS64) {
				platform_irq_restore64(irq_state);
				return MUTEX64_ERR_BUSY;
			}
			mutex->waiters[mutex->waiter_count++] = task;
		}
		status = task_sleep_prepare64(task);
		if (status != 0) {
			index = waiter_index64(mutex, task);
			if (index >= 0) {
				waiter_remove64(mutex, (uint32_t) index);
			}
			platform_irq_restore64(irq_state);
			return status;
		}
		platform_irq_restore64(irq_state);
		status = task_sleep_commit64(task);
		if (status != 0) {
			irq_state = platform_irq_save64();
			index = waiter_index64(mutex, task);
			if (index >= 0) {
				waiter_remove64(mutex, (uint32_t) index);
			}
			platform_irq_restore64(irq_state);
			return status;
		}
	}
}

int mutex64_unlock(struct MUTEX64 *mutex)
{
	struct TASK64 *owner;
	struct TASK64 *wake;
	uint64_t irq_state;
	int status;

	if (mutex == NULL) {
		return MUTEX64_ERR_INVALID;
	}
	owner = mutex_owner_current64();
	irq_state = platform_irq_save64();
	if (mutex->locked == 0 || mutex->owner != owner) {
		platform_irq_restore64(irq_state);
		return MUTEX64_ERR_INVALID;
	}
	wake = mutex->waiter_count != 0 ? mutex->waiters[0] : NULL;
	if (wake != NULL) {
		waiter_remove64(mutex, 0);
	}
	mutex->owner = NULL;
	mutex->locked = 0;
	platform_irq_restore64(irq_state);
	/* Scheduler queue operations have their own IRQ protection. Wake only
	   after releasing the mutex wait-queue critical section. */
	status = wake != NULL ? task_run64(wake, -1, 0) : 0;
	return status;
}
