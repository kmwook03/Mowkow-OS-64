#include <keyboard64.h>
#include <stddef.h>
#include <stdint.h>
#include <usbhid64.h>

/* USB HID usage ID -> the Set-1 key encoding consumed by console64. */
static const uint16_t usage_to_key64[0x66] = {
	[0x04] = 0x1e, [0x05] = 0x30, [0x06] = 0x2e, [0x07] = 0x20,
	[0x08] = 0x12, [0x09] = 0x21, [0x0a] = 0x22, [0x0b] = 0x23,
	[0x0c] = 0x17, [0x0d] = 0x24, [0x0e] = 0x25, [0x0f] = 0x26,
	[0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
	[0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1f, [0x17] = 0x14,
	[0x18] = 0x16, [0x19] = 0x2f, [0x1a] = 0x11, [0x1b] = 0x2d,
	[0x1c] = 0x15, [0x1d] = 0x2c,
	[0x1e] = 0x02, [0x1f] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
	[0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
	[0x26] = 0x0a, [0x27] = 0x0b,
	[0x28] = 0x1c, [0x29] = 0x01, [0x2a] = 0x0e, [0x2b] = 0x0f,
	[0x2c] = 0x39, [0x2d] = 0x0c, [0x2e] = 0x0d, [0x2f] = 0x1a,
	[0x30] = 0x1b, [0x31] = 0x2b, [0x33] = 0x27, [0x34] = 0x28,
	[0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35,
	[0x39] = 0x3a,
	[0x3a] = 0x3b, [0x3b] = 0x3c, [0x3c] = 0x3d, [0x3d] = 0x3e,
	[0x3e] = 0x3f, [0x3f] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
	[0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,
	[0x49] = KEY64_INSERT, [0x4a] = KEY64_HOME, [0x4b] = KEY64_PGUP,
	[0x4c] = KEY64_DELETE, [0x4d] = KEY64_END, [0x4e] = KEY64_PGDN,
	[0x4f] = KEY64_RIGHT, [0x50] = KEY64_LEFT,
	[0x51] = KEY64_DOWN, [0x52] = KEY64_UP,
};

int usbhid64_key_transition(uint8_t usage, int pressed, uint16_t *key)
{
	uint16_t value;

	if (key == NULL || usage >= sizeof(usage_to_key64) /
			sizeof(usage_to_key64[0])) {
		return -1;
	}
	value = usage_to_key64[usage];
	if (value == 0) {
		return -1;
	}
	if (pressed == 0) {
		value = (value & KEY64_EXT) | ((value & 0xffU) | 0x80U);
	}
	*key = value;
	return 0;
}

int usbhid64_emit_transition(struct FIFO64 *fifo, uint8_t usage,
	int pressed)
{
	struct EVENT64 event;
	uint16_t key;

	if (fifo == NULL || usbhid64_key_transition(usage, pressed, &key) != 0) {
		return -1;
	}
	event.type = EVENT64_KEYBOARD;
	event.data = key;
	return fifo64_put(fifo, event);
}

static int emit_key64(struct FIFO64 *fifo, uint16_t key, int pressed)
{
	struct EVENT64 event;

	if (pressed == 0) {
		key = (key & KEY64_EXT) | ((key & 0xffU) | 0x80U);
	}
	event.type = EVENT64_KEYBOARD;
	event.data = key;
	return fifo64_put(fifo, event);
}

static int usage_present64(const uint8_t keys[6], uint8_t usage)
{
	unsigned int i;

	for (i = 0; i < 6U; i++) {
		if (keys[i] == usage) {
			return 1;
		}
	}
	return 0;
}

void usbhid64_state_init(struct USBHID64_STATE *state)
{
	unsigned int i;

	if (state == NULL) {
		return;
	}
	state->modifiers = 0;
	for (i = 0; i < 6U; i++) {
		state->keys[i] = 0;
	}
}

int usbhid64_process_report(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t report[8])
{
	static const uint16_t modifier_keys[8] = {
		0x1d, 0x2a, 0x38, KEY64_EXT | 0x5b,
		KEY64_RCTRL, 0x36, KEY64_RALT, KEY64_EXT | 0x5c,
	};
	uint8_t pressed_modifiers;
	uint8_t released_modifiers;
	unsigned int emitted;
	unsigned int i;

	if (fifo == NULL || state == NULL || report == NULL) {
		return -1;
	}
	emitted = 0;
	pressed_modifiers = report[0] & (uint8_t) ~state->modifiers;
	released_modifiers = state->modifiers & (uint8_t) ~report[0];
	for (i = 0; i < 8U; i++) {
		if ((pressed_modifiers & (1U << i)) != 0) {
			if (emit_key64(fifo, modifier_keys[i], 1) != 0) {
				return -2;
			}
			emitted++;
		}
	}
	for (i = 0; i < 6U; i++) {
		uint8_t usage = state->keys[i];

		if (usage != 0 && usage_present64(report + 2, usage) == 0) {
			if (usbhid64_emit_transition(fifo, usage, 0) != 0) {
				return -3;
			}
			emitted++;
		}
	}
	for (i = 0; i < 6U; i++) {
		uint8_t usage = report[i + 2U];

		if (usage != 0 && usage_present64(state->keys, usage) == 0) {
			if (usbhid64_emit_transition(fifo, usage, 1) != 0) {
				return -4;
			}
			emitted++;
		}
	}
	for (i = 0; i < 8U; i++) {
		if ((released_modifiers & (1U << i)) != 0) {
			if (emit_key64(fifo, modifier_keys[i], 0) != 0) {
				return -5;
			}
			emitted++;
		}
	}
	state->modifiers = report[0];
	for (i = 0; i < 6U; i++) {
		state->keys[i] = report[i + 2U];
	}
	return (int) emitted;
}
