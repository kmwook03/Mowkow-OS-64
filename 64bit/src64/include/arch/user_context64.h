#ifndef MOWKOW64_ARCH_USER_CONTEXT64_H
#define MOWKOW64_ARCH_USER_CONTEXT64_H

#include <stddef.h>
#include <stdint.h>

/* Kernel-owned native return state. Never accept this layout from userspace. */
struct USER_CONTEXT64 {
	_Alignas(16) uint8_t fp[512];
	uint64_t frame[100];
	uintptr_t tls;
	uintptr_t readonly_tls;
};

_Static_assert(offsetof(struct USER_CONTEXT64, readonly_tls) == 1320, "native TLS offset");
_Static_assert(offsetof(struct USER_CONTEXT64, frame) == 512, "native frame offset");
/* Task context; native frame and FP snapshot are the live syscall entry. */
void platform_user_capture64(struct USER_CONTEXT64 *context,
	const void *frame, const void *fp, int zero_result);
/* IRQ-off; save runner continuation, then resume the captured user frame. */
int platform_user_resume64(const struct USER_CONTEXT64 *context, uintptr_t *runner);
void platform_user_restore_tls64(const struct USER_CONTEXT64 *context);
int platform_user_mode64(const void *frame);
int platform_user_signal_enter64(struct USER_CONTEXT64 *context, void *frame, void *fp,
	uintptr_t handler, uintptr_t restorer, uintptr_t *return_sp, int zero_result);
int platform_user_signal_restore64(const struct USER_CONTEXT64 *context,
	void *frame, void *fp, uintptr_t return_sp);

#endif
