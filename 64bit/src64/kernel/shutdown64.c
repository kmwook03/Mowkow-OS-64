#include <arch/platform64.h>
#include <console64.h>
#include <fd64.h>
#include <shutdown64.h>

static int shutdown_busy64;

void shutdown64(struct CONSOLE64 *con)
{
	uint64_t state;
	int status;

	state = platform_irq_save64();
	if (shutdown_busy64 != 0) {
		platform_irq_restore64(state);
		console64_message_con(con, MESSAGE_SHUTDOWN_BUSY64);
		return;
	}
	shutdown_busy64 = 1;
	platform_irq_restore64(state);

	console64_message_con(con, MESSAGE_SHUTDOWN_SYNC64);
	/* Do not disable scheduling while another task may own the storage mutex. */
	status = fd64_shutdown();
	if (status != 0) {
		console64_message_con(con, MESSAGE_SHUTDOWN_FAILED64);
		state = platform_irq_save64();
		shutdown_busy64 = 0;
		platform_irq_restore64(state);
		return;
	}
	platform_irq_disable64();
	console64_message_con(con, MESSAGE_SHUTDOWN_POWEROFF64);
	(void) platform_poweroff64();
	console64_message_con(con, MESSAGE_SHUTDOWN_HALTED64);
	/* No scheduler, input, or storage activity may resume after the final flush. */
	for (;;) {
		platform_halt64();
	}
}
