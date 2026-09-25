#include <keyboard64.h>

static int shift_down64;
static int ctrl_down64;
static int alt_down64;

int keyboard64_track_modifier(uint16_t key)
{
	switch (key) {
	case 0x2a: case 0x36: shift_down64 = 1; return 1;
	case 0xaa: case 0xb6: shift_down64 = 0; return 1;
	case 0x1d: case KEY64_RCTRL: ctrl_down64 = 1; return 1;
	case 0x9d: case (KEY64_RCTRL | 0x80): ctrl_down64 = 0; return 1;
	case 0x38: case KEY64_RALT: alt_down64 = 1; return 1;
	case 0xb8: case (KEY64_RALT | 0x80): alt_down64 = 0; return 1;
	default: return 0;
	}
}

int keyboard64_alt(void)
{
	return alt_down64;
}

int keyboard64_shift(void)
{
	return shift_down64;
}

int keyboard64_ctrl(void)
{
	return ctrl_down64;
}
