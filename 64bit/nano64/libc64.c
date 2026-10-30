/* Private vendor-facing libc. Ordinary strings use bounded builders; the
 * three unbounded ABI entry points below exist only for upstream nano calls.
 */
#include <mowos.h>
#include <stdint.h>

static void *crt_malloc64(size_t size) { return malloc(size); }
static void crt_free64(void *ptr) { free(ptr); }
#include "include/nano_posix64.h"

struct NANO_ALLOC64 {
	size_t size64;
	struct NANO_ALLOC64 *next64;
	uintptr_t seal64;
	uintptr_t padding64;
};

_Static_assert(sizeof(struct NANO_ALLOC64) % 16 == 0, "preserve CRT payload alignment");

static struct NANO_ALLOC64 *allocations64;
static int allocation_fault64;

static uintptr_t seal64(const struct NANO_ALLOC64 *item)
{
	return (uintptr_t) item ^ item->size64 ^ (uintptr_t) item->next64 ^ 0x4e616e6f3634ULL;
}

static int valid_allocations64(void)
{
	size_t count = 0;

	if (allocation_fault64) { return 0; }
	for (struct NANO_ALLOC64 *item = allocations64; item != NULL; item = item->next64) {
		if (item->seal64 != seal64(item) || ++count > 65536) {
			allocation_fault64 = 1;
			return 0;
		}
	}
	return 1;
}

void *nano_malloc64(size_t size)
{
	struct NANO_ALLOC64 *item;

	if (size == 0) { size = 1; }
	if (size > SIZE_MAX - sizeof(*item) || !valid_allocations64()) {
		errno = ENOMEM;
		return NULL;
	}
	item = crt_malloc64(size + sizeof(*item));
	if (item == NULL) { errno = ENOMEM; return NULL; }
	*item = (struct NANO_ALLOC64) { size, allocations64, 0, 0 };
	item->seal64 = seal64(item);
	allocations64 = item;
	return item + 1;
}

void nano_free64(void *ptr)
{
	struct NANO_ALLOC64 *previous = NULL;

	if (ptr == NULL || !valid_allocations64()) { return; }
	for (struct NANO_ALLOC64 *item = allocations64; item != NULL; item = item->next64) {
		if (item + 1 == ptr) {
			if (previous == NULL) { allocations64 = item->next64; }
			else {
				previous->next64 = item->next64;
				previous->seal64 = seal64(previous);
			}
			crt_free64(item);
			return;
		}
		previous = item;
	}
}

void *nano_realloc64(void *ptr, size_t size)
{
	if (ptr == NULL) { return nano_malloc64(size); }
	if (!valid_allocations64()) { errno = ENOMEM; return NULL; }
	for (struct NANO_ALLOC64 *item = allocations64; item != NULL; item = item->next64) {
		if (item + 1 == ptr) {
			if (size == 0) { nano_free64(ptr); return NULL; }
			void *replacement = nano_malloc64(size);

			if (replacement == NULL) { return NULL; }
			memcpy(replacement, ptr, size < item->size64 ? size : item->size64);
			nano_free64(ptr);
			return replacement;
		}
	}
	errno = EINVAL;
	return NULL;
}

void *nano_memmove64(void *dst, const void *src, size_t size)
{
	unsigned char *out = dst;
	const unsigned char *in = src;

	if ((uintptr_t) out < (uintptr_t) in) {
		for (size_t i = 0; i < size; i++) { out[i] = in[i]; }
	} else {
		while (size != 0) { size--; out[size] = in[size]; }
	}
	return dst;
}

char *strcpy(char *dst, const char *src)
{
	char *start = dst;

	do { *dst = *src++; } while (*dst++ != '\0');
	return start;
}

char *strcat(char *dst, const char *src)
{
	char *end = dst + strlen(dst);

	do { *end = *src++; } while (*end++ != '\0');
	return dst;
}

char *nano_strncpy64(char *dst, const char *src, size_t size)
{
	size_t i = 0;

	while (i < size && src[i] != '\0') { dst[i] = src[i]; i++; }
	while (i < size) { dst[i++] = '\0'; }
	return dst;
}

char *nano_strchr64(const char *text, int byte)
{
	do {
		if ((unsigned char) *text == (unsigned char) byte) { return (char *) text; }
	} while (*text++ != '\0');
	return NULL;
}

char *nano_strrchr64(const char *text, int byte)
{
	const char *last = NULL;

	do {
		if ((unsigned char) *text == (unsigned char) byte) { last = text; }
	} while (*text++ != '\0');
	return (char *) last;
}

char *nano_strstr64(const char *text, const char *needle)
{
	size_t size = strlen(needle);

	do {
		if (strncmp(text, needle, size) == 0) { return (char *) text; }
	} while (*text++ != '\0');
	return NULL;
}

char *nano_strpbrk64(const char *text, const char *accept)
{
	for (; *text != '\0'; text++) {
		if (nano_strchr64(accept, *text) != NULL) { return (char *) text; }
	}
	return NULL;
}

int nano_tolower64(int byte) { return byte >= 'A' && byte <= 'Z' ? byte + 32 : byte; }
int nano_toupper64(int byte) { return byte >= 'a' && byte <= 'z' ? byte - 32 : byte; }
int nano_isalpha64(int byte) { return (byte >= 'A' && byte <= 'Z') ||
	(byte >= 'a' && byte <= 'z'); }
int nano_isdigit64(int byte) { return byte >= '0' && byte <= '9'; }
int nano_isalnum64(int byte) { return nano_isalpha64(byte) || nano_isdigit64(byte); }
int nano_isblank64(int byte) { return byte == ' ' || byte == '\t'; }
int nano_isspace64(int byte) { return byte == ' ' || (byte >= '\t' && byte <= '\r'); }
int nano_iscntrl64(int byte) { return (byte >= 0 && byte < 32) || byte == 127; }
int nano_ispunct64(int byte) { return byte >= 33 && byte <= 126 && !nano_isalnum64(byte); }
int nano_isxdigit64(int byte) { return nano_isdigit64(byte) ||
	(nano_tolower64(byte) >= 'a' && nano_tolower64(byte) <= 'f'); }

/* Deliberately limited to ASCII and Korean word classification. No claim of
 * a full Unicode locale; uncased scalars are preserved, never byte-folded.
 */
int nano_iswalnum64(wint_t scalar)
{
	return (scalar < 128 && nano_isalnum64((int) scalar)) ||
		(scalar >= 0x1100 && scalar <= 0x11ff) ||
		(scalar >= 0x3131 && scalar <= 0x318e) ||
		(scalar >= 0xac00 && scalar <= 0xd7a3);
}
int nano_iswblank64(wint_t scalar) { return scalar == ' ' || scalar == '\t'; }
int nano_iswpunct64(wint_t scalar) { return scalar < 128 && nano_ispunct64((int) scalar); }
wint_t nano_towlower64(wint_t scalar)
{
	return scalar < 128 ? (wint_t) nano_tolower64((int) scalar) : scalar;
}
int nano_libc_wcwidth64(wchar_t scalar) { return nano_wcwidth64((uint32_t) scalar); }
int nano_libc_wctomb64(char *text, wchar_t scalar)
{
	return nano_wctomb64(text, (uint32_t) scalar);
}

int nano_strncasecmp64(const char *left, const char *right, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		int a = nano_tolower64((unsigned char) left[i]);
		int b = nano_tolower64((unsigned char) right[i]);

		if (a != b || a == 0) { return a - b; }
	}
	return 0;
}
int nano_strcasecmp64(const char *left, const char *right)
{
	return nano_strncasecmp64(left, right, SIZE_MAX);
}
char *nano_strcasestr64(const char *text, const char *needle)
{
	size_t count = strlen(needle);

	do {
		if (nano_strncasecmp64(text, needle, count) == 0) { return (char *) text; }
	} while (*text++ != '\0');
	return NULL;
}

long nano_strtol64(const char *text, char **end, int base)
{
	const char *start = text;
	unsigned long value = 0, limit;
	int negative, overflow = 0, digits = 0;

	if (base != 0 && (base < 2 || base > 36)) {
		errno = EINVAL;
		if (end != NULL) { *end = (char *) text; }
		return 0;
	}
	while (nano_isspace64((unsigned char) *text)) { text++; }
	negative = *text == '-';
	if (*text == '-' || *text == '+') { text++; }
	if ((base == 0 || base == 16) && text[0] == '0' &&
		(text[1] == 'x' || text[1] == 'X') && nano_isxdigit64((unsigned char) text[2])) {
		base = 16;
		text += 2;
	}
	if (base == 0) { base = *text == '0' ? 8 : 10; }
	limit = (unsigned long) __LONG_MAX__ + (unsigned long) negative;
	while (*text != '\0') {
		int digit;

		if (nano_isdigit64((unsigned char) *text)) { digit = *text - '0'; }
		else if (nano_isalpha64((unsigned char) *text)) {
			digit = nano_tolower64((unsigned char) *text) - 'a' + 10;
		} else { break; }

		if (digit < 0 || digit >= base) { break; }
		digits = 1;
		if (value > (limit - (unsigned int) digit) / (unsigned int) base) { overflow = 1; }
		else if (!overflow) { value = value * (unsigned int) base + (unsigned int) digit; }
		text++;
	}
	if (end != NULL) { *end = (char *) (digits ? text : start); }
	if (overflow) { errno = ERANGE; return negative ? -__LONG_MAX__ - 1L : __LONG_MAX__; }
	if (negative) { return value == limit ? -__LONG_MAX__ - 1L : -(long) value; }
	return (long) value;
}

int nano_atoi64(const char *text)
{
	long value = nano_strtol64(text, NULL, 10);

	if (value > __INT_MAX__) { errno = ERANGE; return __INT_MAX__; }
	if (value < -__INT_MAX__ - 1L) { errno = ERANGE; return -__INT_MAX__ - 1; }
	return (int) value;
}
