/* BCM2712 always-on GPIO: M1's only observable debug channel. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <block64.h>
#include <fd64.h>
#include <memory64.h>
#include <stddef.h>
#include <stdint.h>

#define GIO_AON_BASE 0x107d517c00ULL
#define GIO_DATA_OFFSET 0x04
#define GIO_IODIR_OFFSET 0x08
#define ACT_LED_PIN 9
#define ACT_LED_MASK (1U << ACT_LED_PIN)
#define HANGUL_FONT_SIZE 11520U

static uint8_t disk_hangul_font[HANGUL_FONT_SIZE];

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
	uint32_t window_base_high;
	uint32_t window_base_limit;
	uint32_t window_limit_high;
	uint32_t window_pci_base;
	uint32_t window_root_command;
	uint32_t xhci_capability[2];
	uint32_t xhci_hcsparams1[2];
	uintptr_t rp1_base;
	int status;

	arch64_mmu_finish_high();
	arch64_cpu_init();
	status = arch64_fb_probe(&bootinfo);
	if (status != 0) {
		arch64_panic_blink(status == -2 ? 2 : 3);
	}
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
	arch64_dbg_puts("M3a: vectors + GIC + timer IRQ\n");
	arch64_dbg_puts("M3b: exception-frame scheduler\n");
	arch64_dbg_puts("M3c: shared mtask64 scheduler\n");
	arch64_dbg_puts("M3d: 4 KiB TTBR0 + TTBR1 paging\n");
	arch64_dbg_puts("M3e: TTBR1 high-half kernel + empty user TTBR0\n");
	init_memory64();
	if (block64_init() != 0) {
		arch64_dbg_puts("M4a: SDHCI init failed\n");
		arch64_panic_blink(6);
	}
	arch64_dbg_puts("M4a: SDHCI card ready\n");
	if (fd64_init() != 0) {
		arch64_dbg_puts("M4a: FAT32 mount failed\n");
		arch64_panic_blink(7);
	}
	arch64_dbg_puts("M4a: FAT32 mounted from boot SD\n");
	status = m4_write_smoke64();
	if (status == 0) {
		arch64_dbg_puts("M4b: write smoke created; reboot to verify persistence\n");
	} else if (status == 1) {
		arch64_dbg_puts("M4b: write smoke persisted across reboot\n");
	} else if (status == -2) {
		arch64_dbg_puts("M4b: existing write smoke is corrupt\n");
		arch64_panic_blink(9);
	} else {
		arch64_dbg_puts("M4b: write smoke failed\n");
		arch64_panic_blink(8);
	}
	if (m4_load_hangul_font64() != 0) {
		arch64_dbg_puts("M4c: H04.FNT load failed\n");
		arch64_panic_blink(10);
	}
	arch64_dbg_puts("M4c: H04.FNT loaded from boot SD\n");
	arch64_dbg_puts("M4c: SD 카드 한글 글꼴 적용 성공\n");
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
	arch64_dbg_puts("M5a: RP1 PCIe link up, id=");
	dbg_hex32(rp1_vendor_device);
	arch64_dbg_puts(" class/rev=");
	dbg_hex32(rp1_class_revision);
	arch64_dbg_puts(" bar0=");
	dbg_hex32(rp1_bar0);
	arch64_dbg_puts("\n");
	pcie64_outbound_state(&window_pci_base, &window_base_limit,
		&window_base_high, &window_limit_high, &window_root_command);
	arch64_dbg_puts("M5b: outbound win0 pci-base=");
	dbg_hex32(window_pci_base);
	arch64_dbg_puts(" base/limit=");
	dbg_hex32(window_base_limit);
	arch64_dbg_puts(" high=");
	dbg_hex32(window_base_high);
	arch64_dbg_puts("/");
	dbg_hex32(window_limit_high);
	arch64_dbg_puts(" root-command=");
	dbg_hex32(window_root_command);
	arch64_dbg_puts("\n");
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
	arch64_dbg_puts("M5b: RP1 BAR1 MMIO ready, bar1=");
	dbg_hex32(rp1_bar1);
	arch64_dbg_puts(" root-mem-base=");
	dbg_hex32(root_memory_base);
	arch64_dbg_puts(" root-mem-limit=");
	dbg_hex32(root_memory_limit);
	arch64_dbg_puts(" chip-id=");
	dbg_hex32(rp1_chip_id);
	arch64_dbg_puts(" platform=");
	dbg_hex32(rp1_platform);
	arch64_dbg_puts("\n");
	status = rp164_probe_uart0(rp1_bar1, root_memory_base,
		rp1_uart0_registers);
	if (status != 0) {
		arch64_dbg_puts("M5c: RP1 UART0 register probe failed, status=");
		dbg_hex32((uint32_t) status);
		arch64_dbg_puts("\n");
		arch64_panic_blink(13);
	}
	arch64_dbg_puts("M5c: RP1 UART0 registers accessible, fr=");
	dbg_hex32(rp1_uart0_registers[0]);
	arch64_dbg_puts(" ibrd=");
	dbg_hex32(rp1_uart0_registers[1]);
	arch64_dbg_puts(" fbrd=");
	dbg_hex32(rp1_uart0_registers[2]);
	arch64_dbg_puts(" lcrh=");
	dbg_hex32(rp1_uart0_registers[3]);
	arch64_dbg_puts(" cr=");
	dbg_hex32(rp1_uart0_registers[4]);
	arch64_dbg_puts("\n");
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
	arch64_dbg_puts("M5d: RP1 UART0 internal loopback echoed ");
	dbg_hex32(rp1_uart0_echo & 0xffU);
	arch64_dbg_puts("\n");
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
	arch64_dbg_puts("M6a: RP1 xHCI0 cap/hcs1=");
	dbg_hex32(xhci_capability[0]);
	arch64_dbg_puts("/");
	dbg_hex32(xhci_hcsparams1[0]);
	arch64_dbg_puts(" xHCI1 cap/hcs1=");
	dbg_hex32(xhci_capability[1]);
	arch64_dbg_puts("/");
	dbg_hex32(xhci_hcsparams1[1]);
	arch64_dbg_puts("\n");
	arch64_dbg_puts("MTASK: ");
	arch64_irq_enable();

	/* Keep the M1 heartbeat as an independent liveness signal. */
	for (;;) {
		arch64_scheduler_main_beat();
		act_led_set64(1);
		delay_ms64(500);
		arch64_scheduler_main_beat();
		act_led_set64(0);
		delay_ms64(500);
	}
}
