/**
 * @file mouse.c
 * 
 * @brief 마우스 인터럽트 핸들러 및 초기화 함수 구현
 * 
 * 주의: PS/2 마우스 사용 가정, 마우스 데이터는 3바이트로 구성 (버튼 상태, x 이동량, y 이동량)
 * 
 */
#include "../include/bootpack.h"

struct FIFO32 *mousefifo;
int mousedata0;

/**
 * @brief 마우스 인터럽트 핸들러
 * 
 * 마우스에서 데이터를 수신하여 FIFO 버퍼에 저장
 * 
 * @param esp: 인터럽트 스택 포인터 (사용하지 않음)
 */
void inthandler2c(int *esp)
{
    unsigned char data;
    io_out8(PIC1_OCW2, 0x64);
    io_out8(PIC0_OCW2, 0x62);
    data = io_in8(PORT_KEYDAT);
    fifo32_put(mousefifo, data + mousedata0);
    return;
}

#define KEYCMD_SENDTO_MOUSE    0xd4
#define MOUSECMD_ENABLE        0xf4

/**
 * @brief 마우스 활성화 함수
 * 
 * 마우스 인터럽트 핸들러 설정 및 마우스 활성화 명령 전송
 * 
 * @param fifo: 마우스 데이터를 저장할 FIFO 버퍼 포인터
 * @param data0: 마우스 데이터에 더할 값 (보통 512)
 * @param mdec: 마우스 디코딩 구조체 포인터 (ACK 수신 대기용)
 * 
 * @return void
 */
void enable_mouse(struct FIFO32 *fifo, int data0, struct MOUSE_DEC *mdec)
{
    mousefifo = fifo;
    mousedata0 = data0;
    wait_KBC_sendready();
    io_out8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
    wait_KBC_sendready();
    io_out8(PORT_KEYDAT, MOUSECMD_ENABLE);
    // 성공 시 ACK(0xfa) 수신
    mdec->phase = 0; // ACK(0xfa) 수신 대기
    return;
}

/**
 * @brief PS/2 마우스 데이터 디코딩 함수
 * 
 * 마우스에서 송신한 데이터를 3바이트씩 받아서 버튼 상태, x/y 이동량으로 디코딩
 * 
 * @param mdec: 마우스 디코딩 구조체 포인터
 * @param dat: 마우스에서 수신한 데이터 바이트
 * 
 * @return int: 디코딩 성공 시 1, ACK 대기 중이면 0, 그 외는 -1
 */
int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat)
{
    if (mdec->phase == 0) {
        // ACK(0xfa) 수신
        if (dat == 0xfa) {
            mdec->phase = 1; // 첫 바이트 수신 대기
        }
        return 0;
    }
    if (mdec->phase == 1) {
        // 첫 바이트 수신 대기
        if ((dat & 0xc8) == 0x08) {
            // valid first byte
            mdec->buf[0] = dat; // 버퍼에 첫 바이트 저장
            mdec->phase = 2; // 두 번째 바이트 수신 대기
        }
        return 0;
    }
    if (mdec->phase == 2) {
        // 두 번째 바이트 수신 대기
        mdec->buf[1] = dat; // 버퍼에 두 번째 바이트 저장
        mdec->phase = 3; // 세 번째 바이트 수신 대기
        return 0;
    }
    if (mdec->phase == 3) {
        // 세 번째 바이트 수신 대기
        mdec->buf[2] = dat;     // 버퍼에 세 번째 바이트 저장
        mdec->phase = 1;        // 다음 데이터 수신 대기
        mdec->btn = mdec->buf[0] & 0x07; // 버튼 상태 (하위 3비트)
        mdec->x = mdec->buf[1]; // x 이동량
        mdec->y = mdec->buf[2]; // y 이동량
        if ((mdec->buf[0] & 0x10) != 0) {
            mdec->x |= 0xffffff00;
        }
        if ((mdec->buf[0] & 0x20) != 0) {
            mdec->y |= 0xffffff00;
        }
        mdec->y = -mdec->y; // y축 반전
        return 1;
    }
    return -1; // 예상치 못한 상태
}
