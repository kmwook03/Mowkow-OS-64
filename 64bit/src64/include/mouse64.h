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
	uint8_t discard;
};

/* Internal mouse event marker: a bad i8042 byte broke the packet stream. */
#define MOUSE64_DATA_ERROR 0x100U

/* 키보드 초기화 뒤에 호출한다. */
int init_mouse64(struct FIFO64 *fifo, struct MOUSE_DEC64 *mdec);
void inthandler2c_64(void);
/* 3바이트 패킷이 완성되면 1, 대기 중이면 0. */
int mouse64_decode(struct MOUSE_DEC64 *mdec, uint8_t dat);
void mouse64_decode_reset(struct MOUSE_DEC64 *mdec);
/* A corrupt byte was consumed: discard the remaining bytes of that packet. */
void mouse64_decode_error(struct MOUSE_DEC64 *mdec);

#endif
