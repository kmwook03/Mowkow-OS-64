#ifndef MOWKOW64_UTF864_H
#define MOWKOW64_UTF864_H

#include <stdint.h>

int utf8_byte_len64(unsigned char byte);
unsigned int utf8_to_unicode64(const char *str, int *len);
/* encodes one code point; returns bytes written (1..3), 0 if out is too small */
int unicode_to_utf864(unsigned int unicode, char *out, int out_size);
/* VFAT long names are UTF-16LE. Both conversions are BMP-only: anything
   outside the basic plane becomes '_' rather than a surrogate pair. */
int utf8_to_utf16_64(const char *in, uint16_t *out, int max_units);
int utf16_to_utf8_64(const uint16_t *in, int units, char *out, int out_size);

#endif
