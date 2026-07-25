#ifndef MOWKOW64_STDINT_H
#define MOWKOW64_STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

/*
 * Fixed-width and pointer-width limit macros, via GCC's own builtin
 * __*_MAX__ macros (always available, no libc needed). Required by
 * third_party/micropython/py/mpconfig.h's MP_INT_MAX/SIZE_MAX checks.
 */
#define INT8_MAX   __INT8_MAX__
#define INT16_MAX  __INT16_MAX__
#define INT32_MAX  __INT32_MAX__
#define INT64_MAX  __INT64_MAX__
#define UINT8_MAX  __UINT8_MAX__
#define UINT16_MAX __UINT16_MAX__
#define UINT32_MAX __UINT32_MAX__
#define UINT64_MAX __UINT64_MAX__

#define INT8_MIN  (-INT8_MAX - 1)
#define INT16_MIN (-INT16_MAX - 1)
#define INT32_MIN (-INT32_MAX - 1)
#define INT64_MIN (-INT64_MAX - 1)

#define INTPTR_MAX  __INTPTR_MAX__
#define UINTPTR_MAX __UINTPTR_MAX__
#define INTPTR_MIN  (-INTPTR_MAX - 1)

#define SIZE_MAX __SIZE_MAX__

#endif
