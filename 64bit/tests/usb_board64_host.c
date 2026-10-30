/* Run the real Pi boot/input orchestration with modeled device outcomes. */
#include "poll_test64.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arch/platform64.h>
#include <mtask64.h>

#include "../src64/arch/aarch64/board64.c"

enum BOARD_CASE64 {
	NORMAL64, KEY_PREFIX64, KEY_CONFIG64, MOUSE_PREFIX64, MOUSE_CONFIG64,
	KEY_RESET64, KEY_SLOT64, KEY_ADDRESS64, KEY_POLL64, KEY_ARM64,
	RETIRE_FAIL64, FALLBACK64, RECOVER64, MOUSE_RECOVER64
};
static enum BOARD_CASE64 mode64;
static uint32_t selected64, keyboard_controller64;
static unsigned int quarantined64[2], starts64[2], polls64[2], mouse_events64;
static uint32_t enum_port64[2];
static unsigned int loops64, beats64, reaps64;
static uint64_t clock64;
static jmp_buf finish64;
static char serial64[16384];
static size_t serial_used64;
static uint8_t pixels64[800 * 600];
static struct SHEET64 sheet64;
static int console_token64, cursor_updates64, system_keys64, console_keys64;
struct MEMMAN64 memman64;

static struct CONSOLE64 *console64_token(void)
{
	return (struct CONSOLE64 *) (void *) &console_token64;
}
void arch64_dbg_puts(const char *text)
{
	size_t bytes = strlen(text);

	assert(bytes < sizeof(serial64) - serial_used64);
	memcpy(serial64 + serial_used64, text, bytes + 1);
	serial_used64 += bytes;
}
void arch64_panic_blink(int code)
{
	fprintf(stderr, "board case %d unexpectedly panicked: %d\n", mode64, code);
	longjmp(finish64, 2);
}
void arch64_enter_high(void) { abort(); }
void arch64_early_init(void) { }
void arch64_mmu_init(void) { }
void arch64_mmu_finish_high(void) { }
void arch64_cpu_init(void) { }
void arch64_irqctl_init(void) { }
int arch64_mmu_self_test(void) { return 0; }
int arch64_scheduler_init(void) { return 0; }
void arch64_timer_init(struct FIFO64 *fifo) { (void) fifo; }
void arch64_timer_set_debug_output(int enabled) { (void) enabled; }
void arch64_irq_enable(void) { }
void arch64_scheduler_main_beat(void) { beats64++; }
int arch64_fp_context_self_test(void) { return 1; }
void aarch64_act_led_set64(int on) { (void) on; }
uint64_t aarch64_boot_counter64(void) { return clock64 += 500; }
uint64_t aarch64_boot_frequency64(void) { return 1000; }
void aarch64_boot_relax64(void)
{
	if (++loops64 == 6) { longjmp(finish64, 1); }
}
uint64_t platform_irq_save64(void) { return 0; }
void platform_irq_restore64(uint64_t state) { (void) state; }
int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) task; (void) level; (void) priority;
	abort();
}
int arch64_fb_probe(struct BOOTINFO64 *info)
{
	*info = (struct BOOTINFO64) { .scrnx = 800, .scrny = 600, .bpp = 32,
		.bytes_per_scanline = 3200, .vram = (uintptr_t) pixels64 };
	return 0;
}
void arch64_fb_set_hangul_font(const uint8_t *font) { assert(font != NULL); }
void init_palette64(void) { }
void init_memory64(void) { }
size_t memman64_total(const struct MEMMAN64 *man) { (void) man; return 0x100000; }
int block64_init(void) { return 0; }
int fd64_init(void) { return 0; }
int fd64_open(struct FDHANDLE64 *file, const char *name)
{
	assert(strcmp(name, "H04.FNT") == 0);
	memset(file, 0, sizeof(*file));
	file->info.size = HANGUL64_FONT_SIZE;
	return 1;
}
size_t fd64_read(struct FDHANDLE64 *file, void *data, size_t bytes)
{
	(void) file;
	memset(data, 0, bytes);
	return bytes;
}
void window64_set_hangul_font(const uint8_t *font) { assert(font != NULL); }
void console64_set_hangul_font(const uint8_t *font) { assert(font != NULL); }
struct CONSOLE64 *console64_active(void) { return console64_token(); }
void console64_init_on_sheet(const struct BOOTINFO64 *info, struct SHEET64 *sheet)
{
	assert(info->bpp == 32 && sheet == &sheet64);
	assert(sheet->bysize == 600);
}
int console64_start_task(struct CONSOLE64 *console) { assert(console != NULL); return 0; }
int console64_post_input_key(struct CONSOLE64 *console, uint16_t key)
{
	(void) console; (void) key;
	assert(key != 0x57);
	console_keys64++;
	return 0;
}
void console64_reap_closed(void) { reaps64++; }
void console64_cursor_update(uint64_t ticks)
{
	assert(ticks == 123);
	cursor_updates64++;
}
uint64_t arch64_timer_ticks(void) { return 123; }
struct SHEET64 *gui64_init(const struct BOOTINFO64 *info)
{
	sheet64.buf = pixels64;
	sheet64.bxsize = info->scrnx;
	sheet64.bysize = info->scrny;
	return &sheet64;
}
struct CONSOLE64 *gui64_focused_console(void) { return console64_token(); }
int gui64_handle_key(uint16_t key)
{
	system_keys64++;
	return key == 0x57;
}
void gui64_mouse_event(int32_t dx, int32_t dy, int32_t buttons)
{
	(void) dx; (void) dy; (void) buttons;
	mouse_events64++;
}
int aarch64_boot_smoke_sheet64(void) { return 0; }
int aarch64_boot_smoke_mouse64(void) { return 0; }
int aarch64_boot_smoke_framebuffer64(const struct BOOTINFO64 *info) { (void) info; return 0; }
int aarch64_boot_smoke_window64(const struct BOOTINFO64 *info, const uint8_t *font)
{
	(void) info; (void) font;
	return 0;
}
int aarch64_boot_smoke_gui64(const struct BOOTINFO64 *info, struct SHEET64 *sheet)
{
	(void) info; (void) sheet;
	return 0;
}
int aarch64_boot_smoke_keyboard64(uintptr_t base, uint32_t slot,
	const struct XHCI64_HID_RESULT *hid)
{
	(void) base; (void) slot; (void) hid;
	return 0;
}
int pcie64_probe_rp1(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d,
	uint32_t *e, uint32_t *f, uint32_t *g, uint32_t *h)
{
	*a = *b = *c = *d = *e = *f = *g = *h = 0;
	return 0;
}
int pcie64_enable_rp1_dma(uint32_t state[4]) { memset(state, 0, 16); return 0; }
int rp164_probe(uint32_t bar, uint32_t root, uint32_t command, uint32_t *id, uint32_t *platform)
{
	(void) bar; (void) root; (void) command;
	*id = *platform = 0;
	return 0;
}
int rp164_probe_uart0(uint32_t bar, uint32_t root, uint32_t registers[5])
{
	(void) bar; (void) root;
	memset(registers, 0, 20);
	return 0;
}
int rp164_uart0_loopback(uint32_t bar, uint32_t root, uint32_t *echo)
{
	(void) bar; (void) root; *echo = 0; return 0;
}
int rp164_bar1_base(uint32_t bar, uint32_t root, uintptr_t *base)
{
	(void) bar; (void) root; *base = 0x1000; return 0;
}
int xhci64_probe_rp1(uintptr_t base, uint32_t caps[2], uint32_t params[2])
{
	assert(base == 0x1000); caps[0] = caps[1] = params[0] = params[1] = 0; return 0;
}
int xhci64_reset_rp1(uintptr_t base, struct XHCI64_RESET_RESULT results[2])
{
	assert(base == 0x1000); memset(results, 0, sizeof(*results) * 2); return 0;
}
int xhci64_select_controller(uint32_t controller)
{
	assert(controller <= 1); selected64 = controller; return 0;
}
int xhci64_start_rp1(uintptr_t base, struct XHCI64_START_RESULT *result)
{
	assert(base == 0x1000);
	selected64 = keyboard_controller64;
	starts64[selected64]++;
	memset(result, 0, sizeof(*result));
	result->controller_id = selected64;
	return 0;
}
int xhci64_start_controller(uintptr_t base, uint32_t controller,
	struct XHCI64_START_RESULT *result)
{
	assert(base == 0x1000 && controller == (keyboard_controller64 ^ 1U));
	assert(++starts64[controller] == 1);
	selected64 = controller;
	memset(result, 0, sizeof(*result)); result->controller_id = controller; return 0;
}
int xhci64_port_inventory(uintptr_t base, struct XHCI64_PORT_INVENTORY results[2])
{
	assert(base == 0x1000); memset(results, 0, sizeof(*results) * 2);
	results[0].port_count = results[1].port_count = 3;
	return 0;
}
int xhci64_noop_command(uintptr_t base, struct XHCI64_COMMAND_RESULT *result)
{
	assert(base == 0x1000); memset(result, 0, sizeof(*result)); return 0;
}
int xhci64_reset_connected_port(uintptr_t base, struct XHCI64_PORT_RESULT *result)
{
	assert(base == 0x1000);
	if (quarantined64[selected64] && mode64 != RECOVER64 && mode64 != MOUSE_RECOVER64) {
		result->port_id = 0;
		return -2;
	}
	result->port_id = quarantined64[selected64] ? 2 : 1;
	enum_port64[selected64] = result->port_id;
	result->status_after = 3U << 10;
	return selected64 == keyboard_controller64 && mode64 == KEY_RESET64 ? -4 : 0;
}
int xhci64_enable_slot(uintptr_t base, struct XHCI64_SLOT_RESULT *result)
{
	assert(base == 0x1000); result->slot_id = 1;
	return selected64 == keyboard_controller64 && mode64 == KEY_SLOT64 ? -3 : 0;
}
int xhci64_address_device(uintptr_t base, uint32_t port, uint32_t speed, uint32_t slot,
	struct XHCI64_ADDRESS_RESULT *result)
{
	assert(base == 0x1000 && port == enum_port64[selected64] && speed == 3 && slot == 1);
	(void) result;
	return selected64 == keyboard_controller64 && mode64 == KEY_ADDRESS64 ? -3 : 0;
}
int xhci64_read_device_descriptor8(uintptr_t base, uint32_t slot,
	struct XHCI64_DESCRIPTOR_RESULT *result)
{
	assert(base == 0x1000 && slot == 1); (void) result;
	return (mode64 == KEY_PREFIX64 && selected64 == keyboard_controller64) ||
		(mode64 == MOUSE_PREFIX64 && selected64 != keyboard_controller64) ? -4 : 0;
}
static int hid64(struct XHCI64_HID_RESULT *result)
{
	*result = (struct XHCI64_HID_RESULT) { .configuration_value = 1,
		.endpoint_address = 0x81, .endpoint_max_packet = 8, .endpoint_interval = 10 };
	return 0;
}
int xhci64_find_boot_keyboard(uintptr_t base, uint32_t slot, struct XHCI64_HID_RESULT *result)
{
	assert(base == 0x1000 && slot == 1 && selected64 == keyboard_controller64);
	return mode64 == KEY_CONFIG64 || mode64 == RETIRE_FAIL64 ||
		(mode64 == RECOVER64 && enum_port64[selected64] == 1) ? -8 : hid64(result);
}
int xhci64_find_boot_mouse(uintptr_t base, uint32_t slot, struct XHCI64_HID_RESULT *result)
{
	assert(base == 0x1000 && slot == 1 && selected64 != keyboard_controller64);
	return mode64 == MOUSE_CONFIG64 ||
		(mode64 == MOUSE_RECOVER64 && enum_port64[selected64] == 1) ? -8 : hid64(result);
}
int xhci64_configure_boot_hid(uintptr_t base, uint32_t slot, uint32_t speed,
	const struct XHCI64_HID_RESULT *hid, struct XHCI64_CONFIGURE_RESULT *result)
{
	assert(base == 0x1000 && slot == 1 && speed == 3 && hid->endpoint_address == 0x81);
	(void) result; return 0;
}
int xhci64_configure_boot_keyboard(uintptr_t base, uint32_t slot, uint32_t speed,
	const struct XHCI64_HID_RESULT *hid, struct XHCI64_CONFIGURE_RESULT *result)
{
	return xhci64_configure_boot_hid(base, slot, speed, hid, result);
}
int xhci64_hid_set_boot_protocol(uintptr_t base, uint32_t slot,
	const struct XHCI64_HID_RESULT *hid)
{
	assert(base == 0x1000 && slot == 1 && hid->endpoint_address == 0x81); return 0;
}
static int poll_port_valid64(void)
{
	return !quarantined64[selected64] ||
		((mode64 == RECOVER64 || mode64 == MOUSE_RECOVER64) && enum_port64[selected64] == 2);
}
int xhci64_keyboard_arm(uintptr_t base, uint32_t slot, const struct XHCI64_HID_RESULT *hid)
{
	assert(base == 0x1000 && slot == 1 && hid->endpoint_address == 0x81);
	assert(selected64 == keyboard_controller64 && poll_port_valid64());
	return mode64 == KEY_ARM64 ? -1 : 0;
}
int xhci64_hid_arm(uintptr_t base, uint32_t slot, const struct XHCI64_HID_RESULT *hid)
{
	assert(base == 0x1000 && slot == 1 && hid->endpoint_address == 0x81);
	assert(selected64 != keyboard_controller64 && poll_port_valid64()); return 0;
}
int xhci64_keyboard_poll(uintptr_t base, uint32_t slot,
	const struct XHCI64_HID_RESULT *hid, uint8_t report[8])
{
	assert(base == 0x1000 && slot == 1 && hid->endpoint_address == 0x81);
	assert(selected64 == keyboard_controller64 && poll_port_valid64());
	polls64[selected64]++;
	if (mode64 == NORMAL64) {
		memset(report, 0, 8);
		/* F11 press/release, then two ordinary press/release pairs. */
		if (polls64[selected64] == 1) { report[2] = 0x44; }
		if (polls64[selected64] == 3) { report[2] = 4; }
		if (polls64[selected64] == 5) { report[2] = 5; }
		return 8;
	}
	return mode64 == KEY_POLL64 ? -3 : 0;
}
int xhci64_hid_poll(uintptr_t base, uint32_t slot,
	const struct XHCI64_HID_RESULT *hid, uint8_t *report, size_t bytes)
{
	assert(base == 0x1000 && slot == 1 && hid->endpoint_address == 0x81 && bytes == 8);
	assert(selected64 != keyboard_controller64 && poll_port_valid64());
	polls64[selected64]++; memset(report, 0, bytes); return 3;
}
int xhci_port_quarantine64(uintptr_t base, uint32_t port)
{
	assert(base == 0x1000 && port == 1);
	assert(++quarantined64[selected64] == 1);
	return mode64 == RETIRE_FAIL64 ? -110 : 0;
}

int main(void)
{
	for (mode64 = NORMAL64; mode64 <= MOUSE_RECOVER64; mode64++) {
		keyboard_controller64 = mode64 == FALLBACK64 ? 0 : 1;
		memset(quarantined64, 0, sizeof(quarantined64));
		memset(enum_port64, 0, sizeof(enum_port64));
		memset(starts64, 0, sizeof(starts64)); memset(polls64, 0, sizeof(polls64));
		mouse_events64 = loops64 = beats64 = reaps64 = cursor_updates64 = 0;
		system_keys64 = console_keys64 = 0;
		clock64 = serial_used64 = 0;
		assert(setjmp(finish64) == 0 ? (aarch64_high_main(), 0) : loops64 == 6);
		assert(starts64[0] == 1 && starts64[1] == 1 && beats64 >= 5 && reaps64 == 6);
		assert(cursor_updates64 == 6);
		if (mode64 == NORMAL64) { assert(system_keys64 == 6 && console_keys64 == 5); }
		if (mode64 == MOUSE_PREFIX64 || mode64 == MOUSE_CONFIG64) {
			assert(quarantined64[keyboard_controller64 ^ 1U] == 1);
			assert(polls64[keyboard_controller64] == 6 && mouse_events64 == 0);
		} else {
			assert(polls64[keyboard_controller64 ^ 1U] == 6 && mouse_events64 == 6);
		}
		if (mode64 == MOUSE_RECOVER64) {
			assert(quarantined64[keyboard_controller64 ^ 1U] == 1);
			assert(enum_port64[keyboard_controller64 ^ 1U] == 2);
		}
		if (mode64 != NORMAL64 && mode64 != FALLBACK64 &&
				mode64 != MOUSE_PREFIX64 && mode64 != MOUSE_CONFIG64 && mode64 != MOUSE_RECOVER64) {
			assert(quarantined64[keyboard_controller64] == 1);
			if (mode64 == RECOVER64) {
				assert(enum_port64[keyboard_controller64] == 2 && polls64[keyboard_controller64] == 6);
			}
		}
	}
	puts("Pi board orchestration: malformed device isolation, healthy input and heartbeat: PASS");
	return 0;
}
