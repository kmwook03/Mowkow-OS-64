#include <fifo64.h>

void fifo64_init(struct FIFO64 *fifo, uint32_t size, struct EVENT64 *buf)
{
	fifo->size = size;
	fifo->buf = buf;
	fifo->free = size;
	fifo->flags = 0;
	fifo->p = 0;
	fifo->q = 0;
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
