#include <asmfunc64.h>
#include <int64.h>
#include <mtask64.h>
#include <timer64.h>

#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

struct TIMERCTL64 timerctl64;

void init_pit64(struct FIFO64 *fifo)
{
	io_out8(PIT_CTRL, 0x34);
	io_out8(PIT_CNT0, 0x9c);
	io_out8(PIT_CNT0, 0x2e);
	timerctl64.count = 0;
	timerctl64.fifo = fifo;
}

void inthandler20_64(void)
{
	struct EVENT64 event;

	io_out8(PIC0_OCW2, 0x60);
	timerctl64.count++;
	event.type = EVENT64_TIMER;
	event.data = (uint32_t) timerctl64.count;
	fifo64_put(timerctl64.fifo, event);
	task_switch64();
}
