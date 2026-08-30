#ifndef MOWKOW64_FIFO64_H
#define MOWKOW64_FIFO64_H

#include <stdint.h>

#define FIFO64_FLAGS_OVERRUN 0x0001

struct TASK64;

enum EVENT64_TYPE {
	EVENT64_TIMER = 1,
	EVENT64_KEYBOARD = 2,
	EVENT64_MOUSE = 3,
};

struct EVENT64 {
	uint32_t type;
	uint32_t data;
};

struct FIFO64 {
	uint32_t size;
	uint32_t free;
	uint32_t flags;
	uint32_t p;
	uint32_t q;
	struct EVENT64 *buf;
	struct TASK64 *task;
};

void fifo64_init(struct FIFO64 *fifo, uint32_t size, struct EVENT64 *buf, struct TASK64 *task);
int fifo64_put(struct FIFO64 *fifo, struct EVENT64 data);
int fifo64_get(struct FIFO64 *fifo, struct EVENT64 *data);
uint32_t fifo64_status(const struct FIFO64 *fifo);

#endif
