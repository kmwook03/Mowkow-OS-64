#include "include/nano_posix64.h"

struct NANO_FORMAT64 {
	char *dst64;
	size_t capacity64;
	size_t used64;
	int failed64;
};

static void emit64(struct NANO_FORMAT64 *out, const char *text, size_t count, int pad)
{
	if (out->failed64 || count > (size_t) __INT_MAX__ - out->used64) {
		out->failed64 = 1;
		return;
	}
	if (out->capacity64 > 0 && out->used64 < out->capacity64 - 1) {
		size_t room = out->capacity64 - 1 - out->used64;
		size_t copy = count < room ? count : room;

		if (text != NULL) { memcpy(out->dst64 + out->used64, text, copy); }
		else { memset(out->dst64 + out->used64, pad, copy); }
	}
	out->used64 += count;
}

static int number64(const char **format)
{
	int result = 0;

	while (**format >= '0' && **format <= '9') {
		int digit = *(*format)++ - '0';

		if (result > (__INT_MAX__ - digit) / 10) { return -1; }
		result = result * 10 + digit;
	}
	return result;
}

int nano_vsnprintf64(char *dst, size_t capacity, const char *format, va_list args)
{
	struct NANO_FORMAT64 out = { dst, capacity, 0, 0 };

	if (format == NULL || (capacity != 0 && dst == NULL)) { errno = EINVAL; return -1; }
	while (*format != '\0' && !out.failed64) {
		if (*format != '%') { emit64(&out, format++, 1, 0); continue; }
		format++;
		int left = 0, zero = 0, alternate = 0, plus = 0, blank = 0;

		for (; *format != '\0'; format++) {
			if (*format == '-') { left = 1; }
			else if (*format == '0') { zero = 1; }
			else if (*format == '#') { alternate = 1; }
			else if (*format == '+') { plus = 1; }
			else if (*format == ' ') { blank = 1; }
			else { break; }
		}
		int dynamic_width = *format == '*';
		int width = dynamic_width ? (format++, va_arg(args, int)) : number64(&format);
		int precision = -1;

		if ((!dynamic_width && width < 0) || width == -__INT_MAX__ - 1) {
			out.failed64 = 1; break;
		}
		if (width < 0) { left = 1; width = -width; }
		if (*format == '.') {
			format++;
			int dynamic_precision = *format == '*';

			precision = dynamic_precision ? (format++, va_arg(args, int)) : number64(&format);
			if (!dynamic_precision && precision < 0) { out.failed64 = 1; break; }
			if (precision < 0) { precision = -1; }
		}
		int length = 0;

		if (*format == 'l') { length = 1; format++; if (*format == 'l') { length = 2; format++; } }
		else if (*format == 'z' || *format == 't') { length = 1; format++; }
		else if (*format == 'h') {
			length = -1; format++;
			if (*format == 'h') { length = -2; format++; }
		}
		char spec = *format;
		char digits[66], sign[3];
		const char *text = digits;
		size_t count = 0, signs = 0, zeros = 0;

		if (spec == '\0') { out.failed64 = 1; break; }
		format++;
		if (spec == 's') {
			text = va_arg(args, const char *);
			if (text == NULL) { text = "(null)"; }
			while ((precision < 0 || count < (size_t) precision) && text[count] != '\0') { count++; }
		} else if (spec == 'c' || spec == '%') {
			digits[0] = spec == '%' ? '%' : (char) va_arg(args, int);
			count = 1;
		} else if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' ||
			spec == 'X' || spec == 'o' || spec == 'p') {
			unsigned long long value;
			int base = spec == 'o' ? 8 : ((spec == 'x' || spec == 'X' || spec == 'p') ? 16 : 10);
			const char *alphabet = spec == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";

			if (spec == 'd' || spec == 'i') {
				long long signed_value = length == 2 ? va_arg(args, long long) :
					(length == 1 ? va_arg(args, long) : va_arg(args, int));

				if (length == -1) { signed_value = (short) signed_value; }
				if (length == -2) { signed_value = (signed char) signed_value; }
				value = (unsigned long long) signed_value;
				if (signed_value < 0) { sign[signs++] = '-'; value = 0ULL - value; }
				else if (plus || blank) { sign[signs++] = plus ? '+' : ' '; }
			} else if (spec == 'p') { value = (uintptr_t) va_arg(args, void *); }
			else { value = length == 2 ? va_arg(args, unsigned long long) :
				(length == 1 ? va_arg(args, unsigned long) : va_arg(args, unsigned int)); }
			if (length == -1) { value = (unsigned short) value; }
			if (length == -2) { value = (unsigned char) value; }
			if (spec == 'p' || (alternate && base == 16 && value != 0)) {
				sign[signs++] = '0'; sign[signs++] = spec == 'X' ? 'X' : 'x';
			}
			if (value != 0 || precision != 0) {
				do { digits[count++] = alphabet[value % (unsigned int) base];
					value /= (unsigned int) base; } while (value != 0);
			}
			for (size_t i = 0; i < count / 2; i++) {
				char swap = digits[i]; digits[i] = digits[count - 1 - i]; digits[count - 1 - i] = swap;
			}
			if (precision > 0 && (size_t) precision > count) { zeros = (size_t) precision - count; }
			if (alternate && base == 8 && (count == 0 || digits[0] != '0') && zeros == 0) { zeros = 1; }
			if (zero && !left && precision < 0 && (size_t) width > count + signs) {
				zeros = (size_t) width - count - signs;
			}
		} else { errno = EINVAL; if (capacity != 0) { dst[0] = '\0'; } return -1; }
		size_t padding = (size_t) width > count + signs + zeros ?
			(size_t) width - count - signs - zeros : 0;

		if (!left) { emit64(&out, NULL, padding, ' '); }
		emit64(&out, sign, signs, 0);
		emit64(&out, NULL, zeros, '0');
		emit64(&out, text, count, 0);
		if (left) { emit64(&out, NULL, padding, ' '); }
	}
	if (capacity != 0) { dst[out.used64 < capacity ? out.used64 : capacity - 1] = '\0'; }
	if (out.failed64) { errno = EOVERFLOW; return -1; }
	return (int) out.used64;
}

int nano_snprintf64(char *dst, size_t capacity, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = nano_vsnprintf64(dst, capacity, format, args);
	va_end(args);
	return count;
}

/* Exact libc compatibility ABI; only upstream nano supplies this capacity. */
int sprintf(char *dst, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = nano_vsnprintf64(dst, SIZE_MAX, format, args);
	va_end(args);
	return count;
}

int nano_vfprintf64(FILE *stream, const char *format, va_list args)
{
	char text[1024];
	int count = nano_vsnprintf64(text, sizeof(text), format, args);

	if (count < 0) { return -1; }
	if ((size_t) count >= sizeof(text)) { errno = EOVERFLOW; return -1; }
	return nano_fwrite64(text, 1, (size_t) count, stream) == (size_t) count ? count : -1;
}
int nano_fprintf64(FILE *stream, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = nano_vfprintf64(stream, format, args);
	va_end(args);
	return count;
}
int nano_printf64(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = nano_vfprintf64(nano_stdout64, format, args);
	va_end(args);
	return count;
}
int nano_fputc64(int byte, FILE *stream) { return nano_putc64(byte, stream); }
int nano_putchar64(int byte) { return nano_putc64(byte, nano_stdout64); }
