#ifndef _UTF8_H_
#define _UTF8_H_

int utf8_byte_len(unsigned char byte);
unsigned int utf8_to_unicode(const char *str, int *len);

#endif // _UTF8_H_
