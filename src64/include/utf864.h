#ifndef MOWKOW64_UTF864_H
#define MOWKOW64_UTF864_H

#include <stdint.h>

int utf8_byte_len64(unsigned char byte);
unsigned int utf8_to_unicode64(const char *str, int *len);
/* 코드포인트 하나를 부호화한다. 쓴 바이트 수(1..3)를 돌려주고, out이 모자라면 0. */
int unicode_to_utf864(unsigned int unicode, char *out, int out_size);
/* VFAT 긴 이름은 UTF-16LE다. 두 변환 모두 BMP까지만 다루고, 그 밖의 글자는
   대리 쌍 대신 '_'로 바꾼다. */
int utf8_to_utf16_64(const char *in, uint16_t *out, int max_units);
int utf16_to_utf8_64(const uint16_t *in, int units, char *out, int out_size);

#endif
