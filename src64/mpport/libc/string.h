#ifndef MOWKOW64_MPPORT_STRING_H
#define MOWKOW64_MPPORT_STRING_H

/*
 * x86_64-elf-gcc ships no libc, so <string.h> doesn't exist. The
 * MicroPython core needs memcpy/memset/memmove/memcmp/strlen/strcmp/
 * strncmp/strchr; kstring64.h (built for the kernel itself, Stage 0.2)
 * already provides exactly these with matching signatures.
 */
#include <kstring64.h>

#endif
