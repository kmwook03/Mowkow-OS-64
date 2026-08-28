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
 * 고정 폭과 포인터 폭 한계값 매크로. GCC가 스스로 정의하는 __*_MAX__를
 * 그대로 쓰므로 이 툴체인에서 값이 어긋날 일이 없다.
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
