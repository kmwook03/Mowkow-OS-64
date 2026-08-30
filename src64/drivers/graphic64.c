/*
 * VGA DAC 팔레트 -- src/drivers/graphic.c:22-80의 64비트 이식.
 */

#include <asmfunc64.h>
#include <graphic64.h>
#include <stdint.h>

#define PALETTE64_DAC_INDEX 0x03c8
#define PALETTE64_DAC_DATA  0x03c9

/* 32비트 트리와 같은 16색. 인덱스 7이 흰색, 15가 어두운 회색이다. */
static const uint8_t base_rgb[16 * 3] = {
	0x00, 0x00, 0x00,   /*  0 검정 */
	0xff, 0x00, 0x00,   /*  1 밝은 빨강 */
	0x00, 0xff, 0x00,   /*  2 밝은 초록 */
	0xff, 0xff, 0x00,   /*  3 밝은 노랑 */
	0x00, 0x00, 0xff,   /*  4 밝은 파랑 */
	0xff, 0x00, 0xff,   /*  5 밝은 보라 */
	0x00, 0xff, 0xff,   /*  6 밝은 하늘색 */
	0xff, 0xff, 0xff,   /*  7 흰색 */
	0xc6, 0xc6, 0xc6,   /*  8 연한 회색 */
	0x84, 0x00, 0x00,   /*  9 어두운 빨강 */
	0x00, 0x84, 0x00,   /* 10 어두운 초록 */
	0x84, 0x84, 0x00,   /* 11 어두운 노랑 */
	0x00, 0x00, 0x84,   /* 12 어두운 파랑 */
	0x84, 0x00, 0x84,   /* 13 어두운 보라 */
	0x00, 0x84, 0x84,   /* 14 어두운 하늘색 */
	0x84, 0x84, 0x84    /* 15 어두운 회색 */
};

static uint8_t cube_rgb[PALETTE64_APP_COUNT * 3];

void set_palette64(int32_t start, int32_t end, const uint8_t *rgb)
{
	uint64_t rflags;
	int32_t i;

	rflags = io_load_rflags();
	io_cli();
	io_out8(PALETTE64_DAC_INDEX, (uint8_t) start);
	for (i = start; i <= end; i++) {
		/* DAC는 채널당 6비트라 8비트 값을 4로 나눈다. */
		io_out8(PALETTE64_DAC_DATA, rgb[0] / 4);
		io_out8(PALETTE64_DAC_DATA, rgb[1] / 4);
		io_out8(PALETTE64_DAC_DATA, rgb[2] / 4);
		rgb += 3;
	}
	io_store_rflags(rflags);
}

static void build_cube(void)
{
	int32_t r, g, b;

	for (b = 0; b < 6; b++) {
		for (g = 0; g < 6; g++) {
			for (r = 0; r < 6; r++) {
				cube_rgb[(r + g * 6 + b * 36) * 3 + 0] = (uint8_t) (r * 51);
				cube_rgb[(r + g * 6 + b * 36) * 3 + 1] = (uint8_t) (g * 51);
				cube_rgb[(r + g * 6 + b * 36) * 3 + 2] = (uint8_t) (b * 51);
			}
		}
	}
}

void init_palette64(void)
{
	build_cube();
	set_palette64(0, 15, base_rgb);
	set_palette64(PALETTE64_APP_START, PALETTE64_APP_END, cube_rgb);
}

void palette64_install(const uint8_t *rgb)
{
	set_palette64(PALETTE64_APP_START, PALETTE64_APP_END, rgb);
}

void palette64_restore(void)
{
	set_palette64(PALETTE64_APP_START, PALETTE64_APP_END, cube_rgb);
}
