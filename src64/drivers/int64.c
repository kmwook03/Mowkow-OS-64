#include <asmfunc64.h>
#include <int64.h>

void init_pic64(void)
{
	io_out8(PIC0_IMR, 0xff);
	io_out8(PIC1_IMR, 0xff);

	io_out8(PIC0_ICW1, 0x11);
	io_out8(PIC0_ICW2, 0x20);
	io_out8(PIC0_ICW3, 1 << 2);
	io_out8(PIC0_ICW4, 0x01);

	io_out8(PIC1_ICW1, 0x11);
	io_out8(PIC1_ICW2, 0x28);
	io_out8(PIC1_ICW3, 2);
	io_out8(PIC1_ICW4, 0x01);

	/* 마스터: IRQ0 타이머, IRQ1 키보드, IRQ2 캐스케이드(슬레이브가 여기 달림).
	   슬레이브: IRQ12 마우스. */
	io_out8(PIC0_IMR, 0xf8);
	io_out8(PIC1_IMR, 0xef);
}
