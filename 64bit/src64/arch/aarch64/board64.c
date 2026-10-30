/* Raspberry Pi 5 boot ordering and the live USB/GUI event loop. */
#include <arch/arch64.h>
#include <arch/usb_xhci64.h>
#include <arch/aarch64_boot64.h>
#include <arch/aarch64_poll64.h>
#include <bootinfo64.h>
#include <block64.h>
#include <console64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <gui64.h>
#include <hangul64.h>
#include <memory64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>
#include <usbhid64.h>
#include <window64.h>

/* Kernel-lifetime storage shared by framebuffer, windows and consoles. */
static uint8_t disk_hangul_font[HANGUL64_FONT_SIZE];
static struct SHEET64 *boot_console_sheet64;

static int load_hangul_font64(void)
{
	struct FDHANDLE64 fh;
	size_t loaded;
	size_t n;

	if (fd64_open(&fh, "H04.FNT") == 0 || fh.info.size != HANGUL64_FONT_SIZE) {
		return -1;
	}
	loaded = 0;
	while (loaded < HANGUL64_FONT_SIZE) {
		n = fd64_read(&fh, disk_hangul_font + loaded,
			HANGUL64_FONT_SIZE - loaded);
		if (n == 0) {
			return -1;
		}
		loaded += n;
	}
	arch64_fb_set_hangul_font(disk_hangul_font);
	return 0;
}

static int init_gui64(const struct BOOTINFO64 *bootinfo)
{
	struct SHEET64 *console_sheet;

	console_sheet = gui64_init(bootinfo);
	if (console_sheet == NULL || console_sheet->buf == NULL ||
			console_sheet->bxsize != bootinfo->scrnx ||
			console_sheet->bysize <= 0 || console_sheet->bysize > bootinfo->scrny) {
		return -1;
	}
	boot_console_sheet64 = console_sheet;
	window64_set_hangul_font(disk_hangul_font);
	return aarch64_boot_smoke_gui64(bootinfo, console_sheet);
}

static int init_console64(const struct BOOTINFO64 *bootinfo)
{
	if (boot_console_sheet64 == NULL) {
		return -1;
	}
	console64_set_hangul_font(disk_hangul_font);
	console64_init_on_sheet(bootinfo, boot_console_sheet64);
	if (console64_start_task(console64_active()) != 0) {
		return -2;
	}
	return 0;
}

void aarch64_boot_log_hex64(uint32_t value)
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

static int reject_port64(uintptr_t rp1_base, uint32_t controller, uint32_t port)
{
	int status;

	if (port == 0) { return -19; }
	status = xhci64_select_controller(controller);
	if (status == 0) { status = xhci_port_quarantine64(rp1_base, port); }
	arch64_dbg_puts("USB port quarantine controller/port/status=");
	aarch64_boot_log_hex64(controller);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(port);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64((uint32_t) status);
	arch64_dbg_puts("\n");
	return status;
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
	struct XHCI64_PORT_INVENTORY xhci_inventory[2];
	struct XHCI64_COMMAND_RESULT xhci_command;
	struct XHCI64_PORT_RESULT xhci_port;
	struct XHCI64_SLOT_RESULT xhci_slot;
	struct XHCI64_ADDRESS_RESULT xhci_address;
	struct XHCI64_DESCRIPTOR_RESULT xhci_descriptor;
	struct XHCI64_HID_RESULT xhci_hid;
	struct XHCI64_CONFIGURE_RESULT xhci_configure;
	struct XHCI64_START_RESULT mouse_start;
	struct XHCI64_PORT_RESULT mouse_port;
	struct XHCI64_SLOT_RESULT mouse_slot;
	struct XHCI64_ADDRESS_RESULT mouse_address;
	struct XHCI64_DESCRIPTOR_RESULT mouse_descriptor;
	struct XHCI64_HID_RESULT mouse_hid;
	struct XHCI64_CONFIGURE_RESULT mouse_configure;
	struct USBHID64_STATE hid_state;
	struct USBHID64_MOUSE_REPORT mouse_decoded;
	struct CONSOLE64 *focused_console;
	uint8_t hid_report[8];
	uint8_t mouse_report[8];
	uint32_t mouse_stage;
	uint32_t keyboard_attempts = 0;
	uint32_t mouse_attempts = 0;
	int quarantine_status;
	struct FIFO64 hid_fifo;
	struct EVENT64 hid_event_buffer[32];
	struct EVENT64 hid_event;
	uint64_t heartbeat_frequency;
	struct POLL_DEADLINE64 heartbeat_deadline;
	int heartbeat_led_on;
	int fp_test_reported;
	uintptr_t rp1_base;
	int mouse_available;
	int keyboard_available = 1;
	int mouse_status;
	int status;

	arch64_mmu_finish_high();
	arch64_cpu_init();
	status = arch64_fb_probe(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("PANIC: framebuffer init failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		/* No framebuffer exists yet, so the LED is the only visible diagnostic. */
		arch64_panic_blink(status <= -2 && status >= -12 ? -status : 3);
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
	/* 태스크 스택도 memman64에서 나오므로 scheduler보다 먼저 한 번만
	   초기화한다. scheduler 뒤에 다시 초기화하면 이미 할당한 스택이
	   free list에 재등록되어 예외 복귀 frame이 파일 데이터에 덮어쓰인다. */
	init_memory64();
	if (memman64_total(&memman64) < MEMMAN64_PAGE_SIZE) {
		arch64_dbg_puts("PANIC: memory init found no usable pages\n");
		arch64_panic_blink(4);
	}
	status = arch64_scheduler_init();
	if (status != 0) {
		arch64_dbg_puts("M3: scheduler init failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(4);
	}
	arch64_timer_init(NULL);
	arch64_dbg_puts("M3: exceptions + scheduler + high-half paging OK\n");
	status = block64_init();
	if (status != 0) {
		arch64_dbg_puts("M4a: SDHCI init failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(6);
	}
	status = fd64_init();
	if (status != 0) {
		arch64_dbg_puts("M4a: FAT32 mount failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(7);
	}
#if MOWKOW64_BOOT_TESTS
	arch64_dbg_puts("destructive boot tests=enabled\n");
	status = aarch64_boot_smoke_storage64();
	if (status == -2) {
		arch64_dbg_puts("M4b: existing write smoke is corrupt\n");
		arch64_panic_blink(9);
	} else if (status != 0 && status != 1) {
		arch64_dbg_puts("M4b: write smoke failed\n");
		arch64_panic_blink(8);
	}
#endif
	if (load_hangul_font64() != 0) {
		arch64_dbg_puts("M4c: H04.FNT load failed\n");
		arch64_panic_blink(10);
	}
	arch64_dbg_puts("M4: SDHCI + FAT32 + Hangul font OK\n");
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
			aarch64_boot_log_hex64(rp1_vendor_device);
			arch64_dbg_puts(" root-buses=");
			aarch64_boot_log_hex64(root_bus_numbers);
			arch64_dbg_puts(" root-mem=");
			aarch64_boot_log_hex64(root_memory_base);
			arch64_dbg_puts("-");
			aarch64_boot_log_hex64(root_memory_limit);
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" command/status=");
		aarch64_boot_log_hex64(rp1_command_status);
		arch64_dbg_puts(" bar1=");
		aarch64_boot_log_hex64(rp1_bar1);
		arch64_dbg_puts(" root-mem-base=");
		aarch64_boot_log_hex64(root_memory_base);
		arch64_dbg_puts(" root-mem-limit=");
		aarch64_boot_log_hex64(root_memory_limit);
		arch64_dbg_puts(" chip-id=");
		aarch64_boot_log_hex64(rp1_chip_id);
		arch64_dbg_puts("\n");
		arch64_panic_blink(12);
	}
	status = rp164_probe_uart0(rp1_bar1, root_memory_base,
		rp1_uart0_registers);
	if (status != 0) {
		arch64_dbg_puts("M5c: RP1 UART0 register probe failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(13);
	}
	rp1_uart0_echo = 0;
	status = rp164_uart0_loopback(rp1_bar1, root_memory_base,
		&rp1_uart0_echo);
	if (status != 0) {
		arch64_dbg_puts("M5d: RP1 UART0 loopback failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" received=");
		aarch64_boot_log_hex64(rp1_uart0_echo);
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" usb0=");
		aarch64_boot_log_hex64(xhci_capability[0]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hcsparams1[0]);
		arch64_dbg_puts(" usb1=");
		aarch64_boot_log_hex64(xhci_capability[1]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hcsparams1[1]);
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" xhci0 cmd/sts=");
		aarch64_boot_log_hex64(xhci_reset[0].command_before);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_reset[0].status_before);
		arch64_dbg_puts(" xhci1 cmd/sts=");
		aarch64_boot_log_hex64(xhci_reset[1].command_before);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_reset[1].status_before);
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
		aarch64_boot_log_hex64(rp1_dma_state[0]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[1]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[2]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[3]);
		arch64_dbg_puts("\n");
		arch64_panic_blink(17);
	}
	status = xhci64_start_rp1(rp1_base, &xhci_start);
	if (status != 0) {
		arch64_dbg_puts("M6c: RP1 xHCI start failed, controller=");
		aarch64_boot_log_hex64(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" hcs2=");
		aarch64_boot_log_hex64(xhci_start.hcsparams2);
		arch64_dbg_puts(" cmd/sts=");
		aarch64_boot_log_hex64(xhci_start.command);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_start.status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(17);
	}
	arch64_dbg_puts("M6c: RP1 xHCI running, controller=");
	aarch64_boot_log_hex64(xhci_start.controller_id);
	arch64_dbg_puts(" cmd/sts=");
	aarch64_boot_log_hex64(xhci_start.command);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(xhci_start.status);
	arch64_dbg_puts("\n");
	status = xhci64_port_inventory(rp1_base, xhci_inventory);
	if (status != 0) {
		arch64_dbg_puts("M8b: xHCI root-port inventory failed\n");
		arch64_panic_blink(40);
	}
	arch64_dbg_puts("M8b: ports usb0 count/connected/enabled=");
	aarch64_boot_log_hex64(xhci_inventory[0].port_count);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(xhci_inventory[0].connected_mask);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(xhci_inventory[0].enabled_mask);
	arch64_dbg_puts(" usb1=");
	aarch64_boot_log_hex64(xhci_inventory[1].port_count);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(xhci_inventory[1].connected_mask);
	arch64_dbg_puts("/");
	aarch64_boot_log_hex64(xhci_inventory[1].enabled_mask);
	arch64_dbg_puts("\n");
	xhci_command.event_status = 0;
	xhci_command.event_control = 0;
	xhci_command.command_pointer_low = 0;
	xhci_command.controller_status = 0;
	status = xhci64_noop_command(rp1_base, &xhci_command);
	if (status != 0) {
		arch64_dbg_puts("M6d: RP1 xHCI No-op failed, controller=");
		aarch64_boot_log_hex64(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_command.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_command.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_command.command_pointer_low);
		arch64_dbg_puts(" usbsts=");
		aarch64_boot_log_hex64(xhci_command.controller_status);
		arch64_dbg_puts(" dma=");
		aarch64_boot_log_hex64(rp1_dma_state[0]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[1]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[2]);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(rp1_dma_state[3]);
		arch64_dbg_puts("\n");
		arch64_panic_blink(18);
	}
keyboard_port64:
	keyboard_available = 1;
	keyboard_attempts++;
	xhci_port.port_id = 0;
	xhci_port.protocol_major = 0;
	xhci_port.status_before = 0;
	xhci_port.status_after = 0;
	status = xhci64_reset_connected_port(rp1_base, &xhci_port);
	if (status != 0) {
		arch64_dbg_puts("M6e: RP1 xHCI port reset failed, controller=");
		aarch64_boot_log_hex64(xhci_start.controller_id);
		arch64_dbg_puts(" status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" port/protocol=");
		aarch64_boot_log_hex64(xhci_port.port_id);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_port.protocol_major);
		arch64_dbg_puts(" portsc=");
		aarch64_boot_log_hex64(xhci_port.status_before);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_port.status_after);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
	}
	xhci_slot.slot_id = 0;
	xhci_slot.event_status = 0;
	xhci_slot.event_control = 0;
	xhci_slot.command_pointer_low = 0;
	status = xhci64_enable_slot(rp1_base, &xhci_slot);
	if (status != 0) {
		arch64_dbg_puts("M6f: RP1 xHCI Enable Slot failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" slot=");
		aarch64_boot_log_hex64(xhci_slot.slot_id);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_slot.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_slot.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_slot.command_pointer_low);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" address/state=");
		aarch64_boot_log_hex64(xhci_address.device_address);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_address.slot_state);
		arch64_dbg_puts(" ctx/mps=");
		aarch64_boot_log_hex64(xhci_address.context_size);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_address.ep0_max_packet);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_address.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_address.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_address.command_pointer_low);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" usb/class=");
		aarch64_boot_log_hex64(xhci_descriptor.bcd_usb);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_descriptor.device_class_protocol);
		arch64_dbg_puts(" mps=");
		aarch64_boot_log_hex64(xhci_descriptor.ep0_max_packet);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_descriptor.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_descriptor.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_descriptor.trb_pointer_low);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" vid/pid=");
		aarch64_boot_log_hex64(xhci_hid.vendor_product);
		arch64_dbg_puts(" cfg/intf/ep=");
		aarch64_boot_log_hex64(xhci_hid.configuration_value);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hid.interface_number);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hid.endpoint_address);
		arch64_dbg_puts(" total/event=");
		aarch64_boot_log_hex64(xhci_hid.total_length);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hid.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_hid.event_control);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
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
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" ep/state/interval=");
		aarch64_boot_log_hex64(xhci_configure.endpoint_id);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_configure.endpoint_state);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_configure.interval);
		arch64_dbg_puts(" event=");
		aarch64_boot_log_hex64(xhci_configure.event_status);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(xhci_configure.event_control);
		arch64_dbg_puts(" ptr-lo=");
		aarch64_boot_log_hex64(xhci_configure.command_pointer_low);
		arch64_dbg_puts("\n");
		goto keyboard_degraded64;
	}
	if (aarch64_boot_smoke_keyboard64(rp1_base, xhci_slot.slot_id, &xhci_hid) != 0) {
		goto keyboard_degraded64;
	}
	goto keyboard_done64;
keyboard_degraded64:
	keyboard_available = 0;
	quarantine_status = reject_port64(rp1_base, xhci_start.controller_id, xhci_port.port_id);
	arch64_dbg_puts("DEGRADED: USB keyboard port rejected\n");
	if (quarantine_status == 0 && keyboard_attempts <
			xhci_inventory[xhci_start.controller_id].port_count) {
		goto keyboard_port64;
	}
keyboard_done64:
	status = aarch64_boot_smoke_mouse64();
	if (status != 0) {
		arch64_dbg_puts("M8a: USB HID mouse report decode failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(39);
	}
	mouse_start = (struct XHCI64_START_RESULT) { 0 };
	mouse_port = (struct XHCI64_PORT_RESULT) { 0 };
	mouse_slot = (struct XHCI64_SLOT_RESULT) { 0 };
	mouse_address = (struct XHCI64_ADDRESS_RESULT) { 0 };
	mouse_descriptor = (struct XHCI64_DESCRIPTOR_RESULT) { 0 };
	mouse_hid = (struct XHCI64_HID_RESULT) { 0 };
	mouse_configure = (struct XHCI64_CONFIGURE_RESULT) { 0 };
	mouse_available = 0;
	mouse_stage = 1U;
	status = xhci64_start_controller(rp1_base, xhci_start.controller_id ^ 1U, &mouse_start);
mouse_port64:
	mouse_attempts++;
	mouse_port = (struct XHCI64_PORT_RESULT) { 0 };
	if (status == 0) {
		mouse_stage = 2U;
		status = xhci64_reset_connected_port(rp1_base, &mouse_port);
	}
	if (status == 0) {
		mouse_stage = 3U;
		status = xhci64_enable_slot(rp1_base, &mouse_slot);
	}
	if (status == 0) {
		mouse_stage = 4U;
		status = xhci64_address_device(rp1_base, mouse_port.port_id,
			(mouse_port.status_after >> 10) & 0xfU, mouse_slot.slot_id,
			&mouse_address);
	}
	if (status == 0) {
		mouse_stage = 5U;
		status = xhci64_read_device_descriptor8(rp1_base,
			mouse_slot.slot_id, &mouse_descriptor);
	}
	if (status == 0) {
		mouse_stage = 6U;
		status = xhci64_find_boot_mouse(rp1_base, mouse_slot.slot_id,
			&mouse_hid);
	}
	if (status == 0) {
		mouse_stage = 7U;
		status = xhci64_configure_boot_hid(rp1_base, mouse_slot.slot_id,
			(mouse_port.status_after >> 10) & 0xfU, &mouse_hid,
			&mouse_configure);
	}
	if (status == 0) {
		mouse_stage = 8U;
		status = xhci64_hid_set_boot_protocol(rp1_base,
			mouse_slot.slot_id, &mouse_hid);
	}
	if (status != 0) {
		quarantine_status = reject_port64(rp1_base, xhci_start.controller_id ^ 1U, mouse_port.port_id);
		arch64_dbg_puts("DEGRADED: USB mouse init failed, stage/status=");
		aarch64_boot_log_hex64(mouse_stage);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts(" port/portsc=");
		aarch64_boot_log_hex64(mouse_port.port_id);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(mouse_port.status_after);
		arch64_dbg_puts(" slot/vidpid=");
		aarch64_boot_log_hex64(mouse_slot.slot_id);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(mouse_hid.vendor_product);
		arch64_dbg_puts(" cfg/intf/ep/mps=");
		aarch64_boot_log_hex64(mouse_hid.configuration_value);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(mouse_hid.interface_number);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(mouse_hid.endpoint_address);
		arch64_dbg_puts("/");
		aarch64_boot_log_hex64(mouse_hid.endpoint_max_packet);
		arch64_dbg_puts("\n");
		if (quarantine_status == 0 && mouse_attempts <
				xhci_inventory[xhci_start.controller_id ^ 1U].port_count) {
			status = 0;
			goto mouse_port64;
		}
	} else {
		mouse_available = 1;
	}
	if (xhci64_select_controller(xhci_start.controller_id) != 0) {
		arch64_dbg_puts("PANIC: USB keyboard controller unavailable\n");
		arch64_panic_blink(41);
	}
	status = aarch64_boot_smoke_sheet64();
	if (status != 0) {
		arch64_dbg_puts("M7a: 32bpp sheet compositor failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(32);
	}
	status = aarch64_boot_smoke_framebuffer64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7b/c: live framebuffer sheet failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(33);
	}
	status = aarch64_boot_smoke_window64(&bootinfo, disk_hangul_font);
	if (status != 0) {
		arch64_dbg_puts("M7d: live window sheet failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(34);
	}
	status = init_gui64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7e: GUI sheet stack failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(35);
	}
	arch64_timer_set_debug_output(0);
	status = init_console64(&bootinfo);
	if (status != 0) {
		arch64_dbg_puts("M7f/j: console64 init failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(36);
	}
	status = fifo64_init(&hid_fifo, 32U, hid_event_buffer, NULL);
	if (status != 0) {
		arch64_dbg_puts("PANIC: live USB keyboard FIFO init failed\n");
		arch64_panic_blink(29);
	}
	usbhid64_state_init(&hid_state);
	status = keyboard_available ?
		xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id, &xhci_hid) : 0;
	if (status != 0) {
		arch64_dbg_puts("M6o: live USB keyboard arm failed, status=");
		aarch64_boot_log_hex64((uint32_t) status);
		arch64_dbg_puts("\n");
		keyboard_available = 0;
		reject_port64(rp1_base, xhci_start.controller_id, xhci_port.port_id);
	}
	if (mouse_available != 0 && (xhci64_select_controller(xhci_start.controller_id ^ 1U) != 0 ||
			xhci64_hid_arm(rp1_base, mouse_slot.slot_id, &mouse_hid) != 0)) {
		arch64_dbg_puts("DEGRADED: live USB mouse arm failed\n");
		mouse_available = 0;
		reject_port64(rp1_base, xhci_start.controller_id ^ 1U, mouse_port.port_id);
	}
	if (xhci64_select_controller(xhci_start.controller_id) != 0) {
		arch64_dbg_puts("PANIC: USB keyboard controller restore failed\n");
		arch64_panic_blink(42);
	}
	heartbeat_frequency = aarch64_boot_frequency64();
	if (poll_deadline_init64(&heartbeat_deadline, aarch64_boot_counter64(),
			heartbeat_frequency, 500U, 1000U) != 0) {
		aarch64_poll_fail64("board", "heartbeat", "CNTFRQ_EL0", 0, heartbeat_frequency, -22);
		arch64_panic_blink(44);
	}
	heartbeat_led_on = 0;
	fp_test_reported = 0;
	aarch64_act_led_set64(heartbeat_led_on);
	arch64_irq_enable();

	/* Keep the ACT heartbeat as an independent liveness signal. */
	for (;;) {
		if (keyboard_available) {
			status = xhci64_select_controller(xhci_start.controller_id);
			if (status == 0) {
				status = xhci64_keyboard_poll(rp1_base, xhci_slot.slot_id,
					&xhci_hid, hid_report);
			}
			if (status < 0) {
				arch64_dbg_puts("DEGRADED: USB keyboard poll failed, status=");
				aarch64_boot_log_hex64((uint32_t) status);
				arch64_dbg_puts("\n");
				keyboard_available = 0;
				reject_port64(rp1_base, xhci_start.controller_id, xhci_port.port_id);
			} else if (status > 0) {
				status = usbhid64_process_report_n(&hid_fifo, &hid_state,
					hid_report, (size_t) status);
				if (status < 0) {
					arch64_dbg_puts("USB keyboard report/FIFO error, status=");
					aarch64_boot_log_hex64((uint32_t) status);
					arch64_dbg_puts("\n");
				}
				while (fifo64_get(&hid_fifo, &hid_event) == 0) {
					if (gui64_handle_key((uint16_t) hid_event.data) != 0) { continue; }
					focused_console = gui64_focused_console();
					if (focused_console != NULL &&
						console64_post_input_key(focused_console,
							(uint16_t) hid_event.data) != 0) {
						arch64_dbg_puts("M7j: console task FIFO failed\n");
						arch64_panic_blink(38);
					}
				}
				if (xhci64_keyboard_arm(rp1_base, xhci_slot.slot_id, &xhci_hid) != 0) {
					arch64_dbg_puts("DEGRADED: USB keyboard rearm failed\n");
					keyboard_available = 0;
					reject_port64(rp1_base, xhci_start.controller_id, xhci_port.port_id);
				}
			}
		}
		if (mouse_available != 0) {
			status = xhci64_select_controller(xhci_start.controller_id ^ 1U);
			if (status == 0) {
				mouse_status = xhci64_hid_poll(rp1_base,
					mouse_slot.slot_id, &mouse_hid, mouse_report,
					sizeof(mouse_report));
			} else {
				mouse_status = status;
			}
			if (mouse_status < 0) {
				arch64_dbg_puts("\nDEGRADED: USB mouse poll failed, status=");
				aarch64_boot_log_hex64((uint32_t) mouse_status);
				arch64_dbg_puts("\n");
				mouse_available = 0;
				reject_port64(rp1_base, xhci_start.controller_id ^ 1U, mouse_port.port_id);
			} else if (mouse_status > 0) {
				status = usbhid64_decode_mouse_report(mouse_report,
					(size_t) mouse_status, &mouse_decoded);
				if (status != 0) {
					arch64_dbg_puts("\nDEGRADED: USB mouse decode failed\n");
					mouse_available = 0;
					reject_port64(rp1_base, xhci_start.controller_id ^ 1U, mouse_port.port_id);
				} else {
					gui64_mouse_event(mouse_decoded.dx, mouse_decoded.dy,
						(int32_t) mouse_decoded.buttons);
					if (xhci64_hid_arm(rp1_base, mouse_slot.slot_id,
							&mouse_hid) != 0) {
						arch64_dbg_puts("\nDEGRADED: USB mouse rearm failed\n");
						mouse_available = 0;
						reject_port64(rp1_base, xhci_start.controller_id ^ 1U, mouse_port.port_id);
					}
				}
			}
		}
		/* 닫기 요청을 처리하고 잠든 console task와 window를 회수한다. */
		console64_cursor_update(arch64_timer_ticks());
		console64_reap_closed();
		/* Do not stall USB polling while preserving the 500 ms heartbeat. */
		if (poll_deadline_expired64(&heartbeat_deadline, aarch64_boot_counter64())) {
			arch64_scheduler_main_beat();
			if (fp_test_reported == 0) {
				status = arch64_fp_context_self_test();
				if (status < 0) {
					arch64_dbg_puts("\nM9b: FP/SIMD context self-test failed\n");
					arch64_panic_blink(43);
				} else if (status > 0) {
					fp_test_reported = 1;
				}
			}
			heartbeat_led_on = !heartbeat_led_on;
			aarch64_act_led_set64(heartbeat_led_on);
			heartbeat_deadline.start += heartbeat_deadline.ticks;
			if (poll_deadline_expired64(&heartbeat_deadline, aarch64_boot_counter64())) {
				heartbeat_deadline.start = aarch64_boot_counter64();
			}
		}
		aarch64_boot_relax64();
	}
}
