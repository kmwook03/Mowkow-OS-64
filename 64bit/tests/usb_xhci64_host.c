/* Exercise USB policy with real HID parsing and a scripted transport bridge. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/usb_xhci64.h>
#include "../src64/arch/aarch64/xhci_internal64.h"

static uint8_t device64[18] = {
	18, 1, 0, 2, 0, 0, 0, 8, 0x34, 0x12, 0x78, 0x56, 0, 1, 0, 0, 0, 1
};
static uint8_t config64[] = {
	9, 2, 34, 0, 1, 1, 0, 0x80, 50,
	9, 4, 2, 0, 1, 3, 1, 1, 0,
	9, 0x21, 0x11, 1, 0, 1, 0x22, 63, 0,
	7, 5, 0x81, 3, 8, 0, 10
};
static struct XHCI64_HID_RESULT hid64 = {
	.configuration_value = 1, .interface_number = 2, .endpoint_address = 0x81,
	.endpoint_max_packet = 8, .endpoint_interval = 10
};
static unsigned int reads64, writes64, configure64, interrupts64, arms64, polls64;
static unsigned int fail_read64, report_case64;
static int change_header64;
static int control_error64, command_error64, interrupt_error64, async_result64;
static int control_ready64, command_ready64, interrupt_ready64;
static uint32_t encoded_interval64;
static uint64_t last_setup64;

static void reset64(void)
{
	reads64 = writes64 = configure64 = interrupts64 = arms64 = polls64 = 0;
	fail_read64 = report_case64 = 0;
	change_header64 = 0;
	control_error64 = command_error64 = interrupt_error64 = async_result64 = 0;
	control_ready64 = command_ready64 = interrupt_ready64 = 1;
	config64[16] = 1;
	config64[33] = 10;
	device64[0] = 18;
	device64[7] = 8;
}

int xhci_control_ready64(uint32_t slot_id, uint32_t trbs)
{
	assert(trbs == 2 || trbs == 3);
	return slot_id > 0 && slot_id <= XHCI64_MAX_SLOTS && control_ready64;
}
int xhci_command_ready64(void) { return command_ready64; }
int xhci_interrupt_ready64(void) { return interrupt_ready64; }

static void transfer64(struct XHCI64_TRANSFER_RESULT *result)
{
	result->event_status = 0x01000000;
	result->event_control = 0x01038001;
	result->trb_pointer_low = 0x12340;
}

int xhci_control_read64(uintptr_t base, uint32_t slot, uint64_t setup,
	uint8_t *data, uint32_t length, struct XHCI64_TRANSFER_RESULT *result)
{
	uint32_t type = (uint32_t) (setup >> 24) & 255;

	assert(base == 0x1000 && slot == 1);
	assert((setup & 0xffffff) == 0x680 && (setup >> 32 & 0xffff) == 0);
	assert((setup >> 48) == length && (type == 1 || type == 2));
	reads64++;
	transfer64(result);
	if (reads64 == fail_read64) { return control_error64; }
	if (type == 1) {
		assert(length == 8 || length == 18);
		memcpy(data, device64, length);
	} else {
		assert(length == 9 || length == sizeof(config64));
		memcpy(data, config64, length);
		if (length > 9 && change_header64) { data[5]++; }
	}
	return 0;
}
int xhci_control_write64(uintptr_t base, uint32_t slot, uint64_t setup)
{
	assert(base == 0x1000 && slot == 1);
	writes64++;
	last_setup64 = setup;
	return control_error64;
}
int xhci_configure_interrupt64(uintptr_t base, uint32_t slot,
	uint32_t endpoint_id, uint32_t packet, uint32_t interval,
	struct XHCI64_CONFIGURE_RESULT *result)
{
	assert(base == 0x1000 && slot == 1 && endpoint_id == 3 && packet == 8);
	assert(writes64 == 1 && last_setup64 == 0x10900);
	configure64++;
	encoded_interval64 = interval;
	result->endpoint_id = endpoint_id;
	result->interval = interval;
	result->event_status = 0x01000000;
	result->event_control = 0x01008401;
	result->command_pointer_low = 0x45670;
	result->endpoint_state = command_error64 ? 0 : 1;
	return command_error64;
}
int xhci_interrupt_read64(uintptr_t base, uint32_t slot,
	uint32_t endpoint_id, uint32_t packet, uint32_t timeout_ms,
	uint8_t *data, struct XHCI64_TRANSFER_RESULT *result)
{
	assert(base == 0x1000 && slot == 1 && endpoint_id == 3 && packet == 8);
	assert(timeout_ms == 10000);
	interrupts64++;
	assert(interrupts64 <= 4);
	transfer64(result);
	if (interrupt_error64) { return interrupt_error64; }
	memset(data, 0, packet);
	if (report_case64 == 1 || (report_case64 == 2 && interrupts64 == 1)) {
		data[2] = 1; /* Rollover retries must preserve the four-report bound. */
	} else if (report_case64 == 3) {
		data[0] = 2; /* A held modifier is not a complete release. */
	} else if (report_case64 == 4 || report_case64 == 2) {
		data[2] = 4;
	}
	return 0;
}
int xhci_interrupt_arm64(uintptr_t base, uint32_t slot,
	uint32_t endpoint_id, uint32_t packet)
{
	assert(base == 0x1000 && slot == 1 && endpoint_id == 3 && packet == 8);
	arms64++;
	return async_result64;
}
int xhci_interrupt_poll64(uintptr_t base, uint32_t slot,
	uint32_t endpoint_id, uint32_t packet, uint8_t *report, size_t length)
{
	assert(base == 0x1000 && slot == 1 && endpoint_id == 3 && packet == 8);
	assert(report != NULL && length == 8);
	polls64++;
	return async_result64;
}

static void descriptors64(void)
{
	struct XHCI64_DESCRIPTOR_RESULT descriptor;
	struct XHCI64_HID_RESULT hid;

	reset64();
	assert(xhci64_read_device_descriptor8(0x1000, 1, &descriptor) == 0);
	assert(descriptor.bcd_usb == 0x200 && descriptor.ep0_max_packet == 8);
	assert(descriptor.event_status == 0x01000000 && descriptor.trb_pointer_low == 0x12340);
	device64[0] = 8;
	assert(xhci64_read_device_descriptor8(0x1000, 1, &descriptor) == -4);
	device64[0] = 18;
	device64[7] = 9;
	assert(xhci64_read_device_descriptor8(0x1000, 1, &descriptor) == -5);
	reset64();
	assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) == 0 && reads64 == 3);
	assert(hid.vendor_product == 0x56781234 && hid.total_length == 34);
	assert(hid.configuration_value == 1 && hid.interface_number == 2);
	assert(hid.endpoint_address == 0x81 && hid.endpoint_max_packet == 8);
	assert(hid.endpoint_interval == 10 && hid.event_status == 0x01000000);
	reset64();
	config64[16] = 2;
	assert(xhci64_find_boot_mouse(0x1000, 1, &hid) == 0);
	assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) == -8);
	reset64();
	change_header64 = 1;
	assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) == -8);
	assert(writes64 == 0 && configure64 == 0);
	reset64();
	device64[7] = 9;
	assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) == -3);
	reset64();
	config64[33] = 0;
	assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) == -8);
	for (unsigned int stage = 1; stage <= 3; stage++) {
		reset64();
		fail_read64 = stage;
		control_error64 = -110;
		assert(xhci64_find_boot_keyboard(0x1000, 1, &hid) ==
			(stage == 1 ? -2 : stage == 2 ? -4 : -7));
		assert(reads64 == stage);
	}
	for (unsigned int failure = 0; failure < 2; failure++) {
		reset64();
		fail_read64 = 1;
		control_error64 = failure == 0 ? -110 : -5;
		assert(xhci64_read_device_descriptor8(0x1000, 1, &descriptor) ==
			(failure == 0 ? -2 : -3));
	}
	reset64();
	control_ready64 = 0;
	memset(&descriptor, 0xa5, sizeof(descriptor));
	assert(xhci64_read_device_descriptor8(0x1000, 1, &descriptor) == -1);
	assert(reads64 == 0 && descriptor.bcd_usb == 0xa5a5a5a5);
	assert(xhci64_read_device_descriptor8(0x1000, 1, NULL) == -1);
}

static void configuration64(void)
{
	struct XHCI64_CONFIGURE_RESULT result;
	struct XHCI64_HID_RESULT invalid = hid64;

	for (uint32_t speed = 1; speed <= 5; speed++) {
		reset64();
		assert(xhci64_configure_boot_keyboard(0x1000, 1, speed, &hid64, &result) == 0);
		assert(encoded_interval64 == (speed >= 3 ? 9U : 7U));
		assert(configure64 == 1 && result.endpoint_state == 1);
	}
	reset64();
	control_error64 = -110;
	assert(xhci64_configure_boot_hid(0x1000, 1, 3, &hid64, &result) == -2);
	assert(configure64 == 0 && result.endpoint_id == 0);
	for (unsigned int failure = 0; failure < 2; failure++) {
		reset64();
		command_error64 = failure == 0 ? -5 : -71;
		assert(xhci64_configure_boot_hid(0x1000, 1, 3, &hid64, &result) ==
			(failure == 0 ? -3 : -4));
		assert(result.command_pointer_low == 0x45670);
	}
	reset64();
	command_ready64 = 0;
	assert(xhci64_configure_boot_hid(0x1000, 1, 3, &hid64, &result) == -1);
	assert(writes64 == 0 && configure64 == 0);
	reset64();
	invalid.endpoint_address = 0x80;
	assert(xhci64_configure_boot_hid(0x1000, 1, 3, &invalid, &result) == -1);
	assert(writes64 == 0);
	invalid = hid64;
	invalid.endpoint_interval = 17;
	assert(xhci64_configure_boot_hid(0x1000, 1, 3, &invalid, &result) == -1);
	invalid.endpoint_interval = 10;
	invalid.endpoint_max_packet = 16;
	assert(xhci64_configure_boot_hid(0x1000, 1, 2, &invalid, &result) == -1);
	assert(xhci64_hid_set_boot_protocol(0x1000, 1, &hid64) == 0);
	assert(last_setup64 == 0x200000b21ULL);
	assert(xhci64_hid_set_boot_protocol(0x1000, 0, &hid64) == -1);
}

static void reports64(void)
{
	struct XHCI64_KEY_RESULT result;
	uint8_t report[8];
	const int transport[] = { -22, -5, -71, 0, 3, 8 };
	const int legacy[] = { -1, -2, -3, 0, 3, 8 };

	reset64();
	report_case64 = 2;
	assert(xhci64_read_boot_key(0x1000, 1, &hid64, &result) == 0);
	assert(interrupts64 == 2 && result.keycode == 4 && result.endpoint_id == 3);
	reset64();
	report_case64 = 1;
	assert(xhci64_read_boot_key(0x1000, 1, &hid64, &result) == -5);
	assert(interrupts64 == 4);
	reset64();
	assert(xhci64_read_boot_release(0x1000, 1, &hid64, &result) == 0);
	assert(interrupts64 == 1 && result.modifier == 0 && result.keycode == 0);
	reset64();
	report_case64 = 3;
	assert(xhci64_read_boot_release(0x1000, 1, &hid64, &result) == -4);
	assert(interrupts64 == 4 && result.modifier == 2);
	reset64();
	interrupt_error64 = -110;
	assert(xhci64_read_boot_key(0x1000, 1, &hid64, &result) == -4);
	assert(result.trb_pointer_low == 0x12340);
	assert(xhci64_read_boot_release(0x1000, 1, &hid64, &result) == -3);
	reset64();
	interrupt_ready64 = 0;
	assert(xhci64_read_boot_key(0x1000, 1, &hid64, &result) == -3);
	assert(xhci64_read_boot_release(0x1000, 1, &hid64, &result) == -1);
	assert(interrupts64 == 0);
	for (unsigned int index = 0; index < sizeof(transport) / sizeof(transport[0]); index++) {
		reset64();
		async_result64 = transport[index];
		assert(xhci64_hid_poll(0x1000, 1, &hid64, report, 8) == legacy[index]);
		assert(xhci64_keyboard_poll(0x1000, 1, &hid64, report) ==
			(legacy[index] == 3 ? -3 : legacy[index]));
		assert(polls64 == 2);
		if (async_result64 <= 0) {
			assert(xhci64_keyboard_arm(0x1000, 1, &hid64) ==
				(async_result64 == 0 ? 0 : -1));
		}
	}
	reset64();
	assert(xhci64_hid_arm(0x1000, 1, NULL) == -1 && arms64 == 0);
	assert(xhci64_hid_poll(0x1000, 1, NULL, report, 8) == -1 && polls64 == 0);
}

int main(void)
{
	descriptors64();
	configuration64();
	reports64();
	puts("USB/xHCI descriptor, control, endpoint and report bridge: PASS");
	return 0;
}
