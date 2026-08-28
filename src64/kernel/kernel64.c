/*
 * kernel64.c -- 커널 진입점
 *
 * loader64.asm이 롱 모드로 들어온 뒤 여기로 뛴다. 초기화 순서에는 의존
 * 관계가 있다. GDT/IDT -> 메모리 -> 파일 시스템 -> 글꼴 -> 콘솔 -> 태스크
 * -> 이벤트 큐와 PIT/키보드 -> PIC -> sti.
 *
 * 콘솔이 뜨기 전에는 COM1(시리얼)이 유일한 출력 통로다. 그래서 각 단계의
 * 점검 결과를 여기서 시리얼로 찍는다. 부팅이 이상하면 이 출력을 먼저 본다.
 */
#include <asmfunc64.h>
#include <block64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <int64.h>
#include <keyboard64.h>
#include <kstring64.h>
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

static void serial_fat32_smoke(void)
{
	struct FDHANDLE64 fh;
	char buf[33];
	size_t n;
	size_t i;

	serial_print("fat32 files=");
	serial_print_uint(fd64_file_count());
	serial_print("\r\n");
	if (fd64_open(&fh, "readme.txt") == 0) {
		serial_print("fat32 readme=open-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf) - 1);
	serial_print("fat32 readme=");
	for (i = 0; i < n; i++) {
		serial_putc(buf[i]);
	}
	serial_print("\r\n");
}

/* 페이즈 0 점검: 만들고, 쓰고, 내보내고, 다시 읽는다. 부팅마다 돌기 때문에
   FAT32 쓰기 경로가 망가지면 콘솔이 뜨기도 전에 COM1에 드러난다. */
static void serial_fat32_write_smoke(void)
{
	struct FDHANDLE64 fh;
	const char text[] = "fat32 write smoke\n";
	char buf[32];
	size_t len;
	size_t n;
	size_t i;

	len = sizeof(text) - 1;
	if (fd64_create(&fh, "SMOKE.TXT") == 0) {
		serial_print("fat32 write=create-failed\r\n");
		return;
	}
	n = fd64_write(&fh, text, len);
	serial_print("fat32 write=");
	serial_print_uint(n);
	serial_print(" sectors=");
	serial_print_uint((uint64_t) fd64_sync());
	serial_print("\r\n");
	if (fd64_open(&fh, "SMOKE.TXT") == 0) {
		serial_print("fat32 write=reopen-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf));
	serial_print("fat32 readback=");
	for (i = 0; i < n && i < len; i++) {
		serial_putc(buf[i] == '\n' ? ' ' : buf[i]);
	}
	serial_print(n == len ? "[len-ok]\r\n" : "[len-bad]\r\n");
}

/* 캐시가 생기면서 쓰기가 섹터 경계를 넘고 클러스터 사슬을 늘리게 되었다.
   그래서 부팅 점검은 클러스터 하나보다 큰 파일을 다룬다. 위치에서 만든
   무늬 1500바이트를 쓰고 다시 읽어 바이트마다 견준다. */
#define CHAIN_SMOKE_SIZE 1500

static uint8_t chain_smoke_buf[CHAIN_SMOKE_SIZE];

static void serial_fat32_chain_smoke(void)
{
	struct FDHANDLE64 fh;
	size_t i;
	size_t n;
	int ok;

	for (i = 0; i < CHAIN_SMOKE_SIZE; i++) {
		chain_smoke_buf[i] = (uint8_t) (i * 7 + 1);
	}
	if (fd64_create(&fh, "CHAIN.BIN") == 0) {
		serial_print("fat32 chain=create-failed\r\n");
		return;
	}
	n = fd64_write(&fh, chain_smoke_buf, CHAIN_SMOKE_SIZE);
	for (i = 0; i < CHAIN_SMOKE_SIZE; i++) {
		chain_smoke_buf[i] = 0;
	}
	if (n != CHAIN_SMOKE_SIZE || fd64_open(&fh, "CHAIN.BIN") == 0) {
		serial_print("fat32 chain=write-failed\r\n");
		return;
	}
	n = fd64_read(&fh, chain_smoke_buf, CHAIN_SMOKE_SIZE);
	ok = n == CHAIN_SMOKE_SIZE;
	for (i = 0; i < n; i++) {
		if (chain_smoke_buf[i] != (uint8_t) (i * 7 + 1)) {
			ok = 0;
			break;
		}
	}
	serial_print(ok ? "fat32 chain=ok\r\n" : "fat32 chain=FAIL\r\n");
}

/* VFAT 점검: 8.3으로 적을 수 없는 이름이 만들기, 같은 이름으로 다시 열기,
   목록에 나오기를 모두 견뎌야 한다. */
static void serial_fat32_lfn_smoke(void)
{
	static const char *lfn_name = "한글이름.txt";
	struct FDHANDLE64 fh;
	struct FDINFO64 finfo;
	char name[FD64_NAME_MAX];
	uint32_t i;
	uint32_t count;
	int listed;

	if (fd64_create(&fh, lfn_name) == 0 || fd64_write(&fh, "lfn", 3) != 3) {
		serial_print("fat32 lfn=create-failed\r\n");
		return;
	}
	if (fd64_open(&fh, lfn_name) == 0 || fh.info.size != 3) {
		serial_print("fat32 lfn=reopen-failed\r\n");
		return;
	}
	listed = 0;
	count = fd64_file_count();
	for (i = 0; i < count; i++) {
		if (fd64_file_at(i, &finfo, name, sizeof(name)) == 0) {
			continue;
		}
		if (strcmp(name, lfn_name) == 0) {
			listed = 1;
		}
	}
	serial_print(listed != 0 ? "fat32 lfn=ok\r\n" : "fat32 lfn=not-listed\r\n");
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
	/* 마우스와 F11은 창 계층이 먼저 가져간다. 남은 키만 포커스가 있는
	   콘솔의 태스크로 보낸다 -- 콘솔이 아닌 창이 활성이면 아무 데도 안 간다. */
	if (gui64_handle_system_event(event) != 0) {
		return;
	}
	if (event->type == EVENT64_KEYBOARD) {
		con = gui64_focused_console();
		if (con != NULL) {
			console64_post_key(con, (uint16_t) event->data);
		}
	}
}

static void serial_keyboard64_smoke(void)
{
	/* a, Up 누름, Up 뗌, Pause 6바이트, Enter */
	static const uint8_t seq[] = {
		0x1e,
		0xe0, 0x48,
		0xe0, 0xc8,
		0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5,
		0x1c
	};
	static const uint16_t want[] = {
		0x1e, KEY64_UP, KEY64_UP | 0x80, 0x1c
	};
	uint16_t key;
	uint32_t i;
	uint32_t n = 0;
	int ok = 1;

	for (i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		if (keyboard64_decode(seq[i], &key) == 0) {
			continue;
		}
		if (n >= sizeof(want) / sizeof(want[0]) || key != want[n]) {
			ok = 0;
			break;
		}
		n++;
	}
	if (ok != 0 && n == sizeof(want) / sizeof(want[0])) {
		serial_print("keyboard64 smoke=ok\r\n");
	} else {
		serial_print("keyboard64 smoke=FAIL at ");
		serial_print_uint(n);
		serial_print("\r\n");
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
	block64_init();
	serial_print("disk transport=");
	serial_print(block64_transport());
	serial_print(" part-base=");
	serial_print_uint(block64_part_base());
	serial_print("\r\n");
	fd64_init();
	serial_fat32_smoke();
	serial_fat32_write_smoke();
	serial_fat32_chain_smoke();
	serial_fat32_lfn_smoke();
	serial_sheet64_smoke();
	serial_keyboard64_smoke();
	serial_print(console64_hangul_smoke() != 0 ?
		"hangul64 smoke=ok\r\n" : "hangul64 smoke=FAIL\r\n");
	load_hangul_font();
	init_palette64();
	console64_init(boot_info);
	task_init64();
	fifo64_init(&event_fifo, EVENT_BUF_SIZE, event_buf, task_now64());
	if (console64_start_task(console64_active()) != 0) {
		serial_print("console task=start-failed\r\n");
	}
	init_pit64(&event_fifo);
	init_pic64();
	io_sti();
	/* 인터럽트를 켠 뒤에 KBC를 건드린다. 마우스 활성화 명령의 ACK(0xfa)는
	   IRQ12로 돌아오므로, 마스킹된 상태에서 보내면 출력 버퍼에 갇힌 채
	   에지를 놓쳐 이후 패킷이 오지 않는다 (32비트 bootpack.c:76-85과 같은 순서). */
	init_keyboard64(&event_fifo);
	init_mouse64(&event_fifo, gui64_mouse_dec());

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
