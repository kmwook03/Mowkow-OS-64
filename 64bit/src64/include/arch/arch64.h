#ifndef MOWKOW64_ARCH_ARCH64_H
#define MOWKOW64_ARCH_ARCH64_H

#include <stddef.h>
#include <stdint.h>

#define ARCH64_KERNEL_VA_BASE 0xffffff8000000000ULL

static inline uintptr_t arch64_phys_to_virt(uintptr_t physical)
{
	return (uintptr_t) ARCH64_KERNEL_VA_BASE + physical;
}

static inline uintptr_t arch64_virt_to_phys(uintptr_t virtual_address)
{
	if (virtual_address >= (uintptr_t) ARCH64_KERNEL_VA_BASE) {
		return virtual_address - (uintptr_t) ARCH64_KERNEL_VA_BASE;
	}
	return virtual_address;
}

struct BOOTINFO64;
struct FIFO64;

void arch64_irq_disable(void);
void arch64_irq_enable(void);
void arch64_halt(void);
void arch64_halt_with_irq(void);

void arch64_early_init(void);
void arch64_cpu_init(void);
void arch64_mmu_init(void);
void arch64_mmu_finish_high(void);
int arch64_mmu_self_test(void);
int arch64_user_map_range(uintptr_t base, size_t size, int executable);
void arch64_user_unmap_all(void);
void arch64_sync_user_code(uintptr_t physical, size_t size);
void arch64_irqctl_init(void);
void arch64_timer_init(struct FIFO64 *fifo);
void arch64_timer_set_debug_output(int enabled);
uint64_t arch64_timer_ticks(void);
void arch64_input_init(struct FIFO64 *fifo);
int arch64_fb_probe(struct BOOTINFO64 *bootinfo);
void arch64_fb_set_hangul_font(const uint8_t *font);
int pcie64_probe_rp1(uint32_t *vendor_device, uint32_t *class_revision,
	uint32_t *bar0, uint32_t *bar1, uint32_t *command_status,
	uint32_t *root_bus_numbers, uint32_t *root_memory_base,
	uint32_t *root_memory_limit);
int pcie64_enable_rp1_dma(uint32_t state[4]);
int rp164_probe(uint32_t bar1, uint32_t root_memory_base,
	uint32_t command_status, uint32_t *chip_id, uint32_t *platform);
int rp164_probe_uart0(uint32_t bar1, uint32_t root_memory_base,
	uint32_t registers[5]);
int rp164_uart0_loopback(uint32_t bar1, uint32_t root_memory_base,
	uint32_t *echoed);
int rp164_bar1_base(uint32_t bar1, uint32_t root_memory_base,
	uintptr_t *virtual_base);
int xhci64_probe_rp1(uintptr_t rp1_base, uint32_t capability[2],
	uint32_t hcsparams1[2]);

struct XHCI64_RESET_RESULT {
	uint32_t command_before;
	uint32_t status_before;
	uint32_t command_after;
	uint32_t status_after;
};

int xhci64_reset_rp1(uintptr_t rp1_base,
	struct XHCI64_RESET_RESULT results[2]);

struct XHCI64_START_RESULT {
	uint32_t controller_id;
	uint32_t command;
	uint32_t status;
	uint32_t hcsparams2;
	uint32_t port_status[3];
};

int xhci64_start_rp1(uintptr_t rp1_base,
	struct XHCI64_START_RESULT *result);
int xhci64_start_controller(uintptr_t rp1_base, uint32_t controller_id,
	struct XHCI64_START_RESULT *result);
int xhci64_select_controller(uint32_t controller_id);

#define XHCI64_INVENTORY_PORTS 4U

struct XHCI64_PORT_INVENTORY {
	uint32_t port_count;
	uint32_t connected_mask;
	uint32_t enabled_mask;
	uint32_t port_status[XHCI64_INVENTORY_PORTS];
};

int xhci64_port_inventory(uintptr_t rp1_base,
	struct XHCI64_PORT_INVENTORY inventory[2]);

struct XHCI64_COMMAND_RESULT {
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
	uint32_t controller_status;
};

int xhci64_noop_command(uintptr_t rp1_base,
	struct XHCI64_COMMAND_RESULT *result);

struct XHCI64_PORT_RESULT {
	uint32_t port_id;
	uint32_t protocol_major;
	uint32_t status_before;
	uint32_t status_after;
};

int xhci64_reset_connected_port(uintptr_t rp1_base,
	struct XHCI64_PORT_RESULT *result);

struct XHCI64_SLOT_RESULT {
	uint32_t slot_id;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};

int xhci64_enable_slot(uintptr_t rp1_base,
	struct XHCI64_SLOT_RESULT *result);

struct XHCI64_ADDRESS_RESULT {
	uint32_t device_address;
	uint32_t slot_state;
	uint32_t context_size;
	uint32_t ep0_max_packet;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};

int xhci64_address_device(uintptr_t rp1_base, uint32_t port_id,
	uint32_t port_speed, uint32_t slot_id,
	struct XHCI64_ADDRESS_RESULT *result);

struct XHCI64_DESCRIPTOR_RESULT {
	uint32_t bcd_usb;
	uint32_t device_class_protocol;
	uint32_t ep0_max_packet;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t trb_pointer_low;
};

int xhci64_read_device_descriptor8(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_DESCRIPTOR_RESULT *result);

struct XHCI64_HID_RESULT {
	uint32_t vendor_product;
	uint32_t configuration_value;
	uint32_t interface_number;
	uint32_t endpoint_address;
	uint32_t endpoint_max_packet;
	uint32_t endpoint_interval;
	uint32_t total_length;
	uint32_t event_status;
	uint32_t event_control;
};

int xhci64_find_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_HID_RESULT *result);
int xhci64_find_boot_mouse(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_HID_RESULT *result);

struct XHCI64_CONFIGURE_RESULT {
	uint32_t endpoint_id;
	uint32_t endpoint_state;
	uint32_t interval;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};

int xhci64_configure_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t port_speed, const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_CONFIGURE_RESULT *result);
int xhci64_configure_boot_hid(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t port_speed, const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_CONFIGURE_RESULT *result);

struct XHCI64_KEY_RESULT {
	uint32_t modifier;
	uint32_t keycode;
	uint32_t endpoint_id;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t trb_pointer_low;
};

int xhci64_read_boot_key(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result);
int xhci64_read_boot_release(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result);
int xhci64_keyboard_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid);
int xhci64_hid_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid);
int xhci64_keyboard_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid);
int xhci64_keyboard_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t report[8]);
int xhci64_hid_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid);
int xhci64_hid_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t *report,
	size_t report_length);

void arch64_scheduler_init(void);
uintptr_t arch64_scheduler_tick(uintptr_t frame);
void arch64_scheduler_main_beat(void);
int arch64_scheduler_healthy(void);
uintptr_t arch64_task_frame_init(void (*entry)(void), uintptr_t stack_base,
	size_t stack_size);
uint64_t arch64_irq_save(void);
void arch64_irq_restore(uint64_t state);

int arch64_enter_user(uintptr_t entry, uintptr_t stack, uint64_t argc,
	uintptr_t argv, uintptr_t *saved_kernel_sp);
void arch64_leave_user(uintptr_t kernel_sp, int status);

void arch64_dbg_puts(const char *s);
void arch64_panic_blink(int code);
void arch64_enter_high(void) __attribute__((noreturn));

#endif
