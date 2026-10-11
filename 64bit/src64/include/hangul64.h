#ifndef MOWKOW64_HANGUL64_H
#define MOWKOW64_HANGUL64_H

#include <draw64.h>

#define HANGUL64_FONT_SIZE 11520U

struct HANGUL64 {
	int state;
	int cho;
	int jung;
	int jong;
};

struct HANGUL64_FEED_RESULT {
	char committed[7];
	uint8_t committed_length;
	char passthrough;
};

void hangul64_init(struct HANGUL64 *hangul);
int hangul64_key_to_cho(char c);
int hangul64_key_to_jung(char c);
int hangul64_key_to_jong(char c);
int hangul64_compose_utf8(char *dest, const struct HANGUL64 *hangul);
uint16_t hangul64_to_johab(const struct HANGUL64 *hangul);
/* One NUL-terminated UTF-8 character, or an exact three-byte Hangul scalar. */
uint16_t hangul64_utf8_to_johab(const unsigned char *s);
/* Malformed codes, short fonts and invalid destinations are a no-op. */
void hangul64_draw_johab(const struct SURFACE64 *surface, int32_t x, int32_t y,
	uint8_t color, const uint8_t *font, size_t font_size, uint16_t code);
void hangul64_draw_unicode(const struct SURFACE64 *surface, int32_t x, int32_t y,
	uint8_t color, const uint8_t *font, size_t font_size, unsigned int unicode);
int hangul64_composite_jung(int jung1, int jung2);
int hangul64_split_composite_jung(int jung);
int hangul64_composite_jong(int jong, int cho);
int hangul64_first_jong(int jong);
int hangul64_second_jong(int jong);
int hangul64_jong_to_cho(int jong);
/* Printable Dubeolsik key: commit/passthrough plus the updated preedit state.
 * '\0' commits and clears preedit; '\b' removes one composing element, or
 * passes backspace through when no preedit exists. Neither edits committed
 * text. Return 0, -1 for NULL arguments, or -2 for invalid state/components
 * (reset to empty, with no output). compose/to_johab return 0 for invalid
 * components; compose requires a destination of at least four bytes.
 */
int hangul64_feed(struct HANGUL64 *hangul, char key,
	struct HANGUL64_FEED_RESULT *result);

#endif
