/* AArch64 exception-return adapter for the shared mtask64 scheduler. */
#include <arch/arch64.h>
#include <arch/exception_frame64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_QUANTUM_TICKS 10U
#define SPSR_EL1H 0x5ULL

static volatile uint64_t main_beats;
static volatile uint64_t worker_beats;
static volatile uint64_t sleep_cycles;
static volatile uint64_t wake_cycles;
static uint64_t scheduler_ticks;
static struct TASK64 *worker_task;
static struct TASK64 *fp_task_a_task;
static struct TASK64 *fp_task_b_task;

volatile uint64_t fp_context_a_beats;
volatile uint64_t fp_context_b_beats;
volatile uint64_t fp_context_errors;

extern void fp_context_task_a64(void);
extern void fp_context_task_b64(void);

static void worker_task64(void)
{
	for (;;) {
		worker_beats++;
		if ((worker_beats & 0xffffU) == 0) {
			sleep_cycles++;
			task_sleep64(task_now64());
		}
		__asm__ volatile ("yield");
	}
}

uintptr_t arch64_task_frame_init(void (*entry)(void), uintptr_t stack_base,
	size_t stack_size)
{
	struct ARCH64_EXCEPTION_FRAME *frame;
	uintptr_t stack_top;
	unsigned int i;

	if (entry == NULL || stack_base == 0 || stack_size < sizeof *frame) {
		return 0;
	}
	stack_top = (stack_base + stack_size) & ~(uintptr_t) 0x0f;
	frame = (struct ARCH64_EXCEPTION_FRAME *) (stack_top - sizeof *frame);
	for (i = 0; i < sizeof *frame / sizeof (uint64_t); i++) {
		((uint64_t *) frame)[i] = 0;
	}
	frame->elr = (uintptr_t) entry;
	frame->spsr = SPSR_EL1H;
	return (uintptr_t) frame;
}

void arch64_scheduler_init(void)
{
	struct TASK64 *worker;
	struct TASK64 *fp_task_a;
	struct TASK64 *fp_task_b;
	uintptr_t worker_stack;
	uintptr_t fp_stack_a;
	uintptr_t fp_stack_b;

	task_init64();
	main_beats = 0;
	worker_beats = 0;
	sleep_cycles = 0;
	wake_cycles = 0;
	scheduler_ticks = 0;
	worker_task = NULL;
	fp_task_a_task = NULL;
	fp_task_b_task = NULL;
	fp_context_a_beats = 0;
	fp_context_b_beats = 0;
	fp_context_errors = 0;

	worker = task_alloc64();
	worker_stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	if (worker != NULL && worker_stack != 0 &&
			task_set_entry64(worker, worker_task64, worker_stack,
			TASK64_STACK_SIZE) == 0) {
		worker_task = worker;
		task_run64(worker, 0, 1);
	}

	fp_task_a = task_alloc64();
	fp_task_b = task_alloc64();
	fp_stack_a = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	fp_stack_b = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	if (fp_task_a != NULL && fp_task_b != NULL && fp_stack_a != 0 &&
			fp_stack_b != 0 && task_set_entry64(fp_task_a,
			fp_context_task_a64, fp_stack_a, TASK64_STACK_SIZE) == 0 &&
			task_set_entry64(fp_task_b, fp_context_task_b64, fp_stack_b,
			TASK64_STACK_SIZE) == 0) {
		task_run64(fp_task_a, 0, 1);
		task_run64(fp_task_b, 0, 1);
		fp_task_a_task = fp_task_a;
		fp_task_b_task = fp_task_b;
	} else {
		fp_context_errors = 1;
	}
}

uintptr_t arch64_scheduler_tick(uintptr_t frame)
{
	struct TASK64 *current;
	struct TASK64 *next;

	scheduler_ticks++;
	current = task_now64();
	if (current == NULL) {
		return frame;
	}
	current->context.frame = frame;
	if (current->flags != TASK64_FLAGS_SLEEP_PENDING &&
			scheduler_ticks % SCHEDULER_QUANTUM_TICKS != 0) {
		return frame;
	}
	next = task_switch_prepare64();
	if (next == NULL || next->context.frame == 0) {
		return frame;
	}
	return next->context.frame;
}

void arch64_scheduler_main_beat(void)
{
	main_beats++;
	if (worker_task != NULL &&
			worker_task->flags == TASK64_FLAGS_ALLOCATED) {
		task_run64(worker_task, 0, 1);
		wake_cycles++;
	}
}

int arch64_scheduler_healthy(void)
{
	return taskctl64.switches >= 2 && main_beats != 0 && worker_beats != 0 &&
		sleep_cycles != 0 && wake_cycles != 0;
}

int arch64_fp_context_self_test(void)
{
	if (fp_context_errors != 0) {
		return -1;
	}
	if (fp_task_a_task == NULL || fp_task_b_task == NULL ||
			fp_context_a_beats < 16 || fp_context_b_beats < 16 ||
			fp_task_a_task->switches < 2 || fp_task_b_task->switches < 2) {
		return 0;
	}
	if (task_kill64(fp_task_a_task) != 0 ||
			task_kill64(fp_task_b_task) != 0) {
		return -1;
	}
	fp_task_a_task = NULL;
	fp_task_b_task = NULL;
	return 1;
}
