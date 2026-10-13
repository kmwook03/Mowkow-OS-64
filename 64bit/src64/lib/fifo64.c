/*
 * fifo64.c -- 이벤트 큐
 *
 * 인터럽트 핸들러가 넣고 태스크가 꺼내 간다. 넣을 때 받는 태스크가 자고
 * 있으면 깨우므로, 소비자는 큐가 빌 때마다 잠들어도 이벤트를 놓치지 않는다.
 */
#include <arch/platform64.h>
#include <fifo64.h>
#include <mtask64.h>

static int fifo64_validate_nolock(const struct FIFO64 *fifo)
{
	if (fifo == NULL || fifo->buf == NULL || fifo->size == 0 ||
			fifo->free > fifo->size || fifo->p >= fifo->size ||
			fifo->q >= fifo->size) {
		return FIFO64_ERR_INVALID;
	}
	return 0;
}

int fifo64_init(struct FIFO64 *fifo, uint32_t size, struct EVENT64 *buf,
	struct TASK64 *task)
{
	if (fifo == NULL) {
		return FIFO64_ERR_INVALID;
	}
	fifo->size = 0;
	fifo->free = 0;
	fifo->flags = 0;
	fifo->p = 0;
	fifo->q = 0;
	fifo->overruns = 0;
	fifo->buf = NULL;
	fifo->task = NULL;
	if (size == 0 || buf == NULL) {
		return FIFO64_ERR_INVALID;
	}
	fifo->size = size;
	fifo->buf = buf;
	fifo->free = size;
	fifo->task = task;
	return 0;
}

int fifo64_put(struct FIFO64 *fifo, struct EVENT64 data)
{
	struct TASK64 *wake_task;
	uint64_t irq_state;
	int status;

	wake_task = NULL;
	irq_state = platform_irq_save64();
	status = fifo64_validate_nolock(fifo);
	if (status != 0) {
		goto out;
	}
	if (fifo->free == 0) {
		fifo->flags |= FIFO64_FLAGS_OVERRUN;
		if (fifo->overruns != UINT64_MAX) {
			fifo->overruns++;
		}
		status = FIFO64_ERR_FULL;
		goto out;
	}
	fifo->buf[fifo->p] = data;
	fifo->p++;
	if (fifo->p == fifo->size) {
		fifo->p = 0;
	}
	fifo->free--;
	if (fifo->task != NULL &&
			(fifo->task->flags == TASK64_FLAGS_ALLOCATED ||
			fifo->task->flags == TASK64_FLAGS_SLEEP_PENDING)) {
		wake_task = fifo->task;
	}
	status = 0;
out:
	platform_irq_restore64(irq_state);
	if (wake_task != NULL) {
		(void) task_run64(wake_task, -1, 0);
	}
	return status;
}

int fifo64_get(struct FIFO64 *fifo, struct EVENT64 *data)
{
	uint64_t irq_state;
	int status;

	irq_state = platform_irq_save64();
	status = fifo64_validate_nolock(fifo);
	if (status != 0 || data == NULL) {
		status = FIFO64_ERR_INVALID;
		goto out;
	}
	if (fifo->free == fifo->size) {
		status = FIFO64_ERR_EMPTY;
		goto out;
	}
	*data = fifo->buf[fifo->q];
	fifo->q++;
	if (fifo->q == fifo->size) {
		fifo->q = 0;
	}
	fifo->free++;
	status = 0;
out:
	platform_irq_restore64(irq_state);
	return status;
}

uint32_t fifo64_status(const struct FIFO64 *fifo)
{
	uint64_t irq_state;
	uint32_t count;

	irq_state = platform_irq_save64();
	if (fifo64_validate_nolock(fifo) != 0) {
		count = 0;
	} else {
		count = fifo->size - fifo->free;
	}
	platform_irq_restore64(irq_state);
	return count;
}

uint64_t fifo64_overruns(const struct FIFO64 *fifo)
{
	uint64_t irq_state;
	uint64_t overruns;

	irq_state = platform_irq_save64();
	if (fifo64_validate_nolock(fifo) != 0) {
		overruns = 0;
	} else {
		overruns = fifo->overruns;
	}
	platform_irq_restore64(irq_state);
	return overruns;
}
