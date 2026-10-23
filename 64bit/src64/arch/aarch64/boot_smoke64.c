/* Pi 5 boot self-checks; live display probes run before GUI publication. */
#include <arch/arch64.h>
#include <arch/usb_xhci64.h>
#include <arch/aarch64_boot64.h>
#include <arch/aarch64_poll64.h>
#include <bootinfo64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <memory64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>
#include <usbhid64.h>
#include <window64.h>

#define SHEET_SMOKE_WIDTH64 4U
#define SHEET_SMOKE_HEIGHT64 2U
#define SHEET_SMOKE_STRIDE64 24U
#define STRIP_SMOKE_WIDTH64 160U
#define STRIP_SMOKE_HEIGHT64 32U
#define WINDOW_SMOKE_WIDTH64 320U
#define WINDOW_SMOKE_HEIGHT64 96U

static uint8_t sheet_smoke_vram64[SHEET_SMOKE_STRIDE64 * SHEET_SMOKE_HEIGHT64]
	__attribute__((aligned(4)));
static uint8_t sheet_smoke_buffer64[SHEET_SMOKE_WIDTH64 * SHEET_SMOKE_HEIGHT64];
static uint8_t strip_smoke_buffer64[STRIP_SMOKE_WIDTH64 * STRIP_SMOKE_HEIGHT64];
static uint8_t window_smoke_buffer64[WINDOW_SMOKE_WIDTH64 * WINDOW_SMOKE_HEIGHT64];

#if MOWKOW64_BOOT_TESTS
int aarch64_boot_smoke_storage64(void)
{
	static const char payload[] = "Mowkow OS M4 SDHCI write smoke\n";
	struct FDHANDLE64 fh;
	char buffer[sizeof(payload) - 1];
	size_t i;
	size_t n;

	if (fd64_open(&fh, "M4TEST.TXT") != 0) {
		if (fh.info.size != sizeof(payload) - 1) {
			return -2;
		}
		n = fd64_read(&fh, buffer, sizeof(buffer));
		if (n != sizeof(buffer)) {
			return -2;
		}
		for (i = 0; i < sizeof(buffer); i++) {
			if (buffer[i] != payload[i]) {
				return -2;
			}
		}
		return 1; /* A previous boot wrote and synced this exact payload. */
	}
	if (fd64_create(&fh, "M4TEST.TXT") == 0 ||
			fd64_write(&fh, payload, sizeof(payload) - 1) !=
				sizeof(payload) - 1 || fd64_sync() < 0) {
		return -1;
	}
	/* Re-open through fd64 so the directory entry and length are checked too. */
	if (fd64_open(&fh, "M4TEST.TXT") == 0 ||
			fh.info.size != sizeof(payload) - 1 ||
			fd64_read(&fh, buffer, sizeof(buffer)) != sizeof(buffer)) {
		return -1;
	}
	for (i = 0; i < sizeof(buffer); i++) {
		if (buffer[i] != payload[i]) {
			return -1;
		}
	}
	return 0;
}
#endif

int aarch64_boot_smoke_sheet64(void)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	uint32_t *row0;
	uint32_t *row1;
	unsigned int i;

	for (i = 0; i < sizeof(sheet_smoke_vram64); i++) {
		sheet_smoke_vram64[i] = 0x5aU;
	}
	sheet_smoke_buffer64[0] = 1U;
	sheet_smoke_buffer64[1] = 2U;
	sheet_smoke_buffer64[2] = 4U;
	sheet_smoke_buffer64[3] = 7U;
	sheet_smoke_buffer64[4] = 8U;
	sheet_smoke_buffer64[5] = 9U;
	sheet_smoke_buffer64[6] = 14U;
	sheet_smoke_buffer64[7] = 16U;
	ctl = shtctl64_init(&memman64, sheet_smoke_vram64, sizeof(sheet_smoke_vram64),
		SHEET_SMOKE_WIDTH64,
		SHEET_SMOKE_HEIGHT64, SHEET_SMOKE_STRIDE64, 32U);
	if (ctl == NULL) {
		return -1;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -2;
	}
	sheet64_setbuf(sheet, sheet_smoke_buffer64, sizeof(sheet_smoke_buffer64),
		SHEET_SMOKE_WIDTH64, SHEET_SMOKE_WIDTH64,
		SHEET_SMOKE_HEIGHT64, -1);
	sheet->vx0 = 0;
	sheet->vy0 = 0;
	sheet64_updown(sheet, 0);
	row0 = (uint32_t *) (void *) sheet_smoke_vram64;
	row1 = (uint32_t *) (void *) (sheet_smoke_vram64 + SHEET_SMOKE_STRIDE64);
	if (row0[0] != 0x00ff0000U || row0[1] != 0x0000ff00U ||
			row0[2] != 0x000000ffU || row0[3] != 0x00ffffffU ||
			row1[0] != 0x00c6c6c6U || row1[1] != 0x00840000U ||
			row1[2] != 0x00008484U || row1[3] != 0x00000000U) {
		return -3;
	}
	for (i = SHEET_SMOKE_WIDTH64 * 4U; i < SHEET_SMOKE_STRIDE64; i++) {
		if (sheet_smoke_vram64[i] != 0x5aU ||
				sheet_smoke_vram64[SHEET_SMOKE_STRIDE64 + i] != 0x5aU) {
			return -4;
		}
	}
	return 0;
}

int aarch64_boot_smoke_mouse64(void)
{
	static const uint8_t report3[3] = { 0x05U, 0x05U, 0xfbU };
	static const uint8_t report4[4] = { 0x02U, 0x80U, 0x7fU, 0xffU };
	struct USBHID64_MOUSE_REPORT decoded;

	if (usbhid64_decode_mouse_report(report3, sizeof(report3), &decoded) != 0 ||
			decoded.buttons != 0x05U || decoded.dx != 5 || decoded.dy != -5 ||
			decoded.wheel != 0) {
		return -1;
	}
	if (usbhid64_decode_mouse_report(report4, sizeof(report4), &decoded) != 0 ||
			decoded.buttons != 0x02U || decoded.dx != -128 || decoded.dy != 127 ||
			decoded.wheel != -1) {
		return -2;
	}
	if (usbhid64_decode_mouse_report(report3, 2U, &decoded) == 0) {
		return -3;
	}
	return 0;
}

int aarch64_boot_smoke_framebuffer64(const struct BOOTINFO64 *bootinfo)
{
	static const uint8_t purple[3] = { 0x80U, 0x20U, 0xc0U };
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	uint8_t *vram;
	volatile uint32_t *first_row;
	uint32_t x;
	uint32_t y;
	uint32_t y_offset;

	if (bootinfo == NULL || bootinfo->bpp != 32U ||
			bootinfo->scrnx < STRIP_SMOKE_WIDTH64 + 64U ||
			bootinfo->scrny < STRIP_SMOKE_HEIGHT64 + 64U ||
			bootinfo->bytes_per_scanline < bootinfo->scrnx * 4U) {
		return -1;
	}
	set_palette64(PALETTE64_APP_START, PALETTE64_APP_START, purple);
	for (y = 0; y < STRIP_SMOKE_HEIGHT64; y++) {
		for (x = 0; x < STRIP_SMOKE_WIDTH64; x++) {
			if (x < 32U) {
				strip_smoke_buffer64[y * STRIP_SMOKE_WIDTH64 + x] = 1U;
			} else if (x < 64U) {
				strip_smoke_buffer64[y * STRIP_SMOKE_WIDTH64 + x] = 2U;
			} else if (x < 96U) {
				strip_smoke_buffer64[y * STRIP_SMOKE_WIDTH64 + x] = 4U;
			} else if (x < 128U) {
				strip_smoke_buffer64[y * STRIP_SMOKE_WIDTH64 + x] = 7U;
			} else {
				strip_smoke_buffer64[y * STRIP_SMOKE_WIDTH64 + x] = PALETTE64_APP_START;
			}
		}
	}
	y_offset = (uint32_t) bootinfo->scrny - STRIP_SMOKE_HEIGHT64 - 32U;
	vram = (uint8_t *) bootinfo->vram +
		(uintptr_t) y_offset * bootinfo->bytes_per_scanline + 64U * 4U;
	ctl = shtctl64_init(&memman64, vram,
		(size_t) (bootinfo->scrny - y_offset) * bootinfo->bytes_per_scanline -
		(size_t) 64U * 4U, STRIP_SMOKE_WIDTH64,
		STRIP_SMOKE_HEIGHT64, bootinfo->bytes_per_scanline, 32U);
	if (ctl == NULL) {
		return -2;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -3;
	}
	sheet64_setbuf(sheet, strip_smoke_buffer64, sizeof(strip_smoke_buffer64),
		STRIP_SMOKE_WIDTH64, STRIP_SMOKE_WIDTH64,
		STRIP_SMOKE_HEIGHT64, -1);
	sheet->vx0 = 0;
	sheet->vy0 = 0;
	sheet64_updown(sheet, 0);
	first_row = (volatile uint32_t *) (void *) vram;
	if (first_row[0] != 0x00ff0000U || first_row[32] != 0x0000ff00U ||
			first_row[64] != 0x000000ffU || first_row[96] != 0x00ffffffU ||
			first_row[128] != 0x008020c0U) {
		return -4;
	}
	return 0;
}

int aarch64_boot_smoke_window64(const struct BOOTINFO64 *bootinfo,
	const uint8_t *font)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	uint8_t *vram;
	volatile uint32_t *row0;
	volatile uint32_t *row1;
	uint32_t x_offset;
	uint32_t y_offset;
	struct SURFACE64 surface;

	if (bootinfo == NULL || bootinfo->bpp != 32U ||
			bootinfo->scrnx < WINDOW_SMOKE_WIDTH64 + 32U ||
			bootinfo->scrny < WINDOW_SMOKE_HEIGHT64 + 96U) {
		return -1;
	}
	x_offset = (uint32_t) bootinfo->scrnx - WINDOW_SMOKE_WIDTH64 - 32U;
	y_offset = (uint32_t) bootinfo->scrny - WINDOW_SMOKE_HEIGHT64 - 64U;
	vram = (uint8_t *) bootinfo->vram +
		(uintptr_t) y_offset * bootinfo->bytes_per_scanline + x_offset * 4U;
	ctl = shtctl64_init(&memman64, vram,
		(size_t) (bootinfo->scrny - y_offset) * bootinfo->bytes_per_scanline -
		(size_t) x_offset * 4U, WINDOW_SMOKE_WIDTH64,
		WINDOW_SMOKE_HEIGHT64, bootinfo->bytes_per_scanline, 32U);
	if (ctl == NULL) {
		return -2;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -3;
	}
	window64_set_hangul_font(font);
	sheet64_setbuf(sheet, window_smoke_buffer64, sizeof(window_smoke_buffer64),
		WINDOW_SMOKE_WIDTH64, WINDOW_SMOKE_WIDTH64,
		WINDOW_SMOKE_HEIGHT64, -1);
	surface = sheet64_surface(sheet);
	make_window64(&surface, "머꼬 M7d", 1);
	sheet->vx0 = 0;
	sheet->vy0 = 0;
	sheet64_updown(sheet, 0);
	row0 = (volatile uint32_t *) (void *) vram;
	row1 = (volatile uint32_t *) (void *) (vram + bootinfo->bytes_per_scanline);
	if (row0[0] != 0x00c6c6c6U || row0[WINDOW_SMOKE_WIDTH64 - 1U] != 0U ||
			row1[1] != 0x00ffffffU) {
		return -4;
	}
	return 0;
}

int aarch64_boot_smoke_keyboard64(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	struct XHCI64_KEY_RESULT xhci_key;
	struct FIFO64 hid_fifo;
	struct EVENT64 hid_event_buffer[2];
	struct EVENT64 hid_event;
	uint8_t hid_report[8];
	uint32_t hid_index;
	uint16_t key_make;
	uint16_t key_break;
	struct POLL_DEADLINE64 keyboard_test_deadline;
	uint64_t heartbeat_frequency;
	int status;

	xhci_key.modifier = 0;
	xhci_key.keycode = 0;
	xhci_key.endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	xhci_key.event_status = 0;
	xhci_key.event_control = 0;
	xhci_key.trb_pointer_low = 0;
	status = xhci64_keyboard_set_boot_protocol(rp1_base, slot_id, hid);
	if (status == 0) {
		status = xhci64_keyboard_arm(rp1_base, slot_id, hid);
	}
	arch64_dbg_puts("M6 keyboard test: press and release one letter key\n");
	heartbeat_frequency = aarch64_boot_frequency64();
	if (poll_deadline_init64(&keyboard_test_deadline, aarch64_boot_counter64(),
			heartbeat_frequency, 30U, 1U) != 0) {
		aarch64_poll_fail64("usb-keyboard", "boot-input", "CNTFRQ_EL0",
			0, heartbeat_frequency, -22);
		arch64_dbg_puts("M6k: USB keyboard test clock frequency invalid\n");
		return -1;
	}
	while (status == 0 && xhci_key.keycode == 0) {
		if (poll_deadline_expired64(&keyboard_test_deadline, aarch64_boot_counter64())) {
			status = -110;
			aarch64_poll_fail64("usb-keyboard", "boot-key-press", "EVENT_STATUS",
				0, xhci_key.event_status, status);
			break;
		}
		status = xhci64_keyboard_poll(rp1_base, slot_id,
			hid, hid_report);
		if (status == 0) {
			aarch64_boot_relax64();
			continue;
		}
		if (status < 0) {
			break;
		}
		if (usbhid64_keyboard_report_valid(hid_report, (size_t) status) != 0) {
			status = xhci64_keyboard_arm(rp1_base, slot_id, hid);
			continue;
		}
		xhci_key.modifier = hid_report[0];
		for (hid_index = 2U; hid_index < 8U; hid_index++) {
			if (usbhid64_key_transition(hid_report[hid_index], 1, &key_make) == 0) {
				xhci_key.keycode = hid_report[hid_index];
				break;
			}
		}
		if (xhci_key.keycode != 0) {
			status = 0;
		} else {
			status = xhci64_keyboard_arm(rp1_base, slot_id, hid);
		}
	}
	if (status != 0) {
		arch64_dbg_puts("M6k: USB keyboard report failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" ep/mod/key=");
		aarch64_boot_log_hex64(xhci_key.endpoint_id);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_key.modifier);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_key.keycode);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_key.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_key.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_key.trb_pointer_low);
		arch64_dbg_puts("\n");
		return -1;
	}
	key_make = 0;
	key_break = 0;
	(void) poll_deadline_init64(&keyboard_test_deadline, aarch64_boot_counter64(),
		heartbeat_frequency, 30U, 1U);
	status = xhci64_keyboard_arm(rp1_base, slot_id, hid);
	while (status == 0) {
		int key_still_pressed = 0;

		if (poll_deadline_expired64(&keyboard_test_deadline, aarch64_boot_counter64())) {
			status = -110;
			aarch64_poll_fail64("usb-keyboard", "boot-key-release", "EVENT_STATUS",
				0, xhci_key.event_status, status);
			break;
		}
		status = xhci64_keyboard_poll(rp1_base, slot_id,
			hid, hid_report);
		if (status == 0) {
			aarch64_boot_relax64();
			continue;
		}
		if (status < 0) {
			break;
		}
		for (hid_index = 2U; hid_index < 8U; hid_index++) {
			if (hid_report[hid_index] == xhci_key.keycode) {
				key_still_pressed = 1;
				break;
			}
		}
		if (usbhid64_keyboard_report_valid(hid_report, (size_t) status) != 0) {
			key_still_pressed = 1;
		}
		if (key_still_pressed == 0) {
			status = 0;
			break;
		}
		status = xhci64_keyboard_arm(rp1_base, slot_id, hid);
	}
	if (status == 0) {
		status = usbhid64_key_transition((uint8_t) xhci_key.keycode, 1,
			&key_make);
	}
	if (status == 0) {
		status = usbhid64_key_transition((uint8_t) xhci_key.keycode, 0,
			&key_break);
	}
	if (status != 0) {
		arch64_dbg_puts("M6l: USB HID translation failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" usage/release=");
		aarch64_boot_log_hex64(xhci_key.keycode);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(0);
		arch64_dbg_puts("\n");
		return -1;
	}
	hid_event.type = 0;
	hid_event.data = 0;
	status = fifo64_init(&hid_fifo, 2U, hid_event_buffer, NULL);
	if (status == 0) {
		status = usbhid64_emit_transition(&hid_fifo,
			(uint8_t) xhci_key.keycode, 1);
	}
	if (status == 0) {
		status = usbhid64_emit_transition(&hid_fifo,
			(uint8_t) xhci_key.keycode, 0);
	}
	if (status == 0 && (fifo64_status(&hid_fifo) != 2U ||
			fifo64_get(&hid_fifo, &hid_event) != 0 ||
			hid_event.type != EVENT64_KEYBOARD ||
			hid_event.data != key_make)) {
		status = -2;
	}
	if (status == 0 && (fifo64_get(&hid_fifo, &hid_event) != 0 ||
			hid_event.type != EVENT64_KEYBOARD ||
			hid_event.data != key_break || fifo64_status(&hid_fifo) != 0)) {
		status = -3;
	}
	if (status != 0) {
		arch64_dbg_puts("M6m: USB keyboard FIFO failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" remaining/type/data=");
		aarch64_boot_log_hex64(fifo64_status(&hid_fifo));
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(hid_event.type);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(hid_event.data);
		arch64_dbg_puts("\n");
		arch64_panic_blink(27);
	}
	return 0;
}

int aarch64_boot_smoke_gui64(const struct BOOTINFO64 *bootinfo,
	struct SHEET64 *console_sheet)
{
	static const char label[] = "머꼬 M7e GUI console sheet";
	struct SURFACE64 surface;
	volatile uint32_t *row;
	uint32_t x;
	uint32_t y;
	int found;

	surface = (struct SURFACE64) {
		console_sheet->buf, (size_t) console_sheet->bxsize * console_sheet->bysize,
		(uint32_t) console_sheet->bxsize, console_sheet->bxsize, console_sheet->bysize,
		0, 0, console_sheet->bxsize, console_sheet->bysize
	};

	putstr64(&surface, 64, 64, COL64_FFFFFF, label);
	sheet64_refresh(console_sheet, 64, 64, 400, 80);
	found = 0;
	for (y = 64U; y < 80U && found == 0; y++) {
		row = (volatile uint32_t *) (void *) ((uint8_t *) bootinfo->vram +
			(uintptr_t) y * bootinfo->bytes_per_scanline);
		for (x = 64U; x < 400U; x++) {
			if (console_sheet->buf[y * (uint32_t) console_sheet->bxsize + x] ==
					COL64_FFFFFF) {
				if (row[x] != 0x00ffffffU) {
					return -2;
				}
				found = 1;
				break;
			}
		}
	}
	return found != 0 ? 0 : -3;
}
