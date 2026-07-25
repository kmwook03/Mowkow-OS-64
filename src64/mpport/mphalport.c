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
	/* console64_write already handles newline translation; same
	 * reasoning as ports/unix's own cooked == uncooked forwarding. */
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
 * MICROPY_HAL_HAS_VT100=0 (mpconfigport.h): our console doesn't parse
 * escape sequences, so move the cursor and erase with plain backspaces
 * and spaces instead of VT100 codes.
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
