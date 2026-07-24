#ifndef MOWKOW64_CONSOLE64_H
#define MOWKOW64_CONSOLE64_H

#include <stdint.h>

struct BOOTINFO64;

void console64_set_hangul_font(const uint8_t *font);
void console64_init(const struct BOOTINFO64 *boot_info);
void console64_puts(const char *s);
void console64_write(const char *s, uint64_t len);
uint64_t console64_read(char *dst, uint64_t len);
void console64_process_key(uint8_t scancode);

#endif
