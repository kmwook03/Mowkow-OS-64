#ifndef MOWKOW64_USBHID64_H
#define MOWKOW64_USBHID64_H

#include <stdint.h>

struct FIFO64;

struct USBHID64_STATE {
	uint8_t modifiers;
	uint8_t keys[6];
};

int usbhid64_key_transition(uint8_t usage, int pressed, uint16_t *key);
int usbhid64_emit_transition(struct FIFO64 *fifo, uint8_t usage,
	int pressed);
void usbhid64_state_init(struct USBHID64_STATE *state);
int usbhid64_process_report(struct FIFO64 *fifo,
	struct USBHID64_STATE *state, const uint8_t report[8]);

#endif
