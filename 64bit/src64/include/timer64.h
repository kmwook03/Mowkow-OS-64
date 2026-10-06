#ifndef MOWKOW64_TIMER64_H
#define MOWKOW64_TIMER64_H

#include <fifo64.h>
#include <stdint.h>

struct TIMERCTL64 {
	uint64_t count;
	struct FIFO64 *fifo;
};

extern struct TIMERCTL64 timerctl64;

/* IRQ/PIT tick 초기화 전에도 쓸 수 있는 x86 polling deadline이다. */
uint64_t poll_deadline64(uint32_t milliseconds);
int poll_deadline_expired64(uint64_t deadline);
void init_pit64(struct FIFO64 *fifo);
void inthandler20_64(void);

#endif
