#ifndef MOWKOW64_NANO_LIBC_LIMITS_H
#define MOWKOW64_NANO_LIBC_LIMITS_H
#define CHAR_BIT __CHAR_BIT__
#define INT_MAX __INT_MAX__
#define INT_MIN (-INT_MAX - 1)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#define PATH_MAX 256
#endif
