#ifndef MOWKOW64_MPHALPORT_H
#define MOWKOW64_MPHALPORT_H

/*
 * mp_hal_set_interrupt_char() is declared here (shared/runtime/
 * interrupt_char.h), backed by shared/runtime/interrupt_char.c -- pyexec.c
 * calls it unconditionally when MICROPY_KBD_EXCEPTION is on.
 */
#include "shared/runtime/interrupt_char.h"

/*
 * Signatures must match py/mphal.h exactly (mp_uint_t, not our own
 * fixed-width types) -- verified by actually compiling against it
 * (python_porting.md Stage 1.4).
 */

int mp_hal_stdin_rx_chr(void);
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
mp_uint_t mp_hal_ticks_ms(void);
void mp_hal_delay_ms(mp_uint_t ms);

/*
 * readline.c calls these directly when MICROPY_HAL_HAS_VT100 is off,
 * expecting the port to supply them (see mpconfigport.h).
 */
void mp_hal_move_cursor_back(unsigned int pos);
void mp_hal_erase_line_from_cursor(unsigned int n_chars_to_erase);

#endif
