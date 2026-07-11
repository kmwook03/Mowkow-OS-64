#include <asmfunc64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fifo64.h>
#include <int64.h>
#include <keyboard64.h>
#include <stdint.h>
#include <timer64.h>

#define EVENT_BUF_SIZE 128

static struct FIFO64 event_fifo;
static struct EVENT64 event_buf[EVENT_BUF_SIZE];

static void serial_init(void)
{
	io_out8(0x3f8 + 1, 0x00);
	io_out8(0x3f8 + 3, 0x80);
	io_out8(0x3f8 + 0, 0x03);
	io_out8(0x3f8 + 1, 0x00);
	io_out8(0x3f8 + 3, 0x03);
	io_out8(0x3f8 + 2, 0xc7);
	io_out8(0x3f8 + 4, 0x0b);
}

static void serial_putc(char c)
{
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

static void serial_print(const char *s)
{
	while (*s != '\0') {
		serial_putc(*s++);
	}
}

static void process_event64(const struct EVENT64 *event)
{
	if (event->type == EVENT64_TIMER) {
		return;
	}
	if (event->type == EVENT64_KEYBOARD) {
		console64_process_key((uint8_t) event->data);
	}
}

void kernel64_main(const struct BOOTINFO64 *boot_info)
{
	struct EVENT64 event;

	serial_init();
	serial_print("Mowkow OS x86_64 kernel64_main\r\n");
	init_gdtidt64();
	console64_init(boot_info->vram, boot_info->scrnx, boot_info->scrny);
	fifo64_init(&event_fifo, EVENT_BUF_SIZE, event_buf);
	init_pit64(&event_fifo);
	init_keyboard64(&event_fifo);
	init_pic64();
	io_sti();

	for (;;) {
		io_cli();
		if (fifo64_get(&event_fifo, &event) == 0) {
			io_sti();
			process_event64(&event);
		} else {
			io_stihlt();
		}
	}
}
