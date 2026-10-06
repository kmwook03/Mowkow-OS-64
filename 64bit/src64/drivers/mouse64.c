/*
 * PS/2 마우스 -- src/drivers/mouse.c의 64비트 이식.
 *
 * 키보드와 같은 i8042를 쓴다. 키보드 쪽 init_keyboard64가 이미 KBC_MODE 0x47로
 * 마우스 인터럽트까지 켜 두므로, 여기서는 마우스에 활성화 명령만 보낸다.
 */

#include <asmfunc64.h>
#include <fifo64.h>
#include <int64.h>
#include <mouse64.h>
#include <stddef.h>
#include <timer64.h>

#define PORT_KEYDAT 0x0060
#define PORT_KEYSTA 0x0064
#define PORT_KEYCMD 0x0064
#define KEYSTA_SEND_NOTREADY 0x02
#define KEYCMD_SENDTO_MOUSE  0xd4
#define MOUSECMD_ENABLE      0xf4
#define KBC_TIMEOUT_TICKS 20U
#define KBC_POLL_LIMIT 1000000U

static struct FIFO64 *mousefifo64;

static int wait_kbc_sendready64(void)
{
	uint64_t start;
	uint32_t polls;

	start = timerctl64.count;
	for (polls = 0; polls < KBC_POLL_LIMIT; polls++) {
		if ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) {
			return 0;
		}
		if (timerctl64.count - start >= KBC_TIMEOUT_TICKS) {
			break;
		}
	}
	return -1;
}

int init_mouse64(struct FIFO64 *fifo, struct MOUSE_DEC64 *mdec)
{
	if (fifo == NULL || mdec == NULL) {
		return -1;
	}
	mousefifo64 = fifo;
	if (wait_kbc_sendready64() != 0) {
		return -1;
	}
	io_out8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
	if (wait_kbc_sendready64() != 0) {
		return -1;
	}
	io_out8(PORT_KEYDAT, MOUSECMD_ENABLE);
	mdec->phase = 0;        /* ACK(0xfa) 대기 */
	mdec->x = 0;
	mdec->y = 0;
	mdec->btn = 0;
	return 0;
}

void inthandler2c_64(void)
{
	struct EVENT64 event;

	/* 슬레이브 먼저, 그다음 마스터 (캐스케이드 IRQ2). */
	io_out8(PIC1_OCW2, 0x64);
	io_out8(PIC0_OCW2, 0x62);
	event.type = EVENT64_MOUSE;
	event.data = io_in8(PORT_KEYDAT);
	fifo64_put(mousefifo64, event);
}

int mouse64_decode(struct MOUSE_DEC64 *mdec, uint8_t dat)
{
	if (mdec->phase == 0) {
		if (dat == 0xfa) {
			mdec->phase = 1;
		}
		return 0;
	}
	if (mdec->phase == 1) {
		if ((dat & 0xc8) == 0x08) {
			mdec->buf[0] = dat;
			mdec->phase = 2;
		}
		return 0;
	}
	if (mdec->phase == 2) {
		mdec->buf[1] = dat;
		mdec->phase = 3;
		return 0;
	}
	if (mdec->phase == 3) {
		mdec->buf[2] = dat;
		mdec->phase = 1;
		mdec->btn = mdec->buf[0] & 0x07;
		mdec->x = mdec->buf[1];
		mdec->y = mdec->buf[2];
		if ((mdec->buf[0] & 0x10) != 0) {
			mdec->x |= (int32_t) 0xffffff00;
		}
		if ((mdec->buf[0] & 0x20) != 0) {
			mdec->y |= (int32_t) 0xffffff00;
		}
		mdec->y = -mdec->y;     /* 화면 y축은 아래로 증가 */
		return 1;
	}
	return 0;
}
