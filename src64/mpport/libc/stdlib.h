#ifndef MOWKOW64_MPPORT_STDLIB_H
#define MOWKOW64_MPPORT_STDLIB_H

#include <stddef.h>

/*
 * Declarations only -- with MICROPY_ENABLE_GC=1, py/malloc.c #defines
 * malloc/realloc to gc_alloc/gc_realloc before these are ever called, so
 * the real libc versions are dead code in this config. No definitions
 * provided; if a future config path actually calls one, the linker will
 * say so and it gets implemented then.
 */
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif
