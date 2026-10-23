#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <arch/platform64.h>
#include <asmfunc64.h>
#include <fifo64.h>
#include <keyboard64.h>
#include <mouse64.h>
#include <mtask64.h>
#include <timer64.h>
#include <usbhid64.h>

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "Input check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

static uint8_t controller_status64;
static uint8_t controller_data64;

uint64_t platform_irq_save64(void) { return 0; }
void platform_irq_restore64(uint64_t state) { (void) state; }
int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) task;
	(void) level;
	(void) priority;
	return 0;
}
uint8_t io_in8(uint16_t port)
{
	return port == 0x64 ? controller_status64 : controller_data64;
}
void io_out8(uint16_t port, uint8_t data) { (void) port; (void) data; }
uint64_t poll_deadline64(uint32_t milliseconds) { return milliseconds; }
int poll_deadline_expired64(uint64_t deadline) { (void) deadline; return 0; }

static int pop64(struct FIFO64 *fifo, uint16_t key)
{
	struct EVENT64 event;

	CHECK64(fifo64_get(fifo, &event) == 0);
	CHECK64(event.type == EVENT64_KEYBOARD && event.data == key);
	return 0;
}

static int modifiers64(void)
{
	static const uint16_t pairs[][4] = {
		{ 0x2a, 0x36, 0xaa, 0xb6 },
		{ 0x1d, KEY64_RCTRL, 0x9d, KEY64_RCTRL | 0x80 },
		{ 0x38, KEY64_RALT, 0xb8, KEY64_RALT | 0x80 },
	};
	int (*const states[])(void) = { keyboard64_shift, keyboard64_ctrl, keyboard64_alt };

	for (size_t i = 0; i < 3; i++) {
		CHECK64(keyboard64_track_modifier(pairs[i][0]) == 1);
		CHECK64(keyboard64_track_modifier(pairs[i][1]) == 1);
		CHECK64(states[i]() == 1);
		CHECK64(keyboard64_track_modifier(pairs[i][2]) == 1 && states[i]() == 1);
		CHECK64(keyboard64_track_modifier(pairs[i][3]) == 1 && states[i]() == 0);
	}
	return 0;
}

#ifndef INPUT64_AARCH64
static int ps264(void)
{
	static const uint8_t pause[] = { 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5 };
	static const uint8_t printscreen[] = { 0xe0, 0x2a, 0xe0, 0x37, 0xe0, 0xb7, 0xe0, 0xaa };
	struct EVENT64 events[32], event;
	struct FIFO64 fifo;
	struct MOUSE_DEC64 mouse = { 0 };
	uint16_t key = 0xaaaa;

	CHECK64(fifo64_init(&fifo, 32, events, NULL) == 0);
	CHECK64(init_keyboard64(&fifo) == 0);
	CHECK64(init_mouse64(&fifo, &mouse) == 0);
	CHECK64(mouse64_decode(NULL, 0) == 0);
	CHECK64(keyboard64_decode(0x1e, NULL) == 0);
	for (size_t prefix = 0; prefix <= sizeof(pause); prefix++) {
		keyboard64_decode_reset();
		for (size_t i = 0; i < prefix; i++) {
			CHECK64(keyboard64_decode(pause[i], &key) == 0);
		}
		CHECK64(keyboard64_decode(0x1e, &key) == 1 && key == 0x1e);
	}
	CHECK64(keyboard64_decode(0xe0, &key) == 0);
	CHECK64(keyboard64_decode(0x1e, &key) == 0);
	CHECK64(keyboard64_decode(0x1e, &key) == 1 && key == 0x1e);
	CHECK64(keyboard64_decode(0xe0, &key) == 0);
	CHECK64(keyboard64_decode(0x48, NULL) == 0);
	CHECK64(keyboard64_decode(0x48, &key) == 1 && key == KEY64_UP);
	CHECK64(keyboard64_decode(0xe0, &key) == 0);
	CHECK64(keyboard64_decode(0xc8, &key) == 1 && key == (KEY64_UP | 0x80));
	for (size_t i = 0; i < sizeof(printscreen); i++) {
		int status = keyboard64_decode(printscreen[i], &key);

		CHECK64(status == (i == 3 || i == 5));
		if (status) {
			CHECK64(key == (KEY64_EXT | printscreen[i]));
			CHECK64(keyboard64_track_modifier(key) == 0);
		}
	}
	CHECK64(keyboard64_shift() == 0);
	for (size_t i = 0; i < 256; i++) {
		keyboard64_decode_reset();
		(void) keyboard64_decode(0xe0, &key);
		(void) keyboard64_decode((uint8_t) i, &key);
		keyboard64_decode_reset();
	}
	CHECK64(keyboard64_decode(0xe0, &key) == 0);
	controller_status64 = 0x80;
	controller_data64 = 0x48;
	inthandler21_64();
	CHECK64(fifo64_status(&fifo) == 0);
	controller_status64 = 0;
	controller_data64 = 0x1e;
	inthandler21_64();
	CHECK64(pop64(&fifo, 0x1e) == 0);
	CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 0);
	CHECK64(mouse64_decode(&mouse, 0xfa) == 0 && mouse.phase == 1);
	CHECK64(mouse64_decode(&mouse, 0x09) == 0 && mouse.phase == 2);
	CHECK64(mouse64_decode(&mouse, 5) == 0 && mouse.phase == 3);
	CHECK64(mouse.x == 0 && mouse.y == 0 && mouse.btn == 0);
	CHECK64(mouse64_decode(&mouse, 6) == 1);
	CHECK64(mouse.x == 5 && mouse.y == -6 && mouse.btn == 1);
	CHECK64(mouse64_decode(&mouse, 0xc8) == 0);
	CHECK64(mouse64_decode(&mouse, 0x08) == 0);
	CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 1);
	CHECK64(mouse.x == 5 && mouse.y == -6 && mouse.btn == 1);
	CHECK64(mouse64_decode(&mouse, 0x38) == 0);
	CHECK64(mouse64_decode(&mouse, 0xff) == 0);
	CHECK64(mouse64_decode(&mouse, 0xfe) == 1 && mouse.x == -1 && mouse.y == 2);
	CHECK64(mouse64_decode(&mouse, 0x08) == 0);
	controller_status64 = 0x40;
	controller_data64 = 1;
	inthandler2c_64();
	CHECK64(fifo64_get(&fifo, &event) == 0 && event.data == MOUSE64_DATA_ERROR);
	mouse64_decode_error(&mouse);
	CHECK64(mouse.phase == 1 && mouse.discard == 1);
	/* The damaged packet's Y byte looks like a header but must be discarded. */
	CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 1);
	CHECK64(mouse64_decode(&mouse, 0x08) == 0);
	CHECK64(mouse64_decode(&mouse, 1) == 0);
	CHECK64(mouse64_decode(&mouse, 2) == 1 && mouse.x == 1 && mouse.y == -2);
	mouse.phase = -1;
	CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 2);
	for (int phase = 1; phase <= 3; phase++) {
		mouse.phase = phase;
		mouse64_decode_error(&mouse);
		for (int remaining = 0; remaining < 3 - phase; remaining++) {
			CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 1);
		}
		CHECK64(mouse64_decode(&mouse, 0x08) == 0 && mouse.phase == 2);
	}
	controller_status64 = 0;
	return 0;
}
#endif

static int reports64(void)
{
	static const uint16_t modifier_keys[] = {
		0x1d, 0x2a, 0x38, KEY64_EXT | 0x5b,
		KEY64_RCTRL, 0x36, KEY64_RALT, KEY64_EXT | 0x5c,
	};
	struct EVENT64 buffer[32];
	struct FIFO64 fifo;
	struct USBHID64_STATE state, saved;
	uint8_t report[8] = { 0 };

	CHECK64(fifo64_init(&fifo, 32, buffer, NULL) == 0);
	usbhid64_state_init(&state);
	report[2] = report[3] = report[4] = 4;
	report[5] = 0xff;
	report[1] = 0xff; /* OEM byte is not part of the key array. */
	CHECK64(usbhid64_process_report_n(&fifo, &state, report, 8) == 1);
	CHECK64(pop64(&fifo, 0x1e) == 0);
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 0);
	for (unsigned int error = 1; error <= 3; error++) {
		memset(report + 2, (int) error, 6);
		report[0] = 2;
		CHECK64(usbhid64_process_report_n(&fifo, &state, report, 8) == (error == 1));
		if (error == 1) {
			CHECK64(pop64(&fifo, 0x2a) == 0);
		}
		CHECK64(state.keys[0] == 4 && state.modifiers == 2);
	}
	memset(report, 0, sizeof(report));
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 2);
	CHECK64(pop64(&fifo, 0x9e) == 0 && pop64(&fifo, 0xaa) == 0);
	report[0] = 0xff;
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 8);
	for (size_t i = 0; i < 8; i++) {
		CHECK64(pop64(&fifo, modifier_keys[i]) == 0);
	}
	report[0] = 0;
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 8);
	for (size_t i = 0; i < 8; i++) {
		CHECK64(pop64(&fifo, modifier_keys[i] | 0x80) == 0);
	}
	saved = state;
	for (size_t length = 0; length < 8; length++) {
		CHECK64(usbhid64_process_report_n(&fifo, &state, report, length) == -22);
		CHECK64(memcmp(&state, &saved, sizeof(state)) == 0 && fifo64_status(&fifo) == 0);
	}
	/* Unsupported usages never disable the device; supported ones emit once. */
	for (unsigned int usage = 4; usage <= 255; usage++) {
		uint16_t key;
		int supported = usbhid64_key_transition((uint8_t) usage, 1, &key) == 0;

		report[2] = (uint8_t) usage;
		CHECK64(usbhid64_process_report(&fifo, &state, report) == supported);
		if (supported) {
			CHECK64(pop64(&fifo, key) == 0);
		}
		report[2] = 0;
		CHECK64(usbhid64_process_report(&fifo, &state, report) == supported);
		if (supported) {
			CHECK64(pop64(&fifo, key | 0x80) == 0);
		}
	}
	/* One-slot FIFO forces failure after each accepted transition. Retries
	 * must match the normal modifier/key ordering without repeated events.
	 */
	CHECK64(fifo64_init(&fifo, 1, buffer, NULL) == 0);
	report[0] = 3;
	report[2] = 4;
	report[3] = 5;
	for (size_t i = 0; i < 4; i++) {
		static const uint16_t expected[] = { 0x1d, 0x2a, 0x1e, 0x30 };
		int status = usbhid64_process_report(&fifo, &state, report);

		CHECK64(status == (i < 3 ? FIFO64_ERR_FULL : 1));
		CHECK64(pop64(&fifo, expected[i]) == 0);
	}
	CHECK64(fifo64_overruns(&fifo) == 3 && (fifo.flags & FIFO64_FLAGS_OVERRUN));
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 0);
	memset(report, 0, sizeof(report));
	for (size_t i = 0; i < 4; i++) {
		static const uint16_t expected[] = { 0x9e, 0xb0, 0x9d, 0xaa };
		int status = usbhid64_process_report(&fifo, &state, report);

		CHECK64(status == (i < 3 ? FIFO64_ERR_FULL : 1));
		CHECK64(pop64(&fifo, expected[i]) == 0);
	}
	CHECK64(usbhid64_process_report(&fifo, &state, report) == 0);
	return 0;
}

static const uint8_t keyboard_config64[] = {
	9, 2, 34, 0, 1, 1, 0, 0x80, 50,
	9, 4, 0, 0, 1, 3, 1, 1, 0,
	9, 0x21, 0x11, 1, 0, 1, 0x22, 63, 0,
	7, 5, 0x81, 3, 8, 0, 10,
};

static int descriptors64(void)
{
	struct USBHID64_CONFIG config = { 0 }, saved;
	uint8_t data[128];
	uint32_t id = 0xdeadbeef;

	for (uint32_t address = 0; address < 256; address++) {
		int valid = address >= 0x81 && address <= 0x8f;

		CHECK64(usbhid64_endpoint_id(address, 8, 1, &id) == (valid ? 0 : -22));
		if (valid) {
			CHECK64(id >= 3 && id <= 31 && id == (address & 15) * 2 + 1);
		}
	}
	CHECK64(usbhid64_endpoint_id(UINT32_MAX, 8, 1, &id) == -22);
	CHECK64(usbhid64_endpoint_id(0x81, 2, 1, &id) == -22);
	CHECK64(usbhid64_endpoint_id(0x81, 65, 1, &id) == -22);
	CHECK64(usbhid64_endpoint_id(0x81, UINT32_MAX, 1, &id) == -22);
	CHECK64(usbhid64_endpoint_id(0x81, 8, 0, &id) == -22);
	CHECK64(usbhid64_endpoint_id(0x81, 8, 256, &id) == -22);
	CHECK64(usbhid64_parse_boot_config(keyboard_config64, 34, 1, &config) == 0);
	CHECK64(config.configuration_value == 1 && config.interface_number == 0);
	CHECK64(config.endpoint_address == 0x81 && config.endpoint_max_packet == 8);
	CHECK64(config.endpoint_interval == 10);
	saved = config;
	CHECK64(usbhid64_parse_boot_config(keyboard_config64, 34, 2, &config) == -19);
	CHECK64(memcmp(&config, &saved, sizeof(config)) == 0);
	for (unsigned int test = 0; test < 13; test++) {
		memcpy(data, keyboard_config64, 34);
		switch (test) {
		case 0: data[0] = 8; break;
		case 1: data[2] = 33; break;
		case 2: data[9] = 2; break;
		case 3: data[18] = 6; break;
		case 4: data[23] = 2; break;
		case 5: data[27] = 6; break;
		case 6: data[29] = 0x80; break;
		case 7: data[29] = 0x91; break;
		case 8: data[32] = 8; break;
		case 9: data[31] = 7; break;
		case 10: data[33] = 0; break;
		case 11: data[13] = 2; break;
		case 12: data[25] = data[26] = 0; break;
		}
		CHECK64(usbhid64_parse_boot_config(data, 34, 1, &config) == -84);
		CHECK64(memcmp(&config, &saved, sizeof(config)) == 0);
	}
	/* The old parser stopped at the first endpoint and missed bad suffixes. */
	memcpy(data, keyboard_config64, 34);
	data[2] = 43;
	data[34] = 2; data[35] = 4;
	memcpy(data + 36, keyboard_config64 + 27, 7);
	CHECK64(usbhid64_parse_boot_config(data, 43, 1, &config) == -84);
	memcpy(data, keyboard_config64, 34);
	memcpy(data + 34, keyboard_config64 + 27, 7);
	data[2] = 41; data[13] = 2;
	CHECK64(usbhid64_parse_boot_config(data, 41, 1, &config) == -84);
	data[36] = 0x82;
	CHECK64(usbhid64_parse_boot_config(data, 41, 1, &config) == 0);
	CHECK64(config.endpoint_address == 0x81);
	memcpy(data, keyboard_config64, 34);
	data[12] = 1;
	CHECK64(usbhid64_parse_boot_config(data, 34, 1, &config) == -19);
	/* Composite keyboard + mouse: selection stays paired with its interface. */
	memcpy(data, keyboard_config64, 34);
	memcpy(data + 34, keyboard_config64 + 9, 25);
	data[2] = 59; data[4] = 2; data[36] = 1; data[41] = 2;
	data[54] = 0x82; data[56] = 3;
	CHECK64(usbhid64_parse_boot_config(data, 59, 1, &config) == 0);
	CHECK64(config.interface_number == 0 && config.endpoint_address == 0x81);
	CHECK64(usbhid64_parse_boot_config(data, 59, 2, &config) == 0);
	CHECK64(config.interface_number == 1 && config.endpoint_max_packet == 3);
	return 0;
}

static int descriptor_boundaries64(void)
{
	static const uint8_t types[][2] = {
		{ 1, 18 }, { 2, 9 }, { 4, 9 }, { 5, 7 }, { 6, 10 }, { 7, 9 },
		{ 8, 8 }, { 9, 3 }, { 10, 4 }, { 11, 8 }, { 15, 5 }, { 16, 3 },
		{ 48, 6 }, { 49, 8 }
	};
	long page = sysconf(_SC_PAGESIZE);
	uint8_t *mapping = mmap(NULL, (size_t) page * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *end;
	uint8_t data[128];
	struct USBHID64_CONFIG result, saved;

	CHECK64(mapping != MAP_FAILED && page > 128);
	end = mapping + page;
	CHECK64(mprotect(end, (size_t) page, PROT_NONE) == 0);
	memset(&saved, 0xa5, sizeof(saved));
	for (size_t type = 0; type < sizeof(types) / sizeof(types[0]); type++) {
		for (size_t size = 2; size < types[type][1]; size++) {
			size_t total = sizeof(keyboard_config64) + size;

			memcpy(data, keyboard_config64, sizeof(keyboard_config64));
			memset(data + sizeof(keyboard_config64), 0, size);
			data[2] = (uint8_t) total;
			data[34] = (uint8_t) size;
			data[35] = types[type][0];
			memcpy(end - total, data, total);
			result = saved;
			CHECK64(usbhid64_parse_boot_config(end - total, total, 1, &result) == -84);
			CHECK64(memcmp(&result, &saved, sizeof(result)) == 0);
		}
	}
	/* Unknown class/vendor descriptors are still bounded and safely skipped. */
	memcpy(data, keyboard_config64, 34);
	data[2] = 36; data[34] = 2; data[35] = 0x99;
	CHECK64(usbhid64_parse_boot_config(data, 36, 1, &result) == 0);
	data[35] = 0;
	CHECK64(usbhid64_parse_boot_config(data, 36, 1, &result) == -84);
	memcpy(data, keyboard_config64, 34);
	data[4] = 2;
	CHECK64(usbhid64_parse_boot_config(data, 34, 1, &result) == -84);
	/* Duplicate alternate zero and cross-interface endpoint aliases are invalid. */
	memcpy(data, keyboard_config64, 34);
	memcpy(data + 34, keyboard_config64 + 9, 25);
	data[2] = 59; data[54] = 0x82;
	CHECK64(usbhid64_parse_boot_config(data, 59, 1, &result) == -84);
	data[4] = 2; data[36] = 1; data[54] = 0x81;
	CHECK64(usbhid64_parse_boot_config(data, 59, 1, &result) == -84);
	/* HID class-specific lengths include every declared subordinate entry. */
	memcpy(data, keyboard_config64, 27);
	memcpy(data + 30, keyboard_config64 + 27, 7);
	data[2] = 37; data[18] = 12; data[23] = 2;
	data[27] = 0x23; data[28] = 1; data[29] = 0;
	CHECK64(usbhid64_parse_boot_config(data, 37, 1, &result) == 0);
	data[27] = 0x22;
	CHECK64(usbhid64_parse_boot_config(data, 37, 1, &result) == -84);
	data[27] = 0x23; data[28] = 0;
	CHECK64(usbhid64_parse_boot_config(data, 37, 1, &result) == -84);
	data[28] = 1; data[23] = 3;
	CHECK64(usbhid64_parse_boot_config(data, 37, 1, &result) == -84);
	CHECK64(munmap(mapping, (size_t) page * 2) == 0);
	return 0;
}

static int guards64(void)
{
	long page = sysconf(_SC_PAGESIZE);
	uint8_t *mapping, *end;
	struct USBHID64_CONFIG config;
	struct USBHID64_STATE state, saved;
	struct USBHID64_MOUSE_REPORT mouse;
	struct FIFO64 fifo;
	struct EVENT64 events[32];
	uint32_t random = 1;

	CHECK64(page > 0);
	mapping = mmap(NULL, (size_t) page * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK64(mapping != MAP_FAILED);
	end = mapping + page;
	CHECK64(mprotect(end, (size_t) page, PROT_NONE) == 0);
	CHECK64(fifo64_init(&fifo, 32, events, NULL) == 0);
	usbhid64_state_init(&state);
	saved = state;
	for (size_t length = 0; length < 8; length++) {
		memset(end - length, 0, length);
		CHECK64(usbhid64_process_report_n(&fifo, &state, end - length, length) == -22);
		CHECK64(memcmp(&state, &saved, sizeof(state)) == 0);
	}
	for (size_t length = 0; length < sizeof(keyboard_config64); length++) {
		memcpy(end - length, keyboard_config64, length);
		CHECK64(usbhid64_parse_boot_config(end - length, length, 1, &config) != 0);
	}
	for (unsigned int iteration = 0; iteration < 10000; iteration++) {
		uint8_t *data = end - sizeof(keyboard_config64);

		memcpy(data, keyboard_config64, sizeof(keyboard_config64));
		random = random * 1664525U + 1013904223U;
		data[(random >> 8) % sizeof(keyboard_config64)] = (uint8_t) random;
		(void) usbhid64_parse_boot_config(data, sizeof(keyboard_config64), 1, &config);
	}
	for (size_t length = 0; length < 3; length++) {
		CHECK64(usbhid64_decode_mouse_report(end - length, length, &mouse) == -1);
	}
	end[-3] = 7; end[-2] = 0x80; end[-1] = 0x7f;
	CHECK64(usbhid64_decode_mouse_report(end - 3, 3, &mouse) == 0);
	CHECK64(mouse.buttons == 7 && mouse.dx == -128 && mouse.dy == 127 && mouse.wheel == 0);
	CHECK64(munmap(mapping, (size_t) page * 2) == 0);
	return 0;
}

int main(void)
{
	CHECK64(modifiers64() == 0);
#ifndef INPUT64_AARCH64
	CHECK64(ps264() == 0);
#endif
	CHECK64(reports64() == 0);
	CHECK64(descriptors64() == 0);
	CHECK64(guards64() == 0);
	CHECK64(descriptor_boundaries64() == 0);
	puts("PS/2 and USB HID packets, reports, descriptors and endpoint bounds: PASS");
	return 0;
}
