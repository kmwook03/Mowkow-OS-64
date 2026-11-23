#include <arch/user_context64.h>
#include <kstring64.h>
#include <process64.h>
#include <user_access64.h>
#include <interrupt64.h>

int platform_user_signal_enter64(struct USER_CONTEXT64 *context, void *native, void *fp,
	uintptr_t handler, uintptr_t restorer, uintptr_t *return_sp, int zero_result)
{
	struct INTERRUPT_FRAME64 *frame = native;
	uintptr_t stack;

	/* Preserve the ABI red zone and the handler's call-entry alignment. */
	if ((frame->cs & 3) != 3 || frame->rsp < 144) { return -14; }
	stack = ((frame->rsp - 128) & ~(uintptr_t) 15) - 8;
	if (copy_to_user64((void *) stack, &restorer, sizeof(restorer)) != 0) { return -14; }
	platform_user_capture64(context, frame, fp, zero_result);
	frame->rip = handler; frame->rsp = stack; frame->rdi = 32;
	frame->rsi = frame->rdx = frame->rax = 0;
	*return_sp = stack + 8;
	return 0;
}

int platform_user_signal_restore64(const struct USER_CONTEXT64 *context,
	void *native, void *fp, uintptr_t return_sp)
{
	struct INTERRUPT_FRAME64 *frame = native;

	if ((frame->cs & 3) != 3 || frame->rsp != return_sp) { return -22; }
	/* All privilege, PC, flags and FP state originate in a kernel snapshot.
	 * Userspace provides no selectors, MSRs or FXSAVE control bits.
	 */
	memcpy(frame, context->frame, sizeof(*frame));
	memcpy(fp, context->fp, sizeof(context->fp));
	platform_user_restore_tls64(context);
	return 0;
}

int platform_user_mode64(const void *native)
{
	const struct INTERRUPT_FRAME64 *frame = native;

	return (frame->cs & 3) == 3;
}
