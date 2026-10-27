#ifndef MOWKOW64_MP_LIFETIME_PROBE64_H
#define MOWKOW64_MP_LIFETIME_PROBE64_H

#include <stdarg.h>
#include "py/runtime.h"

#ifndef P0_BOUNDARY64
#define P0_BOUNDARY64 0
#endif
#ifndef P0_COMPACT64
#define P0_COMPACT64 0
#endif

extern bool lifetime_enabled64;
mp_obj_t lifetime_msg64(const mp_obj_type_t *type, mp_rom_error_text_t msg);
mp_obj_t lifetime_vlist64(const mp_obj_type_t *type, mp_rom_error_text_t fmt, va_list args);
mp_obj_t lifetime_args64(const mp_obj_type_t *type, size_t count, const mp_obj_t *args);
bool lifetime_print64(const mp_print_t *print, mp_obj_t exception);
void lifetime_sweep64(void);
void lifetime_roots64(void);
void lifetime_run64(const char *mode, void *heap, size_t size);

#endif
