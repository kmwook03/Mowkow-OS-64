/* RP1's two DWC3 host blocks expose xHCI capability registers at offset 0. */
#include <arch/arch64.h>
#include <arch/aarch64_memory64.h>
#include <arch/aarch64_poll64.h>
#include <stdint.h>
#include "xhci_internal64.h"
#include "xhci_platform64.h"

#define RP1_USB0_BASE 0x200000U
#define RP1_USB1_BASE 0x300000U
#define XHCI_CAPLENGTH_HCIVERSION 0x00U
#define XHCI_HCSPARAMS1 0x04U
#define XHCI_HCSPARAMS2 0x08U
#define XHCI_HCCPARAMS1 0x10U
#define XHCI_RTSOFF 0x18U
#define XHCI_DBOFF 0x14U
#define XHCI_USBCMD 0x00U
#define XHCI_USBSTS 0x04U
#define XHCI_CRCR 0x18U
#define XHCI_DCBAAP 0x30U
#define XHCI_CONFIG 0x38U
#define XHCI_PORTSC_BASE 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_IR0_BASE 0x20U
#define XHCI_IMAN 0x00U
#define XHCI_IMOD 0x04U
#define XHCI_ERSTSZ 0x08U
#define XHCI_ERSTBA 0x10U
#define XHCI_ERDP 0x18U
#define XHCI_USBCMD_RUN (1U << 0)
#define XHCI_USBCMD_HCRST (1U << 1)
#define XHCI_USBSTS_HCH (1U << 0)
#define XHCI_USBSTS_CNR (1U << 11)
#define XHCI_TIMEOUT_MS 1000U
#define XHCI_MAX_SLOTS XHCI64_MAX_SLOTS
#define XHCI_RING_TRBS 256U
#define XHCI_MAX_SCRATCHPADS 4U
#define XHCI_TRB_TYPE_LINK (6U << 10)
#define XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND (10U << 10)
#define XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND (9U << 10)
#define XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND (11U << 10)
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND (12U << 10)
#define XHCI_TRB_TYPE_NOOP_COMMAND (23U << 10)
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32U
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33U
#define XHCI_TRB_TYPE_SETUP_STAGE (2U << 10)
#define XHCI_TRB_TYPE_DATA_STAGE (3U << 10)
#define XHCI_TRB_TYPE_STATUS_STAGE (4U << 10)
#define XHCI_TRB_TYPE_NORMAL (1U << 10)
#define XHCI_TRB_TOGGLE_CYCLE (1U << 1)
#define XHCI_TRB_CYCLE (1U << 0)
#define XHCI_TRB_IOC (1U << 5)
#define XHCI_TRB_IDT (1U << 6)
#define XHCI_TRB_DIR_IN (1U << 16)
#define XHCI_TRB_TRT_IN (3U << 16)
#define XHCI_ERDP_EHB (1U << 3)
#define XHCI_EXT_CAP_SUPPORTED_PROTOCOL 2U
#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR (1U << 4)
#define XHCI_PORTSC_PP (1U << 9)
#define XHCI_PORTSC_SPEED_MASK (0xfU << 10)
#define XHCI_PORTSC_WRC (1U << 19)
#define XHCI_PORTSC_PRC (1U << 21)
#define XHCI_PORTSC_WPR (1U << 31)
#define XHCI_PORTSC_NEUTRAL_MASK 0x0000ffe9U
#define XHCI_PORT_CONNECT_TIMEOUT_MS 3000U
#define XHCI_PORT_RESET_RECOVERY_MS 50U
#define XHCI_CONTEXT_DWORDS64 1024U

/* DWC3 exposes the xHCI register file below its global/device registers. */
#define XHCI_REGISTER_BYTES64 0x8000U
#define XHCI_EXT_CAP_LIMIT64 64U
#define XHCI_MAX_PORTS64 255U
#define XHCI_PORT_MASK_WORDS64 ((XHCI_MAX_PORTS64 + 31U) / 32U)
#define XHCI_TRB_EVENT_DATA64 (1U << 2)
#define XHCI_TRB_TYPE_MASK64 (0x3fU << 10)

struct XHCI64_TRB {
	uint64_t parameter;
	uint32_t status;
	uint32_t control;
};

struct XHCI64_ERST_ENTRY {
	uint64_t address;
	uint32_t size;
	uint32_t reserved;
};

struct XHCI64_CONTROLLER_STATE {
	uintptr_t capability;
	uint32_t context_dwords;
	uint32_t started;
	uint32_t ep0_ready;
	uint32_t interrupt_ready;
	uint32_t active_port;
	uint32_t active_slot;
	uint32_t enabled_slot;
	uint32_t enabled_port;
	uint32_t selected_port;
	uint32_t quarantined_ports[XHCI_PORT_MASK_WORDS64];
	uint64_t dcbaa[XHCI_MAX_SLOTS + 1] __attribute__((aligned(64)));
	struct XHCI64_TRB command_ring[XHCI_RING_TRBS]
		__attribute__((aligned(4096)));
	struct XHCI64_TRB event_ring[XHCI_RING_TRBS]
		__attribute__((aligned(4096)));
	struct XHCI64_ERST_ENTRY erst __attribute__((aligned(64)));
	uint64_t scratchpad_array[XHCI_MAX_SCRATCHPADS]
		__attribute__((aligned(64)));
	uint8_t scratchpads[XHCI_MAX_SCRATCHPADS][4096]
		__attribute__((aligned(4096)));
	uint32_t input_context[XHCI_CONTEXT_DWORDS64] __attribute__((aligned(4096)));
	uint32_t device_context[XHCI_CONTEXT_DWORDS64] __attribute__((aligned(4096)));
	struct XHCI64_TRB ep0_ring[XHCI_RING_TRBS]
		__attribute__((aligned(4096)));
	struct XHCI64_TRB interrupt_ring[XHCI_RING_TRBS]
		__attribute__((aligned(4096)));
	uint8_t descriptor_buffer[XHCI64_CONTROL_BYTES] __attribute__((aligned(64)));
	uint8_t interrupt_report[XHCI64_INTERRUPT_BYTES] __attribute__((aligned(64)));
	uint32_t command_enqueue;
	uint32_t command_cycle;
	uint32_t event_dequeue;
	uint32_t event_cycle;
	uint32_t ep0_enqueue;
	uint32_t ep0_cycle;
	uint32_t interrupt_enqueue;
	uint32_t interrupt_cycle;
	uint64_t interrupt_pending_pointer;
	uint32_t interrupt_pending_slot;
	uint32_t interrupt_pending_endpoint;
	uint32_t interrupt_pending_length;
};

_Static_assert(sizeof(struct XHCI64_TRB) == 16, "TRBs must occupy 16 bytes");
_Static_assert(offsetof(struct XHCI64_TRB, control) == 12, "TRB control offset");
_Static_assert(sizeof(struct XHCI64_ERST_ENTRY) == 16, "ERST entry size");
_Static_assert(33U * 16U <= XHCI_CONTEXT_DWORDS64, "input contexts must fit 64-byte layout");
_Static_assert(32U * 16U <= XHCI_CONTEXT_DWORDS64, "device contexts must fit 64-byte layout");

_Static_assert(sizeof(((struct XHCI64_CONTROLLER_STATE *) 0)->command_ring) == 4096U,
	"command ring must fit one DMA page");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, command_ring) % 4096U == 0,
	"command ring alignment");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, event_ring) % 4096U == 0,
	"event ring alignment");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, ep0_ring) % 4096U == 0,
	"EP0 ring alignment");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, interrupt_ring) % 4096U == 0,
	"interrupt ring alignment");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, input_context) % 64U == 0,
	"input context alignment");
_Static_assert(offsetof(struct XHCI64_CONTROLLER_STATE, device_context) % 64U == 0,
	"device context alignment");

static struct XHCI64_CONTROLLER_STATE xhci_states[2]
	__attribute__((aligned(4096)));
static uint32_t xhci_controller_id;
static uint32_t xhci_controller_offset = RP1_USB0_BASE;

#define xhci0_dcbaa (xhci_states[xhci_controller_id].dcbaa)
#define xhci0_command_ring (xhci_states[xhci_controller_id].command_ring)
#define xhci0_event_ring (xhci_states[xhci_controller_id].event_ring)
#define xhci0_erst (xhci_states[xhci_controller_id].erst)
#define xhci0_scratchpad_array (xhci_states[xhci_controller_id].scratchpad_array)
#define xhci0_scratchpads (xhci_states[xhci_controller_id].scratchpads)
#define xhci0_input_context (xhci_states[xhci_controller_id].input_context)
#define xhci0_device_context (xhci_states[xhci_controller_id].device_context)
#define xhci0_ep0_ring (xhci_states[xhci_controller_id].ep0_ring)
#define xhci0_interrupt_ring (xhci_states[xhci_controller_id].interrupt_ring)
#define xhci0_descriptor_buffer (xhci_states[xhci_controller_id].descriptor_buffer)
#define xhci0_interrupt_report (xhci_states[xhci_controller_id].interrupt_report)
#define xhci0_command_enqueue (xhci_states[xhci_controller_id].command_enqueue)
#define xhci0_command_cycle (xhci_states[xhci_controller_id].command_cycle)
#define xhci0_event_dequeue (xhci_states[xhci_controller_id].event_dequeue)
#define xhci0_event_cycle (xhci_states[xhci_controller_id].event_cycle)
#define xhci0_ep0_enqueue (xhci_states[xhci_controller_id].ep0_enqueue)
#define xhci0_ep0_cycle (xhci_states[xhci_controller_id].ep0_cycle)
#define xhci0_interrupt_enqueue (xhci_states[xhci_controller_id].interrupt_enqueue)
#define xhci0_interrupt_cycle (xhci_states[xhci_controller_id].interrupt_cycle)
#define xhci0_interrupt_pending_pointer \
	(xhci_states[xhci_controller_id].interrupt_pending_pointer)

int xhci64_select_controller(uint32_t controller_id)
{
	if (controller_id > 1U) {
		return -1;
	}
	xhci_controller_id = controller_id;
	xhci_controller_offset = controller_id == 0U ? RP1_USB0_BASE : RP1_USB1_BASE;
	return 0;
}

static uint64_t rp1_dma_address64(const void *address);

struct XHCI64_REGISTERS {
	uintptr_t operational;
	uintptr_t runtime;
	uintptr_t interrupter;
	uintptr_t doorbells;
	uint32_t caplength;
	uint32_t hcsparams1;
	uint32_t context_dwords;
	uint32_t port_count;
};

static int selected_valid64(void)
{
	return xhci_controller_id < 2U && xhci_controller_offset ==
		(xhci_controller_id == 0 ? RP1_USB0_BASE : RP1_USB1_BASE);
}

static uintptr_t controller_capability64(uintptr_t rp1_base, uint32_t offset)
{
	if (rp1_base == 0 || (rp1_base & 3U) != 0 ||
			rp1_base > UINTPTR_MAX - offset - XHCI_REGISTER_BYTES64) {
		return 0;
	}
	return rp1_base + offset;
}

static int register_span64(uint32_t offset, uint32_t bytes)
{
	return offset <= XHCI_REGISTER_BYTES64 && bytes <= XHCI_REGISTER_BYTES64 - offset;
}

static int register_layout64(uintptr_t capability, struct XHCI64_REGISTERS *regs)
{
	uint32_t cap;
	uint32_t hcc;
	uint32_t runtime;
	uint32_t doorbells;
	uint32_t ports_end;

	if (regs == NULL || capability == 0 || (capability & 3U) != 0 ||
			capability > UINTPTR_MAX - XHCI_REGISTER_BYTES64) {
		return -22;
	}
	cap = *(volatile uint32_t *) capability;
	regs->caplength = cap & 255U;
	regs->hcsparams1 = *(volatile uint32_t *) (capability + XHCI_HCSPARAMS1);
	hcc = *(volatile uint32_t *) (capability + XHCI_HCCPARAMS1);
	if (cap == 0xdeaddeadU || (cap >> 16) < 0x100U || (cap >> 16) >= 0x200U ||
			regs->caplength < 0x20U || (regs->caplength & 3U) != 0 ||
			regs->hcsparams1 == UINT32_MAX || regs->hcsparams1 == 0xdeaddeadU ||
			hcc == UINT32_MAX || hcc == 0xdeaddeadU) {
		return -71;
	}
	regs->port_count = regs->hcsparams1 >> 24;
	if (regs->port_count == 0 || (regs->hcsparams1 & 255U) < XHCI_MAX_SLOTS) {
		return -71;
	}
	ports_end = regs->caplength + XHCI_PORTSC_BASE + regs->port_count * XHCI_PORT_STRIDE;
	runtime = *(volatile uint32_t *) (capability + XHCI_RTSOFF);
	doorbells = *(volatile uint32_t *) (capability + XHCI_DBOFF);
	if ((runtime & 31U) != 0 || (doorbells & 3U) != 0 ||
			!register_span64(0, ports_end) || !register_span64(runtime, 0x40U) ||
			!register_span64(doorbells, (XHCI_MAX_SLOTS + 1U) * 4U) ||
			runtime < ports_end || doorbells < ports_end ||
			(runtime < doorbells + (XHCI_MAX_SLOTS + 1U) * 4U &&
			 doorbells < runtime + 0x40U)) {
		return -71;
	}
	regs->context_dwords = (hcc & (1U << 2)) != 0 ? 16U : 8U;
	regs->operational = capability + regs->caplength;
	regs->runtime = capability + runtime;
	regs->interrupter = regs->runtime + XHCI_IR0_BASE;
	regs->doorbells = capability + doorbells;
	return 0;
}

static int port_isolated64(uint32_t port_id)
{
	if (!selected_valid64() || port_id == 0 || port_id > XHCI_MAX_PORTS64) { return 1; }
	return (xhci_states[xhci_controller_id].quarantined_ports[(port_id - 1U) / 32U] &
		(1U << ((port_id - 1U) % 32U))) != 0;
}

static int slot_usable64(uint32_t slot_id)
{
	return selected_valid64() && slot_id != 0 && slot_id <= XHCI_MAX_SLOTS &&
		xhci_states[xhci_controller_id].active_slot == slot_id &&
		!port_isolated64(xhci_states[xhci_controller_id].active_port);
}

static int link_valid64(const struct XHCI64_TRB *ring)
{
	const struct XHCI64_TRB *link = &ring[XHCI_RING_TRBS - 1U];

	return link->parameter == rp1_dma_address64(ring) && link->status == 0 &&
		(link->control & ~XHCI_TRB_CYCLE) == (XHCI_TRB_TYPE_LINK | XHCI_TRB_TOGGLE_CYCLE);
}

/* One-pass command/EP0 queues may reach the reserved Link TRB but never use it
 * as data. Interrupt and event queues wrap, so their cycle state may be 0 or 1.
 */
static int dma_state_valid64(void)
{
	uint64_t dma;

	return selected_valid64() && xhci_cache_size64() <= 64U &&
		rp1_dma_range64(xhci_physical64((uintptr_t) &xhci_states[xhci_controller_id]),
			sizeof(xhci_states[0]), 4096U, &dma) == 0;
}

static int rings_valid64(void)
{
	const struct XHCI64_CONTROLLER_STATE *state;

	if (!dma_state_valid64()) { return 0; }
	state = &xhci_states[xhci_controller_id];
	return state->active_slot <= XHCI_MAX_SLOTS && state->enabled_slot <= XHCI_MAX_SLOTS &&
		state->enabled_port <= XHCI_MAX_PORTS64 && state->selected_port <= XHCI_MAX_PORTS64 &&
		((state->enabled_slot == 0) == (state->enabled_port == 0)) &&
		(state->active_slot == 0 || (state->active_slot == state->enabled_slot &&
		 state->active_port == state->enabled_port)) &&
		state->active_port <= XHCI_MAX_PORTS64 &&
		((state->active_slot == 0) == (state->active_port == 0)) &&
		state->started == 1 && state->ep0_ready <= 1U && state->interrupt_ready <= 1U &&
		state->command_enqueue <= XHCI_RING_TRBS - 1U && state->command_cycle == 1U &&
		state->ep0_enqueue <= XHCI_RING_TRBS - 1U && state->ep0_cycle == 1U &&
		state->interrupt_enqueue < XHCI_RING_TRBS - 1U && state->interrupt_cycle <= 1U &&
		state->event_dequeue < XHCI_RING_TRBS && state->event_cycle <= 1U &&
		(state->context_dwords == 8U || state->context_dwords == 16U) &&
		link_valid64(state->command_ring) &&
		(!state->ep0_ready || link_valid64(state->ep0_ring)) &&
		(!state->interrupt_ready || link_valid64(state->interrupt_ring));
}

static int context_valid64(uint32_t words, uint32_t endpoint)
{
	return (words == 8U || words == 16U) && endpoint <= 31U &&
		(endpoint + 2U) <= XHCI_CONTEXT_DWORDS64 / words &&
		(endpoint + 1U) <= XHCI_CONTEXT_DWORDS64 / words;
}

static int active_layout64(uintptr_t capability, struct XHCI64_REGISTERS *regs)
{
	return rings_valid64() && xhci_states[xhci_controller_id].capability == capability &&
		register_layout64(capability, regs) == 0 &&
		xhci_states[xhci_controller_id].context_dwords == regs->context_dwords &&
		context_valid64(regs->context_dwords, 31U);
}

/* DMA completion values are compared as integers; never dereference them. */
static int ring_pointer64(uint64_t pointer, const struct XHCI64_TRB *ring,
	uint32_t entries, uint32_t *index)
{
	uint64_t base = rp1_dma_address64(ring);
	uint64_t delta;

	if (base == 0 || pointer < base || entries > XHCI_RING_TRBS - 1U) { return 0; }
	delta = pointer - base;
	if ((delta & (sizeof(*ring) - 1U)) != 0 || delta / sizeof(*ring) >= entries) {
		return 0;
	}
	if (index != NULL) { *index = (uint32_t) (delta / sizeof(*ring)); }
	return 1;
}

static int command_pointer64(uint64_t pointer)
{
	uint32_t index;
	uint32_t type;
	const struct XHCI64_TRB *trb;

	if (!rings_valid64() ||
			!ring_pointer64(pointer, xhci0_command_ring, xhci0_command_enqueue, &index)) {
		return 0;
	}
	trb = &xhci0_command_ring[index];
	type = trb->control & XHCI_TRB_TYPE_MASK64;
	return (trb->control & XHCI_TRB_CYCLE) == xhci0_command_cycle &&
		(type == XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND || type == XHCI_TRB_TYPE_NOOP_COMMAND ||
		 type == XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND ||
		 type == XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND ||
		 type == XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND);
}

static int transfer_pointer64(uint64_t pointer, uint32_t endpoint)
{
	uint32_t index;
	uint32_t cycle;
	const struct XHCI64_TRB *trb;

	if (!rings_valid64()) { return 0; }
	if (endpoint == 1U) {
		if (!xhci_states[xhci_controller_id].ep0_ready ||
				!ring_pointer64(pointer, xhci0_ep0_ring, xhci0_ep0_enqueue, &index) ||
				index + 1U != xhci0_ep0_enqueue) { return 0; }
		trb = &xhci0_ep0_ring[index];
		return (trb->control & XHCI_TRB_TYPE_MASK64) == XHCI_TRB_TYPE_STATUS_STAGE &&
			(trb->control & XHCI_TRB_IOC) && (trb->control & XHCI_TRB_CYCLE) == 1U;
	}
	if (!xhci_states[xhci_controller_id].interrupt_ready || endpoint < 3U ||
			endpoint > 31U || (endpoint & 1U) == 0 ||
			!ring_pointer64(pointer, xhci0_interrupt_ring, XHCI_RING_TRBS - 1U, &index) ||
			index != (xhci0_interrupt_enqueue == 0 ? XHCI_RING_TRBS - 2U :
				xhci0_interrupt_enqueue - 1U)) { return 0; }
	cycle = xhci0_interrupt_enqueue == 0 ? xhci0_interrupt_cycle ^ 1U :
		xhci0_interrupt_cycle;
	trb = &xhci0_interrupt_ring[index];
	return trb->parameter == rp1_dma_address64(xhci0_interrupt_report) &&
		(trb->control & XHCI_TRB_TYPE_MASK64) == XHCI_TRB_TYPE_NORMAL &&
		(trb->control & XHCI_TRB_IOC) && (trb->control & XHCI_TRB_CYCLE) == cycle;
}

static uintptr_t xhci_capability64(uintptr_t rp1_base)
{
	return selected_valid64() ?
		controller_capability64(rp1_base, xhci_controller_offset) : 0;
}

static int controller_has_connection64(uintptr_t base)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = base;
	uintptr_t operational;
	uint32_t hcsparams1;
	uint32_t port_count;
	uint32_t port;

	if (register_layout64(capability, &regs) != 0) { return -1; }

	hcsparams1 = regs.hcsparams1;
	port_count = (hcsparams1 >> 24) & 0xffU;
	operational = regs.operational;
	for (port = 0; port < port_count; port++) {
		uint32_t status = *(volatile uint32_t *) (operational +
			XHCI_PORTSC_BASE + port * XHCI_PORT_STRIDE);

		if ((status & XHCI_PORTSC_CCS) != 0) {
			return 1;
		}
	}
	return 0;
}

static uint64_t rp1_dma_address64(const void *address)
{
	uint64_t dma;

	if (address == NULL || rp1_dma_range64(xhci_physical64((uintptr_t) address),
			1U, 1U, &dma) != 0) { return 0; }
	return dma;
}

static void zero_words64(void *address, size_t bytes)
{
	uint64_t *words = (uint64_t *) address;
	size_t count = (bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t);
	size_t i;

	for (i = 0; i < count; i++) {
		words[i] = 0;
	}
}

static int advance_interrupt_ring64(void)
{
	if (!rings_valid64() || !xhci_states[xhci_controller_id].interrupt_ready) {
		return -22;
	}
	xhci0_interrupt_enqueue++;
	if (xhci0_interrupt_enqueue == XHCI_RING_TRBS - 1U) {
		xhci0_interrupt_ring[XHCI_RING_TRBS - 1U].control =
			XHCI_TRB_TYPE_LINK | XHCI_TRB_TOGGLE_CYCLE |
			xhci0_interrupt_cycle;
		xhci_clean_cache64(&xhci0_interrupt_ring[XHCI_RING_TRBS - 1U],
			sizeof(xhci0_interrupt_ring[XHCI_RING_TRBS - 1U]));
		xhci0_interrupt_enqueue = 0;
		xhci0_interrupt_cycle ^= 1U;
	}
	return 0;
}

static const char *xhci_device64(unsigned int controller)
{
	return controller == 0 ? "xhci0" : "xhci1";
}

static int xhci_deadline64(struct POLL_DEADLINE64 *deadline, uint32_t milliseconds,
	unsigned int controller, const char *stage)
{
	uint64_t frequency = xhci_frequency64();
	int status = poll_deadline_init64(deadline, xhci_counter64(), frequency, milliseconds, 1000U);

	if (status != 0) {
		aarch64_poll_fail64(xhci_device64(controller), stage, "CNTFRQ_EL0", 0, frequency, status);
	}
	return status;
}

static int wait_bits64(volatile uint32_t *register_address, uint32_t mask,
	uint32_t expected, unsigned int controller, const char *stage, const char *register_name)
{
	struct POLL_DEADLINE64 deadline;
	uint32_t sampled;

	if (xhci_deadline64(&deadline, XHCI_TIMEOUT_MS, controller, stage) != 0) { return -22; }
	for (;;) {
		sampled = *register_address;
		if ((sampled & mask) == expected) { return 0; }
		if (poll_deadline_expired64(&deadline, xhci_counter64())) {
			aarch64_poll_fail64(xhci_device64(controller), stage, register_name,
				(uintptr_t) register_address, sampled, -110);
			return -110;
		}
		xhci_relax64();
	}
}

static int delay_xhci64(uint32_t milliseconds)
{
	struct POLL_DEADLINE64 deadline;

	if (xhci_deadline64(&deadline, milliseconds, xhci_controller_id, "port-recovery") != 0) {
		return -22;
	}
	while (!poll_deadline_expired64(&deadline, xhci_counter64())) { xhci_relax64(); }
	return 0;
}

static uint32_t port_protocol_major64(uintptr_t capability, uint32_t port_id)
{
	struct XHCI64_REGISTERS regs;
	uint32_t offset;
	uint32_t major = 0;

	if (register_layout64(capability, &regs) != 0 || port_id == 0 ||
			port_id > regs.port_count) { return 0; }
	offset = (*(volatile uint32_t *) (capability + XHCI_HCCPARAMS1) >> 16) * 4U;
	for (unsigned int remaining = 0; offset != 0; remaining++) {
		uint32_t header;
		uint32_t next;
		uint32_t size = 4U;

		if (remaining >= XHCI_EXT_CAP_LIMIT64 || offset < 0x20U ||
				!register_span64(offset, size)) { return 0; }
		header = *(volatile uint32_t *) (capability + offset);
		next = ((header >> 8) & 255U) * 4U;
		if ((header & 255U) == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
			uint32_t ports;
			uint32_t first;
			uint32_t count;

			if (!register_span64(offset, 16U)) { return 0; }
			ports = *(volatile uint32_t *) (capability + offset + 8U);
			first = ports & 255U;
			count = (ports >> 8) & 255U;
			size = 16U + (ports >> 28) * 4U;
			if (!register_span64(offset, size) || first == 0 || count == 0 ||
					first > regs.port_count || count > regs.port_count - first + 1U) {
				return 0;
			}
			if (port_id >= first && port_id - first < count) {
				if (major != 0) { return 0; }
				major = header >> 24;
			}
		}
		if (next == 0) { return major; }
		if (next < size || next > XHCI_REGISTER_BYTES64 - offset) { return 0; }
		offset += next;
	}
	return major;
}

static int probe_controller64(uintptr_t base, uint32_t *capability,
	uint32_t *hcsparams1)
{
	struct XHCI64_REGISTERS regs;

	if (register_layout64(base, &regs) != 0) { return -1; }
	*capability = *(volatile uint32_t *) (base + XHCI_CAPLENGTH_HCIVERSION);
	*hcsparams1 = regs.hcsparams1;
	return 0;
}

static int advance_event_ring64(uintptr_t interrupter)
{
	if (!rings_valid64()) { return -22; }
	xhci0_event_dequeue++;
	if (xhci0_event_dequeue == XHCI_RING_TRBS) {
		xhci0_event_dequeue = 0;
		xhci0_event_cycle ^= 1U;
	}
	*(volatile uint64_t *) (interrupter + XHCI_ERDP) =
		rp1_dma_address64(&xhci0_event_ring[xhci0_event_dequeue]) |
		XHCI_ERDP_EHB;
	xhci_barrier64();
	return 0;
}

static int wait_command_completion64(uintptr_t operational,
	uintptr_t interrupter, uint64_t expected_pointer,
	struct XHCI64_COMMAND_RESULT *result, uint32_t *slot_id)
{
	struct POLL_DEADLINE64 deadline;
	uint32_t expected_index;
	uint32_t expected_slot;

	if (result == NULL || !rings_valid64() ||
			!command_pointer64(expected_pointer)) { return -3; }

	if (!ring_pointer64(expected_pointer, xhci0_command_ring,
			xhci0_command_enqueue, &expected_index)) { return -3; }
	expected_slot = xhci0_command_ring[expected_index].control >> 24;
	if (xhci_deadline64(&deadline, XHCI_TIMEOUT_MS, xhci_controller_id,
			"command-completion") != 0) { return -3; }
	while (!poll_deadline_expired64(&deadline, xhci_counter64())) {
		struct XHCI64_TRB *event;
		uint32_t completion_code;
		uint32_t control;
		uint32_t type;
		uint64_t event_pointer;

		if (!rings_valid64()) { return -3; }
		event = &xhci0_event_ring[xhci0_event_dequeue];
		xhci_invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			xhci_relax64();
			continue;
		}
		result->event_status = event->status;
		result->event_control = control;
		result->command_pointer_low = (uint32_t) event->parameter;
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		if (advance_event_ring64(interrupter) != 0) { return -3; }
		if (type != XHCI_TRB_TYPE_COMMAND_COMPLETION) {
			continue;
		}
		completion_code = result->event_status >> 24;
		result->controller_status = *(volatile uint32_t *)
			(operational + XHCI_USBSTS);
		if (completion_code != 1U ||
				!command_pointer64(event_pointer) ||
				event_pointer != expected_pointer ||
				(expected_slot != 0 && (control >> 24) != expected_slot)) {
			aarch64_poll_fail64(xhci_device64(xhci_controller_id), "command-event",
				"USBSTS", operational + XHCI_USBSTS, result->controller_status, -71);
			return -3;
		}
		if (slot_id != NULL) {
			*slot_id = control >> 24;
		}
		return 0;
	}
	result->controller_status = *(volatile uint32_t *) (operational + XHCI_USBSTS);
	aarch64_poll_fail64(xhci_device64(xhci_controller_id), "command-completion",
		"USBSTS", operational + XHCI_USBSTS, result->controller_status, -110);
	return -2;
}

static int wait_transfer_completion64(uintptr_t interrupter,
	uint64_t expected_pointer, uint32_t slot_id, uint32_t endpoint_id,
	uint32_t timeout_ms, struct XHCI64_TRANSFER_RESULT *result)
{
	struct POLL_DEADLINE64 deadline;

	if (result == NULL || slot_id == 0 || slot_id > XHCI_MAX_SLOTS ||
			!transfer_pointer64(expected_pointer, endpoint_id)) { return -3; }

	if (xhci_deadline64(&deadline, timeout_ms, xhci_controller_id,
			"transfer-completion") != 0) { return -3; }
	while (!poll_deadline_expired64(&deadline, xhci_counter64())) {
		struct XHCI64_TRB *event;
		uint32_t completion_code;
		uint32_t control;
		uint32_t event_endpoint_id;
		uint32_t event_slot_id;
		uint32_t type;
		uint64_t event_pointer;

		if (!rings_valid64()) { return -3; }
		event = &xhci0_event_ring[xhci0_event_dequeue];
		xhci_invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			xhci_relax64();
			continue;
		}
		result->event_status = event->status;
		result->event_control = control;
		result->trb_pointer_low = (uint32_t) event->parameter;
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		if (advance_event_ring64(interrupter) != 0) { return -3; }
		if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
			continue;
		}
		completion_code = result->event_status >> 24;
		event_endpoint_id = (control >> 16) & 0x1fU;
		event_slot_id = control >> 24;
		if ((control & XHCI_TRB_EVENT_DATA64) != 0 || completion_code != 1U ||
				(result->event_status & 0x00ffffffU) != 0 ||
				event_endpoint_id != endpoint_id || event_slot_id != slot_id ||
				!transfer_pointer64(event_pointer, endpoint_id) ||
				event_pointer != expected_pointer) {
			aarch64_poll_fail64(xhci_device64(xhci_controller_id), "transfer-event",
				"TRB_STATUS_CONTROL", (uintptr_t) event,
				((uint64_t) result->event_status << 32) | control, -71);
			return -3;
		}
		return 0;
	}
	aarch64_poll_fail64(xhci_device64(xhci_controller_id), "transfer-completion",
		"IMAN", interrupter + XHCI_IMAN, *(volatile uint32_t *) (interrupter + XHCI_IMAN), -110);
	return -2;
}

int xhci64_probe_rp1(uintptr_t rp1_base, uint32_t capability[2],
	uint32_t hcsparams1[2])
{
	int status;

	if (capability == NULL || hcsparams1 == NULL) {
		return -1;
	}
	status = probe_controller64(controller_capability64(rp1_base, RP1_USB0_BASE), &capability[0],
		&hcsparams1[0]);
	if (status != 0) {
		return -2;
	}
	status = probe_controller64(controller_capability64(rp1_base, RP1_USB1_BASE), &capability[1],
		&hcsparams1[1]);
	if (status != 0) {
		return -3;
	}
	return 0;
}

static int reset_controller64(uintptr_t base,
	struct XHCI64_RESET_RESULT *result, unsigned int controller)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = base;
	uintptr_t operational;
	volatile uint32_t *command;
	volatile uint32_t *status;

	if (register_layout64(capability, &regs) != 0) { return -6; }

	operational = regs.operational;
	command = (volatile uint32_t *) (operational + XHCI_USBCMD);
	status = (volatile uint32_t *) (operational + XHCI_USBSTS);
	result->command_before = *command;
	result->status_before = *status;

	if (wait_bits64(status, XHCI_USBSTS_CNR, 0, controller, "controller-ready", "USBSTS") != 0) {
		return -5;
	}
	*command = result->command_before & ~XHCI_USBCMD_RUN;
	xhci_barrier64();
	if (wait_bits64(status, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH,
			controller, "controller-halt", "USBSTS") != 0) {
		return -1;
	}
	*command = *command | XHCI_USBCMD_HCRST;
	xhci_barrier64();
	if (wait_bits64(command, XHCI_USBCMD_HCRST, 0, controller, "controller-reset", "USBCMD") != 0) {
		return -2;
	}
	if (wait_bits64(status, XHCI_USBSTS_CNR, 0, controller, "controller-ready", "USBSTS") != 0) {
		return -3;
	}
	result->command_after = *command;
	result->status_after = *status;
	if ((result->status_after & XHCI_USBSTS_HCH) == 0) {
		return -4;
	}
	return 0;
}

int xhci64_reset_rp1(uintptr_t rp1_base,
	struct XHCI64_RESET_RESULT results[2])
{
	struct XHCI64_REGISTERS regs;
	int status;

	if (results == NULL ||
			register_layout64(controller_capability64(rp1_base, RP1_USB0_BASE), &regs) != 0 ||
			register_layout64(controller_capability64(rp1_base, RP1_USB1_BASE), &regs) != 0) {
		return -1;
	}
	xhci64_select_controller(0);
	xhci_states[0].started = 0;
	xhci_states[1].started = 0;
	status = reset_controller64(controller_capability64(rp1_base, RP1_USB0_BASE), &results[0], 0);
	if (status != 0) {
		return -10 + status;
	}
	status = reset_controller64(controller_capability64(rp1_base, RP1_USB1_BASE), &results[1], 1);
	if (status != 0) {
		return -20 + status;
	}
	return 0;
}

static int start_selected_controller64(uintptr_t rp1_base,
	struct XHCI64_START_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	uintptr_t interrupter;
	uint32_t hcsparams1;
	uint32_t scratchpads;
	uint64_t command_dma;
	uint64_t event_dma;
	unsigned int i;

	if (register_layout64(capability, &regs) != 0) { return -2; }

	if (result == NULL || !dma_state_valid64()) {
		return -1;
	}
	result->controller_id = xhci_controller_id;
	hcsparams1 = regs.hcsparams1;
	result->hcsparams2 = *(volatile uint32_t *)
		(capability + XHCI_HCSPARAMS2);
	scratchpads = (((result->hcsparams2 >> 21) & 0x1fU) << 5) |
		((result->hcsparams2 >> 27) & 0x1fU);
	if ((hcsparams1 & 0xffU) < XHCI_MAX_SLOTS ||
			scratchpads > XHCI_MAX_SCRATCHPADS) {
		return -2;
	}

	xhci_states[xhci_controller_id].started = 0;
	xhci_states[xhci_controller_id].active_port = 0;
	xhci_states[xhci_controller_id].active_slot = 0;
	xhci_states[xhci_controller_id].enabled_slot = 0;
	xhci_states[xhci_controller_id].enabled_port = 0;
	xhci_states[xhci_controller_id].selected_port = 0;
	for (size_t word = 0; word < XHCI_PORT_MASK_WORDS64; word++) {
		xhci_states[xhci_controller_id].quarantined_ports[word] = 0;
	}
	xhci_states[xhci_controller_id].ep0_ready = 0;
	xhci_states[xhci_controller_id].interrupt_ready = 0;
	xhci_states[xhci_controller_id].capability = capability;
	xhci_states[xhci_controller_id].context_dwords = regs.context_dwords;
	zero_words64(xhci0_dcbaa, sizeof(xhci0_dcbaa));
	zero_words64(xhci0_command_ring, sizeof(xhci0_command_ring));
	zero_words64(xhci0_event_ring, sizeof(xhci0_event_ring));
	zero_words64(&xhci0_erst, sizeof(xhci0_erst));
	zero_words64(xhci0_scratchpad_array, sizeof(xhci0_scratchpad_array));
	xhci0_command_enqueue = 0;
	xhci0_command_cycle = 1;
	xhci0_event_dequeue = 0;
	xhci0_event_cycle = 1;
	xhci0_ep0_enqueue = 0;
	xhci0_ep0_cycle = 1;
	xhci0_interrupt_enqueue = 0;
	xhci0_interrupt_cycle = 1;
	xhci0_interrupt_pending_pointer = 0;
	if (scratchpads != 0) {
		for (i = 0; i < scratchpads; i++) {
			zero_words64(xhci0_scratchpads[i],
				sizeof(xhci0_scratchpads[i]));
			xhci0_scratchpad_array[i] =
				rp1_dma_address64(xhci0_scratchpads[i]);
			xhci_clean_cache64(xhci0_scratchpads[i],
				sizeof(xhci0_scratchpads[i]));
		}
		xhci0_dcbaa[0] = rp1_dma_address64(xhci0_scratchpad_array);
	}
	command_dma = rp1_dma_address64(xhci0_command_ring);
	event_dma = rp1_dma_address64(xhci0_event_ring);
	xhci0_command_ring[XHCI_RING_TRBS - 1].parameter = command_dma;
	xhci0_command_ring[XHCI_RING_TRBS - 1].control = XHCI_TRB_TYPE_LINK |
		XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
	xhci0_erst.address = event_dma;
	xhci0_erst.size = XHCI_RING_TRBS;

	xhci_clean_cache64(xhci0_dcbaa, sizeof(xhci0_dcbaa));
	xhci_clean_cache64(xhci0_command_ring, sizeof(xhci0_command_ring));
	xhci_clean_cache64(xhci0_event_ring, sizeof(xhci0_event_ring));
	xhci_clean_cache64(&xhci0_erst, sizeof(xhci0_erst));
	xhci_clean_cache64(xhci0_scratchpad_array, sizeof(xhci0_scratchpad_array));

	operational = regs.operational;
	interrupter = regs.interrupter;
	*(volatile uint32_t *) (operational + XHCI_CONFIG) = XHCI_MAX_SLOTS;
	*(volatile uint64_t *) (operational + XHCI_DCBAAP) =
		rp1_dma_address64(xhci0_dcbaa);
	*(volatile uint64_t *) (operational + XHCI_CRCR) =
		command_dma | XHCI_TRB_CYCLE;
	*(volatile uint32_t *) (interrupter + XHCI_IMAN) = 1U;
	*(volatile uint32_t *) (interrupter + XHCI_IMOD) = 0;
	*(volatile uint32_t *) (interrupter + XHCI_ERSTSZ) = 1U;
	*(volatile uint64_t *) (interrupter + XHCI_ERSTBA) =
		rp1_dma_address64(&xhci0_erst);
	*(volatile uint64_t *) (interrupter + XHCI_ERDP) = event_dma;
	xhci_barrier64();

	*(volatile uint32_t *) (operational + XHCI_USBCMD) = XHCI_USBCMD_RUN;
	xhci_barrier64();
	if (wait_bits64((volatile uint32_t *) (operational + XHCI_USBSTS),
			XHCI_USBSTS_HCH, 0, xhci_controller_id, "controller-run", "USBSTS") != 0) {
		result->command = *(volatile uint32_t *)
			(operational + XHCI_USBCMD);
		result->status = *(volatile uint32_t *)
			(operational + XHCI_USBSTS);
		return -3;
	}
	result->command = *(volatile uint32_t *)
		(operational + XHCI_USBCMD);
	result->status = *(volatile uint32_t *)
		(operational + XHCI_USBSTS);
	for (i = 0; i < 3; i++) {
		result->port_status[i] = i < regs.port_count ? *(volatile uint32_t *)
			(operational + XHCI_PORTSC_BASE + i * XHCI_PORT_STRIDE) : 0;
	}
	xhci_states[xhci_controller_id].started = 1;
	return 0;
}

int xhci64_start_controller(uintptr_t rp1_base, uint32_t controller_id,
	struct XHCI64_START_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uint32_t offset = controller_id == 0 ? RP1_USB0_BASE : RP1_USB1_BASE;

	if (controller_id > 1U || result == NULL ||
			register_layout64(controller_capability64(rp1_base, offset), &regs) != 0 ||
			xhci64_select_controller(controller_id) != 0) {
		return -1;
	}
	return start_selected_controller64(rp1_base, result);
}

int xhci64_start_rp1(uintptr_t rp1_base,
	struct XHCI64_START_RESULT *result)
{
	uint32_t controller_id;
	int connected;

	/* Prefer the keyboard controller when both hosts have a device. */
	connected = controller_has_connection64(controller_capability64(rp1_base, RP1_USB1_BASE));

	if (connected < 0) { return -1; }
	controller_id = connected ? 1U : 0U;
	return xhci64_start_controller(rp1_base, controller_id, result);
}

int xhci64_port_inventory(uintptr_t rp1_base,
	struct XHCI64_PORT_INVENTORY inventory[2])
{
	static const uint32_t offsets[2] = { RP1_USB0_BASE, RP1_USB1_BASE };
	uint32_t controller;

	if (inventory == NULL) {
		return -1;
	}
	for (controller = 0; controller < 2U; controller++) {
		uintptr_t capability = controller_capability64(rp1_base, offsets[controller]);
		struct XHCI64_REGISTERS regs;
		uintptr_t operational;
		uint32_t count;
		uint32_t port;

		if (register_layout64(capability, &regs) != 0) { return -1; }
		count = regs.port_count;
		operational = regs.operational;
		inventory[controller].port_count = count;
		inventory[controller].connected_mask = 0;
		inventory[controller].enabled_mask = 0;
		for (port = 0; port < XHCI64_INVENTORY_PORTS; port++) {
			uint32_t status = 0;

			if (port < count) {
				status = *(volatile uint32_t *) (operational +
					XHCI_PORTSC_BASE + port * XHCI_PORT_STRIDE);
				if ((status & XHCI_PORTSC_CCS) != 0) {
					inventory[controller].connected_mask |= 1U << port;
				}
				if ((status & XHCI_PORTSC_PED) != 0) {
					inventory[controller].enabled_mask |= 1U << port;
				}
			}
			inventory[controller].port_status[port] = status;
		}
	}
	return 0;
}

int xhci64_noop_command(uintptr_t rp1_base,
	struct XHCI64_COMMAND_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	uintptr_t interrupter;
	volatile uint32_t *doorbell;
	uint64_t command_pointer;

	if (active_layout64(capability, &regs) == 0) { return -1; }

	if (result == NULL || !xhci_command_ready64()) {
		return -1;
	}
	operational = regs.operational;
	interrupter = regs.interrupter;
	doorbell = (volatile uint32_t *) regs.doorbells;

	command_pointer = rp1_dma_address64(
		&xhci0_command_ring[xhci0_command_enqueue]);
	xhci0_command_ring[xhci0_command_enqueue].parameter = 0;
	xhci0_command_ring[xhci0_command_enqueue].status = 0;
	xhci0_command_ring[xhci0_command_enqueue].control =
		XHCI_TRB_TYPE_NOOP_COMMAND | xhci0_command_cycle;
	xhci_clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	xhci_barrier64();
	return wait_command_completion64(operational, interrupter,
		command_pointer, result, NULL);
}

int xhci64_enable_slot(uintptr_t rp1_base,
	struct XHCI64_SLOT_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	uintptr_t interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	int status;

	if (active_layout64(capability, &regs) == 0) { return -1; }

	if (result == NULL || xhci_states[xhci_controller_id].enabled_slot != 0 ||
			xhci_states[xhci_controller_id].selected_port > regs.port_count ||
			port_isolated64(xhci_states[xhci_controller_id].selected_port) ||
			!xhci_command_ready64()) {
		return -1;
	}
	result->slot_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	operational = regs.operational;
	interrupter = regs.interrupter;
	doorbell = (volatile uint32_t *) regs.doorbells;
	command_result.event_status = 0;
	command_result.event_control = 0;
	command_result.command_pointer_low = 0;
	command_result.controller_status = 0;

	command_pointer = rp1_dma_address64(
		&xhci0_command_ring[xhci0_command_enqueue]);
	xhci0_command_ring[xhci0_command_enqueue].parameter = 0;
	xhci0_command_ring[xhci0_command_enqueue].status = 0;
	xhci0_command_ring[xhci0_command_enqueue].control =
		XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND | xhci0_command_cycle;
	xhci_clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	xhci_barrier64();
	status = wait_command_completion64(operational, interrupter,
		command_pointer, &command_result, &result->slot_id);
	result->event_status = command_result.event_status;
	result->event_control = command_result.event_control;
	result->command_pointer_low = command_result.command_pointer_low;
	if (status != 0) {
		return status;
	}
	if (result->slot_id == 0 || result->slot_id > XHCI_MAX_SLOTS) {
		return -4;
	}
	xhci_states[xhci_controller_id].enabled_slot = result->slot_id;
	xhci_states[xhci_controller_id].enabled_port = xhci_states[xhci_controller_id].selected_port;
	return 0;
}

int xhci64_address_device(uintptr_t rp1_base, uint32_t port_id,
	uint32_t port_speed, uint32_t slot_id,
	struct XHCI64_ADDRESS_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	uintptr_t interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	uint64_t ep0_dma;
	uint32_t *slot_context;
	uint32_t *ep0_context;
	uint32_t context_dwords;
	uint32_t max_packet;
	int status;

	if (active_layout64(capability, &regs) == 0) { return -1; }

	if (result == NULL || port_id == 0 || slot_id == 0 ||
			slot_id > XHCI_MAX_SLOTS || port_speed == 0 || port_speed > 5U ||
			port_id > regs.port_count || port_isolated64(port_id) ||
			xhci_states[xhci_controller_id].active_slot != 0 ||
			(xhci_states[xhci_controller_id].enabled_slot != 0 &&
			 (xhci_states[xhci_controller_id].enabled_slot != slot_id ||
			  xhci_states[xhci_controller_id].enabled_port != port_id)) ||
			!context_valid64(regs.context_dwords, 1U) ||
			!xhci_command_ready64()) {
		return -1;
	}
	result->device_address = 0;
	result->slot_state = 0;
	result->context_size = 0;
	result->ep0_max_packet = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	context_dwords = regs.context_dwords;
	if (port_speed <= 2U) {
		max_packet = 8U;
	} else if (port_speed >= 4U) {
		max_packet = 512U;
	} else {
		max_packet = 64U;
	}
	/* Retain ownership even if Address Device fails after publishing DCBAA. */
	xhci_states[xhci_controller_id].active_port = port_id;
	xhci_states[xhci_controller_id].active_slot = slot_id;
	xhci_states[xhci_controller_id].enabled_slot = slot_id;
	xhci_states[xhci_controller_id].enabled_port = port_id;
	xhci_states[xhci_controller_id].selected_port = port_id;
	result->context_size = context_dwords * sizeof(uint32_t);
	result->ep0_max_packet = max_packet;
	zero_words64(xhci0_input_context, sizeof(xhci0_input_context));
	zero_words64(xhci0_device_context, sizeof(xhci0_device_context));
	xhci_states[xhci_controller_id].ep0_ready = 0;
	xhci0_ep0_enqueue = 0;
	xhci0_ep0_cycle = 1;
	zero_words64(xhci0_ep0_ring, sizeof(xhci0_ep0_ring));

	/* Add the Slot and Default Control Endpoint contexts. */
	xhci0_input_context[1] = 3U;
	slot_context = xhci0_input_context + context_dwords;
	ep0_context = xhci0_input_context + context_dwords * 2U;
	slot_context[0] = (port_speed << 20) | (1U << 27);
	slot_context[1] = port_id << 16;
	ep0_dma = rp1_dma_address64(xhci0_ep0_ring);
	ep0_context[1] = (3U << 1) | (4U << 3) | (max_packet << 16);
	ep0_context[2] = (uint32_t) ep0_dma | 1U;
	ep0_context[3] = (uint32_t) (ep0_dma >> 32);
	ep0_context[4] = 8U;
	xhci0_ep0_ring[XHCI_RING_TRBS - 1].parameter = ep0_dma;
	xhci0_ep0_ring[XHCI_RING_TRBS - 1].control = XHCI_TRB_TYPE_LINK |
		XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
	xhci0_dcbaa[slot_id] = rp1_dma_address64(xhci0_device_context);
	xhci_clean_cache64(xhci0_input_context, sizeof(xhci0_input_context));
	xhci_clean_cache64(xhci0_device_context, sizeof(xhci0_device_context));
	xhci_clean_cache64(xhci0_ep0_ring, sizeof(xhci0_ep0_ring));
	xhci_clean_cache64(&xhci0_dcbaa[slot_id], sizeof(xhci0_dcbaa[slot_id]));

	operational = regs.operational;
	interrupter = regs.interrupter;
	doorbell = (volatile uint32_t *) regs.doorbells;
	command_result.event_status = 0;
	command_result.event_control = 0;
	command_result.command_pointer_low = 0;
	command_result.controller_status = 0;
	command_pointer = rp1_dma_address64(
		&xhci0_command_ring[xhci0_command_enqueue]);
	xhci0_command_ring[xhci0_command_enqueue].parameter =
		rp1_dma_address64(xhci0_input_context);
	xhci0_command_ring[xhci0_command_enqueue].status = 0;
	xhci0_command_ring[xhci0_command_enqueue].control =
		(slot_id << 24) | XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND |
		xhci0_command_cycle;
	xhci_clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	xhci_barrier64();
	status = wait_command_completion64(operational, interrupter,
		command_pointer, &command_result, NULL);
	result->event_status = command_result.event_status;
	result->event_control = command_result.event_control;
	result->command_pointer_low = command_result.command_pointer_low;
	if (status != 0) {
		return status;
	}
	xhci_invalidate_cache64(xhci0_device_context,
		context_dwords * sizeof(uint32_t));
	result->device_address = xhci0_device_context[3] & 0xffU;
	result->slot_state = (xhci0_device_context[3] >> 27) & 0x1fU;
	if (result->device_address == 0 || result->slot_state != 2U) {
		return -4;
	}
	xhci_states[xhci_controller_id].ep0_ready = 1;
	return 0;
}

/* Preserve legacy wait helpers internally; the module bridge uses errno. */
static int transfer_status64(int status)
{
	return status == -2 ? -110 : status == 0 ? 0 : -5;
}

int xhci_control_ready64(uint32_t slot_id, uint32_t trbs)
{
	return rings_valid64() && xhci_states[xhci_controller_id].ep0_ready &&
		slot_usable64(slot_id) && (trbs == 2U || trbs == 3U) &&
		trbs < XHCI_RING_TRBS - 1U - xhci0_ep0_enqueue;
}

int xhci_command_ready64(void)
{
	return rings_valid64() && xhci0_command_enqueue < XHCI_RING_TRBS - 1U;
}

int xhci_interrupt_ready64(void)
{
	return rings_valid64() && xhci_states[xhci_controller_id].interrupt_ready &&
		xhci0_interrupt_enqueue < XHCI_RING_TRBS - 1U;
}

int xhci_control_read64(uintptr_t rp1_base, uint32_t slot_id, uint64_t setup,
	uint8_t *data, uint32_t length, struct XHCI64_TRANSFER_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t interrupter;
	volatile uint32_t *doorbells;
	uint64_t buffer_dma;
	uint64_t status_pointer;
	uint32_t index;
	int status;

	if (active_layout64(capability, &regs) == 0) { return -22; }

	if (data == NULL || result == NULL || length == 0 ||
			length > sizeof(xhci0_descriptor_buffer) ||
			!xhci_control_ready64(slot_id, 3U)) {
		return -22;
	}
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	zero_words64(xhci0_descriptor_buffer,
		sizeof(xhci0_descriptor_buffer));
	buffer_dma = rp1_dma_address64(xhci0_descriptor_buffer);
	index = xhci0_ep0_enqueue;

	/* The USB layer supplies the setup packet; DMA remains controller-owned. */
	xhci0_ep0_ring[index].parameter = setup;
	xhci0_ep0_ring[index].status = 8U;
	xhci0_ep0_ring[index].control = XHCI_TRB_TYPE_SETUP_STAGE |
		XHCI_TRB_IDT | XHCI_TRB_TRT_IN | xhci0_ep0_cycle;
	index++;
	xhci0_ep0_ring[index].parameter = buffer_dma;
	xhci0_ep0_ring[index].status = length;
	xhci0_ep0_ring[index].control = XHCI_TRB_TYPE_DATA_STAGE |
		XHCI_TRB_DIR_IN | xhci0_ep0_cycle;
	index++;
	xhci0_ep0_ring[index].parameter = 0;
	xhci0_ep0_ring[index].status = 0;
	xhci0_ep0_ring[index].control = XHCI_TRB_TYPE_STATUS_STAGE |
		XHCI_TRB_IOC | xhci0_ep0_cycle;
	status_pointer = rp1_dma_address64(&xhci0_ep0_ring[index]);
	index++;
	xhci_clean_cache64(xhci0_descriptor_buffer,
		sizeof(xhci0_descriptor_buffer));
	xhci_clean_cache64(&xhci0_ep0_ring[xhci0_ep0_enqueue],
		3U * sizeof(struct XHCI64_TRB));
	xhci0_ep0_enqueue = index;
	interrupter = regs.interrupter;
	doorbells = (volatile uint32_t *) regs.doorbells;
	doorbells[slot_id] = 1U;
	xhci_barrier64();
	status = wait_transfer_completion64(interrupter, status_pointer,
		slot_id, 1U, XHCI_TIMEOUT_MS, result);
	if (status != 0) {
		return transfer_status64(status);
	}
	xhci_invalidate_cache64(xhci0_descriptor_buffer, length);
	for (uint32_t byte = 0; byte < length; byte++) {
		data[byte] = xhci0_descriptor_buffer[byte];
	}
	return 0;
}

int xhci_control_write64(uintptr_t rp1_base, uint32_t slot_id, uint64_t setup)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t interrupter;
	volatile uint32_t *doorbells;
	struct XHCI64_TRANSFER_RESULT transfer;
	uint64_t status_pointer;
	uint32_t index;

	if (active_layout64(capability, &regs) == 0) { return -22; }

	if (!xhci_control_ready64(slot_id, 2U)) {
		return -22;
	}
	index = xhci0_ep0_enqueue;
	xhci0_ep0_ring[index].parameter = setup;
	xhci0_ep0_ring[index].status = 8U;
	xhci0_ep0_ring[index].control = XHCI_TRB_TYPE_SETUP_STAGE |
		XHCI_TRB_IDT | xhci0_ep0_cycle;
	index++;
	xhci0_ep0_ring[index].parameter = 0;
	xhci0_ep0_ring[index].status = 0;
	xhci0_ep0_ring[index].control = XHCI_TRB_TYPE_STATUS_STAGE |
		XHCI_TRB_DIR_IN | XHCI_TRB_IOC | xhci0_ep0_cycle;
	status_pointer = rp1_dma_address64(&xhci0_ep0_ring[index]);
	index++;
	xhci_clean_cache64(&xhci0_ep0_ring[xhci0_ep0_enqueue],
		2U * sizeof(struct XHCI64_TRB));
	xhci0_ep0_enqueue = index;
	interrupter = regs.interrupter;
	doorbells = (volatile uint32_t *) regs.doorbells;
	doorbells[slot_id] = 1U;
	xhci_barrier64();
	return transfer_status64(wait_transfer_completion64(interrupter, status_pointer,
		slot_id, 1U, XHCI_TIMEOUT_MS, &transfer));
}

int xhci_configure_interrupt64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint32_t interval,
	struct XHCI64_CONFIGURE_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	uintptr_t interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	uint64_t interrupt_dma;
	uint32_t *slot_context;
	uint32_t *endpoint_context;
	uint32_t context_dwords;
	uint32_t i;
	int status;

	if (active_layout64(capability, &regs) == 0 || !slot_usable64(slot_id)) { return -22; }

	if (result == NULL || slot_id == 0 || slot_id > XHCI_MAX_SLOTS ||
			endpoint_id < 3U || endpoint_id > 31U || (endpoint_id & 1U) == 0 ||
			packet < 3U || packet > XHCI64_INTERRUPT_BYTES || interval > 15U ||
			xhci0_interrupt_pending_pointer != 0 || !context_valid64(regs.context_dwords, endpoint_id) ||
			!xhci_command_ready64()) {
		return -22;
	}
	result->endpoint_id = endpoint_id;
	result->interval = interval;
	context_dwords = regs.context_dwords;
	zero_words64(xhci0_input_context, sizeof(xhci0_input_context));
	xhci_states[xhci_controller_id].interrupt_ready = 0;
	zero_words64(xhci0_interrupt_ring, sizeof(xhci0_interrupt_ring));
	xhci_invalidate_cache64(xhci0_device_context,
		(endpoint_id + 1U) * context_dwords * sizeof(uint32_t));
	xhci0_input_context[1] = 1U | (1U << endpoint_id);
	slot_context = xhci0_input_context + context_dwords;
	for (i = 0; i < context_dwords; i++) {
		slot_context[i] = xhci0_device_context[i];
	}
	slot_context[0] &= ~(0x1fU << 27);
	slot_context[0] |= endpoint_id << 27;
	endpoint_context = xhci0_input_context +
		(endpoint_id + 1U) * context_dwords;
	interrupt_dma = rp1_dma_address64(xhci0_interrupt_ring);
	endpoint_context[0] = interval << 16;
	endpoint_context[1] = (3U << 1) | (7U << 3) |
		(packet << 16);
	endpoint_context[2] = (uint32_t) interrupt_dma | 1U;
	endpoint_context[3] = (uint32_t) (interrupt_dma >> 32);
	endpoint_context[4] = packet |
		(packet << 16);
	xhci0_interrupt_ring[XHCI_RING_TRBS - 1].parameter = interrupt_dma;
	xhci0_interrupt_ring[XHCI_RING_TRBS - 1].control = XHCI_TRB_TYPE_LINK |
		XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
	xhci_clean_cache64(xhci0_input_context, sizeof(xhci0_input_context));
	xhci_clean_cache64(xhci0_interrupt_ring, sizeof(xhci0_interrupt_ring));
	xhci0_interrupt_enqueue = 0;
	xhci0_interrupt_cycle = 1;

	operational = regs.operational;
	interrupter = regs.interrupter;
	doorbell = (volatile uint32_t *) regs.doorbells;
	command_result.event_status = 0;
	command_result.event_control = 0;
	command_result.command_pointer_low = 0;
	command_result.controller_status = 0;
	command_pointer = rp1_dma_address64(
		&xhci0_command_ring[xhci0_command_enqueue]);
	xhci0_command_ring[xhci0_command_enqueue].parameter =
		rp1_dma_address64(xhci0_input_context);
	xhci0_command_ring[xhci0_command_enqueue].status = 0;
	xhci0_command_ring[xhci0_command_enqueue].control =
		(slot_id << 24) | XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND |
		xhci0_command_cycle;
	xhci_clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	xhci_barrier64();
	status = wait_command_completion64(operational, interrupter,
		command_pointer, &command_result, NULL);
	result->event_status = command_result.event_status;
	result->event_control = command_result.event_control;
	result->command_pointer_low = command_result.command_pointer_low;
	if (status != 0) {
		return -5;
	}
	xhci_invalidate_cache64(xhci0_device_context + endpoint_id * context_dwords,
		context_dwords * sizeof(uint32_t));
	result->endpoint_state =
		xhci0_device_context[endpoint_id * context_dwords] & 7U;
	if (result->endpoint_state != 1U) {
		return -71;
	}
	xhci_states[xhci_controller_id].interrupt_ready = 1;
	return 0;
}

int xhci_interrupt_read64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint32_t timeout_ms,
	uint8_t *data, struct XHCI64_TRANSFER_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t interrupter;
	volatile uint32_t *doorbells;
	uint64_t report_dma;
	uint64_t trb_pointer;
	uint32_t index;
	int status;

	if (active_layout64(capability, &regs) == 0 || !slot_usable64(slot_id)) { return -22; }

	if (data == NULL || result == NULL || slot_id == 0 || slot_id > XHCI_MAX_SLOTS ||
			endpoint_id < 3U || endpoint_id > 31U || (endpoint_id & 1U) == 0 ||
			packet < 3U || packet > XHCI64_INTERRUPT_BYTES ||
			xhci0_interrupt_pending_pointer != 0 || !xhci_interrupt_ready64()) {
		return -22;
	}
	interrupter = regs.interrupter;
	doorbells = (volatile uint32_t *) regs.doorbells;
	report_dma = rp1_dma_address64(xhci0_interrupt_report);
	zero_words64(xhci0_interrupt_report,
		sizeof(xhci0_interrupt_report));
	index = xhci0_interrupt_enqueue;
	xhci0_interrupt_ring[index].parameter = report_dma;
	xhci0_interrupt_ring[index].status = packet;
	xhci0_interrupt_ring[index].control = XHCI_TRB_TYPE_NORMAL |
		XHCI_TRB_IOC | xhci0_interrupt_cycle;
	trb_pointer = rp1_dma_address64(&xhci0_interrupt_ring[index]);
	xhci_clean_cache64(xhci0_interrupt_report,
		sizeof(xhci0_interrupt_report));
	xhci_clean_cache64(&xhci0_interrupt_ring[index],
		sizeof(xhci0_interrupt_ring[index]));
	if (advance_interrupt_ring64() != 0) { return -22; }
	doorbells[slot_id] = endpoint_id;
	xhci_barrier64();
	status = wait_transfer_completion64(interrupter, trb_pointer,
		slot_id, endpoint_id, timeout_ms, result);
	if (status != 0) {
		return transfer_status64(status);
	}
	xhci_invalidate_cache64(xhci0_interrupt_report,
		packet);
	for (uint32_t byte = 0; byte < packet; byte++) {
		data[byte] = xhci0_interrupt_report[byte];
	}
	return 0;
}

int xhci_interrupt_arm64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	volatile uint32_t *doorbells;
	uint32_t index;

	if (active_layout64(capability, &regs) == 0 || !slot_usable64(slot_id)) { return -22; }

	if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS ||
			endpoint_id < 3U || endpoint_id > 31U || (endpoint_id & 1U) == 0 ||
			xhci0_interrupt_pending_pointer != 0 ||
			packet < 3U ||
			packet > sizeof(xhci0_interrupt_report) ||
			!xhci_interrupt_ready64()) {
		return -22;
	}
	zero_words64(xhci0_interrupt_report, sizeof(xhci0_interrupt_report));
	index = xhci0_interrupt_enqueue;
	xhci0_interrupt_ring[index].parameter =
		rp1_dma_address64(xhci0_interrupt_report);
	xhci0_interrupt_ring[index].status = packet;
	xhci0_interrupt_ring[index].control = XHCI_TRB_TYPE_NORMAL |
		XHCI_TRB_IOC | xhci0_interrupt_cycle;
	xhci0_interrupt_pending_pointer =
		rp1_dma_address64(&xhci0_interrupt_ring[index]);
	xhci_states[xhci_controller_id].interrupt_pending_slot = slot_id;
	xhci_states[xhci_controller_id].interrupt_pending_endpoint = endpoint_id;
	xhci_states[xhci_controller_id].interrupt_pending_length = packet;
	xhci_clean_cache64(xhci0_interrupt_report, sizeof(xhci0_interrupt_report));
	xhci_clean_cache64(&xhci0_interrupt_ring[index],
		sizeof(xhci0_interrupt_ring[index]));
	if (advance_interrupt_ring64() != 0) { return -22; }
	doorbells = (volatile uint32_t *) regs.doorbells;
	doorbells[slot_id] = endpoint_id;
	xhci_barrier64();
	return 0;
}

static int interrupt_failure64(const struct XHCI64_REGISTERS *regs,
	const struct XHCI64_TRB *event, uint32_t status, uint32_t control, int error)
{
	aarch64_poll_fail64(xhci_device64(xhci_controller_id), "interrupt-event", "TRB_STATUS_CTRL",
		(uintptr_t) event + offsetof(struct XHCI64_TRB, status),
		((uint64_t) status << 32) | control, error);
	aarch64_poll_fail64(xhci_device64(xhci_controller_id), "interrupt-event", "USBSTS",
		regs->operational + XHCI_USBSTS,
		*(volatile uint32_t *) (regs->operational + XHCI_USBSTS), error);
	return error;
}

int xhci_interrupt_poll64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint8_t *report,
	size_t report_length)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t interrupter;
	struct XHCI64_TRB *event;
	uint64_t event_pointer;
	uint32_t actual_length;
	uint32_t completion_code;
	uint32_t control;
	uint32_t i;
	uint32_t residual_length;
	uint32_t event_status;
	uint32_t type;

	if (active_layout64(capability, &regs) == 0 || !slot_usable64(slot_id)) { return -22; }

	if (report == NULL || report_length == 0 ||
			report_length > sizeof(xhci0_interrupt_report) || slot_id == 0 ||
			packet < 3U || packet > sizeof(xhci0_interrupt_report) ||
			slot_id > XHCI_MAX_SLOTS || endpoint_id < 3U || endpoint_id > 31U || (endpoint_id & 1U) == 0 ||
			!transfer_pointer64(xhci0_interrupt_pending_pointer, endpoint_id) ||
			xhci0_interrupt_ring[xhci0_interrupt_enqueue == 0 ? XHCI_RING_TRBS - 2U :
				xhci0_interrupt_enqueue - 1U].status != packet ||
			xhci_states[xhci_controller_id].interrupt_pending_slot != slot_id ||
			xhci_states[xhci_controller_id].interrupt_pending_endpoint != endpoint_id ||
			xhci_states[xhci_controller_id].interrupt_pending_length != packet) {
		return -22;
	}
	interrupter = regs.interrupter;
	for (i = 0; i < XHCI_RING_TRBS; i++) {
		if (!rings_valid64()) { return -22; }
		event = &xhci0_event_ring[xhci0_event_dequeue];
		xhci_invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			return 0;
		}
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		event_status = event->status;
		completion_code = event_status >> 24;
		if (advance_event_ring64(interrupter) != 0) { return -5; }
		if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
			continue;
		}
		residual_length = event_status & 0x00ffffffU;
		if ((control & XHCI_TRB_EVENT_DATA64) != 0 ||
				(completion_code != 1U && completion_code != 13U) ||
				residual_length > packet ||
				((control >> 16) & 0x1fU) != endpoint_id ||
				(control >> 24) != slot_id ||
				!transfer_pointer64(event_pointer, endpoint_id) ||
				event_pointer != xhci0_interrupt_pending_pointer) {
			return interrupt_failure64(&regs, event, event_status, control, -5);
		}
		actual_length = packet - residual_length;
		if (actual_length == 0U) {
			return interrupt_failure64(&regs, event, event_status, control, -71);
		}
		if (actual_length > report_length) {
			actual_length = (uint32_t) report_length;
		}
		xhci_invalidate_cache64(xhci0_interrupt_report,
			packet);
		for (size_t byte = 0; byte < report_length; byte++) {
			report[byte] = byte < actual_length ?
				xhci0_interrupt_report[byte] : 0;
		}
		xhci0_interrupt_pending_pointer = 0;
		return (int) actual_length;
	}
	return 0;
}

int xhci64_reset_connected_port(uintptr_t rp1_base,
	struct XHCI64_PORT_RESULT *result)
{
	struct XHCI64_REGISTERS regs;
	uintptr_t capability = xhci_capability64(rp1_base);
	uintptr_t operational;
	volatile uint32_t *portsc;
	struct POLL_DEADLINE64 deadline;
	uint32_t hcsparams1;
	uint32_t port_count;
	uint32_t status;
	uint32_t reset_bit;
	uint32_t completion_bit;
	uint32_t port;

	if (active_layout64(capability, &regs) == 0) { return -1; }

	if (result == NULL) {
		return -1;
	}
	xhci_states[xhci_controller_id].selected_port = 0;
	result->port_id = 0;
	result->protocol_major = 0;
	result->status_before = 0;
	result->status_after = 0;
	hcsparams1 = regs.hcsparams1;
	port_count = (hcsparams1 >> 24) & 0xffU;
	operational = regs.operational;

	if (xhci_deadline64(&deadline, XHCI_PORT_CONNECT_TIMEOUT_MS, xhci_controller_id,
			"port-connect") != 0) { return -1; }
	portsc = NULL;
	while (!poll_deadline_expired64(&deadline, xhci_counter64())) {
		for (port = 1; port <= port_count; port++) {
			volatile uint32_t *candidate = (volatile uint32_t *)
				(operational + XHCI_PORTSC_BASE +
				(port - 1U) * XHCI_PORT_STRIDE);

			status = *candidate;
			if (!port_isolated64(port) && (status & XHCI_PORTSC_CCS) != 0) {
				portsc = candidate;
				result->port_id = port;
				result->status_before = status;
				break;
			}
		}
		if (portsc != NULL) {
			break;
		}
		xhci_relax64();
	}
	if (portsc == NULL) {
		/* Capture every actual port: a last-port-only status hides earlier ports. */
		for (port = 1; port <= port_count; port++) {
			uintptr_t address = operational + XHCI_PORTSC_BASE + (port - 1U) * XHCI_PORT_STRIDE;

			aarch64_poll_fail64(xhci_device64(xhci_controller_id), "port-connect", "PORTSC",
				address, *(volatile uint32_t *) address, -110);
		}
		return -2;
	}

	result->protocol_major = port_protocol_major64(capability,
		result->port_id);
	if (result->protocol_major == 0) {
		return -3;
	}
	if (result->protocol_major >= 3U) {
		reset_bit = XHCI_PORTSC_WPR;
		completion_bit = XHCI_PORTSC_WRC;
	} else {
		reset_bit = XHCI_PORTSC_PR;
		completion_bit = XHCI_PORTSC_PRC;
	}
	*portsc = (result->status_before & XHCI_PORTSC_NEUTRAL_MASK) |
		reset_bit;
	xhci_barrier64();
	if (wait_bits64(portsc, reset_bit, 0, xhci_controller_id, "port-reset", "PORTSC") != 0) {
		result->status_after = *portsc;
		return -4;
	}
	if (delay_xhci64(XHCI_PORT_RESET_RECOVERY_MS) != 0) { return -4; }
	result->status_after = *portsc;
	if ((result->status_after & completion_bit) == 0) {
		return -5;
	}
	if ((result->status_after & (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) !=
			(XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) {
		return -6;
	}
	if ((result->status_after & XHCI_PORTSC_PP) == 0 ||
			(result->status_after & XHCI_PORTSC_SPEED_MASK) == 0) {
		return -7;
	}
	xhci_states[xhci_controller_id].selected_port = result->port_id;
	return 0;
}

/* Disable only the root port; do not stop/reset either host controller. DMA
 * buffers stay owned until Disable Slot acknowledges retirement. On failure
 * quarantine remains set and existing ownership is retained for bounded retry.
 */
int xhci_port_quarantine64(uintptr_t rp1_base, uint32_t port_id)
{
	struct XHCI64_REGISTERS regs;
	struct XHCI64_CONTROLLER_STATE *state;
	struct XHCI64_COMMAND_RESULT result = { 0 };
	uintptr_t capability = xhci_capability64(rp1_base);
	volatile uint32_t *portsc;
	uint32_t status;
	uint32_t index;
	uint64_t pointer;
	int failure;

	if (!selected_valid64() || xhci_states[xhci_controller_id].capability != capability ||
			register_layout64(capability, &regs) != 0 || port_id == 0 ||
			port_id > regs.port_count) { return -22; }
	state = &xhci_states[xhci_controller_id];
	state->quarantined_ports[(port_id - 1U) / 32U] |= 1U << ((port_id - 1U) % 32U);
	portsc = (volatile uint32_t *) (regs.operational + XHCI_PORTSC_BASE +
		(port_id - 1U) * XHCI_PORT_STRIDE);
	status = *portsc;
	if (status & XHCI_PORTSC_PED) {
		/* PED is RW1CS. Preserve PP and clear reset/change/write-strobe bits. */
		*portsc = (status & XHCI_PORTSC_NEUTRAL_MASK) | XHCI_PORTSC_PED;
		xhci_barrier64();
	}
	if (wait_bits64(portsc, XHCI_PORTSC_PED, 0, xhci_controller_id, "port-disable", "PORTSC") != 0) { return -110; }
	if (state->enabled_port != port_id || state->enabled_slot == 0) { return 0; }
	if (!xhci_command_ready64()) { return -22; }
	index = xhci0_command_enqueue;
	pointer = rp1_dma_address64(&xhci0_command_ring[index]);
	xhci0_command_ring[index].parameter = 0;
	xhci0_command_ring[index].status = 0;
	xhci0_command_ring[index].control = (state->enabled_slot << 24) |
		XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND | xhci0_command_cycle;
	xhci_clean_cache64(&xhci0_command_ring[index], sizeof(xhci0_command_ring[index]));
	xhci0_command_enqueue++;
	*(volatile uint32_t *) regs.doorbells = 0;
	xhci_barrier64();
	failure = wait_command_completion64(regs.operational, regs.interrupter,
		pointer, &result, NULL);
	if (failure != 0) { return transfer_status64(failure); }
	/* The device can no longer DMA through these contexts. Leave memory static. */
	xhci0_dcbaa[state->enabled_slot] = 0;
	xhci_clean_cache64(&xhci0_dcbaa[state->enabled_slot], sizeof(uint64_t));
	state->ep0_ready = 0;
	state->interrupt_ready = 0;
	state->interrupt_pending_pointer = 0;
	state->interrupt_pending_slot = 0;
	state->interrupt_pending_endpoint = 0;
	state->interrupt_pending_length = 0;
	state->active_slot = 0;
	state->active_port = 0;
	state->enabled_slot = 0;
	state->enabled_port = 0;
	if (state->selected_port == port_id) { state->selected_port = 0; }
	return 0;
}
