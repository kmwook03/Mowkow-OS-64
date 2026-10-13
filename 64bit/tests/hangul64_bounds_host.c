#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <hangul64.h>
#include <utf864.h>
#include <window64.h>

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "Hangul bounds check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

const uint8_t hankaku64[4096] = { ['A' * 16] = 0xff, ['?' * 16] = 0xff };
static uint8_t font64[HANGUL64_FONT_SIZE];

static int legacy_glyphs64(void)
{
	uint8_t pixels[256];
	struct SURFACE64 surface = { pixels, sizeof(pixels), 16, 16, 16, 0, 0, 16, 16 };
	uint64_t hash = UINT64_C(14695981039346656037);

	/* Golden hash from the original renderer and repository H04.FNT: every
	 * modern syllable followed by U+3131..U+3163 compatibility jamo.
	 */
	for (unsigned int range = 0; range < 2; range++) {
		unsigned int start = range ? 0x3131 : 0xac00;
		unsigned int end = range ? 0x3163 : 0xd7a3;

		for (unsigned int cp = start; cp <= end; cp++) {
			memset(pixels, 0, sizeof(pixels));
			hangul64_draw_unicode(&surface, 0, 0, 7, font64, sizeof(font64), cp);
			for (size_t i = 0; i < sizeof(pixels); i++) {
				hash = (hash ^ pixels[i]) * UINT64_C(1099511628211);
			}
		}
	}
	CHECK64(hash == UINT64_C(0x16435cb2fff86587));
	return 0;
}

static int states64(void)
{
	static const struct HANGUL64 bad[] = {
		{ 0, 0, -1, -1 }, { 1, 0, 0, -1 }, { 2, -1, 0, -1 },
		{ 2, 0, -1, -1 }, { 2, 0, 0, 1 }, { 3, 0, 0, 0 },
		{ 3, 0, 0, 3 }, { 4, 0, 0, 1 }, { 1, -1, -1, -1 },
		{ INT_MIN, 0, 0, 0 }, { INT_MAX, 0, 0, 0 },
		{ 1, -2, -1, -1 }, { 1, 19, -1, -1 }, { 1, INT_MAX, -1, -1 },
		{ 1, -1, -2, -1 }, { 1, -1, 21, -1 }, { 1, -1, INT_MIN, -1 },
		{ 3, 0, 0, -2 }, { 3, 0, 0, 28 }, { 3, 0, 0, INT_MAX },
	};
	struct HANGUL64 h;
	struct HANGUL64_FEED_RESULT feed;
	char output[4];
	int length;

	hangul64_init(NULL);
	CHECK64(hangul64_compose_utf8(NULL, NULL) == 0);
	CHECK64(hangul64_compose_utf8(output, NULL) == 0 && output[0] == '\0');
	CHECK64(hangul64_to_johab(NULL) == 0);
	CHECK64(hangul64_utf8_to_johab(NULL) == 0);
	CHECK64(hangul64_utf8_to_johab((const unsigned char *) "\xeb\xb0") == 0);
	CHECK64(hangul64_utf8_to_johab((const unsigned char *) "\xeb\x30\x80") == 0);
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		for (size_t k = 0; k < 4; k++) {
			h = bad[i];
			memset(&feed, 0xa5, sizeof(feed));
			CHECK64(hangul64_feed(&h, "rk\b\0"[k], &feed) == -2);
			CHECK64(h.state == 0 && h.cho == -1 && h.jung == -1 && h.jong == -1);
			CHECK64(feed.committed[0] == '\0' && feed.committed_length == 0);
			CHECK64(feed.passthrough == '\0');
		}
	}
	/* Every index combination, including absent and just-out-of-range fields. */
	for (int cho = -2; cho <= 19; cho++) {
		for (int jung = -2; jung <= 21; jung++) {
			for (int jong = -2; jong <= 28; jong++) {
				int valid = cho >= -1 && cho < 19 && jung >= -1 && jung < 21 &&
					jong >= -1 && jong < 28 &&
					(jong <= 0 || (cho >= 0 && jung >= 0));

				h = (struct HANGUL64) { 2, cho, jung, jong };
				memset(output, 0xa5, sizeof(output));
				length = hangul64_compose_utf8(output, &h);
				if (!valid) {
					CHECK64(length == 0 && output[0] == '\0');
					CHECK64(hangul64_to_johab(&h) == 0);
				} else if (cho >= 0 && jung >= 0) {
					int consumed;
					uint32_t scalar = 0xac00U + (uint32_t) cho * 588 +
						(uint32_t) jung * 28 + (uint32_t) (jong < 0 ? 0 : jong);

					CHECK64(length == 3 && output[3] == '\0');
					CHECK64(utf8_to_unicode64(output, &consumed) == scalar);
					CHECK64(hangul64_utf8_to_johab((unsigned char *) output) ==
						hangul64_to_johab(&h));
				} else if (cho >= 0 || jung >= 0) {
					CHECK64(length == 3 && output[3] == '\0');
				} else {
					CHECK64(length == 0);
				}
			}
		}
	}
	return 0;
}

static int clipping64(void)
{
	uint8_t pixels[29 * 19 + 32];
	uint8_t expected[sizeof(pixels)];
	uint8_t glyph[16 * 16] = { 0 };
	struct SURFACE64 source = { glyph, sizeof(glyph), 16, 16, 16, 0, 0, 16, 16 };
	struct SURFACE64 target = { pixels + 16, 29 * 18 + 23, 29, 23, 19, 3, 2, 20, 17 };

	hangul64_draw_unicode(&source, 0, 0, 7, font64, sizeof(font64), 0xac01);
	for (int y = -20; y <= 24; y++) {
		for (int x = -20; x <= 24; x++) {
			memset(pixels, 0xa5, sizeof(pixels));
			memset(expected, 0xa5, sizeof(expected));
			for (int row = 0; row < 16; row++) {
				for (int col = 0; col < 16; col++) {
					int px = x + col, py = y + row;

					if (px >= 3 && px < 20 && py >= 2 && py < 17 &&
						glyph[row * 16 + col] != 0) {
						expected[16 + py * 29 + px] = 7;
					}
				}
			}
			hangul64_draw_unicode(&target, x, y, 7, font64, sizeof(font64), 0xac01);
			CHECK64(memcmp(pixels, expected, sizeof(pixels)) == 0);
		}
	}
	/* Clips may extend outside the surface; padding must never be painted. */
	target.x0 = target.y0 = INT_MIN;
	target.x1 = target.y1 = INT_MAX;
	memset(pixels, 0xa5, sizeof(pixels));
	memset(expected, 0xa5, sizeof(expected));
	for (int row = 0; row < 16; row++) {
		for (int col = 0; col < 16; col++) {
			if (glyph[row * 16 + col] && col + 20 < 23 && row + 17 < 19) {
				expected[16 + (row + 17) * 29 + col + 20] = 7;
			}
		}
	}
	hangul64_draw_unicode(&target, 20, 17, 7, font64, sizeof(font64), 0xac01);
	CHECK64(memcmp(pixels, expected, sizeof(pixels)) == 0);
	hangul64_draw_unicode(&target, INT_MAX, INT_MAX, 7, font64, sizeof(font64), 0xac01);
	hangul64_draw_unicode(&target, INT_MIN, INT_MIN, 7, font64, sizeof(font64), 0xac01);
	CHECK64(memcmp(pixels, expected, sizeof(pixels)) == 0);
	/* ASCII and Hangul in one window string share the same clip. */
	window64_set_hangul_font(font64);
	memset(pixels, 0xa5, sizeof(pixels));
	putstr64(&target, -7, 18, 7, "A각A");
	CHECK64(pixels[16 + 18 * 29] == 7);
	CHECK64(pixels[16 + 18 * 29 + 17] == 7);
	for (size_t i = 0; i < sizeof(pixels); i++) {
		if (i >= 16 + 18 * 29 && i < 16 + 18 * 29 + 23) {
			continue;
		}
		CHECK64(pixels[i] == 0xa5);
	}
	return 0;
}

static int guarded64(void)
{
	long page = sysconf(_SC_PAGESIZE);
	size_t font_pages;
	uint8_t *mapping, *font_end, *pixels;
	struct SURFACE64 target;

	CHECK64(page > 0);
	font_pages = (sizeof(font64) + (size_t) page - 1) / (size_t) page;
	mapping = mmap(NULL, (font_pages + 3) * (size_t) page, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK64(mapping != MAP_FAILED);
	font_end = mapping + font_pages * (size_t) page;
	CHECK64(mprotect(font_end, (size_t) page, PROT_NONE) == 0);
	memcpy(font_end - sizeof(font64), font64, sizeof(font64));
	pixels = font_end + 2 * (size_t) page - 256;
	CHECK64(mprotect(pixels + 256, (size_t) page, PROT_NONE) == 0);
	target = (struct SURFACE64) { pixels, 256, 16, 16, 16, 0, 0, 16, 16 };
	/* All 16-bit codes exercise reserved fields and the largest font index. */
	for (unsigned int code = 0; code <= UINT16_MAX; code++) {
		unsigned int cho = (code >> 10) & 31, jung = (code >> 5) & 31;
		unsigned int jong = code & 31;
		int valid = (code & 0x8000) && (cho == 0 || (cho >= 2 && cho <= 20)) &&
			(jung == 0 || (jung >= 3 && jung <= 7) || (jung >= 10 && jung <= 15) ||
			 (jung >= 18 && jung <= 23) || (jung >= 26 && jung <= 29)) &&
			(jong == 0 || (jong >= 2 && jong <= 17) || (jong >= 19 && jong <= 29)) &&
			(jong == 0 || (cho != 0 && jung != 0));

		memset(pixels, 0xa5, 256);
		hangul64_draw_johab(&target, 0, 0, 7, font_end - sizeof(font64),
			sizeof(font64), (uint16_t) code);
		if (!valid) {
			for (size_t i = 0; i < 256; i++) {
				CHECK64(pixels[i] == 0xa5);
			}
		}
	}
	/* 궇 uses final glyph 359: one missing byte must reject the entire draw. */
	for (size_t size = 0; size < sizeof(font64); size++) {
		memset(pixels, 0xa5, 256);
		hangul64_draw_unicode(&target, 0, 0, 7, font_end - size, size,
			0xac00 + 8 * 28 + 27);
		for (size_t i = 0; i < 256; i++) {
			CHECK64(pixels[i] == 0xa5);
		}
	}
	memset(pixels, 0xa5, 256);
	/* Invalid destinations must be rejected before either font or pixel access. */
	for (unsigned int i = 0; i < 7; i++) {
		struct SURFACE64 bad = target;

		switch (i) {
		case 0: bad.size = 255; break;
		case 1: bad.stride = 15; break;
		case 2: bad.width = -1; break;
		case 3: bad.height = INT_MAX; bad.stride = UINT32_MAX; break;
		case 4: bad.x0 = bad.x1 + 1; break;
		case 5: bad.pixels = NULL; break;
		case 6: bad.pixels = (uint8_t *) (UINTPTR_MAX - 16); break;
		}
		CHECK64(!surface64_valid(&bad));
		hangul64_draw_unicode(&bad, 0, 0, 7, font_end, sizeof(font64), 0xac00);
		putstr64(&bad, 0, 0, 7, "각A");
	}
	hangul64_draw_johab(NULL, 0, 0, 7, font_end, sizeof(font64), 0x8840);
	hangul64_draw_unicode(&target, 0, 0, 7, NULL, sizeof(font64), 0xac00);
	putstr64(&target, INT_MAX, INT_MAX, 7, "AA각");
	putstr64(&target, 0, 0, 7, NULL);
	for (size_t i = 0; i < 256; i++) {
		CHECK64(pixels[i] == 0xa5);
	}
	CHECK64(munmap(mapping, (font_pages + 3) * (size_t) page) == 0);
	return 0;
}

int main(void)
{
	FILE *font = fopen("common/font/H04.FNT", "rb");

	CHECK64(font != NULL);
	CHECK64(fread(font64, 1, sizeof(font64), font) == sizeof(font64));
	CHECK64(fclose(font) == 0);
	CHECK64(legacy_glyphs64() == 0);
	CHECK64(states64() == 0);
	CHECK64(clipping64() == 0);
	CHECK64(guarded64() == 0);
	puts("Hangul state, font bounds and clipped drawing: PASS");
	return 0;
}
