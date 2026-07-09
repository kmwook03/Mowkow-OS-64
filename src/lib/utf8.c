/**
 * @file utf8.c
 * 
 * @brief UTF-8 인코딩 관련 함수 구현
 * 
 */

#include "../include/utf8.h"

/**
 * @brief UTF-8 바이트 길이 반환 함수
 * 
 * @return UTF-8 문자 바이트 길이 정수
 */
int utf8_byte_len(unsigned char byte)
{
    if ((byte & 0x80) == 0x00) return 1;    // 0xxxxxxx: ASCII (1바이트)
    if ((byte & 0xE0) == 0xC0) return 2;    // 110xxxxx: 2바이트
    if ((byte & 0xF0) == 0xE0) return 3;    // 1110xxxx: 한글 (3바이트)
    if ((byte & 0xF8) == 0xF0) return 4;    // 11110xxx: 4바이트
    return 0; // 잘못된 UTF-8 바이트
}

unsigned int utf8_to_unicode(const char *str, int *len)
{
    unsigned char *s = (unsigned char *) str;
    int length = utf8_byte_len(*s);
    unsigned int unicode = 0;

    switch (length) {
        case 1:
            unicode = s[0];
            *len = 1;
            break;
        case 2:
            unicode = ((s[0] & 0x1F) << 6 | (s[1] & 0x3F));
            *len = 2;
            break;
        case 3:
            unicode = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            *len = 3;
            break;
        case 4:
            unicode = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            *len = 4;
            break;
        default:
            unicode = 0; // 잘못된 UTF-8 바이트
            *len = 1;
            break;
    }

    return unicode;
}
