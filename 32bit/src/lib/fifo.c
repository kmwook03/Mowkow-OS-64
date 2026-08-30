/**
 * @file fifo.c
 * 
 * @brief FIFO 버퍼 관련 함수 구현
 * 
 */

#include "../include/bootpack.h"

#define FLAGS_OVERRUN    0x0001

/**
 * @brief 32비트 FIFO 버퍼 초기화 함수
 * 
 * @param fifo: 초기화할 FIFO 버퍼 포인터
 * @param size: 버퍼 크기
 * @param buf: 버퍼 배열 포인터
 * @param task: 관련 태스크 포인터
 * @return: void
 */
void fifo32_init(struct FIFO32 *fifo, int size, int *buf, struct TASK *task)
{
    fifo->size = size;
    fifo->buf = buf;
    fifo->free = size; // number of free space, fifo->free = size => empty
    fifo->flags = 0;
    fifo->p = 0;    // write position
    fifo->q = 0;    // read position
    fifo->task = task;
    return;
}

/**
 * @brief 32비트 FIFO 버퍼에 데이터 넣기 함수
 * 
 * @param fifo: 데이터 넣을 FIFO 버퍼 포인터
 * @param data: 넣을 데이터
 * @return: 0 성공, -1 실패 (버퍼가 가득 참)
 */
int fifo32_put(struct FIFO32 *fifo, int data)
{
    if (fifo->free == 0) { // 버퍼 가득 참
        fifo->flags |= FLAGS_OVERRUN;
        return -1;
    }
    fifo->buf[fifo->p] = data;
    fifo->p++;
    if (fifo->p == fifo->size) {
        fifo->p = 0;
    }
    fifo->free--;
    if (fifo->task != 0) {
        if (fifo->task->flags != 2) {       // 태스크가 슬립 상태라면
            task_run(fifo->task, -1, 0);    // 깨운다
        }
    }
    return 0;
}

/**
 * @brief 32비트 FIFO 버퍼에서 데이터 꺼내기 함수
 * 
 * @param fifo: 데이터 꺼낼 FIFO 버퍼 포인터
 * @return: 꺼낸 데이터, 버퍼가 비어있을 경우 -1
 */
int fifo32_get(struct FIFO32 *fifo)
{
    int data;
    if (fifo->free == fifo->size) {
        return -1; // 버퍼가 비어있음
    }
    data = fifo->buf[fifo->q];
    fifo->q++;
    if (fifo->q == fifo->size) {
        fifo->q = 0;
    }
    fifo->free++;
    return data;
}

/**
 * @brief 32비트 FIFO 버퍼 상태 확인 함수
 * 
 * @param fifo: 상태 확인할 FIFO 버퍼 포인터
 * @return: 버퍼에 저장된 데이터 개수
 */
int fifo32_status(struct FIFO32 *fifo)
{
    return fifo->size - fifo->free;
}
