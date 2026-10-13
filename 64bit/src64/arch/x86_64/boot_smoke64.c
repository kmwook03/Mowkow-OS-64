/* x86 boot self-checks. Destructive FAT32 cases require an explicit test build. */
#include <arch/x86_boot64.h>
#include <console64.h>
#include <fd64.h>
#include <keyboard64.h>
#include <kstring64.h>
#include <memory64.h>
#include <sheet64.h>

void x86_boot_smoke_fpu64(void)
{
	volatile double d;

	d = 1.0;
	d *= 2.0;
	x86_boot_log64("fpu smoke=");
	x86_boot_log_uint64((uint64_t) d);
	x86_boot_log64("\r\n");
}

static void fat32_read_smoke64(void)
{
	struct FDHANDLE64 fh;
	char buf[33];
	size_t n;
	size_t i;

	x86_boot_log64("fat32 files=");
	x86_boot_log_uint64(fd64_file_count());
	x86_boot_log64("\r\n");
	if (fd64_open(&fh, "readme.txt") == 0) {
		x86_boot_log64("fat32 readme=open-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf) - 1);
	x86_boot_log64("fat32 readme=");
	for (i = 0; i < n; i++) {
		x86_boot_log_byte64(buf[i]);
	}
	x86_boot_log64("\r\n");
}

#if MOWKOW64_BOOT_TESTS
/* Exercise allocation across clusters and VFAT names on disposable media. */
#define CHAIN_SMOKE_SIZE64 1500
static uint8_t chain_smoke_buf64[CHAIN_SMOKE_SIZE64];

static void fat32_write_smoke64(void)
{
	struct FDHANDLE64 fh;
	const char text[] = "fat32 write smoke\n";
	char buf[32];
	size_t len;
	size_t n;
	size_t i;

	len = sizeof(text) - 1;
	if (fd64_create(&fh, "SMOKE.TXT") == 0) {
		x86_boot_log64("fat32 write=create-failed\r\n");
		return;
	}
	n = fd64_write(&fh, text, len);
	x86_boot_log64("fat32 write=");
	x86_boot_log_uint64(n);
	x86_boot_log64(" sectors=");
	x86_boot_log_uint64((uint64_t) fd64_sync());
	x86_boot_log64("\r\n");
	if (fd64_open(&fh, "SMOKE.TXT") == 0) {
		x86_boot_log64("fat32 write=reopen-failed\r\n");
		return;
	}
	n = fd64_read(&fh, buf, sizeof(buf));
	x86_boot_log64("fat32 readback=");
	for (i = 0; i < n && i < len; i++) {
		x86_boot_log_byte64(buf[i] == '\n' ? ' ' : buf[i]);
	}
	x86_boot_log64(n == len ? "[len-ok]\r\n" : "[len-bad]\r\n");
}

static void fat32_chain_smoke64(void)
{
	struct FDHANDLE64 fh;
	size_t i;
	size_t n;
	int ok;

	for (i = 0; i < CHAIN_SMOKE_SIZE64; i++) {
		chain_smoke_buf64[i] = (uint8_t) (i * 7 + 1);
	}
	if (fd64_create(&fh, "CHAIN.BIN") == 0) {
		x86_boot_log64("fat32 chain=create-failed\r\n");
		return;
	}
	n = fd64_write(&fh, chain_smoke_buf64, CHAIN_SMOKE_SIZE64);
	for (i = 0; i < CHAIN_SMOKE_SIZE64; i++) {
		chain_smoke_buf64[i] = 0;
	}
	if (n != CHAIN_SMOKE_SIZE64 || fd64_open(&fh, "CHAIN.BIN") == 0) {
		x86_boot_log64("fat32 chain=write-failed\r\n");
		return;
	}
	n = fd64_read(&fh, chain_smoke_buf64, CHAIN_SMOKE_SIZE64);
	ok = n == CHAIN_SMOKE_SIZE64;
	for (i = 0; i < n; i++) {
		if (chain_smoke_buf64[i] != (uint8_t) (i * 7 + 1)) {
			ok = 0;
			break;
		}
	}
	x86_boot_log64(ok ? "fat32 chain=ok\r\n" : "fat32 chain=FAIL\r\n");
}

static void fat32_lfn_smoke64(void)
{
	static const char *lfn_name = "한글이름.txt";
	struct FDHANDLE64 fh;
	struct FDINFO64 finfo;
	char name[FD64_NAME_MAX];
	uint32_t i;
	uint32_t count;
	int listed;

	if (fd64_create(&fh, lfn_name) == 0 || fd64_write(&fh, "lfn", 3) != 3) {
		x86_boot_log64("fat32 lfn=create-failed\r\n");
		return;
	}
	if (fd64_open(&fh, lfn_name) == 0 || fh.info.size != 3) {
		x86_boot_log64("fat32 lfn=reopen-failed\r\n");
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
	x86_boot_log64(listed != 0 ? "fat32 lfn=ok\r\n" : "fat32 lfn=not-listed\r\n");
}

#endif

/* Synthetic 8bpp surface with padded rows; no live framebuffer is modified. */
#define SMOKE_WIDTH64 64
#define SMOKE_HEIGHT64 32
#define SMOKE_STRIDE64 80
static uint8_t smoke_vram64[SMOKE_STRIDE64 * SMOKE_HEIGHT64];
static uint8_t smoke_buf_a64[16 * 16];
static uint8_t smoke_buf_b64[16 * 16];

static uint8_t smoke_pixel64(int32_t x, int32_t y)
{
	return smoke_vram64[(uint32_t) y * SMOKE_STRIDE64 + x];
}

static void sheet_smoke64(void)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *a;
	struct SHEET64 *b;
	int32_t i;
	int ok = 1;

	for (i = 0; i < SMOKE_STRIDE64 * SMOKE_HEIGHT64; i++) {
		smoke_vram64[i] = 0;
	}
	for (i = 0; i < 16 * 16; i++) {
		smoke_buf_a64[i] = 1;
		smoke_buf_b64[i] = 2;
	}
	smoke_buf_b64[1 * 16 + 1] = 99;   /* 투명 픽셀 */

	ctl = shtctl64_init(&memman64, smoke_vram64, sizeof(smoke_vram64),
		SMOKE_WIDTH64, SMOKE_HEIGHT64, SMOKE_STRIDE64, 8);
	if (ctl == NULL) {
		x86_boot_log64("sheet64 smoke=alloc-failed\r\n");
		return;
	}
	a = sheet64_alloc(ctl);
	b = sheet64_alloc(ctl);
	if (a == NULL || b == NULL) {
		x86_boot_log64("sheet64 smoke=sheet-alloc-failed\r\n");
		return;
	}
	sheet64_setbuf(a, smoke_buf_a64, sizeof(smoke_buf_a64), 16, 16, 16, -1);
	sheet64_setbuf(b, smoke_buf_b64, sizeof(smoke_buf_b64), 16, 16, 16, 99);
	a->vx0 = 0;
	a->vy0 = 0;
	b->vx0 = 8;
	b->vy0 = 8;
	sheet64_updown(a, 0);
	sheet64_updown(b, 1);

	/* b는 (8,8)에서 16x16이므로 x,y 8..23을 덮는다. */
	if (smoke_pixel64(0, 0) != 1) { ok = 0; }            /* a만 있는 곳 */
	if (smoke_pixel64(8, 8) != 2) { ok = 0; }            /* b가 a를 덮음 */
	if (smoke_pixel64(9, 9) != 1) { ok = 0; }            /* b의 투명 픽셀로 a가 비침 */
	if (smoke_pixel64(60, 30) != 0) { ok = 0; }          /* 어느 시트에도 없는 곳 */
	if (smoke_vram64[8 * SMOKE_WIDTH64 + 8] != 0) { ok = 0; }  /* 스트라이드를 xsize로 잘못 쓰면 여기 값이 남는다 */

	sheet64_slide(b, 40, 0);
	if (smoke_pixel64(8, 8) != 1) { ok = 0; }            /* b가 비켜난 자리를 a가 되칠함 */
	if (smoke_pixel64(40, 0) != 2) { ok = 0; }

	x86_boot_log64(ok ? "sheet64 smoke=ok\r\n" : "sheet64 smoke=FAIL\r\n");

	memman64_free_4k(&memman64, (uintptr_t) ctl->map,
		(size_t) SMOKE_WIDTH64 * (size_t) SMOKE_HEIGHT64);
	memman64_free_4k(&memman64, (uintptr_t) ctl, sizeof (struct SHTCTL64));
}

static void keyboard_smoke64(void)
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
		x86_boot_log64("keyboard64 smoke=ok\r\n");
	} else {
		x86_boot_log64("keyboard64 smoke=FAIL at ");
		x86_boot_log_uint64(n);
		x86_boot_log64("\r\n");
	}
}

extern uint8_t _bss_start[];

void x86_boot_smoke_bss64(void)
{
	x86_boot_log64("bss probe=");
	x86_boot_log_uint64(_bss_start[0]);
	x86_boot_log_byte64(' ');
	x86_boot_log_uint64(_bss_start[1]);
	x86_boot_log_byte64(' ');
	x86_boot_log_uint64(_bss_start[2]);
	x86_boot_log_byte64(' ');
	x86_boot_log_uint64(_bss_start[3]);
	x86_boot_log64("\r\n");
}

void x86_boot_smoke_storage64(void)
{
	fat32_read_smoke64();
#if MOWKOW64_BOOT_TESTS
	x86_boot_log64("destructive boot tests=enabled\r\n");
	fat32_write_smoke64();
	fat32_chain_smoke64();
	fat32_lfn_smoke64();
#endif
}

void x86_boot_smoke_gui_input64(void)
{
	sheet_smoke64();
	keyboard_smoke64();
	x86_boot_log64(console64_hangul_smoke() != 0 ?
		"hangul64 smoke=ok\r\n" : "hangul64 smoke=FAIL\r\n");
}
