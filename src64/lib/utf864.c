#include <utf864.h>

int utf8_byte_len64(unsigned char byte)
{
	if ((byte & 0x80) == 0x00) {
		return 1;
	}
	if ((byte & 0xe0) == 0xc0) {
		return 2;
	}
	if ((byte & 0xf0) == 0xe0) {
		return 3;
	}
	if ((byte & 0xf8) == 0xf0) {
		return 4;
	}
	return 0;
}

unsigned int utf8_to_unicode64(const char *str, int *len)
{
	const unsigned char *s;
	int length;
	unsigned int unicode;

	s = (const unsigned char *) str;
	length = utf8_byte_len64(*s);
	unicode = 0;
	if (length == 1) {
		unicode = s[0];
		*len = 1;
	} else if (length == 2) {
		unicode = ((unsigned int) (s[0] & 0x1f) << 6) | (unsigned int) (s[1] & 0x3f);
		*len = 2;
	} else if (length == 3) {
		unicode = ((unsigned int) (s[0] & 0x0f) << 12) |
			((unsigned int) (s[1] & 0x3f) << 6) | (unsigned int) (s[2] & 0x3f);
		*len = 3;
	} else if (length == 4) {
		unicode = ((unsigned int) (s[0] & 0x07) << 18) |
			((unsigned int) (s[1] & 0x3f) << 12) |
			((unsigned int) (s[2] & 0x3f) << 6) | (unsigned int) (s[3] & 0x3f);
		*len = 4;
	} else {
		*len = 1;
	}
	return unicode;
}
