#include "mylib.h"
#include "apilib.h"
#include "apihan.h" 
#include <stdarg.h>



static struct HANGUL_STATE h_state;
static int lang_mode = 1; // 0:Eng, 1:Kor

void console_writer(const char *str, void *aux) {
    api_putstr((char *)str);
}

char *gets(char *buf) {
    int i = 0;
    int key;

    /* 오토마타 초기화 */
    apihan_init(&h_state, console_writer, 0);

    while (1) {
        key = api_getkey(1);

        /* [Enter] 입력 종료 */
        if (key == 0x0A) {
            api_putstr("\n");
            break;
        }

        /* [Backspace] */
        if (key == 0x7F) {
            apihan_backspace(&h_state, buf, &i);
            continue;
        }

        /* [Tab] 스페이스 4칸 */
        if (key == 0x09) {
            if (h_state.state > 0) {
                apihan_init(&h_state, console_writer, 0);
            }

            int k;
            for (k=0; k<4; k++) {
                if (i < 255) {
                    buf[i++] = ' ';
                    api_putstr(" ");
                }
            }
            continue;
        }
        
        if (key == 0xFF) {
            lang_mode ^= 1;
            // 전환 시 조합 중인 글자 처리가 애매하므로 리셋
            apihan_init(&h_state, console_writer, 0);
            continue;
        }

        /* 일반 입력 */
        if (lang_mode == 1) {
            apihan_run(&h_state, key, buf, &i);
        } else {
            // 영어 모드
            if (h_state.state > 0) apihan_init(&h_state, console_writer, 0); // 혹시 모를 잔여 상태 리셋
            
            buf[i++] = key;
            char s[2] = {key, 0};
            api_putstr(s);
        }
    }
    buf[i] = 0;
    return buf;
}

int isdigit(char c) { return (c >= '0' && c <= '9'); }
int putchar(int c) { char buf[2]={(char)c,0}; api_putstr(buf); return c; }
int puts(const char *s) { api_putstr((char *)s); api_putstr("\n"); return 0; }
void itoa(int value, char *str, int base) { 
    char *ptr = str; char *low; if (base < 2 || base > 36) { *str = '\0'; return; }
    char *rc = ptr; int sign = value; if (sign < 0 && base == 10) value = -value;
    do { *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[value % base]; value /= base; } while (value);
    if (sign < 0 && base == 10) *ptr++ = '-'; *ptr = '\0'; low = rc; char *high = ptr - 1;
    while (low < high) { char temp = *low; *low++ = *high; *high-- = temp; }
}
int printf(const char* format, ...) { 
    va_list ap; char buf[1024]; char temp[32]; char *p = buf; const char *f = format;
    va_start(ap, format);
    while (*f) {
        if (*f == '%') {
            f++;
            switch (*f) {
                case 'd': { int i = va_arg(ap, int); itoa(i, temp, 10); char *t = temp; while (*t) *p++ = *t++; break; }
                case 'x': { int i = va_arg(ap, int); itoa(i, temp, 16); char *t = temp; while (*t) *p++ = *t++; break; }
                case 's': { char *s = va_arg(ap, char*); if (!s) s = "(null)"; while (*s) *p++ = *s++; break; }
                case 'c': { int c = va_arg(ap, int); *p++ = (char)c; break; }
                default: { *p++ = '%'; *p++ = *f; break; }
            }
        } else { *p++ = *f; }
        f++; if (p - buf > 1000) break; 
    }
    *p = '\0'; api_putstr(buf); va_end(ap); return (p - buf);
}
void exit(int status) { api_end(); }
void *malloc(int size) { return (void *)api_malloc(size); }

static void _mini_itoa(char **buf, char *end, int val, int base) {
    char temp[32];
    int i = 0;
    int sign = 0;

    if (val == 0) {
        if (*buf < end) *(*buf)++ = '0';
        return;
    }

    if (val < 0 && base == 10) {
        sign = 1;
        val = -val;
    }

    while (val != 0) {
        int rem = val % base;
        temp[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        val = val / base;
    }

    if (sign) {
        temp[i++] = '-';
    }

    while (i > 0) {
        if (*buf < end) {
            *(*buf)++ = temp[--i];
        } else {
            break; 
        }
    }
}

/* vsnprintf 대체 함수: %d, %s, %c, %x 지원 */
int mini_vsnprintf(char *buf, int size, const char *fmt, va_list ap) {
    char *ptr = buf;
    char *end = buf + size - 1; // 널 문자를 위해 1바이트 남김

    while (*fmt && ptr < end) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': { // 문자열
                    char *s = va_arg(ap, char *);
                    if (!s) s = "(null)";
                    while (*s && ptr < end) {
                        *ptr++ = *s++;
                    }
                    break;
                }
                case 'd': { // 정수 (10진수)
                    int val = va_arg(ap, int);
                    _mini_itoa(&ptr, end, val, 10);
                    break;
                }
                case 'x': { // 정수 (16진수)
                    int val = va_arg(ap, int);
                    _mini_itoa(&ptr, end, val, 16);
                    break;
                }
                case 'c': { // 문자
                    char c = (char)va_arg(ap, int);
                    if (ptr < end) *ptr++ = c;
                    break;
                }
                case '%': { // 퍼센트 리터럴
                    if (ptr < end) *ptr++ = '%';
                    break;
                }
                default: // 지원하지 않는 포맷은 그대로 출력
                    if (ptr < end) *ptr++ = '%';
                    if (ptr < end) *ptr++ = *fmt;
                    break;
            }
        } else {
            *ptr++ = *fmt;
        }
        fmt++;
    }
    *ptr = '\0'; // 널 문자 종료
    return ptr - buf;
}

int my_strlen(char *str) {
    int len = 0;
    while (*str++) len++;
    return len;
}


int isxdigit(int c) {
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'a' && c <= 'f') return 1;
    if (c >= 'A' && c <= 'F') return 1;
    return 0;
}

int is_digits(char *s, int base) {
    if (*s == '\0') return 0;
    while (*s) {
        if (base == 8) {
            if (*s < '0' || *s > '7') return 0;
        } else if (base == 10) {
            if (!isdigit(*s)) return 0;
        } else if (base == 16) {
            if (!isxdigit(*s)) return 0;
        }
        s++;
    }
    return 1;
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
}

int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int my_tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

char *my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char *my_strncpy(char *dest, char *src, int n) {
    char *d = dest;
    int i;
    for (i = 0; i < n; i++) {
        if (src[i] == '\0') {
            break;
        }
        d[i] = src[i];
    }
    d[i] = '\0';
    return dest;
}

char *my_strstr(char *haystack, char *needle) {
    char *h, *n;

    if (*needle == 0) {
        return haystack;
    }

    for (; *haystack != 0; haystack++) {
        if (*haystack == *needle) {
            h = haystack;
            n = needle;
            while (*h != 0 && *n != 0 && *h == *n) {
                h++;
                n++;
            }

            if (*n == 0) {
                return haystack;
            }
        }
    }
    return 0;
}

int my_atoi(char *str) {
    int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

// long strtol(const char *nptr, char **endptr, int base) {
//     const char *s = nptr;
//     unsigned long acc;
//     int c;
//     unsigned long cutoff;
//     int neg = 0, any, cutlim;

//     /* 1. 공백 건너뛰기 */
//     do {
//         c = *s++;
//     } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

//     if (c == '-') {
//         neg = 1;
//         c = *s++;
//     } else if (c == '+') {
//         c = *s++;
//     }

//     /* 2. 진법(Base) 자동 탐지 */
//     if ((base == 0 || base == 16) &&
//         c == '0' && (*s == 'x' || *s == 'X')) {
//         c = s[1];
//         s += 2;
//         base = 16;
//     }
//     if (base == 0)
//         base = c == '0' ? 8 : 10;

//     /* 3. 오버플로우 체크용 기준값 설정 (unsigned long 기준) */
//     /* 간단한 OS용이므로 오버플로우 처리는 최소화하거나 생략해도 되지만, 안전을 위해 포함 */
//     cutoff = neg ? -(unsigned long)-2147483648 : 2147483647; 
//     cutlim = cutoff % (unsigned long)base;
//     cutoff /= (unsigned long)base;

//     /* 4. 변환 루프 */
//     for (acc = 0, any = 0;; c = *s++) {
//         if (c >= '0' && c <= '9')
//             c -= '0';
//         else if (c >= 'A' && c <= 'Z')
//             c -= 'A' - 10;
//         else if (c >= 'a' && c <= 'z')
//             c -= 'a' - 10;
//         else
//             break;

//         if (c >= base)
//             break;

//         if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
//             any = -1; // 오버플로우 발생
//         else {
//             any = 1;
//             acc *= base;
//             acc += c;
//         }
//     }

//     if (any < 0) {
//         acc = neg ? -2147483648 : 2147483647;
//     } else if (neg)
//         acc = -acc;

//     if (endptr != 0)
//         *endptr = (char *)(any ? s - 1 : nptr);

//     return (long)acc;
// }
