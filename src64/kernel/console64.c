#include <asmfunc64.h>
#include <console64.h>
#include <timer64.h>
#include <stdint.h>

#define CONSOLE_ATTR 0x0f00
#define CONSOLE_INPUT_MAX 64

static volatile uint16_t *console_vram;
static uint16_t console_width;
static uint16_t console_height;
static uint16_t cursor_x;
static uint16_t cursor_y;
static char input_line[CONSOLE_INPUT_MAX];
static uint16_t input_len;

static const char keymap[128] = {
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
	[0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = ';', [0x28] = '\'',
	[0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
	[0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
	[0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static void serial_putc(char c)
{
	if (c == '\n') {
		serial_putc('\r');
	}
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

static void put_cell(uint16_t x, uint16_t y, char c)
{
	console_vram[(uint32_t) y * console_width + x] = (uint16_t) c | CONSOLE_ATTR;
}

static void scroll_if_needed(void)
{
	uint16_t x;
	uint16_t y;

	if (cursor_y < console_height) {
		return;
	}
	for (y = 1; y < console_height; y++) {
		for (x = 0; x < console_width; x++) {
			console_vram[(uint32_t) (y - 1) * console_width + x] =
				console_vram[(uint32_t) y * console_width + x];
		}
	}
	for (x = 0; x < console_width; x++) {
		put_cell(x, console_height - 1, ' ');
	}
	cursor_y = console_height - 1;
}

static void console_putchar(char c)
{
	if (c == '\n') {
		cursor_x = 0;
		cursor_y++;
		serial_putc('\n');
		scroll_if_needed();
		return;
	}
	put_cell(cursor_x, cursor_y, c);
	serial_putc(c);
	cursor_x++;
	if (cursor_x >= console_width) {
		cursor_x = 0;
		cursor_y++;
		scroll_if_needed();
	}
}

static void console_backspace(void)
{
	if (input_len == 0 || cursor_x == 0) {
		return;
	}
	input_len--;
	cursor_x--;
	put_cell(cursor_x, cursor_y, ' ');
	serial_putc('\b');
	serial_putc(' ');
	serial_putc('\b');
}

static int str_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

static void print_uint64(uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		console_putchar('0');
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + (value % 10));
		value /= 10;
	}
	while (i > 0) {
		console_putchar(buf[--i]);
	}
}

static void prompt(void)
{
	console64_puts("> ");
}

static void clear_screen(void)
{
	uint16_t x;
	uint16_t y;

	for (y = 0; y < console_height; y++) {
		for (x = 0; x < console_width; x++) {
			put_cell(x, y, ' ');
		}
	}
	cursor_x = 0;
	cursor_y = 0;
}

static void execute_command(void)
{
	input_line[input_len] = '\0';
	console_putchar('\n');
	if (input_len == 0) {
		prompt();
		return;
	}
	if (str_eq(input_line, "help")) {
		console64_puts("commands: help clear ticks\n");
	} else if (str_eq(input_line, "clear")) {
		clear_screen();
	} else if (str_eq(input_line, "ticks")) {
		console64_puts("ticks ");
		print_uint64(timerctl64.count);
		console_putchar('\n');
	} else {
		console64_puts("unknown command\n");
	}
	input_len = 0;
	prompt();
}

void console64_init(uintptr_t vram, uint16_t width, uint16_t height)
{
	console_vram = (volatile uint16_t *) vram;
	console_width = width != 0 ? width : 80;
	console_height = height != 0 ? height : 25;
	cursor_x = 0;
	cursor_y = 0;
	input_len = 0;
	clear_screen();
	console64_puts("Mowkow OS x86_64 console\n");
	console64_puts("type help and press enter\n");
	prompt();
}

void console64_puts(const char *s)
{
	while (*s != '\0') {
		console_putchar(*s++);
	}
}

void console64_process_key(uint8_t scancode)
{
	char c;

	if ((scancode & 0x80) != 0) {
		return;
	}
	if (scancode == 0x1c) {
		execute_command();
		return;
	}
	if (scancode == 0x0e) {
		console_backspace();
		return;
	}
	c = keymap[scancode];
	if (c == '\0' || input_len >= CONSOLE_INPUT_MAX - 1) {
		return;
	}
	input_line[input_len++] = c;
	console_putchar(c);
}
