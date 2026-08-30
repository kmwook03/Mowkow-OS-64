/**
 * @file keyboard.c
 * 
 * @brief 키보드 인터럽트 핸들러 및 초기화 함수 구현
 * 
 */
#include "../include/bootpack.h"

struct FIFO32 *keyfifo;
int keydata0;

/**
 * @brief 키보드 인터럽트 핸들러
 * 
 * 키보드에서 데이터가 들어왔을 때 호출되는 인터럽트 핸들러
 * 
 * @param esp: 스택 포인터
 * @return: void
 */
void inthandler21(int *esp)
{
    int data;
    io_out8(PIC0_OCW2, 0x61); // PIC0에 IRQ-01이 처리되었음을 알림
    data = io_in8(PORT_KEYDAT);
    fifo32_put(keyfifo, data + keydata0);
    
    return;
}

#define PORT_KEYSTA             0x0064
#define KEYSTA_SEND_NOTREADY    0x02
#define KEYCMD_WRITE_MODE       0x60
#define KBC_MODE                0x47

/**
 * @brief 키보드 컨트롤러 준비 대기 함수
 * 
 * 키보드 컨트롤러가 데이터를 보낼 준비가 될 때까지 대기
 * 
 * @return: void
 */
void wait_KBC_sendready(void)
{
    for (;;) {
        if ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) { 
            return;
        }
    }
}

/**
 * @brief 키보드 초기화 함수
 * 
 * 키보드 컨트롤러를 초기화하고 FIFO 버퍼 설정
 * 
 * @param fifo: 키보드 데이터 저장용 FIFO 버퍼 포인터
 * @param data0: 키보드 데이터에 더해질 오프셋 값
 * @return: void
 */
void init_keyboard(struct FIFO32 *fifo, int data0)
{
    keyfifo = fifo;
    keydata0 = data0;
    wait_KBC_sendready();
    io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
    wait_KBC_sendready();
    io_out8(PORT_KEYDAT, KBC_MODE);
    return;
}
