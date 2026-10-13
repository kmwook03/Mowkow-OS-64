#ifndef MOWKOW64_UTF864_H
#define MOWKOW64_UTF864_H

#include <stddef.h>
#include <stdint.h>

#define UTF64_REPLACEMENT 0xfffdU

/* Decode one scalar from exactly the available bytes; NUL is a valid scalar.
 * Return 0, -84 (malformed/empty input), or -22 (NULL pointer arguments).
 * Malformed nonempty input produces U+FFFD and consumes one byte, so display
 * callers make progress without swallowing a following valid character.
 * Empty input consumes zero bytes. NULL arguments leave outputs unchanged.
 * Caller supplies readable input and writable outputs; they must not overlap.
 */
int utf8_decode64(const char *str, size_t available, uint32_t *unicode,
	size_t *consumed);

/* Lead-byte width only; this does not validate a complete sequence. */
int utf8_byte_len64(unsigned char byte);
/* Compatibility wrapper for NUL-terminated strings only. For byte buffers use
 * utf8_decode64(). Malformed input returns U+FFFD and len=1.
 */
unsigned int utf8_to_unicode64(const char *str, int *len);
/* Encode one scalar: 1..4 bytes, or 0 on invalid scalar/NULL/short output.
 * Failure does not modify output; no NUL terminator is appended.
 */
int unicode_to_utf864(unsigned int unicode, char *out, int out_size);
/* Preserve all scalars using surrogate pairs; reject malformed UTF and invalid
 * pointers/counts/capacities. Legacy result: count on success, -1 on failure.
 * Input/output must be valid, nonoverlapping objects. No lossy substitution.
 * UTF-8 input is NUL-terminated. UTF-16 output has no terminator; max_units is
 * its capacity (a pair requires two units). Failure may leave a valid prefix
 * that the caller must discard.
 * UTF-16 input has exactly units readable elements; out_size includes NUL.
 * UTF-8 output is NUL-terminated, and empty on failure if out_size is positive
 * and out is non-NULL. Embedded NUL in UTF-16 is preserved in the byte count.
 */
int utf8_to_utf16_64(const char *in, uint16_t *out, int max_units);
int utf16_to_utf8_64(const uint16_t *in, int units, char *out, int out_size);

#endif
