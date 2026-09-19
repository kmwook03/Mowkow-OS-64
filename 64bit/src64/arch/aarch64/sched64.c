/* AArch64 exception-return adapter for the shared mtask64 scheduler. */
#include <arch/arch64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_QUANTUM_TICKS 10U
#define SPSR_EL1H 0x5ULL

struct IRQ_FRAME64 {
	uint64_t x[31];
	uint64_t elr;
	uint64_t spsr;
	uint64_t reserved;
};

_Static_assert(sizeof (struct IRQ_FRAME64) == 272,
	"vectors64.S IRQ frame layout mismatch");

static volatile uint64_t main_beats;
static volatile uint64_t worker_beats;
static volatile uint64_t sleep_cycles;
static volatile uint64_t wake_cycles;
static uint64_t scheduler_ticks;
static struct TASK64 *worker_task;

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
	struct IRQ_FRAME64 *frame;
	uintptr_t stack_top;
	unsigned int i;

	if (entry == NULL || stack_base == 0 || stack_size < sizeof *frame) {
		return 0;
	}
	stack_top = (stack_base + stack_size) & ~(uintptr_t) 0x0f;
	frame = (struct IRQ_FRAME64 *) (stack_top - sizeof *frame);
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
	uintptr_t worker_stack;

	init_memory64();
	task_init64();
	main_beats = 0;
	worker_beats = 0;
	sleep_cycles = 0;
	wake_cycles = 0;
	scheduler_ticks = 0;
	worker_task = NULL;

	worker = task_alloc64();
	worker_stack = memman64_alloc_4k(&memman64, TASK64_STACK_SIZE);
	if (worker != NULL && worker_stack != 0 &&
			task_set_entry64(worker, worker_task64, worker_stack,
			TASK64_STACK_SIZE) == 0) {
		worker_task = worker;
		task_run64(worker, 0, 1);
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
