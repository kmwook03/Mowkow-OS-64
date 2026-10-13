#ifndef MOWKOW64_MPPORT_STDIO_H
#define MOWKOW64_MPPORT_STDIO_H

#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/*
 * 선언만 둔다. MicroPython 코어에서 디버그 전용이거나 꺼 놓은 기능의 호출
 * 자리(DEBUG_PRINTF, MICROPY_DEBUG_PRINTERS 덤프, persistentcode.c의 mmap
 * 저장 경로) 때문에 필요하다. 현재 CORE_FEATURES 설정에서도 디버그 출력과
 * POSIX/VFS 파일 경로는 꺼져 있으므로 정의는 두지 않는다. 달라지면 링커가
 * 알려 줄 것이고, 그때 구현하면 된다.
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
