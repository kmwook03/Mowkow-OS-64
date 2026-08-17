/*
 * string.c -- 앱용 최소 문자열/메모리 함수
 *
 * libc가 없으므로 앱이 쓰는 만큼만 직접 둔다.
 */
#include <mowos.h>

size_t strlen(const char *s)
{
	size_t n;

	n = 0;
	while (s[n] != '\0') {
		n++;
	}
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return (unsigned char) *a - (unsigned char) *b;
		}
		a++;
		b++;
	}
	return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
			return (unsigned char) a[i] - (unsigned char) b[i];
		}
	}
	return 0;
}

void *memcpy(void *dst, const void *src, size_t size)
{
	unsigned char *d;
	const unsigned char *s;

	d = (unsigned char *) dst;
	s = (const unsigned char *) src;
	while (size-- > 0) {
		*d++ = *s++;
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t size)
{
	const unsigned char *pa;
	const unsigned char *pb;
	size_t i;

	pa = (const unsigned char *) a;
	pb = (const unsigned char *) b;
	for (i = 0; i < size; i++) {
		if (pa[i] != pb[i]) {
			return pa[i] - pb[i];
		}
	}
	return 0;
}

void *memset(void *dst, int value, size_t size)
{
	unsigned char *d;

	d = (unsigned char *) dst;
	while (size-- > 0) {
		*d++ = (unsigned char) value;
	}
	return dst;
}
