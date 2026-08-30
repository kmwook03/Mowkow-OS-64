#ifndef MOWKOW64_MPHALPORT_H
#define MOWKOW64_MPHALPORT_H

/*
 * mp_hal_set_interrupt_char()는 여기(shared/runtime/interrupt_char.h)에
 * 선언되어 있고, pyexec.c가 그 함수를 부른다.
 */
#include "shared/runtime/interrupt_char.h"

/*
 * 서명은 py/mphal.h와 정확히 같아야 한다. 우리 고정 폭 타입이 아니라
 * mp_uint_t를 그대로 쓴다.
 */

int mp_hal_stdin_rx_chr(void);
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
mp_uint_t mp_hal_ticks_ms(void);
void mp_hal_delay_ms(mp_uint_t ms);

/*
 * MICROPY_HAL_HAS_VT100이 꺼져 있으면 readline.c가 이 함수들을 직접 부른다.
 * 이름과 서명이 업스트림이 기대하는 그대로여야 한다.
 */
void mp_hal_move_cursor_back(unsigned int pos);
void mp_hal_erase_line_from_cursor(unsigned int n_chars_to_erase);

#endif
