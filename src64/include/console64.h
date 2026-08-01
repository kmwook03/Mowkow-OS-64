#ifndef MOWKOW64_CONSOLE64_H
#define MOWKOW64_CONSOLE64_H

#include <stdint.h>

struct BOOTINFO64;
struct FIFO64;
struct SHEET64;

void console64_set_hangul_font(const uint8_t *font);
const uint8_t *console64_hangul_font(void);
void console64_attach_sheet(struct SHEET64 *sht, uint16_t ox, uint16_t oy,
	uint16_t w, uint16_t h);
void console64_init(const struct BOOTINFO64 *boot_info);
void console64_puts(const char *s);
void console64_prompt(void);
void console64_write(const char *s, uint64_t len);
uint64_t console64_read(char *dst, uint64_t len);
void console64_process_key(uint8_t scancode);
void console64_set_event_fifo(struct FIFO64 *fifo);
void console64_repl_set_active(int active);
int console64_repl_getchar(void);

#endif
