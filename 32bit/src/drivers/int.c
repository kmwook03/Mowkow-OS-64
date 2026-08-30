/**
 * @file int.c
 * 
 * @brief 인터럽트 관련 함수 구현
 * 
 * PIC 초기화 및 스푸리어스 인터럽트 핸들러 구현
 * 
 */

#include "../include/bootpack.h"
#include <stdio.h>

/**
 * @brief PIC 초기화 함수
 * 
 * 마스터 PIC와 슬레이브 PIC를 초기화하고 인터럽트를 설정
 * 
 * @return: void
 */
void init_pic(void)
{
    io_out8(PIC0_IMR, 0xff); // 모든 인터럽트 비활성화
    io_out8(PIC1_IMR, 0xff); // 모든 인터럽트 비활성화

    io_out8(PIC0_ICW1, 0x11);   // edge trigger mode
    io_out8(PIC0_ICW2, 0x20);   // IRQ0-7은 INT20-27에서 받음
    io_out8(PIC0_ICW3, 1 << 2); // PIC1를 IRQ2에 연결
    io_out8(PIC0_ICW4, 0x01);   // non-buffer mode

    io_out8(PIC1_ICW1, 0x11);   // edge trigger mode
    io_out8(PIC1_ICW2, 0x28);   // IRQ8-15는 INT28-2f에서 받음
    io_out8(PIC1_ICW3, 2);      // PIC1를 IRQ2에 연결
    io_out8(PIC1_ICW4, 0x01);   // non-buffer mode

    // io_out8(PIC0_IMR, 0xfb); // 11111011 : PIC1 제외하고 비활성화 (PIC1만 활성화)
    io_out8(PIC0_IMR, 0xb8); // 10111000 : IRQ 0,1,2,6 활성화
    io_out8(PIC1_IMR, 0xff); // 11111111 : 모든 인터럽트 비활성화

    return;
}

/**
 * @brief IRQ-07 스푸리어스 인터럽트 핸들러
 * 
 * PIC 초기화 시 가끔 발생하는 스푸리어스 인터럽트를 처리
 * 
 * @param esp: 스택 포인터
 * @return: void
 */
void inthandler27(int *esp)
{
    // PIC1에서 IRQ-07 스퓨리어스 인터럽트 발생 시 처리
    io_out8(PIC0_OCW2, 0x67); // IRQ-07 핸들링이 끝났다고 알림
    return;
}
