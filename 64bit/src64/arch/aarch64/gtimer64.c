/* EL1 physical generic timer, PPI 30, at 100 Hz. */
#include <arch/arch64.h>
#include <fifo64.h>
#include <stdint.h>

#define TIMER_HZ 100U

static uint64_t timer_ticks;
static uint32_t timer_interval;
static int timer_debug_output;

static void timer_reload64(void)
{
	__asm__ volatile ("msr cntp_tval_el0, %0" :: "r" ((uint64_t) timer_interval));
}

void arch64_timer_init(struct FIFO64 *fifo)
{
	uint64_t frequency;

	(void) fifo;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	timer_interval = (uint32_t) (frequency / TIMER_HZ);
	if (timer_interval == 0) {
		timer_interval = 1;
	}
	timer_ticks = 0;
	timer_debug_output = 1;
	timer_reload64();
	/* ENABLE=1, IMASK=0. */
	__asm__ volatile ("msr cntp_ctl_el0, %0\n\tisb" :: "r" (1ULL) : "memory");
}

void arch64_timer_set_debug_output(int enabled)
{
	timer_debug_output = enabled != 0;
}

uintptr_t arch64_timer_handle_irq(uintptr_t frame)
{
	/* Move the compare point first so the level interrupt deasserts before EOI. */
	timer_reload64();
	timer_ticks++;
	frame = arch64_scheduler_tick(frame);
	if (timer_debug_output != 0 && timer_ticks % TIMER_HZ == 0) {
		arch64_dbg_puts(arch64_scheduler_healthy() != 0 ? "M" : "?");
	}
	return frame;
}
