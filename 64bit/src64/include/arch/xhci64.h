#ifndef MOWKOW64_ARCH_XHCI64_H
#define MOWKOW64_ARCH_XHCI64_H

#include <stdint.h>

/* RP1 xHCI controller operations. A single task-context poller serializes
 * selection and each complete operation; these calls must not run in IRQs.
 * Controller selection also selects its private DMA buffers and ring state.
 */
int xhci64_probe_rp1(uintptr_t rp1_base, uint32_t capability[2],
	uint32_t hcsparams1[2]);

struct XHCI64_RESET_RESULT {
	uint32_t command_before;
	uint32_t status_before;
	uint32_t command_after;
	uint32_t status_after;
};

int xhci64_reset_rp1(uintptr_t rp1_base,
	struct XHCI64_RESET_RESULT results[2]);

struct XHCI64_START_RESULT {
	uint32_t controller_id;
	uint32_t command;
	uint32_t status;
	uint32_t hcsparams2;
	uint32_t port_status[3];
};

int xhci64_start_rp1(uintptr_t rp1_base,
	struct XHCI64_START_RESULT *result);
int xhci64_start_controller(uintptr_t rp1_base, uint32_t controller_id,
	struct XHCI64_START_RESULT *result);
int xhci64_select_controller(uint32_t controller_id);

#define XHCI64_INVENTORY_PORTS 4U

struct XHCI64_PORT_INVENTORY {
	uint32_t port_count;
	uint32_t connected_mask;
	uint32_t enabled_mask;
	uint32_t port_status[XHCI64_INVENTORY_PORTS];
};

int xhci64_port_inventory(uintptr_t rp1_base,
	struct XHCI64_PORT_INVENTORY inventory[2]);

struct XHCI64_COMMAND_RESULT {
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
	uint32_t controller_status;
};

int xhci64_noop_command(uintptr_t rp1_base,
	struct XHCI64_COMMAND_RESULT *result);

struct XHCI64_PORT_RESULT {
	uint32_t port_id;
	uint32_t protocol_major;
	uint32_t status_before;
	uint32_t status_after;
};

int xhci64_reset_connected_port(uintptr_t rp1_base,
	struct XHCI64_PORT_RESULT *result);

/* Quarantine the selected controller's root port until controller reset/start.
 * No host reset or sibling-port write. Failed retirement retains DMA ownership.
 * Single poller/task context only; may poll for bounded hardware completion.
 * Enable Slot associates its slot with the last successfully reset root port;
 * quarantine retires only a slot associated with the requested port.
 */
int xhci_port_quarantine64(uintptr_t rp1_base, uint32_t port_id);

struct XHCI64_SLOT_RESULT {
	uint32_t slot_id;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};

int xhci64_enable_slot(uintptr_t rp1_base,
	struct XHCI64_SLOT_RESULT *result);

struct XHCI64_ADDRESS_RESULT {
	uint32_t device_address;
	uint32_t slot_state;
	uint32_t context_size;
	uint32_t ep0_max_packet;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};

int xhci64_address_device(uintptr_t rp1_base, uint32_t port_id,
	uint32_t port_speed, uint32_t slot_id,
	struct XHCI64_ADDRESS_RESULT *result);

struct XHCI64_CONFIGURE_RESULT {
	uint32_t endpoint_id;
	uint32_t endpoint_state;
	uint32_t interval;
	uint32_t event_status;
	uint32_t event_control;
	uint32_t command_pointer_low;
};


#endif
