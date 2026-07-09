/* parse.h */
#ifndef PARSE_H
#define PARSE_H

#include "data.h"

/* 버퍼 및 토큰 크기 정의 */
#define BUFFER_SIZE 10240
#define TOKEN_SIZE  256

/* Reader 구조체 정의 */
typedef struct {
    char buf[BUFFER_SIZE];    // 입력 버퍼
    char token[TOKEN_SIZE];   // 현재 토큰
    int pos;                  // 버퍼 내 현재 위치
    int depth;                // 괄호 깊이 (REPL 들여쓰기용)
} Reader;

extern Reader YY_reader;

/* 함수 프로토타입 */
void init_reader(void);
void reader_fill_buffer(void);
char* next_token(void);
char* get_current_token(void);
Value* read_expr(void);

#endif /* PARSE_H */
