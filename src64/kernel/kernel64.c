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
#define HANGUL_FONT_SIZE 11520

static struct FIFO64 event_fifo;
static struct EVENT64 event_buf[EVENT_BUF_SIZE];
static uint8_t hangul_font_buf[HANGUL_FONT_SIZE];

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

static void serial_fpu_smoke(void)
{
	volatile double d;

	d = 1.0;
	d *= 2.0;
	serial_print("fpu smoke=");
	serial_print_uint((uint64_t) d);
	serial_print("\r\n");
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

/* Phase 0 check: create, write, sync, read back. Runs every boot, so a
   regression in the FAT12 write path shows up on COM1 before the console. */
static void serial_fat12_write_smoke(void)
{
	struct FDHANDLE64 fh;
	const char text[] = "fat12 write smoke\n";
	char buf[32];
	size_t len;
	size_t n;
	size_t i;

	len = sizeof(text) - 1;
	if (fd64_create(&fh, "SMOKE.TXT") == 0) {
		serial_print("fat12 write=create-failed\r\n");
		return;
	}
	n = fd64_write(&fh, text, len);
	serial_print("fat12 write=");
	serial_print_uint(n);
	serial_print(" sectors=");
	serial_print_uint((uint64_t) fd64_sync());
	serial_print("\r\n");
	if (fd64_open(&fh, "SMOKE.TXT") == 0) {
		serial_print("fat12 write=reopen-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf));
	serial_print("fat12 readback=");
	for (i = 0; i < n && i < len; i++) {
		serial_putc(buf[i] == '\n' ? ' ' : buf[i]);
	}
	serial_print(n == len ? "[len-ok]\r\n" : "[len-bad]\r\n");
}

static void load_hangul_font(void)
{
	struct FDHANDLE64 fh;
	size_t loaded;
	size_t n;

	loaded = 0;
	if (fd64_open(&fh, "H04.FNT") == 0) {
		serial_print("hangul font=open-failed\r\n");
		return;
	}
	while (loaded < HANGUL_FONT_SIZE) {
		n = fd64_read(&fh, hangul_font_buf + loaded, HANGUL_FONT_SIZE - loaded);
		if (n == 0) {
			break;
		}
		loaded += n;
	}
	if (loaded == HANGUL_FONT_SIZE) {
		console64_set_hangul_font(hangul_font_buf);
		serial_print("hangul font=loaded\r\n");
	} else {
		serial_print("hangul font=short-read\r\n");
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

extern uint8_t _bss_start[];

static void serial_bss_smoke(void)
{
	serial_print("bss probe=");
	serial_print_uint(_bss_start[0]);
	serial_putc(' ');
	serial_print_uint(_bss_start[1]);
	serial_putc(' ');
	serial_print_uint(_bss_start[2]);
	serial_putc(' ');
	serial_print_uint(_bss_start[3]);
	serial_print("\r\n");
}

void kernel64_main(const struct BOOTINFO64 *boot_info)
{
	struct EVENT64 event;

	serial_init();
	serial_print("Mowkow OS x86_64 kernel64_main\r\n");
	init_gdtidt64();
	serial_bss_smoke();
	init_fpu64();
	serial_fpu_smoke();
	init_memory64();
	fd64_init();
	serial_fat12_smoke();
	serial_fat12_write_smoke();
	load_hangul_font();
	console64_init(boot_info);
	task_init64();
	fifo64_init(&event_fifo, EVENT_BUF_SIZE, event_buf, task_now64());
	console64_set_event_fifo(&event_fifo);
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
