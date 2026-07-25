#ifndef MOWKOW64_MPPORT_STDIO_H
#define MOWKOW64_MPPORT_STDIO_H

#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/*
 * Declarations only, for a handful of debug-only / disabled-feature call
 * sites in the MicroPython core (DEBUG_PRINTF, MICROPY_DEBUG_PRINTERS
 * dumps, persistentcode.c's mmap save path). None of these are reachable
 * in this port's config (rom level MINIMUM, debug printers off), so no
 * definitions are provided -- if that ever changes, the linker will say
 * so and it gets implemented then.
 */
typedef struct FILE FILE;

extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
FILE *fopen(const char *path, const char *mode);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fclose(FILE *stream);

#endif
