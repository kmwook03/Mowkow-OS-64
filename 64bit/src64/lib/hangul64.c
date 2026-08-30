/*
 * hangul64.c -- 두벌식 한글 오토마타와 글자 그리기
 *
 * 자판에서 온 자모를 모아 음절 하나를 만든다. 상태는 초성/중성/종성이 어디
 * 까지 찼는지로 나뉘고, 겹자모와 종성 넘겨주기(예: '갃'에서 'ㅅ'이 다음
 * 글자의 초성이 되는 경우)까지 여기서 처리한다.
 *
 * 그리기는 초성 19 x 중성 21 x 종성 28을 모두 담지 않고, 조합형 글꼴
 * (H04.FNT)의 벌 번호를 골라 세 조각을 겹쳐 찍는 방식이다.
 */
#include <hangul64.h>

static const uint8_t hangul_code[3][32] = {
	{ 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 6, 7, 8, 9, 10, 11, 0, 0, 12, 13, 14, 15, 16, 17, 0, 0, 18, 19, 20, 21, 0, 0},
	{ 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0, 0}
};
static const uint8_t first[2][20] = {
	{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1},
	{0, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 3, 3, 3}
};
static const uint8_t middle[3][22] = {
	{0, 0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0},
	{0, 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5}
};
static const int compat_cho_to_cho_idx[30] = {
	0, 1, -1, 2, -1, -1, 3, 4, 5, -1,
	-1, -1, -1, -1, -1, -1, 6, 7, 8, -1,
	9, 10, 11, 12, 13, 14, 15, 16, 17, 18
};
static const uint8_t u2j_cho[19] = {
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
};
static const uint8_t u2j_jung[21] = {
	3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29
};
static const uint8_t u2j_jong[28] = {
	0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29
};
static const int jong_to_cho_table[28] = {
	-1, 0, 1, -1, 2, -1, -1, 3, 5, -1, -1, -1, -1, -1,
	-1, -1, 6, 7, -1, 9, 10, 11, 12, 14, 15, 16, 17, 18
};
static const char *cho_utf8[19] = {
	"\xe3\x84\xb1", "\xe3\x84\xb2", "\xe3\x84\xb4", "\xe3\x84\xb7", "\xe3\x84\xb8",
	"\xe3\x84\xb9", "\xe3\x85\x81", "\xe3\x85\x82", "\xe3\x85\x83", "\xe3\x85\x85",
	"\xe3\x85\x86", "\xe3\x85\x87", "\xe3\x85\x88", "\xe3\x85\x89", "\xe3\x85\x8a",
	"\xe3\x85\x8b", "\xe3\x85\x8c", "\xe3\x85\x8d", "\xe3\x85\x8e"
};
static const char *jung_utf8[21] = {
	"\xe3\x85\x8f", "\xe3\x85\x90", "\xe3\x85\x91", "\xe3\x85\x92", "\xe3\x85\x93",
	"\xe3\x85\x94", "\xe3\x85\x95", "\xe3\x85\x96", "\xe3\x85\x97", "\xe3\x85\x98",
	"\xe3\x85\x99", "\xe3\x85\x9a", "\xe3\x85\x9b", "\xe3\x85\x9c", "\xe3\x85\x9d",
	"\xe3\x85\x9e", "\xe3\x85\x9f", "\xe3\x85\xa0", "\xe3\x85\xa1", "\xe3\x85\xa2",
	"\xe3\x85\xa3"
};

void hangul64_init(struct HANGUL64 *hangul)
{
	hangul->state = 0;
	hangul->cho = -1;
	hangul->jung = -1;
	hangul->jong = -1;
}

int hangul64_key_to_cho(char c)
{
	switch (c) {
	case 'r': return 0;
	case 'R': return 1;
	case 's': return 2;
	case 'e': return 3;
	case 'E': return 4;
	case 'f': return 5;
	case 'a': return 6;
	case 'q': return 7;
	case 'Q': return 8;
	case 't': return 9;
	case 'T': return 10;
	case 'd': return 11;
	case 'w': return 12;
	case 'W': return 13;
	case 'c': return 14;
	case 'z': return 15;
	case 'x': return 16;
	case 'v': return 17;
	case 'g': return 18;
	default: return -1;
	}
}

int hangul64_key_to_jung(char c)
{
	switch (c) {
	case 'k': return 0;
	case 'o': return 1;
	case 'i': return 2;
	case 'O': return 3;
	case 'j': return 4;
	case 'p': return 5;
	case 'u': return 6;
	case 'P': return 7;
	case 'h': return 8;
	case 'y': return 12;
	case 'n': return 13;
	case 'b': return 17;
	case 'm': return 18;
	case 'l': return 20;
	default: return -1;
	}
}

int hangul64_key_to_jong(char c)
{
	switch (c) {
	case 'r': return 1;
	case 'R': return 2;
	case 's': return 4;
	case 'e': return 7;
	case 'f': return 8;
	case 'a': return 16;
	case 'q': return 17;
	case 't': return 19;
	case 'T': return 20;
	case 'd': return 21;
	case 'w': return 22;
	case 'c': return 23;
	case 'z': return 24;
	case 'x': return 25;
	case 'v': return 26;
	case 'g': return 27;
	default: return -1;
	}
}

int hangul64_compose_utf8(char *dest, const struct HANGUL64 *hangul)
{
	unsigned int unicode;
	const char *src;
	int jong;

	if (hangul->cho != -1 && hangul->jung == -1) {
		src = cho_utf8[hangul->cho];
		dest[0] = src[0];
		dest[1] = src[1];
		dest[2] = src[2];
		dest[3] = 0;
		return 3;
	}
	if (hangul->cho == -1 && hangul->jung != -1) {
		src = jung_utf8[hangul->jung];
		dest[0] = src[0];
		dest[1] = src[1];
		dest[2] = src[2];
		dest[3] = 0;
		return 3;
	}
	if (hangul->cho < 0 || hangul->cho > 18 || hangul->jung < 0 || hangul->jung > 20) {
		return 0;
	}
	jong = hangul->jong == -1 ? 0 : hangul->jong;
	unicode = (unsigned int) (hangul->cho * 588 + hangul->jung * 28 + jong + 0xac00);
	dest[0] = (char) (0xe0 | ((unicode >> 12) & 0x0f));
	dest[1] = (char) (0x80 | ((unicode >> 6) & 0x3f));
	dest[2] = (char) (0x80 | (unicode & 0x3f));
	dest[3] = 0;
	return 3;
}

uint16_t hangul64_to_johab(const struct HANGUL64 *hangul)
{
	uint16_t johab;

	johab = 0x8000;
	if (hangul->cho != -1) {
		johab |= (uint16_t) (u2j_cho[hangul->cho] & 0x1f) << 10;
	}
	if (hangul->jung != -1) {
		johab |= (uint16_t) (u2j_jung[hangul->jung] & 0x1f) << 5;
	}
	if (hangul->jong != -1) {
		johab |= (uint16_t) (u2j_jong[hangul->jong] & 0x1f);
	}
	return johab;
}

uint16_t hangul64_utf8_to_johab(const unsigned char *s)
{
	unsigned int unicode;
	int cho;
	int jung;
	int jong;
	uint16_t johab;

	unicode = ((unsigned int) (s[0] & 0x0f) << 12) |
		((unsigned int) (s[1] & 0x3f) << 6) | (unsigned int) (s[2] & 0x3f);
	if (unicode < 0xac00 || unicode > 0xd7a3) {
		return 0;
	}
	unicode -= 0xac00;
	cho = (int) (unicode / 588);
	unicode %= 588;
	jung = (int) (unicode / 28);
	jong = (int) (unicode % 28);
	johab = 0x8000;
	johab |= (uint16_t) (u2j_cho[cho] & 0x1f) << 10;
	johab |= (uint16_t) (u2j_jung[jung] & 0x1f) << 5;
	johab |= (uint16_t) (u2j_jong[jong] & 0x1f);
	return johab;
}

void hangul64_draw_johab(uint8_t *vram, uint32_t stride, uint16_t x, uint16_t y,
	uint8_t color, const uint8_t *font, uint16_t code)
{
	int cho_val;
	int jung_val;
	int jong_val;
	int cho_idx;
	int jung_idx;
	int jong_idx;
	int jong_exist;
	int cho_type;
	int jung_type;
	int jong_type;
	const uint8_t *p_cho;
	const uint8_t *p_jung;
	const uint8_t *p_jong;
	int row;
	int bit;

	cho_val = (code >> 10) & 0x1f;
	jung_val = (code >> 5) & 0x1f;
	jong_val = code & 0x1f;
	cho_idx = hangul_code[0][cho_val];
	jung_idx = hangul_code[1][jung_val];
	jong_idx = hangul_code[2][jong_val];
	jong_exist = jong_idx != 0;
	cho_type = middle[1 + jong_exist][jung_idx];
	jung_type = first[jong_exist][cho_idx];
	jong_type = middle[0][jung_idx];
	p_cho = font + (cho_type * 20 + cho_idx) * 32;
	p_jung = font + (160 + jung_type * 22 + jung_idx) * 32;
	p_jong = font + (248 + jong_type * 28 + jong_idx) * 32;
	for (row = 0; row < 16; row++) {
		uint8_t line1;
		uint8_t line2;
		uint8_t *p;

		line1 = p_cho[row * 2] | p_jung[row * 2];
		line2 = p_cho[row * 2 + 1] | p_jung[row * 2 + 1];
		if (jong_exist) {
			line1 |= p_jong[row * 2];
			line2 |= p_jong[row * 2 + 1];
		}
		p = vram + (uint32_t) (y + row) * stride + x;
		for (bit = 0; bit < 8; bit++) {
			if ((line1 & (0x80 >> bit)) != 0) {
				p[bit] = color;
			}
			if ((line2 & (0x80 >> bit)) != 0) {
				p[bit + 8] = color;
			}
		}
	}
}

void hangul64_draw_unicode(uint8_t *vram, uint32_t stride, uint16_t x, uint16_t y,
	uint8_t color, const uint8_t *font, unsigned int unicode)
{
	struct HANGUL64 h;
	uint16_t johab;

	if (unicode >= 0xac00 && unicode <= 0xd7a3) {
		unicode -= 0xac00;
		h.cho = (int) (unicode / 588);
		h.jung = (int) ((unicode % 588) / 28);
		h.jong = (int) (unicode % 28);
		h.state = 2;
		johab = hangul64_to_johab(&h);
		hangul64_draw_johab(vram, stride, x, y, color, font, johab);
	} else if (unicode >= 0x3131 && unicode <= 0x314e) {
		h.state = 1;
		h.cho = compat_cho_to_cho_idx[unicode - 0x3131];
		h.jung = -1;
		h.jong = -1;
		if (h.cho != -1) {
			hangul64_draw_johab(vram, stride, x, y, color, font, hangul64_to_johab(&h));
		}
	} else if (unicode >= 0x314f && unicode <= 0x3163) {
		h.state = 0;
		h.cho = -1;
		h.jung = (int) (unicode - 0x314f);
		h.jong = -1;
		hangul64_draw_johab(vram, stride, x, y, color, font, hangul64_to_johab(&h));
	}
}

int hangul64_composite_jung(int jung1, int jung2)
{
	if (jung1 == 8) {
		if (jung2 == 0) return 9;
		if (jung2 == 1) return 10;
		if (jung2 == 20) return 11;
	}
	if (jung1 == 13) {
		if (jung2 == 4) return 14;
		if (jung2 == 5) return 15;
		if (jung2 == 20) return 16;
	}
	if (jung1 == 18 && jung2 == 20) {
		return 19;
	}
	return -1;
}

int hangul64_split_composite_jung(int jung)
{
	if (jung == 9 || jung == 10 || jung == 11) return 8;
	if (jung == 14 || jung == 15 || jung == 16) return 13;
	if (jung == 19) return 18;
	return -1;
}

int hangul64_composite_jong(int jong, int cho)
{
	if (jong == 1 && cho == 9) return 3;
	if (jong == 4) {
		if (cho == 12) return 5;
		if (cho == 18) return 6;
	}
	if (jong == 8) {
		if (cho == 0) return 9;
		if (cho == 6) return 10;
		if (cho == 7) return 11;
		if (cho == 9) return 12;
		if (cho == 16) return 13;
		if (cho == 17) return 14;
		if (cho == 18) return 15;
	}
	if (jong == 17 && cho == 9) return 18;
	return -1;
}

int hangul64_first_jong(int jong)
{
	if (jong == 3) return 1;
	if (jong == 5 || jong == 6) return 4;
	if (jong >= 9 && jong <= 15) return 8;
	if (jong == 18) return 17;
	return -1;
}

int hangul64_second_jong(int jong)
{
	switch (jong) {
	case 3: return 9;
	case 5: return 12;
	case 6: return 18;
	case 9: return 0;
	case 10: return 6;
	case 11: return 7;
	case 12: return 9;
	case 13: return 16;
	case 14: return 17;
	case 15: return 18;
	case 18: return 9;
	default: return -1;
	}
}

int hangul64_jong_to_cho(int jong)
{
	if (jong < 0 || jong >= 28) {
		return -1;
	}
	return jong_to_cho_table[jong];
}
