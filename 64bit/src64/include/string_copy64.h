#ifndef MOWKOW64_STRING_COPY64_H
#define MOWKOW64_STRING_COPY64_H

#include <stddef.h>

/* Copy at most capacity bytes from a NUL-terminated source. Return 0 on
 * success, -7 on truncation, -22 for invalid arguments. A nonempty destination
 * is always terminated, including on failure. No source scan beyond capacity.
 * Only exact self-copy is supported when the buffers overlap.
 */
static inline int str_copy64(char *dst, size_t capacity, const char *src)
{
	size_t i;
	char byte;

	if (dst == NULL || capacity == 0) {
		return -22;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return -22;
	}
	for (i = 0; i < capacity; i++) {
		byte = src[i];
		if (i == capacity - 1) {
			dst[i] = '\0';
			return byte == '\0' ? 0 : -7;
		}
		dst[i] = byte;
		if (byte == '\0') {
			return 0;
		}
	}
	return -7;
}

/* Append a complete string to a builder with a known used-byte count.
 * Initialize dst[0] and *used to zero. Invalid metadata or insufficient room
 * leaves both unchanged; no partial UTF-8/string suffix is appended.
 */
static inline int str_append64(char *dst, size_t capacity, size_t *used, const char *src)
{
	size_t length;
	size_t remaining;
	size_t i;

	if (dst == NULL || src == NULL || used == NULL || *used >= capacity) {
		return -22;
	}
	for (i = 0; i < *used; i++) {
		if (dst[i] == '\0') {
			return -22;
		}
	}
	if (dst[*used] != '\0') {
		return -22;
	}
	remaining = capacity - *used;
	for (length = 0; length < remaining && src[length] != '\0'; length++) {
	}
	if (length == remaining) {
		return -7;
	}
	for (i = 0; i < length; i++) {
		dst[*used + i] = src[i];
	}
	*used += length;
	dst[*used] = '\0';
	return 0;
}

#endif
