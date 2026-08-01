#ifndef MOWKOW64_MOUSE64_H
#define MOWKOW64_MOUSE64_H

#include <fifo64.h>
#include <stdint.h>

struct MOUSE_DEC64 {
	uint8_t buf[3];
	int32_t phase;
	int32_t x;
	int32_t y;
	int32_t btn;
};

void init_mouse64(struct FIFO64 *fifo, struct MOUSE_DEC64 *mdec);
void inthandler2c_64(void);
/* 3바이트 패킷이 완성되면 1, 대기 중이면 0. */
int mouse64_decode(struct MOUSE_DEC64 *mdec, uint8_t dat);

#endif
