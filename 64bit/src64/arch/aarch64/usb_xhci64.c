/* USB descriptor and boot-HID policy over controller-owned RP1 transport. */
#include <arch/usb_xhci64.h>
#include <usbhid64.h>

#include "xhci_internal64.h"

_Static_assert(USBHID64_REPORT_MAX <= XHCI64_INTERRUPT_BYTES,
	"boot HID reports must fit the controller transfer buffer");

/* Keep external legacy statuses while private transport returns errno. */
static int legacy_transfer_status64(int status)
{
	return status == -22 ? -1 : status == -110 ? -2 : status == 0 ? 0 : -3;
}

static int read_descriptor64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t descriptor_type, uint32_t descriptor_index, uint32_t length,
	uint8_t *data, struct XHCI64_DESCRIPTOR_RESULT *result)
{
	struct XHCI64_TRANSFER_RESULT transfer = { 0 };
	uint64_t setup;
	int status;

	if (result == NULL || length == 0 || length > XHCI64_CONTROL_BYTES ||
			!xhci_control_ready64(slot_id, 3U)) {
		return -1;
	}
	*result = (struct XHCI64_DESCRIPTOR_RESULT) { 0 };
	setup = 0x0000000000000680ULL | ((uint64_t) descriptor_index << 16) |
		((uint64_t) descriptor_type << 24) | ((uint64_t) length << 48);
	status = xhci_control_read64(rp1_base, slot_id, setup, data, length, &transfer);
	result->event_status = transfer.event_status;
	result->event_control = transfer.event_control;
	result->trb_pointer_low = transfer.trb_pointer_low;
	return legacy_transfer_status64(status);
}

int xhci64_read_device_descriptor8(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_DESCRIPTOR_RESULT *result)
{
	uint8_t data[XHCI64_CONTROL_BYTES];
	int status;

	status = read_descriptor64(rp1_base, slot_id, 1U, 0, 8U, data, result);
	if (status != 0) {
		return status;
	}
	if (data[0] != 18U ||
			data[1] != 1U) {
		return -4;
	}
	result->bcd_usb = (uint32_t) data[2] |
		((uint32_t) data[3] << 8);
	result->device_class_protocol =
		((uint32_t) data[4] << 16) |
		((uint32_t) data[5] << 8) |
		data[6];
	result->ep0_max_packet = data[7];
	if (result->ep0_max_packet != 8U && result->ep0_max_packet != 16U &&
			result->ep0_max_packet != 32U &&
			result->ep0_max_packet != 64U) {
		return -5;
	}
	return 0;
}

static int find_boot_hid64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t protocol, struct XHCI64_HID_RESULT *result)
{
	struct XHCI64_DESCRIPTOR_RESULT transfer;
	struct USBHID64_CONFIG config;
	uint8_t header[9];
	uint32_t total_length;
	uint8_t data[XHCI64_CONTROL_BYTES];
	int status;

	if (result == NULL) {
		return -1;
	}
	result->vendor_product = 0;
	result->configuration_value = 0;
	result->interface_number = 0;
	result->endpoint_address = 0;
	result->endpoint_max_packet = 0;
	result->endpoint_interval = 0;
	result->total_length = 0;
	result->event_status = 0;
	result->event_control = 0;
	status = read_descriptor64(rp1_base, slot_id, 1U, 0, 18U,
		data, &transfer);
	if (status != 0) {
		return -2;
	}
	if (data[0] != 18U ||
			data[1] != 1U ||
			data[17] == 0 || (data[7] != 8U && data[7] != 16U &&
			 data[7] != 32U && data[7] != 64U)) {
		return -3;
	}
	result->vendor_product = (uint32_t) data[8] |
		((uint32_t) data[9] << 8) |
		((uint32_t) data[10] << 16) |
		((uint32_t) data[11] << 24);

	status = read_descriptor64(rp1_base, slot_id, 2U, 0, 9U,
		data, &transfer);
	if (status != 0) {
		return -4;
	}
	if (data[0] != 9U ||
			data[1] != 2U) {
		return -5;
	}
	total_length = (uint32_t) data[2] |
		((uint32_t) data[3] << 8);
	if (total_length < 9U || total_length > sizeof(data)) {
		return -6;
	}
	for (size_t byte = 0; byte < sizeof(header); byte++) { header[byte] = data[byte]; }
	result->total_length = total_length;
	result->configuration_value = data[5];
	status = read_descriptor64(rp1_base, slot_id, 2U, 0, total_length,
		data, &transfer);
	result->event_status = transfer.event_status;
	result->event_control = transfer.event_control;
	if (status != 0) {
		return -7;
	}

	/* A full response must describe the same configuration as its header. */
	for (size_t byte = 0; byte < sizeof(header); byte++) {
		if (header[byte] != data[byte]) { return -8; }
	}
	status = usbhid64_parse_boot_config(data,
		total_length, protocol, &config);
	if (status != 0) {
		return -8;
	}
	result->configuration_value = config.configuration_value;
	result->interface_number = config.interface_number;
	result->endpoint_address = config.endpoint_address;
	result->endpoint_max_packet = config.endpoint_max_packet;
	result->endpoint_interval = config.endpoint_interval;
	return 0;
}

int xhci64_find_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_HID_RESULT *result)
{
	return find_boot_hid64(rp1_base, slot_id, 1U, result);
}

int xhci64_find_boot_mouse(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_HID_RESULT *result)
{
	return find_boot_hid64(rp1_base, slot_id, 2U, result);
}

static int control_no_data64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t request_type, uint32_t request, uint32_t value,
	uint32_t request_index)
{
	uint64_t setup;

	if (request_type > 255U || request > 255U || value > 0xffffU ||
			request_index > 0xffffU || !xhci_control_ready64(slot_id, 2U)) {
		return -1;
	}
	setup = (uint64_t) request_type | ((uint64_t) request << 8) |
		((uint64_t) value << 16) | ((uint64_t) request_index << 32);
	return legacy_transfer_status64(xhci_control_write64(rp1_base, slot_id, setup));
}

static int hid_endpoint64(const struct XHCI64_HID_RESULT *hid, uint32_t *id)
{
	if (hid == NULL || hid->configuration_value == 0 ||
		hid->configuration_value > 255U || hid->interface_number > 255U) {
		return -1;
	}
	return usbhid64_endpoint_id(hid->endpoint_address,
		hid->endpoint_max_packet, hid->endpoint_interval, id);
}

static uint32_t endpoint_interval64(uint32_t port_speed, uint32_t interval)
{
	uint32_t encoded;
	uint32_t period;

	if (port_speed >= 3U) {
		return interval == 0 ? 0 : interval - 1U;
	}
	encoded = 3U;
	period = 1U;
	while (period < interval && encoded < 10U) {
		period <<= 1;
		encoded++;
	}
	return encoded;
}

int xhci64_configure_boot_hid(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t port_speed, const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_CONFIGURE_RESULT *result)
{
	uint32_t endpoint_id;
	uint32_t interval;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			slot_id > XHCI64_MAX_SLOTS || port_speed == 0 || port_speed > 5U ||
			hid_endpoint64(hid, &endpoint_id) != 0 ||
			(port_speed >= 3U && hid->endpoint_interval > 16U) ||
			(port_speed == 2U && hid->endpoint_max_packet > 8U) ||
			!xhci_command_ready64()) {
		return -1;
	}
	result->endpoint_id = 0;
	result->endpoint_state = 0;
	result->interval = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	status = control_no_data64(rp1_base, slot_id, 0U, 9U,
		hid->configuration_value, 0);
	if (status != 0) {
		return -2;
	}
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	interval = endpoint_interval64(port_speed, hid->endpoint_interval);
	status = xhci_configure_interrupt64(rp1_base, slot_id, endpoint_id,
		hid->endpoint_max_packet, interval, result);
	return status == -71 ? -4 : status == 0 ? 0 : -3;
}

int xhci64_configure_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t port_speed, const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_CONFIGURE_RESULT *result)
{
	if (hid == NULL || hid->endpoint_max_packet < 8U) {
		return -1;
	}
	return xhci64_configure_boot_hid(rp1_base, slot_id, port_speed, hid,
		result);
}

int xhci64_read_boot_key(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result)
{
	struct XHCI64_TRANSFER_RESULT transfer = { 0 };
	uint8_t data[XHCI64_INTERRUPT_BYTES];
	uint32_t endpoint_id;
	uint32_t index;
	uint32_t report;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			slot_id > XHCI64_MAX_SLOTS || hid_endpoint64(hid, &endpoint_id) != 0 ||
			hid->endpoint_max_packet < 8U ||
			hid->endpoint_max_packet > sizeof(data)) {
		return -1;
	}
	result->modifier = 0;
	result->keycode = 0;
	result->endpoint_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	result->endpoint_id = endpoint_id;
	status = xhci64_keyboard_set_boot_protocol(rp1_base, slot_id, hid);
	if (status != 0) {
		return -2;
	}
	for (report = 0; report < 4U; report++) {
		if (!xhci_interrupt_ready64()) {
			return -3;
		}
		status = xhci_interrupt_read64(rp1_base, slot_id, endpoint_id,
			hid->endpoint_max_packet, 10000U, data, &transfer);
		result->event_status = transfer.event_status;
		result->event_control = transfer.event_control;
		result->trb_pointer_low = transfer.trb_pointer_low;
		if (status != 0) {
			return -4;
		}
		if (usbhid64_keyboard_report_valid(data,
			hid->endpoint_max_packet) != 0) {
			continue;
		}
		result->modifier = data[0];
		for (index = 2U; index < 8U; index++) {
			uint16_t key;

			if (usbhid64_key_transition(data[index], 1, &key) == 0) {
				result->keycode = data[index];
				return 0;
			}
		}
	}
	return -5;
}

int xhci64_hid_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	uint32_t endpoint_id;

	if (slot_id == 0 || slot_id > XHCI64_MAX_SLOTS ||
		hid_endpoint64(hid, &endpoint_id) != 0) {
		return -1;
	}
	return control_no_data64(rp1_base, slot_id, 0x21U, 0x0bU, 0,
		hid->interface_number);
}

int xhci64_keyboard_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	if (hid == NULL || hid->endpoint_max_packet < 8U) {
		return -1;
	}
	return xhci64_hid_set_boot_protocol(rp1_base, slot_id, hid);
}

int xhci64_read_boot_release(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result)
{
	struct XHCI64_TRANSFER_RESULT transfer = { 0 };
	uint8_t data[XHCI64_INTERRUPT_BYTES];
	uint32_t endpoint_id;
	uint32_t index;
	uint32_t attempt;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			slot_id > XHCI64_MAX_SLOTS || hid_endpoint64(hid, &endpoint_id) != 0 ||
			hid->endpoint_max_packet < 8U ||
			!xhci_interrupt_ready64()) {
		return -1;
	}
	result->modifier = 0;
	result->keycode = 0;
	result->endpoint_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	result->endpoint_id = endpoint_id;
	for (attempt = 0; attempt < 4U; attempt++) {
		if (!xhci_interrupt_ready64()) {
			return -2;
		}
		status = xhci_interrupt_read64(rp1_base, slot_id, endpoint_id,
			hid->endpoint_max_packet, 10000U, data, &transfer);
		result->event_status = transfer.event_status;
		result->event_control = transfer.event_control;
		result->trb_pointer_low = transfer.trb_pointer_low;
		if (status != 0) {
			return -3;
		}
		if (usbhid64_keyboard_report_valid(data,
			hid->endpoint_max_packet) != 0) {
			continue;
		}
		result->modifier = data[0];
		result->keycode = 0;
		for (index = 2U; index < 8U; index++) {
			uint16_t key;

			if (usbhid64_key_transition(data[index], 1, &key) == 0) {
				result->keycode = data[index];
				break;
			}
		}
		if (result->modifier == 0 && result->keycode == 0) {
			return 0;
		}
	}
	return -4;
}

int xhci64_hid_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	uint32_t endpoint_id;

	if (hid_endpoint64(hid, &endpoint_id) != 0) {
		return -1;
	}
	return xhci_interrupt_arm64(rp1_base, slot_id, endpoint_id,
		hid->endpoint_max_packet) == 0 ? 0 : -1;
}

int xhci64_hid_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t *report, size_t report_length)
{
	uint32_t endpoint_id;
	int status;

	if (hid_endpoint64(hid, &endpoint_id) != 0) {
		return -1;
	}
	status = xhci_interrupt_poll64(rp1_base, slot_id, endpoint_id,
		hid->endpoint_max_packet, report, report_length);
	return status == -22 ? -1 : status == -5 ? -2 : status == -71 ? -3 : status;
}

int xhci64_keyboard_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	if (hid == NULL || hid->endpoint_max_packet < 8U) {
		return -1;
	}
	return xhci64_hid_arm(rp1_base, slot_id, hid);
}

int xhci64_keyboard_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t report[8])
{
	int status = xhci64_hid_poll(rp1_base, slot_id, hid, report, 8U);

	return status > 0 && status < 8 ? -3 : status;
}

