#ifndef MOWKOW64_NANO_UTF64_H
#define MOWKOW64_NANO_UTF64_H

#include <stddef.h>
#include <stdint.h>

/* Stateless UTF-8 conversion for the private nano libc shim. mbtowc never
 * reads beyond available; malformed/incomplete input returns -1/EILSEQ,
 * leaving the output unchanged. NUL returns zero, NULL text resets (zero).
 * Scalars use uint32_t independently of hosted wchar_t. The private libc
 * adapter must convert its own wchar_t explicitly. NULL wide is allowed for
 * validation. wctomb needs four writable bytes;
 * invalid scalars return -1/EILSEQ without writing. NULL text resets (zero).
 */
int nano_mbtowc64(uint32_t *wide, const char *text, size_t available);
int nano_wctomb64(char *text, uint32_t wide);

/* Match the current OS renderer: supported Hangul syllables and compatibility
 * jamo take two cells; other printable scalars take one, including combining
 * marks/fallback glyphs. NUL is zero; controls/invalid scalars return -1.
 * This is a target display policy, not full Unicode wcwidth/locale support.
 */
int nano_wcwidth64(uint32_t wide);

#endif
