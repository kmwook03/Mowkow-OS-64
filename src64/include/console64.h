#ifndef MOWKOW64_CONSOLE64_H
#define MOWKOW64_CONSOLE64_H

#include <stdint.h>

void console64_init(uintptr_t vram, uint16_t width, uint16_t height);
void console64_puts(const char *s);
void console64_process_key(uint8_t scancode);

#endif
