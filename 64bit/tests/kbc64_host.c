#include <fifo64.h>
#include <keyboard64.h>
#include <mouse64.h>
#include <stddef.h>
#include <stdint.h>

#define KBC_STATUS_PORT 0x0064
#define KBC_SEND_NOTREADY 0x02
#define KBC_TIMEOUT_ERROR 0x40
#define KBC_PARITY_ERROR  0x80

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint8_t statuses[8];
static uint32_t status_count;
static uint32_t status_index;
static uint32_t input_calls;
static uint32_t output_calls;
static uint32_t deadline_calls;
static uint32_t expiry_checks;
static uint32_t expire_after;

static void set_statuses(const uint8_t *values, uint32_t count,
	uint32_t expiry)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		statuses[i] = values[i];
	}
	status_count = count;
	status_index = 0;
	input_calls = 0;
	output_calls = 0;
	deadline_calls = 0;
	expiry_checks = 0;
	expire_after = expiry;
}

uint8_t io_in8(uint16_t port)
{
	uint8_t status;

	input_calls++;
	if (port != KBC_STATUS_PORT || status_count == 0) {
		return 0;
	}
	status = statuses[status_index];
	if (status_index + 1 < status_count) {
		status_index++;
	}
	return status;
}

void io_out8(uint16_t port, uint8_t value)
{
	(void) port;
	(void) value;
	output_calls++;
}

uint64_t poll_deadline64(uint32_t milliseconds)
{
	CHECK64(milliseconds == 200U);
	deadline_calls++;
	return 1;
}

int poll_deadline_expired64(uint64_t deadline)
{
	CHECK64(deadline == 1);
	expiry_checks++;
	return expiry_checks >= expire_after;
}

int fifo64_put(struct FIFO64 *fifo, struct EVENT64 data)
{
	(void) fifo;
	(void) data;
	return 0;
}

static int test_keyboard_errors_and_timeout(void)
{
	struct FIFO64 fifo;
	const uint8_t timeout_error[] = { KBC_TIMEOUT_ERROR };
	const uint8_t parity_error[] = { KBC_PARITY_ERROR };
	const uint8_t busy[] = { KBC_SEND_NOTREADY };

	set_statuses(timeout_error, 1, 10);
	CHECK64(init_keyboard64(&fifo) == -1);
	CHECK64(input_calls == 1 && output_calls == 0 && deadline_calls == 1);

	set_statuses(parity_error, 1, 10);
	CHECK64(init_keyboard64(&fifo) == -1);
	CHECK64(input_calls == 1 && output_calls == 0 && deadline_calls == 1);

	set_statuses(busy, 1, 3);
	CHECK64(init_keyboard64(&fifo) == -1);
	CHECK64(input_calls == 3 && expiry_checks == 3);
	CHECK64(output_calls == 0 && deadline_calls == 1);
	return 0;
}

static int test_keyboard_and_mouse_ready(void)
{
	struct FIFO64 fifo;
	struct MOUSE_DEC64 mouse;
	const uint8_t ready[] = { 0, 0 };

	set_statuses(ready, 2, 10);
	CHECK64(init_keyboard64(&fifo) == 0);
	CHECK64(input_calls == 2 && output_calls == 2 && deadline_calls == 2);

	set_statuses(ready, 2, 10);
	CHECK64(init_mouse64(&fifo, &mouse) == 0);
	CHECK64(input_calls == 2 && output_calls == 2 && deadline_calls == 2);
	CHECK64(mouse.phase == 0 && mouse.x == 0 && mouse.y == 0 && mouse.btn == 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_keyboard_errors_and_timeout();
	if (status != 0) {
		return status;
	}
	return test_keyboard_and_mouse_ready();
}
