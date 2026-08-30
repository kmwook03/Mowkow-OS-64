#ifndef MOWKOW64_MPPORT_STDLIB_H
#define MOWKOW64_MPPORT_STDLIB_H

#include <stddef.h>

/*
 * 선언만 둔다. MICROPY_ENABLE_GC=1이면 py/malloc.c가 이 함수들이 불리기
 * 전에 malloc/realloc을 gc_alloc/gc_realloc으로 바꿔 버리므로, 진짜 libc
 * 판은 이 설정에서 죽은 코드다. 정의는 두지 않는다. 나중에 정말로 부르는
 * 경로가 생기면 링커가 알려 줄 것이고, 그때 구현하면 된다.
 */
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif
