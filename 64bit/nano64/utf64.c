#include <stdint.h>
#include <utf864.h>

#include "include/nano_errno64.h"
#include "include/nano_utf64.h"

int nano_mbtowc64(uint32_t *wide, const char *text, size_t available)
{
	uint32_t scalar;
	size_t consumed;

	if (text == NULL) { return 0; }
	if (utf8_decode64(text, available, &scalar, &consumed) != 0) {
		nano_errno64 = NANO_EILSEQ64;
		return -1;
	}
	if (wide != NULL) { *wide = scalar; }
	return scalar == 0 ? 0 : (int) consumed;
}

int nano_wctomb64(char *text, uint32_t wide)
{
	int count;

	if (text == NULL) { return 0; }
	count = unicode_to_utf864(wide, text, 4);
	if (count == 0) {
		nano_errno64 = NANO_EILSEQ64;
		return -1;
	}
	return count;
}

int nano_wcwidth64(uint32_t wide)
{
	uint32_t scalar = wide;

	if (scalar == 0) { return 0; }
	if (scalar < 0x20 || (scalar >= 0x7f && scalar < 0xa0) ||
		(scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff) {
		return -1;
	}
	if ((scalar >= 0xac00 && scalar <= 0xd7a3) ||
		(scalar >= 0x3131 && scalar <= 0x3163)) {
		return 2;
	}
	return 1;
}
