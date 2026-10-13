/*
 * utf864.c -- UTF-8 <-> 유니코드 <-> UTF-16 변환
 *
 * 콘솔은 UTF-8을, VFAT 긴 이름은 UTF-16을 쓴다. 둘을 잇는 자리가 여기다.
 * UTF-8과 UTF-16은 모든 Unicode scalar를 보존하고 malformed 입력을 거부한다.
 */
#include <utf864.h>

int utf8_byte_len64(unsigned char byte)
{
	if ((byte & 0x80) == 0x00) {
		return 1;
	}
	if (byte >= 0xc2 && byte <= 0xdf) {
		return 2;
	}
	if ((byte & 0xf0) == 0xe0) {
		return 3;
	}
	if (byte >= 0xf0 && byte <= 0xf4) {
		return 4;
	}
	return 0;
}

int utf8_decode64(const char *str, size_t available, uint32_t *unicode,
	size_t *consumed)
{
	const unsigned char *s = (const unsigned char *) str;
	uint32_t scalar;
	uint32_t minimum;
	size_t width;
	size_t i;

	if (str == NULL || unicode == NULL || consumed == NULL) {
		return -22;
	}
	*unicode = UTF64_REPLACEMENT;
	*consumed = available != 0 ? 1 : 0;
	if (available == 0) {
		return -84;
	}
	width = (size_t) utf8_byte_len64(s[0]);
	if (width == 0 || width > available) {
		return -84;
	}
	if (width == 1) {
		scalar = s[0];
	} else {
		minimum = width == 2 ? 0x80U : (width == 3 ? 0x800U : 0x10000U);
		scalar = s[0] & (0x7fU >> width);
		for (i = 1; i < width; i++) {
			if ((s[i] & 0xc0) != 0x80) {
				return -84;
			}
			scalar = (scalar << 6) | (s[i] & 0x3f);
		}
		if (scalar < minimum || (scalar >= 0xd800 && scalar <= 0xdfff) ||
			scalar > 0x10ffff) {
			return -84;
		}
	}
	*unicode = scalar;
	*consumed = width;
	return 0;
}

/* NUL-terminated compatibility paths must stop before reading past NUL. */
static int utf8_decode_string64(const char *str, uint32_t *unicode,
	size_t *consumed)
{
	size_t available = 0;
	int width;

	if (str == NULL) {
		return -22;
	}
	width = utf8_byte_len64((unsigned char) str[0]);
	/* Stop at NUL before inspecting any byte past the string object. */
	while (available < (size_t) (width != 0 ? width : 1)) {
		if (str[available++] == '\0') {
			break;
		}
	}
	return utf8_decode64(str, available, unicode, consumed);
}

unsigned int utf8_to_unicode64(const char *str, int *len)
{
	uint32_t unicode = UTF64_REPLACEMENT;
	size_t consumed = 0;

	if (len == NULL) {
		return unicode;
	}
	utf8_decode_string64(str, &unicode, &consumed);
	*len = (int) consumed;
	return unicode;
}

int unicode_to_utf864(unsigned int unicode, char *out, int out_size)
{
	if (out == NULL || (unicode >= 0xd800 && unicode <= 0xdfff) ||
		unicode > 0x10ffff) {
		return 0;
	}
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
	if (unicode < 0x10000) {
		if (out_size < 3) {
			return 0;
		}
		out[0] = (char) (0xe0 | (unicode >> 12));
		out[1] = (char) (0x80 | ((unicode >> 6) & 0x3f));
		out[2] = (char) (0x80 | (unicode & 0x3f));
		return 3;
	}
	if (out_size < 4) {
		return 0;
	}
	out[0] = (char) (0xf0 | (unicode >> 18));
	out[1] = (char) (0x80 | ((unicode >> 12) & 0x3f));
	out[2] = (char) (0x80 | ((unicode >> 6) & 0x3f));
	out[3] = (char) (0x80 | (unicode & 0x3f));
	return 4;
}

int utf8_to_utf16_64(const char *in, uint16_t *out, int max_units)
{
	uint32_t unicode;
	size_t consumed;
	int units = 0;
	int need;

	if (in == NULL || out == NULL || max_units < 0) {
		return -1;
	}
	while (*in != '\0') {
		if (utf8_decode_string64(in, &unicode, &consumed) != 0) {
			return -1;
		}
		need = unicode > 0xffff ? 2 : 1;
		/* Reserve both surrogate units before writing either one. */
		if (need > max_units - units) {
			return -1;
		}
		if (need == 2) {
			unicode -= 0x10000;
			out[units++] = (uint16_t) (0xd800 | (unicode >> 10));
			out[units++] = (uint16_t) (0xdc00 | (unicode & 0x3ff));
		} else {
			out[units++] = (uint16_t) unicode;
		}
		in += consumed;
	}
	return units;
}

int utf16_to_utf8_64(const uint16_t *in, int units, char *out, int out_size)
{
	unsigned int unicode;
	int i;
	int n;
	int len;

	if (out == NULL || out_size <= 0) {
		return -1;
	}
	out[0] = '\0';
	if (in == NULL || units < 0) {
		return -1;
	}
	n = 0;
	for (i = 0; i < units; i++) {
		unicode = in[i];
		if (unicode >= 0xd800 && unicode <= 0xdbff) {
			if (units - i < 2 || in[i + 1] < 0xdc00 || in[i + 1] > 0xdfff) {
				out[0] = '\0';
				return -1;
			}
			unicode = 0x10000 + ((unicode - 0xd800) << 10) + (in[++i] - 0xdc00);
		} else if (unicode >= 0xdc00 && unicode <= 0xdfff) {
			out[0] = '\0';
			return -1;
		}
		len = unicode_to_utf864(unicode, out + n, out_size - n - 1);
		if (len == 0) {
			out[0] = '\0';
			return -1;
		}
		n += len;
	}
	out[n] = '\0';
	return n;
}
