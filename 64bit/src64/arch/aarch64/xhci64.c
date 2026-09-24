/* RP1's two DWC3 host blocks expose xHCI capability registers at offset 0. */
#include <arch/arch64.h>
#include <stdint.h>

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
#define XHCI_MAX_SLOTS 64U
#define XHCI_RING_TRBS 256U
#define XHCI_MAX_SCRATCHPADS 4U
#define XHCI_TRB_TYPE_LINK (6U << 10)
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
#define RP1_SYSTEM_RAM_ALIAS 0x1000000000ULL

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

static uint64_t xhci0_dcbaa[XHCI_MAX_SLOTS + 1]
	__attribute__((aligned(64)));
static struct XHCI64_TRB xhci0_command_ring[XHCI_RING_TRBS]
	__attribute__((aligned(4096)));
static struct XHCI64_TRB xhci0_event_ring[XHCI_RING_TRBS]
	__attribute__((aligned(4096)));
static struct XHCI64_ERST_ENTRY xhci0_erst __attribute__((aligned(64)));
static uint64_t xhci0_scratchpad_array[XHCI_MAX_SCRATCHPADS]
	__attribute__((aligned(64)));
static uint8_t xhci0_scratchpads[XHCI_MAX_SCRATCHPADS][4096]
	__attribute__((aligned(4096)));
static uint32_t xhci0_input_context[1024] __attribute__((aligned(4096)));
static uint32_t xhci0_device_context[1024] __attribute__((aligned(4096)));
static struct XHCI64_TRB xhci0_ep0_ring[XHCI_RING_TRBS]
	__attribute__((aligned(4096)));
static struct XHCI64_TRB xhci0_interrupt_ring[XHCI_RING_TRBS]
	__attribute__((aligned(4096)));
static uint8_t xhci0_descriptor_buffer[512] __attribute__((aligned(64)));
static uint8_t xhci0_keyboard_report[64] __attribute__((aligned(64)));
static uint32_t xhci0_command_enqueue;
static uint32_t xhci0_command_cycle;
static uint32_t xhci0_event_dequeue;
static uint32_t xhci0_event_cycle;
static uint32_t xhci0_ep0_enqueue;
static uint32_t xhci0_ep0_cycle;
static uint32_t xhci0_interrupt_enqueue;
static uint32_t xhci0_interrupt_cycle;
static uint64_t xhci0_keyboard_pending_pointer;
static uint32_t xhci_controller_offset = RP1_USB0_BASE;

static volatile uint8_t *xhci_capability64(uintptr_t rp1_base)
{
	return (volatile uint8_t *) (rp1_base + xhci_controller_offset);
}

static int controller_has_connection64(uintptr_t base)
{
	volatile uint8_t *capability = (volatile uint8_t *) base;
	volatile uint8_t *operational;
	uint32_t caplength;
	uint32_t hcsparams1;
	uint32_t port_count;
	uint32_t port;

	caplength = *(volatile uint32_t *) capability & 0xffU;
	hcsparams1 = *(volatile uint32_t *)
		(capability + XHCI_HCSPARAMS1);
	port_count = (hcsparams1 >> 24) & 0xffU;
	operational = capability + caplength;
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
	return RP1_SYSTEM_RAM_ALIAS +
		(uint64_t) arch64_virt_to_phys((uintptr_t) address);
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

static void clean_cache64(const void *address, size_t bytes)
{
	uintptr_t current;
	uintptr_t end;
	uint64_t ctr;
	uintptr_t line_size;

	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	line_size = 4ULL << ((ctr >> 16) & 0xfU);
	current = (uintptr_t) address & ~(line_size - 1);
	end = ((uintptr_t) address + bytes + line_size - 1) & ~(line_size - 1);
	for (; current < end; current += line_size) {
		__asm__ volatile ("dc cvac, %0" :: "r" (current) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void invalidate_cache64(const void *address, size_t bytes)
{
	uintptr_t current;
	uintptr_t end;
	uint64_t ctr;
	uintptr_t line_size;

	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	line_size = 4ULL << ((ctr >> 16) & 0xfU);
	current = (uintptr_t) address & ~(line_size - 1);
	end = ((uintptr_t) address + bytes + line_size - 1) & ~(line_size - 1);
	for (; current < end; current += line_size) {
		__asm__ volatile ("dc ivac, %0" :: "r" (current) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void advance_interrupt_ring64(void)
{
	xhci0_interrupt_enqueue++;
	if (xhci0_interrupt_enqueue == XHCI_RING_TRBS - 1U) {
		xhci0_interrupt_ring[XHCI_RING_TRBS - 1U].control =
			XHCI_TRB_TYPE_LINK | XHCI_TRB_TOGGLE_CYCLE |
			xhci0_interrupt_cycle;
		clean_cache64(&xhci0_interrupt_ring[XHCI_RING_TRBS - 1U],
			sizeof(xhci0_interrupt_ring[XHCI_RING_TRBS - 1U]));
		xhci0_interrupt_enqueue = 0;
		xhci0_interrupt_cycle ^= 1U;
	}
}

static uint64_t counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static int wait_bits64(volatile uint32_t *register_address, uint32_t mask,
	uint32_t expected)
{
	uint64_t deadline;
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000U) * XHCI_TIMEOUT_MS;
	while ((int64_t) (counter64() - deadline) < 0) {
		if ((*register_address & mask) == expected) {
			return 0;
		}
		__asm__ volatile ("yield");
	}
	return -1;
}

static void delay_xhci64(uint32_t milliseconds)
{
	uint64_t deadline;
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000U) * milliseconds;
	while ((int64_t) (counter64() - deadline) < 0) {
		__asm__ volatile ("yield");
	}
}

static uint32_t port_protocol_major64(volatile uint8_t *capability,
	uint32_t port_id)
{
	uint32_t hccparams1;
	uint32_t offset;
	unsigned int remaining;

	hccparams1 = *(volatile uint32_t *) (capability + XHCI_HCCPARAMS1);
	offset = ((hccparams1 >> 16) & 0xffffU) * 4U;
	for (remaining = 0; offset != 0 && remaining < 64U; remaining++) {
		volatile uint32_t *extended =
			(volatile uint32_t *) (capability + offset);
		uint32_t header = extended[0];
		uint32_t next = (header >> 8) & 0xffU;

		if ((header & 0xffU) == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
			uint32_t ports = extended[2];
			uint32_t first = ports & 0xffU;
			uint32_t count = (ports >> 8) & 0xffU;

			if (port_id >= first && port_id < first + count) {
				return header >> 24;
			}
		}
		if (next == 0) {
			break;
		}
		offset += next * 4U;
	}
	return 0;
}

static int probe_controller64(uintptr_t base, uint32_t *capability,
	uint32_t *hcsparams1)
{
	volatile uint8_t *registers = (volatile uint8_t *) base;
	uint32_t cap;
	uint32_t params;
	uint32_t caplength;
	uint32_t version;

	cap = *(volatile uint32_t *)
		(registers + XHCI_CAPLENGTH_HCIVERSION);
	params = *(volatile uint32_t *) (registers + XHCI_HCSPARAMS1);
	*capability = cap;
	*hcsparams1 = params;
	if (cap == 0xdeaddeadU || cap == 0xffffffffU || params == 0xdeaddeadU ||
			params == 0xffffffffU) {
		return -1;
	}
	caplength = cap & 0xffU;
	version = cap >> 16;
	if (caplength < 0x20U || (caplength & 3U) != 0 || version < 0x0100U ||
			version >= 0x0200U) {
		return -2;
	}
	return 0;
}

static void advance_event_ring64(volatile uint8_t *interrupter)
{
	xhci0_event_dequeue++;
	if (xhci0_event_dequeue == XHCI_RING_TRBS) {
		xhci0_event_dequeue = 0;
		xhci0_event_cycle ^= 1U;
	}
	*(volatile uint64_t *) (interrupter + XHCI_ERDP) =
		rp1_dma_address64(&xhci0_event_ring[xhci0_event_dequeue]) |
		XHCI_ERDP_EHB;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static int wait_command_completion64(volatile uint8_t *operational,
	volatile uint8_t *interrupter, uint64_t expected_pointer,
	struct XHCI64_COMMAND_RESULT *result, uint32_t *slot_id)
{
	uint64_t deadline;
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000U) * XHCI_TIMEOUT_MS;
	while ((int64_t) (counter64() - deadline) < 0) {
		struct XHCI64_TRB *event =
			&xhci0_event_ring[xhci0_event_dequeue];
		uint32_t completion_code;
		uint32_t control;
		uint32_t type;
		uint64_t event_pointer;

		invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			__asm__ volatile ("yield");
			continue;
		}
		result->event_status = event->status;
		result->event_control = control;
		result->command_pointer_low = (uint32_t) event->parameter;
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		advance_event_ring64(interrupter);
		if (type != XHCI_TRB_TYPE_COMMAND_COMPLETION) {
			continue;
		}
		completion_code = result->event_status >> 24;
		result->controller_status = *(volatile uint32_t *)
			(operational + XHCI_USBSTS);
		if (completion_code != 1U || event_pointer != expected_pointer) {
			return -3;
		}
		if (slot_id != NULL) {
			*slot_id = control >> 24;
		}
		return 0;
	}
	result->controller_status = *(volatile uint32_t *)
		(operational + XHCI_USBSTS);
	return -2;
}

static int wait_transfer_completion64(volatile uint8_t *interrupter,
	uint64_t expected_pointer, uint32_t slot_id, uint32_t endpoint_id,
	uint32_t timeout_ms, struct XHCI64_DESCRIPTOR_RESULT *result)
{
	uint64_t deadline;
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000U) * timeout_ms;
	while ((int64_t) (counter64() - deadline) < 0) {
		struct XHCI64_TRB *event =
			&xhci0_event_ring[xhci0_event_dequeue];
		uint32_t completion_code;
		uint32_t control;
		uint32_t event_endpoint_id;
		uint32_t event_slot_id;
		uint32_t type;
		uint64_t event_pointer;

		invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			__asm__ volatile ("yield");
			continue;
		}
		result->event_status = event->status;
		result->event_control = control;
		result->trb_pointer_low = (uint32_t) event->parameter;
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		advance_event_ring64(interrupter);
		if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
			continue;
		}
		completion_code = result->event_status >> 24;
		event_endpoint_id = (control >> 16) & 0x1fU;
		event_slot_id = control >> 24;
		if (completion_code != 1U ||
				(result->event_status & 0x00ffffffU) != 0 ||
				event_endpoint_id != endpoint_id || event_slot_id != slot_id ||
				event_pointer != expected_pointer) {
			return -3;
		}
		return 0;
	}
	return -2;
}

int xhci64_probe_rp1(uintptr_t rp1_base, uint32_t capability[2],
	uint32_t hcsparams1[2])
{
	int status;

	if (capability == NULL || hcsparams1 == NULL) {
		return -1;
	}
	status = probe_controller64(rp1_base + RP1_USB0_BASE, &capability[0],
		&hcsparams1[0]);
	if (status != 0) {
		return -2;
	}
	status = probe_controller64(rp1_base + RP1_USB1_BASE, &capability[1],
		&hcsparams1[1]);
	if (status != 0) {
		return -3;
	}
	return 0;
}

static int reset_controller64(uintptr_t base,
	struct XHCI64_RESET_RESULT *result)
{
	volatile uint8_t *capability = (volatile uint8_t *) base;
	volatile uint8_t *operational;
	volatile uint32_t *command;
	volatile uint32_t *status;
	uint32_t caplength;

	caplength = *(volatile uint32_t *) capability & 0xffU;
	operational = capability + caplength;
	command = (volatile uint32_t *) (operational + XHCI_USBCMD);
	status = (volatile uint32_t *) (operational + XHCI_USBSTS);
	result->command_before = *command;
	result->status_before = *status;

	if (wait_bits64(status, XHCI_USBSTS_CNR, 0) != 0) {
		return -5;
	}
	*command = result->command_before & ~XHCI_USBCMD_RUN;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (wait_bits64(status, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH) != 0) {
		return -1;
	}
	*command = *command | XHCI_USBCMD_HCRST;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (wait_bits64(command, XHCI_USBCMD_HCRST, 0) != 0) {
		return -2;
	}
	if (wait_bits64(status, XHCI_USBSTS_CNR, 0) != 0) {
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
	int status;

	if (results == NULL) {
		return -1;
	}
	xhci_controller_offset = RP1_USB0_BASE;
	status = reset_controller64(rp1_base + RP1_USB0_BASE, &results[0]);
	if (status != 0) {
		return -10 + status;
	}
	status = reset_controller64(rp1_base + RP1_USB1_BASE, &results[1]);
	if (status != 0) {
		return -20 + status;
	}
	return 0;
}

int xhci64_start_rp1(uintptr_t rp1_base,
	struct XHCI64_START_RESULT *result)
{
	volatile uint8_t *capability;
	volatile uint8_t *operational;
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	uint32_t caplength;
	uint32_t hcsparams1;
	uint32_t scratchpads;
	uint64_t command_dma;
	uint64_t event_dma;
	unsigned int i;

	if (result == NULL) {
		return -1;
	}
	if (!controller_has_connection64(rp1_base + RP1_USB0_BASE) &&
			controller_has_connection64(rp1_base + RP1_USB1_BASE)) {
		xhci_controller_offset = RP1_USB1_BASE;
	} else {
		xhci_controller_offset = RP1_USB0_BASE;
	}
	result->controller_id =
		xhci_controller_offset == RP1_USB1_BASE ? 1U : 0U;
	capability = xhci_capability64(rp1_base);
	caplength = *(volatile uint32_t *) capability & 0xffU;
	hcsparams1 = *(volatile uint32_t *)
		(capability + XHCI_HCSPARAMS1);
	result->hcsparams2 = *(volatile uint32_t *)
		(capability + XHCI_HCSPARAMS2);
	scratchpads = (((result->hcsparams2 >> 21) & 0x1fU) << 5) |
		((result->hcsparams2 >> 27) & 0x1fU);
	if ((hcsparams1 & 0xffU) < XHCI_MAX_SLOTS ||
			scratchpads > XHCI_MAX_SCRATCHPADS) {
		return -2;
	}

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
	xhci0_keyboard_pending_pointer = 0;
	if (scratchpads != 0) {
		for (i = 0; i < scratchpads; i++) {
			zero_words64(xhci0_scratchpads[i],
				sizeof(xhci0_scratchpads[i]));
			xhci0_scratchpad_array[i] =
				rp1_dma_address64(xhci0_scratchpads[i]);
			clean_cache64(xhci0_scratchpads[i],
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

	clean_cache64(xhci0_dcbaa, sizeof(xhci0_dcbaa));
	clean_cache64(xhci0_command_ring, sizeof(xhci0_command_ring));
	clean_cache64(xhci0_event_ring, sizeof(xhci0_event_ring));
	clean_cache64(&xhci0_erst, sizeof(xhci0_erst));
	clean_cache64(xhci0_scratchpad_array, sizeof(xhci0_scratchpad_array));

	operational = capability + caplength;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
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
	__asm__ volatile ("dsb sy" ::: "memory");

	*(volatile uint32_t *) (operational + XHCI_USBCMD) = XHCI_USBCMD_RUN;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (wait_bits64((volatile uint32_t *) (operational + XHCI_USBSTS),
			XHCI_USBSTS_HCH, 0) != 0) {
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
		result->port_status[i] = *(volatile uint32_t *) (operational +
			XHCI_PORTSC_BASE + i * XHCI_PORT_STRIDE);
	}
	return 0;
}

int xhci64_noop_command(uintptr_t rp1_base,
	struct XHCI64_COMMAND_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *operational;
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbell;
	uint64_t command_pointer;
	uint32_t caplength;

	if (result == NULL) {
		return -1;
	}
	caplength = *(volatile uint32_t *) capability & 0xffU;
	operational = capability + caplength;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbell = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));

	command_pointer = rp1_dma_address64(
		&xhci0_command_ring[xhci0_command_enqueue]);
	xhci0_command_ring[xhci0_command_enqueue].parameter = 0;
	xhci0_command_ring[xhci0_command_enqueue].status = 0;
	xhci0_command_ring[xhci0_command_enqueue].control =
		XHCI_TRB_TYPE_NOOP_COMMAND | xhci0_command_cycle;
	clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	__asm__ volatile ("dsb sy" ::: "memory");
	return wait_command_completion64(operational, interrupter,
		command_pointer, result, NULL);
}

int xhci64_enable_slot(uintptr_t rp1_base,
	struct XHCI64_SLOT_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *operational;
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	uint32_t caplength;
	int status;

	if (result == NULL || xhci0_command_enqueue >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	result->slot_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	caplength = *(volatile uint32_t *) capability & 0xffU;
	operational = capability + caplength;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbell = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
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
	clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	__asm__ volatile ("dsb sy" ::: "memory");
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
	return 0;
}

int xhci64_address_device(uintptr_t rp1_base, uint32_t port_id,
	uint32_t port_speed, uint32_t slot_id,
	struct XHCI64_ADDRESS_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *operational;
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	uint64_t ep0_dma;
	uint32_t *slot_context;
	uint32_t *ep0_context;
	uint32_t caplength;
	uint32_t context_dwords;
	uint32_t max_packet;
	int status;

	if (result == NULL || port_id == 0 || slot_id == 0 ||
			slot_id > XHCI_MAX_SLOTS || port_speed == 0 ||
			xhci0_command_enqueue >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	result->device_address = 0;
	result->slot_state = 0;
	result->context_size = 0;
	result->ep0_max_packet = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	caplength = *(volatile uint32_t *) capability & 0xffU;
	context_dwords = (*(volatile uint32_t *)
		(capability + XHCI_HCCPARAMS1) & (1U << 2)) != 0 ? 16U : 8U;
	if (port_speed <= 2U) {
		max_packet = 8U;
	} else if (port_speed >= 4U) {
		max_packet = 512U;
	} else {
		max_packet = 64U;
	}
	result->context_size = context_dwords * sizeof(uint32_t);
	result->ep0_max_packet = max_packet;
	zero_words64(xhci0_input_context, sizeof(xhci0_input_context));
	zero_words64(xhci0_device_context, sizeof(xhci0_device_context));
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
	clean_cache64(xhci0_input_context, sizeof(xhci0_input_context));
	clean_cache64(xhci0_device_context, sizeof(xhci0_device_context));
	clean_cache64(xhci0_ep0_ring, sizeof(xhci0_ep0_ring));
	clean_cache64(&xhci0_dcbaa[slot_id], sizeof(xhci0_dcbaa[slot_id]));

	operational = capability + caplength;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbell = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
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
	clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	__asm__ volatile ("dsb sy" ::: "memory");
	status = wait_command_completion64(operational, interrupter,
		command_pointer, &command_result, NULL);
	result->event_status = command_result.event_status;
	result->event_control = command_result.event_control;
	result->command_pointer_low = command_result.command_pointer_low;
	if (status != 0) {
		return status;
	}
	invalidate_cache64(xhci0_device_context,
		context_dwords * sizeof(uint32_t));
	result->device_address = xhci0_device_context[3] & 0xffU;
	result->slot_state = (xhci0_device_context[3] >> 27) & 0x1fU;
	if (result->device_address == 0 || result->slot_state != 2U) {
		return -4;
	}
	return 0;
}

static int read_descriptor64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t descriptor_type, uint32_t descriptor_index, uint32_t length,
	struct XHCI64_DESCRIPTOR_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbells;
	uint64_t buffer_dma;
	uint64_t status_pointer;
	uint32_t index;
	int status;

	if (result == NULL || slot_id == 0 || slot_id > XHCI_MAX_SLOTS ||
			length == 0 || length > sizeof(xhci0_descriptor_buffer) ||
			xhci0_ep0_enqueue + 3U >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	result->bcd_usb = 0;
	result->device_class_protocol = 0;
	result->ep0_max_packet = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	zero_words64(xhci0_descriptor_buffer,
		sizeof(xhci0_descriptor_buffer));
	buffer_dma = rp1_dma_address64(xhci0_descriptor_buffer);
	index = xhci0_ep0_enqueue;

	/* Standard GET_DESCRIPTOR followed by an OUT status stage. */
	xhci0_ep0_ring[index].parameter = 0x0000000000000680ULL |
		((uint64_t) descriptor_index << 16) |
		((uint64_t) descriptor_type << 24) | ((uint64_t) length << 48);
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
	clean_cache64(xhci0_descriptor_buffer,
		sizeof(xhci0_descriptor_buffer));
	clean_cache64(&xhci0_ep0_ring[xhci0_ep0_enqueue],
		3U * sizeof(struct XHCI64_TRB));
	xhci0_ep0_enqueue = index;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbells = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
	doorbells[slot_id] = 1U;
	__asm__ volatile ("dsb sy" ::: "memory");
	status = wait_transfer_completion64(interrupter, status_pointer,
		slot_id, 1U, XHCI_TIMEOUT_MS, result);
	if (status != 0) {
		return status;
	}
	invalidate_cache64(xhci0_descriptor_buffer, length);
	return 0;
}

int xhci64_read_device_descriptor8(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_DESCRIPTOR_RESULT *result)
{
	int status;

	status = read_descriptor64(rp1_base, slot_id, 1U, 0, 8U, result);
	if (status != 0) {
		return status;
	}
	if (xhci0_descriptor_buffer[0] < 8U ||
			xhci0_descriptor_buffer[1] != 1U) {
		return -4;
	}
	result->bcd_usb = (uint32_t) xhci0_descriptor_buffer[2] |
		((uint32_t) xhci0_descriptor_buffer[3] << 8);
	result->device_class_protocol =
		((uint32_t) xhci0_descriptor_buffer[4] << 16) |
		((uint32_t) xhci0_descriptor_buffer[5] << 8) |
		xhci0_descriptor_buffer[6];
	result->ep0_max_packet = xhci0_descriptor_buffer[7];
	if (result->ep0_max_packet != 8U && result->ep0_max_packet != 16U &&
			result->ep0_max_packet != 32U &&
			result->ep0_max_packet != 64U) {
		return -5;
	}
	return 0;
}

int xhci64_find_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	struct XHCI64_HID_RESULT *result)
{
	struct XHCI64_DESCRIPTOR_RESULT transfer;
	uint32_t offset;
	uint32_t total_length;
	int keyboard_interface;
	int status;

	if (result == NULL) {
		return -1;
	}
	result->vendor_product = 0;
	result->configuration_value = 0;
	result->interface_number = 0;
	result->endpoint_address = 0;
	result->endpoint_max_packet = 0;
	result->endpoint_interval = 0;
	result->total_length = 0;
	result->event_status = 0;
	result->event_control = 0;
	status = read_descriptor64(rp1_base, slot_id, 1U, 0, 18U,
		&transfer);
	if (status != 0) {
		return -2;
	}
	if (xhci0_descriptor_buffer[0] != 18U ||
			xhci0_descriptor_buffer[1] != 1U ||
			xhci0_descriptor_buffer[17] == 0) {
		return -3;
	}
	result->vendor_product = (uint32_t) xhci0_descriptor_buffer[8] |
		((uint32_t) xhci0_descriptor_buffer[9] << 8) |
		((uint32_t) xhci0_descriptor_buffer[10] << 16) |
		((uint32_t) xhci0_descriptor_buffer[11] << 24);

	status = read_descriptor64(rp1_base, slot_id, 2U, 0, 9U,
		&transfer);
	if (status != 0) {
		return -4;
	}
	if (xhci0_descriptor_buffer[0] != 9U ||
			xhci0_descriptor_buffer[1] != 2U) {
		return -5;
	}
	total_length = (uint32_t) xhci0_descriptor_buffer[2] |
		((uint32_t) xhci0_descriptor_buffer[3] << 8);
	if (total_length < 9U || total_length > sizeof(xhci0_descriptor_buffer)) {
		return -6;
	}
	result->total_length = total_length;
	result->configuration_value = xhci0_descriptor_buffer[5];
	status = read_descriptor64(rp1_base, slot_id, 2U, 0, total_length,
		&transfer);
	result->event_status = transfer.event_status;
	result->event_control = transfer.event_control;
	if (status != 0) {
		return -7;
	}

	keyboard_interface = 0;
	for (offset = 0; offset + 2U <= total_length;) {
		uint32_t descriptor_length = xhci0_descriptor_buffer[offset];
		uint32_t descriptor_type = xhci0_descriptor_buffer[offset + 1U];

		if (descriptor_length < 2U || offset + descriptor_length > total_length) {
			return -8;
		}
		if (descriptor_type == 4U && descriptor_length >= 9U) {
			keyboard_interface =
				xhci0_descriptor_buffer[offset + 5U] == 3U &&
				xhci0_descriptor_buffer[offset + 6U] == 1U &&
				xhci0_descriptor_buffer[offset + 7U] == 1U;
			if (keyboard_interface != 0) {
				result->interface_number =
					xhci0_descriptor_buffer[offset + 2U];
			}
		} else if (descriptor_type == 5U && descriptor_length >= 7U &&
				keyboard_interface != 0 &&
				(xhci0_descriptor_buffer[offset + 2U] & 0x80U) != 0 &&
				(xhci0_descriptor_buffer[offset + 3U] & 3U) == 3U) {
			result->endpoint_address =
				xhci0_descriptor_buffer[offset + 2U];
			result->endpoint_max_packet =
				(uint32_t) xhci0_descriptor_buffer[offset + 4U] |
				((uint32_t) xhci0_descriptor_buffer[offset + 5U] << 8);
			result->endpoint_max_packet &= 0x7ffU;
			result->endpoint_interval =
				xhci0_descriptor_buffer[offset + 6U];
			break;
		}
		offset += descriptor_length;
	}
	if (result->configuration_value == 0 || result->endpoint_address == 0 ||
			result->endpoint_max_packet == 0) {
		return -9;
	}
	return 0;
}

static int control_no_data64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t request_type, uint32_t request, uint32_t value,
	uint32_t request_index)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbells;
	struct XHCI64_DESCRIPTOR_RESULT transfer;
	uint64_t status_pointer;
	uint32_t index;

	if (request_type > 255U || request > 255U || value > 0xffffU ||
			request_index > 0xffffU ||
			xhci0_ep0_enqueue + 2U >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	index = xhci0_ep0_enqueue;
	xhci0_ep0_ring[index].parameter = (uint64_t) request_type |
		((uint64_t) request << 8) | ((uint64_t) value << 16) |
		((uint64_t) request_index << 32);
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
	clean_cache64(&xhci0_ep0_ring[xhci0_ep0_enqueue],
		2U * sizeof(struct XHCI64_TRB));
	xhci0_ep0_enqueue = index;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbells = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
	doorbells[slot_id] = 1U;
	__asm__ volatile ("dsb sy" ::: "memory");
	return wait_transfer_completion64(interrupter, status_pointer,
		slot_id, 1U, XHCI_TIMEOUT_MS, &transfer);
}

static uint32_t endpoint_interval64(uint32_t port_speed, uint32_t interval)
{
	uint32_t encoded;
	uint32_t period;

	if (port_speed >= 3U) {
		return interval == 0 ? 0 : interval - 1U;
	}
	encoded = 3U;
	period = 1U;
	while (period < interval && encoded < 10U) {
		period <<= 1;
		encoded++;
	}
	return encoded;
}

int xhci64_configure_boot_keyboard(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t port_speed, const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_CONFIGURE_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *operational;
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbell;
	struct XHCI64_COMMAND_RESULT command_result;
	uint64_t command_pointer;
	uint64_t interrupt_dma;
	uint32_t *slot_context;
	uint32_t *endpoint_context;
	uint32_t caplength;
	uint32_t context_dwords;
	uint32_t endpoint_id;
	uint32_t interval;
	uint32_t i;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			slot_id > XHCI_MAX_SLOTS || port_speed == 0 ||
			(hid->endpoint_address & 0x80U) == 0 ||
			(hid->endpoint_address & 0x0fU) == 0 ||
			hid->endpoint_max_packet == 0 ||
			xhci0_command_enqueue >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	result->endpoint_id = 0;
	result->endpoint_state = 0;
	result->interval = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->command_pointer_low = 0;
	status = control_no_data64(rp1_base, slot_id, 0U, 9U,
		hid->configuration_value, 0);
	if (status != 0) {
		return -2;
	}
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	interval = endpoint_interval64(port_speed, hid->endpoint_interval);
	result->endpoint_id = endpoint_id;
	result->interval = interval;
	caplength = *(volatile uint32_t *) capability & 0xffU;
	context_dwords = (*(volatile uint32_t *)
		(capability + XHCI_HCCPARAMS1) & (1U << 2)) != 0 ? 16U : 8U;
	zero_words64(xhci0_input_context, sizeof(xhci0_input_context));
	zero_words64(xhci0_interrupt_ring, sizeof(xhci0_interrupt_ring));
	invalidate_cache64(xhci0_device_context,
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
		(hid->endpoint_max_packet << 16);
	endpoint_context[2] = (uint32_t) interrupt_dma | 1U;
	endpoint_context[3] = (uint32_t) (interrupt_dma >> 32);
	endpoint_context[4] = hid->endpoint_max_packet |
		(hid->endpoint_max_packet << 16);
	xhci0_interrupt_ring[XHCI_RING_TRBS - 1].parameter = interrupt_dma;
	xhci0_interrupt_ring[XHCI_RING_TRBS - 1].control = XHCI_TRB_TYPE_LINK |
		XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
	clean_cache64(xhci0_input_context, sizeof(xhci0_input_context));
	clean_cache64(xhci0_interrupt_ring, sizeof(xhci0_interrupt_ring));
	xhci0_interrupt_enqueue = 0;
	xhci0_interrupt_cycle = 1;

	operational = capability + caplength;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbell = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
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
	clean_cache64(&xhci0_command_ring[xhci0_command_enqueue],
		sizeof(xhci0_command_ring[xhci0_command_enqueue]));
	xhci0_command_enqueue++;
	*doorbell = 0;
	__asm__ volatile ("dsb sy" ::: "memory");
	status = wait_command_completion64(operational, interrupter,
		command_pointer, &command_result, NULL);
	result->event_status = command_result.event_status;
	result->event_control = command_result.event_control;
	result->command_pointer_low = command_result.command_pointer_low;
	if (status != 0) {
		return -3;
	}
	invalidate_cache64(xhci0_device_context + endpoint_id * context_dwords,
		context_dwords * sizeof(uint32_t));
	result->endpoint_state =
		xhci0_device_context[endpoint_id * context_dwords] & 7U;
	if (result->endpoint_state != 1U) {
		return -4;
	}
	return 0;
}

int xhci64_read_boot_key(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbells;
	struct XHCI64_DESCRIPTOR_RESULT transfer;
	uint64_t report_dma;
	uint64_t trb_pointer;
	uint32_t endpoint_id;
	uint32_t index;
	uint32_t report;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			(hid->endpoint_address & 0x80U) == 0 ||
			hid->endpoint_max_packet < 8U ||
			hid->endpoint_max_packet > sizeof(xhci0_keyboard_report)) {
		return -1;
	}
	result->modifier = 0;
	result->keycode = 0;
	result->endpoint_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	transfer.bcd_usb = 0;
	transfer.device_class_protocol = 0;
	transfer.ep0_max_packet = 0;
	transfer.event_status = 0;
	transfer.event_control = 0;
	transfer.trb_pointer_low = 0;
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	result->endpoint_id = endpoint_id;
	status = xhci64_keyboard_set_boot_protocol(rp1_base, slot_id, hid);
	if (status != 0) {
		return -2;
	}
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbells = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
	report_dma = rp1_dma_address64(xhci0_keyboard_report);

	for (report = 0; report < 4U; report++) {
		if (xhci0_interrupt_enqueue >= XHCI_RING_TRBS - 1U) {
			return -3;
		}
		zero_words64(xhci0_keyboard_report,
			sizeof(xhci0_keyboard_report));
		index = xhci0_interrupt_enqueue;
		xhci0_interrupt_ring[index].parameter = report_dma;
		xhci0_interrupt_ring[index].status = hid->endpoint_max_packet;
		xhci0_interrupt_ring[index].control = XHCI_TRB_TYPE_NORMAL |
			XHCI_TRB_IOC | xhci0_interrupt_cycle;
		trb_pointer = rp1_dma_address64(&xhci0_interrupt_ring[index]);
		clean_cache64(xhci0_keyboard_report,
			sizeof(xhci0_keyboard_report));
		clean_cache64(&xhci0_interrupt_ring[index],
			sizeof(xhci0_interrupt_ring[index]));
		advance_interrupt_ring64();
		doorbells[slot_id] = endpoint_id;
		__asm__ volatile ("dsb sy" ::: "memory");
		status = wait_transfer_completion64(interrupter, trb_pointer,
			slot_id, endpoint_id, 10000U, &transfer);
		result->event_status = transfer.event_status;
		result->event_control = transfer.event_control;
		result->trb_pointer_low = transfer.trb_pointer_low;
		if (status != 0) {
			return -4;
		}
		invalidate_cache64(xhci0_keyboard_report,
			hid->endpoint_max_packet);
		result->modifier = xhci0_keyboard_report[0];
		for (index = 2U; index < 8U; index++) {
			if (xhci0_keyboard_report[index] != 0) {
				result->keycode = xhci0_keyboard_report[index];
				return 0;
			}
		}
	}
	return -5;
}

int xhci64_keyboard_set_boot_protocol(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	if (hid == NULL || slot_id == 0) {
		return -1;
	}
	return control_no_data64(rp1_base, slot_id, 0x21U, 0x0bU, 0,
		hid->interface_number);
}

int xhci64_read_boot_release(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid,
	struct XHCI64_KEY_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	volatile uint32_t *doorbells;
	struct XHCI64_DESCRIPTOR_RESULT transfer;
	uint64_t trb_pointer;
	uint32_t attempt;
	uint32_t endpoint_id;
	uint32_t index;
	int status;

	if (hid == NULL || result == NULL || slot_id == 0 ||
			hid->endpoint_max_packet < 8U ||
			xhci0_interrupt_enqueue >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	result->modifier = 0;
	result->keycode = 0;
	result->endpoint_id = 0;
	result->event_status = 0;
	result->event_control = 0;
	result->trb_pointer_low = 0;
	transfer.bcd_usb = 0;
	transfer.device_class_protocol = 0;
	transfer.ep0_max_packet = 0;
	transfer.event_status = 0;
	transfer.event_control = 0;
	transfer.trb_pointer_low = 0;
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	result->endpoint_id = endpoint_id;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	doorbells = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
	for (attempt = 0; attempt < 4U; attempt++) {
		if (xhci0_interrupt_enqueue >= XHCI_RING_TRBS - 1U) {
			return -2;
		}
		zero_words64(xhci0_keyboard_report,
			sizeof(xhci0_keyboard_report));
		index = xhci0_interrupt_enqueue;
		xhci0_interrupt_ring[index].parameter =
			rp1_dma_address64(xhci0_keyboard_report);
		xhci0_interrupt_ring[index].status = hid->endpoint_max_packet;
		xhci0_interrupt_ring[index].control = XHCI_TRB_TYPE_NORMAL |
			XHCI_TRB_IOC | xhci0_interrupt_cycle;
		trb_pointer = rp1_dma_address64(&xhci0_interrupt_ring[index]);
		clean_cache64(xhci0_keyboard_report,
			sizeof(xhci0_keyboard_report));
		clean_cache64(&xhci0_interrupt_ring[index],
			sizeof(xhci0_interrupt_ring[index]));
		advance_interrupt_ring64();
		doorbells[slot_id] = endpoint_id;
		__asm__ volatile ("dsb sy" ::: "memory");
		status = wait_transfer_completion64(interrupter, trb_pointer,
			slot_id, endpoint_id, 10000U, &transfer);
		result->event_status = transfer.event_status;
		result->event_control = transfer.event_control;
		result->trb_pointer_low = transfer.trb_pointer_low;
		if (status != 0) {
			return -3;
		}
		invalidate_cache64(xhci0_keyboard_report,
			hid->endpoint_max_packet);
		result->modifier = xhci0_keyboard_report[0];
		result->keycode = 0;
		for (index = 2U; index < 8U; index++) {
			if (xhci0_keyboard_report[index] != 0) {
				result->keycode = xhci0_keyboard_report[index];
				break;
			}
		}
		if (result->modifier == 0 && result->keycode == 0) {
			return 0;
		}
	}
	return -4;
}

int xhci64_keyboard_arm(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint32_t *doorbells;
	uint32_t endpoint_id;
	uint32_t index;

	if (hid == NULL || slot_id == 0 || xhci0_keyboard_pending_pointer != 0 ||
			hid->endpoint_max_packet < 8U ||
			hid->endpoint_max_packet > sizeof(xhci0_keyboard_report) ||
			xhci0_interrupt_enqueue >= XHCI_RING_TRBS - 1U) {
		return -1;
	}
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	zero_words64(xhci0_keyboard_report, sizeof(xhci0_keyboard_report));
	index = xhci0_interrupt_enqueue;
	xhci0_interrupt_ring[index].parameter =
		rp1_dma_address64(xhci0_keyboard_report);
	xhci0_interrupt_ring[index].status = hid->endpoint_max_packet;
	xhci0_interrupt_ring[index].control = XHCI_TRB_TYPE_NORMAL |
		XHCI_TRB_IOC | xhci0_interrupt_cycle;
	xhci0_keyboard_pending_pointer =
		rp1_dma_address64(&xhci0_interrupt_ring[index]);
	clean_cache64(xhci0_keyboard_report, sizeof(xhci0_keyboard_report));
	clean_cache64(&xhci0_interrupt_ring[index],
		sizeof(xhci0_interrupt_ring[index]));
	advance_interrupt_ring64();
	doorbells = (volatile uint32_t *) (capability +
		(*(volatile uint32_t *) (capability + XHCI_DBOFF) & ~3U));
	doorbells[slot_id] = endpoint_id;
	__asm__ volatile ("dsb sy" ::: "memory");
	return 0;
}

int xhci64_keyboard_poll(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid, uint8_t report[8])
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *runtime;
	volatile uint8_t *interrupter;
	struct XHCI64_TRB *event;
	uint64_t event_pointer;
	uint32_t completion_code;
	uint32_t control;
	uint32_t endpoint_id;
	uint32_t i;
	uint32_t type;

	if (hid == NULL || report == NULL || slot_id == 0 ||
			xhci0_keyboard_pending_pointer == 0) {
		return -1;
	}
	endpoint_id = ((hid->endpoint_address & 0x0fU) << 1) + 1U;
	runtime = capability +
		(*(volatile uint32_t *) (capability + XHCI_RTSOFF) & ~0x1fU);
	interrupter = runtime + XHCI_IR0_BASE;
	for (;;) {
		event = &xhci0_event_ring[xhci0_event_dequeue];
		invalidate_cache64(event, sizeof(*event));
		control = event->control;
		if ((control & XHCI_TRB_CYCLE) != xhci0_event_cycle) {
			return 0;
		}
		type = (control >> 10) & 0x3fU;
		event_pointer = event->parameter;
		completion_code = event->status >> 24;
		advance_event_ring64(interrupter);
		if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
			continue;
		}
		if (completion_code != 1U || (event->status & 0x00ffffffU) != 0 ||
				((control >> 16) & 0x1fU) != endpoint_id ||
				(control >> 24) != slot_id ||
				event_pointer != xhci0_keyboard_pending_pointer) {
			return -2;
		}
		invalidate_cache64(xhci0_keyboard_report,
			hid->endpoint_max_packet);
		for (i = 0; i < 8U; i++) {
			report[i] = xhci0_keyboard_report[i];
		}
		xhci0_keyboard_pending_pointer = 0;
		return 1;
	}
}

int xhci64_reset_connected_port(uintptr_t rp1_base,
	struct XHCI64_PORT_RESULT *result)
{
	volatile uint8_t *capability = xhci_capability64(rp1_base);
	volatile uint8_t *operational;
	volatile uint32_t *portsc;
	uint64_t deadline;
	uint64_t frequency;
	uint32_t caplength;
	uint32_t hcsparams1;
	uint32_t port_count;
	uint32_t status;
	uint32_t reset_bit;
	uint32_t completion_bit;
	uint32_t port;

	if (result == NULL) {
		return -1;
	}
	result->port_id = 0;
	result->protocol_major = 0;
	result->status_before = 0;
	result->status_after = 0;
	caplength = *(volatile uint32_t *) capability & 0xffU;
	hcsparams1 = *(volatile uint32_t *)
		(capability + XHCI_HCSPARAMS1);
	port_count = (hcsparams1 >> 24) & 0xffU;
	operational = capability + caplength;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() +
		(frequency / 1000U) * XHCI_PORT_CONNECT_TIMEOUT_MS;
	portsc = NULL;
	while ((int64_t) (counter64() - deadline) < 0) {
		for (port = 1; port <= port_count; port++) {
			volatile uint32_t *candidate = (volatile uint32_t *)
				(operational + XHCI_PORTSC_BASE +
				(port - 1U) * XHCI_PORT_STRIDE);

			status = *candidate;
			if ((status & XHCI_PORTSC_CCS) != 0) {
				portsc = candidate;
				result->port_id = port;
				result->status_before = status;
				break;
			}
		}
		if (portsc != NULL) {
			break;
		}
		__asm__ volatile ("yield");
	}
	if (portsc == NULL) {
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
	__asm__ volatile ("dsb sy" ::: "memory");
	if (wait_bits64(portsc, reset_bit, 0) != 0) {
		result->status_after = *portsc;
		return -4;
	}
	delay_xhci64(XHCI_PORT_RESET_RECOVERY_MS);
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
	return 0;
}
