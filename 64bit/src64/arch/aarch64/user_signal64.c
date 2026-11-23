#include <arch/user_context64.h>
#include <kstring64.h>
#include <process64.h>
#include <user_access64.h>
#include <arch/exception_frame64.h>

int platform_user_signal_enter64(struct USER_CONTEXT64 *context, void *native, void *fp,
	uintptr_t handler, uintptr_t restorer, uintptr_t *return_sp, int zero_result)
{
	struct ARCH64_EXCEPTION_FRAME *frame = native;
	uintptr_t stack;

	if ((frame->spsr & 15) != 0 || frame->sp_el0 < 16) { return -14; }
	stack = (frame->sp_el0 - 16) & ~(uintptr_t) 15;
	if (!process64_user_write_valid((void *) stack, 16)) { return -14; }
	platform_user_capture64(context, frame, fp, zero_result);
	frame->elr = handler; frame->sp_el0 = stack; frame->x[0] = 32;
	frame->x[1] = frame->x[2] = 0; frame->x[30] = restorer;
	*return_sp = stack;
	return 0;
}

int platform_user_signal_restore64(const struct USER_CONTEXT64 *context,
	void *native, void *fp, uintptr_t return_sp)
{
	struct ARCH64_EXCEPTION_FRAME *frame = native;

	(void) fp;
	if ((frame->spsr & 15) != 0 || frame->sp_el0 != return_sp) { return -22; }
	memcpy(frame, context->frame, sizeof(*frame));
	platform_user_restore_tls64(context);
	return 0;
}

int platform_user_mode64(const void *native)
{
	const struct ARCH64_EXCEPTION_FRAME *frame = native;

	return (frame->spsr & 15) == 0;
}
