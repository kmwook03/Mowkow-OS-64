#ifndef MOWKOW64_ARCH_AARCH64_BOOT64_H
#define MOWKOW64_ARCH_AARCH64_BOOT64_H

#include <stdint.h>

struct BOOTINFO64;
struct SHEET64;
struct XHCI64_HID_RESULT;

/* Pi 5 boot support. GPIO works both before and after the high-half switch.
 * Clock accessors use the physical counter; no scheduler or IRQ is required.
 */
void aarch64_act_led_set64(int on);
uint64_t aarch64_boot_counter64(void);
uint64_t aarch64_boot_frequency64(void);
void aarch64_boot_relax64(void);
void aarch64_boot_log_hex64(uint32_t value);

/* Preserve boot dependencies: storage after FAT mount; sheet probes after memory
 * and framebuffer setup, before live GUI creation. The window font has kernel
 * lifetime. GUI pixels are checked only after the console sheet is published.
 * Keyboard probe needs a configured controller and consumes one press/release;
 * its FIFO is private. Synthetic sheet/mouse checks remain in default builds.
 */
int aarch64_boot_smoke_sheet64(void);
int aarch64_boot_smoke_mouse64(void);
int aarch64_boot_smoke_framebuffer64(const struct BOOTINFO64 *bootinfo);
int aarch64_boot_smoke_window64(const struct BOOTINFO64 *bootinfo,
	const uint8_t *font);
int aarch64_boot_smoke_gui64(const struct BOOTINFO64 *bootinfo,
	struct SHEET64 *console_sheet);
int aarch64_boot_smoke_keyboard64(uintptr_t rp1_base, uint32_t slot_id,
	const struct XHCI64_HID_RESULT *hid);
#if MOWKOW64_BOOT_TESTS
/* Mutates M4TEST.TXT; call only with disposable test media. Returns 1 when a
 * prior boot's payload is verified, 0 after writing, or a negative failure.
 */
int aarch64_boot_smoke_storage64(void);
#endif

#endif
