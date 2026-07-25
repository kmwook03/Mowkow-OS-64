#ifndef MOWKOW64_MPPORT_UNISTD_H
#define MOWKOW64_MPPORT_UNISTD_H

#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef long ssize_t;

/*
 * Declarations only -- these back py/reader.c's POSIX file reader
 * (MICROPY_READER_POSIX, set to 0 for this port, Stage 1.3) and
 * persistentcode.c's .mpy save path (unreachable at rom level MINIMUM).
 * Not reachable in this config; implemented if that changes.
 */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
