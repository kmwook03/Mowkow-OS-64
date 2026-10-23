#ifndef MOWKOW64_USBHID64_H
#define MOWKOW64_USBHID64_H

#include <stddef.h>
#include <stdint.h>

struct FIFO64;

#define USBHID64_REPORT_MAX 64U

struct USBHID64_CONFIG {
	uint32_t configuration_value;
	uint32_t interface_number;
	uint32_t endpoint_address;
	uint32_t endpoint_max_packet;
	uint32_t endpoint_interval;
};

/* Boot interrupt-IN endpoints supported by the fixed report buffer.
 * Reject endpoint zero, reserved address bits and unsupported packet sizes.
 */
int usbhid64_endpoint_id(uint32_t address, uint32_t packet, uint32_t interval,
	uint32_t *endpoint_id);
/* Parse the complete configuration, including descriptors after the chosen
 * interface. Protocol is 1 (keyboard) or 2 (mouse); output changes on success.
 * Check known descriptor prefixes before fields, interface/endpoint counts,
 * alternate-zero endpoint ownership and all HID subordinate extents. Type
 * 0x21 is HID-specific only inside a HID interface; unknown class/vendor
 * descriptors are skipped using their bounded bLength.
 */
int usbhid64_parse_boot_config(const uint8_t *data, size_t length,
	uint32_t protocol, struct USBHID64_CONFIG *config);

struct USBHID64_STATE {
	uint8_t modifiers;
	uint8_t keys[6];
};

struct USBHID64_MOUSE_REPORT {
	uint32_t buttons;
	int32_t dx;
	int32_t dy;
	int32_t wheel;
};

int usbhid64_key_transition(uint8_t usage, int pressed, uint16_t *key);
int usbhid64_emit_transition(struct FIFO64 *fifo, uint8_t usage,
	int pressed);
void usbhid64_state_init(struct USBHID64_STATE *state);
/* 0 for a complete ordinary report, -11 for phantom/error usages, -22 if short. */
int usbhid64_keyboard_report_valid(const uint8_t *report, size_t length);
int usbhid64_process_report(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t report[8]);
/* Report byte 1 is OEM-reserved and ignored. Error usages 1..3 preserve the
 * ordinary key set while modifiers still transition. Duplicate/unsupported
 * usages are ignored. State advances only for events accepted by the FIFO,
 * so callers may drain and retry after an overrun without duplicate events.
 * Single producer owns state; return event count or negative errno.
 */
int usbhid64_process_report_n(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t *report, size_t length);
/* HID boot mouse의 3바이트 기본 report와 선택적인 4번째 wheel 바이트를
   GUI가 바로 소비할 signed 이동량으로 변환한다. */
int usbhid64_decode_mouse_report(const uint8_t *report, size_t length,
	struct USBHID64_MOUSE_REPORT *decoded);

#endif
