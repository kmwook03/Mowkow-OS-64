/*
 * utf864.c -- UTF-8 <-> 유니코드 <-> UTF-16 변환
 *
 * 콘솔은 UTF-8을, VFAT 긴 이름은 UTF-16을 쓴다. 둘을 잇는 자리가 여기다.
 * 다루는 범위는 BMP(U+FFFF 이하)까지다.
 */
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

int unicode_to_utf864(unsigned int unicode, char *out, int out_size)
{
	if (unicode < 0x80) {
		if (out_size < 1) {
			return 0;
		}
		out[0] = (char) unicode;
		return 1;
	}
	if (unicode < 0x800) {
		if (out_size < 2) {
			return 0;
		}
		out[0] = (char) (0xc0 | (unicode >> 6));
		out[1] = (char) (0x80 | (unicode & 0x3f));
		return 2;
	}
	if (out_size < 3) {
		return 0;
	}
	out[0] = (char) (0xe0 | (unicode >> 12));
	out[1] = (char) (0x80 | ((unicode >> 6) & 0x3f));
	out[2] = (char) (0x80 | (unicode & 0x3f));
	return 3;
}

int utf8_to_utf16_64(const char *in, uint16_t *out, int max_units)
{
	unsigned int unicode;
	int units;
	int len;

	units = 0;
	while (*in != '\0') {
		unicode = utf8_to_unicode64(in, &len);
		in += len;
		/* BMP까지만 다룬다. 대리 쌍(surrogate pair)을 받으려면 단위 두 개와
		   그에 맞는 해독기가 필요한데, 이 OS의 파일 이름에는 그런 글자가
		   쓰이지 않는다. */
		if (unicode > 0xffff) {
			unicode = '_';
		}
		if (units >= max_units) {
			return -1;
		}
		out[units++] = (uint16_t) unicode;
	}
	return units;
}

int utf16_to_utf8_64(const uint16_t *in, int units, char *out, int out_size)
{
	unsigned int unicode;
	int i;
	int n;
	int len;

	n = 0;
	for (i = 0; i < units; i++) {
		unicode = in[i];
		if (unicode >= 0xd800 && unicode <= 0xdfff) {
			unicode = '_';
		}
		len = unicode_to_utf864(unicode, out + n, out_size - n - 1);
		if (len == 0) {
			return -1;
		}
		n += len;
	}
	out[n] = '\0';
	return n;
}
