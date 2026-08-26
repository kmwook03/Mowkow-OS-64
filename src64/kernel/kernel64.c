#include <asmfunc64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <int64.h>
#include <keyboard64.h>
#include <memory64.h>
#include <gui64.h>
#include <mouse64.h>
#include <mtask64.h>
#include <sheet64.h>
#include <stdint.h>
#include <timer64.h>

#define EVENT_BUF_SIZE 128
#define HANGUL_FONT_SIZE 11520

static struct FIFO64 event_fifo;
static struct EVENT64 event_buf[EVENT_BUF_SIZE];
static uint8_t hangul_font_buf[HANGUL_FONT_SIZE];
static struct MOUSE_DEC64 mdec64;

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

/* Phase 3 check: 겹침 처리와 스트라이드를 실제 화면 없이 검증한다.
   가짜 VRAM은 폭 64, 스트라이드 80으로 잡아 stride > xsize 경로를 강제한다
   (실제 VBE 모드에서 둘이 다를 수 있음). 화면에는 아무것도 그리지 않는다. */
#define SMOKE_W      64
#define SMOKE_H      32
#define SMOKE_STRIDE 80

static uint8_t smoke_vram[SMOKE_STRIDE * SMOKE_H];
static uint8_t smoke_buf_a[16 * 16];
static uint8_t smoke_buf_b[16 * 16];

static uint8_t smoke_pixel(int32_t x, int32_t y)
{
	return smoke_vram[(uint32_t) y * SMOKE_STRIDE + x];
}

static void serial_sheet64_smoke(void)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *a;
	struct SHEET64 *b;
	int32_t i;
	int ok = 1;

	for (i = 0; i < SMOKE_STRIDE * SMOKE_H; i++) {
		smoke_vram[i] = 0;
	}
	for (i = 0; i < 16 * 16; i++) {
		smoke_buf_a[i] = 1;
		smoke_buf_b[i] = 2;
	}
	smoke_buf_b[1 * 16 + 1] = 99;   /* 투명 픽셀 */

	ctl = shtctl64_init(&memman64, smoke_vram, SMOKE_W, SMOKE_H, SMOKE_STRIDE);
	if (ctl == NULL) {
		serial_print("sheet64 smoke=alloc-failed\r\n");
		return;
	}
	a = sheet64_alloc(ctl);
	b = sheet64_alloc(ctl);
	if (a == NULL || b == NULL) {
		serial_print("sheet64 smoke=sheet-alloc-failed\r\n");
		return;
	}
	sheet64_setbuf(a, smoke_buf_a, 16, 16, -1);
	sheet64_setbuf(b, smoke_buf_b, 16, 16, 99);
	a->vx0 = 0;
	a->vy0 = 0;
	b->vx0 = 8;
	b->vy0 = 8;
	sheet64_updown(a, 0);
	sheet64_updown(b, 1);

	/* b는 (8,8)에서 16x16이므로 x,y 8..23을 덮는다. */
	if (smoke_pixel(0, 0) != 1) { ok = 0; }            /* a만 있는 곳 */
	if (smoke_pixel(8, 8) != 2) { ok = 0; }            /* b가 a를 덮음 */
	if (smoke_pixel(9, 9) != 1) { ok = 0; }            /* b의 투명 픽셀로 a가 비침 */
	if (smoke_pixel(60, 30) != 0) { ok = 0; }          /* 어느 시트에도 없는 곳 */
	if (smoke_vram[8 * SMOKE_W + 8] != 0) { ok = 0; }  /* 스트라이드를 xsize로 잘못 쓰면 여기 값이 남는다 */

	sheet64_slide(b, 40, 0);
	if (smoke_pixel(8, 8) != 1) { ok = 0; }            /* b가 비켜난 자리를 a가 되칠함 */
	if (smoke_pixel(40, 0) != 2) { ok = 0; }

	serial_print(ok ? "sheet64 smoke=ok\r\n" : "sheet64 smoke=FAIL\r\n");

	memman64_free_4k(&memman64, (uintptr_t) ctl->map,
		(size_t) SMOKE_W * (size_t) SMOKE_H);
	memman64_free_4k(&memman64, (uintptr_t) ctl, sizeof (struct SHTCTL64));
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
	struct CONSOLE64 *con;

	if (event->type == EVENT64_TIMER) {
		return;
	}
	if (event->type == EVENT64_KEYBOARD) {
		if (event->data == 0x57) {          /* F11 */
			gui64_raise_bottom_window();
			return;
		}
		con = gui64_focused_console();
		if (con != NULL) {
			console64_process_key(con, (uint8_t) event->data);
		}
		return;
	}
	if (event->type == EVENT64_MOUSE) {
		if (mouse64_decode(&mdec64, (uint8_t) event->data) != 0) {
			gui64_mouse_event(mdec64.x, mdec64.y, mdec64.btn);
		}
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
	serial_sheet64_smoke();
	load_hangul_font();
	init_palette64();
	console64_init(boot_info);
	task_init64();
	fifo64_init(&event_fifo, EVENT_BUF_SIZE, event_buf, task_now64());
	console64_set_event_fifo(&event_fifo);
	init_pit64(&event_fifo);
	init_pic64();
	io_sti();
	/* 인터럽트를 켠 뒤에 KBC를 건드린다. 마우스 활성화 명령의 ACK(0xfa)는
	   IRQ12로 돌아오므로, 마스킹된 상태에서 보내면 출력 버퍼에 갇힌 채
	   에지를 놓쳐 이후 패킷이 오지 않는다 (32비트 bootpack.c:76-85과 같은 순서). */
	init_keyboard64(&event_fifo);
	init_mouse64(&event_fifo, &mdec64);

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
