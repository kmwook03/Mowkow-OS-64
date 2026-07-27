#include <kstring64.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *) dst;
	const uint8_t *s = (const uint8_t *) src;

	while (n-- > 0) {
		*d++ = *s++;
	}
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = (uint8_t *) dst;

	while (n-- > 0) {
		*d++ = (uint8_t) c;
	}
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *) dst;
	const uint8_t *s = (const uint8_t *) src;

	if (d == s || n == 0) {
		return dst;
	}
	if (d < s) {
		while (n-- > 0) {
			*d++ = *s++;
		}
	} else {
		d += n;
		s += n;
		while (n-- > 0) {
			*--d = *--s;
		}
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *pa = (const uint8_t *) a;
	const uint8_t *pb = (const uint8_t *) b;

	while (n-- > 0) {
		if (*pa != *pb) {
			return (int) *pa - (int) *pb;
		}
		pa++;
		pb++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n > 0 && *a != '\0' && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (n == 0) {
		return 0;
	}
	return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

char *strchr(const char *s, int c)
{
	while (*s != '\0') {
		if (*s == (char) c) {
			return (char *) s;
		}
		s++;
	}
	if ((char) c == '\0') {
		return (char *) s;
	}
	return NULL;
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++) != '\0') {
	}
	return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
	for (; i < n; i++) {
		dst[i] = '\0';
	}
	return dst;
}
