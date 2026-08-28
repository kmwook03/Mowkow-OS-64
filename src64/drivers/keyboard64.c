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
static int shift_down;
static int ctrl_down;

int keyboard64_track_modifier(uint8_t scancode)
{
	switch (scancode) {
	case 0x2a: case 0x36: shift_down = 1; return 1;
	case 0xaa: case 0xb6: shift_down = 0; return 1;
	case 0x1d: ctrl_down = 1; return 1;
	case 0x9d: ctrl_down = 0; return 1;
	default: return 0;
	}
}

int keyboard64_shift(void)
{
	return shift_down;
}

int keyboard64_ctrl(void)
{
	return ctrl_down;
}

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

void inthandler21_64(void)
{
	struct EVENT64 event;

	io_out8(PIC0_OCW2, 0x61);
	event.type = EVENT64_KEYBOARD;
	event.data = io_in8(PORT_KEYDAT);
	fifo64_put(keyfifo64, event);
}
