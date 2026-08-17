#ifndef MOWKOW64_MPPORT_STRING_H
#define MOWKOW64_MPPORT_STRING_H

/*
 * x86_64-elf-gcc에는 libc가 없어서 <string.h>도 없다. MicroPython 코어는
 * memcpy/memset/memmove/memcmp/strlen/strcmp/strncmp/strchr를 쓰는데,
 * 커널용으로 만든 kstring64.h(Stage 0.2)가 같은 서명으로 이미 다 갖고 있다.
 */
#include <kstring64.h>

#endif
