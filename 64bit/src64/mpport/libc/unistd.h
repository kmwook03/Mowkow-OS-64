#ifndef MOWKOW64_MPPORT_UNISTD_H
#define MOWKOW64_MPPORT_UNISTD_H

#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef long ssize_t;

/*
 * 선언만 둔다. py/reader.c의 POSIX 파일 reader(MICROPY_READER_POSIX, 이
 * 포트에서는 0)와 persistentcode.c의 POSIX/VFS .mpy 저장 경로를 받쳐 주는
 * 이름들이다. 현재 CORE_FEATURES 설정에서도 그 파일 경로는 꺼져 있어 불리지
 * 않으며, 달라지면 그때 구현한다.
 */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
