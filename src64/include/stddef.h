#ifndef MOWKOW64_STDDEF_H
#define MOWKOW64_STDDEF_H

typedef unsigned long size_t;

#define NULL ((void *) 0)
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
