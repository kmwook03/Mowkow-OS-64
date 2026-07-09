/* mylib.h */
#ifndef MYLIB_H
#define MYLIB_H

#include <stdarg.h>

/* Input Functions */
char *gets(char *buf);

/* Character & String Utilities */
int isdigit(char c);
void itoa(int value, char *str, int base);
int my_atoi(char *str);
int isspace(int c);
int isxdigit(int c);
int is_digits(char *s, int base);
int isalpha(int c);

/* Output Functions */
int putchar(int c);
int puts(const char *s);
int printf(const char* format, ...);

/* System & Memory Functions */
void exit(int status);
void *malloc(int size);

int mini_vsnprintf(char *buf, int size, const char *fmt, va_list ap);
int my_strlen(char *str);
int my_tolower(int c);
char *my_strcpy(char *dest, const char *src);
char *my_strncpy(char *dest, char *src, int n);
char *my_strstr(char *haystack, char *needle);

// long strtol(const char *nptr, char **endptr, int base);
#endif
