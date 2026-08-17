/*
 * keyboard64.c -- PS/2 키보드, IRQ1
 *
 * 스캔코드를 해독해 이벤트 큐에 넣는다. 확장 키(e0 접두)는 KEY64_EXT 비트를
 * 달아 한 값으로 만들어 주므로, 위쪽에서는 스캔코드 상태를 다시 볼 일이 없다.
 */
#include <asmfunc64.h>
#include <int64.h>
#include <keyboard64.h>

#define PORT_KEYDAT 0x0060
#define PORT_KEYSTA 0x0064
#define PORT_KEYCMD 0x0064
#define KEYSTA_SEND_NOTREADY 0x02
#define KEYCMD_WRITE_MODE 0x60
#define KBC_MODE 0x47

static struct FIFO64 *keyfifo64;
static uint8_t ext_pending;
static uint8_t skip_bytes;

static void wait_kbc_sendready64(void)
{
	while ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) != 0) {
	}
}

void init_keyboard64(struct FIFO64 *fifo)
{
	keyfifo64 = fifo;
	wait_kbc_sendready64();
	io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
	wait_kbc_sendready64();
	io_out8(PORT_KEYDAT, KBC_MODE);
}

int keyboard64_decode(uint8_t byte, uint16_t *out)
{
	if (skip_bytes != 0) {
		skip_bytes--;
		return 0;
	}
	if (byte == 0xe1) {
		/* Pause는 e1 1d 45 e1 9d c5 여섯 바이트로 온다. 통째로 버린다. */
		skip_bytes = 5;
		ext_pending = 0;
		return 0;
	}
	if (byte == 0xe0) {
		ext_pending = 1;
		return 0;
	}
	if (ext_pending != 0) {
		*out = (uint16_t) (byte | KEY64_EXT);
		ext_pending = 0;
	} else {
		*out = byte;
	}
	return 1;
}

void inthandler21_64(void)
{
	struct EVENT64 event;
	uint16_t key;
	uint8_t byte;

	io_out8(PIC0_OCW2, 0x61);
	byte = io_in8(PORT_KEYDAT);
	if (keyboard64_decode(byte, &key) == 0) {
		return;
	}
	event.type = EVENT64_KEYBOARD;
	event.data = key;
	fifo64_put(keyfifo64, event);
}
