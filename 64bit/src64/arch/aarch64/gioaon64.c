/* BCM2712 always-on GPIO: M1's only observable debug channel. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <block64.h>
#include <console64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <gui64.h>
#include <hangul64.h>
#include <keyboard64.h>
#include <keymap64.h>
#include <memory64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>
#include <usbhid64.h>
#include <utf864.h>
#include <window64.h>

#define GIO_AON_BASE 0x107d517c00ULL
#define GIO_DATA_OFFSET 0x04
#define GIO_IODIR_OFFSET 0x08
#define ACT_LED_PIN 9
#define ACT_LED_MASK (1U << ACT_LED_PIN)
#define HANGUL_FONT_SIZE 11520U

static uint8_t disk_hangul_font[HANGUL_FONT_SIZE];

#define M7A_SMOKE_WIDTH 4U
#define M7A_SMOKE_HEIGHT 2U
#define M7A_SMOKE_STRIDE 24U
#define M7B_STRIP_WIDTH 160U
#define M7B_STRIP_HEIGHT 32U
#define M7D_WINDOW_WIDTH 320U
#define M7D_WINDOW_HEIGHT 96U

static uint8_t m7a_smoke_vram[M7A_SMOKE_STRIDE * M7A_SMOKE_HEIGHT]
	__attribute__((aligned(4)));
static uint8_t m7a_smoke_buffer[M7A_SMOKE_WIDTH * M7A_SMOKE_HEIGHT];
static uint8_t m7b_strip_buffer[M7B_STRIP_WIDTH * M7B_STRIP_HEIGHT];
static uint8_t m7d_window_buffer[M7D_WINDOW_WIDTH * M7D_WINDOW_HEIGHT];
static struct SHEET64 *m7e_console_sheet;

static uintptr_t gio_address64(uint32_t offset)
{
	uintptr_t pc;
	uintptr_t physical;

	__asm__ volatile ("adr %0, ." : "=r" (pc));
	physical = (uintptr_t) GIO_AON_BASE + offset;
	if (pc >= (uintptr_t) ARCH64_KERNEL_VA_BASE) {
		return arch64_phys_to_virt(physical);
	}
	return physical;
}

static inline void barrier64(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void act_led_set64(int on)
{
	volatile uint32_t *gio_data;
	uint32_t value;

	gio_data = (volatile uint32_t *) gio_address64(GIO_DATA_OFFSET);
	value = *gio_data;

	/* The Pi 5 ACT LED is active-low. */
	if (on != 0) {
		value &= ~ACT_LED_MASK;
	} else {
		value |= ACT_LED_MASK;
	}
	*gio_data = value;
	barrier64();
}

static void act_led_init64(void)
{
	volatile uint32_t *gio_iodir;
	uint32_t direction;

	gio_iodir = (volatile uint32_t *) gio_address64(GIO_IODIR_OFFSET);
	direction = *gio_iodir;

	/* brcmstb GIO uses 0 for output and 1 for input. */
	direction &= ~ACT_LED_MASK;
	*gio_iodir = direction;
	barrier64();
	act_led_set64(0);
}

static uint64_t counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static void delay_ms64(uint32_t milliseconds)
{
	uint64_t frequency;
	uint64_t deadline;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000) * milliseconds;
	while ((int64_t) (counter64() - deadline) < 0) {
		__asm__ volatile ("yield");
	}
}

static int m4_write_smoke64(void)
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

static int m4_load_hangul_font64(void)
{
	struct FDHANDLE64 fh;
	size_t loaded;
	size_t n;

	if (fd64_open(&fh, "H04.FNT") == 0 || fh.info.size != HANGUL_FONT_SIZE) {
		return -1;
	}
	loaded = 0;
	while (loaded < HANGUL_FONT_SIZE) {
		n = fd64_read(&fh, disk_hangul_font + loaded,
			HANGUL_FONT_SIZE - loaded);
		if (n == 0) {
			return -1;
		}
		loaded += n;
	}
	arch64_fb_set_hangul_font(disk_hangul_font);
	return 0;
}

static int m7a_sheet32_smoke64(void)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	uint32_t *row0;
	uint32_t *row1;
	unsigned int i;

	for (i = 0; i < sizeof(m7a_smoke_vram); i++) {
		m7a_smoke_vram[i] = 0x5aU;
	}
	m7a_smoke_buffer[0] = 1U;
	m7a_smoke_buffer[1] = 2U;
	m7a_smoke_buffer[2] = 4U;
	m7a_smoke_buffer[3] = 7U;
	m7a_smoke_buffer[4] = 8U;
	m7a_smoke_buffer[5] = 9U;
	m7a_smoke_buffer[6] = 14U;
	m7a_smoke_buffer[7] = 16U;
	ctl = shtctl64_init(&memman64, m7a_smoke_vram, M7A_SMOKE_WIDTH,
		M7A_SMOKE_HEIGHT, M7A_SMOKE_STRIDE, 32U);
	if (ctl == NULL) {
		return -1;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -2;
	}
	sheet64_setbuf(sheet, m7a_smoke_buffer, M7A_SMOKE_WIDTH,
		M7A_SMOKE_HEIGHT, -1);
	sheet->vx0 = 0;
	sheet->vy0 = 0;
	sheet64_updown(sheet, 0);
	row0 = (uint32_t *) (void *) m7a_smoke_vram;
	row1 = (uint32_t *) (void *) (m7a_smoke_vram + M7A_SMOKE_STRIDE);
	if (row0[0] != 0x00ff0000U || row0[1] != 0x0000ff00U ||
			row0[2] != 0x000000ffU || row0[3] != 0x00ffffffU ||
			row1[0] != 0x00c6c6c6U || row1[1] != 0x00840000U ||
			row1[2] != 0x00008484U || row1[3] != 0x00000000U) {
		return -3;
	}
	for (i = M7A_SMOKE_WIDTH * 4U; i < M7A_SMOKE_STRIDE; i++) {
		if (m7a_smoke_vram[i] != 0x5aU ||
				m7a_smoke_vram[M7A_SMOKE_STRIDE + i] != 0x5aU) {
			return -4;
		}
	}
	return 0;
}

static int m7b_live_sheet64(const struct BOOTINFO64 *bootinfo)
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
			bootinfo->scrnx < M7B_STRIP_WIDTH + 64U ||
			bootinfo->scrny < M7B_STRIP_HEIGHT + 64U ||
			bootinfo->bytes_per_scanline < bootinfo->scrnx * 4U) {
		return -1;
	}
	set_palette64(PALETTE64_APP_START, PALETTE64_APP_START, purple);
	for (y = 0; y < M7B_STRIP_HEIGHT; y++) {
		for (x = 0; x < M7B_STRIP_WIDTH; x++) {
			if (x < 32U) {
				m7b_strip_buffer[y * M7B_STRIP_WIDTH + x] = 1U;
			} else if (x < 64U) {
				m7b_strip_buffer[y * M7B_STRIP_WIDTH + x] = 2U;
			} else if (x < 96U) {
				m7b_strip_buffer[y * M7B_STRIP_WIDTH + x] = 4U;
			} else if (x < 128U) {
				m7b_strip_buffer[y * M7B_STRIP_WIDTH + x] = 7U;
			} else {
				m7b_strip_buffer[y * M7B_STRIP_WIDTH + x] = PALETTE64_APP_START;
			}
		}
	}
	y_offset = (uint32_t) bootinfo->scrny - M7B_STRIP_HEIGHT - 32U;
	vram = (uint8_t *) bootinfo->vram +
		(uintptr_t) y_offset * bootinfo->bytes_per_scanline + 64U * 4U;
	ctl = shtctl64_init(&memman64, vram, M7B_STRIP_WIDTH,
		M7B_STRIP_HEIGHT, bootinfo->bytes_per_scanline, 32U);
	if (ctl == NULL) {
		return -2;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -3;
	}
	sheet64_setbuf(sheet, m7b_strip_buffer, M7B_STRIP_WIDTH,
		M7B_STRIP_HEIGHT, -1);
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

static int m7d_live_window64(const struct BOOTINFO64 *bootinfo)
{
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	uint8_t *vram;
	volatile uint32_t *row0;
	volatile uint32_t *row1;
	uint32_t x_offset;
	uint32_t y_offset;

	if (bootinfo == NULL || bootinfo->bpp != 32U ||
			bootinfo->scrnx < M7D_WINDOW_WIDTH + 32U ||
			bootinfo->scrny < M7D_WINDOW_HEIGHT + 96U) {
		return -1;
	}
	x_offset = (uint32_t) bootinfo->scrnx - M7D_WINDOW_WIDTH - 32U;
	y_offset = (uint32_t) bootinfo->scrny - M7D_WINDOW_HEIGHT - 64U;
	vram = (uint8_t *) bootinfo->vram +
		(uintptr_t) y_offset * bootinfo->bytes_per_scanline + x_offset * 4U;
	ctl = shtctl64_init(&memman64, vram, M7D_WINDOW_WIDTH,
		M7D_WINDOW_HEIGHT, bootinfo->bytes_per_scanline, 32U);
	if (ctl == NULL) {
		return -2;
	}
	sheet = sheet64_alloc(ctl);
	if (sheet == NULL) {
		return -3;
	}
	window64_set_hangul_font(disk_hangul_font);
	make_window64(m7d_window_buffer, M7D_WINDOW_WIDTH, M7D_WINDOW_HEIGHT,
		"머꼬 M7d", 1);
	sheet64_setbuf(sheet, m7d_window_buffer, M7D_WINDOW_WIDTH,
		M7D_WINDOW_HEIGHT, -1);
	sheet->vx0 = 0;
	sheet->vy0 = 0;
	sheet64_updown(sheet, 0);
	row0 = (volatile uint32_t *) (void *) vram;
	row1 = (volatile uint32_t *) (void *) (vram + bootinfo->bytes_per_scanline);
	if (row0[0] != 0x00c6c6c6U || row0[M7D_WINDOW_WIDTH - 1U] != 0U ||
			row1[1] != 0x00ffffffU) {
		return -4;
	}
	return 0;
}

static int m7e_gui_stack64(const struct BOOTINFO64 *bootinfo)
{
	static const char label[] = "머꼬 M7e GUI console sheet";
	struct SHEET64 *console_sheet;
	volatile uint32_t *row;
	uint32_t x;
	uint32_t y;
	int found;

	console_sheet = gui64_init(bootinfo);
	if (console_sheet == NULL || console_sheet->buf == NULL ||
			console_sheet->bxsize != bootinfo->scrnx ||
			console_sheet->bysize != bootinfo->scrny) {
		return -1;
	}
	m7e_console_sheet = console_sheet;
	window64_set_hangul_font(disk_hangul_font);
	putstr64(console_sheet->buf, (uint32_t) console_sheet->bxsize,
		64, 64, COL64_FFFFFF, label);
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

static int m7f_console_init64(const struct BOOTINFO64 *bootinfo)
{
	if (m7e_console_sheet == NULL) {
		return -1;
	}
	console64_set_hangul_font(disk_hangul_font);
	console64_init_on_sheet(bootinfo, m7e_console_sheet);
	if (console64_start_task(console64_active()) != 0) {
		return -2;
	}
	return 0;
}

static void dbg_hex32(uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	char text[11];
	int shift;
	int position;

	text[0] = '0';
	text[1] = 'x';
	position = 2;
	for (shift = 28; shift >= 0; shift -= 4) {
		text[position++] = digits[(value >> shift) & 0x0fU];
	}
	text[position] = '\0';
	arch64_dbg_puts(text);
}

void arch64_irq_disable(void)
{
	__asm__ volatile ("msr daifset, #2" ::: "memory");
}

void arch64_irq_enable(void)
{
	__asm__ volatile ("msr daifclr, #2" ::: "memory");
}

uint64_t arch64_irq_save(void)
{
	uint64_t state;

	__asm__ volatile ("mrs %0, daif\n\tmsr daifset, #2" : "=r" (state) :: "memory");
	return state;
}

void arch64_irq_restore(uint64_t state)
{
	__asm__ volatile ("msr daif, %0" :: "r" (state) : "memory");
}

void arch64_halt(void)
{
	__asm__ volatile ("wfi");
}

void arch64_halt_with_irq(void)
{
	__asm__ volatile ("msr daifclr, #2\n\twfi" ::: "memory");
}

void arch64_early_init(void)
{
	act_led_init64();
}

void arch64_panic_blink(int code)
{
	int i;

	act_led_init64();
	if (code < 1) {
		code = 1;
	}
	for (;;) {
		for (i = 0; i < code; i++) {
			act_led_set64(1);
			delay_ms64(150);
			act_led_set64(0);
			delay_ms64(150);
		}
		delay_ms64(1200);
	}
}

void aarch64_main(void)
{
	arch64_early_init();
	arch64_mmu_init();
	arch64_enter_high();
}

void aarch64_high_main(void)
{
	struct BOOTINFO64 bootinfo;
	uint32_t rp1_bar0;
	uint32_t rp1_bar1;
	uint32_t rp1_chip_id;
	uint32_t rp1_class_revision;
	uint32_t rp1_command_status;
	uint32_t rp1_platform;
	uint32_t rp1_vendor_device;
	uint32_t rp1_uart0_registers[5];
	uint32_t rp1_uart0_echo;
	uint32_t root_bus_numbers;
	uint32_t root_memory_base;
	uint32_t root_memory_limit;
	uint32_t rp1_dma_state[4];
	uint32_t xhci_capability[2];
	uint32_t xhci_hcsparams1[2];
	struct XHCI64_RESET_RESULT xhci_reset[2];
	struct XHCI64_START_RESULT xhci_start;
	struct XHCI64_COMMAND_RESULT xhci_command;
	struct XHCI64_PORT_RESULT xhci_port;
	struct XHCI64_SLOT_RESULT xhci_slot;
	struct XHCI64_ADDRESS_RESULT xhci_address;
	struct XHCI64_DESCRIPTOR_RESULT xhci_descriptor;
	struct XHCI64_HID_RESULT xhci_hid;
	struct XHCI64_CONFIGURE_RESULT xhci_configure;
	struct XHCI64_KEY_RESULT xhci_key;
	struct USBHID64_STATE hid_state;
	uint8_t hid_report[8];
	uint32_t hid_index;
	uint16_t key_make;
	uint16_t key_break;
	struct FIFO64 hid_fifo;
	struct EVENT64 hid_event_buffer[32];
	struct EVENT64 hid_event;
	uint64_t heartbeat_frequency;
	uint64_t heartbeat_deadline;
	uint64_t heartbeat_half_period;
	int heartbeat_led_on;
	uintptr_t rp1_base;
	int status;

	arch64_mmu_finish_high();
	arch64_cpu_init();
	status = arch64_fb_probe(&bootinfo);
	if (status != 0) {
		arch64_panic_blink(status == -2 ? 2 : 3);
	}
	init_palette64();
	status = arch64_mmu_self_test();
	if (status != 0) {
		arch64_panic_blink(5);
	}
	arch64_dbg_puts("Mowkow OS\n");
	arch64_dbg_puts("Raspberry Pi 5 / AArch64\n");
	arch64_dbg_puts("M2: MMU + mailbox framebuffer OK\n\n");
	arch64_dbg_puts("한글 화면 출력 성공\n");
	arch64_irqctl_init();
	arch64_scheduler_init();
	arch64_timer_init(NULL);
	arch64_dbg_puts("M3: exceptions + scheduler + high-half paging OK\n");
	init_memory64();
	if (block64_init() != 0) {
		arch64_dbg_puts("M4a: SDHCI init failed\n");
		arch64_panic_blink(6);
	}
	if (fd64_init() != 0) {
		arch64_dbg_puts("M4a: FAT32 mount failed\n");
		arch64_panic_blink(7);
	}
	status = m4_write_smoke64();
	if (status == -2) {
		arch64_dbg_puts("M4b: existing write smoke is corrupt\n");
		arch64_panic_blink(9);
	} else if (status != 0 && status != 1) {
		arch64_dbg_puts("M4b: write smoke failed\n");
		arch64_panic_blink(8);
	}
	if (m4_load_hangul_font64() != 0) {
		arch64_dbg_puts("M4c: H04.FNT load failed\n");
		arch64_panic_blink(10);
	}
	arch64_dbg_puts("M4: SDHCI + FAT32 + write + Hangul font OK\n");
	rp1_vendor_device = 0;
	rp1_class_revision = 0;
	rp1_bar0 = 0;
	rp1_bar1 = 0;
	rp1_command_status = 0;
	root_bus_numbers = 0;
	root_memory_base = 0;
	root_memory_limit = 0;
	status = pcie64_probe_rp1(&rp1_vendor_device, &rp1_class_revision,
		&rp1_bar0, &rp1_bar1, &rp1_command_status, &root_bus_numbers,
		&root_memory_base, &root_memory_limit);
	if (status != 0) {
		if (status == -1) {
			arch64_dbg_puts("M5a: RP1 PCIe link down\n");
		} else {
			arch64_dbg_puts("M5a: RP1 config-space probe failed, id=");
			dbg_hex32(rp1_vendor_device);
			arch64_dbg_puts(" root-buses=");
			dbg_hex32(root_bus_numbers);
			arch64_dbg_puts(" root-mem=");
			dbg_hex32(root_memory_base);
			arch64_dbg_puts("-");
			dbg_hex32(root_memory_limit);
			arch64_dbg_puts("\n");
		}
		arch64_panic_blink(11);
	}
	rp1_chip_id = 0;
	rp1_platform = 0;
	status = rp164_probe(rp1_bar1, root_memory_base, rp1_command_status,
		&rp1_chip_id, &rp1_platform);
	if (status != 0) {
		arch64_dbg_puts("M5b: RP1 BAR1 probe failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" command/status=");
		dbg_hex32(rp1_command_status);
		arch64_dbg_puts(" bar1=");
		dbg_hex32(rp1_bar1);
		arch64_dbg_puts(" root-mem-base=");
		dbg_hex32(root_memory_base);
		arch64_dbg_puts(" root-mem-limit=");
		dbg_hex32(root_memory_limit);
		arch64_dbg_puts(" chip-id=");
		dbg_hex32(rp1_chip_id);
		arch64_dbg_puts("\n");
		arch64_panic_blink(12);
	}
	status = rp164_probe_uart0(rp1_bar1, root_memory_base,
		rp1_uart0_registers);
	if (status != 0) {
		arch64_dbg_puts("M5c: RP1 UART0 register probe failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(13);
	}
	rp1_uart0_echo = 0;
	status = rp164_uart0_loopback(rp1_bar1, root_memory_base,
		&rp1_uart0_echo);
	if (status != 0) {
		arch64_dbg_puts("M5d: RP1 UART0 loopback failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" received=");
		dbg_hex32(rp1_uart0_echo);
		arch64_dbg_puts("\n");
		arch64_panic_blink(14);
	}
	arch64_dbg_puts("M5: PCIe + RP1 BAR1 + UART loopback OK\n");
	if (rp164_bar1_base(rp1_bar1, root_memory_base, &rp1_base) != 0) {
		arch64_dbg_puts("M6a: RP1 BAR1 address unavailable\n");
		arch64_panic_blink(15);
	}
	xhci_capability[0] = 0;
	xhci_capability[1] = 0;
	xhci_hcsparams1[0] = 0;
	xhci_hcsparams1[1] = 0;
	status = xhci64_probe_rp1(rp1_base, xhci_capability, xhci_hcsparams1);
	if (status != 0) {
		arch64_dbg_puts("M6a: RP1 xHCI capability probe failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" usb0=");
		dbg_hex32(xhci_capability[0]);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hcsparams1[0]);
		arch64_dbg_puts(" usb1=");
		dbg_hex32(xhci_capability[1]);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hcsparams1[1]);
		arch64_dbg_puts("\n");
		arch64_panic_blink(15);
	}
	xhci_reset[0].command_before = 0;
	xhci_reset[0].status_before = 0;
	xhci_reset[0].command_after = 0;
	xhci_reset[0].status_after = 0;
	xhci_reset[1].command_before = 0;
	xhci_reset[1].status_before = 0;
	xhci_reset[1].command_after = 0;
	xhci_reset[1].status_after = 0;
	status = xhci64_reset_rp1(rp1_base, xhci_reset);
	if (status != 0) {
		arch64_dbg_puts("M6b: RP1 xHCI reset failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" xhci0 cmd/sts=");
		dbg_hex32(xhci_reset[0].command_before);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_reset[0].status_before);
		arch64_dbg_puts(" xhci1 cmd/sts=");
		dbg_hex32(xhci_reset[1].command_before);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_reset[1].status_before);
		arch64_dbg_puts("\n");
		arch64_panic_blink(16);
	}
	xhci_start.command = 0;
	xhci_start.status = 0;
	xhci_start.hcsparams2 = 0;
	xhci_start.controller_id = 0;
	xhci_start.port_status[0] = 0;
	xhci_start.port_status[1] = 0;
	xhci_start.port_status[2] = 0;
	rp1_dma_state[0] = 0;
	rp1_dma_state[1] = 0;
	rp1_dma_state[2] = 0;
	rp1_dma_state[3] = 0;
	status = pcie64_enable_rp1_dma(rp1_dma_state);
	if (status != 0) {
		arch64_dbg_puts("M6c: RP1 DMA inbound setup failed, state=");
		dbg_hex32(rp1_dma_state[0]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[1]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[2]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[3]);
		arch64_dbg_puts("\n");
		arch64_panic_blink(17);
	}
	status = xhci64_start_rp1(rp1_base, &xhci_start);
	if (status != 0) {
		arch64_dbg_puts("M6c: RP1 xHCI start failed, controller=");
		dbg_hex32(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" hcs2=");
		dbg_hex32(xhci_start.hcsparams2);
		arch64_dbg_puts(" cmd/sts=");
		dbg_hex32(xhci_start.command);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_start.status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(17);
	}
	arch64_dbg_puts("M6c: RP1 xHCI running, controller=");
	dbg_hex32(xhci_start.controller_id);
	arch64_dbg_puts(" cmd/sts=");
	dbg_hex32(xhci_start.command);
	arch64_dbg_puts("/");
	dbg_hex32(xhci_start.status);
	arch64_dbg_puts("\n");
	xhci_command.event_status = 0;
	xhci_command.event_control = 0;
	xhci_command.command_pointer_low = 0;
	xhci_command.controller_status = 0;
	status = xhci64_noop_command(rp1_base, &xhci_command);
	if (status != 0) {
		arch64_dbg_puts("M6d: RP1 xHCI No-op failed, controller=");
		dbg_hex32(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_command.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_command.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_command.command_pointer_low);
		arch64_dbg_puts(" usbsts=");
		dbg_hex32(xhci_command.controller_status);
		arch64_dbg_puts(" dma=");
		dbg_hex32(rp1_dma_state[0]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[1]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[2]);
		arch64_dbg_puts("/");
		dbg_hex32(rp1_dma_state[3]);
		arch64_dbg_puts("\n");
		arch64_panic_blink(18);
	}
	xhci_port.port_id = 0;
	xhci_port.protocol_major = 0;
	xhci_port.status_before = 0;
	xhci_port.status_after = 0;
	status = xhci64_reset_connected_port(rp1_base, &xhci_port);
	if (status != 0) {
		arch64_dbg_puts("M6e: RP1 xHCI port reset failed, controller=");
		dbg_hex32(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" port/protocol=");
		dbg_hex32(xhci_port.port_id);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_port.protocol_major);
		arch64_dbg_puts(" portsc=");
		dbg_hex32(xhci_port.status_before);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_port.status_after);
		arch64_dbg_puts("\n");
		arch64_panic_blink(19);
	}
	xhci_slot.slot_id = 0;
	xhci_slot.event_status = 0;
	xhci_slot.event_control = 0;
	xhci_slot.command_pointer_low = 0;
	status = xhci64_enable_slot(rp1_base, &xhci_slot);
	if (status != 0) {
		arch64_dbg_puts("M6f: RP1 xHCI Enable Slot failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" slot=");
		dbg_hex32(xhci_slot.slot_id);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_slot.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_slot.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_slot.command_pointer_low);
		arch64_dbg_puts("\n");
		arch64_panic_blink(20);
	}
	xhci_address.device_address = 0;
	xhci_address.slot_state = 0;
	xhci_address.context_size = 0;
	xhci_address.ep0_max_packet = 0;
	xhci_address.event_status = 0;
	xhci_address.event_control = 0;
	xhci_address.command_pointer_low = 0;
	status = xhci64_address_device(rp1_base, xhci_port.port_id,
		(xhci_port.status_after >> 10) & 0xfU, xhci_slot.slot_id,
		&xhci_address);
	if (status != 0) {
		arch64_dbg_puts("M6g: RP1 xHCI Address Device failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" address/state=");
		dbg_hex32(xhci_address.device_address);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_address.slot_state);
		arch64_dbg_puts(" ctx/mps=");
		dbg_hex32(xhci_address.context_size);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_address.ep0_max_packet);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_address.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_address.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_address.command_pointer_low);
		arch64_dbg_puts("\n");
		arch64_panic_blink(21);
	}
	xhci_descriptor.bcd_usb = 0;
	xhci_descriptor.device_class_protocol = 0;
	xhci_descriptor.ep0_max_packet = 0;
	xhci_descriptor.event_status = 0;
	xhci_descriptor.event_control = 0;
	xhci_descriptor.trb_pointer_low = 0;
	status = xhci64_read_device_descriptor8(rp1_base, xhci_slot.slot_id,
		&xhci_descriptor);
	if (status != 0) {
		arch64_dbg_puts("M6h: RP1 xHCI descriptor read failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" usb/class=");
		dbg_hex32(xhci_descriptor.bcd_usb);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_descriptor.device_class_protocol);
		arch64_dbg_puts(" mps=");
		dbg_hex32(xhci_descriptor.ep0_max_packet);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_descriptor.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_descriptor.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_descriptor.trb_pointer_low);
		arch64_dbg_puts("\n");
		arch64_panic_blink(22);
	}
	xhci_hid.vendor_product = 0;
	xhci_hid.configuration_value = 0;
	xhci_hid.interface_number = 0;
	xhci_hid.endpoint_address = 0;
	xhci_hid.endpoint_max_packet = 0;
	xhci_hid.endpoint_interval = 0;
	xhci_hid.total_length = 0;
	xhci_hid.event_status = 0;
	xhci_hid.event_control = 0;
	status = xhci64_find_boot_keyboard(rp1_base, xhci_slot.slot_id,
		&xhci_hid);
	if (status != 0) {
		arch64_dbg_puts("M6i: USB boot keyboard descriptor failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" vid/pid=");
		dbg_hex32(xhci_hid.vendor_product);
		arch64_dbg_puts(" cfg/intf/ep=");
		dbg_hex32(xhci_hid.configuration_value);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hid.interface_number);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hid.endpoint_address);
		arch64_dbg_puts(" total/event=");
		dbg_hex32(xhci_hid.total_length);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hid.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_hid.event_control);
		arch64_dbg_puts("\n");
		arch64_panic_blink(23);
	}
	xhci_configure.endpoint_id = 0;
	xhci_configure.endpoint_state = 0;
	xhci_configure.interval = 0;
	xhci_configure.event_status = 0;
	xhci_configure.event_control = 0;
	xhci_configure.command_pointer_low = 0;
	status = xhci64_configure_boot_keyboard(rp1_base, xhci_slot.slot_id,
		(xhci_port.status_after >> 10) & 0xfU, &xhci_hid,
		&xhci_configure);
	if (status != 0) {
		arch64_dbg_puts("M6j: USB keyboard configure failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" ep/state/interval=");
		dbg_hex32(xhci_configure.endpoint_id);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_configure.endpoint_state);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_configure.interval);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_configure.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_configure.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_configure.command_pointer_low);
		arch64_dbg_puts("\n");
		arch64_panic_blink(24);
	}
	xhci_key.modifier = 0;
	xhci_key.keycode = 0;
	xhci_key.endpoint_id = ((xhci_hid.endpoint_address & 0x0fU) << 1) + 1U;
	xhci_key.event_status = 0;
	xhci_key.event_control = 0;
	xhci_key.trb_pointer_low = 0;
	status = xhci64_keyboard_set_boot_protocol(rp1_base, xhci_slot.slot_id,
		&xhci_hid);
	if (status == 0) {
		status = xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id, &xhci_hid);
	}
	arch64_dbg_puts("M6 keyboard test: press and release one letter key\n");
	while (status == 0 && xhci_key.keycode == 0) {
		status = xhci64_keyboard_poll(rp1_base, xhci_slot.slot_id,
			&xhci_hid, hid_report);
		if (status == 0) {
			__asm__ volatile ("yield");
			continue;
		}
		if (status < 0) {
			break;
		}
		xhci_key.modifier = hid_report[0];
		for (hid_index = 2U; hid_index < 8U; hid_index++) {
			if (hid_report[hid_index] != 0) {
				xhci_key.keycode = hid_report[hid_index];
				break;
			}
		}
		if (xhci_key.keycode != 0) {
			status = 0;
		} else {
			status = xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id,
				&xhci_hid);
		}
	}
	if (status != 0) {
		arch64_dbg_puts("M6k: USB keyboard report failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" ep/mod/key=");
		dbg_hex32(xhci_key.endpoint_id);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_key.modifier);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_key.keycode);
		arch64_dbg_puts(" event=");
		dbg_hex32(xhci_key.event_status);
		arch64_dbg_puts("/");
		dbg_hex32(xhci_key.event_control);
		arch64_dbg_puts(" ptr-lo=");
		dbg_hex32(xhci_key.trb_pointer_low);
		arch64_dbg_puts("\n");
		arch64_panic_blink(25);
	}
	key_make = 0;
	key_break = 0;
	status = xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id, &xhci_hid);
	while (status == 0) {
		int key_still_pressed = 0;

		status = xhci64_keyboard_poll(rp1_base, xhci_slot.slot_id,
			&xhci_hid, hid_report);
		if (status == 0) {
			__asm__ volatile ("yield");
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
		if (key_still_pressed == 0) {
			status = 0;
			break;
		}
		status = xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id,
			&xhci_hid);
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
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" usage/release=");
		dbg_hex32(xhci_key.keycode);
		arch64_dbg_puts("/");
		dbg_hex32(0);
		arch64_dbg_puts("\n");
		arch64_panic_blink(26);
	}
	hid_event.type = 0;
	hid_event.data = 0;
	fifo64_init(&hid_fifo, 2U, hid_event_buffer, NULL);
	status = usbhid64_emit_transition(&hid_fifo,
		(uint8_t) xhci_key.keycode, 1);
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
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts(" remaining/type/data=");
		dbg_hex32(fifo64_status(&hid_fifo));
		arch64_dbg_puts("/");
		dbg_hex32(hid_event.type);
		arch64_dbg_puts("/");
		dbg_hex32(hid_event.data);
		arch64_dbg_puts("\n");
		arch64_panic_blink(27);
	}
	status = m7a_sheet32_smoke64();
	if (status != 0) {
		arch64_dbg_puts("M7a: 32bpp sheet compositor failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(32);
	}
	status = m7b_live_sheet64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7b/c: live framebuffer sheet failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(33);
	}
	status = m7d_live_window64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7d: live window sheet failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(34);
	}
	status = m7e_gui_stack64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7e: GUI sheet stack failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(35);
	}
	arch64_timer_set_debug_output(0);
	status = m7f_console_init64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7f/j: console64 init failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(36);
	}
	fifo64_init(&hid_fifo, 32U, hid_event_buffer, NULL);
	usbhid64_state_init(&hid_state);
	status = xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id, &xhci_hid);
	if (status != 0) {
		arch64_dbg_puts("M6o: live USB keyboard arm failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(29);
	}
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (heartbeat_frequency));
	heartbeat_half_period = heartbeat_frequency / 2U;
	if (heartbeat_half_period == 0U) {
		heartbeat_half_period = 1U;
	}
	heartbeat_deadline = counter64() + heartbeat_half_period;
	heartbeat_led_on = 0;
	act_led_set64(heartbeat_led_on);
	arch64_irq_enable();

	/* Keep the M1 heartbeat as an independent liveness signal. */
	for (;;) {
		status = xhci64_keyboard_poll(rp1_base, xhci_slot.slot_id,
			&xhci_hid, hid_report);
		if (status < 0) {
			arch64_dbg_puts("\nM6o: live USB keyboard poll failed, status=");
			dbg_hex32((uint32_t) status);
			arch64_dbg_puts("\n");
			arch64_panic_blink(29);
		}
		if (status > 0) {
			status = usbhid64_process_report(&hid_fifo, &hid_state,
				hid_report);
			if (status < 0 || xhci64_keyboard_arm(rp1_base,
					xhci_slot.slot_id, &xhci_hid) != 0) {
				arch64_dbg_puts("\nM6o: live USB keyboard rearm failed\n");
				arch64_panic_blink(29);
			}
			while (fifo64_get(&hid_fifo, &hid_event) == 0) {
				if (console64_post_input_key(console64_active(),
						(uint16_t) hid_event.data) != 0) {
					arch64_dbg_puts("M7j: console task FIFO failed\n");
					arch64_panic_blink(38);
				}
			}
		}
		/* Do not stall USB polling while preserving the 500 ms heartbeat. */
		if ((int64_t) (counter64() - heartbeat_deadline) >= 0) {
			arch64_scheduler_main_beat();
			heartbeat_led_on = !heartbeat_led_on;
			act_led_set64(heartbeat_led_on);
			heartbeat_deadline += heartbeat_half_period;
			if ((int64_t) (counter64() - heartbeat_deadline) >= 0) {
				heartbeat_deadline = counter64() + heartbeat_half_period;
			}
		}
		__asm__ volatile ("yield");
	}
}
