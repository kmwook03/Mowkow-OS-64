#ifndef MOWKOW64_ARCH_USB_XHCI64_H
#define MOWKOW64_ARCH_USB_XHCI64_H

#include <arch/xhci64.h>
#include <stddef.h>
#include <stdint.h>

/* USB descriptor/boot-HID policy over the selected RP1 xHCI controller.
 * Keep selection stable across each call. Only the board's serialized poller
 * may call these operations; no IRQ use or concurrent controller switching.
 * Existing public result layouts and legacy status codes are preserved.
 */
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

#endif
