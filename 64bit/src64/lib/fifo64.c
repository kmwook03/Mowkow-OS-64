/*
 * fifo64.c -- 이벤트 큐
 *
 * 인터럽트 핸들러가 넣고 태스크가 꺼내 간다. 넣을 때 받는 태스크가 자고
 * 있으면 깨우므로, 소비자는 큐가 빌 때마다 잠들어도 이벤트를 놓치지 않는다.
 */
#include <fifo64.h>
#include <mtask64.h>

void fifo64_init(struct FIFO64 *fifo, uint32_t size, struct EVENT64 *buf, struct TASK64 *task)
{
	fifo->size = size;
	fifo->buf = buf;
	fifo->free = size;
	fifo->flags = 0;
	fifo->p = 0;
	fifo->q = 0;
	fifo->task = task;
}

int fifo64_put(struct FIFO64 *fifo, struct EVENT64 data)
{
	if (fifo->free == 0) {
		fifo->flags |= FIFO64_FLAGS_OVERRUN;
		return -1;
	}
	fifo->buf[fifo->p] = data;
	fifo->p++;
	if (fifo->p == fifo->size) {
		fifo->p = 0;
	}
	fifo->free--;
	if (fifo->task != 0 && fifo->task->flags != TASK64_FLAGS_RUNNING) {
		task_run64(fifo->task, -1, 0);
	}
	return 0;
}

int fifo64_get(struct FIFO64 *fifo, struct EVENT64 *data)
{
	if (fifo->free == fifo->size) {
		return -1;
	}
	*data = fifo->buf[fifo->q];
	fifo->q++;
	if (fifo->q == fifo->size) {
		fifo->q = 0;
	}
	fifo->free++;
	return 0;
}

uint32_t fifo64_status(const struct FIFO64 *fifo)
{
	return fifo->size - fifo->free;
}
