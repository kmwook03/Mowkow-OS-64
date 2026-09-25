#ifndef MOWKOW64_USBHID64_H
#define MOWKOW64_USBHID64_H

#include <stddef.h>
#include <stdint.h>

struct FIFO64;

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
int usbhid64_process_report(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t report[8]);
/* HID boot mouse의 3바이트 기본 report와 선택적인 4번째 wheel 바이트를
   GUI가 바로 소비할 signed 이동량으로 변환한다. */
int usbhid64_decode_mouse_report(const uint8_t *report, size_t length,
	struct USBHID64_MOUSE_REPORT *decoded);

#endif
