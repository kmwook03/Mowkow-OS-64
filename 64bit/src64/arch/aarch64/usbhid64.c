#include <keyboard64.h>
#include <stddef.h>
#include <stdint.h>
#include <usbhid64.h>

int usbhid64_endpoint_id(uint32_t address, uint32_t packet, uint32_t interval,
	uint32_t *endpoint_id)
{
	if (endpoint_id == NULL || address < 0x81U || address > 0x8fU ||
		packet < 3U || packet > USBHID64_REPORT_MAX || interval == 0 || interval > 255U) {
		return -22;
	}
	*endpoint_id = ((address & 0x0fU) << 1) + 1U;
	return 0;
}

int usbhid64_parse_boot_config(const uint8_t *data, size_t length,
	uint32_t protocol, struct USBHID64_CONFIG *config)
{
	struct USBHID64_CONFIG selected = { 0 };
	size_t offset;
	uint32_t endpoints = 0;
	uint32_t expected = 0;
	uint32_t seen = 0;
	uint32_t interface_number = 0;
	int have_interface = 0;
	int eligible = 0;
	int have_hid = 0;
	int hid_class = 0;

	if (data == NULL || config == NULL || (protocol != 1U && protocol != 2U)) {
		return -22;
	}
	if (length < 9U || data[0] != 9U || data[1] != 2U ||
		((size_t) data[2] | ((size_t) data[3] << 8)) != length ||
		data[4] == 0 || data[5] == 0) {
		return -84;
	}
	for (offset = 9U; offset < length;) {
		const uint8_t *descriptor = data + offset;
		size_t size;
		uint32_t type;

		if (length - offset < 2U) {
			return -84;
		}
		size = descriptor[0];
		type = descriptor[1];
		if (size < 2U || size > length - offset) {
			return -84;
		}
		if (type == 2U) {
			return -84;
		} else if (type == 4U) {
			if (size < 9U || (have_interface && seen != expected)) {
				return -84;
			}
			have_interface = 1;
			expected = descriptor[4];
			seen = 0;
			endpoints = 0;
			have_hid = 0;
			hid_class = descriptor[5] == 3U;
			interface_number = descriptor[2];
			/* SET_CONFIGURATION selects alternate setting zero. */
			eligible = descriptor[3] == 0 && descriptor[5] == 3U &&
				descriptor[6] == 1U && descriptor[7] == protocol;
		} else if (type == 0x21U && hid_class) {
			uint32_t i;
			int report_descriptor = 0;

			if (!have_interface || size < 6U || descriptor[5] == 0 ||
				descriptor[5] > (size - 6U) / 3U) {
				return -84;
			}
			for (i = 0; i < descriptor[5]; i++) {
				if (descriptor[6U + i * 3U] == 0x22U &&
					(descriptor[7U + i * 3U] || descriptor[8U + i * 3U])) {
					report_descriptor = 1;
				}
			}
			if (!report_descriptor) {
				return -84;
			}
			have_hid = 1;
		} else if (type == 5U) {
			uint32_t address, id, packet;

			if (!have_interface || size < 7U || seen >= expected) {
				return -84;
			}
			address = descriptor[2];
			if ((address & 0x70U) || (address & 0x0fU) == 0) {
				return -84;
			}
			id = ((address & 0x0fU) << 1) + (address >> 7);
			if (endpoints & (1U << id)) {
				return -84;
			}
			endpoints |= 1U << id;
			seen++;
			if (eligible && (address & 0x80U) && (descriptor[3] & 3U) == 3U) {
				packet = (uint32_t) descriptor[4] | ((uint32_t) descriptor[5] << 8);
				if (!have_hid || descriptor[3] != 3U ||
					usbhid64_endpoint_id(address, packet, descriptor[6], &id) != 0 ||
					(protocol == 1U && packet < 8U)) {
					return -84;
				}
				if (selected.endpoint_address == 0) {
					selected.configuration_value = data[5];
					selected.interface_number = interface_number;
					selected.endpoint_address = address;
					selected.endpoint_max_packet = packet;
					selected.endpoint_interval = descriptor[6];
				}
			}
		}
		offset += size;
	}
	if (have_interface && seen != expected) {
		return -84;
	}
	if (selected.endpoint_address == 0) {
		return -19;
	}
	*config = selected;
	return 0;
}

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

int usbhid64_keyboard_report_valid(const uint8_t *report, size_t length)
{
	unsigned int i;

	if (report == NULL || length < 8U) {
		return -22;
	}
	for (i = 2U; i < 8U; i++) {
		if (report[i] >= 1U && report[i] <= 3U) {
			return -11;
		}
	}
	return 0;
}

int usbhid64_process_report_n(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t *report, size_t length)
{
	static const uint16_t modifier_keys[8] = {
		0x1d, 0x2a, 0x38, KEY64_EXT | 0x5b,
		KEY64_RCTRL, 0x36, KEY64_RALT, KEY64_EXT | 0x5c,
	};
	uint8_t pressed_modifiers;
	uint8_t released_modifiers;
	unsigned int emitted;
	unsigned int i;
	unsigned int j;
	uint8_t next[6] = { 0 };
	unsigned int count = 0;
	uint16_t key;
	int rollover = 0;
	int status;

	if (fifo == NULL || state == NULL || report == NULL || length < 8U) {
		return -22;
	}
	for (i = 0; i < 6U; i++) {
		uint8_t usage = report[i + 2U];

		if (usage >= 1U && usage <= 3U) {
			rollover = 1;
		} else if (usbhid64_key_transition(usage, 1, &key) == 0 &&
			!usage_present64(next, usage)) {
			next[count++] = usage;
		}
	}
	if (rollover) {
		for (i = 0; i < 6U; i++) {
			next[i] = state->keys[i];
		}
	}
	emitted = 0;
	pressed_modifiers = report[0] & (uint8_t) ~state->modifiers;
	released_modifiers = state->modifiers & (uint8_t) ~report[0];
	for (i = 0; i < 8U; i++) {
		if ((pressed_modifiers & (1U << i)) != 0) {
			status = emit_key64(fifo, modifier_keys[i], 1);
			if (status != 0) {
				return status;
			}
			state->modifiers |= (uint8_t) (1U << i);
			emitted++;
		}
	}
	for (i = 0; i < 6U; i++) {
		uint8_t usage = state->keys[i];

		if (usage != 0 && usage_present64(next, usage) == 0) {
			status = usbhid64_emit_transition(fifo, usage, 0);
			if (status != 0) {
				return status;
			}
			state->keys[i] = 0;
			emitted++;
		}
	}
	for (i = 0; i < 6U; i++) {
		uint8_t usage = next[i];

		if (usage != 0 && usage_present64(state->keys, usage) == 0) {
			status = usbhid64_emit_transition(fifo, usage, 1);
			if (status != 0) {
				return status;
			}
			for (j = 0; j < 6U; j++) {
				if (state->keys[j] == 0) {
					state->keys[j] = usage;
					break;
				}
			}
			emitted++;
		}
	}
	for (i = 0; i < 8U; i++) {
		if ((released_modifiers & (1U << i)) != 0) {
			status = emit_key64(fifo, modifier_keys[i], 0);
			if (status != 0) {
				return status;
			}
			state->modifiers &= (uint8_t) ~(1U << i);
			emitted++;
		}
	}
	return (int) emitted;
}

int usbhid64_process_report(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t report[8])
{
	if (fifo == NULL || state == NULL || report == NULL) {
		return -1;
	}
	return usbhid64_process_report_n(fifo, state, report, 8U);
}

int usbhid64_decode_mouse_report(const uint8_t *report, size_t length,
	struct USBHID64_MOUSE_REPORT *decoded)
{
	if (report == NULL || decoded == NULL || length < 3U) {
		return -1;
	}
	decoded->buttons = report[0] & 0x07U;
	decoded->dx = (int32_t) (int8_t) report[1];
	/* HID boot mouse의 Y축은 화면 좌표와 같이 아래쪽이 양수다. PS/2
	   mouse64_decode()에서 하던 부호 반전은 USB report에는 적용하지 않는다. */
	decoded->dy = (int32_t) (int8_t) report[2];
	decoded->wheel = length >= 4U ? (int32_t) (int8_t) report[3] : 0;
	return 0;
}
