/* x86 early COM1 diagnostics and boot font ownership. */
#include <arch/x86_boot64.h>
#include <asmfunc64.h>
#include <block64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <fd64.h>
#include <framebuffer64.h>
#include <gui64.h>
#include <hangul64.h>
#include <keyboard64.h>
#include <kstring64.h>

static uint8_t hangul_font_buf64[HANGUL64_FONT_SIZE];

static void x86_boot_serial_init64(void)
{
	io_out8(0x3f8 + 1, 0x00);
	io_out8(0x3f8 + 3, 0x80);
	io_out8(0x3f8 + 0, 0x03);
	io_out8(0x3f8 + 1, 0x00);
	io_out8(0x3f8 + 3, 0x03);
	io_out8(0x3f8 + 2, 0xc7);
	io_out8(0x3f8 + 4, 0x0b);
}

void x86_boot_log_byte64(char c)
{
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

void x86_boot_log64(const char *s)
{
	while (*s != '\0') {
		x86_boot_log_byte64(*s++);
	}
}

void x86_boot_log_uint64(uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		x86_boot_log_byte64('0');
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + value % 10);
		value /= 10;
	}
	while (i > 0) {
		x86_boot_log_byte64(buf[--i]);
	}
}

static void x86_boot_log_int64(int64_t value)
{
	uint64_t magnitude;

	if (value >= 0) {
		x86_boot_log_uint64((uint64_t) value);
		return;
	}
	x86_boot_log_byte64('-');
	magnitude = (uint64_t) (-(value + 1)) + 1U;
	x86_boot_log_uint64(magnitude);
}

void x86_boot_panic64(const char *stage, int status)
{
	x86_boot_log64("PANIC: init stage=");
	x86_boot_log64(stage);
	x86_boot_log64(" status=");
	x86_boot_log_int64(status);
	x86_boot_log64("\r\n");
	io_cli();
	for (;;) {
		io_hlt();
	}
}

int x86_boot_info_valid64(const struct BOOTINFO64 *boot_info)
{
	struct FRAMEBUFFER64 framebuffer;

	return boot_info != NULL && boot_info->scrnx >= 8U &&
		boot_info->scrny >= 16U && boot_info->bpp == 8U &&
		framebuffer_boot64(&framebuffer, boot_info) == 0;
}

void x86_boot_load_font64(void)
{
	struct FDHANDLE64 fh;
	size_t loaded;
	size_t n;

	loaded = 0;
	if (fd64_open(&fh, "H04.FNT") == 0) {
		x86_boot_log64("hangul font=open-failed\r\n");
		return;
	}
	while (loaded < HANGUL64_FONT_SIZE) {
		n = fd64_read(&fh, hangul_font_buf64 + loaded, HANGUL64_FONT_SIZE - loaded);
		if (n == 0) {
			break;
		}
		loaded += n;
	}
	if (loaded == HANGUL64_FONT_SIZE) {
		console64_set_hangul_font(hangul_font_buf64);
		x86_boot_log64("hangul font=loaded\r\n");
	} else {
		x86_boot_log64("hangul font=short-read\r\n");
	}
}

void x86_boot_diag_init64(void)
{
	x86_boot_serial_init64();
	x86_boot_log64("Mowkow OS x86_64 kernel64_main\r\n");
}

void x86_boot_diag_storage64(void)
{
	x86_boot_log64("disk transport=");
	x86_boot_log64(block64_transport());
	x86_boot_log64(" part-base=");
	x86_boot_log_uint64(block64_part_base());
	x86_boot_log64("\r\n");
	if (strcmp(block64_transport(), "ata") == 0) {
		x86_boot_log64("DEGRADED: AHCI unavailable, using ATA PIO\r\n");
	}
}

void x86_boot_diag_gui64(void)
{
	if (gui64_available() == 0) {
		x86_boot_log64("DEGRADED: GUI unavailable, using direct framebuffer\r\n");
	}
}

void x86_boot_diag_keyboard64(void)
{
	x86_boot_log64("KBC status=");
	x86_boot_log_uint64(keyboard64_controller_status());
	x86_boot_log64("\r\n");
}

void x86_boot_diag_mouse64(void)
{
	x86_boot_log64("DEGRADED: PS/2 mouse unavailable, KBC status=");
	x86_boot_log_uint64(keyboard64_controller_status());
	x86_boot_log64("\r\n");
}
