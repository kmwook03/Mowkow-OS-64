/* x86 kernel entry: preserve dependency order, then route top-level events.
 * COM1 support and self-check implementation live under arch/x86_64/.
 */
#include <arch/x86_boot64.h>
#include <asmfunc64.h>
#include <block64.h>
#include <console64.h>
#include <dsctbl64.h>
#include <fd64.h>
#include <fifo64.h>
#include <graphic64.h>
#include <gui64.h>
#include <int64.h>
#include <kernel64.h>
#include <keyboard64.h>
#include <memory64.h>
#include <mouse64.h>
#include <mtask64.h>
#include <timer64.h>

#define KERNEL_EVENT_BUF_SIZE64 128
static struct FIFO64 event_fifo64;
static struct EVENT64 event_buf64[KERNEL_EVENT_BUF_SIZE64];

static void process_event64(const struct EVENT64 *event)
{
	struct CONSOLE64 *con;

	if (event->type == EVENT64_TIMER) {
		return;
	}
	/* 마우스와 F11은 창 계층이 먼저 가져간다. 남은 키만 포커스가 있는
	   콘솔의 태스크로 보낸다 -- 콘솔이 아닌 창이 활성이면 아무 데도 안 간다. */
	if (gui64_handle_system_event(event) != 0) {
		return;
	}
	if (event->type == EVENT64_KEYBOARD) {
		con = gui64_focused_console();
		if (con != NULL) {
			console64_post_key(con, (uint16_t) event->data);
		}
	}
}

void kernel64_main(const struct BOOTINFO64 *boot_info)
{
	struct EVENT64 event;
	int status;

	x86_boot_diag_init64();
	if (x86_boot_info_valid64(boot_info) == 0) {
		x86_boot_panic64("framebuffer", -1);
	}
	init_gdtidt64();
	x86_boot_smoke_bss64();
	init_fpu64();
	x86_boot_smoke_fpu64();
	init_memory64();
	if (memman64_total(&memman64) < MEMMAN64_PAGE_SIZE) {
		x86_boot_panic64("memory", -1);
	}
	status = block64_init();
	if (status != 0) {
		x86_boot_panic64("block", status);
	}
	x86_boot_diag_storage64();
	status = fd64_init();
	if (status != 0) {
		x86_boot_panic64("fat32", status);
	}
	x86_boot_smoke_storage64();
	x86_boot_smoke_gui_input64();
	x86_boot_load_font64();
	init_palette64();
	console64_init(boot_info);
	x86_boot_diag_gui64();
	status = task_init64();
	if (status != 0) {
		x86_boot_panic64("scheduler", status);
	}
	status = fifo64_init(&event_fifo64, KERNEL_EVENT_BUF_SIZE64, event_buf64, task_now64());
	if (status != 0) {
		x86_boot_panic64("event-fifo", status);
	}
	status = console64_start_task(console64_active());
	if (status != 0) {
		x86_boot_panic64("console-task", status);
	}
	init_pit64(&event_fifo64);
	init_pic64();
	io_sti();
	/* 인터럽트를 켠 뒤에 KBC를 건드린다. 마우스 활성화 명령의 ACK(0xfa)는
	   IRQ12로 돌아오므로, 마스킹된 상태에서 보내면 출력 버퍼에 갇힌 채
	   에지를 놓쳐 이후 패킷이 오지 않는다 (32비트 bootpack.c:76-85과 같은 순서). */
	status = init_keyboard64(&event_fifo64);
	if (status != 0) {
		x86_boot_diag_keyboard64();
		x86_boot_panic64("ps2-keyboard", status);
	}
	status = init_mouse64(&event_fifo64, gui64_mouse_dec());
	if (status != 0) {
		x86_boot_diag_mouse64();
	}

	for (;;) {
		io_cli();
		if (fifo64_get(&event_fifo64, &event) == 0) {
			io_sti();
			process_event64(&event);
			/* 닫히기를 기다리는 콘솔 거두기. 타이머가 100Hz로 이벤트를
			   넣으므로 이 자리는 꾸준히 지나간다 (console64.c의 협조적 닫기). */
			console64_reap_closed();
		} else {
			task_sleep64(task_now64());
			io_sti();
		}
	}
}
