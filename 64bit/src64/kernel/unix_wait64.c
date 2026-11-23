#include <arch/platform64.h>
#include <console64.h>
#include <mtask64.h>
#include <process64.h>
#include <uapi/unix64.h>
#include <unix_wait64.h>
#include <user_access64.h>

#define WAIT_PENDING64 1
#define TICK_NS64 10000000ULL

struct UNIX_WAITER64 {
	struct TASK64 *task;
	struct PROCESS64 *process;
	struct ADDRESS_SPACE64 *space;
	uintptr_t address;
	uint64_t start;
	uint64_t duration;
	int timed;
	int result;
};

/* Single-core IRQ-save protects registry/condition/sleep admission. There is
 * no queue lock across scheduler admission, nor allocation under this guard.
 * A waiter's task/process remain owned by its suspended syscall continuation.
 */
static struct UNIX_WAITER64 waiters64[MAX_TASKS64];
static uint64_t ticks64;

static void wake_waiter64(struct UNIX_WAITER64 *waiter, int result)
{
	waiter->result = result;
	/* Retain the entry even on admission failure; the next tick retries. */
	(void) task_run64(waiter->task, -1, 0);
}

void unix_wait_tick64(uint64_t ticks)
{
	uint64_t irq = platform_irq_save64();

	ticks64 = ticks;
	process64_wake_tick64();
	for (size_t i = 0; i < MAX_TASKS64; i++) {
		struct UNIX_WAITER64 *waiter = &waiters64[i];

		if (waiter->task == NULL) { continue; }
		if (waiter->result != WAIT_PENDING64) {
			wake_waiter64(waiter, waiter->result);
		} else if (console64_close_requested(waiter->process->console) ||
			process64_signal_pending64(waiter->process)) {
			wake_waiter64(waiter, -4);
		} else if (waiter->timed && ticks - waiter->start >= waiter->duration) {
			wake_waiter64(waiter, -110);
		}
	}
	platform_irq_restore64(irq);
}

int unix_clock_get64(uintptr_t output)
{
	uint64_t irq = platform_irq_save64();
	uint64_t now = ticks64;
	struct UNIX_TIMESPEC64 result = {
		.seconds = (int64_t) (now / 100),
		.nanoseconds = (int64_t) (now % 100 * TICK_NS64)
	};

	platform_irq_restore64(irq);
	return copy_to_user64((void *) output, &result, sizeof(result));
}

int unix_futex_wait64(uintptr_t address, int32_t expected, uintptr_t timeout)
{
	struct USER_ACCESS64 access = {0};
	struct PROCESS64 *process = process64_current();
	struct TASK64 *task = task_now64();
	struct UNIX_WAITER64 *waiter = NULL;
	uint64_t duration = 0, irq;
	int32_t value;
	int status;

	if (process == NULL || task == NULL || address % sizeof(value)) { return -22; }
	if (timeout != 0) {
		struct UNIX_TIMESPEC64 time;

		status = copy_from_user64(&time, (const void *) timeout, sizeof(time));
		if (status != 0) { return status; }
		if (time.seconds < 0 || time.nanoseconds < 0 || time.nanoseconds >= 1000000000 ||
			(uint64_t) time.seconds > ((uint64_t) INT64_MAX - 101) / 100) { return -22; }
		duration = (uint64_t) time.seconds * 100 +
			((uint64_t) time.nanoseconds + TICK_NS64 - 1) / TICK_NS64;
		/* Tick-phase uncertainty must not shorten a positive relative wait. */
		if (duration != 0) { duration++; }
	}
	status = user_access_begin64(&access, (const void *) address, sizeof(value), USER_READ64);
	if (status != 0) { return status; }
	irq = platform_irq_save64();
	status = copy_from_user64(&value, (const void *) address, sizeof(value));
	if (status == 0 && value != expected) { status = -11; }
	if (status == 0 && (console64_close_requested(process->console) ||
		process64_signal_pending64(process))) { status = -4; }
	if (status == 0 && timeout != 0 && duration == 0) { status = -110; }
	if (status != 0) { goto unlock; }
	for (size_t i = 0; i < MAX_TASKS64; i++) {
		if (waiters64[i].task == NULL) { waiter = &waiters64[i]; break; }
	}
	if (waiter == NULL) { status = -11; goto unlock; }
	*waiter = (struct UNIX_WAITER64) {
		.task = task, .process = process, .space = access.space,
		.address = address, .start = ticks64, .duration = duration,
		.timed = timeout != 0, .result = WAIT_PENDING64
	};
	status = task_sleep_prepare64(task);
	if (status != 0) { *waiter = (struct UNIX_WAITER64) {0}; goto unlock; }
	platform_irq_restore64(irq);
	status = task_sleep_commit64(task);
	irq = platform_irq_save64();
	if (status == 0) {
		/* Unrelated wakes are permitted as spurious futex returns. Close
		 * must unwind before the ordinary user-return termination boundary.
		 */
		status = (console64_close_requested(process->console) ||
			process64_signal_pending64(process)) ? -4 :
			(waiter->result == WAIT_PENDING64 ? 0 : waiter->result);
	}
	*waiter = (struct UNIX_WAITER64) {0};
unlock:
	platform_irq_restore64(irq);
	user_access_end64(&access);
	return status;
}

int unix_futex_wake64(uintptr_t address)
{
	struct USER_ACCESS64 access = {0};
	uint64_t irq;
	int count = 0;
	int status;

	if (address % sizeof(int32_t)) { return -22; }
	status = user_access_begin64(&access, (const void *) address, sizeof(int32_t), USER_READ64);
	if (status != 0) { return status; }
	irq = platform_irq_save64();
	for (size_t i = 0; i < MAX_TASKS64; i++) {
		struct UNIX_WAITER64 *waiter = &waiters64[i];

		if (waiter->task == NULL || waiter->space != access.space ||
			waiter->address != address || waiter->result != WAIT_PENDING64) { continue; }
		wake_waiter64(waiter, 0);
		count++;
	}
	platform_irq_restore64(irq);
	user_access_end64(&access);
	return count;
}
