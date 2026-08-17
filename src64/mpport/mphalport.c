/*
 * mphalport.c -- MicroPython이 요구하는 HAL 구현
 *
 * 표준 입출력은 콘솔(console64)로, 시간은 PIT 틱으로 잇는다. 우리 콘솔은
 * VT100 escape를 해석하지 않으므로 커서 이동과 지우기는 백스페이스와
 * 공백으로 대신한다(mpconfigport.h의 MICROPY_HAL_HAS_VT100=0).
 */
#include "py/mphal.h"

#include <asmfunc64.h>
#include <console64.h>
#include <string.h>
#include <timer64.h>

int mp_hal_stdin_rx_chr(void)
{
	return console64_repl_getchar();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len)
{
	console64_write(str, (uint64_t) len);
	return len;
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
	/* console64_write가 줄바꿈 변환을 이미 해 준다. ports/unix가 cooked와
		 * uncooked를 같은 함수로 넘기는 것과 같은 이유다. */
	mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str)
{
	mp_hal_stdout_tx_strn(str, strlen(str));
}

mp_uint_t mp_hal_ticks_ms(void)
{
	return timerctl64.count * 10;
}

void mp_hal_delay_ms(mp_uint_t ms)
{
	uint64_t target;

	target = timerctl64.count + (ms / 10 + 1);
	while (timerctl64.count < target) {
		io_hlt();
	}
}

/*
 * MICROPY_HAL_HAS_VT100=0 (mpconfigport.h): 우리 콘솔은 escape 열을
 * 해석하지 않는다. 그래서 커서 이동과 지우기를 VT100 코드가 아니라
 * 백스페이스와 공백으로 처리한다.
 */
void mp_hal_move_cursor_back(unsigned int pos)
{
	unsigned int i;

	for (i = 0; i < pos; i++) {
		console64_write("\b", 1);
	}
}

void mp_hal_erase_line_from_cursor(unsigned int n_chars_to_erase)
{
	unsigned int i;

	for (i = 0; i < n_chars_to_erase; i++) {
		console64_write(" ", 1);
	}
	for (i = 0; i < n_chars_to_erase; i++) {
		console64_write("\b", 1);
	}
}
