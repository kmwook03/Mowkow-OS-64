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
		/* BMP only: a surrogate pair would need two units and a decoder to
		   match, and no name in this OS needs one */
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
