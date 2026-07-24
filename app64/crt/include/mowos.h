#ifndef MOWKOW_APP64_MOWOS_H
#define MOWKOW_APP64_MOWOS_H

typedef unsigned long size_t;

long write(int fd, const void *buf, size_t len);
long read(int fd, void *buf, size_t len);
int open(const char *path, int flags);
int close(int fd);
long seek(int fd, long offset, int whence);
void exit(int status) __attribute__((noreturn));
void *alloc(size_t size);
void free_alloc(void *ptr, size_t size);
void *malloc(size_t size);
void free(void *ptr);
unsigned long ticks(void);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
int puts(const char *s);

#endif
