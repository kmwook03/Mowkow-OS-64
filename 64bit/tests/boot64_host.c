#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/platform64.h>
#include <asmfunc64.h>
#include <block64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <gui64.h>
#include <hangul64.h>
#include <kernel64.h>
#include <keyboard64.h>
#include <kstring64.h>
#include <memory64.h>
#include <mouse64.h>
#include <mtask64.h>
#include <timer64.h>

struct MEMMAN64 memman64;
uint8_t _bss_start[4];
static jmp_buf stop64;
static int mode64, phase64, irq64, uart_writes64, reaps64, routed64, system64;
static int creates64, writes64, syncs64, font_installs64, reads64, allocations64;
static struct TASK64 main_task64;
static uint8_t console_token64;
static struct MOUSE_DEC64 mouse64;
static char serial64[8192];
static size_t serial_length64;
static int trace64;

struct BOOT_FILE64 {
	char name[FD64_NAME_MAX];
	uint8_t data[HANGUL64_FONT_SIZE];
	size_t size;
};
static struct BOOT_FILE64 files64[5];
static unsigned int file_count64;

enum BOOT_CASE64 {
	NORMAL64, AHCI64, BAD_FB64, NO_MEMORY64, BLOCK_FAIL64, FAT_FAIL64,
	TASK_FAIL64, EVENT_FIFO_FAIL64, CONSOLE_FAIL64, KEYBOARD_FAIL64, MOUSE_FAIL64,
	GUI_FAIL64, FONT_MISSING64, FONT_SHORT64, BLOCK_MIN64,
	BAD_FB_VRAM64, BAD_FB_WIDTH64, BAD_FB_HEIGHT64, BAD_FB_PITCH64,
	BAD_FB_TYPE64, BAD_FB_NULL64
};

/* Inject a failure only in the main event FIFO; other FIFO code is real. */
int boot_fixture_fifo_init64(struct FIFO64 *fifo, uint32_t size,
	struct EVENT64 *events, struct TASK64 *task);
int fifo64_init(struct FIFO64 *fifo, uint32_t size,
	struct EVENT64 *events, struct TASK64 *task)
{
	if (mode64 == EVENT_FIFO_FAIL64 && size == 128) { return -22; }
	return boot_fixture_fifo_init64(fifo, size, events, task);
}

static struct CONSOLE64 *console64_token(void)
{
	return (struct CONSOLE64 *) (void *) &console_token64;
}

static void phase_expect64(int phase)
{
	assert(phase64 == phase - 1);
	phase64 = phase;
}

uint8_t io_in8(uint16_t port) { return port == 0x3fd ? 0x20 : 0xa5; }
void io_out8(uint16_t port, uint8_t value)
{
	if (port == 0x3f8 && uart_writes64 >= 7) {
		assert(serial_length64 + 1 < sizeof(serial64));
		serial64[serial_length64++] = (char) value;
		serial64[serial_length64] = '\0';
	} else {
		static const uint16_t ports[] = { 0x3f9, 0x3fb, 0x3f8, 0x3f9, 0x3fb, 0x3fa, 0x3fc };
		static const uint8_t values[] = { 0, 0x80, 3, 0, 3, 0xc7, 0x0b };
		assert(uart_writes64 < 7);
		assert(port == ports[uart_writes64] && value == values[uart_writes64]);
		uart_writes64++;
	}
}
void io_cli(void) { irq64 = 1; }
void io_sti(void) { irq64 = 0; }
void io_hlt(void) { assert(irq64 == 1); longjmp(stop64, 2); }
uint64_t platform_irq_save64(void) { int old = irq64; irq64 = 1; return old; }
void platform_irq_restore64(uint64_t state) { irq64 = (int) state; }
void init_gdtidt64(void) { phase_expect64(1); }
void init_fpu64(void) { phase_expect64(2); }
void init_memory64(void) { phase_expect64(3); }
size_t memman64_total(const struct MEMMAN64 *man)
{
	(void) man;
	return mode64 == NO_MEMORY64 ? 0 : MEMMAN64_PAGE_SIZE;
}
uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	assert(phase64 >= 5);
	allocations64++;
	return (uintptr_t) calloc(1, size);
}
int memman64_free_4k(struct MEMMAN64 *man, uintptr_t address, size_t size)
{
	(void) man;
	(void) size;
	allocations64--;
	free((void *) address);
	return 0;
}
int block64_init(void)
{
	phase_expect64(4);
	return mode64 == BLOCK_FAIL64 ? -5 : mode64 == BLOCK_MIN64 ? INT_MIN : 0;
}
const char *block64_transport(void) { return mode64 == AHCI64 ? "ahci" : "ata"; }
uint64_t block64_part_base(void) { return UINT64_MAX; }
int fd64_init(void) { phase_expect64(5); return mode64 == FAT_FAIL64 ? -22 : 0; }
uint32_t fd64_file_count(void) { assert(phase64 >= 5); return file_count64; }
int fd64_open(struct FDHANDLE64 *file, const char *name)
{
	assert(phase64 >= 5);
	if (mode64 == FONT_MISSING64 && strcmp(name, "H04.FNT") == 0) { return 0; }
	for (unsigned int i = 0; i < file_count64; i++) {
		if (strcmp(files64[i].name, name) == 0) {
			*file = (struct FDHANDLE64) { .cluster = i, .info.size = files64[i].size };
			return 1;
		}
	}
	return 0;
}
size_t fd64_read(struct FDHANDLE64 *file, void *dst, size_t size)
{
	struct BOOT_FILE64 *entry = &files64[file->cluster];
	size_t available = entry->size - file->pos;

	if (size > available) { size = available; }
	if (size > 127) { size = 127; } /* font and chain reads may be partial */
	if (file->cluster != 1 && size < available) {
		/* Chain smoke expects the normal filesystem's complete requested read. */
		size = available < 1500 ? available : 1500;
	}
	memcpy(dst, entry->data + file->pos, size);
	file->pos += (uint32_t) size;
	reads64++;
	return size;
}
int fd64_create(struct FDHANDLE64 *file, const char *name)
{
	assert(phase64 == 5 && MOWKOW64_BOOT_TESTS == 1);
	assert(file_count64 < (sizeof(files64) / sizeof(files64[0])));
	assert(str_copy64(files64[file_count64].name, FD64_NAME_MAX, name) == 0);
	*file = (struct FDHANDLE64) { .cluster = file_count64++ };
	creates64++;
	return 1;
}
size_t fd64_write(struct FDHANDLE64 *file, const void *src, size_t size)
{
	assert(MOWKOW64_BOOT_TESTS == 1 && size <= HANGUL64_FONT_SIZE);
	memcpy(files64[file->cluster].data, src, size);
	files64[file->cluster].size = size;
	file->info.size = (uint32_t) size;
	writes64++;
	return size;
}
int fd64_sync(void) { assert(MOWKOW64_BOOT_TESTS == 1); syncs64++; return 2; }
int fd64_file_at(uint32_t index, struct FDINFO64 *info, char *name, size_t size)
{
	assert(index < file_count64);
	*info = (struct FDINFO64) { .size = files64[index].size };
	assert(str_copy64(name, size, files64[index].name) == 0);
	return 1;
}
uint32_t graphic64_rgb32(uint8_t index) { return index; }
int console64_hangul_smoke(void) { assert(phase64 == 5); return 1; }
void console64_set_hangul_font(const uint8_t *font)
{
	assert(phase64 == 5 && mode64 != FONT_MISSING64 && mode64 != FONT_SHORT64);
	for (size_t i = 0; i < HANGUL64_FONT_SIZE; i++) { assert(font[i] == (uint8_t) i); }
	font_installs64++;
}
void init_palette64(void)
{
	assert(allocations64 == 0);
	assert(strstr(serial64, "sheet64 smoke=ok\r\n") != NULL);
	assert(strstr(serial64, "keyboard64 smoke=ok\r\n") != NULL);
	phase_expect64(6);
}
void console64_init(const struct BOOTINFO64 *info) { assert(info != NULL); phase_expect64(7); }
int gui64_available(void) { return mode64 != GUI_FAIL64; }
int task_init64(void)
{
	phase_expect64(8);
	main_task64 = (struct TASK64) { .flags = TASK64_FLAGS_RUNNING };
	return mode64 == TASK_FAIL64 ? -12 : 0;
}
struct TASK64 *task_now64(void) { return &main_task64; }
struct CONSOLE64 *console64_active(void) { return console64_token(); }
int console64_start_task(struct CONSOLE64 *con)
{
	assert(con == console64_token());
	phase_expect64(9);
	return mode64 == CONSOLE_FAIL64 ? -3 : 0;
}
int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) task;
	(void) level;
	(void) priority;
	assert(irq64 == 0);
	return 0;
}
int task_sleep64(struct TASK64 *task)
{
	assert(task == &main_task64 && irq64 == 1 && phase64 == 13);
	longjmp(stop64, 1);
}
void init_pit64(struct FIFO64 *fifo) { assert(fifo->task == &main_task64); phase_expect64(10); }
void init_pic64(void) { phase_expect64(11); }
int init_keyboard64(struct FIFO64 *fifo)
{
	assert(irq64 == 0 && fifo->size == 128);
	phase_expect64(12);
	keyboard64_decode_reset();
	return mode64 == KEYBOARD_FAIL64 ? -110 : 0;
}
struct MOUSE_DEC64 *gui64_mouse_dec(void) { return &mouse64; }
int init_mouse64(struct FIFO64 *fifo, struct MOUSE_DEC64 *mouse)
{
	static const struct EVENT64 events[] = {
		{ EVENT64_TIMER, 1 }, { EVENT64_MOUSE, 2 },
		{ EVENT64_KEYBOARD, 0x1e }, { EVENT64_KEYBOARD, 0x30 },
		{ EVENT64_KEYBOARD, 0x57 }
	};
	assert(irq64 == 0 && mouse == &mouse64);
	phase_expect64(13);
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		assert(fifo64_put(fifo, events[i]) == 0);
	}
	return mode64 == MOUSE_FAIL64 ? -110 : 0;
}
int gui64_handle_system_event(const struct EVENT64 *event)
{
	assert(irq64 == 0);
	if (event->type == EVENT64_MOUSE || event->data == 0x57) { system64++; return 1; }
	return 0;
}
struct CONSOLE64 *gui64_focused_console(void) { return routed64 == 0 ? console64_token() : NULL; }
void console64_post_key(struct CONSOLE64 *con, uint16_t key)
{
	assert(con == console64_token() && key == 0x1e && irq64 == 0);
	routed64++;
}
void console64_reap_closed(void) { assert(irq64 == 0); reaps64++; }

static void run_case64(int mode)
{
	struct BOOTINFO64 info = { .vram = 1, .scrnx = 128, .scrny = 64,
		.bytes_per_scanline = 136, .bpp = 8, .framebuffer_type = 1 };
	int result;

	mode64 = mode;
	phase64 = 0;
	irq64 = 1;
	uart_writes64 = reaps64 = routed64 = system64 = 0;
	creates64 = writes64 = syncs64 = font_installs64 = reads64 = 0;
	allocations64 = 0;
	serial_length64 = 0;
	serial64[0] = '\0';
	memset(files64, 0, sizeof(files64));
	assert(str_copy64(files64[0].name, FD64_NAME_MAX, "readme.txt") == 0);
	assert(str_copy64(files64[1].name, FD64_NAME_MAX, "H04.FNT") == 0);
	memcpy(files64[0].data, "fixture FAT32\n", 14);
	files64[0].size = 14;
	files64[1].size = mode == FONT_SHORT64 ? HANGUL64_FONT_SIZE - 1 : HANGUL64_FONT_SIZE;
	for (size_t i = 0; i < HANGUL64_FONT_SIZE; i++) { files64[1].data[i] = (uint8_t) i; }
	file_count64 = 2;
	keyboard64_decode_reset();
	if (mode == BAD_FB64) { info.bpp = 32; }
	if (mode == BAD_FB_VRAM64) { info.vram = 0; }
	if (mode == BAD_FB_WIDTH64) { info.scrnx = 7; }
	if (mode == BAD_FB_HEIGHT64) { info.scrny = 15; }
	if (mode == BAD_FB_PITCH64) { info.bytes_per_scanline = 127; }
	if (mode == BAD_FB_TYPE64) { info.framebuffer_type = 0; }
	result = setjmp(stop64);
	if (result == 0) { kernel64_main(mode == BAD_FB_NULL64 ? NULL : &info); assert(0); }
	const char *panic = NULL;
	switch (mode) {
	case BAD_FB_VRAM64: case BAD_FB_WIDTH64: case BAD_FB_HEIGHT64:
	case BAD_FB_PITCH64: case BAD_FB_TYPE64: case BAD_FB_NULL64:
	case BAD_FB64: panic = "framebuffer status=-1"; assert(phase64 == 0); break;
	case NO_MEMORY64: panic = "memory status=-1"; assert(phase64 == 3); break;
	case BLOCK_FAIL64: panic = "block status=-5"; assert(phase64 == 4); break;
	case BLOCK_MIN64: panic = "block status=-2147483648"; assert(phase64 == 4); break;
	case FAT_FAIL64: panic = "fat32 status=-22"; assert(phase64 == 5); break;
	case TASK_FAIL64: panic = "scheduler status=-12"; assert(phase64 == 8); break;
	case EVENT_FIFO_FAIL64: panic = "event-fifo status=-22"; assert(phase64 == 8); break;
	case CONSOLE_FAIL64: panic = "console-task status=-3"; assert(phase64 == 9); break;
	case KEYBOARD_FAIL64: panic = "ps2-keyboard status=-110"; assert(phase64 == 12); break;
	default:
		assert(result == 1 && reaps64 == 5 && routed64 == 1 && system64 == 2);
		assert(strstr(serial64, "part-base=18446744073709551615\r\n") != NULL);
		assert(font_installs64 == (mode != FONT_MISSING64 && mode != FONT_SHORT64));
		if (mode == AHCI64) { assert(strstr(serial64, "DEGRADED: AHCI") == NULL); }
		if (mode == GUI_FAIL64) { assert(strstr(serial64, "DEGRADED: GUI") != NULL); }
		if (mode == MOUSE_FAIL64) { assert(strstr(serial64, "DEGRADED: PS/2 mouse") != NULL); }
		if (mode == FONT_MISSING64) { assert(strstr(serial64, "font=open-failed") != NULL); }
		if (mode == FONT_SHORT64) { assert(strstr(serial64, "font=short-read") != NULL); }
		break;
	}
	if (panic != NULL) { assert(result == 2 && strstr(serial64, panic) != NULL); }
	if (phase64 >= 6) {
		assert(creates64 == 3 * MOWKOW64_BOOT_TESTS && writes64 == creates64);
		assert(syncs64 == MOWKOW64_BOOT_TESTS);
	}
	assert(allocations64 == 0);
	if (trace64 != 0) { printf("case=%d phase=%d\n%s", mode, phase64, serial64); }
}

int main(int argc, char **argv)
{
	(void) argv;
	trace64 = argc > 1;
	for (int mode = NORMAL64; mode <= BAD_FB_NULL64; mode++) { run_case64(mode); }
	puts("Boot order, fatal/degraded diagnostics, font ownership, IRQ routing and test gate: PASS");
	return 0;
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	(void) address;
	(void) bytes;
}

void platform_framebuffer_sync64(void)
{
}
