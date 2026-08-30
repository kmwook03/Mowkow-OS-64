/** 
 * @file dsctbl.c
 * 
 * @brief GDT/IDT 구조체 정의 및 초기화 함수 구현
 * 
 * */
#include "../include/bootpack.h"

/**
 * @brief GDT/IDT 초기화 함수
 * 
 * GDT와 IDT를 초기화하고 필요한 게이트 디스크립터를 설정한다.
 * 
 * @return: void
 */
void init_gdtidt(void)
{
    struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *) ADR_GDT; // GDT range 0x00270000 - 0x0027ffff
    struct GATE_DESCRIPTOR    *idt = (struct GATE_DESCRIPTOR    *) ADR_IDT; // IDT range 0x0026f800 - 0x0026ffff
    int i;

    // GDT 초기화
    // 모든 세그먼트 디스크립터(8192개 엔트리)를 0으로 설정(limit=0, base=0, ar=0)
    for (i = 0; i<LIMIT_GDT/8; i++) {
        set_segmdesc(gdt + i, 0, 0, 0); // gdt + 1 = gdt + sizeof(SEGMENT_DESCRIPTOR) * 1
    }

    // 첫 세그먼트는 전체 메모리 영역을 나타냄
    set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, AR_DATA32_RW);
    // 두 번째 세그먼트는 부트팩 영역을 나타냄
    set_segmdesc(gdt + 2, LIMIT_BOTPAK, ADR_BOTPAK, AR_CODE32_ER);
    
    load_gdtr(LIMIT_GDT, ADR_GDT);

    // IDT 초기화
    for (i = 0; i < LIMIT_IDT/8; i++) {
        set_gatedesc(idt + i, 0, 0, 0);
    }
    load_idtr(LIMIT_IDT, ADR_IDT);
    // 필요한 인터럽트 핸들러 설정
    set_gatedesc(idt + 0x0c, (int) asm_inthandler0c, 2 * 8, AR_INTGATE32);              // 스택 예외 인터럽트
    set_gatedesc(idt + 0x0d, (int) asm_inthandler0d, 2 * 8, AR_INTGATE32);              // 보호 예외 인터럽트
    set_gatedesc(idt + 0x20, (int) asm_inthandler20, 2 * 8, AR_INTGATE32);              // 타이머 인터럽트
    set_gatedesc(idt + 0x21, (int) asm_inthandler21, 2 * 8, AR_INTGATE32);              // 키보드
    set_gatedesc(idt + 0x26, (int) asm_inthandler26, 2 * 8, AR_INTGATE32);              // FDC
    set_gatedesc(idt + 0x27, (int) asm_inthandler27, 2 * 8, AR_INTGATE32);              // PIC1에서 오는 IRQ-07(스퓨리어스 인터럽트)
    set_gatedesc(idt + 0x2c, (int) asm_inthandler2c, 2 * 8, AR_INTGATE32);              // 마우스
    set_gatedesc(idt + 0x40, (int) asm_hrb_api,      2 * 8, AR_INTGATE32 + 0x60);       // 소프트웨어 인터럽트(API 호출)

    return;
}

/**
 * @brief 세그먼트 디스크립터 설정 함수
 * 
 * @param sd: 세그먼트 디스크립터 주소
 * @param limit: 세그먼트 한계 (페이지 단위)
 * @param base: 세그먼트 시작 주소
 * @param ar: 접근 권한
 */
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar)
{
    if (limit > 0xfffff) {
        ar |= 0x8000; // G_bit = 1
        limit /= 0x1000;
    }
    sd->limit_low    = limit & 0xffff;
    sd->base_low     = base & 0xffff;
    sd->base_mid     = (base >> 16) & 0xff;
    sd->access_right = ar & 0xff;
    sd->limit_high   = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
    sd->base_high    = (base >> 24) & 0xff;
    return;
}

/**
 * @brief 게이트 디스크립터 설정 함수
 * 
 * @param gd: 게이트 디스크립터 주소
 * @param offset: 인터럽트 핸들러 주소
 * @param selector: 코드 세그먼트 선택자
 * @param ar: 접근 권한
 */
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar)
{
    gd->offset_low = offset & 0xffff;
    gd->selector = selector;
    gd->dw_count = (ar >> 8) & 0xff;
    gd->access_right = ar & 0xff;
    gd->offset_high = (offset >> 16) & 0xffff;
    return;
}
