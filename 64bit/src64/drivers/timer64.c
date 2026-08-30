/*
 * timer64.c -- PIT(8254) 타이머, IRQ0
 *
 * 100Hz로 맞춘다(1193182 / 11932 = 약 100). 
 * 매 틱마다 이벤트를 큐에 넣고태스크를 바꾼다. 
 * 커널의 시간 단위 10ms 산출 근거
 */
#include <asmfunc64.h>
#include <int64.h>
#include <mtask64.h>
#include <timer64.h>

#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

struct TIMERCTL64 timerctl64;

void init_pit64(struct FIFO64 *fifo)
{
	io_out8(PIT_CTRL, 0x34);        /* 채널 0, 낮은 바이트부터, 모드 2 */
	io_out8(PIT_CNT0, 0x9c);        /* 분주비 11932 = 0x2e9c, 낮은 바이트 */
	io_out8(PIT_CNT0, 0x2e);        /* 높은 바이트 */
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
