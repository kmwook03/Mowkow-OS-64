#ifndef MOWKOW64_AARCH64_XHCI_INTERNAL64_H
#define MOWKOW64_AARCH64_XHCI_INTERNAL64_H

#include <arch/xhci64.h>
#include <stddef.h>
#include <stdint.h>

#define XHCI64_MAX_SLOTS 64U
#define XHCI64_CONTROL_BYTES 512U
#define XHCI64_INTERRUPT_BYTES 64U

struct XHCI64_TRANSFER_RESULT {
	uint32_t event_status;
	uint32_t event_control;
	uint32_t trb_pointer_low;
};

/* Private transport bridge for usb_xhci64.c. The board's single poller owns
 * controller selection for an entire operation, including readiness checks.
 * DMA storage and pending-transfer ownership remain entirely in xhci64.c.
 * Reads copy bytes out only on completion; caller buffers need no DMA mapping.
 * Predicates return boolean; transfers return 0/count or negative errno.
 * Start/address/configure must succeed before the respective queues are ready.
 * Command and EP0 queues are one-pass; exhaustion requires controller restart.
 * Async pending ownership survives errors; no other read/configure may reuse
 * that interrupt DMA buffer until completion or controller reinitialization.
 */
int xhci_control_ready64(uint32_t slot_id, uint32_t trbs);
int xhci_command_ready64(void);
int xhci_interrupt_ready64(void);
int xhci_control_read64(uintptr_t rp1_base, uint32_t slot_id, uint64_t setup,
	uint8_t *data, uint32_t length, struct XHCI64_TRANSFER_RESULT *result);
int xhci_control_write64(uintptr_t rp1_base, uint32_t slot_id, uint64_t setup);
int xhci_configure_interrupt64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint32_t interval,
	struct XHCI64_CONFIGURE_RESULT *result);
int xhci_interrupt_read64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint32_t timeout_ms,
	uint8_t *data, struct XHCI64_TRANSFER_RESULT *result);
int xhci_interrupt_arm64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet);
int xhci_interrupt_poll64(uintptr_t rp1_base, uint32_t slot_id,
	uint32_t endpoint_id, uint32_t packet, uint8_t *report, size_t report_length);

#endif
