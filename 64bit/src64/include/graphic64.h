#ifndef MOWKOW64_GRAPHIC64_H
#define MOWKOW64_GRAPHIC64_H

#include <stdint.h>

/*
 * 8bpp 팔레트. 로드맵 decision 3c에 따라 0-15는 예약이며 어떤 창도 바꿀 수
 * 없다 -- 한글 콘솔이 그 두 색(0 배경, 7 글자)으로 그려지기 때문이다.
 * 창이 쓸 수 있는 범위는 16-231(6x6x6 색 큐브)이다.
 */
#define PALETTE64_APP_START 16
#define PALETTE64_APP_END   231
#define PALETTE64_APP_COUNT (PALETTE64_APP_END - PALETTE64_APP_START + 1)

void set_palette64(int32_t start, int32_t end, const uint8_t *rgb);
void init_palette64(void);

/* 창이 자기 색표를 건다. rgb는 PALETTE64_APP_COUNT * 3 바이트.
   0-15는 건드리지 않는다. */
void palette64_install(const uint8_t *rgb);
/* 기본 색 큐브로 되돌린다. 창이 아니라 컴포지터가 책임진다. */
void palette64_restore(void);

#endif
