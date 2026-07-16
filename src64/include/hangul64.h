#ifndef MOWKOW64_HANGUL64_H
#define MOWKOW64_HANGUL64_H

#include <stdint.h>

struct HANGUL64 {
	int state;
	int cho;
	int jung;
	int jong;
};

void hangul64_init(struct HANGUL64 *hangul);
int hangul64_key_to_cho(char c);
int hangul64_key_to_jung(char c);
int hangul64_key_to_jong(char c);
int hangul64_compose_utf8(char *dest, const struct HANGUL64 *hangul);
uint16_t hangul64_to_johab(const struct HANGUL64 *hangul);
uint16_t hangul64_utf8_to_johab(const unsigned char *s);
void hangul64_draw_johab(uint8_t *vram, uint32_t stride, uint16_t x, uint16_t y,
	uint8_t color, const uint8_t *font, uint16_t code);
void hangul64_draw_unicode(uint8_t *vram, uint32_t stride, uint16_t x, uint16_t y,
	uint8_t color, const uint8_t *font, unsigned int unicode);
int hangul64_composite_jung(int jung1, int jung2);
int hangul64_split_composite_jung(int jung);
int hangul64_composite_jong(int jong, int cho);
int hangul64_first_jong(int jong);
int hangul64_second_jong(int jong);
int hangul64_jong_to_cho(int jong);

#endif
