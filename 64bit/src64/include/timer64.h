#ifndef MOWKOW64_TIMER64_H
#define MOWKOW64_TIMER64_H

#include <fifo64.h>
#include <stdint.h>

struct TIMERCTL64 {
	uint64_t count;
	struct FIFO64 *fifo;
};

extern struct TIMERCTL64 timerctl64;

void init_pit64(struct FIFO64 *fifo);
void inthandler20_64(void);

#endif
