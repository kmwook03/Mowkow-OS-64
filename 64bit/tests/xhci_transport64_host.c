/* CPU/cache hooks model completion; the controller and ring code is unchanged. */
#define _GNU_SOURCE
#define XHCI64_HOST_TEST
#include "poll_test64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src64/arch/aarch64/xhci64.c"

static uint8_t *registers64;
static uint64_t clock64, frequency64 = 1000;
static unsigned int complete64;
static uint32_t completion_code64 = 1;
static unsigned int copies64;
static uint32_t port_model_id64, port_model_value64, port_last_write64;
static int port_stuck64;
static uint32_t disable_after64;
static uint32_t allocated_slot64 = 1;
static uintptr_t dma_physical64 = 0x100000U;
static uintptr_t cache_line64 = 64;

enum COMPLETION64 { NONE64, CONTROL64, INTERRUPT64, CONFIGURE64, ADDRESS64, DISABLE64, ENABLE64 };

uintptr_t xhci_physical64(uintptr_t address)
{
	uintptr_t first = (uintptr_t) xhci_states;

	return address >= first && address - first < sizeof(xhci_states) ?
		dma_physical64 + address - first : UINTPTR_MAX;
}
uintptr_t xhci_cache_size64(void) { return cache_line64; }
uint64_t xhci_counter64(void) { return clock64++; }
uint64_t xhci_frequency64(void) { return frequency64; }
void xhci_relax64(void) { clock64 += 100; }
void xhci_clean_cache64(const void *address, size_t bytes)
{
	assert(address != NULL && bytes != 0);
}
void xhci_invalidate_cache64(const void *address, size_t bytes)
{
	assert(address != NULL && bytes != 0);
	copies64++;
}

static uintptr_t capability64(void)
{
	return (uintptr_t) registers64 + xhci_controller_offset;
}

static void event64(uint64_t pointer, uint32_t type, uint32_t endpoint,
	uint32_t status)
{
	struct XHCI64_TRB *event = &xhci0_event_ring[xhci0_event_dequeue];

	event->parameter = pointer;
	event->status = status;
	event->control = ((xhci_states[xhci_controller_id].active_slot ?
		xhci_states[xhci_controller_id].active_slot : 1U) << 24) |
		(endpoint << 16) | (type << 10) | xhci0_event_cycle;
}

void xhci_barrier64(void)
{
	uint32_t completion = complete64;
	uint32_t last;

	if (port_model_id64 != 0) {
		uint32_t *portsc = (uint32_t *) (capability64() + 0x40 + XHCI_PORTSC_BASE +
			(port_model_id64 - 1U) * XHCI_PORT_STRIDE);
		uint32_t command = *portsc;

		port_last_write64 = command;
		if ((command & XHCI_PORTSC_PR) != 0) {
			port_model_value64 |= XHCI_PORTSC_PRC | XHCI_PORTSC_PED;
		} else if (!port_stuck64 && (command & XHCI_PORTSC_PED)) {
			port_model_value64 &= ~XHCI_PORTSC_PED;
		}
		*portsc = port_model_value64;
	}
	if (completion == DISABLE64 && (xhci0_command_enqueue <= disable_after64 ||
			(xhci0_command_ring[xhci0_command_enqueue - 1].control & XHCI_TRB_TYPE_MASK64) !=
			XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND)) { return; }
	complete64 = NONE64; /* Event-ring acknowledgement must not complete twice. */
	if (completion == CONTROL64) {
		last = xhci0_ep0_enqueue - 1;
		assert(xhci0_ep0_ring[last].control & XHCI_TRB_IOC);
		for (uint32_t byte = 0; byte < XHCI64_CONTROL_BYTES; byte++) {
			xhci0_descriptor_buffer[byte] = (uint8_t) (byte ^ 0x5a);
		}
		event64(rp1_dma_address64(&xhci0_ep0_ring[last]),
			XHCI_TRB_TYPE_TRANSFER_EVENT, 1, completion_code64 << 24);
	} else if (completion == INTERRUPT64) {
		last = xhci0_interrupt_enqueue == 0 ? XHCI_RING_TRBS - 2 :
			xhci0_interrupt_enqueue - 1;
		for (uint32_t byte = 0; byte < XHCI64_INTERRUPT_BYTES; byte++) {
			xhci0_interrupt_report[byte] = (uint8_t) (byte + 1);
		}
		event64(rp1_dma_address64(&xhci0_interrupt_ring[last]),
			XHCI_TRB_TYPE_TRANSFER_EVENT, 3, completion_code64 << 24);
	} else if (completion == ENABLE64) {
		last = xhci0_command_enqueue - 1;
		event64(rp1_dma_address64(&xhci0_command_ring[last]),
			XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, completion_code64 << 24);
		xhci0_event_ring[xhci0_event_dequeue].control &= 0x00ffffffU;
		xhci0_event_ring[xhci0_event_dequeue].control |= allocated_slot64 << 24;
	} else if (completion == DISABLE64) {
		last = xhci0_command_enqueue - 1;
		event64(rp1_dma_address64(&xhci0_command_ring[last]),
			XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, completion_code64 << 24);
			xhci0_event_ring[xhci0_event_dequeue].control &= 0x00ffffffU;
		xhci0_event_ring[xhci0_event_dequeue].control |=
			xhci0_command_ring[last].control & 0xff000000U;
	} else if (completion == ADDRESS64) {
		xhci0_device_context[3] = (2U << 27) | 1U;
		last = xhci0_command_enqueue - 1;
		event64(rp1_dma_address64(&xhci0_command_ring[last]),
			XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, completion_code64 << 24);
	} else if (completion == CONFIGURE64) {
		uint32_t words = (*(uint32_t *) (capability64() + XHCI_HCCPARAMS1) & 4) ? 16 : 8;
		uint32_t endpoint = 31;

		while (!(xhci0_input_context[1] & (1U << endpoint))) { endpoint--; }
		xhci0_device_context[endpoint * words] = 1;
		last = xhci0_command_enqueue - 1;
		event64(rp1_dma_address64(&xhci0_command_ring[last]),
			XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, completion_code64 << 24);
	}
}

static void controllers64(void)
{
	struct XHCI64_START_RESULT start;
	struct XHCI64_ADDRESS_RESULT address;

	for (uint32_t controller = 0; controller < 2; controller++) {
		uintptr_t capability = (uintptr_t) registers64 +
			(controller == 0 ? RP1_USB0_BASE : RP1_USB1_BASE);

		*(uint32_t *) capability = 0x01100040;
		*(uint32_t *) (capability + XHCI_HCSPARAMS1) = (3U << 24) | 64;
		*(uint32_t *) (capability + XHCI_HCCPARAMS1) = controller == 0 ? 0 : 4;
		*(uint32_t *) (capability + XHCI_RTSOFF) = 0x1000;
		*(uint32_t *) (capability + XHCI_DBOFF) = 0x2000;
		assert(xhci64_start_controller((uintptr_t) registers64, controller, &start) == 0);
		assert(start.controller_id == controller);
		assert(xhci0_event_cycle == 1 && xhci0_interrupt_cycle == 1);
		assert(xhci0_command_ring[255].parameter == rp1_dma_address64(xhci0_command_ring));
		assert(xhci0_erst.address == rp1_dma_address64(xhci0_event_ring));
		complete64 = ADDRESS64;
		assert(xhci64_address_device((uintptr_t) registers64, 1, 3, 1, &address) == 0);
		assert(address.context_size == (controller == 0 ? 32U : 64U));
	}
}

static void control64(void)
{
	struct XHCI64_TRANSFER_RESULT result;
	uint8_t data[XHCI64_CONTROL_BYTES + 2];
	uint64_t setup = 0x0008000001000680ULL;

	assert(xhci64_select_controller(0) == 0);
	memset(data, 0xa5, sizeof(data));
	complete64 = CONTROL64;
	assert(xhci_control_read64((uintptr_t) registers64, 1, setup, data + 1, 8, &result) == 0);
	assert(xhci0_ep0_ring[0].parameter == setup && xhci0_ep0_ring[1].status == 8);
	assert(xhci0_ep0_enqueue == 3 && data[0] == 0xa5 && data[9] == 0xa5);
	for (unsigned int byte = 0; byte < 8; byte++) { assert(data[byte + 1] == (byte ^ 0x5a)); }
	assert(result.event_status == 0x01000000 && result.trb_pointer_low ==
		(uint32_t) rp1_dma_address64(&xhci0_ep0_ring[2]));
	complete64 = CONTROL64;
	assert(xhci_control_write64((uintptr_t) registers64, 1, 0x10900) == 0);
	assert(xhci0_ep0_ring[3].parameter == 0x10900 && xhci0_ep0_enqueue == 5);
	memset(data, 0xa5, sizeof(data));
	completion_code64 = 2;
	complete64 = CONTROL64;
	assert(xhci_control_read64((uintptr_t) registers64, 1, setup, data, 8, &result) == -5);
	assert(data[0] == 0xa5 && result.event_status == 0x02000000);
	completion_code64 = 1;
	clock64 = UINT64_MAX - 20;
	assert(xhci_control_read64((uintptr_t) registers64, 1, setup, data, 8, &result) == -110);
	assert(data[0] == 0xa5);
	assert(xhci_control_read64((uintptr_t) registers64, 0, setup, data, 8, &result) == -22);
	assert(xhci_control_read64((uintptr_t) registers64, 1, setup, data, 513, &result) == -22);
	assert(xhci64_select_controller(1) == 0 && xhci0_ep0_enqueue == 0);
}

static void configuration64(void)
{
	struct XHCI64_CONFIGURE_RESULT result;

	for (uint32_t controller = 0; controller < 2; controller++) {
		uint32_t words = controller == 0 ? 8 : 16;

		assert(xhci64_select_controller(controller) == 0);
		memset(&result, 0, sizeof(result));
		complete64 = CONFIGURE64;
		assert(xhci_configure_interrupt64((uintptr_t) registers64, 1, 3, 8, 9, &result) == 0);
		assert(result.endpoint_state == 1 && result.endpoint_id == 3 && result.interval == 9);
		assert(xhci0_input_context[4 * words] == (9U << 16));
		assert(xhci0_input_context[4 * words + 4] == (8U | (8U << 16)));
		assert(xhci0_interrupt_ring[255].parameter == rp1_dma_address64(xhci0_interrupt_ring));
		complete64 = CONFIGURE64;
		assert(xhci_configure_interrupt64((uintptr_t) registers64, 1, 31, 8, 15, &result) == 0);
		assert(xhci0_input_context[32 * words] == (15U << 16));
		assert(result.endpoint_id == 31 && result.endpoint_state == 1);
		complete64 = CONFIGURE64;
		assert(xhci_configure_interrupt64((uintptr_t) registers64, 1, 3, 8, 9, &result) == 0);
	}
}

static void interrupt64(void)
{
	struct XHCI64_TRANSFER_RESULT result = { 0 };
	uint8_t data[66];
	uint64_t pending[2];

	assert(xhci64_select_controller(0) == 0);
	memset(data, 0xa5, sizeof(data));
	complete64 = INTERRUPT64;
	assert(xhci_interrupt_read64((uintptr_t) registers64, 1, 3, 8, 10000,
		data + 1, &result) == 0);
	assert(data[0] == 0xa5 && data[9] == 0xa5);
	for (unsigned int byte = 1; byte <= 8; byte++) { assert(data[byte] == byte); }
	for (uint32_t controller = 0; controller < 2; controller++) {
		assert(xhci64_select_controller(controller) == 0);
		assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
		pending[controller] = xhci0_interrupt_pending_pointer;
		assert(pending[controller] != 0);
		assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == -22);
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == 0);
	}
	assert(pending[0] != pending[1]);
	for (uint32_t controller = 0; controller < 2; controller++) {
		assert(xhci64_select_controller(controller) == 0);
		assert(xhci0_interrupt_pending_pointer == pending[controller]);
		xhci0_interrupt_report[0] = (uint8_t) (controller + 0x40);
		event64(pending[controller], XHCI_TRB_TYPE_TRANSFER_EVENT, 3, (13U << 24) | 5);
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 7, data, 8) == -22);
		assert(xhci0_interrupt_pending_pointer == pending[controller]);
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data + 1, 8) == 3);
		assert(data[1] == controller + 0x40 && data[4] == 0 && data[8] == 0);
		assert(xhci0_interrupt_pending_pointer == 0);
	}
}

static void capabilities64(void)
{
	size_t page = (size_t) sysconf(_SC_PAGESIZE);
	size_t bytes = XHCI_REGISTER_BYTES64 + page;
	uint8_t *mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uintptr_t cap = (uintptr_t) mapping;
	uint32_t *hcc;
	uint32_t *ext;
	struct XHCI64_REGISTERS regs;

	assert(mapping != MAP_FAILED);
	assert(mprotect(mapping + XHCI_REGISTER_BYTES64, page, PROT_NONE) == 0);
	assert(xhci64_select_controller(0) == 0);
	memcpy(mapping, (const void *) capability64(), XHCI_REGISTER_BYTES64);
	hcc = (uint32_t *) (mapping + XHCI_HCCPARAMS1);
	ext = (uint32_t *) (mapping + XHCI_REGISTER_BYTES64 - 16);
	*hcc = ((XHCI_REGISTER_BYTES64 - 16) / 4) << 16;
	ext[0] = (2U << 24) | 2;
	ext[2] = 1U | (3U << 8);
	assert(port_protocol_major64(cap, 1) == 2);
	ext[2] |= 1U << 28; /* PSI would cross into the protected page. */
	assert(port_protocol_major64(cap, 1) == 0);
	*hcc = ((XHCI_REGISTER_BYTES64 - 4) / 4) << 16;
	ext = (uint32_t *) (mapping + XHCI_REGISTER_BYTES64 - 4);
	ext[0] = (2U << 24) | 2;
	assert(port_protocol_major64(cap, 1) == 0);
	ext[0] = 0xffU | (1U << 8); /* Next header is exactly outside the window. */
	assert(port_protocol_major64(cap, 1) == 0);
	*hcc = (XHCI_REGISTER_BYTES64 / 4) << 16;
	assert(port_protocol_major64(cap, 1) == 0);
	*hcc = 0xffff0000U;
	assert(port_protocol_major64(cap, 1) == 0);
	*hcc = 7U << 16; /* First header overlaps the fixed capability registers. */
	assert(port_protocol_major64(cap, 1) == 0);
	for (uint32_t nodes = 64; nodes <= 65; nodes++) {
		*hcc = (0x3000U / 4U) << 16;
		for (uint32_t node = 0; node < nodes; node++) {
			ext = (uint32_t *) (mapping + 0x3000U + node * 4U);
			ext[0] = 0xffU | (1U << 8);
		}
		ext[0] = (2U << 24) | 2;
		ext[2] = 1U | (3U << 8);
		assert(port_protocol_major64(cap, 1) == (nodes == 64 ? 2U : 0U));
	}
	ext = (uint32_t *) (mapping + 0x3000);
	ext[0] = (2U << 24) | (4U << 8) | 2U;
	ext[2] = 1U | (3U << 8);
	ext[4] = 0xffU | (255U << 8);
	/* The first match must not hide a malformed trailing chain. */
	for (uint32_t offset = 0x3010; offset < XHCI_REGISTER_BYTES64; offset += 1020) {
		*(uint32_t *) (mapping + offset) = 0xffU | (255U << 8);
	}
	assert(port_protocol_major64(cap, 1) == 0);
	ext[0] = (2U << 24) | (1U << 8) | 2U; /* Next overlaps the protocol body. */
	assert(port_protocol_major64(cap, 1) == 0);
	ext[0] = (2U << 24) | 2U;
	ext[2] = 0;
	assert(port_protocol_major64(cap, 1) == 0);
	ext[2] = 3U | (2U << 8); /* Port range exceeds the controller's root ports. */
	assert(port_protocol_major64(cap, 1) == 0);
	assert(register_layout64(UINTPTR_MAX - 3U, &regs) < 0);
	assert(controller_capability64(UINTPTR_MAX - RP1_USB1_BASE, RP1_USB1_BASE) == 0);
	assert(register_layout64(cap + 1, &regs) < 0);
	for (uint32_t offset = 0; offset < 2; offset++) {
		uint32_t *field = (uint32_t *) (mapping + (offset ? XHCI_DBOFF : XHCI_RTSOFF));
		uint32_t saved = *field;
		const uint32_t bad[] = { 0, 1, 0xfffffffcU, XHCI_REGISTER_BYTES64, 0x40 };

		for (unsigned int index = 0; index < sizeof(bad) / sizeof(bad[0]); index++) {
			*field = bad[index];
			assert(register_layout64(cap, &regs) < 0);
		}
		*field = saved;
	}
	assert(munmap(mapping, bytes) == 0);
}

static void rejected_state64(void)
{
	struct XHCI64_CONTROLLER_STATE before = xhci_states[0];
	struct XHCI64_COMMAND_RESULT command;
	struct XHCI64_TRANSFER_RESULT transfer;
	struct XHCI64_CONFIGURE_RESULT configured;
	uint8_t data[8];
	uint8_t registers_before[XHCI_REGISTER_BYTES64];

	memset(data, 0xa5, sizeof(data));
	memcpy(registers_before, registers64 + RP1_USB0_BASE, sizeof(registers_before));
	assert(!xhci_command_ready64() && !xhci_control_ready64(1, 3) && !xhci_interrupt_ready64());
	assert(xhci64_noop_command((uintptr_t) registers64, &command) == -1);
	assert(xhci_control_read64((uintptr_t) registers64, 1, 0, data, 8, &transfer) == -22);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == -22);
	assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == -22);
	assert(xhci_configure_interrupt64((uintptr_t) registers64, 1, 3, 8, 0, &configured) == -22);
	assert(memcmp(&before, &xhci_states[0], sizeof(before)) == 0);
	assert(memcmp(registers_before, registers64 + RP1_USB0_BASE, sizeof(registers_before)) == 0);
	for (size_t byte = 0; byte < sizeof(data); byte++) { assert(data[byte] == 0xa5); }
}

static void corrupted_metadata64(void)
{
	struct XHCI64_CONTROLLER_STATE good;
	uint32_t *fields[12];
	const uint32_t bad[] = { UINT32_MAX, 256, 2 };

	assert(xhci64_select_controller(0) == 0);
	good = xhci_states[0];
	xhci_states[0].started = 0;
	rejected_state64();
	xhci_states[0] = good;
	struct XHCI64_START_RESULT start;
	struct XHCI64_RESET_RESULT reset[2];
	assert(xhci64_start_controller(UINTPTR_MAX - 3U, 1, &start) < 0);
	assert(xhci64_reset_rp1(UINTPTR_MAX - 3U, reset) < 0);
	/* A foreign active base is rejected before even reading its fixed header. */
	assert(xhci_interrupt_arm64(4, 1, 3, 8) == -22);
	assert(xhci_controller_id == 0 && memcmp(&good, &xhci_states[0], sizeof(good)) == 0);
	fields[0] = &xhci0_command_enqueue;
	fields[1] = &xhci0_event_dequeue;
	fields[2] = &xhci0_ep0_enqueue;
	fields[3] = &xhci0_interrupt_enqueue;
	fields[4] = &xhci0_command_cycle;
	fields[5] = &xhci0_event_cycle;
	fields[6] = &xhci0_ep0_cycle;
	fields[7] = &xhci0_interrupt_cycle;
	fields[8] = &xhci_states[0].context_dwords;
	fields[9] = &xhci_states[0].started;
	fields[10] = &xhci_states[0].ep0_ready;
	fields[11] = &xhci_states[0].interrupt_ready;
	for (unsigned int field = 0; field < 12; field++) {
		for (unsigned int value = 0; value < 3; value++) {
			if (field < 4 && bad[value] == 2) { continue; }
			*fields[field] = bad[value];
			rejected_state64();
			xhci_states[0] = good;
		}
	}
	xhci_controller_id = 2;
	rejected_state64();
	assert(xhci64_select_controller(0) == 0);
	xhci_controller_offset = RP1_USB1_BASE;
	rejected_state64();
	assert(xhci64_select_controller(0) == 0);
	for (unsigned int ring = 0; ring < 3; ring++) {
		struct XHCI64_TRB *link = ring == 0 ? &xhci0_command_ring[255] :
			ring == 1 ? &xhci0_ep0_ring[255] : &xhci0_interrupt_ring[255];

		link->parameter++;
		rejected_state64();
		xhci_states[0] = good;
		link->control ^= XHCI_TRB_TOGGLE_CYCLE;
		rejected_state64();
		xhci_states[0] = good;
	}
	*(uint32_t *) (capability64() + XHCI_HCCPARAMS1) ^= 4;
	/* A changed CSZ must not reinterpret a previously initialized context. */
	struct XHCI64_CONFIGURE_RESULT configured;
	assert(xhci_configure_interrupt64((uintptr_t) registers64, 1, 3, 8, 0, &configured) == -22);
	assert(memcmp(&good, &xhci_states[0], sizeof(good)) == 0);
	*(uint32_t *) (capability64() + XHCI_HCCPARAMS1) ^= 4;
	assert(context_valid64(8, 31) && context_valid64(16, 31));
	assert(!context_valid64(0, 31) && !context_valid64(12, 31));
	assert(!context_valid64(16, 32) && !context_valid64(UINT32_MAX, UINT32_MAX));
	/* Exhausted one-pass queues reject cleanly, including UINT32_MAX addition. */
	xhci0_ep0_enqueue = UINT32_MAX;
	assert(!xhci_control_ready64(1, 3));
	xhci_states[0] = good;
	xhci0_command_enqueue = 255;
	struct XHCI64_COMMAND_RESULT command;
	assert(xhci64_noop_command((uintptr_t) registers64, &command) == -1);
	xhci_states[0] = good;
}

static void bad_completions64(void)
{
	struct XHCI64_CONTROLLER_STATE good;
	struct XHCI64_TRANSFER_RESULT transfer = { 0 };
	uint8_t data[8];
	uint64_t pointers[5];

	assert(xhci64_select_controller(0) == 0);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
	good = xhci_states[0];
	pointers[0] = xhci0_interrupt_pending_pointer + 1;
	pointers[1] = rp1_dma_address64(&xhci0_interrupt_ring[255]);
	pointers[2] = rp1_dma_address64(xhci_states[1].interrupt_ring);
	pointers[3] = rp1_dma_address64(xhci0_descriptor_buffer);
	pointers[4] = UINT64_MAX;
	for (unsigned int index = 0; index < 5; index++) {
		memset(data, 0xa5, sizeof(data));
		xhci0_interrupt_pending_pointer = pointers[index];
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == -22);
		assert(xhci0_event_dequeue == good.event_dequeue && data[0] == 0xa5);
		xhci_states[0] = good;
		event64(pointers[index], XHCI_TRB_TYPE_TRANSFER_EVENT, 3, 1U << 24);
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == -5);
		assert(strcmp(poll_last64.stage, "interrupt-event") == 0 &&
			strcmp(poll_last64.register_name, "USBSTS") == 0 && poll_last64.error == -5);
		assert(xhci0_interrupt_pending_pointer == good.interrupt_pending_pointer);
		assert(data[0] == 0xa5);
		xhci_states[0] = good;
	}
	for (unsigned int error = 0; error < 4; error++) {
		event64(good.interrupt_pending_pointer, XHCI_TRB_TYPE_TRANSFER_EVENT, 3,
			(1U << 24) | (error == 1 ? 9 : 0));
		struct XHCI64_TRB *event = &xhci0_event_ring[xhci0_event_dequeue];

		if (error == 0) { event->control |= XHCI_TRB_EVENT_DATA64; }
		if (error == 2) { event->control ^= 1U << 16; }
		if (error == 3) { event->control ^= 1U << 24; }
		assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == -5);
		assert(data[0] == 0xa5 && xhci0_interrupt_pending_pointer == good.interrupt_pending_pointer);
		xhci_states[0] = good;
	}
	/* Cycle ownership mismatch is an empty event slot, never a completion. */
	event64(good.interrupt_pending_pointer, XHCI_TRB_TYPE_TRANSFER_EVENT, 3, 1U << 24);
	xhci0_event_ring[xhci0_event_dequeue].control ^= XHCI_TRB_CYCLE;
	assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == 0);
	assert(xhci0_event_dequeue == good.event_dequeue && data[0] == 0xa5);
	xhci_states[0] = good;
	assert(wait_transfer_completion64(capability64() + 0x1020,
		pointers[0], 1, 3, 1000, &transfer) == -3);
	/* The pending buffer cannot be overwritten by a synchronous transfer. */
	assert(xhci_interrupt_read64((uintptr_t) registers64, 1, 3, 8, 1000, data, &transfer) == -22);
	xhci0_interrupt_pending_pointer = 0;
}

static void wraps64(void)
{
	struct XHCI64_COMMAND_RESULT command;
	uint8_t data[8];
	uint64_t expected;
	uintptr_t operational = capability64() + 0x40;
	uintptr_t interrupter = capability64() + 0x1020;

	xhci0_event_dequeue = 255;
	xhci0_interrupt_enqueue = 254;
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
	assert(xhci0_interrupt_enqueue == 0 && xhci0_interrupt_cycle == 0);
	event64(xhci0_interrupt_pending_pointer, XHCI_TRB_TYPE_TRANSFER_EVENT, 3, 1U << 24);
	assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == 8);
	assert(xhci0_event_dequeue == 0 && xhci0_event_cycle == 0);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
	event64(xhci0_interrupt_pending_pointer, XHCI_TRB_TYPE_TRANSFER_EVENT, 3, 1U << 24);
	assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == 8);
	/* Command completion pointers must select a posted aligned data TRB. */
	xhci0_command_ring[xhci0_command_enqueue].control = XHCI_TRB_TYPE_NOOP_COMMAND | 1U;
	expected = rp1_dma_address64(&xhci0_command_ring[xhci0_command_enqueue++]);
	assert(wait_command_completion64(operational, interrupter, expected + 1, &command, NULL) == -3);
	event64(expected + 1, XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, 1U << 24);
	assert(wait_command_completion64(operational, interrupter, expected, &command, NULL) == -3);
	event64(expected, XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, 1U << 24);
	assert(wait_command_completion64(operational, interrupter, expected, &command, NULL) == 0);
	xhci0_command_ring[xhci0_command_enqueue].control =
		XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND | (1U << 24) | 1U;
	expected = rp1_dma_address64(&xhci0_command_ring[xhci0_command_enqueue++]);
	event64(expected, XHCI_TRB_TYPE_COMMAND_COMPLETION, 0, 1U << 24);
	xhci0_event_ring[xhci0_event_dequeue].control ^= 3U << 24;
	assert(wait_command_completion64(operational, interrupter, expected, &command, NULL) == -3);
}

static void polling64(void)
{
	volatile uint32_t value = 1;
	unsigned int failures = poll_failures64;

	clock64 = UINT64_MAX - 500U;
	assert(wait_bits64(&value, 1, 0, 1, "controller-halt", "USBSTS") == -110);
	assert(clock64 < 1000 && poll_failures64 == failures + 1U &&
		strcmp(poll_last64.device, "xhci1") == 0 && poll_last64.value == 1 &&
		poll_last64.address == (uintptr_t) &value && poll_last64.error == -110);
	value = 0; failures = poll_failures64;
	assert(wait_bits64(&value, 1, 0, 0, "controller-halt", "USBSTS") == 0);
	assert(poll_failures64 == failures);
	frequency64 = 0;
	assert(wait_bits64(NULL, 1, 0, 0, "controller-halt", "USBSTS") == -22);
	assert(strcmp(poll_last64.register_name, "CNTFRQ_EL0") == 0);
	frequency64 = UINT64_MAX;
	assert(wait_bits64(NULL, 1, 0, 0, "controller-halt", "USBSTS") == -22);
	frequency64 = 1; value = 1;
	assert(wait_bits64(&value, 1, 0, 0, "controller-halt", "USBSTS") == -110);
	frequency64 = 1000;
}

static void faults64(void)
{
	struct XHCI64_CONTROLLER_STATE before;

	polling64();
	capabilities64();
	configuration64();
	before = xhci_states[xhci_controller_id];
	assert(dma_state_valid64());
	dma_physical64 = AARCH64_RAM_BYTES64 - 4096U;
	assert(!dma_state_valid64() && !rings_valid64());
	dma_physical64 = 0x100001U;
	assert(!dma_state_valid64());
	dma_physical64 = UINTPTR_MAX;
	assert(!dma_state_valid64() && rp1_dma_address64(NULL) == 0);
	dma_physical64 = 0x100000U;
	cache_line64 = 128;
	assert(!dma_state_valid64());
	cache_line64 = 64;
	assert(dma_state_valid64() && memcmp(&before,
		&xhci_states[xhci_controller_id], sizeof(before)) == 0);
	corrupted_metadata64();
	bad_completions64();
	wraps64();
	puts("xHCI capability guard page, metadata, pointers, context and cycle wrap: PASS");
}

static void port_model_setup64(uint32_t port, int stuck)
{
	port_model_id64 = port;
	port_stuck64 = stuck;
	port_model_value64 = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_PP | (3U << 10);
	*(uint32_t *) (capability64() + 0x40 + XHCI_PORTSC_BASE +
		(port - 1U) * XHCI_PORT_STRIDE) = port_model_value64;
}

static void quarantine64(void)
{
	struct XHCI64_CONTROLLER_STATE healthy;
	struct XHCI64_ADDRESS_RESULT address;
	struct XHCI64_PORT_RESULT port;
	struct XHCI64_SLOT_RESULT slot;
	uint8_t healthy_registers[XHCI_REGISTER_BYTES64];
	uint8_t data[8];
	uint64_t pending;
	uint32_t enqueue;

	configuration64();
	assert(xhci64_select_controller(1) == 0);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
	healthy = xhci_states[1];
	memcpy(healthy_registers, registers64 + RP1_USB1_BASE, sizeof(healthy_registers));
	assert(xhci64_select_controller(0) == 0);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == 0);
	pending = xhci0_interrupt_pending_pointer;
	enqueue = xhci0_command_enqueue;
	/* A sibling root port is isolated without retiring the live device. */
	port_model_setup64(2, 0);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 2) == 0);
	assert(port_isolated64(2) && slot_usable64(1));
	assert(xhci0_interrupt_pending_pointer == pending && xhci0_command_enqueue == enqueue);
	assert((port_last_write64 & (XHCI_PORTSC_PR | XHCI_PORTSC_WPR | (0x7fU << 17))) == 0);
	assert(port_last_write64 & XHCI_PORTSC_PP);
	/* A failed hardware disable blocks new requests but retains all DMA ownership. */
	port_model_setup64(1, 1);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == -110);
	assert(port_isolated64(1) && !slot_usable64(1));
	assert(xhci_states[0].active_slot == 1 && xhci0_interrupt_pending_pointer == pending);
	assert(xhci0_command_enqueue == enqueue);
	assert(xhci_interrupt_arm64((uintptr_t) registers64, 1, 3, 8) == -22);
	assert(!xhci_control_ready64(1, 3));
	port_model_setup64(1, 0);
	completion_code64 = 2;
	complete64 = DISABLE64;
	disable_after64 = xhci0_command_enqueue;
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == -5);
	assert(xhci0_dcbaa[1] != 0 && xhci0_interrupt_pending_pointer == pending);
	completion_code64 = 1;
	complete64 = NONE64;
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == -110);
	assert(xhci_states[0].active_slot == 1 && xhci0_interrupt_pending_pointer == pending);
	complete64 = DISABLE64;
	disable_after64 = xhci0_command_enqueue;
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == 0);
	assert(xhci_states[0].active_slot == 0 && xhci0_dcbaa[1] == 0);
	assert(xhci0_interrupt_pending_pointer == 0);
	assert(!xhci_states[0].ep0_ready && !xhci_states[0].interrupt_ready);
	assert(xhci64_address_device((uintptr_t) registers64, 1, 3, 2, &address) == -1);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == 0);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 0) == -22);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 256) == -22);
	assert(xhci_port_quarantine64(4, 1) == -22);
	/* The scanner skips both rejected ports and can enumerate a healthy sibling. */
	*(uint32_t *) (capability64() + XHCI_HCCPARAMS1) = (0x3000U / 4U) << 16;
	*(uint32_t *) (capability64() + 0x3000) = (2U << 24) | 2U;
	*(uint32_t *) (capability64() + 0x3008) = 1U | (3U << 8);
	port_model_setup64(3, 0);
	assert(xhci64_reset_connected_port((uintptr_t) registers64, &port) == 0);
	assert(port.port_id == 3);
	port_model_id64 = 0;
	allocated_slot64 = 2;
	complete64 = ENABLE64;
	assert(xhci64_enable_slot((uintptr_t) registers64, &slot) == 0 && slot.slot_id == 2);
	complete64 = ADDRESS64;
	assert(xhci64_address_device((uintptr_t) registers64, 3, 3, 2, &address) == 0);
	assert(slot_usable64(2) && !slot_usable64(1));
	assert(memcmp(&healthy, &xhci_states[1], sizeof(healthy)) == 0);
	assert(memcmp(healthy_registers, registers64 + RP1_USB1_BASE, sizeof(healthy_registers)) == 0);
	assert(xhci64_select_controller(1) == 0);
	event64(xhci0_interrupt_pending_pointer, XHCI_TRB_TYPE_TRANSFER_EVENT, 3, 1U << 24);
	assert(xhci_interrupt_poll64((uintptr_t) registers64, 1, 3, 8, data, 8) == 8);
	/* Retire a known enabled slot even if addressing never published a context. */
	assert(xhci64_select_controller(0) == 0);
	port_model_setup64(3, 0);
	complete64 = DISABLE64; disable_after64 = xhci0_command_enqueue;
	assert(xhci_port_quarantine64((uintptr_t) registers64, 3) == 0);
	*(uint32_t *) (capability64() + XHCI_HCSPARAMS1) = (4U << 24) | 64U;
	*(uint32_t *) (capability64() + 0x3008) = 1U | (4U << 8);
	port_model_setup64(4, 0);
	assert(xhci64_reset_connected_port((uintptr_t) registers64, &port) == 0 && port.port_id == 4);
	port_model_id64 = 0;
	allocated_slot64 = 3;
	complete64 = ENABLE64;
	assert(xhci64_enable_slot((uintptr_t) registers64, &slot) == 0 && slot.slot_id == 3);
	assert(xhci_states[0].active_slot == 0 && xhci_states[0].enabled_slot == 3);
	assert(xhci64_address_device((uintptr_t) registers64, 4, 0, 3, &address) == -1);
	assert(xhci_port_quarantine64((uintptr_t) registers64, 1) == 0);
	assert(xhci_states[0].enabled_slot == 3); /* An unrelated port cannot retire it. */
	port_model_setup64(4, 0);
	complete64 = DISABLE64; disable_after64 = xhci0_command_enqueue;
	assert(xhci_port_quarantine64((uintptr_t) registers64, 4) == 0);
	assert(xhci_states[0].enabled_slot == 0);
	puts("xHCI quarantine: PED, slot retirement/retry, DMA retention and sibling isolation: PASS");
}

int main(int argc, char **argv)
{
	registers64 = calloc(1, 0x400000);
	assert(registers64 != NULL);
	controllers64();
	if (argc == 2 && strcmp(argv[1], "--quarantine") == 0) {
		quarantine64();
	} else if (argc == 2 && strcmp(argv[1], "--faults") == 0) {
		faults64();
	} else {
		control64();
		configuration64();
		interrupt64();
	}
	assert(copies64 != 0);
	free(registers64);
	puts("xHCI transport: control TRBs, DMA copies, contexts and controller isolation: PASS");
	return 0;
}
