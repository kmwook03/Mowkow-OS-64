/**
 * @file timer.c
 * 
 * @brief PIT(Programmable Interval Timer) 제어 및 타이머 관리 기능 구현
 * 
 */
#include "../include/bootpack.h"

#define PIT_CTRL    0x0043		// PIT 제어 포트
#define PIT_CNT0    0x0040		// PIT 채널 0 데이터 포트

struct TIMERCTL timerctl;		// 타이머 컨트롤러 구조체

#define TIMER_FLAGS_ALLOC   1   // timer is allocated
#define TIMER_FLAGS_USING   2   // timer is running

/**
 * @brief PIT 초기화 함수
 * 
 * PIT을 모드 2(주기적 인터럽트)로 설정하여 약 100Hz로 인터럽트를 발생시키도록 초기화
 * 
 */
void init_pit(void)
{
	int i;
	struct TIMER *t;
	io_out8(PIT_CTRL, 0x34);	// 0번 카운터 사용, 모드 2, 2진수 카운트
	io_out8(PIT_CNT0, 0x9c);
	io_out8(PIT_CNT0, 0x2e);	// 0x2e9c = 11932, PIT의 클럭은 1193182Hz이므로 약 100Hz로 인터럽트 발생
	timerctl.count = 0;
	// 타이머 풀 초기화
	for (i = 0; i < MAX_TIMER; i++) {
		timerctl.timers0[i].flags = 0; // 미사용 마킹
	}
	t = timer_alloc(); 
	t->timeout = 0xffffffff;		// 가장 큰 값으로 설정하여 가장 마지막에 인터럽트 발생하도록 함 -> 항상 next에 0xfff...ffff이 오도록 보장 -> 예외처리 간소화 가능
	t->flags = TIMER_FLAGS_USING;
	t->next = 0; // end marker
	timerctl.t0 = t; 
	timerctl.next = 0xffffffff;
	return;
}

struct TIMER *timer_alloc(void)
{
	int i;
	for (i = 0; i < MAX_TIMER; i++) {
		if (timerctl.timers0[i].flags == 0) {
			timerctl.timers0[i].flags = TIMER_FLAGS_ALLOC;
			timerctl.timers0[i].flags2 = 0;
			return &timerctl.timers0[i];
		}
	}
	return 0;
}

void timer_free(struct TIMER *timer)
{
	timer->flags = 0;
	return;
}

void timer_init(struct TIMER *timer, struct FIFO32 *fifo, int data)
{
	timer->fifo = fifo;
	timer->data = data;
	return;
}


/**
 * @brief 타이머 설정 함수
 * 
 * 타이머를 지정된 시간 후에 인터럽트가 발생하도록 설정
 * 
 * @param timer: 설정할 타이머 포인터
 * @param timeout: 타이머가 인터럽트를 발생시킬 때까지의 시간 (PIT 틱 단위)
 */
void timer_settime(struct TIMER *timer, unsigned int timeout)
{
	int e;
	struct TIMER *t, *s;
	timer->timeout = timeout + timerctl.count;
	timer->flags = TIMER_FLAGS_USING;
	e = io_load_eflags();
	io_cli();
	t = timerctl.t0;
	if (timer->timeout <= t->timeout) {
		timerctl.t0 = timer;
		timer->next = t;
		timerctl.next = timer->timeout;
		io_store_eflags(e);
		return;
	}
	for (;;) {
		s = t;
		t = t->next;
		if (timer->timeout <= t->timeout) {
			s->next = timer;
			timer->next = t;
			io_store_eflags(e);
			return;
		}
	}
}

int timer_cancel(struct TIMER *timer)
{
	int e;
	struct TIMER *t;
	e = io_load_eflags();
	io_cli();
	if (timer->flags == TIMER_FLAGS_USING) {
		if (timer == timerctl.t0) {
			t = timer->next;
			timerctl.t0 = t;
			timerctl.next = t->timeout;
		} else {
			t = timerctl.t0;
			for (;;) {
				if (t->next == timer) {
					t->next = timer->next;
					break;
				}
				t = t->next;
			}
			t->next = timer->next;
		}
		timer->flags = TIMER_FLAGS_ALLOC;
		io_store_eflags(e);
		return 1;	// cancelled successfully
	}
	io_store_eflags(e);
	return 0; // was not needed to cancel
}

void timer_cancelall(struct FIFO32 *fifo)
{
	int e, i;
	struct TIMER *t;
	e = io_load_eflags();
	io_cli(); // disable CPU interrupts
	for (i=0; i<MAX_TIMER; i++) {
		t = &timerctl.timers0[i];
		if (t->flags != 0 && t->flags2 != 0 && t->fifo == fifo) {
			timer_cancel(t);
			timer_free(t);
		}
	}
	io_store_eflags(e); // restore CPU interrupt flag
	return;
}

void inthandler20(int *esp)
{
	struct TIMER *timer;
    char ts = 0;
	io_out8(PIC0_OCW2, 0x60);
	timerctl.count++;
	if (timerctl.next > timerctl.count) {
		return;
	}
	timer = timerctl.t0;
	for (;;) {
		if (timer->timeout > timerctl.count) {
			break;
		}
		timer->flags = TIMER_FLAGS_ALLOC;
        if (timer != task_timer) {
            fifo32_put(timer->fifo, timer->data);
        } else {
            ts = 1; // raise task switch flag
        }
		timer = timer->next;
	}
	timerctl.t0 = timer;
	timerctl.next = timer->timeout;
    if (ts != 0) {
        task_switch();
    }
	return;
}
