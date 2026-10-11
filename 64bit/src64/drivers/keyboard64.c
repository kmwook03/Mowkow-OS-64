/*
 * keyboard64.c -- PS/2 키보드, IRQ1
 *
 * 스캔코드를 해독해 이벤트 큐에 넣는다. 확장 키(e0 접두)는 KEY64_EXT 비트를
 * 달아 한 값으로 만들어 주므로, 위쪽에서는 스캔코드 상태를 다시 볼 일이 없다.
 */
#include <asmfunc64.h>
#include <arch/platform64.h>
#include <int64.h>
#include <keyboard64.h>
#include <stddef.h>
#include <timer64.h>

#define PORT_KEYDAT 0x0060
#define PORT_KEYSTA 0x0064
#define PORT_KEYCMD 0x0064
#define KEYSTA_SEND_NOTREADY 0x02
#define KEYSTA_TIMEOUT_ERROR 0x40
#define KEYSTA_PARITY_ERROR  0x80
#define KEYCMD_WRITE_MODE 0x60
#define KBC_MODE 0x47
#define KBC_TIMEOUT_MS 200U
#define KBC_POLL_LIMIT 1000000U

static struct FIFO64 *keyfifo64;
static uint8_t ext_pending;
static uint8_t pause_index;
static int shift_down;
static int ctrl_down;
static int alt_down;

/* 오른쪽 Ctrl/Alt는 e0 접두가 붙어 KEY64_EXT 비트를 달고 온다. 왼쪽 키와
   같은 상태를 공유한다 -- 앱은 어느 쪽을 눌렀는지 구분하지 않는다. */
int keyboard64_track_modifier(uint16_t key)
{
	switch (key) {
	case 0x2a: shift_down |= 1; return 1;
	case 0x36: shift_down |= 2; return 1;
	case 0xaa: shift_down &= ~1; return 1;
	case 0xb6: shift_down &= ~2; return 1;
	case 0x1d: ctrl_down |= 1; return 1;
	case KEY64_RCTRL: ctrl_down |= 2; return 1;
	case 0x9d: ctrl_down &= ~1; return 1;
	case (KEY64_RCTRL | 0x80): ctrl_down &= ~2; return 1;
	case 0x38: alt_down |= 1; return 1;
	case KEY64_RALT: alt_down |= 2; return 1;
	case 0xb8: alt_down &= ~1; return 1;
	case (KEY64_RALT | 0x80): alt_down &= ~2; return 1;
	default: return 0;
	}
}

int keyboard64_alt(void)
{
	return alt_down != 0;
}

int keyboard64_shift(void)
{
	return shift_down != 0;
}

int keyboard64_ctrl(void)
{
	return ctrl_down != 0;
}

static int wait_kbc_sendready64(void)
{
	uint64_t deadline;
	uint32_t polls;
	uint8_t status;

	deadline = poll_deadline64(KBC_TIMEOUT_MS);
	for (polls = 0; polls < KBC_POLL_LIMIT; polls++) {
		status = io_in8(PORT_KEYSTA);
		if ((status & (KEYSTA_TIMEOUT_ERROR | KEYSTA_PARITY_ERROR)) != 0) {
			return -1;
		}
		if ((status & KEYSTA_SEND_NOTREADY) == 0) {
			return 0;
		}
		if (poll_deadline_expired64(deadline) != 0) {
			break;
		}
	}
	return -1;
}

uint8_t keyboard64_controller_status(void)
{
	return io_in8(PORT_KEYSTA);
}

int init_keyboard64(struct FIFO64 *fifo)
{
	uint64_t irq_state;

	if (fifo == NULL) {
		return -1;
	}
	irq_state = platform_irq_save64();
	keyfifo64 = fifo;
	keyboard64_decode_reset();
	platform_irq_restore64(irq_state);
	if (wait_kbc_sendready64() != 0) {
		return -1;
	}
	io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
	if (wait_kbc_sendready64() != 0) {
		return -1;
	}
	io_out8(PORT_KEYDAT, KBC_MODE);
	return 0;
}

void keyboard64_decode_reset(void)
{
	ext_pending = 0;
	pause_index = 0;
}

static int extended_key64(uint8_t byte)
{
	switch (byte & 0x7fU) {
	case 0x1c: case 0x1d: case 0x35: case 0x37: case 0x38:
	case 0x47: case 0x48: case 0x49: case 0x4b: case 0x4d:
	case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x5b: case 0x5c: case 0x5d:
		return 1;
	default:
		return 0;
	}
}

int keyboard64_decode(uint8_t byte, uint16_t *out)
{
	static const uint8_t pause64[] = { 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5 };

	if (out == NULL) {
		return 0;
	}
	if (pause_index != 0) {
		if (byte == pause64[pause_index]) {
			pause_index++;
			if (pause_index == sizeof(pause64)) {
				pause_index = 0;
			}
			return 0;
		}
		/* A truncated/malformed Pause must not swallow the next real key. */
		keyboard64_decode_reset();
	}
	if (byte == 0xe1) {
		/* Pause는 e1 1d 45 e1 9d c5 여섯 바이트로 온다. 통째로 버린다. */
		pause_index = 1;
		ext_pending = 0;
		return 0;
	}
	if (byte == 0xe0) {
		ext_pending = 1;
		return 0;
	}
	if (ext_pending != 0) {
		ext_pending = 0;
		/* PrintScreen's fake shifts do not alter the real modifier state. */
		if ((byte & 0x7fU) == 0x2a || (byte & 0x7fU) == 0x36) {
			return 0;
		}
		if (extended_key64(byte)) {
			*out = (uint16_t) (byte | KEY64_EXT);
			return 1;
		}
		/* Unknown multimedia keys must not become ordinary letter presses. */
		return 0;
	}
	if ((byte & 0x7fU) == 0 || (byte & 0x7fU) > 0x58U) {
		return 0;
	}
	*out = byte;
	return 1;
}

void inthandler21_64(void)
{
	struct EVENT64 event;
	uint16_t key;
	uint8_t byte;
	uint8_t status;

	io_out8(PIC0_OCW2, 0x61);
	status = io_in8(PORT_KEYSTA);
	byte = io_in8(PORT_KEYDAT);
	if (status & (KEYSTA_TIMEOUT_ERROR | KEYSTA_PARITY_ERROR)) {
		keyboard64_decode_reset();
		return;
	}
	if (keyboard64_decode(byte, &key) == 0) {
		return;
	}
	event.type = EVENT64_KEYBOARD;
	event.data = key;
	fifo64_put(keyfifo64, event);
}
