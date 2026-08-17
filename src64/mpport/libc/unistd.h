#ifndef MOWKOW64_MPPORT_UNISTD_H
#define MOWKOW64_MPPORT_UNISTD_H

#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef long ssize_t;

/*
 * 선언만 둔다. py/reader.c의 POSIX 파일 reader(MICROPY_READER_POSIX, 이
 * 포트에서는 0, Stage 1.3)와 persistentcode.c의 .mpy 저장 경로(rom level
 * MINIMUM에서는 닿지 않는다)를 받쳐 주는 이름들이다. 이 설정에서는 불리지
 * 않으며, 달라지면 그때 구현한다.
 */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
