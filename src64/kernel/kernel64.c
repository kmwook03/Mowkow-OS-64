#include <asmfunc64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fd64.h>
#include <fifo64.h>
#include <int64.h>
#include <keyboard64.h>
#include <memory64.h>
#include <mtask64.h>
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

static void serial_print_uint(uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		serial_putc('0');
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + value % 10);
		value /= 10;
	}
	while (i > 0) {
		serial_putc(buf[--i]);
	}
}

static void serial_fat12_smoke(void)
{
	struct FDHANDLE64 fh;
	char buf[33];
	size_t n;
	size_t i;

	serial_print("fat12 files=");
	serial_print_uint(fd64_file_count());
	serial_print("\r\n");
	if (fd64_open(&fh, "readme.txt") == 0) {
		serial_print("fat12 readme=open-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf) - 1);
	serial_print("fat12 readme=");
	for (i = 0; i < n; i++) {
		serial_putc(buf[i]);
	}
	serial_print("\r\n");
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
	init_memory64();
	fd64_init();
	serial_fat12_smoke();
	console64_init(boot_info->vram, boot_info->scrnx, boot_info->scrny);
	task_init64();
	fifo64_init(&event_fifo, EVENT_BUF_SIZE, event_buf, task_now64());
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
			task_sleep64(task_now64());
			io_sti();
		}
	}
}
